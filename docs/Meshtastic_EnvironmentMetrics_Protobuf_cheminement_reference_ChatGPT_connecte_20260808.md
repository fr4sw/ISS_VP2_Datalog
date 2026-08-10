# Meshtastic — cheminement complet d’une télémétrie `EnvironmentMetrics`

## Document de référence technique

**Objet :** suivre une mesure environnementale depuis le capteur jusqu’au nœud destinataire, en détaillant Protobuf, `Telemetry`, `Data`, `MeshPacket`, chiffrement, relais et transport LoRa.

**Date :** 2026-08-08

---

## 1. Vue d’ensemble

```text
CAPTEUR
  │
  ▼
EnvironmentMetrics
  │ Protobuf
  ▼
Telemetry
  │ Protobuf
  ▼
Data
  │ sérialisation
  ▼
données à chiffrer
  │ AES-CTR (canal)
  ▼
ciphertext
  │
  ▼
MeshPacket / informations radio
  │
  ▼
LoRa
  │
  ▼
nœud relais éventuel
  │
  ▼
nœud destinataire
  │
  ▼
déchiffrement
  │
  ▼
Data
  │ Protobuf
  ▼
Telemetry
  │
  ▼
EnvironmentMetrics
  │
  ▼
valeurs physiques
```

**Point essentiel :** `EnvironmentMetrics` n’est pas directement un paquet LoRa. C’est un message Protobuf imbriqué dans `Telemetry`, lui-même transporté par `Data`.

---

## 2. Sources officielles

### Protobufs Meshtastic

https://github.com/meshtastic/protobufs

Fichiers principaux :

- `meshtastic/telemetry.proto`
- `meshtastic/mesh.proto`
- `meshtastic/portnums.proto`
- `meshtastic/channel.proto`

### Firmware

https://github.com/meshtastic/firmware

Source de référence pour le comportement effectivement exécuté.

### Documentation du protocole

https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md

Cette documentation décrit notamment `MeshPacket`, `Data`, `PortNum`, routage, chiffrement, nonce, ACK et PhoneAPI.

### Documentation officielle

https://meshtastic.org/

### Wire Format Protobuf

https://protobuf.dev/programming-guides/encoding/

---

# 3. Exemple de départ

On considère un capteur mesurant :

```text
Température       = 24,6 °C
Humidité relative = 63,2 %
Pression          = 1013,2 hPa
```

Objet logique :

```text
EnvironmentMetrics
├── temperature = 24,6
├── relative_humidity = 63,2
└── barometric_pressure = 1013,2
```

---

# 4. `EnvironmentMetrics`

Référence :

https://github.com/meshtastic/protobufs/blob/master/meshtastic/telemetry.proto

La structure contient notamment des champs tels que :

```protobuf
message EnvironmentMetrics {
    optional float temperature = 1;
    optional float relative_humidity = 2;
    optional float barometric_pressure = 3;
    ...
}
```

Les numéros de champs sont essentiels pour le Wire Format Protobuf.

---

# 5. Wire Format Protobuf

La clé d’un champ est calculée par :

```text
key = (field_number << 3) | wire_type
```

| Wire type | Valeur | Utilisation |
|---|---:|---|
| VARINT | 0 | entiers, enums, booléens |
| FIXED64 | 1 | valeurs fixes 64 bits |
| LENGTH_DELIMITED | 2 | `bytes`, chaînes, messages imbriqués |
| FIXED32 | 5 | `float` |

Un `float` utilise donc le Wire Type `5`.

---

# 6. Sérialisation de `EnvironmentMetrics`

## 6.1 Température

```protobuf
optional float temperature = 1;
```

```text
field_number = 1
wire_type    = 5
key          = (1 << 3) | 5 = 0x0D
```

Pour `24,6`, le `float32` IEEE-754 est `0x41C4CCCD`.

Little-endian :

```text
CD CC C4 41
```

Champ :

```text
0D CD CC C4 41
```

## 6.2 Humidité

```protobuf
optional float relative_humidity = 2;
```

```text
key = (2 << 3) | 5 = 0x15
```

