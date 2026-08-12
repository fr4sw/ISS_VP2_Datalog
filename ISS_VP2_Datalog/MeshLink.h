// ============================================================================
// Fichier   : MeshLink.h
// Rôle      : Liaison serie avec un appareil Meshtastic (T114) branche sur
//             les broches GPIO du bus partage (Serial2/UARTE1, meme
//             principe que Gps.h : MESH_ENABLE commute le CD4053 sur le
//             T114 au lieu du GPS). Utilise "a la maniere d'un modem" (terme
//             de l'utilisateur) : chaque envoi ouvre la liaison, parle,
//             attend une confirmation, referme - pas de connexion permanente
//             a maintenir.
//             PERIMETRE : toute la connaissance PROTOCOLE DE SESSION avec la
//             radio (constantes de champ ToRadio/FromRadio liees a la
//             poignee de main et a la file d'emission, sequence
//             d'etablissement/fermeture) vit ICI - pas dans
//             MeshtasticTelemetry.h, qui ne connait QUE la structure du
//             message de telemetrie lui-meme (organisation revue suite a
//             une remarque justifiee de l'utilisateur : les deux etaient
//             melangees dans un premier jet).
// Fonctions : meshLinkSendEnvironmentTelemetry() - INITIE l'envoi d'un
//             message de telemetrie environnementale (temperature/humidite/
//             pression/vent/pluie), encapsule dans les messages protobuf
//             Meshtastic adequats (voir MeshtasticTelemetry.h). Renvoie
//             false sans rien envoyer si le bus partage etait occupe (GPS,
//             voir SharedUart.h), si un envoi precedent est encore en
//             cours (voir plus bas), ou si le message etait trop
//             volumineux. Renvoyer true signifie seulement que la trame a
//             ete ECRITE sur l'UART - PAS qu'elle a ete emise par le T114
//             sur le reseau radio (voir meshLinkUpdate()).
//             meshLinkUpdate() - A APPELER A CHAQUE loop() (meme principe
//             que gpsUpdate()). Maintient l'alimentation du T114 apres un
//             envoi, guette une confirmation FromRadio.queue_status
//             specifiquement liee a NOTRE paquet (voir MESHPACKET_FIELD_ID),
//             et coupe l'alimentation soit peu apres cette confirmation
//             (MESH_POST_QUEUE_HOLD_MS, marge pour l'emission radio
//             elle-meme), soit au bout de MESH_TX_HOLD_MS (Config.h) en
//             filet de securite si aucune confirmation n'arrive. Non
//             bloquant : n'attend jamais activement, ne retarde donc
//             jamais la reception RS485 (Serial1).
// Historique : un premier jet coupait l'alimentation des le premier octet
//             recu en reponse, en le traitant a tort comme un accuse de
//             reception generique. Analyse d'un echange reel (utilisateur +
//             outillage tiers de decodage protobuf) : cet octet etait en
//             realite un fragment de FromRadio.config, sans aucun rapport
//             avec notre paquet - la coupure intervenait ~47ms apres
//             l'ecriture, bien trop tot pour une emission LoRa reelle.
//             MEME chose decouverte pour la poignee de main : un delai fixe
//             de 250ms etait utilise "a l'aveugle" plutot que d'attendre le
//             vrai signal de fin (FromRadio.config_complete_id).
//             ATTENTION - LIMITE ASSUMEE DE CE JET : les numeros de champ
//             ci-dessous pour QueueStatus et FromRadio.queue_status sont
//             une hypothese raisonnable (voir commentaires), PAS verifies
//             octet par octet sur un vrai T114 comme cela a ete fait pour
//             config_complete_id (voir FROMRADIO_FIELD_CONFIG_COMPLETE_ID).
//             Si meshLinkUpdate() ne detecte jamais de QueueStatus
//             correspondant (log "queue_status jamais recu"), c'est le
//             premier endroit a verifier - le filet de securite
//             MESH_TX_HOLD_MS continue de s'appliquer dans ce cas, donc
//             aucune regression fonctionnelle si ces numeros sont faux,
//             juste une perte de l'optimisation.
// Sequence  : 1) reservation du bus partage (SharedUart) ;
//             2) mise sous/en route du T114 (Power::enableMesh()) ;
//             3) poignee de main : ToRadio.want_config_id (valeur
//                MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG), attente du vrai
//                FromRadio.config_complete_id en retour (voir
//                performHandshake() dans MeshLink.cpp) ;
//             4) envoi du message de telemetrie (ToRadio.packet), memorisation
//                de l'ID de paquet attribue (voir MeshtasticTelemetry.h) ;
//             5) ALIMENTATION MAINTENUE, attente du FromRadio.queue_status
//                correspondant a cet ID (ou timeout) ;
//             6) coupure (Serial2.end() puis Power::disableMesh()) et
//                liberation du bus partage.
// Référence : Format de trame (START1/START2/longueur) et sequence de
//             connexion : https://meshtastic.org/docs/development/device/client-api/
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

