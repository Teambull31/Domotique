# Feuille de route — appartement connecté (ESP32 / C++)

Chaque ligne = un nœud dans `firmware/nodes/<nom>/` (3 envs PlatformIO : DevKitC, C3, S3). Cocher au fur et à mesure.
Tout est compilé et testé sur PC (logique) ; **rien n'est encore validé sur du vrai matériel**.

## Phase 1 — Socle & mesure
- [x] Socle `DomoCore` (portail de configuration, MQTT, OTA, découverte HA, chien de garde, diagnostic)
- [x] `climate` : BME280 temp/humidité/pression (une carte par pièce)
- [x] `air` : CO₂ (SCD40) + particules (PMS5003) — salon, chambre, bureau
- [x] `power` : suivi conso (PZEM-004T v3) au tableau électrique

## Phase 2 — Sécurité
- [x] `door` : contacts d'ouverture portes/fenêtres (reed, deep-sleep sur pile)
- [x] `leak` : sondes de fuite (cuisine, SdB, lave-linge) + alerte HA
- [ ] `smoke` : relais d'alerte des détecteurs de fumée (lecture seule, sans toucher à leur sécurité)
- [ ] `cam` : ESP32-CAM sonnette / entrée

## Phase 3 — Confort & automatisation
- [x] `presence` : radar mmWave LD2410 + relais de lumière automatique (forçage manuel respecté)
- [x] `shutters` : volets roulants, position estimée au temps de course (durées réglables dans HA)
- [ ] `shutters` v2 : fin de course par mesure du courant moteur (plus précis que le temps)
- [x] `heating` : thermostat à contact sec (entrée thermostat chaudière / vanne), hors-gel, fenêtre ouverte
- [ ] `heating` fil pilote : radiateurs électriques (interface opto-triac 4 ordres, par un électricien)
- [ ] `ir` : télécommande IR (TV, clim)
- [ ] `plants` : humidité du sol + pompe

## Phase « tranquillité » (proches âgés) — `homeassistant/`
- [x] Alertes : inactivité, porte la nuit / restée ouverte, fuite répétée jusqu'à acquittement, cuisson
      oubliée, chaleur/froid, CO2, piles et capteurs en panne
- [x] Tableau de bord du proche aidant, bilan quotidien du soir, script de test des alertes
- [x] Un seul endroit à adapter par site (groupes), templates testés avec le moteur de HA
- [x] `call` : bouton d'appel (ESP32 sur pile, voyant de confirmation) — complément, **pas** une téléassistance certifiée

## Phase « contrôle » — panneau et tablettes
- [x] `gate` : portail motorisé (état + commande par impulsion sécurisée)
- [x] Tableau de bord « Maison » : panneau Raspberry Pi (kiosque) et tablette des proches
- [x] État de la maison en une phrase, consommation du jour / du mois et coût estimé

## Phase 4 — Énergie
- [ ] Délestage / heures creuses : lave-linge, chauffe-eau (relais certifiés)
- [ ] Tableau de bord conso par pièce (HA Energy) — la conso globale est sur le tableau « Maison »

## Règles
- **230 V** : modules certifiés/isolés uniquement, tableau coupé, ou électricien pour le tableau.
- Identifiant d'un nœud = type + pièce (+ emplacement), saisis dans le portail : les changer crée de
  nouvelles entités dans HA (supprimer les anciennes).
- Aucun secret dans le code : WiFi/MQTT saisis au 1er démarrage (portail captif).
- Déploiement chez des tiers : voir `DEPLOIEMENT.md`.
