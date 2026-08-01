// ============================================================================
// Fichier   : BmeIndoor.cpp
// ============================================================================
#include <Wire.h>
#include "BmeIndoor.h"
#include "HalPins.h"

BmeIndoor bmeIndoor;

// Reglages de sur-echantillonnage et de filtrage : valeurs recommandees par
// Bosch pour un usage de type "station environnementale" (mesure stable,
// faible dynamique), voir Bosch BME680 datasheet §3.3.4.
static const uint8_t BME680_TEMPERATURE_OVERSAMPLING = BME680_OS_8X;
static const uint8_t BME680_HUMIDITY_OVERSAMPLING    = BME680_OS_2X;
static const uint8_t BME680_PRESSURE_OVERSAMPLING    = BME680_OS_4X;
static const uint8_t BME680_IIR_FILTER_SIZE          = BME680_FILTER_SIZE_3;

// La bibliotheque Adafruit_BME680 exprime bmeSensor.pressure en pascals.
static const float PASCAL_TO_HECTOPASCAL = 0.01f;

void BmeIndoor::begin()
{
    measurementState = STATE_IDLE;
    lastMeasurementStartMillis = 0;
    measurementReadyMillis = 0;
    sensorReady = false;
    lastData.dataValid = false;

#if USE_BME680
    beginI2cBus();

    bool beginSuccess = bmeSensor.begin(BME680_I2C_ADDRESS, &Wire);
    if (beginSuccess == false)
    {
        Serial.println(F("[BmeIndoor] Erreur : BME680 non detecte"));
        return;
    }

    bmeSensor.setTemperatureOversampling(BME680_TEMPERATURE_OVERSAMPLING);
    bmeSensor.setHumidityOversampling(BME680_HUMIDITY_OVERSAMPLING);
    bmeSensor.setPressureOversampling(BME680_PRESSURE_OVERSAMPLING);
    bmeSensor.setIIRFilterSize(BME680_IIR_FILTER_SIZE);

    sensorReady = true;
    Serial.println(F("[BmeIndoor] BME680 initialise"));
#endif
}

void BmeIndoor::update()
{
#if USE_BME680
    if (sensorReady == false)
    {
        return;
    }

    if (measurementState == STATE_IDLE)
    {
        unsigned long elapsedMillis = millis() - lastMeasurementStartMillis;
        bool intervalElapsed = (elapsedMillis >= BME680_READ_INTERVAL_MS);
        if (intervalElapsed == false)
        {
            return;
        }

        // beginReading() demarre une conversion et renvoie l'instant
        // (millis()) auquel le resultat sera disponible, ou 0 en cas
        // d'echec de communication I2C.
        unsigned long conversionReadyMillis = bmeSensor.beginReading();
        if (conversionReadyMillis == 0)
        {
            Serial.println(F("[BmeIndoor] Erreur : demarrage de mesure impossible"));
            return;
        }

        lastMeasurementStartMillis = millis();
        measurementReadyMillis = conversionReadyMillis;
        measurementState = STATE_MEASURING;
        return;
    }

    // measurementState == STATE_MEASURING
    bool conversionDone = (millis() >= measurementReadyMillis);
    if (conversionDone == false)
    {
        return;
    }

    bool endSuccess = bmeSensor.endReading();
    if (endSuccess == false)
    {
        Serial.println(F("[BmeIndoor] Erreur : lecture BME680 invalide"));
        lastData.dataValid = false;
        measurementState = STATE_IDLE;
        return;
    }

    lastData.temperatureIndoor = bmeSensor.temperature;
    lastData.humidityIndoor    = bmeSensor.humidity;
    lastData.pressureIndoor    = bmeSensor.pressure * PASCAL_TO_HECTOPASCAL;
    lastData.dataValid = true;

#if DEBUG
    Serial.print(F("[BmeIndoor] BME680 "));
    Serial.print(F(" - "));
    Serial.print(F("Temp Int : ")); Serial.print(lastData.temperatureIndoor, 1); Serial.print(F(" C  / "));
    Serial.print(F("Hum Int : ")); Serial.print(lastData.humidityIndoor, 1); Serial.println(F(" %  /  "));
    Serial.print(F("Pression : ")); Serial.print(lastData.pressureIndoor, 1); Serial.println(F(" HPa"));
#endif

    measurementState = STATE_IDLE;
#endif
}

bool BmeIndoor::getData(IndoorData &data)
{
    data = lastData;
    return lastData.dataValid;
}
