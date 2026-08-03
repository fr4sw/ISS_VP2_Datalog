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

// Direction du vent : la console Davis quantifie la direction sur 16
// secteurs de la rose des vents (22.5 degres chacun) pour determiner la
// direction "dominante" d'un intervalle d'archivage (majorite des
// echantillons). Nord = 0 degre (jamais 360, convention WeeWx/Weatherlink).
// Constantes partagees entre IssCommon (conversion brute) et DataLogger
// (histogramme de synthese sur l'intervalle).
// Référence : docs/Davis_Data_Archived_v3.pdf (definition champ "Wind Dir")
#define WIND_DIR_SECTOR_COUNT       16
#define WIND_DIR_SECTOR_WIDTH_DEG   22.5f

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

// Frequence de transmission theorique de l'ISS, utilisee pour calculer un
// taux de reception (trames recues / trames attendues sur un intervalle).
// Chaque station transmet en moyenne toutes les 2.5s (ID 1, "raw" 0), avec
// un leger supplement par ID pour eviter les collisions entre stations
// partageant la meme frequence radio.
// Référence : VantageSerialProtocolDocs_v261.pdf, §XIV.6 "Calculating ISS
// reception" (formule VP2/Vantage Vue). stationId ici est le champ brut
// 0-7 extrait de la trame, qui correspond directement a "ID-1" de la
// documentation Davis (ID humain 1-8, affiche sur le dip-switch).
#define ISS_BASE_SECONDS_PER_PACKET       2.5f
#define ISS_EXTRA_SECONDS_PER_PACKET_ID   (1.0f / 16.0f)

float issSecondsPerPacket(uint8_t stationId);
bool checkCRC(const uint8_t frameBytes[], uint8_t frameLength);
bool decodeFrame(const IssRawFrame &rawFrame, IssData &result);

// Reutilisees par DataLogger pour l'histogramme de direction dominante
// (règle 6 : la correspondance secteur<->degre n'existe qu'a un seul
// endroit, meme si elle est utilisee par deux modules).
uint8_t  windDirectionSectorFromDeg(uint16_t degrees);
uint16_t windDirectionDegFromSector(uint8_t sector);
