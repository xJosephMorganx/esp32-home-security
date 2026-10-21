#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
#include "hub_config.h"
#include "network_service.h"
#include "wifi_control.h"
#include "notification_service_impl.h"

namespace {
QueueHandle_t updates=nullptr;
struct Response {char bytes[2048];size_t size=0;bool overflow=false;};
esp_err_t receive(esp_http_client_event_t* e) {
  auto* response=static_cast<Response*>(e->user_data);
  if(e->event_id==HTTP_EVENT_ON_DATA && e->data_len>0) {
    if(response->size+size_t(e->data_len)>=sizeof(response->bytes)) {
      response->overflow=true;return ESP_FAIL;
    }
    memcpy(response->bytes+response->size,e->data,e->data_len);
    response->size+=e->data_len;response->bytes[response->size]=0;
  }
  return ESP_OK;
}
bool fetchWeather(Weather& w) {
  Response response{};
  esp_http_client_config_t cfg{};
  // Open-Meteo geocoding: Zapopan, Jalisco; city coordinates, not a home address.
  cfg.url="https://api.open-meteo.com/v1/forecast?latitude=20.72111&longitude=-103.38742&current=temperature_2m,weather_code,is_day&temperature_unit=celsius&timeformat=unixtime&timezone=America%2FMexico_City&forecast_days=1";
  cfg.timeout_ms=6000;
  cfg.crt_bundle_attach=esp_crt_bundle_attach;
  cfg.event_handler=receive;cfg.user_data=&response;
  cfg.disable_auto_redirect=true;
  auto client=esp_http_client_init(&cfg);
  if(!client) {w.httpStatus=-1;return false;}
  auto result=esp_http_client_perform(client);
  w.httpStatus=result==ESP_OK?esp_http_client_get_status_code(client):-1;
  esp_http_client_cleanup(client);
  if(result!=ESP_OK || w.httpStatus!=200 || response.overflow)return false;
  cJSON* root=cJSON_ParseWithLength(response.bytes,response.size);
  if(!root)return false;
  auto current=cJSON_GetObjectItemCaseSensitive(root,"current");
  auto temp=cJSON_GetObjectItemCaseSensitive(current,"temperature_2m");
  auto code=cJSON_GetObjectItemCaseSensitive(current,"weather_code");
  auto day=cJSON_GetObjectItemCaseSensitive(current,"is_day");
  auto stamp=cJSON_GetObjectItemCaseSensitive(current,"time");
  const time_t now=time(nullptr);
  bool good=cJSON_IsNumber(temp)&&cJSON_IsNumber(code)&&cJSON_IsNumber(day)&&cJSON_IsNumber(stamp);
  if(good) good=isfinite(temp->valuedouble)&&temp->valuedouble>=-80&&temp->valuedouble<=65
    && code->valuedouble==code->valueint && code->valueint>=0&&code->valueint<=99
    &&(day->valuedouble==0||day->valuedouble==1)
    &&stamp->valuedouble>double(now)-7200&&stamp->valuedouble<=double(now)+900;
  if(good) {
    w.valid=true;w.temperature=temp->valuedouble;w.code=code->valueint;
    w.day=day->valueint==1;w.observedAt=int64_t(stamp->valuedouble);w.fetchedAt=millis();
  }
  cJSON_Delete(root);return good;
}
void worker(void*) {
  Weather last;
  uint32_t lastTry=0;
  bool attempted=false,previousSuccess=false;
  for(;;) {
    const uint32_t now=millis();
    if(WiFi.status()==WL_CONNECTED && time(nullptr)>1704067200 &&
        (!attempted||uint32_t(now-lastTry)>=(previousSuccess?Config::WEATHER_PERIOD_MS:60000UL))) {
      attempted=true;lastTry=now;
      previousSuccess=fetchWeather(last);
      xQueueOverwrite(updates,&last);
      Serial.printf("Weather: %s (HTTP %d)\n",previousSuccess?"updated":"unavailable",last.httpStatus);
    }
    notificationPump();
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
}
bool networkBegin() {
  notificationBegin();
  if(!wifiControlBegin()){Serial.println("Wi-Fi initialization failed");return false;}
  if(!HUB_WIFI_SSID[0])Serial.println("Wi-Fi not configured; local UI available.");
  updates=xQueueCreate(1,sizeof(Weather));
  if(!updates)return false;
  configTzTime(Config::TIMEZONE,"pool.ntp.org","time.cloudflare.com");
  if(xTaskCreate(worker,"weather",16384,nullptr,1,nullptr)!=pdPASS) {
    vQueueDelete(updates);updates=nullptr;return false;
  }
  notificationWorkerReady(true);
  return true;
}
bool networkReadWeather(Weather& result) {
  return updates && xQueueReceive(updates,&result,0)==pdTRUE;
}

const char* weatherLabel(int code,bool day) {
  switch(code) {
    case 0:return day?"Sunny":"Clear";
    case 1:return day?"Mostly sunny":"Mostly clear";
    case 2:return "Partly cloudy";
    case 3:return "Cloudy";
    case 45:case 48:return "Fog";
    case 51:case 53:case 55:return "Drizzle";
    case 56:case 57:case 66:case 67:return "Freezing rain";
    case 61:case 63:case 65:case 80:case 81:case 82:return "Rain";
    case 71:case 73:case 75:case 77:case 85:case 86:return "Snow";
    case 95:case 96:case 99:return "Thunderstorm";
    default:return "Unknown";
  }
}
