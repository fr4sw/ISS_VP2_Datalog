// ============================================================================
// Fichier   : MeshLink.h
// Rôle      : Liaison serie avec un appareil Meshtastic (T114) branche sur
//             les broches GPIO du bus partage (Serial2/UARTE1, meme
//             principe que Gps.h : MESH_ENABLE commute le CD4053 sur le
//             T114 au lieu du GPS). Utilise "a la maniere d'un modem" (terme
//             de l'utilisateur) : chaque envoi ouvre la liaison, parle,
//             attend une reponse, referme - pas de connexion permanente a
//             maintenir.
// Fonctions : meshLinkSendEnvironmentTelemetry() - INITIE l'envoi d'un
//             message de telemetrie environnementale (temperature/humidite/
//             pression/vent/pluie), encapsule dans les messages protobuf
//             Meshtastic adequats (voir MeshtasticTelemetry.h). Renvoie
//             false sans rien envoyer si le bus partage etait occupe (GPS,
//             voir SharedUart.h), si un envoi precedent est encore en
//             attente de reponse (voir plus bas), ou si le message etait
//             trop volumineux. Renvoyer true signifie seulement que la
//             trame a ete ECRITE sur l'UART - PAS qu'elle a ete recue ni
//             emise par le T114 sur le reseau radio (voir meshLinkUpdate()).
//             meshLinkUpdate() - A APPELER A CHAQUE loop() (meme principe
//             que gpsUpdate()). Maintient l'alimentation du T114 apres un
//             envoi et attend une reponse (n'importe quel octet recu vaut
//             accuse de reception - le contenu n'est pas interprete, voir
//             LIMITE ASSUMEE ci-dessous), jusqu'a MESH_ACK_TIMEOUT_MS
//             (Config.h, 3 min) : couper l'alimentation immediatement apres
//             l'ecriture ne laisse pas au T114 le temps reel de transmettre
//             sur le reseau radio. Non bloquant : n'attend jamais activement,
//             ne retarde donc jamais la reception RS485 (Serial1).
// Sequence  : 1) reservation du bus partage (SharedUart) ;
//             2) mise sous/en route du T114 (Power::enableMesh()) ;
//             3) "poignee de main" minimale : ToRadio.want_config_id, pour
//                que le T114 cesse d'emettre du texte de debug brut sur ce
//                port et parle protobuf (voir doc citee dans MeshLink.cpp).
//                ATTENTION - LIMITE ASSUMEE DE CE PREMIER JET : la reponse
//                (FromRadio, configuration/etat du nœud) n'est PAS
//                interpretee, seulement videe du tampon de reception. Idem
//                pour l'"accuse de reception" attendu par meshLinkUpdate() :
//                n'importe quel octet recu est considere comme une preuve
//                d'activite suffisante, le contenu exact du FromRadio n'est
//                pas decode. A ameliorer si necessaire une fois le lien
//                valide sur le materiel reel (règle 26).
//             4) envoi du message de telemetrie (ToRadio.packet) ;
//             5) ALIMENTATION MAINTENUE, attente d'une reponse (ou timeout) ;
//             6) coupure (Serial2.end() puis Power::disableMesh()) et
//                liberation du bus partage.
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
void meshLinkUpdate();
#endif
