"""Tests de la veille du panneau (logique pure) : python3 panel/raspberry/test_panneau_veille.py"""
import importlib.util
import os
import tempfile
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
            f.write("DELAI=30  # plus court\nVEILLE_NUIT=1\nINCONNU=5\nACTIF_JOUR=abc\n")
        try:
            conf = veille.lire_reglages(f.name)
        finally:
            os.unlink(f.name)
        self.assertEqual(conf["DELAI"], 30)
        self.assertEqual(conf["VEILLE_NUIT"], 1)
        self.assertEqual(conf["ACTIF_JOUR"], 100)  # valeur invalide : défaut conservé


if __name__ == "__main__":
    unittest.main(verbosity=2)
