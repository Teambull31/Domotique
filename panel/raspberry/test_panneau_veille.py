"""Tests de la veille du panneau (logique pure) : python3 panel/raspberry/test_panneau_veille.py"""
import http.server
import importlib.util
import json
import os
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
