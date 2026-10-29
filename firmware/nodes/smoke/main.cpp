// Nœud "smoke" : écoute les détecteurs de fumée / de monoxyde de carbone (CO) et prévient le proche quand
// l'un d'eux sonne. Aucun fil vers les détecteurs, rien de modifié : leur sécurité reste entière.
// Reconnaît la tonalité aiguë (~3 kHz) et les bips répétés des alarmes (motifs normalisés T3 fumée,
// T4 CO) ; un micro-ondes (quelques bips) ou une tonalité continue ne déclenchent pas.
// Complément de confort : PAS un système d'alarme certifié ; garder des détecteurs conformes (NF EN 14604).
// Micro I2S INMP441 (3,3 V) : SCK -> PIN_I2S_BCK, WS -> PIN_I2S_WS, SD -> PIN_I2S_DIN, L/R -> GND.
// Alimenté sur secteur, posé à quelques mètres des détecteurs (couloir). Broches : lib/DomoCore/src/DomoPins.h
#include <DomoCore.h>
#include <DomoLogic.h>
#include <driver/i2s.h>

#define RATE 16000
#define FRAME 256          // trame de 16 ms
// Niveau minimal ~ -60 dBFS, soit ~60 dB SPL pour l'INMP441 (-26 dBFS à 94 dB SPL) : un détecteur de
// 85 dB à 3 m s'entend encore porte fermée. La fréquence et le motif écartent les autres sons.
#define MIN_RMS 0.001f
#define SEND_INTERVAL_MS 60000

DomoCore core("smoke");
domo::AlarmPattern pattern;
int32_t raw[FRAME];
float frame[FRAME];
bool sounding = false, fault = false, dirty = true, micOk = false;
float peakDb = -120;  // niveau maximal depuis le dernier envoi (aide à placer le micro)
unsigned long lastSend = 0, lastSound = 0;

bool micBegin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;  // INMP441 avec L/R à la masse
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = FRAME;
  if (i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) != ESP_OK) return false;
  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;
  pins.bck_io_num = PIN_I2S_BCK;
  pins.ws_io_num = PIN_I2S_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num = PIN_I2S_DIN;
  return i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
}

// Une trame du micro, normalisée (pleine échelle = 1.0) et centrée ; false si rien n'est reçu.
bool readFrame() {
  size_t got = 0;
  if (i2s_read(I2S_NUM_0, raw, sizeof(raw), &got, pdMS_TO_TICKS(100)) != ESP_OK || got < sizeof(raw))
    return false;
  float mean = 0;
  bool silent = true;
  for (int i = 0; i < FRAME; i++) {
    frame[i] = (float)(raw[i] >> 8) / 8388608.0f;  // échantillon 24 bits
    mean += frame[i];
    if (raw[i] != 0) silent = false;
  }
  if (silent) return false;  // micro débranché : uniquement des zéros
  mean /= FRAME;
  float energy = 0;
  for (float& v : frame) {
    v -= mean;
    energy += v * v;
  }
  float db = 10.0f * log10f(energy / FRAME + 1e-12f);
  if (db > peakDb) peakDb = db;
  return true;
}

void setup() {
  Serial.begin(115200);
  micOk = micBegin();
  core.addEntity("binary_sensor", "alarm", "Détecteur qui sonne", "smoke");
  core.addEntity("sensor", "level", "Niveau sonore max", nullptr, "dBFS",
                 "{\"entity_category\":\"diagnostic\",\"icon\":\"mdi:waveform\"}");
  core.addEntity("binary_sensor", "sensor_fault", "Défaut micro", "problem",
                 nullptr, "{\"entity_category\":\"diagnostic\"}");
  core.begin();
}

void loop() {
  core.loop();
  unsigned long now = millis();
  if (micOk && readFrame()) {
    lastSound = now;
    bool a = pattern.update(domo::alarmTone(frame, FRAME, RATE, MIN_RMS), now);
    if (a != sounding) {
      sounding = a;
      dirty = true;
    }
  } else {
    delay(16);  // pas de trame : ne pas tourner à vide
  }
  // Micro muet 10 s (débranché, mal câblé) : défaut signalé, l'écoute ne protège plus
  bool f = !micOk || now - lastSound > 10000;
  if (f != fault && now > 15000) {
    fault = f;
    dirty = true;
  }

  if (dirty || now - lastSend >= SEND_INTERVAL_MS) {
    JsonDocument doc;
    doc["alarm"] = sounding;
    doc["sensor_fault"] = fault;
    doc["level"] = nullptr;
    if (!fault) doc["level"] = roundf(peakDb);
    if (core.publishTelemetry(doc)) {
      lastSend = now;
      dirty = false;
      peakDb = -120;
    }
  }
}
