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

// Alias local pour la lisibilite du reste de ce fichier (voir MeshLink.h
// pour la definition et le detail des valeurs speciales).
static const uint32_t MESH_WANT_CONFIG_NONCE = MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG;

// Machine a etats minimale pour le cycle "envoi -> attente de confirmation
// -> coupure" (voir MeshLink.h : meshLinkUpdate()). Pas d'enum dediee pour
// un simple booleen - un seul envoi a la fois est possible (règle 15).
static bool          meshWaitingForAck = false;
static unsigned long meshHoldStartMillis = 0;
static unsigned long meshHoldDurationMs = 0;
// ID du paquet de telemetrie actuellement en attente de confirmation (voir
// MeshtasticTelemetry.h : MESHPACKET_FIELD_ID), pour reconnaitre
// specifiquement le FromRadio.queue_status qui lui correspond parmi tout
// le trafic FromRadio (qui peut inclure du relais MQTT ou d'autres nœuds).
static uint32_t      meshSentPacketId = 0;
// Deux niveaux de confirmation croissants (voir MeshLink.h) :
static bool          meshQueueEntrySeen = false;    // notre paquet vu ENTRER en file
static bool          meshQueueDrainSeen = false;    // la file est ENSUITE retombee a vide
static unsigned long meshLastHeartbeatMillis = 0;    // dernier heartbeat de relance (phase B, voir meshLinkUpdate())

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
// Bloquant mais borne - reserve a la poignee de main (performHandshake()),
// un contexte deja bloquant/court ; meshLinkUpdate() utilise la version non
// bloquante plus bas (pumpIncomingFrame()). Se resynchronise sur le
// premier START1 rencontre. Une trame plus grande que bufferCapacity est
// videe du flux (pour ne pas desynchroniser la lecture suivante) mais
// signalee en echec : l'appelant doit retenter.
static bool readOneFrameBlocking(uint8_t *payloadBuffer, size_t bufferCapacity, uint16_t &payloadLength, unsigned long deadlineMillis)
{
    while (true)
    {
        if (millis() >= deadlineMillis) { return false; }
        if (Serial2.available() == 0) { continue; }
        if ((uint8_t)Serial2.read() == MESHTASTIC_FRAME_START1) { break; }
    }
    while (true)
    {
        if (millis() >= deadlineMillis) { return false; }
        if (Serial2.available() == 0) { continue; }
        if ((uint8_t)Serial2.read() == MESHTASTIC_FRAME_START2) { break; }
        return false;   // pas START2 juste apres START1 : pas une vraie trame, on abandonne cette tentative
    }

    uint16_t declaredLength = 0;
    for (uint8_t byteIndex = 0; byteIndex < 2; byteIndex++)
    {
        while (Serial2.available() == 0)
        {
            if (millis() >= deadlineMillis) { return false; }
        }
        declaredLength = (uint16_t)((declaredLength << 8) | (uint8_t)Serial2.read());
    }

    if (declaredLength > bufferCapacity)
    {
        uint16_t remaining = declaredLength;
        while (remaining > 0)
        {
            if (millis() >= deadlineMillis) { return false; }
            if (Serial2.available() > 0)
            {
                Serial2.read();
                remaining = remaining - 1;
            }
        }
        return false;
    }

    for (uint16_t byteIndex = 0; byteIndex < declaredLength; byteIndex++)
    {
        while (Serial2.available() == 0)
        {
            if (millis() >= deadlineMillis) { return false; }
        }
        payloadBuffer[byteIndex] = (uint8_t)Serial2.read();
    }
    payloadLength = declaredLength;
    return true;
}

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

