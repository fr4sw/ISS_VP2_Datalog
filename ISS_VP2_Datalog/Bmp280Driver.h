// ============================================================================
// Fichier : Bmp280Driver.h
// Rôle    : Driver BMP280 I2C.
// ============================================================================

#pragma once

#include <Arduino.h>
#include <Wire.h>

struct Bmp280Data
{
    float temperature;
    float pressure;

    bool temperatureValid;
    bool pressureValid;
};

class Bmp280Driver
{
public:
    bool begin(TwoWire &wire, uint8_t address);

    bool startMeasurement();
    bool measurementDone();
    bool readMeasurement(Bmp280Data &data);

private:
    struct Calibration
    {
        uint16_t dig_T1;
        int16_t  dig_T2;
        int16_t  dig_T3;

        uint16_t dig_P1;
        int16_t  dig_P2;
        int16_t  dig_P3;
        int16_t  dig_P4;
        int16_t  dig_P5;
        int16_t  dig_P6;
        int16_t  dig_P7;
        int16_t  dig_P8;
        int16_t  dig_P9;
    };

    bool readRegisters(uint8_t reg,
                       uint8_t *buffer,
                       size_t length);

    bool writeRegister(uint8_t reg,
                       uint8_t value);

    bool readCalibration();

    float compensateTemperature(int32_t adcTemperature,
                                int32_t &tFine);

    float compensatePressure(int32_t adcPressure,
                             int32_t tFine);

    TwoWire *i2c;
    uint8_t i2cAddress;

    Calibration calibration;

    unsigned long measurementReadyMillis;

    bool measurementInProgress;
    bool initialized;
};