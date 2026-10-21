#pragma once
#include <DomoticaProtocol.h>

namespace Pir {
// Local sensor model only. No numeric wire mapping is defined here.
enum class Observation { Unknown, NoMotion, Motion };
struct Sensor {
  bool started = false, warmed = false, candidate = false, stable = false, ready = false;
  uint32_t startedAt = 0, changedAt = 0;
  constexpr bool sample(bool high, uint32_t now, uint32_t warmup, uint32_t filter) {
    if (!started) { started = true; startedAt = now; }
    if (!warmed) {
      if (!warmup || !Domotica::elapsed(now, startedAt, warmup)) return false;
      // Discard every warmup reading and qualify a fresh level after warmup.
      warmed = true; candidate = high; changedAt = now;
    }
    if (candidate != high) { candidate = high; changedAt = now; }
    if (Domotica::elapsed(now, changedAt, filter) && (!ready || candidate != stable)) {
      stable = candidate; ready = true; return true;
    }
    return false;
  }
  constexpr Observation observation(bool configured, bool motionHigh) const {
    return !configured || !ready ? Observation::Unknown
        : stable == motionHigh ? Observation::Motion : Observation::NoMotion;
  }
  constexpr Domotica::PirState wireState(bool configured, bool motionHigh) const {
    if (!configured) return Domotica::PirState::Unknown;
    if (!ready) return Domotica::PirState::NotReady;
    switch (observation(configured, motionHigh)) {
      case Observation::NoMotion: return Domotica::PirState::NoMotion;
      case Observation::Motion: return Domotica::PirState::Motion;
      default: return Domotica::PirState::Unknown;
    }
  }
};
// Only admitted, correlated STATE_SYNC may establish confirmed/online.
constexpr bool statusLed(Domotica::SystemState state, bool confirmed, bool online) {
  return confirmed && Domotica::ledOn(state, online);
}
}
