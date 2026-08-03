// ============================================================================
// Fichier   : IssCommon.cpp
// Rôle      : Decodage unique des trames Davis ISS, quelle que soit la
//             source physique (FSK sans fil ou RS485 filaire). Ne calcule
//             pas le CRC, ne sauvegarde rien (règle 20 : une seule tache).
//             Chaque grandeur physique est convertie par une fonction
//             dediee, nommee, avec ses constantes juste au-dessus
//             (règle 16 : pas de valeur magique) ; decodeFrame() se
//             limite a l'orchestration (extraction des octets + appel).
// Fonctions : decodeFrame() - transforme une trame brute en IssData.
// Référence : Protocole Davis Vantage Pro/Pro2/Vue Serial Protocol,
//             support.davisinstruments.com (CRC-16/CCITT, polynome 0x1021).
//             Repartition des octets utiles par type de trame et formules
//             de conversion : retro-ingenierie communautaire, dekay/
//             DavisRFM69 wiki "Message Protocol" (copie locale dans
//             docs/Message Protocol ... Wiki.pdf) :
//             https://github.com/dekay/DavisRFM69/wiki/Message-Protocol
//             Definition des grandeurs archivees (moyenne/majorite sur
//             l'intervalle) : docs/Davis_Data_Archived_v3.pdf
// ============================================================================
// Remarque  : Vitesse et direction du vent sont extraites de TOUTES les
//             trames (octets 2 et 5), conformement au protocole Davis
//             (repris de l'ancien programme ESP32 fonctionnel), meme en
//             l'absence d'anemometre/girouette physique (valeurs a 0).
//             Le champ specifique au type de trame est decode a part.
// ============================================================================
#include <Arduino.h>
#include "IssCommon.h"
#include "Config.h"

// ----------------------------------------------------------------------
// Vitesse du vent (octet 2, presente dans toutes les trames).
// ----------------------------------------------------------------------
static const float MPH_TO_KPH = 1.60934f;

static uint8_t windSpeedKphFromRaw(uint8_t rawSpeed)
{
    return (uint8_t)((float)rawSpeed * MPH_TO_KPH);
}

// ----------------------------------------------------------------------
// Direction du vent (octet 5, presente dans toutes les trames).
// Le potentiometre de la girouette VP2 ne couvre que 342 des 360 degres
// (zone morte physique autour du Nord) ; la valeur brute est decalee de
// 9 degres a l'origine. Convention Nord = 0 degre (jamais 360), y compris
// pour la zone morte (rawDirection == 0) - coherente avec WeeWx/Weatherlink.
// ATTENTION : la source communautaire (dekay wiki) signale un desaccord
// non tranche : sur certaines unites reelles, 0 est observe en dehors de
// la zone morte. A confirmer sur ce materiel des que l'anemometre/girouette
// sera installe (pas d'anemometre/girouette disponible a ce jour pour
// verifier - voir ToDoList.md).
// Référence : dekay/DavisRFM69 wiki, "Message Protocol", octet "Wind Dir".
// ----------------------------------------------------------------------
static const uint16_t WIND_DIR_RAW_MAX    = 255;   // valeur brute max transmise par la girouette
static const uint16_t WIND_DIR_RANGE_DEG  = 342;   // plage angulaire couverte par le potentiometre VP2
static const uint16_t WIND_DIR_OFFSET_DEG = 9;     // decalage d'origine du potentiometre

static uint16_t windDirectionDegFromRaw(uint8_t rawDirection)
{
    if (rawDirection == 0)
    {
        return 0;
    }
    return WIND_DIR_OFFSET_DEG + (uint16_t)(((uint32_t)rawDirection * WIND_DIR_RANGE_DEG) / WIND_DIR_RAW_MAX);
}

// Conversion degre <-> secteur de la rose des vents (16 secteurs de 22.5
// degres), utilisee par DataLogger pour synthetiser la direction dominante
// d'un intervalle d'archivage (règle Davis : direction = secteur le plus
// souvent echantillonne, voir docs/Davis_Data_Archived_v3.pdf). Secteur 0
// = Nord = 0 degre (convention WeeWx/Weatherlink, jamais 360).
uint8_t windDirectionSectorFromDeg(uint16_t degrees)
{
    long sector = lroundf((float)degrees / WIND_DIR_SECTOR_WIDTH_DEG) % WIND_DIR_SECTOR_COUNT;
    return (uint8_t)sector;
}

uint16_t windDirectionDegFromSector(uint8_t sector)
{
    return (uint16_t)((float)sector * WIND_DIR_SECTOR_WIDTH_DEG);
}

// ----------------------------------------------------------------------
// Taux de reception (§XIV.6 VantageSerialProtocolDocs_v261.pdf) : nombre
// de secondes moyen entre deux trames pour une station donnee. DataLogger
// compare le nombre de trames reellement recues sur un intervalle a
// intervalleSecondes / issSecondsPerPacket() pour en deduire un pourcentage.
// ----------------------------------------------------------------------
float issSecondsPerPacket(uint8_t stationId)
{
    return ISS_BASE_SECONDS_PER_PACKET + ((float)stationId * ISS_EXTRA_SECONDS_PER_PACKET_ID);
}

