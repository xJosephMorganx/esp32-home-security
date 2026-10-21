#pragma once
#include <DomoticaProtocol.h>

// Hub authority, independent of rendering, transport and internet.
// Phase 1: manual arming only; intrusion alarms/exit delay come later.
class SystemController {
public:
  enum class Result { Unchanged, Changed, SensorsOffline };
  constexpr Domotica::SystemState state() const { return state_; }
  constexpr uint32_t revision() const { return revision_; }
  constexpr bool armed() const { return state_==Domotica::SystemState::Armed; }
  // Only sensor connectivity gates arming. RFID never participates.
  constexpr Result request(bool arm,bool doorOnline,bool pirOnline) {
    if(arm==armed())return Result::Unchanged;
    if(arm && !doorOnline && !pirOnline)return Result::SensorsOffline;
    state_=arm?Domotica::SystemState::Armed:Domotica::SystemState::Disarmed;
    revision_=Domotica::nextCounter(revision_);
    return Result::Changed;
  }
private:
  // Every hub boot starts disarmed in its new persisted radio session.
  Domotica::SystemState state_=Domotica::SystemState::Disarmed;
  uint32_t revision_=1;
};
