# Déployer chez un proche ou un client

## Principe : une installation = un site autonome
```
[capteurs ESP32] --WiFi/MQTT--> [Home Assistant du site] --> notifications au proche aidant
```
- **Un Home Assistant par site** (HA Green, ou Raspberry Pi 4/5 + SSD). Pas de cloud obligatoire : ça marche si Internet tombe.
- **Accès à distance pour toi** (support) : Tailscale ou Nabu Casa, avec l'accord de la personne.
- **Tout est configuré sur place, sans recompiler** : à la première mise sous tension, un nœud ouvre un
  WiFi `Domo-<nœud>-setup` (portail captif) où on saisit WiFi, IP du broker MQTT, pièce, mot de passe OTA.
  BOOT maintenu 3 s au démarrage = remise à zéro.
- Les mises à jour se font par WiFi (OTA), sans rien démonter.

## Kit « premier site » (mon père / grands-parents)
| Besoin | Nœud | Pièces |
|---|---|---|
| Savoir que tout va bien | `presence` (radar LD2410) | salon, chambre, cuisine, SdB |
| Porte d'entrée la nuit | `door` | entrée |
| Fuites | `leak` | cuisine, SdB, lave-linge |
| Confort / santé | `air` ou `climate` | chambre, salon |
| Lumière de nuit automatique | `presence` + relais/LED | couloir, SdB |
| Chauffage régulé | `heating` | par pièce |
| Conso | `power` | tableau (électricien) |
Alertes : `homeassistant/packages/senior_care.yaml` (inactivité, porte la nuit, fuite, CO2, pile faible).

## Principes pour les seniors
1. **Zéro charge pour eux** : pas d'appli, pas d'écran à apprendre. Les interrupteurs existants continuent de marcher.
2. **Le matériel se dégrade proprement** : lumière et chauffage fonctionnent en local si le WiFi ou HA tombe.
3. **Pas de caméra dans les pièces de vie/chambres.** Un radar de présence suffit et respecte l'intimité.
4. **Consentement explicite** de la personne, expliqué simplement (ce qui est détecté, qui est notifié). Pas de surveillance cachée.
5. **Un seul contact aidant** au départ, des seuils réglables (`input_number.senior_inactivity_hours`).
6. Ce n'est **pas un dispositif médical ni une alarme sociale/téléassistance** : ne jamais le présenter comme tel.
   Il complète, il ne remplace pas un appel, une téléassistance ou les secours.

## Du bricolage au service (amis puis clients)
- **Sécurité électrique** : tout ce qui touche le 230 V (volets, tableau, relais lumière) = module certifié CE ou électricien
  (NF C 15-100). Les nœuds `power`, `shutters` et la partie 230 V de `presence` sont à raccorder par un professionnel
  si tu n'es pas habilité. Alimentations 5 V certifiées, boîtiers fermés, fusible.
- **Cadre légal pour des clients** : statut d'entreprise (micro-entreprise possible), assurance RC pro, devis/contrat avec
  périmètre et limites de responsabilité, mentions RGPD (données personnelles = présence/absence d'une personne : finalité,
  durée de conservation, accès restreint, pas de transfert hors du site).
- **Standardiser** : mêmes cartes, mêmes boîtiers, mêmes nœuds -> installation et dépannage rapides. Étiqueter chaque nœud (pièce).
- **Documenter chaque site** : plan des nœuds, IP du broker, mots de passe dans un gestionnaire (jamais dans le dépôt).
- **Maintenance** : alerte « capteur muet / pile faible » déjà prévue ; prévoir un passage annuel.
- **Procédure d'un nouveau site** : (1) visite et liste des besoins, (2) devis, (3) HA + broker, (4) flasher les nœuds,
  (5) installer et nommer par pièce, (6) copier le package d'alertes et adapter les `entity_id`, (7) tester chaque alerte
  avec la personne, (8) remettre une fiche d'une page (qui appeler, que faire si ça ne marche plus).
