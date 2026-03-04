/**
 * Domotique — Exemple ESP32
 * Envoie température + humidité via MQTT
 * Bibliothèques requises : PubSubClient, DHT sensor library, ArduinoJson
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ─── Configuration ────────────────────────────────────────────────────────────
const char* WIFI_SSID     = "VotreSSID";
const char* WIFI_PASS     = "VotreMotDePasse";
const char* MQTT_HOST     = "192.168.1.100";  // IP de votre serveur
const int   MQTT_PORT     = 1883;
const char* DEVICE_ID     = "esp32-salon";    // Identifiant unique
const char* ROOM          = "salon";          // Pièce

// DHT22 sur GPIO 4
#define DHT_PIN  4
#define DHT_TYPE DHT22

// Intervalle d'envoi (ms)
#define SEND_INTERVAL 30000

// ─── Topics MQTT ──────────────────────────────────────────────────────────────
String topic_telemetry = String("home/devices/") + DEVICE_ID + "/telemetry";
String topic_status    = String("home/devices/") + DEVICE_ID + "/status";
String topic_command   = String("home/devices/") + DEVICE_ID + "/command";

// ─── Objets ───────────────────────────────────────────────────────────────────
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
DHT          dht(DHT_PIN, DHT_TYPE);

unsigned long lastSend = 0;

// ─── Callback commandes ───────────────────────────────────────────────────────
void onCommand(char* topic, byte* payload, unsigned int length) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) return;

  // Contrôle relais
  if (doc.containsKey("relay")) {
    bool state = doc["relay"];
    digitalWrite(LED_BUILTIN, state ? HIGH : LOW);
    Serial.printf("Relais: %s\n", state ? "ON" : "OFF");
  }

  // Affichage écran (à adapter selon votre écran)
  if (doc.containsKey("display")) {
    const char* text = doc["display"];
    Serial.printf("Écran: %s\n", text);
    // u8g2.clearBuffer();
    // u8g2.drawStr(0, 20, text);
    // u8g2.sendBuffer();
  }
}

// ─── Connexion WiFi ───────────────────────────────────────────────────────────
void connectWiFi() {
  Serial.printf("Connexion WiFi à %s...", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nConnecté! IP: %s\n", WiFi.localIP().toString().c_str());
}

// ─── Connexion MQTT ───────────────────────────────────────────────────────────
void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.printf("Connexion MQTT à %s...", MQTT_HOST);
    // Last Will Testament: marque le device offline si la connexion est perdue
    if (mqtt.connect(DEVICE_ID, nullptr, nullptr, topic_status.c_str(), 1, true, "offline")) {
      Serial.println("OK");
      mqtt.publish(topic_status.c_str(), "online", true);
      mqtt.subscribe(topic_command.c_str());
    } else {
      Serial.printf("Échec (rc=%d), nouvelle tentative dans 5s\n", mqtt.state());
      delay(5000);
    }
  }
}

// ─── Envoi télémétrie ─────────────────────────────────────────────────────────
void sendTelemetry() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Erreur lecture DHT");
    return;
  }

  StaticJsonDocument<256> doc;
  doc["room"]        = ROOM;
  doc["temperature"] = round(temp * 10) / 10.0;
  doc["humidity"]    = round(hum * 10) / 10.0;
  // Ajoutez d'autres capteurs ici :
  // doc["light"]    = analogRead(34) * 100.0 / 4095.0;
  // doc["presence"] = digitalRead(PIR_PIN);
  // doc["battery"]  = analogRead(35) * 3.3 / 4095.0 * 2 / 3.3 * 100;

  char buffer[256];
  serializeJson(doc, buffer);
  mqtt.publish(topic_telemetry.c_str(), buffer);

  Serial.printf("Envoyé: Temp=%.1f°C, Hum=%.1f%%\n", temp, hum);
}

// ─── Setup / Loop ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  dht.begin();

  connectWiFi();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(onCommand);
  mqtt.setKeepAlive(60);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  if (millis() - lastSend >= SEND_INTERVAL) {
    sendTelemetry();
    lastSend = millis();
  }
}
