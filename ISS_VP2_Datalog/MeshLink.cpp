// ============================================================================
// Fichier   : MeshLink.cpp
// Référence : Sequence de connexion et format de trame :
//             https://meshtastic.org/docs/development/device/client-api/
//             ("Streaming version of the API" / "Protobuf API via Serial")
//             - START1 = 0x94, START2 = 0xC3, puis longueur (2 octets, MSB
//             en premier), puis le message protobuf (ToRadio ou FromRadio).
//             - "Any time either side sees a byte that isn't start1/start2
//             ... it is treated as plain text debug output" : d'ou la
//             necessite d'envoyer un ToRadio.want_config_id en premier pour
//             faire cesser ce mode texte avant de parler protobuf.
// ============================================================================
#include "MeshLink.h"
#include "Config.h"

#if USE_MESHTASTIC

#include "BoardConfig.h"
#include "HalPins.h"
#include "Power.h"
#include "Params.h"
#include "SharedUart.h"
#include "EventLog.h"
#include "ProtobufWriter.h"
#include "MeshtasticTelemetry.h"

// START1/START2 : voir reference en tete de fichier.
static const uint8_t MESHTASTIC_FRAME_START1 = 0x94;
static const uint8_t MESHTASTIC_FRAME_START2 = 0xC3;

// ============================================================================
// Machine a etats de session Meshtastic.
//
// La session est entierement non bloquante apres le retour de
// meshLinkSendEnvironmentTelemetry(). Les etats sont :
//
//   MESH_STATE_POWER_ON_SETTLE
//       alimentation du T114 + attente de demarrage
//
//   MESH_STATE_WAIT_WAKE_QUEUE
//       un heartbeat a ete envoye ; un queue_status est attendu.
//       free == 0 est le seul cas qui interdit l'envoi.
//       Sans reponse apres MESH_WAKE_QUEUE_TIMEOUT_MS, on envoie quand meme.
//
//   MESH_STATE_WAIT_TX_STATUS
//       la telemetrie est envoyee. L'entree de notre paquet est confirmee
//       par une diminution de free par rapport a la valeur observee juste
//       avant l'envoi. Un heartbeat est emis toutes les
//       MESH_HEARTBEAT_INTERVAL_MS. Le timeout global est MESH_TX_TIMEOUT_MS.
//
//   MESH_STATE_SHUTDOWN_DELAY
//       la queue a d'abord diminue apres notre envoi, puis est redevenue
//       vide (free == maxlen). La coupure est alors programmee
//       MESH_QUEUE_EMPTY_SHUTDOWN_MS plus tard, sans autre condition.
// ============================================================================
enum MeshState
{
    MESH_STATE_IDLE,
    MESH_STATE_POWER_ON_SETTLE,
    MESH_STATE_WAIT_WAKE_QUEUE,
    MESH_STATE_WAIT_TX_STATUS,
    MESH_STATE_SHUTDOWN_DELAY
};

static MeshState meshState = MESH_STATE_IDLE;

static uint32_t      meshSentPacketId = 0;
static bool          meshQueueEntrySeen = false;
static bool          meshQueueBaselineValid = false;
static uint32_t      meshQueueFreeBeforeTelemetry = 0;
static unsigned long meshPowerOnMillis = 0;
static unsigned long meshWakeStartMillis = 0;
static unsigned long meshTxStartMillis = 0;
static unsigned long meshLastHeartbeatMillis = 0;
static unsigned long meshShutdownStartMillis = 0;

// Donnees de telemetrie conservees entre l'appel du DataLogger et
// l'emission effective, apres le reveil du T114.
static uint32_t meshPendingUtcUnixTime = 0;
static float    meshPendingTemperatureC = 0.0f;
static float    meshPendingHumidityPercent = 0.0f;
static float    meshPendingPressureHpa = 0.0f;
static uint16_t meshPendingWindDirectionDeg = 0;
static float    meshPendingWindSpeedKph = 0.0f;
static float    meshPendingWindGustKph = 0.0f;
static float    meshPendingRainfall1hMm = 0.0f;
static float    meshPendingRainfall24hMm = 0.0f;

