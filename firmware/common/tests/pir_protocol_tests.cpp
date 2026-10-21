#include "../src/DomoticaProtocol.h"
// Regression against the actual consumers, without editing either firmware.
#include "../../hub/door_link.h"
#include "../../door/door_logic.h"
using namespace Domotica;

constexpr Frame pirReport(Type type = Type::PirHello, PirState state = PirState::NotReady) {
  Frame f{}; f.type=type; f.source=Node::Pir; f.destination=Node::Hub;
  f.stamp={0x12345678UL,0x01020304UL}; f.pir=state; return f;
}
constexpr Frame stateSync(Node destination) {
  Frame f{}; f.type=Type::StateSync; f.source=Node::Hub; f.destination=destination;
  f.stamp={9,10}; f.acknowledged={7,8}; f.revision=11;
  f.system=SystemState::Armed; f.channel=6; return f;
}
constexpr bool wireVectors() {
  const uint8_t hello[] = {0x44,0x4d,1,5,3,1,0x78,0x56,0x34,0x12,4,3,2,1,1,0,1};
  const uint8_t doorSync[] = {0x44,0x4d,1,4,1,2,9,0,0,0,10,0,0,0,14,0,7,0,0,0,8,0,0,0,11,0,0,0,3,6};
  uint8_t bytes[kMaxFrameSize]{}; size_t n=0;
  if(encode(pirReport(),bytes,sizeof(bytes),n)!=Error::Ok || n!=sizeof(hello))return false;
  for(size_t i=0;i<n;++i)if(bytes[i]!=hello[i])return false;
  if(encode(stateSync(Node::Door),bytes,sizeof(bytes),n)!=Error::Ok || n!=sizeof(doorSync))return false;
  for(size_t i=0;i<n;++i)if(bytes[i]!=doorSync[i])return false;
  if(encode(stateSync(Node::Pir),bytes,sizeof(bytes),n)!=Error::Ok)return false;
  for(size_t i=0;i<n;++i)if(bytes[i]!=(i==5?3:doorSync[i]))return false;
  return kVersion==1 && kHeaderSize==16 && payloadSize(Type::StateSync)==14 && kMaxFrameSize>=30;
}
constexpr bool allStates() {
  uint8_t bytes[kMaxFrameSize]{}; size_t n=0; Frame out{};
  for(uint8_t t=5;t<=7;++t)for(uint8_t s=0;s<=3;++s) {
    auto f=pirReport(static_cast<Type>(t),static_cast<PirState>(s));
    f.door=DoorState::Open; // Inactive field MUST NOT leak onto wire or decoded frame.
    if(encode(f,bytes,sizeof(bytes),n)!=Error::Ok || n!=17 || bytes[16]!=s)return false;
    if(decode(bytes,n,out)!=Error::Ok || out.pir!=f.pir || out.type!=f.type
        || out.source!=Node::Pir || out.door!=DoorState::Unknown)return false;
    if(pirReady(out.pir)!=(s==2 || s==3))return false;
    for(size_t length=0;length<n;++length)if(decode(bytes,length,out)==Error::Ok)return false;
    if(decode(bytes,n+1,out)==Error::Ok)return false;
  }
  for(uint8_t state=0;state<=4;++state) {
    auto f=stateSync(Node::Pir); f.system=static_cast<SystemState>(state); f.revision=state?1:0;
    if(encode(f,bytes,sizeof(bytes),n)!=Error::Ok || decode(bytes,n,out)!=Error::Ok
        || out.destination!=Node::Pir || out.system!=f.system
        || !correlates(out,{}, {7,8},Node::Pir))return false;
  }
  return true;
}
constexpr bool invalidFrames() {
  uint8_t bytes[kMaxFrameSize]{}; size_t n=0;
  auto f=pirReport(); Frame out=stateSync(Node::Door);
  encode(f,bytes,sizeof(bytes),n);
  for(unsigned state=4;state<=255;++state) {
    bytes[16]=uint8_t(state);
    if(decode(bytes,n,out)!=Error::Payload || out.type!=Type::StateSync)return false;
    f.pir=static_cast<PirState>(state);
    if(validate(f)!=Error::Payload)return false;
  }
  f=pirReport(); f.source=Node::Door;
  if(validate(f)!=Error::Identity)return false;
  f=pirReport(); f.destination=Node::Door;
  if(validate(f)!=Error::Identity)return false;
  f=pirReport(); f.type=Type::Hello;
  if(validate(f)!=Error::Identity)return false;
  f=pirReport(); f.source=static_cast<Node>(4);
  if(validate(f)!=Error::Identity)return false;
  f=stateSync(static_cast<Node>(5));
  if(validate(f)!=Error::Identity)return false;
  f=pirReport(); f.stamp.sequence=0;
  if(validate(f)!=Error::Counter)return false;
  f=pirReport(); f.pir=PirState::Motion;
  for(size_t cap=0;cap<17;++cap) {
    bytes[0]=0;
    if(encode(f,bytes,cap,n)!=Error::Buffer || n!=0 || bytes[0]!=0)return false;
  }
  return true;
}
constexpr bool isolation() {
  // Same counters are normal for independent nodes; recipient is mandatory.
  auto p=stateSync(Node::Pir), d=stateSync(Node::Door);
  if(correlates(p,{7,8},{}) || correlates(p,{7,8},{},Node::Door))return false;
  if(correlates(d,{7,8},{},Node::Pir) || !correlates(d,{7,8},{}))return false;
  if(!correlates(p,{7,8},{},Node::Pir) || correlates(p,{7,9},{7,7},Node::Pir))return false;
  DoorLink hubDoor;
  if(hubDoor.admit(pirReport(),100)!=Order::Invalid || hubDoor.peer.seen)return false;
  Door::Link door; Door::Pending pending;
  pending.start(Type::Hello,DoorState::Closed,{7,8},0);
  if(door.accept(p,pending,6,6,100) || !pending.active || door.hub.seen || door.led(100))return false;
  if(!door.accept(d,pending,6,6,100) || pending.active || !door.led(100))return false;
  // Separate trackers: PIR traffic/reboots do not keep the door alive.
  PeerTracker doorTrack, pirTrack;
  doorTrack.accept({1,1},0); pirTrack.accept({1,1},0);
  pirTrack.accept({1,2},14000);
  if(doorTrack.online(15000) || !pirTrack.online(15000))return false;
  if(pirTrack.accept({1,2},28000)!=Order::Duplicate || pirTrack.online(29000))return false;
  if(pirTrack.accept({2,1},30000)!=Order::NewSession)return false;
  return pirTrack.accept({1,3},30001)==Order::Stale && !doorTrack.online(30001);
}
static_assert(wireVectors(),"Door bytes unchanged; PIR has distinct report type and destination");
static_assert(allStates(),"Every PIR report/state and real global state round trips");
static_assert(invalidFrames(),"Invalid PIR states, identities, lengths and buffers rejected");
static_assert(isolation(),"PIR cannot enter door consumers or acknowledge their pending reports");
static_assert(!isReport(Type::PirEvent) && isPirReport(Type::PirEvent)
              && isSensorReport(Type::PirEvent),"Keep door admission helper door-only");
