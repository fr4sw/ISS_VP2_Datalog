// ============================================================================
// Fichier   : DataLogger.cpp
// ============================================================================
#include <Arduino.h>
#include <SPI.h>
#include <math.h>
#include "DataLogger.h"
#include "BoardConfig.h"
#include "Config.h"
#include "TimeManager.h"
#include "HalPins.h"
#include "Params.h"
#include "EventLog.h"
#include "LocationLog.h"
#if USE_MESHTASTIC
#include "MeshLink.h"
#endif

DataLogger dataLogger;

// Copie une chaine dans un tampon de taille fixe en garantissant TOUJOURS
// la terminaison, quelle que soit la longueur de la source - contrairement
// a strncpy(dest, src, sizeof(dest)) seul, qui ne termine pas dest si src
// fait exactement sizeof(dest) caracteres ou plus (d'ou l'avertissement
// -Wstringop-truncation : le compilateur ne peut pas prouver que ce cas
// n'arrivera jamais, meme si en pratique dateString/timeString ont ici une
// longueur fixe connue). destSize doit etre > 0 (jamais un tampon de
// taille nulle dans ce fichier).
static void safeStrCopy(char *dest, const char *src, size_t destSize)
{
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
}

static void sdDateTimeCallback(uint16_t *fatDate, uint16_t *fatTime)
{
    char dateString[9];
    char timeString[7];
    bool timeValid = timeManager.now(dateString, timeString, false);   // horodatage FAT toujours local (convention systeme de fichiers), independant de DATALOGGERUTC

    if (timeValid == false)
    {
        *fatDate = FAT_DATE(2026, 1, 1);
        *fatTime = FAT_TIME(0, 0, 0);
        return;
    }

    uint16_t yearValue  = (uint16_t)((dateString[0] - '0') * 1000 + (dateString[1] - '0') * 100 + (dateString[2] - '0') * 10 + (dateString[3] - '0'));
    uint8_t  monthValue = (uint8_t)((dateString[4] - '0') * 10 + (dateString[5] - '0'));
    uint8_t  dayValue   = (uint8_t)((dateString[6] - '0') * 10 + (dateString[7] - '0'));
    uint8_t  hourValue   = (uint8_t)((timeString[0] - '0') * 10 + (timeString[1] - '0'));
    uint8_t  minuteValue = (uint8_t)((timeString[2] - '0') * 10 + (timeString[3] - '0'));
    uint8_t  secondValue = (uint8_t)((timeString[4] - '0') * 10 + (timeString[5] - '0'));

    *fatDate = FAT_DATE(yearValue, monthValue, dayValue);
    *fatTime = FAT_TIME(hourValue, minuteValue, secondValue);
}

