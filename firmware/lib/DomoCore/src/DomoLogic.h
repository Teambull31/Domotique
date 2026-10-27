#pragma once
// Logique pure des nœuds (sans Arduino) : testée sur PC par `pio test -e native` (test/).
#include <cctype>
#include <cmath>
#include <string>

namespace domo {

// Thermostat à hystérésis. Sonde muette (NaN) -> arrêt : ne jamais chauffer à l'aveugle.
inline bool thermostatWant(float temp, float target, float hysteresis, bool heating) {
  if (std::isnan(temp)) return false;
  if (temp < target - hysteresis) return true;
  if (temp > target + hysteresis) return false;
  return heating;  // dans la bande : on garde l'état (pas d'oscillation)
}

// Fenêtre ouverte : chute de `drop` °C entre la plus ancienne et la plus récente de `n`
// mesures (une par minute). Après détection, l'historique repart de zéro.
class WindowDetector {
public:
  static constexpr int kMax = 16;
  explicit WindowDetector(int n = 6, float drop = 1.0f) : n_(n < kMax ? n : kMax), drop_(drop) {}
  // Ajoute une mesure ; renvoie true si une ouverture est détectée sur cette mesure.
  bool add(float t) {
    for (int i = n_ - 1; i > 0; i--) h_[i] = h_[i - 1];
    h_[0] = t;
    if (count_ < n_) count_++;
    if (count_ == n_ && h_[n_ - 1] - t >= drop_) {
      reset();
      return true;
    }
    return false;
  }
  void reset() { count_ = 0; }

private:
  float h_[kMax] = {};
  int n_, count_ = 0;
  float drop_;
};

// Volet : position estimée (0 = fermé, 100 = ouvert) après `elapsedMs` de course.
inline float shutterPosition(float startPos, unsigned long elapsedMs, unsigned long travelMs, bool opening) {
  float d = travelMs ? elapsedMs * 100.0f / travelMs : 100.0f;
  float p = opening ? startPos + d : startPos - d;
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// Volet : durée de relais pour aller de `from` à `to` ; en butée, dépassement pour se recaler.
inline unsigned long shutterMoveMs(float from, float to, unsigned long travelMs, unsigned long overrunMs) {
  bool end = (to <= 0 || to >= 100);
  return (unsigned long)(std::fabs(to - from) * travelMs / 100) + (end ? overrunMs : 0);
}

// Pile Li-ion : % entre 3,3 V (vide) et 4,2 V (pleine), borné.
inline float batteryPercent(float volts) {
  float p = (volts - 3.3f) / (4.2f - 3.3f) * 100.0f;
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// Identifiant MQTT/HA : "Entrée Sud" -> "entree-sud" (accents français translittérés, tirets uniques).
inline std::string slug(const char* in) {
  std::string o;
  for (size_t i = 0; in[i]; i++) {
    unsigned char c = in[i];
    if (c == 0xC3 && in[i + 1]) {  // lettres accentuées UTF-8 (U+00C0..U+00FF)
      unsigned char n = (unsigned char)in[++i] | 0x20;  // majuscule -> minuscule
      c = (n >= 0xA0 && n <= 0xA5) ? 'a' : n == 0xA7 ? 'c' : (n >= 0xA8 && n <= 0xAB) ? 'e'
        : (n >= 0xAC && n <= 0xAF) ? 'i' : (n >= 0xB2 && n <= 0xB6) ? 'o'
        : (n >= 0xB9 && n <= 0xBC) ? 'u' : '-';
    } else if (c < 128 && std::isalnum(c)) {
      c = (unsigned char)std::tolower(c);
    } else {
      c = '-';
    }
    if (c == '-' && (o.empty() || o.back() == '-')) continue;
    o += (char)c;
  }
  if (!o.empty() && o.back() == '-') o.pop_back();
  return o.empty() ? "maison" : o;
}

}  // namespace domo