// Construit le message ToRadio "want_config_id" (poignee de main initiale).
static bool meshBuildWantConfigToRadio(uint8_t *outputBuffer, size_t outputCapacity, size_t &outputLength, uint32_t configId)
{
    ProtobufWriter toRadioWriter;
    toRadioWriter.begin(outputBuffer, outputCapacity);
    toRadioWriter.writeVarintField(TORADIO_FIELD_WANT_CONFIG_ID, configId);

    outputLength = toRadioWriter.length();
    return (toRadioWriter.overflowed() == false);
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

// Poignee de main ALLEGEE (MESHSKIPCONFIG=1, defaut - voir Config.h :
// MESH_SKIP_CONFIG_DEFAULT) : un seul heartbeat, sans jamais demander
// want_config_id. Etablit un flux protobuf reconnu par le firmware sans
// l'effet de bord observe avec want_config_id (perte de la connexion
// Bluetooth du T114 avec le telephone). Attend ensuite soit un
// queue_status vide (free == maxlen, preuve que le firmware est reactif
// et sa file d'emission actuellement vide), soit l'expiration du delai -
// tout ce qui est recu entre-temps est journalise en DEBUG_MESH.
static void performLightweightWakeup()
{
    sendHeartbeat();

    unsigned long wakeupDeadline = millis() + MESH_HANDSHAKE_MAX_WAIT_MS;
    bool queueEmptySeen = false;
    uint8_t framePayload[MESH_FRAME_PAYLOAD_MAX_LEN];

    while ((queueEmptySeen == false) && (millis() < wakeupDeadline))
    {
        uint16_t frameLength = 0;
        bool gotFrame = readOneFrameBlocking(framePayload, sizeof(framePayload), frameLength, wakeupDeadline);
        if (gotFrame == false)
        {
            continue;   // trame invalide/trop grande, ou rien reçu : on retente, la boucle verifie deja la deadline
        }

#if DEBUG_MESH
        debugPrintFrame("RX (reveil allege)", framePayload, frameLength);
#endif

        const uint8_t *queueStatusPayload = nullptr;
        uint16_t       queueStatusLength = 0;
        bool foundQueueStatus = findSubMessage(framePayload, frameLength, FROMRADIO_FIELD_QUEUE_STATUS,
                                                queueStatusPayload, queueStatusLength);
        if (foundQueueStatus == true)
        {
            uint32_t freeSlots = 0;
            uint32_t maxSlots = 0;
            bool foundFree = findVarintField(queueStatusPayload, queueStatusLength, QUEUESTATUS_FIELD_FREE, freeSlots);
            bool foundMaxlen = findVarintField(queueStatusPayload, queueStatusLength, QUEUESTATUS_FIELD_MAXLEN, maxSlots);
            if ((foundFree == true) && (foundMaxlen == true) && (freeSlots == maxSlots))
            {
                queueEmptySeen = true;
            }
        }
    }

    if (queueEmptySeen == false)
    {
        Serial.println(F("[MeshLink] Avertissement : pas de queue_status vide recu (timeout reveil) - envoi quand meme"));
        if (Serial) { Serial.flush(); }
        logEvent(F("Timeout reveil allege Meshtastic"));
    }
}

// Poignee de main COMPLETE (MESHSKIPCONFIG=0) : ToRadio.want_config_id,
// attente du vrai signal de fin (FromRadio.config_complete_id echoant
// MESH_WANT_CONFIG_NONCE). Le firmware envoie alors plusieurs trames
// intermediaires (config, NodeInfo...) avant ce signal ; les lire et les
// jeter une a une est le seul moyen fiable de savoir quand s'arreter.
// ATTENTION (confirme par l'utilisateur sur materiel reel) : declenche la
// perte de la connexion Bluetooth du T114 avec le telephone, recuperable
// seulement en redemarrant le module - voir MESH_SKIP_CONFIG_DEFAULT,
// Config.h. A n'utiliser que si le dump de configuration est reellement
// necessaire pour un usage futur (ce projet ne l'exploite pas).
static void performFullConfigHandshake()
{
    uint8_t handshakeBuffer[8];
    size_t handshakeLength = 0;
    bool handshakeBuilt = meshBuildWantConfigToRadio(handshakeBuffer, sizeof(handshakeBuffer), handshakeLength, MESH_WANT_CONFIG_NONCE);
    if (handshakeBuilt == true)
    {
        sendFramedToRadio(handshakeBuffer, handshakeLength);
    }

    unsigned long handshakeDeadline = millis() + MESH_HANDSHAKE_MAX_WAIT_MS;
    bool configComplete = false;
    uint8_t framePayload[MESH_FRAME_PAYLOAD_MAX_LEN];

    while ((configComplete == false) && (millis() < handshakeDeadline))
    {
        uint16_t frameLength = 0;
        bool gotFrame = readOneFrameBlocking(framePayload, sizeof(framePayload), frameLength, handshakeDeadline);
        if (gotFrame == false)
        {
            continue;   // trame invalide/trop grande : on retente, la boucle verifie deja la deadline
        }

#if DEBUG_MESH
        debugPrintFrame("RX (handshake complet)", framePayload, frameLength);
#endif

        uint32_t configCompleteId = 0;
        bool found = findVarintField(framePayload, frameLength, FROMRADIO_FIELD_CONFIG_COMPLETE_ID, configCompleteId);
        if ((found == true) && (configCompleteId == MESH_WANT_CONFIG_NONCE))
        {
            configComplete = true;
        }
    }

    if (configComplete == false)
    {
        Serial.println(F("[MeshLink] Avertissement : config_complete_id jamais recu (timeout handshake) - envoi quand meme"));
        if (Serial) { Serial.flush(); }
        logEvent(F("Timeout poignee de main Meshtastic"));
    }
}

// Dispatch selon MESHSKIPCONFIG (voir Params::getMeshSkipConfig()) - AUCUN
// melange entre les deux chemins (bug corrige : un premier jet envoyait
// systematiquement un heartbeat AVANT de brancher, ce qui perturbait le
// chemin "config complete" en plus de ne pas nonce-r ce heartbeat).
static void performHandshake()
{
    if (params.getMeshSkipConfig() == true)
    {
        performLightweightWakeup();
    }
    else
    {
        performFullConfigHandshake();
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
    meshWaitingForAck = false;
    meshQueueEntrySeen = false;
    meshQueueDrainSeen = false;
    meshLastHeartbeatMillis = 0;
}

bool meshLinkSendEnvironmentTelemetry(uint32_t utcUnixTime,
                                       float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                       uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                       float rainfall1hMm, float rainfall24hMm)
{
    if (meshWaitingForAck == true)
    {
        // Un envoi precedent est encore en cours (voir meshLinkUpdate()) :
        // on ne l'interrompt jamais pour en demarrer un nouveau. Comme
        // MESH_TX_HOLD_MS reste toujours tres inferieur au creneau de
        // transmission (5 min, voir Config.h), ce cas devrait rester rare.
        Serial.println(F("[MeshLink] Envoi precedent encore en cours : nouvel envoi differe"));
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

    Serial2.begin(params.getMeshBaudRate());
    power.enableMesh();
    power.disableGps();

    // Laisse au T114 le temps de demarrer sa pile logicielle avant de lui
    // parler (voir Config.h : MESH_POWERON_SETTLE_MS_DEFAULT / Params).
    delay(params.getMeshPowerOnSettleMs());

    // performHandshake() dispatche lui-meme entre poignee de main allegee
    // et complete selon Params::getMeshSkipConfig() (voir plus haut) -
    // TOUJOURS l'appeler ici, sans re-brancher a ce niveau (bug corrige :
    // un premier jet dupliquait ce branchement ici, empechant purement et
    // simplement performLightweightWakeup() de s'executer en mode allege).
    performHandshake();

    uint8_t telemetryBuffer[128];
    size_t telemetryLength = 0;
    uint32_t packetId = 0;
    bool built = meshBuildEnvironmentTelemetryToRadio(telemetryBuffer, sizeof(telemetryBuffer), telemetryLength, packetId,
                                                        utcUnixTime, temperatureC, relativeHumidityPercent, pressureHpa,
                                                        windDirectionDeg, windSpeedKph, windGustKph, rainfall1hMm, rainfall24hMm);

    if (built == false)
    {
        Serial.println(F("[MeshLink] Erreur : message de telemetrie trop volumineux pour le tampon"));
        if (Serial) { Serial.flush(); }
        logEvent(F("Erreur : message Meshtastic trop volumineux"));
        meshShutdown();
        return false;
    }

    sendFramedToRadio(telemetryBuffer, telemetryLength);
    Serial.print(F("[MeshLink] Telemetrie (id "));
    Serial.print(packetId);
    Serial.println(F(") ecrite sur l'UART, attente de confirmation du T114..."));
    if (Serial) { Serial.flush(); }
    logEvent(F("Telemetrie ecrite vers Meshtastic, attente confirmation"));

    // Alimentation VOLONTAIREMENT maintenue (voir MeshLink.h) : ecrire les
    // octets sur l'UART ne signifie pas que le T114 a fini de les emettre
    // sur le reseau radio. La coupure se fait dans meshLinkUpdate().
    meshSentPacketId = packetId;
    meshQueueEntrySeen = false;
    meshQueueDrainSeen = false;
    meshLastHeartbeatMillis = 0;
    meshWaitingForAck = true;
    meshHoldStartMillis = millis();
    meshHoldDurationMs = MESH_TX_HOLD_MS;
    return true;
}

void meshLinkUpdate()
{
    if (meshWaitingForAck == false)
    {
        return;
    }

    // Phase B (voir MeshLink.h) : une fois notre paquet confirme en file
    // (meshQueueEntrySeen), le firmware ne pousse PAS de queue_status
    // spontanement quand la file est vide (confirme par l'utilisateur :
    // aucune trame observee en l'absence d'evenement) - il faut donc le
    // solliciter activement, avec un nonce DIFFERENT a chaque envoi (voir
    // allocateHeartbeatNonce() : la deduplication Meshtastic ignorerait
    // sinon toute relance identique a la precedente). Rien n'est envoye
    // avant que l'entree en file soit confirmee (phase A, ecoute passive
    // uniquement) ni une fois la vidange deja constatee.
    if ((meshQueueEntrySeen == true) && (meshQueueDrainSeen == false)
        && ((millis() - meshLastHeartbeatMillis) >= MESH_POST_ENTRY_HEARTBEAT_INTERVAL_MS))
    {
        sendHeartbeat();
        meshLastHeartbeatMillis = millis();
    }

    // Assemble les trames qui arrivent, une par appel au plus (non
    // bloquant - rattrape au prochain appel si plusieurs arrivent dans le
    // meme intervalle de loop()). Chaque trame est confrontee a
    // FromRadio.queue_status (voir MeshLink.h pour la structure verifiee) :
    // un mesh_packet_id correspondant a NOTRE dernier envoi confirme que
    // le T114 l'a bien mis en file d'emission ; un QueueStatus SANS ce
    // champ (free == maxlen) apres cette premiere confirmation signifie
    // que la file est retombee a vide - la meilleure preuve disponible que
    // notre paquet en est reellement reparti.
    const uint8_t *framePayload = nullptr;
    uint16_t       frameLength = 0;
    bool gotFrame = pumpIncomingFrame(framePayload, frameLength);
    if (gotFrame == true)
    {
#if DEBUG_MESH
        debugPrintFrame("RX (pendant l'attente)", framePayload, frameLength);
#endif

        const uint8_t *queueStatusPayload = nullptr;
        uint16_t       queueStatusLength = 0;
        bool foundQueueStatus = findSubMessage(framePayload, frameLength, FROMRADIO_FIELD_QUEUE_STATUS,
                                                queueStatusPayload, queueStatusLength);
        if (foundQueueStatus == true)
        {
            uint32_t freeSlots = 0;
            uint32_t maxSlots = 0;
            uint32_t queuedPacketId = 0;
            bool foundFree = findVarintField(queueStatusPayload, queueStatusLength, QUEUESTATUS_FIELD_FREE, freeSlots);
            bool foundMaxlen = findVarintField(queueStatusPayload, queueStatusLength, QUEUESTATUS_FIELD_MAXLEN, maxSlots);
            bool foundPacketId = findVarintField(queueStatusPayload, queueStatusLength, QUEUESTATUS_FIELD_MESH_PACKET_ID, queuedPacketId);

#if DEBUG_MESH
            Serial.print(F("[MeshLink][DEBUG_MESH] queue_status : free="));
            Serial.print(foundFree ? String(freeSlots) : String("?"));
            Serial.print(F(" maxlen="));
            Serial.print(foundMaxlen ? String(maxSlots) : String("?"));
            if (foundPacketId == true)
            {
                Serial.print(F(" mesh_packet_id="));
                Serial.print(queuedPacketId);
            }
            Serial.println();
            if (Serial) { Serial.flush(); }
#endif

            if ((meshQueueEntrySeen == false) && (foundPacketId == true) && (queuedPacketId == meshSentPacketId))
            {
                meshQueueEntrySeen = true;
                Serial.println(F("[MeshLink] Notre paquet confirme en file d'emission (queue_status)"));
                if (Serial) { Serial.flush(); }
                logEvent(F("Telemetrie Meshtastic confirmee en file d'emission"));
                // Phase B (relance active toutes les MESH_POST_ENTRY_HEARTBEAT_INTERVAL_MS,
                // voir plus bas) : lui laisser jusqu'a MESH_TX_HOLD_MS pour
                // converger vers un queue_status vide, pas juste la courte
                // marge post-confirmation d'un premier jet (MESH_POST_QUEUE_HOLD_MS,
                // pensee pour un nudge unique, insuffisante pour plusieurs
                // cycles de relance a 100ms).
                meshHoldStartMillis = millis();
                meshHoldDurationMs = MESH_TX_HOLD_MS;
            }
            else if ((meshQueueEntrySeen == true) && (meshQueueDrainSeen == false)
                     && (foundFree == true) && (foundMaxlen == true) && (freeSlots == maxSlots))
            {
                meshQueueDrainSeen = true;
                Serial.println(F("[MeshLink] File d'emission retombee a vide : notre paquet en est reparti"));
                if (Serial) { Serial.flush(); }
                logEvent(F("Telemetrie Meshtastic confirmee repartie de la file"));
                meshHoldStartMillis = millis();
                meshHoldDurationMs = MESH_QUEUE_DRAIN_HOLD_MS;
            }
        }
    }

    // Idiome robuste au retournement de millis() (~49 jours) : ecart
    // depuis un depart compare a une duree, jamais une echeance absolue.
    if ((millis() - meshHoldStartMillis) >= meshHoldDurationMs)
    {
        if (meshQueueDrainSeen == true)
        {
            Serial.println(F("[MeshLink] Marge post-vidange ecoulee : coupure de l'alimentation Mesh"));
        }
        else if (meshQueueEntrySeen == true)
        {
            Serial.println(F("[MeshLink] Marge post-confirmation ecoulee (sans vidange observee) : coupure de l'alimentation Mesh"));
        }
        else
        {
            Serial.println(F("[MeshLink] Delai ecoule sans confirmation queue_status : coupure de l'alimentation Mesh"));
        }
        if (Serial) { Serial.flush(); }
        logEvent(F("Fin de la fenetre d'envoi Meshtastic"));
        meshShutdown();
    }
}

#endif // USE_MESHTASTIC