void DataLogger::begin()
{
    sequenceNumber = 0;
    lastWriteMillis = 0;
    currentState.frameValid = false;
    currentIndoorState.dataValid = false;
    windSpeedSumKph = 0;
    windSpeedSampleCount = 0;
    windDirectionSampleCount = 0;
    for (uint8_t sectorIndex = 0; sectorIndex < WIND_DIR_SECTOR_COUNT; sectorIndex++)
    {
        windDirectionSectorCounts[sectorIndex] = 0;
    }

    rainTipBaselineSet = false;
    previousRainTipCount = 0;
    rainEpisodeActive = false;
    lastRainTipMillis = 0;
    rainEpisodeCumulativeMm = 0.0f;
    rainEpisodeStartDate[0] = '\0';
    rainEpisodeStartTime[0] = '\0';
    lastRainEventValid = false;
    lastRainEventDate[0] = '\0';
    lastRainEventTime[0] = '\0';
    lastRainEventCumulativeMm = 0.0f;
    dailyRainDateReference[0] = '\0';
    dailyRainCumulativeMm = 0.0f;
    lastMeasurementDate[0] = '\0';
    lastMeasurementTime[0] = '\0';
    lastWindSpeedKph = 0;
    lastWindDirectionDeg = -1;
    lastLocationValid = false;
    lastLocationLatitudeDeg = 0.0f;
    lastLocationLongitudeDeg = 0.0f;

    transmissionSlotInitialized = false;
    lastTransmissionSlotIndex = -1;
    framesReceivedInSlot = 0;
    lastReceptionPercent = 0;
    frameGapBaselineSet = false;
    lastFrameReceivedMillis = 0;
    missedFrameGapCount = 0;
    normalIntervalSumMs = 0;
    normalIntervalCount = 0;
#if USE_MESHTASTIC
    meshSendPending = false;
    meshSendDueMillis = 0;
#endif
#if DEBUG
    framesReceivedSinceLastWrite = 0;
#endif

#if USE_SD_CARD
    bool sdReady = beginSdCard();
    if (sdReady == false)
    {
        return;
    }

    SdFile::dateTimeCallback(sdDateTimeCallback);

    // Nom de fichier construit a partir de la date/heure de demarrage,
    // format court 8.3 impose par FAT : JJHHMMSS.CSV (jour+heure+minute).
    // Un nouveau fichier distinct est ainsi cree a chaque redemarrage,
    // limitant le risque de perte de donnees en cas de fichier corrompu.
    char dateString[9];
    char timeString[7];
    char logFileName[13];
    bool timeValid = timeManager.now(dateString, timeString, params.getDataloggerUtc());

    if (timeValid == true)
    {
        char dayPart[3];
        dayPart[0] = dateString[6];
        dayPart[1] = dateString[7];
        dayPart[2] = '\0';
        snprintf(logFileName, sizeof(logFileName), "%s%s.CSV", dayPart, timeString);
    }
    else
    {
        snprintf(logFileName, sizeof(logFileName), "VP200000.CSV");
    }

    logFile = SD.open(logFileName, FILE_WRITE);
    if (!logFile)
    {
        Serial.println(F("[DataLogger] Erreur : impossible d'ouvrir le fichier de log"));
        Serial.println(logFileName);
        return;
    }

    Serial.print(F("[DataLogger] Fichier de log cree : "));
    Serial.println(logFileName);
    logEvent(F("Nouveau fichier de log CSV cree"));

#if DEBUG
    logFile.println(F("seq,date,heure,stationId,batterieFaible,temperatureC,humidite,rayonnementSolaire,indexUV,pluieMmH,compteurPluie,rafaleKph,vitesseVentKph,directionVent,temperatureInterieureC,humiditeInterieure,pressionInterieureHpa,creneauTransmission,tauxReceptionPct,framesRecuesDepuisEcriture,framesRecuesCreneauEnCours,tramesRateesCreneauEnCours,intervalleMoyenMesureMs"));
#else
    logFile.println(F("seq,date,heure,stationId,batterieFaible,temperatureC,humidite,rayonnementSolaire,indexUV,pluieMmH,compteurPluie,rafaleKph,vitesseVentKph,directionVent,temperatureInterieureC,humiditeInterieure,pressionInterieureHpa,creneauTransmission,tauxReceptionPct"));
#endif
    logFile.flush();
#endif
}

void DataLogger::printDecodedValue(const IssData &data)
{
    Serial.print(F("[DataLogger] Station "));
    Serial.print(data.stationId);
    Serial.print(F(" - "));

    switch (data.sensorType)
    {
        case ISS_TYPE_TEMP:     Serial.print(F("Temperature : ")); Serial.print(data.temperatureOutside, 1); Serial.println(F(" C")); break;
        case ISS_TYPE_HUMIDITY: Serial.print(F("Humidite : ")); Serial.print(data.humidityOutside, 1); Serial.println(F(" %")); break;
        case ISS_TYPE_SOLAR:    Serial.print(F("Rayonnement solaire : ")); Serial.print(data.solarRadiation); Serial.println(F(" W/m2")); break;
        case ISS_TYPE_UV:       Serial.print(F("Index UV : ")); Serial.println(data.uvIndex, 1); break;
        case ISS_TYPE_RAINRATE: Serial.print(F("Pluie (debit) : ")); Serial.print(data.rainRateMmPerHour, 1); Serial.println(F(" mm/h")); break;
        case ISS_TYPE_RAIN:     Serial.print(F("Compteur pluie : ")); Serial.println(data.rainTipCount); break;
        case ISS_TYPE_WINDGUST: Serial.print(F("Rafale vent : ")); Serial.println(data.windGustKph); break;
        default: Serial.println(F("Type non affichable")); break;
    }
}

void DataLogger::updateIndoorData(const IndoorData &data)
{
    currentIndoorState = data;
}

