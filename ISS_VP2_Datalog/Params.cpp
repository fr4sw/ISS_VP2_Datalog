// ============================================================================
// Fichier   : Params.cpp
// ============================================================================
#include <Arduino.h>
#include <SD.h>
#include "Params.h"
#include "Config.h"
#include "BoardConfig.h"
#include "HalPins.h"

Params params;

static const uint8_t PARAMS_LINE_MAX_LENGTH = 40;

// Lit une ligne (jusqu'a '\n' ou fin de fichier) dans buffer, sans la
// bibliotheque String (règle 15 : rester explicite, coherent avec le reste
// du projet qui n'utilise que des tableaux de caracteres).
static bool readLine(File &file, char buffer[], uint8_t bufferSize)
{
    if (file.available() == false)
    {
        return false;
    }

    uint8_t length = 0;
    while ((file.available() > 0) && (length < (bufferSize - 1)))
    {
        char receivedChar = (char)file.read();
        if (receivedChar == '\n')
        {
            break;
        }
        if (receivedChar != '\r')
        {
            buffer[length] = receivedChar;
            length = length + 1;
        }
    }
    buffer[length] = '\0';
    return true;
}

// Interprete une ligne "CLE=VALEUR" et met a jour le parametre correspondant.
// Une seule fonction pour toutes les cles (règle 20 : une tache -
// "appliquer une ligne de parametre" - plutot qu'un bloc if/else duplique
// pour chaque appelant).
static void applyParamLine(const char line[])
{
    if ((line[0] == '#') || (line[0] == '\0'))
    {
        return;
    }

    const char *separator = strchr(line, '=');
    if (separator == nullptr)
    {
        return;
    }

    char key[PARAMS_LINE_MAX_LENGTH];
    uint8_t keyLength = (uint8_t)(separator - line);
    strncpy(key, line, keyLength);
    key[keyLength] = '\0';

    long value = atol(separator + 1);

    if (strcmp(key, "TZ") == 0)
    {
        params.setTimezoneOffsetHours((int8_t)value);
    }
    else if (strcmp(key, "LOGINTERVAL") == 0)
    {
        params.setLogWriteIntervalMs((uint32_t)value);
    }
    else if (strcmp(key, "GPSBAUD") == 0)
    {
        params.setGpsBaudRate((uint32_t)value);
    }
    else
    {
        Serial.print(F("[Params] Avertissement : cle inconnue ignoree : "));
        Serial.println(key);
    }
}

void Params::begin()
{
    timezoneOffsetHours = TIMEZONE_OFFSET_HOURS_DEFAULT;
    logWriteIntervalMs = LOG_WRITE_INTERVAL_MS_DEFAULT;
    gpsBaudRate = UART_GPS_BAUD;

    load();

    Serial.print(F("[Params] TZ = "));
    Serial.print(timezoneOffsetHours);
    Serial.print(F(" h, LOGINTERVAL = "));
    Serial.print(logWriteIntervalMs);
    Serial.print(F(" ms (0 = a chaque trame), GPSBAUD = "));
    Serial.println(gpsBaudRate);
}

void Params::load()
{
#if USE_SD_CARD
    bool sdReady = beginSdCard();
    if (sdReady == false)
    {
        Serial.println(F("[Params] Carte SD indisponible : valeurs par defaut de Config.h conservees"));
        return;
    }

    if (SD.exists(PARAMS_FILE_NAME) == false)
    {
        Serial.println(F("[Params] Aucun fichier de parametres, valeurs par defaut de Config.h conservees"));
        return;
    }

    File paramsFile = SD.open(PARAMS_FILE_NAME, FILE_READ);
    if (!paramsFile)
    {
        Serial.println(F("[Params] Erreur : impossible d'ouvrir le fichier de parametres"));
        return;
    }

    char line[PARAMS_LINE_MAX_LENGTH];
    while (readLine(paramsFile, line, sizeof(line)) == true)
    {
        applyParamLine(line);
    }
    paramsFile.close();

    Serial.println(F("[Params] Parametres charges depuis la carte SD"));
#endif
}

void Params::save()
{
#if USE_SD_CARD
    bool sdReady = beginSdCard();
    if (sdReady == false)
    {
        Serial.println(F("[Params] Erreur : impossible d'enregistrer, carte SD indisponible"));
        return;
    }

    SD.remove(PARAMS_FILE_NAME);
    File paramsFile = SD.open(PARAMS_FILE_NAME, FILE_WRITE);
    if (!paramsFile)
    {
        Serial.println(F("[Params] Erreur : impossible d'ecrire le fichier de parametres"));
        return;
    }

    paramsFile.print(F("TZ="));
    paramsFile.println(timezoneOffsetHours);
    paramsFile.print(F("LOGINTERVAL="));
    paramsFile.println(logWriteIntervalMs);
    paramsFile.print(F("GPSBAUD="));
    paramsFile.println(gpsBaudRate);
    paramsFile.flush();
    paramsFile.close();

    Serial.println(F("[Params] Parametres enregistres sur la carte SD"));
#endif
}

int8_t Params::getTimezoneOffsetHours() const
{
    return timezoneOffsetHours;
}

void Params::setTimezoneOffsetHours(int8_t hours)
{
    timezoneOffsetHours = hours;
}

uint32_t Params::getLogWriteIntervalMs() const
{
    return logWriteIntervalMs;
}

void Params::setLogWriteIntervalMs(uint32_t intervalMs)
{
    logWriteIntervalMs = intervalMs;
}

uint32_t Params::getGpsBaudRate() const
{
    return gpsBaudRate;
}

void Params::setGpsBaudRate(uint32_t baudRate)
{
    gpsBaudRate = baudRate;
}
