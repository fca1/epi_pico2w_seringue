# Pousse-seringue Pico 2 / Pico 2 W

Firmware FreeRTOS/Pico SDK pour un pousse-seringue commandé par TMC5130A. La Pico 2 W
propose deux transports indépendants : USB CDC série et Bluetooth Low Energy NUS.
La référence détaillée des commandes se trouve dans
[`docs/communications.md`](docs/communications.md). L'affectation des GPIO et les
consignes de câblage sont décrites dans [`docs/hardware.md`](docs/hardware.md).

## Fonctions principales

- mouvements manuel, relatif, dosage contrôlé et déchargement de la seringue ;
- limites de course en mm et indication de course restante ;
- accélération trapézoïdale TMC5130A (`A1`, `AMAX`, `DMAX`, `D1`) ;
- StallGuard avec calibration et arrêt sur blocage ;
- configuration et statistiques persistantes en flash ;
- entrée physique DOSE avec pull-up ;
- LED lente hors connexion, rapide connecté, fixe pendant un mouvement.

## BLE : Nordic UART Service uniquement

Le firmware annonce uniquement un profil série compatible Nordic UART Service :

- service NUS : `6e400001-b5a3-f393-e0a9-e50e24dcca9e` ;
- RX, client vers pousse-seringue : `6e400002-b5a3-f393-e0a9-e50e24dcca9e` ;
- TX, pousse-seringue vers client : `6e400003-b5a3-f393-e0a9-e50e24dcca9e`.

Activer les notifications TX, puis écrire une ligne ASCII terminée par `\n` sur RX. Les
commandes machine sont identiques à celles de la console USB. La commande `DOSE` sans
argument déclenche la fourniture avec les paramètres persistants, comme le contact GP13.

Les réponses sont transmises par notification TX, par exemple `OK`, `OK QUEUED`,
`ERR COMMAND` ou `ERR BUSY`.

Exemple Python :

```powershell
python -m pip install -r example/requirements.txt
python example/nus_serial_example.py
# Mouvement explicite seulement après vérification de la mécanique :
python example/nus_serial_example.py "DOSE 0.8 5 0.1"
```

## USB série

La console USB CDC fonctionne en parallèle sur Pico 2 W. Paramètres usuels : 115200 bauds,
8 bits, sans parité, un bit d'arrêt. `HELP` affiche toutes les commandes et la version.

Exemples :

```text
VERSION
STATUS
CONFIG
SET screw_pitch_mm 2
SET position_max_mm 120
DOSE 0.8 5 0.1
UNLOAD 3
RESET
BOOTSEL
```

## Compilation

```powershell
cmake -S . -B build -DPICO_BOARD=pico2_w
cmake --build build --parallel
```

Le firmware produit est `build/paste_dispenser.uf2`. Le script `flash_firmware.bat`
demande automatiquement le mode BOOTSEL par USB puis copie le firmware. Fermer tout
terminal série avant de lancer la mise à jour.

Tests unitaires sur le poste de développement :

```powershell
python tests/run_host_tests.py
```

## Matériel et unités

Voir [`docs/hardware.md`](docs/hardware.md) pour le tableau complet des entrées/sorties,
leurs broches physiques, leur polarité et le câblage du TMC5130A et des boutons.

Les distances sont exprimées en mm, les vitesses en mm/s, les accélérations en mm/s² et
les courants en mA. Le TMC5130A utilise la mesure de courant interne par `RDS(on)` ; aucun
shunt de mesure externe n'est prévu. Les détails de câblage, paramètres, états, défauts et
calibration sont conservés dans la documentation complète.
