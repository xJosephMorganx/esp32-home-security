#include "../door_link.h"
using namespace Domotica;
constexpr bool lifecycle() {
  DoorLink link;
  if(link.current(0)!=DoorState::Unknown || link.peer.online(0))return false;
  Frame f;f.stamp={7,1};f.door=DoorState::Open;
  if(link.admit(f,100)!=Order::First || link.current(100)!=DoorState::Open)return false;
  // Duplicate with altered state cannot overwrite a fact or extend liveness.
  f.door=DoorState::Closed;
  if(link.admit(f,14000)!=Order::Duplicate || link.current(14000)!=DoorState::Open)return false;
  if(link.peer.online(15100) || link.current(15100)!=DoorState::Unknown)return false;
  if(link.lastState!=DoorState::Open || !link.peer.seen)return false;
  f.stamp.sequence=2;
  if(link.admit(f,16000)!=Order::Fresh || link.current(16000)!=DoorState::Closed)return false;
  f.stamp={8,1};f.door=DoorState::Unknown;
  if(link.admit(f,17000)!=Order::NewSession || link.current(17000)!=DoorState::Unknown)return false;
  f.stamp={7,999};f.door=DoorState::Open;
  if(link.admit(f,18000)!=Order::Stale || link.lastState!=DoorState::Unknown)return false;
  f.stamp={8,2};f.source=Node::Hub;
  if(link.admit(f,19000)!=Order::Invalid || link.peer.receivedAt!=17000)return false;
  return true;
}
constexpr bool wraps() {
  DoorLink link;Frame f;f.stamp={UINT32_MAX,UINT32_MAX};f.door=DoorState::Closed;
  link.admit(f,UINT32_MAX-1000);
  if(!link.peer.online(1000)||link.peer.online(15000))return false;
  f.stamp.sequence=1;
  if(link.admit(f,15001)!=Order::Fresh)return false;
  f.stamp={1,1};
  return link.admit(f,15002)==Order::NewSession;
}
static_assert(lifecycle(),"Door lifecycle, replay and timeout");
static_assert(wraps(),"Timer, sequence and boot wrap");
