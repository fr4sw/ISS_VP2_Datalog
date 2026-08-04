// ============================================================================
// Fichier   : EventLog.h
// Rôle      : Journal d'evenements persiste sur la carte SD (demarrages,
//             erreurs, connexions/deconnexions GPS, ecritures RTC, ...),
//             complementaire du moniteur serie qui n'est lu qu'en direct.
//             Un seul fichier, partage entre tous les modules, ouvert une
//             fois et laisse ouvert (append + flush a chaque ligne).
// Fonctions : begin()             - ouvre (ou cree) EVENTLOG_FILE_NAME.
//             logEvent(message)  - ajoute une ligne horodatee au journal.
//                                   N'echoue jamais bruyamment : si la carte
//                                   SD est indisponible, la ligne est
//                                   simplement perdue (le message reste
//                                   visible sur le moniteur serie, qui
//                                   continue d'etre alimente en parallele
//                                   par chaque module - EventLog ne
//                                   remplace pas les Serial.println()
//                                   existants, il les complete en differe).
// Remarque  : Ce module ne decide PAS quels evenements meritent d'etre
//             journalises - chaque module appelle logEvent() aux memes
//             endroits ou il appelle deja Serial.println() pour un message
//             d'etat/erreur "toujours affiche" (voir CodingRules.md §23).
// ============================================================================
#pragma once
#include <Arduino.h>

void logEventBegin();
void logEvent(const __FlashStringHelper *message);
void logEvent(const __FlashStringHelper *message, long value);