void DataLogger::getSnapshot(Snapshot &snapshot) const
{
    snapshot.temperatureOutsideC = currentState.temperatureOutside;
    snapshot.humidityOutsidePercent = currentState.humidityOutside;
    snapshot.windSpeedKph = lastWindSpeedKph;
    snapshot.windDirectionDeg = lastWindDirectionDeg;
    snapshot.windGustKph = currentState.windGustKph;
    snapshot.pressureValid = currentIndoorState.dataValid;
    snapshot.pressureHpa = currentIndoorState.pressureIndoor;
    safeStrCopy(snapshot.lastMeasurementDate, lastMeasurementDate, sizeof(snapshot.lastMeasurementDate));
    safeStrCopy(snapshot.lastMeasurementTime, lastMeasurementTime, sizeof(snapshot.lastMeasurementTime));
    snapshot.rainActive = rainEpisodeActive;
    snapshot.lastRainEventValid = lastRainEventValid;
    safeStrCopy(snapshot.lastRainEventDate, lastRainEventDate, sizeof(snapshot.lastRainEventDate));
    safeStrCopy(snapshot.lastRainEventTime, lastRainEventTime, sizeof(snapshot.lastRainEventTime));
    snapshot.lastRainEventCumulativeMm = lastRainEventCumulativeMm;
    snapshot.locationValid = lastLocationValid;
    snapshot.latitudeDeg = lastLocationLatitudeDeg;
    snapshot.longitudeDeg = lastLocationLongitudeDeg;
}

uint16_t DataLogger::getLastFrameNumberInSlot() const
{
    return framesReceivedInSlot;
}

// Accumule un echantillon de vitesse/direction (appele a CHAQUE trame,
// puisque le protocole Davis transmet vitesse+direction sur toutes les
// trames). La direction n'est comptabilisee dans l'histogramme que si le
// vent souffle (vitesse > 0), conformement a la definition Davis du champ
// "Wind Dir" archive (voir docs/Davis_Data_Archived_v3.pdf : "si la vitesse
// du vent est quasiment toujours a 0, aucune direction n'est indiquee").
void DataLogger::accumulateWindSample(uint8_t windSpeedKph, uint16_t windDirectionDeg)
{
    windSpeedSumKph = windSpeedSumKph + windSpeedKph;
    windSpeedSampleCount = windSpeedSampleCount + 1;

    if (windSpeedKph > 0)
    {
        uint8_t sector = windDirectionSectorFromDeg(windDirectionDeg);
        windDirectionSectorCounts[sector] = windDirectionSectorCounts[sector] + 1;
        windDirectionSampleCount = windDirectionSampleCount + 1;
    }
}

