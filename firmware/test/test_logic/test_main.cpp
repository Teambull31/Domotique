// Tests de la logique pure (DomoLogic.h), exécutés sur PC : pio test -e native
#include <DomoLogic.h>
#include <unity.h>

#include <cmath>
#include <initializer_list>

using domo::thermostatWant;
using domo::WindowDetector;

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
  return UNITY_END();
}
