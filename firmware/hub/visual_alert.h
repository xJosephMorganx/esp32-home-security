#pragma once
#include "notification_policy.h"
constexpr bool visualNotice(Notice kind) {
  return kind!=Notice::Armed&&kind!=Notice::Disarmed&&kind!=Notice::Test;
}
struct VisualAlert {
  bool active=false,shown=false;
  Notice kind=Notice::Motion;
  uint32_t shownAt=0;
  constexpr void trigger(Notice next) {
    if(visualNotice(next)){active=true;shown=false;kind=next;}
  }
  constexpr bool tick(uint32_t now,bool frameDone,bool armed) {
    if(!active)return false;
    if(!armed){active=false;return true;}
    if(!shown && frameDone){shown=true;shownAt=now;}
    if(shown && uint32_t(now-shownAt)>=10000){active=false;return true;}
    return false;
  }
};