void DataLogger::logRecord(const IssData &data)
{
    if (data.frameValid == false)
    {
        return;
    }

    currentState.stationId = data.stationId;
    currentState.batteryLow = data.batteryLow;
    currentState.frameValid = true;

    accumulateWindSample(data.windSpeedKph, data.windDirectionDeg);

    // Heure courante lue UNE SEULE fois par trame : sert a la fois a
    // detecter le franchissement du creneau de transmission ci-dessous, et
    // a l'ecriture eventuelle de la ligne CSV plus bas (evite de relire
    // deux fois le RTC/GPS pour la meme trame).
    char dateString[9];
    char timeString[7];
    bool timeValid = timeManager.now(dateString, timeString, params.getDataloggerUtc());

    // Le creneau (Config.h : TRANSMISSION_SLOT_MINUTES) est verifie a
    // CHAQUE trame (pas seulement a l'ecriture), pour detecter son
    // franchissement au plus tot et forcer une ecriture au bon moment.
    // IMPORTANT : cette verification doit avoir lieu AVANT d'incrementer
    // framesReceivedInSlot pour la trame courante. Sinon, la trame qui
    // fait justement franchir la frontiere du creneau serait comptee dans
    // l'ANCIEN creneau (incrementee puis englobee dans le calcul du taux
    // avant la remise a zero), qui lui revient a tort, au lieu d'etre
    // creditee au nouveau creneau qu'elle vient de demarrer - d'ou un
    // deficit systematique de 1 trame par creneau (taux bloque ~97-98%
    // meme sur liaison filaire sans perte reelle).
    bool transmissionSlotRow = false;
    if (timeValid == true)
    {
        transmissionSlotRow = checkReceptionSlotBoundary(timeString, data.stationId);
    }

    framesReceivedInSlot = framesReceivedInSlot + 1;

    // Mesure de l'ecart avec la trame precedente (voir DataLogger.h et
    // Config.h : FRAME_GAP_WARNING_MS) - alimente a la fois le compteur de
    // trames probablement ratees ET la moyenne "propre" utilisee par
    // checkReceptionSlotBoundary() pour calibrer le taux de reception sur
    // le comportement reel de CE materiel, plutot que sur une formule
    // theorique qui s'est reveleee erronee pour au moins une station.
    if (frameGapBaselineSet == false)
    {
        frameGapBaselineSet = true;
    }
    else
    {
        unsigned long gapMs = millis() - lastFrameReceivedMillis;
        if (gapMs > FRAME_GAP_WARNING_MS)
        {
            missedFrameGapCount = missedFrameGapCount + 1;
#if DEBUG
            Serial.print(F("[DataLogger] Trame recue "));
            Serial.print(gapMs);
            Serial.println(F(" ms apres la precedente (> FRAME_GAP_WARNING_MS : au moins une trame probablement ratee)"));
#endif
        }
        else
        {
            normalIntervalSumMs = normalIntervalSumMs + (uint32_t)gapMs;
            normalIntervalCount = normalIntervalCount + 1;
        }
    }
    lastFrameReceivedMillis = millis();

#if DEBUG
    framesReceivedSinceLastWrite = framesReceivedSinceLastWrite + 1;
    printDecodedValue(data);
#endif

    // Un clic de pluie (changement du compteur, pas seulement une
    // augmentation - le compteur est un compteur 7 bits qui boucle a 128)
    // force une ecriture immediate, independamment du cumul normal.
    bool rainTipOccurred = false;
    bool rainEpisodeJustStarted = false;

    // règle 6 : les codes de trame ne sont testes qu'a un seul endroit dans
    // tout le projet, via les constantes ISS_TYPE_* de IssCommon.h (jamais
    // recopies en litteral).
    switch (data.sensorType)
    {
        case ISS_TYPE_TEMP:     currentState.temperatureOutside = data.temperatureOutside; break;
        case ISS_TYPE_HUMIDITY: currentState.humidityOutside = data.humidityOutside; break;
        case ISS_TYPE_SOLAR:    currentState.solarRadiation = data.solarRadiation; break;
        case ISS_TYPE_UV:       currentState.uvIndex = data.uvIndex; break;
        case ISS_TYPE_RAINRATE: currentState.rainRateMmPerHour = data.rainRateMmPerHour; break;
        case ISS_TYPE_WINDGUST: currentState.windGustKph = data.windGustKph; break;
        case ISS_TYPE_RAIN:
        {
            currentState.rainTipCount = data.rainTipCount;
            if (rainTipBaselineSet == false)
            {
                // Premiere trame de pluie recue depuis le demarrage : le
                // compteur peut deja etre non nul (cumul depuis la mise
                // sous tension de l'ISS, pas depuis notre demarrage a
                // nous) - on memorise juste la reference, sans declencher
                // d'ecriture immediate a tort.
                rainTipBaselineSet = true;
                previousRainTipCount = data.rainTipCount;
            }
            else if (data.rainTipCount != previousRainTipCount)
            {
                rainTipOccurred = true;

                // Delta tolerant au bouclage du compteur 7 bits (0-127,
                // voir IssCommon.cpp : RAIN_TIP_COUNT_MASK) : couvre a la
                // fois le cas normal (delta petit) et un saut de plusieurs
                // clics rate entre deux trames (aucune trame RAIN perdue
                // n'est traitee differemment d'un clic simple).
                uint16_t tipDelta = (uint16_t)(((int16_t)data.rainTipCount - (int16_t)previousRainTipCount + 128) % 128);
                previousRainTipCount = data.rainTipCount;

                if (rainEpisodeActive == false)
                {
                    // Voir DataLogger.h : un message n'est journalise qu'au
                    // DEBUT d'un episode, pas a chaque clic individuel.
                    rainEpisodeActive = true;
                    rainEpisodeJustStarted = true;
                    rainEpisodeCumulativeMm = 0.0f;
                    safeStrCopy(rainEpisodeStartDate, dateString, sizeof(rainEpisodeStartDate));
                    safeStrCopy(rainEpisodeStartTime, timeString, sizeof(rainEpisodeStartTime));
                }
                rainEpisodeCumulativeMm = rainEpisodeCumulativeMm + ((float)tipDelta * RAIN_MM_PER_TIP);
                lastRainTipMillis = millis();

                lastRainEventValid = true;
                safeStrCopy(lastRainEventDate, dateString, sizeof(lastRainEventDate));
                safeStrCopy(lastRainEventTime, timeString, sizeof(lastRainEventTime));
                lastRainEventCumulativeMm = rainEpisodeCumulativeMm;

                // Cumul journalier (voir DataLogger.h) : remis a zero au
                // premier clic d'un nouveau jour, sinon simplement
                // incremente comme le cumul d'episode ci-dessus.
                if (strncmp(dailyRainDateReference, dateString, sizeof(dailyRainDateReference)) != 0)
                {
                    dailyRainCumulativeMm = 0.0f;
                    safeStrCopy(dailyRainDateReference, dateString, sizeof(dailyRainDateReference));
                }
                dailyRainCumulativeMm = dailyRainCumulativeMm + ((float)tipDelta * RAIN_MM_PER_TIP);
            }
            break;
        }
        default: break;
    }

    uint32_t logWriteIntervalMs = params.getLogWriteIntervalMs();
    bool intervalDue = (logWriteIntervalMs == 0) || ((millis() - lastWriteMillis) >= logWriteIntervalMs);
    bool writeDue = intervalDue || rainTipOccurred || transmissionSlotRow;
    if (writeDue == false)
    {
        return;
    }
    if (timeValid == false)
    {
        Serial.println(F("[DataLogger] Erreur : horodatage indisponible, ligne ignoree"));
        return;
    }
    lastWriteMillis = millis();

    if (rainEpisodeJustStarted == true)
    {
        Serial.println(F("[DataLogger] Debut d'un episode de pluie"));
        logEvent(F("Debut episode de pluie"));
    }

    writeLine(dateString, timeString, transmissionSlotRow, transmissionSlotRow || rainTipOccurred);
}