// ----------------------------------------------------------------------
// Temperature exterieure (trame type ISS_TYPE_TEMP).
// ----------------------------------------------------------------------
static const float TEMPERATURE_RAW_TO_FAHRENHEIT = 160.0f;   // diviseur brut -> °F (1/10 de °F x 16)
static const float FAHRENHEIT_FREEZING_POINT      = 32.0f;
static const float FAHRENHEIT_TO_CELSIUS_RATIO    = 5.0f / 9.0f;

static float temperatureOutsideCFromRaw(uint8_t highByte, uint8_t lowByte)
{
    int16_t rawTemperature = (int16_t)((uint16_t)highByte << 8 | lowByte);
    float temperatureFahrenheit = (float)rawTemperature / TEMPERATURE_RAW_TO_FAHRENHEIT;
    return (temperatureFahrenheit - FAHRENHEIT_FREEZING_POINT) * FAHRENHEIT_TO_CELSIUS_RATIO;
}

// ----------------------------------------------------------------------
// Humidite exterieure (trame type ISS_TYPE_HUMIDITY).
// ----------------------------------------------------------------------
static const float HUMIDITY_RAW_TO_PERCENT = 10.0f;

static float humidityOutsideFromRaw(uint8_t highByte, uint8_t lowByte)
{
    uint16_t rawHumidity = ((uint16_t)(highByte >> 4) << 8) | lowByte;
    return (float)rawHumidity / HUMIDITY_RAW_TO_PERCENT;
}

// ----------------------------------------------------------------------
// Rayonnement solaire (trame type ISS_TYPE_SOLAR). 0xFF = capteur absent.
// ----------------------------------------------------------------------
static const uint8_t SOLAR_RAW_NO_SENSOR   = 0xFF;
static const uint8_t SOLAR_RAW_SHIFT_BITS  = 6;
static const float   SOLAR_RAW_TO_WM2      = 1.757936f;

static uint16_t solarRadiationFromRaw(uint8_t highByte, uint8_t lowByte)
{
    if (highByte == SOLAR_RAW_NO_SENSOR)
    {
        return 0;
    }
    uint16_t rawSolar = ((uint16_t)highByte << 8 | lowByte) >> SOLAR_RAW_SHIFT_BITS;
    return (uint16_t)((float)rawSolar * SOLAR_RAW_TO_WM2);
}

// ----------------------------------------------------------------------
// Index UV (trame type ISS_TYPE_UV). 0xFF = capteur absent.
// ----------------------------------------------------------------------
static const uint8_t UV_RAW_NO_SENSOR  = 0xFF;
static const uint8_t UV_RAW_SHIFT_BITS = 6;
static const float   UV_RAW_TO_INDEX   = 50.0f;

static float uvIndexFromRaw(uint8_t highByte, uint8_t lowByte)
{
    if (highByte == UV_RAW_NO_SENSOR)
    {
        return 0.0f;
    }
    uint16_t rawUv = ((uint16_t)highByte << 8 | lowByte) >> UV_RAW_SHIFT_BITS;
    return (float)rawUv / UV_RAW_TO_INDEX;
}

// ----------------------------------------------------------------------
// Debit de pluie (trame type ISS_TYPE_RAINRATE). 0xFF = pas de goutte
// recente (debit nul). Deux echelles selon l'auget declenche (fort/faible
// debit), selectionnees par le bit 0x40 de l'octet 4.
// ----------------------------------------------------------------------
static const uint8_t  RAINRATE_RAW_NO_DROP        = 0xFF;
static const uint8_t  RAINRATE_STRONG_FLAG_MASK   = 0x40;
static const uint8_t  RAINRATE_TIME_HIGH_MASK     = 0x30;
static const uint8_t  RAINRATE_TIME_HIGH_SHIFT    = 4;
static const uint16_t RAINRATE_TIME_HIGH_UNIT     = 250;
static const float    RAINRATE_STRONG_NUMERATOR   = 11520.0f;   // mm/h, auget "fort debit"
static const float    RAINRATE_LIGHT_NUMERATOR    = 720.0f;     // mm/h, auget "faible debit"

static float rainRateMmPerHourFromRaw(uint8_t timeLowByte, uint8_t flagsByte)
{
    if (timeLowByte == RAINRATE_RAW_NO_DROP)
    {
        return 0.0f;
    }
    bool strongRain = (flagsByte & RAINRATE_STRONG_FLAG_MASK) != 0;
    uint16_t rawTime = (uint16_t)(((flagsByte & RAINRATE_TIME_HIGH_MASK) >> RAINRATE_TIME_HIGH_SHIFT) * RAINRATE_TIME_HIGH_UNIT) + timeLowByte;
    if (rawTime == 0)
    {
        return 0.0f;
    }
    if (strongRain == true)
    {
        return RAINRATE_STRONG_NUMERATOR / rawTime;
    }
    return RAINRATE_LIGHT_NUMERATOR / rawTime;
}

