// ============================================================================
// Fichier : Bme280Driver.cpp
// Rôle    : Driver BME280 I2C.
// ============================================================================

#include "Bme280Driver.h"

static const uint8_t REG_STATUS       = 0xF3;
static const uint8_t REG_CTRL_HUM     = 0xF2;
static const uint8_t REG_CTRL_MEAS    = 0xF4;
static const uint8_t REG_CONFIG       = 0xF5;
static const uint8_t REG_DATA         = 0xF7;

static const uint8_t OSRS_T_8X = 0x04;
static const uint8_t OSRS_P_4X = 0x03;
static const uint8_t OSRS_H_2X = 0x02;

// IIR filter coefficient 4.
// For a weather station, filtering pressure is useful against short
// disturbances while retaining normal atmospheric variations.
static const uint8_t FILTER_4 = 0x02;

// Measurement time from Bosch datasheet formula:
// t_meas = 1.25 + 2.3*T + (2.3*P + 0.575) + (2.3*H + 0.575) ms
// for T=8x, P=4x, H=2x:
// 1.25 + 18.4 + 9.775 + 5.175 = 34.6 ms.
// Add margin for robust polling on the MCU.
static const unsigned long MEASUREMENT_TIME_MS = 40;

bool Bme280Driver::begin(TwoWire &wire, uint8_t address)
{
    i2c = &wire;
    i2cAddress = address;
    measurementInProgress = false;
    measurementReadyMillis = 0;
    initialized = false;

    uint8_t chipId = 0;

    if (readRegisters(0xD0, &chipId, 1) == false)
    {
        return false;
    }

    if (chipId != 0x60)
    {
        return false;
    }

    // Soft reset.
    if (writeRegister(0xE0, 0xB6) == false)
    {
        return false;
    }

    // Bosch specifies a startup delay after reset before reading calibration.
    delay(3);

    if (readCalibration() == false)
    {
        return false;
    }

    // Humidity oversampling must be written before CTRL_MEAS because the
    // humidity setting is latched when CTRL_MEAS is written.
    if (writeRegister(REG_CTRL_HUM, OSRS_H_2X) == false)
    {
        return false;
    }

    // Temperature 8x, pressure 4x, forced mode = 01.
    uint8_t ctrlMeas =
        (OSRS_T_8X << 5) |
        (OSRS_P_4X << 2) |
        0x01;

    if (writeRegister(REG_CTRL_MEAS, ctrlMeas) == false)
    {
        return false;
    }

    // Config: filter coefficient 4, standby field is irrelevant in forced mode.
    uint8_t config = (FILTER_4 << 2);

    if (writeRegister(REG_CONFIG, config) == false)
    {
        return false;
    }

    // Return to sleep. The next startMeasurement() will select forced mode.
    if (writeRegister(REG_CTRL_MEAS,
                      (OSRS_T_8X << 5) | (OSRS_P_4X << 2)) == false)
    {
        return false;
    }

    initialized = true;
    return true;
}

bool Bme280Driver::startMeasurement()
{
    if (initialized == false || measurementInProgress == true)
    {
        return false;
    }

    // Humidity setting remains configured; writing CTRL_MEAS starts a new
    // forced measurement.
    uint8_t ctrlMeas =
        (OSRS_T_8X << 5) |
        (OSRS_P_4X << 2) |
        0x01;

    if (writeRegister(REG_CTRL_MEAS, ctrlMeas) == false)
    {
        return false;
    }

    measurementReadyMillis = millis() + MEASUREMENT_TIME_MS;
    measurementInProgress = true;

    return true;
}

bool Bme280Driver::measurementDone()
{
    if (measurementInProgress == false)
    {
        return false;
    }

    if ((long)(millis() - measurementReadyMillis) < 0)
    {
        return false;
    }

    uint8_t status = 0;

    if (readRegisters(REG_STATUS, &status, 1) == false)
    {
        return false;
    }

    // measuring bit is bit 3. If still set, conversion is not finished.
    if ((status & 0x08) != 0)
    {
        return false;
    }

    return true;
}

