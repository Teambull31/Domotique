#!/usr/bin/env bash
# Lance le tableau de bord en plein écran : Cage (compositeur Wayland, sans bureau) + Chromium en
# mode kiosque. Lancé par panneau.service ; réglages dans /etc/default/panneau (URL, NAVIGATEUR, RENDU).
# Indépendant de la carte : Raspberry Pi 3/4/5, Orange Pi et autres cartes ARM sous Debian/Armbian.
# Essai sans écran : PANNEAU_ESSAI=1 affiche la commande au lieu de la lancer.
set -euo pipefail

: "${URL:?URL absente de /etc/default/panneau}"
NAVIGATEUR="${NAVIGATEUR:-$(command -v chromium-browser || command -v chromium || echo chromium)}"

# Rendu : « gpu » (accélération graphique), « logiciel » (carte dont l'image n'a pas de pilote 3D,
# fréquent sur les cartes ARM hors Raspberry Pi : sinon écran noir), « auto » = logiciel si la carte
# n'expose aucun nœud de rendu graphique.
rendu="${RENDU:-auto}"
if [[ "$rendu" == auto ]]; then
  rendu=gpu
  compgen -G "${PANNEAU_DRI:-/dev/dri}/renderD*" >/dev/null || rendu=logiciel
fi

flags=(--kiosk --noerrdialogs --disable-infobars --no-first-run --password-store=basic
       --ozone-platform=wayland --check-for-update-interval=31536000 --disable-features=Translate
       --overscroll-history-navigation=0 --hide-crash-restore-bubble)
# Curseur matériel défaillant avec certains pilotes d'affichage ARM (curseur figé ou invisible) ;
# sans effet sur l'usage au doigt.
export WLR_NO_HARDWARE_CURSORS=1
if [[ "$rendu" == logiciel ]]; then
  export WLR_RENDERER=pixman
  flags+=(--disable-gpu)
fi

if [[ -n "${PANNEAU_ESSAI:-}" ]]; then
  echo "rendu=$rendu WLR_RENDERER=${WLR_RENDERER:-} cage -d -- $NAVIGATEUR ${flags[*]} $URL"
  exit 0
fi
exec cage -d -- "$NAVIGATEUR" "${flags[@]}" "$URL"
