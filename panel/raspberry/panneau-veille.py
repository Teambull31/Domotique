#!/usr/bin/env python3
"""Veille du panneau : écran tamisé après quelques secondes sans contact, plein éclat au toucher.

Le premier contact sur un écran tamisé ne fait que le réveiller : il n'est PAS transmis au tableau de
bord (périphériques d'entrée « saisis » pendant la veille), donc pas de lumière allumée ou de portail
ouvert par erreur en réveillant l'écran. Niveaux plus bas la nuit.
Écrans à rétroéclairage pilotable (/sys/class/backlight : DSI, dont l'écran officiel Raspberry Pi).
Réglages : /etc/default/panneau-veille (voir install-kiosk.sh). Lancé par le service panneau-veille.
"""
import fcntl
import glob
import os
import select
import time

EVIOCGRAB = 0x40044590  # _IOW('E', 0x90, int) : accès exclusif à un périphérique d'entrée

DEFAUTS = {
    "DELAI": 60,          # secondes sans contact avant la veille
    "ACTIF_JOUR": 100,    # luminosité (%) en cours d'utilisation, le jour
    "ACTIF_NUIT": 40,
    "VEILLE_JOUR": 15,    # luminosité (%) en veille, le jour (l'état reste lisible de loin)
    "VEILLE_NUIT": 3,
    "NUIT_DEBUT": 22,     # heure de début de nuit
    "NUIT_FIN": 7,
}


def lire_reglages(chemin="/etc/default/panneau-veille"):
    conf = dict(DEFAUTS)
    try:
        with open(chemin, encoding="utf-8") as f:
            for ligne in f:
                cle, _, val = ligne.split("#")[0].partition("=")
                if cle.strip() in conf and val.strip().isdigit():
                    conf[cle.strip()] = int(val.strip())
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

    def tour(self, maintenant, contact, heure):
        """Renvoie (luminosité % ou None, saisir: True/False/None)."""
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


def appliquer_luminosite(pct, motif="/sys/class/backlight/*"):
    for bl in glob.glob(motif):
        try:
            with open(os.path.join(bl, "max_brightness")) as f:
                maxi = int(f.read())
            with open(os.path.join(bl, "brightness"), "w") as f:
                f.write(str(maxi * pct // 100))
        except OSError:
            pass


def ouvrir_entrees(motif="/dev/input/event*"):
    fds = []
    for chemin in glob.glob(motif):
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
        pct, saisie = veille.tour(time.monotonic(), bool(prets), heure)
        if saisie is not None:
            saisir(fds, saisie)
        if pct is not None:
            appliquer_luminosite(pct, RETRO)
        if time.monotonic() - dernier_reglage > 60:  # passage jour/nuit
            dernier_reglage = time.monotonic()
            appliquer_luminosite(niveau(not veille.en_veille, heure, conf), RETRO)


if __name__ == "__main__":
    main()
