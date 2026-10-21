#include "../src/RfidContract.h"
#include "../../hub/door_link.h"
#include "../../hub/pir_link.h"
using namespace Domotica;
constexpr Frame card(uint32_t sequence=2,CardMode mode=CardMode::Normal,uint32_t token=10) {
  Frame f{};f.type=Type::CardRequest;f.source=Node::Rfid;f.destination=Node::Hub;
  f.stamp={7,sequence};f.targetHubBoot=9;f.contextId=token;f.cardMode=mode;
  f.uid.length=4;f.uid.bytes[0]=0;f.uid.bytes[1]=1;f.uid.bytes[2]=2;f.uid.bytes[3]=3;
  return f; // Synthetic codec input, NOT an authorized/default card.
}
constexpr Frame answer(Type type=Type::CardResult) {
  Frame f{};f.type=type;f.source=Node::Hub;f.destination=Node::Rfid;
  f.stamp={9,10};f.acknowledged={7,2};f.contextId=10;f.cardMode=CardMode::Normal;
  f.outcome=CardOutcome::Armed;f.system=SystemState::Armed;f.revision=1;f.channel=6;
  return f;
}
constexpr bool rfidCodec() {
  uint8_t bytes[kMaxFrameSize]{};size_t n=0;Frame out{};
  const uint8_t golden[]={68,77,1,10,4,1,7,0,0,0,2,0,0,0,20,0,
                         9,0,0,0,10,0,0,0,1,4,0,1,2,3,0,0,0,0,0,0};
  if(encode(card(),bytes,sizeof(bytes),n)!=Error::Ok || n!=sizeof(golden))return false;
  for(size_t i=0;i<n;++i)if(bytes[i]!=golden[i])return false;
  const uint8_t lengths[]={4,7,10};
  for(auto len:lengths) {
    auto f=card();f.uid.length=len;
    for(uint8_t i=4;i<len;++i)f.uid.bytes[i]=uint8_t(i+1);
    if(encode(f,bytes,sizeof(bytes),n)!=Error::Ok || decode(bytes,n,out)!=Error::Ok
        || !sameUid(f.uid,out.uid) || out.targetHubBoot!=9 || out.contextId!=10)return false;
    for(size_t truncated=0;truncated<n;++truncated)if(decode(bytes,truncated,out)==Error::Ok)return false;
    for(size_t cap=0;cap<n;++cap) {
      size_t written=42;
      if(encode(f,bytes,cap,written)!=Error::Buffer || written!=0)return false;
    }
  }
  for(uint8_t mode=1;mode<=2;++mode)for(uint8_t result=0;result<=11;++result) {
    auto f=answer();f.cardMode=static_cast<CardMode>(mode);f.outcome=static_cast<CardOutcome>(result);
    if(!outcomeForMode(f.outcome,f.cardMode)) {
      if(validate(f)!=Error::Payload)return false;
      continue;
    }
    if(encode(f,bytes,sizeof(bytes),n)!=Error::Ok || n!=36
        || decode(bytes,n,out)!=Error::Ok || out.outcome!=f.outcome
        || out.system!=f.system || out.revision!=f.revision
        || !resultMatches(out,card(2,f.cardMode)))return false;
  }
  for(uint8_t mode=0;mode<=2;++mode) {
    auto f=answer(Type::CardContext);f.cardMode=static_cast<CardMode>(mode);
    if(encode(f,bytes,sizeof(bytes),n)!=Error::Ok || n!=29
        || decode(bytes,n,out)!=Error::Ok || out.cardMode!=f.cardMode
        || !contextMatches(out,{7,2}))return false;
  }
  for(uint8_t type=8;type<=9;++type) {
    Frame f{};f.type=static_cast<Type>(type);f.source=Node::Rfid;f.stamp={7,1};
    if(encode(f,bytes,sizeof(bytes),n)!=Error::Ok || n!=16
        || decode(bytes,n,out)!=Error::Ok || out.type!=f.type || out.uid.length)return false;
  }
  auto sync=answer(Type::StateSync);
  return encode(sync,bytes,sizeof(bytes),n)==Error::Ok && n==30
      && decode(bytes,n,out)==Error::Ok && correlates(out,{7,2},{},Node::Rfid)
      && !correlates(out,{7,2},{}) && !correlates(out,{7,2},{},Node::Pir);
}
constexpr bool rfidInvalid() {
  auto f=card();uint8_t bytes[37]{};size_t n=0;Frame out=answer();
  encode(f,bytes,sizeof(bytes),n);
  if(decode(bytes,37,out)!=Error::Length)return false;
  const size_t positions[]={2,3,4,5,14,15,24,25,35};
  const uint8_t bad[]={2,99,2,4,19,1,0,5,1};
  for(size_t i=0;i<sizeof(positions)/sizeof(positions[0]);++i) {
    const auto save=bytes[positions[i]];bytes[positions[i]]=bad[i];
    if(decode(bytes,n,out)==Error::Ok || out.type!=Type::CardResult)return false;
    bytes[positions[i]]=save;
  }
  for(uint8_t len=0;len<12;++len) {
    f=card();f.uid.length=len;
    if(validUid(f.uid)!=(len==4 || len==7 || len==10))return false;
  }
  f=card();f.targetHubBoot=0;if(validate(f)!=Error::Payload)return false;
  f=card();f.contextId=0;if(validate(f)!=Error::Payload)return false;
  f=answer();f.destination=Node::Pir;if(validate(f)!=Error::Identity)return false;
  f=answer(Type::CardContext);f.contextId=11;if(validate(f)!=Error::Payload)return false;
  f=answer();f.outcome=static_cast<CardOutcome>(12);if(validate(f)!=Error::Payload)return false;
  f=answer();f.stamp.boot=10;if(resultMatches(f,card()))return false;
  f=answer();f.contextId=11;if(resultMatches(f,card()))return false;
  f=answer();f.cardMode=CardMode::Enroll;f.outcome=CardOutcome::Added;
  if(resultMatches(f,card()))return false;
  DoorLink door;PirLink pir;
  return door.admit(card(),0)==Order::Invalid && pir.admit(card(),0)==Order::Invalid
      && !isSensorNode(Node::Rfid) && isLeafNode(Node::Rfid);
}
constexpr bool dedupAndModes() {
  CardGate g;g.begin(9,7);
  if(!g.issue(10,CardMode::Normal,0) || g.admit(card(),1,true)!=CardAdmission::New)return false;
  if(g.admit(card(),2,true)!=CardAdmission::Duplicate || g.outcome!=CardOutcome::InProgress)return false;
  if(g.issue(11,CardMode::Enroll,3) || g.finish(CardOutcome::Added))return false;
  if(!g.finish(CardOutcome::Armed) || g.finish(CardOutcome::Disarmed))return false;
  g.revoke();
  if(g.admit(card(),9000,false)!=CardAdmission::Duplicate || g.outcome!=CardOutcome::Armed)return false;
  auto changed=card();changed.uid.bytes[0]=9;
  if(g.admit(changed,9001,true)!=CardAdmission::Conflict)return false;
  if(!g.issue(11,CardMode::Enroll,10000))return false;
  if(g.admit(card(3),10001,true)!=CardAdmission::WrongContext)return false; // old normal
  if(g.admit(card(3,CardMode::Enroll,11),10001,true)!=CardAdmission::New)return false;
  if(!g.finish(CardOutcome::Added))return false;
  g.revoke();
  if(!g.issue(12,CardMode::Normal,10002))return false;
  if(g.admit(card(4,CardMode::Enroll,11),10003,true)!=CardAdmission::WrongContext)return false;
  if(g.admit(card(3,CardMode::Enroll,11),10004,true)!=CardAdmission::Duplicate)return false;
  if(g.admit(card(),10005,true)!=CardAdmission::Stale)return false;
  // Every terminal denial/persistence failure remains terminal on retry.
  if(g.admit(card(4,CardMode::Normal,12),10006,true)!=CardAdmission::New)return false;
  if(!g.finish(CardOutcome::Unauthorized) || g.admit(card(4,CardMode::Normal,12),10007,true)!=CardAdmission::Duplicate)return false;
  if(!g.issue(13,CardMode::Enroll,11000)
      || g.admit(card(5,CardMode::Enroll,13),11001,true)!=CardAdmission::New
      || !g.finish(CardOutcome::StorageError))return false;
  return g.admit(card(5,CardMode::Enroll,13),11002,true)==CardAdmission::Duplicate
      && g.outcome==CardOutcome::StorageError;
}
constexpr bool expiryAndRestart() {
  CardGate g;g.begin(9,7);g.issue(10,CardMode::Normal,UINT32_MAX-99);
  if(g.admit(card(),4900,true)!=CardAdmission::WrongContext)return false; // exactly 5000 ms
  g.begin(9,7);g.issue(10,CardMode::Normal,UINT32_MAX-99);
  if(g.admit(card(),4899,false)!=CardAdmission::WrongContext)return false;
  if(g.admit(card(),4899,true)!=CardAdmission::New || !g.finish(CardOutcome::Disarmed))return false;
  g.begin(10,7);g.issue(11,CardMode::Normal,0);
  if(g.admit(card(),1,true)!=CardAdmission::WrongContext)return false; // hub reboot
  g.begin(9,8);g.issue(12,CardMode::Normal,0);
  if(g.admit(card(),1,true)!=CardAdmission::WrongContext)return false; // reader reboot
  g.begin(9,7);g.issue(10,CardMode::Normal,0);g.revoke();
  if(g.admit(card(),1,true)!=CardAdmission::WrongContext || g.issue(10,CardMode::Normal,2))return false;
  g.issue(11,CardMode::Disabled,3);
  return g.admit(card(2,CardMode::Normal,11),4,true)==CardAdmission::WrongContext;
}
constexpr bool presentations() {
  PresentationLatch p;
  if(p.observe(CardPresence::Present,0,true) || p.observe(CardPresence::Present,1000,true))return false;
  p.observe(CardPresence::Absent,1001,true);
  if(p.observe(CardPresence::Present,1500,true))return false;
  p.observe(CardPresence::Absent,2000,true);p.observe(CardPresence::Absent,2500,true);
  if(!p.observe(CardPresence::Present,2501,true) || p.observe(CardPresence::Present,3000,true))return false;
  p.reset(); // mode change/reboot with card still present
  if(p.observe(CardPresence::Present,3100,true))return false;
  p.observe(CardPresence::Absent,4000,true);p.observe(CardPresence::Unknown,4500,true);
  if(p.observe(CardPresence::Present,4501,true))return false;
  p.observe(CardPresence::Absent,UINT32_MAX-99,true);p.observe(CardPresence::Absent,400,true);
  if(!p.observe(CardPresence::Present,401,true))return false;
  p.observe(CardPresence::Absent,1000,true);p.observe(CardPresence::Absent,1600,true);
  return !p.observe(CardPresence::Present,1601,false) && !p.observe(CardPresence::Present,1700,true);
}
constexpr bool policyAndLeds() {
  for(uint8_t state=0;state<=4;++state) {
    auto s=static_cast<SystemState>(state);
    if(decideNormalCard(s,false,true,true)!=CardOutcome::Unauthorized)return false;
    for(uint8_t flags=0;flags<4;++flags) {
      auto leds=rfidLeds(s,(flags&1)!=0,(flags&2)!=0);
      if(leds.red!=(flags==3 && (state==3 || state==4))
          || leds.green!=(flags==3 && state==1))return false;
    }
  }
  if(decideNormalCard(SystemState::Unknown,true,true,true)!=CardOutcome::Unavailable)return false;
  if(decideNormalCard(SystemState::Disarmed,true,false,false)!=CardOutcome::SensorsOffline)return false;
  if(decideNormalCard(SystemState::Disarmed,true,true,false)!=CardOutcome::Armed
      || decideNormalCard(SystemState::Disarmed,true,false,true)!=CardOutcome::Armed)return false;
  for(uint8_t state=2;state<=4;++state)
    if(decideNormalCard(static_cast<SystemState>(state),true,false,false)!=CardOutcome::Disarmed)return false;
  return kMaxAccessCards==32 && kEnrollmentMs==30000;
}
static_assert(rfidCodec(),"RFID wire golden, UID sizes, outcomes, context and sync");
static_assert(rfidInvalid(),"Reject malformed RFID and cross-node traffic");
static_assert(dedupAndModes(),"At-most-once, result loss, changed retries and mode boundaries");
static_assert(expiryAndRestart(),"Revocation, offline, time wrap, reboot and non-reusable grants");
static_assert(presentations(),"Held cards, removal interval, mode reset and no offline replay");
static_assert(policyAndLeds(),"Only sensors gate arming; authorized disarm; confirmed LED states");
