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
ExecStart=/usr/bin/cage -d -- $BROWSER_BIN --kiosk --noerrdialogs --disable-infobars --no-first-run \\
  --password-store=basic --ozone-platform=wayland --check-for-update-interval=31536000 \\
  --disable-features=Translate --overscroll-history-navigation=0 $URL
Restart=always
RestartSec=5

[Install]
WantedBy=graphical.target
UNIT

# Luminosité jour / nuit (écrans DSI avec rétroéclairage pilotable)
install -m 0755 "$(dirname "$0")/ecran.sh" /usr/local/bin/panneau-ecran
cat >/etc/cron.d/panneau-ecran <<'CRON'
# Panneau : écran tamisé la nuit (22 h - 7 h)
0 22 * * * root /usr/local/bin/panneau-ecran nuit
0 7 * * * root /usr/local/bin/panneau-ecran jour
CRON

systemctl disable getty@tty1.service || true
systemctl set-default graphical.target
systemctl daemon-reload
systemctl enable panneau.service

echo "Panneau installé. Redémarrer : sudo reboot"