// ----------------------------------------------------------------------
// Compteur de pluie (trame type ISS_TYPE_RAIN) : compteur 7 bits, cumule
// depuis la mise sous tension de l'ISS (bit de poids fort reserve).
// ----------------------------------------------------------------------
static const uint8_t RAIN_TIP_COUNT_MASK = 0x7F;

static uint16_t rainTipCountFromRaw(uint8_t rawByte)
{
    return (uint16_t)(rawByte & RAIN_TIP_COUNT_MASK);
}

// ----------------------------------------------------------------------
// Rafale de vent (trame type ISS_TYPE_WINDGUST).
// ----------------------------------------------------------------------
static uint16_t windGustKphFromRaw(uint8_t rawGust)
{
    return (uint16_t)((float)rawGust * MPH_TO_KPH);
}


// checkCRC() : CRC-16/CCITT, polynome 0x1021, valeur initiale 0x0000.
// A verifier contre la reference officielle Davis avant mise en production
// (support.davisinstruments.com, Vantage Serial Protocol) : je n'ai pas
// pu confirmer la table de constantes exacte utilisee par Davis, cette
// implementation suit l'algorithme CCITT standard le plus communement
// documente pour ce protocole.

bool checkCRC(const uint8_t frameBytes[], uint8_t frameLength)
{
    uint16_t crcAccumulator = 0x0000;

    for (uint8_t index = 0; index < frameLength; index++ )
    {
        crcAccumulator = crcAccumulator ^ ((uint16_t)frameBytes[index] << 8);
        for (uint8_t bitIndex = 0; bitIndex < 8; bitIndex++ )
        {
            if ((crcAccumulator & 0x8000) != 0)
            {
                crcAccumulator = (crcAccumulator << 1) ^ 0x1021;
            }
            else
            {
                crcAccumulator = crcAccumulator << 1;
            }
        }
    }

    return (crcAccumulator == 0x0000);
}

bool decodeFrame(const IssRawFrame &rawFrame, IssData &result)
{
    result.frameValid = false;

    // La verification CRC est une tache distincte de l'interpretation
    // des champs (règle 20). Elle n'est effectuee que si la trame en
    // comporte un (mode FSK- en fait il y en a aussi en RS485).

    if (rawFrame.hasCrc == true)
    {
        bool crcOk = checkCRC(rawFrame.bytes, rawFrame.length);
        if (crcOk == false)
        {
            Serial.println(F("[IssCommon] Erreur : CRC invalide, trame rejetee"));
            return false;
        }
    }

    result.sensorType = (rawFrame.bytes[0] >> 4) & 0x0F;
    result.batteryLow = (rawFrame.bytes[0] >> 3) & 0x01;
    result.stationId = rawFrame.bytes[0] & 0x07;

    // Champs communs a TOUTES les trames, quel que soit le type
    // (sans anemometre/girouette : valeurs attendues a 0).
    result.windSpeedKph = windSpeedKphFromRaw(rawFrame.bytes[2]);
    result.windDirectionDeg = windDirectionDegFromRaw(rawFrame.bytes[5]);

    switch (result.sensorType)
    {
        case ISS_TYPE_TEMP:
        {
            result.temperatureOutside = temperatureOutsideCFromRaw(rawFrame.bytes[3], rawFrame.bytes[4]);
            break;
        }
        case ISS_TYPE_HUMIDITY:
        {
            result.humidityOutside = humidityOutsideFromRaw(rawFrame.bytes[4], rawFrame.bytes[3]);
            break;
        }
        case ISS_TYPE_SOLAR:
        {
            result.solarRadiation = solarRadiationFromRaw(rawFrame.bytes[3], rawFrame.bytes[4]);
            break;
        }
        case ISS_TYPE_UV:
        {
            result.uvIndex = uvIndexFromRaw(rawFrame.bytes[3], rawFrame.bytes[4]);
            break;
        }
        case ISS_TYPE_RAINRATE:
        {
            result.rainRateMmPerHour = rainRateMmPerHourFromRaw(rawFrame.bytes[3], rawFrame.bytes[4]);
            break;
        }
        case ISS_TYPE_RAIN:
        {
            result.rainTipCount = rainTipCountFromRaw(rawFrame.bytes[3]);
            break;
        }
        case ISS_TYPE_WINDGUST:
        {
            result.windGustKph = windGustKphFromRaw(rawFrame.bytes[3]);
            break;
        }
        case ISS_TYPE_SUPERCAP:
        {
            // Non exploite pour l'instant (pas de champ dedie dans IssData).
            break;
        }
        default:
        {
            Serial.print(F("[IssCommon] Type de trame non gere : 0x"));
            Serial.println(result.sensorType, HEX);
            return false;
        }
    }

    result.frameValid = true;
    return true;
}
