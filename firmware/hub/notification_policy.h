#pragma once
#include <stdint.h>
#include <stddef.h>
enum class Notice : uint8_t { Armed,Disarmed,Motion,DoorOpen,DoorClosed,CardAdded,CardRemoved,
  DoorOffline,DoorOnline,PirOffline,PirOnline,RfidOffline,RfidOnline,Test };
struct NotificationPolicy {
  bool armed=false;
  uint8_t watched=0,previous=0;
  template<class Emit> constexpr void observe(bool next,uint8_t online,Emit emit) {
    if(next!=armed) {
      armed=next;watched=next?online:0;previous=online;
      emit(next?Notice::Armed:Notice::Disarmed);return;
    }
    if(armed)for(uint8_t i=0;i<3;++i)if((watched&(1<<i))&&((previous^online)&(1<<i)))
      emit(static_cast<Notice>(uint8_t(Notice::DoorOffline)+i*2+((online&(1<<i))?1:0)));
    previous=online;
  }
  constexpr bool accepts(Notice kind) const {
    return kind==Notice::Test || (armed && (kind==Notice::Motion||kind==Notice::DoorOpen
      ||kind==Notice::DoorClosed||kind==Notice::CardAdded||kind==Notice::CardRemoved));
  }
};
constexpr bool urlEncode(const char* input,char* output,size_t capacity) {
  const char hex[]="0123456789ABCDEF";size_t n=0;
  for(size_t i=0;input[i];++i) {
    unsigned char c=static_cast<unsigned char>(input[i]);
    bool safe=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~';
    if(n+(safe?1:3)>=capacity)return false;
    if(safe)output[n++]=char(c);else {output[n++]='%';output[n++]=hex[c>>4];output[n++]=hex[c&15];}
  }
  if(!capacity)return false;
  output[n]=0;return true;
}
constexpr bool retryNotification(int status,uint8_t attempts,uint8_t maximum) {
  return status==429 && attempts<maximum;
}
