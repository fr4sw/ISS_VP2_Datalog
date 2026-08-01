// ============================================================================
// Fichier   : BmeIndoor.h
// Rôle      : Lecture du capteur interieur BME680 (temperature, humidite,
//             pression atmospherique), relie en I2C. Le module ne connait
//             pas l'usage fait de ces valeurs (règle 4) : il fournit
//             uniquement la derniere mesure valide via getData().
// Fonctions : begin()   - initialise le bus I2C et le capteur.
//             update()  - fait avancer le cycle de mesure, a appeler dans
//                         loop(). Ne bloque jamais l'execution (voir note).
//             getData() - renvoie true et remplit IndoorData si une mesure
//                         valide est disponible, false sinon (aucun capteur
//                         detecte, ou aucune mesure terminee depuis begin()).
// Note      : Une mesure BME680 (temperature+humidite+pression+gaz) dure
//             typiquement 150 a 200 ms. Un appel bloquant (performReading())
//             retarderait d'autant la lecture des trames RS485 dans
//             IssRs485::update(), avec un risque de perte de trame. Le
//             module utilise donc le cycle non bloquant beginReading()/
//             endReading() de la bibliotheque, piloté par une machine a
//             etats (STATE_IDLE / STATE_MEASURING).
// Dépendances : Bibliotheque "Adafruit BME680 Library" (Arduino Library
//             Manager), qui requiert elle-meme "Adafruit Unified Sensor" et
//             "Adafruit BusIO".
// Référence : Bosch BME680, datasheet BST-BME680-DS001
//             github.com/adafruit/Adafruit_BME680
// ============================================================================
#pragma once
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME680.h>
#include "BoardConfig.h"
#include "Config.h"

struct IndoorData
{
    float temperatureIndoor;
    float humidityIndoor;
    float pressureIndoor;
    bool  dataValid;
};

class BmeIndoor
{
public:
    void begin();
    void update();
    bool getData(IndoorData &data);

private:
    enum State
    {
        STATE_IDLE,
        STATE_MEASURING
    };

    Adafruit_BME680 bmeSensor;
    State            measurementState;
    unsigned long    lastMeasurementStartMillis;
    unsigned long    measurementReadyMillis;
    bool             sensorReady;
    IndoorData       lastData;
};

extern BmeIndoor bmeIndoor;
