# Communications et commandes

Le pousse-seringue possède uniquement deux interfaces : USB série et Bluetooth Low Energy
Nordic UART Service (NUS). Toutes les distances sont en mm, les vitesses en mm/s, les
accélérations en mm/s² et les courants en mA.

Les deux interfaces utilisent les mêmes commandes ASCII et la même file FreeRTOS. Terminer
chaque commande par LF (`\n`) ou CR/LF. Une commande mise en file répond `OK QUEUED` ; une
lecture répond par son contenu puis `OK`.

## 1. USB série

Port USB CDC à 115200 bauds, 8 bits, sans parité et 1 bit d'arrêt. Les commandes ne sont pas
sensibles à la casse. Une erreur de syntaxe répond `ERR SYNTAX_OR_QUEUE`.

### `HELP`

Envoi : `HELP`

Retour : version et liste des commandes, puis `OK`.

<div style="break-after: page; page-break-after: always;"></div>

### `VERSION`

Envoi : `VERSION`

Retour : par exemple `PasteDispenser 1.6.0`, puis `OK`.

<div style="break-after: page; page-break-after: always;"></div>

### `STATUS`

Envoi : `STATUS`

Retour : télémétrie JSON puis `OK`. Principaux champs : `state`, `position_mm`,
`remaining_course_mm`, `used_course_mm`, `activation_count`, `sg_result`, `load`,
`trigger_dose_mm` et `fault`.

<div style="break-after: page; page-break-after: always;"></div>

### `CONFIG`

Envoi : `CONFIG`

Retour : tous les paramètres persistants puis `OK`.

<div style="break-after: page; page-break-after: always;"></div>

### `SET`

Envoi : `SET <paramètre> <valeur>`

Exemple : `SET dosing_speed_mm_s 4`

Paramètres : `screw_pitch_mm`, `motor_steps_per_rev`, `microsteps`,
`motor_run_current_mA`, `motor_hold_current_mA`, `manual_speed_mm_s`,
`dosing_speed_mm_s`, `trigger_dose_mm`, `a1_mm_s2`, `amax_mm_s2`, `dmax_mm_s2`,
`d1_mm_s2`, `retract_distance_mm`, `retract_speed_mm_s`, `retract_delay_ms`,
`position_min_mm`, `position_max_mm`, `manual_timeout_ms`, `stallguard_threshold`,
`stallguard_warning_level`, `stallguard_critical_level`, `stallguard_filter_count` et
`stallguard_enabled`.

Les courants sont compris entre 200 et 1200 mA RMS. Le TMC5130A utilise la mesure interne
`RDS(on)` avec `RREF=7,5 kΩ`. Les accélérations suivent `A1`, `AMAX`, `DMAX`, puis `D1`.
Les paramètres du pilote moteur prennent complètement effet après `RESET`.

<div style="break-after: page; page-break-after: always;"></div>

### `PUSH`

Envoi : `PUSH`

Démarre une poussée à `manual_speed_mm_s`. Arrêter avec `STOP`.

<div style="break-after: page; page-break-after: always;"></div>

### `PULL`

Envoi : `PULL`

Démarre une traction à `manual_speed_mm_s`. Arrêter avec `STOP`.

<div style="break-after: page; page-break-after: always;"></div>

### `STOP`

Envoi : `STOP`

Arrête le mouvement et vide les commandes en attente.

<div style="break-after: page; page-break-after: always;"></div>

### `DOSE`

Envoi : `DOSE [distance_mm] [vitesse_mm_s] [recul_mm]`

Sans argument, utilise `trigger_dose_mm`, `dosing_speed_mm_s` et `retract_distance_mm`,
exactement comme le contact GP13. Une dose supérieure à la course restante est refusée.

<div style="break-after: page; page-break-after: always;"></div>

### `MOVE`

Envoi : `MOVE <distance_mm_signée> [vitesse_mm_s]`

Une valeur positive pousse ; une valeur négative tire.

<div style="break-after: page; page-break-after: always;"></div>

### `UNLOAD`

Envoi : `UNLOAD [vitesse_mm_s]`

Revient vers `position_min_mm`. Avec StallGuard calibré, un blocage est traité comme la
butée de début de course.

<div style="break-after: page; page-break-after: always;"></div>

### `ZERO`

Envoi : `ZERO`

Définit la position courante comme zéro.

<div style="break-after: page; page-break-after: always;"></div>

### `FAULTRESET`

Envoi : `FAULTRESET`

Acquitte un défaut. Si la cause subsiste, il sera détecté de nouveau.

<div style="break-after: page; page-break-after: always;"></div>

### `RESET`

Envoi : `RESET`

Arrête le moteur puis redémarre le RP2350.

<div style="break-after: page; page-break-after: always;"></div>

### `BOOTSEL`

Envoi USB uniquement : `BOOTSEL`

Redémarre en mode de téléchargement USB. `flash_firmware.bat` automatise ensuite la copie
du firmware.

<div style="break-after: page; page-break-after: always;"></div>

### `SGCAL START`

Envoi : `SGCAL START`

Démarre l'acquisition StallGuard. Effectuer un mouvement manuel stable pendant au moins
100 échantillons.

<div style="break-after: page; page-break-after: always;"></div>

### `SGCAL FINISH`

Envoi : `SGCAL FINISH`

Calcule et sauvegarde la référence et les seuils StallGuard.

<div style="break-after: page; page-break-after: always;"></div>

### `SGCAL CANCEL`

Envoi : `SGCAL CANCEL`

Annule la calibration en cours.

<div style="break-after: page; page-break-after: always;"></div>

### `FLUSH`

Envoi : `FLUSH`

Remet `activation_count` à zéro et sauvegarde la statistique.

<div style="break-after: page; page-break-after: always;"></div>

## 2. Bluetooth Low Energy — Nordic UART Service

UUID NUS :

- service : `6e400001-b5a3-f393-e0a9-e50e24dcca9e` ;
- RX, client vers pousse-seringue : `6e400002-b5a3-f393-e0a9-e50e24dcca9e` ;
- TX, pousse-seringue vers client : `6e400003-b5a3-f393-e0a9-e50e24dcca9e`.

Activer les notifications TX, puis écrire sur RX une des commandes ASCII décrites au
chapitre USB, terminée par `\n`. `BOOTSEL` reste réservé à l'USB local. Les réponses longues
sont découpées selon le MTU et doivent être concaténées jusqu'à `OK`, `OK QUEUED` ou une
ligne `ERR ...`.

La connexion BLE est rompue après 60 secondes sans écriture RX ni notification TX. Chaque
échange NUS relance cette temporisation. L'exemple `example/nus_serial_example.py` permet de
tester la liaison depuis un PC.

## États et défauts

`state` : `BOOT`, `READY`, `MANUAL_PUSH`, `MANUAL_PULL`, `DOSING`, `RETRACTING`,
`HOMING`, `STOPPING` ou `FAULT`.

`fault` : `0` aucun, `1` conflit PUSH/PULL, `2` communication SPI TMC5130,
`3` surchauffe, `4` StallGuard critique, `5` timeout manuel, `6` limite logicielle.

`load` : `0` normal, `1` élevé, `2` avertissement, `3` blocage critique.
