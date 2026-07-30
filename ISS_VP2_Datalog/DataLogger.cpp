// ============================================================================
// Fichier   : DataLogger.cpp (complet, corrige)
// ============================================================================
#include <Arduino.h>
#include <SPI.h>
#include "DataLogger.h"
#include "BoardConfig.h"
#include "Config.h"
#include "TimeManager.h"
#include "HalPins.h"

DataLogger dataLogger;

static void sdDateTimeCallback(uint16_t *fatDate, uint16_t *fatTime)
{
    char dateString[9];
    char timeString[7];
    bool timeValid = timeManager.now(dateString, timeString);

    if (timeValid == false)
    {
        *fatDate = FAT_DATE(2026, 1, 1);
        *fatTime = FAT_TIME(0, 0, 0);
        return;
    }

    uint16_t yearValue  = (uint16_t)((dateString[0] - '0') * 1000 + (dateString[1] - '0') * 100 + (dateString[2] - '0') * 10 + (dateString[3] - '0'));
    uint8_t  monthValue = (uint8_t)((dateString[4] - '0') * 10 + (dateString[5] - '0'));
    uint8_t  dayValue   = (uint8_t)((dateString[6] - '0') * 10 + (dateString[7] - '0'));
    uint8_t  hourValue   = (uint8_t)((timeString[0] - '0') * 10 + (timeString[1] - '0'));
    uint8_t  minuteValue = (uint8_t)((timeString[2] - '0') * 10 + (timeString[3] - '0'));
    uint8_t  secondValue = (uint8_t)((timeString[4] - '0') * 10 + (timeString[5] - '0'));

    *fatDate = FAT_DATE(yearValue, monthValue, dayValue);
    *fatTime = FAT_TIME(hourValue, minuteValue, secondValue);
}

void DataLogger::begin()
{
    sequenceNumber = 0;
    lastWriteMillis = 0;
    currentState.frameValid = false;

#if USE_SD_CARD
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    configureSpiPins();
    SPI.begin();

    bool sdReady = SD.begin(SPI_SD_FREQUENCY, PIN_SD_CS);
    if (sdReady == false)
    {
        Serial.println(F("[DataLogger] Erreur : carte SD non detectee"));
        return;
    }

    SdFile::dateTimeCallback(sdDateTimeCallback);

    // Nom de fichier construit a partir de la date/heure de demarrage,
    // format court 8.3 impose par FAT : VPddHHMM.CSV (jour+heure+minute).
    // Un nouveau fichier distinct est ainsi cree a chaque redemarrage,
    // limitant le risque de perte de donnees en cas de fichier corrompu.
    // Remarque  : Nom de fichier long construit a partir de la date/heure de
//             demarrage, sur le modele de l'ancien programme ESP32
//             (rotation vers YYYYMMDDHHMMSS.CSV une fois l'heure connue).
//             Necessite que la bibliotheque SD/SdFat du core nRF52840
//             (Seeeduino, version 1.2.4) accepte les noms longs (LFN),
//             a verifier a la compilation/execution - sinon, elle
//             tronquera silencieusement au format 8.3.
// ============================================================================
    char dateString[9];
    char timeString[7];
    char logFileName[13];
    bool timeValid = timeManager.now(dateString, timeString);

    if (timeValid == true)
    {
    // Format : JJHHMMSS.CSV (jour du mois + heure complete), 8 caracteres
    // avant l'extension, conforme au 8.3.
    char dayPart[3];
    dayPart[0] = dateString[6];
    dayPart[1] = dateString[7];
    dayPart[2] = '\0';
    snprintf(logFileName, sizeof(logFileName), "%s%s.CSV", dayPart, timeString);
    }
    else
    {
        snprintf(logFileName, sizeof(logFileName), "VP200000.CSV");
    }

    logFile = SD.open(logFileName, FILE_WRITE);
    if (!logFile)
    {
        Serial.println(F("[DataLogger] Erreur : impossible d'ouvrir le fichier de log"));
            Serial.println(logFileName);
        return;
    }

    Serial.print(F("[DataLogger] Fichier de log cree : "));
    Serial.println(logFileName);

    logFile.println(F("seq,date,heure,stationId,batterieFaible,temperatureC,humidite,rayonnementSolaire,indexUV,pluieMmH,compteurPluie,rafaleKph,directionVent"));
    logFile.flush();
#endif
}

