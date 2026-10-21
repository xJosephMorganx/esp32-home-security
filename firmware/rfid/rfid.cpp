#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <nvs.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "board_pins.h"
#include "rfid_link.h"
#if __has_include("config.local.h")
#include "config.local.h"
#else
#include "config.example.h"
#endif

namespace {
using namespace Domotica;
constexpr uint32_t kSendCallbackTimeoutMs=1000;
constexpr size_t kChannelCount=sizeof(RfidConfig::kChannels);
static_assert(kChannelCount>0 && kChannelCount<=14,"Provide legal channels");
static_assert(sizeof(RfidConfig::kHubStaMac)==6 && sizeof(RfidConfig::kRfidStaMac)==6
    && sizeof(RfidConfig::kPmk)==16 && sizeof(RfidConfig::kLmk)==16,"Provisioning sizes");
static_assert(RfidConfig::kSampleMs>0 && RfidConfig::kSampleMs<=100,"Presence polling period");
struct RxItem {
  uint8_t source[6],destination[6],bytes[kMaxFrameSize];
  uint8_t length,channel;
  uint32_t at;
};
struct TxItem {uint8_t destination[6];esp_now_send_status_t status;};
QueueHandle_t rxQueue=nullptr,txQueue=nullptr;
StaticQueue_t rxQueueControl,txQueueControl;
uint8_t rxStorage[kRxQueueDepth*sizeof(RxItem)],txStorage[sizeof(TxItem)];
portMUX_TYPE countersMux=portMUX_INITIALIZER_UNLOCKED;
uint32_t rxDrops=0,callbackRejects=0,txDrops=0;
nvs_handle_t nvsHandle=0;
Rfid::Link link;
MFRC522 reader(RfidPins::ss,RfidPins::rst);
uint8_t staMac[6]{};
uint32_t boot=0,sequence=0,lastHeartbeat=0,dwellStarted=0,sendStarted=0;
uint32_t lastDiagnostic=0,lastSample=0,rejected=0,sendErrors=0,radioFailures=0;
uint32_t unknownResults=0,readerErrors=0;
uint8_t channel=0,savedChannel=0;
size_t channelIndex=0;
bool transport=false,radioInitialized=false,inFlight=false,searching=true,needHello=true;
bool readerReady=false;
RfidLeds leds{};

void writeLed(uint8_t pin,RfidConfig::Level polarity,bool on) {
  if(polarity==RfidConfig::Level::Unspecified)return;
  digitalWrite(pin,on==(polarity==RfidConfig::Level::High)?HIGH:LOW);
}
void updateLeds(uint32_t now) {
  leds=rfidLeds(link.system,link.synchronized,transport && !searching && link.online(now));
  writeLed(RfidPins::red,RfidConfig::kRedActiveLevel,leds.red);
  writeLed(RfidPins::green,RfidConfig::kGreenActiveLevel,leds.green);
}
void abandonCard(const char* reason) {
  if(link.card.active) {
    ++unknownResults;
    Serial.printf("CARD result unknown seq=%lu: %s; no replay\n",
                  (unsigned long)link.card.frame.stamp.sequence,reason);
  }
  link.revoke();
}
void fault(const char* reason) {
  abandonCard(reason);link.disconnect();transport=false;updateLeds(millis());
  Serial.printf("TRANSPORT DISABLED: %s; reset after correction\n",reason);
}

// Adapted transport primitives from the existing PIR implementation; no shared edits.
#include "radio_functions.inc"

bool nextSequence() {
  // A new persisted session is required before any sequence can be reused.
  if(sequence==UINT32_MAX){fault("sequence exhausted");return false;}
  ++sequence;return true;
}
void startHeartbeat(Type type,uint32_t now) {
  if(!nextSequence())return;
  Frame f{};f.type=type;f.source=Node::Rfid;f.destination=Node::Hub;f.stamp={boot,sequence};
  if(!link.heartbeat.start(f,now)){fault("heartbeat encoding");return;}
  lastHeartbeat=now;
}
void receive() {
  RxItem item{};
  for(size_t i=0;i<kRxQueueDepth && xQueueReceive(rxQueue,&item,0)==pdTRUE;++i) {
    const uint32_t now=millis();
    Frame f{};
    const bool cardWasActive=link.card.active;
    if(memcmp(item.source,RfidConfig::kHubStaMac,6)!=0 || memcmp(item.destination,staMac,6)!=0
        || decode(item.bytes,item.length,f)!=Error::Ok
        || !link.accept(f,channel,item.channel,item.at,now)){++rejected;continue;}
    if(cardWasActive && !link.card.active && f.type!=Type::CardResult) {
      ++unknownResults;Serial.println("CARD result unknown: session/mode changed; no replay");
    }
    if(f.type==Type::CardResult)
      Serial.printf("CARD RESULT request=%lu/%lu context=%lu mode=%u outcome=%u state=%u revision=%lu\n",
                    (unsigned long)f.acknowledged.boot,(unsigned long)f.acknowledged.sequence,
                    (unsigned long)f.contextId,unsigned(f.cardMode),unsigned(f.outcome),
                    unsigned(f.system),(unsigned long)f.revision);
    if(f.type==Type::CardContext)
      Serial.printf("CARD CONTEXT id=%lu mode=%u\n",(unsigned long)f.contextId,unsigned(f.cardMode));
    if(f.type==Type::StateSync) {
      Serial.printf("STATE_SYNC ack=%lu/%lu hub=%lu/%lu state=%u revision=%lu channel=%u\n",
                    (unsigned long)f.acknowledged.boot,(unsigned long)f.acknowledged.sequence,
                    (unsigned long)f.stamp.boot,(unsigned long)f.stamp.sequence,
                    unsigned(f.system),(unsigned long)f.revision,channel);
      if(searching) {
        searching=false;needHello=false;lastHeartbeat=now;
        Serial.println("HUB ONLINE: fresh correlated STATE_SYNC; removal required");
        if(savedChannel!=channel) {
          if(nvs_set_u8(nvsHandle,"channel",channel)==ESP_OK && nvs_commit(nvsHandle)==ESP_OK)
            savedChannel=channel;
          else Serial.println("NVS channel save failed");
        }
      }
    }
  }
}
void expireLink(uint32_t now) {
  if(!searching && !link.online(now)) {
    abandonCard("hub offline");link.disconnect();searching=true;needHello=true;
    Serial.println("HUB OFFLINE: searching; removal required");
  }
  if(link.card.expired(now))abandonCard("750ms deadline");
  if(link.heartbeat.expired(now)) {
    link.heartbeat.active=false;Serial.println("HEARTBEAT ACK deadline");
  }
}
void schedule(uint32_t now) {
  expireLink(now);
  if(!inFlight && !link.heartbeat.active && !link.card.active) {
    if(searching) {
      if(!needHello && elapsed(now,dwellStarted,kChannelDwellMs))
        if(!setChannel(RfidConfig::kChannels[(channelIndex+1)%kChannelCount]))return;
      if(needHello) {
        needHello=false;dwellStarted=now;startHeartbeat(Type::RfidHello,now);
      }
    } else if(elapsed(now,lastHeartbeat,kHeartbeatMs))startHeartbeat(Type::RfidStatus,now);
  }
  Rfid::Pending& p=link.card.active?link.card:link.heartbeat;
  if(transport && !inFlight && p.due(now)) {
    ++p.attempts;inFlight=true;sendStarted=now;
    const esp_err_t result=esp_now_send(RfidConfig::kHubStaMac,p.bytes,p.length);
    Serial.printf("TX type=%u seq=%lu attempt=%u channel=%u\n",unsigned(p.frame.type),
                  (unsigned long)p.frame.stamp.sequence,p.attempts,channel);
    if(result!=ESP_OK) {
      inFlight=false;++sendErrors;Serial.printf("TX submission failed: %s\n",esp_err_to_name(result));
    }
  }
}

CardPresence pollCard(CardUid& uid) {
  // Check actual register communication, not only a failed REQA, before counting absence.
  const byte version=reader.PCD_ReadRegister(MFRC522::VersionReg);
  if(version==0 || version==0xFF || (reader.PCD_ReadRegister(MFRC522::TxControlReg)&3)!=3) {
    ++readerErrors;return CardPresence::Unknown;
  }
  byte atqa[2]{},size=sizeof(atqa);
  reader.PCD_StopCrypto1();
  const auto status=reader.PICC_WakeupA(atqa,&size);
  if(status==MFRC522::STATUS_TIMEOUT) {
    // Only the chip's timer IRQ, with no RF error, qualifies as measured absence.
    const byte irq=reader.PCD_ReadRegister(MFRC522::ComIrqReg);
    const byte errors=reader.PCD_ReadRegister(MFRC522::ErrorReg);
    // Include temperature/write faults as well as collision/CRC/parity/protocol errors.
    if((irq&1) && !(errors&0xDF))return CardPresence::Absent;
    ++readerErrors;return CardPresence::Unknown;
  }
  if(status!=MFRC522::STATUS_OK) {
    ++readerErrors;return CardPresence::Unknown; // Collision is never removal.
  }
  MFRC522::Uid selected{};
  const auto selectedStatus=reader.PICC_Select(&selected);
  if(selectedStatus==MFRC522::STATUS_OK) {
    uid.length=selected.size;
    if(uid.length<=sizeof(uid.bytes))memcpy(uid.bytes,selected.uidByte,uid.length);
  } else ++readerErrors;
  // Also halt after failed selection: never leave a selected card ACTIVE.
  if(reader.PICC_HaltA()!=MFRC522::STATUS_OK) {
    ++readerErrors;uid=CardUid{};return CardPresence::Unknown;
  }
  reader.PCD_StopCrypto1();
  return CardPresence::Present; // Failed UID consumes the latch too.
}
void sampleReader(uint32_t now) {
  if(!readerReady || !elapsed(now,lastSample,RfidConfig::kSampleMs))return;
  lastSample=now;CardUid uid{};
  const CardPresence presence=pollCard(uid);
  now=millis(); // SPI polling may have crossed grant/result deadlines.
  if(transport)expireLink(now);
  if(!transport || searching || link.heartbeat.active || inFlight) {
    link.latch.observe(presence,now,false);return; // Never queue a presentation.
  }
  if(sequence==UINT32_MAX){fault("sequence exhausted");return;}
  if(link.present(presence,uid,{boot,sequence+1},now)) {
    ++sequence;
    Serial.printf("CARD REQUEST seq=%lu context=%lu mode=%u UID length=%u bytes=",
                  (unsigned long)sequence,(unsigned long)link.card.frame.contextId,
                  unsigned(link.card.frame.cardMode),uid.length);
    for(uint8_t i=0;i<uid.length;++i)Serial.printf("%02X",uid.bytes[i]);
    Serial.println();
  }
}
void diagnostics(uint32_t now) {
  if(!elapsed(now,lastDiagnostic,kHeartbeatMs))return;
  lastDiagnostic=now;
  uint32_t drops,bad,txLost;
  portENTER_CRITICAL(&countersMux);drops=rxDrops;bad=callbackRejects;txLost=txDrops;portEXIT_CRITICAL(&countersMux);
  Serial.printf("DIAG transport=%s channel=%u session=%lu online=%u state=%u red=%u green=%u "
                "rxDropped=%lu rejected=%lu callbackBad=%lu txDropped=%lu submitErrors=%lu "
                "macFailures=%lu unknownResults=%lu readerErrors=%lu rxAgeMs=%lu\n",
                transport?(searching?"SEARCH":"LINKED"):"DISABLED",channel,(unsigned long)boot,
                transport && link.online(now),unsigned(link.system),leds.red,leds.green,
                (unsigned long)drops,(unsigned long)rejected,(unsigned long)bad,(unsigned long)txLost,
                (unsigned long)sendErrors,(unsigned long)radioFailures,(unsigned long)unknownResults,
                (unsigned long)readerErrors,
                (unsigned long)(link.hub.seen?uint32_t(now-link.hub.receivedAt):UINT32_MAX));
}
}

