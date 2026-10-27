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
  if (!bmeOk || millis() - lastSend < SEND_INTERVAL_MS) return;
  lastSend = millis();

  JsonDocument doc;
  doc["temperature"] = roundf(bme.readTemperature() * 10) / 10.0;
  doc["humidity"]    = roundf(bme.readHumidity() * 10) / 10.0;
  doc["pressure"]    = roundf(bme.readPressure() / 10.0) / 10.0;
  core.publishTelemetry(doc);
}