#if DEBUG_MESH
// Affiche un tampon d'octets en hexadecimal, prefixe par sa direction -
// seule chose exploitable a l'oeil pour une trame protobuf binaire (pas de
// decodage generique ici). Le flush() est indispensable sur port USB-CDC :
// sans lui, des lignes emises a plusieurs secondes d'intervalle peuvent
// s'afficher avec le meme horodatage sur le moniteur serie (tampon non
// vide entre-temps), donnant une fausse impression de blocage instantane.
static void debugPrintFrame(const char direction[], const uint8_t *frameBytes, size_t length)
{
    Serial.print(F("[MeshLink][DEBUG_MESH] "));
    Serial.print(direction);
    Serial.print(F(" ("));
    Serial.print(length);
    Serial.print(F(" octets) : "));
    for (size_t byteIndex = 0; byteIndex < length; byteIndex++)
    {
        if (frameBytes[byteIndex] < 0x10)
        {
            Serial.print(F("0"));
        }
        Serial.print(frameBytes[byteIndex], HEX);
        Serial.print(F(" "));
    }
    Serial.println();
    if (Serial) { Serial.flush(); }
}
#endif

static void sendFramedToRadio(const uint8_t *toRadioBytes, size_t length)
{
    Serial2.write(MESHTASTIC_FRAME_START1);
    Serial2.write(MESHTASTIC_FRAME_START2);
    Serial2.write((uint8_t)((length >> 8) & 0xFF));
    Serial2.write((uint8_t)(length & 0xFF));
    Serial2.write(toRadioBytes, length);

#if DEBUG_MESH
    debugPrintFrame("TX", toRadioBytes, length);
#endif
}

// Lit UNE trame FromRadio complete depuis Serial2 avant deadlineMillis.
// --- Scan protobuf minimal ("sauter les champs inconnus"), PAS un
// decodeur generique (règle 15) - trois variantes selon ce qu'on cherche,
// meme logique de saut par-dessus les champs qui ne nous interessent pas.
// Un wiretype non reconnu (groupes, obsoletes) fait abandonner le scan
// plutot que risquer une interpretation erronee.

// Cherche UN champ VARINT precis au niveau racine de payload.
static bool findVarintField(const uint8_t *payload, uint16_t length, uint8_t targetFieldNumber, uint32_t &value)
{
    uint16_t offset = 0;
    while (offset < length)
    {
        uint8_t tag = payload[offset];
        offset = offset + 1;
        uint8_t fieldNumber = tag >> 3;
        uint8_t wireType = tag & 0x07;

        if (wireType == 0)   // varint
        {
            uint32_t fieldValue = 0;
            uint8_t shift = 0;
            bool complete = false;
            while (offset < length)
            {
                uint8_t b = payload[offset];
                offset = offset + 1;
                fieldValue = fieldValue | (((uint32_t)(b & 0x7F)) << shift);
                shift = shift + 7;
                if ((b & 0x80) == 0) { complete = true; break; }
            }
            if (complete == false) { return false; }
            if (fieldNumber == targetFieldNumber)
            {
                value = fieldValue;
                return true;
            }
        }
        else if (wireType == 2)   // length-delimited (sous-message/bytes/string)
        {
            uint32_t subLength = 0;
            uint8_t shift = 0;
            bool complete = false;
            while (offset < length)
            {
                uint8_t b = payload[offset];
                offset = offset + 1;
                subLength = subLength | (((uint32_t)(b & 0x7F)) << shift);
                shift = shift + 7;
                if ((b & 0x80) == 0) { complete = true; break; }
            }
            if (complete == false) { return false; }
            if ((uint32_t)offset + subLength > (uint32_t)length) { return false; }
            offset = (uint16_t)(offset + subLength);
        }
        else if (wireType == 5)   // fixed32
        {
            if ((uint32_t)offset + 4 > (uint32_t)length) { return false; }
            offset = (uint16_t)(offset + 4);
        }
        else if (wireType == 1)   // fixed64
        {
            if ((uint32_t)offset + 8 > (uint32_t)length) { return false; }
            offset = (uint16_t)(offset + 8);
        }
        else
        {
            return false;
        }
    }
    return false;
}

