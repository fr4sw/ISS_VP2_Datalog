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
// MeshtasticTelemetry.h : MESHPACKET_FIELD_ID) - permet de reconnaitre
// SPECIFIQUEMENT le FromRadio.queue_status qui lui correspond, plutot que
// n'importe quel trafic FromRadio qui passerait par la (voir l'historique
// dans MeshLink.h : c'est exactement le bug precedent).
static uint32_t      meshSentPacketId = 0;
// true des qu'un FromRadio.queue_status correspondant a meshSentPacketId a
// ete vu - a partir de la, on ne coupe plus qu'apres une marge courte
// (MESH_POST_QUEUE_HOLD_MS), plutot que d'attendre tout MESH_TX_HOLD_MS en
// filet de securite.
static bool          meshQueueConfirmed = false;

#if DEBUG_MESH
// Affiche un tampon d'octets en hexadecimal sur le moniteur serie, prefixe
// par sa direction (TX/RX) - c'est la seule chose exploitable a l'oeil pour
// une trame protobuf binaire (pas de decodage ici, juste la preuve que
// quelque chose part/arrive et sa taille). Reste dans MeshLink.cpp (pas de
// fichier dedie) : usage strictement local a ce module, pas de règle 20
// enfreinte (une seule tache - "journaliser une trame Mesh").
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
    // Sans ce flush(), deux lignes emises a plusieurs secondes d'intervalle
    // peuvent s'afficher sur le moniteur serie avec le meme horodatage
    // (tampon CDC-USB non vide entre-temps) - source probable de la
    // confusion "le timeout semble instantane" relevee par l'utilisateur :
    // le code peut tres bien avoir reellement attendu, sans que ça se voie.
    if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
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

// Lit UNE trame FromRadio complete depuis Serial2 avant deadlineMillis
// (millis() absolu). Bloquant mais borne - contexte deja bloquant/court
// (voir performHandshake()), pas besoin d'une machine a etats async ici
// (règle 15). Se resynchronise sur le premier START1 rencontre, en jetant
// tout octet avant (bruit ou texte de debug - voir reference en tete de
// fichier : "any byte that isn't start1/start2 is treated as plain text
// debug output"). Une trame plus grande que bufferCapacity est videe du
// flux (pour ne pas desynchroniser la lecture suivante) mais signalee en
// echec : l'appelant doit alors simplement retenter (il y aura d'autres
// trames).
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

// Scan minimal "sauter les champs inconnus" d'un message protobuf deja
// assemble, a la recherche d'UN champ VARINT precis au niveau racine - PAS
// un decodeur protobuf generique (règle 15 : juste ce dont ce projet a
// besoin). Gere les 4 wiretypes standard pour pouvoir sauter correctement
// par-dessus les champs qui ne nous interessent pas ; un wiretype non
// reconnu (groupes, obsoletes) fait abandonner le scan plutot que risquer
// une interpretation erronee. Reutilisee pour FROMRADIO_FIELD_CONFIG_COMPLETE_ID
// (voir performHandshake()) - le meme scan "sauter/chercher" s'applique
// quel que soit le champ recherche, seul le numero de champ change.
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

