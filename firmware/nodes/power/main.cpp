// Nœud "power" : tension / courant / puissance / énergie / fréquence (PZEM-004T v3, UART2).
// PZEM : TX -> PIN_UART_RX, RX -> PIN_UART_TX (broches : lib/DomoCore/src/DomoPins.h). Le PZEM mesure du 230 V : installation au tableau
// par un électricien, pince fournie (non invasive), ESP32 alimenté en 5 V TBTS séparé.
#include <DomoCore.h>
#include <PZEM004Tv30.h>

#define SEND_INTERVAL_MS 10000

DomoCore core("power");
PZEM004Tv30 pzem(Serial1, PIN_UART_RX, PIN_UART_TX);
unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  core.addEntity("sensor", "voltage", "Tension", "voltage", "V");
  core.addEntity("sensor", "current", "Courant", "current", "A");
  core.addEntity("sensor", "power", "Puissance", "power", "W");
  core.addEntity("sensor", "energy", "Énergie", "energy", "kWh");
  core.addEntity("sensor", "frequency", "Fréquence", "frequency", "Hz");
  core.addEntity("sensor", "pf", "Facteur de puissance", "power_factor", nullptr);
  core.begin();
}

void loop() {
  core.loop();
  if (millis() - lastSend < SEND_INTERVAL_MS) return;
  lastSend = millis();

  float v = pzem.voltage();
  if (isnan(v)) return;  // PZEM muet : on ne publie rien plutôt que des zéros trompeurs
  JsonDocument doc;
  doc["voltage"] = v;
  doc["current"] = pzem.current();
  doc["power"] = pzem.power();
  doc["energy"] = pzem.energy();
  doc["frequency"] = pzem.frequency();
  doc["pf"] = pzem.pf();
  core.publishTelemetry(doc);
}
