#!/usr/bin/env bash
# Avant chaque lancement du panneau : après une coupure de courant, Chromium affiche « Restaurer les
# pages ? » par-dessus le tableau de bord (et sans clavier, impossible de le fermer). On marque la
# session précédente comme fermée proprement. Lancé par panneau.service (ExecStartPre).
set -euo pipefail
prefs="${1:-${HOME:-/home/kiosk}/.config/chromium/Default/Preferences}"
[[ -f "$prefs" ]] || exit 0
sed -i -e 's/"exited_cleanly":[[:space:]]*false/"exited_cleanly":true/g' \
       -e 's/"exit_type":[[:space:]]*"[^"]*"/"exit_type":"Normal"/g' "$prefs"