// Verifie si le creneau de transmission (Config.h : TRANSMISSION_SLOT_MINUTES,
// cale sur l'horloge murale, pas sur un simple compte a rebours) vient
// d'etre franchi. Si oui, calcule le taux de reception (trames recues /
// trames attendues, IssCommon.h : issSecondsPerPacket()) sur le creneau qui
// vient de se refermer, remet le compteur a zero, et renvoie true (pour que
// logRecord() force une ecriture SD - point de verification entre les deux
// cadences). N'est jamais reinitialise par une ecriture forcee ailleurs
// (clic de pluie) : seul le franchissement reel d'un creneau le remet a
// zero.
bool DataLogger::checkReceptionSlotBoundary(const char timeString[7], uint8_t stationId)
{
    uint8_t currentHour = (uint8_t)((timeString[0] - '0') * 10 + (timeString[1] - '0'));
    uint8_t currentMinute = (uint8_t)((timeString[2] - '0') * 10 + (timeString[3] - '0'));
    int16_t minuteOfDay = (int16_t)currentHour * 60 + (int16_t)currentMinute;
    int16_t currentSlotIndex = minuteOfDay / TRANSMISSION_SLOT_MINUTES;

    bool newSlot = (transmissionSlotInitialized == false) || (currentSlotIndex != lastTransmissionSlotIndex);
    if (newSlot == false)
    {
        return false;
    }

    // Base du calcul : intervalle moyen REELLEMENT mesure sur ce creneau
    // (voir logRecord()), pas la formule theorique Davis
    // (issSecondsPerPacket()) - celle-ci s'est reveleee ne pas correspondre
    // au comportement reel observe sur au moins une station (mesure ~2,56s/
    // trame contre 2,5s theoriques pour stationId=0), rendant le taux
    // systematiquement legerement sous-estime independamment de toute
    // perte reelle. La moyenne "propre" (normalIntervalSumMs/Count)
    // exclut deliberement les ecarts > FRAME_GAP_WARNING_MS (missedFrameGapCount) :
    // sans cette exclusion, une vraie perte se "diluerait" dans la moyenne
    // et masquerait sa propre trace au lieu de faire baisser le taux.
    float measuredAverageIntervalMs;
    if (normalIntervalCount > 0)
    {
        measuredAverageIntervalMs = (float)normalIntervalSumMs / (float)normalIntervalCount;
        params.setIssAverageFrameIntervalMs((uint32_t)(measuredAverageIntervalMs + 0.5f));
    }
    else
    {
        // Aucune mesure "propre" sur ce creneau (tout premier creneau
        // apres demarrage a froid, ou creneau entierement en echec) :
        // repli sur la derniere valeur mesuree connue (persistee, voir
        // Params - utile des le redemarrage si elle a ete sauvegardee via
        // SAVE), et a defaut seulement sur la formule theorique.
        measuredAverageIntervalMs = (float)params.getIssAverageFrameIntervalMs();
        if (measuredAverageIntervalMs <= 0.0f)
        {
            measuredAverageIntervalMs = issSecondsPerPacket(stationId) * 1000.0f;
        }
    }

    float expectedFrames = ((float)TRANSMISSION_SLOT_MINUTES * 60.0f * 1000.0f) / measuredAverageIntervalMs;
    float receptionPercentFloat = 0.0f;
    if (expectedFrames > 0.0f)
    {
        receptionPercentFloat = 100.0f * (float)framesReceivedInSlot / expectedFrames;
    }
    if (receptionPercentFloat > 100.0f)
    {
        // Toujours plafonne a 100% (règle Davis : un depassement signifie
        // simplement une legere avance sur l'estimation theorique, pas une
        // reception "superieure a la normale").
        receptionPercentFloat = 100.0f;
    }
    lastReceptionPercent = (uint8_t)(receptionPercentFloat + 0.5f);

    Serial.print(F("[DataLogger] Taux de reception sur le creneau ecoule : "));
    Serial.print(lastReceptionPercent);
    Serial.print(F(" % (intervalle moyen mesure : "));
    Serial.print(measuredAverageIntervalMs);
    Serial.print(F(" ms, trames probablement ratees : "));
    Serial.print(missedFrameGapCount);
    Serial.println(F(")"));

    framesReceivedInSlot = 0;
    missedFrameGapCount = 0;
    normalIntervalSumMs = 0;
    normalIntervalCount = 0;
    lastTransmissionSlotIndex = currentSlotIndex;
    transmissionSlotInitialized = true;

    return true;
}

