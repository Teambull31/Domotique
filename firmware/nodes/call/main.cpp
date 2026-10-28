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
RTC_DATA_ATTR static bool armedForPress = true;  // sommeil armé sur l'appui (sinon sur le relâchement)
volatile bool pressedAwake = false;             // appui pendant l'éveil (battement, WiFi lent)

void IRAM_ATTR onPress() { pressedAwake = true; }

// Envoie l'appel en attente ; true si rien n'est en attente ou si l'envoi a réussi.
bool sendPending(float battery) {
  if (!pending) return true;
  JsonDocument evt;
  evt["call"] = true;
  evt["battery"] = battery;
  if (!core.publishTelemetry(evt, false)) return false;  // non retenu : jamais rejoué par le broker
  pending = false;
  return true;
}

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
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool pressed = domo::appelDemande(digitalRead(BUTTON_PIN) == LOW,  // lu avant le WiFi
                                    cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_GPIO,
                                    armedForPress);
  if (pressed) pending = true;
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), onPress, FALLING);
  float battery = roundf(domoBatteryPercent(BATTERY_ADC));
  // off_delay : l'entité revient seule à "off" ; l'alerte côté HA reste active jusqu'à prise en compte
  core.addEntity("binary_sensor", "call", "Appel", "safety", nullptr, "{\"off_delay\":60}");
  core.addEntity("sensor", "battery", "Batterie", "battery", "%");
  core.setSleepy(HEARTBEAT_SEC);
  core.begin();

  bool ok = core.waitConnected(15000);
  ok = ok && sendPending(battery);
  if (ok) {
    JsonDocument doc;  // état de repos retenu (battement, niveau de pile)
    doc["call"] = false;
    doc["battery"] = battery;
    core.publishTelemetry(doc);
    core.flush();
  }
  // Appui pendant un réveil sans appel (battement de coeur, nouvel essai) : pas de réveil possible
  // pendant l'éveil, il serait perdu. Après un appel, ce sont les rebonds du relâchement : ignorés.
  if (pressedAwake && !pressed) {
    pressed = pending = true;
    ok = core.waitConnected(15000) && sendPending(battery);
    core.flush();
  }
  detachInterrupt(digitalPinToInterrupt(BUTTON_PIN));
  if (pressed) confirm(ok);
  failures = ok ? 0 : failures + 1;
  // Bouton encore enfoncé : réveil au relâchement (sans effet), puis au prochain appui.
  int level = digitalRead(BUTTON_PIN);
  armedForPress = level == HIGH;
  domoDeepSleep(BUTTON_PIN, level, domo::sleepSeconds(failures, HEARTBEAT_SEC));
}

void loop() {}
