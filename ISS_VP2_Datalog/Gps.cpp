// ============================================================================
// Fichier   : Gps.cpp
// ============================================================================
#include "Gps.h"

// Tout le contenu (y compris la dependance a la bibliotheque TinyGPSPlus)
// n'est compile que si un mode temps utilisant le GPS est reellement
// selectionne dans Config.h : pas besoin d'installer TinyGPSPlus pour
// TIME_MODE_RTC_ONLY ou TIME_MODE_MANUAL.
#if (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_GPS_ONLY)

#include <TinyGPSPlus.h>
#include <RTClib.h>
#include "BoardConfig.h"
#include "HalPins.h"
#include "Power.h"
#include "EventLog.h"
#include "Params.h"
#if TIME_MODE == TIME_MODE_GPS_RTC
    #include "Rtc.h"
#endif

static TinyGPSPlus gpsParser;
static bool        gpsEverSynced = false;

enum GpsState
{
    GPS_STATE_ACQUIRING,   // GPS alimente, en attente d'un point valide
    GPS_STATE_WAITING      // GPS coupe, en attente de la prochaine tentative
};

static GpsState       gpsState;
static unsigned long  gpsStateStartMillis;

#if TIME_MODE == TIME_MODE_GPS_ONLY
static uint32_t      gpsReferenceUnixTime;
static unsigned long gpsReferenceMillis;
#endif

static void gpsStartAcquisition()
{
    Serial2.begin(params.getGpsBaudRate());
    power.enableGps();
    power.disableMesh();   // GPS et Mesh partagent le meme UART (Serial2)

    gpsState = GPS_STATE_ACQUIRING;
    gpsStateStartMillis = millis();

    Serial.println(F("[Gps] Acquisition demarree"));
}

// Applique un point GPS valide (ecart en secondes par rapport a l'ancienne
// reference, toujours journalise, meme hors DEBUG - règle utilisateur :
// "enregistrer dans les logs les ecarts pour observer si anomalie
// importante").
static void gpsApplyFix(uint32_t fixUnixTime, uint8_t satelliteCount)
{
    Serial.print(F("[Gps] Point valide recupere, satellites = "));
    Serial.println(satelliteCount);

#if TIME_MODE == TIME_MODE_GPS_RTC
    uint32_t previousUnixTime = 0;
    bool previousAvailable = rtcNow(previousUnixTime);
    rtcSetDateTime(DateTime(fixUnixTime));
#elif TIME_MODE == TIME_MODE_GPS_ONLY
    uint32_t previousUnixTime = gpsReferenceUnixTime;
    bool previousAvailable = gpsEverSynced;
    gpsReferenceUnixTime = fixUnixTime;
    gpsReferenceMillis = millis();
#endif

    if (previousAvailable == true)
    {
        int32_t driftSeconds = (int32_t)(fixUnixTime - previousUnixTime);
        Serial.print(F("[Gps] Ecart mesure avant resynchronisation : "));
        Serial.print(driftSeconds);
        Serial.println(F(" s"));
        logEvent(F("Point GPS valide, horloge resynchronisee"));
    }
    else
    {
        logEvent(F("Point GPS valide, premiere synchronisation"));
    }

    gpsEverSynced = true;

    power.disableGps();
    gpsState = GPS_STATE_WAITING;
    gpsStateStartMillis = millis();
}

bool gpsBegin()
{
    gpsEverSynced = false;
    gpsStartAcquisition();
    return true;
}

