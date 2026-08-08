// ============================================================================
// Fichier   : BleLink.h
// Rôle      : Annonce BLE minimale, lecture seule, pour affichage par une
//             appli generique (nRF Connect, LightBlue, etc.) - PAS d'appli
//             dediee necessaire. Deux services :
//               1) Environmental Sensing Service (UUID SIG standard 0x181A)
//                  avec des caracteristiques SIG standard (temperature,
//                  humidite, pression, vent, pluie) : la plupart des applis
//                  generiques les reconnaissent et les affichent NOMMEES et
//                  DECODEES automatiquement (pas juste de l'hexa brut),
//                  puisque leur UUID/format est defini par le Bluetooth SIG
//                  (voir GATT Specification Supplement).
//               2) Un service custom avec UNE caracteristique texte
//                  (UTF-8 brut, lisible tel quel dans n'importe quelle
//                  appli meme sans decodage SIG) pour les champs composites
//                  qui n'ont pas d'equivalent standard propre : date/heure
//                  de la derniere mesure, date/heure+cumul de la derniere
//                  pluie, etat (pluie/sec), coordonnees GPS.
//             Lecture seule (règle utilisateur : "sans client... juste
//             afficher les donnees") : aucune caracteristique en ecriture,
//             rien a configurer depuis le telephone/PC.
// Fonctions : bleLinkBegin()  - demarre l'annonce BLE (a appeler une fois
//             dans setup(), apres dataLogger.begin()).
//             bleLinkUpdate() - rafraichit les caracteristiques depuis
//             DataLogger::Snapshot (voir DataLogger.h), au plus toutes les
//             BLE_UPDATE_INTERVAL_MS (Config.h). A appeler a chaque loop().
// LIMITE ASSUMEE DE CE PREMIER JET : implementation ecrite a partir de la
//             connaissance de l'API Adafruit Bluefruit nRF52 (bundlee dans
//             le cœur Seeeduino:nrf52 utilise par ce projet, pas de
//             bibliotheque supplementaire a installer), mais PAS testee a
//             la compilation ici (pas de toolchain nRF52 disponible cote
//             assistant) - de petits ecarts d'API (nom exact d'une
//             methode, signature) sont possibles au premier essai. Voir
//             aussi les "A VERIFIER" dans BleLink.cpp pour les facteurs
//             d'echelle des caracteristiques SIG (resolution exacte selon
//             le GATT Specification Supplement, a confirmer avec un
//             analyseur BLE si les valeurs affichees semblent decalees).
// Référence : Bluetooth GATT Specification Supplement (UUID et formats des
//             caracteristiques standard utilisees ci-dessous) ; exemples
//             officiels Adafruit_Bluefruit_nRF52_Libraries (bleuart,
//             bleadv_button) pour la structure generale begin()/update().
// ============================================================================
#pragma once
#include <Arduino.h>
#include "Config.h"

#if USE_BLE
void bleLinkBegin();
void bleLinkUpdate();
#endif
