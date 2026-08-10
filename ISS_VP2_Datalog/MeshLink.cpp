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

// Valeur de want_config_id : voir MeshtasticTelemetry.h pour le detail des
// valeurs speciales decouvertes par analyse d'un echange reel sur T114
// (bug releve par l'utilisateur : "1" dans un premier jet n'etait pas
// une simple valeur arbitraire non nulle comme suppose a tort).
// MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG : config sans la NodeDB, plus
// rapide - c'est ce dont ce projet a besoin (la NodeDB n'est pas exploitee).
static const uint32_t MESH_WANT_CONFIG_NONCE = MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG;

// Machine a etats minimale pour l'attente d'accuse de reception (voir
// MeshLink.h : meshLinkUpdate()). Pas d'enum dediee pour un simple booleen -
// un seul envoi a la fois est possible (règle 15 : pas de complexite non
// necessaire).
static bool          meshWaitingForAck = false;
static unsigned long meshAckWaitStartMillis = 0;

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
        if ((uint8_t)Serial2.peek() == MESHTASTIC_FRAME_START1)
        {
            Serial2.read();
            break;
        }
        Serial2.read();
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

// Scan minimal "sauter les champs inconnus" d'un message FromRadio deja
// assemble, a la recherche du seul champ qui nous interesse
// (FROMRADIO_FIELD_CONFIG_COMPLETE_ID, voir MeshtasticTelemetry.h) - PAS
// un decodeur protobuf generique (règle 15 : juste ce dont ce projet a
// besoin). Gere les 4 wiretypes standard pour pouvoir sauter correctement
// par-dessus les champs qui ne nous interessent pas ; un wiretype non
// reconnu (groupes, obsoletes) fait abandonner le scan plutot que risquer
// une interpretation erronee.
static bool extractConfigCompleteId(const uint8_t *payload, uint16_t length, uint32_t &configCompleteId)
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
            uint32_t value = 0;
            uint8_t shift = 0;
            bool complete = false;
            while (offset < length)
            {
                uint8_t b = payload[offset];
                offset = offset + 1;
                value = value | (((uint32_t)(b & 0x7F)) << shift);
                shift = shift + 7;
                if ((b & 0x80) == 0) { complete = true; break; }
            }
            if (complete == false) { return false; }
            if (fieldNumber == FROMRADIO_FIELD_CONFIG_COMPLETE_ID)
            {
                configCompleteId = value;
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
        bool found = extractConfigCompleteId(framePayload, frameLength, configCompleteId);
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
    bool built = meshBuildEnvironmentTelemetryToRadio(telemetryBuffer, sizeof(telemetryBuffer), telemetryLength,
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
    Serial.println(F("[MeshLink] Telemetrie ecrite sur l'UART, attente d'une reponse du T114..."));
    if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
    logEvent(F("Telemetrie ecrite vers Meshtastic, attente reponse"));

    // Alimentation VOLONTAIREMENT maintenue (voir MeshLink.h) : ecrire les
    // octets sur l'UART ne signifie pas que le T114 a fini de les emettre
    // sur le reseau radio. La coupure se fait dans meshLinkUpdate(), sur
    // reponse recue ou sur timeout.
    meshWaitingForAck = true;
    meshAckWaitStartMillis = millis();
    return true;
}

void meshLinkUpdate()
{
    if (meshWaitingForAck == false)
    {
        return;
    }

    // Vide et journalise ce qui arrive (utile pour le diagnostic, voir
    // DEBUG_MESH), mais NE DECLENCHE PLUS la coupure d'alimentation sur la
    // simple presence d'octets - voir Config.h : MESH_TX_HOLD_MS pour
    // l'explication complete (analyse d'un echange reel montrant que le
    // premier octet recu n'etait pas un accuse lie a notre paquet, mais un
    // fragment de FromRadio.config sans rapport, en cours de dialogue de
    // configuration independant).
    if (Serial2.available() > 0)
    {
#if DEBUG_MESH
        uint8_t rxBuffer[64];
        size_t  rxLength = 0;
        while ((Serial2.available() > 0) && (rxLength < sizeof(rxBuffer)))
        {
            rxBuffer[rxLength] = (uint8_t)Serial2.read();
            rxLength = rxLength + 1;
        }
        debugPrintFrame("RX (pendant l'attente, non attribuable a notre paquet)", rxBuffer, rxLength);
#else
        while (Serial2.available() > 0)
        {
            Serial2.read();
        }
#endif
    }

    unsigned long holdElapsed = millis() - meshAckWaitStartMillis;
    if (holdElapsed >= MESH_TX_HOLD_MS)
    {
        Serial.println(F("[MeshLink] Delai ecoule (voir MESH_TX_HOLD_MS) : coupure de l'alimentation Mesh"));
        if (Serial) { Serial.flush(); }   // jamais de flush() sans hote connecte (voir DataLogger.cpp, premiere occurrence)
        logEvent(F("Fin de la fenetre d'envoi Meshtastic"));
        meshShutdown();
    }
}

#endif // USE_MESHTASTIC
