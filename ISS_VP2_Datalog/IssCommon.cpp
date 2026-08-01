// ============================================================================
// Fichier   : IssCommon.cpp
// Rôle      : Decodage unique des trames Davis ISS, quelle que soit la
//             source physique (FSK sans fil ou RS485 filaire). Ne calcule
//             pas le CRC, ne sauvegarde rien (règle 20 : une seule tache).
// Fonctions : decodeFrame() - transforme une trame brute en IssData.
// Référence : Protocole Davis Vantage Pro2, structure trame communautaire
//             github.com/dcbo/ISS-MQTT-Gateway ; Davis Data Archived v3,
//             meteoengins.fr/documents/davis/Davis_Data_Archived_v3.pdf
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

static uint16_t windDegFromRaw(uint8_t rawDirection)
{
    if (rawDirection == 0)
    {
        return 0;
    }
    return (uint16_t)(((uint32_t)rawDirection * 342UL) / 255UL);
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
    result.windSpeedKph = (uint8_t)(rawFrame.bytes[2] * 1.60934f);
    result.windDirectionDeg = (uint16_t)((rawFrame.bytes[5] * 360UL) / 255UL);
    
    switch (result.sensorType)
    {
        case ISS_TYPE_TEMP:
        {
            int16_t rawTemperature = (int16_t)((uint16_t)rawFrame.bytes[3] << 8 | rawFrame.bytes[4]);
            float temperatureFahrenheit = (float)rawTemperature / 160.0f;
            result.temperatureOutside = (temperatureFahrenheit - 32.0f) * 5.0f / 9.0f;
            break;
        }
        case ISS_TYPE_HUMIDITY:
        {
            uint16_t rawHumidity = ((uint16_t)(rawFrame.bytes[4] >> 4) << 8) | rawFrame.bytes[3];
            result.humidityOutside = (float)rawHumidity / 10.0f;
            break;
        }
        case ISS_TYPE_SOLAR:
        {
            if (rawFrame.bytes[3] != 0xFF)
            {
                uint16_t rawSolar = ((uint16_t)rawFrame.bytes[3] << 8 | rawFrame.bytes[4]) >> 6;
                result.solarRadiation = (uint16_t)((float)rawSolar * 1.757936f);
            }
            else result.solarRadiation = 0;
            break;
        }
        case ISS_TYPE_UV:
        {
            if (rawFrame.bytes[3] != 0xFF)
            {
                uint16_t rawUv = ((uint16_t)rawFrame.bytes[3] << 8 | rawFrame.bytes[4]) >> 6;
                result.uvIndex = (float)rawUv / 50.0f;
            }
            break;
        }
        case ISS_TYPE_RAINRATE:
        {
            if (rawFrame.bytes[3] != 0xFF)
            {
                bool strongRain = (rawFrame.bytes[4] & 0x40) != 0;
                uint16_t rawTime = ((uint16_t)((rawFrame.bytes[4] & 0x30) >> 4) * 250) + rawFrame.bytes[3];
                if (rawTime != 0)
                {
                    result.rainRateMmPerHour = strongRain ? (11520.0f / rawTime) : (720.0f / rawTime);
                }
            }
            else result.rainRateMmPerHour = 0;
            break;
        }
        case ISS_TYPE_RAIN:
        {
            result.rainTipCount = rawFrame.bytes[3] & 0x7F;
            break;
        }
        case ISS_TYPE_WINDGUST:
        {
            result.windGustKph = (uint16_t)(rawFrame.bytes[3] * 1.60934f);
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
