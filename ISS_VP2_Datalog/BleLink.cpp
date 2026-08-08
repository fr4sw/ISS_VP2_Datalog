// ============================================================================
// Fichier   : BleLink.cpp
// ============================================================================
#include "BleLink.h"

#if USE_BLE

#include <bluefruit.h>
#include "DataLogger.h"
#include "EventLog.h"

// --- Environmental Sensing Service (0x181A) et caracteristiques SIG
// standard (voir BleLink.h). Chaque UUID/format/resolution vient du GATT
// Specification Supplement du Bluetooth SIG - A VERIFIER avec un analyseur
// BLE (ex : nRF Connect en mode "raw") si une valeur affichee semble
// decalee d'un facteur 10/100/1000, les resolutions exactes n'ayant pas pu
// etre revalidees a la compilation ici (voir BleLink.h).
static BLEService essService(0x181A);

static BLECharacteristic temperatureChar(0x2A6E);    // sint16, 0.01 degC
static BLECharacteristic humidityChar(0x2A6F);        // uint16, 0.01 %
static BLECharacteristic pressureChar(0x2A6D);        // uint32, 0.1 Pa (A VERIFIER)
static BLECharacteristic windSpeedChar(0x2A70);       // uint16, 0.01 m/s ("True Wind Speed")
static BLECharacteristic windDirectionChar(0x2A71);   // uint16, 0.01 degre ("True Wind Direction")
static BLECharacteristic windGustChar(0x2A72);        // uint16, 0.01 m/s ("Apparent Wind Speed",
                                                       // reutilisee ici pour la rafale - pas de
                                                       // caracteristique SIG "gust speed" dediee,
                                                       // seulement un "Gust Factor" (0x2A74) qui est
                                                       // un ratio sans unite, pas une vitesse : pas
                                                       // le bon champ pour notre donnee)
static BLECharacteristic rainfallChar(0x2A78);        // uint16, 0.001 m = 1 mm/LSB (A VERIFIER)

// --- Service custom pour le texte de synthese (voir BleLink.h). UUID 128
// bits genere pour ce projet (aleatoire, pas de collision a redouter avec
// les UUID 16 bits reserves SIG utilises ci-dessus).
static const uint8_t SUMMARY_SERVICE_UUID128[16] =
{
    0x6E, 0x94, 0x00, 0x01, 0x1B, 0x5D, 0x4E, 0x5F,
    0x9A, 0x0E, 0x87, 0x43, 0x4A, 0x30, 0x5A, 0xC1
};
static const uint8_t SUMMARY_CHAR_UUID128[16] =
{
    0x6E, 0x94, 0x00, 0x02, 0x1B, 0x5D, 0x4E, 0x5F,
    0x9A, 0x0E, 0x87, 0x43, 0x4A, 0x30, 0x5A, 0xC1
};
static const uint16_t SUMMARY_CHAR_MAX_LEN = 180;

static BLEService        summaryService(SUMMARY_SERVICE_UUID128);
static BLECharacteristic summaryChar(SUMMARY_CHAR_UUID128);

static unsigned long lastBleUpdateMillis = 0;

// Prepare une caracteristique SIG standard de longueur fixe, lecture seule
// + notification (voir BleLink.h : pas d'ecriture, jamais rien a
// configurer depuis le client).
static void beginFixedLenCharacteristic(BLECharacteristic &characteristic, uint16_t byteLength)
{
    characteristic.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    characteristic.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    characteristic.setFixedLen(byteLength);
    characteristic.begin();
}

