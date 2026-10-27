#pragma once
#include <Arduino.h>
#include "DomoLogic.h"
#include "DomoPins.h"
#if !defined(CONFIG_IDF_TARGET_ESP32C3)
#include <driver/rtc_io.h>
#endif

// Nœuds sur pile : sommeil profond, réveil quand la broche passe au niveau opposé à l'état
// actuel, ou par minuterie (battement de coeur).
inline void domoDeepSleep(gpio_num_t pin, int currentLevel, uint64_t heartbeatSec) {
#if defined(CONFIG_IDF_TARGET_ESP32C3)
  // Pas d'ext0 sur C3 : réveil GPIO (GPIO0..5 uniquement). Le pull-up interne n'est pas garanti
  // en sommeil : prévoir une résistance de 100 kΩ vers 3V3.
  gpio_pullup_en(pin);
  gpio_pulldown_dis(pin);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pin,
      currentLevel ? ESP_GPIO_WAKEUP_GPIO_LOW : ESP_GPIO_WAKEUP_GPIO_HIGH);
#else
  rtc_gpio_pullup_en(pin);
  rtc_gpio_pulldown_dis(pin);
  esp_sleep_enable_ext0_wakeup(pin, currentLevel ? 0 : 1);
#endif
  esp_sleep_enable_timer_wakeup(heartbeatSec * 1000000ULL);
  esp_deep_sleep_start();
}

// Niveau de pile (%) via diviseur 1/2 sur une entrée ADC (Li-ion 3,3 V..4,2 V).
// À lire avant d'allumer le WiFi : les pics d'émission font chuter la tension. Moyenne de 8 mesures.
inline float domoBatteryPercent(uint8_t adcPin) {
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(adcPin);
  return domo::batteryPercent(mv / 8 * 2 / 1000.0f);
}
