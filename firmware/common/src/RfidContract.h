#pragma once
#include "DomoticaProtocol.h"

namespace Domotica {
constexpr uint32_t kCardGrantMs=5000;
constexpr uint32_t kEnrollmentMs=30000;
constexpr uint32_t kCardRemovalMs=500;
constexpr size_t kMaxAccessCards=32;

struct RfidLeds { bool red=false,green=false; };
constexpr RfidLeds rfidLeds(SystemState s,bool synchronized,bool online) {
  return {synchronized && online && (s==SystemState::Armed || s==SystemState::Alarm),
          synchronized && online && s==SystemState::Disarmed};
} // EXIT_DELAY explicitly means both off, not confirmed ARMED or DISARMED.

// A hub-side decision only, NOT proof of execution. Apply once before reporting success.
constexpr CardOutcome decideNormalCard(SystemState s,bool authorized,bool doorOnline,bool pirOnline) {
  if(!authorized)return CardOutcome::Unauthorized;
  if(s==SystemState::Disarmed)
    return doorOnline || pirOnline ? CardOutcome::Armed : CardOutcome::SensorsOffline;
  if(s==SystemState::Armed || s==SystemState::Alarm || s==SystemState::ExitDelay)
    return CardOutcome::Disarmed;
  return CardOutcome::Unavailable;
}
constexpr bool sameCardRequest(const Frame& a,const Frame& b) {
  return a.type==Type::CardRequest && b.type==Type::CardRequest && same(a.stamp,b.stamp)
      && a.targetHubBoot==b.targetHubBoot && a.contextId==b.contextId
      && a.cardMode==b.cardMode && sameUid(a.uid,b.uid);
}
constexpr bool resultMatches(const Frame& result,const Frame& pending) {
  return validate(result)==Error::Ok && validate(pending)==Error::Ok
      && result.type==Type::CardResult && pending.type==Type::CardRequest
      && result.stamp.boot==pending.targetHubBoot && same(result.acknowledged,pending.stamp)
      && result.contextId==pending.contextId && result.cardMode==pending.cardMode;
}
constexpr bool contextMatches(const Frame& context,Stamp pendingHeartbeat) {
  return context.type==Type::CardContext && validate(context)==Error::Ok
      && valid(pendingHeartbeat) && same(context.acknowledged,pendingHeartbeat);
}

enum class CardAdmission : uint8_t { New, Duplicate, Stale, Conflict, WrongContext, Invalid };
// Contract reference state machine only: no NVS, UID authorization, GPIO or radio.
// Single-threaded hub owner must call admit BEFORE any effect, then finish.
// Revoke on mode changes/offline/cancel; begin only on a validated new session.
struct CardGate {
  uint32_t hubBoot=0,readerBoot=0,token=0,issuedAt=0;
  CardMode mode=CardMode::Disabled;
  bool grant=false,cached=false;
  Frame request{};
  CardOutcome outcome=CardOutcome::InProgress;
  constexpr void begin(uint32_t hub,uint32_t reader) {
    hubBoot=hub;readerBoot=reader;token=0;issuedAt=0;mode=CardMode::Disabled;
    grant=false;cached=false;request=Frame{};outcome=CardOutcome::InProgress;
  }
  constexpr void revoke() { grant=false;mode=CardMode::Disabled; }
  // Tokens never wrap/reuse within a hub session. A new context uses its TX sequence.
  constexpr bool issue(uint32_t id,CardMode next,uint32_t now) {
    if(!hubBoot || !readerBoot || !id || id<=token || static_cast<uint8_t>(next)>2
        || (cached && outcome==CardOutcome::InProgress))return false;
    token=id;mode=next;issuedAt=now;grant=next!=CardMode::Disabled;return true;
  }
  constexpr CardAdmission admit(const Frame& f,uint32_t now,bool readerOnline) {
    if(validate(f)!=Error::Ok || f.type!=Type::CardRequest)return CardAdmission::Invalid;
    if(f.targetHubBoot!=hubBoot || f.stamp.boot!=readerBoot)return CardAdmission::WrongContext;
    // Look up duplicates BEFORE checking revoked/expired grants. Never execute again.
    if(cached && same(f.stamp,request.stamp))
      return sameCardRequest(f,request)?CardAdmission::Duplicate:CardAdmission::Conflict;
    if(cached && !newer(f.stamp.sequence,request.stamp.sequence))return CardAdmission::Stale;
    if(!readerOnline || !grant || f.contextId!=token || f.cardMode!=mode
        || elapsed(now,issuedAt,kCardGrantMs))return CardAdmission::WrongContext;
    grant=false;cached=true;request=f;outcome=CardOutcome::InProgress;
    return CardAdmission::New;
  }
  constexpr bool finish(CardOutcome terminal) {
    if(!cached || outcome!=CardOutcome::InProgress || terminal==CardOutcome::InProgress
        || !outcomeForMode(terminal,request.cardMode))return false;
    outcome=terminal;return true;
  }
};

enum class CardPresence : uint8_t { Unknown, Absent, Present };
struct PresentationLatch {
  bool armed=false,timing=false;
  uint32_t absentAt=0;
  constexpr void reset() { armed=false;timing=false; }
  // Absence must be measured physically, including HALTed cards; an IO error is Unknown.
  constexpr bool observe(CardPresence presence,uint32_t now,bool contextUsable) {
    if(!contextUsable || presence==CardPresence::Unknown){reset();return false;}
    if(presence==CardPresence::Absent) {
      if(!timing){timing=true;absentAt=now;}
      if(elapsed(now,absentAt,kCardRemovalMs))armed=true;
      return false;
    }
    timing=false;
    const bool fresh=armed;armed=false;return fresh;
  }
};
} // namespace Domotica
