#!/usr/bin/env python3
"""Veille du panneau : écran tamisé après quelques secondes sans contact, plein éclat au toucher.

Le premier contact sur un écran tamisé ne fait que le réveiller : il n'est PAS transmis au tableau de
bord (périphériques d'entrée « saisis » pendant la veille), donc pas de lumière allumée ou de portail
ouvert par erreur en réveillant l'écran. Niveaux plus bas la nuit.
Écrans à rétroéclairage pilotable (/sys/class/backlight : DSI, dont l'écran officiel Raspberry Pi, et
la plupart des écrans à nappe des cartes ARM, Orange Pi compris), quelle que soit la carte.
Alerte (facultatif, HA_URL + HA_TOKEN) : tant que l'« État de la maison » n'est pas « Tout va bien »
(fuite, portail ouvert, appel…), l'écran reste à pleine luminosité pour que le problème se voie.
Réglages : /etc/default/panneau-veille (voir install-kiosk.sh). Lancé par le service panneau-veille.
"""
import fcntl
import glob
import json
import os
import select
import threading
import time
import urllib.request

EVIOCGRAB = 0x40044590  # _IOW('E', 0x90, int) : accès exclusif à un périphérique d'entrée

DEFAUTS = {
    "DELAI": 60,          # secondes sans contact avant la veille
    "ACTIF_JOUR": 100,    # luminosité (%) en cours d'utilisation, le jour
    "ACTIF_NUIT": 40,
    "VEILLE_JOUR": 15,    # luminosité (%) en veille, le jour (l'état reste lisible de loin)
    "VEILLE_NUIT": 3,
    "NUIT_DEBUT": 22,     # heure de début de nuit
    "NUIT_FIN": 7,
    "HA_URL": "",         # ex. http://192.168.1.10:8123 (vide = pas de réveil sur alerte)
    "HA_TOKEN": "",       # jeton longue durée de l'utilisateur « panneau » (profil HA > Sécurité)
    "HA_ENTITE": "sensor.etat_de_la_maison",
}


def lire_reglages(chemin="/etc/default/panneau-veille"):
    conf = dict(DEFAUTS)
    try:
        with open(chemin, encoding="utf-8") as f:
            for ligne in f:
                cle, _, val = ligne.split("#")[0].partition("=")
                cle, val = cle.strip(), val.strip().strip('"').strip("'")
                if cle not in conf:
                    continue
                if isinstance(DEFAUTS[cle], int):
                    if val.isdigit():
                        conf[cle] = int(val)
                else:
                    conf[cle] = val
    except FileNotFoundError:
        pass
    return conf


def est_nuit(heure, conf):
    debut, fin = conf["NUIT_DEBUT"], conf["NUIT_FIN"]
    return heure >= debut or heure < fin if debut > fin else debut <= heure < fin


def niveau(actif, heure, conf):
    """Luminosité (%) voulue selon l'usage et l'heure."""
    moment = "NUIT" if est_nuit(heure, conf) else "JOUR"
    return conf[("ACTIF_" if actif else "VEILLE_") + moment]


class Veille:
    """Logique pure (testée) : à chaque tour, dit s'il faut changer la luminosité ou la saisie."""

    def __init__(self, conf, maintenant):
        self.conf, self.dernier_contact, self.en_veille, self.saisi = conf, maintenant, False, False

    def tour(self, maintenant, contact, heure, alerte=False):
        """Renvoie (luminosité % ou None, saisir: True/False/None)."""
        if alerte:  # problème dans la maison : écran éclairé et utilisable tant qu'il dure
            self.dernier_contact = maintenant
            if self.en_veille:
                self.en_veille, self.saisi = False, False
                return niveau(True, heure, self.conf), False
        if contact:
            self.dernier_contact = maintenant
            if self.en_veille:  # réveil : ce contact reste saisi (non transmis)
                self.en_veille = False
                return niveau(True, heure, self.conf), None
            return None, None
        if self.saisi is True and not self.en_veille and maintenant - self.dernier_contact > 0.5:
            self.saisi = False  # doigt levé depuis 0,5 s : rendre la main au tableau de bord
            return None, False
        if not self.en_veille and maintenant - self.dernier_contact > self.conf["DELAI"]:
            self.en_veille, self.saisi = True, True
            return niveau(False, heure, self.conf), True
        return None, None


def maison_en_alerte(conf):
    """True si l'« État de la maison » signale un problème. HA injoignable ou non configuré : False
    (l'écran ne doit pas rester allumé pour rien)."""
    if not conf["HA_URL"] or not conf["HA_TOKEN"]:
        return False
    req = urllib.request.Request(
        f"{conf['HA_URL'].rstrip('/')}/api/states/{conf['HA_ENTITE']}",
        headers={"Authorization": f"Bearer {conf['HA_TOKEN']}"})
    try:
        with urllib.request.urlopen(req, timeout=5) as rep:
            etat = json.load(rep).get("state", "")
    except (OSError, ValueError):
        return False
    return etat not in ("Tout va bien", "unknown", "unavailable", "")


