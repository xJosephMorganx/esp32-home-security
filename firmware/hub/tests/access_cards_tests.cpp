#include "../access_list.h"
#include "../rfid_authority.h"
#include "../../rfid/rfid_link.h" // Read-only interoperability check against the actual reader.
using namespace Domotica;
constexpr CardUid uid(uint8_t first) {return {4,{first,0,2,3}};}
struct FakeStore {
  AccessList list;
  bool ready=true,failWrite=false;
  unsigned writes=0;
  constexpr bool save(const AccessList& value) {
    ++writes;
    if(failWrite){ready=false;return false;}
    list=value;return true;
  }
};
constexpr Frame request(uint32_t sequence,uint32_t token,CardMode mode,CardUid card) {
  Frame f;f.type=Type::CardRequest;f.source=Node::Rfid;f.destination=Node::Hub;
  f.stamp={2,sequence};f.targetHubBoot=1;f.contextId=token;f.cardMode=mode;f.uid=card;return f;
}
constexpr bool storageFormat() {
  AccessList a;
  if(!a.add(uid(0))||a.add(uid(0)))return false;
  CardUid longUid{10,{1,2,3,4,5,6,7,8,9,0}};
  if(!a.add(longUid))return false;
  uint8_t bytes[kAccessBlobSize]={};AccessList b;
  if(!encodeAccess(a,bytes)||!decodeAccess(bytes,sizeof(bytes),b)||b.count!=2||b.find(longUid)!=1)return false;
  bytes[8]^=1;
  if(decodeAccess(bytes,sizeof(bytes),b)||b.count!=2)return false;
  for(uint8_t i=1;a.count<kMaxAccessCards;++i)if(!a.add(uid(i)))return false;
  if(a.add(uid(99))||!a.remove(longUid)||!a.add(uid(99)))return false;
  a.cards[1]=a.cards[0];
  return !a.valid()&&!encodeAccess(a,bytes);
}
constexpr bool authority() {
  RfidAuthority<FakeStore> a;SystemController c;
  a.gate.begin(1,2);
  if(!a.start(10,true)||!a.gate.issue(10,CardMode::Enroll,10))return false;
  auto f=request(1,10,CardMode::Enroll,uid(1));
  if(a.gate.admit(f,20,true)!=CardAdmission::New)return false;
  if(a.execute(f,c,false,false)!=CardOutcome::Added||c.armed()||a.enrolling||a.store.writes!=1)return false;
  if(a.gate.admit(f,30,true)!=CardAdmission::Duplicate||a.gate.outcome!=CardOutcome::Added)return false;
  if(!a.gate.issue(11,CardMode::Normal,40))return false;
  f=request(2,11,CardMode::Normal,uid(1));
  if(a.gate.admit(f,50,true)!=CardAdmission::New||a.execute(f,c,false,false)!=CardOutcome::SensorsOffline)return false;
  a.gate.issue(12,CardMode::Normal,60);f=request(3,12,CardMode::Normal,uid(1));
  if(a.gate.admit(f,70,true)!=CardAdmission::New||a.execute(f,c,false,true)!=CardOutcome::Armed)return false;
  c.request(false,false,false); // A later touch disarm does not replay the earlier toggle.
  if(a.gate.admit(f,80,true)!=CardAdmission::Duplicate||c.armed()||a.gate.outcome!=CardOutcome::Armed)return false;
  c.request(true,true,false);
  a.gate.issue(13,CardMode::Normal,90);f=request(4,13,CardMode::Normal,uid(1));
  if(a.gate.admit(f,100,true)!=CardAdmission::New||a.execute(f,c,false,false)!=CardOutcome::Disarmed)return false;
  a.start(110,true);a.gate.issue(14,CardMode::Enroll,110);
  f=request(5,14,CardMode::Enroll,uid(2));a.cancel();
  if(a.gate.admit(f,120,true)!=CardAdmission::WrongContext||a.store.writes!=1)return false;
  a.start(130,true);a.tick(30130,true);
  if(a.enrolling||a.gate.grant)return false;
  a.start(30200,true);a.gate.issue(15,CardMode::Enroll,30200);
  f=request(6,15,CardMode::Enroll,uid(2));a.store.failWrite=true;
  if(a.gate.admit(f,30210,true)!=CardAdmission::New||a.execute(f,c,true,true)!=CardOutcome::StorageError)return false;
  if(a.store.ready||a.store.list.find(uid(2))>=0||a.start(30220,true))return false;
  return a.gate.admit(f,30230,true)==CardAdmission::Duplicate && a.store.writes==2;
}
constexpr bool readerInterop() {
  Rfid::Link reader;Frame hb;
  hb.type=Type::RfidHello;hb.source=Node::Rfid;hb.stamp={2,1};
  if(!reader.heartbeat.start(hb,10))return false;
  Frame sync;sync.type=Type::StateSync;sync.source=Node::Hub;sync.destination=Node::Rfid;
  sync.stamp={1,1};sync.acknowledged=hb.stamp;sync.system=SystemState::Disarmed;sync.revision=1;sync.channel=1;
  if(!reader.accept(sync,1,1,20,20))return false;
  Frame context;context.type=Type::CardContext;context.source=Node::Hub;context.destination=Node::Rfid;
  context.stamp={1,2};context.contextId=2;context.cardMode=CardMode::Normal;context.acknowledged=hb.stamp;
  if(!reader.accept(context,1,1,30,30))return false;
  reader.present(CardPresence::Absent,uid(1),{2,2},40);
  reader.present(CardPresence::Absent,uid(1),{2,2},540);
  if(!reader.present(CardPresence::Present,uid(1),{2,2},550))return false;
  Frame result;result.type=Type::CardResult;result.source=Node::Hub;result.destination=Node::Rfid;
  result.stamp={1,3};result.acknowledged={2,2};result.contextId=2;result.cardMode=CardMode::Normal;
  result.outcome=CardOutcome::Armed;result.system=SystemState::Armed;result.revision=2;result.channel=1;
  return reader.accept(result,1,1,560,560)&&reader.system==SystemState::Armed&&!reader.card.active;
}
static_assert(storageFormat(),"Versioned card set, CRC, exact UID, duplicates and capacity");
static_assert(authority(),"Enrollment, single execution, touch interleave, timeout and storage failure");
static_assert(readerInterop(),"Actual RFID link accepts sync, grant and correlated result");
