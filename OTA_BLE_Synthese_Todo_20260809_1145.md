# OTA (mise à jour firmware) par Bluetooth — synthèse et état d'avancement

**Statut au moment de la rédaction : mis en pause.** Le firmware embarque déjà tout le nécessaire côté carte (service DFU actif). Le blocage se situe uniquement côté outillage PC/téléphone pour *déclencher* le transfert — rien à modifier dans le code pour reprendre. Ce document sert de point de reprise quand un deuxième Xiao sera disponible, pour ne pas risquer de bricker l'unique carte de test actuelle pendant les essais.

---

## 1. Ce qui est acquis côté firmware

- Bootloader confirmé présent sur la carte : **Adafruit nRF52 Bootloader** (visible dans l'IDE Arduino, *Outils → Programmateur* → "Bootloader DFU for bluefruit nRF52"). Il gère nativement le DFU (Device Firmware Update) par BLE, sans rien à écrire de spécifique côté application au-delà de l'enregistrement du service.
- Le firmware ISS_VP2_Datalog enregistre le service `BLEDfu` (fourni clé en main par la bibliothèque Bluefruit) dans `BleLink.cpp` :
  ```cpp
  static BLEDfu bledfu;
  // ...
  bledfu.begin();   // appelé dans bleLinkBegin(), avant les autres services
  ```
  Ce service n'a qu'un rôle : annoncer un point d'entrée qui redémarre la carte en mode bootloader. C'est ensuite le bootloader lui-même (pas notre code applicatif) qui gère le transfert du nouveau firmware via son propre service BLE Nordic Secure DFU.
- Programmateur utilisé pour flasher/récupérer la carte : **JLink**, déjà configuré dans l'IDE — c'est le même lien physique/même config que pour la programmation normale, aucun matériel supplémentaire nécessaire pour graver ou regraver le bootloader (*Outils → Graver la séquence d'amorçage*).
- **Aucun risque de brique définitive constaté** : à chaque essai OTA raté, une reprogrammation filaire classique a suffi à récupérer la carte. Le bootloader n'a jamais été corrompu par un DFU BLE incomplet.

## 2. Procédure de principe (résumé)

1. Compiler le firmware normalement dans l'IDE.
2. Générer le paquet DFU (`.zip`) à partir du binaire compilé :
   - soit automatiquement à l'export de l'IDE (à vérifier selon la version du core Seeeduino installée),
   - soit manuellement avec `adafruit-nrfutil` :
     ```
     adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application firmware.hex firmware_dfu.zip
     ```
3. Sur la carte déjà alimentée et faisant tourner un firmware avec `BLEDfu` actif, se connecter avec une appli compatible DFU et lui transmettre le `.zip`.
4. L'appli envoie le firmware par morceaux ; la carte redémarre en bootloader, applique la mise à jour, puis redémarre sur le nouveau firmware.

## 3. Outils testés et résultat

### Côté PC

| Outil | Résultat | Remarque |
|---|---|---|
| **Bluetooth LE Explorer** (Microsoft Store) | ❌ Ne gère pas DFU | C'est un navigateur GATT générique — il ne connaît pas le protocole de contrôle Nordic Secure DFU (négociation, découpage en paquets, etc.). Utilise le Bluetooth natif du PC, mais ça ne suffit pas : il manque la couche protocole. |
| **nRF Connect for Desktop** | Non testé jusqu'au bout | Sa fonction DFU nécessite très probablement un **dongle USB Nordic** (nRF52840 Dongle ou DK) pour servir de "central" BLE — le Bluetooth intégré du PC seul ne suffit généralement pas avec cet outil. Pas de dongle disponible → piste abandonnée sans test réel. |
| **`adafruit-nrfutil`** (version fournie dans le package Seeeduino, `.../tools/adafruit-nrfutil/win32`) | ⚠️ Incomplète | Cette copie embarquée ne supporte pas le BLE (sous-commande `dfu ble` absente/non fonctionnelle) — semble être une build minimale liée au core Arduino, sans les dépendances Bluetooth Python complètes. |
| `adafruit-nrfutil dfu serial ...` | ❌ Mauvaise sous-commande | C'est le DFU **filaire** (UART), pas Bluetooth — piste explorée par erreur au départ à cause d'un exemple de commande copié depuis une doc Mac/Linux (`/dev/tty...`, non transposable directement sous Windows). |
| `pip install --upgrade adafruit-nrfutil` (version complète) | Non testé jusqu'au bout | Installée pour obtenir la sous-commande `dfu ble`. Même réserve que nRF Connect Desktop : dépend probablement de `pc-ble-driver`/un dongle Nordic pour fonctionner réellement depuis un PC. Non validé faute de dongle. |

### Côté téléphone (Android)

| Outil | Résultat | Remarque |
|---|---|---|
| **nRF Connect for Mobile** | ❌ Sélecteur de fichier bloqué | Le bouton DFU (icône flèche vers le bas + barre) s'affiche bien après connexion normale à la carte (comportement attendu : c'est cette première connexion qui permet à l'appli de découvrir le service DFU). Mais le sélecteur de fichier interne refuse de proposer/sélectionner le `.zip`, quelle que soit son origine (transfert Bluetooth PC→tel, téléchargement Google Drive). Cause probable : filtrage par type MIME (`application/octet-stream` au lieu de `application/zip` selon le mode de transfert), pas un souci avec le fichier lui-même. Le menu "Partager" d'Android ne propose de toute façon pas nRF Connect comme cible — l'appli n'accepte les fichiers que via son sélecteur interne, pas via l'intégration Partager standard d'Android. |
| **nRF DFU** *(nom correct — pas "nRF Toolbox", erreur de nommage initiale corrigée en cours de discussion)* | ⚠️ Accepte le fichier mais transfert incomplet | Contrairement à nRF Connect, celui-ci **accepte** le `.zip` sans problème (preuve que le fichier généré est valide). Séquence observée : "Bootloader enabled" → "DFU initialized" → "Uploading" jusqu'à 100% → **reste bloqué indéfiniment, ne passe jamais à "Completed"**. Testé avec : distance < 30 cm, attente d'environ 3 minutes après le 100%, sans amélioration. Reproduit sur plusieurs essais. |

## 4. Hypothèses envisagées pour le blocage à 100% (nRF DFU)

Par ordre de probabilité décroissante, aucune n'a été définitivement confirmée ni éliminée avec certitude :

1. **Version du bootloader** — anciennes versions du bootloader Adafruit nRF52 connues pour des soucis de fiabilité sur l'étape finale du DFU BLE (vérification CRC32 + validation avant redémarrage). **Piste la plus probable**, jamais testée (mise à jour du bootloader via JLink non tentée par choix — voir §5).
2. **Gestion agressive de la batterie côté Android** — certains téléphones (Samsung/Xiaomi/OnePlus notamment) tuent les connexions BLE d'arrière-plan pile au moment de la commande finale de validation. Suggéré (*Paramètres → Applications → nRF DFU → Batterie → Sans restriction*, écran allumé pendant tout le transfert) — **résultat non confirmé explicitement par l'utilisateur avant la mise en pause**, à retester.
3. **Cache GATT système Android** — un résidu de cache au niveau du système (pas de l'appairage classique, qui lui a été vérifié comme vide) peut perturber la fin d'un transfert DFU sur Android récent. Suggéré (redémarrage complet du téléphone), pas testé de façon isolée (un redémarrage était prévu juste avant la décision de mise en pause).
4. **Interférences radio 2,4 GHz** — écarté en bonne partie : testé à moins de 30 cm sur plusieurs essais sans amélioration.
5. **Appairage résiduel** — écarté : aucune entrée "ISS-VP2-Datalog" ni "DfuTarg" trouvée dans les paramètres Bluetooth du téléphone (normal, un appareil en mode DFU ne s'appaire pas classiquement, il n'est visible que via le scan interne de l'appli).

## 5. Décision et raison de la mise en pause

Un seul Xiao nRF52840 disponible actuellement pour les tests. Le test le plus prometteur restant (mise à jour du bootloader via JLink) est en théorie sans risque puisque filaire, mais implique de modifier une zone mémoire sensible (bootloader) sur l'unique carte utilisée pour tout le reste du projet (ISS, GPS, Mesh, BLE) — le risque, même faible, de rendre la carte totalement injoignable (y compris en filaire) pendant les essais n'a pas été jugé acceptable tant qu'un deuxième exemplaire n'est pas disponible pour absorber un éventuel problème.

## 6. Plan de reprise avec un deuxième Xiao

Ordre recommandé, du moins risqué au plus risqué :

1. **Retester nRF DFU** avec les paramètres de batterie Android désactivés + téléphone fraîchement redémarré (hypothèses 2 et 3 du §4) — gratuit, zéro risque, à faire en premier même si peu probable.
2. **Mettre à jour le bootloader** sur le Xiao "de réserve" (celui qu'on peut se permettre de bricker temporairement) via l'IDE (*Outils → Graver la séquence d'amorçage*, JLink filaire) — c'est le test le plus décisif d'après le diagnostic. Si ça résout le blocage à 100%, appliquer la même mise à jour sur la carte de terrain en toute confiance.
3. Si le bootloader à jour ne change rien : creuser côté PC avec un vrai dongle Nordic (nRF52840 Dongle, peu coûteux) pour lever l'inconnue sur `nRF Connect for Desktop` / `adafruit-nrfutil dfu ble` — élimine le téléphone de l'équation et donne un canal de test plus contrôlable/reproductible.
4. Envisager en dernier recours **nRF Connect Mobile** avec un contournement du sélecteur de fichier (ex : renommer temporairement le `.zip` en `.txt` puis le renommer une fois sélectionné, ou utiliser une appli tierce de gestion de fichiers permettant de forcer le type MIME) — piste non explorée, faible priorité vu que nRF DFU accepte déjà le fichier sans problème.

## 7. Rappel — l'OTA n'est pas structurante pour le reste

Le firmware fonctionne normalement et se programme sans problème en filaire (JLink) en attendant. La mise en pause de l'OTA ne bloque aucun autre chantier du projet (Mesh, BLE de lecture, calibration du taux de réception, épisodes de pluie, etc.).
