#include "../visual_alert.h"
#include "../notification_routes.h"
constexpr bool timing() {
  VisualAlert a;a.trigger(Notice::Armed);if(a.active)return false;
  a.trigger(Notice::Motion);
  if(a.tick(50000,false,true)||a.shown)return false;
  if(a.tick(51000,true,true)||!a.shown)return false;
  if(a.tick(60999,true,true)||!a.active)return false;
  if(!a.tick(61000,true,true)||a.active)return false;
  a.trigger(Notice::DoorOpen);a.tick(100,true,true);
  a.trigger(Notice::DoorClosed);a.tick(9000,true,true);
  if(a.kind!=Notice::DoorClosed||a.tick(10100,true,true))return false;
  if(!a.tick(19000,true,true))return false;
  a.trigger(Notice::Motion);a.tick(UINT32_MAX-4999,true,true);
  if(a.tick(4999,true,true)||!a.tick(5000,true,true))return false;
  a.trigger(Notice::Motion);return a.tick(1,false,false)&&!a.active;
}
static_assert(timing(),"Ten visible seconds, replacement, rollover and disarm");
static_assert(!visualNotice(Notice::Armed)&&!visualNotice(Notice::Disarmed)&&!visualNotice(Notice::Test),"No arm or test overlay");
static_assert(emailResponseUrl("https://script.googleusercontent.com/macros/echo?x=1")
 &&!emailResponseUrl("http://script.googleusercontent.com/")
 &&!emailResponseUrl("https://script.googleusercontent.com.evil.test/")
 &&!emailResponseUrl("https://script.googleusercontent.com@evil.test/")
 &&!emailResponseUrl(""),"Only the Google HTTPS response host");
