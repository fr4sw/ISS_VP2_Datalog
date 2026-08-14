// ============================================================================
// Fichier : Bmp280Driver.cpp
// Rôle    : Driver BMP280 I2C.
// ============================================================================

#include "Bmp280Driver.h"

// ----------------------------------------------------------------------------
// Registres BMP280
// ----------------------------------------------------------------------------

static const uint8_t REG_CHIP_ID  = 0xD0;
static const uint8_t REG_RESET    = 0xE0;
static const uint8_t REG_STATUS   = 0xF3;
static const uint8_t REG_CTRL_MEAS = 0xF4;
static const uint8_t REG_CONFIG   = 0xF5;
static const uint8_t REG_DATA     = 0xF7;

// ----------------------------------------------------------------------------
// Configuration de mesure
//
// Temperature oversampling = x8
// Pressure oversampling    = x4
// Mode                     = forced
//
// Le calcul du temps de conversion donne environ 29,4 ms.
// Une marge est prise ici : 35 ms.
// ----------------------------------------------------------------------------

static const uint8_t TEMPERATURE_OVERSAMPLING = 0x04;
static const uint8_t PRESSURE_OVERSAMPLING    = 0x03;

static const unsigned long MEASUREMENT_TIME_MS = 35;

// ----------------------------------------------------------------------------
// begin
// ----------------------------------------------------------------------------

bool Bmp280Driver::begin(TwoWire &wire, uint8_t address)
{
    i2c = &wire;
    i2cAddress = address;

    measurementInProgress = false;
    measurementReadyMillis = 0;
    initialized = false;

    // ------------------------------------------------------------------------
    // Vérification du CHIP_ID
    // ------------------------------------------------------------------------

    uint8_t chipId = 0;

    if (readRegisters(REG_CHIP_ID, &chipId, 1) == false)
    {
        return false;
    }

    if (chipId != 0x58)
    {
        return false;
    }

    // ------------------------------------------------------------------------
    // Soft reset
    // ------------------------------------------------------------------------

    if (writeRegister(REG_RESET, 0xB6) == false)
    {
        return false;
    }

    delay(3);

    // ------------------------------------------------------------------------
    // Lecture des coefficients de calibration
    // ------------------------------------------------------------------------

    if (readCalibration() == false)
    {
        return false;
    }

    // ------------------------------------------------------------------------
    // Configuration
    //
    // IIR filter désactivé pour cette première version.
    // ------------------------------------------------------------------------

    if (writeRegister(REG_CONFIG, 0x00) == false)
    {
        return false;
    }

    // Préparation des oversamplings.
    // Le mode reste sleep ici.
    if (writeRegister(
            REG_CTRL_MEAS,
            (TEMPERATURE_OVERSAMPLING << 5) |
            (PRESSURE_OVERSAMPLING << 2)) == false)
    {
        return false;
    }

    initialized = true;

    return true;
}

// ----------------------------------------------------------------------------
// startMeasurement
// ----------------------------------------------------------------------------

bool Bmp280Driver::startMeasurement()
{
    if (initialized == false)
    {
        return false;
    }

    if (measurementInProgress == true)
    {
        return false;
    }

    // ------------------------------------------------------------------------
    // Forced mode = 01
    // ------------------------------------------------------------------------

    uint8_t ctrlMeas =
        (TEMPERATURE_OVERSAMPLING << 5) |
        (PRESSURE_OVERSAMPLING << 2) |
        0x01;

    if (writeRegister(REG_CTRL_MEAS, ctrlMeas) == false)
    {
        return false;
    }

    measurementReadyMillis =
        millis() + MEASUREMENT_TIME_MS;

    measurementInProgress = true;

    return true;
}

// ----------------------------------------------------------------------------
// measurementDone
// ----------------------------------------------------------------------------

