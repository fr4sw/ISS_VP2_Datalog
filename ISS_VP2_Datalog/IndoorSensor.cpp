// ============================================================================
// Fichier : IndoorSensor.cpp
// Rôle    : Détection automatique et acquisition des capteurs environnementaux.
// Phase 2 : BME680 + BME280.
// ============================================================================

#include "IndoorSensor.h"

IndoorSensor indoorSensor;

static const uint8_t BME680_TEMPERATURE_OVERSAMPLING = BME680_OS_8X;
static const uint8_t BME680_HUMIDITY_OVERSAMPLING    = BME680_OS_2X;
static const uint8_t BME680_PRESSURE_OVERSAMPLING    = BME680_OS_4X;
static const uint8_t BME680_IIR_FILTER_SIZE          = BME680_FILTER_SIZE_3;

static const float PASCAL_TO_HECTOPASCAL = 0.01f;

void IndoorSensor::begin()
{
    measurementState = STATE_IDLE;
    lastMeasurementStartMillis = 0;
    measurementReadyMillis = 0;
    sensorReady = false;

    sensorInfo.type = INDOOR_SENSOR_NONE;
    sensorInfo.name = "Aucun";
    sensorInfo.i2cAddress = 0;
    sensorInfo.capabilities = 0;
    sensorInfo.supported = false;

    resetData();

    Wire.begin();
    Wire.setClock(I2C_SPEED);

    Serial.println(F("[IndoorSensor] Recherche capteur I2C..."));

    if (detectSensor() == false)
    {
        Serial.println(F("[IndoorSensor] Aucun capteur environnemental reconnu"));
        return;
    }

    if (sensorInfo.supported == false)
    {
        Serial.print(F("[IndoorSensor] Capteur reconnu mais non supporte : "));
        Serial.println(sensorInfo.name);
        return;
    }

    if (sensorInfo.type == INDOOR_SENSOR_BME680)
    {
        bool beginSuccess = bme680Sensor.begin(sensorInfo.i2cAddress);

        if (beginSuccess == false)
        {
            Serial.println(F("[IndoorSensor] Erreur initialisation BME680"));
            return;
        }

        bme680Sensor.setTemperatureOversampling(BME680_TEMPERATURE_OVERSAMPLING);
        bme680Sensor.setHumidityOversampling(BME680_HUMIDITY_OVERSAMPLING);
        bme680Sensor.setPressureOversampling(BME680_PRESSURE_OVERSAMPLING);
        bme680Sensor.setIIRFilterSize(BME680_IIR_FILTER_SIZE);

        sensorReady = true;

        Serial.print(F("[IndoorSensor] BME680 initialise a 0x"));
        if (sensorInfo.i2cAddress < 0x10) Serial.print('0');
        Serial.println(sensorInfo.i2cAddress, HEX);
    }
    else if (sensorInfo.type == INDOOR_SENSOR_BME280)
    {
        if (bme280Sensor.begin(Wire, sensorInfo.i2cAddress) == false)
        {
            Serial.println(F("[IndoorSensor] Erreur initialisation BME280"));
            return;
        }

        sensorReady = true;

        Serial.print(F("[IndoorSensor] BME280 initialise a 0x"));
        if (sensorInfo.i2cAddress < 0x10) Serial.print('0');
        Serial.println(sensorInfo.i2cAddress, HEX);
    }
}

void IndoorSensor::update()
{
    if (sensorReady == false)
    {
        return;
    }

    if (sensorInfo.type == INDOOR_SENSOR_BME280)
    {
        if (measurementState == STATE_IDLE)
        {
            unsigned long elapsedMillis =
                millis() - lastMeasurementStartMillis;

            if (elapsedMillis < BME680_READ_INTERVAL_MS)
            {
                return;
            }

            if (bme280Sensor.startMeasurement() == false)
            {
                Serial.println(F("[IndoorSensor] Erreur : demarrage mesure BME280"));
                return;
            }

            lastMeasurementStartMillis = millis();
            measurementState = STATE_MEASURING;
            return;
        }

        if (bme280Sensor.measurementDone() == false)
        {
            return;
        }

        Bme280Data bmeData;

        if (bme280Sensor.readMeasurement(bmeData) == false)
        {
            Serial.println(F("[IndoorSensor] Erreur : lecture BME280 invalide"));
            lastData.dataValid = false;
            measurementState = STATE_IDLE;
            return;
        }

        lastData.temperatureIndoor = bmeData.temperature;
        lastData.humidityIndoor = bmeData.humidity;
        lastData.pressureIndoor = bmeData.pressure;

        lastData.temperatureValid = bmeData.temperatureValid;
        lastData.humidityValid = bmeData.humidityValid;
        lastData.pressureValid = bmeData.pressureValid;
        lastData.dataValid =
            lastData.temperatureValid &&
            lastData.pressureValid;

#if DEBUG
        Serial.print(F("[IndoorSensor] BME280 - Temp : "));
        Serial.print(lastData.temperatureIndoor, 1);
        Serial.print(F(" C / Hum : "));
        Serial.print(lastData.humidityIndoor, 1);
        Serial.print(F(" % / Pression : "));
        Serial.print(lastData.pressureIndoor, 1);
        Serial.println(F(" hPa"));
#endif

        measurementState = STATE_IDLE;
        return;
    }

    // -------------------------------------------------------------------------
    // BME680 : comportement conservé de la phase 1.
    // -------------------------------------------------------------------------

    if (sensorInfo.type == INDOOR_SENSOR_BME680)
    {
        if (measurementState == STATE_IDLE)
        {
            unsigned long elapsedMillis =
                millis() - lastMeasurementStartMillis;

            if (elapsedMillis < BME680_READ_INTERVAL_MS)
            {
                return;
            }

            unsigned long conversionReadyMillis =
                bme680Sensor.beginReading();

            if (conversionReadyMillis == 0)
            {
                Serial.println(F("[IndoorSensor] Erreur : demarrage mesure BME680"));
                return;
            }

            lastMeasurementStartMillis = millis();
            measurementReadyMillis = conversionReadyMillis;
            measurementState = STATE_MEASURING;
            return;
        }

        if (millis() < measurementReadyMillis)
        {
            return;
        }

        if (bme680Sensor.endReading() == false)
        {
            Serial.println(F("[IndoorSensor] Erreur : lecture BME680 invalide"));
            lastData.dataValid = false;
            measurementState = STATE_IDLE;
            return;
        }

        lastData.temperatureIndoor = bme680Sensor.temperature;
        lastData.humidityIndoor = bme680Sensor.humidity;
        lastData.pressureIndoor =
            bme680Sensor.pressure * PASCAL_TO_HECTOPASCAL;

        lastData.temperatureValid = true;
        lastData.humidityValid = true;
        lastData.pressureValid = true;
        lastData.dataValid = true;

#if DEBUG
        Serial.print(F("[IndoorSensor] BME680 - Temp : "));
        Serial.print(lastData.temperatureIndoor, 1);
        Serial.print(F(" C / Hum : "));
        Serial.print(lastData.humidityIndoor, 1);
        Serial.print(F(" % / Pression : "));
        Serial.print(lastData.pressureIndoor, 1);
        Serial.println(F(" hPa"));
#endif

        measurementState = STATE_IDLE;
    }
}

