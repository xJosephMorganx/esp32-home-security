#pragma once
#include <DomoticaProtocol.h>

// Transport-independent admission logic. Never connect demo arming to this state.
struct DoorLink {
  Domotica::PeerTracker peer;
  Domotica::DoorState lastState=Domotica::DoorState::Unknown;
  constexpr Domotica::Order admit(const Domotica::Frame& frame,uint32_t receivedAt) {
    if(!Domotica::isReport(frame.type) || Domotica::validate(frame)!=Domotica::Error::Ok)
      return Domotica::Order::Invalid;
    auto order=peer.accept(frame.stamp,receivedAt);
    if(order==Domotica::Order::First || order==Domotica::Order::NewSession || order==Domotica::Order::Fresh)
      lastState=frame.door;
    return order;
  }
  constexpr Domotica::DoorState current(uint32_t now) const {
    return peer.online(now)?lastState:Domotica::DoorState::Unknown;
  }
};
