// ============================================================================
// Fichier      : BoardConfig.h
// Rôle         : Définition exhaustive des GPIO, débits et fréquences bus.
//                Aucune logique, aucun test fonctionnel.
// Référence    : CodingRules_Gen.md §7, §9
// Remarque     : Ne jamais definir PIN_LED a partir de LED_BUILTIN sur ce
//                core : LED_BUILTIN est lui-meme defini a partir de
//                PIN_LED dans variant.h, ce qui cree une reference
//                circulaire si on les redefinit l'un via l'autre.
//                On utilise directement la constante native LED_RED du
//                variant, qui ne depend d'aucune autre macro.
// ============================================================================
#pragma once
#include <Arduino.h>

//undef pour éviter les warnings
#undef PIN_LED
#define PIN_LED   LED_RED   // LED rouge native de la carte XIAO nRF52840
//==============Partie === ESP32 ================================================================================
#if defined(ARDUINO_ARCH_ESP32)

  #define PIN_RADIO_CS     5
  #define PIN_RADIO_SCK    18
  #define PIN_RADIO_MISO   19
  #define PIN_RADIO_MOSI   23
  #define PIN_RADIO_RST    4
  #define PIN_RADIO_IRQ    2

  #define PIN_RS485_RX     16
  #define PIN_RS485_TX     -1

  #define PIN_I2C_SDA      21
  #define PIN_I2C_SCL      22
  #define I2C_SPEED        100000UL   // I2C standard mode (100 kHz)


  #define PIN_GPS_RX       34
  #define PIN_GPS_TX       -1

  #define PIN_SD_CS        15
  #define PIN_SD_SCK       18
  #define PIN_SD_MISO      19
  #define PIN_SD_MOSI      23

  #define PIN_LED          2

  #define UART_ISS_BAUD    19200   // FSK, débit natif module ISS Davis
  #define UART_RS485_BAUD  9600    // RS485 filaire, mesuré terrain
  #define SPI_SD_FREQUENCY 4000000

//==============Partie === nRF52 ================================================================================
#elif defined(ARDUINO_ARCH_NRF52)

  // RS485 : reception seule (RO du transceiver -> RX du MCU).
  // PIN_RS485_TX volontairement absent : la broche D6 reste disponible
  // pour un autre usage (ex : LED, GPIO auxiliaire).
  #define PIN_RS485_RX     4    // D4 P0.04
  #define PIN_RS485_TX     -1   // D4 P0.04

  // GPS : reception seule (module GPS -> RX du MCU).
  // PIN_GPS_TX volontairement absent : la broche D1 reste disponible.
  #define PIN_GPS_RX       10   // D10 P1.15
  // PIN_GPS_ENABLE active l'alimentation du GPS avec un 0
  #define PIN_GPS_ENABLE   5    // D5 P0.05

  // Meshtastic : Tx-Rx, commuté avec GPS selon les ENABLE
  #define PIN_MESH_RX      10   // D10 P1.15
  #define PIN_MESH_TX      9    // D9 P1.14
  // PIN_MESH_ENABLE active l'alimentation du Mesh avec un 0
  #define PIN_MESH_ENABLE  6    // D6 P1.11

  // Niveaux logiques des broches ENABLE (rappel explicite, evite toute
  // ambiguite sur le sens actif/inactif dans Power.cpp).
  #define POWER_ENABLE_ACTIVE_LEVEL    LOW
  #define POWER_ENABLE_INACTIVE_LEVEL  HIGH
  
  #define PIN_I2C_SDA      7    // D7 P1.12
  #define PIN_I2C_SCL      8    // D8 P1.13
  #define I2C_SPEED        100000UL   // I2C standard mode (100 kHz)

// Les macros PIN_SPI_SCK/MISO/MOSI du core restent inchangees (D8/D9/D10).
// Nos broches reelles (D0/D1/D2) sont conservees sous un nom distinct
// pour eviter toute confusion avec les macros natives du core.
#define PIN_SD_SPI_SCK    0    // D0 / P0.02
#define PIN_SD_SPI_MISO   2    // D2 / P0.28
#define PIN_SD_SPI_MOSI   1    // D1 / P0.03
#define PIN_SD_CS         3    // D3 / P0.29
#define SPI_SD_FREQUENCY 4000000

#else
  #error "BoardConfig.h : cible non definie"
#endif

//==============Partie === Commune ================================================================================

// Adresse I2C du capteur interieur BME680 : 0x76 si SDO relie a GND (cablage
// retenu sur ce projet), 0x77 si SDO relie a VCC. Constante materielle liee
// au cablage, independante de l'architecture du microcontroleur.
#define BME680_I2C_ADDRESS   0x76

// Debit GPS
#define UART_GPS_BAUD    9600
