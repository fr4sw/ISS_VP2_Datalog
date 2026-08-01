# ISS\_VP2\_Datalog

# Readme

Version : 0.1 Date : 2026-07-30

Rédacteur : Rémi Chapdelaine

# 1. Objectif du projet

Le projet ISS\_VP2\_Datalog a pour but de fournir un datalogger externe aux stations Davis Vantage Pro2 ET transmettre les informations par un lien meshtastic (ou tout autre modem série).

Ce projet a vocation a utiliser directement l'ISS (ensemble de capteurs) sans nécessiter la console et le module datalogger de Davis, permettant des économies.

L'horodatage fiable fait partie des principes important pour pouvoir recoller les informations des différentes stations.

La normalisation des unités et des mesures doit viser les bonnes pratiques en météorologie, dans les limites de ce qui est fourni par Davis.

# 2. Contexte

Ce projet a été utilisé plusieurs années comme projet de fin d'étude en BTS Systèmes numériques/CIEL Electronique en partenariat avec l'association MeteoR-OI.

# 3. Pré-requis - versions

Testé :

IDE/Core : Arduino IDE portable 1.8.19, core Seed nRF52 Boards 1.1.13, core ESP32 3.2.0-\>3.3.11

Librairies : Adafruit BME680 2.0.6 (Adafruit BusIO 1.17.4, Adafruit unified sensors 1.1.15)

# 4. Matériel Davis

Ce projet est conçu pour récupérer les informations d'une ensemble de capteurs ISS Davis Vantage Pro2 qui existe sous 2 forme :

- filaire= liaison RS485, alimentation par le fil

- sans fil = liaison radio en modulation FSK, fréquence selon les normes locales ISM (868, 915, 433 MHz)

# 5. Matériel

Des cartes spécifiques ont été conçues par les étudiants de BTS durant les projets ou par l'enseignant, cf rubrique Hardware.

Le logiciel utilisé est Kicad 9.

# 6. Logiciel

Le logiciel est structuré pour permettre :

- des développements partagés (modules par fonction)

- une maintenance facile (commentaires)

- un développement sur des matériels différents (couches d'abstraction, définitions des cartes et pins centralisées)

- Arduino 1.8.19 portable ou PlatformIO, les 2 peuvent etre utilisés

Les développements doivent respecter les règles de codage (cf doc) et etre accessible à des BTS.  
Chaque ligne de code doit pouvoir être expliquée à un étudiant de BTS.  
Si une ligne nécessite une longue explication, elle doit être réécrite de manière plus explicite.

Une partie du développement a été réalisé avec l'aide de Claide Sonnet 5 (Perplexity), en corrigeant ses erreurs et affinant les possibiités.

# 7. Gestion de l'alimentation

La cible visée étant l'autonomie totale (énergétique et réseau) la préservation de l'énergie devra etre incluse dans les réflexions.

L'usage de panneaux photovoltaique et de batteries ou super condensateurs est quasi indispensable et sera à optimiser.

# 8. A venir

Cf ToDo list pour les détails

Tester gestion du temps avec GPS et RTC

Intégrer BME680 et/ou d'autres capteurs pour disposer de la température interne et de la pression atmosphérique (éléments qui étaient fournis par la console et non par l'ISS)

Synthétiser les informations au pas de 5 minutes comme le fait la console+datalogger

# 9. ?

?

