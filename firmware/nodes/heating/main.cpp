// Nœud "heating" : thermostat d'une pièce (BME280 + contact sec de relais vers l'entrée thermostat
// de la chaudière / vanne / convecteur). Hystérésis locale : fonctionne sans Home Assistant.
// Hors-gel (7 °C) toujours actif, même si le chauffage est désactivé.
// Fenêtre ouverte : chute de 1 °C en 5 min -> chauffe suspendue 30 min (hors-gel maintenu).
#include <Adafruit_BME280.h>
#include <DomoCore.h>
#include <DomoLogic.h>
#include <Preferences.h>

#define RELAY_PIN PIN_RELAY_HEAT  // broches : lib/DomoCore/src/DomoPins.h
#define HYSTERESIS 0.3f
#define FROST_C 7.0f
#define SEND_INTERVAL_MS 30000
#define MIN_CYCLE_MS (3UL * 60UL * 1000UL)  // chaudière : pas de marche/arrêt à moins de 3 min
#define WINDOW_DROP_C 1.0f
#define WINDOW_PAUSE_MS (30UL * 60UL * 1000UL)

DomoCore core("heating");
Adafruit_BME280 bme;
Preferences prefs;
bool bmeOk = false, enabled = true, heating = false, dirty = true;
float setpoint = 19.0f, temp = NAN, offset = 0;  // offset : correction du capteur (°C)
unsigned long lastSend = 0, lastValid = 0, windowSince = 0, lastSwitch = 0;
bool switchedOnce = false;
bool windowOpen = false, sensorFault = false;
domo::WindowDetector window(6, WINDOW_DROP_C);  // une mesure par minute sur 5 min

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Wire.begin(PIN_SDA, PIN_SCL);
  bmeOk = bme.begin(0x76) || bme.begin(0x77);

  prefs.begin("heating", false);
  setpoint = prefs.getFloat("setpoint", 19.0f);
  enabled = prefs.getBool("enabled", true);
  offset = prefs.getFloat("offset", 0);

  core.addEntity("sensor", "temperature", "Température", "temperature", "°C");
  core.addEntity("sensor", "humidity", "Humidité", "humidity", "%");
  // Entité thermostat HA (carte thermostat, modes Chauffe / Arrêt ; "Arrêt" garde le hors-gel)
  core.addEntity("climate", "thermostat", "Thermostat", nullptr, nullptr, R"({
    "modes": ["off", "heat"], "min_temp": 10, "max_temp": 25, "temp_step": 0.5,
    "precision": 0.1, "temperature_unit": "C",
    "current_temperature_template": "{{ value_json.temperature }}",
    "temperature_state_template": "{{ value_json.setpoint }}",
    "temperature_command_template": "{\"setpoint\": {{ value }} }",
    "mode_state_template": "{{ 'heat' if value_json.enabled else 'off' }}",
    "mode_command_template": "{\"enabled\": {{ 'true' if value == 'heat' else 'false' }} }",
    "action_template": "{{ 'heating' if value_json.heating else ('idle' if value_json.enabled else 'off') }}"
  })");
  core.addEntity("binary_sensor", "heating", "En chauffe", "heat");
  core.addEntity("number", "temp_offset", "Correction température", nullptr, "°C", "{\"min\":-5,\"max\":5,\"step\":0.1,\"mode\":\"box\",\"entity_category\":\"config\"}");
  core.addEntity("binary_sensor", "sensor_fault", "Défaut capteur", "problem",
                 nullptr, "{\"entity_category\":\"diagnostic\"}");
  core.addEntity("binary_sensor", "window", "Fenêtre ouverte détectée", "window");
  core.onCommand([](JsonDocument& d) {
    if (d["setpoint"].is<float>()) {
      setpoint = constrain((float)d["setpoint"], 10.0f, 25.0f);
      prefs.putFloat("setpoint", setpoint);
    }
    if (d["enabled"].is<bool>()) { enabled = d["enabled"]; prefs.putBool("enabled", enabled); }
    if (d["temp_offset"].is<float>()) {
      offset = constrain((float)d["temp_offset"], -5.0f, 5.0f);
      prefs.putFloat("offset", offset);
      window.reset();  // le saut de mesure ne doit pas passer pour une fenêtre ouverte
    }
    dirty = true;
  });
  core.begin();
}

void loop() {
  core.loop();
  static unsigned long lastRead = 0;
  static unsigned long lastRetry = 0;
  if (!bmeOk && millis() - lastRetry >= 30000) {  // capteur absent au démarrage ou rebranché
    lastRetry = millis();
    bmeOk = bme.begin(0x76) || bme.begin(0x77);
  }
  if (bmeOk && millis() - lastRead >= 5000) {
    lastRead = millis();
    float t = bme.readTemperature();
    // Capteur débranché = valeurs aberrantes (pas forcément NaN) : on les rejette.
    if (t > -20.0f && t < 60.0f) { temp = t + offset; lastValid = millis(); }
  }
  // Plus de mesure valide depuis 2 min : on considère le capteur muet.
  if (!isnan(temp) && millis() - lastValid > 120000UL) temp = NAN;

  static unsigned long lastHist = 0;
  if (!isnan(temp) && millis() - lastHist >= 60000UL) {
    lastHist = millis();
    if (window.add(temp) && !windowOpen) {
      windowOpen = true;
      windowSince = millis();
      dirty = true;
    }
  }
  if (windowOpen && millis() - windowSince >= WINDOW_PAUSE_MS) {
    windowOpen = false;
    window.reset();  // repartir d'un historique propre (la pièce remonte en température)
    dirty = true;
  }

  // Sécurité : sans mesure valide (capteur muet) le relais est coupé
  float target = (enabled && !windowOpen) ? setpoint : FROST_C;
  bool want = domo::thermostatWant(temp, target, HYSTERESIS, heating);
  // Anti-cycles courts (usure chaudière/relais), sauf coupure de sécurité capteur muet
  bool cycleOk = !switchedOnce || millis() - lastSwitch >= MIN_CYCLE_MS || isnan(temp);
  if (want != heating && cycleOk) {
    heating = want;
    lastSwitch = millis();
    switchedOnce = true;
    digitalWrite(RELAY_PIN, heating ? HIGH : LOW);
    dirty = true;
  }

  // Publié même capteur muet : null -> HA affiche "inconnu" (pas la dernière valeur retenue) et
  // "Défaut capteur" -> le proche sait que le chauffage est coupé par sécurité.
  bool faultNow = isnan(temp) && millis() > 15000;  // pas de faux défaut pendant le démarrage
  if (faultNow != sensorFault) { sensorFault = faultNow; dirty = true; }
  if (dirty || millis() - lastSend >= SEND_INTERVAL_MS) {
    JsonDocument doc;
    doc["temperature"] = nullptr;  // remplacés ci-dessous si la mesure est valide
    doc["humidity"] = nullptr;
    if (!isnan(temp)) {
      doc["temperature"] = roundf(temp * 10) / 10.0;
      doc["humidity"] = roundf(bme.readHumidity() * 10) / 10.0;
    }
    doc["sensor_fault"] = sensorFault;
    doc["setpoint"] = setpoint;
    doc["enabled"] = enabled;
    doc["heating"] = heating;
    doc["window"] = windowOpen;
    doc["temp_offset"] = offset;
    if (core.publishTelemetry(doc)) { lastSend = millis(); dirty = false; }
  }
}
