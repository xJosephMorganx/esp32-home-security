#pragma once
#include <stdint.h>
#include "notification_service.h"
#include "visual_alert.h"
enum class Page : uint8_t { Home, Events, Nodes, Settings, Diagnostics, TouchTest, Cards, AddCard, ScanCard, CardDetail, RemoveCard, WhatsApp };
struct Weather {
  bool valid=false, day=true;
  float temperature=0;
  int code=-1, httpStatus=0;
  uint32_t fetchedAt=0;
  int64_t observedAt=0;
};
struct LogEntry { char localTime[9]="--:--:--"; char text[32]={}; };
struct UiModel {
  Page page=Page::Home;
  NotificationStatus notifications;
  uint8_t notificationChannel=0;
  VisualAlert alert;
  bool demo=false, armed=false, wifi=false, touchOk=false, weatherStale=false;
  bool networkTaskOk=false;
  bool radioReady=false,doorSeen=false,doorOnline=false;
  uint8_t doorState=0,pirState=0;
  bool pirSeen=false,pirOnline=false,rfidOnline=false;
  const char* rfidMessage="Not configured";
  bool cardStorageReady=false;
  uint8_t cardCount=0,cardPage=0,scanSeconds=0;
  char cardRows[2][21]={},selectedCard[21]={},cardNotice[32]="Ready";
  const char* doorMessage="Not configured";
  const char* pirMessage="Not configured";
  uint32_t radioRx=0,radioRejected=0,radioDropped=0,radioTxErrors=0;
  const char* radioMessage="Not configured";
  uint32_t touchErrors=0, uptime=0, heap=0;
  int touchX=-1,touchY=-1,eventPage=0;
  char clock[6]="--:--", mac[18]="--", weatherAge[24]="Never";
  Weather weather;
  LogEntry events[16];
  uint8_t eventCount=0;
  int channel=0;
};
const char* weatherLabel(int code,bool day);
