"""Exécute la logique de l'alerte d'inactivité avec le vrai moteur de templates de Home Assistant.

Usage : python homeassistant/test_templates.py   (prérequis : pip install homeassistant)
check_config ne valide que la forme du YAML ; ce test vérifie le résultat du template.
"""
import asyncio
import sys
import tempfile
from datetime import timedelta
from pathlib import Path

import yaml
from homeassistant.core import HomeAssistant
from homeassistant.helpers.template import Template
import homeassistant.util.dt as dt_util

PACKAGE = Path(__file__).parent / "packages" / "senior_care.yaml"
ROOMS = ["salon", "chambre", "cuisine"]


def activity_template() -> str:
    pkg = yaml.safe_load(PACKAGE.read_text(encoding="utf-8"))
    for block in pkg["template"]:
        for sensor in block.get("binary_sensor", []):
            if sensor.get("unique_id") == "senior_activite_recente":
                return sensor["state"]
    sys.exit("binary_sensor senior_activite_recente introuvable dans le package")


async def main() -> int:
    hass = HomeAssistant(tempfile.mkdtemp())
    tpl = activity_template()
    now = dt_util.utcnow()
    cases = [
        ("installation neuve, jamais d'activité", ["off", "off", "off"], "unknown", True),
        ("quelqu'un dans le salon maintenant", ["on", "off", "off"], (now - timedelta(hours=20)).isoformat(), True),
        ("dernière activité il y a 2 h", ["off", "off", "off"], (now - timedelta(hours=2)).isoformat(), True),
        ("dernière activité il y a 9 h, capteur indisponible", ["off", "unavailable", "off"],
         (now - timedelta(hours=9)).isoformat(), False),
    ]
    failures = 0
    for name, states, last, expected in cases:
        hass.states.async_set("input_number.senior_inactivity_hours", "8")
        for room, state in zip(ROOMS, states):
            hass.states.async_set(f"binary_sensor.presence_{room}_presence", state)
        hass.states.async_set("sensor.derniere_activite_a_la_maison", last)
        result = Template(tpl, hass).async_render()
        ok = result == expected
        failures += not ok
        print(f"{'OK   ' if ok else 'ÉCHEC'} {name} : {result} (attendu {expected})")
    await hass.async_stop(force=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
