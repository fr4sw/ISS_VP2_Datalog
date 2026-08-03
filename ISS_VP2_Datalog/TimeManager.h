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
// Référence : CodingRules_Gen.md §12
// ============================================================================
#pragma once
#include <Arduino.h>

class TimeManager
{
public:
    void begin();
    void update();
    bool now(char dateString[9], char timeString[7]);

private:
    bool rtcValid;
    unsigned long manualStartMillis;
};

extern TimeManager timeManager;
