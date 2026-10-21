#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include "HubSmallFont.h"
#include "display_driver.h"
#include "hub_config.h"
#include "ui.h"

namespace {
constexpr uint16_t WHITE=0xffff,INK=0x2105,MUTED=0x6b8f,LINE=0xe77c,
  PANEL=0xf79e,GREEN=0x140d,RED=0xc988,GRAY=0x8c71,WEATHER=0xf7be,GOLD=0xa361;
struct Rect {int x=0,y=0,w=0,h=0;};
Rect pending,active;
UiModel snapshot;
int stripY=0;
// GFX is used only as an in-memory text/geometry engine. It never owns SPI.
class StripCanvas : public Adafruit_GFX {
public:
  uint16_t pixels[320*2];
  int sx=0,sy=0,sw=320,sh=2;
  StripCanvas():Adafruit_GFX(320,240){}
  void begin(int x,int y,int w,int h) {
    sx=x;sy=y;sw=w;sh=h;
    for(int i=0;i<w*h;++i)pixels[i]=WHITE;
    setTextWrap(false);
  }
  void drawPixel(int16_t x,int16_t y,uint16_t c) override {
    if(x>=sx && x<sx+sw && y>=sy && y<sy+sh)pixels[(y-sy)*sw+x-sx]=c;
  }
  void writeFillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) override {
    int x0=max(int(x),sx),x1=min(int(x+w),sx+sw);
    int y0=max(int(y),sy),y1=min(int(y+h),sy+sh);
    for(int j=y0;j<y1;++j)for(int i=x0;i<x1;++i)pixels[(j-sy)*sw+i-sx]=c;
  }
  void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t c) override {writeFillRect(x,y,w,h,c);}
  void drawFastHLine(int16_t x,int16_t y,int16_t w,uint16_t c) override {writeFillRect(x,y,w,1,c);}
  void drawFastVLine(int16_t x,int16_t y,int16_t h,uint16_t c) override {writeFillRect(x,y,1,h,c);}
} g;

