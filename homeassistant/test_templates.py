"""Exécute les templates du package "tranquillité" avec le vrai moteur de Home Assistant.

Usage : python homeassistant/test_templates.py   (prérequis : pip install homeassistant)
check_config ne valide que la forme du YAML ; ce test vérifie le résultat des templates.
"""
import asyncio
import inspect
import json
import shutil
import sys
import tempfile
from datetime import timedelta
from pathlib import Path
from unittest.mock import patch

import yaml
from homeassistant.core import HomeAssistant, State
from homeassistant.config_entries import ConfigEntries, ConfigEntry
from homeassistant.helpers import config_validation as cv, device_registry as dr, entity_registry as er
from homeassistant.helpers.entity import entity_sources
from homeassistant.helpers.template import Template, async_load_custom_templates
import homeassistant.util.dt as dt_util

HERE = Path(__file__).parent
PACKAGE = yaml.safe_load((HERE / "packages" / "senior_care.yaml").read_text(encoding="utf-8"))
MAISON = yaml.safe_load((HERE / "packages" / "maison.yaml").read_text(encoding="utf-8"))
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
        self.verify(name, Template(template, self.hass).async_render(variables or {}), expected)

    def verify(self, name: str, result, expected) -> None:
        if isinstance(expected, str):
            ok = expected in str(result)
        else:
            ok = result == expected
        self.failures += not ok
        print(f"{'OK   ' if ok else 'ÉCHEC'} {name} : {str(result).strip()[:90]!r} (attendu {expected!r})")


