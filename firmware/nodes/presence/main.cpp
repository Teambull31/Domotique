// Nœud "presence" : radar mmWave LD2410 (détecte aussi une personne immobile) + relais éclairage.
// LD2410 : TX -> GPIO16 (RX2), RX -> GPIO17 (TX2), alim 5 V.  Relais lumière : GPIO26.
// Résilience : si Home Assistant / le WiFi tombent, le mode auto continue en local.
#include <DomoCore.h>
#include <ld2410.h>

#define RELAY_PIN 26
#define OFF_DELAY_MS (5UL * 60UL * 1000UL)  // extinction 5 min après la dernière présence
#define SEND_INTERVAL_MS 30000

DomoCore core("presence");
ld2410 radar;
bool radarOk = false;
bool lightOn = false;
bool autoMode = true;
bool present = false;
unsigned long lastPresence = 0, lastSend = 0;
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
  Serial2.begin(256000, SERIAL_8N1, 16, 17);
  radarOk = radar.begin(Serial2);

  core.addEntity("binary_sensor", "presence", "Présence", "occupancy");
  core.addEntity("sensor", "distance", "Distance", "distance", "cm");
  core.addEntity("switch", "light", "Lumière");
  core.addEntity("switch", "auto", "Mode automatique");
  core.onCommand([](JsonDocument& d) {
    if (d["auto"].is<bool>()) { autoMode = d["auto"]; dirty = true; }
    if (d["light"].is<bool>()) { autoMode = false; setLight(d["light"]); }
  });
  core.begin();
}

void loop() {
  core.loop();
  static float distance = 0;
  radar.read();
  if (radarOk && radar.isConnected()) {
    bool now = radar.presenceDetected();
    if (now) {
      lastPresence = millis();
      distance = radar.movingTargetDetected() ? radar.movingTargetDistance()
                                              : radar.stationaryTargetDistance();
    }
    if (now != present) { present = now; dirty = true; }
    if (autoMode) {
      if (present) setLight(true);
      else if (millis() - lastPresence > OFF_DELAY_MS) setLight(false);
    }
  }

  if (dirty || millis() - lastSend >= SEND_INTERVAL_MS) {
    JsonDocument doc;
    doc["presence"] = present;
    doc["distance"] = distance;
    doc["light"] = lightOn;
    doc["auto"] = autoMode;
    if (core.publishTelemetry(doc)) { lastSend = millis(); dirty = false; }
  }
}
