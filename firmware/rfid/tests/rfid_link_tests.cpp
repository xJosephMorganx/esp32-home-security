#include "../rfid_link.h"
using namespace Domotica;
using namespace Rfid;

constexpr Frame heartbeat(uint32_t seq=1) {
  Frame f{};f.type=Type::RfidHello;f.source=Node::Rfid;f.destination=Node::Hub;
  f.stamp={3,seq};return f;
}
constexpr Frame sync(uint32_t seq=1,uint32_t hubBoot=7) {
  Frame f{};f.type=Type::StateSync;f.source=Node::Hub;f.destination=Node::Rfid;
  f.stamp={hubBoot,seq};f.acknowledged={3,1};f.system=SystemState::Disarmed;
  f.revision=1;f.channel=6;return f;
}
constexpr Frame context(uint32_t seq=2,CardMode mode=CardMode::Normal) {
  Frame f=sync(seq);f.type=Type::CardContext;f.contextId=seq;f.cardMode=mode;return f;
}
constexpr CardUid uid(uint8_t length=7) {
  CardUid u{};u.length=length;for(uint8_t i=1;i<length;++i)u.bytes[i]=i;return u;
}
constexpr bool connect(Link& link) {
  return link.heartbeat.start(heartbeat(),100)
      && link.accept(sync(),6,6,110,110)
      && link.accept(context(),6,6,120,120);
}
constexpr bool request(Link& link) {
  link.present(CardPresence::Absent,{}, {3,2},200);
  link.present(CardPresence::Absent,{}, {3,2},700);
  return link.present(CardPresence::Present,uid(),{3,2},750);
}
constexpr Frame result(const Link& link,CardOutcome outcome=CardOutcome::Armed) {
  Frame f=sync(3);f.type=Type::CardResult;f.acknowledged=link.card.frame.stamp;
  f.contextId=link.card.frame.contextId;f.cardMode=link.card.frame.cardMode;
  f.outcome=outcome;f.system=SystemState::Armed;f.revision=2;return f;
}
constexpr bool retries() {
  Link l{};if(!connect(l)||!request(l))return false;
  if(!l.card.due(750))return false;
  uint8_t original[kMaxFrameSize]{};
  for(size_t i=0;i<l.card.length;++i)original[i]=l.card.bytes[i];
  ++l.card.attempts;
  if(l.card.due(999)||!l.card.due(1000))return false;
  ++l.card.attempts;
  if(!l.card.due(1250)||l.card.expired(1499)||!l.card.expired(1500))return false;
  Frame inProgress=result(l,CardOutcome::InProgress);
  if(!l.accept(inProgress,6,6,1300,1300)||!l.card.active||l.card.startedAt!=750)return false;
  for(size_t i=0;i<l.card.length;++i)if(original[i]!=l.card.bytes[i])return false;
  auto terminal=result(l);terminal.stamp.sequence=4;
  return !l.accept(terminal,6,6,1499,1500); // Processing deadline, not callback time.
}
constexpr bool heldAndRenewed() {
  Link l{};if(!connect(l))return false;
  if(l.present(CardPresence::Present,uid(),{3,2},130))return false; // Boot with held card.
  if(!request(l))return false;
  auto r=result(l);
  // Outcome is the historic action; snapshot is the CURRENT state.
  r.system=SystemState::Disarmed;
  if(!l.accept(r,6,6,800,800)||l.card.active||l.system!=SystemState::Disarmed)return false;
  if(!l.accept(context(4),6,6,810,810))return false;
  if(l.present(CardPresence::Present,uid(4),{3,3},900))return false;
  l.present(CardPresence::Absent,{}, {3,3},1000);
  if(l.present(CardPresence::Present,uid(),{3,3},1499))return false;
  l.present(CardPresence::Absent,{}, {3,3},1600);
  l.present(CardPresence::Absent,{}, {3,3},2100);
  return l.present(CardPresence::Present,uid(10),{3,3},2150);
}
constexpr bool rejectionAndRecovery() {
  Link l{};if(!connect(l)||!request(l))return false;
  Frame r=result(l);r.acknowledged.sequence=9;
  if(l.accept(r,6,6,800,800))return false;
  r=result(l);
  if(l.accept(r,6,5,800,800))return false;
  r.destination=Node::Pir;
  if(l.accept(r,6,6,800,800))return false;
  l.disconnect();
  if(l.accept(result(l),6,6,900,900)||l.card.active||l.grant)return false;
  if(l.accept(context(4),6,6,900,900))return false;
  if(!l.accept(sync(5),6,6,1000,1000))return false;
  if(l.accept(sync(4),6,6,1010,1010))return false;
  if(!l.accept(context(6),6,6,1020,1020))return false;
  return !l.present(CardPresence::Present,uid(),{3,3},1100);
}
constexpr bool sessionAndModes() {
  Link l{};if(!connect(l)||!request(l))return false;
  if(!l.accept(context(3,CardMode::Enroll),6,6,800,800)||l.card.active)return false;
  if(l.present(CardPresence::Present,uid(),{3,3},810))return false;
  l.present(CardPresence::Absent,{}, {3,3},820);
  l.present(CardPresence::Absent,{}, {3,3},1320);
  if(!l.present(CardPresence::Present,uid(),{3,3},1350)
      || l.card.frame.cardMode!=CardMode::Enroll)return false;
  const Frame old=result(l,CardOutcome::Added);
  if(!l.accept(sync(1,8),6,6,1400,1400)||l.card.active||l.grant)return false;
  if(l.accept(old,6,6,1410,1410))return false;
  return !l.accept(sync(9,7),6,6,1420,1420);
}
constexpr bool presenceFailures() {
  Link l{};if(!connect(l))return false;
  l.present(CardPresence::Absent,{}, {3,2},200);
  l.present(CardPresence::Absent,{}, {3,2},700);
  if(l.present(CardPresence::Present,{}, {3,2},710))return false;
  if(l.present(CardPresence::Present,uid(),{3,2},720))return false;
  l.present(CardPresence::Absent,{}, {3,2},730);
  l.present(CardPresence::Unknown,{}, {3,2},1000);
  l.present(CardPresence::Absent,{}, {3,2},1200);
  if(l.present(CardPresence::Present,uid(),{3,2},1600))return false;
  return !l.usable(5100);
}
constexpr bool uidAndLeds() {
  constexpr uint8_t lengths[]={4,7,10};
  for(uint8_t n : lengths) {
    Link l{};if(!connect(l))return false;
    l.present(CardPresence::Absent,{}, {3,2},200);
    l.present(CardPresence::Absent,{}, {3,2},700);
    if(!l.present(CardPresence::Present,uid(n),{3,2},750))return false;
    Frame decoded{};
    if(decode(l.card.bytes,l.card.length,decoded)!=Error::Ok || !sameUid(decoded.uid,uid(n)))return false;
  }
  for(uint8_t s=0;s<5;++s) {
    const auto state=static_cast<SystemState>(s);
    const auto on=rfidLeds(state,true,true);
    if(on.red!=(s==3 || s==4) || on.green!=(s==1))return false;
    if(rfidLeds(state,true,false).red || rfidLeds(state,false,true).green)return false;
  }
  return true;
}
constexpr bool lostReplyAndHeartbeat() {
  Link l{};if(!connect(l)||!request(l))return false;
  CardGate hub{};hub.begin(7,3);
  if(!hub.issue(2,CardMode::Normal,120))return false;
  const Frame sent=l.card.frame;
  if(hub.admit(sent,760,true)!=CardAdmission::New || !hub.finish(CardOutcome::Armed))return false;
  if(hub.admit(sent,1000,true)!=CardAdmission::Duplicate)return false;
  if(hub.admit(sent,1250,true)!=CardAdmission::Duplicate)return false;
  auto hbSync=sync(3);
  if(!l.accept(hbSync,6,6,1300,1300)||!l.card.active)return false;
  if(l.card.expired(1499)||!l.card.expired(1500))return false;
  l.revoke();
  if(l.accept(result(l),6,6,1501,1501))return false;
  l.disconnect();
  if(hub.admit(sent,16000,false)!=CardAdmission::Duplicate)return false;
  return hub.outcome==CardOutcome::Armed && !l.card.active && !l.grant;
}
static_assert(retries(),"Exact retries, InProgress and processing deadline");
static_assert(heldAndRenewed(),"Held card, UID replacement and physical removal");
static_assert(rejectionAndRecovery(),"Correlation, channel, recipient and no offline replay");
static_assert(sessionAndModes(),"Enrollment and new hub session invalidate pending cards");
static_assert(presenceFailures(),"Unknown presence and failed UID cannot rearm");
static_assert(uidAndLeds(),"Exact UID bytes and all global LED states");
static_assert(lostReplyAndHeartbeat(),"Lost results never create another action; heartbeat is not card ACK");
