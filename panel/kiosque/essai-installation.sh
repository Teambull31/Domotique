#!/usr/bin/env bash
# Essai réel de l'installation dans un conteneur (CI : .github/workflows/panel.yml) :
#   docker run --rm --privileged -v "$PWD/panel/kiosque:/src:ro" debian:bookworm-slim \
#     bash /src/essai-installation.sh            # Debian, Armbian (même base), Raspberry Pi OS Trixie
#   ... ubuntu:24.04 bash /src/essai-installation.sh refus   # doit être refusé (Chromium en snap)
# Installe pour de vrai (apt), contrôle réglages, droits et unités systemd, réinstalle sans adresse
# (changement de carte), puis lance le kiosque sans écran (Cage headless, rendu logiciel automatique) :
# la page de test n'appelle /rendu-ok que si Chromium l'a réellement affichée et exécutée.
# --privileged : bac à sable de Chromium (espaces de noms), refusé par le profil Docker par défaut.
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
echec() { echo "ÉCHEC : $*" >&2; exit 1; }

apt-get update -qq
if [[ "${1:-}" == refus ]]; then
  if bash /src/install-kiosk.sh http://127.0.0.1:8123/ >/tmp/i.log 2>&1; then echec "système accepté"; fi
  grep -q snap /tmp/i.log || echec "refus sans explication : $(tail -1 /tmp/i.log)"
  if dpkg -s snapd >/dev/null 2>&1; then echec "snapd installé"; fi
  echo "OK : système refusé proprement ($(tail -1 /tmp/i.log | cut -c1-60)…)"
  exit 0
fi

# Pas de systemd actif dans le conteneur : systemctl simulé ; systemd-analyze installé pour vérifier
apt-get install -y -qq --no-install-recommends systemd >/dev/null
mkdir -p /usr/local/sbin
printf '#!/bin/sh\necho "[systemctl $*]"\n' >/usr/local/sbin/systemctl
chmod +x /usr/local/sbin/systemctl

url='http://127.0.0.1:8123/tableau-maison/accueil/?kiosk&x=1'
umask 000  # environnement permissif : les fichiers doivent quand même être créés en 644
bash /src/install-kiosk.sh "$url" >/tmp/i.log 2>&1 || { tail -20 /tmp/i.log; echec "installation"; }
grep -E "^(Carte|Panneau installé)" /tmp/i.log

grep -qxF "URL=\"$url\"" /etc/default/panneau || echec "adresse mal enregistrée : $(grep ^URL /etc/default/panneau)"
nav="$(sed -n 's/^NAVIGATEUR="\(.*\)"$/\1/p' /etc/default/panneau)"
[[ -x "$nav" ]] || echec "navigateur absent : $nav"
command -v cage >/dev/null || echec "cage absent"
command -v Xwayland >/dev/null || echec "xwayland absent (Cage ne démarrerait pas)"
for f in /etc/systemd/system/panneau.service /etc/systemd/system/panneau-veille.service \
         /etc/systemd/system/panneau-relance.service /etc/systemd/system/panneau-relance.timer \
         /etc/systemd/system.conf.d/panneau-chien-de-garde.conf /etc/default/panneau; do
  [[ "$(stat -c %a "$f")" == 644 ]] || echec "droits $(stat -c %a "$f") sur $f"
done
[[ "$(stat -c %a /etc/default/panneau-veille)" == 600 ]] || echec "jeton HA lisible par tous"
(cd /etc/systemd/system && /usr/bin/systemd-analyze verify --man=no panneau.service \
   panneau-veille.service panneau-relance.timer panneau-relance.service) || echec "unités systemd"
echo "OK : installation ($("$nav" --version 2>/dev/null | head -1), cage, xwayland, droits, unités)"

# Changement de carte : réinstallation sans adresse, rendu choisi conservé
sed -i 's/^RENDU=auto$/RENDU=logiciel/' /etc/default/panneau
bash /src/install-kiosk.sh >/tmp/i2.log 2>&1 || { tail -5 /tmp/i2.log; echec "réinstallation sans adresse"; }
grep -qxF "URL=\"$url\"" /etc/default/panneau || echec "adresse perdue à la réinstallation"
grep -qx "RENDU=logiciel" /etc/default/panneau || echec "rendu perdu à la réinstallation"
sed -i 's/^RENDU=logiciel$/RENDU=auto/' /etc/default/panneau
echo "OK : réinstallation sans adresse"

# Kiosque lancé pour de vrai, sans écran
mkdir -p /srv/tableau-maison/accueil
cat >/srv/tableau-maison/accueil/index.html <<'HTML'
<html><head><title>Maison</title></head>
<body><h1>Tout va bien</h1><script>fetch('/rendu-ok')</script></body></html>
HTML
python3 -m http.server 8123 --bind 127.0.0.1 --directory /srv >/tmp/http.log 2>&1 &
mkdir -p /run/xdg && chown kiosk /run/xdg && chmod 700 /run/xdg
su kiosk -s /bin/bash -c 'set -a; . /etc/default/panneau; set +a
  export XDG_RUNTIME_DIR=/run/xdg WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 HOME=/home/kiosk
  timeout 60 /usr/local/bin/panneau-lancer' >/tmp/kiosque.log 2>&1 &
for _ in $(seq 1 60); do
  grep -q "GET /rendu-ok" /tmp/http.log && break
  sleep 1
done
if ! grep -q "GET /rendu-ok" /tmp/http.log; then
  {
    set +e  # diagnostic : tout afficher, même si une commande échoue
    echo "--- /dev/dri :"; ls -l /dev/dri 2>/dev/null || echo "absent"
    echo "--- espaces de noms utilisateur (bac à sable de Chromium) :"
    cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>/dev/null || echo "pas de restriction AppArmor"
    echo "--- processus :"  # pas de ps dans les images minimales
    for p in /proc/[0-9]*; do tr '\0' ' ' <"$p/cmdline" 2>/dev/null; echo; done |
      grep -E "^(cage|/usr/lib/chromium|Xwayland)" | cut -c1-150
    echo "--- requêtes reçues :"; cat /tmp/http.log
    echo "--- fin du journal du kiosque :"; tail -n 30 /tmp/kiosque.log | cut -c1-220
    echo "--- Chromium seul, hors de Cage :"
    su kiosk -s /bin/bash -c "HOME=/tmp timeout 30 $nav --headless=new --disable-gpu --dump-dom \
      http://127.0.0.1:8123/tableau-maison/accueil/" 2>&1 | tail -n 5 | cut -c1-220
  } >&2
  echec "le tableau de bord n'a pas été affiché par le kiosque"
fi
echo "OK : kiosque lancé, tableau de bord affiché (Cage + $(basename "$nav")) ; $(grep -h "^Panneau" /tmp/kiosque.log | tr '\n' ' ')"
