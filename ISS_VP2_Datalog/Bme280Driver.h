// ============================================================================
// Fichier : Bme280Driver.h
// Rôle    : Driver BME280 I2C, acquisition non bloquante en mode forcé.
// Référence : Bosch BME280 datasheet BST-BME280-DS002.
// ============================================================================

#pragma once

#include <Arduino.h>
#include <Wire.h>

struct Bme280Data
{
    float temperature;
    float humidity;
    float pressure;
    bool temperatureValid;
    bool humidityValid;
    bool pressureValid;
};

class Bme280Driver
{
public:
    bool begin(TwoWire &wire, uint8_t address);
    bool startMeasurement();
    bool measurementDone();
    bool readMeasurement(Bme280Data &data);

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

        uint8_t  dig_H1;
        int16_t  dig_H2;
        uint8_t  dig_H3;
        int16_t  dig_H4;
        int16_t  dig_H5;
        int8_t   dig_H6;
    };

    bool readRegisters(uint8_t reg, uint8_t *buffer, size_t length);
    bool writeRegister(uint8_t reg, uint8_t value);
    bool readCalibration();

    float compensateTemperature(int32_t adc_T, int32_t &tFine);
    float compensatePressure(int32_t adc_P, int32_t tFine);
    float compensateHumidity(int32_t adc_H, int32_t tFine);

    TwoWire *i2c;
    uint8_t i2cAddress;
    Calibration calibration;

    unsigned long measurementReadyMillis;
    bool measurementInProgress;
    bool initialized;
};
