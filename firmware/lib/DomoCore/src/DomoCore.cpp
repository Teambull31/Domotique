#include "DomoCore.h"
#include "DomoLogic.h"
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#define WDT_TIMEOUT_S 60  // boucle bloquée plus de 60 s (bus I2C figé...) -> redémarrage

// Nœuds sur pile : canal et point d'accès du dernier réveil, conservés pendant le deep sleep.
// Connexion directe au réveil suivant, sans recherche sur tous les canaux (étape la plus longue).
RTC_DATA_ATTR static int32_t rtcChannel = 0;
RTC_DATA_ATTR static uint8_t rtcBssid[6];
// Réveils depuis la dernière publication de la discovery (retenue sur le broker : inutile de la
// renvoyer à chaque réveil, ~1 ko d'émission radio en moins)
RTC_DATA_ATTR static uint32_t rtcWakesSinceDiscovery = 0;

// Reconnexion rapide d'un nœud sur pile avec le canal/BSSID mémorisés ; false -> chemin normal.
static bool fastReconnect() {
  if (rtcChannel <= 0 || esp_reset_reason() != ESP_RST_DEEPSLEEP) return false;
  WiFi.mode(WIFI_STA);
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK || !conf.sta.ssid[0]) return false;
  char ssid[33] = {0}, pass[65] = {0};
  memcpy(ssid, conf.sta.ssid, sizeof(conf.sta.ssid));
  memcpy(pass, conf.sta.password, sizeof(conf.sta.password));
  WiFi.begin(ssid, pass, rtcChannel, rtcBssid);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 4000) delay(10);
  if (WiFi.status() == WL_CONNECTED) return true;
  rtcChannel = 0;  // box changée de canal ou remplacée : recherche complète
  WiFi.disconnect();
  return false;
}

DomoCore::DomoCore(const char* nodeType) : _nodeType(nodeType), _mqtt(_wifi) {}

static String slug(const String& s) { return String(domo::slug(s.c_str()).c_str()); }

