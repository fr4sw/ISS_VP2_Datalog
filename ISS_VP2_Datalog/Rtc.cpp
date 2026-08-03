// ============================================================================
// Fichier   : Rtc.cpp
// ============================================================================
#include <Wire.h>
#include <RTClib.h>
#include "Rtc.h"
#include "HalPins.h"
#include "Config.h"
#include "EventLog.h"

static RTC_DS3231 rtcDevice;
static bool        rtcDeviceReady = false;

bool rtcBegin()
{
    beginI2cBus();

    rtcDeviceReady = rtcDevice.begin(&Wire);
    if (rtcDeviceReady == false)
    {
        Serial.println(F("[Rtc] Erreur : DS3231 non detecte"));
        return false;
    }

#if RTC_FORCE_MANUAL_TIME
    Serial.println(F("[Rtc] RTC_FORCE_MANUAL_TIME actif (Config.h) : reecriture obligatoire"));
    rtcSetDateTime(DateTime(MANUAL_TIME_YEAR, MANUAL_TIME_MONTH, MANUAL_TIME_DAY,
                             MANUAL_TIME_HOUR, MANUAL_TIME_MINUTE, MANUAL_TIME_SECOND));
#else
    bool timeNotReliable = rtcDevice.lostPower();
    if (timeNotReliable == true)
    {
        Serial.println(F("[Rtc] RTC non fiable (perte d'alimentation detectee) : reinitialisation"));
        rtcSetDateTime(DateTime(MANUAL_TIME_YEAR, MANUAL_TIME_MONTH, MANUAL_TIME_DAY,
                                 MANUAL_TIME_HOUR, MANUAL_TIME_MINUTE, MANUAL_TIME_SECOND));
    }
  #if DEBUG
    else
    {
        Serial.println(F("[Rtc] RTC OK, horodatage conserve depuis la derniere mise a l'heure"));
    }
  #endif
#endif

    return true;
}

bool rtcNow(uint32_t &utcUnixTime)
{
    if (rtcDeviceReady == false)
    {
        return false;
    }
    utcUnixTime = rtcDevice.now().unixtime();
    return true;
}

void rtcSetDateTime(const DateTime &dateTime)
{
    rtcDevice.adjust(dateTime);

    Serial.print(F("[Rtc] Horodatage RTC (UTC) ecrit : "));
    Serial.print(dateTime.year());
    Serial.print(F("-"));
    Serial.print(dateTime.month());
    Serial.print(F("-"));
    Serial.print(dateTime.day());
    Serial.print(F(" "));
    Serial.print(dateTime.hour());
    Serial.print(F(":"));
    Serial.print(dateTime.minute());
    Serial.print(F(":"));
    Serial.println(dateTime.second());

    logEvent(F("Ecriture RTC (resynchronisation)"));
}