// Localise un sous-message (champ length-delimited) de numero
// targetFieldNumber au niveau racine de payload.
static bool findSubMessage(const uint8_t *payload, uint16_t length, uint8_t targetFieldNumber,
                            const uint8_t *&subPayload, uint16_t &subLength)
{
    uint16_t offset = 0;
    while (offset < length)
    {
        uint8_t tag = payload[offset];
        offset = offset + 1;
        uint8_t fieldNumber = tag >> 3;
        uint8_t wireType = tag & 0x07;

        if (wireType == 2)   // length-delimited
        {
            uint32_t declaredSubLength = 0;
            uint8_t shift = 0;
            bool complete = false;
            while (offset < length)
            {
                uint8_t b = payload[offset];
                offset = offset + 1;
                declaredSubLength = declaredSubLength | (((uint32_t)(b & 0x7F)) << shift);
                shift = shift + 7;
                if ((b & 0x80) == 0) { complete = true; break; }
            }
            if (complete == false) { return false; }
            if ((uint32_t)offset + declaredSubLength > (uint32_t)length) { return false; }
            if (fieldNumber == targetFieldNumber)
            {
                subPayload = payload + offset;
                subLength = (uint16_t)declaredSubLength;
                return true;
            }
            offset = (uint16_t)(offset + declaredSubLength);
        }
        else if (wireType == 0)   // varint
        {
            uint8_t shift = 0;
            bool complete = false;
            while (offset < length)
            {
                uint8_t b = payload[offset];
                offset = offset + 1;
                shift = shift + 7;
                if ((b & 0x80) == 0) { complete = true; break; }
            }
            if (complete == false) { return false; }
        }
        else if (wireType == 5)
        {
            if ((uint32_t)offset + 4 > (uint32_t)length) { return false; }
            offset = (uint16_t)(offset + 4);
        }
        else if (wireType == 1)
        {
            if ((uint32_t)offset + 8 > (uint32_t)length) { return false; }
            offset = (uint16_t)(offset + 8);
        }
        else
        {
            return false;
        }
    }
    return false;
}

// Construit le message ToRadio "heartbeat" avec un nonce EXPLICITE (voir
// MeshLink.h : la deduplication Meshtastic exige un nonce different a
// chaque envoi, jamais 0 ni 1 - voir allocateHeartbeatNonce()).
static bool meshBuildHeartbeatToRadio(uint8_t *outputBuffer, size_t outputCapacity, size_t &outputLength, uint32_t nonce)
{
    uint8_t heartbeatBuffer[8];
    ProtobufWriter heartbeatWriter;
    heartbeatWriter.begin(heartbeatBuffer, sizeof(heartbeatBuffer));
    heartbeatWriter.writeVarintField(HEARTBEAT_FIELD_NONCE, nonce);

    ProtobufWriter toRadioWriter;
    toRadioWriter.begin(outputBuffer, outputCapacity);
    toRadioWriter.writeBytesField(TORADIO_FIELD_HEARTBEAT, heartbeatWriter.data(), heartbeatWriter.length());

    outputLength = toRadioWriter.length();
    return ((toRadioWriter.overflowed() == false) && (heartbeatWriter.overflowed() == false));
}

// Alloue un nonce de heartbeat UNIQUE a chaque appel : 0 et 1 sont
// specialement interpretes par le firmware (voir MeshLink.h) - la sequence
// utilisee ici demarre a 2 et incremente, ne revenant jamais sur 0/1 meme
// en cas de retournement du compteur (situation purement theorique : il
// faudrait ~4 milliards d'envois).
static uint32_t nextHeartbeatNonce = 2;
static uint32_t allocateHeartbeatNonce()
{
    uint32_t nonce = nextHeartbeatNonce;
    nextHeartbeatNonce = nextHeartbeatNonce + 1;
    if (nextHeartbeatNonce < 2) { nextHeartbeatNonce = 2; }
    return nonce;
}

static void sendHeartbeat()
{
    uint32_t nonce = allocateHeartbeatNonce();
    uint8_t heartbeatBuffer[8];
    size_t heartbeatLength = 0;
    bool built = meshBuildHeartbeatToRadio(heartbeatBuffer, sizeof(heartbeatBuffer), heartbeatLength, nonce);
    if (built == true)
    {
        sendFramedToRadio(heartbeatBuffer, heartbeatLength);
    }
}

