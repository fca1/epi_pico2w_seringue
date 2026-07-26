# Pousse-seringue Pico 2 / Pico 2 W

Firmware Pico SDK pour un pousse-seringue à TMC5130A. Cette première version fonctionnelle
pilote le moteur en SPI, lit les entrées `PUSH`/`PULL`/`DOSE`, applique un anti-rebond de 30 ms,
refuse les commandes concurrentes et arrête le moteur si les deux boutons sont pressés.

Le firmware utilise FreeRTOS en mode SMP sur les deux cœurs RP2350. Il est construit pour
la Pico 2 sans radio ou pour la Pico 2 W avec BLE et Wi-Fi.

La référence imprimable des trois interfaces — USB série, BLE et Wi-Fi — avec une page
par commande se trouve dans [`docs/communications.md`](docs/communications.md).

## Découpage FreeRTOS

| Tâche | Priorité | Responsabilité |
|---|---:|---|
| `motor` | 6 | machine d’états, boutons, TMC5130, StallGuard et sécurités, période 5 ms |
| `communications` | 4 | collecte des commandes BLE/WebSocket sur Pico 2 W |
| `usb-command` | 4 | console ASCII et commandes JSON terminées par un retour à la ligne sur USB CDC |
| `telemetry` | 2 | formatage et émission périodique de l’état vers USB et la radio disponible |
| `status-led` | 1 | animation non bloquante de la LED |
| `wifi` | 3 | scan, connexion et serveur réseau, uniquement sur Pico 2 W |

Le contrôle moteur est l’unique propriétaire des accès de mouvement et reçoit les ordres
par une file FreeRTOS. L’état destiné à la télémétrie est copié séparément afin que le
formatage JSON et les communications ne retardent pas la boucle de sécurité.

## État de l’implémentation

- Opérationnel et compilé : driver SPI TMC5130A, rampes, vitesse/position relative,
  dosage avec recul, conversions mm/micro-pas, boutons et machine d’états.
- Service GATT BTstack réel : commandes JSON, notifications d’état et arrêt local à la
  déconnexion. Interface Web Bluetooth avec `pointerdown/up/cancel`.
- Sécurité : conflit de boutons, erreur SPI, surchauffe, timeout manuel, limites logicielles
  et StallGuard filtré.
- Calibration StallGuard relative : `sg_calibrate_start`, déplacement manuel sur une zone
  normale (100 échantillons minimum), puis `sg_calibrate_finish`. Le firmware mémorise la
  référence et fixe provisoirement LOAD_HIGH à 70 % et STALL_ERROR à 50 % de cette valeur.
  La calibration est à refaire après une modification de vitesse, courant, moteur, vis,
  seringue ou pâte.
- Configuration persistante versionnée avec CRC et deux secteurs flash alternés, séparés
  des secteurs réservés par BTstack.
- Provisionnement Wi-Fi BLE avec scan SSID, liste classée/dédupliquée, saisie du mot de
  passe, reconnexion au démarrage et serveur HTTP local avec interface embarquée.
- Le provisionnement est ouvert cinq minutes au premier démarrage ou en maintenant PULL
  pendant le démarrage. Maintenir PUSH et PULL cinq secondes efface la configuration puis
  redémarre la carte.
- L’interface locale utilise `ws://adresse-ip/ws` pour les commandes et la télémétrie.
  Une fermeture TCP/WebSocket injecte immédiatement `STOP`. `GET /api/status` et
  `POST /api/command` restent disponibles pour le diagnostic et la compatibilité.

## Câblage

| Pico 2 W | TMC5130A / fonction |
|---|---|
| GP18 | SCK |
| GP19 | SDI / MOSI |
| GP16 | SDO / MISO |
| GP17 | CSN |
| GP20 | DRV_ENN (actif bas) |
| GP14 | bouton PUSH vers GND |
| GP15 | bouton PULL vers GND |
| GP13 | entrée DOSE vers GND, pull-up interne |
| GND | masse logique commune |

Le moteur et la puissance du TMC utilisent une alimentation adaptée, un découplage conforme
à la fiche technique et une masse commune. L’alimentation moteur est distincte de la broche
3,3 V de la Pico. Les GPIO sont centralisés dans
`include/board_config.h`.

## LED d’état de la Pico 2 W

La LED interne est pilotée sans temporisation bloquante :

| Affichage | État |
|---|---|
| clignotement lent, basculement toutes les 500 ms | aucune connexion BLE ou Wi-Fi |
| clignotement rapide, basculement toutes les 100 ms | BLE ou Wi-Fi connecté |
| allumée fixe | mouvement moteur en cours, y compris dosage et recul |

