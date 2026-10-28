#!/usr/bin/env bash
# Transforme une carte ARM en panneau mural : au démarrage, le tableau de bord Home Assistant
# s'affiche en plein écran, sans bureau ni clavier.
# Cartes : Raspberry Pi 3B+/4/5 (Raspberry Pi OS Lite 64 bits), Orange Pi et autres cartes ARM sous
# Armbian ou Debian (Bookworm ou plus récent). Ubuntu : non (Chromium n'y existe qu'en snap).
# Usage : sudo ./install-kiosk.sh [http://<ip-de-home-assistant>:8123/tableau-maison/accueil]
#   Adresse facultative si /etc/default/panneau existe déjà (réinstallation, changement de carte).
# Voir docs/PANNEAU.md (cartes compatibles, connexion automatique à HA, changement de carte).
set -euo pipefail

ICI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF="${PANNEAU_CONF:-/etc/default/panneau}"

# Paquet disponible dans les dépôts apt (après apt-get update) ? Sortie capturée, pas de
# « apt-cache | grep -q » : grep s'arrête au 1er résultat, apt-cache reçoit SIGPIPE et, avec
# pipefail, le test échoue alors que le paquet existe.
installable() {
  local out
  out="$(apt-cache policy "$1" 2>/dev/null)"
  [[ "$out" == *"Candidate: "[^\(]* ]]
}

# Ubuntu : « chromium-browser » n'est qu'un renvoi vers le snap (il dépend de snapd)
renvoi_snap() {
  local out
  out="$(apt-cache depends "$1" 2>/dev/null)"
  [[ "$out" == *snapd* ]]
}

# Chromium en paquet deb : « chromium-browser » sur Raspberry Pi OS Bookworm, « chromium » sur Debian,
# Armbian et Raspberry Pi OS Trixie. Sur Ubuntu, « chromium-browser » n'est qu'un renvoi vers le snap
# (inutilisable en kiosque sous un utilisateur dédié) : refusé. Rien d'affiché = pas de Chromium deb.
choisir_navigateur() {
  if installable chromium-browser && ! renvoi_snap chromium-browser; then
    echo chromium-browser
  elif installable chromium; then
    echo chromium
  fi
}

# Groupes existants parmi ceux demandés, séparés par des virgules (images minimales : « render » ou
# « input » peuvent manquer, et usermod échouerait).
groupes_existants() {
  local g out=()
  for g in "$@"; do
    if getent group "$g" >/dev/null; then out+=("$g"); fi
  done
  (IFS=,; echo "${out[*]}")
}

modele_carte() {
  if [[ -r /proc/device-tree/model ]]; then tr -d '\0' </proc/device-tree/model; else echo "inconnue"; fi
}

# Valeur d'une clé du fichier de réglages (vide si absente)
lire_cle() {
  [[ -f "$1" ]] || return 0
  sed -n "s/^$2=//p" "$1" | tail -n 1 | tr -d '"'
}

# Réglages du panneau (sans secret, lisibles par l'utilisateur kiosk) : créés au premier passage,
# sinon seules l'adresse et le navigateur sont mis à jour (le choix du rendu est conservé).
# Commentaires sur des lignes à part : systemd (EnvironmentFile) ne les retire pas en fin de ligne.
# Valeurs entre guillemets : une adresse avec « & » reste lisible par systemd comme par un shell.
ecrire_reglages() {
  local conf="$1" url="$2" nav="$3"
  if [[ ! -e "$conf" ]]; then
    cat >"$conf" <<CONF
# Panneau domotique : réglages de l'affichage. À recopier avec /etc/default/panneau-veille sur une
# carte de remplacement, puis relancer install-kiosk.sh sans argument.
# Adresse du tableau de bord
URL="$url"
# Navigateur (détecté à l'installation)
NAVIGATEUR="$nav"
# Rendu : auto (conseillé), gpu, ou logiciel (écran noir avec auto : image sans pilote 3D)
RENDU=auto
CONF
    return 0
  fi
  local cle val
  for cle in URL NAVIGATEUR; do
    if [[ $cle == URL ]]; then val="$url"; else val="$nav"; fi
    if grep -q "^$cle=" "$conf"; then
      val="$(printf '%s' "$val" | sed 's/[&|\\]/\\&/g')"  # « & » d'une adresse : littéral pour sed
      sed -i "s|^$cle=.*|$cle=\"$val\"|" "$conf"
    else
      echo "$cle=\"$val\"" >>"$conf"
    fi
  done
}

main() {
  # Fichiers créés en 644 quel que soit l'environnement : une unité systemd modifiable par tous
  # permettrait à n'importe quel utilisateur (dont kiosk) d'exécuter du code en root.
  umask 022
  local url="${1:-$(lire_cle "$CONF" URL)}"
  if [[ -z "$url" ]]; then
    echo "Usage : sudo $0 http://<ip-home-assistant>:8123/tableau-maison/accueil" >&2
    exit 1
  fi
  if [[ $EUID -ne 0 ]]; then
    echo "À lancer avec sudo." >&2
    exit 1
  fi
  if ! command -v apt-get >/dev/null; then
    echo "Système non pris en charge : Raspberry Pi OS, Debian ou Armbian (Debian) requis." >&2
    exit 1
  fi

  local mem_mo
  mem_mo=$(awk '/^MemTotal/ {print int($2 / 1024)}' /proc/meminfo)
  echo "Carte : $(modele_carte) ; mémoire : $mem_mo Mo"
  if ((mem_mo < 1500)); then
    echo "Attention : moins de 2 Go de mémoire, le tableau de bord sera lent (2 Go ou plus conseillés)."
  fi

  apt-get update
  local nav_pkg
  nav_pkg="$(choisir_navigateur)"
  if [[ -z "$nav_pkg" ]]; then
    echo "Chromium (paquet deb) introuvable. Sur Ubuntu il n'existe qu'en snap, inutilisable ici :" \
         "installer Raspberry Pi OS Lite, Debian ou Armbian (variante Debian)." >&2
    exit 1
  fi
  # python3 : absent de certaines images minimales (Armbian), utilisé par la veille et l'attente de HA
  # xwayland : sans lui, Cage refuse de démarrer (écran noir), et son paquet ne le demande pas
  apt-get install -y --no-install-recommends cage xwayland "$nav_pkg" fonts-noto-color-emoji python3
  local nav="/usr/bin/$nav_pkg"

  # Utilisateur dédié, sans mot de passe ni droits d'administration
  if ! id kiosk >/dev/null 2>&1; then
    useradd --create-home --shell /usr/sbin/nologin kiosk
  fi
  local groupes
  groupes="$(groupes_existants video input render)"
  if [[ -n "$groupes" ]]; then usermod -aG "$groupes" kiosk; fi

  ecrire_reglages "$CONF" "$url" "$nav"
  chmod 644 "$CONF"

  install -m 0755 "$ICI/lancer.sh" /usr/local/bin/panneau-lancer
  install -m 0755 "$ICI/panneau-veille.py" /usr/local/bin/panneau-veille
  install -m 0755 "$ICI/ecran.sh" /usr/local/bin/panneau-ecran   # réglage manuel ponctuel
  install -m 0755 "$ICI/chromium-propre.sh" /usr/local/bin/panneau-chromium-propre
  install -m 0755 "$ICI/attendre-ha.py" /usr/local/bin/panneau-attendre-ha

  # Service : Cage (compositeur Wayland plein écran) + Chromium en mode kiosque sur tty1
  cat >/etc/systemd/system/panneau.service <<'UNIT'
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
EnvironmentFile=/etc/default/panneau
Environment=XDG_SESSION_TYPE=wayland
ExecStartPre=/usr/local/bin/panneau-chromium-propre
ExecStartPre=/usr/bin/python3 /usr/local/bin/panneau-attendre-ha ${URL}
TimeoutStartSec=infinity
ExecStart=/usr/local/bin/panneau-lancer
Restart=always
RestartSec=5

[Install]
WantedBy=graphical.target
UNIT

  # Veille de l'écran : tamisé après 1 min sans contact, plein éclat au toucher (le contact qui
  # réveille n'est pas transmis au tableau de bord), niveaux plus bas la nuit. Écrans à rétroéclairage
  # pilotable (DSI, et la plupart des écrans à nappe des cartes ARM).
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

  # Relance du navigateur chaque nuit : après des semaines, Chromium grossit en mémoire, et après une
  # mise à jour de Home Assistant le tableau de bord demande à être rechargé (personne pour le faire).
  cat >/etc/systemd/system/panneau-relance.service <<'UNIT'
[Unit]
Description=Relance quotidienne du navigateur du panneau

[Service]
Type=oneshot
ExecStart=/usr/bin/systemctl try-restart panneau.service
UNIT
  cat >/etc/systemd/system/panneau-relance.timer <<'UNIT'
[Unit]
Description=Relance du navigateur du panneau chaque nuit à 4 h

[Timer]
OnCalendar=*-*-* 04:00:00

[Install]
WantedBy=timers.target
UNIT

  # Chien de garde matériel : carte figée (noyau bloqué, mémoire saturée) -> redémarrage
  # automatique, au lieu d'un écran gelé jusqu'à la prochaine visite. 15 s : accepté par les
  # chiens de garde des Raspberry Pi (maximum) et des cartes Allwinner / Rockchip.
  mkdir -p /etc/systemd/system.conf.d
  cat >/etc/systemd/system.conf.d/panneau-chien-de-garde.conf <<'CONF'
[Manager]
RuntimeWatchdogSec=15
RebootWatchdogSec=2min
CONF

  # Image « avec bureau » : son gestionnaire de connexion prendrait l'écran à la place du panneau
  if [[ -L /etc/systemd/system/display-manager.service ]]; then
    local dm
    dm="$(basename "$(readlink -f /etc/systemd/system/display-manager.service)")"
    systemctl disable "$dm" || true
    echo "Bureau graphique ($dm) désactivé : l'écran est réservé au panneau."
  fi
  # Heures de nuit de la veille : fuseau horaire à régler (images Armbian/Debian souvent en UTC)
  if [[ "$(readlink -f /etc/localtime 2>/dev/null)" == */UTC || ! -e /etc/localtime ]]; then
    echo "Attention : fuseau horaire UTC. Le régler : sudo timedatectl set-timezone Europe/Paris"
  fi

  # Droits forcés aussi sur des fichiers déjà présents (cat > garde les droits d'un ancien fichier)
  chmod 644 /etc/systemd/system/panneau.service /etc/systemd/system/panneau-veille.service \
            /etc/systemd/system/panneau-relance.service /etc/systemd/system/panneau-relance.timer \
            /etc/systemd/system.conf.d/panneau-chien-de-garde.conf

  systemctl disable getty@tty1.service || true
  systemctl set-default graphical.target
  systemctl daemon-reload
  systemctl enable panneau.service panneau-veille.service panneau-relance.timer

  echo "Panneau installé ($nav_pkg). Redémarrer : sudo reboot"
}

# Exécuté (et non chargé par les tests avec « source »)
if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then main "$@"; fi
