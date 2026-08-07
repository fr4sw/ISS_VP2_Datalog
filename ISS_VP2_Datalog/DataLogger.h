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
// Remarque 3 : la position GPS (latitude/longitude) n'est PAS ecrite dans
//             ce CSV - une station est fixe, la repeter sur chaque ligne
//             (toutes les 30s) gaspillerait de l'espace SD pour une donnee
//             qui ne change quasiment jamais. Recuperee via
//             TimeManager::location() (règle 102 : jamais un acces direct a
//             Gps.h) et journalisee a part, uniquement sur changement, voir
//             LocationLog.h.
// Remarque 4 : en DEBUG uniquement (Config.h), deux colonnes supplementaires
//             donnent le detail du comptage de reception, pour verifier
//             framesRecuesDepuisEcriture (voir logRecord()) et
//             framesRecuesCreneauEnCours (voir checkReceptionSlotBoundary())
//             independamment du taux de reception deja calcule - utile
//             pour diagnostiquer un ecart (ex : 98% au lieu de 100% sur
//             liaison filaire).
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SD.h>
#include "IssCommon.h"
#include "BmeIndoor.h"

class DataLogger
{
public:
    // Photo en lecture seule des donnees courantes, pour un consommateur
    // externe qui ne doit jamais toucher aux accumulateurs/etat interne
    // (BleLink notamment - voir BleLink.cpp). Toutes les chaines sont
    // "NA"/vides et les booleens a false tant qu'aucune mesure/position
    // n'est encore disponible.
    struct Snapshot
    {
        float    temperatureOutsideC;
        float    humidityOutsidePercent;
        uint8_t  windSpeedKph;
        int16_t  windDirectionDeg;      // -1 = NA
        uint8_t  windGustKph;
        char     lastMeasurementDate[9];
        char     lastMeasurementTime[7];
        bool     rainActive;            // true = episode de pluie en cours
        bool     lastRainEventValid;
        char     lastRainEventDate[9];
        char     lastRainEventTime[7];
        float    lastRainEventCumulativeMm;   // episode en cours si actif, sinon dernier episode termine
        bool     locationValid;
        float    latitudeDeg;
        float    longitudeDeg;
    };

    void begin();
    void update();
    void logRecord(const IssData &data);
    void updateIndoorData(const IndoorData &data);
    void getSnapshot(Snapshot &snapshot) const;

private:
    void printDecodedValue(const IssData &data);
    void accumulateWindSample(uint8_t windSpeedKph, uint16_t windDirectionDeg);
    void resetWindAccumulators();
    bool checkReceptionSlotBoundary(const char timeString[7], uint8_t stationId);
    void writeLine(const char dateString[9], const char timeString[7], bool isTransmissionSlotRow, bool forceFlush);
    void checkRainEpisodeTimeout();

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

    // Derniere synthese ecrite (voir writeLine()) - alimente BleLink (règle
    // 102-like : BleLink ne lit jamais IssData/accumulateurs directement,
    // seulement cette "photo" deja stabilisee).
    char     lastMeasurementDate[9];
    char     lastMeasurementTime[7];
    uint8_t  lastWindSpeedKph;
    int16_t  lastWindDirectionDeg;   // -1 = NA (calme sur tout l'intervalle)
    bool     lastLocationValid;
    float    lastLocationLatitudeDeg;
    float    lastLocationLongitudeDeg;

    // Declenchement immediat sur clic de pluie (rainTipCount change).
    bool     rainTipBaselineSet;
    uint16_t previousRainTipCount;

    // Episode de pluie (règle utilisateur : un message n'est journalise
    // qu'au DEBUT et a la FIN d'un episode, pas a chaque clic - voir
    // logRecord()/checkRainEpisodeTimeout()). Un episode se termine apres
    // RAIN_EPISODE_TIMEOUT_MS sans nouveau clic (Config.h).
    bool          rainEpisodeActive;
    unsigned long lastRainTipMillis;
    float         rainEpisodeCumulativeMm;
    char          rainEpisodeStartDate[9];
    char          rainEpisodeStartTime[7];
    // Dernier evenement pluie connu (actif OU termine) : c'est cette copie,
    // jamais remise a zero au demarrage d'un nouvel episode tant qu'elle
    // n'a pas ete "ecrasee" par un evenement plus recent, qui alimente le
    // module BLE (Etat "sec" + derniere pluie connue).
    bool          lastRainEventValid;
    char          lastRainEventDate[9];
    char          lastRainEventTime[7];
    float         lastRainEventCumulativeMm;

    // Taux de reception, calcule sur le creneau TRANSMISSION_SLOT_MINUTES
    // (cale sur l'horloge murale, pas sur un simple compte a rebours).
    bool     transmissionSlotInitialized;
    int16_t  lastTransmissionSlotIndex;
    uint16_t framesReceivedInSlot;
    uint8_t  lastReceptionPercent;

#if USE_MESHTASTIC
    // Envoi de la telemetrie Mesh a chaque creneau de 5 min (meme
    // declencheur que le taux de reception), differe de
    // MESH_SEND_SETTLE_MS (Config.h) - voir writeLine()/update(). Non
    // bloquant : on ne fait qu'armer une echeance ici, verifiee a chaque
    // tour de loop() dans update(). Les valeurs a envoyer sont figees
    // (snapshot) au moment de l'armement, pas relues au moment de l'envoi -
    // elles doivent correspondre au creneau qui vient de se refermer, pas
    // a l'etat (deja en train de changer) du creneau suivant.
    bool          meshSendPending;
    unsigned long meshSendDueMillis;
    float         meshSendTemperatureC;
    float         meshSendHumidityPercent;
    float         meshSendPressureHpa;
    uint16_t      meshSendWindDirectionDeg;
    float         meshSendWindSpeedKph;
    float         meshSendWindGustKph;
    float         meshSendRainfallMm;
#endif

#if DEBUG
    // Voir Remarque 4 ci-dessus : uniquement pour verification/diagnostic,
    // jamais lu en production (règle 26 : pas de cout permanent pour un
    // besoin de diagnostic ponctuel).
    uint16_t framesReceivedSinceLastWrite;
#endif
};

extern DataLogger dataLogger;
