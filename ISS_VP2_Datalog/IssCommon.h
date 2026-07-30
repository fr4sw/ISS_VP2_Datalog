// ============================================================================
// Fichier   : IssCommon.h
// Rôle      : Types et fonctions de decodage partages entre IssRs485 et
//             IssWireless. Un seul point d'appel dans le programme
//             principal, quelle que soit la source physique de la trame.
// Fonctions : checkCRC()   - verifie la somme de controle (FSK uniquement).
//             decodeFrame()- interprete les champs, ne verifie pas le CRC.
// Référence : Davis Vantage Pro/Pro2/Vue Serial Protocol,
//             support.davisinstruments.com (CRC-16/CCITT, polynome 0x1021)
// ============================================================================
// mapping des types conforme a
//             l'ancien programme ESP32/SX1276, valable aussi bien en
//             FSK radio qu'en RS485 filaire)
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

#define ISS_TYPE_SUPERCAP    0x2
#define ISS_TYPE_UV          0x4
#define ISS_TYPE_RAINRATE    0x5
#define ISS_TYPE_SOLAR       0x6
#define ISS_TYPE_TEMP        0x8
#define ISS_TYPE_WINDGUST    0x9
#define ISS_TYPE_HUMIDITY    0xA
#define ISS_TYPE_RAIN        0xE

struct IssRawFrame
{
    uint8_t bytes[ISS_FRAME_MAX_LENGTH];
    uint8_t length;
    bool    hasCrc;
};

struct IssData
{
    uint8_t  sensorType;
    uint8_t  stationId;
    uint8_t  batteryLow;
    uint8_t  windSpeedKph;
    uint16_t windDirectionDeg;
    float    temperatureOutside;
    float    humidityOutside;
    uint16_t solarRadiation;
    float    uvIndex;
    float    rainRateMmPerHour;
    uint16_t rainTipCount;
    uint16_t windGustKph;
    bool     frameValid;
};

bool checkCRC(const uint8_t frameBytes[], uint8_t frameLength);
bool decodeFrame(const IssRawFrame &rawFrame, IssData &result);
