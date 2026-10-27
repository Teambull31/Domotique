// Nœud "air" : CO2 + température + humidité (SCD40, I2C) et particules PM1/2.5/10 (PMS5003, UART2).
// SCD40 : I2C PIN_SDA/PIN_SCL.  PMS5003 : TX capteur -> PIN_UART_RX, alim 5 V.
// Broches selon la carte : lib/DomoCore/src/DomoPins.h
#include <Adafruit_PM25AQI.h>
#include <DomoCore.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>

#define SEND_INTERVAL_MS 30000

DomoCore core("air");
SensirionI2CScd4x scd4x;
Adafruit_PM25AQI pms;
bool pmsOk = false;
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  scd4x.begin(Wire);
  scd4x.stopPeriodicMeasurement();
  scd4x.startPeriodicMeasurement();

  Serial1.begin(9600, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  pmsOk = pms.begin_UART(&Serial1);

  core.addEntity("sensor", "co2", "CO2", "carbon_dioxide", "ppm");
  core.addEntity("sensor", "temperature", "Température", "temperature", "°C");
  core.addEntity("sensor", "humidity", "Humidité", "humidity", "%");
  core.addEntity("sensor", "pm1", "PM1", "pm1", "µg/m³");
  core.addEntity("sensor", "pm25", "PM2.5", "pm25", "µg/m³");
  core.addEntity("sensor", "pm10", "PM10", "pm10", "µg/m³");
  core.begin();
}

void loop() {
  core.loop();
  if (millis() - lastSend < SEND_INTERVAL_MS) return;

  JsonDocument doc;
  uint16_t co2 = 0;
  float t = 0, h = 0;
  if (scd4x.readMeasurement(co2, t, h) == 0 && co2 != 0) {
    doc["co2"] = co2;
    doc["temperature"] = roundf(t * 10) / 10.0;
    doc["humidity"] = roundf(h * 10) / 10.0;
  }
  PM25_AQI_Data d;
  if (pmsOk && pms.read(&d)) {
    doc["pm1"] = d.pm10_env;
    doc["pm25"] = d.pm25_env;
    doc["pm10"] = d.pm100_env;
  }
  if (doc.size() == 0) return;  // pas encore de mesure valide : réessayer au prochain tour
  lastSend = millis();
  core.publishTelemetry(doc);
}
