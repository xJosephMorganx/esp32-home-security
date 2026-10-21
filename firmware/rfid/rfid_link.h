#pragma once
#include <RfidContract.h>

namespace Rfid {
using namespace Domotica;
struct Pending {
  bool active=false;
  Frame frame{};
  uint8_t bytes[kMaxFrameSize]{};
  size_t length=0;
  uint32_t startedAt=0;
  uint8_t attempts=0;
  constexpr bool start(const Frame& f,uint32_t now) {
    if(encode(f,bytes,sizeof(bytes),length)!=Error::Ok)return false;
    frame=f;startedAt=now;attempts=0;active=true;return true;
  }
  constexpr bool expired(uint32_t now) const {
    return active && elapsed(now,startedAt,kAckTimeoutMs*kMaxAttempts);
  }
  constexpr bool due(uint32_t now) const {
    return active && !expired(now) && attempts<kMaxAttempts
        && elapsed(now,startedAt,uint32_t(attempts)*kAckTimeoutMs);
  }
};

struct Link {
  PeerTracker hub{};
  Pending heartbeat{},card{};
  Stamp confirmed{};
  uint32_t confirmedStarted=0,contextId=0,grantStarted=0;
  SystemState system=SystemState::Unknown;
  uint32_t revision=0;
  CardMode mode=CardMode::Disabled;
  bool synchronized=false,grant=false;
  PresentationLatch latch{};
  constexpr bool online(uint32_t now) const {return synchronized && hub.online(now);}
  constexpr void revoke() {
    grant=false;card.active=false;latch.reset();
  }
  constexpr void disconnect() {
    revoke();synchronized=false;heartbeat.active=false;
    // Retain replay tracker and confirmed heartbeat across channel recovery.
  }
  constexpr bool usable(uint32_t now) const {
    return online(now) && grant && mode!=CardMode::Disabled && !card.active
        && !elapsed(now,grantStarted,kCardGrantMs);
  }
  constexpr bool accept(const Frame& f,uint8_t channel,uint8_t rxChannel,
                        uint32_t receivedAt,uint32_t now) {
    if(validate(f)!=Error::Ok || f.source!=Node::Hub || f.destination!=Node::Rfid
        || channel!=rxChannel || elapsed(now,receivedAt,kOfflineMs))return false;
    const Order order=hub.classify(f.stamp);
    if(order!=Order::First && order!=Order::NewSession && order!=Order::Fresh)return false;
    if(f.type==Type::StateSync) {
      const Stamp pending=heartbeat.active && !heartbeat.expired(now)?heartbeat.frame.stamp:Stamp{};
      if(f.channel!=channel || !correlates(f,pending,confirmed,Node::Rfid))return false;
      if(!online(now) || order==Order::NewSession)revoke();
      if(heartbeat.active && same(f.acknowledged,heartbeat.frame.stamp)) {
        confirmed=heartbeat.frame.stamp;confirmedStarted=heartbeat.startedAt;heartbeat.active=false;
      }
      synchronized=true;system=f.system;revision=f.revision;
    } else if(f.type==Type::CardContext) {
      if(!online(now) || f.stamp.boot!=hub.last.boot || !contextMatches(f,confirmed)
          || elapsed(now,confirmedStarted,kCardGrantMs))return false;
      if(f.cardMode!=mode)revoke();
      mode=f.cardMode;contextId=f.contextId;grantStarted=confirmedStarted;
      grant=mode!=CardMode::Disabled;
      // A renewal in the same mode never rearms the presentation latch.
    } else if(f.type==Type::CardResult) {
      if(!online(now) || !card.active || card.expired(now)
          || f.channel!=channel || !resultMatches(f,card.frame))return false;
      system=f.system;revision=f.revision;
      if(f.outcome!=CardOutcome::InProgress)card.active=false;
    } else return false;
    hub.accept(f.stamp,receivedAt);return true;
  }
  constexpr bool present(CardPresence presence,const CardUid& uid,Stamp id,uint32_t now) {
    if(!latch.observe(presence,now,usable(now)))return false;
    if(!validUid(uid))return false; // Failed UID still consumes this presentation.
    Frame f{};f.type=Type::CardRequest;f.source=Node::Rfid;f.destination=Node::Hub;
    f.stamp=id;f.targetHubBoot=hub.last.boot;f.contextId=contextId;f.cardMode=mode;f.uid=uid;
    grant=false;
    return card.start(f,now);
  }
};
}
