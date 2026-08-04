# ToDoList

- TimeManager : etudier un mode combine GPS+MANUEL (le GPS resynchronise
  le point de reference manuel de la meme facon que GPS+RTC), pour le cas
  ou aucun RTC n'est present mais un GPS existe par intermittence.

- TimeManager mode manuel : le calcul simplifie ne gere pas le changement
  de mois. A ameliorer si des campagnes de plusieurs semaines sont prevues
  sans GPS ni RTC.
  
- affiner la couche d'abstraction avec ESP32 et les fonctions spécifiques (web, OTA)

- A terme : remplacer les #define DEBUG/DEBUG_RAW_FRAMES/DEBUG_GPS par des
  parametres runtime (module Params + SerialConsole), comme deja fait pour
  TZ/LOGINTERVAL/GPSBAUD. Impact important a prevoir (les #if de
  compilation conditionnelle deviendraient des if() executes a chaque
  loop() - a mesurer sur la taille memoire/temps d'execution avant de
  generaliser).

- A terme : generaliser le principe Params/SerialConsole a d'autres
  parametres (ex : activation/desactivation d'un capteur interieur donne -
  BME680, BME280, DHT22, ... - sans reprogrammer), pour rendre le
  changement de capteur une operation de configuration plutot que de
  code. Necessitera une interface capteur commune (voir règle 3,
  modularite) avant de brancher plusieurs implementations interchangeables.

- A terme : interface Bluetooth (configuration/consultation a distance),
  a evaluer une fois Meshtastic en place.