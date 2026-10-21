#include <Arduino.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <atomic>
#include "radio_service.h"
#include "network_service.h"
#include "sync_scheduler.h"
#include "notification_service.h"
#if __has_include("radio_secrets.h")
#include "radio_secrets.h"
#endif
#ifndef HUB_DOOR_MAC_HEX
#define HUB_DOOR_MAC_HEX ""
#endif
#ifndef HUB_DOOR_LMK_HEX
#define HUB_DOOR_LMK_HEX ""
#endif
#ifndef HUB_ESPNOW_PMK_HEX
#define HUB_ESPNOW_PMK_HEX ""
#endif
#ifndef HUB_PIR_MAC_HEX
#define HUB_PIR_MAC_HEX ""
#endif
#ifndef HUB_PIR_LMK_HEX
#define HUB_PIR_LMK_HEX ""
#endif

#ifndef HUB_RFID_MAC_HEX
#define HUB_RFID_MAC_HEX ""
#endif
#ifndef HUB_RFID_LMK_HEX
#define HUB_RFID_LMK_HEX ""
#endif

namespace {
RadioStatus state;
uint8_t peerMac[3][6],hubMac[6];
constexpr Domotica::Node ids[]={Domotica::Node::Door,Domotica::Node::Pir,Domotica::Node::Rfid};
const char* names[]={"Door","PIR","RFID"};
struct Packet {uint8_t source[6],destination[6],data[Domotica::kMaxFrameSize];uint8_t size;uint32_t at;};
struct TxResult {uint8_t destination[6];esp_now_send_status_t result;};
QueueHandle_t rx=nullptr,txDone=nullptr;
std::atomic<uint32_t> overflow{0},invalid{0};
SyncScheduler syncs;
RfidAuthority<AccessStore> cards;
bool contextPending=false;
Domotica::Stamp contextReference{},syncSentReference{};
Domotica::Frame cardResults[8];
uint8_t resultHead=0,resultCount=0;
void queueResult(const Domotica::Frame& request,Domotica::CardOutcome outcome) {
  auto& result=cardResults[(resultHead+resultCount)%8];result={};
  result.type=Domotica::Type::CardResult;result.acknowledged=request.stamp;
  result.contextId=request.contextId;result.cardMode=request.cardMode;result.outcome=outcome;
  ++resultCount;
}
int inFlight=-1;
uint32_t sentAt=0,lastDiagnostics=0;
int nibble(char c) {
  if(c>='0'&&c<='9')return c-'0';
  if(c>='a'&&c<='f')return c-'a'+10;
  if(c>='A'&&c<='F')return c-'A'+10;
  return -1;
}
bool parse(const char* text,uint8_t* bytes,size_t count) {
  if(strlen(text)!=count*2)return false;
  uint8_t any=0;
  for(size_t i=0;i<count;++i) {
    int a=nibble(text[i*2]),b=nibble(text[i*2+1]);
    if(a<0||b<0)return false;
    bytes[i]=uint8_t(a*16+b);any|=bytes[i];
  }
  return any!=0;
}

void received(const esp_now_recv_info_t* info,const uint8_t* data,int length) {
  if(!info||!data||length<16||length>int(Domotica::kMaxFrameSize)){++invalid;return;}
  Packet p{};
  memcpy(p.source,info->src_addr,6);memcpy(p.destination,info->des_addr,6);
  memcpy(p.data,data,length);p.size=length;p.at=millis();
  if(xQueueSend(rx,&p,0)!=pdTRUE)++overflow;
}
void sent(const esp_now_send_info_t* info,esp_now_send_status_t result) {
  if(!info||!info->des_addr){++invalid;return;}
  TxResult item{};memcpy(item.destination,info->des_addr,6);item.result=result;
  if(xQueueSend(txDone,&item,0)!=pdTRUE)++overflow;
}
bool fail(const char* why){state.ready=false;state.message=why;Serial.printf("ESP-NOW: %s\n",why);return false;}
const Domotica::PeerTracker& tracker(int i){return i==0?state.door.peer:i==1?state.pir.peer:cards.heartbeat;}
const char* pirLabel(Domotica::PirState s) {
  switch(s) {
    case Domotica::PirState::Motion:return "Motion detected";
    case Domotica::PirState::NoMotion:return "No motion";
    case Domotica::PirState::NotReady:return "Warming up";
    default:return "PIR unknown";
  }
}
}
bool radioBegin() {
  if(!cards.store.begin())cards.note("Storage error");
  if(!networkRadioStable())return fail("Wi-Fi radio unavailable");
  uint8_t pmk[16],lmk[3][16];
  if(!parse(HUB_ESPNOW_PMK_HEX,pmk,16))return fail("Invalid PMK");
  if(esp_wifi_get_mac(WIFI_IF_STA,hubMac)!=ESP_OK)return fail("Invalid STA identity");
  const char* macText[]={HUB_DOOR_MAC_HEX,HUB_PIR_MAC_HEX,HUB_RFID_MAC_HEX};
  const char* keyText[]={HUB_DOOR_LMK_HEX,HUB_PIR_LMK_HEX,HUB_RFID_LMK_HEX};
  bool configured[3]={false,false,false};
  for(int i=0;i<3;++i) {
    configured[i]=parse(macText[i],peerMac[i],6) && !(peerMac[i][0]&1)
      && memcmp(peerMac[i],hubMac,6) && parse(keyText[i],lmk[i],16);
    if(!configured[i])state.peers[i].message="Not configured";
  }
  for(int i=1;i<3;++i)for(int j=0;j<i;++j) {
    if(configured[i] && !memcmp(peerMac[j],peerMac[i],6)) {
      configured[i]=false;state.peers[i].message="Duplicate MAC";
    }
  }
  nvs_handle_t prefs;
  if(nvs_open("hub-radio",NVS_READWRITE,&prefs)!=ESP_OK)return fail("Session storage failed");
  uint32_t previous=0;
  esp_err_t read=nvs_get_u32(prefs,"boot",&previous);
  if(read!=ESP_OK && read!=ESP_ERR_NVS_NOT_FOUND){nvs_close(prefs);return fail("Session read failed");}
  state.session=Domotica::nextCounter(previous);
  bool saved=nvs_set_u32(prefs,"boot",state.session)==ESP_OK && nvs_commit(prefs)==ESP_OK;
  uint32_t verified=0;
  saved=saved && nvs_get_u32(prefs,"boot",&verified)==ESP_OK && verified==state.session;
  nvs_close(prefs);
  if(!saved)return fail("Session storage failed");

  rx=xQueueCreate(Domotica::kRxQueueDepth,sizeof(Packet));
  txDone=xQueueCreate(1,sizeof(TxResult));
  if(!rx||!txDone)return fail("Queue allocation failed");
  if(esp_now_init()!=ESP_OK)return fail("Radio initialization failed");
  if(esp_now_set_pmk(pmk)!=ESP_OK || esp_now_register_recv_cb(received)!=ESP_OK
      || esp_now_register_send_cb(sent)!=ESP_OK) {
    esp_now_deinit();return fail("Radio configuration failed");
  }
  for(int i=0;i<3;++i) {
    if(configured[i]) {
      esp_now_peer_info_t peer{};
      memcpy(peer.peer_addr,peerMac[i],6);memcpy(peer.lmk,lmk[i],16);
      peer.ifidx=WIFI_IF_STA;peer.channel=0;peer.encrypt=true;
      state.peers[i].enabled=esp_now_add_peer(&peer)==ESP_OK;
      state.peers[i].message=state.peers[i].enabled?"Waiting":"Peer setup failed";
    }
    Serial.printf("ESP-NOW %s: %s\n",names[i],state.peers[i].message);
  }
  state.ready=true;state.message="Encrypted / ready";
  return true;
}
uint8_t radioOnlineMask(uint32_t now) {
  uint8_t mask=0;
  if(state.ready)for(int i=0;i<3;++i)if(state.peers[i].enabled&&tracker(i).online(now))mask|=uint8_t(1<<i);
  return mask;
}
const RadioStatus& radioStatus(){return state;}
const RfidAuthority<AccessStore>& radioCards(){return cards;}
bool radioStartEnrollment(uint32_t now) {
  contextPending=false;
  return cards.start(now,state.ready&&state.peers[2].enabled&&cards.heartbeat.online(now));
}
void radioCancelEnrollment(){cards.cancel();contextPending=false;}
bool radioRemoveCard(const Domotica::CardUid& uid){contextPending=false;return cards.remove(uid);}

