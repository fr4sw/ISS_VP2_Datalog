// ============================================================================
// Fichier   : TimeManager.cpp
// Remarque  : RTC et GPS sont toujours lus/ecrits en UTC, sous forme d'un
//             compteur de secondes depuis l'epoque Unix (uint32_t, voir
//             Rtc.cpp/Gps.cpp). Le seul endroit qui applique le decalage de
//             fuseau horaire (Params::getTimezoneOffsetHours(), modifiable
//             sans reprogrammer) est formatLocalDateTime() ci-dessous.
// ============================================================================
#include <Arduino.h>
#include "TimeManager.h"
#include "HalPins.h"
#include "Config.h"
#include "BoardConfig.h"
#include "Params.h"
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_RTC_ONLY)
    #include "Rtc.h"
#endif
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_GPS_ONLY)
    #include "Gps.h"
#endif
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_RTC_ONLY) || (TIME_MODE == TIME_MODE_GPS_ONLY)
    #include <RTClib.h>
#endif

TimeManager timeManager;

#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_RTC_ONLY) || (TIME_MODE == TIME_MODE_GPS_ONLY)
// Applique un decalage (heures locales, ou 0 pour l'UTC) a un horodatage
// UTC et le formate en "YYYYMMDD"/"HHMMSS". Chaque valeur est explicitement
// ramenee a la plage attendue par son format (%100 pour un champ 2
// chiffres, %10000 pour un champ 4 chiffres) : sans cela, le compilateur
// ne peut pas prouver que le resultat rentre dans un tampon de taille
// exacte (avertissement -Wformat-truncation), meme si annee/mois/jour/
// heure/minute/seconde sont en pratique toujours dans une plage valide.
static void formatDateTime(uint32_t utcUnixTime, bool useUtc, char dateString[9], char timeString[7])
{
    int32_t  offsetSeconds = useUtc ? 0 : ((int32_t)params.getTimezoneOffsetHours() * 3600L);
    uint32_t localUnixTime = (uint32_t)((int64_t)utcUnixTime + (int64_t)offsetSeconds);
    DateTime localDateTime(localUnixTime);

    uint16_t yearValue   = localDateTime.year() % 10000;
    uint8_t  monthValue  = localDateTime.month() % 100;
    uint8_t  dayValue    = localDateTime.day() % 100;
    uint8_t  hourValue   = localDateTime.hour() % 100;
    uint8_t  minuteValue = localDateTime.minute() % 100;
    uint8_t  secondValue = localDateTime.second() % 100;

    snprintf(dateString, 9, "%04u%02u%02u", yearValue, monthValue, dayValue);
    snprintf(timeString, 7, "%02u%02u%02u", hourValue, minuteValue, secondValue);
}
#endif

void TimeManager::begin()
{
    rtcValid = false;
    manualStartMillis = 0;

#if DEBUG
    Serial.print(F("[TimeManager] Begin, TIME_MODE = "));
    Serial.println(TIME_MODE);
#endif

#if TIME_MODE == TIME_MODE_GPS_RTC
    rtcValid = rtcBegin();
    if (rtcValid == false)
    {
        Serial.println(F("[TimeManager] Erreur : RTC absent, mode GPS+RTC degrade"));
    }
    gpsBegin();   // resynchronise periodiquement le RTC en arriere-plan (voir Gps.cpp)

#elif TIME_MODE == TIME_MODE_RTC_ONLY
    rtcValid = rtcBegin();
    if (rtcValid == false)
    {
        Serial.println(F("[TimeManager] Erreur : RTC absent ou non reponsif"));
    }

#elif TIME_MODE == TIME_MODE_GPS_ONLY
    gpsBegin();

#elif TIME_MODE == TIME_MODE_MANUAL
    manualStartMillis = millis();
    Serial.println(F("[TimeManager] Mode manuel actif : horodatage fige initialise dans Config.h"));

#else
    #error "TimeManager.cpp : TIME_MODE inconnu ou non defini"
#endif
}

void TimeManager::update()
{
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_GPS_ONLY)
    gpsUpdate();
