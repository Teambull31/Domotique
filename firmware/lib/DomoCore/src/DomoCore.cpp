#include "DomoCore.h"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFiManager.h>

DomoCore::DomoCore(const char* nodeType) : _nodeType(nodeType), _mqtt(_wifi) {}

static String slug(String s) {
  s.toLowerCase();
  String o;
  for (char c : s) o += (isalnum((unsigned char)c) ? c : '-');
  return o;
}

void DomoCore::begin() {
  Preferences prefs;
  prefs.begin("domo", false);

  pinMode(0, INPUT_PULLUP);
  if (digitalRead(0) == LOW) {
    delay(3000);
    if (digitalRead(0) == LOW) {
      prefs.clear();
      WiFiManager().resetSettings();
      Serial.println("Configuration effacée");
    }
  }

  _room = prefs.getString("room", "maison");
  _host = prefs.getString("host", "");
  _port = prefs.getUShort("port", 1883);
  _user = prefs.getString("user", "");
  _pass = prefs.getString("pass", "");
  _ota  = prefs.getString("ota", "changeme");

  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  char portBuf[8];
  snprintf(portBuf, sizeof(portBuf), "%u", _port);
  WiFiManagerParameter pRoom("room", "Pièce (ex: salon)", _room.c_str(), 24);
  WiFiManagerParameter pHost("host", "Adresse MQTT (IP de Home Assistant)", _host.c_str(), 64);
  WiFiManagerParameter pPort("port", "Port MQTT", portBuf, 6);
  WiFiManagerParameter pUser("user", "Utilisateur MQTT", _user.c_str(), 32);
  WiFiManagerParameter pPass("pass", "Mot de passe MQTT", _pass.c_str(), 64);
  WiFiManagerParameter pOta("ota", "Mot de passe mise à jour (OTA)", _ota.c_str(), 32);
  for (auto* p : {&pRoom, &pHost, &pPort, &pUser, &pPass, &pOta}) wm.addParameter(p);
  bool save = false;
  wm.setSaveParamsCallback([&] { save = true; });
  wm.setConfigPortalTimeout(300);

  String ap = "Domo-" + _nodeType + "-setup";
  if (!wm.autoConnect(ap.c_str())) ESP.restart();

  if (save) {
    prefs.putString("room", pRoom.getValue());
    prefs.putString("host", pHost.getValue());
    prefs.putUShort("port", atoi(pPort.getValue()));
    prefs.putString("user", pUser.getValue());
    prefs.putString("pass", pPass.getValue());
    prefs.putString("ota", pOta.getValue());
    ESP.restart();  // recharge proprement avec la config enregistrée
  }

  _id = _nodeType + "-" + slug(_room);
  String base = "home/devices/" + _id;
  _tTelemetry = base + "/telemetry";
  _tStatus    = base + "/status";
  _tCommand   = base + "/command";

  WiFi.setHostname(_id.c_str());
  WiFi.setAutoReconnect(true);
  ArduinoOTA.setHostname(_id.c_str());
  ArduinoOTA.setPassword(_ota.c_str());
  ArduinoOTA.begin();

  _mqtt.setServer(_host.c_str(), _port);
  _mqtt.setBufferSize(1024);
  _mqtt.setKeepAlive(30);
  _mqtt.setCallback([this](char*, byte* p, unsigned int l) { handleMessage(p, l); });
}

bool DomoCore::connectMQTT() {
  if (_host.isEmpty()) return false;
  const char* user = _user.length() ? _user.c_str() : nullptr;
  const char* pass = _pass.length() ? _pass.c_str() : nullptr;
  if (!_mqtt.connect(_id.c_str(), user, pass, _tStatus.c_str(), 1, true, "offline")) {
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
  if (WiFi.status() != WL_CONNECTED) return;  // la reconnexion WiFi est automatique
  if (!_mqtt.connected()) {
    if (millis() - _lastTry < 5000) return;  // essai MQTT toutes les 5 s, sans bloquer
    _lastTry = millis();
    if (!connectMQTT()) return;
  }
  _mqtt.loop();
}

bool DomoCore::waitConnected(uint32_t timeoutMs) {
  unsigned long t0 = millis();
  while (!_mqtt.connected() && millis() - t0 < timeoutMs) {
    _lastTry = 0;
    loop();
    delay(50);
  }
  return _mqtt.connected();
}

void DomoCore::flush(uint32_t ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { _mqtt.loop(); delay(10); }
}

void DomoCore::handleMessage(byte* payload, unsigned int len) {
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
                         const char* deviceClass, const char* unit, const char* extraJson) {
  _entities.push_back({component, key, name, deviceClass ? deviceClass : "",
                       unit ? unit : "", extraJson ? extraJson : ""});
}

void DomoCore::publishDiscovery() {
  for (auto& e : _entities) {
    JsonDocument doc;
    doc["name"] = e.name;
    doc["unique_id"] = _id + "_" + e.key;
    doc["has_entity_name"] = true;
    doc["state_topic"] = _tTelemetry;
    doc["availability_topic"] = _tStatus;
    doc["value_template"] = "{{ value_json." + e.key + " }}";
    if (e.deviceClass.length()) doc["device_class"] = e.deviceClass;
    if (e.unit.length()) doc["unit_of_measurement"] = e.unit;

    if (e.component == "sensor" && e.unit.length())
      doc["state_class"] = (e.deviceClass == "energy") ? "total_increasing" : "measurement";
    if (e.component == "binary_sensor" || e.component == "switch") {
      doc["payload_on"] = "True";      // ArduinoJson émet true -> Jinja "True"
      doc["payload_off"] = "False";
      doc["state_on"] = "True";
      doc["state_off"] = "False";
    }
    if (e.component == "binary_sensor") { doc.remove("state_on"); doc.remove("state_off"); }
    if (e.component == "switch") {
      doc.remove("payload_on"); doc.remove("payload_off");
      doc["command_topic"] = _tCommand;
      doc["payload_on"]  = "{\"" + e.key + "\":true}";
      doc["payload_off"] = "{\"" + e.key + "\":false}";
    }
    if (e.component == "number") {
      doc["command_topic"] = _tCommand;
      doc["command_template"] = "{\"" + e.key + "\": {{ value }} }";
    }
    if (e.component == "cover") {
      doc["command_topic"] = _tCommand;
      doc["payload_open"]  = "{\"" + e.key + "\":\"open\"}";
      doc["payload_close"] = "{\"" + e.key + "\":\"close\"}";
      doc["payload_stop"]  = "{\"" + e.key + "\":\"stop\"}";
    }
    if (e.extra.length()) {
      JsonDocument extra;
      if (deserializeJson(extra, e.extra) == DeserializationError::Ok)
        for (JsonPair kv : extra.as<JsonObject>()) doc[kv.key()] = kv.value();
    }

    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"][0] = _id;
    dev["name"] = _id;
    dev["suggested_area"] = _room;
    dev["manufacturer"] = "DIY ESP32";
    dev["model"] = _nodeType;

    String topic = "homeassistant/" + e.component + "/" + _id + "/" + e.key + "/config";
    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(topic.c_str(), payload.c_str(), true);
  }
}
