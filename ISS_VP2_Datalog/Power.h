// ============================================================================
// Fichier   : Power.h
// Rôle      : Point d'entree unique pour l'activation des peripheriques
//             consommateurs d'energie. Aucun autre module n'effectue de
//             digitalWrite lie a l'alimentation d'un peripherique externe.
// Fonctions : begin()        - initialise les broches de commande d'alimentation.
//             enableGps()    - active l'alimentation du module GPS.
//             disableGps()   - coupe l'alimentation du module GPS.
//             enableMesh()   - active l'alimentation du module radio Mesh.
//             disableMesh()  - coupe l'alimentation du module radio Mesh.
// Référence : CodingRules_Gen.md §11
// Note      : Version initiale sans gestion de veille processeur.
//             La veille sera traitee dans une evolution distincte.
// ============================================================================
#pragma once
#include <Arduino.h>

class Power
{
public:
    void begin();
    void enableGps();
    void disableGps();
    void enableMesh();
    void disableMesh();
};

extern Power power;
