# Communications et commandes

Ce document décrit les trois moyens de communication du pousse-seringue : USB série,
Bluetooth Low Energy et Wi-Fi. Toutes les distances sont en mm, les vitesses en mm/s,
les accélérations en mm/s² et les courants en mA.

## Vue d’ensemble

| Liaison | Disponibilité | Format | Usage principal |
|---|---|---|---|
| USB série | Pico 2 et Pico 2 W | commandes ASCII ou JSON, une ligne par ordre | mise au point, configuration locale et maintenance |
| BLE | Pico 2 W | JSON UTF-8 dans des caractéristiques GATT | application mobile, navigateur ou outil Python proche de la machine |
| Wi-Fi | Pico 2 W après provisionnement BLE | JSON par WebSocket ou HTTP | supervision et commande sur le réseau local |

Les trois interfaces alimentent la même file FreeRTOS. Les mêmes règles de sécurité
s’appliquent : un mouvement n’est accepté que dans `READY`, `STOP` est prioritaire et un
défaut doit être acquitté avant un nouveau mouvement. La télémétrie JSON a la même forme
sur les trois liaisons.

## 1. USB série

Port USB CDC à 115200 bauds, 8 bits, sans parité, 1 bit d’arrêt. Terminer chaque commande
par LF (`\n`) ou CR/LF. Les commandes ne sont pas sensibles à la casse. Une commande mise
en file répond `OK QUEUED`; une lecture locale répond par son contenu puis `OK`; une erreur
de syntaxe répond `ERR SYNTAX_OR_QUEUE`.

### `HELP`

Envoi : `HELP`

Retour : version logicielle, liste complète des commandes, paramètres acceptés par `SET`,
puis `OK`.

<div style="break-after: page; page-break-after: always;"></div>

### `VERSION`

Envoi : `VERSION`

Retour : la version courante, par exemple `PasteDispenser 1.4.1`, puis `OK`.

<div style="break-after: page; page-break-after: always;"></div>

### `STATUS`

Envoi : `STATUS`

Retour : état JSON immédiat, puis `OK`. Les champs principaux sont `state`, `position_mm`,
`remaining_course_mm`, `activation_count`, `load` et `fault`.

<div style="break-after: page; page-break-after: always;"></div>

### `CONFIG`

Envoi : `CONFIG`

Retour : une ligne contenant tous les paramètres persistants, puis `OK`. Le mot de passe
Wi-Fi n’est jamais affiché.

<div style="break-after: page; page-break-after: always;"></div>

### `SET`

Envoi : `SET <paramètre> <valeur>`

Exemple : `SET dosing_speed_mm_s 4`

La valeur est validée puis sauvegardée en flash. Paramètres : `screw_pitch_mm`,
`motor_steps_per_rev`, `microsteps`, `motor_run_current_mA`, `motor_hold_current_mA`,
`manual_speed_mm_s`, `dosing_speed_mm_s`, `trigger_dose_mm`, `a1_mm_s2`,
`amax_mm_s2`, `dmax_mm_s2`, `d1_mm_s2`,
`retract_distance_mm`, `retract_speed_mm_s`, `retract_delay_ms`, `position_min_mm`,
`position_max_mm`, `manual_timeout_ms`, `stallguard_threshold`,
`stallguard_warning_level`, `stallguard_critical_level`, `stallguard_filter_count` et
`stallguard_enabled`. Les paramètres mécaniques prennent complètement effet après `RESET`.

`motor_run_current_mA` et `motor_hold_current_mA` sont compris entre 200 et 1200 mA RMS.
Le TMC5130A fonctionne en mesure interne `RDS(on)` (`internal_Rsense=1`), sans shunt
`Rsense` externe. La carte doit néanmoins comporter `RREF=7,5 kΩ` entre `5VOUT` et
`AIN/IREF`, avec `BRA` et `BRB` reliées directement à la masse.

Les quatre accélérations constituent le profil trapézoïdal avancé du TMC5130 : `A1` puis
`AMAX` pendant l’accélération, `DMAX` puis `D1` pendant la décélération. Elles sont données
en `mm/s²`, sauvegardées en flash et appliquées après redémarrage. Exemple série :

```text
SET a1_mm_s2 80
SET amax_mm_s2 200
SET dmax_mm_s2 180
SET d1_mm_s2 60
RESET
```

En BLE, HTTP API ou WebSocket, utiliser successivement :

```json
{"command":"set_config","parameter":"a1_mm_s2","value":80}
{"command":"set_config","parameter":"amax_mm_s2","value":200}
{"command":"set_config","parameter":"dmax_mm_s2","value":180}
{"command":"set_config","parameter":"d1_mm_s2","value":60}
{"command":"reboot"}
```

Ces réglages sont volontairement absents de l’interface graphique HTTP : ils restent
accessibles par l’API HTTP avancée, le WebSocket, le BLE et l’USB série.

