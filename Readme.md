# ISS\_VP2\_Datalog

# Readme

Version : 0.1.3 Date : 2026-08-04

Rédacteur : Rémi Chapdelaine

# 1. Objectif du projet

Le projet ISS\_VP2\_Datalog a pour but de fournir un datalogger externe aux stations Davis Vantage Pro2 ET transmettre les informations par un lien meshtastic (ou tout autre modem série).

Ce projet a vocation a utiliser directement l'ISS (ensemble de capteurs) sans nécessiter la console et le module datalogger de Davis, permettant des économies.

L'horodatage fiable fait partie des principes important pour pouvoir recoller les informations des différentes stations.

La normalisation des unités et des mesures doit viser les bonnes pratiques en météorologie, dans les limites de ce qui est fourni par Davis.
## 1A. Etat actuel (4/8/2026 v0.1.3)

En cours de mise au point, fonctionnel avec ISS filaire (seul temp et Hum testés), nRF52840-Xiao, BME680, RTC DS3231, GPS, carte µSD.
Certains paramétrage peuvent s'effectuer depuis le moniteur série (serial).
Sur la carte SD on écrit toutes les 30s (paramétrable) dans un fichier .csv; toutes les 5 minutes (paramétrable) on fait la synthèse (comme le fait WeeWx/Davis...) et on enregistre aussi (c'est cela qu'on enverra).
On écrit aussi un EVENTS.log qui récapitule les évènements principaux (dont les durée d'allumage GPS et les décalages horaires - permettront d'estimer la possibilité d'allonger la durée de recalage).
-> release v0.1.3

La prochaine étape est l'implantation du Meshtastic en protobuff simplifié.
-> 13/8/26 globalement validé, les infos remontent mais timing à consolider

En cours : automatisation du capteur I2C interne pour supporter/reconaitre automatiquement BME680, BME280, BMP280 ...

# 2. Contexte
Ce projet a été utilisé plusieurs années comme projet de fin d'étude en BTS Systèmes numériques/CIEL Electronique en partenariat avec l'association MeteoR-OI.

# 3. Pré-requis - versions
Testé :
IDE/Core : Arduino IDE portable 1.8.19, core Seed nRF52 Boards 1.1.13, core ESP32 3.2.0-\>3.3.11 (Wire et SD sont utilisés mais elles sont incluses dans les Cores)
Librairies : 
- BME680 Adafruit (2.0.6) (Adafruit BusIO 1.17.4, Adafruit unified sensors 1.1.15) https://github.com/adafruit/Adafruit_BME680
- RTClib Adafruit (2.1.4) https://github.com/adafruit/RTClib
- TinyGPSplus mikalhart (1.0.3) (utile seulement si GPS - Cf config/boardConfig) https://github.com/mikalhart/TinyGPSPlus

# 4. Matériel Davis

Ce projet est conçu pour récupérer les informations d'une ensemble de capteurs ISS Davis Vantage Pro2 qui existe sous 2 formes :

- filaire= liaison RS485, alimentation par le fil

- sans fil = liaison radio en modulation FSK, fréquence selon les normes locales ISM (868, 915, 433 MHz)

## 4A. Documentations
- Wiki du projet DavisRFM69, décrit les trames entre ISS et console : https://github.com/dekay/DavisRFM69/wiki/Message-Protocol
- Documentation Davis de la communication avec le Datalogger (weatherlink) : https://support.davisinstruments.com/article/rbzgl0rh6k-vantage-pro-pro-2-and-vue-communications-reference-2-6-1-any-os pointe vers https://cdn.shopify.com/s/files/1/0515/5992/3873/files/VantageSerialProtocolDocs_v261.pdf?v=1614399559
  
- Meteoengins décrit la méthode d'enregistrement dans Weatherlink et datalogger ainsi que les calculs de données dérivées (indice de chaleur, point de rosée, évapotranspiration, ...) : http://meteoengins.fr/documents/davis/Davis_Data_Archived_v3.pdf

# 5. Matériel

Des cartes spécifiques ont été conçues par les étudiants de BTS durant les projets ou par l'enseignant, cf rubrique Hardware.

Le logiciel utilisé est Kicad 9.

# 6. Logiciel

Le logiciel est structuré pour permettre :

- des développements partagés (modules par fonction)

- une maintenance facile (commentaires, documentation)

- un développement sur des matériels différents (couches d'abstraction, définitions des cartes et pins centralisées)

- Arduino 1.8.19 portable ou PlatformIO, les 2 peuvent etre utilisés

Les développements doivent respecter les règles de codage (cf doc) et etre accessible à des BTS.  
Chaque ligne de code doit pouvoir être expliquée à un étudiant de BTS.  
Si une ligne nécessite une longue explication, elle doit être réécrite de manière plus explicite.

Une partie du développement a été réalisé avec l'aide de Claude Sonnet 5, en corrigeant ses erreurs et affinant les possibiités.

Les règles de codages sont dans le document CodingRules.md

# 7. Gestion de l'alimentation

La cible visée étant l'autonomie totale (énergétique et réseau) la préservation de l'énergie devra etre incluse dans les réflexions.

L'usage de panneaux photovoltaique et de batteries ou super condensateurs est quasi indispensable et sera à optimiser.

# 8. A venir

Cf ToDo list pour les détails

Tester gestion du temps avec GPS et RTC - en cours, reste GPS

Intégrer BME680 - réalisé et d'autres capteurs (BME280, DHT, ...) pour disposer de la température interne et de la pression atmosphérique (éléments qui étaient fournis par la console et non par l'ISS)
Calibration des différentes données (température, humidité, pression) et stockage des paramètres de calibration

Synthétiser les informations au pas de 5 minutes comme le fait la console+datalogger

# 9. ?

?