void bleLinkBegin()
{
    Bluefruit.begin();

    // Bluefruit.setName() seul laisse la caracteristique GAP "Device Name"
    // ECRITE PAR DEFAUT (permission NO_ACCESS non appliquee par la
    // bibliotheque) - observe avec LightBlue, qui proposait de renommer
    // l'appareil a distance. On appelle directement l'API SoftDevice
    // sous-jacente pour figer le nom ET verrouiller l'ecriture en meme
    // temps (le seul moyen de restreindre ce champ specifique du GAP, la
    // lecture restant toujours publique quoi qu'il arrive - c'est standard
    // BLE, pas une limite de ce code). A VERIFIER A LA COMPILATION (non
    // teste ici, voir BleLink.h) : si sd_ble_gap_device_name_set() ou
    // BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS ne sont pas trouves, chercher le
    // nom exact dans ble_gap.h (SoftDevice S140, deja inclus
    // transitivement par bluefruit.h).
    ble_gap_conn_sec_mode_t nameWritePermission;
    BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(&nameWritePermission);
    sd_ble_gap_device_name_set(&nameWritePermission, (const uint8_t *)BLE_DEVICE_NAME, strlen(BLE_DEVICE_NAME));

    Bluefruit.setTxPower(BLE_TX_POWER_DBM);

    // La bibliotheque Bluefruit fait clignoter une LED (LED_BLUE / LED_CONN,
    // definie dans variant.h de la carte - voir le cœur Seeeduino:nrf52,
    // dossier variants/Seeed_XIAO_nRF52840) pendant l'annonce/la connexion
    // BLE, PAR DEFAUT, sans code ecrit dans ce projet (comportement interne
    // de Bluefruit.begin()). Desactive ici : une LED qui clignote en
    // continu pendant toute la duree de l'annonce est une consommation
    // parasite non negligeable sur une station alimentee sur batterie/
    // panneau solaire (voir aussi le point OTA/consommation a venir).
    // Reactiver avec Bluefruit.autoConnLed(true) si le retour visuel est
    // utile en phase de mise au point.
    Bluefruit.autoConnLed(false);

    essService.begin();
    beginFixedLenCharacteristic(temperatureChar, 2);
    beginFixedLenCharacteristic(humidityChar, 2);
    beginFixedLenCharacteristic(pressureChar, 4);
    beginFixedLenCharacteristic(windSpeedChar, 2);
    beginFixedLenCharacteristic(windDirectionChar, 2);
    beginFixedLenCharacteristic(windGustChar, 2);
    beginFixedLenCharacteristic(rainfallChar, 2);

    summaryService.begin();
    summaryChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
    summaryChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    summaryChar.setMaxLen(SUMMARY_CHAR_MAX_LEN);
    summaryChar.begin();

    // Annonce continue (règle utilisateur : pas d'appli dediee, une appli
    // generique doit pouvoir trouver et lire l'appareil a tout moment sans
    // manipulation prealable).
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(essService);
    Bluefruit.Advertising.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(160, 400);   // unites de 0,625ms -> 100-250ms, compromis
                                                    // usuel visibilite/consommation
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);   // 0 = annonce en continu, jamais de timeout

    Serial.println(F("[BleLink] Annonce BLE demarree"));
    logEvent(F("BLE demarre"));
}

// Formate un champ date/heure "date,heure" (dateString "YYYYMMDD",
// timeString "HHMMSS") en "YYYY-MM-DD HH:MM:SS" plus lisible pour un
// humain parcourant la caracteristique texte - "NA" si vide.
static void appendDateTime(char *buffer, size_t bufferSize, const char dateString[9], const char timeString[7])
{
    size_t currentLength = strlen(buffer);
    if ((dateString[0] == '\0') || (timeString[0] == '\0'))
    {
        snprintf(buffer + currentLength, bufferSize - currentLength, "NA");
        return;
    }
    snprintf(buffer + currentLength, bufferSize - currentLength,
             "%.4s-%.2s-%.2s %.2s:%.2s:%.2s",
             dateString, dateString + 4, dateString + 6,
             timeString, timeString + 2, timeString + 4);
}

