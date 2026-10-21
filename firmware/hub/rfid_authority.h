#pragma once
#include <RfidContract.h>
#include "system_controller.h"

// Called only by the hub main loop; storage and radio use the same serial owner.
template<class Store> class RfidAuthority {
public:
  Store store;
  Domotica::CardGate gate;
  Domotica::PeerTracker heartbeat;
  bool enrolling=false;
  uint32_t enrollmentAt=0,noticeRevision=0;
  const char* notice="Ready";
  constexpr void note(const char* text){notice=text;++noticeRevision;}
  constexpr void cancel(const char* why="Cancelled") {
    gate.revoke();
    if(enrolling){enrolling=false;note(why);}
  }
  constexpr bool start(uint32_t now,bool online) {
    gate.revoke();
    if(!store.ready){note("Storage error");return false;}
    if(!online){note("Reader offline");return false;}
    enrolling=true;enrollmentAt=now;note("Scan card");return true;
  }
  constexpr void tick(uint32_t now,bool online) {
    if(!online)cancel("Reader offline");
    else if(enrolling&&Domotica::elapsed(now,enrollmentAt,Domotica::kEnrollmentMs))cancel("Scan timed out");
  }
  constexpr Domotica::CardMode mode() const {
    return !store.ready?Domotica::CardMode::Disabled:
      enrolling?Domotica::CardMode::Enroll:Domotica::CardMode::Normal;
  }
  constexpr bool remove(const Domotica::CardUid& uid) {
    cancel();gate.revoke();
    if(!store.ready){note("Storage error");return false;}
    auto candidate=store.list;
    if(!candidate.remove(uid)){note("Card not found");return false;}
    bool ok=store.save(candidate);note(ok?"Card removed":"Storage error");return ok;
  }
  constexpr Domotica::CardOutcome execute(const Domotica::Frame& request,SystemController& controller,
                                bool doorOnline,bool pirOnline) {
    using namespace Domotica;
    CardOutcome result=CardOutcome::StorageError;
    if(store.ready) {
      if(request.cardMode==CardMode::Enroll) {
        if(store.list.find(request.uid)>=0)result=CardOutcome::AlreadyExists;
        else if(store.list.count>=kMaxAccessCards)result=CardOutcome::Full;
        else {
          auto candidate=store.list;
          result=candidate.add(request.uid)&&store.save(candidate)?CardOutcome::Added:CardOutcome::StorageError;
        }
      } else {
        result=decideNormalCard(controller.state(),store.list.find(request.uid)>=0,doorOnline,pirOnline);
        if(result==CardOutcome::Armed||result==CardOutcome::Disarmed) {
          auto applied=controller.request(result==CardOutcome::Armed,doorOnline,pirOnline);
          if(applied!=SystemController::Result::Changed)result=CardOutcome::Unavailable;
        }
      }
    }
    // Caller reserves the request via gate.admit BEFORE reaching this effect.
    gate.finish(result);
    if(request.cardMode==CardMode::Enroll){enrolling=false;gate.revoke();}
    note(label(result));return result;
  }
  static constexpr const char* label(Domotica::CardOutcome result) {
    using O=Domotica::CardOutcome;
    switch(result) {
      case O::Unauthorized:return "Card not authorized";
      case O::Armed:return "System armed";
      case O::Disarmed:return "System disarmed";
      case O::SensorsOffline:return "Cannot arm: sensors offline";
      case O::Added:return "Card added";
      case O::AlreadyExists:return "Card already exists";
      case O::Full:return "Card list full";
      case O::StorageError:return "Storage error";
      case O::Expired:return "Card request expired";
      default:return "Card request rejected";
    }
  }
};