<div style="break-after: page; page-break-after: always;"></div>

### `PUSH`

Envoi : `PUSH`

Démarre une poussée manuelle à `manual_speed_mm_s`. La maintenir jusqu’à l’envoi de `STOP`.

<div style="break-after: page; page-break-after: always;"></div>

### `PULL`

Envoi : `PULL`

Démarre une traction manuelle à `manual_speed_mm_s`. La maintenir jusqu’à `STOP`.

<div style="break-after: page; page-break-after: always;"></div>

### `STOP`

Envoi : `STOP`

Arrêt prioritaire. La file de commandes en attente est vidée avant l’insertion de l’arrêt.

<div style="break-after: page; page-break-after: always;"></div>

### `DOSE`

Envoi : `DOSE <distance_mm> [vitesse_mm_s] [recul_mm]`

Exemple : `DOSE 0.8 4 0.1`. La vitesse omise utilise `dosing_speed_mm_s`. Le recul omis
utilise `retract_distance_mm`.

<div style="break-after: page; page-break-after: always;"></div>

### `MOVE`

Envoi : `MOVE <distance_mm_signée> [vitesse_mm_s]`

Exemple : `MOVE -2 3`. Une valeur positive pousse; une valeur négative tire.

<div style="break-after: page; page-break-after: always;"></div>

### `UNLOAD`

Envoi : `UNLOAD [vitesse_mm_s]`

Revient vers `position_min_mm`. Avec StallGuard calibré, un blocage est traité comme la
butée de début de course et permet le démontage de la seringue.

<div style="break-after: page; page-break-after: always;"></div>

### `ZERO`

Envoi : `ZERO`

Définit la position courante du moteur comme zéro. À utiliser machine immobile.

<div style="break-after: page; page-break-after: always;"></div>

### `FAULTRESET`

Envoi : `FAULTRESET`

Acquitte l’état `FAULT` sans redémarrer. Si la cause matérielle subsiste, le défaut sera
détecté de nouveau.

<div style="break-after: page; page-break-after: always;"></div>

### `RESET`

Envoi : `RESET`

Arrête le moteur puis redémarre le RP2350 par watchdog. Les paramètres persistants sont
rechargés au démarrage.

<div style="break-after: page; page-break-after: always;"></div>

### `BOOTSEL`

Envoi USB uniquement : `BOOTSEL`

Arrête le moteur et redémarre directement en mode de téléchargement USB. Le script
`flash_firmware.bat` utilise cette commande : aucun bouton de la carte n'est nécessaire.

<div style="break-after: page; page-break-after: always;"></div>

### `SGCAL START`

Envoi : `SGCAL START`

Démarre l’acquisition StallGuard. Effectuer ensuite un mouvement manuel stable sur une
zone mécanique normale pendant au moins 100 échantillons.

<div style="break-after: page; page-break-after: always;"></div>

### `SGCAL FINISH`

Envoi : `SGCAL FINISH`

Calcule et sauvegarde la référence, le seuil d’avertissement et le seuil critique.

<div style="break-after: page; page-break-after: always;"></div>

### `SGCAL CANCEL`

Envoi : `SGCAL CANCEL`

Annule la calibration courante et restaure la configuration StallGuard précédente.

<div style="break-after: page; page-break-after: always;"></div>

### `FLUSH`

Envoi : `FLUSH`

Remet `activation_count` à zéro et sauvegarde immédiatement la statistique.

<div style="break-after: page; page-break-after: always;"></div>

## 2. Bluetooth Low Energy

### UART BLE — Nordic UART Service

Le service NUS standard utilise les UUID suivants :

- service : `6e400001-b5a3-f393-e0a9-e50e24dcca9e` ;
- RX, écriture client vers seringue : `6e400002-b5a3-f393-e0a9-e50e24dcca9e` ;
- TX, notification seringue vers client : `6e400003-b5a3-f393-e0a9-e50e24dcca9e`.

Commande de provisionnement :

```text
WIFI:mon_reseau;PASSWORD:mon_mot_de_passe
```

Réponses possibles sur TX : `OK WIFI CONNECTING`, `ERR FORMAT`, `ERR TOO_LONG`,
`ERR WIFI_REQUEST` ou `ERR PROVISIONING_CLOSED`. Activer les notifications TX avant
l’écriture RX. Une commande tenant dans une seule écriture ne nécessite pas de terminaison ;
si l’application la fragmente, ajouter `\n` à la fin. La limite est de 111 octets, avec un
SSID de 1 à 32 octets et un mot de passe de 0 à 64 octets.

La fenêtre de provisionnement doit être ouverte au premier démarrage ou en maintenant PULL
pendant le démarrage. Les identifiants sont persistés uniquement après connexion réussie.

