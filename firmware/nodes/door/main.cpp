// Nœud "door" : contact d'ouverture porte/fenêtre sur pile (deep sleep).
// Contact reed entre PIN_WAKE et GND (fermé = porte fermée = broche à 0).
// Diviseur 2x100k de la pile vers PIN_BATTERY. Broches selon la carte : lib/DomoCore/src/DomoPins.h
#include <DomoCore.h>
#include <DomoSleep.h>

#define CONTACT_PIN PIN_WAKE
#define BATTERY_ADC PIN_BATTERY
#define HEARTBEAT_SEC 3600

DomoCore core("door");

void setup() {
  Serial.begin(115200);
  pinMode(CONTACT_PIN, INPUT_PULLUP);
  core.addEntity("binary_sensor", "open", "Ouverture", "door");
  core.addEntity("sensor", "battery", "Batterie", "battery", "%");
  core.setSleepy(HEARTBEAT_SEC);
  core.begin();

  int level = digitalRead(CONTACT_PIN);  // HIGH = contact écarté = ouvert
  JsonDocument doc;
  doc["open"] = (level == HIGH);
  doc["battery"] = roundf(domoBatteryPercent(BATTERY_ADC));
  if (core.waitConnected(15000)) {
    core.publishTelemetry(doc);
    core.flush();
  }
  // Relire juste avant de dormir : si l'état a changé entre-temps, le réveil est immédiat.
  domoDeepSleep(CONTACT_PIN, digitalRead(CONTACT_PIN), HEARTBEAT_SEC);
}

void loop() {}