L’état mouvement est prioritaire sur l’état de connexion.

### Entrée physique DOSE

`GP13` est configurée en entrée avec la résistance **pull-up interne** de la Pico. Le contact
externe doit donc relier GP13 à GND lorsqu’il est actif :

- niveau haut ou contact ouvert : repos ;
- niveau bas pendant au moins 30 ms : demande de dosage ;
- une seule dose est déclenchée par appui, même si le signal reste bas ;
- la demande n’est acceptée que lorsque la machine est dans l’état `READY` ;
- la course distribuée est `trigger_dose_mm`, 0,80 mm par défaut ;
- le dosage utilise `dosing_speed_mm_s`, puis le recul anti-goutte configuré.

La valeur se règle par BLE ou WebSocket avec :

```json
{"command":"set_trigger_dose","distance_mm":0.80}
```

La valeur est enregistrée en flash. Elle représente la course du piston. Pour exprimer une
quantité volumique, il faut la convertir avec la section interne réelle de la seringue :
`volume_mm3 = distance_mm × π × diamètre_interne_mm² / 4`, avec `1 mm³ = 1 µL`.

## Compilation et flash

Pour un diagnostic radio sans aucune tentative de connexion Wi-Fi, configurer avec
`-DDISPENSER_WIFI_AUTOCONNECT=OFF`. Ce mode conserve les identifiants mémorisés,
ouvre le provisionnement BLE pendant cinq minutes et autorise les scans sans lancer
automatiquement une association au réseau.

Prérequis : Pico SDK et `PICO_SDK_PATH`. Cloner le dépôt avec ses sous-modules :

```powershell
git clone --recurse-submodules https://github.com/fca1/epi_pico2w_seringue.git
```

Pour une Pico 2 sans radio :

```powershell
cmake -S . -B build-pico2 -G Ninja -DPICO_BOARD=pico2
cmake --build build-pico2
```

