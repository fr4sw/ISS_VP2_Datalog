// ============================================================================
// Fichier   : Rtc.cpp
// ============================================================================
#include <Wire.h>
#include <RTClib.h>
#include "Rtc.h"
#include "HalPins.h"
#include "Config.h"

static RTC_DS3231 rtcDevice;

// Ecrit les valeurs MANUAL_TIME_* de Config.h dans le RTC. Fonction unique
// utilisee par les deux cas de reecriture (règle 20 : une seule tache,
// appelee depuis plusieurs points plutot que dupliquee).
static void rtcApplyManualTime()
{
    rtcDevice.adjust(DateTime(MANUAL_TIME_YEAR, MANUAL_TIME_MONTH, MANUAL_TIME_DAY,
                               MANUAL_TIME_HOUR, MANUAL_TIME_MINUTE, MANUAL_TIME_SECOND));
    Serial.println(F("[Rtc] Horodatage RTC ecrit avec les valeurs MANUAL_TIME_* de Config.h"));
}

bool rtcBegin()
{
    beginI2cBus();

    bool devicePresent = rtcDevice.begin(&Wire);
    if (devicePresent == false)
    {
        Serial.println(F("[Rtc] Erreur : DS3231 non detecte"));
        return false;
    }

#if RTC_FORCE_MANUAL_TIME
    Serial.println(F("[Rtc] RTC_FORCE_MANUAL_TIME actif (Config.h) : reecriture obligatoire"));
    rtcApplyManualTime();
#else
    bool timeNotReliable = rtcDevice.lostPower();
    if (timeNotReliable == true)
    {
        Serial.println(F("[Rtc] RTC non fiable (perte d'alimentation detectee) : reinitialisation"));
        rtcApplyManualTime();
    }
  #if DEBUG
    else {Serial.print(F("[Rtc] Rtc OK"));
    Serial.print(F(" - "));
    Serial.print(F("TIME_MODE : ")); Serial.print(TIME_MODE); Serial.println(F(" "));
    }
  #endif
    
#endif

    return true;
}

void rtcCopyDateTime(char dateString[9], char timeString[7])
{
    DateTime currentDateTime = rtcDevice.now();

    snprintf(dateString, 9, "%04d%02d%02d", currentDateTime.year(), currentDateTime.month(), currentDateTime.day());
    snprintf(timeString, 7, "%02d%02d%02d", currentDateTime.hour(), currentDateTime.minute(), currentDateTime.second());
}
