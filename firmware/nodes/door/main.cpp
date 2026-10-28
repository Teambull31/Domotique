// Nœud "door" : contact d'ouverture porte/fenêtre sur pile (deep sleep).
// Contact reed entre PIN_WAKE et GND (fermé = porte fermée = broche à 0).
// Diviseur 2x100k de la pile vers PIN_BATTERY. Broches selon la carte : lib/DomoCore/src/DomoPins.h
#include <DomoCore.h>
#include <DomoLogic.h>
#include <DomoSleep.h>

#define CONTACT_PIN PIN_WAKE
#define BATTERY_ADC PIN_BATTERY
#define HEARTBEAT_SEC 3600

DomoCore core("door");
RTC_DATA_ATTR static unsigned failures = 0;  // envois ratés d'affilée (conservé en sommeil)

void setup() {
  Serial.begin(115200);
  pinMode(CONTACT_PIN, INPUT_PULLUP);
  delay(20);  // rebonds du contact reed
  // Lu AVANT la connexion WiFi (2 à 5 s) : une porte ouverte puis vite refermée doit être signalée.
  int level = digitalRead(CONTACT_PIN);  // HIGH = contact écarté = ouvert
  float battery = roundf(domoBatteryPercent(BATTERY_ADC));  // avant le WiFi (chute de tension)
  core.addEntity("binary_sensor", "open", "Ouverture", "door");
  core.addEntity("sensor", "battery", "Batterie", "battery", "%");
  core.setSleepy(HEARTBEAT_SEC);
  core.begin();

  JsonDocument doc;
  doc["open"] = (level == HIGH);
  doc["battery"] = battery;
  bool ok = core.waitConnected(15000) && core.publishTelemetry(doc);
  if (ok) {
    core.flush();
    int now = digitalRead(CONTACT_PIN);
    if (now != level) {  // état changé pendant la connexion : publier l'état final aussi
      doc["open"] = (now == HIGH);
      core.publishTelemetry(doc);
      core.flush();
    }
  }
  // Envoi raté : nouvel essai rapide (alerte de nuit). Relire l'état juste avant de dormir : s'il a
  // changé entre-temps, le réveil est immédiat.
  failures = ok ? 0 : failures + 1;
  domoDeepSleep(CONTACT_PIN, digitalRead(CONTACT_PIN), domo::sleepSeconds(failures, HEARTBEAT_SEC));
}

void loop() {}
