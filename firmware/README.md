# Firmware ESP32 (C++ / PlatformIO)

| Nœud | Matériel | Fonction |
|---|---|---|
| `climate` | BME280 | température, humidité, pression |
| `air` | SCD40 + PMS5003 | CO2, particules (PMS en veille entre deux mesures) |
| `power` | PZEM-004T v3 | tension, courant, puissance, énergie (kWh) |
| `door` | contact reed, pile | ouverture porte/fenêtre (deep sleep, ouverture brève non perdue) |
| `call` | bouton + voyant, pile | bouton d'appel : alerte au proche répétée jusqu'à prise en compte, voyant de confirmation |
| `leak` | sonde d'eau, pile | fuite (deep sleep, confirmée sur 2 lectures) |
| `presence` | LD2410 + relais | présence (même immobile) + lumière auto locale ; forçage HA respecté tant que la pièce est occupée ; défaut radar signalé |
| `shutters` | 2 relais | volet roulant interverrouillé, position 0-100 % estimée et mémorisée |
| `gate` | relais contact sec + fin(s) de course | portail motorisé : état et commande par impulsion, jamais d'impulsion pendant un mouvement ([câblage](../docs/PANNEAU.md#4-portail-motorisé-nœud-gate)) |
| `pilote` | 2 optotriacs (+ BME280 facultatif) | radiateurs électriques par fil pilote : Confort / Éco / Hors-gel / Arrêt, ou Auto (régulation par sonde) ; panne = Confort |
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
| Voyant (bouton d'appel) | 26 | 4 | 4 |
| Fin de course fermé / ouvert (portail) | 33 / 32 | 3 / 10 | 6 / 15 |
| Bouton BOOT (configuration) | 0 | 9 | 0 |

Source : `lib/DomoCore/src/DomoPins.h`. Sur C3, le réveil deep sleep n'est possible que sur GPIO0..5 et le
pull-up interne n'est pas garanti en sommeil : ajouter 100 kΩ entre la broche de réveil et 3V3.
Le C3 (monocœur, peu de broches) convient aux capteurs simples (porte, fuite, climat) ; préférer le S3 ou
le DevKitC pour `air`, `presence` et `shutters`.

## Utilisation
1. `pip install platformio`
2. `cd firmware && pio run -e <nœud>[-c3|-s3] -t upload` (1er flash en USB, ensuite OTA possible).
3. Au premier démarrage, se connecter au WiFi `Domo-<nœud>-setup` et renseigner : WiFi, IP du broker MQTT
   (Home Assistant), pièce, emplacement (facultatif ; obligatoire si 2 nœuds du même type dans la même pièce,
   ex. `door` « salon » + « fenetre » -> `door-salon-fenetre`), mot de passe OTA (8 caractères minimum ; vide = mise à jour WiFi désactivée, reflash en USB). Ensuite le portail ne s'ouvre plus jamais tout seul (box en panne ou plus lente à
   redémarrer : le nœud continue ses automatismes et se reconnecte). Pour le rouvrir : alimenter le nœud
   puis, dans les 2 s, maintenir BOOT 3 s (valeurs conservées, mots de passe vides = inchangés) ; 10 s =
   effacement complet (seul moyen de désactiver l'OTA). Ne pas tenir BOOT pendant la mise sous tension
   (mode téléchargement).
4. Dans HA : intégration **MQTT** activée -> les entités apparaissent seules, rangées par pièce.

## Robustesse (socle DomoCore)
- **Nœuds secteur** : chien de garde matériel (boucle bloquée 60 s -> redémarrage), redémarrage après
  15 min sans MQTT, LWT `offline`, diagnostic dans HA (signal WiFi, durée de fonctionnement), version du
  firmware dans la fiche appareil, bouton « Redémarrer », entités redéclarées quand Home Assistant redémarre ;
  connexion MQTT qui ne bloque pas les automatismes (délais courts, essais espacés de 5 à 60 s) ; actionneurs
  mis en sécurité (`onSafeState`, ex. volet arrêté et position mémorisée) avant une OTA ou un redémarrage.
- **Nœuds sur pile** (`setSleepy`) : pas de LWT mais `expire_after` (indisponible après 2 réveils manqués),
  dernier état retenu sur le broker, jamais de portail ni de redémarrage en boucle si le WiFi manque ;
  reconnexion directe au point d'accès mémorisé (canal/BSSID en mémoire RTC), envoi raté retenté après
  1, 2, 4… min (plafonné au battement d'1 h), discovery republiée environ une fois par jour.
- **Capteur en panne** : mesures publiées à `null` (« inconnu » dans HA, jamais une valeur figée) et entité
  de diagnostic « Défaut capteur » / « Défaut radar », reprise par l'alerte « Capteur à vérifier ».
- Réglages sur place depuis HA (Paramètres de l'appareil), sans recompiler : durées de montée/descente des
  volets, délai d'extinction de la lumière, correction de température.
- Les automatismes critiques (hors-gel, lumière auto, arrêt des volets) tournent en local, sans HA ni WiFi.

Les 11 nœuds compilent pour les 3 cartes (33 environnements) ;
ils n'ont pas encore été testés sur du vrai matériel.

## Vérifications (aussi lancées par le CI à chaque push)
- `pio test -e native` : tests sur PC de la logique critique, sans ESP32 (`lib/DomoCore/src/DomoLogic.h` :
  thermostat et hors-gel, fenêtre ouverte, position des volets, lumière automatique et forçage manuel,
  niveau de pile, identifiants, bornage des réglages).
- `pio check -e <nœud> --skip-packages` : analyse statique clang-tidy.
- Compilation : tout avertissement dans le code des nœuds est une erreur (`-Werror`).
- Home Assistant : `homeassistant/check_config.sh` (schéma) et `python homeassistant/test_templates.py`
  (résultat des alertes calculé par le moteur de HA).

Nouveau nœud : `nodes/<nom>/main.cpp` + une section `[<nom>]` et ses 3 `[env:<nom>...]` dans `platformio.ini`, puis `core.addEntity(...)`.
