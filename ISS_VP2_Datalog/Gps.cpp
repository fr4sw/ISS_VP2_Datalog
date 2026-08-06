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
#include "SharedUart.h"
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

static bool gpsTryStartAcquisition()
{
    bool busAcquired = sharedUartAcquire(SHARED_UART_GPS);
    if (busAcquired == false)
    {
        // Bus partage (Serial2) actuellement utilise par Mesh : on ne
        // force jamais - voir SharedUart.h. Nouvelle tentative au prochain
        // appel de gpsUpdate() (quelques millisecondes), la fenetre
        // d'indisponibilite du bus etant toujours tres courte.
        return false;
    }

    Serial2.begin(params.getGpsBaudRate());
    power.enableGps();
    power.disableMesh();   // GPS et Mesh partagent le meme UART (Serial2)

    gpsState = GPS_STATE_ACQUIRING;
    gpsStateStartMillis = millis();

    Serial.println(F("[Gps] Acquisition demarree"));
    return true;
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
        logEvent(F("Point GPS valide, ecart RTC avant resynchronisation (s)"), driftSeconds);
    }
    else
    {
        logEvent(F("Point GPS valide, premiere synchronisation"));
    }

    gpsEverSynced = true;

    sharedUartRelease(SHARED_UART_GPS);
    power.disableGps();
    gpsState = GPS_STATE_WAITING;
    gpsStateStartMillis = millis();
}

bool gpsBegin()
{
    gpsEverSynced = false;
    bool started = gpsTryStartAcquisition();
    if (started == false)
    {
        // Tres improbable au demarrage (Mesh pas encore actif), mais gere
        // par coherence : on retente au tout prochain gpsUpdate() en
        // laissant gpsStateStartMillis "tres ancien".
        gpsState = GPS_STATE_WAITING;
        gpsStateStartMillis = 0;
    }
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
            // gpsTryStartAcquisition() ne met gpsStateStartMillis a jour
            // qu'en cas de succes : si le bus est occupe (Mesh), le
            // prochain appel de gpsUpdate() retrouvera resyncDue a true et
            // reessaiera aussitot, sans delai dedie a gerer ici.
            gpsTryStartAcquisition();
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

#if DEBUG_GPS
        // Affiche la trame GGA complete (heure UTC + nombre de satellites,
        // memes champs vides si pas encore de fix), en plus du resume
        // ci-dessous. La date vient d'une trame separee (RMC), non affichee
        // ici : ce bloc ne montre que la trame reellement utilisee pour
        // l'heure et le nombre de satellites, comme demande.
        static char ggaLineBuffer[90];
        static uint8_t ggaLineLength = 0;

        if ((receivedChar == '\r') || (receivedChar == '\n'))
        {
            if (ggaLineLength > 0)
            {
                ggaLineBuffer[ggaLineLength] = '\0';
                bool isGgaSentence = (strstr(ggaLineBuffer, "GGA") != nullptr);
                if (isGgaSentence == true)
                {
                    Serial.print(F("[Gps] Trame GGA : "));
                    Serial.println(ggaLineBuffer);
                }
                ggaLineLength = 0;
            }
        }
        else if (ggaLineLength < (sizeof(ggaLineBuffer) - 1))
        {
            ggaLineBuffer[ggaLineLength] = receivedChar;
            ggaLineLength = ggaLineLength + 1;
        }
#endif
    }

    // isValid() reste vrai indefiniment des qu'une valeur a ete decodee une
    // fois (gpsParser est statique, jamais reinitialise entre deux sessions
    // GPS) : sans verification de fraicheur via age() (ms depuis la
    // derniere trame decodee avec succes pour ce champ), un champ peut
    // rester "valide" alors qu'il date de la session precedente (ex : la
    // veille), et etre ecrit a tort dans le RTC.
    bool fixDateTimeValid = gpsParser.date.isValid() && gpsParser.time.isValid()
                          && (gpsParser.date.age() <= GPS_FIX_MAX_AGE_MS)
                          && (gpsParser.time.age() <= GPS_FIX_MAX_AGE_MS);
    uint8_t satelliteCount = 0;
    if ((gpsParser.satellites.isValid() == true) && (gpsParser.satellites.age() <= GPS_FIX_MAX_AGE_MS))
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

        sharedUartRelease(SHARED_UART_GPS);
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
