// Nœud "call" : bouton d'appel sur pile (deep sleep), à garder sur soi ou près du lit.
// Bouton (gros, contrasté) entre PIN_WAKE et GND ; voyant + résistance 330 Ω entre PIN_LED et GND.
// Diviseur pile sur PIN_BATTERY. Broches selon la carte : lib/DomoCore/src/DomoPins.h
// Appui -> événement "appel" vers HA (alerte au proche répétée jusqu'à prise en compte) ; le voyant
// confirme : allumé 3 s = appel transmis, clignotement rapide = échec, nouvel essai automatique.
// Complément de confort : PAS une téléassistance certifiée (pas de secours, pas de liaison vocale).
#include <DomoCore.h>
#include <DomoLogic.h>
#include <DomoSleep.h>

#define BUTTON_PIN PIN_WAKE
#define BATTERY_ADC PIN_BATTERY
#define HEARTBEAT_SEC 3600

DomoCore core("call");
RTC_DATA_ATTR static unsigned failures = 0;   // envois ratés d'affilée (conservé en sommeil)
RTC_DATA_ATTR static bool pending = false;    // appel pas encore transmis : à renvoyer au réveil

void confirm(bool ok) {
  pinMode(PIN_LED, OUTPUT);
  if (ok) {
    digitalWrite(PIN_LED, HIGH);
    delay(3000);
  } else {
    for (int i = 0; i < 15; i++) {
      digitalWrite(PIN_LED, i % 2 ? LOW : HIGH);
      delay(100);
    }
  }
  digitalWrite(PIN_LED, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(20);  // rebonds
  bool pressed = digitalRead(BUTTON_PIN) == LOW;  // lu avant le WiFi
  if (pressed) pending = true;
  float battery = roundf(domoBatteryPercent(BATTERY_ADC));
  // off_delay : l'entité revient seule à "off" ; l'alerte côté HA reste active jusqu'à prise en compte
  core.addEntity("binary_sensor", "call", "Appel", "safety", nullptr, "{\"off_delay\":60}");
  core.addEntity("sensor", "battery", "Batterie", "battery", "%");
  core.setSleepy(HEARTBEAT_SEC);
  core.begin();

  bool ok = core.waitConnected(15000);
  if (ok && pending) {
    JsonDocument evt;
    evt["call"] = true;
    evt["battery"] = battery;
    ok = core.publishTelemetry(evt, false);  // événement non retenu : jamais rejoué par le broker
    if (ok) pending = false;
  }
  if (ok) {
    JsonDocument doc;  // état de repos retenu (battement, niveau de pile)
    doc["call"] = false;
    doc["battery"] = battery;
    core.publishTelemetry(doc);
    core.flush();
  }
  if (pressed) confirm(ok);
  failures = ok ? 0 : failures + 1;
  // Bouton encore enfoncé : réveil au relâchement (sans effet), puis au prochain appui.
  domoDeepSleep(BUTTON_PIN, digitalRead(BUTTON_PIN), domo::sleepSeconds(failures, HEARTBEAT_SEC));
}

void loop() {}
