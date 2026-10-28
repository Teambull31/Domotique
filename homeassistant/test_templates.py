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


def template_sensor(unique_id: str) -> dict:
    for block in PACKAGE["template"]:
        for sensor in block.get("sensor", []):
            if sensor.get("unique_id") == unique_id:
                return sensor
    sys.exit(f"capteur {unique_id} introuvable")


def trigger_template(auto: dict, trigger_id: str) -> str:
    return next(t["value_template"] for t in auto["trigger"] if t.get("id") == trigger_id)


class Checker:
    def __init__(self, hass: HomeAssistant):
        self.hass, self.failures = hass, 0

    def states(self, values: dict[str, str], ago: dict[str, float] | None = None) -> None:
        """ago : depuis combien de secondes l'entité est dans cet état (last_changed)."""
        for entity_id, state in values.items():
            ts = dt_util.utcnow().timestamp() - (ago or {}).get(entity_id, 0)
            self.hass.states.async_set(entity_id, state, {"friendly_name": entity_id.split(".")[1]},
                                       timestamp=ts)
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

    # --- Capteurs à vérifier (compteurs : une alerte par nouveau capteur en problème) ---
    piles_s, muets_s, defauts_s = (template_sensor(u) for u in
                                   ("senior_piles_faibles", "senior_capteurs_muets", "senior_capteurs_en_defaut"))
    piles = GROUPS["senior_piles"]
    c.states({piles[0]: "85", piles[1]: "15", piles[2]: "unavailable"})
    c.check("piles : une pile à 15 % comptée", piles_s["state"], 1)
    c.check("piles : le nom et le niveau sont listés", piles_s["attributes"]["noms"], f"{piles[1].split('.')[1]} (15 %)")
    c.states({piles[0]: "100", piles[1]: "9", piles[2]: "18"})
    c.check("piles : 2 faibles, '100' non compté (pas de comparaison de texte)", piles_s["state"], 2)

    capteurs = GROUPS["senior_capteurs_pile"]
    c.states({capteurs[0]: "unavailable", capteurs[1]: "unavailable", capteurs[2]: "off"},
             ago={capteurs[0]: 2 * 3600, capteurs[1]: 600})
    c.check("muets : seul le capteur indisponible depuis plus d'1 h compte", muets_s["state"], 1)

    defauts = GROUPS["senior_defauts"]
    c.states({**{d: "off" for d in defauts}, defauts[0]: "on", defauts[1]: "on"},
             ago={defauts[0]: 3600, defauts[1]: 60})
    c.check("défauts : seul le défaut de plus de 10 min compte", defauts_s["state"], 1)

    faults = automation("Senior - capteur muet")
    cond = faults["condition"][0]["value_template"]

    def trig(entity, frm, to, noms=""):
        hass.states.async_set(entity, to, {"noms": noms})
        to_state = hass.states.get(entity)
        hass.states.async_set(entity, frm)
        return {"trigger": {"entity_id": entity, "from_state": hass.states.get(entity), "to_state": to_state}}

    c.check("alerte : 2e pile faible (1 -> 2) notifiée", cond, True, trig("sensor.piles_faibles", "1", "2"))
    c.check("alerte : pile remplacée (2 -> 1) silencieuse", cond, False, trig("sensor.piles_faibles", "2", "1"))
    c.check("alerte : redémarrage de HA (inconnu -> 1) silencieux", cond, False,
            trig("sensor.piles_faibles", "unknown", "1"))
    c.check("alerte : message", faults["action"][0]["data"]["message"], "Pile faible : porte (12 %)",
            trig("sensor.piles_faibles", "0", "1", "porte (12 %)"))

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

    # --- Fuite : alerte collante si une sonde se tait pendant la fuite ---
    leak_tpl = next(sensor["state"] for block in PACKAGE["template"] for sensor in block.get("binary_sensor", [])
                    if sensor.get("unique_id") == "senior_fuite_quelque_part")
    sondes = GROUPS["senior_fuites"]
    fuite = "binary_sensor.fuite_d_eau_quelque_part"

    def leak(states, current):
        c.states({**dict(zip(sondes, states)), fuite: current})
        return {"this": hass.states.get(fuite)}

    c.check("fuite : une sonde mouillée déclenche", leak_tpl, True, leak(["on", "off"], "off"))
    c.check("fuite : sonde muette pendant la fuite -> alerte maintenue", leak_tpl, True,
            leak(["unavailable", "off"], "on"))
    c.check("fuite : sonde muette sans fuite en cours -> pas d'alerte", leak_tpl, False,
            leak(["unavailable", "off"], "off"))
    c.check("fuite : toutes les sondes sèches -> fin d'alerte", leak_tpl, False, leak(["off", "off"], "on"))

    # --- Bilan quotidien ---
    report = automation("Senior - bilan quotidien")["action"][0]["data"]["message"]
    c.states({"sensor.presence_detectee_aujourd_hui": "6.5", "sensor.ouvertures_de_la_porte_aujourd_hui": "3",
              "sensor.derniere_activite_a_la_maison": (now - timedelta(minutes=5)).isoformat(),
              "input_boolean.senior_alerts_enabled": "on", t[0]: "20.5", t[1]: "unavailable"})
    c.check("bilan : durée de présence en h/min", report, "6 h 30")
    c.check("bilan : nombre d'ouvertures de porte", report, "ouverte 3 fois")
    c.check("bilan : température disponible citée", report, "20.5 °C")
    c.states({"sensor.presence_detectee_aujourd_hui": "unknown", "sensor.derniere_activite_a_la_maison": "unknown"})
    c.check("bilan : sans données, pas d'erreur", report, "Dernière activité : inconnue")

    await hass.async_stop(force=True)
    print("TOUS OK" if not c.failures else f"{c.failures} ÉCHEC(S)")
    return 1 if c.failures else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
