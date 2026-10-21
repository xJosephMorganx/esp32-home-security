#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <nvs.h>
#include <atomic>
#include <DomoticaProtocol.h>
#include "hub_config.h"
#include "wifi_control.h"

namespace {
enum class Phase {Stopped,Local,Attempt,Cancel,Associated};
Phase phase=Phase::Stopped;
nvs_handle_t storage;
bool savedStorage=false,initial=true,configured=false;
uint8_t localChannel=Config::LOCAL_CHANNEL;
uint32_t since=0,lastAttempt=0,budget=0;
std::atomic<uint32_t> disconnectedAt{0};
std::atomic<bool> disconnectSeen{false};
std::atomic<uint16_t> disconnectReason{0};
bool legal(uint8_t channel) {
  wifi_country_t country{};
  return esp_wifi_get_country(&country)==ESP_OK && channel>=country.schan
      && channel<unsigned(country.schan)+country.nchan;
}
bool associated() {wifi_ap_record_t ap{};return esp_wifi_sta_get_ap_info(&ap)==ESP_OK;}
void cancel(uint32_t now) {
  phase=Phase::Cancel;since=now;
  disconnectSeen=false;
  esp_wifi_scan_stop();
  // ASSOC_LEAVE does not trigger the core's one-time initial retry.
  esp_wifi_disconnect();
}
void rememberChannel() {
  uint8_t channel=networkChannel();
  if(channel && channel!=localChannel) {
    localChannel=channel;
    if(savedStorage && (nvs_set_u8(storage,"channel",channel)!=ESP_OK || nvs_commit(storage)!=ESP_OK))
      Serial.println("Wi-Fi: channel persistence failed");
    Serial.printf("Wi-Fi: AP channel %u\n",channel);
  }
}
}
bool wifiControlBegin() {
  configured=HUB_WIFI_SSID[0]!=0;
  WiFi.persistent(false);WiFi.setAutoReconnect(false);
  WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info){
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      disconnectedAt=millis();disconnectSeen=true;
      disconnectReason=info.wifi_sta_disconnected.reason;
    }
  });
  if(!WiFi.mode(WIFI_STA))return false;
  WiFi.setSleep(false);
  savedStorage=nvs_open("hub-network",NVS_READWRITE,&storage)==ESP_OK;
  uint8_t previous=Config::LOCAL_CHANNEL;
  if(savedStorage && nvs_get_u8(storage,"channel",&previous)!=ESP_OK)previous=Config::LOCAL_CHANNEL;
  localChannel=legal(previous)?previous:Config::LOCAL_CHANNEL;
  if(!legal(localChannel)||esp_wifi_set_channel(localChannel,WIFI_SECOND_CHAN_NONE)!=ESP_OK)return false;
  // Configure credentials without starting association. radioBegin runs first.
  if(configured)WiFi.begin(HUB_WIFI_SSID,HUB_WIFI_PASSWORD,0,nullptr,false);
  phase=Phase::Local;
  Serial.printf("Wi-Fi: local channel %u, credentials %s\n",localChannel,configured?"configured":"absent");
  return true;
}
uint8_t networkChannel() {
  uint8_t channel=0;wifi_second_chan_t secondary;
  return esp_wifi_get_channel(&channel,&secondary)==ESP_OK?channel:0;
}
bool networkRadioStable(){return phase==Phase::Local||phase==Phase::Associated;}
void networkTick(uint32_t now) {
  uint16_t reason=disconnectReason.exchange(0);
  if(reason)Serial.printf("Wi-Fi: disconnect reason %u\n",unsigned(reason));
  if(phase==Phase::Stopped)return;
  if(phase==Phase::Associated) {
    if(associated()){rememberChannel();return;}
    Serial.println("Wi-Fi: AP lost; restoring local channel");cancel(now);return;
  }
  if(phase==Phase::Attempt) {
    if(associated()) {
      phase=Phase::Associated;rememberChannel();
      Serial.printf("Wi-Fi: associated on channel %u\n",networkChannel());return;
    }
    if(Domotica::elapsed(now,since,budget))cancel(now);
    return;
  }
  if(phase==Phase::Cancel) {
    // Drain the asynchronous disconnect and any core initial retry before setting channel.
    if(!Domotica::elapsed(now,since,200))return;
    if(disconnectSeen && !Domotica::elapsed(now,disconnectedAt.load(),200))return;
    if(associated()){cancel(now);return;}
    if(esp_wifi_set_channel(localChannel,WIFI_SECOND_CHAN_NONE)==ESP_OK) {
      phase=Phase::Local;
      Serial.printf("Wi-Fi: local channel restored to %u\n",localChannel);
    } else cancel(now);
    return;
  }
  if(configured && (initial||Domotica::elapsed(now,lastAttempt,Domotica::kWifiRetryPeriodMs))) {
    budget=initial?Domotica::kWifiInitialBudgetMs:Domotica::kWifiRetryBudgetMs;
    initial=false;lastAttempt=now;since=now;phase=Phase::Attempt;
    if(!WiFi.STA.connect())cancel(now);
  }
}
