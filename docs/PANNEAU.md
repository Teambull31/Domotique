# Panneau de contrôle (Raspberry Pi + écran) et tablette des proches

Un même tableau de bord Home Assistant, **Maison** (`homeassistant/dashboards/maison.yaml`), sert :
- au **panneau mural** : Raspberry Pi + écran tactile ~6-7 pouces, allumé en permanence ;
- à la **tablette des grands-parents**, qui pilote les équipements de **leur** maison (chaque site a son
  propre Home Assistant, voir [DEPLOIEMENT.md](DEPLOIEMENT.md)).

Deux pages, grandes tuiles au doigt :
- **Accueil** : état de la maison en une phrase (« Tout va bien » / « 2 points à vérifier » + détail :
  fuite, porte ou portail ouvert, capteur en panne, pile faible), températures, chauffage, éclairage
  (un appui = allumer/éteindre), portail (ouverture/fermeture **avec confirmation**), volets.
- **Consommation** : puissance en ce moment, électricité et coût estimé du jour et du mois, histogramme
  des 30 derniers jours.

## 1. Côté Home Assistant (une fois par site)
1. Copier `homeassistant/packages/maison.yaml` dans `/config/packages/` et
   `homeassistant/custom_templates/maison.jinja` dans `/config/custom_templates/`.
2. Copier `homeassistant/dashboards/maison.yaml` dans `/config/dashboards/` et l'enregistrer (bloc
   `lovelace:` en tête du fichier). Adapter les `entity_id` (lumières, volets, thermostat) ; supprimer les
   tuiles des équipements absents.
3. Régler **Tarif de l'électricité** (prix TTC du kWh du contrat) dans Paramètres > Entités.
4. Créer un utilisateur **panneau** (Paramètres > Personnes), **non administrateur** : depuis l'écran, on
   pilote la maison mais on ne modifie pas la configuration.

## 2. Panneau mural (Raspberry Pi)
**Matériel** : Raspberry Pi 4 ou 5 (2 Go suffisent), alimentation officielle, carte microSD 16 Go,
écran tactile 6-7 pouces. Préférer un écran **DSI** (connecteur plat, ex. écran officiel Raspberry Pi) :
sa luminosité est pilotable, donc tamisée la nuit. Un écran HDMI + USB tactile fonctionne aussi, mais sa
luminosité se règle sur l'écran.

1. Avec Raspberry Pi Imager : **Raspberry Pi OS Lite (64 bits)**, en renseignant WiFi, nom d'utilisateur
   et SSH dans les réglages avancés.
2. Copier le dossier `panel/raspberry/` sur le Pi, puis :
   ```
   sudo ./install-kiosk.sh http://<ip-de-home-assistant>:8123/tableau-maison/accueil
   sudo reboot
   ```
   Au démarrage, le tableau de bord s'affiche en plein écran (Cage + Chromium, sans bureau). Écran
   tamisé de 22 h à 7 h (écrans DSI) ; `sudo panneau-ecran 60` pour régler à la main.
3. **Connexion automatique** du panneau à HA (pas de clavier à l'écran) : dans `configuration.yaml`,
   autoriser **la seule adresse IP du Pi** (réservée dans la box) à se connecter en tant que *panneau* :
   ```yaml
   homeassistant:
     auth_providers:
       - type: trusted_networks
         trusted_networks:
           - 192.168.1.50/32        # IP fixe du Raspberry Pi
         trusted_users:
           192.168.1.50/32: <id de l'utilisateur panneau>   # Paramètres > Personnes > Utilisateurs
         allow_bypass_login: true
       - type: homeassistant          # garder la connexion normale pour tous les autres
   ```
4. Écran à l'envers ou en portrait : l'orientation se règle dans `/boot/firmware/cmdline.txt` selon
   l'écran (voir sa documentation, ex. `video=DSI-1:panel_orientation=upside_down`).

## 3. Tablette des grands-parents
- **Le plus simple** : application **Home Assistant** (Android/iOS), connectée avec **leur** compte
  (non administrateur) sur **leur** HA. Dans le profil de ce compte, choisir **Maison** comme tableau de
  bord par défaut et masquer la barre latérale : l'application s'ouvre directement sur les grandes tuiles.
- **Tablette dédiée, toujours allumée** (Android) : *Fully Kiosk Browser* avec l'adresse du tableau de
  bord en page de démarrage, écran maintenu allumé sur son support de charge, retour automatique à
  l'accueil après quelques minutes.
- Hors de la maison (proches qui veulent aider à distance) : accès sécurisé Nabu Casa ou Tailscale, jamais
  de port ouvert sur la box.

## 4. Portail motorisé (nœud `gate`)
Le nœud ne remplace rien de la motorisation : il **imite le bouton de la télécommande** et **lit un capteur
de position**. Les sécurités (cellules photo-électriques, détection d'obstacle) restent celles du moteur.
- **Commande** : relais à contact sec (sortie `PIN_RELAY1`) branché sur l'entrée impulsion de la carte de
  la motorisation (souvent notée START, PP « pas à pas » ou OPEN : voir sa notice). Impulsion de 0,5 s.
- **Position** : contact magnétique d'extérieur (NO, adapté aux portails) entre `PIN_INPUT1` et GND, fermé
  quand le portail est fermé. Un 2e contact « ouvert » (`PIN_INPUT2`) est facultatif : le déclarer dans HA
  (**Fin de course ouvert installé**). Régler **Durée de manœuvre** (chronométrer une ouverture complète).
- **Sûreté** : l'ordre n'est envoyé que si l'état le justifie (jamais d'impulsion pendant un mouvement, qui
  l'arrêterait ou l'inverserait), anti double appui, confirmation obligatoire sur l'écran ; alerte si le
  portail reste ouvert 10 min.
- Boîtier étanche (IP65), alimentation 5 V depuis l'alimentation du moteur ou un bloc dédié ; tout
  raccordement au 230 V par un électricien.
