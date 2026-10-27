#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

// Socle commun des nœuds : portail de configuration (WiFi/MQTT/pièce saisis au 1er démarrage,
// stockés en flash), MQTT avec LWT, OTA, auto-découverte Home Assistant.
// Topics : home/devices/<nœud>-<pièce>/{telemetry,status,command}
class DomoCore {
public:
  using CommandHandler = std::function<void(JsonDocument&)>;

  explicit DomoCore(const char* nodeType);

  // Bloquant au 1er démarrage (portail "Domo-<nœud>-setup"). Maintenir BOOT (GPIO0)
  // 3 s au démarrage efface la configuration.
  void begin();
  // Nœud sur pile (deep sleep) : à appeler avant begin(). Pas de LWT (sinon HA verrait le nœud
  // "hors ligne" à chaque sommeil) mais expire_after ; télémétrie retenue ; une fois configuré,
  // jamais de portail si le WiFi est absent (on se rendort au lieu de vider la pile).
  void setSleepy(uint32_t heartbeatSec) { _sleepySec = heartbeatSec; }
  void loop();

  // Déclare une entité Home Assistant (MQTT discovery), à appeler avant begin().
  // component : sensor | binary_sensor | switch | number | cover
  // key : clé JSON dans la télémétrie (et dans les commandes pour switch/number/cover).
  // extraJson : champs de discovery additionnels, ex. {"min":7,"max":25,"step":0.5}
  void addEntity(const char* component, const char* key, const char* name,
                 const char* deviceClass = nullptr, const char* unit = nullptr,
                 const char* extraJson = nullptr);

  void onCommand(CommandHandler h) { _handler = h; }
  bool publishTelemetry(JsonDocument& doc);
  bool connected() { return _mqtt.connected(); }
  // Pour nœuds sur pile : attendre la connexion MQTT, puis laisser partir les paquets.
  bool waitConnected(uint32_t timeoutMs);
  void flush(uint32_t ms = 300);
  const String& room() const { return _room; }

private:
  bool connectMQTT();
  void handleMessage(byte* payload, unsigned int len);
  void publishDiscovery();

  String _nodeType, _id, _room = "maison";
  String _host, _user, _pass, _ota;
  bool _otaOn = false;
  uint32_t _sleepySec = 0;
  uint16_t _port = 1883;
  String _tTelemetry, _tStatus, _tCommand;
  WiFiClient _wifi;
  PubSubClient _mqtt;
  CommandHandler _handler;
  unsigned long _lastTry = 0, _lastOnline = 0;
  struct Entity { String component, key, name, deviceClass, unit, extra; };
  std::vector<Entity> _entities;
};
