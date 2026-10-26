#include "DomoCore.h"
#include <ArduinoOTA.h>

DomoCore::DomoCore(const char* deviceId, const char* room)
    : _id(deviceId), _room(room), _mqtt(_wifi) {
  String base = String("home/devices/") + deviceId;
  _tTelemetry = base + "/telemetry";
  _tStatus    = base + "/status";
  _tCommand   = base + "/command";
}

void DomoCore::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(_id);
  connectWiFi();

  ArduinoOTA.setHostname(_id);
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.begin();

  _mqtt.setServer(MQTT_HOST, MQTT_PORT);
  _mqtt.setBufferSize(1024);
  _mqtt.setKeepAlive(30);
  _mqtt.setCallback([this](char* t, byte* p, unsigned int l) { handleMessage(t, p, l); });
}

void DomoCore::connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi %s", WIFI_SSID);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) Serial.printf(" OK %s\n", WiFi.localIP().toString().c_str());
  else Serial.println(" échec, nouvel essai plus tard");
}

bool DomoCore::connectMQTT() {
  const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
  const char* pass = strlen(MQTT_PASS) ? MQTT_PASS : nullptr;
  if (!_mqtt.connect(_id, user, pass, _tStatus.c_str(), 1, true, "offline")) {
    Serial.printf("MQTT rc=%d\n", _mqtt.state());
    return false;
  }
  _mqtt.publish(_tStatus.c_str(), "online", true);
  _mqtt.subscribe(_tCommand.c_str());
  publishDiscovery();
  return true;
}

void DomoCore::loop() {
  ArduinoOTA.handle();
  // Reconnexions non bloquantes (essai toutes les 5 s)
  if (WiFi.status() != WL_CONNECTED || !_mqtt.connected()) {
    if (millis() - _lastTry < 5000) return;
    _lastTry = millis();
    if (WiFi.status() != WL_CONNECTED) { connectWiFi(); return; }
    if (!connectMQTT()) return;
  }
  _mqtt.loop();
}

void DomoCore::handleMessage(char*, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return;
  if (_handler) _handler(doc);
}

bool DomoCore::publishTelemetry(JsonDocument& doc) {
  if (!_mqtt.connected()) return false;
  doc["room"] = _room;
  String out;
  serializeJson(doc, out);
  return _mqtt.publish(_tTelemetry.c_str(), out.c_str());
}

void DomoCore::addEntity(const char* component, const char* key, const char* name,
                         const char* deviceClass, const char* unit) {
  _entities.push_back({component, key, name, deviceClass ? deviceClass : "", unit ? unit : ""});
}

void DomoCore::publishDiscovery() {
  for (auto& e : _entities) {
    JsonDocument doc;
    String uid = String(_id) + "_" + e.key;
    doc["name"] = e.name;
    doc["unique_id"] = uid;
    doc["state_topic"] = _tTelemetry;
    doc["value_template"] = String("{{ value_json.") + e.key + " }}";
    doc["availability_topic"] = _tStatus;
    if (e.deviceClass.length()) doc["device_class"] = e.deviceClass;
    if (e.unit.length()) doc["unit_of_measurement"] = e.unit;
    if (e.component == "switch" || e.component == "light") {
      doc["command_topic"] = _tCommand;
      doc["payload_on"]  = String("{\"") + e.key + "\":true}";
      doc["payload_off"] = String("{\"") + e.key + "\":false}";
      doc["state_on"] = "True";
      doc["state_off"] = "False";
    }
    if (e.component == "binary_sensor") {
      doc["payload_on"] = "True";
      doc["payload_off"] = "False";
    }
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = _id;
    dev["name"] = String(_id);
    dev["suggested_area"] = _room;
    dev["manufacturer"] = "DIY ESP32";

    String topic = String("homeassistant/") + e.component + "/" + _id + "/" + e.key + "/config";
    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(topic.c_str(), payload.c_str(), true);
  }
}
