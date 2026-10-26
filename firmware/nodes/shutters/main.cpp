// Nœud "shutters" : un volet roulant filaire (2 relais montée/descente, interverrouillés).
// Relais : MONTEE=GPIO26, DESCENTE=GPIO27. À raccorder par un électricien (230 V) avec carte
// relais à isolation renforcée ; ne jamais piloter les deux sens simultanément (logiciel + câblage).
#include <DomoCore.h>

#define UP_PIN 26
#define DOWN_PIN 27
#define TRAVEL_MS 25000UL   // durée de course complète, à mesurer sur place
#define DEAD_TIME_MS 500UL  // pause obligatoire à l'inversion

DomoCore core("shutters");
enum State { OPEN, CLOSED, OPENING, CLOSING, STOPPED } state = STOPPED;
unsigned long moveStart = 0, lastSend = 0;
bool dirty = true;

const char* stateName() {
  static const char* n[] = {"open", "closed", "opening", "closing", "stopped"};
  return n[state];
}

void halt(State finalState) {
  digitalWrite(UP_PIN, LOW);
  digitalWrite(DOWN_PIN, LOW);
  state = finalState;
  dirty = true;
}

void move(State dir) {
  halt(STOPPED);
  delay(DEAD_TIME_MS);
  digitalWrite(dir == OPENING ? UP_PIN : DOWN_PIN, HIGH);
  state = dir;
  moveStart = millis();
  dirty = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(UP_PIN, OUTPUT);
  pinMode(DOWN_PIN, OUTPUT);
  halt(STOPPED);

  core.addEntity("cover", "cover", "Volet", "shutter");
  core.onCommand([](JsonDocument& d) {
    const char* c = d["cover"] | "";
    if (!strcmp(c, "open")) move(OPENING);
    else if (!strcmp(c, "close")) move(CLOSING);
    else if (!strcmp(c, "stop")) halt(STOPPED);
  });
  core.begin();
}

void loop() {
  core.loop();
  if ((state == OPENING || state == CLOSING) && millis() - moveStart >= TRAVEL_MS)
    halt(state == OPENING ? OPEN : CLOSED);

  if (dirty || millis() - lastSend >= 60000) {
    JsonDocument doc;
    doc["cover"] = stateName();
    if (core.publishTelemetry(doc)) { lastSend = millis(); dirty = false; }
  }
}
