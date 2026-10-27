// Nœud "climate" : température / humidité / pression (BME280, I2C).
// Câblage : SDA=PIN_SDA, SCL=PIN_SCL, VCC=3V3, GND (broches : lib/DomoCore/src/DomoPins.h).
#include <Adafruit_BME280.h>
#include <DomoCore.h>

#define SEND_INTERVAL_MS 30000

DomoCore core("climate");
Adafruit_BME280 bme;
bool bmeOk = false;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  bmeOk = bme.begin(0x76) || bme.begin(0x77);
  if (!bmeOk) Serial.println("BME280 introuvable");

  core.addEntity("sensor", "temperature", "Température", "temperature", "°C");
  core.addEntity("sensor", "humidity", "Humidité", "humidity", "%");
  core.addEntity("sensor", "pressure", "Pression", "pressure", "hPa");
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
  doc["temperature"] = roundf(t * 10) / 10.0;
  doc["humidity"]    = roundf(h * 10) / 10.0;
  doc["pressure"]    = roundf(p * 10) / 10.0;
  core.publishTelemetry(doc);
}
