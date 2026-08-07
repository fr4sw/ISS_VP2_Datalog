// ============================================================================
// Fichier   : LocationLog.h
// Rôle      : Historique des positions GPS, persiste sur la carte SD dans un
//             fichier separe du CSV temps reel (LOCATION_FILE_NAME, Config.h).
//             Une station est fixe : la position ne change quasiment jamais
//             (sauf deplacement physique) - repeter latitude/longitude sur
//             CHAQUE ligne du CSV (toutes les 30s) serait un gaspillage
//             d'espace SD pour une donnee qui ne varie pas. Une ligne n'est
//             ajoutee ici que lorsque la position obtenue differe de la
//             derniere position deja journalisee.
// Fonctions : begin()   - ouvre (ou cree) LOCATION_FILE_NAME.
//             record()  - ajoute une ligne horodatee si la position a
//                          change depuis le dernier appel ayant ecrit une
//                          ligne (ne fait rien sinon - l'appelant peut donc
//                          appeler record() aussi souvent qu'il le souhaite,
//                          par exemple a chaque ligne CSV, sans se soucier
//                          du filtrage).
// ============================================================================
#pragma once
#include <Arduino.h>

void locationLogBegin();
void locationLogRecord(const char dateString[9], const char timeString[7],
                        float latitudeDeg, float longitudeDeg, uint8_t satelliteCount);