void DomoCore::begin() {
  Preferences prefs;
  prefs.begin("domo", false);

  // Bouton BOOT, à presser dans les 2 s qui suivent la mise sous tension (pressé pendant le reset,
  // l'ESP32 passe en mode téléchargement) : 3 s = portail de configuration (valeurs conservées),
  // 10 s = effacement complet. Pas de fenêtre d'attente au réveil d'un nœud sur pile, ni après un
  // redémarrage logiciel (perte de MQTT, chien de garde) : les commandes locales reprennent aussitôt.
  bool forcePortal = false;
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_POWERON || reason == ESP_RST_EXT || reason == ESP_RST_UNKNOWN) {
    pinMode(PIN_BOOT, INPUT_PULLUP);
    unsigned long t0 = millis();
    while (millis() - t0 < 2000 && digitalRead(PIN_BOOT) == HIGH) delay(10);
    unsigned long p0 = millis();
    while (digitalRead(PIN_BOOT) == LOW && millis() - p0 < 10000) delay(10);
    unsigned long held = millis() - p0;
    if (held >= 10000) {
      prefs.clear();
      WiFiManager().resetSettings();
      Serial.println("Configuration effacée");
    } else if (held >= 3000) {
      forcePortal = true;
    }
  }

  _room = prefs.getString("room", "maison");
  _label = prefs.getString("label", "");
  _host = prefs.getString("host", "");
  _port = prefs.getUShort("port", 1883);
  _user = prefs.getString("user", "");
  _pass = prefs.getString("pass", "");
  _ota  = prefs.getString("ota", "");

  // Identifiant unique par nœud : 2 capteurs du même type dans une pièce partageraient sinon
  // leurs topics MQTT et se déconnecteraient l'un l'autre en boucle.
  _id = _nodeType + "-" + slug(_room);
  String label = _label;
  label.trim();
  if (label.length()) _id += "-" + slug(label);
  // Nom visible dans la liste des appareils de la box : à définir AVANT le démarrage du WiFi
  WiFi.setHostname(_id.c_str());
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  char portBuf[8];
  snprintf(portBuf, sizeof(portBuf), "%u", _port);
  WiFiManagerParameter pRoom("room", "Pièce (ex: salon)", _room.c_str(), 24);
  WiFiManagerParameter pLabel("label", "Emplacement (facultatif, ex: fenetre) : obligatoire si 2 capteurs "
                              "du même type dans la pièce", _label.c_str(), 24);
  WiFiManagerParameter pHost("host", "Adresse MQTT (IP de Home Assistant)", _host.c_str(), 64);
  WiFiManagerParameter pPort("port", "Port MQTT", portBuf, 6);
  WiFiManagerParameter pUser("user", "Utilisateur MQTT", _user.c_str(), 32);
  // Mots de passe jamais pré-remplis (le portail est un point d'accès ouvert) : vide = inchangé.
  WiFiManagerParameter pPass("pass", "Mot de passe MQTT (vide = inchangé)", "", 64);
  WiFiManagerParameter pOta("ota", "Mot de passe OTA, 8 caractères min. (vide = inchangé ; "
                            "jamais défini = mise à jour WiFi désactivée)", "", 32);
  for (auto* p : {&pRoom, &pLabel, &pHost, &pPort, &pUser, &pPass, &pOta}) wm.addParameter(p);
  bool save = false;
  wm.setSaveParamsCallback([&] { save = true; });
  wm.setConfigPortalTimeout(300);
  String ap = "Domo-" + _nodeType + "-setup";
  if (forcePortal) {
    wm.startConfigPortal(ap.c_str());
    if (!save) ESP.restart();  // portail fermé sans enregistrer : démarrage normal
  } else if (!_sleepySec && !_host.isEmpty()) {
    // Nœud secteur déjà configuré : connexion en arrière-plan, sans attendre (jusqu'à 20 s sinon).
    // Box éteinte, en redémarrage ou WiFi coupé la nuit : automatismes et boutons locaux (volets,
    // lumière, hors-gel) actifs dès le démarrage ; MQTT se connecte dès que le WiFi revient (loop).
    WiFi.begin();
  } else if (_host.isEmpty() || !fastReconnect()) {
    // Ici : nœud sur pile, ou jamais configuré. Réveil sur pile : connexion directe au point d'accès
    // mémorisé (fastReconnect) ; sinon, jamais configuré : portail de configuration ; nœud sur pile
    // configuré : connexion attendue (il envoie puis se rendort), jamais de portail automatique ;
    // WiFi absent : essai au réveil suivant.
    if (!_host.isEmpty()) {
      wm.setEnableConfigPortal(false);
      wm.setConnectTimeout(15);
    }
    if (!wm.autoConnect(ap.c_str())) {
      if (_host.isEmpty() && !_sleepySec) ESP.restart();  // jamais configuré : portail au redémarrage
      if (!_host.isEmpty()) WiFi.begin();  // identifiants enregistrés : reconnexion en arrière-plan
    }
  }

  if (save) {
    prefs.putString("room", pRoom.getValue());
    prefs.putString("label", pLabel.getValue());
    prefs.putString("host", pHost.getValue());
    int port = atoi(pPort.getValue());
    prefs.putUShort("port", (port > 0 && port < 65536) ? port : 1883);
    prefs.putString("user", pUser.getValue());
    if (strlen(pPass.getValue())) prefs.putString("pass", pPass.getValue());
    if (strlen(pOta.getValue())) prefs.putString("ota", pOta.getValue());
    ESP.restart();  // recharge proprement avec la config enregistrée
  }

  String base = "home/devices/" + _id;
  _tTelemetry = base + "/telemetry";
  _tStatus    = base + "/status";
  _tCommand   = base + "/command";
  _tDiag      = base + "/diag";

  if (_sleepySec && WiFi.status() == WL_CONNECTED) {  // mémorisé pour le prochain réveil
    rtcChannel = WiFi.channel();
    memcpy(rtcBssid, WiFi.BSSID(), sizeof(rtcBssid));
  }
  WiFi.setAutoReconnect(true);
  // OTA uniquement si un mot de passe est défini : jamais de mise à jour WiFi ouverte.
  if (_ota.length() >= 8) {
    ArduinoOTA.setHostname(_id.c_str());
    ArduinoOTA.setPassword(_ota.c_str());
    ArduinoOTA.onStart([this] {
      if (_safe) _safe();
    });
    ArduinoOTA.onProgress([](unsigned, unsigned) { esp_task_wdt_reset(); });
    ArduinoOTA.begin();
    _otaOn = true;
  }

  // Chien de garde matériel pour les nœuds secteur : un relais (chauffage, volet) ne doit
  // jamais rester figé sur un programme planté. Armé après le portail (qui bloque 5 min).
  if (!_sleepySec) {
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);
  }

  _mqtt.setServer(_host.c_str(), _port);
  _mqtt.setBufferSize(2048);  // discovery du thermostat ~1,3 ko
  _mqtt.setKeepAlive(30);
  // Broker local : il répond en millisecondes. Délais courts pour qu'une tentative de connexion
  // ne bloque pas les automatismes (fin de course d'un volet) : 1 s TCP, 2 s pour la réponse MQTT
  // (au lieu de 3 s et 15 s par défaut).
  _wifi.setTimeout(1);
  _mqtt.setSocketTimeout(2);
  _mqtt.setCallback([this](char* topic, byte* p, unsigned int l) {
    // Home Assistant redémarré : republier la discovery (après l'appel, le tampon MQTT est partagé)
    if (!strcmp(topic, "homeassistant/status")) _rediscover = (l == 6 && !memcmp(p, "online", 6));
    else handleMessage(p, l);
  });
}

