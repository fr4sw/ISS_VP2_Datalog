# ToDoList

- TimeManager : etudier un mode combine GPS+MANUEL (le GPS resynchronise
  le point de reference manuel de la meme facon que GPS+RTC), pour le cas
  ou aucun RTC n'est present mais un GPS existe par intermittence.
- Power/BoardConfig (nRF52840) : PIN_GPS_RX et PIN_MESH_RX partagent la
  meme broche physique (D10/P1.15). Definir la logique d'exclusion mutuelle
  temporelle GPS/Mesh dans Power.cpp avant d'activer les deux modules.
- TimeManager mode manuel : le calcul simplifie ne gere pas le changement
  de mois. A ameliorer si des campagnes de plusieurs semaines sont prevues
  sans GPS ni RTC.