// ============================================================================
// Fichier : IndoorSensorTypes.h
// Rôle    : Types communs aux capteurs environnementaux I2C.
// ============================================================================

#pragma once

#include <Arduino.h>

enum IndoorSensorType : uint8_t
{
    INDOOR_SENSOR_NONE = 0,
    INDOOR_SENSOR_BME680,
    INDOOR_SENSOR_BME280,
    INDOOR_SENSOR_BMP280,
    INDOOR_SENSOR_BMP390,
    INDOOR_SENSOR_DPS310
};

enum IndoorSensorCapability : uint8_t
{
    INDOOR_CAP_PRESSURE    = 0x01,
    INDOOR_CAP_TEMPERATURE = 0x02,
    INDOOR_CAP_HUMIDITY    = 0x04
};

struct IndoorSensorInfo
{
    IndoorSensorType type;
    const char *name;
    uint8_t i2cAddress;
    uint8_t capabilities;
    bool supported;
};

struct IndoorData
{
    float temperatureIndoor;
    float humidityIndoor;
    float pressureIndoor;

    bool temperatureValid;
    bool humidityValid;
    bool pressureValid;

    bool dataValid;
};