Les ordres ASCII de mouvement acceptés sur RX sont les mêmes que sur l’USB série : `PUSH`, `PULL`,
`STOP`, `DOSE`, `MOVE`, `UNLOAD`, `ZERO`, `FAULTRESET`, `RESET`, `FLUSH`, `SET` et `SGCAL`.
`BOOTSEL` est volontairement réservé à l'USB local.
La réponse est transmise sur TX (`OK`, `ERR COMMAND` ou `ERR BUSY`).

<div style="break-after: page; page-break-after: always;"></div>

## 3. Wi-Fi

Le Wi-Fi doit d’abord être configuré par BLE. Après connexion, la Pico expose :

- `ws://<adresse-ip>/ws` pour les commandes et la télémétrie temps réel ;
- `GET http://<adresse-ip>/api/status` pour lire l’état ;
- `POST http://<adresse-ip>/api/command` pour envoyer une commande JSON.

La carte annonce aussi le nom mDNS `dispenser.local` et le service DNS-SD
`PasteDispenser._http._tcp` sur le port 80. Il est donc possible d’utiliser
`http://dispenser.local/`, `ws://dispenser.local/ws` et les mêmes chemins d’API sur les
hôtes prenant en charge mDNS.

Pour WebSocket, envoyer directement l’objet JSON sous forme de trame texte. Pour HTTP,
placer le même objet dans le corps de la requête POST avec `Content-Type: application/json`.

L’exemple `example/nus_serial_example.py` montre l’échange de commandes ASCII par NUS.

### État Wi-Fi : `GET /api/status`

Exemple : `curl http://192.168.1.42/api/status`. Retour : télémétrie JSON courante.

<div style="break-after: page; page-break-after: always;"></div>

### `push_start`

WebSocket ou corps POST : `{"command":"push_start"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `push_stop`

WebSocket ou corps POST : `{"command":"push_stop"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `pull_start`

WebSocket ou corps POST : `{"command":"pull_start"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `pull_stop`

WebSocket ou corps POST : `{"command":"pull_stop"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `stop`

WebSocket ou corps POST : `{"command":"stop"}`. La fermeture de la session WebSocket
injecte aussi un arrêt local.

<div style="break-after: page; page-break-after: always;"></div>

### `dose`

WebSocket ou corps POST :
`{"command":"dose","distance_mm":0.8,"speed_mm_s":4,"retract_mm":0.1}`.

<div style="break-after: page; page-break-after: always;"></div>

### `move_relative`

WebSocket ou corps POST :
`{"command":"move_relative","distance_mm":-2,"speed_mm_s":3}`.

<div style="break-after: page; page-break-after: always;"></div>

### `unload_syringe`

WebSocket ou corps POST : `{"command":"unload_syringe","speed_mm_s":3}`.

<div style="break-after: page; page-break-after: always;"></div>

### `set_zero`

WebSocket ou corps POST : `{"command":"set_zero"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `reset`

WebSocket ou corps POST : `{"command":"reset"}`. Acquitte le défaut courant.

<div style="break-after: page; page-break-after: always;"></div>

### `reboot`

WebSocket ou corps POST : `{"command":"reboot"}`. La liaison est interrompue pendant le
redémarrage.

<div style="break-after: page; page-break-after: always;"></div>

### `set_trigger_dose`

WebSocket ou corps POST : `{"command":"set_trigger_dose","distance_mm":0.8}`.

<div style="break-after: page; page-break-after: always;"></div>

### `set_config`

WebSocket ou corps POST :
`{"command":"set_config","parameter":"dosing_speed_mm_s","value":4}`.

<div style="break-after: page; page-break-after: always;"></div>

### `flush_statistics`

WebSocket ou corps POST : `{"command":"flush_statistics"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `sg_calibrate_start`

WebSocket ou corps POST : `{"command":"sg_calibrate_start"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `sg_calibrate_finish`

WebSocket ou corps POST : `{"command":"sg_calibrate_finish"}`.

<div style="break-after: page; page-break-after: always;"></div>

### `sg_calibrate_cancel`

WebSocket ou corps POST : `{"command":"sg_calibrate_cancel"}`.

<div style="break-after: page; page-break-after: always;"></div>

## Codes d’état communs

`state` : `BOOT`, `READY`, `MANUAL_PUSH`, `MANUAL_PULL`, `DOSING`, `RETRACTING`,
`HOMING`, `STOPPING` ou `FAULT`.

`fault` : `0` aucun, `1` conflit PUSH/PULL, `2` communication SPI TMC5130,
`3` surchauffe, `4` StallGuard critique, `5` timeout manuel, `6` limite logicielle.

`load` : `0` normal, `1` élevé, `2` avertissement, `3` blocage critique.

Les réponses HTTP à `POST /api/command` sont `{"ok":true}` si la commande a été comprise
et mise en file, sinon `{"ok":false}`. Une mise en file confirme la réception, pas la fin
du mouvement : la fin doit toujours être confirmée par la télémétrie.
