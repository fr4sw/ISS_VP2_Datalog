// ============================================================================
// Fichier   : MeshLink.h
// Role      : Gestion de la liaison serie avec le T114 Meshtastic et de la
//             machine a etats de transmission.
//
// Sequence de session :
//   1) reservation du bus partage ;
//   2) alimentation du T114 et attente MESHPOWERONMS ;
//   3) envoi d'un Heartbeat de reveil ;
//   4) attente d'un queue_status : seul free == 0 bloque l'envoi ; si aucun
//      queue_status n'arrive avant MESH_WAKE_QUEUE_TIMEOUT_MS, envoi quand
//      meme ;
//   5) envoi de la telemetrie et memorisation de son packet_id ;
//   6) lancement du timeout MESH_TX_TIMEOUT_MS et des heartbeats periodiques
//      MESH_HEARTBEAT_INTERVAL_MS ;
//   7) quand un queue_status montre que free a diminue par rapport a la
//      valeur observee juste avant notre envoi, le paquet est marque comme
//      present dans la queue ;
//   8) quand un queue_status ulterieur indique free == maxlen, la queue est
//      consideree vide et l'arret est programme MESH_QUEUE_EMPTY_SHUTDOWN_MS
//      plus tard, sans autre condition ;
//   9) si le timeout global expire avant cela, arret immediat.
//
// La machine est non bloquante apres le retour de
// meshLinkSendEnvironmentTelemetry(). meshLinkUpdate() doit etre appelee a
// chaque passage dans loop().
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

// --- meshtastic/mesh.proto : message ToRadio (client -> radio) ---
#define TORADIO_FIELD_PACKET           1   // MeshPacket a emettre (utilise aussi
                                            // par MeshtasticTelemetry.cpp)
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

// Le nonce est conserve globalement dans MeshLink.cpp et incremente a
// chaque Heartbeat. Il commence a 2 et ne peut jamais revenir a 0 ou 1.

// --- meshtastic/mesh.proto : message FromRadio (radio -> client) ---
// Seuls les champs dont ce projet a l'usage reel sont listes ici (règle 15).
// Les deux VERIFIES octet par octet sur un vrai T114 (fiables a 100%) :
#define FROMRADIO_FIELD_QUEUE_STATUS         11   // QueueStatus (sous-message, voir ci-dessous)

// --- meshtastic/mesh.proto : message QueueStatus ---
// Structure et numeros de champ VERIFIES octet par octet sur un vrai T114 :
// QueueStatus { free (2), maxlen (3), mesh_packet_id (4, varint - PAS
// fixed32, contrairement a MeshPacket.id) }. Le champ 4 reste decode s'il
// est present pour diagnostic/evolution, mais le handshake simplifie par
// Heartbeat ne depend plus de sa presence. Un QueueStatus ou free == maxlen
// signale une file entierement vide.
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
