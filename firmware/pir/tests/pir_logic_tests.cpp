#include "../pir_logic.h"
using Pir::Observation;
constexpr bool startupAndEdges() {
  Pir::Sensor s;
  if (s.sample(false, 100, 1000, 20)) return false;
  if (s.sample(true, 1099, 1000, 20)) return false;
  if (s.observation(true, true) != Observation::Unknown) return false;
  if (s.sample(true, 1100, 1000, 20) || s.sample(true, 1119, 1000, 20)) return false;
  if (!s.sample(true, 1120, 1000, 20)) return false;
  if (s.observation(true, true) != Observation::Motion) return false;
  if (s.sample(true, 2000, 1000, 20)) return false; // Held motion is not another edge.
  if (s.sample(false, 2001, 1000, 20) || s.sample(true, 2010, 1000, 20)) return false;
  if (s.sample(false, 2020, 1000, 20) || !s.sample(false, 2040, 1000, 20)) return false;
  return s.observation(true, true) == Observation::NoMotion
      && s.observation(true, false) == Observation::Motion
      && s.observation(false, true) == Observation::Unknown;
}
constexpr bool rolloverAndUnset() {
  Pir::Sensor s;
  s.sample(false, UINT32_MAX - 49, 100, 10);
  if (s.sample(false, 49, 100, 10) || s.sample(false, 50, 100, 10)) return false;
  if (!s.sample(false, 60, 100, 10)) return false;
  Pir::Sensor unset;
  unset.sample(false, 0, 0, 0); unset.sample(false, 100000, 0, 0);
  return !unset.ready && unset.observation(true, true) == Observation::Unknown;
}
constexpr bool ledRules() {
  using Domotica::SystemState;
  for (unsigned i = 0; i <= 4; ++i) {
    const auto state = static_cast<SystemState>(i);
    if (Pir::statusLed(state, false, true) || Pir::statusLed(state, true, false)) return false;
    if (Pir::statusLed(state, true, true) != (state == SystemState::Armed || state == SystemState::Alarm)) return false;
  }
  return true;
}
static_assert(startupAndEdges(), "Warmup, initial motion, filtering and polarity");
static_assert(rolloverAndUnset(), "Timer rollover and unset warmup");
static_assert(ledRules(), "Only confirmed armed/alarm with live hub can light LED");

constexpr bool wireReadiness() {
  using Domotica::PirState;
  Pir::Sensor s;
  if (s.wireState(false, true) != PirState::Unknown
      || s.wireState(true, true) != PirState::NotReady) return false;
  s.sample(true, 0, 1000, 20);
  s.sample(true, 1000, 1000, 20);
  if (s.wireState(true, true) != PirState::NotReady) return false;
  s.sample(true, 1020, 1000, 20);
  if (s.wireState(true, true) != PirState::Motion
      || s.wireState(true, false) != PirState::NoMotion
      || s.wireState(false, true) != PirState::Unknown) return false;
  s.sample(false, 1030, 1000, 20);
  s.sample(false, 1050, 1000, 20);
  return s.wireState(true, true) == PirState::NoMotion
      && s.wireState(true, false) == PirState::Motion;
}
static_assert(wireReadiness(), "Explicit protocol mapping, not enum casts; no false NO_MOTION");
