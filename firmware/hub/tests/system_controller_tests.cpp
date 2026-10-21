#include "../system_controller.h"
using namespace Domotica;
constexpr bool policy() {
  SystemController c;
  using R=SystemController::Result;
  if(c.state()!=SystemState::Disarmed||c.revision()!=1)return false;
  if(c.request(true,false,false)!=R::SensorsOffline||c.revision()!=1)return false;
  if(c.request(true,true,false)!=R::Changed||!c.armed()||c.revision()!=2)return false;
  if(c.request(true,true,true)!=R::Unchanged||c.revision()!=2)return false;
  if(c.request(false,false,false)!=R::Changed||c.armed()||c.revision()!=3)return false;
  if(c.request(true,false,true)!=R::Changed||!c.armed()||c.revision()!=4)return false;
  if(c.request(false,false,false)!=R::Changed||c.armed())return false;
  SystemController reboot;
  return !reboot.armed()&&reboot.revision()==1;
}
constexpr bool syncAndLed() {
  SystemController c;
  c.request(true,true,false);
  Frame f;
  f.type=Type::StateSync;f.source=Node::Hub;f.destination=Node::Door;
  f.stamp={2,1};f.acknowledged={1,5};f.channel=1;
  f.system=c.state();f.revision=c.revision();
  uint8_t bytes[kMaxFrameSize]={};size_t size=0;Frame decoded;
  if(encode(f,bytes,sizeof(bytes),size)!=Error::Ok||decode(bytes,size,decoded)!=Error::Ok)return false;
  if(!ledOn(decoded.system,true)||ledOn(decoded.system,false))return false;
  c.request(false,false,false);
  return !ledOn(c.state(),true)&&decoded.revision==2;
}
static_assert(policy(),"Arming policy, revision, disarm and reboot");
static_assert(syncAndLed(),"Real global state serializes using unchanged v1 LED rule");