// Version NON BLOQUANTE de l'assemblage de trame, pour meshLinkUpdate()
// (appelee a chaque loop(), ne doit jamais bloquer - retarderait la
// reception RS485). Etat conserve entre les appels : consomme tout ce qui
// est disponible sur Serial2 a chaque appel, peut completer une trame
// etalee sur plusieurs appels. Renvoie true une fois par trame assemblee.
enum IncomingFrameState
{
    FRAME_STATE_WAIT_START1,
    FRAME_STATE_WAIT_START2,
    FRAME_STATE_WAIT_LEN_HI,
    FRAME_STATE_WAIT_LEN_LO,
    FRAME_STATE_WAIT_PAYLOAD
};
static IncomingFrameState incomingFrameState = FRAME_STATE_WAIT_START1;
static uint16_t           incomingFrameDeclaredLength = 0;
static uint16_t           incomingFrameReceivedLength = 0;
static uint8_t             incomingFramePayload[MESH_FRAME_PAYLOAD_MAX_LEN];

static bool pumpIncomingFrame(const uint8_t *&payloadOut, uint16_t &lengthOut)
{
    // Plafond de securite (voir Config.h : MESH_PUMP_MAX_BYTES_PER_CALL) :
    // sans handshake prealable, le T114 peut rester en mode "texte de
    // debug" et inonder Serial2 en continu - un while() sans limite ici
    // ne rendrait alors JAMAIS la main a loop() si le flux arrive plus
    // vite qu'on ne le consomme, gelant toute la carte (console ET
    // reception RS485). Bug reel observe (console figee avec
    // MESHSKIPCONFIG=1) : les octets restants sont traites au(x)
    // prochain(s) appel(s), rien n'est perdu, juste etale dans le temps.
    uint16_t bytesProcessed = 0;
    while ((Serial2.available() > 0) && (bytesProcessed < MESH_PUMP_MAX_BYTES_PER_CALL))
    {
        uint8_t b = (uint8_t)Serial2.read();
        bytesProcessed = bytesProcessed + 1;
        switch (incomingFrameState)
        {
            case FRAME_STATE_WAIT_START1:
                if (b == MESHTASTIC_FRAME_START1) { incomingFrameState = FRAME_STATE_WAIT_START2; }
                break;

            case FRAME_STATE_WAIT_START2:
                incomingFrameState = (b == MESHTASTIC_FRAME_START2) ? FRAME_STATE_WAIT_LEN_HI : FRAME_STATE_WAIT_START1;
                break;

            case FRAME_STATE_WAIT_LEN_HI:
                incomingFrameDeclaredLength = (uint16_t)((uint16_t)b << 8);
                incomingFrameState = FRAME_STATE_WAIT_LEN_LO;
                break;

            case FRAME_STATE_WAIT_LEN_LO:
                incomingFrameDeclaredLength = (uint16_t)(incomingFrameDeclaredLength | b);
                incomingFrameReceivedLength = 0;
                if ((incomingFrameDeclaredLength == 0) || (incomingFrameDeclaredLength > sizeof(incomingFramePayload)))
                {
                    incomingFrameState = FRAME_STATE_WAIT_START1;   // vide ou trop grande : resynchronisation
                }
                else
                {
                    incomingFrameState = FRAME_STATE_WAIT_PAYLOAD;
                }
                break;

            case FRAME_STATE_WAIT_PAYLOAD:
                incomingFramePayload[incomingFrameReceivedLength] = b;
                incomingFrameReceivedLength = incomingFrameReceivedLength + 1;
                if (incomingFrameReceivedLength >= incomingFrameDeclaredLength)
                {
                    incomingFrameState = FRAME_STATE_WAIT_START1;
                    payloadOut = incomingFramePayload;
                    lengthOut = incomingFrameDeclaredLength;
                    return true;
                }
                break;
        }
    }
    return false;
}

