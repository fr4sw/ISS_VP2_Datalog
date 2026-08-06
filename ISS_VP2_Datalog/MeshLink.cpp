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

static void sendFramedToRadio(const uint8_t *toRadioBytes, size_t length)
{
    Serial2.write(MESHTASTIC_FRAME_START1);
    Serial2.write(MESHTASTIC_FRAME_START2);
    Serial2.write((uint8_t)((length >> 8) & 0xFF));
    Serial2.write((uint8_t)(length & 0xFF));
    Serial2.write(toRadioBytes, length);
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
    while ((millis() - handshakeStartMillis) < MESH_HANDSHAKE_SETTLE_MS)
    {
        while (Serial2.available() > 0)
        {
            Serial2.read();   // vidange volontaire, non interpretee (voir MeshLink.h)
        }
    }
}

bool meshLinkSendEnvironmentTelemetry(uint32_t utcUnixTime,
                                       float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                       uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                       float rainfall1hMm)
{
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
                                                        windDirectionDeg, windSpeedKph, windGustKph, rainfall1hMm);

    bool sendSuccess = false;
    if (built == false)
    {
        Serial.println(F("[MeshLink] Erreur : message de telemetrie trop volumineux pour le tampon"));
        logEvent(F("Erreur : message Meshtastic trop volumineux"));
    }
    else
    {
        sendFramedToRadio(telemetryBuffer, telemetryLength);
        Serial.println(F("[MeshLink] Telemetrie envoyee vers Meshtastic"));
        logEvent(F("Telemetrie envoyee vers Meshtastic"));
        sendSuccess = true;
    }

    power.disableMesh();
    sharedUartRelease(SHARED_UART_MESH);

    return sendSuccess;
}

#endif // USE_MESHTASTIC
