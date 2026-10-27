# Firmware ESP32 (C++ / PlatformIO)

| Nœud | Matériel | Fonction |
|---|---|---|
| `climate` | BME280 | température, humidité, pression |
| `air` | SCD40 + PMS5003 | CO2, particules (PMS en veille entre deux mesures) |
| `power` | PZEM-004T v3 | tension, courant, puissance, énergie (kWh) |
| `door` | contact reed, pile | ouverture porte/fenêtre (deep sleep, ouverture brève non perdue) |
| `leak` | sonde d'eau, pile | fuite (deep sleep, confirmée sur 2 lectures) |
| `presence` | LD2410 + relais | présence (même immobile) + lumière auto locale ; forçage HA respecté tant que la pièce est occupée ; défaut radar signalé |
| `shutters` | 2 relais | volet roulant interverrouillé, position 0-100 % estimée et mémorisée |
| `heating` | BME280 + relais | entité thermostat HA, hors-gel, fenêtre ouverte, anti-cycles courts (3 min), coupure si capteur muet |

Liste d'achat : [`docs/MATERIEL.md`](../docs/MATERIEL.md).

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

## Robustesse (socle DomoCore)
- **Nœuds secteur** : chien de garde matériel (boucle bloquée 60 s -> redémarrage), redémarrage après
  15 min sans MQTT, LWT `offline`, diagnostic dans HA (signal WiFi, durée de fonctionnement), version du
  firmware dans la fiche appareil, bouton « Redémarrer », entités redéclarées quand Home Assistant redémarre.
- **Nœuds sur pile** (`setSleepy`) : pas de LWT mais `expire_after` (indisponible après 2 réveils manqués),
  dernier état retenu sur le broker, jamais de portail ni de redémarrage en boucle si le WiFi manque.
- Les automatismes critiques (hors-gel, lumière auto, arrêt des volets) tournent en local, sans HA ni WiFi.

Les 8 nœuds compilent pour les 3 cartes (24 environnements) ;
ils n'ont pas encore été testés sur du vrai matériel.

Nouveau nœud : `nodes/<nom>/main.cpp` + une section `[<nom>]` et ses 3 `[env:<nom>...]` dans `platformio.ini`, puis `core.addEntity(...)`.
