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
// Remarque 1 : Vitesse et direction du vent, echantillonnees a chaque trame
//             (toutes les ~2.5s), sont synthetisees sur l'intervalle plutot
//             que simplement ecrasees par le dernier echantillon (sinon les
//             variations rapides du vent seraient masquees) :
//               - vitesse  : moyenne des echantillons de l'intervalle ;
//               - direction: secteur (16 points, 22.5°) le plus souvent
//                 echantillonne (majorite), calme exclu. Formule Davis, voir
//                 docs/Davis_Data_Archived_v3.pdf et IssCommon.cpp.
// Remarque 2 : TROIS cumuls/declencheurs independants coexistent (Config.h) :
//               - LOG_WRITE_INTERVAL_MS (Params) : cadence d'ECRITURE SD,
//                 purement locale (economie d'ecriture/energie).
//               - TRANSMISSION_SLOT_MINUTES : creneau cale sur l'horloge
//                 murale (00, 05, 10, ...), comme WeeWx/Weatherlink, base
//                 du calcul du taux de reception (tauxReceptionPct). Son
//                 franchissement force LUI AUSSI une ecriture SD (colonne
//                 creneauTransmission a 1), pour avoir un point de
//                 verification visible entre les deux cadences. Aucune
//                 transmission radio n'existe encore (Mesh a venir).
//               - Un clic de pluie (rainTipCount) force egalement une
//                 ecriture immediate.
//             Aucun de ces trois declencheurs ne reinitialise les deux
//             autres cumuls : ecrire plus souvent qu'attendu ne fait que
//             photographier l'etat courant, sans perturber la suite.
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
    bool checkReceptionSlotBoundary(const char timeString[7], uint8_t stationId);
    void writeLine(const char dateString[9], const char timeString[7], bool isTransmissionSlotRow);

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

    // Declenchement immediat sur clic de pluie (rainTipCount change).
    bool     rainTipBaselineSet;
    uint16_t previousRainTipCount;

    // Taux de reception, calcule sur le creneau TRANSMISSION_SLOT_MINUTES
    // (cale sur l'horloge murale, pas sur un simple compte a rebours).
    bool     transmissionSlotInitialized;
    int16_t  lastTransmissionSlotIndex;
    uint16_t framesReceivedInSlot;
    uint8_t  lastReceptionPercent;
};

extern DataLogger dataLogger;
