// ============================================================================
// Fichier   : Gps.h
// Rôle      : Acquisition d'un point date/heure GPS (NMEA, sur Serial2 -
//             UARTE1, materiel independant du RS485 - voir HalPins.cpp),
//             utilisee par TimeManager pour les modes TIME_MODE_GPS_RTC et
//             TIME_MODE_GPS_ONLY. N'est utilise que par TimeManager
//             (règle 102) : le reste du programme ne connait jamais la
//             source de l'heure.
// Fonctions : gpsBegin()        - alimente le GPS (module Power, règle 101)
//                                 et demarre une premiere acquisition.
//             gpsUpdate()       - a appeler dans loop(). Machine a etats
//                                 non bloquante :
//                                   GPS_STATE_ACQUIRING : lit le flux NMEA,
//                                     jusqu'a obtenir un point valide avec
//                                     au moins GPS_MINIMUM_SATELLITES
//                                     satellites, ou expiration de
//                                     GPS_TIMEOUT (Config.h).
//                                   GPS_STATE_WAITING : GPS coupe (economie
//                                     d'energie), jusqu'a la prochaine
//                                     resynchronisation programmee
//                                     (GPS_RTC_RESYNC_INTERVAL_MS ou
//                                     GPS_ONLY_RESYNC_INTERVAL_MS, Config.h,
//                                     selon TIME_MODE).
//                                 En TIME_MODE_GPS_RTC, un point valide est
//                                 ecrit directement dans le RTC (Rtc.h) :
//                                 le RTC reste l'unique source lue par
//                                 TimeManager::now(), le GPS ne fait que le
//                                 recaler periodiquement.
//                                 En TIME_MODE_GPS_ONLY (pas de RTC), le
//                                 point est conserve en RAM et complete par
//                                 millis() ecoule jusqu'au point suivant
//                                 (voir gpsNow()).
//             gpsNow()          - (TIME_MODE_GPS_ONLY uniquement) estimation
//                                 courante de l'heure UTC, sous forme d'un
//                                 compteur de secondes depuis l'epoque Unix
//                                 (coherent avec Rtc::rtcNow()), a partir du
//                                 dernier point GPS obtenu, complete par
//                                 millis() ecoule. Renvoie false si aucun
//                                 point n'a encore ete obtenu depuis le
//                                 demarrage.
// Dépendances : Bibliotheque "TinyGPSPlus" (Mikal Hart, Arduino Library
//             Manager) : parseur NMEA eprouve, evite d'ecrire un parseur
//             "maison" (règle 15 : pas de code intelligent).
// Référence : Trames NMEA 0183 (RMC/GGA), format standard.
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

bool gpsBegin();
void gpsUpdate();

#if TIME_MODE == TIME_MODE_GPS_ONLY
bool gpsNow(uint32_t &utcUnixTime);
#endif
