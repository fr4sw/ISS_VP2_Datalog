// ============================================================================
// Fichier   : DataLogger.h
// Rôle      : Ecriture des enregistrements sur support de stockage (SD).
//             Maintient un etat courant par station (derniere valeur
//             connue de chaque grandeur), mis a jour a chaque trame
//             decodee, complete par la derniere mesure valide du capteur
//             interieur BME680 (updateIndoorData()), et ecrit une ligne
//             CSV consolidee des que la periode Params::getLogWriteIntervalMs()
//             est ecoulee (0 = a chaque trame). Un nouveau fichier, nomme a
//             partir de la date et de l'heure de demarrage, est cree a
//             chaque redemarrage.
// Remarque  : Vitesse et direction du vent, echantillonnees a chaque trame
//             (toutes les ~2.5s), sont synthetisees sur l'intervalle plutot
//             que simplement ecrasees par le dernier echantillon (sinon les
//             variations rapides du vent seraient masquees) :
//               - vitesse  : moyenne des echantillons de l'intervalle ;
//               - direction: secteur (16 points, 22.5°) le plus souvent
//                 echantillonne (majorite), calme exclu. Formule Davis, voir
//                 docs/Davis_Data_Archived_v3.pdf et IssCommon.cpp.
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
    void accumulateWindSample(uint8_t windSpeedKph, uint16_t windDirectionDeg);
    void resetWindAccumulators();
    void writeLine();

    unsigned long  sequenceNumber;
    File           logFile;
    IssData        currentState;
    IndoorData     currentIndoorState;
    unsigned long  lastWriteMillis;

    // Accumulateurs de synthese du vent depuis la derniere ligne ecrite.
    uint32_t windSpeedSumKph;
    uint16_t windSpeedSampleCount;
    uint16_t windDirectionSectorCounts[WIND_DIR_SECTOR_COUNT];
    uint16_t windDirectionSampleCount;
};

extern DataLogger dataLogger;
