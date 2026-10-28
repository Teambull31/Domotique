// Tests de la logique pure (DomoLogic.h), exécutés sur PC : pio test -e native
#include <DomoLogic.h>
#include <unity.h>

#include <cmath>
#include <initializer_list>

using domo::thermostatWant;
using domo::CycleGuard;
using domo::GateLogic;
using domo::PresenceLight;
using domo::WindowDetector;
using domo::batteryPercent;
using domo::shutterMoveMs;
using domo::shutterPosition;
using domo::sanitize;
using domo::sleepSeconds;
using domo::slug;

void setUp() {}
void tearDown() {}

// --- Thermostat ---
void test_thermostat_chauffe_sous_la_bande() { TEST_ASSERT_TRUE(thermostatWant(18.5f, 19.0f, 0.3f, false)); }
void test_thermostat_coupe_au_dessus_de_la_bande() { TEST_ASSERT_FALSE(thermostatWant(19.5f, 19.0f, 0.3f, true)); }
void test_thermostat_garde_son_etat_dans_la_bande() {
  TEST_ASSERT_TRUE(thermostatWant(19.1f, 19.0f, 0.3f, true));
  TEST_ASSERT_FALSE(thermostatWant(18.9f, 19.0f, 0.3f, false));
}
void test_thermostat_sonde_muette_coupe_toujours() {
  TEST_ASSERT_FALSE(thermostatWant(NAN, 19.0f, 0.3f, true));
  TEST_ASSERT_FALSE(thermostatWant(NAN, 7.0f, 0.3f, false));
}
void test_thermostat_hors_gel() { TEST_ASSERT_TRUE(thermostatWant(6.0f, 7.0f, 0.3f, false)); }

// --- Fenêtre ouverte ---
void test_fenetre_chute_de_1C_en_5_min_detectee() {
  WindowDetector w(6, 1.0f);
  const float t[] = {20.0f, 19.9f, 19.7f, 19.5f, 19.2f};
  for (float v : t) TEST_ASSERT_FALSE(w.add(v));
  TEST_ASSERT_TRUE(w.add(19.0f));  // 20,0 -> 19,0 sur 6 mesures
}
void test_fenetre_baisse_lente_ignoree() {
  WindowDetector w(6, 1.0f);
  for (int i = 0; i < 30; i++) TEST_ASSERT_FALSE(w.add(20.0f - i * 0.1f));  // 0,5 °C / 5 min
}
void test_fenetre_pas_de_detection_avant_6_mesures() {
  WindowDetector w(6, 1.0f);
  TEST_ASSERT_FALSE(w.add(22.0f));
  TEST_ASSERT_FALSE(w.add(18.0f));  // grosse chute mais historique incomplet
}
void test_fenetre_historique_repart_de_zero_apres_detection() {
  WindowDetector w(6, 1.0f);
  for (float v : {20.0f, 19.8f, 19.6f, 19.4f, 19.2f}) w.add(v);
  TEST_ASSERT_TRUE(w.add(19.0f));
  for (int i = 0; i < 5; i++) TEST_ASSERT_FALSE(w.add(18.0f));  // pas de redétection immédiate
}

// --- Volets ---
void test_volet_position_mi_course() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, shutterPosition(0, 12500, 25000, true));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, shutterPosition(100, 10000, 20000, false));
}
void test_volet_position_bornee_en_butee() {
  TEST_ASSERT_EQUAL_FLOAT(100.0f, shutterPosition(80, 60000, 25000, true));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, shutterPosition(20, 60000, 25000, false));
}
void test_volet_duree_position_intermediaire_sans_depassement() {
  TEST_ASSERT_EQUAL_UINT32(5000, shutterMoveMs(20, 40, 25000, 2000));
}
void test_volet_duree_butee_avec_depassement_de_recalage() {
  TEST_ASSERT_EQUAL_UINT32(25000 + 2000, shutterMoveMs(0, 100, 25000, 2000));
  TEST_ASSERT_EQUAL_UINT32(2000, shutterMoveMs(0, 0, 25000, 2000));  // déjà fermé : recalage seul
}

// --- Pile ---
void test_pile_bornes() {
  TEST_ASSERT_EQUAL_FLOAT(0.0f, batteryPercent(3.0f));
  TEST_ASSERT_EQUAL_FLOAT(100.0f, batteryPercent(4.3f));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 50.0f, batteryPercent(3.75f));
}