bool Bmp280Driver::measurementDone()
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

    // STATUS.measuring = bit 3.
    //
    // 0 : conversion terminée
    // 1 : conversion en cours

    if ((status & 0x08) != 0)
    {
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// readMeasurement
// ----------------------------------------------------------------------------

bool Bmp280Driver::readMeasurement(Bmp280Data &data)
{
    if (measurementInProgress == false)
    {
        return false;
    }

    if (measurementDone() == false)
    {
        return false;
    }

    // ------------------------------------------------------------------------
    // 0xF7..0xF9 : pression
    // 0xFA..0xFC : température
    // ------------------------------------------------------------------------

    uint8_t raw[6];

    if (readRegisters(REG_DATA, raw, sizeof(raw)) == false)
    {
        measurementInProgress = false;
        return false;
    }

    int32_t adcPressure =
        ((int32_t)raw[0] << 12) |
        ((int32_t)raw[1] << 4) |
        ((int32_t)raw[2] >> 4);

    int32_t adcTemperature =
        ((int32_t)raw[3] << 12) |
        ((int32_t)raw[4] << 4) |
        ((int32_t)raw[5] >> 4);

    // Valeur "skipped" définie par Bosch.
    if (adcPressure == 0x80000 ||
        adcTemperature == 0x80000)
    {
        measurementInProgress = false;

        data.temperatureValid = false;
        data.pressureValid = false;

        return false;
    }

    // ------------------------------------------------------------------------
    // Compensation température.
    //
    // tFine est ensuite utilisé pour la compensation pression.
    // ------------------------------------------------------------------------

    int32_t tFine = 0;

    data.temperature =
        compensateTemperature(adcTemperature, tFine);

    data.pressure =
        compensatePressure(adcPressure, tFine);

    data.temperatureValid = true;
    data.pressureValid = true;

    measurementInProgress = false;

    return true;
}

// ----------------------------------------------------------------------------
// readRegisters
// ----------------------------------------------------------------------------

bool Bmp280Driver::readRegisters(uint8_t reg,
                                  uint8_t *buffer,
                                  size_t length)
{
    i2c->beginTransmission(i2cAddress);
    i2c->write(reg);

    if (i2c->endTransmission(false) != 0)
    {
        return false;
    }

    if (i2c->requestFrom(i2cAddress,
                         (uint8_t)length) != length)
    {
        return false;
    }

    for (size_t i = 0; i < length; i++)
    {
        buffer[i] = i2c->read();
    }

    return true;
}

// ----------------------------------------------------------------------------
// writeRegister
// ----------------------------------------------------------------------------

bool Bmp280Driver::writeRegister(uint8_t reg,
                                  uint8_t value)
{
    i2c->beginTransmission(i2cAddress);

    i2c->write(reg);
    i2c->write(value);

    return (i2c->endTransmission() == 0);
}

// ----------------------------------------------------------------------------
// readCalibration
// ----------------------------------------------------------------------------

bool Bmp280Driver::readCalibration()
{
    uint8_t calibrationData[24];

    if (readRegisters(0x88,
                      calibrationData,
                      sizeof(calibrationData)) == false)
    {
        return false;
    }

    calibration.dig_T1 =
        (uint16_t)calibrationData[0] |
        ((uint16_t)calibrationData[1] << 8);

    calibration.dig_T2 =
        (int16_t)(
            (uint16_t)calibrationData[2] |
            ((uint16_t)calibrationData[3] << 8));

    calibration.dig_T3 =
        (int16_t)(
            (uint16_t)calibrationData[4] |
            ((uint16_t)calibrationData[5] << 8));

    calibration.dig_P1 =
        (uint16_t)calibrationData[6] |
        ((uint16_t)calibrationData[7] << 8);

    calibration.dig_P2 =
        (int16_t)(
            (uint16_t)calibrationData[8] |
            ((uint16_t)calibrationData[9] << 8));

    calibration.dig_P3 =
        (int16_t)(
            (uint16_t)calibrationData[10] |
            ((uint16_t)calibrationData[11] << 8));

    calibration.dig_P4 =
        (int16_t)(
            (uint16_t)calibrationData[12] |
            ((uint16_t)calibrationData[13] << 8));

    calibration.dig_P5 =
        (int16_t)(
            (uint16_t)calibrationData[14] |
            ((uint16_t)calibrationData[15] << 8));

    calibration.dig_P6 =
        (int16_t)(
            (uint16_t)calibrationData[16] |
            ((uint16_t)calibrationData[17] << 8));

    calibration.dig_P7 =
        (int16_t)(
            (uint16_t)calibrationData[18] |
            ((uint16_t)calibrationData[19] << 8));

    calibration.dig_P8 =
        (int16_t)(
            (uint16_t)calibrationData[20] |
            ((uint16_t)calibrationData[21] << 8));

    calibration.dig_P9 =
        (int16_t)(
            (uint16_t)calibrationData[22] |
            ((uint16_t)calibrationData[23] << 8));

    // P1 et T1 ne peuvent pas être nuls pour un composant correctement
    // calibré.
    if (calibration.dig_T1 == 0 ||
        calibration.dig_P1 == 0)
    {
        return false;
    }

    return true;
}

// ----------------------------------------------------------------------------
// compensateTemperature
// ----------------------------------------------------------------------------

float Bmp280Driver::compensateTemperature(int32_t adcTemperature,
                                           int32_t &tFine)
{
    float var1 =
        (((float)adcTemperature / 16384.0f) -
         ((float)calibration.dig_T1 / 1024.0f)) *
        (float)calibration.dig_T2;

    float var2 =
        (((float)adcTemperature / 131072.0f) -
         ((float)calibration.dig_T1 / 8192.0f));

    var2 =
        var2 *
        var2 *
        (float)calibration.dig_T3;

    float fineTemperature =
        var1 + var2;

    tFine = (int32_t)fineTemperature;

    return fineTemperature / 5120.0f;
}

// ----------------------------------------------------------------------------
// compensatePressure
// ----------------------------------------------------------------------------

float Bmp280Driver::compensatePressure(int32_t adcPressure,
                                        int32_t tFine)
{
    float var1 =
        ((float)tFine / 2.0f) - 64000.0f;

    float var2 =
        var1 *
        var1 *
        ((float)calibration.dig_P6 / 32768.0f);

    var2 =
        var2 +
        var1 *
        ((float)calibration.dig_P5 * 2.0f);

    var2 =
        (var2 / 4.0f) +
        ((float)calibration.dig_P4 * 65536.0f);

    var1 =
        (((float)calibration.dig_P3 *
          var1 *
          var1) /
         524288.0f) +
        ((float)calibration.dig_P2 * var1);

    var1 =
        (var1 / 524288.0f) +
        (float)calibration.dig_P1;

    if (var1 == 0.0f)
    {
        return 0.0f;
    }

    float pressure =
        1048576.0f -
        (float)adcPressure;

    pressure =
        (pressure - (var2 / 4096.0f)) *
        6250.0f /
        var1;

    var1 =
        ((float)calibration.dig_P9 *
         pressure *
         pressure) /
        2147483648.0f;

    var2 =
        pressure *
        ((float)calibration.dig_P8 / 32768.0f);

    pressure +=
        (var1 + var2 +
         ((float)calibration.dig_P7)) /
        16.0f;

    // Compensation Bosch : résultat en Pa.
    // Interface IndoorData : hPa.
    return pressure * 0.01f;
}