# Firmware ESP32 (C++ / PlatformIO)

1. `pip install platformio`
2. `cp secrets.h.example secrets.h` et renseigner WiFi + broker MQTT (celui de Home Assistant).
3. Éditer `DEVICE_ID` / `ROOM` dans `nodes/<nœud>/src/main.cpp`.
4. `pio run -e climate -t upload` (1er flash en USB, ensuite OTA possible).

Dans HA, activer l'intégration **MQTT** : les entités apparaissent automatiquement (discovery).
Le format `home/devices/<id>/telemetry` reste lisible par le dashboard du dépôt.

Nouveau nœud : créer `nodes/<nom>/src/main.cpp`, ajouter un `[env:<nom>]` dans `platformio.ini`
(avec `build_src_filter = +<../nodes/<nom>/src/>`), déclarer les entités avec `core.addEntity(...)`.

> Non compilé à ce stade (PlatformIO absent de l'environnement de rédaction) : premier build à valider chez toi.
