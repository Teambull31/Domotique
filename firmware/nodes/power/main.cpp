// Nœud "power" : tension / courant / puissance / énergie / fréquence (PZEM-004T v3, UART2).
// PZEM : TX -> PIN_UART_RX, RX -> PIN_UART_TX (broches : lib/DomoCore/src/DomoPins.h). Le PZEM mesure du 230 V : installation au tableau
// par un électricien, pince fournie (non invasive), ESP32 alimenté en 5 V TBTS séparé.
#include <DomoCore.h>
#include <PZEM004Tv30.h>

#define SEND_INTERVAL_MS 10000

DomoCore core("power");
PZEM004Tv30 pzem(Serial1, PIN_UART_RX, PIN_UART_TX);
unsigned long lastSend = 0;
int failures = 0;  // lectures PZEM ratées d'affilée (une erreur isolée sur le bus est normale)

void setup() {
  Serial.begin(115200);
  core.addEntity("sensor", "voltage", "Tension", "voltage", "V");
  core.addEntity("sensor", "current", "Courant", "current", "A");
  core.addEntity("sensor", "power", "Puissance", "power", "W");
  core.addEntity("sensor", "energy", "Énergie", "energy", "kWh");
  core.addEntity("sensor", "frequency", "Fréquence", "frequency", "Hz");
  core.addEntity("sensor", "pf", "Facteur de puissance", "power_factor", nullptr);
  core.addEntity("binary_sensor", "sensor_fault", "Défaut capteur", "problem",
                 nullptr, "{\"entity_category\":\"diagnostic\"}");
  core.begin();
}

void loop() {
  core.loop();
  if (millis() - lastSend < SEND_INTERVAL_MS) return;
  lastSend = millis();

  float v = pzem.voltage();
  failures = isnan(v) ? failures + 1 : 0;
  if (isnan(v) && failures < 3) return;  // erreur isolée : on attend la lecture suivante
  JsonDocument doc;
  // PZEM muet 30 s : valeurs à null ("inconnu" dans HA) plutôt que la dernière puissance retenue,
  // qui ferait croire à une plaque de cuisson restée allumée (ou éteinte).
  if (isnan(v)) {
    for (const char* k : {"voltage", "current", "power", "energy", "frequency", "pf"}) doc[k] = nullptr;
    doc["sensor_fault"] = true;
    core.publishTelemetry(doc);
    return;
  }
  doc["sensor_fault"] = false;
  doc["voltage"] = v;
  doc["current"] = pzem.current();
  doc["power"] = pzem.power();
  doc["energy"] = pzem.energy();
  doc["frequency"] = pzem.frequency();
  doc["pf"] = pzem.pf();
  core.publishTelemetry(doc);
}
