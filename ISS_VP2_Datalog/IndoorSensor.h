// ============================================================================
// Fichier : IndoorSensor.h
// Rôle    : Interface générique du capteur environnemental intérieur.
// ============================================================================

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>

#include "BoardConfig.h"
#include "Config.h"
#include "IndoorSensorTypes.h"
#include "Bme280Driver.h"
#include "Bmp280Driver.h"

class IndoorSensor
{
public:
    void begin();
    void update();
    bool getData(IndoorData &data);
    bool getSensorInfo(IndoorSensorInfo &info);

private:
    enum State
    {
        STATE_IDLE,
        STATE_MEASURING
    };

    bool detectSensor();
    bool readI2cRegister(uint8_t address, uint8_t reg, uint8_t &value);
    bool probeAddress(uint8_t address);

    void resetData();
    void setSensorInfo(IndoorSensorType type,
                       const char *name,
                       uint8_t address,
                       uint8_t capabilities,
                       bool supported);

    Adafruit_BME680 bme680Sensor;
    Bme280Driver bme280Sensor;
    Bmp280Driver bmp280Sensor;

    IndoorSensorInfo sensorInfo;
    IndoorData lastData;

    State measurementState;
    unsigned long lastMeasurementStartMillis;
    unsigned long measurementReadyMillis;
    bool sensorReady;
};

extern IndoorSensor indoorSensor;