class Surveillance(threading.Thread):
    """Interroge HA toutes les 30 s, sans jamais bloquer la boucle du toucher."""

    def __init__(self, conf, periode=30):
        super().__init__(daemon=True)
        self.conf, self.periode, self.alerte = conf, periode, False

    def run(self):
        while True:
            self.alerte = maison_en_alerte(self.conf)
            time.sleep(self.periode)


def valeur_retro(pct, maxi):
    """Valeur à écrire pour pct % : jamais 0 si pct > 0. Certains écrans n'ont que quelques niveaux
    (max_brightness 7 ou 31) : 3 % arrondi à 0 éteindrait complètement l'écran."""
    v = maxi * pct // 100
    return max(1, v) if pct > 0 and maxi > 0 else v


def appliquer_luminosite(pct, motif="/sys/class/backlight/*"):
    for bl in glob.glob(motif):
        try:
            with open(os.path.join(bl, "max_brightness")) as f:
                maxi = int(f.read())
            with open(os.path.join(bl, "brightness"), "w") as f:
                f.write(str(valeur_retro(pct, maxi)))
        except (OSError, ValueError):
            pass


POINTEURS = ("ID_INPUT_TOUCHSCREEN", "ID_INPUT_TABLET", "ID_INPUT_MOUSE", "ID_INPUT_TOUCHPAD")


def est_pointeur(evenement, sysfs="/sys/class/input", udev="/run/udev/data"):
    """Écran tactile (ou souris) d'après udev ; None si udev ne sait pas. Les autres entrées ne sont
    ni surveillées ni bloquées : bouton d'alimentation (Raspberry Pi 5, cartes ARM : le bloquer
    empêcherait l'arrêt pendant la veille), récepteur infrarouge, télécommande HDMI-CEC."""
    try:
        with open(os.path.join(sysfs, evenement, "dev")) as f:
            majmin = f.read().strip()
        with open(os.path.join(udev, "c" + majmin)) as f:
            proprietes = f.read().split()
    except OSError:
        return None
    return any(f"E:{p}=1" in proprietes for p in POINTEURS)


def entrees_a_surveiller(motif="/dev/input/event*", sysfs="/sys/class/input", udev="/run/udev/data"):
    chemins = sorted(glob.glob(motif))
    choisis = [c for c in chemins if est_pointeur(os.path.basename(c), sysfs, udev)]
    return choisis or chemins  # écran non reconnu par udev : toutes les entrées, comme avant


def ouvrir_entrees(motif="/dev/input/event*"):
    fds = []
    for chemin in entrees_a_surveiller(motif):
        try:
            fds.append(os.open(chemin, os.O_RDONLY | os.O_NONBLOCK))
        except OSError:
            pass
    return fds


def saisir(fds, oui):
    for fd in fds:
        try:
            fcntl.ioctl(fd, EVIOCGRAB, 1 if oui else 0)
        except OSError:
            pass


# Chemins remplaçables par variables d'environnement (essais sans écran ni Raspberry Pi)
RETRO = os.environ.get("PANNEAU_RETRO", "/sys/class/backlight/*")
ENTREES = os.environ.get("PANNEAU_ENTREES", "/dev/input/event*")
REGLAGES = os.environ.get("PANNEAU_REGLAGES", "/etc/default/panneau-veille")


def main():
    conf = lire_reglages(REGLAGES)
    fds = ouvrir_entrees(ENTREES)
    veille = Veille(conf, time.monotonic())
    surveillance = Surveillance(conf)
    surveillance.start()
    appliquer_luminosite(niveau(True, time.localtime().tm_hour, conf), RETRO)
    dernier_reglage = time.monotonic()
    while True:
        if fds:
            prets, _, _ = select.select(fds, [], [], 0.25)
        else:  # écran tactile pas encore détecté : réessayer
            time.sleep(1)
            prets, fds = [], ouvrir_entrees(ENTREES)
        for fd in prets:
            try:
                while os.read(fd, 4096):
                    pass
            except BlockingIOError:
                pass
            except OSError:  # périphérique débranché : on rouvre tout
                for f in fds:
                    os.close(f)
                fds = ouvrir_entrees(ENTREES)
                break
        heure = time.localtime().tm_hour
        pct, saisie = veille.tour(time.monotonic(), bool(prets), heure, surveillance.alerte)
        if saisie is not None:
            saisir(fds, saisie)
        if pct is not None:
            appliquer_luminosite(pct, RETRO)
        if time.monotonic() - dernier_reglage > 60:  # passage jour/nuit
            dernier_reglage = time.monotonic()
            appliquer_luminosite(niveau(not veille.en_veille, heure, conf), RETRO)


if __name__ == "__main__":
    main()