// Meme principe que findVarintField(), pour un champ FIXED32 (little-endian,
// voir ProtobufWriter::writeFixed32Field() - meme convention cote ecriture).
// Utilisee pour QueueStatus.mesh_packet_id (voir MeshLink.h - hypothese de
// type non verifiee, par analogie avec MeshPacket.id qui est fixed32).
static bool findFixed32Field(const uint8_t *payload, uint16_t length, uint8_t targetFieldNumber, uint32_t &value)
{
    uint16_t offset = 0;
    while (offset < length)
    {
        uint8_t tag = payload[offset];
        offset = offset + 1;
        uint8_t fieldNumber = tag >> 3;
        uint8_t wireType = tag & 0x07;

        if (wireType == 5)   // fixed32
        {
            if ((uint32_t)offset + 4 > (uint32_t)length) { return false; }
            uint32_t fieldValue = (uint32_t)payload[offset]
                                 | ((uint32_t)payload[offset + 1] << 8)
                                 | ((uint32_t)payload[offset + 2] << 16)
                                 | ((uint32_t)payload[offset + 3] << 24);
            offset = (uint16_t)(offset + 4);
            if (fieldNumber == targetFieldNumber)
            {
                value = fieldValue;
                return true;
            }
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
        else if (wireType == 2)   // length-delimited
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
// targetFieldNumber au niveau racine de payload - utilisee pour retrouver
// FromRadio.queue_status avant d'y chercher mesh_packet_id (voir
// meshLinkUpdate()). Meme logique de saut que les fonctions ci-dessus.
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
// Deplacee ici depuis MeshtasticTelemetry.cpp (organisation revue, voir
// MeshLink.h) : ce message ne concerne que la session, pas la telemetrie.
static bool meshBuildWantConfigToRadio(uint8_t *outputBuffer, size_t outputCapacity, size_t &outputLength, uint32_t configId)
{
    ProtobufWriter toRadioWriter;
    toRadioWriter.begin(outputBuffer, outputCapacity);
    toRadioWriter.writeVarintField(TORADIO_FIELD_WANT_CONFIG_ID, configId);

    outputLength = toRadioWriter.length();
    return (toRadioWriter.overflowed() == false);
}

// Envoie la "poignee de main" et attend le VRAI signal de fin
// (FromRadio.config_complete_id echoant MESH_WANT_CONFIG_NONCE, voir
// MeshtasticTelemetry.h) - PAS un delai fixe arbitraire comme dans un
// premier jet (bug releve par l'utilisateur, analyse d'un echange reel :
// avec MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG, le firmware peut envoyer
// plusieurs trames intermediaires - Config, eventuellement d'autres -
// avant ce signal ; les lire et les jeter une a une est le seul moyen
// fiable de savoir quand s'arreter). Attente bornee par
// MESH_HANDSHAKE_MAX_WAIT_MS (Config.h) : acceptable ici, Serial2 est un
// UART materiel independant du RS485 (Serial1), cette attente ne retarde
// jamais la reception des trames ISS.
static void performHandshake()
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
            // Trame invalide/trop grande pour notre tampon (voir
            // readOneFrameBlocking()) : on retente simplement, la
            // condition de la boucle verifie deja la deadline globale.
            continue;
        }

#if DEBUG_MESH
        debugPrintFrame("RX (handshake)", framePayload, frameLength);
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
        Serial.println(F("[MeshLink] Avertissement : config_complete_id jamais recu (timeout handshake) - envoi quand meme, au risque d'un paquet non pris en compte"));
        if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
        logEvent(F("Timeout poignee de main Meshtastic"));
    }
}

