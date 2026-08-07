// ============================================================================
// Fichier   : EventLog.cpp
// ============================================================================
#include <Arduino.h>
#include <SD.h>
#include "EventLog.h"
#include "Config.h"
#include "HalPins.h"
#include "TimeManager.h"
#include "Params.h"

static File eventLogFile;
static bool eventLogReady = false;

void logEventBegin()
{
#if USE_SD_CARD
    bool sdReady = beginSdCard();
    if (sdReady == false)
    {
        Serial.println(F("[EventLog] Carte SD indisponible : journal desactive pour cette session"));
        return;
    }

    eventLogFile = SD.open(EVENTLOG_FILE_NAME, FILE_WRITE);
    if (!eventLogFile)
    {
        Serial.println(F("[EventLog] Erreur : impossible d'ouvrir le journal d'evenements"));
        return;
    }

    eventLogReady = true;
#endif
}

#if USE_SD_CARD
static void writeLogTimestamp()
{
    char dateString[9];
    char timeString[7];
    bool timeValid = timeManager.now(dateString, timeString, params.getDataloggerUtc());

    if (timeValid == true)
    {
        eventLogFile.print(dateString);
        eventLogFile.print(F(" "));
        eventLogFile.print(timeString);
    }
    else
    {
        eventLogFile.print(F("--------  ------"));
    }
    eventLogFile.print(F(" - "));
}
#endif

void logEvent(const __FlashStringHelper *message)
{
#if USE_SD_CARD
    if (eventLogReady == false)
    {
        return;
    }

    writeLogTimestamp();
    eventLogFile.println(message);
    eventLogFile.flush();
#else
    (void)message;
#endif
}

void logEvent(const __FlashStringHelper *message, long value)
{
#if USE_SD_CARD
    if (eventLogReady == false)
    {
        return;
    }

    writeLogTimestamp();
    eventLogFile.print(message);
    eventLogFile.print(F(" : "));
    eventLogFile.println(value);
    eventLogFile.flush();
#else
    (void)message;
    (void)value;
#endif
}
