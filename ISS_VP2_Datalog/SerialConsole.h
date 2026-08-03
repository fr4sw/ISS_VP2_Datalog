// ============================================================================
// Fichier   : SerialConsole.h
// Rôle      : Interface operateur en ligne de commande sur le port serie
//             USB, pour consulter/modifier les parametres (module Params)
//             sans reprogrammer. Toujours active (pas de "sequence de
//             reveil" a taper au demarrage : plus simple et plus robuste -
//             une commande tapee au mauvais moment est juste ignoree si
//             elle ne correspond a rien).
// Fonctions : begin() - initialisation (rien a faire pour l'instant, la
//                       reservee pour usages futurs).
//             update() - a appeler dans loop(). Assemble les caracteres
//                       recus sans jamais bloquer, exécute la commande des
//                       qu'une ligne complete (terminee par '\n') est
//                       recue.
// Commandes : HELP                - affiche l'aide.
//             GET                 - affiche les parametres courants.
//             SET <CLE> <VALEUR>  - modifie un parametre en RAM (pas encore
//                                   persiste, voir SAVE). Cles : TZ (minutes
//                                   par rapport a l'UTC), LOGINTERVAL (ms,
//                                   0 = ecriture a chaque trame).
//             SAVE                - enregistre les parametres courants sur
//                                   la carte SD (PARAMS_FILE_NAME).
// Remarque  : Le coeur "assembler une ligne, la decouper en mots, dispatcher"
//             est ecrit independamment du port physique (une seule fonction
//             prend une ligne de texte en entree) : une extension future
//             (ex : reception de commandes via Meshtastic) pourra reutiliser
//             ce decoupage sans le reecrire. Non implemente aujourd'hui
//             (règle 15 : pas d'abstraction speculative non testee).
// ============================================================================
#pragma once
#include <Arduino.h>

void serialConsoleBegin();
void serialConsoleUpdate();
