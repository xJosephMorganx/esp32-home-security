#include "../door_link.h"
#include "../pir_link.h"
#include "../sync_scheduler.h"
using namespace Domotica;
constexpr bool admission() {
  DoorLink door;PirLink pir;
  Frame d;d.stamp={1,1};d.door=DoorState::Closed;
  Frame p;p.type=Type::PirHello;p.source=Node::Pir;p.stamp={1,1};p.pir=PirState::NotReady;
  if(door.admit(p,10)!=Order::Invalid||pir.admit(d,10)!=Order::Invalid)return false;
  if(door.admit(d,10)!=Order::First||pir.admit(p,10)!=Order::First)return false;
  p.pir=PirState::Motion;
  if(pir.admit(p,100)!=Order::Duplicate||pir.lastState!=PirState::NotReady||pir.peer.receivedAt!=10)return false;
  p.stamp.sequence=2;
  if(pir.admit(p,14999)!=Order::Fresh||pir.current(15010)!=PirState::Motion)return false;
  if(door.current(15010)!=DoorState::Unknown||door.peer.online(15010))return false;
  if(pir.current(29999)!=PirState::Unknown)return false;
  p.stamp={2,1};p.pir=PirState::Unknown;
  if(pir.admit(p,30000)!=Order::NewSession||pir.lastState!=PirState::Unknown)return false;
  p.stamp={1,3};
  return pir.admit(p,30001)==Order::Stale && door.lastState==DoorState::Closed;
}
constexpr bool scheduling() {
  SyncScheduler s;PeerTracker d,p;
  d.accept({1,4},10);p.accept({2,7},10);
  s.peers[0].refresh(d,20,1);s.peers[1].refresh(p,20,1);
  if(s.take(true,true)!=0)return false;
  // A busy door cannot starve PIR, even while its own pending flag stays set.
  if(s.take(true,true)!=1||s.take(true,true)!=0)return false;
  if(!same(s.peers[0].reference,d.last)||!same(s.peers[1].reference,p.last))return false;
  s.peers[0].pending=false;s.peers[0].lastSync=20;s.peers[0].publishedRevision=1;
  s.peers[0].refresh(d,21,1);
  if(s.peers[0].pending||s.take(true,false)!=-1)return false;
  s.peers[0].refresh(d,21,2);
  if(!s.peers[0].pending||s.peers[1].publishedRevision!=0)return false;
  s.peers[0].pending=false;s.peers[0].publishedRevision=2;
  s.peers[0].refresh(d,5020,2);
  if(!s.peers[0].pending)return false;
  PeerTracker unseen;
  s.peers[1]=PeerSync{};s.peers[1].refresh(unseen,9000,2);
  return !s.peers[1].pending;
}
static_assert(admission(),"Independent sensor types, duplicate handling, timeout and restart");
static_assert(scheduling(),"Fair sends, disabled peer, own ACK, revision and heartbeat");
constexpr bool threePeers() {
  SyncScheduler s;
  s.peers[0].reply({1,1});s.peers[1].reply({2,1});
  // RFID result/context work also gets a turn without replacing its heartbeat slot.
  for(int cycle=0;cycle<4;++cycle) {
    if(s.take(true,true,true,true)!=0)return false;
    if(s.take(true,true,true,true)!=1)return false;
    if(s.take(true,true,true,true)!=2)return false;
  }
  s.peers[0].pending=false;s.peers[1].pending=false;
  if(s.take(true,true,false,true)!=-1)return false;
  s.peers[2].reply({3,7});
  return s.take(true,true,true)==2&&Domotica::same(s.peers[2].reference,{3,7});
}
static_assert(threePeers(),"Third peer work cannot starve sensor acknowledgements");
