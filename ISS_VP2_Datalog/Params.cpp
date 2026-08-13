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
    else if (strcmp(key, "MESHBAUD") == 0)
    {
        params.setMeshBaudRate((uint32_t)value);
    }
    else if (strcmp(key, "GPSMINSAT") == 0)
    {
        params.setGpsMinSatellites((uint8_t)value);
    }
    else if (strcmp(key, "DATALOGGERUTC") == 0)
    {
        params.setDataloggerUtc(value != 0);
    }
    else if (strcmp(key, "MESHUTC") == 0)
    {
        params.setMeshUtc(value != 0);
    }
    else if (strcmp(key, "ISSAVGINTERVALMS") == 0)
    {
        params.setIssAverageFrameIntervalMs((uint32_t)value);
    }
    else if (strcmp(key, "BLEENABLED") == 0)
    {
        params.setBleEnabled(value != 0);
    }
    else if (strcmp(key, "MESHPOWERONMS") == 0)
    {
        params.setMeshPowerOnSettleMs((uint32_t)value);
    }
    else if (strcmp(key, "MESHSKIPHANDSHAKE") == 0)
    {
        params.setMeshSkipHandshake(value != 0);
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
    meshBaudRate = MESH_BAUD_DEFAULT;
    gpsMinSatellites = GPS_MINIMUM_SATELLITES;
    dataloggerUtc = DATALOGGER_UTC_DEFAULT;
    meshUtc = MESH_UTC_DEFAULT;
    issAverageFrameIntervalMs = 0;   // 0 = pas encore mesure, voir Params.h
    bleEnabled = BLE_ENABLED_DEFAULT;
    meshPowerOnSettleMs = MESH_POWERON_SETTLE_MS_DEFAULT;
    meshSkipHandshake = MESH_SKIP_HANDSHAKE_DEFAULT;

    load();

    Serial.print(F("[Params] TZ = "));
    Serial.print(timezoneOffsetHours);
    Serial.print(F(" h, LOGINTERVAL = "));
    Serial.print(logWriteIntervalMs);
    Serial.print(F(" ms (0 = a chaque trame), GPSBAUD = "));
    Serial.print(gpsBaudRate);
    Serial.print(F(", MESHBAUD = "));
    Serial.print(meshBaudRate);
    Serial.print(F(", GPSMINSAT = "));
    Serial.print(gpsMinSatellites);
    Serial.print(F(", DATALOGGERUTC = "));
    Serial.print(dataloggerUtc);
    Serial.print(F(", MESHUTC = "));
    Serial.print(meshUtc);
    Serial.print(F(", ISSAVGINTERVALMS = "));
    Serial.print(issAverageFrameIntervalMs);
    Serial.print(F(", BLEENABLED = "));
    Serial.print(bleEnabled);
    Serial.print(F(", MESHPOWERONMS = "));
    Serial.print(meshPowerOnSettleMs);
    Serial.print(F(", MESHSKIPHANDSHAKE = "));
    Serial.println(meshSkipHandshake);
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
    paramsFile.print(F("MESHBAUD="));
    paramsFile.println(meshBaudRate);
    paramsFile.print(F("GPSMINSAT="));
    paramsFile.println(gpsMinSatellites);
    paramsFile.print(F("DATALOGGERUTC="));
    paramsFile.println(dataloggerUtc ? 1 : 0);
    paramsFile.print(F("MESHUTC="));
    paramsFile.println(meshUtc ? 1 : 0);
    paramsFile.print(F("ISSAVGINTERVALMS="));
    paramsFile.println(issAverageFrameIntervalMs);
    paramsFile.print(F("BLEENABLED="));
    paramsFile.println(bleEnabled ? 1 : 0);
    paramsFile.print(F("MESHPOWERONMS="));
    paramsFile.println(meshPowerOnSettleMs);
    paramsFile.print(F("MESHSKIPHANDSHAKE="));
    paramsFile.println(meshSkipHandshake ? 1 : 0);
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

uint32_t Params::getMeshBaudRate() const
{
    return meshBaudRate;
}

void Params::setMeshBaudRate(uint32_t baudRate)
{
    meshBaudRate = baudRate;
}

uint8_t Params::getGpsMinSatellites() const
{
    return gpsMinSatellites;
}

void Params::setGpsMinSatellites(uint8_t minSatellites)
{
    gpsMinSatellites = minSatellites;
}

bool Params::getDataloggerUtc() const
{
    return dataloggerUtc;
}

void Params::setDataloggerUtc(bool useUtc)
{
    dataloggerUtc = useUtc;
}

bool Params::getMeshUtc() const
{
    return meshUtc;
}

void Params::setMeshUtc(bool useUtc)
{
    meshUtc = useUtc;
}

uint32_t Params::getIssAverageFrameIntervalMs() const
{
    return issAverageFrameIntervalMs;
}

void Params::setIssAverageFrameIntervalMs(uint32_t intervalMs)
{
    issAverageFrameIntervalMs = intervalMs;
}

bool Params::getBleEnabled() const
{
    return bleEnabled;
}

void Params::setBleEnabled(bool enabled)
{
    bleEnabled = enabled;
}

uint32_t Params::getMeshPowerOnSettleMs() const
{
    return meshPowerOnSettleMs;
}

void Params::setMeshPowerOnSettleMs(uint32_t settleMs)
{
    meshPowerOnSettleMs = settleMs;
}

bool Params::getMeshSkipHandshake() const
{
    return meshSkipHandshake;
}

void Params::setMeshSkipHandshake(bool skip)
{
    meshSkipHandshake = skip;
}