void DataLogger::writeLine(const char dateString[9], const char timeString[7], bool isTransmissionSlotRow, bool forceFlush)
{
    sequenceNumber = sequenceNumber + 1;

    // Synthese du vent sur l'intervalle ecoule (voir DataLogger.h) :
    // vitesse moyenne, direction majoritaire (secteur le plus echantillonne,
    // "NA" si calme sur tout l'intervalle - jamais 0, qui serait une vraie
    // valeur Nord).
    uint8_t windSpeedAverageKph = 0;
    if (windSpeedSampleCount > 0)
    {
        windSpeedAverageKph = (uint8_t)(windSpeedSumKph / windSpeedSampleCount);
    }

    char windDirectionField[8];
    int16_t windDirectionDegForSnapshot = -1;
    if (windDirectionSampleCount == 0)
    {
        snprintf(windDirectionField, sizeof(windDirectionField), "NA");
    }
    else
    {
        uint8_t majoritySector = 0;
        uint16_t majorityCount = 0;
        for (uint8_t sectorIndex = 0; sectorIndex < WIND_DIR_SECTOR_COUNT; sectorIndex++)
        {
            if (windDirectionSectorCounts[sectorIndex] > majorityCount)
            {
                majorityCount = windDirectionSectorCounts[sectorIndex];
                majoritySector = sectorIndex;
            }
        }
        uint16_t majorityDirectionDeg = windDirectionDegFromSector(majoritySector);
        snprintf(windDirectionField, sizeof(windDirectionField), "%u", majorityDirectionDeg);
        windDirectionDegForSnapshot = (int16_t)majorityDirectionDeg;
    }

    // Voir DataLogger.h : photo de la derniere synthese, pour BleLink.
    safeStrCopy(lastMeasurementDate, dateString, sizeof(lastMeasurementDate));
    safeStrCopy(lastMeasurementTime, timeString, sizeof(lastMeasurementTime));
    lastWindSpeedKph = windSpeedAverageKph;
    lastWindDirectionDeg = windDirectionDegForSnapshot;

    // Champs du capteur interieur formates a part : affiche "NA" tant
    // qu'aucune mesure valide n'a ete recue depuis le demarrage (règle 6 -
    // une valeur 0.0 par defaut serait indiscernable d'une mesure reelle).
    char indoorTemperatureField[8];
    char indoorHumidityField[8];
    char indoorPressureField[8];

    if (currentIndoorState.dataValid == true)
    {
        snprintf(indoorTemperatureField, sizeof(indoorTemperatureField), "%.1f", currentIndoorState.temperatureIndoor);
        snprintf(indoorHumidityField, sizeof(indoorHumidityField), "%.1f", currentIndoorState.humidityIndoor);
        snprintf(indoorPressureField, sizeof(indoorPressureField), "%.1f", currentIndoorState.pressureIndoor);
    }
    else
    {
        snprintf(indoorTemperatureField, sizeof(indoorTemperatureField), "NA");
        snprintf(indoorHumidityField, sizeof(indoorHumidityField), "NA");
        snprintf(indoorPressureField, sizeof(indoorPressureField), "NA");
    }

    // Taux de reception : affiche uniquement sur la ligne qui referme le
    // creneau (isTransmissionSlotRow), "NA" sinon - c'est justement le
    // role de la colonne creneauTransmission de le signaler sans ambiguite.
    char receptionField[6];
    if (isTransmissionSlotRow == true)
    {
        snprintf(receptionField, sizeof(receptionField), "%u", lastReceptionPercent);
    }
    else
    {
        snprintf(receptionField, sizeof(receptionField), "NA");
    }

    uint8_t transmissionSlotFlag = 0;
    if (isTransmissionSlotRow == true)
    {
        transmissionSlotFlag = 1;
    }

    // Position GPS : journalisee separement (LocationLog.h), PAS dans le
    // CSV temps reel - voir Config.h : LOCATION_FILE_NAME. locationLogRecord()
    // se charge lui-meme de ne rien ecrire si la position n'a pas change
    // depuis la derniere ligne (station fixe : ca ne devrait arriver
    // qu'apres chaque resynchronisation GPS reussie, soit environ 1x/jour).
    float latitudeDeg = 0.0f;
    float longitudeDeg = 0.0f;
    uint8_t locationSatelliteCount = 0;
    bool locationAvailable = timeManager.location(latitudeDeg, longitudeDeg, locationSatelliteCount);
    if (locationAvailable == true)
    {
        locationLogRecord(dateString, timeString, latitudeDeg, longitudeDeg, locationSatelliteCount);
        lastLocationValid = true;
        lastLocationLatitudeDeg = latitudeDeg;
        lastLocationLongitudeDeg = longitudeDeg;
    }

    char line[300];
#if DEBUG
    // Voir Remarque 4, DataLogger.h : uniquement pour verification du
    // comptage de reception, jamais en production. intervalleMoyenMesureMs
    // (Params::getIssAverageFrameIntervalMs()) etait jusqu'ici uniquement
    // consultable via GET sur la console serie, jamais archive - ajoute
    // ici pour qu'il reste disponible en relisant le CSV plus tard, sans
    // avoir besoin d'une session serie live au bon moment.
    snprintf(line, sizeof(line), "%lu,%s,%s,%u,%u,%.1f,%.1f,%u,%.1f,%.1f,%u,%u,%u,%s,%s,%s,%s,%u,%s,%u,%u,%u,%lu",
             sequenceNumber, dateString, timeString,
             currentState.stationId, currentState.batteryLow,
             currentState.temperatureOutside, currentState.humidityOutside,
             currentState.solarRadiation, currentState.uvIndex,
             currentState.rainRateMmPerHour, currentState.rainTipCount,
             currentState.windGustKph, windSpeedAverageKph, windDirectionField,
             indoorTemperatureField, indoorHumidityField, indoorPressureField,
             transmissionSlotFlag, receptionField,
             framesReceivedSinceLastWrite, framesReceivedInSlot, missedFrameGapCount,
             (unsigned long)params.getIssAverageFrameIntervalMs());
#else
    snprintf(line, sizeof(line), "%lu,%s,%s,%u,%u,%.1f,%.1f,%u,%.1f,%.1f,%u,%u,%u,%s,%s,%s,%s,%u,%s",
             sequenceNumber, dateString, timeString,
             currentState.stationId, currentState.batteryLow,
             currentState.temperatureOutside, currentState.humidityOutside,
             currentState.solarRadiation, currentState.uvIndex,
             currentState.rainRateMmPerHour, currentState.rainTipCount,
             currentState.windGustKph, windSpeedAverageKph, windDirectionField,
             indoorTemperatureField, indoorHumidityField, indoorPressureField,
             transmissionSlotFlag, receptionField);
#endif

#if USE_SD_CARD
    if (logFile)
    {
        logFile.println(line);
        // flush() est une ecriture SD BLOQUANTE (potentiellement plusieurs
        // dizaines de ms selon la carte). L'appeler sur CHAQUE ligne (y
        // compris les ecritures de routine toutes les 30s) est un candidat
        // serieux pour expliquer un taux de reception legerement mais
        // systematiquement sous 100% meme sur liaison filaire propre : un
        // blocage de quelques dizaines de ms peut suffire a deborder le
        // tampon UART du RS485 (voir IssRs485.cpp) et perdre un octet en
        // plein milieu d'une trame en cours de reception. On ne force le
        // flush() immediat que sur les evenements qui en ont reellement
        // besoin (creneau de 5 min, clic de pluie) - les ecritures de
        // routine restent en cache OS/carte et sont de toute facon
        // physiquement ecrites au plus tard au flush() suivant. A VALIDER
        // sur le terrain : comparer le taux de reception avant/apres ce
        // changement.
        if (forceFlush == true)
        {
            logFile.flush();
        }
    }
#endif

    Serial.println(line);

#if DEBUG
    framesReceivedSinceLastWrite = 0;
#endif

#if USE_MESHTASTIC
    if (isTransmissionSlotRow == true)
    {
        // Voir DataLogger.h : figer maintenant les valeurs a envoyer (le
        // creneau qui vient de se refermer), armer l'echeance non
        // bloquante verifiee dans update(). Pression : capteur interieur
        // (l'ISS Davis n'a pas de barometre), rafale : derniere valeur
        // connue (pas de synthese sur l'intervalle, contrairement a la
        // vitesse moyenne). Pluie : cumul de l'episode en cours utilise
        // comme approximation de "pluie sur la derniere heure" - PAS un
        // vrai cumul glissant 1h (non implemente), acceptable tant qu'un
        // episode ne s'etale pas sur plusieurs heures (a surveiller).
        meshSendPending = true;
        meshSendDueMillis = millis() + MESH_SEND_SETTLE_MS;
        meshSendTemperatureC = currentState.temperatureOutside;
        meshSendHumidityPercent = currentState.humidityOutside;
        meshSendPressureHpa = (currentIndoorState.dataValid == true) ? currentIndoorState.pressureIndoor : 0.0f;
        meshSendWindDirectionDeg = (windDirectionDegForSnapshot < 0) ? 0 : (uint16_t)windDirectionDegForSnapshot;
        meshSendWindSpeedKph = (float)windSpeedAverageKph;
        meshSendWindGustKph = (float)currentState.windGustKph;
        meshSendRainfallMm = (rainEpisodeActive == true) ? rainEpisodeCumulativeMm : 0.0f;
        meshSendRainfall24hMm = dailyRainCumulativeMm;
    }
#endif

    resetWindAccumulators();
}