bool DomoCore::connectMQTT() {
  if (_host.isEmpty()) return false;
  const char* user = _user.length() ? _user.c_str() : nullptr;
  const char* pass = _pass.length() ? _pass.c_str() : nullptr;
  // ID client MQTT suffixé par l'adresse de la puce : un doublon de configuration ne provoque pas
  // de déconnexions en cascade côté broker.
  char client[64];
  snprintf(client, sizeof(client), "%s-%06llx", _id.c_str(), ESP.getEfuseMac() & 0xFFFFFF);
  bool ok = _sleepySec ? _mqtt.connect(client, user, pass)
                       : _mqtt.connect(client, user, pass, _tStatus.c_str(), 1, true, "offline");
  if (!ok) {
    Serial.printf("MQTT rc=%d\n", _mqtt.state());
    return false;
  }
  _mqtt.publish(_tStatus.c_str(), "online", true);
  _mqtt.subscribe(_tCommand.c_str());
  if (!_sleepySec) _mqtt.subscribe("homeassistant/status");
  // Nœud sur pile : discovery au démarrage (mise sous tension, nouvelle configuration) puis environ
  // une fois par jour (24 réveils), au cas où le broker aurait perdu ses messages retenus.
  bool wake = esp_reset_reason() == ESP_RST_DEEPSLEEP;
  if (!_sleepySec || !wake || rtcWakesSinceDiscovery >= 24) {
    publishDiscovery();
    rtcWakesSinceDiscovery = 0;
  } else {
    rtcWakesSinceDiscovery++;
  }
  return true;
}