bool Bme280Driver::readMeasurement(Bme280Data &data)
{
    if (measurementInProgress == false)
    {
        return false;
    }

    if (measurementDone() == false)
    {
        return false;
    }

    uint8_t raw[8];

    if (readRegisters(REG_DATA, raw, sizeof(raw)) == false)
    {
        measurementInProgress = false;
        return false;
    }

    int32_t adcP =
        ((int32_t)raw[0] << 12) |
        ((int32_t)raw[1] << 4) |
        ((int32_t)raw[2] >> 4);

    int32_t adcT =
        ((int32_t)raw[3] << 12) |
        ((int32_t)raw[4] << 4) |
        ((int32_t)raw[5] >> 4);

    int32_t adcH =
        ((int32_t)raw[6] << 8) |
        (int32_t)raw[7];

    int32_t tFine = 0;

    data.temperature = compensateTemperature(adcT, tFine);
    data.pressure = compensatePressure(adcP, tFine);
    data.humidity = compensateHumidity(adcH, tFine);

    data.temperatureValid = (adcT != 0x80000);
    data.pressureValid = (adcP != 0x80000);
    data.humidityValid = (adcH != 0x8000);

    measurementInProgress = false;

    return data.temperatureValid &&
           data.pressureValid &&
           data.humidityValid;
}

bool Bme280Driver::readRegisters(uint8_t reg,
                                  uint8_t *buffer,
                                  size_t length)
{
    i2c->beginTransmission(i2cAddress);
    i2c->write(reg);

    if (i2c->endTransmission(false) != 0)
    {
        return false;
    }

    size_t received = i2c->requestFrom(i2cAddress,
                                       (uint8_t)length);

    if (received != length)
    {
        return false;
    }

    for (size_t i = 0; i < length; i++)
    {
        buffer[i] = i2c->read();
    }

    return true;
}

bool Bme280Driver::writeRegister(uint8_t reg, uint8_t value)
{
    i2c->beginTransmission(i2cAddress);
    i2c->write(reg);
    i2c->write(value);

    return (i2c->endTransmission() == 0);
}

bool Bme280Driver::readCalibration()
{
    uint8_t c1[24];

    if (readRegisters(0x88, c1, sizeof(c1)) == false)
    {
        return false;
    }

    calibration.dig_T1 = (uint16_t)c1[0] | ((uint16_t)c1[1] << 8);
    calibration.dig_T2 = (int16_t)((uint16_t)c1[2] | ((uint16_t)c1[3] << 8));
    calibration.dig_T3 = (int16_t)((uint16_t)c1[4] | ((uint16_t)c1[5] << 8));

    calibration.dig_P1 = (uint16_t)c1[6] | ((uint16_t)c1[7] << 8);
    calibration.dig_P2 = (int16_t)((uint16_t)c1[8] | ((uint16_t)c1[9] << 8));
    calibration.dig_P3 = (int16_t)((uint16_t)c1[10] | ((uint16_t)c1[11] << 8));
    calibration.dig_P4 = (int16_t)((uint16_t)c1[12] | ((uint16_t)c1[13] << 8));
    calibration.dig_P5 = (int16_t)((uint16_t)c1[14] | ((uint16_t)c1[15] << 8));
    calibration.dig_P6 = (int16_t)((uint16_t)c1[16] | ((uint16_t)c1[17] << 8));
    calibration.dig_P7 = (int16_t)((uint16_t)c1[18] | ((uint16_t)c1[19] << 8));
    calibration.dig_P8 = (int16_t)((uint16_t)c1[20] | ((uint16_t)c1[21] << 8));
    calibration.dig_P9 = (int16_t)((uint16_t)c1[22] | ((uint16_t)c1[23] << 8));

    uint8_t h1 = 0;
    if (readRegisters(0xA1, &h1, 1) == false)
    {
        return false;
    }

    calibration.dig_H1 = h1;

    uint8_t h[7];

    if (readRegisters(0xE1, h, sizeof(h)) == false)
    {
        return false;
    }

    calibration.dig_H2 =
        (int16_t)((uint16_t)h[0] | ((uint16_t)h[1] << 8));

    calibration.dig_H3 = h[2];

    // H4 is a signed 12-bit value:
    // H4[11:4] = E4, H4[3:0] = E5[3:0].
    int16_t h4 =
        (int16_t)(((uint16_t)h[3] << 4) | (h[4] & 0x0F));

    // H5 is a signed 12-bit value:
    // H5[11:4] = E6, H5[3:0] = E5[7:4].
    int16_t h5 =
        (int16_t)(((uint16_t)h[5] << 4) | (h[4] >> 4));

    // Sign extend 12-bit values.
    if ((h4 & 0x0800) != 0)
    {
        h4 |= (int16_t)0xF000;
    }

    if ((h5 & 0x0800) != 0)
    {
        h5 |= (int16_t)0xF000;
    }

    calibration.dig_H4 = h4;
    calibration.dig_H5 = h5;
    calibration.dig_H6 = (int8_t)h[6];

    // A zero pressure calibration coefficient is not valid for normal
    // operation and indicates that calibration data could not be read.
    if (calibration.dig_P1 == 0)
    {
        return false;
    }

    if (calibration.dig_T1 == 0)
    {
        return false;
    }

    return true;
}

