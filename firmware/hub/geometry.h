#pragma once
#include <stdint.h>
struct Point { int x,y; };
constexpr Point nativeToLogical(int x,int y,bool flip) {
  return flip ? Point{y,239-x} : Point{319-y,x};
}
constexpr Point logicalToNative(int x,int y,bool flip) {
  return flip ? Point{239-y,x} : Point{y,319-x};
}
// Release inside the original target; leaving it cancels until a clean release.
class TapTracker {
  bool down=false, blocked=true;
  int target=-1;
public:
  constexpr int update(bool valid,int fingers,int hit,bool enabled) {
    if(!valid || fingers>1 || !enabled) { blocked=true;target=-1;return -1; }
    if(fingers==0) {
      int result=(down && !blocked)?target:-1;
      down=false;blocked=false;target=-1;return result;
    }
    if(!down) {down=true;target=blocked?-1:hit;}
    if(hit!=target) {target=-1;blocked=true;}
    return -1;
  }
};
