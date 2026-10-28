# Matériel par nœud

Prix : ordres de grandeur indicatifs (AliExpress / Amazon), à vérifier au moment de l'achat.
Carte conseillée selon tes cartes disponibles (DevKitC = ESP32 classique, C3, S3) ; brochages dans
`firmware/README.md`.

| Nœud | Carte conseillée | Composants | Ordre de prix (hors carte) |
|---|---|---|---|
| `climate` | C3 | BME280 (version 3,3 V, I2C) — **pas** BMP280 (sans humidité) | 3–6 € |
| `air` | S3 ou DevKitC | Sensirion SCD40 (vrai CO2 NDIR, pas un "eCO2" CCS811/SGP30) + Plantower PMS5003 + câble 8 broches | 35–60 € |
| `power` | S3 ou DevKitC | PZEM-004T **v3** avec tore (pince fermée) 100 A | 8–15 € |
| `door` | C3 (+ 100 kΩ) | contact reed NO + aimant, accu Li-ion 18650 ou LiPo + support, 2 × 100 kΩ (pont diviseur), boîtier | 5–10 € |
| `call` | C3 (+ 100 kΩ) | gros bouton poussoir contrasté (type « champignon »), LED + 330 Ω, accu + support, 2 × 100 kΩ, boîtier à garder sur soi ou fixer près du lit | 5–10 € |
| `leak` | C3 (+ 100 kΩ) | sonde d'eau à sortie numérique (module comparateur), accu + support, 2 × 100 kΩ | 4–8 € |
| `presence` | S3 ou DevKitC | radar HLK-LD2410 (B ou C), module relais 1 voie 3,3 V ou relais statique | 6–12 € |
| `shutters` | S3 ou DevKitC | module 2 relais 3,3 V à **isolation renforcée** (ou module volet du commerce à contacts secs) | 5–10 € |
| `gate` | DevKitC ou S3 | module relais 1 voie 3,3 V (contact sec), contact magnétique d'extérieur NO (+ un 2e facultatif), boîtier IP65 | 10–20 € |
| `pilote` | DevKitC ou S3 | module « fil pilote » 230 V (2 optotriacs MOC3041 + diodes 1N4007, isolé) ou à monter par un électricien, BME280 facultatif (mode Auto) | 8–15 € |
| `heating` | DevKitC ou S3 | BME280 + relais **contact sec** vers l'entrée thermostat de la chaudière / du fil pilote | 5–10 € |

## Panneau mural (voir PANNEAU.md)
Raspberry Pi 4 ou 5 (2 Go), alimentation officielle, microSD 16 Go, écran tactile 6-7 pouces (DSI de
préférence : luminosité pilotable), support mural ou cadre. Tablette des proches : n'importe quelle tablette
récente avec l'application Home Assistant.

## Commun à tous les nœuds secteur
- Alimentation 5 V USB de qualité (≥ 1 A) ; pour un montage au tableau, module HLK-PM01 **par un électricien**.
- Boîtier aéré pour `climate`/`air`/`heating` : capteur à l'extérieur ou éloigné de l'ESP32 (qui chauffe).

## Nœuds sur pile
- Une carte de développement consomme plusieurs mA en veille (régulateur, LED, puce USB) : l'autonomie
  sera de quelques semaines. Pour des mois, utiliser un module nu (ESP32-C3 SuperMini sans LED, ou
  module + régulateur faible courant de repos type HT7333 / MCP1700).
- C3 : résistance 100 kΩ entre la broche de réveil et 3V3 (le pull-up interne n'est pas garanti en sommeil).

## Sécurité
Tout ce qui touche au 230 V (`power`, `shutters`, alimentation au tableau, chaudière) est raccordé par un
électricien, conformément à la NF C 15-100. Les nœuds ne remplacent ni un détecteur de fumée certifié ni
une téléassistance.
