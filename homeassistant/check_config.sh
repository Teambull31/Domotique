#!/usr/bin/env bash
# Vérifie les packages HA avec le vrai validateur de Home Assistant (schémas d'automatisations,
# déclencheurs, templates). Usage : homeassistant/check_config.sh   (prérequis : pip install homeassistant)
# check_config renvoie 0 même si une automatisation est invalide : on cherche donc les erreurs dans sa sortie.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cfg="$(mktemp -d)"
trap 'rm -rf "$cfg"' EXIT
mkdir -p "$cfg/packages"
cp "$here"/packages/*.yaml "$cfg/packages/"
mkdir -p "$cfg/custom_templates" && cp "$here"/custom_templates/*.jinja "$cfg/custom_templates/"
printf 'homeassistant:\n  packages: !include_dir_named packages\n' > "$cfg/configuration.yaml"
out="$(hass --script check_config -c "$cfg" 2>&1 || true)"
if grep -E "ERROR|Invalid config|Failed config" <<<"$out"; then
  echo "Configuration Home Assistant invalide" >&2
  exit 1
fi
echo "Packages Home Assistant valides"
