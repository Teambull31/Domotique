// Nœud "heating" : thermostat d'une pièce (BME280 + contact sec de relais vers l'entrée thermostat
// de la chaudière / vanne / convecteur). Hystérésis locale : fonctionne sans Home Assistant.
// Hors-gel (7 °C) toujours actif, même si le chauffage est désactivé.
// Fenêtre ouverte : chute de 1 °C en 5 min -> chauffe suspendue 30 min (hors-gel maintenu).
#include <Adafruit_BME280.h>
#include <DomoCore.h>
#include <Preferences.h>

#define RELAY_PIN PIN_RELAY_HEAT  // broches : lib/DomoCore/src/DomoPins.h
#define HYSTERESIS 0.3f
#define FROST_C 7.0f
#define SEND_INTERVAL_MS 30000
#define WINDOW_DROP_C 1.0f
#define WINDOW_PAUSE_MS (30UL * 60UL * 1000UL)

DomoCore core("heating");
Adafruit_BME280 bme;
Preferences prefs;
bool bmeOk = false, enabled = true, heating = false, dirty = true;
float setpoint = 19.0f, temp = NAN;
unsigned long lastSend = 0, lastValid = 0, windowSince = 0;
bool windowOpen = false;
float history[6];  // une mesure par minute sur 5 min
int histCount = 0;

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Wire.begin(PIN_SDA, PIN_SCL);
  bmeOk = bme.begin(0x76) || bme.begin(0x77);

  prefs.begin("heating", false);
  setpoint = prefs.getFloat("setpoint", 19.0f);
  enabled = prefs.getBool("enabled", true);

  core.addEntity("sensor", "temperature", "Température", "temperature", "°C");
  core.addEntity("sensor", "humidity", "Humidité", "humidity", "%");
  core.addEntity("number", "setpoint", "Consigne", "temperature", "°C",
                 "{\"min\":10,\"max\":25,\"step\":0.5}");
  core.addEntity("switch", "enabled", "Chauffage actif");
  core.addEntity("binary_sensor", "heating", "En chauffe", "heat");
  core.addEntity("binary_sensor", "window", "Fenêtre ouverte détectée", "window");
  core.onCommand([](JsonDocument& d) {
    if (d["setpoint"].is<float>()) {
      setpoint = constrain((float)d["setpoint"], 10.0f, 25.0f);
      prefs.putFloat("setpoint", setpoint);
    }
    if (d["enabled"].is<bool>()) { enabled = d["enabled"]; prefs.putBool("enabled", enabled); }
    dirty = true;
  });
  core.begin();
}

void loop() {
  core.loop();
  static unsigned long lastRead = 0;
  if (bmeOk && millis() - lastRead >= 5000) {
    lastRead = millis();
    float t = bme.readTemperature();
    // Capteur débranché = valeurs aberrantes (pas forcément NaN) : on les rejette.
    if (t > -20.0f && t < 60.0f) { temp = t; lastValid = millis(); }
  }
  // Plus de mesure valide depuis 2 min : on considère le capteur muet.
  if (!isnan(temp) && millis() - lastValid > 120000UL) temp = NAN;

  static unsigned long lastHist = 0;
  if (!isnan(temp) && millis() - lastHist >= 60000UL) {
    lastHist = millis();
    for (int i = 5; i > 0; i--) history[i] = history[i - 1];
    history[0] = temp;
    if (histCount < 6) histCount++;
    if (!windowOpen && histCount == 6 && history[5] - temp >= WINDOW_DROP_C) {
      windowOpen = true;
      windowSince = millis();
      dirty = true;
    }
  }
  if (windowOpen && millis() - windowSince >= WINDOW_PAUSE_MS) {
    windowOpen = false;
    histCount = 0;  // repartir d'un historique propre (la pièce remonte en température)
    dirty = true;
  }

  // Sécurité : sans mesure valide (capteur muet) le relais est coupé
  float target = (enabled && !windowOpen) ? setpoint : FROST_C;
  bool want = heating;
  if (isnan(temp)) want = false;
  else if (temp < target - HYSTERESIS) want = true;
  else if (temp > target + HYSTERESIS) want = false;
  if (want != heating) {
    heating = want;
    digitalWrite(RELAY_PIN, heating ? HIGH : LOW);
    dirty = true;
  }

  if ((dirty || millis() - lastSend >= SEND_INTERVAL_MS) && !isnan(temp)) {
    JsonDocument doc;
    doc["temperature"] = roundf(temp * 10) / 10.0;
    doc["humidity"] = roundf(bme.readHumidity() * 10) / 10.0;
    doc["setpoint"] = setpoint;
    doc["enabled"] = enabled;
    doc["heating"] = heating;
    doc["window"] = windowOpen;
    if (core.publishTelemetry(doc)) { lastSend = millis(); dirty = false; }
  }
}