#endif
}

bool TimeManager::now(char dateString[9], char timeString[7], bool useUtc)
{
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_RTC_ONLY)
    // Le RTC est l'unique source lue ici : en mode GPS_RTC, c'est le GPS
    // (via gpsUpdate(), appele dans update()) qui le tient a jour en
    // arriere-plan - inutile de dupliquer la logique de choix GPS/RTC.
    if (rtcValid == false)
    {
        dateString[0] = '\0';
        timeString[0] = '\0';
        Serial.println(F("[TimeManager] Erreur : RTC indisponible"));
        return false;
    }
    uint32_t utcUnixTime = 0;
    bool available = rtcNow(utcUnixTime);
    if (available == false)
    {
        dateString[0] = '\0';
        timeString[0] = '\0';
        Serial.println(F("[TimeManager] Erreur : lecture RTC impossible"));
        return false;
    }
    formatDateTime(utcUnixTime, useUtc, dateString, timeString);
    return true;

#elif TIME_MODE == TIME_MODE_GPS_ONLY
    uint32_t utcUnixTime = 0;
    bool available = gpsNow(utcUnixTime);
    if (available == false)
    {
        dateString[0] = '\0';
        timeString[0] = '\0';
        Serial.println(F("[TimeManager] Erreur : aucun point GPS disponible pour l'instant"));
        return false;
    }
    formatDateTime(utcUnixTime, useUtc, dateString, timeString);
    return true;

#elif TIME_MODE == TIME_MODE_MANUAL
    // Mode manuel : pas de source UTC reelle (horodatage fige initialise
    // dans Config.h), useUtc n'a donc aucun effet ici.
    unsigned long elapsedSeconds = (millis() - manualStartMillis) / 1000;
    unsigned long totalSeconds = MANUAL_TIME_HOUR * 3600UL + MANUAL_TIME_MINUTE * 60UL + MANUAL_TIME_SECOND + elapsedSeconds;
    unsigned long currentDay = MANUAL_TIME_DAY + (totalSeconds / 86400UL);
    unsigned long secondsToday = totalSeconds % 86400UL;
    unsigned long currentHour = secondsToday / 3600UL;
    unsigned long currentMinute = (secondsToday % 3600UL) / 60UL;
    unsigned long currentSecond = secondsToday % 60UL;

    // Limitation connue : ne gere pas le changement de mois (voir ToDoList.md).
    snprintf(dateString, 9, "%04d%02d%02lu", MANUAL_TIME_YEAR, MANUAL_TIME_MONTH, currentDay % 100);
    snprintf(timeString, 7, "%02lu%02lu%02lu", currentHour % 100, currentMinute % 100, currentSecond % 100);
    return true;
#endif
}

// Seul point d'acces a la position GPS pour le reste du programme (règle
// 102) : Gps.h n'est inclus ici que si TIME_MODE en depend deja pour
// l'heure (voir les #include en tete de fichier) - hors GPS_RTC/GPS_ONLY,
// il n'y a simplement jamais de position disponible.
bool TimeManager::location(float &latitudeDeg, float &longitudeDeg, uint8_t &satelliteCount)
{
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_GPS_ONLY)
    return gpsLastLocation(latitudeDeg, longitudeDeg, satelliteCount);
#else
    return false;
#endif
}

// Epoque Unix UTC brute (voir TimeManager.h) : contrairement a now(), pas
// de mise en forme locale/UTC - directement ce que renvoie la source RTC/
// GPS, sans passer par formatDateTime(). TIME_MODE_MANUAL n'a pas de
// notion d'UTC reelle (horodatage fige arbitraire, voir Config.h) : renvoie
// systematiquement false, l'appelant (MeshLink) doit s'en accommoder.
bool TimeManager::nowUtcUnix(uint32_t &utcUnixTime)
{
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_RTC_ONLY)
    if (rtcValid == false)
    {
        return false;
    }
    return rtcNow(utcUnixTime);

#elif TIME_MODE == TIME_MODE_GPS_ONLY
    return gpsNow(utcUnixTime);

#else
    return false;
#endif
}