bool intersects(int y,int h) {return y<g.sy+g.sh && y+h>g.sy;}
void text(const char* s,int cx,int top,uint16_t c=INK,int size=1) {
  g.setFont(size==2?&FreeSans12pt7b:size==1?&FreeSans9pt7b:&HubSmallFont);
  g.setTextSize(1);g.setTextColor(c);
  int16_t bx,by;uint16_t w,h;
  g.getTextBounds(s,0,0,&bx,&by,&w,&h);
  if(!intersects(top,h))return;
  g.setCursor(cx-int(w)/2-bx,top-by);g.print(s);
}
void thickLine(int x,int y,int xx,int yy,uint16_t c) {
  g.drawLine(x,y,xx,yy,c);g.drawLine(x+1,y,xx+1,yy,c);
}
void shield(int cx,int cy,bool armed,uint16_t c) {
  // Fixed geometry in both states, approximately 34 x 40 pixels.
  const int pts[][2]={{-16,-17},{0,-21},{16,-17},{16,0},{11,11},{0,19},{-11,11},{-16,0},{-16,-17}};
  for(int i=1;i<9;++i)thickLine(cx+pts[i-1][0],cy+pts[i-1][1],cx+pts[i][0],cy+pts[i][1],c);
  if(armed){thickLine(cx-8,cy,cx-2,cy+6,c);thickLine(cx-2,cy+6,cx+9,cy-7,c);}
  else {thickLine(cx-7,cy-6,cx+7,cy+8,c);thickLine(cx+7,cy-6,cx-7,cy+8,c);}
}
void weatherIcon(int x,int y,const Weather& w) {
  if(!w.valid)return;
  if(w.code<=1) {
    if(w.day) {
      g.drawCircle(x,y,7,GOLD);g.drawCircle(x,y,6,GOLD);
      for(int d=-1;d<=1;++d)for(int e=-1;e<=1;++e)if(d||e)g.drawLine(x+d*10,y+e*10,x+d*13,y+e*13,GOLD);
    } else {g.fillCircle(x,y,11,MUTED);g.fillCircle(x+5,y-4,9,WEATHER);}
  } else {
    g.fillCircle(x-7,y,6,MUTED);g.fillCircle(x,y-4,8,MUTED);g.fillCircle(x+8,y,6,MUTED);
    g.fillRect(x-7,y,16,6,MUTED);
    if(w.code>=95) {
      g.drawLine(x+3,y+7,x-2,y+12,GOLD);g.drawLine(x-2,y+12,x+2,y+12,GOLD);
      g.drawLine(x+2,y+12,x-3,y+17,GOLD);
    } else if((w.code>=71&&w.code<=77)||w.code==85||w.code==86) {
      for(int i=-1;i<=1;++i){g.drawFastHLine(x+i*7-2,y+12,5,MUTED);g.drawFastVLine(x+i*7,y+10,5,MUTED);}
    } else if(w.code>=51)for(int i=-1;i<=1;++i)g.drawLine(x+i*7,y+10,x+i*7-2,y+14,MUTED);
  }
}
void navIcon(int index,int x,int y,uint16_t c) {
  if(index==0) {g.drawLine(x-8,y,x,y-7,c);g.drawLine(x,y-7,x+8,y,c);g.drawRect(x-5,y,11,8,c);}
  if(index==1)for(int i=0;i<3;++i){g.fillRect(x-7,y-5+i*5,2,2,c);g.drawFastHLine(x-2,y-5+i*5,10,c);}
  if(index==2){g.drawCircle(x,y,3,c);g.drawCircle(x,y,7,c);}
  if(index==3){g.drawCircle(x,y,6,c);g.drawCircle(x,y,2,c);g.drawFastHLine(x-9,y,3,c);g.drawFastHLine(x+6,y,3,c);g.drawFastVLine(x,y-9,3,c);g.drawFastVLine(x,y+6,3,c);}
}
void nav(const UiModel& m) {
  const char* names[]={"Home","Events","Nodes","Setup"};
  int selected=int(m.page);if(selected>3)selected=3;
  g.fillRect(0,192,320,48,PANEL);g.drawFastHLine(0,192,320,LINE);
  for(int i=0;i<4;++i) {
    if(i==selected){g.fillRect(i*80,193,80,47,LINE);g.fillRect(i*80,192,80,3,GRAY);}
    navIcon(i,i*80+40,207,i==selected?INK:MUTED);
    text(names[i],i*80+40,225,i==selected?INK:MUTED,0);
  }
}
void header(const UiModel& m) {
  const char* titles[]={"Home","Events","Nodes","Settings","Diagnostics","Touch test","Access cards","Add card","Scan card","Card details","Remove card","Notifications"};
  text(titles[int(m.page)],160,7,INK,0);
  if(m.demo)text("UI DEMO",32,9,MUTED,0);
  text(m.clock,289,9,INK,0);g.drawFastHLine(0,27,320,LINE);
}
const char* motionLabel(const UiModel& m) {
  if(!m.pirOnline)return "PIR Offline";
  switch(m.pirState) {
    case 1:return "Warming up";
    case 2:return "No motion";
    case 3:return "Motion detected";
    default:return "Unknown";
  }
}
void home(const UiModel& m) {
  if(intersects(36,148)) {
    uint16_t c=m.armed?RED:GREEN;
    g.fillRoundRect(10,36,145,148,8,c);
    g.fillCircle(82,93,46,m.armed?0xe3b2:0x65b6);
    g.fillCircle(82,93,42,WHITE);shield(82,93,m.armed,c);
    text(m.armed?"Armed":"Disarmed",82,151,WHITE,1);
  }
  if(intersects(36,82)) {
    g.fillRoundRect(165,36,145,82,6,WEATHER);
    text("Zapopan",237,41,MUTED,0);
    if(m.weather.valid) {
      char temp[16];snprintf(temp,sizeof(temp),"%.0f C",m.weather.temperature);
      weatherIcon(186,76,m.weather);text(temp,252,62,INK,2);
      text(m.weatherStale?"Stale weather":weatherLabel(m.weather.code,m.weather.day),237,99,MUTED,0);
    } else {text("-- C",237,62,INK,2);text("Weather unavailable",237,99,MUTED,0);}
  }
  if(intersects(123,38)) {
    text(m.doorOnline?(m.doorState==2?"Door open":m.doorState==1?"Door closed":"Door unknown"):"Door Offline",237,124,INK,0);
    text(motionLabel(m),237,145,INK,0);
    g.fillCircle(175,127,2,!m.doorOnline||!m.doorState?GRAY:m.doorState==2?RED:GREEN);g.fillCircle(175,148,2,!m.pirOnline||m.pirState<2?GRAY:m.pirState==3?RED:GREEN);
  }
  if(intersects(166,18)) {
    char count[8];snprintf(count,sizeof(count),"%u/3",unsigned(m.doorOnline)+unsigned(m.pirOnline)+unsigned(m.rfidOnline));
    text(count,184,169,MUTED,0);
    text(m.wifi?"Wi-Fi online":"Wi-Fi offline",260,169,MUTED,0);
  }
}
void button(const char* s,int x,int y,int w,int h) {
  g.fillRoundRect(x,y,w,h,5,PANEL);text(s,x+w/2,y+(h-12)/2,INK,0);
}
void events(const UiModel& m) {
  if(m.eventCount==0)text("No events",160,80,MUTED);
  for(int row=0;row<3;++row) {
    int index=m.eventPage*3+row;if(index>=m.eventCount)break;
    auto& e=m.events[index];int y=35+row*34;
    text(e.text,160,y,INK,0);
    text(e.localTime[0]=='-'?"Time unavailable":e.localTime,160,y+14,MUTED,0);
  }
  button("Previous",10,140,100,44);button("Next",210,140,100,44);
  char page[32];snprintf(page,sizeof(page),"%d / %d",m.eventPage+1,max(1,(m.eventCount+2)/3));text(page,160,158,MUTED,0);
}
void nodes(const UiModel& m) {
  const char* names[]={"Door","Motion","RFID"};
  for(int i=0;i<3;++i) {
    int y=35+i*50;g.fillRoundRect(10,y,300,44,5,PANEL);
    text(names[i],70,y+9,INK,1);
    text(i==2?(m.rfidOnline?(m.armed?"Armed":"Disarmed"):"Unknown"):i==1?motionLabel(m):i==0?(m.doorOnline?(m.doorState==2?"Open":m.doorState==1?"Closed":"Unknown"):"Door Offline"):"Unknown",215,y+6,INK,0);
    text(i==0?m.doorMessage:i==1?m.pirMessage:m.rfidMessage,215,y+24,MUTED,0);
  }
}
void settings() {
  button("Diagnostics",10,36,145,48);button("Touch test",165,36,145,48);
  button("Access cards",10,92,300,44);
  button("Notifications",10,140,145,44);
  text("Open-Meteo.com",237,149,MUTED,0);text("CC BY 4.0",237,166,MUTED,0);
}
void accessCards(const UiModel& m) {
  if(m.page==Page::Cards) {
    text(m.cardStorageReady?m.cardNotice:"Storage error - access disabled",160,32,MUTED,0);
    for(int row=0;row<2;++row)if(m.cardRows[row][0]) {
      int y=50+row*44;g.fillRoundRect(10,y,300,42,5,PANEL);
      char label[20];snprintf(label,sizeof(label),"Card %u",unsigned(m.cardPage*2+row+1));
      text(label,160,y+5,MUTED,0);text(m.cardRows[row],160,y+23,INK,0);
    }
    if(!m.cardCount)text("No authorized cards",160,85,MUTED,0);
    button("Previous",10,140,90,44);button("Add card",110,140,100,44);button("Next",220,140,90,44);
  } else if(m.page==Page::AddCard) {
    text("Add one card to this hub",160,42,INK,1);
    text("Remove the card from the reader first",160,74,MUTED,0);
    text("Then start scanning and present it",160,94,MUTED,0);
    text("Local management / no PIN",160,117,MUTED,0);
    button("Back",10,140,145,44);button("Scan card",165,140,145,44);
  } else if(m.page==Page::ScanCard) {
    text(m.cardNotice,160,47,INK,1);
    text("This step does not arm the system",160,82,MUTED,0);
    char remaining[24];snprintf(remaining,sizeof(remaining),"%u seconds remaining",m.scanSeconds);
    text(remaining,160,109,MUTED,0);button("Cancel",85,140,150,44);
  } else {
    text(m.page==Page::RemoveCard?"Remove this card?":"Authorized card UID",160,45,INK,1);
    text(m.selectedCard,160,83,INK,0);
    text(m.page==Page::RemoveCard?"This removes its access permission":"Stored on this hub",160,110,MUTED,0);
    button("Back",10,140,145,44);button("Remove",165,140,145,44);
  }
}
void whatsapp(const UiModel& m) {
  const auto& n=m.notifications;char line[64];
  snprintf(line,sizeof(line),"%s  >",notificationChannelName(m.notificationChannel));
  button(line,10,32,300,40);
  snprintf(line,sizeof(line),"%s / %u %s",n.message,n.recipients,m.notificationChannel==2?"email route":"recipients");text(line,160,77,INK,0);
  snprintf(line,sizeof(line),"Pending %lu / accepted %lu",(unsigned long)n.pending,(unsigned long)n.accepted);text(line,160,92,MUTED,0);
  snprintf(line,sizeof(line),"Failed %lu / uncertain %lu",(unsigned long)n.failed,(unsigned long)n.unknown);text(line,160,107,MUTED,0);
  snprintf(line,sizeof(line),"HTTP %d / %d",n.http[0],n.http[1]);text(line,160,122,MUTED,0);
  button("Back",10,140,145,44);button("Send test",165,140,145,44);
}
void alertText(const char* s,int top) {
  g.setFont(&FreeSans9pt7b);g.setTextSize(2);g.setTextColor(WHITE);
  int16_t bx,by;uint16_t w,h;g.getTextBounds(s,0,0,&bx,&by,&w,&h);
  if(intersects(top,h)){g.setCursor(160-int(w)/2-bx,top-by);g.print(s);}
  g.setTextSize(1);
}
void alert(const UiModel& m) {
  g.fillRect(0,0,320,240,RED);
  const char* first="System",*second="Event";
  switch(m.alert.kind) {
    case Notice::Motion:first="Motion";second="Detected";break;
    case Notice::DoorOpen:first="Door";second="Opened";break;
    case Notice::DoorClosed:first="Door";second="Closed";break;
    case Notice::CardAdded:first="Access card";second="Added";break;
    case Notice::CardRemoved:first="Access card";second="Removed";break;
    case Notice::DoorOffline:first="Door";second="Disconnected";break;
    case Notice::DoorOnline:first="Door";second="Reconnected";break;
    case Notice::PirOffline:first="PIR";second="Disconnected";break;
    case Notice::PirOnline:first="PIR";second="Reconnected";break;
    case Notice::RfidOffline:first="RFID";second="Disconnected";break;
    case Notice::RfidOnline:first="RFID";second="Reconnected";break;
    default:break;
  }
  text("System event",160,24,WHITE,0);
  alertText(first,78);alertText(second,123);
  text("Tap to close",160,210,WHITE,0);
}
void diagnostics(const UiModel& m) {
  char s[64];text(Config::VERSION,160,34,INK,0);
  snprintf(s,sizeof(s),"MAC %s / CH %d",m.mac,m.channel);text(s,160,49,MUTED,0);
  snprintf(s,sizeof(s),"Touch %s / errors %lu",m.touchOk?"OK":"ERROR",(unsigned long)m.touchErrors);text(s,160,64,INK,0);
  snprintf(s,sizeof(s),"Heap %lu B / HTTP %d",(unsigned long)m.heap,m.weather.httpStatus);text(s,160,79,MUTED,0);
  snprintf(s,sizeof(s),"Weather: %s",m.weatherAge);text(s,160,94,MUTED,0);
  snprintf(s,sizeof(s),"Door: %s / PIR: %s",m.doorMessage,m.pirMessage);text(s,160,109,MUTED,0);
  snprintf(s,sizeof(s),"RX %lu / reject %lu / lost %lu",(unsigned long)m.radioRx,(unsigned long)m.radioRejected,(unsigned long)m.radioDropped);text(s,160,124,MUTED,0);
  button("Back",110,140,100,44);
}
void touchTest(const UiModel& m) {
  text("Touch corners and center",160,37,INK,0);
  char s[32];snprintf(s,sizeof(s),"X %d  Y %d",m.touchX,m.touchY);text(s,160,60,MUTED,0);
  const int pts[][2]={{8,85},{311,85},{8,180},{311,180},{160,110}};
  for(auto& p:pts){g.drawFastHLine(p[0]-5,p[1],11,GRAY);g.drawFastVLine(p[0],p[1]-5,11,GRAY);}
  if(m.touchX>=0)g.drawCircle(m.touchX,m.touchY,5,RED);
  button("Back",110,140,100,44);
}
void draw(const UiModel& m) {
  if(m.alert.active){alert(m);return;}
  if(intersects(0,28))header(m);
  if(intersects(28,164))switch(m.page) {
    case Page::Home:home(m);break;case Page::Events:events(m);break;
    case Page::Nodes:nodes(m);break;case Page::Settings:settings();break;
    case Page::Diagnostics:diagnostics(m);break;case Page::TouchTest:touchTest(m);break;
    case Page::WhatsApp:whatsapp(m);break;
    case Page::Cards:case Page::AddCard:case Page::ScanCard:case Page::CardDetail:case Page::RemoveCard:
      accessCards(m);break;
  }
  if(intersects(192,48))nav(m);
}
bool inside(int x,int y,int left,int top,int w,int h){return x>=left&&x<left+w&&y>=top&&y<top+h;}
}

