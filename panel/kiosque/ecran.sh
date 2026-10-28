#!/usr/bin/env bash
# Luminosité du panneau : ecran.sh jour | nuit | <0-100>
# Fonctionne avec les écrans à rétroéclairage pilotable (/sys/class/backlight : écrans DSI, dont
# l'écran officiel Raspberry Pi, écrans à nappe des cartes ARM), quelle que soit la carte.
# Écran HDMI : réglage sur l'écran lui-même.
set -euo pipefail

case "${1:-}" in
  jour) pct=100 ;;
  nuit) pct=15 ;;
  ''|*[!0-9]*) echo "Usage : $0 jour|nuit|<0-100>" >&2; exit 1 ;;
  *) pct=$1 ;;
esac

found=0
for bl in /sys/class/backlight/*; do
  [[ -e "$bl/max_brightness" ]] || continue
  max=$(cat "$bl/max_brightness")
  val=$(( max * pct / 100 ))
  if (( pct > 0 && val == 0 )); then val=1; fi  # écran à peu de niveaux : jamais éteint par l'arrondi
  echo "$val" > "$bl/brightness"
  found=1
done
if [[ $found -eq 0 ]]; then
  echo "Pas de rétroéclairage pilotable (écran HDMI ?) : rien à faire." >&2
fi
