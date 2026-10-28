# Fiche d'installation sur site (checklist)

À suivre dans l'ordre chez un proche ou un client. Les principes et le modèle économique sont dans
[`DEPLOIEMENT.md`](DEPLOIEMENT.md), le matériel dans [`MATERIEL.md`](MATERIEL.md).

## 1. Avant de partir
- [ ] Nœuds flashés et testés sur table : `cd firmware && pio run -e <nœud>[-c3|-s3] -t upload`.
- [ ] Étiquette sur chaque boîtier : type de nœud + pièce prévue.
- [ ] Mot de passe OTA du site généré (≥ 8 caractères) et rangé dans le gestionnaire de mots de passe.

## 2. Home Assistant du site
- [ ] HA installé (HA Green ou Raspberry Pi), à jour, sauvegardes automatiques activées.
- [ ] Module complémentaire **Mosquitto broker** + utilisateur HA dédié `domo` (mot de passe fort) ;
      intégration **MQTT** activée.
- [ ] App **Home Assistant** installée sur le téléphone de chaque proche aidant ; inscrire leurs
      services `notify.mobile_app_…` dans le groupe `proche_aidant` en tête de `senior_care.yaml`
      (tous les proches listés reçoivent toutes les alertes), puis lancer **Tester les alertes du proche**.
- [ ] Accès à distance (Nabu Casa ou Tailscale) pour le dépannage, avec l'accord écrit du client.

## 3. Mise en service de chaque nœud
- [ ] Alimenter le nœud, se connecter au WiFi `Domo-<nœud>-setup` (portail ouvert 5 min).
- [ ] Renseigner : WiFi du site, IP de HA, port 1883, utilisateur/mot de passe `domo`, **pièce**,
      **emplacement** si 2 nœuds du même type dans la pièce, mot de passe OTA.
- [ ] Dans HA > Paramètres > Appareils : l'appareil apparaît (ex. `door-entree`) ; lui attribuer sa pièce.
- [ ] Diagnostic : **Signal WiFi** au-dessus de −75 dBm, sinon déplacer le nœud ou ajouter un répéteur.

## 4. Réglages (Paramètres de chaque appareil dans HA)
- [ ] Volets : chronométrer une montée et une descente complètes, saisir **Durée de montée/descente**.
- [ ] Présence : **Extinction après** (couloir 1–2 min, pièce de vie 5–10 min).
- [ ] Climat / thermostat : comparer à un thermomètre de référence posé 30 min à côté, saisir
      **Correction température**.
- [ ] Thermostat : consigne de confort, vérifier le clic du relais en baissant puis remontant la consigne.
- [ ] Compteur d'eau : **Litres par impulsion** (indiqué sur le compteur ou l'émetteur, souvent 1 ou 10 L),
      **Index du compteur** (relevé sur le cadran, en m³), **Tarif de l'eau** (facture, €/m³) ;
      **Alerte eau qui coule après** : 60 min par défaut (plus si un jardin est arrosé longtemps).

## 5. Alertes « tranquillité »
- [ ] Copier `homeassistant/packages/senior_care.yaml` dans `/config/packages/` et
      `homeassistant/custom_templates/maison.jinja` dans `/config/custom_templates/`, puis adapter au site :
      les **groupes** en tête du fichier (radars, sondes de fuite, boutons d'appel, températures), la porte
      d'entrée, le CO2 de la chambre et la puissance de cuisson. Piles faibles, capteurs muets et défauts
      sont repérés automatiquement parmi les nœuds (rien à lister ; téléphones exclus).
- [ ] Vérifier (`homeassistant/check_config.sh` et `python homeassistant/test_templates.py`, ou Outils
      de développement > Vérifier la configuration), puis redémarrer HA.
- [ ] Tableau de bord du proche : copier `homeassistant/dashboards/tranquillite.yaml` dans
      `/config/dashboards/` et l'enregistrer (bloc `lovelace:` en tête du fichier), adapter les `entity_id`.
- [ ] Panneau mural et/ou tablette de la personne : tableau de bord **Maison** (état de la maison,
      éclairage, portail, consommation), voir [PANNEAU.md](PANNEAU.md).
- [ ] Lancer le script **Tester les alertes du proche** : les 2 notifications arrivent, la 2ᵉ sonne
      téléphone en silencieux.

## 6. Recette (tester chaque capteur pour de vrai)
- [ ] Porte : ouvrir/fermer, l'état suit dans HA en moins de 5 s ; ouverture brève aussi.
- [ ] Fuite : chiffon mouillé sur la sonde -> alerte urgente, puis rappel à 5 min ; acquitter.
- [ ] Présence : entrer, rester immobile assis 2 min (doit rester « détecté »), sortir -> extinction.
- [ ] Volets : ouverture/fermeture complètes, puis position 50 %. Boutons muraux : un appui = course
      complète, un appui pendant la course = arrêt ; essayer aussi **WiFi coupé** (box débranchée).
- [ ] Puissance : allumer un appareil de puissance connue (bouilloire ~2000 W), la mesure suit.
- [ ] Eau : remplir un seau de 10 L, « Eau consommée » augmente de 10 L (± 1 impulsion) et le débit
      s'affiche ; l'index de HA suit celui du cadran.
- [ ] Bouton d'appel : appui -> voyant allumé 3 s, alerte « Appel à l'aide » sur le téléphone du proche,
      rappel 5 min plus tard ; éteindre « Appel en cours ». Montrer le bouton à la personne âgée.
- [ ] Pile : niveau de batterie cohérent (accu chargé ≈ 100 %).
- [ ] Panne simulée : débrancher le radar (ou une sonde de température) -> « Défaut » en 10 s dans HA,
      notification « Capteur à vérifier » après 10 min ; rebrancher.
- [ ] Inactivité (à faire un après-midi où la maison est vide) : régler **Alerte si aucune présence
      détectée** sur 2 h -> alerte au bout de 2 h, rappel 1 h plus tard ; acquitter, puis remettre le
      réglage habituel (8 h par défaut).
- [ ] Le premier soir : le **bilan de la journée** arrive à 20 h sur le téléphone du proche.

## 7. Remise au client
- [ ] Imprimer et remplir le [guide d'utilisation](GUIDE_UTILISATEUR.md) (nom et numéro à appeler),
      le laisser près de l'écran et le parcourir ensemble.
- [ ] Personne âgée : rien à faire, rien à toucher ; expliquer simplement ce qui est surveillé (et ce
      qui ne l'est pas : ce n'est **ni une téléassistance ni un détecteur de chute**).
- [ ] Proche aidant : les alertes fuite et inactivité **se répètent jusqu'à acquittement** (tableau de bord
      Tranquillité > Alertes) ; couper les alertes en cas d'absence (**Alertes tranquillité actives** :
      fuite, porte la nuit et pannes restent actives) ; qui appeler en cas de panne.
- [ ] Consentement RGPD signé (personne âgée et proche), liste des capteurs et de leur emplacement.

## 8. Entretien
- Piles : alerte automatique sous 20 % ou capteur muet ~3 h.
- Mises à jour à distance (OTA), avec le mot de passe OTA du site :
  `PLATFORMIO_UPLOAD_FLAGS="--auth=<mot de passe OTA>" pio run -e <nœud>[-c3|-s3] -t upload --upload-port <ip du nœud>`
  (PlatformIO passe seul en mode OTA quand le port est une adresse IP).
- Visite annuelle : test des alertes, dépoussiérage des capteurs d'air, contrôle des fixations.