// --- Identifiants ---
void test_slug_accents_et_espaces() {
  TEST_ASSERT_EQUAL_STRING("entree", slug("Entrée").c_str());
  TEST_ASSERT_EQUAL_STRING("salle-de-bain", slug("Salle de bain").c_str());
  TEST_ASSERT_EQUAL_STRING("etage-ete", slug("ÉTAGE Été").c_str());
  TEST_ASSERT_EQUAL_STRING("cuisine-sejour", slug("Cuisine/Séjour").c_str());
}
void test_slug_tirets_uniques_et_vide() {
  TEST_ASSERT_EQUAL_STRING("chambre-mamie", slug("  Chambre   Mamie  ").c_str());
  TEST_ASSERT_EQUAL_STRING("maison", slug("   ").c_str());
  TEST_ASSERT_EQUAL_STRING("maison", slug("").c_str());
}

// --- Réglages (mémoire flash / HA) ---
void test_reglage_borne() {
  TEST_ASSERT_EQUAL_FLOAT(25.0f, sanitize(40.0f, 10, 25, 19));
  TEST_ASSERT_EQUAL_FLOAT(10.0f, sanitize(-3.0f, 10, 25, 19));
  TEST_ASSERT_EQUAL_FLOAT(21.5f, sanitize(21.5f, 10, 25, 19));
}
void test_reglage_nan_remplace_par_defaut() { TEST_ASSERT_EQUAL_FLOAT(19.0f, sanitize(NAN, 10, 25, 19)); }
void test_thermostat_consigne_nan_garde_le_hors_gel() {
  // Sans garde-fou, une consigne NaN rend thermostatWant() figé : jamais de chauffe à 5 °C.
  float target = sanitize(NAN, 10, 25, 19);
  TEST_ASSERT_TRUE(thermostatWant(5.0f, target, 0.3f, false));
}

// --- Présence et lumière automatique ---
static const unsigned long HOLD = 10000, OFF = 5UL * 60 * 1000;

void test_presence_rien_au_demarrage() {
  PresenceLight p;
  p.update(false, 500, HOLD, OFF);  // millis() petit au démarrage
  TEST_ASSERT_FALSE(p.present());
  TEST_ASSERT_FALSE(p.light());
}
void test_presence_allume_et_maintient_10s() {
  PresenceLight p;
  p.update(true, 1000, HOLD, OFF);
  TEST_ASSERT_TRUE(p.present());
  TEST_ASSERT_TRUE(p.light());
  p.update(false, 1000 + HOLD - 1, HOLD, OFF);
  TEST_ASSERT_TRUE(p.present());  // clignotement du radar absorbé
  p.update(false, 1000 + HOLD + 1, HOLD, OFF);
  TEST_ASSERT_FALSE(p.present());
  TEST_ASSERT_TRUE(p.light());  // reste allumée jusqu'au délai d'extinction
}
void test_presence_eteint_apres_delai() {
  PresenceLight p;
  p.update(true, 1000, HOLD, OFF);
  p.update(false, 1000 + OFF + 1, HOLD, OFF);
  TEST_ASSERT_FALSE(p.light());
}
void test_forcage_respecte_tant_que_occupe() {
  PresenceLight p;
  p.update(true, 1000, HOLD, OFF);
  p.force(false);  // "éteindre pour dormir"
  for (unsigned long t = 2000; t < 3600000; t += 60000) {
    p.update(true, t, HOLD, OFF);  // toujours quelqu'un dans la pièce
    TEST_ASSERT_FALSE(p.light());
  }
}
void test_auto_reprend_quand_piece_vide() {
  PresenceLight p;
  p.update(true, 1000, HOLD, OFF);
  p.force(false);
  p.update(false, 1000 + OFF + 1, HOLD, OFF);  // pièce vide depuis le délai
  TEST_ASSERT_FALSE(p.manual());
  p.update(true, 1000 + OFF + 5000, HOLD, OFF);  // quelqu'un revient
  TEST_ASSERT_TRUE(p.light());
}
void test_mode_auto_desactive() {
  PresenceLight p;
  p.autoMode = false;
  p.update(true, 1000, HOLD, OFF);
  TEST_ASSERT_TRUE(p.present());
  TEST_ASSERT_FALSE(p.light());
}

// --- Anti-cycles courts du chauffage ---
static const unsigned long CYCLE = 3UL * 60 * 1000;