Pour une Pico 2 W :

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
```

Maintenir `BOOTSEL`, brancher la Pico puis copier `build/paste_dispenser.uf2` sur le
volume USB `RPI-RP2`. La sortie JSON de diagnostic est disponible sur USB CDC.

Sous Windows, le téléchargement peut être automatisé sans maintenir BOOTSEL si le firmware
est déjà actif :

```bat
flash_firmware.bat build\paste_dispenser.uf2
```

Le script détecte le port USB de la Pico, demande le redémarrage en mode BOOTSEL à
1 200 bauds, attend le volume `RP2350`/`RPI-RP2`, puis copie le fichier UF2.

Sur Pico 2 comme sur Pico 2 W, chaque commande ASCII ou objet JSON décrit dans cette
documentation peut être envoyé sur le port COM à 115200 bauds, suivi de `\n`. La carte
répond `OK QUEUED`, `OK` ou `ERR ...`. Sur la Pico 2 sans radio, la télémétrie contient
`"radio_available":false` et seules les primitives BLE, Wi-Fi, HTTP et WebSocket sont
absentes ; les fonctions locales, USB, moteur, boutons, sécurité, stockage et statistiques
restent compilées.

## Console USB série ASCII

La console USB CDC reste disponible sur la Pico 2 W en même temps que BLE et Wi-Fi. Elle
utilise 115200 bauds, 8 bits, sans parité, 1 bit d’arrêt. Les commandes sont insensibles à
la casse et se terminent par CR/LF ou LF.

| Commande | Fonction |
|---|---|
| `HELP` | affiche toutes les commandes et la version logicielle |
| `VERSION` | affiche `PasteDispenser 1.2.0` |
| `STATUS` | retourne immédiatement l’état JSON complet |
| `CONFIG` | affiche tous les paramètres persistants |
| `PUSH`, `PULL`, `STOP` | mouvement manuel et arrêt prioritaire |
| `DOSE 0.8 4 0.1` | fournit 0,8 mm à 4 mm/s puis recule de 0,1 mm |
| `MOVE -2 3` | déplacement relatif signé en mm |
| `UNLOAD 3` | revient au début de course à 3 mm/s ou jusqu’au blocage |
| `ZERO` | définit la position actuelle comme zéro |
| `FAULTRESET` | acquitte un défaut sans redémarrer |
| `RESET` | arrête le moteur et redémarre le microcontrôleur |
| `SGCAL START`, `SGCAL FINISH`, `SGCAL CANCEL` | calibration StallGuard |
| `FLUSH` | remet le compteur d’activations à zéro |
| `SET nom valeur` | valide et sauvegarde un paramètre en flash |

Exemple de session :

```text
VERSION
PasteDispenser 1.2.0
OK
SET dosing_speed_mm_s 4
OK QUEUED
SET trigger_dose_mm 0.8
OK QUEUED
STATUS
{"state":"READY",...}
OK
DOSE 0.8 4 0.1
OK QUEUED
```

Les noms acceptés par `SET` sont ceux de la table « Paramétrage principal », auxquels
s’ajoutent `stallguard_threshold`, `stallguard_warning_level`,
`stallguard_critical_level` et `stallguard_filter_count`. Une valeur invalide n’est pas
sauvegardée. Les paramètres mécaniques (pas moteur, micro-pas, vis et courants) prennent
complètement effet après `RESET`.

## Tests hôte

Avec GCC/MinGW :

```powershell
gcc -std=c11 -DUNIT_TEST -Iinclude tests/test_main.c src/app_state.c src/motor_control.c src/command_api.c src/safety.c src/stallguard_calibration.c src/ws_crypto.c src/tmc_current.c -lm -o unit_tests.exe
./unit_tests.exe
```

Les tests couvrent notamment le cas de référence 200 pas/tour, 16 micro-pas et vis de
2 mm/tour, soit 1600 micro-pas/mm, ainsi que les transitions et refus concurrents.

## Commande et état par Bluetooth Low Energy

Cette section décrit le protocole simplement. Les chaînes et objets JSON sont encodés en
UTF-8. La page Web Bluetooth doit être servie en HTTPS ou depuis `localhost`.

### Service et caractéristiques

Le service principal est `7e400001-b5a3-f393-e0a9-e50e24dcca9e`.

La Pico utilise une adresse BLE statique aléatoire dérivée de son identifiant matériel.
Elle reste donc stable pour une carte donnée, distingue plusieurs pousse-seringues et évite
la réutilisation d’un ancien cache GATT Windows après un changement de firmware.

| Fin UUID | Sens | Contenu |
|---|---|---|
| `0002` | navigateur → Pico | commande machine JSON, écriture |
| `0003` | Pico → navigateur | état machine JSON, lecture et notifications |
| `0004` | navigateur → Pico | SSID Wi-Fi UTF-8 |
| `0005` | navigateur → Pico | mot de passe Wi-Fi UTF-8 |
| `0006` | navigateur → Pico | déclenchement de la connexion Wi-Fi |
| `0007` | Pico → navigateur | état Wi-Fi texte |
| `0008` | Pico → navigateur | adresse IPv4 texte |
| `0009` | navigateur → Pico | déclenchement du scan Wi-Fi |
| `000a` | Pico → navigateur | résultat JSON du scan Wi-Fi |

Chaque UUID complet reprend le préfixe, par exemple la commande est
`7e400002-b5a3-f393-e0a9-e50e24dcca9e`.

### Ordres moteur

Les ordres sont écrits sur la caractéristique `0002`.

| Action | Objet JSON à envoyer | Paramètres |
|---|---|---|
| pousser manuellement | `{"command":"push_start"}` | utilise la vitesse manuelle |
| arrêter la poussée | `{"command":"push_stop"}` | toujours accepté |
| tirer manuellement | `{"command":"pull_start"}` | utilise la vitesse manuelle |
| arrêter la traction | `{"command":"pull_stop"}` | toujours accepté |
| arrêt général | `{"command":"stop"}` | priorité maximale |
| doser | `{"command":"dose","distance_mm":0.8,"speed_mm_s":5,"retract_mm":0.1}` | valeurs positives en mm et mm/s |
| déplacement relatif | `{"command":"move_relative","distance_mm":-2,"speed_mm_s":5}` | distance signée |
| décharger la seringue | `{"command":"unload_syringe","speed_mm_s":5}` | revient au début de course ou s’arrête sur blocage |
| définir la position zéro | `{"command":"set_zero"}` | machine au repos uniquement |
| acquitter un défaut | `{"command":"reset"}` | depuis `FAULT` uniquement |
| régler l’entrée DOSE | `{"command":"set_trigger_dose","distance_mm":0.8}` | plage 0–100 mm, sauvegardée |
| régler un paramètre | `{"command":"set_config","parameter":"dosing_speed_mm_s","value":4}` | même liste que la commande USB `SET` |
| redémarrer | `{"command":"reboot"}` | arrêt du moteur puis redémarrage matériel |
| remettre les statistiques à zéro | `{"command":"flush_statistics"}` | sauvegardé immédiatement |

Une commande de mouvement concurrente est ignorée si la machine n’est pas `READY`. Le
client doit observer la notification suivante pour confirmer le changement d’état. Une
perte de la liaison BLE pendant un mouvement manuel injecte localement `STOP`.

### Calibration StallGuard

| Étape | Commande |
|---|---|
| commencer l’acquisition | `{"command":"sg_calibrate_start"}` |
| annuler | `{"command":"sg_calibrate_cancel"}` |
| calculer et sauvegarder les seuils | `{"command":"sg_calibrate_finish"}` |

Après `sg_calibrate_start`, effectuer un déplacement manuel normal et stable pendant au
moins 100 échantillons, relâcher le bouton, attendre `READY`, puis envoyer
`sg_calibrate_finish`.

### État machine reçu

La caractéristique `0003` est à lire une première fois puis à surveiller par notifications.
Exemple :

```json
{
  "state": "READY",
  "position_mm": 12.450,
  "remaining_course_mm": 107.550,
  "used_course_mm": 12.450,
  "activation_count": 42,
  "unload_result": "none",
  "sg_result": 380,
  "load": 0,
  "sg_calibrating": false,
  "sg_samples": 0,
  "trigger_dose_mm": 0.800,
  "radio_available": true,
  "fault": 0
}
```

`state` peut valoir `BOOT`, `READY`, `MANUAL_PUSH`, `MANUAL_PULL`, `DOSING`,
`RETRACTING`, `HOMING`, `STOPPING` ou `FAULT`. Les états réseau internes
`WIFI_CONNECTING` et `BLE_PROVISIONING` sont également réservés.

`load` signifie : `0` = charge normale, `1` = charge élevée, `2` = avertissement de
blocage, `3` = blocage critique. `sg_result` est une indication relative, jamais une force
en newtons.

`fault` signifie : `0` = aucun défaut, `1` = boutons PUSH et PULL simultanés,
`2` = communication SPI, `3` = surchauffe, `4` = StallGuard critique,
`5` = durée manuelle dépassée, `6` = limite logicielle de position.

`radio_available` vaut `true` pour une compilation Pico 2 W et `false` pour une Pico 2.

`remaining_course_mm` est la distance encore disponible dans le sens de poussée :
`position_max_mm - position_mm`. `used_course_mm` est la distance parcourue depuis
`position_min_mm`.

`activation_count` compte, depuis le dernier `flush_statistics`, chaque poussée manuelle
acceptée, chaque dosage accepté et chaque déplacement relatif positif accepté. Les
tractions et le recul anti-goutte ne sont pas comptés.

`unload_result` vaut `none` avant la première demande, `running` pendant le déchargement,
`position_min` lorsque le début de course est atteint, `stall` lorsque StallGuard a arrêté
le mouvement sur un blocage, ou `stopped` après un ordre d’arrêt. Un blocage détecté lors
du déchargement est considéré comme la butée mécanique de début de course : le mouvement
est arrêté sans passer en `FAULT` et la position est recalée sur `position_min_mm`.

### Paramétrage principal

| Paramètre | Défaut | Rôle |
|---|---:|---|
| `motor_steps_per_rev` | 200 | pas complets par tour moteur |
| `microsteps` | 16 | micro-pas par pas complet |
| `screw_pitch_mm` | 2,0 | avance de la vis par tour |
| `motor_run_current_mA` | 800 mA | courant RMS pendant le mouvement |
| `motor_hold_current_mA` | 300 mA | courant RMS de maintien |
| `manual_speed_mm_s` | 5,0 | vitesse PUSH/PULL |
| `dosing_speed_mm_s` | 5,0 | vitesse du dosage physique |
| `trigger_dose_mm` | 0,80 | course commandée par GP13 |
| `acceleration_mm_s2` | 100 | accélération et décélération |
| `retract_distance_mm` | 0,10 | recul après dosage |
| `retract_speed_mm_s` | 3,0 | vitesse de recul |
| `retract_delay_ms` | 50 | attente avant recul |
| `position_min_mm` / `position_max_mm` | 0 / 120 | limites logicielles |
| `manual_timeout_ms` | 30000 | durée manuelle maximale |
| `stallguard_enabled` | faux | activé après calibration réussie |

La résistance de mesure du courant du TMC5130A est fixée à `0,1 Ω` dans
`TMC_SENSE_RESISTOR_OHM`. Les courants sont saisis en mA. Le firmware calcule les valeurs
`IRUN` et `IHOLD` avec une tension pleine échelle de 325 mV. Avec 800 mA demandés, le pas
de réglage le plus proche donne environ 778 mA RMS.

Les distances sont exprimées en mm, les vitesses linéaires en mm/s, les accélérations en
mm/s² et les courants en mA. La vitesse moteur équivalente en tours par seconde est :
`vitesse_tr_s = vitesse_mm_s / screw_pitch_mm`. Avec 5 mm/s et une vis de 2 mm/tour,
la vitesse vaut 2,5 tr/s. Les unités internes des registres TMC restent confinées au driver.

Tous les paramètres de `device_config_t` sont stockés ensemble dans deux secteurs flash
alternés avec numéro de séquence, version, longueur implicite de structure et CRC. Une
écriture d’un paramètre conserve les autres champs. Le SSID et le mot de passe ne sont
enregistrés qu’après une connexion Wi-Fi réussie ; ils sont ensuite relus au redémarrage et
réutilisés automatiquement. Une tentative avec des identifiants incorrects ne remplace pas
la dernière configuration fonctionnelle.

Le compteur d’activations utilise deux autres secteurs sous forme de journal de pages. Il
reste disponible après une coupure d’alimentation sans provoquer l’effacement d’un secteur
à chaque incrément. L’enregistrement est réalisé au retour dans l’état `READY`.

Maintenir PUSH et PULL pendant cinq secondes efface la configuration et les statistiques.

### Provisionnement Wi-Fi par BLE

1. Écrire une valeur quelconque sur `0009` pour démarrer le scan.
2. Lire `000a` jusqu’à obtenir `"scanning":false`.
3. Choisir un élément de `networks`, puis écrire son `ssid` sur `0004`.
4. Écrire le mot de passe sur `0005`.
5. Écrire une valeur quelconque sur `0006`.
6. Lire `0007` : `CONNECTING`, `CONNECTED`, `AUTH_FAILED` ou `TIMEOUT`.
7. En cas de succès, lire l’adresse sur `0008` et ouvrir `http://adresse-ip`.

