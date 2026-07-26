# Pousse-seringue Pico 2 W

Firmware Pico SDK pour un pousse-seringue à TMC5130A. Cette première version fonctionnelle
pilote le moteur en SPI, lit les boutons `PUSH`/`PULL`, applique un anti-rebond de 30 ms,
refuse les commandes concurrentes et arrête le moteur si les deux boutons sont pressés.

## État de l’implémentation

- Opérationnel et compilé : driver SPI TMC5130A, rampes, vitesse/position relative,
  dosage avec recul, conversions mm/micro-pas, boutons et machine d’états.
- Service GATT BTstack réel : commandes JSON, notifications d’état et arrêt local à la
  déconnexion. Interface Web Bluetooth avec `pointerdown/up/cancel`.
- Sécurité : conflit de boutons, erreur SPI, surchauffe, timeout manuel, limites logicielles
  et StallGuard filtré. Ces fonctions doivent être étalonnées et validées sur le matériel.
- Calibration StallGuard relative : `sg_calibrate_start`, déplacement manuel sur une zone
  normale (100 échantillons minimum), puis `sg_calibrate_finish`. Le firmware mémorise la
  référence et fixe provisoirement LOAD_HIGH à 70 % et STALL_ERROR à 50 % de cette valeur.
  Ces ratios sont des points de départ, pas une mesure de force ; refaire la calibration
  après toute modification de vitesse, courant, moteur, vis, seringue ou pâte.
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

Le prototype ne doit pas être utilisé sans arrêt d’urgence matériel et validation sur banc.

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
| GND | masse logique commune |

Le moteur et la puissance du TMC doivent disposer d’une alimentation adaptée, d’un
découplage conforme à la fiche technique et d’une masse commune. Ne jamais alimenter le
moteur depuis la broche 3,3 V de la Pico. Les GPIO sont centralisés dans
`include/board_config.h`.

## Compilation et flash

Prérequis : Pico SDK et `PICO_SDK_PATH`.

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
```

Maintenir `BOOTSEL`, brancher la Pico puis copier `build/paste_dispenser.uf2` sur le
volume USB `RPI-RP2`. La sortie JSON de diagnostic est disponible sur USB CDC.

## Tests hôte

Avec GCC/MinGW :

```powershell
gcc -std=c11 -DUNIT_TEST -Iinclude tests/test_main.c src/app_state.c src/motor_control.c src/command_api.c src/safety.c src/stallguard_calibration.c src/ws_crypto.c -lm -o unit_tests.exe
./unit_tests.exe
```

Les tests couvrent notamment le cas de référence 200 pas/tour, 16 micro-pas et vis de
2 mm/tour, soit 1600 micro-pas/mm, ainsi que les transitions et refus concurrents.

## Protocole Web Bluetooth prévu

Service `7e400001-b5a3-f393-e0a9-e50e24dcca9e`, commande JSON en écriture sur
`...0002...`, état JSON notifié sur `...0003...`. La page doit être servie en HTTPS
(ou depuis `localhost`) car Web Bluetooth exige un contexte sécurisé.
