#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <time.h>
#include "board_pins.h"
#include "hub_config.h"
#include "display_driver.h"
#include "FT6336.h"
#include "geometry.h"
#include "network_service.h"
#include "ui.h"
#include "radio_service.h"
#include "notification_service.h"

FT6336 touch(Board::CTP_SDA,Board::CTP_SCL,Board::CTP_INT,Board::CTP_RST,240,320);
UiModel model;
SystemController controller;
TapTracker taps;
Domotica::CardUid selectedCard;
uint32_t lastTouch=0,lastStatus=0,lastTouchReport=0;

void addEvent(const char* message) {
  for(int i=min(int(model.eventCount),15);i>0;--i)model.events[i]=model.events[i-1];
  // Capture the hub's local clock at registration, never at render time.
  // Reports do not carry a remote timestamp in protocol v1.
  auto& entry=model.events[0];
  memcpy(entry.localTime,"--:--:--",sizeof(entry.localTime));
  const time_t epoch=time(nullptr);
  if(epoch>1704067200) {
    struct tm local{};
    if(localtime_r(&epoch,&local))strftime(entry.localTime,sizeof(entry.localTime),"%H:%M:%S",&local);
  }
  snprintf(model.events[0].text,sizeof(model.events[0].text),"%s",message);
  if(model.eventCount<16)++model.eventCount;
  if(model.page==Page::Events)uiInvalidate(0,28,320,164);
}
void dismissAlert() {
  model.alert.active=false;model.page=Page::Home;taps=TapTracker{};uiInterrupt();
}
void showNotice(Notice kind) {
  if(kind==Notice::Disarmed){if(model.alert.active)dismissAlert();return;}
  if(!visualNotice(kind))return;
  radioCancelEnrollment();model.page=Page::Home;model.alert.trigger(kind);
  taps=TapTracker{};uiInterrupt();
}
void dispatch(int action) {
  if(action>=NavHome && action<=NavSettings) {
    radioCancelEnrollment();
    model.page=static_cast<Page>(action);model.eventPage=0;uiInvalidate();return;
  }
  switch(action) {
    case ArmTile: {
      const auto& radio=radioStatus();
      const uint32_t now=millis();
      auto result=controller.request(!controller.armed(),radio.ready && radio.peers[0].enabled && radio.door.peer.online(now),
        radio.ready && radio.peers[1].enabled && radio.pir.peer.online(now));
      if(result==SystemController::Result::Changed) {
        notificationObserve(controller.armed(),radioOnlineMask(millis()));
        model.armed=controller.armed();
        addEvent(model.armed?"System armed":"System disarmed");
        uiInvalidate(10,36,145,148);
      } else if(result!=SystemController::Result::Unchanged) {
        addEvent("Cannot arm: sensors offline");
        model.page=Page::Events;model.eventPage=0;uiInvalidate();
      }
      break;
    }
    case OpenCards:radioCancelEnrollment();model.page=Page::Cards;model.cardPage=0;uiInvalidate();break;
    case AddCard:model.page=Page::AddCard;uiInvalidate();break;
    case StartScan:
      if(radioStartEnrollment(millis()))model.page=Page::ScanCard;
      else model.page=Page::Cards;
      uiInvalidate();break;
    case CancelScan:radioCancelEnrollment();model.page=Page::Cards;uiInvalidate();break;
    case CardsPrevious:if(model.cardPage)--model.cardPage;uiInvalidate();break;
    case CardsNext:if((model.cardPage+1)*2<model.cardCount)++model.cardPage;uiInvalidate();break;
    case CardRow0:case CardRow1: {
      int index=model.cardPage*2+action-CardRow0;
      const auto& store=radioCards().store;
      if(store.ready&&index<store.list.count) {
        selectedCard=store.list.cards[index];
        snprintf(model.selectedCard,sizeof(model.selectedCard),"%s",model.cardRows[action-CardRow0]);
        model.page=Page::CardDetail;uiInvalidate();
      }
      break;
    }
    case CardBack:model.page=model.page==Page::RemoveCard?Page::CardDetail:Page::Cards;uiInvalidate();break;
    case RemoveCard:model.page=Page::RemoveCard;uiInvalidate();break;
    case ConfirmRemove:
      if(radioRemoveCard(selectedCard)){addEvent("Card removed");notificationEvent(Notice::CardRemoved);}
      else addEvent("Card removal failed");
      model.page=Page::Cards;uiInvalidate();break;
    case OpenWhatsApp:model.page=Page::WhatsApp;uiInvalidate();break;
    case TestWhatsApp:notificationTest(model.notificationChannel);uiInvalidate();break;
    case NextNotificationChannel:
      model.notificationChannel=(model.notificationChannel+1)%3;
      model.notifications=notificationStatus(model.notificationChannel);uiInvalidate();break;
    case DismissAlert:dismissAlert();break;
    case OpenDiagnostics:model.page=Page::Diagnostics;uiInvalidate();break;
    case OpenTouchTest:model.page=Page::TouchTest;uiInvalidate();break;
    case GoBack:model.page=Page::Settings;uiInvalidate();break;
    case Previous:
      if(model.eventPage>0){--model.eventPage;uiInvalidate(0,28,320,164);}break;
    case Next:
      if((model.eventPage+1)*3<model.eventCount){++model.eventPage;uiInvalidate(0,28,320,164);}break;
  }
}
void pollTouch(uint32_t now) {
  if(uint32_t(now-lastTouch)<(model.touchOk?20UL:200UL))return;
  lastTouch=now;
  touch.read();
  if(model.touchOk!=touch.readValid) Serial.printf("Touch status: %s\n",touch.readValid?"OK":"ERROR");
  model.touchOk=touch.readValid;model.touchErrors=touch.errors;
  int hit=NoAction;
  if(touch.readValid && touch.touches==1) {
    auto p=nativeToLogical(touch.points[0].x,touch.points[0].y,Config::LANDSCAPE_FLIP);
    model.touchX=p.x;model.touchY=p.y;hit=uiHit(model,p.x,p.y);
    if(model.page==Page::TouchTest && uint32_t(now-lastTouchReport)>=200) {
      lastTouchReport=now;
      Serial.printf("Touch raw=(%u,%u) UI=(%d,%d)\n",touch.points[0].x,touch.points[0].y,p.x,p.y);
      if(hit==NoAction && !uiBusy())uiInvalidate(0,28,320,164);
    }
  }
  const int action=taps.update(touch.readValid,touch.touches,hit,!uiBusy());
  if(action!=NoAction)dispatch(action);
}
void updateStatus(uint32_t now) {
  const auto& radio=radioStatus();
  bool doorOnline=radio.ready && radio.peers[0].enabled && radio.door.peer.online(now);
  uint8_t doorState=doorOnline?uint8_t(radio.door.current(now)):0;
  if(model.doorOnline && !doorOnline)addEvent("Door offline");
  if(model.doorOnline!=doorOnline || model.doorState!=doorState || model.doorSeen!=radio.door.peer.seen) {
    model.doorOnline=doorOnline;model.doorState=doorState;model.doorSeen=radio.door.peer.seen;
    if(model.page==Page::Home)uiInvalidate(165,123,145,61);
    if(model.page==Page::Nodes)uiInvalidate(0,28,320,164);
  }
  bool pirOnline=radio.ready && radio.peers[1].enabled && radio.pir.peer.online(now);
  uint8_t pirState=pirOnline?uint8_t(radio.pir.current(now)):0;
  if(model.pirOnline && !pirOnline)addEvent("PIR offline");
  if(model.pirOnline!=pirOnline || model.pirState!=pirState || model.pirSeen!=radio.pir.peer.seen) {
    model.pirOnline=pirOnline;model.pirState=pirState;model.pirSeen=radio.pir.peer.seen;
    if(model.page==Page::Home)uiInvalidate(165,123,145,61);
    if(model.page==Page::Nodes)uiInvalidate(0,28,320,164);
  }
  const auto& cards=radioCards();
  bool rfidOnline=radio.ready&&radio.peers[2].enabled&&cards.heartbeat.online(now);
  if(model.rfidOnline&&!rfidOnline)addEvent("RFID offline");
  if(model.rfidOnline!=rfidOnline) {
    model.rfidOnline=rfidOnline;
    if(model.page==Page::Home)uiInvalidate(165,166,145,18);
    if(model.page==Page::Nodes)uiInvalidate(0,28,320,164);
  }
  model.rfidMessage=radio.peers[2].message;
  if(model.armed!=controller.armed()) {
    model.armed=controller.armed();
    if(model.page==Page::Home)uiInvalidate(10,36,145,148);
    if(model.page==Page::Nodes)uiInvalidate(0,28,320,164);
  }
  model.cardStorageReady=cards.store.ready;model.cardCount=cards.store.ready?cards.store.list.count:0;
  if(model.cardPage*2>=model.cardCount)model.cardPage=model.cardCount?uint8_t((model.cardCount-1)/2):0;
  for(int row=0;row<2;++row) {
    model.cardRows[row][0]=0;int index=model.cardPage*2+row;
    if(index<model.cardCount) {
      const auto& uid=cards.store.list.cards[index];
      for(int byte=0;byte<uid.length;++byte)snprintf(model.cardRows[row]+byte*2,3,"%02X",uid.bytes[byte]);
    }
  }
  static uint32_t lastCardNotice=UINT32_MAX,lastCardGeneration=UINT32_MAX;
  if(lastCardNotice!=cards.noticeRevision||lastCardGeneration!=cards.store.generation) {
    lastCardNotice=cards.noticeRevision;lastCardGeneration=cards.store.generation;
    snprintf(model.cardNotice,sizeof(model.cardNotice),"%s",cards.notice);
    if(model.page>=Page::Cards&&model.page<=Page::RemoveCard)uiInvalidate(0,28,320,164);
  }
  if(model.page==Page::ScanCard) {
    if(!cards.enrolling){model.page=Page::Cards;uiInvalidate();}
    else {
      uint32_t spent=uint32_t(now-cards.enrollmentAt);
      uint8_t seconds=spent>=Domotica::kEnrollmentMs?0:uint8_t((Domotica::kEnrollmentMs-spent+999)/1000);
      if(seconds!=model.scanSeconds){model.scanSeconds=seconds;uiInvalidate(0,100,320,30);}
    }
  }
  model.doorMessage=radio.peers[0].message;model.pirMessage=radio.peers[1].message;
  model.radioReady=radio.ready;model.radioMessage=radio.message;
  model.radioRx=radio.accepted;model.radioRejected=radio.rejected;
  model.radioDropped=radio.dropped;model.radioTxErrors=radio.sendErrors;
  Weather weather;
  if(networkReadWeather(weather)) {
    model.weather=weather;
    if(model.page==Page::Home)uiInvalidate(165,36,145,82);
  }
  if(uint32_t(now-lastStatus)<1000)return;
  lastStatus=now;
  bool online=WiFi.status()==WL_CONNECTED;
  if(online!=model.wifi) {
    model.wifi=online;addEvent(online?"Wi-Fi connected":"Wi-Fi disconnected");
    if(model.page==Page::Home)uiInvalidate(165,166,145,18);
    if(model.page==Page::Events)uiInvalidate(0,28,320,164);
  }
  model.uptime=now/1000;model.heap=ESP.getFreeHeap();model.channel=networkChannel();
  time_t epoch=time(nullptr);char clock[6]="--:--";
  if(epoch>1704067200){struct tm local{};localtime_r(&epoch,&local);strftime(clock,sizeof(clock),"%H:%M",&local);}
  if(strcmp(clock,model.clock)!=0){memcpy(model.clock,clock,sizeof(clock));uiInvalidate(264,0,56,27);}
  bool stale=model.weather.valid && (uint32_t(now-model.weather.fetchedAt)>Config::WEATHER_STALE_MS
      || (epoch>1704067200 && epoch-model.weather.observedAt>3600));
  if(stale!=model.weatherStale){model.weatherStale=stale;if(model.page==Page::Home)uiInvalidate(165,36,145,82);}
  if(model.weather.valid)snprintf(model.weatherAge,sizeof(model.weatherAge),"%lum ago%s",
      (unsigned long)(uint32_t(now-model.weather.fetchedAt)/60000),stale?" / stale":"");
  model.notifications=notificationStatus(model.notificationChannel);
  if(model.page==Page::WhatsApp && !uiBusy())uiInvalidate(0,28,320,112);
  if(model.page==Page::Diagnostics && !uiBusy())uiInvalidate(0,28,320,112);
}
void setup() {
  Serial.begin(115200);
  model.demo=false;model.armed=controller.armed();
  Serial.printf("%s | LCD SPI 1 MHz | UI demo %s\n",Config::VERSION,model.demo?"ON":"OFF");
  displayBegin();
  touch.begin();Wire.setClock(100000);touch.setRotation(ROTATION_NORMAL);
  touch.read();model.touchOk=touch.readValid;
  uint8_t mac[6];
  if(esp_read_mac(mac,ESP_MAC_WIFI_STA)==ESP_OK)
    snprintf(model.mac,sizeof(model.mac),"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
  notificationSetVisualCallback(showNotice);
  model.networkTaskOk=networkBegin();
  radioBegin();
  Serial.printf("Touch I2C: %s | MAC %s\n",model.touchOk?"valid read":"ERROR",model.mac);
  Serial.printf("Touch diagnostics: I2C error=%u, bytes=%u\n",touch.lastI2cError,touch.lastReadCount);
  Wire.beginTransmission(FT6336_ADDR);
  uint8_t probe=Wire.endTransmission();
  Serial.printf("Touch probe 0x38: %u | SDA=%d SCL=%d\n",probe,digitalRead(Board::CTP_SDA),digitalRead(Board::CTP_SCL));
  Serial.printf("Landscape: 320x240, flip=%s | renderer queued\n",Config::LANDSCAPE_FLIP?"ON":"OFF");
  addEvent("System started disarmed");
  uiInvalidate();
}
void loop() {
  static bool firstFrameReported=false;
  uint32_t now=millis();
  networkTick(now);radioTick(now,addEvent,controller);
  pollTouch(now);updateStatus(millis());uiTick(model);
  if(model.alert.tick(millis(),!uiBusy(),controller.armed()))dismissAlert();
  if(!firstFrameReported && !uiBusy()) {
    firstFrameReported=true;
    Serial.printf("Initial UI transfer complete | free heap %lu B\n",(unsigned long)ESP.getFreeHeap());
  }
  delay(1); // Yield to network/RTOS; no full-screen blocking transfer.
}