Le mot de passe sauvegardé n’est jamais exposé par une caractéristique en lecture.

### Exemple Python BLE côté hôte

Le programme `example/ble_dispenser_example.py` recherche le service, se connecte au
pousse-seringue, active les notifications d’état, transmet les valeurs d’exemple de
`example/ble_dispenser_example.py`, puis demande une fourniture contrôlée de 0,8 mm :

```powershell
python -m pip install -r example/requirements.txt
python example/ble_dispenser_example.py
```

Une autre course peut être demandée avec `--distance`, par exemple
`python example/ble_dispenser_example.py --distance 1.2`. Le programme ne masque pas un
défaut matériel : il affiche la télémétrie reçue et signale si l’action reste refusée parce
que `state` vaut `FAULT`.

Le second exemple `example/ble_wifi_dispenser_example.py` illustre le changement de
transport complet : il configure par BLE le SSID `EPI` et son mot de passe d’exemple,
attend l’adresse IPv4, ferme volontairement BLE, puis envoie les paramètres et l’ordre de
dosage par `POST /api/command` sur le Wi-Fi :

```powershell
python example/ble_wifi_dispenser_example.py
```

Les options `--ssid`, `--password` et `--distance` permettent de remplacer les valeurs de
démonstration. Le mot de passe n’est jamais écrit dans les traces du programme. La fenêtre
de provisionnement doit être ouverte : premier démarrage, compilation avec auto-connexion
désactivée, ou maintien de PULL pendant le démarrage.

