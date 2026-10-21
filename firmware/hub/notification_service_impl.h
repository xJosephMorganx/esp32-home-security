#pragma once
// One TLS worker, separate bounded queues and delivery results for each channel.
#include <atomic>
#include <strings.h>
#include "notification_service.h"
#include "notification_config.h"
#include "notification_routes.h"
namespace Notifications {
const char* phones[]={HUB_WHATSAPP_PHONE_1,HUB_WHATSAPP_PHONE_2};
const char* waKeys[]={HUB_WHATSAPP_APIKEY_1,HUB_WHATSAPP_APIKEY_2};
const char* messengerKeys[]={HUB_MESSENGER_APIKEY_1,HUB_MESSENGER_APIKEY_2};
const char* names[]={"WhatsApp","Messenger","Email"};
NotificationPolicy policy;
void (*visual)(Notice)=nullptr; // Main-loop use only, independent of network availability.
std::atomic<bool> workerReady{false};
struct Job {char text[128],stamp[48];uint32_t at;Notice kind;};
struct Channel {
  uint8_t configured=0;
  QueueHandle_t queue=nullptr;
  std::atomic<uint32_t> pending{0},accepted{0},failed{0},unknown{0},dropped{0},expired{0};
  std::atomic<int> http[2]{{0},{0}};
  Job job{};bool active=false;
  uint8_t remaining=0,attempts[2]={};uint32_t retryAt[2]={};
} channels[3];
const char* label(Notice n) {
  const char* labels[]={"System armed","System disarmed","Motion detected","Door opened","Door closed",
    "Access card added","Access card removed","Door disconnected","Door reconnected","PIR disconnected",
    "PIR reconnected","RFID disconnected","RFID reconnected","Notification test"};
  return labels[unsigned(n)];
}
bool validKey(const char* key){return key[0]&&strlen(key)<=128;}
bool validRecipient(int i) {
  size_t length=strlen(phones[i]);
  if(length<9||length>16||phones[i][0]!='+'||!validKey(waKeys[i]))return false;
  for(size_t p=1;p<length;++p)if(phones[i][p]<'0'||phones[i][p]>'9')return false;
  return true;
}
void enqueueFor(Notice kind,int channel) {
  auto& c=channels[channel];if(!c.configured)return;
  if(!c.queue||!workerReady.load()){++c.dropped;return;}
  Job job{};job.at=millis();job.kind=kind;time_t epoch=time(nullptr);
  if(epoch>1704067200) {
    struct tm local{};localtime_r(&epoch,&local);strftime(job.stamp,sizeof(job.stamp),"%Y-%m-%d %H:%M:%S",&local);
  } else snprintf(job.stamp,sizeof(job.stamp),"Clock unavailable / uptime %lus",(unsigned long)(job.at/1000));
  snprintf(job.text,sizeof(job.text),"Domotica Hub\n%s\n%s",label(kind),job.stamp);
  ++c.pending;
  if(xQueueSend(c.queue,&job,0)!=pdTRUE){--c.pending;++c.dropped;}
}
void enqueue(Notice kind) {
  if(visual)visual(kind);
  for(int channel=0;channel<3;++channel)enqueueFor(kind,channel);
}
struct Reply {char text[1024]={},location[2048]={};size_t size=0;bool overflow=false;};
esp_err_t receive(esp_http_client_event_t* e) {
  auto* reply=static_cast<Reply*>(e->user_data);
  if(e->event_id==HTTP_EVENT_ON_HEADER && e->header_key && e->header_value
      && !strcasecmp(e->header_key,"Location")) {
    size_t n=strlen(e->header_value);
    if(n<sizeof(reply->location))memcpy(reply->location,e->header_value,n+1);
  }
  if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0) {
    if(reply->size+size_t(e->data_len)>=sizeof(reply->text)){reply->overflow=true;return ESP_OK;}
    memcpy(reply->text+reply->size,e->data,e->data_len);reply->size+=e->data_len;reply->text[reply->size]=0;
  }
  return ESP_OK;
}
int request(const char* url,Reply& reply,const char* body=nullptr) {
  esp_http_client_config_t cfg{};
  cfg.url=url;cfg.timeout_ms=8000;cfg.crt_bundle_attach=esp_crt_bundle_attach;
  cfg.event_handler=receive;cfg.user_data=&reply;cfg.disable_auto_redirect=true;
  auto client=esp_http_client_init(&cfg);if(!client)return -2;
  if(body) {
    esp_http_client_set_method(client,HTTP_METHOD_POST);
    esp_http_client_set_header(client,"Content-Type","application/json");
    esp_http_client_set_post_field(client,body,strlen(body));
  }
  auto result=esp_http_client_perform(client);
  int status=result==ESP_OK?esp_http_client_get_status_code(client):-1;
  esp_http_client_cleanup(client);return status;
}
int sendCallMeBot(int channel,int recipient,const Job& job,int& rawHttp) {
  char phone[49],key[385],message[385],url[1024];
  const char* apiKey=channel==0?waKeys[recipient]:messengerKeys[recipient];
  if(!urlEncode(apiKey,key,sizeof(key))||!urlEncode(job.text,message,sizeof(message)))return -2;
  int length;
  if(channel==0) {
    if(!urlEncode(phones[recipient],phone,sizeof(phone)))return -2;
    length=snprintf(url,sizeof(url),"https://api.callmebot.com/whatsapp.php?phone=%s&text=%s&apikey=%s",phone,message,key);
  } else length=snprintf(url,sizeof(url),"https://api.callmebot.com/facebook/send.php?apikey=%s&text=%s",key,message);
  if(length<0||size_t(length)>=sizeof(url))return -2;
  Reply reply;int status=request(url,reply);rawHttp=status;
  if(status>=200&&status<300) {
    if(reply.overflow)return -1;
    for(size_t i=0;i<reply.size;++i)if(reply.text[i]>='A'&&reply.text[i]<='Z')reply.text[i]+=32;
    if(strstr(reply.text,"error")||strstr(reply.text,"invalid")||strstr(reply.text,"failed"))return -2;
    return 200;
  }
  return status;
}
int sendEmail(const Job& job,int& rawHttp) {
  cJSON* root=cJSON_CreateObject();if(!root)return -2;
  bool built=cJSON_AddStringToObject(root,"secret",HUB_EMAIL_SECRET)
    &&cJSON_AddStringToObject(root,"event",noticeEventKey(job.kind))
    &&cJSON_AddStringToObject(root,"time",job.stamp);
  char* body=built?cJSON_PrintUnformatted(root):nullptr;cJSON_Delete(root);
  if(!body)return -2;
  Reply reply;int status=request(HUB_EMAIL_URL,reply,body);cJSON_free(body);
  // Google returns the result via a separate HTTPS GET. Never resend the POST,
  // forward the secret, or follow a login/untrusted redirect.
  if(status==302||status==303) {
    if(!emailResponseUrl(reply.location)){rawHttp=status;return -1;}
    char location[2048];memcpy(location,reply.location,sizeof(location));reply={};
    status=request(location,reply);
  }
  rawHttp=status;
  if(status<200||status>=300)return status;
  if(reply.overflow)return -1;
  root=cJSON_ParseWithLength(reply.text,reply.size);if(!root)return -1;
  auto ok=cJSON_GetObjectItemCaseSensitive(root,"ok");
  auto reason=cJSON_GetObjectItemCaseSensitive(root,"reason");
  int result=-1;
  if(cJSON_IsTrue(ok))result=200;
  else if(cJSON_IsFalse(ok)) {
    // The script explicitly marks send failures as potentially ambiguous.
    bool uncertain=cJSON_IsString(reason)&&strstr(reason->valuestring,"uncertain");
    result=uncertain?-1:-2;
  }
  cJSON_Delete(root);return result;
}
}
const char* notificationChannelName(uint8_t channel){return Notifications::names[channel<3?channel:0];}
void notificationSetVisualCallback(void (*callback)(Notice)){Notifications::visual=callback;}
void notificationBegin() {
  using namespace Notifications;
  for(int i=0;i<2;++i) {
    if(validRecipient(i))channels[0].configured|=uint8_t(1<<i);
    if(validKey(messengerKeys[i]))channels[1].configured|=uint8_t(1<<i);
  }
  if(channels[0].configured==3&&!strcmp(phones[0],phones[1]))channels[0].configured=1;
  if(channels[1].configured==3&&!strcmp(messengerKeys[0],messengerKeys[1]))channels[1].configured=1;
  if(startsWith(HUB_EMAIL_URL,"https://script.google.com/macros/s/")&&HUB_EMAIL_SECRET[0])channels[2].configured=1;
  for(auto& c:channels)if(c.configured)c.queue=xQueueCreate(NotificationConfig::queueDepth,sizeof(Job));
}
void notificationWorkerReady(bool ready){Notifications::workerReady=ready;}
void notificationObserve(bool armed,uint8_t onlineMask) {
  Notifications::policy.observe(armed,onlineMask,Notifications::enqueue);
}
void notificationEvent(Notice kind) {
  if(Notifications::policy.accepts(kind))Notifications::enqueue(kind);
}
void notificationTest(uint8_t channel){if(channel<3)Notifications::enqueueFor(Notice::Test,channel);}
NotificationStatus notificationStatus(uint8_t channel) {
  using namespace Notifications;
  NotificationStatus s;if(channel>=3)return s;
  const auto& c=channels[channel];s.recipients=uint8_t(bool(c.configured&1)+bool(c.configured&2));
  s.pending=c.pending.load();s.accepted=c.accepted.load();s.failed=c.failed.load();s.unknown=c.unknown.load();
  s.dropped=c.dropped.load();s.expired=c.expired.load();s.http[0]=c.http[0];s.http[1]=c.http[1];
  s.message=!c.configured?"Not configured":!c.queue||!workerReady?"Worker unavailable":s.pending?"Pending":"Ready";
  return s;
}
void notificationPump() {
  using namespace Notifications;
  using namespace NotificationConfig;
  static bool attempted=false;static uint32_t lastSend=0;static uint8_t nextChannel=0;
  uint32_t now=millis();
  for(int i=0;i<3;++i) {
    auto& c=channels[i];if(!c.queue)continue;
    if(!c.active && xQueueReceive(c.queue,&c.job,0)==pdTRUE) {
      c.active=true;c.remaining=c.configured;
      for(int n=0;n<2;++n){c.attempts[n]=0;c.retryAt[n]=now;}
    }
    if(c.active&&uint32_t(now-c.job.at)>=expiryMs){++c.expired;--c.pending;c.active=false;}
  }
  if(WiFi.status()!=WL_CONNECTED||time(nullptr)<=1704067200)return;
  if(attempted&&uint32_t(now-lastSend)<intervalMs)return;
  for(int n=0;n<3;++n) {
    int channel=(nextChannel+n)%3;auto& c=channels[channel];if(!c.active)continue;
    for(int i=0;i<2;++i)if((c.remaining&(1<<i))&&int32_t(now-c.retryAt[i])>=0) {
      int raw=-2;int result=channel==2?sendEmail(c.job,raw):sendCallMeBot(channel,i,c.job,raw);
      c.http[i]=raw;++c.attempts[i];lastSend=millis();attempted=true;nextChannel=(channel+1)%3;
      Serial.printf("%s destination %d: HTTP %d, result %d\n",names[channel],i+1,raw,result);
      if(retryNotification(result,c.attempts[i],maxAttempts))c.retryAt[i]=lastSend+rateLimitRetryMs;
      else {
        c.remaining&=uint8_t(~(1<<i));
        if(result==200)++c.accepted;
        else if(result==-1||result>=500)++c.unknown;
        else ++c.failed;
      }
      if(!c.remaining){c.active=false;--c.pending;}return;
    }
  }
}
