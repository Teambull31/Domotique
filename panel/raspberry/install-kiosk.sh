#!/usr/bin/env bash
# Transforme un Raspberry Pi (Raspberry Pi OS Lite 64 bits, Bookworm) en panneau mural :
# au démarrage, le tableau de bord Home Assistant s'affiche en plein écran, sans bureau ni clavier.
# Usage : sudo ./install-kiosk.sh http://<ip-de-home-assistant>:8123/tableau-maison/accueil
# Voir docs/PANNEAU.md (connexion automatique du panneau à HA, rotation de l'écran).
set -euo pipefail

URL="${1:-}"
if [[ -z "$URL" ]]; then
  echo "Usage : sudo $0 http://<ip-home-assistant>:8123/tableau-maison/accueil" >&2
  exit 1
fi
if [[ $EUID -ne 0 ]]; then
  echo "À lancer avec sudo." >&2
  exit 1
fi

# Navigateur : chromium-browser sur Raspberry Pi OS, chromium sur Debian
BROWSER_PKG=chromium-browser
if ! apt-cache show chromium-browser >/dev/null 2>&1; then BROWSER_PKG=chromium; fi

apt-get update
apt-get install -y --no-install-recommends cage "$BROWSER_PKG" fonts-noto-color-emoji
BROWSER_BIN="$(command -v chromium-browser || command -v chromium)"

# Utilisateur dédié, sans mot de passe ni droits d'administration
if ! id kiosk >/dev/null 2>&1; then
  useradd --create-home --shell /usr/sbin/nologin kiosk
fi
usermod -aG video,input,render kiosk

# Service : Cage (compositeur Wayland plein écran) + Chromium en mode kiosque sur tty1
cat >/etc/systemd/system/panneau.service <<UNIT
[Unit]
Description=Panneau domotique (Cage + Chromium)
After=systemd-user-sessions.service network-online.target
Wants=network-online.target
Conflicts=getty@tty1.service

[Service]
User=kiosk
PAMName=login
TTYPath=/dev/tty1
TTYReset=yes
TTYVHangup=yes
TTYVTDisallocate=yes
StandardInput=tty-fail
UtmpIdentifier=tty1
UtmpMode=user
Environment=XDG_SESSION_TYPE=wayland
ExecStartPre=/usr/local/bin/panneau-chromium-propre
ExecStartPre=/usr/bin/python3 /usr/local/bin/panneau-attendre-ha $URL
TimeoutStartSec=infinity
ExecStart=/usr/bin/cage -d -- $BROWSER_BIN --kiosk --noerrdialogs --disable-infobars --no-first-run \\
  --password-store=basic --ozone-platform=wayland --check-for-update-interval=31536000 \\
  --disable-features=Translate --overscroll-history-navigation=0 --hide-crash-restore-bubble $URL
Restart=always
RestartSec=5

[Install]
WantedBy=graphical.target
UNIT

# Veille de l'écran : tamisé après 1 min sans contact, plein éclat au toucher (le contact qui réveille
# n'est pas transmis au tableau de bord), niveaux plus bas la nuit. Écrans DSI (rétroéclairage pilotable).
install -m 0755 "$(dirname "$0")/panneau-veille.py" /usr/local/bin/panneau-veille
install -m 0755 "$(dirname "$0")/ecran.sh" /usr/local/bin/panneau-ecran   # réglage manuel ponctuel
install -m 0755 "$(dirname "$0")/chromium-propre.sh" /usr/local/bin/panneau-chromium-propre
install -m 0755 "$(dirname "$0")/attendre-ha.py" /usr/local/bin/panneau-attendre-ha
if [[ ! -e /etc/default/panneau-veille ]]; then
  cat >/etc/default/panneau-veille <<'CONF'
# Veille du panneau (luminosités en %, heures 0-23) ; puis : sudo systemctl restart panneau-veille
DELAI=60          # secondes sans contact avant la veille
ACTIF_JOUR=100
ACTIF_NUIT=40
VEILLE_JOUR=15    # assez pour lire l'état de la maison de loin
VEILLE_NUIT=3
NUIT_DEBUT=22
NUIT_FIN=7
# Réveil sur alerte (facultatif) : écran éclairé tant que l'État de la maison signale un problème.
# Jeton : profil de l'utilisateur « panneau » dans HA > Sécurité > Jetons d'accès longue durée.
#HA_URL=http://192.168.1.10:8123
#HA_TOKEN=
CONF
fi
chmod 600 /etc/default/panneau-veille  # contient le jeton HA
cat >/etc/systemd/system/panneau-veille.service <<'UNIT'
[Unit]
Description=Veille de l'écran du panneau (tamisé sans contact, réveil au toucher)

[Service]
ExecStart=/usr/bin/python3 /usr/local/bin/panneau-veille
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
UNIT
rm -f /etc/cron.d/panneau-ecran  # ancienne version : tamisage à heure fixe

systemctl disable getty@tty1.service || true
systemctl set-default graphical.target
systemctl daemon-reload
systemctl enable panneau.service panneau-veille.service

echo "Panneau installé. Redémarrer : sudo reboot"