// Coupure complete du lien Mesh : Serial2.end() AVANT de couper
// l'alimentation - sur ce coeur nRF52, un Serial2.begin(nouveau debit)
// sans end() prealable ne reconfigure pas fiablement le debit reel de
// l'UART (verifie a l'oscilloscope).
static void meshShutdown()
{
    Serial2.end();
    power.disableMesh();
    sharedUartRelease(SHARED_UART_MESH);

    meshState = MESH_STATE_IDLE;
    meshSentPacketId = 0;
    meshQueueEntrySeen = false;
    meshQueueBaselineValid = false;
    meshQueueFreeBeforeTelemetry = 0;
    meshPowerOnMillis = 0;
    meshWakeStartMillis = 0;
    meshTxStartMillis = 0;
    meshLastHeartbeatMillis = 0;
    meshShutdownStartMillis = 0;
}

// Prepare et envoie la telemetrie conservee apres le reveil.
// Cette fonction est appelee uniquement depuis meshLinkUpdate(), donc hors
// du contexte de reservation initiale du bus.
static bool sendPendingEnvironmentTelemetry()
{
    uint8_t telemetryBuffer[128];
    size_t telemetryLength = 0;
    uint32_t packetId = 0;

    bool built = meshBuildEnvironmentTelemetryToRadio(
        telemetryBuffer, sizeof(telemetryBuffer), telemetryLength, packetId,
        meshPendingUtcUnixTime,
        meshPendingTemperatureC,
        meshPendingHumidityPercent,
        meshPendingPressureHpa,
        meshPendingWindDirectionDeg,
        meshPendingWindSpeedKph,
        meshPendingWindGustKph,
        meshPendingRainfall1hMm,
        meshPendingRainfall24hMm);

    if (built == false)
    {
        Serial.println(F("[MeshLink] Erreur : message de telemetrie trop volumineux pour le tampon"));
        if (Serial) { Serial.flush(); }
        logEvent(F("Erreur : message Meshtastic trop volumineux"));
        meshShutdown();
        return false;
    }

    sendFramedToRadio(telemetryBuffer, telemetryLength);

    meshSentPacketId = packetId;
    meshQueueEntrySeen = false;
    meshTxStartMillis = millis();
    meshLastHeartbeatMillis = meshTxStartMillis;
    meshState = MESH_STATE_WAIT_TX_STATUS;

    Serial.print(F("[MeshLink] Telemetrie (id "));
    Serial.print(packetId);
    Serial.println(F(") envoyee, surveillance de la file pendant 5 s"));
    if (Serial) { Serial.flush(); }
    logEvent(F("Telemetrie Meshtastic envoyee, surveillance de la file"));

    return true;
}

bool meshLinkSendEnvironmentTelemetry(uint32_t utcUnixTime,
                                       float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                       uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                       float rainfall1hMm, float rainfall24hMm)
{
    if (meshState != MESH_STATE_IDLE)
    {
        Serial.println(F("[MeshLink] Session precedente encore en cours : nouvel envoi differe"));
        if (Serial) { Serial.flush(); }
        return false;
    }

    bool busAcquired = sharedUartAcquire(SHARED_UART_MESH);
    if (busAcquired == false)
    {
        Serial.println(F("[MeshLink] Bus partage occupe (GPS) : envoi differe au prochain creneau"));
        if (Serial) { Serial.flush(); }
        return false;
    }

    // Conserve les donnees jusqu'a l'emission effective, qui intervient
    // apres le reveil non bloquant du T114.
    meshPendingUtcUnixTime = utcUnixTime;
    meshPendingTemperatureC = temperatureC;
    meshPendingHumidityPercent = relativeHumidityPercent;
    meshPendingPressureHpa = pressureHpa;
    meshPendingWindDirectionDeg = windDirectionDeg;
    meshPendingWindSpeedKph = windSpeedKph;
    meshPendingWindGustKph = windGustKph;
    meshPendingRainfall1hMm = rainfall1hMm;
    meshPendingRainfall24hMm = rainfall24hMm;

    Serial2.begin(params.getMeshBaudRate());
    power.enableMesh();
    power.disableGps();

    meshPowerOnMillis = millis();
    meshState = MESH_STATE_POWER_ON_SETTLE;

    Serial.println(F("[MeshLink] T114 sous tension : attente demarrage"));
    if (Serial) { Serial.flush(); }

    return true;
}