// --- meshtastic/mesh.proto : message ToRadio (client -> radio) ---
#define TORADIO_FIELD_PACKET           1   // MeshPacket a emettre sur le mesh (utilise
                                            // aussi par MeshtasticTelemetry.cpp)
#define TORADIO_FIELD_WANT_CONFIG_ID   3   // uint32, demarre la "poignee de main"

// Valeurs SPECIALES de want_config_id (donc de FromRadio.config_complete_id
// en retour, qui l'echo tel quel) - decouvertes par analyse d'un echange
// reel sur T114 (confirmees par l'utilisateur, PAS documentees dans les
// commentaires des .proto officiels a notre connaissance) : le firmware
// bifurque son comportement selon la valeur exacte envoyee, ce n'est PAS
// un simple jeton opaque comme suppose a tort dans un premier jet.
//   SPECIAL_NONCE_ONLY_CONFIG (69420) -> renvoie la config (device/module/
//     canaux) SANS la base de nœuds (NodeInfo*N) - plus rapide, c'est ce
//     qu'on veut ici (on n'exploite pas la NodeDB).
//   SPECIAL_NONCE_ONLY_DB (69421) -> l'inverse : NodeDB SEULEMENT, sans la
//     config.
//   Toute AUTRE valeur -> comportement non caracterise ici (vraisemblablement
//     config + NodeDB completes, plus lent) - a eviter sans raison precise.
#define MESHTASTIC_SPECIAL_NONCE_ONLY_CONFIG   69420UL
#define MESHTASTIC_SPECIAL_NONCE_ONLY_DB       69421UL

// --- meshtastic/mesh.proto : message FromRadio (radio -> client) ---
// Seuls les CHAMPS DONT CE PROJET A L'USAGE REEL sont listes ici (règle 15).
//
// config_complete_id : VERIFIE octet par octet sur un vrai T114 (voir
// l'historique ci-dessus) - fiable a 100%. Echo la valeur de
// want_config_id envoyee, une fois la sequence de config terminee.
#define FROMRADIO_FIELD_CONFIG_COMPLETE_ID   7   // uint32, echo de want_config_id
//
// queue_status : HYPOTHESE, PAS verifiee octet par octet (voir "LIMITE
// ASSUMEE" ci-dessus) - numero de champ deduit par analogie avec l'ordre
// probable du oneof payload_variant de FromRadio (config_complete_id, dont
// on est sur, est en position 7 ; queue_status est generalement documente
// comme intervenant apres la sequence de config initiale dans les echanges
// releves par l'utilisateur). A CONFIRMER avec les octets bruts d'un
// FromRadio.queue_status reel si meshLinkUpdate() ne le detecte jamais.
#define FROMRADIO_FIELD_QUEUE_STATUS         10   // QueueStatus (sous-message, voir ci-dessous)

// --- meshtastic/mesh.proto : message QueueStatus ---
// Structure rapportee par l'utilisateur (outillage tiers de decodage) :
// QueueStatus { res, free, maxlen, mesh_packet_id }. Numeros de champ =
// HYPOTHESE (ordre de declaration le plus probable), meme reserve que
// FROMRADIO_FIELD_QUEUE_STATUS ci-dessus. Seul mesh_packet_id nous
// interesse ici (identifier NOTRE paquet, pas exploiter free/maxlen pour
// l'instant).
#define QUEUESTATUS_FIELD_MESH_PACKET_ID     4   // uint32/fixed32, ID du paquet concerne

#if USE_MESHTASTIC
bool meshLinkSendEnvironmentTelemetry(uint32_t utcUnixTime,
                                       float temperatureC, float relativeHumidityPercent, float pressureHpa,
                                       uint16_t windDirectionDeg, float windSpeedKph, float windGustKph,
                                       float rainfall1hMm, float rainfall24hMm);
void meshLinkUpdate();
#endif
