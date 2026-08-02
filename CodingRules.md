# ISS\_VP2\_Datalog

# Coding Rules

Version : 0.2 Date : 2026-07-30

# 1. Objectif

Le projet ISS\_VP2\_Datalog est développé selon les principes suivants :

- lisibilité ;

- simplicité ;

- robustesse ;

- traçabilité ;

- portabilité.

Le code est destiné à être relu et maintenu plusieurs années après son écriture.

La compréhension du logiciel est prioritaire sur sa concision.

# 2. Environnement de développement

L'environnement de référence est :

- Arduino IDE 1.8.19 Portable

PlatformIO doit compiler exactement les mêmes sources.

Aucune fonctionnalité spécifique à PlatformIO ne doit être utilisée.

# 3. Philosophie

Le projet est organisé en modules indépendants.

Chaque module possède une responsabilité unique.

Le programme principal ne contient que l'enchaînement des traitements.

# 4. Portabilité

Le code métier ne doit jamais dépendre :

- du microcontrôleur ;

- de la carte utilisée ;

- des GPIO.

Les différences matérielles sont regroupées dans :

- Config.h

- BoardConfig.h

- Power.cpp

# 5. Organisation

Chaque module possède :

Module.h

Module.cpp

Le fichier .ino contient uniquement :

setup()

loop()

# 6. Unicité (constantes, ...)

Toute constante utilisee a plusieurs endroits (types de trame, broches, seuils...) DOIT etre definie une seule fois via \#define ou enum dans le .h correspondant, jamais recopiee en valeur litterale (0x08, 0x0A...) dans un .cpp.

Toute violation constatee est un defaut de conception a corriger immediatement, meme si le code compile et fonctionne en apparence - le risque d'incoherence silencieuse prime sur la commodite locale d'ecriture, en particulier pour le debug et la maintenance à long terme. 

# 7. Configuration

Config.h contient uniquement :

- options du projet ;

- activation des modules.

Exemple :

USE\_GPS

USE\_DS3231

ISS\_RS485

ISS\_WIRELESS

Aucun GPIO.

# 8. Configuration matérielle

BoardConfig.h contient uniquement :

- GPIO ;

- débits UART ;

- fréquence SPI ;

- constantes matérielles.

Aucune logique.

# 9. Abstraction matérielle (HAL)

Si des éléments dépendent des choix de cartes ou core (Serial1, nombre de parametres, …) ils seront regrouppés dans un couple HAL.h/cpp

# 10. Nommage

## Constantes

Toujours en majuscules.

Exemple :

PIN\_GPS\_RX

PIN\_SD\_CS

UART\_ISS\_BAUD

GPS\_SYNC\_PERIOD

## Variables

Toujours en camelCase.

Exemple :

gpsFix

rtcValid

frameCounter

temperatureOutside

## Fonctions

Toujours en camelCase.

Exemple :

begin()

update()

readFrame()

decodeFrame()

saveRecord()

## Classes

Majuscule initiale.

Exemple :

TimeManager

PowerManager

SDLogger

## Structures

Majuscule initiale.

Exemple :

ISSData

GPSData

Record

SystemState

# 11. GPIO

Aucun numéro de GPIO dans le code.

Toujours utiliser :

PIN\_GPS\_RX  
PIN\_SD\_CS  
PIN\_LED

Jamais :  
D4  
P0\_04  
5

# 12. Interfaces matérielles

Toutes les interfaces sont initialisées explicitement, avec les références aux pins utilisées, même si ce sont celles par défaut du core (cela peut évoluer ou ne pas correspondre au projet).

Exemple :

Wire.begin(PIN\_SCL, PIN\_SDA, I2C\_SPEED, ...)

SPI.begin(...)

Serial.begin(…)

# 13. Variables globales

Les variables globales sont limitées au strict nécessaire.

Elles sont regroupées dans une structure.

# 14. Lisibilité

La lisibilité est prioritaire sur la concision.

Le code doit pouvoir être compris plusieurs années après son écriture.

# 15. Pas de code "intelligent", éviter les astuces, préférer la lisibilité

Les astuces de programmation sont évitées. 
Le code doit être explicite.

En particulier on évitera au maximum l'utilisation de ? on préférera des if explicite.

# 16. Pas de valeurs magiques

Toute constante ayant une signification physique possède un nom explicite.

Exemple :

TEMPERATURE\_RESOLUTION

GPS\_TIMEOUT

GPS\_MINIMUM\_SATELLITES

# 17. Une instruction = une action

Une instruction ne réalise qu'une seule action.

Les effets de bord sont évités.

# 18. Pas d'opérateurs abrégés

Éviter autant que possible : +=  -=  \*=  /=  ++  --  ...

Préférer : adresse = adresse \* 2;

L'utilisation de ++ ou – est autorisé dans les boucles for

# 19. Pas de pointeurs inutiles

Préférer les références.

Exemple :

decodeFrame(ISSData &issData)

plutôt que

decodeFrame(ISSData \*issData)

# 20. Fonctions

+ Une fonction réalise une seule tâche.

Exemple : readFrame()  ne calcule pas le CRC.

DecodeFrame() ne sauvegarde pas sur SD.

+ On ne crée pas de fonction inutile : si une fonction n'est utilisé qu'une seule foisUne fonction réalise une seule tâche.

# 21. Documentation

Les commentaires expliquent : Pourquoi, et non Comment.

Le code doit être suffisamment clair pour se passer de commentaires décrivant son fonctionnement.

# 22. Traçabilité

Toute information provenant :

- d'une datasheet ;

- d'un manuel ;

- d'une documentation officielle ;

doit être référencée, le lien doit etre fourni en commentaire.

# 23. Gestion des erreurs

Aucune erreur ne doit être ignorée.

Chaque erreur doit :

- être détectée ;

- être signalée ;

- permettre un diagnostic.

# 24. Compatibilité

Toute évolution doit rester compatible avec :

Arduino IDE 1.8.19 Portable.

PlatformIO ne doit nécessiter aucune modification des sources.

# 25. Refactoring

Le projet doit compiler après chaque étape.

Aucune réécriture complète sans validation intermédiaire.

# 26. Optimisation

Les optimisations mémoire, vitesse ou consommation sont réalisées uniquement après validation fonctionnelle.

# 27. Documentation

Chaque document répond à une seule question.

Exemple :

Architecture.md		→ Comment le logiciel est organisé ?

PowerManagement.md	→ Comment est gérée l'énergie ?

BoardSupport.md		→ Quelles cartes sont supportées ?

# 28. Principe fondamental

Chaque ligne de code doit pouvoir être expliquée à un étudiant de BTS.

Si une ligne nécessite une longue explication, elle doit être réécrite de manière plus explicite.

## 29. Une seule nature de changement par commit

Chaque commit doit correspondre à **une seule catégorie de modification**.

Exemples :

- correction d'un bug ;

- portage matériel ;

- modularisation ;

- renommage ;

- optimisation ;

- documentation.

Il est interdit de mélanger plusieurs catégories dans le même commit.



# Particularités de ce projet

# 101. Gestion de l'alimentation

Toutes les commandes passent par le module Power.

Aucun digitalWrite() de gestion d'alimentation dans les autres modules.

# 102. Horodatage

Le reste du programme utilise uniquement :

TimeManager.now()

Le programme ne connaît jamais la source de l'heure.

