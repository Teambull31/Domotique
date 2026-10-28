// Nœud "gate" : portail motorisé — état (fins de course) et commande par impulsion sur l'entrée
// « pas à pas » / « ouverture » de la motorisation (contact sec, comme le bouton de la télécommande).
// Relais (contact sec, isolé) : PIN_RELAY1 -> bornier impulsion de la motorisation.
// Fin de course « fermé » : contact (reed/fin de course) entre PIN_INPUT1 et GND, fermé = portail fermé.
// Fin de course « ouvert » (facultatif) : entre PIN_INPUT2 et GND ; à déclarer dans HA.
// Variante « télécommande sacrifiée » (portail non câblable) : le relais/optocoupleur appuie sur le
// bouton d'une télécommande appairée, et la position vient d'un capteur sur pile au portail, transmise
// par HA (« Position fournie par HA » + automatisation de packages/maison.yaml).
// Broches selon la carte : lib/DomoCore/src/DomoPins.h
// Les sécurités (cellules, détection d'obstacle) restent celles de la motorisation : ne rien shunter.
#include <DomoCore.h>
#include <DomoLogic.h>
#include <Preferences.h>

#define PULSE_PIN PIN_RELAY1
#define CLOSED_PIN PIN_INPUT1
#define OPEN_PIN PIN_INPUT2
#define PULSE_MS 500
#define DEBOUNCE_MS 100

DomoCore core("gate");
Preferences prefs;
float travelS = 25;          // durée d'une manœuvre complète (réglable dans HA)
bool hasOpenSensor = false;  // fin de course « ouvert » câblé (réglable dans HA)
bool extSensor = false;      // position « fermé » fournie par HA (capteur sur pile au portail)
bool closedExt = false;      // dernière position reçue de HA
bool unconfirmedFlag = false;
domo::GateLogic gate(25000);
unsigned long pulseEnd = 0, lastSend = 0;
bool dirty = true;

// Entrée filtrée (rebonds) : état stable du contact (true = fermé à la masse)
struct Debounced {
  explicit Debounced(int p) : pin(p) {}
  int pin;
  bool stable = false, last = false;
  unsigned long since = 0;
  bool read() {
    bool raw = digitalRead(pin) == LOW;
    if (raw != last) {
      last = raw;
      since = millis();
    }
    if (raw != stable && millis() - since >= DEBOUNCE_MS) stable = raw;
    return stable;
  }
};
Debounced closedIn(CLOSED_PIN), openIn(OPEN_PIN);

domo::GateLogic::State current() {
  bool closed = extSensor ? closedExt : closedIn.read();
  int open = (hasOpenSensor && !extSensor) ? (openIn.read() ? 1 : 0) : -1;
  return gate.state(closed, open, millis());
}

void pulse() {
  digitalWrite(PULSE_PIN, HIGH);
  pulseEnd = millis() + PULSE_MS;
  dirty = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(PULSE_PIN, OUTPUT);
  digitalWrite(PULSE_PIN, LOW);
  pinMode(CLOSED_PIN, INPUT_PULLUP);
  pinMode(OPEN_PIN, INPUT_PULLUP);
  prefs.begin("gate", false);
  travelS = domo::sanitize(prefs.getFloat("travel_s", 25), 5, 120, 25);
  hasOpenSensor = prefs.getBool("open_sensor", false);
  extSensor = prefs.getBool("ext_sensor", false);
  gate.setTravel(travelS * 1000UL);
  closedIn.stable = closedIn.last = digitalRead(CLOSED_PIN) == LOW;  // état initial sans attendre
  openIn.stable = openIn.last = digitalRead(OPEN_PIN) == LOW;

  core.addEntity("cover", "gate", "Portail", "gate");
  core.addEntity("number", "travel_s", "Durée de manœuvre", "duration", "s",
                 "{\"min\":5,\"max\":120,\"step\":1,\"mode\":\"box\",\"entity_category\":\"config\"}");
  core.addEntity("switch", "open_sensor", "Fin de course ouvert installé", nullptr, nullptr,
                 "{\"entity_category\":\"config\"}");
  core.addEntity("switch", "ext_sensor", "Position fournie par HA", nullptr, nullptr,
                 "{\"entity_category\":\"config\"}");
  core.addEntity("binary_sensor", "unconfirmed", "Manœuvre non confirmée", "problem");
  core.onCommand([](JsonDocument& d) {
    const char* c = d["gate"] | "";
    domo::GateLogic::State s = current();
    bool go = (!strcmp(c, "open") && gate.command(domo::GateLogic::CMD_OPEN, s, millis())) ||
              (!strcmp(c, "close") && gate.command(domo::GateLogic::CMD_CLOSE, s, millis())) ||
              (!strcmp(c, "stop") && gate.command(domo::GateLogic::CMD_STOP, s, millis()));
    if (go) pulse();
    if (d["travel_s"].is<float>()) {
      travelS = domo::sanitize(d["travel_s"], 5, 120, travelS);
      prefs.putFloat("travel_s", travelS);
      gate.setTravel(travelS * 1000UL);
    }
    if (d["ext_sensor"].is<bool>()) {
      extSensor = d["ext_sensor"];
      prefs.putBool("ext_sensor", extSensor);
    }
    if (d["closed_ext"].is<bool>()) closedExt = d["closed_ext"];  // envoyé (retenu) par HA
    if (d["open_sensor"].is<bool>()) {
      hasOpenSensor = d["open_sensor"];
      prefs.putBool("open_sensor", hasOpenSensor);
    }
    dirty = true;
  });
  // OTA ou redémarrage pendant une impulsion : relâcher le contact
  core.onSafeState([] { digitalWrite(PULSE_PIN, LOW); });
  core.begin();
}

void loop() {
  core.loop();
  if (pulseEnd && (long)(millis() - pulseEnd) >= 0) {
    digitalWrite(PULSE_PIN, LOW);
    pulseEnd = 0;
  }
  static domo::GateLogic::State last = domo::GateLogic::STOPPED;
  domo::GateLogic::State s = current();
  if (s != last) {
    last = s;
    dirty = true;
  }
  bool unc = gate.unconfirmed(s, millis());
  if (unc != unconfirmedFlag) {
    unconfirmedFlag = unc;
    dirty = true;
  }

  bool moving = s == domo::GateLogic::OPENING || s == domo::GateLogic::CLOSING;
  if (dirty || millis() - lastSend >= (moving ? 2000UL : 60000UL)) {
    JsonDocument doc;
    doc["gate"] = domo::GateLogic::name(s);
    doc["travel_s"] = travelS;
    doc["open_sensor"] = hasOpenSensor;
    doc["ext_sensor"] = extSensor;
    doc["unconfirmed"] = unconfirmedFlag;
    if (core.publishTelemetry(doc)) {
      lastSend = millis();
      dirty = false;
    }
  }
}