void DataLogger::resetWindAccumulators()
{
    windSpeedSumKph = 0;
    windSpeedSampleCount = 0;
    windDirectionSampleCount = 0;
    for (uint8_t sectorIndex = 0; sectorIndex < WIND_DIR_SECTOR_COUNT; sectorIndex++)
    {
        windDirectionSectorCounts[sectorIndex] = 0;
    }
}

void DataLogger::update()
{
    checkRainEpisodeTimeout();

#if USE_MESHTASTIC
    meshLinkUpdate();

    if ((meshSendPending == true) && (millis() >= meshSendDueMillis))
    {
        meshSendPending = false;

        uint32_t utcUnixTime = 0;
        bool utcAvailable = timeManager.nowUtcUnix(utcUnixTime);
        if (utcAvailable == false)
        {
            Serial.println(F("[DataLogger] Envoi Mesh annule : pas de source UTC disponible (voir TimeManager)"));
        }
        else
        {
            Serial.println(F("[DataLogger] Envoi de la telemetrie Mesh du creneau qui vient de se refermer"));
            meshLinkSendEnvironmentTelemetry(utcUnixTime,
                                              meshSendTemperatureC, meshSendHumidityPercent, meshSendPressureHpa,
                                              meshSendWindDirectionDeg, meshSendWindSpeedKph, meshSendWindGustKph,
                                              meshSendRainfallMm, meshSendRainfall24hMm);
        }
    }
#endif
}

// Cloture un episode de pluie apres RAIN_EPISODE_TIMEOUT_MS sans nouveau
// clic (Config.h). Doit etre appelee reguilerement (voir loop()) car,
// contrairement au debut d'un episode, sa fin n'est PAS declenchee par une
// trame ISS - il faut bien la detecter "en l'absence" d'evenement.
void DataLogger::checkRainEpisodeTimeout()
{
    if (rainEpisodeActive == false)
    {
        return;
    }
    if ((millis() - lastRainTipMillis) < RAIN_EPISODE_TIMEOUT_MS)
    {
        return;
    }

    long tenthsOfMm = lroundf(rainEpisodeCumulativeMm * 10.0f);
    Serial.print(F("[DataLogger] Fin d'episode de pluie, cumul (dixiemes de mm) : "));
    Serial.println(tenthsOfMm);
    logEvent(F("Fin episode de pluie (cumul, dixiemes de mm)"), tenthsOfMm);

    rainEpisodeActive = false;
}