void setup() {
  writeLed(RfidPins::red,RfidConfig::kRedActiveLevel,false);
  writeLed(RfidPins::green,RfidConfig::kGreenActiveLevel,false);
  pinMode(RfidPins::red,RfidConfig::kRedActiveLevel==RfidConfig::Level::Unspecified?INPUT:OUTPUT);
  pinMode(RfidPins::green,RfidConfig::kGreenActiveLevel==RfidConfig::Level::Unspecified?INPUT:OUTPUT);
  pinMode(RfidPins::irq,INPUT);
  Serial.begin(115200);
  Serial.printf("\nRFID wire v1 / DomoticaProtocol 1.2.0 / MFRC522 1.4.12 / ESP32 %s\n",
                ESP_ARDUINO_VERSION_STR);
  Serial.println("User-confirmed pins: SS17 RST23 SCK18 MISO21 MOSI19 IRQ22 green25 red26");
  SPI.begin(RfidPins::sck,RfidPins::miso,RfidPins::mosi,RfidPins::ss);
  reader.PCD_Init();delay(50); // Preserved functional initialization, startup only.
  reader.PCD_DumpVersionToSerial();
  const byte version=reader.PCD_ReadRegister(MFRC522::VersionReg);
  readerReady=version!=0 && version!=0xFF;
  if(!readerReady)Serial.println("READER unavailable; no card requests");
  rxQueue=xQueueCreateStatic(kRxQueueDepth,sizeof(RxItem),rxStorage,&rxQueueControl);
  txQueue=xQueueCreateStatic(1,sizeof(TxItem),txStorage,&txQueueControl);
  if(!rxQueue || !txQueue){fault("queues");return;}
  WiFi.persistent(false);WiFi.setAutoReconnect(false);
  if(!WiFi.mode(WIFI_STA)){fault("STA start");return;}
  const esp_err_t disconnected=esp_wifi_disconnect();
  if(disconnected!=ESP_OK && disconnected!=ESP_ERR_WIFI_NOT_CONNECT) {
    checked(disconnected,"STA disconnect");return;
  }
  if(!checked(esp_wifi_set_ps(WIFI_PS_NONE),"Wi-Fi sleep")
      || !checked(esp_wifi_get_mac(WIFI_IF_STA,staMac),"STA MAC"))return;
  Serial.print("RFID STA MAC=");printMac(staMac);Serial.println();
  if(!configurationValid()){fault("missing/invalid local provisioning");return;}
  if(!startSession()){fault("NVS session persistence");return;}
  beginRadio();
}
void loop() {
  uint32_t now=millis();
  if(transport) {
    expireLink(now);completeSend(now);
    if(transport)receive();
  }
  sampleReader(millis());
  if(transport)schedule(millis());
  else if(radioInitialized) {
    RxItem rx{};TxItem tx{};
    for(size_t i=0;i<kRxQueueDepth && xQueueReceive(rxQueue,&rx,0)==pdTRUE;++i){}
    xQueueReceive(txQueue,&tx,0);
  }
  now=millis();updateLeds(now);diagnostics(now);yield();
}
