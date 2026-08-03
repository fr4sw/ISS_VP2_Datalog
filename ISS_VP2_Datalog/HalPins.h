// ============================================================================
// Fichier   : HalPins.h
// Rôle      : Seul endroit du projet contenant les #if d'architecture pour
//             la configuration des broches materielles (UART, I2C, SPI).
//             Tous les autres modules appellent ces fonctions sans jamais
//             tester eux-memes ARDUINO_ARCH_xxx (règle 4).
// Fonctions : configureRs485Pins() - applique setPins() si necessaire.
//             configureI2cPins()  - idem pour Wire.
//             configureSpiPins()  - idem pour SPI (hors CS, gere a part).
//             beginI2cBus()       - point unique d'initialisation du bus
//                                   I2C (broches + begin() + frequence).
//                                   Idempotent : plusieurs modules peuvent
//                                   partager le meme bus I2C sans double
//                                   initialisation (ex : BmeIndoor et Rtc).
//             beginSdCard()       - point unique d'initialisation de la
//                                   carte SD (broches + SPI + SD.begin()).
//                                   Idempotent, meme principe que
//                                   beginI2cBus() (ex : DataLogger, Params,
//                                   EventLog partagent la meme carte).
//                                   Renvoie l'etat (true = carte prete).
//             Serial2 (GPS/Mesh) n'a pas besoin de fonction configureXxxPins() :
//             ses broches sont fixes a la construction de l'objet (voir plus
//             bas), seul Serial2.begin(baud) reste a faire par le module
//             qui l'utilise (Gps.cpp).
// Remarque  : Declaration du second peripherique UART materiel (UARTE1),
//             distinct de Serial1 (UARTE0, deja dedie au RS485).
//             Les symboles UARTE1_IRQn et NRF_UARTE1_BASE sont fournis
//             nativement par le SoC Nordic (nrf52840.h, MDK), aucune
//             redefinition necessaire.
// Référence : cores/nRF5/nordic/nrfx/mdk/nrf52840.h (Nordic MDK)
// ============================================================================
// ============================================================================
#pragma once
#include <Arduino.h>
#include <SPI.h>
#include "BoardConfig.h"

void configureRs485Pins();
void configureI2cPins();
void configureSpiPins();
void beginI2cBus();
bool beginSdCard();

#if defined(ARDUINO_ARCH_NRF52)
    extern Uart Serial2;   // GPS/Mesh, commute via CD4053
#endif
