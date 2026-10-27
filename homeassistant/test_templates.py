"""Exécute les templates du package "tranquillité" avec le vrai moteur de Home Assistant.

Usage : python homeassistant/test_templates.py   (prérequis : pip install homeassistant)
check_config ne valide que la forme du YAML ; ce test vérifie le résultat des templates.
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

PACKAGE = yaml.safe_load((Path(__file__).parent / "packages" / "senior_care.yaml").read_text(encoding="utf-8"))
GROUPS = {name: cfg["entities"] for name, cfg in PACKAGE["group"].items()}


def activity_template() -> str:
    for block in PACKAGE["template"]:
        for sensor in block.get("binary_sensor", []):
            if sensor.get("unique_id") == "senior_activite_recente":
                return sensor["state"]
    sys.exit("binary_sensor senior_activite_recente introuvable")


def automation(alias_start: str) -> dict:
    for auto in PACKAGE["automation"]:
        if auto["alias"].startswith(alias_start):
            return auto
    sys.exit(f"automatisation '{alias_start}' introuvable")


def trigger_template(auto: dict, trigger_id: str) -> str:
    return next(t["value_template"] for t in auto["trigger"] if t.get("id") == trigger_id)


class Checker:
    def __init__(self, hass: HomeAssistant):
        self.hass, self.failures = hass, 0

    def states(self, values: dict[str, str]) -> None:
        for entity_id, state in values.items():
            self.hass.states.async_set(entity_id, state, {"friendly_name": entity_id.split(".")[1]})
        for group, members in GROUPS.items():  # expand() lit l'attribut entity_id des groupes
            self.hass.states.async_set(f"group.{group}", "off", {"entity_id": members})

    def check(self, name: str, template: str, expected, variables=None) -> None:
        result = Template(template, self.hass).async_render(variables or {})
        if isinstance(expected, str):
            ok = expected in str(result)
        else:
            ok = result == expected
        self.failures += not ok
        print(f"{'OK   ' if ok else 'ÉCHEC'} {name} : {str(result).strip()[:90]!r} (attendu {expected!r})")


async def main() -> int:
    hass = HomeAssistant(tempfile.mkdtemp())
    c = Checker(hass)
    now = dt_util.utcnow()
    presence = GROUPS["senior_presence"]

    # --- Activité récente (alerte d'inactivité) ---
    tpl = activity_template()
    for name, states, last, expected in [
        ("activité : installation neuve", ["off", "off", "off"], "unknown", True),
        ("activité : quelqu'un au salon maintenant", ["on", "off", "off"], (now - timedelta(hours=20)).isoformat(), True),
        ("activité : dernière il y a 2 h", ["off", "off", "off"], (now - timedelta(hours=2)).isoformat(), True),
        ("activité : dernière il y a 9 h, radar indisponible", ["off", "unavailable", "off"],
         (now - timedelta(hours=9)).isoformat(), False),
    ]:
        c.states({"input_number.senior_inactivity_hours": "8", "sensor.derniere_activite_a_la_maison": last,
                  **dict(zip(presence, states))})
        c.check(name, tpl, expected)

    # --- Capteurs à vérifier ---
    faults = automation("Senior - capteur muet")
    piles = GROUPS["senior_piles"]
    c.states({piles[0]: "85", piles[1]: "15", piles[2]: "unavailable"})
    c.check("pile : une pile à 15 % déclenche", trigger_template(faults, "pile"), True)
    c.check("pile : le message nomme la bonne pile", faults["action"][0]["data"]["message"],
            f"{piles[1].split('.')[1]} (15 %)", {"trigger": {"id": "pile"}})
    c.states({piles[0]: "85", piles[1]: "100", piles[2]: "unavailable"})
    c.check("pile : 'indisponible' n'est pas une pile faible", trigger_template(faults, "pile"), False)
    c.check("pile : 100 % ne déclenche pas (pas de comparaison de texte)", trigger_template(faults, "pile"), False)

    defauts = GROUPS["senior_defauts"]
    c.states({d: "off" for d in defauts})
    c.check("défaut : aucun défaut", trigger_template(faults, "defaut"), False)
    c.states({**{d: "off" for d in defauts}, defauts[0]: "on"})
    c.check("défaut : un radar en défaut déclenche", trigger_template(faults, "defaut"), True)

    # --- Chaleur / froid ---
    temps = automation("Senior - température")
    t = GROUPS["senior_temperatures"]
    c.states({t[0]: "29.5", t[1]: "21"})
    c.check("chaud : 29,5 °C déclenche", trigger_template(temps, "chaud"), True)
    c.check("chaud : le message cite la pièce et la valeur", temps["action"][0]["data"]["message"],
            "29.5 °C", {"trigger": {"id": "chaud"}})
    c.check("froid : rien sous 17 °C", trigger_template(temps, "froid"), False)
    c.states({t[0]: "unavailable", t[1]: "16"})
    c.check("froid : 16 °C déclenche, capteur indisponible ignoré", trigger_template(temps, "froid"), True)

    await hass.async_stop(force=True)
    print("TOUS OK" if not c.failures else f"{c.failures} ÉCHEC(S)")
    return 1 if c.failures else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