void gpsUpdate()
{
    if (gpsState == GPS_STATE_WAITING)
    {
#if TIME_MODE == TIME_MODE_GPS_RTC
        unsigned long resyncIntervalMs = GPS_RTC_RESYNC_INTERVAL_MS;
#elif TIME_MODE == TIME_MODE_GPS_ONLY
        unsigned long resyncIntervalMs = GPS_ONLY_RESYNC_INTERVAL_MS;
#endif
        unsigned long elapsedSinceLastTry = millis() - gpsStateStartMillis;
        bool resyncDue = (elapsedSinceLastTry >= resyncIntervalMs);
        if (resyncDue == true)
        {
            Serial.println(F("[Gps] Nouvelle tentative de resynchronisation"));
            gpsStartAcquisition();
        }
        return;
    }

    // GPS_STATE_ACQUIRING : on alimente le parseur NMEA a chaque octet
    // disponible, sans jamais bloquer (règle : ne pas perturber le reste
    // de loop(), en particulier la reception RS485 - meme si celle-ci est
    // sur un UART materiel distinct, Serial2/UARTE1, voir HalPins.cpp).
    while (Serial2.available() > 0)
    {
        char receivedChar = (char)Serial2.read();
        gpsParser.encode(receivedChar);
    }

    bool fixDateTimeValid = gpsParser.date.isValid() && gpsParser.time.isValid();
    uint8_t satelliteCount = 0;
    if (gpsParser.satellites.isValid() == true)
    {
        satelliteCount = (uint8_t)gpsParser.satellites.value();
    }

#if DEBUG_GPS
    // Ne journalise que l'information utile au reglage (heure UTC decodee
    // et nombre de satellites), pas le flux NMEA complet (bien trop
    // volumineux) - et seulement quand l'un des deux change, pour ne pas
    // saturer le moniteur serie a chaque passage de loop().
    static uint32_t lastLoggedRawTime = 0xFFFFFFFF;
    static uint8_t  lastLoggedSatelliteCount = 0xFF;
    uint32_t currentRawTime = 0xFFFFFFFF;
    if (gpsParser.time.isValid() == true)
    {
        currentRawTime = gpsParser.time.value();   // HHMMSSCC
    }
    bool debugValuesChanged = (currentRawTime != lastLoggedRawTime) || (satelliteCount != lastLoggedSatelliteCount);
    if (debugValuesChanged == true)
    {
        Serial.print(F("[Gps] Heure UTC recue : "));
        if (gpsParser.time.isValid() == true)
        {
            Serial.print(gpsParser.time.hour());
            Serial.print(F(":"));
            Serial.print(gpsParser.time.minute());
            Serial.print(F(":"));
            Serial.print(gpsParser.time.second());
        }
        else
        {
            Serial.print(F("--:--:--"));
        }
        Serial.print(F(" - satellites = "));
        Serial.println(satelliteCount);

        lastLoggedRawTime = currentRawTime;
        lastLoggedSatelliteCount = satelliteCount;
    }
#endif

    if ((fixDateTimeValid == true) && (satelliteCount >= GPS_MINIMUM_SATELLITES))
    {
        DateTime fixDateTime(gpsParser.date.year(), gpsParser.date.month(), gpsParser.date.day(),
                              gpsParser.time.hour(), gpsParser.time.minute(), gpsParser.time.second());
        gpsApplyFix(fixDateTime.unixtime(), satelliteCount);
        return;
    }

    unsigned long acquiringElapsed = millis() - gpsStateStartMillis;
    bool timedOut = (acquiringElapsed >= GPS_TIMEOUT);
    if (timedOut == true)
    {
        Serial.print(F("[Gps] Erreur : delai depasse sans point valide, satellites = "));
        Serial.println(satelliteCount);
        logEvent(F("Erreur : timeout acquisition GPS"));

        power.disableGps();
        gpsState = GPS_STATE_WAITING;
        gpsStateStartMillis = millis();
    }
}

#if TIME_MODE == TIME_MODE_GPS_ONLY
bool gpsNow(uint32_t &utcUnixTime)
{
    if (gpsEverSynced == false)
    {
        return false;
    }

    unsigned long elapsedSeconds = (millis() - gpsReferenceMillis) / 1000UL;
    utcUnixTime = gpsReferenceUnixTime + (uint32_t)elapsedSeconds;
    return true;
}
#endif

#endif // (TIME_MODE == TIME_MODE_GPS_RTC) || (TIME_MODE == TIME_MODE_GPS_ONLY)