Pour `63,2` :

```text
float32 = 0x427CCCCD
little-endian = CD CC 7C 42
```

Champ :

```text
15 CD CC 7C 42
```

## 6.3 Pression

```protobuf
optional float barometric_pressure = 3;
```

```text
key = (3 << 3) | 5 = 0x1D
```

Pour `1013,2` :

```text
float32 = 0x447D4CCD
little-endian = CD 4C 7D 44
```

Champ :

```text
1D CD 4C 7D 44
```

---

# 7. `EnvironmentMetrics` sérialisé

```text
0D CD CC C4 41
15 CD CC 7C 42
1D CD 4C 7D 44
```

Sur une ligne :

```text
0D CD CC C4 41 15 CD CC 7C 42 1D CD 4C 7D 44
```

Taille : **15 octets**.

---

# 8. Encapsulation dans `Telemetry`

Référence :

https://github.com/meshtastic/protobufs/blob/master/meshtastic/telemetry.proto

Conceptuellement :

```text
Telemetry
├── time
└── environment_metrics
      ├── temperature
      ├── relative_humidity
      └── barometric_pressure
```

Le champ `environment_metrics = 3` est un message imbriqué, donc Wire Type `2` :

```text
key = (3 << 3) | 2 = 0x1A
```

La longueur de `EnvironmentMetrics` est 15 octets : `0F`.

```text
1A 0F
0D CD CC C4 41
15 CD CC 7C 42
1D CD 4C 7D 44
```

Taille : **17 octets**.

---

# 9. Pourquoi `1A 0F` ?

Protobuf utilise pour un message imbriqué :

```text
clé
longueur
contenu
```

Ici :

```text
1A  → champ 3 + Wire Type 2
0F  → longueur = 15 octets
<15 octets> → EnvironmentMetrics
```

---

# 10. `Data`

Référence :

https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto

Conceptuellement :

```protobuf
message Data {
    PortNum portnum = 1;
    bytes payload = 2;
    ...
}
```

Pour la télémétrie :

```text
portnum = TELEMETRY_APP
payload = sérialisation de Telemetry
```

Référence des `PortNum` :

https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto

---

# 11. Encodage de `portnum`

Champ 1, Wire Type 0 :

```text
08
```

`TELEMETRY_APP` vaut `67` décimal, soit `0x43` :

```text
08 43
```

---

# 12. Encodage du `payload`

Le champ `bytes payload = 2` utilise Wire Type `2` :

```text
key = (2 << 3) | 2 = 0x12
```

`Telemetry` fait 17 octets, donc longueur `0x11` :

```text
12 11
```

suivi des 17 octets.

---

# 13. `Data` sérialisé

```text
08 43
12 11
1A 0F
0D CD CC C4 41
15 CD CC 7C 42
1D CD 4C 7D 44
```

Sur une ligne :

```text
08 43 12 11 1A 0F 0D CD CC C4 41 15 CD CC 7C 42 1D CD 4C 7D 44
```

Taille : **21 octets**.

---

# 14. Les trois niveaux Protobuf

```text
EnvironmentMetrics
        │ Protobuf
        ▼
Telemetry
        │ Protobuf
        ▼
Data
```

Dans notre exemple :

```text
EnvironmentMetrics = 15 octets
Telemetry          = 17 octets
Data               = 21 octets
```

---

# 15. `MeshPacket`

Référence :

https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto

Le `MeshPacket` constitue la structure réseau logique. On y trouve notamment des informations permettant de gérer :

```text
from
 to
channel
id
flags
hop_limit
hop_start
payload_variant
```

Conceptuellement :

```text
MeshPacket
├── from
├── to
├── channel
├── id
├── hop_limit
├── hop_start
└── payload_variant
      ├── decoded = Data
      └── encrypted = bytes
```

**Important :** un `MeshPacket` logique ne doit pas être confondu avec le format exact des octets transmis par la PHY LoRa.

---

# 16. Exemple de paramètres réseau

Valeurs fictives utilisées uniquement pour illustrer :

```text
from = 0x11223344
to   = 0x55667788
id   = 0x12345678

hop_start = 3
hop_limit = 3
```

