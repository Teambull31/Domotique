// Nœud "shutters" : un volet roulant filaire (2 relais montée/descente, interverrouillés).
// Relais : MONTEE=PIN_RELAY1, DESCENTE=PIN_RELAY2 (broches : lib/DomoCore/src/DomoPins.h). À raccorder par un électricien (230 V) avec carte
// relais à isolation renforcée ; ne jamais piloter les deux sens simultanément (logiciel + câblage).
// Position estimée au temps de course (0 = fermé, 100 = ouvert), mémorisée à chaque arrêt.
// Aller en butée (0 ou 100) prolonge la course pour recaler l'estimation.
#include <DomoCore.h>
#include <Preferences.h>

#define UP_PIN PIN_RELAY1
#define DOWN_PIN PIN_RELAY2
#define TRAVEL_MS 25000UL   // durée de course complète, à mesurer sur place
#define OVERRUN_MS 2000UL   // dépassement en butée pour se recaler
#define DEAD_TIME_MS 500UL  // pause obligatoire à l'inversion

DomoCore core("shutters");
Preferences prefs;
enum State { IDLE, OPENING, CLOSING } state = IDLE;
float pos = -1;  // -1 = position inconnue (premier démarrage)
float startPos = 0, target = 0;
unsigned long moveStart = 0, moveMs = 0, lastSend = 0;
bool dirty = true;

float currentPos() {
  if (state == IDLE) return pos;
  float d = (millis() - moveStart) * 100.0f / TRAVEL_MS;
  return constrain(state == OPENING ? startPos + d : startPos - d, 0.0f, 100.0f);
}

void halt() {
  digitalWrite(UP_PIN, LOW);
  digitalWrite(DOWN_PIN, LOW);
  if (state != IDLE) {
    pos = currentPos();
    state = IDLE;
    prefs.putFloat("pos", pos);
  }
  dirty = true;
}

void moveTo(float tgt) {
  tgt = constrain(tgt, 0.0f, 100.0f);
  if (pos < 0 && state == IDLE) {  // position inconnue : course complète vers la butée la plus proche
    tgt = tgt >= 50 ? 100 : 0;
    pos = 100 - tgt;
  }
  float from = currentPos();
  bool end = (tgt == 0 || tgt == 100);
  if (!end && fabsf(tgt - from) < 1) return;
  State dir = (tgt > from || (end && tgt == 100)) ? OPENING : CLOSING;

  if (state != IDLE) {
    halt();
    delay(DEAD_TIME_MS);
  }
  startPos = pos;
  target = tgt;
  moveMs = fabsf(tgt - startPos) * TRAVEL_MS / 100 + (end ? OVERRUN_MS : 0);
  digitalWrite(dir == OPENING ? UP_PIN : DOWN_PIN, HIGH);
  state = dir;
  moveStart = millis();
  dirty = true;
}

const char* stateName() {
  if (state == OPENING) return "opening";
  if (state == CLOSING) return "closing";
  if (pos < 0) return "stopped";
  return pos > 0.5f ? "open" : "closed";
}

void setup() {
  Serial.begin(115200);
  pinMode(UP_PIN, OUTPUT);
  pinMode(DOWN_PIN, OUTPUT);
  digitalWrite(UP_PIN, LOW);
  digitalWrite(DOWN_PIN, LOW);
  prefs.begin("shutters", false);
  pos = prefs.getFloat("pos", -1);

  core.addEntity("cover", "cover", "Volet", "shutter");
  core.onCommand([](JsonDocument& d) {
    const char* c = d["cover"] | "";
    if (!strcmp(c, "open")) moveTo(100);
    else if (!strcmp(c, "close")) moveTo(0);
    else if (!strcmp(c, "stop")) halt();
    if (d["cover_pos"].is<float>()) moveTo(d["cover_pos"]);
  });
  core.begin();
}

void loop() {
  core.loop();
  if (state != IDLE && millis() - moveStart >= moveMs) {
    halt();
    pos = target;  // butée ou cible atteinte
    prefs.putFloat("pos", pos);
  }

  if (dirty || millis() - lastSend >= (state == IDLE ? 60000UL : 1000UL)) {
    JsonDocument doc;
    doc["cover"] = stateName();
    if (pos >= 0 || state != IDLE) doc["cover_pos"] = (int)roundf(currentPos());
    if (core.publishTelemetry(doc)) { lastSend = millis(); dirty = false; }
  }
}
