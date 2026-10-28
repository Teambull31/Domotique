// Nœud "leak" : sonde de fuite d'eau sur pile (deep sleep), alerte immédiate au contact de l'eau.
// Module sonde : sortie digitale sur PIN_WAKE (HIGH = humide). Diviseur pile sur PIN_BATTERY.
// Broches selon la carte : lib/DomoCore/src/DomoPins.h
// Battement de coeur toutes les heures : HA détecte un capteur mort (batterie, WiFi).
#include <DomoCore.h>
#include <DomoLogic.h>
#include <DomoSleep.h>

#define PROBE_PIN PIN_WAKE
#define BATTERY_ADC PIN_BATTERY
#define HEARTBEAT_SEC 3600

DomoCore core("leak");
RTC_DATA_ATTR static unsigned failures = 0;  // envois ratés d'affilée (conservé en sommeil)

void setup() {
  Serial.begin(115200);
  pinMode(PROBE_PIN, INPUT);
  // Confirmation : humide sur 2 lectures à 300 ms d'écart (goutte de condensation, parasite)
  int level = digitalRead(PROBE_PIN);
  if (level == HIGH) {
    delay(300);
    level = digitalRead(PROBE_PIN);
  }
  float battery = roundf(domoBatteryPercent(BATTERY_ADC));  // avant le WiFi (chute de tension)
  core.addEntity("binary_sensor", "wet", "Fuite", "moisture");
  core.addEntity("sensor", "battery", "Batterie", "battery", "%");
  core.setSleepy(HEARTBEAT_SEC);
  core.begin();

  JsonDocument doc;
  doc["wet"] = (level == HIGH);
  doc["battery"] = battery;
  bool ok = core.waitConnected(15000) && core.publishTelemetry(doc);
  if (ok) {
    core.flush();
    int now = digitalRead(PROBE_PIN);
    if (now != level) {  // sonde séchée (ou mouillée) pendant la connexion : publier l'état final,
      doc["wet"] = (now == HIGH);  // sinon "fuite" resterait affichée (et l'alerte répétée) 1 h
      core.publishTelemetry(doc);
      core.flush();
    }
  }
  failures = ok ? 0 : failures + 1;
  domoDeepSleep(PROBE_PIN, digitalRead(PROBE_PIN), domo::sleepSeconds(failures, HEARTBEAT_SEC));
}

void loop() {}