## Référence des primitives de communication

Chaque primitive ci-dessous correspond à une opération complète disponible pour un client.

### Primitive `BLE_CONNECT`

Le client recherche un nom commençant par `PasteDispenser-` et le service
`7e400001-b5a3-f393-e0a9-e50e24dcca9e`, ouvre GATT puis active les notifications de la
caractéristique `0003`.

### Primitive `BLE_MACHINE_COMMAND`

Écriture UTF-8 d’un objet JSON sur `0002`. Les valeurs de `command` disponibles sont
`push_start`, `push_stop`, `pull_start`, `pull_stop`, `stop`, `dose`, `move_relative`,
`set_zero`, `reset`, `set_trigger_dose`, `flush_statistics`, `sg_calibrate_start`,
`sg_calibrate_finish`, `sg_calibrate_cancel`, `set_config` et `reboot`. Les paramètres
numériques utilisent `distance_mm`, `speed_mm_s`, `retract_mm` et `value`; `set_config`
utilise aussi la chaîne `parameter`.

### Primitive `BLE_MACHINE_STATUS`

Lecture ou notification sur `0003`. La réponse JSON contient `state`, `position_mm`,
`remaining_course_mm`, `used_course_mm`, `activation_count`, `unload_result`, `sg_result`, `load`,
`sg_calibrating`, `sg_samples`, `trigger_dose_mm` et `fault`.

