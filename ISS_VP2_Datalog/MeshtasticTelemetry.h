// ============================================================================
// Fichier   : MeshtasticTelemetry.h
// Rôle      : Connaissance des messages protobuf Meshtastic necessaires pour
//             emettre une trame de telemetrie environnementale (ToRadio
//             contenant un MeshPacket contenant un Data de type
//             TELEMETRY_APP contenant un Telemetry.environment_metrics).
//             Construit les octets a envoyer via MeshLink, en s'appuyant
//             uniquement sur ProtobufWriter (aucune dependance a nanopb ou
//             a du code protobuf genere : implementation choisie
//             volontairement pour rester simple et lisible sans outillage
//             de generation, au prix de ne couvrir que les champs utiles a
//             ce projet - a etendre au meme endroit si besoin).
// Fonctions : meshBuildEnvironmentTelemetryToRadio() - construit un message
//             ToRadio complet, pret a etre envoye tel quel par MeshLink
//             (qui se charge de l'entete de trame START1/START2/longueur).
//             meshBuildWantConfigToRadio() - construit le message ToRadio
//             de "poignee de main" initiale (voir MeshLink.cpp).
// Référence : Tous les numeros de champs ci-dessous sont copies depuis les
//             fichiers .proto officiels du projet Meshtastic (depot
//             meshtastic/protobufs, miroir consultable sur
//             https://buf.build/meshtastic/protobufs/), lus le
//             2026-08-04 sur les branches main/master :
//               - meshtastic/mesh.proto      (Data, MeshPacket, ToRadio)
//               - meshtastic/portnums.proto  (PortNum, TELEMETRY_APP = 67)
//               - meshtastic/telemetry.proto (Telemetry, EnvironmentMetrics)
//             Ces numeros de champ font partie du contrat public du
//             protocole Meshtastic (compatibilite descendante garantie par
//             le projet) : ils ne changent normalement pas, mais si de
//             nouvelles trames restent illisibles par l'appareil, verifier
//             en premier que ces fichiers n'ont pas evolue entre-temps.
// ============================================================================
#pragma once
#include <Arduino.h>

// --- meshtastic/mesh.proto : message ToRadio (client -> radio) ---
#define TORADIO_FIELD_PACKET           1   // MeshPacket a emettre sur le mesh
#define TORADIO_FIELD_WANT_CONFIG_ID   3   // uint32, demarre la "poignee de main"

// --- meshtastic/mesh.proto : message MeshPacket ---
// --- meshtastic/mesh.proto : message MeshPacket ---
// ATTENTION : le champ FROM (1) etait absent d'un premier jet de ce code -
// bug releve par l'utilisateur (verification independante sur un vrai
// T114) : sans lui, le firmware ne peut pas attribuer/router le paquet et
// ne l'emet tout simplement jamais sur le reseau radio. TOUJOURS l'ecrire,
// meme a 0 (voir meshBuildEnvironmentTelemetryToRadio() : c'est la
// convention des clients officiels - la valeur reelle du nœud est
// substituee par le firmware lui-meme a la reception depuis le port
// serie local, le client n'a pas besoin de connaitre son propre node ID).
#define MESHPACKET_FIELD_FROM       1   // fixed32, 0 = laisse le firmware substituer
                                         // le node ID reel du port serie local
#define MESHPACKET_FIELD_TO         2   // fixed32, destinataire (broadcast ci-dessous)
#define MESHPACKET_FIELD_CHANNEL    3   // uint32, index de canal (0 = canal principal)
#define MESHPACKET_FIELD_DECODED    4   // Data en clair (vs "encrypted", non utilise ici)
#define MESHPACKET_FIELD_WANT_ACK   10  // bool

#define MESHTASTIC_BROADCAST_ADDR   0xFFFFFFFFUL   // ^tous les nœuds (mesh.proto)

// --- meshtastic/mesh.proto : message Data ---
#define DATA_FIELD_PORTNUM   1   // PortNum (enum, encode en varint)
#define DATA_FIELD_PAYLOAD   2   // bytes (ici : un Telemetry serialise)

// --- meshtastic/portnums.proto : enum PortNum ---
#define PORTNUM_TELEMETRY_APP   67

// --- meshtastic/telemetry.proto : message Telemetry ---
#define TELEMETRY_FIELD_TIME                  1   // fixed32, secondes UTC (epoque Unix)
#define TELEMETRY_FIELD_ENVIRONMENT_METRICS   3   // EnvironmentMetrics (voir ci-dessous)

// --- meshtastic/telemetry.proto : message EnvironmentMetrics ---
// https://buf.build/meshtastic/protobufs/docs/master%3Ameshtastic#meshtastic.EnvironmentMetrics
// Champs vent/pluie presents nativement (ajoutes pour les stations meteo) :
// exactement ce dont ce projet a besoin, pas de detournement de champ.
// autres champs potentiellement utiles : 
// 7 IAQ (BME680) indice de qualité de l'air
// 9 Lux, 10 White Lux, 11 IR Lux, 12 UV lux
// 17 wind lull (vent calme)
// 18 Radiation
// 20 Pluie dernières 24h -> à implémenter
// 21 Sol : humidité, 22 Sol temperature

#define ENV_FIELD_TEMPERATURE           1   // float, °C
#define ENV_FIELD_RELATIVE_HUMIDITY     2   // float, %
#define ENV_FIELD_BAROMETRIC_PRESSURE   3   // float, hPa
#define ENV_FIELD_WIND_DIRECTION       13   // uint32, degres (0-359)
#define ENV_FIELD_WIND_SPEED           14   // float, m/s (PAS km/h : conversion a la charge de l'appelant)
#define ENV_FIELD_WIND_GUST            16   // float, m/s
#define ENV_FIELD_RAINFALL_1H          19   // float, mm
#define ENV_FIELD_RAINFALL_24H         20   // float, mm - implemente (voir "a implementer" ci-dessus,
                                             // desormais fait : cumul depuis minuit, voir DataLogger.cpp)

// Conversion vitesse du vent : le projet calcule en km/h (DataLogger),
// Meshtastic attend du m/s (EnvironmentMetrics.wind_speed/wind_gust).
#define KPH_TO_MPS   (1.0f / 3.6f)

// Construit le message ToRadio de telemetrie environnementale dans
// outputBuffer. Renvoie true si tout a tenu dans outputCapacity (règle 23 :
// un echec doit etre detectable, jamais un debordement silencieux).
bool meshBuildEnvironmentTelemetryToRadio(uint8_t *outputBuffer, size_t outputCapacity, size_t &outputLength,
                                           uint32_t utcUnixTime,
                                           float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                           uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                           float rainfall1hMm, float rainfall24hMm);

// Construit le message ToRadio "want_config_id" (poignee de main initiale,
// voir MeshLink.cpp). configId est une valeur arbitraire non nulle choisie
// par le client, renvoyee telle quelle par la radio dans FromRadio pour
// signaler la fin de l'envoi de sa configuration (non exploite dans ce
// premier jet, voir MeshLink.h).
bool meshBuildWantConfigToRadio(uint8_t *outputBuffer, size_t outputCapacity, size_t &outputLength, uint32_t configId);
