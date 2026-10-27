// Nœud "climate" : température / humidité / pression (BME280, I2C).
// Câblage : SDA=PIN_SDA, SCL=PIN_SCL, VCC=3V3, GND (broches : lib/DomoCore/src/DomoPins.h).
#include <Adafruit_BME280.h>
#include <DomoCore.h>
#include <DomoLogic.h>
#include <Preferences.h>

#define SEND_INTERVAL_MS 30000

DomoCore core("climate");
Adafruit_BME280 bme;
bool bmeOk = false;
unsigned long lastSend = 0;
Preferences prefs;
float offset = 0;  // correction (°C) : l'ESP32 chauffe le capteur s'il est trop proche

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  bmeOk = bme.begin(0x76) || bme.begin(0x77);
  if (!bmeOk) Serial.println("BME280 introuvable");

  core.addEntity("sensor", "temperature", "Température", "temperature", "°C");
  core.addEntity("sensor", "humidity", "Humidité", "humidity", "%");
  core.addEntity("sensor", "pressure", "Pression", "pressure", "hPa");
  core.addEntity("binary_sensor", "sensor_fault", "Défaut capteur", "problem",
                 nullptr, "{\"entity_category\":\"diagnostic\"}");
  core.addEntity("number", "temp_offset", "Correction température", nullptr, "°C", "{\"min\":-5,\"max\":5,\"step\":0.1,\"mode\":\"box\",\"entity_category\":\"config\"}");
  prefs.begin("climate", false);
  offset = domo::sanitize(prefs.getFloat("offset", 0), -5, 5, 0);
  core.onCommand([](JsonDocument& d) {
    if (d["temp_offset"].is<float>()) {
      offset = domo::sanitize(d["temp_offset"], -5, 5, offset);
      prefs.putFloat("offset", offset);
      lastSend = 0;  // valeur corrigée publiée tout de suite
    }
  });
  core.begin();
}

void loop() {
  core.loop();
  if (millis() - lastSend < SEND_INTERVAL_MS) return;
  lastSend = millis();
  // Capteur absent ou rebranché : nouvel essai à chaque intervalle plutôt qu'un nœud muet à vie
  if (!bmeOk) bmeOk = bme.begin(0x76) || bme.begin(0x77);

  JsonDocument doc;
  doc["temp_offset"] = offset;
  float t = NAN, h = NAN, p = NAN;
  if (bmeOk) {
    t = bme.readTemperature();
    h = bme.readHumidity();
    p = bme.readPressure() / 100.0f;
  }
  // Capteur absent, ou fil débranché = valeurs aberrantes (pas forcément NaN) : publier null
  // (HA affiche "inconnu" au lieu de garder la dernière valeur retenue) et le défaut.
  if (!(t > -40 && t < 85 && h >= 0 && h <= 100 && p > 300 && p < 1100)) {
    bmeOk = false;
    doc["temperature"] = nullptr;
    doc["humidity"] = nullptr;
    doc["pressure"] = nullptr;
    doc["sensor_fault"] = true;
    core.publishTelemetry(doc);
    return;
  }
  doc["sensor_fault"] = false;
  doc["temperature"] = roundf((t + offset) * 10) / 10.0;
  doc["humidity"]    = roundf(h * 10) / 10.0;
  doc["pressure"]    = roundf(p * 10) / 10.0;
  core.publishTelemetry(doc);
}
