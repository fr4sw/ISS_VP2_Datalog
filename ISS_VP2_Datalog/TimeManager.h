// ============================================================================
// Fichier   : TimeManager.h
// Rôle      : Fournit l'heure courante selon UN SEUL mode configure dans
//             Config.h (TIME_MODE). Les modes sont mutuellement exclusifs :
//             GPS+RTC, RTC seul, GPS seul, ou manuel. Le reste du logiciel
//             n'appelle jamais directement le GPS, le RTC, ou millis()
//             pour dater un enregistrement.
// Fonctions : begin()  - initialise la source configuree par TIME_MODE.
//             update() - a appeler dans loop(), lit GPS/RTC si necessaire.
//             now()    - renvoie la date/heure courante, source masquee.
//                        useUtc=true : pas de decalage de fuseau applique
//                        (voir Params::getDataloggerUtc()/getMeshUtc()) ;
//                        useUtc=false (habituel) : heure locale (TZ).
//             location() - derniere position GPS connue (latitude/longitude
//                        degres decimaux, nombre de satellites du fix).
//                        Renvoie toujours false hors TIME_MODE_GPS_RTC/
//                        TIME_MODE_GPS_ONLY, ou tant qu'aucune position
//                        n'a encore ete obtenue. Seul point d'acces a la
//                        position GPS pour le reste du programme (regle
//                        102, voir Gps.h).
// Référence : CodingRules_Gen.md §12
// ============================================================================
#pragma once
#include <Arduino.h>

class TimeManager
{
public:
    void begin();
    void update();
    bool now(char dateString[9], char timeString[7], bool useUtc);
    bool nowUtcUnix(uint32_t &utcUnixTime);   // epoque Unix UTC brute, ex. pour MeshLink (protocole
                                              // Meshtastic : champ Telemetry.time TOUJOURS en UTC,
                                              // voir MeshtasticTelemetry.h). false en TIME_MODE_MANUAL
                                              // (pas de source UTC reelle).
    bool location(float &latitudeDeg, float &longitudeDeg, uint8_t &satelliteCount);

private:
    bool rtcValid;
    unsigned long manualStartMillis;
};

extern TimeManager timeManager;
