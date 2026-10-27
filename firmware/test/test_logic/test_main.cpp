// Tests de la logique pure (DomoLogic.h), exécutés sur PC : pio test -e native
#include <DomoLogic.h>
#include <unity.h>

#include <cmath>
#include <initializer_list>

using domo::thermostatWant;
using domo::WindowDetector;
using domo::batteryPercent;
using domo::shutterMoveMs;
using domo::shutterPosition;
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
  return UNITY_END();
}
