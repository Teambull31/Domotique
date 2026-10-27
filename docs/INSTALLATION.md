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
- [ ] App **Home Assistant** installée sur le téléphone du proche aidant ; renommer son service de
      notification en `mobile_app_proche` (ou adapter le package).
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

## 5. Alertes « tranquillité »
- [ ] Copier `homeassistant/packages/senior_care.yaml` dans `/config/packages/` et l'adapter au site :
      les **groupes** en tête du fichier (une liste par type de capteur : radars, sondes de fuite, piles,
      défauts, températures), puis la porte d'entrée, le CO2 de la chambre et la puissance de cuisson ; vérifier (`homeassistant/check_config.sh` ou Outils de développement >
      Vérifier la configuration), redémarrer HA.
- [ ] Tableau de bord du proche : copier `homeassistant/dashboards/tranquillite.yaml` dans
      `/config/dashboards/` et l'enregistrer (bloc `lovelace:` en tête du fichier), adapter les `entity_id`.
- [ ] Lancer le script **Tester les alertes du proche** : les 2 notifications arrivent, la 2ᵉ sonne
      téléphone en silencieux.

## 6. Recette (tester chaque capteur pour de vrai)
- [ ] Porte : ouvrir/fermer, l'état suit dans HA en moins de 5 s ; ouverture brève aussi.
- [ ] Fuite : chiffon mouillé sur la sonde -> alerte urgente, puis rappel à 5 min ; acquitter.
- [ ] Présence : entrer, rester immobile assis 2 min (doit rester « détecté »), sortir -> extinction.
- [ ] Volets : ouverture/fermeture complètes, puis position 50 %.
- [ ] Puissance : allumer un appareil de puissance connue (bouilloire ~2000 W), la mesure suit.
- [ ] Pile : niveau de batterie cohérent (accu chargé ≈ 100 %).

## 7. Remise au client
- [ ] Personne âgée : rien à faire, rien à toucher ; expliquer simplement ce qui est surveillé (et ce
      qui ne l'est pas : ce n'est **ni une téléassistance ni un détecteur de chute**).
- [ ] Proche aidant : recevoir/acquitter une alerte, couper les alertes en cas d'absence
      (**Alertes tranquillité actives**), qui appeler en cas de panne.
- [ ] Consentement RGPD signé (personne âgée et proche), liste des capteurs et de leur emplacement.

## 8. Entretien
- Piles : alerte automatique sous 20 % ou capteur muet ~3 h.
- Mises à jour à distance (OTA), avec le mot de passe OTA du site :
  `PLATFORMIO_UPLOAD_FLAGS="--auth=<mot de passe OTA>" pio run -e <nœud>[-c3|-s3] -t upload --upload-port <ip du nœud>`
  (PlatformIO passe seul en mode OTA quand le port est une adresse IP).
- Visite annuelle : test des alertes, dépoussiérage des capteurs d'air, contrôle des fixations.
