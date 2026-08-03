// ============================================================================
// Fichier   : DataLogger.cpp
// ============================================================================
#include <Arduino.h>
#include <SPI.h>
#include "DataLogger.h"
#include "BoardConfig.h"
#include "Config.h"
#include "TimeManager.h"
#include "HalPins.h"
#include "Params.h"
#include "EventLog.h"

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
    currentIndoorState.dataValid = false;
    windSpeedSumKph = 0;
    windSpeedSampleCount = 0;
    windDirectionSampleCount = 0;
    for (uint8_t sectorIndex = 0; sectorIndex < WIND_DIR_SECTOR_COUNT; sectorIndex++)
    {
        windDirectionSectorCounts[sectorIndex] = 0;
    }

#if USE_SD_CARD
    bool sdReady = beginSdCard();
    if (sdReady == false)
    {
        return;
    }

    SdFile::dateTimeCallback(sdDateTimeCallback);

    // Nom de fichier construit a partir de la date/heure de demarrage,
    // format court 8.3 impose par FAT : JJHHMMSS.CSV (jour+heure+minute).
    // Un nouveau fichier distinct est ainsi cree a chaque redemarrage,
    // limitant le risque de perte de donnees en cas de fichier corrompu.
    char dateString[9];
    char timeString[7];
    char logFileName[13];
    bool timeValid = timeManager.now(dateString, timeString);

    if (timeValid == true)
    {
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
    logEvent(F("Nouveau fichier de log CSV cree"));

    logFile.println(F("seq,date,heure,stationId,batterieFaible,temperatureC,humidite,rayonnementSolaire,indexUV,pluieMmH,compteurPluie,rafaleKph,vitesseVentKph,directionVent,temperatureInterieureC,humiditeInterieure,pressionInterieureHpa"));
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

void DataLogger::updateIndoorData(const IndoorData &data)
{
    currentIndoorState = data;
}

// Accumule un echantillon de vitesse/direction (appele a CHAQUE trame,
// puisque le protocole Davis transmet vitesse+direction sur toutes les
// trames). La direction n'est comptabilisee dans l'histogramme que si le
// vent souffle (vitesse > 0), conformement a la definition Davis du champ
// "Wind Dir" archive (voir docs/Davis_Data_Archived_v3.pdf : "si la vitesse
// du vent est quasiment toujours a 0, aucune direction n'est indiquee").
void DataLogger::accumulateWindSample(uint8_t windSpeedKph, uint16_t windDirectionDeg)
{
    windSpeedSumKph = windSpeedSumKph + windSpeedKph;
    windSpeedSampleCount = windSpeedSampleCount + 1;

    if (windSpeedKph > 0)
    {
        uint8_t sector = windDirectionSectorFromDeg(windDirectionDeg);
        windDirectionSectorCounts[sector] = windDirectionSectorCounts[sector] + 1;
        windDirectionSampleCount = windDirectionSampleCount + 1;
    }
}

void DataLogger::logRecord(const IssData &data)
{
    if (data.frameValid == false)
    {
        return;
    }

#if DEBUG
    printDecodedValue(data);
#endif

    currentState.stationId = data.stationId;
    currentState.batteryLow = data.batteryLow;
    currentState.frameValid = true;

    accumulateWindSample(data.windSpeedKph, data.windDirectionDeg);

    // règle 6 : les codes de trame ne sont testes qu'a un seul endroit dans
    // tout le projet, via les constantes ISS_TYPE_* de IssCommon.h (jamais
    // recopies en litteral).
    switch (data.sensorType)
    {
        case ISS_TYPE_TEMP:     currentState.temperatureOutside = data.temperatureOutside; break;
        case ISS_TYPE_HUMIDITY: currentState.humidityOutside = data.humidityOutside; break;
        case ISS_TYPE_SOLAR:    currentState.solarRadiation = data.solarRadiation; break;
        case ISS_TYPE_UV:       currentState.uvIndex = data.uvIndex; break;
        case ISS_TYPE_RAINRATE: currentState.rainRateMmPerHour = data.rainRateMmPerHour; break;
        case ISS_TYPE_RAIN:     currentState.rainTipCount = data.rainTipCount; break;
        case ISS_TYPE_WINDGUST: currentState.windGustKph = data.windGustKph; break;
        default: break;
    }

    uint32_t logWriteIntervalMs = params.getLogWriteIntervalMs();
    bool writeDue = (logWriteIntervalMs == 0) || ((millis() - lastWriteMillis) >= logWriteIntervalMs);
    if (writeDue == false)
    {
        return;
    }
    lastWriteMillis = millis();

    writeLine();
}

void DataLogger::writeLine()
{
    char dateString[9];
    char timeString[7];
    bool timeValid = timeManager.now(dateString, timeString);
    if (timeValid == false)
    {
        Serial.println(F("[DataLogger] Erreur : horodatage indisponible, ligne ignoree"));
        return;
    }

    sequenceNumber = sequenceNumber + 1;

    // Synthese du vent sur l'intervalle ecoule (voir DataLogger.h) :
    // vitesse moyenne, direction majoritaire (secteur le plus echantillonne,
    // "NA" si calme sur tout l'intervalle - jamais 0, qui serait une vraie
    // valeur Nord).
    uint8_t windSpeedAverageKph = 0;
    if (windSpeedSampleCount > 0)
    {
        windSpeedAverageKph = (uint8_t)(windSpeedSumKph / windSpeedSampleCount);
    }

    char windDirectionField[8];
    if (windDirectionSampleCount == 0)
    {
        snprintf(windDirectionField, sizeof(windDirectionField), "NA");
    }
    else
    {
        uint8_t majoritySector = 0;
        uint16_t majorityCount = 0;
        for (uint8_t sectorIndex = 0; sectorIndex < WIND_DIR_SECTOR_COUNT; sectorIndex++)
        {
            if (windDirectionSectorCounts[sectorIndex] > majorityCount)
            {
                majorityCount = windDirectionSectorCounts[sectorIndex];
                majoritySector = sectorIndex;
            }
        }
        uint16_t majorityDirectionDeg = windDirectionDegFromSector(majoritySector);
        snprintf(windDirectionField, sizeof(windDirectionField), "%u", majorityDirectionDeg);
    }

    // Champs du capteur interieur formates a part : affiche "NA" tant
    // qu'aucune mesure valide n'a ete recue depuis le demarrage (règle 6 -
    // une valeur 0.0 par defaut serait indiscernable d'une mesure reelle).
    char indoorTemperatureField[8];
    char indoorHumidityField[8];
    char indoorPressureField[8];

    if (currentIndoorState.dataValid == true)
    {
        snprintf(indoorTemperatureField, sizeof(indoorTemperatureField), "%.1f", currentIndoorState.temperatureIndoor);
        snprintf(indoorHumidityField, sizeof(indoorHumidityField), "%.1f", currentIndoorState.humidityIndoor);
        snprintf(indoorPressureField, sizeof(indoorPressureField), "%.1f", currentIndoorState.pressureIndoor);
    }
    else
    {
        snprintf(indoorTemperatureField, sizeof(indoorTemperatureField), "NA");
        snprintf(indoorHumidityField, sizeof(indoorHumidityField), "NA");
        snprintf(indoorPressureField, sizeof(indoorPressureField), "NA");
    }

    char line[240];
    snprintf(line, sizeof(line), "%lu,%s,%s,%u,%u,%.1f,%.1f,%u,%.1f,%.1f,%u,%u,%u,%s,%s,%s,%s",
             sequenceNumber, dateString, timeString,
             currentState.stationId, currentState.batteryLow,
             currentState.temperatureOutside, currentState.humidityOutside,
             currentState.solarRadiation, currentState.uvIndex,
             currentState.rainRateMmPerHour, currentState.rainTipCount,
             currentState.windGustKph, windSpeedAverageKph, windDirectionField,
             indoorTemperatureField, indoorHumidityField, indoorPressureField);

#if USE_SD_CARD
    if (logFile)
    {
        logFile.println(line);
        logFile.flush();
    }
#endif

    Serial.println(line);

    resetWindAccumulators();
}

void DataLogger::resetWindAccumulators()
{
    windSpeedSumKph = 0;
    windSpeedSampleCount = 0;
    windDirectionSampleCount = 0;
    for (uint8_t sectorIndex = 0; sectorIndex < WIND_DIR_SECTOR_COUNT; sectorIndex++)
    {
        windDirectionSectorCounts[sectorIndex] = 0;
    }
}

void DataLogger::update()
{
    // Reserve pour operations periodiques futures.
}
