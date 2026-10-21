#include "../notification_policy.h"
struct Log {Notice items[16]{};unsigned count=0;};
struct Emit {Log* log;constexpr void operator()(Notice n)const {log->items[log->count++]=n;}};
constexpr bool policy() {
  NotificationPolicy p;Log log;Emit emit{&log};
  p.observe(false,1,emit);
  if(log.count||p.accepts(Notice::DoorOpen)||p.accepts(Notice::CardAdded))return false;
  p.observe(true,5,emit); // Door and RFID connected at arming; PIR absent.
  if(log.count!=1||log.items[0]!=Notice::Armed||!p.accepts(Notice::Motion))return false;
  p.observe(true,7,emit); // Late PIR does not join the watched set.
  if(log.count!=1)return false;
  p.observe(true,2,emit);
  if(log.count!=3||log.items[1]!=Notice::DoorOffline||log.items[2]!=Notice::RfidOffline)return false;
  p.observe(true,2,emit);if(log.count!=3)return false;
  p.observe(true,7,emit);
  if(log.count!=5||log.items[3]!=Notice::DoorOnline||log.items[4]!=Notice::RfidOnline)return false;
  p.observe(false,0,emit);
  if(log.count!=6||log.items[5]!=Notice::Disarmed||p.accepts(Notice::CardRemoved))return false;
  p.observe(true,2,emit);p.observe(true,0,emit);
  return log.count==8&&log.items[7]==Notice::PirOffline&&p.accepts(Notice::Test);
}
constexpr bool encoding() {
  char out[32]={};const char expected[]="%2B52%20%26%0A%C3%B1";
  if(!urlEncode("+52 &\n\xC3\xB1",out,sizeof(out)))return false;
  for(unsigned i=0;i<sizeof(expected);++i)if(out[i]!=expected[i])return false;
  char shortOut[3]={};return !urlEncode("+",shortOut,sizeof(shortOut));
}
static_assert(policy(),"State changes always; activity only armed; watched peers fixed per arm cycle");
static_assert(encoding(),"Encode phone plus, message UTF-8, delimiters, and reject buffer overflow");
static_assert(retryNotification(429,1,3)&&!retryNotification(429,3,3)
  &&!retryNotification(-1,1,3)&&!retryNotification(500,1,3)&&!retryNotification(200,1,3),
  "Bounded explicit rejection retries, never repeat ambiguous sends or accepted recipients");
