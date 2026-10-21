#pragma once
#include <DomoticaProtocol.h>

// Pending application acknowledgements remain independent across recipients.
struct PeerSync {
  bool pending=false;
  Domotica::Stamp reference{};
  uint32_t lastSync=0,publishedRevision=0;
  constexpr void reply(Domotica::Stamp stamp) {reference=stamp;pending=true;}
  constexpr void refresh(const Domotica::PeerTracker& peer,uint32_t now,uint32_t revision) {
    if(peer.seen && (Domotica::elapsed(now,lastSync,Domotica::kHeartbeatMs)||publishedRevision!=revision))
      reply(peer.last);
  }
};
struct SyncScheduler {
  PeerSync peers[3];
  uint8_t next=0;
  constexpr int take(bool doorEnabled,bool pirEnabled,bool rfidEnabled=false,bool rfidWork=false) {
    const bool enabled[3]={doorEnabled,pirEnabled,rfidEnabled};
    for(int n=0;n<3;++n) {
      int i=(next+n)%3;
      if(enabled[i]&&(peers[i].pending||(i==2&&rfidWork))){next=uint8_t((i+1)%3);return i;}
    }
    return -1;
  }
};