void test_cycle_premiere_bascule_immediate() {
  CycleGuard g(CYCLE);
  TEST_ASSERT_TRUE(g.apply(false, true, false, 1000));
}
void test_cycle_bascule_bloquee_moins_de_3_min() {
  CycleGuard g(CYCLE);
  g.apply(false, true, false, 1000);                         // allumé
  TEST_ASSERT_TRUE(g.apply(true, false, false, 1000 + 60000));  // extinction refusée à 1 min
  TEST_ASSERT_FALSE(g.apply(true, false, false, 1000 + CYCLE));  // acceptée à 3 min
}
void test_cycle_sonde_muette_coupe_immediatement() {
  CycleGuard g(CYCLE);
  g.apply(false, true, false, 1000);
  TEST_ASSERT_FALSE(g.apply(true, false, true, 1000 + 5000));  // sécurité : pas d'attente
}
void test_cycle_etat_inchange_ne_consomme_pas_le_delai() {
  CycleGuard g(CYCLE);
  g.apply(false, true, false, 1000);
  g.apply(true, true, false, 200000);  // pas de bascule
  TEST_ASSERT_FALSE(g.apply(true, false, false, 1000 + CYCLE));
}

// --- Sommeil des nœuds sur pile ---
void test_sommeil_normal_apres_envoi_reussi() { TEST_ASSERT_EQUAL_UINT32(3600, sleepSeconds(0, 3600)); }
void test_sommeil_nouvel_essai_rapide_puis_espace() {
  TEST_ASSERT_EQUAL_UINT32(60, sleepSeconds(1, 3600));
  TEST_ASSERT_EQUAL_UINT32(120, sleepSeconds(2, 3600));
  TEST_ASSERT_EQUAL_UINT32(480, sleepSeconds(4, 3600));
}
void test_sommeil_jamais_plus_que_le_battement() {
  TEST_ASSERT_EQUAL_UINT32(3600, sleepSeconds(7, 3600));
  TEST_ASSERT_EQUAL_UINT32(3600, sleepSeconds(1000, 3600));  // box en panne des jours : pas de dépassement
}

// --- Portail ---
static const unsigned long TRAVEL = 25000;

void test_portail_etat_par_capteurs() {
  GateLogic g(TRAVEL);
  TEST_ASSERT_EQUAL(GateLogic::CLOSED, g.state(true, -1, 0));
  TEST_ASSERT_EQUAL(GateLogic::OPEN, g.state(false, -1, 0));    // sans capteur ouvert : pas fermé = ouvert
  TEST_ASSERT_EQUAL(GateLogic::OPEN, g.state(false, 1, 0));
  TEST_ASSERT_EQUAL(GateLogic::STOPPED, g.state(false, 0, 0));  // entre les deux, sans ordre en cours
}
void test_portail_ouvrir_depuis_ferme_puis_mouvement() {
  GateLogic g(TRAVEL);
  TEST_ASSERT_TRUE(g.command(GateLogic::CMD_OPEN, GateLogic::CLOSED, 1000));
  TEST_ASSERT_EQUAL(GateLogic::OPENING, g.state(false, 0, 5000));
  TEST_ASSERT_EQUAL(GateLogic::OPEN, g.state(false, 1, 20000));  // fin de course ouvert atteinte
}
void test_portail_pas_d_impulsion_pendant_un_mouvement() {
  GateLogic g(TRAVEL);
  g.command(GateLogic::CMD_OPEN, GateLogic::CLOSED, 1000);
  // une 2e impulsion arrêterait ou inverserait le portail : refusée
  TEST_ASSERT_FALSE(g.command(GateLogic::CMD_OPEN, GateLogic::OPENING, 10000));
  TEST_ASSERT_FALSE(g.command(GateLogic::CMD_CLOSE, GateLogic::OPENING, 10000));
  TEST_ASSERT_TRUE(g.command(GateLogic::CMD_STOP, GateLogic::OPENING, 10000));
}
void test_portail_ordres_inutiles_ignores() {
  GateLogic g(TRAVEL);
  TEST_ASSERT_FALSE(g.command(GateLogic::CMD_CLOSE, GateLogic::CLOSED, 1000));
  TEST_ASSERT_FALSE(g.command(GateLogic::CMD_OPEN, GateLogic::OPEN, 1000));
  TEST_ASSERT_FALSE(g.command(GateLogic::CMD_STOP, GateLogic::CLOSED, 1000));
}
void test_portail_anti_double_appui() {
  GateLogic g(TRAVEL);
  TEST_ASSERT_TRUE(g.command(GateLogic::CMD_OPEN, GateLogic::CLOSED, 1000));
  TEST_ASSERT_FALSE(g.command(GateLogic::CMD_CLOSE, GateLogic::STOPPED, 1500));  // 0,5 s après : refusé
  TEST_ASSERT_TRUE(g.command(GateLogic::CMD_CLOSE, GateLogic::STOPPED, 4000));
}
void test_portail_mouvement_estime_expire() {
  GateLogic g(TRAVEL);
  g.command(GateLogic::CMD_CLOSE, GateLogic::OPEN, 1000);
  TEST_ASSERT_EQUAL(GateLogic::CLOSING, g.state(false, 0, 10000));
  TEST_ASSERT_EQUAL(GateLogic::STOPPED, g.state(false, 0, 1000 + TRAVEL + 1));  // bloqué en route
}

