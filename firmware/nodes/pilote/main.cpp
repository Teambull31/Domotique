// Nœud "pilote" : radiateurs électriques par fil pilote (une zone : un ou plusieurs radiateurs reliés
// au même fil pilote). Modes Confort / Éco / Hors-gel / Arrêt, ou Auto = régulation avec une sonde
// BME280 dans la pièce (Confort sous la consigne, Éco au-dessus ; sonde muette -> Hors-gel).
// Sorties : PIN_RELAY1 -> optotriac de la demi-alternance positive, PIN_RELAY2 -> négative
// (MOC3041 + diode 1N4007 chacun, ou module « fil pilote » du commerce). 230 V : montage et
// raccordement par un électricien, module isolé, jamais de fil pilote relié à l'ESP32 directement.
// Sorties coupées (démarrage, panne, mise à jour) = Confort : le radiateur reprend son thermostat.
// Sonde (facultative, mode Auto) : SDA=PIN_SDA, SCL=PIN_SCL. Broches : lib/DomoCore/src/DomoPins.h
#include <Adafruit_BME280.h>
#include <DomoCore.h>
#include <DomoLogic.h>
#include <Preferences.h>

#define POS_PIN PIN_RELAY1
#define NEG_PIN PIN_RELAY2
#define HYSTERESIS 0.3f
#define SEND_INTERVAL_MS 30000

using domo::Pilote;

DomoCore core("pilote");
Adafruit_BME280 bme;
Preferences prefs;
bool bmeOk = false, dirty = true;
float consigne = 19, temp = NAN;
String mode = "Confort";  // Confort | Éco | Hors-gel | Arrêt | Auto
Pilote ordre = Pilote::CONFORT;
unsigned long lastSend = 0, lastValid = 0;

const char* nomOrdre(Pilote p) {
  switch (p) {
    case Pilote::ECO: return "Éco";
    case Pilote::HORS_GEL: return "Hors-gel";
    case Pilote::ARRET: return "Arrêt";
    default: return "Confort";
  }
}

void appliquer(Pilote p) {
  domo::PiloteSorties s = domo::piloteSorties(p);
  digitalWrite(POS_PIN, s.positive ? HIGH : LOW);
  digitalWrite(NEG_PIN, s.negative ? HIGH : LOW);
  if (p != ordre) {
    ordre = p;
    dirty = true;
  }
}

Pilote ordreVoulu() {
  if (mode == "Éco") return Pilote::ECO;
  if (mode == "Hors-gel") return Pilote::HORS_GEL;
  if (mode == "Arrêt") return Pilote::ARRET;
  if (mode == "Auto") return domo::piloteAuto(temp, consigne, HYSTERESIS, ordre);
  return Pilote::CONFORT;
}

void setup() {
  Serial.begin(115200);
  pinMode(POS_PIN, OUTPUT);
  pinMode(NEG_PIN, OUTPUT);
  appliquer(Pilote::CONFORT);
  Wire.begin(PIN_SDA, PIN_SCL);
  bmeOk = bme.begin(0x76) || bme.begin(0x77);

  prefs.begin("pilote", false);
  mode = prefs.getString("mode", "Confort");
  consigne = domo::sanitize(prefs.getFloat("consigne", 19), 10, 25, 19);

  core.addEntity("select", "mode", "Mode", nullptr, nullptr,
                 R"({"options": ["Confort", "Éco", "Hors-gel", "Arrêt", "Auto"], "icon": "mdi:radiator"})");
  core.addEntity("number", "consigne", "Consigne (mode Auto)", "temperature", "°C",
                 "{\"min\":10,\"max\":25,\"step\":0.5,\"mode\":\"box\"}");
  core.addEntity("sensor", "ordre", "Ordre envoyé", nullptr, nullptr, "{\"icon\":\"mdi:sine-wave\"}");
  core.addEntity("sensor", "temperature", "Température", "temperature", "°C");
  core.addEntity("binary_sensor", "sensor_fault", "Défaut capteur", "problem",
                 nullptr, "{\"entity_category\":\"diagnostic\"}");
  core.onCommand([](JsonDocument& d) {
    const char* m = d["mode"] | "";
    for (const char* ok : {"Confort", "Éco", "Hors-gel", "Arrêt", "Auto"}) {
      if (!strcmp(m, ok)) {
        mode = ok;
        prefs.putString("mode", mode);
      }
    }
    if (d["consigne"].is<float>()) {
      consigne = domo::sanitize(d["consigne"], 10, 25, consigne);
      prefs.putFloat("consigne", consigne);
    }
    dirty = true;
  });
  // OTA ou redémarrage : sorties coupées = Confort (thermostat du radiateur)
  core.onSafeState([] { appliquer(Pilote::CONFORT); });
  core.begin();
}

void loop() {
  core.loop();
  static unsigned long lastRead = 0, lastRetry = 0;
  if (!bmeOk && millis() - lastRetry >= 30000) {
    lastRetry = millis();
    bmeOk = bme.begin(0x76) || bme.begin(0x77);
  }
  if (bmeOk && millis() - lastRead >= 5000) {
    lastRead = millis();
    float t = bme.readTemperature();
    if (t > -20.0f && t < 60.0f) {
      temp = t;
      lastValid = millis();
    }
  }
  if (!isnan(temp) && millis() - lastValid > 120000UL) temp = NAN;  // sonde muette

  appliquer(ordreVoulu());

  // Défaut seulement si la sonde est utile (mode Auto) et après le démarrage
  bool fault = mode == "Auto" && isnan(temp) && millis() > 15000;
  if (dirty || millis() - lastSend >= SEND_INTERVAL_MS) {
    JsonDocument doc;
    doc["mode"] = mode;
    doc["consigne"] = consigne;
    doc["ordre"] = nomOrdre(ordre);
    doc["temperature"] = nullptr;
    if (!isnan(temp)) doc["temperature"] = roundf(temp * 10) / 10.0;
    doc["sensor_fault"] = fault;
    if (core.publishTelemetry(doc)) {
      lastSend = millis();
      dirty = false;
    }
  }
}
