#pragma once
#include <DomoticaProtocol.h>

struct PirLink {
  Domotica::PeerTracker peer;
  Domotica::PirState lastState=Domotica::PirState::Unknown;
  constexpr Domotica::Order admit(const Domotica::Frame& frame,uint32_t at) {
    if(!Domotica::isPirReport(frame.type)||Domotica::validate(frame)!=Domotica::Error::Ok)
      return Domotica::Order::Invalid;
    auto order=peer.accept(frame.stamp,at);
    if(order==Domotica::Order::First||order==Domotica::Order::NewSession||order==Domotica::Order::Fresh)
      lastState=frame.pir;
    return order;
  }
  constexpr Domotica::PirState current(uint32_t now) const {
    return peer.online(now)?lastState:Domotica::PirState::Unknown;
  }
};
