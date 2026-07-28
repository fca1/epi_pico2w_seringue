# Documentation matérielle

Cette page décrit les entrées/sorties utilisées par le firmware du pousse-seringue sur
Raspberry Pi Pico 2 et Pico 2 W. Les numéros `GPxx` désignent les GPIO du RP2350 ; ils ne
doivent pas être confondus avec les numéros des broches physiques du connecteur.

## Affectation des entrées/sorties

| Fonction | GPIO | Broche physique | Direction | État actif / remarques |
|---|---:|---:|---|---|
| Bouton `DOSE` | GP13 | 17 | Entrée | Actif à 0, pull-up interne |
| Bouton `PUSH` | GP14 | 19 | Entrée | Actif à 0, pull-up interne |
| Bouton `PULL` | GP15 | 20 | Entrée | Actif à 0, pull-up interne |
| TMC5130 `SDO` / SPI0 RX (MISO) | GP16 | 21 | Entrée | Données du TMC5130 vers la Pico |
| TMC5130 `CSN` / SPI0 CS | GP17 | 22 | Sortie | Sélection active à 0 |
| TMC5130 `SCK` / SPI0 SCK | GP18 | 24 | Sortie | Horloge SPI, 1 MHz |
| TMC5130 `SDI` / SPI0 TX (MOSI) | GP19 | 25 | Sortie | Données de la Pico vers le TMC5130 |
| TMC5130 `DRV_ENN` | GP20 | 26 | Sortie | Activation du driver active à 0 |

Les entrées `PUSH`, `PULL` et `DOSE` sont filtrées par le firmware avec un anti-rebond de
30 ms. Une activation simultanée de `PUSH` et `PULL` est considérée comme un conflit.

## Câblage des boutons ou contacts

Chaque commande physique se câble avec un contact normalement ouvert entre son GPIO et
une masse `GND` de la Pico :

```text
GP13 ---- contact DOSE ---- GND
GP14 ---- contact PUSH ---- GND
GP15 ---- contact PULL ---- GND
```

Aucune résistance de pull-up externe n'est nécessaire : le firmware active les pull-up
internes. Les GPIO du RP2350 fonctionnent en logique 3,3 V et ne sont pas tolérants au
5 V. Ne jamais appliquer directement 5 V ou une tension moteur sur ces entrées.


## Liaison avec le TMC5130A

La communication utilise le contrôleur `SPI0` à 1 MHz. Relier les masses de la Pico et du
module TMC5130A. Les signaux logiques échangés avec la Pico doivent être en 3,3 V.

Le moteur est raccordé aux sorties de puissance du TMC5130A, pas aux GPIO de la Pico.
L'alimentation moteur doit suivre les caractéristiques du module TMC5130A utilisé. Le
firmware emploie la mesure de courant interne par `RDS(on)` et ne prévoit pas de résistance
shunt de mesure externe.

## LED d'état

La LED intégrée à la carte est utilisée sans broche de connecteur supplémentaire :

- Pico 2 W : LED pilotée par le circuit radio CYW43 ;
- Pico 2 : LED intégrée définie par `PICO_DEFAULT_LED_PIN`.

Elle clignote lentement hors connexion, rapidement lorsqu'une connexion est active et
reste allumée pendant un mouvement.

## Référence dans le code

La définition faisant foi se trouve dans `include/board_config.h`. Toute modification de
câblage doit être reportée à la fois dans ce fichier et dans la présente documentation.
