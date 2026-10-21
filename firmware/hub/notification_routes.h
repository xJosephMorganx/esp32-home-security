#pragma once
#include "notification_policy.h"
constexpr const char* noticeEventKey(Notice n) {
  const char* keys[]={"armed","disarmed","motion","door_open","door_closed","card_added","card_removed",
    "door_offline","door_online","pir_offline","pir_online","rfid_offline","rfid_online","test"};
  return keys[unsigned(n)];
}
constexpr bool startsWith(const char* text,const char* prefix) {
  for(size_t i=0;prefix[i];++i)if(text[i]!=prefix[i])return false;
  return true;
}
constexpr bool emailResponseUrl(const char* url) {
  return startsWith(url,"https://script.googleusercontent.com/");
}
