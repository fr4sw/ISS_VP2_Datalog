// ============================================================================
// Fichier   : LocationLog.cpp
// ============================================================================
#include <Arduino.h>
#include <SD.h>
#include "LocationLog.h"
#include "Config.h"
#include "HalPins.h"

static File locationLogFile;
static bool locationLogReady = false;

static bool  lastLoggedLocationValid = false;
static float lastLoggedLatitudeDeg = 0.0f;
static float lastLoggedLongitudeDeg = 0.0f;

void locationLogBegin()
{
#if USE_SD_CARD
    bool sdReady = beginSdCard();
    if (sdReady == false)
    {
        Serial.println(F("[LocationLog] Carte SD indisponible : historique de position desactive pour cette session"));
        return;
    }

    bool fileExisted = SD.exists(LOCATION_FILE_NAME);
    locationLogFile = SD.open(LOCATION_FILE_NAME, FILE_WRITE);
    if (!locationLogFile)
    {
        Serial.println(F("[LocationLog] Erreur : impossible d'ouvrir l'historique de position"));
        return;
    }
    if (fileExisted == false)
    {
        locationLogFile.println(F("date,heure,latitude,longitude,satellites"));
        locationLogFile.flush();
    }

    locationLogReady = true;
#endif
}

void locationLogRecord(const char dateString[9], const char timeString[7],
                        float latitudeDeg, float longitudeDeg, uint8_t satelliteCount)
{
#if USE_SD_CARD
    if (locationLogReady == false)
    {
        return;
    }

    // Ne journalise que si la position a change depuis la derniere ligne
    // ecrite (voir LocationLog.h) : une comparaison directe suffit, la
    // position provient toujours de la meme conversion TinyGPSPlus (pas
    // d'imprecision d'arrondi entre deux appels successifs tant que le
    // fix GPS sous-jacent n'a pas ete rafraichi).
    if ((lastLoggedLocationValid == true)
        && (latitudeDeg == lastLoggedLatitudeDeg)
        && (longitudeDeg == lastLoggedLongitudeDeg))
    {
        return;
    }

    locationLogFile.print(dateString);
    locationLogFile.print(F(","));
    locationLogFile.print(timeString);
    locationLogFile.print(F(","));
    locationLogFile.print(latitudeDeg, 5);
    locationLogFile.print(F(","));
    locationLogFile.print(longitudeDeg, 5);
    locationLogFile.print(F(","));
    locationLogFile.println(satelliteCount);
    locationLogFile.flush();

    lastLoggedLatitudeDeg = latitudeDeg;
    lastLoggedLongitudeDeg = longitudeDeg;
    lastLoggedLocationValid = true;

    Serial.println(F("[LocationLog] Nouvelle position GPS journalisee"));
#else
    (void)dateString;
    (void)timeString;
    (void)latitudeDeg;
    (void)longitudeDeg;
    (void)satelliteCount;
#endif
}
