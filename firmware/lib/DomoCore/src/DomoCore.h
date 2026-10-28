#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>
#include "DomoPins.h"

#define DOMO_VERSION "0.9.0"  // affichée dans HA (appareil > version du logiciel)

// Socle commun des nœuds : portail de configuration (WiFi/MQTT/pièce saisis au 1er démarrage,
// stockés en flash), MQTT avec LWT, OTA, auto-découverte Home Assistant.
// Topics : home/devices/<nœud>-<pièce>/{telemetry,status,command}
class DomoCore {
public:
  using CommandHandler = std::function<void(JsonDocument&)>;

  explicit DomoCore(const char* nodeType);

  // Bloquant au 1er démarrage (portail "Domo-<nœud>-setup"). Ensuite, jamais de portail automatique.
  // BOOT pressé dans les 2 s après la mise sous tension : 3 s = portail, 10 s = effacement complet.
  void begin();
  // Nœud sur pile (deep sleep) : à appeler avant begin(). Pas de LWT (sinon HA verrait le nœud
  // "hors ligne" à chaque sommeil) mais expire_after ; télémétrie retenue ; une fois configuré,
  // jamais de portail si le WiFi est absent (on se rendort au lieu de vider la pile).
  void setSleepy(uint32_t heartbeatSec) { _sleepySec = heartbeatSec; }
  void loop();

  // Déclare une entité Home Assistant (MQTT discovery), à appeler avant begin().
  // component : sensor | binary_sensor | switch | number | select | cover | climate
  // key : clé JSON dans la télémétrie (et dans les commandes pour switch/number/cover).
  // extraJson : champs de discovery additionnels, ex. {"min":7,"max":25,"step":0.5}
  void addEntity(const char* component, const char* key, const char* name,
                 const char* deviceClass = nullptr, const char* unit = nullptr,
                 const char* extraJson = nullptr);

  void onCommand(CommandHandler h) { _handler = h; }
  // Mise en sécurité des actionneurs juste avant une mise à jour OTA ou un redémarrage décidé par
  // le socle (bouton HA, 15 min sans MQTT) : la boucle du nœud ne tourne plus ensuite.
  void onSafeState(std::function<void()> h) { _safe = h; }
  // retain=false pour un événement ponctuel (appui sur un bouton) : il ne doit pas être rejoué
  // au redémarrage de Home Assistant.
  bool publishTelemetry(JsonDocument& doc, bool retain = true);
  bool connected() { return _mqtt.connected(); }
  // Pour nœuds sur pile : attendre la connexion MQTT, puis laisser partir les paquets.
  bool waitConnected(uint32_t timeoutMs);
  void flush(uint32_t ms = 300);
  const String& room() const { return _room; }

private:
  bool connectMQTT();
  void handleMessage(byte* payload, unsigned int len);
  void publishDiscovery();
  void publishDiag();
  void addDevice(JsonDocument& doc);

  String _nodeType, _id, _room = "maison", _label;
  String _host, _user, _pass, _ota;
  bool _otaOn = false, _rediscover = false;
  uint32_t _sleepySec = 0;
  uint16_t _port = 1883;
  String _tTelemetry, _tStatus, _tCommand, _tDiag;
  WiFiClient _wifi;
  PubSubClient _mqtt;
  CommandHandler _handler;
  std::function<void()> _safe;
  void restart() {
    if (_safe) _safe();
    ESP.restart();
  }
  unsigned long _lastTry = 0, _lastOnline = 0, _lastDiag = 0, _restartAt = 0, _retryMs = 5000;
  struct Entity { String component, key, name, deviceClass, unit, extra; };
  std::vector<Entity> _entities;
};
