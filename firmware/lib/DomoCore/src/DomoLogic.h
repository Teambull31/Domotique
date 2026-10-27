#pragma once
// Logique pure des nœuds (sans Arduino) : testée sur PC par `pio test -e native` (test/).
#include <cmath>

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

}  // namespace domo
