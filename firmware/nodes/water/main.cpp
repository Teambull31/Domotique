// Nœud "water" : compteur d'eau à impulsions (alimenté sur secteur, près du compteur).
// Consommation (litres, index du compteur), débit, et deux alertes : eau qui coule sans arrêt (robinet
// oublié, chasse d'eau qui coule, canalisation percée) et fuite lente probable (l'eau ne s'arrête plus
// jamais 3 h d'affilée, même la nuit).
// Capteur : sortie impulsions du compteur (contact sec, option « émetteur d'impulsions ») ou contact reed
// (ILS) posé sur le compteur, entre PIN_INPUT1 et GND. Régler « Litres par impulsion » (souvent 1 ou 10 L)
// et « Index du compteur » (relevé sur le cadran, en m³) dans HA. Broches : lib/DomoCore/src/DomoPins.h
#include <DomoCore.h>
#include <DomoLogic.h>
#include <Preferences.h>

#define PULSE_PIN PIN_INPUT1
#define DEBOUNCE_US 20000LL                  // rebonds du contact reed
#define GAP_MS (5UL * 60UL * 1000UL)         // impulsions à moins de 5 min : l'eau coule toujours
#define QUIET_MS (3UL * 3600UL * 1000UL)     // pause de 3 h (une nuit sans eau) ...
#define LEAK_WINDOW_MS (24UL * 3600UL * 1000UL)  // ... attendue au moins une fois par 24 h
#define SAVE_MS (10UL * 60UL * 1000UL)       // total enregistré au plus toutes les 10 min (flash)

DomoCore core("water");
Preferences prefs;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t pending = 0;  // impulsions comptées par l'interruption, pas encore traitées
volatile int64_t lastEdgeUs = 0;
double litres = 0;              // total (litres) depuis la mise en service : ne fait que croître
double savedLitres = 0;
double offsetL = 0;             // index du compteur = litres + offsetL (recalage sur le cadran)
float litresPerPulse = 1;
float alertMin = 60;            // minutes d'écoulement continu avant l'alerte
unsigned long lastPulse = 0, interval = 0, lastSend = 0, lastSave = 0;
domo::ContinuousFlow flow(GAP_MS);
domo::SlowLeak slowLeak(QUIET_MS, LEAK_WINDOW_MS);
bool continuous = false, leak = false, dirty = true;

void IRAM_ATTR onPulse() {
  int64_t t = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&mux);
  if (t - lastEdgeUs >= DEBOUNCE_US) pending++;
  lastEdgeUs = t;  // tout front repousse la fenêtre : une rafale de rebonds compte une fois
  portEXIT_CRITICAL_ISR(&mux);
}

void save() {
  if (litres == savedLitres) return;
  prefs.putDouble("litres", litres);
  savedLitres = litres;
  lastSave = millis();
}

void setup() {
  Serial.begin(115200);
  pinMode(PULSE_PIN, INPUT_PULLUP);
  prefs.begin("water", false);
  litres = savedLitres = prefs.getDouble("litres", 0);
  if (!(litres >= 0)) litres = savedLitres = 0;  // NaN ou négatif : on repart de zéro
  offsetL = prefs.getDouble("offset", 0);
  if (offsetL != offsetL) offsetL = 0;  // NaN
  litresPerPulse = domo::sanitize(prefs.getFloat("lpp", 1), 0.1f, 100, 1);
  alertMin = domo::sanitize(prefs.getFloat("alert_min", 60), 10, 600, 60);

  core.addEntity("sensor", "litres", "Eau consommée", "water", "L");
  core.addEntity("sensor", "debit", "Débit", "volume_flow_rate", "L/min");
  core.addEntity("binary_sensor", "continu", "Eau qui coule sans arrêt", "moisture");
  core.addEntity("binary_sensor", "fuite_lente", "Fuite lente probable", "moisture");
  core.addEntity("number", "index", "Index du compteur", "water", "m³",
                 "{\"min\":0,\"max\":999999,\"step\":0.001,\"mode\":\"box\",\"entity_category\":\"config\"}");
  core.addEntity("number", "lpp", "Litres par impulsion", nullptr, "L",
                 "{\"min\":0.1,\"max\":100,\"step\":0.1,\"mode\":\"box\",\"entity_category\":\"config\"}");
  core.addEntity("number", "alerte_min", "Alerte eau qui coule après", "duration", "min",
                 "{\"min\":10,\"max\":600,\"step\":5,\"mode\":\"box\",\"entity_category\":\"config\"}");
  core.onCommand([](JsonDocument& d) {
    // Recalage sur le cadran (m³, double : au litre près). Seul le décalage change : « Eau consommée »
    // ne saute pas (HA compterait sinon tout l'index comme consommé le jour du réglage).
    if (d["index"].is<double>()) {
      double index = d["index"].as<double>();
      if (index >= 0 && index <= 999999) {
        offsetL = index * 1000.0 - litres;
        prefs.putDouble("offset", offsetL);
      }
    }
    if (d["lpp"].is<float>()) {
      litresPerPulse = domo::sanitize(d["lpp"], 0.1f, 100, litresPerPulse);
      prefs.putFloat("lpp", litresPerPulse);
    }
    if (d["alerte_min"].is<float>()) {
      alertMin = domo::sanitize(d["alerte_min"], 10, 600, alertMin);
      prefs.putFloat("alert_min", alertMin);
    }
    dirty = true;
  });
  core.onSafeState(save);  // OTA ou redémarrage : total enregistré
  attachInterrupt(digitalPinToInterrupt(PULSE_PIN), onPulse, FALLING);
  core.begin();
}

void loop() {
  core.loop();
  portENTER_CRITICAL(&mux);
  uint32_t n = pending;
  pending = 0;
  portEXIT_CRITICAL(&mux);

  unsigned long now = millis();
  if (n) {
    litres += n * (double)litresPerPulse;
    if (lastPulse) interval = (now - lastPulse) / n;
    lastPulse = now;
    flow.pulse(now);
    slowLeak.pulse(now);
  }
  bool cont = flow.runMs(now) >= (unsigned long)alertMin * 60000UL;
  bool lk = slowLeak.suspected(now);
  if (cont != continuous || lk != leak) {
    continuous = cont;
    leak = lk;
    dirty = true;
  }
  if (now - lastSave >= SAVE_MS || litres - savedLitres >= 100) save();

  float debit = lastPulse ? domo::flowLpm(litresPerPulse, interval, now - lastPulse) : 0;
  // En écoulement : toutes les 10 s (pas à chaque impulsion : jusqu'à plusieurs par seconde) ;
  // à l'arrêt : toutes les minutes ; alerte ou réglage : tout de suite
  if (dirty || now - lastSend >= (debit > 0 ? 10000UL : 60000UL)) {
    JsonDocument doc;
    doc["litres"] = round(litres * 10) / 10.0;  // double : au litre près même après des milliers de m³
    doc["index"] = round(litres + offsetL) / 1000.0;
    doc["debit"] = roundf(debit * 10) / 10.0;
    doc["continu"] = continuous;
    doc["fuite_lente"] = leak;
    doc["lpp"] = litresPerPulse;
    doc["alerte_min"] = alertMin;
    if (core.publishTelemetry(doc)) {
      lastSend = now;
      dirty = false;
    }
  }
}