bool IndoorSensor::getData(IndoorData &data)
{
    data = lastData;
    return lastData.dataValid;
}

bool IndoorSensor::getSensorInfo(IndoorSensorInfo &info)
{
    if (sensorInfo.type == INDOOR_SENSOR_NONE)
    {
        return false;
    }

    info = sensorInfo;
    return true;
}

bool IndoorSensor::detectSensor()
{
    for (uint8_t address = 0x08; address <= 0x77; address++)
    {
        if (probeAddress(address) == false)
        {
            continue;
        }

        uint8_t chipId = 0;

        // BME680 / BME280 / BMP280 share the ID register 0xD0.
        if (readI2cRegister(address, 0xD0, chipId))
        {
            if (chipId == 0x61)
            {
                setSensorInfo(
                    INDOOR_SENSOR_BME680, "BME680", address,
                    INDOOR_CAP_PRESSURE |
                    INDOOR_CAP_TEMPERATURE |
                    INDOOR_CAP_HUMIDITY,
                    true);
                return true;
            }

            if (chipId == 0x60)
            {
                setSensorInfo(
                    INDOOR_SENSOR_BME280, "BME280", address,
                    INDOOR_CAP_PRESSURE |
                    INDOOR_CAP_TEMPERATURE |
                    INDOOR_CAP_HUMIDITY,
                    true);
                return true;
            }

            if (chipId == 0x58)
            {
                setSensorInfo(
                    INDOOR_SENSOR_BMP280, "BMP280", address,
                    INDOOR_CAP_PRESSURE |
                    INDOOR_CAP_TEMPERATURE,
                    false);
                return true;
            }
        }

        // BMP390: chip ID register 0x00.
        if (readI2cRegister(address, 0x00, chipId) &&
            chipId == 0x60)
        {
            setSensorInfo(
                INDOOR_SENSOR_BMP390, "BMP390", address,
                INDOOR_CAP_PRESSURE |
                INDOOR_CAP_TEMPERATURE,
                false);
            return true;
        }

        // DPS310: product ID register 0x0D.
        if (readI2cRegister(address, 0x0D, chipId) &&
            chipId == 0x10)
        {
            setSensorInfo(
                INDOOR_SENSOR_DPS310, "DPS310", address,
                INDOOR_CAP_PRESSURE |
                INDOOR_CAP_TEMPERATURE,
                false);
            return true;
        }

        Serial.print(F("[IndoorSensor] Peripherique I2C inconnu a 0x"));
        if (address < 0x10) Serial.print('0');
        Serial.println(address, HEX);
    }

    return false;
}

bool IndoorSensor::readI2cRegister(uint8_t address,
                                   uint8_t reg,
                                   uint8_t &value)
{
    Wire.beginTransmission(address);
    Wire.write(reg);

    if (Wire.endTransmission(false) != 0)
    {
        return false;
    }

    if (Wire.requestFrom(address, (uint8_t)1) != 1)
    {
        return false;
    }

    value = Wire.read();
    return true;
}

bool IndoorSensor::probeAddress(uint8_t address)
{
    Wire.beginTransmission(address);
    return (Wire.endTransmission() == 0);
}

void IndoorSensor::setSensorInfo(IndoorSensorType type,
                                  const char *name,
                                  uint8_t address,
                                  uint8_t capabilities,
                                  bool supported)
{
    sensorInfo.type = type;
    sensorInfo.name = name;
    sensorInfo.i2cAddress = address;
    sensorInfo.capabilities = capabilities;
    sensorInfo.supported = supported;

    Serial.print(F("[IndoorSensor] Detecte : "));
    Serial.print(name);
    Serial.print(F(" a 0x"));
    if (address < 0x10) Serial.print('0');
    Serial.println(address, HEX);
}

void IndoorSensor::resetData()
{
    lastData.temperatureIndoor = 0.0f;
    lastData.humidityIndoor = 0.0f;
    lastData.pressureIndoor = 0.0f;

    lastData.temperatureValid = false;
    lastData.humidityValid = false;
    lastData.pressureValid = false;
    lastData.dataValid = false;
}
