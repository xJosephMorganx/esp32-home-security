#include "../geometry.h"
// Compile-time tests execute the actual production coordinate and gesture code.
constexpr bool coordinates(bool flip) {
  for(int x=0;x<320;++x)for(int y=0;y<240;++y) {
    auto native=logicalToNative(x,y,flip);
    if(native.x<0||native.x>=240||native.y<0||native.y>=320)return false;
    auto logical=nativeToLogical(native.x,native.y,flip);
    if(logical.x!=x||logical.y!=y)return false;
  }
  return true;
}
static_assert(coordinates(false),"Landscape mapping must be bijective");
static_assert(coordinates(true),"Flipped mapping must be bijective");
constexpr bool gestures() {
  TapTracker t;
  // Must observe clean release after boot.
  if(t.update(true,1,4,true)!=-1||t.update(true,0,-1,true)!=-1)return false;
  if(t.update(true,1,4,true)!=-1)return false;
  for(int i=0;i<100;++i)if(t.update(true,1,4,true)!=-1)return false;
  if(t.update(true,0,-1,true)!=4||t.update(true,0,-1,true)!=-1)return false;
  // Leaving and returning to the original button must cancel.
  t.update(true,1,4,true);t.update(true,1,-1,true);t.update(true,1,4,true);
  if(t.update(true,0,-1,true)!=-1)return false;
  // I2C error may never act as a release.
  t.update(true,1,4,true);t.update(false,0,-1,true);t.update(true,1,4,true);
  if(t.update(true,0,-1,true)!=-1)return false;
  // Second finger cancels until released.
  t.update(true,1,4,true);t.update(true,2,4,true);t.update(true,1,4,true);
  if(t.update(true,0,-1,true)!=-1)return false;
  // Screen redraw may not reinterpret a held gesture on another page.
  t.update(true,1,4,true);t.update(true,1,1,false);t.update(true,1,1,true);
  if(t.update(true,0,-1,true)!=-1)return false;
  t.update(true,1,1,true);
  return t.update(true,0,-1,true)==1;
}
static_assert(gestures(),"Touch must emit exactly one valid release action");
