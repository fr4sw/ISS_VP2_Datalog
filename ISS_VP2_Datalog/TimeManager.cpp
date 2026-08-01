// ============================================================================
// Fichier   : TimeManager.cpp
// ============================================================================
#include <Arduino.h>
#include "TimeManager.h"
#include "HalPins.h"
#include "Config.h"
#include "BoardConfig.h"
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_RTC_ONLY)
    #include "Rtc.h"
#endif

TimeManager timeManager;

void TimeManager::begin()
{
    gpsFix = false;
    rtcValid = false;
    manualStartMillis = 0;

#if DEBUG
    Serial.print(F("[TimeManager] Begin"));
    Serial.print(F(" - "));
    Serial.print(F("TIME_MODE : ")); Serial.print(TIME_MODE); Serial.println(F(" "));
#endif

#if TIME_MODE == TIME_MODE_GPS_RTC
    rtcValid = rtcBegin();
    if (rtcValid == false)
    {
        Serial.println(F("[TimeManager] Erreur : RTC absent, mode GPS+RTC degrade"));
    }
    Serial1.begin(UART_GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, -1);

#elif TIME_MODE == TIME_MODE_RTC_ONLY
    rtcValid = rtcBegin();
    if (rtcValid == false)
    {
        Serial.println(F("[TimeManager] Erreur : RTC absent ou non reponsif"));
    }

#elif TIME_MODE == TIME_MODE_GPS_ONLY
    Serial1.begin(UART_GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, -1);

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
    gpsFix = gpsUpdateAndCheckFix();
#endif
}

bool TimeManager::now(char dateString[9], char timeString[7])
{
#if TIME_MODE == TIME_MODE_GPS_RTC
    if (gpsFix == true)
    {
        gpsCopyDateTime(dateString, timeString);
        return true;
    }
    if (rtcValid == true)
    {
        rtcCopyDateTime(dateString, timeString);
        return true;
    }
    dateString[0] = '\0';
    timeString[0] = '\0';
    Serial.println(F("[TimeManager] Erreur : ni GPS ni RTC disponibles"));
    return false;

#elif TIME_MODE == TIME_MODE_RTC_ONLY
    if (rtcValid == true)
    {
        rtcCopyDateTime(dateString, timeString);
        return true;
    }
    dateString[0] = '\0';
    timeString[0] = '\0';
    Serial.println(F("[TimeManager] Erreur : RTC indisponible"));
    return false;

#elif TIME_MODE == TIME_MODE_GPS_ONLY
    if (gpsFix == true)
    {
        gpsCopyDateTime(dateString, timeString);
        return true;
    }
    dateString[0] = '\0';
    timeString[0] = '\0';
    Serial.println(F("[TimeManager] Erreur : pas de fix GPS"));
    return false;

#elif TIME_MODE == TIME_MODE_MANUAL
    unsigned long elapsedSeconds = (millis() - manualStartMillis) / 1000;
    unsigned long totalSeconds = MANUAL_TIME_HOUR * 3600UL + MANUAL_TIME_MINUTE * 60UL + MANUAL_TIME_SECOND + elapsedSeconds;
    unsigned long currentDay = MANUAL_TIME_DAY + (totalSeconds / 86400UL);
    unsigned long secondsToday = totalSeconds % 86400UL;
    unsigned long currentHour = secondsToday / 3600UL;
    unsigned long currentMinute = (secondsToday % 3600UL) / 60UL;
    unsigned long currentSecond = secondsToday % 60UL;

    // Limitation connue : ne gere pas le changement de mois (voir ToDoList.md).
    snprintf(dateString, 9, "%04d%02d%02lu", MANUAL_TIME_YEAR, MANUAL_TIME_MONTH, currentDay);
    snprintf(timeString, 7, "%02lu%02lu%02lu", currentHour, currentMinute, currentSecond);
    return true;
#endif
}