void radioTick(uint32_t now,RadioEvent event,SystemController& controller) {
  notificationObserve(controller.armed(),radioOnlineMask(now));
  cards.tick(now,state.ready&&state.peers[2].enabled&&cards.heartbeat.online(now));
  if(!state.ready){contextPending=false;return;}
  state.dropped+=overflow.exchange(0);state.rejected+=invalid.exchange(0);
  TxResult result;
  if(xQueueReceive(txDone,&result,0)==pdTRUE) {
    if(inFlight<0 || memcmp(result.destination,peerMac[inFlight],6)) {
      ++state.rejected;fail("Unexpected TX callback");event("Radio transport fault");return;
    }
    auto& stats=state.peers[inFlight];
    if(result.result==ESP_NOW_SEND_SUCCESS){++state.sent;++stats.sent;}
    else {++state.sendErrors;++stats.sendErrors;}
    inFlight=-1;
  }
  if(inFlight>=0 && Domotica::elapsed(now,sentAt,2000)) {
    ++state.sendErrors;++state.peers[inFlight].sendErrors;
    fail("Send callback timeout");event("Radio transport fault");return;
  }
  Packet p;
  for(size_t count=0;count<Domotica::kRxQueueDepth && xQueueReceive(rx,&p,0)==pdTRUE;++count) {
    now=millis();
    int i=-1;
    for(int n=0;n<3;++n)if(state.peers[n].enabled && !memcmp(p.source,peerMac[n],6))i=n;
    if(i<0){++state.rejected;continue;}
    auto& stats=state.peers[i];
    Domotica::Frame frame;
    if(memcmp(p.destination,hubMac,6)||Domotica::elapsed(now,p.at,Domotica::kOfflineMs)
        ||Domotica::decode(p.data,p.size,frame)!=Domotica::Error::Ok
        ||frame.source!=ids[i]||frame.destination!=Domotica::Node::Hub) {
      ++state.rejected;++stats.rejected;continue;
    }
    if(i==2) {
      using namespace Domotica;
      bool online=cards.heartbeat.online(now);
      cards.tick(now,online); // Enrollment expiry is checked at processing time too.
      // Card duplicates must reach the cache before generic heartbeat order checks.
      if(frame.type==Type::CardRequest) {
        if(resultCount>=8){++state.dropped;continue;}
        auto admission=cards.gate.admit(frame,now,online);
        if(admission==CardAdmission::Duplicate) {
          ++state.duplicates;++stats.duplicates;queueResult(frame,cards.gate.outcome);
        } else if(admission==CardAdmission::New) {
          ++state.accepted;++stats.accepted;
          auto outcome=cards.execute(frame,controller,
            state.peers[0].enabled&&state.door.peer.online(now),
            state.peers[1].enabled&&state.pir.peer.online(now));
          notificationObserve(controller.armed(),radioOnlineMask(millis()));
          if(outcome==CardOutcome::Added)notificationEvent(Notice::CardAdded);
          contextPending=false;
          queueResult(frame,outcome);event(cards.label(outcome));
        } else if(admission==CardAdmission::WrongContext && frame.targetHubBoot==state.session
            && frame.stamp.boot==cards.gate.readerBoot) {
          ++state.rejected;++stats.rejected;
          bool expired=frame.contextId==cards.gate.token&&frame.cardMode==cards.gate.mode
            && elapsed(now,cards.gate.issuedAt,kCardGrantMs);
          queueResult(frame,expired?CardOutcome::Expired:CardOutcome::WrongContext);
        } else {++state.rejected;++stats.rejected;}
        continue;
      }
      if(!isRfidHeartbeat(frame.type)){++state.rejected;++stats.rejected;continue;}
      auto order=cards.heartbeat.accept(frame.stamp,p.at);
      if(order==Order::Stale||order==Order::Invalid){++state.rejected;++stats.rejected;continue;}
      syncs.peers[2].reply(frame.stamp);
      if(order==Order::Duplicate){++state.duplicates;++stats.duplicates;continue;}
      ++state.accepted;++stats.accepted;
      if(order==Order::First||order==Order::NewSession) {
        cards.cancel("Reader restarted");cards.gate.begin(state.session,frame.stamp.boot);
        resultHead=0;resultCount=0;syncSentReference={};
        event(order==Order::NewSession?"RFID restarted":"RFID connected");
      } else if(!online)event("RFID connected");
      contextReference=frame.stamp;contextPending=true;
      continue;
    }
    bool online=tracker(i).online(now);
    auto oldDoor=state.door.lastState;auto oldPir=state.pir.lastState;
    auto order=i==0?state.door.admit(frame,p.at):state.pir.admit(frame,p.at);
    if(order==Domotica::Order::Invalid||order==Domotica::Order::Stale) {
      ++state.rejected;++stats.rejected;continue;
    }
    syncs.peers[i].reply(frame.stamp);
    if(order==Domotica::Order::Duplicate){++state.duplicates;++stats.duplicates;continue;}
    ++state.accepted;++stats.accepted;
    if(order==Domotica::Order::NewSession)event(i==0?"Door restarted":"PIR restarted");
    else if(!online)event(i==0?"Door connected":"PIR connected");
    if(controller.armed() && order!=Domotica::Order::NewSession) {
      if(i==0 && (oldDoor!=frame.door||!online))
        event(frame.door==Domotica::DoorState::Open?"Door open":frame.door==Domotica::DoorState::Closed?"Door closed":"Door unknown");
      if(i==1 && (oldPir!=frame.pir||!online))event(pirLabel(frame.pir));
    }
    if(order!=Domotica::Order::NewSession) {
      if(i==0 && oldDoor!=frame.door) {
        if(frame.door==Domotica::DoorState::Open)notificationEvent(Notice::DoorOpen);
        if(frame.door==Domotica::DoorState::Closed)notificationEvent(Notice::DoorClosed);
      }
      if(i==1 && oldPir!=frame.pir && frame.pir==Domotica::PirState::Motion)notificationEvent(Notice::Motion);
    }
    Serial.printf("%s RX boot=%lu seq=%lu state=%u\n",names[i],(unsigned long)frame.stamp.boot,
      (unsigned long)frame.stamp.sequence,i==0?unsigned(frame.door):unsigned(frame.pir));
  }
  notificationObserve(controller.armed(),radioOnlineMask(millis()));
  if(!cards.heartbeat.online(now))contextPending=false;
  for(int i=0;i<3;++i)if(state.peers[i].enabled) {
    auto& peer=tracker(i);
    state.peers[i].message=peer.online(now)?"Online":peer.seen?"Offline":"Waiting";
    syncs.peers[i].refresh(peer,now,controller.revision());
  }
  if(Domotica::elapsed(now,lastDiagnostics,10000)) {
    lastDiagnostics=now;
    for(int i=0;i<3;++i) {
      const auto& stats=state.peers[i];
      Serial.printf("%s CH=%u %s RX=%lu reject=%lu TX=%lu fail=%lu\n",names[i],networkChannel(),stats.message,
        (unsigned long)stats.accepted,(unsigned long)stats.rejected,(unsigned long)stats.sent,(unsigned long)stats.sendErrors);
    }
  }
  if(inFlight>=0 || !networkRadioStable())return;
  const bool contextReady=contextPending && cards.heartbeat.online(now)
    && Domotica::same(contextReference,syncSentReference);
  const int i=syncs.take(state.peers[0].enabled,state.peers[1].enabled,state.peers[2].enabled,
    resultCount||contextReady);
  if(i<0)return;
  auto& sync=syncs.peers[i];
  if(state.sequence==UINT32_MAX) {
    cards.gate.revoke();ESP.restart();return; // Never reuse a grant token in a hub session.
  }
  Domotica::Frame frame;
  if(i==2 && resultCount)frame=cardResults[resultHead];
  else if(i==2 && !sync.pending && contextReady) {
    frame.type=Domotica::Type::CardContext;frame.acknowledged=contextReference;
    frame.contextId=state.sequence+1;frame.cardMode=cards.mode();
    if(!cards.gate.issue(frame.contextId,frame.cardMode,now)){contextPending=false;return;}
  } else {frame.type=Domotica::Type::StateSync;frame.acknowledged=sync.reference;}
  frame.source=Domotica::Node::Hub;frame.destination=ids[i];
  frame.stamp={state.session,state.sequence+1};frame.channel=networkChannel();
  frame.system=controller.state();frame.revision=controller.revision();
  uint8_t bytes[Domotica::kMaxFrameSize];size_t size=0;
  if(Domotica::encode(frame,bytes,sizeof(bytes),size)!=Domotica::Error::Ok)return;
  state.sequence=frame.stamp.sequence;
  if(frame.type==Domotica::Type::StateSync) {
    sync.pending=false;sync.lastSync=now;sync.publishedRevision=frame.revision;
  } else if(frame.type==Domotica::Type::CardResult) {
    resultHead=(resultHead+1)%8;--resultCount;
  } else contextPending=false;
  inFlight=i;sentAt=now;
  if(esp_now_send(peerMac[i],bytes,size)!=ESP_OK) {
    inFlight=-1;++state.sendErrors;++state.peers[i].sendErrors;
    if(frame.type==Domotica::Type::CardContext)cards.gate.revoke();
  } else {
    if(i==2&&frame.type==Domotica::Type::StateSync)syncSentReference=frame.acknowledged;
    Serial.printf("Hub TX to=%s type=%u seq=%lu state=%u revision=%lu\n",names[i],unsigned(frame.type),
      (unsigned long)state.sequence,unsigned(frame.system),(unsigned long)frame.revision);
  }
}
