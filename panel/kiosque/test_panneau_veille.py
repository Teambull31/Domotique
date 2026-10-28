"""Tests du panneau (veille, lanceur, installation) : python3 panel/kiosque/test_panneau_veille.py"""
import http.server
import importlib.util
import json
import os
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("veille", Path(__file__).with_name("panneau-veille.py"))
veille = importlib.util.module_from_spec(spec)
spec.loader.exec_module(veille)
CONF = dict(veille.DEFAUTS)


class TestNiveaux(unittest.TestCase):
    def test_jour_nuit(self):
        self.assertFalse(veille.est_nuit(12, CONF))
        self.assertTrue(veille.est_nuit(23, CONF))
        self.assertTrue(veille.est_nuit(3, CONF))
        self.assertFalse(veille.est_nuit(7, CONF))

    def test_niveaux(self):
        self.assertEqual(veille.niveau(True, 12, CONF), 100)
        self.assertEqual(veille.niveau(False, 12, CONF), 15)
        self.assertEqual(veille.niveau(True, 23, CONF), 40)
        self.assertEqual(veille.niveau(False, 23, CONF), 3)


class TestVeille(unittest.TestCase):
    def test_veille_apres_le_delai(self):
        v = veille.Veille(CONF, 0)
        self.assertEqual(v.tour(30, False, 12), (None, None))
        self.assertEqual(v.tour(61, False, 12), (15, True))  # tamisé + entrées saisies
        self.assertTrue(v.en_veille)

    def test_premier_contact_reveille_sans_etre_transmis(self):
        v = veille.Veille(CONF, 0)
        v.tour(61, False, 12)
        self.assertEqual(v.tour(70, True, 12), (100, None))  # éclat, mais toujours saisi
        self.assertTrue(v.saisi)
        self.assertEqual(v.tour(70.2, True, 12), (None, None))  # doigt encore posé : toujours saisi
        self.assertEqual(v.tour(70.8, False, 12), (None, False))  # doigt levé 0,5 s : rendu
        self.assertFalse(v.saisi)

    def test_utilisation_normale_jamais_saisie(self):
        v = veille.Veille(CONF, 0)
        for t in range(1, 50):
            self.assertEqual(v.tour(t, True, 12), (None, None))
        self.assertFalse(v.saisi)


class TestAlerte(unittest.TestCase):
    def test_alerte_reveille_l_ecran_et_rend_le_toucher(self):
        v = veille.Veille(CONF, 0)
        v.tour(61, False, 12)  # en veille, entrées saisies
        self.assertEqual(v.tour(70, False, 12, alerte=True), (100, False))
        self.assertFalse(v.en_veille)

    def test_pas_de_veille_tant_que_l_alerte_dure(self):
        v = veille.Veille(CONF, 0)
        for t in range(1, 300, 10):
            self.assertEqual(v.tour(t, False, 12, alerte=True), (None, None))
        self.assertFalse(v.en_veille)
        self.assertEqual(v.tour(300 + 61, False, 12), (15, True))  # alerte finie : veille normale


class FauxHA(http.server.BaseHTTPRequestHandler):
    etat = "Tout va bien"

    def do_GET(self):
        if self.headers.get("Authorization") != "Bearer jeton":
            self.send_response(401)
            self.end_headers()
            return
        self.send_response(200)
        self.end_headers()
        self.wfile.write(json.dumps({"state": FauxHA.etat}).encode())

    def log_message(self, *args):
        pass