void test_portail_fermeture_confirmee() {
  GateLogic g(TRAVEL);
  g.command(GateLogic::CMD_CLOSE, GateLogic::OPEN, 1000);
  TEST_ASSERT_FALSE(g.unconfirmed(GateLogic::CLOSING, 10000));
  TEST_ASSERT_FALSE(g.unconfirmed(GateLogic::CLOSED, 20000));             // atteint : confirmé
  TEST_ASSERT_FALSE(g.unconfirmed(GateLogic::OPEN, 1000 + TRAVEL + 60000));  // plus rien d'attendu
}
void test_portail_fermeture_non_confirmee() {
  GateLogic g(TRAVEL);
  g.command(GateLogic::CMD_CLOSE, GateLogic::OPEN, 1000);  // ordre radio perdu : reste ouvert
  TEST_ASSERT_FALSE(g.unconfirmed(GateLogic::OPEN, 1000 + TRAVEL + 29000));  // encore dans les temps
  TEST_ASSERT_TRUE(g.unconfirmed(GateLogic::OPEN, 1000 + TRAVEL + 31000));
}
void test_portail_stop_n_attend_rien() {
  GateLogic g(TRAVEL);
  g.command(GateLogic::CMD_OPEN, GateLogic::CLOSED, 1000);
  g.command(GateLogic::CMD_STOP, GateLogic::OPENING, 5000);
  TEST_ASSERT_FALSE(g.unconfirmed(GateLogic::STOPPED, 1000 + TRAVEL + 60000));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_thermostat_chauffe_sous_la_bande);
  RUN_TEST(test_thermostat_coupe_au_dessus_de_la_bande);
  RUN_TEST(test_thermostat_garde_son_etat_dans_la_bande);
  RUN_TEST(test_thermostat_sonde_muette_coupe_toujours);
  RUN_TEST(test_thermostat_hors_gel);
  RUN_TEST(test_fenetre_chute_de_1C_en_5_min_detectee);
  RUN_TEST(test_fenetre_baisse_lente_ignoree);
  RUN_TEST(test_fenetre_pas_de_detection_avant_6_mesures);
  RUN_TEST(test_fenetre_historique_repart_de_zero_apres_detection);
  RUN_TEST(test_volet_position_mi_course);
  RUN_TEST(test_volet_position_bornee_en_butee);
  RUN_TEST(test_volet_duree_position_intermediaire_sans_depassement);
  RUN_TEST(test_volet_duree_butee_avec_depassement_de_recalage);
  RUN_TEST(test_pile_bornes);
  RUN_TEST(test_slug_accents_et_espaces);
  RUN_TEST(test_slug_tirets_uniques_et_vide);
  RUN_TEST(test_reglage_borne);
  RUN_TEST(test_reglage_nan_remplace_par_defaut);
  RUN_TEST(test_thermostat_consigne_nan_garde_le_hors_gel);
  RUN_TEST(test_presence_rien_au_demarrage);
  RUN_TEST(test_presence_allume_et_maintient_10s);
  RUN_TEST(test_presence_eteint_apres_delai);
  RUN_TEST(test_forcage_respecte_tant_que_occupe);
  RUN_TEST(test_auto_reprend_quand_piece_vide);
  RUN_TEST(test_mode_auto_desactive);
  RUN_TEST(test_cycle_premiere_bascule_immediate);
  RUN_TEST(test_cycle_bascule_bloquee_moins_de_3_min);
  RUN_TEST(test_cycle_sonde_muette_coupe_immediatement);
  RUN_TEST(test_cycle_etat_inchange_ne_consomme_pas_le_delai);
  RUN_TEST(test_sommeil_normal_apres_envoi_reussi);
  RUN_TEST(test_sommeil_nouvel_essai_rapide_puis_espace);
  RUN_TEST(test_sommeil_jamais_plus_que_le_battement);
  RUN_TEST(test_portail_etat_par_capteurs);
  RUN_TEST(test_portail_ouvrir_depuis_ferme_puis_mouvement);
  RUN_TEST(test_portail_pas_d_impulsion_pendant_un_mouvement);
  RUN_TEST(test_portail_ordres_inutiles_ignores);
  RUN_TEST(test_portail_anti_double_appui);
  RUN_TEST(test_portail_mouvement_estime_expire);
  RUN_TEST(test_portail_fermeture_confirmee);
  RUN_TEST(test_portail_fermeture_non_confirmee);
  RUN_TEST(test_portail_stop_n_attend_rien);
  return UNITY_END();
}