void DomoCore::loop() {
  if (!_sleepySec) esp_task_wdt_reset();
  if (_restartAt && millis() - _restartAt > 500) restart();
  if (_otaOn) ArduinoOTA.handle();
  // La reconnexion WiFi est automatique, mais la pile WiFi de l'ESP32 reste parfois coincée
  // (box redémarrée, changement de canal) : au-delà de 15 min sans MQTT, on redémarre.
  // Les automatismes locaux (hors-gel, lumière auto) reprennent en quelques secondes.
  if (_mqtt.connected()) _lastOnline = millis();
  else if (millis() - _lastOnline > 15UL * 60UL * 1000UL) restart();
  if (WiFi.status() != WL_CONNECTED) return;
  if (!_mqtt.connected()) {
    // Essais espacés progressivement (5 s -> 60 s) quand HA/le broker est arrêté longtemps
    if (_lastTry && millis() - _lastTry < _retryMs) return;  // 1er essai immédiat
    _lastTry = millis() | 1;
    if (!connectMQTT()) {
      _retryMs = min(_retryMs * 2, 60000UL);
      return;
    }
    _retryMs = 5000;
  }
  _mqtt.loop();
  if (!_sleepySec && (_lastDiag == 0 || millis() - _lastDiag > 300000UL)) {
    _lastDiag = millis() | 1;
    publishDiag();
  }
  if (_rediscover) {
    _rediscover = false;
    publishDiscovery();
  }
}

bool DomoCore::waitConnected(uint32_t timeoutMs) {
  unsigned long t0 = millis();
  // Nœud sur pile : essais rapprochés sans l'espacement progressif de loop() (chaque seconde
  // éveillé coûte de la pile)
  while (!_mqtt.connected() && millis() - t0 < timeoutMs) {
    if (WiFi.status() != WL_CONNECTED || !connectMQTT()) delay(100);
  }
  return _mqtt.connected();
}

void DomoCore::flush(uint32_t ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) { _mqtt.loop(); delay(10); }  // NOLINT(bugprone-infinite-loop) : millis() avance
}

void DomoCore::handleMessage(byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len) != DeserializationError::Ok) return;
  if (doc["restart"] | false) {
    _restartAt = millis() | 1;  // laisser partir l'accusé MQTT avant de redémarrer
    return;
  }
  if (_handler) _handler(doc);
}

