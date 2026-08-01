// ============================================================================
// Fichier   : DataLogger.h (complet, corrige)
// Rôle      : Ecriture des enregistrements sur support de stockage (SD).
//             Maintient un etat courant par station (derniere valeur
//             connue de chaque grandeur), mis a jour a chaque trame
//             decodee, complete par la derniere mesure valide du capteur
//             interieur BME680 (updateIndoorData()), et ecrit une ligne
//             CSV consolidee a intervalle regulier. Un nouveau fichier,
//             nomme a partir de la date et de l'heure de demarrage, est
//             cree a chaque redemarrage.
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "IssCommon.h"
#include "BmeIndoor.h"

class DataLogger
{
public:
    void begin();
    void update();
    void logRecord(const IssData &data);
    void updateIndoorData(const IndoorData &data);

private:
    void printDecodedValue(const IssData &data);

    unsigned long  sequenceNumber;
    File           logFile;
    IssData        currentState;
    IndoorData     currentIndoorState;
    unsigned long  lastWriteMillis;
};

extern DataLogger dataLogger;
