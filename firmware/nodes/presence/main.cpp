// Nœud "presence" : radar mmWave LD2410 (détecte aussi une personne immobile) + relais éclairage.
// LD2410 : TX -> PIN_UART_RX, RX -> PIN_UART_TX, alim 5 V.  Relais lumière : PIN_RELAY1.
// Broches selon la carte : lib/DomoCore/src/DomoPins.h
// Résilience : si Home Assistant / le WiFi tombent, le mode auto continue en local.
#include <DomoCore.h>
#include <Preferences.h>
#include <ld2410.h>

#define RELAY_PIN PIN_RELAY1
#define SEND_INTERVAL_MS 30000
#define ABSENT_HOLD_MS 10000  // absence confirmée après 10 s sans détection (anti-clignotement)

DomoCore core("presence");
ld2410 radar;
bool radarOk = false;
bool lightOn = false;
bool autoMode = true;
bool present = false;
bool manual = false;  // lumière forcée depuis HA : l'auto reprend quand la pièce est vide
unsigned long lastPresence = 0, lastSend = 0, lastRadarOk = 0, lastRetry = 0;
bool radarFault = false;
Preferences prefs;
float offMin = 5;  // extinction N min après la dernière présence (réglable dans HA)
#define OFF_DELAY_MS ((unsigned long)(offMin * 60000UL))
bool dirty = true;

void setLight(bool on) {
  if (on == lightOn) return;
  lightOn = on;
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
  dirty = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Serial1.begin(256000, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  radarOk = radar.begin(Serial1);
  prefs.begin("presence", false);
  offMin = prefs.getFloat("off_min", 5);

  core.addEntity("binary_sensor", "presence", "Présence", "occupancy");
  core.addEntity("sensor", "distance", "Distance", "distance", "cm");
  core.addEntity("switch", "light", "Lumière");
  core.addEntity("switch", "auto", "Mode automatique");
  core.addEntity("number", "off_min", "Extinction après", "duration", "min",
                 "{\"min\":1,\"max\":60,\"step\":1,\"mode\":\"box\",\"entity_category\":\"config\"}");
  core.addEntity("binary_sensor", "radar_fault", "Défaut radar", "problem",
                 nullptr, "{\"entity_category\":\"diagnostic\"}");
  core.onCommand([](JsonDocument& d) {
    if (d["auto"].is<bool>()) { autoMode = d["auto"]; dirty = true; }
    if (d["light"].is<bool>()) { manual = true; setLight(d["light"]); }
    if (d["off_min"].is<float>()) {
      offMin = constrain((float)d["off_min"], 1.0f, 60.0f);
      prefs.putFloat("off_min", offMin);
      dirty = true;
    }
  });
  core.begin();
}

void loop() {
  core.loop();
  static float distance = 0;
  radar.read();
  // Radar absent au démarrage ou débranché : nouvel essai toutes les 30 s
  if (!radarOk && millis() - lastRetry > 30000) {
    lastRetry = millis();
    radarOk = radar.begin(Serial1);
  }
  if (radarOk && radar.isConnected()) lastRadarOk = millis();
  // Défaut signalé à HA : sans radar, "pas de présence" ne veut rien dire (alerte inactivité)
  bool fault = millis() - lastRadarOk > 10000;
  if (fault != radarFault) { radarFault = fault; dirty = true; }
  if (radarOk && radar.isConnected()) {
    bool detected = radar.presenceDetected();
    if (detected) {
      lastPresence = millis();
      distance = radar.movingTargetDetected() ? radar.movingTargetDistance()
                                              : radar.stationaryTargetDistance();
    } else {
      distance = 0;
    }
    bool now = detected || (lastPresence && millis() - lastPresence < ABSENT_HOLD_MS);
    if (now != present) { present = now; dirty = true; }
    // Forçage manuel (ex. lumière éteinte pour dormir) respecté tant que la pièce est occupée
    if (manual && !present && millis() - lastPresence > OFF_DELAY_MS) manual = false;
    if (autoMode && !manual) {
      if (present) setLight(true);
      else if (millis() - lastPresence > OFF_DELAY_MS) setLight(false);
    }
  }

  if (dirty || millis() - lastSend >= SEND_INTERVAL_MS) {
    JsonDocument doc;
    // Radar en défaut : présence "inconnue" dans HA. Une valeur figée à "présent" masquerait
    // une vraie inactivité (fausse tranquillité pour le proche).
    doc["presence"] = nullptr;
    doc["distance"] = nullptr;
    if (!radarFault) {
      doc["presence"] = present;
      doc["distance"] = distance;
    }
    doc["light"] = lightOn;
    doc["auto"] = autoMode;
    doc["radar_fault"] = radarFault;
    doc["off_min"] = offMin;
    if (core.publishTelemetry(doc)) { lastSend = millis(); dirty = false; }
  }
}
