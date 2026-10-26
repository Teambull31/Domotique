#pragma once
#include <Arduino.h>
#include <driver/rtc_io.h>

// Nœuds sur pile : sommeil profond, réveil sur changement d'état d'une broche RTC
// (ext0, niveau opposé à l'état actuel) ou par minuterie (battement de coeur).
inline void domoDeepSleep(gpio_num_t pin, int currentLevel, uint64_t heartbeatSec) {
  rtc_gpio_pullup_en(pin);
  rtc_gpio_pulldown_dis(pin);
  esp_sleep_enable_ext0_wakeup(pin, currentLevel ? 0 : 1);
  esp_sleep_enable_timer_wakeup(heartbeatSec * 1000000ULL);
  esp_deep_sleep_start();
}

// Niveau de pile (%) via diviseur 1/2 sur une entrée ADC (Li-ion 3,3 V..4,2 V).
inline float domoBatteryPercent(uint8_t adcPin) {
  float v = analogReadMilliVolts(adcPin) * 2 / 1000.0f;
  return constrain((v - 3.3f) / (4.2f - 3.3f) * 100.0f, 0.0f, 100.0f);
}
