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

// Relais de chauffage avec anti-cycles courts : pas de bascule à moins de `minCycleMs` de la
// précédente (usure chaudière/relais), SAUF la coupure de sécurité quand la sonde est muette.
class CycleGuard {
public:
  explicit CycleGuard(unsigned long minCycleMs) : min_(minCycleMs) {}
  // Renvoie l'état du relais à appliquer maintenant.
  bool apply(bool current, bool want, bool sensorDead, unsigned long now) {
    if (want == current) return current;
    bool safetyOff = sensorDead && !want;
    if (switched_ && now - last_ < min_ && !safetyOff) return current;
    last_ = now;
    switched_ = true;
    return want;
  }

private:
  unsigned long min_, last_ = 0;
  bool switched_ = false;
};

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

// Présence confirmée et lumière automatique d'une pièce (nœud "presence").
// - absence confirmée après `holdMs` sans détection (le radar brut clignote) ;
// - lumière allumée sur présence, éteinte `offDelayMs` après la dernière détection ;
// - un forçage manuel (ex. lumière éteinte pour dormir) est respecté tant que la pièce est occupée,
//   l'automatique reprend quand elle est vide depuis `offDelayMs`.
class PresenceLight {
public:
  bool autoMode = true;

  void update(bool detected, unsigned long now, unsigned long holdMs, unsigned long offDelayMs) {
    if (detected) {
      last_ = now;
      seen_ = true;
    }
    present_ = detected || (seen_ && now - last_ < holdMs);  // jamais "présent" au démarrage
    if (manual_ && !present_ && now - last_ > offDelayMs) manual_ = false;
    if (autoMode && !manual_) {
      if (present_) light_ = true;
      else if (now - last_ > offDelayMs) light_ = false;
    }
  }
  void force(bool on) {
    manual_ = true;
    light_ = on;
  }
  bool present() const { return present_; }
  bool light() const { return light_; }
  bool manual() const { return manual_; }

private:
  unsigned long last_ = 0;
  bool seen_ = false, present_ = false, light_ = false, manual_ = false;
};

// Portail motorisé commandé par impulsion (entrée « pas à pas » de la motorisation, comme la
// télécommande). Les sécurités restent celles du moteur (cellules, fins de course).
// État : fin de course « fermé » (obligatoire), « ouvert » (facultatif : openSw = -1 si absent),
// mouvement estimé pendant `travelMs` après une impulsion.
// Une impulsion n'est envoyée que si l'état la justifie : jamais « ouvrir » un portail en mouvement
// (une 2e impulsion l'arrêterait ou l'inverserait selon la motorisation), anti double appui.
class GateLogic {
public:
  enum State { CLOSED, OPEN, OPENING, CLOSING, STOPPED };
  enum Cmd { CMD_OPEN, CMD_CLOSE, CMD_STOP };

  explicit GateLogic(unsigned long travelMs, unsigned long minGapMs = 2000)
      : travel_(travelMs), gap_(minGapMs) {}
  void setTravel(unsigned long travelMs) { travel_ = travelMs; }

  State state(bool closedSw, int openSw, unsigned long now) const {
    if (closedSw) return CLOSED;
    if (openSw == 1) return OPEN;
    if (moving_ && now - pulseAt_ < travel_) return dir_;
    return openSw < 0 ? OPEN : STOPPED;  // sans capteur d'ouverture : pas fermé = ouvert
  }

  // true -> envoyer une impulsion maintenant.
  bool command(Cmd c, State s, unsigned long now) {
    if (pulsed_ && now - pulseAt_ < gap_) return false;  // double appui
    bool go = (c == CMD_OPEN && (s == CLOSED || s == STOPPED)) ||
              (c == CMD_CLOSE && (s == OPEN || s == STOPPED)) ||
              (c == CMD_STOP && (s == OPENING || s == CLOSING));
    if (!go) return false;
    pulsed_ = true;
    pulseAt_ = now;
    moving_ = c != CMD_STOP;
    dir_ = c == CMD_OPEN ? OPENING : CLOSING;
    return true;
  }

  static const char* name(State s) {
    static const char* n[] = {"closed", "open", "opening", "closing", "stopped"};
    return n[s];
  }

private:
  unsigned long travel_, gap_, pulseAt_ = 0;
  bool pulsed_ = false, moving_ = false;
  State dir_ = OPENING;
};

// Réglage relu en mémoire flash ou reçu de HA : borné, et valeur par défaut si NaN
// (une consigne NaN rendrait toutes les comparaisons fausses : plus de chauffe, même hors-gel).
inline float sanitize(float v, float lo, float hi, float def) {
  if (std::isnan(v)) return def;
  return v < lo ? lo : (v > hi ? hi : v);
}

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

// Nœud sur pile : durée du prochain sommeil. Envoi réussi -> battement normal ; échecs d'affilée ->
// nouvel essai rapide (1, 2, 4, 8... min) pour ne pas retarder une alerte d'une heure, sans dépasser
// le battement normal (box en panne longtemps : la pile ne se vide pas en réveils inutiles).
inline unsigned long sleepSeconds(unsigned failures, unsigned long heartbeatSec) {
  if (failures == 0) return heartbeatSec;
  unsigned long s = 60UL << (failures > 7 ? 6 : failures - 1);
  return s < heartbeatSec ? s : heartbeatSec;
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