void DataLogger::printDecodedValue(const IssData &data)
{
    Serial.print(F("[DataLogger] Station "));
    Serial.print(data.stationId);
    Serial.print(F(" - "));

    switch (data.sensorType)
    {
        case ISS_TYPE_TEMP:     Serial.print(F("Temperature : ")); Serial.print(data.temperatureOutside, 1); Serial.println(F(" C")); break;
        case ISS_TYPE_HUMIDITY: Serial.print(F("Humidite : ")); Serial.print(data.humidityOutside, 1); Serial.println(F(" %")); break;
        case ISS_TYPE_SOLAR:    Serial.print(F("Rayonnement solaire : ")); Serial.print(data.solarRadiation); Serial.println(F(" W/m2")); break;
        case ISS_TYPE_UV:       Serial.print(F("Index UV : ")); Serial.println(data.uvIndex, 1); break;
        case ISS_TYPE_RAINRATE: Serial.print(F("Pluie (debit) : ")); Serial.print(data.rainRateMmPerHour, 1); Serial.println(F(" mm/h")); break;
        case ISS_TYPE_RAIN:     Serial.print(F("Compteur pluie : ")); Serial.println(data.rainTipCount); break;
        case ISS_TYPE_WINDGUST: Serial.print(F("Rafale vent : ")); Serial.println(data.windGustKph); break;
        default: Serial.println(F("Type non affichable")); break;
    }
}

void DataLogger::logRecord(const IssData &data)
{
    if (data.frameValid == false)
    {
        return;
    }

    printDecodedValue(data);

    currentState.stationId = data.stationId;
    currentState.batteryLow = data.batteryLow;
    currentState.windSpeedKph = data.windSpeedKph;
    currentState.windDirectionDeg = data.windDirectionDeg;
    currentState.frameValid = true;

    switch (data.sensorType)
    {
        case 0x08: currentState.temperatureOutside = data.temperatureOutside; break;
        case 0x0A: currentState.humidityOutside = data.humidityOutside; break;
        case 0x06: currentState.solarRadiation = data.solarRadiation; break;
        case 0x04: currentState.uvIndex = data.uvIndex; break;
        case 0x0E: currentState.rainRateMmPerHour = data.rainRateMmPerHour; break;
        case 0x00: currentState.rainTipCount = data.rainTipCount; break;
        case 0x02: currentState.windGustKph = data.windGustKph; break;
        default: break;
    }

    if ((millis() - lastWriteMillis) < LOG_WRITE_INTERVAL_MS)
    {
        return;
    }
    lastWriteMillis = millis();

    char dateString[9];
    char timeString[7];
    bool timeValid = timeManager.now(dateString, timeString);
    if (timeValid == false)
    {
        Serial.println(F("[DataLogger] Erreur : horodatage indisponible, ligne ignoree"));
        return;
    }

    sequenceNumber = sequenceNumber + 1;

    char line[200];
    snprintf(line, sizeof(line), "%lu,%s,%s,%u,%u,%.1f,%.1f,%u,%.1f,%.1f,%u,%u,%u",
             sequenceNumber, dateString, timeString,
             currentState.stationId, currentState.batteryLow,
             currentState.temperatureOutside, currentState.humidityOutside,
             currentState.solarRadiation, currentState.uvIndex,
             currentState.rainRateMmPerHour, currentState.rainTipCount,
             currentState.windGustKph, currentState.windDirectionDeg);

#if USE_SD_CARD
    if (logFile)
    {
        logFile.println(line);
        logFile.flush();
    }
#endif

    Serial.println(line);
}

void DataLogger::update()
{
    // Reserve pour operations periodiques futures.
}
