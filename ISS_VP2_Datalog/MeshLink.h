// ============================================================================
// Fichier   : MeshLink.h
// Rôle      : Liaison serie avec un appareil Meshtastic (T114) branche sur
//             les broches GPIO du bus partage (Serial2/UARTE1, meme
//             principe que Gps.h : MESH_ENABLE commute le CD4053 sur le
//             T114 au lieu du GPS). Utilise "a la maniere d'un modem" (terme
//             de l'utilisateur) : chaque envoi ouvre la liaison, parle,
//             referme - pas de connexion permanente a maintenir.
// Fonction  : meshLinkSendEnvironmentTelemetry() - envoie un message de
//             telemetrie environnementale (temperature/humidite/pression/
//             vent/pluie) vers le mesh, encapsule dans les messages
//             protobuf Meshtastic adequats (voir MeshtasticTelemetry.h).
//             Renvoie false si le bus partage etait occupe par le GPS
//             (voir SharedUart.h) ou si le message etait trop volumineux :
//             dans les deux cas, rien n'est envoye, l'appelant peut
//             reessayer au prochain creneau de transmission.
// Sequence  : 1) reservation du bus partage (SharedUart) ;
//             2) mise sous/en route du T114 (Power::enableMesh()) ;
//             3) "poignee de main" minimale : ToRadio.want_config_id, pour
//                que le T114 cesse d'emettre du texte de debug brut sur ce
//                port et parle protobuf (voir doc citee dans MeshLink.cpp).
//                ATTENTION - LIMITE ASSUMEE DE CE PREMIER JET : la reponse
//                (FromRadio, configuration/etat du nœud) n'est PAS
//                interpretee, seulement videe du tampon de reception. A
//                verifier sur le materiel reel : le T114 accepte-t-il un
//                ToRadio.packet immediatement apres la poignee de main sans
//                qu'on ait lu sa reponse complete ? Si des paquets sont
//                perdus, c'est le premier point a instrumenter.
//             4) envoi du message de telemetrie (ToRadio.packet) ;
//             5) coupure et liberation du bus partage.
// Référence : Format de trame (START1/START2/longueur) et sequence de
//             connexion : https://meshtastic.org/docs/development/device/client-api/
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

#if USE_MESHTASTIC
bool meshLinkSendEnvironmentTelemetry(uint32_t utcUnixTime,
                                       float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                       uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                       float rainfall1hMm);
#endif
