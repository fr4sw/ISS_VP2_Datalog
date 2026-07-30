# MeteoRoi\_2026\_VP2\_Meshtastic

Projet BTS CIEL ER basé sur les ISS des Station Davis VantagePro2 fil ou sans fil, destiné à étendre la portée avec Meshtastic(série) et contenir datalogger, SD, GPS, RTC.

# STATUT

Première version reçue et partiellement testé (cf issues) (30/6/2026)

Pas encore réalisé ni testé - en attente de livraison du PCB et des composants (26 mai 2026).

# Outils\_Logiciels

Kicad 9.0.9 a été utilisé.

# Auteurs

le PCB a été conçu par l'enseignant M.Chapdelaine, faute de conception dans les temps par les étudiants.

# Micro-controleur

Pour des raisons d'économie d'énergie le choix s'est porté sur un nRF52840, largement utilisé dans les répéteurs autonomes meshtastic pour sa faible consommation  Le modele SeedStudio xiao standard a été choisi

# Périphériques

l'UART est multiplexée par un commutateur analogique :  

- le GPS n'est utilisé qu'une fois par jour pour récupérer l'heure exacte, durant ce temps on peut ne pas émettre en Meshtastic

Le RTC a été dupliqué sous forme de Pins pour insérer un module car les chip DS3231M se sont avérés etre des Fakes et le cout d'un module est inférieur au cout du chip !

## Internes

I2C :

> SDA : P1.12 SCL : P1.13

RTC : DS3231M - I2C - ?  Pression (et température, humidité interne) : Carte BMP680 - I2C - 0x77   SPI (le port 0 est plus rapide): 

> SCK = P0.02 MOSI = P0.03 MISO = P0.28

µSD : SPI, CS = P0.29   RS485 : Lien avec ISS filaire - UART (Rx seulement), toute les 2,5s ou un peu plus selon le numéro d'ISS  Rx sur P0.04 (le port 0 permet des interruptions) RFM95W (SX12xx) : SPI (pour ISS sans fil) (risque d'interférence avec le Meshtastic (même bande, mais pas même modulation)  le CS utilise le meme port que la réception RS485 (soit on est en filaire, soit en sans fil) On peuple soit R8 soit R10, on peut effectuer une détection en se mettant en réception et en regardant si on reçoit l'ISS (toutes les 2,5s), ou en mesurant si on reçoit le pull-up R9 (plus douteux)

## Externes

GPS : quelconque : UART (seulement Tx vers Rx µC)  Meshtastic : Quelconque : UART  RX = P1.15 (utilisé pour GPS ou Meshtastic) Tx = P1.14 (utilisé seulement pour Meshtastic

## Multiplexage UART

Le GPS et Meshtastic partagent l'UART, la sélection s'effectue par Mesh\_EN

# Commandes alimentation

Les plus gros consommateurs peuvent etre activés à la demande (actif 0)   
GPS\_EN : P0.05   
Meshtastic\_EN : P1.11

# Alimentation Batterie/USB

Le Xiao simple ne permet pas une gestion fine de plusieurs alimentations et ne dispose que d'une entrée alimentation (différente de 3,3V) partagée avec l'alimentation USB, un cavalier a été prévu pour éviter les drames lors de l'utilisation d'une allimentation externe (batterie), ce cavalier est à enlever IMPERATIVEMENT avant branchement sur USB, sous peine de destruction/explosion du port USB et/ou de la batterie

Ce cavalier est nécessaire pour alimenter le meshtastic en l'absence de batterie.
