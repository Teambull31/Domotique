# Panneau de contrôle (Raspberry Pi + écran) et tablette des proches

Un même tableau de bord Home Assistant, **Maison** (`homeassistant/dashboards/maison.yaml`), sert :
- au **panneau mural** : Raspberry Pi + écran tactile ~6-7 pouces, allumé en permanence ;
- à la **tablette des grands-parents**, qui pilote les équipements de **leur** maison (chaque site a son
  propre Home Assistant, voir [DEPLOIEMENT.md](DEPLOIEMENT.md)).

Deux pages, grandes tuiles au doigt :
- **Accueil** : état de la maison en une phrase (« Tout va bien » / « 2 points à vérifier » + détail :
  fuite, porte ou portail ouvert, capteur en panne ou hors ligne, pile faible), appareils connectés
  (« 12 en ligne », ou lesquels ne répondent plus), températures, chauffage, éclairage
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
   Choisir le téléphone prévenu (portail resté ouvert, ouvert la nuit) à un seul endroit : le script
   `maison_notifier` en tête de `packages/maison.yaml`.
4. Créer un utilisateur **panneau** (Paramètres > Personnes), **non administrateur** : depuis l'écran, on
   pilote la maison mais on ne modifie pas la configuration.

## 2. Panneau mural (Raspberry Pi)
**Matériel** : Raspberry Pi 4 ou 5 (2 Go suffisent), alimentation officielle, carte microSD 16 Go,
écran tactile 6-7 pouces. Préférer un écran **DSI** (connecteur plat, ex. écran officiel Raspberry Pi) :
sa luminosité est pilotable, donc l'écran se tamise quand on ne s'en sert pas. Un écran HDMI + USB tactile
fonctionne aussi, mais reste à pleine luminosité (réglage sur l'écran lui-même).

1. Avec Raspberry Pi Imager : **Raspberry Pi OS Lite (64 bits)**, en renseignant WiFi, nom d'utilisateur
   et SSH dans les réglages avancés.
2. Copier le dossier `panel/raspberry/` sur le Pi, puis :
   ```
   sudo ./install-kiosk.sh http://<ip-de-home-assistant>:8123/tableau-maison/accueil
   sudo reboot
   ```
   Au démarrage, le tableau de bord s'affiche en plein écran (Cage + Chromium, sans bureau).
   **Sans entretien** : le navigateur est relancé chaque nuit à 4 h (mémoire, nouvelle version de HA),
   et le chien de garde matériel redémarre le Pi s'il se fige.
   **Veille** (écrans DSI) : après 1 min sans contact, l'écran se tamise (15 % le jour, 3 % la nuit :
   l'état de la maison reste lisible de loin) ; il revient au toucher, et ce premier contact **ne
   déclenche rien** (pas de lumière allumée ni de portail ouvert en réveillant l'écran). Délai et niveaux
   dans `/etc/default/panneau-veille`, puis `sudo systemctl restart panneau-veille`.
   **Réveil sur alerte** (conseillé) : renseigner `HA_URL` et `HA_TOKEN` (jeton longue durée de
   l'utilisateur *panneau*) dans ce même fichier : tant que l'état de la maison n'est pas « Tout va
   bien » (fuite, portail ouvert, appel…), l'écran reste allumé et le problème se voit de loin.
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

Même installation chez chaque proche (ex. le salon d'un parent) : un panneau par site, relié au Home
Assistant de **ce** site.

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
- **Portail non câblable** (pas d'accès au moteur ni de courant au portail) : **télécommande
  « sacrifiée »**. Acheter une télécommande d'origine supplémentaire, l'appairer au portail, et brancher un
  optocoupleur (PC817) ou le relais aux bornes de son bouton : le nœud `gate`, dans la maison à portée
  radio, « appuie » dessus. Marche avec toutes les marques, y compris les codes tournants (Nice, Came,
  BFT, FAAC…), qu'on ne peut pas copier. La position vient alors d'un nœud `door` sur pile posé au
  portail (pièce « portail ») : activer **Position fournie par HA** sur le nœud gate et adapter
  l'automatisation « position du portail transmise » de `packages/maison.yaml`. Tant que la position
  n'est pas connue (redémarrage, pile du capteur vide), le portail s'affiche **inconnu** et le nœud
  n'envoie aucun ordre à l'aveugle (« fermer » un portail déjà fermé l'ouvrirait).
  Copier le signal d'une télécommande n'est possible qu'avec un **code fixe** (anciennes 433 MHz) ;
  Somfy RTS peut être appairé comme une nouvelle télécommande (ESP32 + CC1101, projet ESPSomfy-RTS).
- **Manœuvre non confirmée** : si le portail n'atteint pas la position attendue 30 s après la durée de
  manœuvre (ordre radio perdu, obstacle, panne), le nœud le signale ; c'est repris par « État de la
  maison » et l'alerte « Capteur à vérifier ».
- **Sûreté** : l'ordre n'est envoyé que si l'état le justifie (jamais d'impulsion pendant un mouvement, qui
  l'arrêterait ou l'inverserait), anti double appui, confirmation obligatoire sur l'écran ; alerte si le
  portail reste ouvert 10 min, rappel à 22 h 30 s'il est encore ouvert, alerte qui sonne même en
  silencieux s'il s'ouvre entre 22 h 30 et 6 h.
- Boîtier étanche (IP65), alimentation 5 V depuis l'alimentation du moteur ou un bloc dédié ; tout
  raccordement au 230 V par un électricien.
