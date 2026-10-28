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
# n'expose aucun nœud de rendu graphique, ou si le rendu matériel échoue au démarrage (nœud présent
# mais pilote inutilisable).
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
echo "Panneau : rendu $rendu" >&2  # visible dans journalctl -u panneau
if [[ "$rendu" == logiciel || "${RENDU:-auto}" != auto ]]; then
  exec cage -d -- "$NAVIGATEUR" "${flags[@]}" "$URL"
fi

# Rendu matériel choisi automatiquement : Cage qui s'arrête en erreur dès le démarrage = pilote
# inutilisable -> nouvel essai en rendu logiciel, au lieu d'un écran noir qui redémarre en boucle.
debut=$SECONDS
code=0
cage -d -- "$NAVIGATEUR" "${flags[@]}" "$URL" || code=$?
if ((code != 0 && SECONDS - debut < ${PANNEAU_DELAI_ECHEC:-20})); then
  echo "Panneau : rendu matériel impossible (code $code), passage au rendu logiciel" >&2
  export WLR_RENDERER=pixman
  exec cage -d -- "$NAVIGATEUR" "${flags[@]}" --disable-gpu "$URL"
fi
exit "$code"