void meshLinkUpdate()
{
    if (meshState == MESH_STATE_IDLE)
    {
        return;
    }

    // ------------------------------------------------------------------------
    // Etat 1 : laisser le T114 demarrer sans bloquer loop().
    // ------------------------------------------------------------------------
    if (meshState == MESH_STATE_POWER_ON_SETTLE)
    {
        if ((millis() - meshPowerOnMillis) < params.getMeshPowerOnSettleMs())
        {
            return;
        }

        // Reveil par heartbeat. Le nonce est global a toutes les sessions
        // et continue donc a s'incrementer d'une emission a la suivante.
        sendHeartbeat();
        meshWakeStartMillis = millis();
        meshQueueBaselineValid = false;
        meshQueueFreeBeforeTelemetry = 0;
        meshState = MESH_STATE_WAIT_WAKE_QUEUE;

        Serial.println(F("[MeshLink] Heartbeat de reveil envoye : attente queue_status"));
        if (Serial) { Serial.flush(); }
        return;
    }

    // ------------------------------------------------------------------------
    // Etats 2 et 3 : reception des FromRadio.
    // On ne lit qu'une trame complete par appel afin de ne jamais bloquer
    // la boucle principale et la reception RS485.
    // ------------------------------------------------------------------------
    const uint8_t *framePayload = nullptr;
    uint16_t frameLength = 0;
    bool gotFrame = pumpIncomingFrame(framePayload, frameLength);

    if (gotFrame == true)
    {
#if DEBUG_MESH
        debugPrintFrame("RX", framePayload, frameLength);
#endif

        const uint8_t *queueStatusPayload = nullptr;
        uint16_t queueStatusLength = 0;
        bool foundQueueStatus = findSubMessage(
            framePayload, frameLength, FROMRADIO_FIELD_QUEUE_STATUS,
            queueStatusPayload, queueStatusLength);

        if (foundQueueStatus == true)
        {
            uint32_t freeSlots = 0;
            uint32_t maxSlots = 0;
            uint32_t queuedPacketId = 0;

            bool foundFree = findVarintField(
                queueStatusPayload, queueStatusLength,
                QUEUESTATUS_FIELD_FREE, freeSlots);
            bool foundMaxlen = findVarintField(
                queueStatusPayload, queueStatusLength,
                QUEUESTATUS_FIELD_MAXLEN, maxSlots);
            bool foundPacketId = findVarintField(
                queueStatusPayload, queueStatusLength,
                QUEUESTATUS_FIELD_MESH_PACKET_ID, queuedPacketId);

#if DEBUG_MESH
            Serial.print(F("[MeshLink][DEBUG_MESH] queue_status : free="));
            if (foundFree == true)
            {
                Serial.print(freeSlots);
            }
            else
            {
                Serial.print(F("?"));
            }

            Serial.print(F(" maxlen="));
            if (foundMaxlen == true)
            {
                Serial.print(maxSlots);
            }
            else
            {
                Serial.print(F("?"));
            }

            if (foundPacketId == true)
            {
                Serial.print(F(" mesh_packet_id="));
                Serial.print(queuedPacketId);
            }
            Serial.println();
            if (Serial) { Serial.flush(); }
#endif

            // ------------------------------------------------------------
            // Reveil : seul free == 0 bloque l'envoi.
            // free > 0 => la telemetrie peut partir immediatement.
            // ------------------------------------------------------------
            if (meshState == MESH_STATE_WAIT_WAKE_QUEUE)
            {
                if ((foundFree == true) && (freeSlots > 0))
                {
                    meshQueueFreeBeforeTelemetry = freeSlots;
                    meshQueueBaselineValid = true;

                    Serial.print(F("[MeshLink] Wake queue disponible : free="));
                    Serial.println(freeSlots);
#if DEBUG_MESH
                    Serial.print(F("[MeshLink][DEBUG_MESH] Baseline queue avant telemetrie : free="));
                    Serial.println(meshQueueFreeBeforeTelemetry);
#endif
                    if (Serial) { Serial.flush(); }

                    sendPendingEnvironmentTelemetry();
                    return;
                }

                if ((foundFree == true) && (freeSlots == 0))
                {
                    Serial.println(F("[MeshLink] Wake queue pleine (free=0) : attente liberation"));
                    if (Serial) { Serial.flush(); }
                }
            }

            // ------------------------------------------------------------
            // Apres envoi : confirmation de l'entree de notre paquet dans
            // la queue par diminution de free.
            //
            // Le chemin de reveil par Heartbeat ne fournit pas toujours
            // mesh_packet_id. On utilise donc le changement d'etat de la
            // queue : free doit etre strictement inferieur a la valeur
            // observee juste avant notre envoi.
            // ------------------------------------------------------------
            if ((meshState == MESH_STATE_WAIT_TX_STATUS) &&
                (meshQueueEntrySeen == false) &&
                (meshQueueBaselineValid == true) &&
                (foundFree == true) &&
                (freeSlots < meshQueueFreeBeforeTelemetry))
            {
                meshQueueEntrySeen = true;

                Serial.print(F("[MeshLink] Entree de notre paquet dans la queue : free "));
                Serial.print(meshQueueFreeBeforeTelemetry);
                Serial.print(F(" -> "));
                Serial.println(freeSlots);
                if (Serial) { Serial.flush(); }
                logEvent(F("Telemetrie Meshtastic entree dans la queue"));
            }

            // ------------------------------------------------------------
            // File redevenue vide APRES avoir constate sa diminution.
            // Aucun autre critere : arret programme 200 ms plus tard.
            // ------------------------------------------------------------
            if ((meshState == MESH_STATE_WAIT_TX_STATUS) &&
                (meshQueueEntrySeen == true) &&
                (foundFree == true) &&
                (foundMaxlen == true) &&
                (freeSlots == maxSlots))
            {
                meshShutdownStartMillis = millis();
                meshState = MESH_STATE_SHUTDOWN_DELAY;

                Serial.println(F("[MeshLink] Queue vide apres passage de notre paquet : arret programme dans 200 ms"));
                if (Serial) { Serial.flush(); }
                logEvent(F("Queue Meshtastic vide apres transmission"));
            }
        }
    }

    // ------------------------------------------------------------------------
    // Etat 2 : timeout du reveil.
    // Apres ce delai, l'absence de queue_status n'est PAS bloquante.
    // On envoie quand meme la telemetrie, conformement au protocole choisi.
    // ------------------------------------------------------------------------
    if (meshState == MESH_STATE_WAIT_WAKE_QUEUE)
    {
        if ((uint32_t)(millis() - meshWakeStartMillis) >= MESH_WAKE_QUEUE_TIMEOUT_MS)
        {
            Serial.println(F("[MeshLink] Timeout wake queue : aucune information bloquante, envoi de la telemetrie"));
            if (Serial) { Serial.flush(); }
            logEvent(F("Timeout wake queue Meshtastic, envoi quand meme"));
            sendPendingEnvironmentTelemetry();
            return;
        }

        return;
    }

    // ------------------------------------------------------------------------
    // Etat 3 : heartbeat periodique, independamment de la presence
    // ou non de notre packet_id dans queue_status.
    // ------------------------------------------------------------------------
    if (meshState == MESH_STATE_WAIT_TX_STATUS)
    {
        if ((millis() - meshTxStartMillis) >= MESH_TX_TIMEOUT_MS)
        {
            Serial.println(F("[MeshLink] Timeout transmission 5 s : coupure du Mesh"));
            if (Serial) { Serial.flush(); }
            logEvent(F("Timeout transmission Meshtastic"));
            meshShutdown();
            return;
        }

        if ((uint32_t)(millis() - meshLastHeartbeatMillis) >= MESH_HEARTBEAT_INTERVAL_MS)
        {
            sendHeartbeat();
            meshLastHeartbeatMillis = millis();
        }

        return;
    }

    // ------------------------------------------------------------------------
    // Etat 4 : la queue est vide ; aucune condition supplementaire n'est
    // requise. On attend simplement les 200 ms parametrables.
    // ------------------------------------------------------------------------
    if (meshState == MESH_STATE_SHUTDOWN_DELAY)
    {
        if ((uint32_t)(millis() - meshShutdownStartMillis) >= MESH_QUEUE_EMPTY_SHUTDOWN_MS)
        {
            Serial.println(F("[MeshLink] Delai post-queue ecoule : arret du Mesh"));
            if (Serial) { Serial.flush(); }
            logEvent(F("Fin de session Meshtastic apres queue vide"));
            meshShutdown();
        }
    }
}

#endif // USE_MESHTASTIC