float Bme280Driver::compensateTemperature(int32_t adc_T, int32_t &tFine)
{
    float var1 =
        (((float)adc_T / 16384.0f) -
         ((float)calibration.dig_T1 / 1024.0f)) *
        (float)calibration.dig_T2;

    float var2 =
        (((float)adc_T / 131072.0f) -
         ((float)calibration.dig_T1 / 8192.0f));

    var2 = var2 * var2 * (float)calibration.dig_T3;

    float fine = var1 + var2;
    tFine = (int32_t)fine;

    return fine / 5120.0f;
}

float Bme280Driver::compensatePressure(int32_t adc_P, int32_t tFine)
{
    float var1 = ((float)tFine / 2.0f) - 64000.0f;
    float var2 = var1 * var1 *
                 ((float)calibration.dig_P6 / 32768.0f);
    var2 = var2 +
           var1 * ((float)calibration.dig_P5 * 2.0f);
    var2 = (var2 / 4.0f) +
           ((float)calibration.dig_P4 * 65536.0f);

    var1 = (((float)calibration.dig_P3 * var1 * var1) /
            524288.0f) +
           ((float)calibration.dig_P2 * var1);

    var1 = (var1 / 524288.0f) +
           ((float)calibration.dig_P1);

    if (var1 == 0.0f)
    {
        return 0.0f;
    }

    float pressure = 1048576.0f - (float)adc_P;
    pressure = (pressure - (var2 / 4096.0f)) * 6250.0f / var1;

    var1 = ((float)calibration.dig_P9 *
            pressure * pressure) / 2147483648.0f;

    var2 = pressure *
           ((float)calibration.dig_P8 / 32768.0f);

    pressure += (var1 + var2 +
                 ((float)calibration.dig_P7)) / 16.0f;

    // Bosch compensation result is Pa.
    return pressure * 0.01f;
}

float Bme280Driver::compensateHumidity(int32_t adc_H, int32_t tFine)
{
    float humidity =
        ((float)tFine) - 76800.0f;

    humidity =
        (float)adc_H -
        (((float)calibration.dig_H4 * 64.0f) +
         ((float)calibration.dig_H5 / 16384.0f) * humidity);

    humidity =
        humidity *
        ((float)calibration.dig_H2 / 65536.0f) *
        (1.0f +
         ((float)calibration.dig_H6 / 67108864.0f) * humidity *
         (1.0f +
          ((float)calibration.dig_H3 / 67108864.0f) * humidity));

    humidity =
        humidity *
        (1.0f -
         ((float)calibration.dig_H1 / 524288.0f) * humidity);

    if (humidity > 100.0f)
    {
        humidity = 100.0f;
    }

    if (humidity < 0.0f)
    {
        humidity = 0.0f;
    }

    return humidity;
}