---

# 17. `hop_limit` et `hop_start`

Exemple :

```text
départ :
hop_start = 3
hop_limit = 3
```

Après un relais :

```text
hop_start = 3
hop_limit = 2
```

Après deux relais :

```text
hop_start = 3
hop_limit = 1
```

Conceptuellement :

```text
hops_away = hop_start - hop_limit
```

Référence :

https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md

---

# 18. Le relais

```text
Nœud A ─────► Relais R ─────► Nœud B
```

Le relais reçoit les informations réseau et le payload chiffré. Il peut décider de retransmettre sans reconstruire `EnvironmentMetrics`.

Traitement conceptuel :

```text
réception
   ↓
contrôle / déduplication
   ↓
décision de relais
   ↓
hop_limit--
   ↓
retransmission
```

Le ciphertext reste le contenu transporté.

---

# 19. Chiffrement du `Data`

Référence :

https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md

Pour le chiffrement de canal, la documentation décrit notamment :

```text
PSK de 16 octets → AES-128-CTR
PSK de 32 octets → AES-256-CTR
```

Chaîne :

```text
Data sérialisé
      ↓
plaintext
      ↓ AES-CTR
ciphertext
```

AES-CTR conserve la longueur :

```text
plaintext  = 21 octets
ciphertext = 21 octets
```

---

# 20. Exemple de clé

Pour illustrer le calcul uniquement :

```text
00 01 02 03 04 05 06 07
08 09 0A 0B 0C 0D 0E 0F
```

Cette clé est fictive et ne représente pas une clé Meshtastic réelle.

---

# 21. Nonce AES-CTR

La documentation du protocole décrit un nonce construit à partir notamment de `packet_id` et `from_node`. La construction exacte doit être vérifiée contre la version firmware ciblée.

Référence :

https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md

Pour l’exemple :

```text
packet_id = 0x12345678
from      = 0x11223344
```

Représentation little-endian d’exemple :

```text
78 56 34 12 00 00 00 00
44 33 22 11
00 00 00 00
```

**Pour une implémentation réelle, le firmware de la version utilisée est la source de vérité.**

---

# 22. AES-CTR

```text
                 clé
                  │
                  ▼
              AES-CTR
                  │
nonce ───────────►│
                  │
                  ▼
             keystream
                  │
                  XOR
                  │
plaintext ────────┘
                  │
                  ▼
             ciphertext
```

```text
ciphertext = plaintext XOR keystream
plaintext  = ciphertext XOR keystream
```

---

# 23. Ce que voit un relais

Le relais peut exploiter les informations nécessaires au routage :

```text
from
to
id
hop_limit
hop_start
channel / informations de canal
...
```

Lorsque le contenu applicatif est chiffré, il ne peut pas simplement lire :

```text
temperature
relative_humidity
barometric_pressure
```

sans disposer de la clé correspondante.

---

# 24. Arrivée au destinataire

Le destinataire reçoit :

```text
header réseau/radio
+
ciphertext
```

Après déchiffrement, il retrouve le `Data` sérialisé :

```text
08 43 12 11
1A 0F
0D CD CC C4 41
15 CD CC 7C 42
1D CD 4C 7D 44
```

---

# 25. Décodage du `Data`

Premier champ :

```text
08 43
```

`08` signifie :

```text
field_number = 1
wire_type = 0
```

Donc :

```text
portnum = 67 = TELEMETRY_APP
```

Deuxième champ :

```text
12 11
```

`12` signifie :

```text
field_number = 2
wire_type = 2
```

`11` indique 17 octets de payload :

```text
1A 0F
0D CD CC C4 41
15 CD CC 7C 42
1D CD 4C 7D 44
```

---

# 26. Décodage de `Telemetry`

```text
1A
```

correspond à :

```text
field_number = 3
wire_type = 2
```

Donc :

```text
environment_metrics
```

Puis :

```text
0F
```

indique 15 octets :

```text
0D CD CC C4 41
15 CD CC 7C 42
1D CD 4C 7D 44
```

---