void uiInvalidate(int x,int y,int w,int h) {
  if(w<=0||h<=0)return;
  int x0=max(0,x),y0=max(0,y),x1=min(320,x+w),y1=min(240,y+h);
  if(pending.w){x0=min(x0,pending.x);y0=min(y0,pending.y);x1=max(x1,pending.x+pending.w);y1=max(y1,pending.y+pending.h);}
  pending={x0,y0,x1-x0,y1-y0};
}
void uiInterrupt(){active={};pending={};uiInvalidate();}
bool uiBusy(){return pending.w||active.w;}
void uiTick(const UiModel& model) {
  if(!active.w) {
    if(!pending.w)return;
    active=pending;pending={};stripY=active.y;snapshot=model;
  }
  int h=min(2,active.y+active.h-stripY);
  g.begin(active.x,stripY,active.w,h);draw(snapshot);
  displayStrip(active.x,stripY,active.w,h,g.pixels);
  stripY+=h;if(stripY>=active.y+active.h)active={};
}
int uiHit(const UiModel& m,int x,int y) {
  if(x<0||x>=320||y<0||y>=240)return NoAction;
  if(m.alert.active)return DismissAlert;
  if(y>=192)return x/80;
  if(m.page==Page::Home&&inside(x,y,10,36,145,148))return ArmTile;
  if(m.page==Page::Settings) {
    if(inside(x,y,10,36,145,48))return OpenDiagnostics;
    if(inside(x,y,165,36,145,48))return OpenTouchTest;
    if(inside(x,y,10,92,300,44))return OpenCards;
    if(inside(x,y,10,140,145,44))return OpenWhatsApp;
  }
  if(m.page==Page::WhatsApp) {
    if(inside(x,y,10,32,300,40))return NextNotificationChannel;
    if(inside(x,y,10,140,145,44))return GoBack;
    if(m.notifications.recipients && inside(x,y,165,140,145,44))return TestWhatsApp;
  }
  if(m.page==Page::Cards) {
    for(int row=0;row<2;++row)if(m.cardRows[row][0]&&inside(x,y,10,50+row*44,300,42))return CardRow0+row;
    if(inside(x,y,10,140,90,44))return CardsPrevious;
    if(inside(x,y,110,140,100,44))return AddCard;
    if(inside(x,y,220,140,90,44))return CardsNext;
  }
  if(m.page==Page::AddCard) {
    if(inside(x,y,10,140,145,44))return CardBack;
    if(inside(x,y,165,140,145,44))return StartScan;
  }
  if(m.page==Page::ScanCard && inside(x,y,85,140,150,44))return CancelScan;
  if(m.page==Page::CardDetail||m.page==Page::RemoveCard) {
    if(inside(x,y,10,140,145,44))return CardBack;
    if(inside(x,y,165,140,145,44))return m.page==Page::CardDetail?RemoveCard:ConfirmRemove;
  }
  if(m.page==Page::Events) {
    if(inside(x,y,10,140,100,44))return Previous;
    if(inside(x,y,210,140,100,44))return Next;
  }
  if((m.page==Page::Diagnostics||m.page==Page::TouchTest)&&inside(x,y,110,140,100,44))return GoBack;
  return NoAction;
}