class TestMaisonEnAlerte(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = http.server.HTTPServer(("127.0.0.1", 0), FauxHA)
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.url = f"http://127.0.0.1:{cls.srv.server_address[1]}"

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()
        cls.srv.server_close()

    def conf(self, token="jeton"):
        return {**CONF, "HA_URL": self.url, "HA_TOKEN": token}

    def test_tout_va_bien(self):
        FauxHA.etat = "Tout va bien"
        self.assertFalse(veille.maison_en_alerte(self.conf()))

    def test_probleme(self):
        FauxHA.etat = "2 points à vérifier"
        self.assertTrue(veille.maison_en_alerte(self.conf()))

    def test_mauvais_jeton_ou_ha_injoignable_pas_d_alerte(self):
        FauxHA.etat = "2 points à vérifier"
        self.assertFalse(veille.maison_en_alerte(self.conf(token="faux")))
        self.assertFalse(veille.maison_en_alerte({**CONF, "HA_URL": "http://127.0.0.1:9", "HA_TOKEN": "x"}))

    def test_non_configure(self):
        self.assertFalse(veille.maison_en_alerte(CONF))


spec2 = importlib.util.spec_from_file_location("attendre", Path(__file__).with_name("attendre-ha.py"))
attendre = importlib.util.module_from_spec(spec2)
spec2.loader.exec_module(attendre)


class TestAttendreHA(unittest.TestCase):
    def test_ha_qui_repond_meme_401(self):
        srv = http.server.HTTPServer(("127.0.0.1", 0), FauxHA)
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        try:
            self.assertTrue(attendre.ha_repond(f"http://127.0.0.1:{srv.server_address[1]}/"))  # 401 sans jeton
        finally:
            srv.shutdown()
            srv.server_close()

    def test_ha_injoignable(self):
        self.assertFalse(attendre.ha_repond("http://127.0.0.1:9/", timeout=1))


class TestES(unittest.TestCase):
    def test_luminosite_ecrite_a_l_echelle(self):
        with tempfile.TemporaryDirectory() as d:
            bl = Path(d) / "bl0"
            bl.mkdir()
            (bl / "max_brightness").write_text("255")
            (bl / "brightness").write_text("0")
            veille.appliquer_luminosite(15, str(Path(d) / "*"))
            self.assertEqual((bl / "brightness").read_text(), "38")

    def test_ecran_a_peu_de_niveaux_jamais_eteint_par_l_arrondi(self):
        self.assertEqual(veille.valeur_retro(3, 7), 1)      # 3 % de 7 niveaux : 0 éteindrait l'écran
        self.assertEqual(veille.valeur_retro(3, 31), 1)     # écran tactile Raspberry Pi 2
        self.assertEqual(veille.valeur_retro(15, 255), 38)
        self.assertEqual(veille.valeur_retro(100, 31), 31)
        self.assertEqual(veille.valeur_retro(0, 255), 0)    # 0 % demandé : éteint
        with tempfile.TemporaryDirectory() as d:
            bl = Path(d) / "bl0"
            bl.mkdir()
            (bl / "max_brightness").write_text("7")
            (bl / "brightness").write_text("7")
            veille.appliquer_luminosite(3, str(Path(d) / "*"))
            self.assertEqual((bl / "brightness").read_text(), "1")

    def test_seuls_ecran_tactile_et_souris_surveilles(self):
        with tempfile.TemporaryDirectory() as d:
            dev, sysfs, udev = Path(d) / "dev", Path(d) / "sys", Path(d) / "udev"
            for p in (dev, sysfs, udev):
                p.mkdir()
            for n, (minor, props) in enumerate([(64, "E:ID_INPUT=1\nE:ID_INPUT_KEY=1\n"),  # bouton marche
                                                (65, "E:ID_INPUT=1\nE:ID_INPUT_TOUCHSCREEN=1\n"),
                                                (66, "E:ID_INPUT=1\nE:ID_INPUT_MOUSE=1\n")]):
                (dev / f"event{n}").write_text("")
                (sysfs / f"event{n}").mkdir()
                (sysfs / f"event{n}" / "dev").write_text(f"13:{minor}\n")
                (udev / f"c13:{minor}").write_text(props)
            choisis = veille.entrees_a_surveiller(str(dev / "event*"), str(sysfs), str(udev))
            self.assertEqual([Path(c).name for c in choisis], ["event1", "event2"])
            # udev muet (données absentes) : toutes les entrées, comme avant
            for f in udev.iterdir():
                f.unlink()
            self.assertEqual(len(veille.entrees_a_surveiller(str(dev / "event*"), str(sysfs), str(udev))), 3)

    def test_reglages_lus_et_commentaires_ignores(self):
        with tempfile.NamedTemporaryFile("w", delete=False) as f:
            f.write("DELAI=30  # plus court\nVEILLE_NUIT=1\nINCONNU=5\nACTIF_JOUR=abc\n"
                    "HA_URL=\"http://192.168.1.10:8123\"\nHA_TOKEN=abc.def\n")
        try:
            conf = veille.lire_reglages(f.name)
        finally:
            os.unlink(f.name)
        self.assertEqual(conf["DELAI"], 30)
        self.assertEqual(conf["VEILLE_NUIT"], 1)
        self.assertEqual(conf["ACTIF_JOUR"], 100)  # valeur invalide : défaut conservé
        self.assertEqual(conf["HA_URL"], "http://192.168.1.10:8123")  # guillemets retirés
        self.assertEqual(conf["HA_TOKEN"], "abc.def")


ICI = Path(__file__).parent


def lancer(env, dri_avec_rendu=True):
    """Lanceur en mode essai : renvoie (code, sortie)."""
    with tempfile.TemporaryDirectory() as dri:
        if dri_avec_rendu:
            (Path(dri) / "renderD128").write_text("")
        r = subprocess.run(["bash", str(ICI / "lancer.sh")], capture_output=True, text=True,
                           env={"PATH": os.environ["PATH"], "PANNEAU_ESSAI": "1", "PANNEAU_DRI": dri,
                                "NAVIGATEUR": "/usr/bin/chromium", **env})
    return r.returncode, r.stdout + r.stderr


class TestLanceur(unittest.TestCase):
    URL = {"URL": "http://ha:8123/tableau-maison/accueil"}

    def test_rendu_gpu_si_la_carte_a_un_noeud_de_rendu(self):
        code, out = lancer(self.URL)
        self.assertEqual(code, 0)
        self.assertIn("rendu=gpu WLR_RENDERER= ", out)
        self.assertNotIn("--disable-gpu", out)
        self.assertIn("--kiosk", out)
        self.assertTrue(out.strip().endswith("http://ha:8123/tableau-maison/accueil"))

    def test_rendu_logiciel_automatique_sans_pilote_3d(self):
        code, out = lancer(self.URL, dri_avec_rendu=False)
        self.assertIn("rendu=logiciel WLR_RENDERER=pixman", out)
        self.assertIn("--disable-gpu", out)

    def test_rendu_force(self):
        self.assertIn("rendu=logiciel", lancer({**self.URL, "RENDU": "logiciel"})[1])
        self.assertIn("rendu=gpu", lancer({**self.URL, "RENDU": "gpu"}, dri_avec_rendu=False)[1])

    def test_sans_adresse_erreur_claire(self):
        code, out = lancer({})
        self.assertNotEqual(code, 0)
        self.assertIn("URL absente", out)


# Faux apt-cache : dépôts de Raspberry Pi OS Bookworm, Debian/Armbian, Ubuntu (Chromium en snap).
# Sortie longue comme le vrai : un « grep -q » qui s'arrête tôt lui envoie SIGPIPE (piège de pipefail).
FAUX_APT = r"""#!/bin/bash
case "$SYSTEME:$1:$2" in
  pios:policy:chromium-browser|pios:policy:chromium|debian:policy:chromium|ubuntu:policy:chromium-browser)
    echo "  Candidate: 1:120" ;;
  *:policy:*) echo "  Candidate: (none)" ;;
  ubuntu:depends:chromium-browser) echo "  Depends: snapd" ;;
  *:depends:*) echo "  Depends: libc6" ;;
esac
yes "     500 http://deb.debian.org/debian bookworm/main arm64 Packages" | head -n 50000
"""


def installeur(script, systeme="debian"):
    with tempfile.TemporaryDirectory() as d:
        (Path(d) / "apt-cache").write_text(FAUX_APT)
        (Path(d) / "apt-cache").chmod(0o755)
        r = subprocess.run(["bash", "-c", f'source "{ICI}/install-kiosk.sh"; {script}'],
                           capture_output=True, text=True,
                           env={"PATH": f"{d}:{os.environ['PATH']}", "SYSTEME": systeme})
    return r.stdout.strip()


class TestInstallation(unittest.TestCase):
    def test_navigateur_selon_le_systeme(self):
        self.assertEqual(installeur("choisir_navigateur", "pios"), "chromium-browser")
        self.assertEqual(installeur("choisir_navigateur", "debian"), "chromium")  # Debian, Armbian
        self.assertEqual(installeur("choisir_navigateur", "ubuntu"), "")  # snap : refusé

    def test_groupes_absents_ignores(self):
        self.assertEqual(installeur("groupes_existants root groupe-qui-n-existe-pas"), "root")

    def test_reglages_crees_puis_mis_a_jour_sans_perdre_le_rendu(self):
        with tempfile.TemporaryDirectory() as d:
            conf = Path(d) / "panneau"
            installeur(f'ecrire_reglages "{conf}" http://ha:8123/a /usr/bin/chromium-browser')
            texte = conf.read_text()
            self.assertIn('URL="http://ha:8123/a"\n', texte)
            self.assertIn("RENDU=auto\n", texte)
            conf.write_text(texte.replace("RENDU=auto", "RENDU=logiciel"))
            # changement de carte (Orange Pi : chromium) et adresse avec « & »
            installeur(f'ecrire_reglages "{conf}" "http://ha:8123/b?kiosk&x=1" /usr/bin/chromium')
            texte = conf.read_text()
            self.assertIn('URL="http://ha:8123/b?kiosk&x=1"\n', texte)
            self.assertIn('NAVIGATEUR="/usr/bin/chromium"\n', texte)
            self.assertIn("RENDU=logiciel\n", texte)
            self.assertEqual(installeur(f'lire_cle "{conf}" URL'), "http://ha:8123/b?kiosk&x=1")
            for ligne in texte.splitlines():  # EnvironmentFile : pas de commentaire en fin de ligne
                self.assertTrue(ligne.startswith("#") or "#" not in ligne, ligne)
            # lu par un shell (« & » compris), puis passé au lanceur comme le fait systemd
            r = subprocess.run(["bash", "-c", f'set -a; . "{conf}"; PANNEAU_ESSAI=1 bash "{ICI}/lancer.sh"'],
                               capture_output=True, text=True, env={"PATH": os.environ["PATH"]})
            self.assertTrue(r.stdout.strip().endswith("http://ha:8123/b?kiosk&x=1"), r.stdout + r.stderr)
            self.assertIn("rendu=logiciel", r.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
