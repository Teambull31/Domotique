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

## Utilisation
1. `pip install platformio`
2. `cd firmware && pio run -e <nœud> -t upload` (1er flash en USB, ensuite OTA possible).
3. Au premier démarrage, se connecter au WiFi `Domo-<nœud>-setup` et renseigner : WiFi, IP du broker MQTT
   (Home Assistant), pièce, mot de passe OTA (8 caractères minimum ; vide = mise à jour WiFi désactivée, reflash en USB). BOOT maintenu 3 s au démarrage = réinitialisation.
4. Dans HA : intégration **MQTT** activée -> les entités apparaissent seules, rangées par pièce.

Brochages dans l'en-tête de chaque `nodes/<nœud>/main.cpp`. Les 8 nœuds compilent avec PlatformIO ;
ils n'ont pas encore été testés sur du vrai matériel.

Nouveau nœud : `nodes/<nom>/main.cpp` + un `[env:<nom>]` dans `platformio.ini`, puis `core.addEntity(...)`.
