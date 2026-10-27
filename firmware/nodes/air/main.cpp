// Nœud "air" : CO2 + température + humidité (SCD40, I2C) et particules PM1/2.5/10 (PMS5003, UART).
// SCD40 : I2C PIN_SDA/PIN_SCL.  PMS5003 : TX capteur -> PIN_UART_RX, RX capteur <- PIN_UART_TX (veille), alim 5 V.
// Broches selon la carte : lib/DomoCore/src/DomoPins.h
// Le PMS5003 (ventilateur, ~8000 h de vie) est mis en veille entre deux mesures de particules.
#include <Adafruit_PM25AQI.h>
#include <DomoCore.h>
#include <SensirionI2CScd4x.h>
#include <Wire.h>

#define SEND_INTERVAL_MS 30000
#define PM_PERIOD_MS (5UL * 60UL * 1000UL)  // une mesure de particules toutes les 5 min
#define PM_WARMUP_MS 30000UL                // le ventilateur doit tourner 30 s avant une mesure fiable

DomoCore core("air");
SensirionI2CScd4x scd4x;
Adafruit_PM25AQI pms;
bool pmsOk = false, pmAwake = false, pmFlushed = false, havePm = false;
uint16_t pm1 = 0, pm25 = 0, pm10 = 0;
unsigned long lastSend = 0, pmWakeAt = 0, pmSleptAt = 0, lastPmAt = 0;
int scdFailures = 0;

void pmsCommand(bool wake) {
  const uint8_t cmd[] = {0x42, 0x4D, 0xE4, 0x00, (uint8_t)wake, 0x01, (uint8_t)(wake ? 0x74 : 0x73)};
  Serial1.write(cmd, sizeof(cmd));
  pmAwake = wake;
  if (wake) { pmWakeAt = millis(); pmFlushed = false; }
  else pmSleptAt = millis();
}

void setup() {
  Serial.begin(115200);
  Wire.begin(PIN_SDA, PIN_SCL);
  scd4x.begin(Wire);
  scd4x.stopPeriodicMeasurement();
  scd4x.startPeriodicMeasurement();

  Serial1.begin(9600, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  pmsOk = pms.begin_UART(&Serial1);
  if (pmsOk) pmsCommand(true);  // le capteur a pu rester en veille avant un redémarrage de l'ESP32

  core.addEntity("sensor", "co2", "CO2", "carbon_dioxide", "ppm");
  core.addEntity("sensor", "temperature", "Température", "temperature", "°C");
  core.addEntity("sensor", "humidity", "Humidité", "humidity", "%");
  core.addEntity("sensor", "pm1", "PM1", "pm1", "µg/m³");
  core.addEntity("sensor", "pm25", "PM2.5", "pm25", "µg/m³");
  core.addEntity("sensor", "pm10", "PM10", "pm10", "µg/m³");
  core.addEntity("binary_sensor", "sensor_fault", "Défaut capteur", "problem",
                 nullptr, "{\"entity_category\":\"diagnostic\"}");
  core.begin();
}

void loop() {
  core.loop();

  if (pmsOk) {
    if (!pmAwake && millis() - pmSleptAt >= PM_PERIOD_MS - PM_WARMUP_MS) pmsCommand(true);
    if (pmAwake && millis() - pmWakeAt >= PM_WARMUP_MS) {
      if (!pmFlushed) {  // jeter les trames du préchauffage restées dans le tampon UART
        while (Serial1.available()) Serial1.read();
        pmFlushed = true;
      }
      PM25_AQI_Data d;
      if (pms.read(&d)) {
        pm1 = d.pm10_env;
        pm25 = d.pm25_env;
        pm10 = d.pm100_env;
        havePm = true;
        lastPmAt = millis();
        pmsCommand(false);
      } else if (millis() - pmWakeAt > PM_WARMUP_MS + 10000) {
        pmsCommand(false);  // pas de trame valide : on réessaiera au prochain cycle
      }
    }
  }

  if (millis() - lastSend < SEND_INTERVAL_MS) return;
  lastSend = millis();
  // Mesure absente ou trop ancienne : null ("inconnu" dans HA) plutôt que la dernière valeur
  // retenue, sur laquelle l'alerte "air confiné" se baserait.
  JsonDocument doc;
  for (const char* k : {"co2", "temperature", "humidity", "pm1", "pm25", "pm10"}) doc[k] = nullptr;
  uint16_t co2 = 0;
  float t = 0, h = 0;
  bool scdOk = scd4x.readMeasurement(co2, t, h) == 0 && co2 != 0;
  scdFailures = scdOk ? 0 : scdFailures + 1;
  if (scdOk) {
    doc["co2"] = co2;
    doc["temperature"] = roundf(t * 10) / 10.0;
    doc["humidity"] = roundf(h * 10) / 10.0;
  }
  bool pmFresh = havePm && millis() - lastPmAt < 3 * PM_PERIOD_MS;  // 3 cycles manqués = périmé
  if (pmFresh) {
    doc["pm1"] = pm1;
    doc["pm25"] = pm25;
    doc["pm10"] = pm10;
  }
  // Défaut : SCD40 muet 90 s, ou PMS5003 sans mesure depuis 15 min (hors démarrage)
  doc["sensor_fault"] = scdFailures >= 3 || (!pmFresh && millis() > 3 * PM_PERIOD_MS);
  core.publishTelemetry(doc);
}