async def main() -> int:
    cfgdir = tempfile.mkdtemp()
    shutil.copytree(HERE / "custom_templates", Path(cfgdir) / "custom_templates")
    hass = HomeAssistant(cfgdir)
    await async_load_custom_templates(hass)  # macros (maison.jinja) comme dans un vrai HA
    await dr.async_load(hass)
    await er.async_load(hass)
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
    # Nœuds repérés via l'intégration MQTT (entity_sources, rempli dans un vrai HA par les entités)
    hass.config_entries = ConfigEntries(hass, {})
    sources = entity_sources(hass)

    def noeuds(valeurs, ago=None):
        """valeurs : {entity_id: (état, device_class)} ; entités des nœuds (MQTT) sauf « telephone »."""
        sources.clear()
        for e in list(hass.states.async_entity_ids()):
            hass.states.async_remove(e)
        for eid, (etat, classe) in valeurs.items():
            if "telephone" not in eid:
                sources[eid] = {"domain": "mqtt", "custom_component": False}
            ts = dt_util.utcnow().timestamp() - (ago or {}).get(eid, 0)
            hass.states.async_set(eid, etat, {"friendly_name": eid.split(".")[1], "device_class": classe},
                                  timestamp=ts)

    piles_s, muets_s, defauts_s = (template_sensor(u) for u in
                                   ("senior_piles_faibles", "senior_capteurs_muets", "senior_capteurs_en_defaut"))
    noeuds({"sensor.door_entree_batterie": ("85", "battery"), "sensor.leak_cuisine_batterie": ("15", "battery"),
            "sensor.call_chambre_batterie": ("unavailable", "battery"),
            "sensor.telephone_du_proche_batterie": ("8", "battery")})  # téléphone : jamais compté
    c.check("piles : une pile à 15 % comptée, pas le téléphone du proche", piles_s["state"], 1)
    c.check("piles : le nom et le niveau sont listés", piles_s["attributes"]["noms"], "leak_cuisine_batterie (15 %)")
    noeuds({"sensor.door_entree_batterie": ("100", "battery"), "sensor.leak_cuisine_batterie": ("9", "battery"),
            "sensor.call_chambre_batterie": ("18", "battery")})
    c.check("piles : 2 faibles, '100' non compté (pas de comparaison de texte)", piles_s["state"], 2)
    c.check("piles : repérées sans groupe à remplir (nouveau capteur)", piles_s["attributes"]["noms"],
            "call_chambre_batterie (18 %)")

    noeuds({"sensor.door_entree_batterie": ("unavailable", "battery"),
            "sensor.leak_cuisine_batterie": ("unavailable", "battery"),
            "sensor.call_chambre_batterie": ("60", "battery"),
            "binary_sensor.climate_salon_defaut_capteur": ("unavailable", "problem")},  # secteur : pas compté
           ago={"sensor.door_entree_batterie": 2 * 3600, "sensor.leak_cuisine_batterie": 600,
                "binary_sensor.climate_salon_defaut_capteur": 2 * 3600})
    c.check("muets : seul le capteur sur pile indisponible depuis plus d'1 h compte", muets_s["state"], 1)
    c.check("muets : le capteur est nommé", muets_s["attributes"]["noms"], "door_entree_batterie")

    # Cas réel : nœud = appareil HA (pile + ouverture) ; muet seulement si TOUTES ses entités le sont
    params = inspect.signature(ConfigEntry.__init__).parameters
    entree = ConfigEntry(**{k: v for k, v in dict(
        domain="mqtt", title="MQTT", data={}, source="user", version=1, minor_version=1, options={},
        unique_id=None, discovery_keys={}, subentries_data=None).items() if k in params})
    hass.config_entries._entries[entree.entry_id] = entree
    noeuds({})  # registre d'abord : un état déjà présent ferait créer « ..._2 »
    appareil = dr.async_get(hass).async_get_or_create(config_entry_id=entree.entry_id,
                                                      identifiers={("mqtt", "door-entree")}, name="door-entree")
    for eid in ("sensor.door_entree_batterie", "binary_sensor.door_entree_ouverture"):
        domaine, objet = eid.split(".")
        er.async_get(hass).async_get_or_create(domaine, "mqtt", eid, suggested_object_id=objet,
                                               device_id=appareil.id, config_entry=entree)
    noeuds({"sensor.door_entree_batterie": ("unavailable", "battery"),
            "binary_sensor.door_entree_ouverture": ("unavailable", "door")},
           ago={"sensor.door_entree_batterie": 2 * 3600, "binary_sensor.door_entree_ouverture": 2 * 3600})
    c.check("muets : nœud muet nommé par son appareil", muets_s["attributes"]["noms"], "door-entree")
    noeuds({"sensor.door_entree_batterie": ("unavailable", "battery"),
            "binary_sensor.door_entree_ouverture": ("off", "door")},
           ago={"sensor.door_entree_batterie": 2 * 3600})
    c.check("muets : une entité du nœud répond encore -> pas muet", muets_s["state"], 0)

    noeuds({"binary_sensor.presence_salon_defaut_radar": ("on", "problem"),
            "binary_sensor.gate_entree_manoeuvre_non_confirmee": ("on", "problem"),
            "binary_sensor.heating_salon_defaut_capteur": ("off", "problem"),
            "binary_sensor.door_entree_ouverture": ("on", "door")},
           ago={"binary_sensor.presence_salon_defaut_radar": 3600,
                "binary_sensor.gate_entree_manoeuvre_non_confirmee": 60})
    c.check("défauts : seul le défaut de plus de 10 min compte (porte ouverte : pas un défaut)",
            defauts_s["state"], 1)
    c.check("défauts : le capteur est nommé", defauts_s["attributes"]["noms"], "presence_salon_defaut_radar")
    sources.clear()

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

    # --- Inactivité à signaler (plage 9 h - 21 h) et alerte répétée ---
    inact = next(sensor["state"] for block in PACKAGE["template"] for sensor in block.get("binary_sensor", [])
                 if sensor.get("unique_id") == "senior_inactivite_a_signaler")
    tz = dt_util.get_default_time_zone()

    def at(hour, activite="off", alertes="on"):
        c.states({"binary_sensor.activite_recente_a_la_maison": activite,
                  "input_boolean.senior_alerts_enabled": alertes})
        return patch("homeassistant.util.dt.now", return_value=dt_util.now(tz).replace(hour=hour, minute=0))

    for name, hour, act, al, expected in [
        ("inactivité : 14 h sans activité -> alerte", 14, "off", "on", True),
        ("inactivité : 22 h sans activité -> rien la nuit", 22, "off", "on", False),
        ("inactivité : 8 h -> rien avant 9 h", 8, "off", "on", False),
        ("inactivité : alertes coupées (absence) -> rien", 14, "off", "off", False),
        ("inactivité : activité détectée -> rien", 14, "on", "on", False),
    ]:
        with at(hour, act, al):
            c.check(name, inact, expected)
    alert_cfg = PACKAGE["alert"]["inactivite"]
    c.states({"sensor.derniere_activite_a_la_maison": (now - timedelta(hours=9)).isoformat(),
              "input_number.senior_inactivity_hours": "8"})
    c.check("inactivité : message avec durée", alert_cfg["message"], "depuis 8 h")
    c.states({"binary_sensor.activite_recente_a_la_maison": "off"})
    c.check("inactivité : fin à 21 h sans activité -> le dire", alert_cfg["done_message"], "toujours aucune activité")
    c.states({"binary_sensor.activite_recente_a_la_maison": "on"})
    c.check("inactivité : fin par activité -> rassurer", alert_cfg["done_message"], "Activité de nouveau détectée")

    # --- Bouton d'appel ---
    appel_msg = automation("Senior - bouton d'appel")["action"][1]["data"]["message"]
    boutons = GROUPS["senior_boutons_appel"]
    c.states({boutons[0]: "on"})
    c.check("appel : la notification nomme le bouton", appel_msg, boutons[0].split(".")[1])
    c.states({boutons[0]: "off"})
    c.check("appel : nom inconnu -> libellé par défaut", appel_msg, "Bouton d'appel")
    c.states({"input_boolean.senior_appel_en_cours": "on"}, ago={"input_boolean.senior_appel_en_cours": 600})
    annule = PACKAGE["script"]["senior_appel_annule"]["sequence"]
    c.verify("appel annulé : seulement si un appel est en cours", annule[0]["state"], "on")
    c.verify("appel annulé : le proche est prévenu", annule[2]["service"], "notify.proche_aidant")
    c.check("appel annulé : message avec l'heure", annule[2]["data"]["message"], "touché sur l'écran de la maison à")
    c.check("appel annulé : la fin d'alerte ne prétend pas « pris en compte »",
            PACKAGE["alert"]["appel"]["done_message"], "Appel à l'aide terminé")
    c.check("appel : le rappel donne l'heure de l'appui", PACKAGE["alert"]["appel"]["message"],
            dt_util.as_local(dt_util.utcnow() - timedelta(seconds=600)).strftime("%H:%M"))

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
    c.check("bilan : aucun capteur trouvé -> le dire (intégration MQTT en panne)", report, "Aucun capteur trouvé")
    for eid in ("sensor.leak_cuisine_batterie", "sensor.door_entree_batterie",
                "binary_sensor.presence_salon_defaut_radar"):
        sources[eid] = {"domain": "mqtt", "custom_component": False}
    c.states({"sensor.leak_cuisine_batterie": "80", "sensor.door_entree_batterie": "95",
              "binary_sensor.presence_salon_defaut_radar": "off"})
    for eid, classe in (("sensor.leak_cuisine_batterie", "battery"), ("sensor.door_entree_batterie", "battery"),
                        ("binary_sensor.presence_salon_defaut_radar", "problem")):
        etat = hass.states.get(eid)
        hass.states.async_set(eid, etat.state, {**etat.attributes, "device_class": classe})
    c.check("bilan : rien à vérifier -> le dire", report, "Capteurs : tous fonctionnent")
    hass.states.async_set("sensor.leak_cuisine_batterie", "12", {"device_class": "battery",
                                                                 "friendly_name": "leak_cuisine_batterie"})
    hass.states.async_set("binary_sensor.presence_salon_defaut_radar", "on",
                          {"device_class": "problem", "friendly_name": "presence_salon_defaut_radar"},
                          timestamp=dt_util.utcnow().timestamp() - 3600)
    c.check("bilan : rappel chaque soir de la pile faible", report, "pile faible : leak_cuisine_batterie (12 %)")
    c.check("bilan : rappel du défaut", report, "en défaut : presence_salon_defaut_radar")
    c.check("bilan : une information par ligne", report, "Dernière activité : inconnue.\nTempératures")
    hass.states.async_set("input_boolean.senior_alerts_enabled", "off")
    c.check("bilan : alertes coupées rappelées", report, "\nAlertes désactivées.")
    sources.clear()

    # --- Package maison : coût et état de la maison ---
    maison_sensors = {s["unique_id"]: s for s in MAISON["template"][0]["sensor"]}
    c.states({"sensor.electricite_aujourd_hui": "12.4", "input_number.maison_tarif_kwh": "0.25"})
    c.check("maison : coût du jour = kWh x tarif", maison_sensors["maison_cout_jour"]["state"], 3.1)
    c.states({"sensor.eau_aujourd_hui": "143", "input_number.maison_tarif_eau": "4.3"})
    c.check("maison : coût de l'eau = litres / 1000 x tarif au m³", maison_sensors["maison_cout_eau_jour"]["state"],
            0.61)
    c.states({"sensor.eau_aujourd_hui": "unavailable"})
    c.check("maison : pas de compteur d'eau -> coût indisponible",
            maison_sensors["maison_cout_eau_jour"]["availability"], False)
    tarif_eau = next(a for a in MAISON["automation"] if a["alias"] == "Maison - tarif de l'eau par défaut")
    c.states({"input_number.maison_tarif_eau": "0"})
    c.check("maison : tarif de l'eau jamais saisi -> valeur par défaut", tarif_eau["condition"][0]["value_template"],
            True)
    c.states({"input_number.maison_tarif_eau": "3.85"})
    c.check("maison : tarif de l'eau saisi conservé", tarif_eau["condition"][0]["value_template"], False)

    def maison(states):
        for e in list(hass.states.async_entity_ids()):  # repartir d'une maison vide
            hass.states.async_remove(e)
        for entity_id, (state, attrs) in states.items():
            hass.states.async_set(entity_id, state, {"friendly_name": entity_id.split(".")[1], **attrs})

    # appareils connectés : entités déclarées comme créées par l'intégration MQTT (dans un vrai HA,
    # integration_entities('mqtt') lit entity_sources, rempli par les entités des nœuds)
    for eid in ("sensor.climate_salon_temperature", "binary_sensor.door_entree_ouverture"):
        sources[eid] = {"domain": "mqtt", "custom_component": False}
    maison({"sensor.climate_salon_temperature": ("21", {"device_class": "temperature"}),
            "binary_sensor.door_entree_ouverture": ("unavailable", {"device_class": "door"})})
    appareils_s = maison_sensors["maison_appareils"]
    c.check("appareils : 1 en ligne, 1 hors ligne", appareils_s["state"], "1 en ligne, 1 hors ligne")
    # nommé par son appareil (enregistré plus haut, comme dans un vrai HA)
    c.check("appareils : l'appareil muet est nommé", appareils_s["attributes"]["hors_ligne"], "door-entree")
    c.check("maison : appareil hors ligne signalé", maison_sensors["maison_etat"]["attributes"]["points"],
            "Hors ligne : door-entree")
    hass.states.async_set("binary_sensor.door_entree_ouverture", "off", {"device_class": "door"})
    c.check("appareils : tous en ligne", appareils_s["state"], "2 en ligne")
    sources.clear()

    nuit = next(a for a in MAISON["automation"] if a["alias"] == "Maison - portail ouvert la nuit")
    maison({"cover.gate_entree_portail": ("closed", {"device_class": "gate"}),
            "cover.shutters_salon_volet": ("open", {"device_class": "shutter"})})
    c.check("portail la nuit : fermé (et volet ouvert) -> rien", nuit["trigger"][0]["value_template"], False)
    c.check("portail la nuit : rappel de 22 h 30 sans objet si fermé", nuit["condition"][1]["value_template"], False)
    maison({"cover.gate_entree_portail": ("opening", {"device_class": "gate"})})
    c.check("portail la nuit : en ouverture -> alerte", nuit["trigger"][0]["value_template"], True)
    c.check("portail la nuit : rappel de 22 h 30 si ouvert", nuit["condition"][1]["value_template"], True)
    nuit_vars, nuit_notif = nuit["action"][0]["variables"], nuit["action"][1]["data"]

    def nuit_trigger(trigger):
        variables = {"trigger": trigger}
        variables |= {k: Template(v, hass).async_render(variables) for k, v in nuit_vars.items()}
        return variables

    ouverture = nuit_trigger({"id": "ouverture", "from_state": State("cover.gate_entree_portail", "closed")})
    c.check("portail la nuit : ouverture -> urgent", nuit_notif["urgent"], True, ouverture)
    c.check("portail la nuit : ouverture -> heure citée", nuit_notif["message"], "gate_entree_portail s'est ouvert à", ouverture)
    for nom, trig in (("rappel de 22 h 30", {"id": "coucher"}),
                      ("nœud revenu en ligne", {"id": "ouverture",
                                                "from_state": State("cover.gate_entree_portail", "unavailable")}),
                      ("entité nouvelle", {"id": "ouverture", "from_state": None})):
        rappel = nuit_trigger(trig)
        c.check(f"portail la nuit : {nom} -> pas urgent", nuit_notif["urgent"], False, rappel)
        c.check(f"portail la nuit : {nom} -> rappel", nuit_notif["message"], "est ouvert : penser à le fermer", rappel)

    # Position du portail transmise au nœud : JSON valide une fois passé par mqtt.publish (cv.string)
    transmise = next(a for a in MAISON["automation"] if a["alias"].startswith("Maison - position du portail"))
    payload = transmise["action"][0]["data"]["payload"]
    for etat_porte, attendu in (("off", True), ("on", False), ("unavailable", "unknown")):
        maison({"binary_sensor.door_portail_ouverture": (etat_porte, {"device_class": "door"})})
        rendu = cv.string(Template(payload, hass).async_render())
        c.verify(f"position portail : capteur {etat_porte} -> {attendu}", json.loads(rendu)["closed_ext"], attendu)

    notif = MAISON["script"]["maison_notifier"]["sequence"][0]["data"]["data"]
    c.check("notifier : urgent -> sonne en silencieux", notif, "alarm_stream", {"urgent": True})
    c.check("notifier : normal -> notification simple", notif, {}, {"urgent": False})
    c.check("notifier : 'urgent' absent -> notification simple", notif, {})

    etat = maison_sensors["maison_etat"]
    maison({"binary_sensor.leak_cuisine_fuite": ("off", {"device_class": "moisture"}),
            "cover.gate_entree_portail": ("closed", {"device_class": "gate"}),
            "sensor.door_entree_batterie": ("80", {"device_class": "battery"})})
    c.check("maison : rien à signaler -> 'Tout va bien'", etat["state"], "Tout va bien")
    sources["sensor.door_entree_batterie"] = {"domain": "mqtt", "custom_component": False}  # pile d'un nœud
    maison({"binary_sensor.leak_cuisine_fuite": ("on", {"device_class": "moisture"}),
            "binary_sensor.fuite_d_eau_quelque_part": ("on", {"device_class": "moisture"}),
            "cover.gate_entree_portail": ("open", {"device_class": "gate"}),
            "sensor.door_entree_batterie": ("12", {"device_class": "battery"}),
            "sensor.telephone_du_proche_batterie": ("5", {"device_class": "battery"}),  # app HA : ignoré
            "sensor.climate_salon_temperature": ("21", {"device_class": "temperature"})})
    c.check("maison : fuite + portail + pile -> '3 points' (groupe fuite non compté 2 fois)",
            etat["state"], "3 points à vérifier")
    c.check("maison : le détail nomme la fuite", etat["attributes"]["points"], "Fuite : leak_cuisine_fuite")
    c.check("maison : le détail nomme la pile", etat["attributes"]["points"], "Pile faible : door_entree_batterie (12 %)")
    c.verify("maison : la batterie du téléphone du proche n'est pas une pile à changer",
             "telephone" in str(Template(etat["attributes"]["points"], hass).async_render()), False)
    maison({"binary_sensor.call_chambre_appel": ("on", {"device_class": "safety"})})
    c.check("maison : appui sur le bouton d'appel signalé", etat["attributes"]["points"], "Appel à l'aide : call_chambre_appel")
    maison({"input_boolean.senior_appel_en_cours": ("on", {}),
            "binary_sensor.call_chambre_appel": ("off", {"device_class": "safety"})})
    c.check("maison : appel en cours (pas encore pris en compte) signalé", etat["state"], "1 point à vérifier")
    c.check("maison : appel en cours en tête de liste", etat["attributes"]["points"], "[\"Appel à l'aide en cours\"]")

    await hass.async_stop(force=True)
    print("TOUS OK" if not c.failures else f"{c.failures} ÉCHEC(S)")
    return 1 if c.failures else 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
