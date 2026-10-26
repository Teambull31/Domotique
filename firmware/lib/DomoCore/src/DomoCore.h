#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

// Socle commun des nœuds : WiFi, MQTT (LWT), OTA, auto-découverte Home Assistant.
// Topics : home/devices/<id>/{telemetry,status,command} (compatible dashboard existant)
class DomoCore {
public:
  using CommandHandler = std::function<void(JsonDocument&)>;

  DomoCore(const char* deviceId, const char* room);

  void begin();
  void loop();

  // Déclare une entité dans Home Assistant (MQTT discovery).
  // component: "sensor" | "binary_sensor" | "switch" | "light" ...
  // key: clé JSON dans la télémétrie. deviceClass/unit peuvent être nullptr.
  void addEntity(const char* component, const char* key, const char* name,
                 const char* deviceClass = nullptr, const char* unit = nullptr);

  void onCommand(CommandHandler h) { _handler = h; }
  bool publishTelemetry(JsonDocument& doc);
  bool connected() { return _mqtt.connected(); }

private:
  void connectWiFi();
  bool connectMQTT();
  void handleMessage(char* topic, byte* payload, unsigned int len);
  void publishDiscovery();

  const char* _id;
  const char* _room;
  String _tTelemetry, _tStatus, _tCommand;
  WiFiClient _wifi;
  PubSubClient _mqtt;
  CommandHandler _handler;
  unsigned long _lastTry = 0;
  struct Entity { String component, key, name, deviceClass, unit; };
  std::vector<Entity> _entities;
};
