// ============================================================================
// Fichier   : MeshLink.h
// Rôle      : Liaison serie avec un appareil Meshtastic (T114) branche sur
//             les broches GPIO du bus partage (Serial2/UARTE1, meme
//             principe que Gps.h : MESH_ENABLE commute le CD4053 sur le
//             T114 au lieu du GPS). Utilise "a la maniere d'un modem" : chaque
//             envoi ouvre la liaison, parle, attend une confirmation, referme
//             - pas de connexion permanente a maintenir.
//             PERIMETRE : toute la connaissance PROTOCOLE DE SESSION avec la
//             radio (poignee de main, file d'emission) vit ICI. La structure
//             du message de telemetrie lui-meme est dans MeshtasticTelemetry.h,
//             qui n'a pas besoin de connaitre la mecanique de session.
// Fonctions : meshLinkSendEnvironmentTelemetry() - INITIE l'envoi d'un
//             message de telemetrie environnementale, encapsule dans les
//             messages protobuf Meshtastic adequats (voir
//             MeshtasticTelemetry.h). Renvoie false sans rien envoyer si le
//             bus partage etait occupe (GPS, voir SharedUart.h), si un envoi
//             precedent est encore en cours, ou si le message etait trop
//             volumineux. Renvoyer true signifie seulement que la trame a
//             ete ECRITE sur l'UART - PAS qu'elle a ete emise sur le reseau
//             radio (voir meshLinkUpdate()).
//             meshLinkUpdate() - A APPELER A CHAQUE loop() (meme principe que
//             gpsUpdate()). Maintient l'alimentation du T114 apres un envoi
//             et guette la confirmation de son sort via FromRadio.queue_status,
//             par ordre de fiabilite croissante :
//               1) notre paquet vu ENTRER dans la file d'emission (son ID y
//                  apparait) -> reduit l'attente a MESH_POST_QUEUE_HOLD_MS ;
//               2) la file redevient VIDE (free == maxlen) APRES l'avoir vu y
//                  entrer -> confirmation forte qu'il en est reellement
//                  reparti (transmis ou abandonne) -> reduit encore l'attente
//                  a MESH_QUEUE_DRAIN_HOLD_MS ;
//               3) sinon, filet de securite MESH_TX_HOLD_MS (Config.h).
//             Non bloquant : n'attend jamais activement, ne retarde donc
//             jamais la reception RS485 (Serial1).
// Sequence  : 1) reservation du bus partage (SharedUart) ;
//             2) mise sous tension du T114 (Power::enableMesh()), attente
//                MESH_POWERON_SETTLE_MS (Config.h/Params) le temps qu'il
//                demarre sa pile logicielle ;
//             3) poignee de main : ToRadio.want_config_id (valeur
//                MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG), attente du vrai
//                FromRadio.config_complete_id en retour (performHandshake()) ;
//             4) envoi du message de telemetrie (ToRadio.packet), memorisation
//                de l'ID de paquet attribue (voir MeshtasticTelemetry.h) ;
//             5) ALIMENTATION MAINTENUE, suivi de queue_status comme decrit
//                ci-dessus ;
//             6) coupure (Serial2.end() puis Power::disableMesh()) et
//                liberation du bus partage.
// Référence : Format de trame (START1/START2/longueur) et sequence de
//             connexion : https://meshtastic.org/docs/development/device/client-api/
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

// --- meshtastic/mesh.proto : message ToRadio (client -> radio) ---
#define TORADIO_FIELD_PACKET           1   // MeshPacket a emettre (utilise aussi
                                            // par MeshtasticTelemetry.cpp)
#define TORADIO_FIELD_WANT_CONFIG_ID   3   // uint32, demarre la poignee de main
#define TORADIO_FIELD_HEARTBEAT        7   // Heartbeat (sous-message, voir ci-dessous)

// --- meshtastic/mesh.proto : message Heartbeat ---
// Structure VERIFIEE octet par octet par l'utilisateur : message Heartbeat
// { uint32 nonce = 1; }. Meshtastic dedoublonne les Heartbeat identiques
// (meme nonce, ou repeter le meme message vide) : un nonce DIFFERENT est
// OBLIGATOIRE a chaque envoi, sous peine que le message soit purement et
// simplement ignore. La valeur 1 a un sens special (force la diffusion du
// NodeInfo du nœud sur le mesh, hors-sujet ici) : le compteur utilise par
// ce projet demarre a 2 et saute 0/1 (voir MeshLink.cpp).
#define HEARTBEAT_FIELD_NONCE          1   // uint32 (varint) - JAMAIS 0 ni 1

// Valeurs SPECIALES de want_config_id (donc de FromRadio.config_complete_id
// en retour, qui l'echo tel quel) - le firmware bifurque son comportement
// selon la valeur exacte envoyee, ce n'est pas un simple jeton opaque :
//   SPECIAL_NONCE_ONLY_CONFIG (69420) -> config (device/module/canaux) SANS
//     la base de nœuds (NodeInfo*N) - plus rapide, la NodeDB n'est pas
//     exploitee ici, c'est ce qu'on veut.
//   SPECIAL_NONCE_ONLY_DB (69421) -> l'inverse, NodeDB seulement.
//   Toute autre valeur -> comportement non caracterise (vraisemblablement
//     config + NodeDB completes, plus lent) - a eviter sans raison precise.
#define MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG   69420UL
#define MESHTASTIC_SPECIAL_NONCE_ONLY_DB       69421UL

// --- meshtastic/mesh.proto : message Heartbeat ---
// VERIFIE sur le code source du firmware (gestionnaire ToRadio_heartbeat_tag,
// PhoneAPI.cpp) : le firmware se contente de logguer sa reception
// ("Got client heartbeat"), SANS repondre ni exploiter son contenu. Contenu
// donc sans importance - on envoie un sous-message VIDE (0 octet). Utile
// uniquement pour faire reconnaitre au firmware un flux protobuf valide
// (bien plus leger qu'un want_config_id, qui declenche un dump de
// configuration complet) - PAS pour obtenir une confirmation en retour,
// contrairement a ce qu'on pourrait esperer de son nom.

// --- meshtastic/mesh.proto : message FromRadio (radio -> client) ---
// Seuls les champs dont ce projet a l'usage reel sont listes ici (règle 15).
// Les deux VERIFIES octet par octet sur un vrai T114 (fiables a 100%) :
#define FROMRADIO_FIELD_CONFIG_COMPLETE_ID   7    // uint32 (varint), echo de want_config_id
#define FROMRADIO_FIELD_QUEUE_STATUS         11   // QueueStatus (sous-message, voir ci-dessous)

// --- meshtastic/mesh.proto : message QueueStatus ---
// Structure et numeros de champ VERIFIES octet par octet sur un vrai T114 :
// QueueStatus { free (2), maxlen (3), mesh_packet_id (4, varint - PAS
// fixed32, contrairement a MeshPacket.id) }. Un QueueStatus ou free ==
// maxlen (champ 4 absent) signale une file entierement vide.
#define QUEUESTATUS_FIELD_FREE               2    // uint32 (varint), places libres
#define QUEUESTATUS_FIELD_MAXLEN             3    // uint32 (varint), capacite totale
#define QUEUESTATUS_FIELD_MESH_PACKET_ID     4    // uint32 (varint), ID du paquet concerne

#if USE_MESHTASTIC
bool meshLinkSendEnvironmentTelemetry(uint32_t utcUnixTime,
                                       float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                       uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                       float rainfall1hMm, float rainfall24hMm);
void meshLinkUpdate();
#endif