void bleLinkUpdate()
{
    if ((millis() - lastBleUpdateMillis) < BLE_UPDATE_INTERVAL_MS)
    {
        return;
    }
    lastBleUpdateMillis = millis();

    DataLogger::Snapshot snapshot;
    dataLogger.getSnapshot(snapshot);

    bool connected = Bluefruit.connected();

    int16_t temperatureRaw = (int16_t)(snapshot.temperatureOutsideC * 100.0f);
    temperatureChar.write((uint8_t *)&temperatureRaw, sizeof(temperatureRaw));
    if (connected) { temperatureChar.notify((uint8_t *)&temperatureRaw, sizeof(temperatureRaw)); }

    uint16_t humidityRaw = (uint16_t)(snapshot.humidityOutsidePercent * 100.0f);
    humidityChar.write((uint8_t *)&humidityRaw, sizeof(humidityRaw));
    if (connected) { humidityChar.notify((uint8_t *)&humidityRaw, sizeof(humidityRaw)); }

    // Pas de pression exterieure Davis (ISS sans barometre) - on publie la
    // pression interieure (BME680) faute de mieux, comme deja fait pour le
    // Mesh (voir DataLogger::writeLine()). A VERIFIER : resolution 0,1 Pa
    // (GATT Specification Supplement) -> hPa * 1000.
    if (snapshot.pressureValid == true)
    {
        uint32_t pressureRaw = (uint32_t)(snapshot.pressureHpa * 1000.0f);
        pressureChar.write((uint8_t *)&pressureRaw, sizeof(pressureRaw));
        if (connected) { pressureChar.notify((uint8_t *)&pressureRaw, sizeof(pressureRaw)); }
    }

    uint16_t windSpeedRaw = (uint16_t)((float)snapshot.windSpeedKph * (1.0f / 3.6f) * 100.0f);   // kph -> m/s -> 0.01 m/s
    windSpeedChar.write((uint8_t *)&windSpeedRaw, sizeof(windSpeedRaw));
    if (connected) { windSpeedChar.notify((uint8_t *)&windSpeedRaw, sizeof(windSpeedRaw)); }

    uint16_t windDirectionRaw = 0xFFFF;   // valeur SIG conventionnelle "donnee non disponible"
    if (snapshot.windDirectionDeg >= 0)
    {
        windDirectionRaw = (uint16_t)(snapshot.windDirectionDeg * 100);
    }
    windDirectionChar.write((uint8_t *)&windDirectionRaw, sizeof(windDirectionRaw));
    if (connected) { windDirectionChar.notify((uint8_t *)&windDirectionRaw, sizeof(windDirectionRaw)); }

    uint16_t windGustRaw = (uint16_t)((float)snapshot.windGustKph * (1.0f / 3.6f) * 100.0f);
    windGustChar.write((uint8_t *)&windGustRaw, sizeof(windGustRaw));
    if (connected) { windGustChar.notify((uint8_t *)&windGustRaw, sizeof(windGustRaw)); }

    uint16_t rainfallRaw = (uint16_t)(snapshot.lastRainEventCumulativeMm + 0.5f);
    rainfallChar.write((uint8_t *)&rainfallRaw, sizeof(rainfallRaw));
    if (connected) { rainfallChar.notify((uint8_t *)&rainfallRaw, sizeof(rainfallRaw)); }

    // Caracteristique texte (voir BleLink.h) : tous les champs composites/
    // sans equivalent SIG propre, en une seule chaine UTF-8 lisible.
    char summary[SUMMARY_CHAR_MAX_LEN];
    summary[0] = '\0';
    size_t offset = 0;

    offset += snprintf(summary + offset, sizeof(summary) - offset, "Mesure:");
    appendDateTime(summary, sizeof(summary), snapshot.lastMeasurementDate, snapshot.lastMeasurementTime);
    offset = strlen(summary);

    offset += snprintf(summary + offset, sizeof(summary) - offset, " | Pluie:");
    if (snapshot.lastRainEventValid == true)
    {
        appendDateTime(summary, sizeof(summary), snapshot.lastRainEventDate, snapshot.lastRainEventTime);
        offset = strlen(summary);
        offset += snprintf(summary + offset, sizeof(summary) - offset, " %.1fmm", snapshot.lastRainEventCumulativeMm);
    }
    else
    {
        offset += snprintf(summary + offset, sizeof(summary) - offset, "NA");
    }

    offset += snprintf(summary + offset, sizeof(summary) - offset, " | Etat:%s",
                        (snapshot.rainActive == true) ? "PLUIE" : "SEC");

    offset += snprintf(summary + offset, sizeof(summary) - offset, " | Pos:");
    if (snapshot.locationValid == true)
    {
        snprintf(summary + offset, sizeof(summary) - offset, "%.5f,%.5f", snapshot.latitudeDeg, snapshot.longitudeDeg);
    }
    else
    {
        snprintf(summary + offset, sizeof(summary) - offset, "NA");
    }

    summaryChar.write((uint8_t *)summary, strlen(summary));
    if (connected) { summaryChar.notify((uint8_t *)summary, strlen(summary)); }
}

#endif