bool DomoCore::publishTelemetry(JsonDocument& doc, bool retain) {
  if (!_mqtt.connected()) return false;
  doc["room"] = _room;
  String out;
  serializeJson(doc, out);
  // Retenue : après un redémarrage de HA ou du broker, l'état s'affiche tout de suite au lieu de
  // "inconnu" jusqu'au prochain envoi (la disponibilité reste gérée par le LWT / expire_after).
  return _mqtt.publish(_tTelemetry.c_str(), out.c_str(), retain);
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
    if (_sleepySec) doc["expire_after"] = _sleepySec * 2 + 300;  // 2 battements manqués
    else doc["availability_topic"] = _tStatus;
    doc["value_template"] = "{{ value_json." + e.key + " }}";
    if (e.deviceClass.length()) doc["device_class"] = e.deviceClass;
    if (e.unit.length()) doc["unit_of_measurement"] = e.unit;

    // Compteurs (énergie, eau, gaz) : total croissant, pour les consommations par jour / mois de HA
    if (e.component == "sensor" && e.unit.length())
      doc["state_class"] = (e.deviceClass == "energy" || e.deviceClass == "water" || e.deviceClass == "gas")
                               ? "total_increasing" : "measurement";
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
    if (e.component == "select") {  // liste de choix : options fournies par le nœud (extraJson)
      doc["command_topic"] = _tCommand;
      doc["command_template"] = "{\"" + e.key + "\": \"{{ value }}\"}";
    }
    if (e.component == "climate") {  // thermostat : topics ici, modèles (templates) fournis par le nœud
      doc.remove("state_topic");
      doc.remove("value_template");
      for (const char* t : {"current_temperature_topic", "temperature_state_topic",
                            "mode_state_topic", "action_topic"})
        doc[t] = _tTelemetry;
      doc["temperature_command_topic"] = _tCommand;
      doc["mode_command_topic"] = _tCommand;
    }
    if (e.component == "cover") {
      doc["command_topic"] = _tCommand;
      doc["payload_open"]  = "{\"" + e.key + "\":\"open\"}";
      doc["payload_close"] = "{\"" + e.key + "\":\"close\"}";
      doc["payload_stop"]  = "{\"" + e.key + "\":\"stop\"}";
    }
    // Position (curseur 0-100 %) seulement pour volets/stores ; un portail est ouvert ou fermé
    if (e.component == "cover" && (e.deviceClass == "shutter" || e.deviceClass == "blind" ||
                                   e.deviceClass == "curtain" || e.deviceClass == "awning")) {
      doc["position_topic"] = _tTelemetry;
      doc["position_template"] = "{{ value_json." + e.key + "_pos }}";
      doc["set_position_topic"] = _tCommand;
      doc["set_position_template"] = "{\"" + e.key + "_pos\": {{ position }} }";
    }
    if (e.extra.length()) {
      JsonDocument extra;
      if (deserializeJson(extra, e.extra) == DeserializationError::Ok)
        for (JsonPair kv : extra.as<JsonObject>()) doc[kv.key()] = kv.value();
    }

    addDevice(doc);
    String topic = "homeassistant/" + e.component + "/" + _id + "/" + e.key + "/config";
    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(topic.c_str(), payload.c_str(), true);
  }

  // Diagnostic (nœuds secteur) : qualité WiFi et durée de fonctionnement, pour le dépannage à distance.
  if (_sleepySec) return;
  const char* diag[][4] = {{"rssi", "Signal WiFi", "signal_strength", "dBm"},
                           {"uptime", "Fonctionne depuis", "duration", "h"}};
  for (auto& d : diag) {
    JsonDocument doc;
    doc["name"] = d[1];
    doc["unique_id"] = _id + "_" + d[0];
    doc["has_entity_name"] = true;
    doc["entity_category"] = "diagnostic";
    doc["state_topic"] = _tDiag;
    doc["availability_topic"] = _tStatus;
    doc["value_template"] = String("{{ value_json.") + d[0] + " }}";
    doc["device_class"] = d[2];
    doc["unit_of_measurement"] = d[3];
    doc["state_class"] = "measurement";
    addDevice(doc);
    String topic = "homeassistant/sensor/" + _id + "/" + d[0] + "/config";
    String payload;
    serializeJson(doc, payload);
    _mqtt.publish(topic.c_str(), payload.c_str(), true);
  }
  // Bouton "Redémarrer" : dépannage à distance sans se déplacer
  JsonDocument btn;
  btn["name"] = "Redémarrer";
  btn["unique_id"] = _id + "_restart";
  btn["has_entity_name"] = true;
  btn["entity_category"] = "config";
  btn["device_class"] = "restart";
  btn["command_topic"] = _tCommand;
  btn["payload_press"] = "{\"restart\":true}";
  btn["availability_topic"] = _tStatus;
  addDevice(btn);
  String payload;
  serializeJson(btn, payload);
  _mqtt.publish(("homeassistant/button/" + _id + "/restart/config").c_str(), payload.c_str(), true);
  _lastDiag = 0;  // diagnostic envoyé dès la prochaine boucle
}

void DomoCore::publishDiag() {
  JsonDocument doc;
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = roundf(esp_timer_get_time() / 360000000.0) / 10.0;  // 64 bits : pas de retour à 0 à 49 j
  String out;
  serializeJson(doc, out);
  _mqtt.publish(_tDiag.c_str(), out.c_str(), true);
}

void DomoCore::addDevice(JsonDocument& doc) {
  JsonObject dev = doc["device"].to<JsonObject>();
  dev["identifiers"][0] = _id;
  dev["name"] = _id;
  dev["suggested_area"] = _room;
  dev["manufacturer"] = "DIY ESP32";
  dev["model"] = _nodeType;
  dev["sw_version"] = DOMO_VERSION;
}
