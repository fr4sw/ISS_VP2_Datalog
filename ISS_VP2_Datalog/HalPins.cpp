// ============================================================================
// Fichier   : HalPins.cpp
// ============================================================================
#include <Arduino.h>
#include <Wire.h>
#include "HalPins.h"

// Remarque   : Cast vers NRF_UARTE_Type*, pas NRF_UART_Type*. L'adresse
//             memoire NRF_UARTE1_BASE reste correcte (confirmee dans
//             nrf52840.h), seul le type de la structure de registres
//             pointee etait errone.
// ============================================================================
#if defined(ARDUINO_ARCH_NRF52)
Uart Serial2(
    (NRF_UARTE_Type *)NRF_UARTE1_BASE,
    UARTE1_IRQn,
    PIN_MESH_RX,
    PIN_MESH_TX
);

extern "C" void UARTE1_IRQHandler()
{
    Serial2.IrqHandler();
}
#endif


void configureRs485Pins()
{
#if defined(ARDUINO_ARCH_NRF52)
    // Ce core exige setPins() avant begin(), l'ESP32 ne l'exige pas
    // (les broches sont passees directement a begin()).
    Serial1.setPins(PIN_RS485_RX, PIN_RS485_TX);
#endif
    // Sur ESP32, rien a faire ici : IssRs485.cpp appelle directement
    // Serial2.begin(baud, config, rx, tx) avec les broches en parametre.
}

void configureI2cPins()
{
#if defined(ARDUINO_ARCH_NRF52)
    Wire.setPins(PIN_I2C_SDA, PIN_I2C_SCL);
#endif
    // Sur ESP32, Wire.begin(sda, scl) prend les broches directement.
}

void beginI2cBus()
{
    // Idempotent : begin() n'est execute qu'une seule fois, meme si
    // plusieurs modules (BmeIndoor, Rtc, ...) partagent le meme bus I2C
    // et appellent chacun cette fonction depuis leur propre begin().
    static bool i2cBusStarted = false;
    if (i2cBusStarted == true)
    {
        return;
    }

    configureI2cPins();
#if defined(ARDUINO_ARCH_NRF52)
    Wire.begin();
#elif defined(ARDUINO_ARCH_ESP32)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
#endif
    Wire.setClock(I2C_SPEED);

    i2cBusStarted = true;
}

void configureSpiPins()
{
#if defined(ARDUINO_ARCH_NRF52)
    SPI.setPins(PIN_SD_SPI_MISO, PIN_SD_SPI_SCK, PIN_SD_SPI_MOSI);
#endif
    // Sur ESP32, SPI.begin(sck, miso, mosi, cs) prend les broches directement.
}