### Primitive `FLUSH_STATISTICS`

Envoi de `{"command":"flush_statistics"}` par BLE, WebSocket ou HTTP. Le compteur
`activation_count` passe à zéro et la nouvelle valeur est écrite dans le journal flash.

### Primitive `UNLOAD_SYRINGE`

Envoi de `{"command":"unload_syringe"}` par BLE, WebSocket ou HTTP. Le paramètre
facultatif `speed_mm_s` choisit la vitesse de retour ; sans ce paramètre,
`manual_speed_mm_s` est utilisé. La commande n’est acceptée que dans l’état `READY`.
La machine passe dans `HOMING` et recule jusqu’à `position_min_mm`. Si StallGuard est
activé et calibré, un blocage critique arrête aussi le mouvement et recale cette position
comme début de course. Surveiller `state` et `unload_result` pour connaître la fin de
l’opération. La primitive `STOP` reste prioritaire.

### Primitive `BLE_WIFI_SCAN_START`

Écriture d’un octet quelconque sur `0009`. Le scan, qui ne modifie aucun identifiant, reste
disponible même après la fermeture de la fenêtre de provisionnement et s’exécute de façon
asynchrone.

### Primitive `BLE_WIFI_SCAN_RESULTS`

Lecture de `000a`. La forme retournée est
`{"scanning":false,"networks":[{"ssid":"Atelier","rssi":-48,"secure":true}]}`.
Tant que `scanning` vaut `true`, le client relit la caractéristique. Les réseaux finaux sont
dédupliqués et classés du RSSI le plus fort au plus faible.

### Primitive `BLE_WIFI_SET_SSID`

Écriture du SSID UTF-8, 32 octets maximum, sur `0004`.

### Primitive `BLE_WIFI_SET_PASSWORD`

Écriture du mot de passe UTF-8, 64 octets maximum, sur `0005`.

### Primitive `BLE_WIFI_CONNECT`

Écriture d’un octet quelconque sur `0006`. La Pico tente la connexion avec le SSID et le
mot de passe précédemment transmis.

### Primitive `BLE_WIFI_STATUS`

Lecture de `0007`. Les valeurs sont `IDLE`, `CONNECTING`, `CONNECTED`, `AUTH_FAILED`,
`NETWORK_NOT_FOUND` et `TIMEOUT`.

### Primitive `BLE_WIFI_IP_ADDRESS`

Lecture de `0008`. Après connexion, la valeur est l’adresse IPv4 sous forme de texte.

### Primitive `BLE_DISCONNECT`

La fermeture de la liaison GATT efface l’état connecté. Si un mouvement manuel est actif,
le firmware place une commande `STOP` dans la file locale.

### Primitive `WEBSOCKET_OPEN`

Connexion à `ws://adresse-ip/ws`. Une seule session de commande est conservée. Une nouvelle
session remplace la précédente.

### Primitive `WEBSOCKET_COMMAND`

Envoi d’une trame texte contenant le même JSON que `BLE_MACHINE_COMMAND`. Les trames client
masquées et leur fragmentation au niveau TCP sont prises en charge.

### Primitive `WEBSOCKET_STATUS`

Le serveur envoie périodiquement une trame texte contenant le même JSON que
`BLE_MACHINE_STATUS`.

### Primitive `WEBSOCKET_CLOSE`

Une fermeture WebSocket ou TCP injecte `STOP` dans la file de commandes locale.

### Primitive `HTTP_GET_STATUS`

`GET /api/status` retourne le dernier état machine avec le type `application/json`.

### Primitive `HTTP_POST_COMMAND`

`POST /api/command` reçoit dans son corps le même JSON que `BLE_MACHINE_COMMAND` et retourne
`{"ok":true}` lorsque la commande est placée dans la file.

### Primitive `HTTP_GET_INTERFACE`

`GET /` retourne l’interface locale embarquée. Cette page ouvre automatiquement le
WebSocket `/ws` pour commander la seringue et afficher la télémétrie.
