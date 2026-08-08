// ============================================================================
// Fichier : ISS_VP2_Datalog.ino
// Rôle    : Enchainement des traitements uniquement (règle 5).
//           Toute logique metier reside dans les modules dedies.
// ============================================================================
#include "Config.h"
#include "BoardConfig.h"
#include "TimeManager.h"
#include "Power.h"
#include "Params.h"
#include "SerialConsole.h"
#include "EventLog.h"
#include "LocationLog.h"
#include "DataLogger.h"
#if USE_BLE
    #include "BleLink.h"
#endif

#if ISS_WIRELESS
    #include "IssWireless.h"
#endif
#if ISS_RS485
    #include "IssRs485.h"
#endif
#if USE_WIFI_IHM
    #include "WifiPortal.h"
#endif
#if USE_BME680
    #include "BmeIndoor.h"
#endif

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);

// Remarque  : Attente de l'enumeration USB CDC avant les premiers
//             messages de debug. Sans cette attente, les messages
//             envoyes avant l'ouverture du moniteur serie par l'hote
//             sont perdus (comportement normal de l'USB CDC virtuel,
//             different d'un UART physique).
// ============================================================================
    unsigned long usbWaitStart = millis();
    while ((!Serial) && (millis() - usbWaitStart < 3000))
    {
        delay(10);
    }

    Serial.println(F("[Main] Demarrage ISS_VP2_Datalog"));

    power.begin();
    serialConsoleBegin();

    // Params et EventLog ont besoin de la carte SD : leur begin() appelle
    // HalPins::beginSdCard() (idempotent), qui l'initialise si necessaire.
    params.begin();
    logEventBegin();
    locationLogBegin();
    logEvent(F("Demarrage ISS_VP2_Datalog"));

    timeManager.begin();
    dataLogger.begin();

#if ISS_WIRELESS
    issWireless.begin();
#endif
#if ISS_RS485
    issRs485.begin();
#endif
#if USE_WIFI_IHM
    wifiPortal.begin();
#endif
#if USE_BME680
    bmeIndoor.begin();
#endif
#if USE_BLE
    bleLinkBegin();
#endif
}

void loop()
{
    serialConsoleUpdate();
    timeManager.update();
#if USE_BLE
    bleLinkUpdate();
#endif

#if USE_BME680
    bmeIndoor.update();
    IndoorData indoorData;
    bool indoorDataValid = bmeIndoor.getData(indoorData);
    if (indoorDataValid == true)
    {
        dataLogger.updateIndoorData(indoorData);
    }
#endif

    IssRawFrame rawFrame;
    bool frameReceived = false;

#if ISS_RS485
    issRs485.update();
    frameReceived = issRs485.getFrame(rawFrame);
#endif
#if ISS_WIRELESS
    issWireless.update();
    frameReceived = issWireless.getFrame(rawFrame);
#endif

    if (frameReceived == true)
    {
#if DEBUG_RAW_FRAMES
        Serial.print(F("[Main] Trame brute : "));
        for (uint8_t index = 0; index < rawFrame.length; index++)
        {
            if (rawFrame.bytes[index] < 0x10)
            {
                Serial.print(F("0"));
            }
            Serial.print(rawFrame.bytes[index], HEX);
            Serial.print(F(" "));
        }
        Serial.println();
#endif

        IssData decodedData;
        bool decodeSuccess = decodeFrame(rawFrame, decodedData);
        if (decodeSuccess == true)
        {
            dataLogger.logRecord(decodedData);
        }
        else
        {
            Serial.println(F("[Main] Erreur : trame ISS non decodable"));
        }
    }

    dataLogger.update();
}
