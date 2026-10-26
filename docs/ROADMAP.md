# Feuille de route — appartement connecté (ESP32 / C++)

Chaque ligne = un nœud dans `firmware/nodes/<nom>/` (un env PlatformIO). Cocher au fur et à mesure.

## Phase 1 — Socle & mesure
- [x] Socle `DomoCore` (WiFi, MQTT, OTA, découverte Home Assistant)
- [x] `climate` : BME280 temp/humidité/pression (une carte par pièce)
- [ ] `air` : CO₂ (SCD40) + particules (PMS5003) — salon, chambre, bureau
- [ ] `power` : suivi conso (PZEM-004T ou pince SCT-013) au tableau électrique

## Phase 2 — Sécurité
- [ ] `door` : contacts d'ouverture portes/fenêtres (reed, deep-sleep sur pile)
- [ ] `leak` : sondes de fuite (cuisine, SdB, lave-linge) + alerte HA
- [ ] `smoke` : relais d'alerte des détecteurs de fumée (lecture seule, sans toucher à leur sécurité)
- [ ] `cam` : ESP32-CAM sonnette / entrée

## Phase 3 — Confort & automatisation
- [ ] `presence` : radar mmWave LD2410 → lumières par pièce
- [ ] `lights` : rubans LED (cuisine, TV) / relais
- [ ] `shutters` : volets roulants (fin de course + courant moteur)
- [ ] `heating` : fil pilote radiateurs / vannes
- [ ] `ir` : télécommande IR (TV, clim)
- [ ] `plants` : humidité du sol + pompe

## Phase 4 — Énergie
- [ ] Délestage / heures creuses : lave-linge, chauffe-eau (relais certifiés)
- [ ] Tableau de bord conso par pièce (HA Energy)

## Règles
- **230 V** : modules certifiés/isolés uniquement, tableau coupé, ou électricien pour le tableau.
- Un nœud = un `unique_id` stable ; ne jamais changer `DEVICE_ID` après coup.
- Secrets dans `firmware/secrets.h` (jamais commité).
