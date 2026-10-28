// Nœud "shutters" : un volet roulant filaire (2 relais montée/descente, interverrouillés).
// Relais : MONTEE=PIN_RELAY1, DESCENTE=PIN_RELAY2 (broches : lib/DomoCore/src/DomoPins.h).
// À raccorder par un électricien (230 V) avec carte relais à isolation renforcée ;
// ne jamais piloter les deux sens simultanément (logiciel + câblage).
// Position estimée au temps de course (0 = fermé, 100 = ouvert), mémorisée à chaque arrêt.
// Aller en butée (0 ou 100) prolonge la course pour recaler l'estimation.
// Durées de montée / descente réglables dans HA (Paramètres de l'appareil) : chronométrer chaque volet.
// Boutons muraux (facultatifs, fonctionnent sans WiFi ni HA) : poussoirs BASSE TENSION entre
// PIN_INPUT1 (montée) / PIN_INPUT2 (descente) et GND. Un appui : course complète ; un appui pendant
// la course : arrêt. Jamais l'inverseur 230 V existant relié à l'ESP32 : le remplacer par un
// poussoir double basse tension, ou passer par un module optocoupleur 230 V.
#include <DomoCore.h>
#include <DomoLogic.h>
#include <Preferences.h>

#define UP_PIN PIN_RELAY1
#define DOWN_PIN PIN_RELAY2
#define OVERRUN_MS 2000UL   // dépassement en butée pour se recaler
#define DEAD_TIME_MS 500UL  // pause obligatoire à l'inversion
#define BTN_UP_PIN PIN_INPUT1
#define BTN_DOWN_PIN PIN_INPUT2

DomoCore core("shutters");
Preferences prefs;
enum State { IDLE, OPENING, CLOSING } state = IDLE;
float pos = -1;  // -1 = position inconnue (premier démarrage)
float startPos = 0, target = 0;
unsigned long moveStart = 0, moveMs = 0, lastSend = 0;
bool dirty = true;
float upS = 25, downS = 25;  // durée de course complète (s), la descente est souvent plus rapide
domo::Debouncer btnUp(50), btnDown(50);

unsigned long travelMs(State dir) { return (dir == OPENING ? upS : downS) * 1000UL; }

float currentPos() {
  if (state == IDLE) return pos;
  return domo::shutterPosition(startPos, millis() - moveStart, travelMs(state), state == OPENING);
}

void halt() {
  digitalWrite(UP_PIN, LOW);
  digitalWrite(DOWN_PIN, LOW);
  if (state != IDLE) {
    pos = currentPos();
    state = IDLE;
    prefs.putFloat("pos", pos);
    prefs.putBool("moving", false);
  }
  dirty = true;
}

void moveTo(float tgt) {
  if (isnan(tgt)) return;  // ordre invalide : ignoré
  tgt = domo::sanitize(tgt, 0, 100, 0);
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
  moveMs = domo::shutterMoveMs(startPos, tgt, travelMs(dir), OVERRUN_MS);
  prefs.putBool("moving", true);  // coupure/redémarrage en route -> position inconnue au boot
  digitalWrite(dir == OPENING ? UP_PIN : DOWN_PIN, HIGH);
  state = dir;
  moveStart = millis();
  dirty = true;
}

// Bouton mural : à l'appui, arrêt si le volet bouge, sinon course complète vers `cible`
void bouton(domo::Debouncer& b, int pin, float cible) {
  if (!b.update(digitalRead(pin) == LOW, millis()) || !b.state()) return;
  if (state != IDLE) halt();
  else moveTo(cible);
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
  pinMode(BTN_UP_PIN, INPUT_PULLUP);  // sans bouton câblé : entrée au repos, aucun effet
  pinMode(BTN_DOWN_PIN, INPUT_PULLUP);
  btnUp.reset(digitalRead(BTN_UP_PIN) == LOW);  // bouton coincé au démarrage : pas de course
  btnDown.reset(digitalRead(BTN_DOWN_PIN) == LOW);
  prefs.begin("shutters", false);
  pos = prefs.getBool("moving", false) ? -1 : prefs.getFloat("pos", -1);
  if (!(pos >= 0 && pos <= 100)) pos = -1;  // valeur invalide (ou NaN) : position inconnue
  upS = domo::sanitize(prefs.getFloat("up_s", 25), 5, 120, 25);
  downS = domo::sanitize(prefs.getFloat("down_s", 25), 5, 120, 25);

  core.addEntity("cover", "cover", "Volet", "shutter");
  const char* travelCfg = "{\"min\":5,\"max\":120,\"step\":0.5,\"mode\":\"box\",\"entity_category\":\"config\"}";
  core.addEntity("number", "up_s", "Durée de montée", "duration", "s", travelCfg);
  core.addEntity("number", "down_s", "Durée de descente", "duration", "s", travelCfg);
  core.onCommand([](JsonDocument& d) {
    const char* c = d["cover"] | "";
    if (!strcmp(c, "open")) moveTo(100);
    else if (!strcmp(c, "close")) moveTo(0);
    else if (!strcmp(c, "stop")) halt();
    if (d["cover_pos"].is<float>()) moveTo(d["cover_pos"]);
    if (state == IDLE) {  // pas de changement de durée en pleine course (estimation faussée)
      if (d["up_s"].is<float>()) { upS = domo::sanitize(d["up_s"], 5, 120, upS); prefs.putFloat("up_s", upS); }
      if (d["down_s"].is<float>()) { downS = domo::sanitize(d["down_s"], 5, 120, downS); prefs.putFloat("down_s", downS); }
    }
    dirty = true;
  });
  // OTA ou redémarrage : arrêter le moteur et mémoriser la position (sinon course jusqu'en butée)
  core.onSafeState([] { halt(); });
  core.begin();
}

void loop() {
  core.loop();
  bouton(btnUp, BTN_UP_PIN, 100);
  bouton(btnDown, BTN_DOWN_PIN, 0);
  if (state != IDLE && millis() - moveStart >= moveMs) {
    halt();
    pos = target;  // butée ou cible atteinte
    prefs.putFloat("pos", pos);
  }

  if (dirty || millis() - lastSend >= (state == IDLE ? 60000UL : 1000UL)) {
    JsonDocument doc;
    doc["cover"] = stateName();
    if (pos >= 0 || state != IDLE) doc["cover_pos"] = (int)roundf(currentPos());
    doc["up_s"] = upS;
    doc["down_s"] = downS;
    if (core.publishTelemetry(doc)) { lastSend = millis(); dirty = false; }
  }
}
