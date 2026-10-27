// Nœud "climate" : température / humidité / pression (BME280, I2C).
// Câblage : SDA=PIN_SDA, SCL=PIN_SCL, VCC=3V3, GND (broches : lib/DomoCore/src/DomoPins.h).
#include <Adafruit_BME280.h>
#include <DomoCore.h>
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
  core.addEntity("number", "temp_offset", "Correction température", nullptr, "°C", "{\"min\":-5,\"max\":5,\"step\":0.1,\"mode\":\"box\",\"entity_category\":\"config\"}");
  prefs.begin("climate", false);
  offset = prefs.getFloat("offset", 0);
  core.onCommand([](JsonDocument& d) {
    if (d["temp_offset"].is<float>()) {
      offset = constrain((float)d["temp_offset"], -5.0f, 5.0f);
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
  if (!bmeOk) return;

  float t = bme.readTemperature(), h = bme.readHumidity(), p = bme.readPressure() / 100.0f;
  // Fil débranché = valeurs aberrantes (pas forcément NaN) : on ne publie rien de faux
  if (!(t > -40 && t < 85 && h >= 0 && h <= 100 && p > 300 && p < 1100)) {
    bmeOk = false;
    return;
  }
  JsonDocument doc;
  doc["temperature"] = roundf((t + offset) * 10) / 10.0;
  doc["humidity"]    = roundf(h * 10) / 10.0;
  doc["pressure"]    = roundf(p * 10) / 10.0;
  doc["temp_offset"] = offset;
  core.publishTelemetry(doc);
}
