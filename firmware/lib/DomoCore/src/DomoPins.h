#pragma once
// Brochages par carte, choisis à la compilation (env PlatformIO <nœud>, <nœud>-c3, <nœud>-s3).
// Toutes les broches retenues évitent les broches de démarrage (strapping), la flash/PSRAM et l'USB.
#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32C3)
// ESP32-C3-DevKitM/C : peu de broches ; réveil deep sleep possible seulement sur GPIO0..5.
#define DOMO_BOARD "ESP32-C3"
#define PIN_BOOT 9          // bouton BOOT
#define PIN_SDA 6
#define PIN_SCL 7
#define PIN_UART_RX 10      // vers TX du capteur (PZEM, LD2410, PMS5003)
#define PIN_UART_TX 5       // vers RX du capteur
#define PIN_RELAY1 0        // lumière (presence), montée (shutters)
#define PIN_RELAY2 4        // descente (shutters)
#define PIN_RELAY_HEAT 0    // chauffage
#define PIN_WAKE GPIO_NUM_3 // contact porte / sonde fuite (+ pull-up externe 100 kΩ conseillé)
#define PIN_BATTERY 1       // diviseur 2x100 kΩ de la pile (ADC1)
#define PIN_LED 4           // voyant de confirmation (bouton d'appel), avec résistance 330 Ω
#define PIN_INPUT1 3        // fin de course « fermé » (portail)
#define PIN_INPUT2 10       // fin de course « ouvert » (portail, facultatif)

#elif defined(CONFIG_IDF_TARGET_ESP32S3)
// ESP32-S3-DevKitC-1 : on évite 0/3/45/46 (strapping), 19/20 (USB), 26..37 (flash/PSRAM octal),
// 38/48 (LED RGB), 43/44 (UART0 du port série).
#define DOMO_BOARD "ESP32-S3"
#define PIN_BOOT 0
#define PIN_SDA 8
#define PIN_SCL 9
#define PIN_UART_RX 18
#define PIN_UART_TX 17
#define PIN_RELAY1 4
#define PIN_RELAY2 5
#define PIN_RELAY_HEAT 4
#define PIN_WAKE GPIO_NUM_6  // broche RTC (réveil ext0)
#define PIN_BATTERY 7        // ADC1
#define PIN_LED 4
#define PIN_INPUT1 6
#define PIN_INPUT2 15

#else
// ESP32-DevKitC (ESP32 classique, WROOM/WROVER)
#define DOMO_BOARD "ESP32"
#define PIN_BOOT 0
#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_UART_RX 16
#define PIN_UART_TX 17
#define PIN_RELAY1 26
#define PIN_RELAY2 27
#define PIN_RELAY_HEAT 25
#define PIN_WAKE GPIO_NUM_33
#define PIN_BATTERY 34
#define PIN_LED 26
#define PIN_INPUT1 33
#define PIN_INPUT2 32
#endif
