// ============================================================================
// Fichier   : SharedUart.h
// Rôle      : Arbitrage du bus UART partage par le GPS et le lien Meshtastic
//             (Serial2/UARTE1, meme broches, commutees par CD4053 selon
//             GPS_ENABLE/MESH_ENABLE - voir HalPins.cpp/Power.cpp). Les deux
//             ne peuvent pas etre actifs en meme temps sur le meme fil.
// Fonctions : sharedUartAcquire()      - reserve le bus pour un usage donne.
//                                        Renvoie false si deja pris par
//                                        l'AUTRE usage (l'appelant doit
//                                        alors reessayer plus tard, pas
//                                        forcer).
//             sharedUartRelease()      - libere le bus, seulement si
//                                        l'appelant en etait bien le
//                                        detenteur (evite qu'un module
//                                        libere par erreur la reservation
//                                        de l'autre).
//             sharedUartCurrentOwner() - etat courant, pour affichage/DEBUG.
// Remarque  : Le GPS a la priorite dans les faits (ses fenetres d'usage sont
//             rares - Config.h : GPS_RTC_RESYNC_INTERVAL_MS/
//             GPS_ONLY_RESYNC_INTERVAL_MS - mais importantes pour la
//             precision de l'horodatage) ; Mesh doit simplement reessayer
//             au creneau de transmission suivant s'il trouve le bus occupe.
// ============================================================================
#pragma once
#include <Arduino.h>

enum SharedUartOwner
{
    SHARED_UART_NONE,
    SHARED_UART_GPS,
    SHARED_UART_MESH
};

bool sharedUartAcquire(SharedUartOwner owner);
void sharedUartRelease(SharedUartOwner owner);
SharedUartOwner sharedUartCurrentOwner();