// Version NON BLOQUANTE de l'assemblage de trame (contrairement a
// readOneFrameBlocking(), utilisee uniquement pendant la poignee de main
// qui est un contexte deja bloquant/court). Necessaire ici : meshLinkUpdate()
// est appelee a chaque loop() et ne doit JAMAIS bloquer (retarderait la
// reception RS485, voir MeshLink.h). Etat conserve entre les appels
// (machine a etats explicite) : consomme tout ce qui est disponible sur
// Serial2 a CHAQUE appel, peut completer une trame etalee sur plusieurs
// appels successifs. Renvoie true UNE fois par trame complete assemblee.
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
    while (Serial2.available() > 0)
    {
        uint8_t b = (uint8_t)Serial2.read();
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
                    // Vide ou trop grande pour notre tampon : on se
                    // resynchronise sans essayer de la lire (meme choix
                    // que readOneFrameBlocking() pour le meme cas).
                    incomingFrameState = FRAME_STATE_WAIT_START1;
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

// Coupure complete du lien Mesh (voir MeshLink.h, point 6 de la sequence) :
// Serial2.end() AVANT de couper l'alimentation - sur ce coeur nRF52, un
// Serial2.begin(nouveauDebit) sans end() prealable ne reconfigure pas
// fiablement le debit reel de l'UART (observe a l'oscilloscope : le debit
// precedent - celui du GPS - restait actif malgre un begin() au bon debit
// Mesh). Factorisee ici car appelee depuis deux endroits (accuse recu,
// timeout) - voir meshLinkUpdate().
static void meshShutdown()
{
    Serial2.end();
    power.disableMesh();
    sharedUartRelease(SHARED_UART_MESH);
    meshWaitingForAck = false;
    meshQueueConfirmed = false;
}

bool meshLinkSendEnvironmentTelemetry(uint32_t utcUnixTime,
                                       float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                       uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                       float rainfall1hMm, float rainfall24hMm)
{
    if (meshWaitingForAck == true)
    {
        // Un envoi precedent est encore en attente de reponse (voir
        // meshLinkUpdate()) : on ne l'interrompt jamais pour en demarrer un
        // nouveau. Comme le timeout (MESH_ACK_TIMEOUT_MS, 3 min) est
        // toujours inferieur au creneau de transmission (5 min, voir
        // Config.h), ce cas devrait rester rare en pratique.
        Serial.println(F("[MeshLink] Envoi precedent encore en attente de reponse : nouvel envoi differe"));
        if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
        return false;
    }

    bool busAcquired = sharedUartAcquire(SHARED_UART_MESH);
    if (busAcquired == false)
    {
        Serial.println(F("[MeshLink] Bus partage occupe (GPS) : envoi differe au prochain creneau"));
        if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
        return false;
    }

    Serial2.begin(params.getMeshBaudRate());
    power.enableMesh();
    power.disableGps();

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
        if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
        logEvent(F("Erreur : message Meshtastic trop volumineux"));
        meshShutdown();
        return false;
    }

    sendFramedToRadio(telemetryBuffer, telemetryLength);
    Serial.print(F("[MeshLink] Telemetrie (id "));
    Serial.print(packetId);
    Serial.println(F(") ecrite sur l'UART, attente de confirmation du T114..."));
    if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
    logEvent(F("Telemetrie ecrite vers Meshtastic, attente confirmation"));

    // Alimentation VOLONTAIREMENT maintenue (voir MeshLink.h) : ecrire les
    // octets sur l'UART ne signifie pas que le T114 a fini de les emettre
    // sur le reseau radio. La coupure se fait dans meshLinkUpdate(), sur
    // confirmation de mise en file (FromRadio.queue_status) ou sur timeout.
    meshSentPacketId = packetId;
    meshQueueConfirmed = false;
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

    // Assemble les trames qui arrivent, une par appel au plus (voir
    // pumpIncomingFrame() - non bloquant, peut en manquer une si plusieurs
    // arrivent dans le meme intervalle de loop(), rattrapee au prochain
    // appel). Chaque trame complete est confrontee a FromRadio.queue_status
    // (voir MeshLink.h : hypothese de numeros de champ, PAS verifiee comme
    // config_complete_id) : si mesh_packet_id correspond a NOTRE dernier
    // envoi, c'est la confirmation que le T114 a bien mis ce paquet precis
    // en file d'emission - pas juste "un octet est arrive" comme dans un
    // premier jet (bug releve par l'utilisateur, voir l'historique dans
    // MeshLink.h).
    const uint8_t *framePayload = nullptr;
    uint16_t       frameLength = 0;
    bool gotFrame = pumpIncomingFrame(framePayload, frameLength);
    if (gotFrame == true)
    {
#if DEBUG_MESH
        debugPrintFrame("RX (pendant l'attente)", framePayload, frameLength);
#endif

        if (meshQueueConfirmed == false)
        {
            const uint8_t *queueStatusPayload = nullptr;
            uint16_t       queueStatusLength = 0;
            bool foundQueueStatus = findSubMessage(framePayload, frameLength, FROMRADIO_FIELD_QUEUE_STATUS,
                                                    queueStatusPayload, queueStatusLength);
            if (foundQueueStatus == true)
            {
                uint32_t queuedPacketId = 0;
                bool foundPacketId = findFixed32Field(queueStatusPayload, queueStatusLength,
                                                       QUEUESTATUS_FIELD_MESH_PACKET_ID, queuedPacketId);
                if ((foundPacketId == true) && (queuedPacketId == meshSentPacketId))
                {
                    meshQueueConfirmed = true;
                    Serial.println(F("[MeshLink] Confirmation queue_status recue pour notre paquet : mise en emission confirmee cote T114"));
                    if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
                    logEvent(F("Telemetrie Meshtastic confirmee en file d'emission"));
                    // Raccourcit l'echeance de coupure : plus besoin d'attendre
                    // tout MESH_TX_HOLD_MS (filet de securite pour le cas SANS
                    // confirmation) - une marge courte suffit desormais pour
                    // laisser le temps a l'emission radio elle-meme (voir
                    // Config.h : MESH_POST_QUEUE_HOLD_MS).
                    meshHoldStartMillis = millis();
                    meshHoldDurationMs = MESH_POST_QUEUE_HOLD_MS;
                }
            }
        }
    }

    // Meme idiome robuste au retournement de millis() (~49 jours) que
    // DataLogger.cpp (voir intervalDue) : ecart depuis un depart compare a
    // une duree, jamais une echeance absolue comparee directement.
    if ((millis() - meshHoldStartMillis) >= meshHoldDurationMs)
    {
        if (meshQueueConfirmed == true)
        {
            Serial.println(F("[MeshLink] Marge post-confirmation ecoulee (voir MESH_POST_QUEUE_HOLD_MS) : coupure de l'alimentation Mesh"));
        }
        else
        {
            Serial.println(F("[MeshLink] Delai ecoule sans confirmation queue_status (voir MESH_TX_HOLD_MS) : coupure de l'alimentation Mesh"));
        }
        if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
        logEvent(F("Fin de la fenetre d'envoi Meshtastic"));
        meshShutdown();
    }
}

#endif // USE_MESHTASTIC
