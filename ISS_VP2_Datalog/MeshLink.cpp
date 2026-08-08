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

// Valeur arbitraire non nulle pour want_config_id (voir MeshtasticTelemetry.h) :
// non exploitee en retour dans ce premier jet, juste non nulle comme l'exige
// le protocole.
static const uint32_t MESH_WANT_CONFIG_NONCE = 1;

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

// Envoie la "poignee de main" et vide ce que renvoie le T114 pendant
// MESH_HANDSHAKE_SETTLE_MS (Config.h), sans l'interpreter (voir MeshLink.h).
// Attente bornee et courte : acceptable ici car Serial2 est un UART
// materiel independant du RS485 (Serial1) - elle ne retarde jamais la
// reception des trames ISS. A remplacer par une machine a etats non
// bloquante (comme Gps.cpp) si cette hypothese se revelait fausse a
// l'usage (règle 26 : optimiser apres validation fonctionnelle, pas avant).
static void performHandshake()
{
    uint8_t handshakeBuffer[8];
    size_t handshakeLength = 0;
    bool handshakeBuilt = meshBuildWantConfigToRadio(handshakeBuffer, sizeof(handshakeBuffer), handshakeLength, MESH_WANT_CONFIG_NONCE);
    if (handshakeBuilt == true)
    {
        sendFramedToRadio(handshakeBuffer, handshakeLength);
    }

    unsigned long handshakeStartMillis = millis();
#if DEBUG_MESH
    uint8_t rxBuffer[64];
    size_t  rxLength = 0;
#endif
    while ((millis() - handshakeStartMillis) < MESH_HANDSHAKE_SETTLE_MS)
    {
        while (Serial2.available() > 0)
        {
            uint8_t receivedByte = (uint8_t)Serial2.read();   // vidange volontaire, non interpretee (voir MeshLink.h)
#if DEBUG_MESH
            if (rxLength < sizeof(rxBuffer))
            {
                rxBuffer[rxLength] = receivedByte;
                rxLength = rxLength + 1;
            }
#else
            (void)receivedByte;
#endif
        }
    }
#if DEBUG_MESH
    if (rxLength > 0)
    {
        debugPrintFrame("RX (handshake, non interprete)", rxBuffer, rxLength);
    }
#endif
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
        return false;
    }

    bool busAcquired = sharedUartAcquire(SHARED_UART_MESH);
    if (busAcquired == false)
    {
        Serial.println(F("[MeshLink] Bus partage occupe (GPS) : envoi differe au prochain creneau"));
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
        logEvent(F("Erreur : message Meshtastic trop volumineux"));
        meshShutdown();
        return false;
    }

    sendFramedToRadio(telemetryBuffer, telemetryLength);
    Serial.println(F("[MeshLink] Telemetrie ecrite sur l'UART, attente d'une reponse du T114..."));
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

    if (Serial2.available() > 0)
    {
        // Accuse de reception minimal (voir MeshLink.h - LIMITE ASSUMEE) :
        // n'importe quel octet recu du T114 est considere comme la preuve
        // qu'il est bien en train de repondre, sans decoder le FromRadio.
#if DEBUG_MESH
        uint8_t rxBuffer[64];
        size_t  rxLength = 0;
        while ((Serial2.available() > 0) && (rxLength < sizeof(rxBuffer)))
        {
            rxBuffer[rxLength] = (uint8_t)Serial2.read();
            rxLength = rxLength + 1;
        }
        debugPrintFrame("RX (reponse au ToRadio.packet)", rxBuffer, rxLength);
#else
        while (Serial2.available() > 0)
        {
            Serial2.read();
        }
#endif
        Serial.println(F("[MeshLink] Reponse recue du T114 : coupure de l'alimentation Mesh"));
        logEvent(F("Reponse Meshtastic recue"));
        meshShutdown();
        return;
    }

    unsigned long ackWaitElapsed = millis() - meshAckWaitStartMillis;
    if (ackWaitElapsed >= MESH_ACK_TIMEOUT_MS)
    {
        Serial.println(F("[MeshLink] Timeout sans reponse du T114 : coupure de l'alimentation Mesh"));
        logEvent(F("Timeout reponse Meshtastic"));
        meshShutdown();
    }
}

#endif // USE_MESHTASTIC