# 27. Décodage final

```text
0D CD CC C4 41
```

→ `temperature = 24,6 °C`

```text
15 CD CC 7C 42
```

→ `relative_humidity = 63,2 %`

```text
1D CD 4C 7D 44
```

→ `barometric_pressure = 1013,2 hPa`

On a donc reconstruit les données applicatives initiales.

---

# 28. Chemin complet

```text
                         CAPTEUR
                            │
                            ▼
                 EnvironmentMetrics
                            │
                       Protobuf
                            ▼
                       Telemetry
                            │
                       Protobuf
                            ▼
                          Data
                            │
                       AES-CTR
                            ▼
                       ciphertext
                            │
                 + header réseau/radio
                            │
                            ▼
                           LoRa
                            │
                            ▼
                         RELAIS
                            │
                      hop_limit--
                            │
                            ▼
                           LoRa
                            │
                            ▼
                     DESTINATAIRE
                            │
                       AES-CTR
                            ▼
                          Data
                            │
                       Protobuf
                            ▼
                       Telemetry
                            │
                            ▼
                 EnvironmentMetrics
                            │
                            ▼
                    valeurs physiques
```

---

# 29. Protobuf, chiffrement et LoRa : trois rôles différents

### Protobuf

Définit la structure et sa sérialisation binaire.

### Chiffrement

Transforme le plaintext en ciphertext pour assurer la confidentialité selon le mécanisme de sécurité utilisé.

### LoRa

Transporte physiquement les données par radio.

Donc :

```text
Protobuf ≠ chiffrement ≠ LoRa
```

---

# 30. `PortNum`

`PortNum` indique quelle application doit interpréter le payload :

```text
67
↓
TELEMETRY_APP
↓
Telemetry
```

Architecture :

```text
Data
├── portnum
│     └── identifie l'application
│
└── payload
      └── bytes interprétés par l'application
```

Référence :

https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto

---

# 31. PhoneAPI

Le nœud peut communiquer avec un téléphone ou un PC via Bluetooth, USB ou TCP/IP selon la configuration.

```text
Nœud Meshtastic
      │
      ├── Bluetooth
      ├── USB
      └── TCP/IP
             │
             ▼
       Téléphone / PC
```

Le protocole API utilise notamment `ToRadio` et `FromRadio`.

Référence :

https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md

---

# 32. MQTT

MQTT peut servir de pont vers le monde IP :

```text
Nœud Meshtastic
      │ LoRa
      ▼
Gateway
      │ IP
      ▼
MQTT broker
      │
      ▼
Application / serveur
```

MQTT ne doit pas être confondu avec le transport radio LoRa.

Référence :

https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md

---

# 33. Canal chiffré et PKI

Le chiffrement de canal et le chiffrement PKI ne doivent pas être confondus.

Le canal utilise notamment :

```text
AES-128-CTR
ou
AES-256-CTR
```

Les messages directs utilisant la PKI suivent un mécanisme différent, documenté avec notamment :

```text
X25519
+
AES-CCM
```

Référence :

https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md

---

# 34. Ce qui est visible et ce qui est protégé

| Information | Relais peut-il l'utiliser ? |
|---|---|
| `from` | Oui |
| `to` | Oui |
| `id` | Oui |
| `hop_limit` | Oui |
| `hop_start` | Oui |
| informations de routage | Oui |
| contenu `Data` | Non, si chiffré |
| `Telemetry` | Non, si chiffré |
| température | Non, si chiffré |
| humidité | Non, si chiffré |
| pression | Non, si chiffré |
| PSK | Non |

La frontière conceptuelle est :

```text
métadonnées réseau
       │
       ▼
    en-tête
       │
       ▼
  chiffrement
       │
       ▼
contenu Data
   ├── Telemetry
   └── EnvironmentMetrics
```

---

# 35. Attention : le paquet radio exact

Les octets calculés dans les sections Protobuf ne constituent **pas** un dump radio complet.

Nous avons calculé :

```text
EnvironmentMetrics
→ Telemetry
→ Data
```

Un dump radio exact dépend notamment de :

