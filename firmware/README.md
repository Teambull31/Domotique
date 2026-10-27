# Firmware ESP32 (C++ / PlatformIO)

| Nœud | Matériel | Fonction |
|---|---|---|
| `climate` | BME280 | température, humidité, pression |
| `air` | SCD40 + PMS5003 | CO2, particules |
| `power` | PZEM-004T v3 | tension, courant, puissance, énergie (kWh) |
| `door` | contact reed, pile | ouverture porte/fenêtre (deep sleep) |
| `leak` | sonde d'eau, pile | fuite (deep sleep) |
| `presence` | LD2410 + relais | présence (même immobile) + lumière auto locale |
| `shutters` | 2 relais | volet roulant (open/close/stop, interverrouillé) |
| `heating` | BME280 + relais | thermostat local avec hors-gel |

## Cartes prises en charge
Chaque nœud existe pour 3 cartes : `<nœud>` (ESP32-DevKitC), `<nœud>-c3` (ESP32-C3-DevKitM/C), `<nœud>-s3` (ESP32-S3-DevKitC-1).

| Fonction | DevKitC | C3 | S3 |
|---|---|---|---|
| I2C SDA / SCL | 21 / 22 | 6 / 7 | 8 / 9 |
| UART capteur RX / TX | 16 / 17 | 10 / 5 | 18 / 17 |
| Relais 1 (lumière, volet montée) | 26 | 0 | 4 |
| Relais 2 (volet descente) | 27 | 4 | 5 |
| Relais chauffage | 25 | 0 | 4 |
| Contact porte / sonde fuite (réveil) | 33 | 3 | 6 |
| Mesure pile (ADC) | 34 | 1 | 7 |
| Bouton BOOT (reset config) | 0 | 9 | 0 |

Source : `lib/DomoCore/src/DomoPins.h`. Sur C3, le réveil deep sleep n'est possible que sur GPIO0..5 et le
pull-up interne n'est pas garanti en sommeil : ajouter 100 kΩ entre la broche de réveil et 3V3.
Le C3 (monocœur, peu de broches) convient aux capteurs simples (porte, fuite, climat) ; préférer le S3 ou
le DevKitC pour `air`, `presence` et `shutters`.

## Utilisation
1. `pip install platformio`
2. `cd firmware && pio run -e <nœud>[-c3|-s3] -t upload` (1er flash en USB, ensuite OTA possible).
3. Au premier démarrage, se connecter au WiFi `Domo-<nœud>-setup` et renseigner : WiFi, IP du broker MQTT
   (Home Assistant), pièce, mot de passe OTA (8 caractères minimum ; vide = mise à jour WiFi désactivée, reflash en USB). BOOT maintenu 3 s au démarrage = réinitialisation.
4. Dans HA : intégration **MQTT** activée -> les entités apparaissent seules, rangées par pièce.

Les 8 nœuds compilent pour les 3 cartes (24 environnements) ;
ils n'ont pas encore été testés sur du vrai matériel.

Nouveau nœud : `nodes/<nom>/main.cpp` + une section `[<nom>]` et ses 3 `[env:<nom>...]` dans `platformio.ini`, puis `core.addEntity(...)`.
