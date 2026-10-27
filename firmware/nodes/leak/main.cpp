// Nœud "leak" : sonde de fuite d'eau sur pile (deep sleep), alerte immédiate au contact de l'eau.
// Module sonde : sortie digitale sur PIN_WAKE (HIGH = humide). Diviseur pile sur PIN_BATTERY.
// Broches selon la carte : lib/DomoCore/src/DomoPins.h
// Battement de coeur toutes les heures : HA détecte un capteur mort (batterie, WiFi).
#include <DomoCore.h>
#include <DomoSleep.h>

#define PROBE_PIN PIN_WAKE
#define BATTERY_ADC PIN_BATTERY
#define HEARTBEAT_SEC 3600

DomoCore core("leak");

void setup() {
  Serial.begin(115200);
  pinMode(PROBE_PIN, INPUT);
  core.addEntity("binary_sensor", "wet", "Fuite", "moisture");
  core.addEntity("sensor", "battery", "Batterie", "battery", "%");
  core.setSleepy(HEARTBEAT_SEC);
  core.begin();

  int level = digitalRead(PROBE_PIN);
  JsonDocument doc;
  doc["wet"] = (level == HIGH);
  doc["battery"] = roundf(domoBatteryPercent(BATTERY_ADC));
  if (core.waitConnected(15000)) {
    core.publishTelemetry(doc);
    core.flush();
  }
  domoDeepSleep(PROBE_PIN, digitalRead(PROBE_PIN), HEARTBEAT_SEC);
}

void loop() {}