1. la version du firmware ;
2. `from` ;
3. `to` ;
4. `id` ;
5. channel ;
6. PSK ;
7. type de chiffrement ;
8. flags ;
9. paramètres de routage ;
10. format radio de la version ciblée.

Pour une implémentation réelle, il faut donc fixer une version firmware précise.

---

# 36. Hiérarchie des sources

Pour une étude d’implémentation :

```text
Firmware de la version ciblée
        │
        ▼
comportement effectivement exécuté
        │
        ▼
protobufs
        │
        ▼
structure exacte des messages
        │
        ▼
documentation SDK
        │
        ▼
explications du protocole
```

Lorsqu’une question porte sur le comportement réel d’une version donnée, le firmware ciblé doit être considéré comme la source de vérité.

---

# 37. Méthode d’analyse recommandée

Pour reproduire un paquet réel :

```text
1. Identifier la version firmware
2. Identifier le type de message
3. Identifier le PortNum
4. Construire le message applicatif
5. Sérialiser EnvironmentMetrics
6. Sérialiser Telemetry
7. Sérialiser Data
8. Construire le contexte MeshPacket
9. Déterminer le mécanisme crypto
10. Construire le nonce exact
11. Chiffrer
12. Construire le format radio exact
13. Comparer avec une capture réelle
```

Pour l’analyse inverse :

```text
1. Capture radio
2. Header
3. Identification source/destination
4. Identification du canal
5. Identification du mode crypto
6. Déchiffrement
7. Protobuf Data
8. PortNum
9. Protobuf Telemetry
10. EnvironmentMetrics
11. Valeurs physiques
```

---

# 38. Tableau de synthèse

| Couche | Objet | Rôle |
|---|---|---|
| Capteur | mesure physique | acquisition |
| Application | `EnvironmentMetrics` | représentation des mesures |
| Protobuf | Wire Format | sérialisation |
| Télémétrie | `Telemetry` | encapsulation |
| Application mesh | `Data` | port + payload |
| Sécurité | AES-CTR / autre selon contexte | protection |
| Réseau | `MeshPacket` | adressage/routage |
| Radio | format radio Meshtastic | transport |
| PHY | LoRa | transmission RF |

---

# 39. Références principales

- Meshtastic protobufs : https://github.com/meshtastic/protobufs
- `telemetry.proto` : https://github.com/meshtastic/protobufs/blob/master/meshtastic/telemetry.proto
- `mesh.proto` : https://github.com/meshtastic/protobufs/blob/master/meshtastic/mesh.proto
- `portnums.proto` : https://github.com/meshtastic/protobufs/blob/master/meshtastic/portnums.proto
- `channel.proto` : https://github.com/meshtastic/protobufs/blob/master/meshtastic/channel.proto
- Firmware : https://github.com/meshtastic/firmware
- Protocole Meshtastic : https://github.com/meshtastic/meshtastic-sdk/blob/main/docs/protocol.md
- Documentation : https://meshtastic.org/
- Encodage Protobuf : https://protobuf.dev/programming-guides/encoding/

---

# 40. Résumé

La chaîne de transmission d’une télémétrie environnementale Meshtastic est :

```text
mesure physique
    ↓
EnvironmentMetrics
    ↓
Telemetry
    ↓
Data
    ↓
sérialisation Protobuf
    ↓
chiffrement
    ↓
MeshPacket / transport réseau
    ↓
format radio
    ↓
LoRa
    ↓
relais éventuels
    ↓
réception
    ↓
déchiffrement
    ↓
Data
    ↓
Telemetry
    ↓
EnvironmentMetrics
    ↓
mesure physique reconstruite
```

La distinction fondamentale est :

```text
EnvironmentMetrics = données applicatives
Protobuf           = représentation binaire structurée
Data               = conteneur applicatif Meshtastic
MeshPacket         = structure réseau Meshtastic
AES                = protection cryptographique
LoRa               = transport radio
```

Pour aller jusqu’au niveau **octet par octet d’un paquet radio réel**, il faut ensuite choisir une version firmware Meshtastic précise et dérouler le format radio exact de cette version.
