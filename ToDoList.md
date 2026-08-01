# ToDoList

- TimeManager : etudier un mode combine GPS+MANUEL (le GPS resynchronise
  le point de reference manuel de la meme facon que GPS+RTC), pour le cas
  ou aucun RTC n'est present mais un GPS existe par intermittence.

- TimeManager mode manuel : le calcul simplifie ne gere pas le changement
  de mois. A ameliorer si des campagnes de plusieurs semaines sont prevues
  sans GPS ni RTC.
  
- affiner la couche d'abstraction avec ESP32 et les fonctions spécifiques (web, OTA)