#include "ui.hpp"
#include "param_units.hpp"
#include "walk.h"
#include "font.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
namespace amalthea {
static constexpr uint32_t BG = 0x000000, FG = 0xeaf1ed, MINT = 0x82e6af,
                          DIM = 0x9aafa6, LINE = 0x294038, SELECT = 0x385b4b,
                          EDIT = 0xf3c873;
void Canvas::rect(int x, int y, int width, int height, uint32_t c) {
  for (int j = std::max(y, 0); j < std::min(y + height, h); j++)
    for (int i = std::max(x, 0); i < std::min(x + width, w); i++)
      pixels[j * w + i] = c | 0xff000000u;
}
void Canvas::blit(const Canvas &source,int x,int y) {
  int left=std::max(0,-x),right=std::min(source.w,w-x);
  if(right<=left)return;
  for(int row=std::max(0,-y);row<std::min(source.h,h-y);row++)
    std::copy(source.pixels.begin()+row*source.w+left,
              source.pixels.begin()+row*source.w+right,
              pixels.begin()+(y+row)*w+x+left);
}
void Canvas::text(int x, int y, const std::string &s, int size,
                  uint32_t color) {
  int gw = size * 24 / 32, gh = size * 44 / 32;
  for (unsigned char c : s) {
    if (c < 32 || c > 127)
      c = '?';
    int k = c - 32;
    for (int j = 0; j < gh; j++) {
      int yy = y + j;
      if (yy < 0 || yy >= h)
        continue;
      for (int i = 0; i < gw; i++) {
        int xx = x + i;
        if (xx < 0 || xx >= w)
          continue;
        unsigned a = fontMask[((k / 16) * 44 + j * 44 / gh) * 384 +
                              (k % 16) * 24 + i * 24 / gw];
        if (!a)
          continue;
        auto old = pixels[yy * w + xx];
        uint32_t out = 0xff000000u;
        for (int sh = 0; sh < 24; sh += 8)
          out |=
              ((((color >> sh) & 255) * a + ((old >> sh) & 255) * (255 - a)) /
               255)
              << sh;
        pixels[yy * w + xx] = out;
      }
    }
    x += gw;
  }
}
void Canvas::textFit(int x,int y,int width,const std::string &text,int size,uint32_t color) {
  if(width<=0 || text.empty())return;
  while(size>2 && int(text.size())*(size*24/32)>width)--size;
  int glyph=size*24/32;
  if(glyph<1)return;
  this->text(x,y,text.substr(0,width/glyph),size,color);
}
uint32_t trackColor(int t) {
  static const uint32_t colors[12] = {0x40e0ff, 0xff9c38, 0xff65df, 0x8cff55,
                                      0x789dff, 0xff6969, 0x4bffc2, 0xd6a2ff,
                                      0xffd36c, 0xf3a0b5, 0xa7d9ff, 0xd4ff91};
  return colors[(t % 12 + 12) % 12];
}
void drawUI(Canvas &c, const Editor &e, const AudioStats &audio, bool help) {
  if((e.splash||e.page==Page::Appliances)&&!e.powerPrompt&&!e.poweringOff){drawSystemUI(c,e);return;}
  if(e.inAppliance()&&e.exitStage==Editor::ExitStage::None&&!e.powerPrompt){if(e.inLifeblood())drawLifebloodUI(c,e,audio);else drawFxUI(c,e,audio);
    // Volume keys: the same overlay as the Tracker's, in the appliance's colour.
    if(e.volumeBanner&&e.notice.empty()){
      float k=c.w/1024.f;auto R=[&](int x,int y,int w,int h,uint32_t col){c.rect(int(x*k),int(y*k),int(w*k),int(h*k),col);};
      uint32_t accent=e.inLifeblood()?0x3fd9d0:0xc4a0ff;
      R(130,260,764,218,0x101822);R(130,260,764,4,accent);
      c.text(int(170*k),int(280*k),"VOLUME",std::max(15,int(40*k)),accent);
      c.text(int(640*k),int(280*k),std::to_string(e.volume)+"%",std::max(15,int(48*k)),0xf3f5fa);
      R(170,386,684,40,0x26303c);R(170,386,684*std::clamp(e.volume,0,100)/100,40,accent);
      return;
    }
    // LIFEBLOOD keeps the field visible: plain notices sit in the bar beneath it.
    if(!e.notice.empty()&&e.inLifeblood()&&!(e.noticeDetailSerial==e.noticeSerial&&!e.noticeDetail.empty())){
      int y=c.h*702/768,h=c.h*62/768,size=std::max(18,c.w*34/1024);c.rect(0,y,c.w,h,0x07080b);c.rect(c.w*24/1024,y,c.w*976/1024,std::max(2,c.w/512),0xe8603c);
      c.textFit(c.w*24/1024,y+(h-size*44/32)/2+2,c.w*976/1024,e.notice,size,0xf3f1ea);return;}
    if(!e.notice.empty()){int b=std::max(2,c.w/256);
     if(e.noticeDetailSerial==e.noticeSerial&&!e.noticeDetail.empty()){
      // Parameter popup: name above, value very large.
      int x=c.w/16,y=c.h*5/16,w=c.w*7/8,h=c.h*3/8,small=std::max(24,c.w*40/1024),big=std::max(48,c.w*120/1024);
      c.rect(x,y,w,h,e.inLifeblood()?0x3fd9d0:0xc4a0ff);c.rect(x+b,y+b,w-2*b,h-2*b,0x101822);
      c.textFit(x+32,y+h/8,w-64,e.notice,small,0x95a4b6);c.textFit(x+32,y+h/8+small+c.h/40,w-64,e.noticeDetail,big,0xf3f5fa);
     }else{int x=c.w/10,y=c.h*3/8,w=c.w*4/5,h=c.h/4,size=std::max(28,c.w*56/1024);
      c.rect(x,y,w,h,0xc4a0ff);c.rect(x+b,y+b,w-2*b,h-2*b,0x101822);c.textFit(x+28,y+h/2-size/2,w-56,e.notice,size,0xf3f5fa);}}return;}
  c.rect(0, 0, c.w, c.h, BG);
  float scale = c.w / 1024.f;
  auto X = [&](int n) { return int(n * scale); };
  auto txt = [&](int x, int y, const std::string &s, int size,
                 uint32_t color = FG) {
    c.text(X(x), X(y), s, std::max(15, X(size)), color);
  };
  auto fit = [&](int x,int y,int width,const std::string &s,int size,uint32_t color=FG) {
    c.textFit(X(x),X(y),X(width),s,std::max(2,X(size)),color);
  };
  auto rect = [&](int x, int y, int w, int h, uint32_t col) {
    c.rect(X(x), X(y), X(w), X(h), col);
  };
  if(e.powerPrompt || e.poweringOff) {
    rect(52,176,920,432,0x101010);rect(52,176,920,6,0xffff00);
    fit(100,214,824,e.poweringOff?"POWERING OFF...":"POWER OFF?",64,0xffff00);
    if(e.poweringOff) {
      fit(100,374,824,"PLEASE WAIT",36,FG);
    } else {
      for(int choice=0;choice<2;choice++) {
        bool selected=(choice==0)==e.powerYes;
        int x=100+choice*440;
        rect(x,334,384,112,selected?trackColor(e.track):0x303030);
        txt(x+92,354,choice==0?"YES":"NO",56,selected?BG:FG);
      }
      fit(100,476,824,"CROSS: YES / NO",32,FG);
      fit(100,526,824,"SHORT PRESS POWER TO CONFIRM",30,FG);
      if(e.dirty || e.editing)fit(76,628,872,"UNSAVED CHANGES WILL BE LOST",28,0xffff00);
    }
    return;
  }
  if(e.exitStage!=Editor::ExitStage::None){
    constexpr uint32_t brand=0x6ee7dc;
    drawAmaltheaMark(c,X(448),X(66),X(128),brand);
    rect(64,242,896,322,0x101a20);rect(64,242,896,3,brand);
    bool save=e.exitStage==Editor::ExitStage::Save;
    bool saving=e.exitStage==Editor::ExitStage::Saving;
    fit(108,281,808,saving?"SAVING SESSION":save?"SAVE BEFORE EXIT?":e.inLifeblood()?"EXIT LIFEBLOOD?":e.inFx()?"EXIT FX?":"EXIT TRACKER?",54,FG);
    if(!saving)for(int choice=0;choice<2;choice++){
      bool selected=e.exitChoice==bool(choice);int x=108+choice*420;
      rect(x,409,388,108,selected?brand:0x26333b);
      fit(x+36,431,316,save?(choice?"YES":"NO"):(choice?"OK":"CANCEL"),50,selected?BG:FG);
    }
    return;
  }
  auto volumeOverlay = [&]() {
    if(!e.notice.empty()&&e.noticeDetailSerial==e.noticeSerial&&!e.noticeDetail.empty()) {
      // A value being dialled: its name above, the value very large (as in the FX appliance).
      uint32_t frame=trackColor(e.track);
      rect(64,240,896,288,frame);rect(70,246,884,276,0x101010);
      fit(104,268,816,e.notice,40,0xb0b0b0);
      fit(104,334,816,e.noticeDetail,120,FG);
      return;
    }
    if(!e.notice.empty()) {
      rect(64,260,896,220,0x101010);rect(64,260,896,5,trackColor(e.track));
      int size=std::min(52,int(800*32/std::max<size_t>(1,e.notice.size())/24));
      txt(104,326,e.notice,size,0xffff00);
      return;
    }
    if (!e.volumeBanner) return;
    rect(130,260,764,218,0x151515); rect(130,260,764,4,0xffff00);
    txt(170,280,"VOLUME",40,0xffff00);
    txt(640,280,std::to_string(e.volume)+"%",48,FG);
    rect(170,386,684,40,0x404040);
    rect(170,386,684*e.volume/100,40,0xffff00);
  };
  const bool bankPage=e.page==Page::Sessions||e.page==Page::Session||e.page==Page::Patterns||e.page==Page::PatternSettings;
  const bool globalPage=e.page==Page::Dashboard||e.page==Page::GlobalInserts||e.page==Page::Device;
  const auto accent = globalPage?0xc4d3eb:bankPage?0x739fff:trackColor(e.track);
  if (e.trackBanner) {
    txt(332, 180, "TRACK", 72, 0xffff00);
    char number[16];
    snprintf(number, sizeof number, "%02d", e.track + 1);
    txt(320, 280, number, 240, accent);
    volumeOverlay();
    return;
  }
  drawAmaltheaMark(c,X(24),X(6),X(52),accent,1);
  txt(88, 10, "TRACKER", 38, accent);
  char b[160];
  snprintf(b, sizeof b, "%s  %03d BPM", e.playing ? "PLAY" : "STOP",
           e.project.bpm);
  txt(410, 18, b, 28);
  drawBattery(c, X(300), X(22), X(20), e.battery, e.charging, DIM);
  const std::string trackBadge=globalPage?(e.page==Page::Dashboard?"MENU":e.page==Page::GlobalInserts?"FX":"SYS"):bankPage?(e.page==Page::Sessions||e.page==Page::Session?"S":"P")+((e.page==Page::Sessions||e.page==Page::Session?e.activeSession:e.activePattern)<0?"--":std::to_string((e.page==Page::Sessions||e.page==Page::Session?e.activeSession:e.activePattern)+1)):e.page==Page::Mixer?std::to_string(e.mixerPage+1)+"/2":"T"+std::to_string(e.track+1);
  rect(788,6,204,104,accent);rect(780,14,220,88,accent);
  txt(780+(220-int(trackBadge.size())*54)/2,8,trackBadge,72,BG);
  const auto &s =
      e.editing ? e.pending : e.project.tracks[e.track].steps[e.row];
  int instrument=effectiveInstrument(e.project.tracks[e.track],s);
  snprintf(b,sizeof b,"PATTERN %02d / %02d %s%s",e.activePattern+1,instrument+1,
           instrumentName(trackPatch(e.project,e.track,instrument).engine),e.project.tracks[e.track].mute?" / MUTED":"");
  if(e.activePattern<0&&!bankPage)snprintf(b,sizeof b,"NEW PATTERN / %02d %s%s",instrument+1,instrumentName(trackPatch(e.project,e.track,instrument).engine),e.project.tracks[e.track].mute?" / MUTED":"");
  if(e.queuedPattern>=0&&!bankPage){size_t n=strlen(b);snprintf(b+n,sizeof b-n," > NEXT %02d",e.queuedPattern+1);}
  if(globalPage)snprintf(b,sizeof b,"%s",e.page==Page::Dashboard?"DASHBOARD":e.page==Page::GlobalInserts?"GLOBAL INSERTS":"DEVICE SETTINGS");
  if(bankPage)snprintf(b,sizeof b,"%s",e.page==Page::Sessions||e.page==Page::Session?"SESSION BANK":"PATTERN BANK");
  if(e.erasing&&e.page==Page::Pattern)fit(24,66,e.shift||e.shift2?610:730,"ERASE MODE - ENTER CLEARS / SHIFT+ENTER EXITS",26,0xff3b30);
  else if(e.hasSelection()&&e.page==Page::Pattern&&!e.editing)fit(24,66,e.shift||e.shift2?610:730,std::to_string(e.selectionLast()-e.selectionFirst()+1)+" ROWS SELECTED - ENTER EDITS ALL",26,0xffff00);
  else fit(24,66,e.shift||e.shift2?610:730,b,26,0xffff00);
  if(e.shift||e.shift2)fit(650,66,116,e.shift?"SHIFT":"FUNC",24,0xffff00);
  rect(24, 111, 976, 2, LINE);
  if (e.page != Page::Pattern) {
    if(e.page==Page::Dashboard){
      txt(32,132,"DASHBOARD",42,FG);
      const char *labels[]={"TRACK","MIXER","MASTER FX","INSERT FX","DEVICE"};
      for(int tile=0;tile<5;tile++){
        int x=24+tile%3*328,y=204+tile/3*274;bool selected=tile==e.dashboardItem;
        uint32_t fill=selected?0x263347:0x111720,ink=selected?0xf4f7ff:0x91a5c1;
        rect(x,y,304,246,selected?accent:0x263140);rect(x+3,y+3,298,240,fill);
        int cx=x+152,cy=y+96;
        if(tile==0){
          rect(cx-55,cy-45,110,90,ink);rect(cx-50,cy-40,100,80,fill);
          for(int n=1;n<4;n++)rect(cx-50,cy-40+n*20,100,3,ink);
          rect(cx-18,cy-40,3,80,ink);rect(cx+18,cy-40,3,80,ink);
        }else if(tile==1){
          for(int n=0;n<3;n++){int fx=cx-38+n*38;rect(fx-2,cy-49,4,98,ink);rect(fx-12,cy-25+n*20,24,12,ink);}
        }else if(tile==2){
          for(int n=0;n<5;n++){int h=30+(n%3)*22;rect(cx-52+n*24,cy+45-h,10,h,ink);}
          rect(cx-57,cy+52,118,4,ink);
        }else if(tile==3){
          for(int n=0;n<3;n++){rect(cx-57+n*40,cy-25,34,50,ink);rect(cx-53+n*40,cy-21,26,42,fill);}
          rect(cx-65,cy-2,10,4,ink);rect(cx+57,cy-2,10,4,ink);
        }else{
          rect(cx-38,cy-38,76,76,ink);rect(cx-18,cy-55,36,110,ink);rect(cx-55,cy-18,110,36,ink);
          rect(cx-30,cy-30,60,60,fill);rect(cx-12,cy-12,24,24,ink);
        }
        int labelWidth=int(std::string(labels[tile]).size())*24;
        txt(x+(304-labelWidth)/2,y+180,labels[tile],32,ink);
      }
    }else if(e.page==Page::GlobalInserts){
      txt(32,132,"INSERT EFFECTS",42,FG);
      int first=std::clamp(e.globalInsertItem-7,0,GlobalInsertCount-8),enabled=0;
      for(auto type:globalInsertTypes())enabled+=e.device.insertEnabled(type);
      txt(820,142,std::to_string(enabled)+"/"+std::to_string(GlobalInsertCount),28,DIM);
      for(int n=first;n<std::min(first+8,GlobalInsertCount);n++){
        int y=198+(n-first)*68;bool selected=n==e.globalInsertItem,on=e.device.insertEnabled(globalInsertTypes()[n]);
        if(selected)rect(24,y,976,62,accent);auto ink=selected?BG:on?FG:DIM;
        fit(48,y+8,730,fxName(globalInsertTypes()[n]),34,ink);
        txt(858,y+6,on?"ON":"OFF",36,ink);
      }
      rect(996,198,4,538,LINE);rect(996,198+first*538/GlobalInsertCount,4,538*8/GlobalInsertCount,accent);
    }else if(e.page==Page::Device){
      txt(32,132,"DEVICE SETTINGS",42,FG);
      rect(24,244,976,164,accent);txt(48,268,"BRIGHTNESS",40,BG);
      txt(748,262,std::to_string(e.device.brightness)+"%",60,BG);
      rect(48,484,928,52,LINE);rect(48,484,928*e.device.brightness/100,52,accent);
    }else if(e.page==Page::Column){
      const auto &t=e.project.tracks[e.track];const auto &cfg=t.columns[e.column];
      fit(32,135,952,std::string(columnName(e.column))+" / COLUMN",42,0xffff00);
      const char *names[]={"LENGTH","CLOCK","DIRECTION"};
      std::string values[]={std::string(cfg.length?"":"TRACK / ")+std::to_string(columnLength(t,e.column))+" STEPS",
        std::string(cfg.clock==255?"TRACK / ":"")+clockName(columnClock(t,e.column)),
        std::string(cfg.direction==255?"TRACK / ":"")+amalthea_direction_name(columnDirection(t,e.column))};
      for(int field=0;field<3;field++){
        int y=220+field*174;bool selected=field==e.columnField;
        if(selected)rect(24,y,976,158,accent);
        auto ink=selected?BG:FG;txt(48,y+10,names[field],32,selected?BG:DIM);fit(48,y+64,928,values[field],56,ink);
      }
    } else if(e.page==Page::Sessions||e.page==Page::Patterns) {
      bool sessions=e.page==Page::Sessions;int selectedSlot=sessions?e.sessionSlot:e.patternSlot;
      int active=sessions?e.activeSession:e.activePattern,count=sessions?64:32;
      txt(32,130,sessions?"SESSIONS":"PATTERNS",44,0x94b6ff);
      auto saved=[&](int slot){return sessions?e.sessionState[slot]:uint8_t(bool(e.patterns[slot].state));};
      std::string live=active<0?"NEW":std::to_string(active+1);
      std::string next=!sessions&&e.queuedPattern>=0?" > NEXT "+std::to_string(e.queuedPattern+1):"";
      fit(420,142,564,"LOADED "+live+next+" / "+std::to_string(selectedSlot+1)+(saved(selectedSlot)==1?" SAVED":saved(selectedSlot)==2?" DAMAGED":" EMPTY"),24,DIM);
      for(int slot=0;slot<count;slot++){
        int x=24+slot%8*122,y=198+slot/8*(sessions?68:134),height=sessions?60:122;
        bool selected=slot==selectedSlot,loaded=slot==active;
        // Slots holding data stand apart from empty ones: a lighter body, a bright top band and a
        // white number; empty slots stay dark with a dimmed number. The loaded slot keeps its vivid
        // blue and underline; a damaged file is amber.
        const int state=saved(slot);
        uint32_t fill=loaded?0x315aca:state==1?0x2a4a7c:state==2?0x5a3a12:0x0d1017;
        if(selected)rect(x-3,y-3,120,height+6,0xf0f4ff);
        rect(x,y,114,height,fill);
        if(!loaded&&state==1)rect(x,y,114,6,0x9fc0ff);
        if(!loaded&&state==2)rect(x,y,114,6,0xffb35c);
        if(!loaded&&!state)rect(x,y,114,1,0x1e2430);
        snprintf(b,sizeof b,"%02d%s",slot+1,state==1?"+":state==2?"!":"");
        txt(x+12,y+(sessions?5:22),b,sessions?32:36,loaded||state?0xffffff:0x4a5468);
        if(loaded)rect(x+12,y+height-10,90,4,0xc4d6ff);
        // The pattern waiting for its bar: an amber frame and NEXT.
        if(!sessions&&slot==e.queuedPattern){
          rect(x,y,114,4,0xffb35c);rect(x,y+height-4,114,4,0xffb35c);rect(x,y,4,height,0xffb35c);rect(x+110,y,4,height,0xffb35c);
          txt(x+12,y+height-38,"NEXT",24,0xffb35c);
        }
      }
    } else if(e.page==Page::Mixer){
      fit(32,132,780,"MIXER",44,FG);
      txt(800,142,std::to_string(e.mixerPage*6+1)+" - "+std::to_string(e.mixerPage*6+6),28,DIM);
      bool anySolo=false;for(const auto&t:e.project.tracks)anySolo|=t.solo;
      for(int channel=0;channel<6;channel++){
        int tr=e.mixerPage*6+channel,x=24+channel*164;const auto&t=e.project.tracks[tr];
        auto color=trackColor(tr);bool selected=e.mixerTrack==tr;uint32_t panel=selected?0x1b2028:0x101318;
        rect(x,200,156,546,panel);rect(x,200,156,5,color);
        txt(x+20,216,"T"+std::to_string(tr+1),42,color);
        txt(x+20,280,std::to_string(t.level)+"%",32,FG);
        // Recessed fader rail, calibrated ticks and a geometric handle.
        rect(x+69,342,6,238,0x050607);
        for(int tick=0;tick<=10;tick++)rect(x+38,342+tick*23,16,tick%5?1:2,0x48505d);
        int fy=572-t.level*222/100;
        if(selected&&e.mixerField==0)rect(x+21,328,112,266,e.editing?color:0x8896ad);
        if(selected&&e.mixerField==0){rect(x+24,331,106,260,panel);rect(x+69,342,6,238,0x050607);}
        rect(x+47,fy,50,14,color);rect(x+51,fy+6,42,2,0x06080a);
        int meter=int(std::min(1.f,audio.meters[tr].load()*4)*238);
        rect(x+139,342,5,238,0x252c34);if(trackAudible(e.project,tr))rect(x+139,580-meter,5,meter,color);
        for(int f=1;f<3;f++){
          int y=610+(f-1)*66;bool value=f==1?t.mute:t.solo,focus=selected&&e.mixerField==f;
          if(focus)rect(x+9,y-3,138,56,e.editing?color:0x8896ad);
          rect(x+12,y,132,50,value?(f==1?0xc17535:0xb7c83b):0x242b34);
          txt(x+22,y+8,f==1?"MUTE":"SOLO",28,value?BG:FG);
        }
        if(anySolo&&!t.solo)rect(x+20,264,116,3,0x434b58);
      }
    } else if(e.page==Page::Randomizer) {
      const char *columns[]={"NOTE","INSTRUMENT","FX 1","FX 2","MODIFIER"};
      txt(32,140,"RANDOMIZER",48,0xffff00);
      fit(32,220,952,std::string(columns[e.column])+" / "+std::to_string(columnLength(e.project.tracks[e.track],e.column))+" STEPS",40,accent);
      const auto setting=e.density[e.track][e.column];
      // MODE first, DENSITY below; the selected row is filled.
      bool modeRow=e.randomizerField==0;
      if(modeRow)rect(24,300,976,110,accent);
      txt(48,322,"MODE",48,modeRow?BG:DIM);
      std::string mode=euclideanMode(setting)?"EUCLIDEAN":"RANDOM";
      txt(976-int(mode.size())*48*24/32,322,mode,48,modeRow?BG:FG);
      if(!modeRow)rect(24,440,976,140,accent);
      txt(48,472,"DENSITY",48,modeRow?DIM:BG);
      txt(720,452,std::to_string(densityPercent(setting))+"%",80,modeRow?FG:BG);
      rect(48,620,928,40,LINE);
      rect(48,620,928*densityPercent(setting)/100,40,accent);
    } else if(e.page==Page::Macro){
      const auto &t=e.project.tracks[e.track];bool local=t.macroOverride&(1<<e.macroModule);
      fit(32,135,952,std::string(macroName(e.macroModule))+" / TRACK",42,0xffff00);
      int first=e.macroField/4*4;auto profile=effectiveMacros(e.project,e.track)[e.macroModule];
      for(int field=first;field<std::min(first+4,9);field++){
        int y=244+(field-first)*120;bool selected=field==e.macroField;
        if(selected)rect(24,y,976,110,accent);auto ink=selected?BG:FG;
        txt(48,y+22,field?macroParameterName(e.macroModule,field-1):"SOURCE",40,ink);
        std::string value=field?macroValueText(e.macroModule,field-1,profile.value[field-1]):local?"TRACK":"MASTER";
        txt(740,y+18,value,44,ink);
      }
      txt(900,190,std::to_string(first/4+1)+"/3",26,DIM);
    } else if(e.page==Page::Track) {
      const auto &t=e.project.tracks[e.track];
      txt(32,130,"TRACK SETTINGS",42,0xffff00);
      const char *names[]={"LENGTH","CLOCK","DIRECTION","INSTRUMENT","VOLUME","PAN","ABRASION SEND","CORROSION SEND","MODULATION SEND","DELAY SEND","REVERB SEND","ENGINE"};
      std::string values[]={std::to_string(t.length)+" STEPS",clockName(t.clock),amalthea_direction_name(t.direction),
        std::to_string(t.instrument+1)+" "+instrumentName(trackPatch(e.project,e.track,t.instrument).engine),
        std::to_string(t.level)+"%",units::pan((int(t.pan)+100)/2),
        std::to_string(t.macroSend[0])+"%",std::to_string(t.macroSend[1])+"%",std::to_string(t.macroSend[2])+"%",
        std::to_string(t.delaySend)+"%",std::to_string(t.reverbSend)+"%",instrumentName(trackPatch(e.project,e.track,t.instrument).engine)};
      int first=std::clamp(e.trackField-7,0,TrackFields-8);
      for(int i=first;i<std::min(first+8,TrackFields);i++) {
        int y=198+(i-first)*68;bool selected=i==e.trackField;
        if(selected)rect(24,y,976,62,accent);auto ink=selected?BG:FG;
        txt(48,y+6,names[i],34,ink);
        int x=std::max(420,970-int(values[i].size())*38*24/32);
        fit(x,y+4,970-x,values[i],38,ink);
      }
      rect(996,198,4,538,LINE);rect(996,198+first*538/TrackFields,4,538*8/TrackFields,accent);
    } else if (e.page == Page::Session||e.page==Page::PatternSettings) {
      bool session=e.page==Page::Session;
      txt(32,132,(session?"SESSION ":"PATTERN ")+std::to_string((session?e.sessionSlot:e.patternSlot)+1),44,0x94b6ff);
      auto o=(e.patternSlot==e.activePattern||(e.activePattern<0&&e.patternSlot==0))?e.patternOverrides:e.patterns[e.patternSlot].overrides;
      const auto&d=e.sessionDefaults;
      std::string values[]={session?std::to_string(d.bpm)+" BPM":o.bpm?std::to_string(o.bpm)+" BPM":"SESSION / "+std::to_string(d.bpm),
        session?scaleName(d.scale):o.scale==255?std::string("SESSION / ")+scaleName(d.scale):scaleName(o.scale),
        session?noteName(60+d.root).substr(0,2):o.root==255?"SESSION / "+noteName(60+d.root).substr(0,2):noteName(60+o.root).substr(0,2)};
      const char *names[]={"TEMPO","SCALE","ROOT NOTE"};
      if(!session)for(int i=0;i<3;i++){
        int y=224+i*174;if(i==e.sessionField)rect(24,y,976,158,accent);
        auto ink=i==e.sessionField?BG:FG;
        txt(48,y+10,names[i],32,ink);fit(48,y+64,928,values[i],52,ink);
      }else{
        // SESSION: TEMPO, then SCALE and ROOT NOTE joined by a bracket to the die that rolls
        // them both, then SWITCH QUANTIZE. A selected row is framed; in edit mode (START) it
        // fills and LEFT / RIGHT change its value. The die fills when selected.
        const std::string rows[]={values[0],values[1],values[2],patternQuantizeName(d.quantize)};
        const char *labels[]={"TEMPO","SCALE","ROOT NOTE","SWITCH QUANTIZE"};
        for(int i=0;i<SessionFields;i++){
          int y=200+i*130,width=i==1||i==2?800:976;bool selected=i==e.sessionField,editing=selected&&e.sessionEdit;
          if(editing)rect(24,y,width,118,accent);
          else if(selected){rect(24,y,width,118,accent);rect(30,y+6,width-12,106,BG);}
          auto ink=editing?BG:FG;
          txt(48,y+8,editing?std::string(labels[i])+"  < EDIT >":labels[i],30,editing?BG:selected?accent:DIM);fit(48,y+46,width-48,rows[i],52,ink);
        }
        bool dieSelected=e.sessionField==SessionDie,armed=dieSelected||e.sessionField==1||e.sessionField==2;uint32_t joint=armed?accent:LINE;
        rect(832,387,24,4,joint);rect(832,517,24,4,joint);rect(852,387,4,134,joint);rect(856,450,14,4,joint);
        int dx=870,dy=390,ds=124;
        rect(dx,dy,ds,ds,dieSelected?accent:armed?accent:0x1c2230);
        if(!dieSelected)rect(dx+(armed?6:3),dy+(armed?6:3),ds-(armed?12:6),ds-(armed?12:6),0x121722);
        uint32_t pip=dieSelected?BG:FG;
        // (five: one in each corner and the centre)
        const int at[5][2]={{26,26},{80,26},{53,53},{26,80},{80,80}};
        for(auto &q:at)rect(dx+q[0]-2,dy+q[1]-2,22,22,pip);
        fit(dx,dy+ds+8,ds,"ROLL",26,armed?accent:DIM);
      }
    } else {
      txt(32,136,"MASTER EFFECTS",42,0xffff00);
      txt(32,211,std::to_string(masterPageIndex(e.masterModule)+1)+" / "+std::to_string(MasterPages),30,DIM);
      txt(220,200,masterName(e.masterModule),42,FG);
      int first=e.masterParameter/MasterParameters*MasterParameters;
      for(int parameter=first;parameter<std::min(first+MasterParameters,masterParameterCount(e.masterModule));parameter++) {
        int y=282+(parameter-first)*114;
        bool selected=parameter==e.masterParameter;
        if(selected)rect(24,y,976,104,accent);
        uint32_t ink=selected?BG:FG;
        txt(48,y+20,masterParameterName(e.masterModule,parameter),44,ink);
        txt(760,y+12,masterValueText(e.masterModule,parameter,e.project.master.value(e.masterModule,parameter)),52,ink);
      }
      if(masterParameterCount(e.masterModule)>4)txt(900,211,std::to_string(first/4+1)+"/"+std::to_string((masterParameterCount(e.masterModule)+3)/4),26,DIM);
    }
    volumeOverlay();return;
  }
  if (help) {
    txt(32, 140, "CONTROLS", 36, MINT);
    const char *lines[] = {"CROSS: MOVE   START: EDIT / COMMIT",
                           "EDIT: UP/DOWN CHANGES VALUE",
                           "FX: LEFT/RIGHT TYPE, UP/DOWN AMOUNT",
                           "TAP X: DENSITY   HOLD X: RANDOMIZE",
                           "F1/F2: PREVIOUS / NEXT TRACK",
                           "L1: SHIFT   R1: FUNC",
                           "Y: MASTER FX   A: COLUMN / BACK",
                           "BOTTOM: PLAY / PATTERNS / ENTER",
                           "L2: DASHBOARD   R2: HOME",
                           "SHIFT: SESSIONS   FUNC: MIXER"};
    int y = 202;
    for (auto l : lines) {
      txt(32, y, l, 28);
      y += 43;
    }
    volumeOverlay();return;
  }
  {
    // Columns sized to what they show (NOTE 3, INST 2, FX 5, FX 5, MOD 3 characters) with an equal
    // space between the values, instead of five equal slots that left INST adrift.
    // Laid out in ColumnOrder (NOTE, MOD, INST, FX 1, FX 2), indexed here by stored column.
    const int leftAt[] = {92,276,460,621,851}, widthAt[] = {100,100,100,134,134};
    int left[Columns],widths[Columns];
    for(int p=0;p<Columns;p++){left[ColumnOrder[p]]=leftAt[p];widths[ColumnOrder[p]]=widthAt[p];}
    const char *names[] = {"NOTE", "INST", "FX1", "FX2", "MOD"};
    txt(24, 122, "ROW", 23, DIM);
    for (int col = 0; col < Columns; col++)
      txt(left[col] + 8, 119, names[col], 28, col == e.column ? accent : DIM);
    int rh = e.visibleRows == 16 ? 36 : 72,
        base = (e.row / e.visibleRows) * e.visibleRows;
    std::array<int,Columns> play{};bool hasColumnStats=false;
    for(int c=0;c<Columns;c++){play[c]=e.playing?audio.columnRow[e.track][c].load():-1;hasColumnStats|=play[c]>=0;}
    // Legacy diagnostic views may publish only the NOTE/track playhead.
    if(e.playing&&!hasColumnStats)play.fill(audio.trackRow[e.track].load());
    bool aligned=true;
    const auto &shown=e.project.tracks[e.track];
    for(int c=1;c<Columns;c++)aligned &= play[c]==play[0] && columnLength(shown,c)==columnLength(shown,0) && columnClock(shown,c)==columnClock(shown,0) && columnDirection(shown,c)==columnDirection(shown,0);
    uint32_t playColor=0;
    for(int shift=0;shift<24;shift+=8)playColor|=(((accent>>shift)&255)*38/100)<<shift;
    for (int i = 0; i < e.visibleRows; i++) {
      int row = base + i, y = 164 + i * rh;
      if (row >= trackDisplayLength(e.project.tracks[e.track]))
        break;
      bool sel = row == e.row;
      auto step = e.project.tracks[e.track].steps[row];
      if (sel && e.editing)
        step = e.pending;
      if(aligned && play[0]==row)rect(24,y,976,rh,playColor);
      else if(play[0]==row)rect(24,y,68,rh,playColor);
      if (row % 4 == 0)
        rect(20, y, 3, rh, accent);
      if (play[0] == row)
        rect(20, y, 5, rh, accent);
      snprintf(b, sizeof b, "%03d", row);
      txt(27, y, b, 26, sel ? MINT : DIM);
      for (int col = 0; col < Columns; col++) {
        int length=columnLength(e.project.tracks[e.track],col);
        if(row>=length){txt(left[col]+8,y+(e.visibleRows==8?12:0),"...",e.visibleRows==8?38:32,0x303030);continue;}
        if(!aligned && play[col]==row){rect(left[col],y,widths[col],rh,playColor);uint32_t edge=0;for(int shift=0;shift<24;shift+=8)edge|=(((accent>>shift)&255)*60/100)<<shift;rect(left[col],y,4,rh,edge);}
        if(row==length-1)rect(left[col],y+rh-1,widths[col]-4,1,LINE);
        bool picked=col==e.column&&e.rowSelected(row)&&!sel;
        if(picked){rect(left[col],y,widths[col]-4,rh,playColor);rect(left[col],y,4,rh,accent);}
        if (sel && col == e.column) {
          // ERASE mode: the cursor keeps the track colour inside a red frame.
          if (e.erasing) {
            rect(left[col] - 3, y - 3, widths[col] + 2, rh + 6, 0xff3b30);
            rect(left[col] + 3, y + 3, widths[col] - 10, rh - 6, accent);
          } else
            rect(left[col], y, widths[col] - 4, rh, accent);
        }
        std::string val;
        if (col == 0)
          val = noteName(step.note);
        else if (col == 1) {
          snprintf(b, sizeof b, "%02d", step.instrument + 1);
          val = step.instrument==NoInstrument?"--":b;
        } else if (col < 4)
          val = fxText(step.fx[col - 2]);
        else val = modifierText(step.modifier);
        txt(left[col] + 8, y + (e.visibleRows == 8 ? 12 : 0), val,
            e.visibleRows == 8 ? 38 : 32,
            sel && col == e.column ? BG : FG);
        // A varied row (FUNC + hold X): a small square after its instrument number.
        if(col==1&&step.variation&&step.instrument!=NoInstrument)
          rect(left[col]+widths[col]-24,y+rh/2-5,10,10,sel&&col==e.column?BG:accent);
      }
    }
  }
  if (e.editing && !e.mixer) {
    const char *columns[]={"NOTE", "INSTRUMENT", "FX 1", "FX 2", "MODIFIER"};
    // Render into a bounded surface, then center it with a full eighth-screen
    // margin on each side. No magnifier content can escape this surface.
    Canvas panel(X(768),X(336));
    panel.rect(0,0,panel.w,panel.h,0x101010);
    panel.rect(0,0,panel.w,X(5),accent);
    const int padding=X(32), contentWidth=panel.w-2*padding;
    auto label=[&](int x,int y,int width,const std::string &text,int size,uint32_t ink) {
      panel.textFit(X(x),X(y),X(width),text,std::max(2,X(size)),ink);
    };
    label(32,20,e.column==2 || e.column==3?190:704,columns[e.column],32,accent);
    int tabStart=std::min((e.eventTab/2)*2,e.eventTabCount()-2);
    for(int visible=0;visible<2;visible++){
      int tab=tabStart+visible;
      int x=32+visible*352;bool selected=e.eventTab==tab;
      panel.rect(X(x),X(64),X(344),X(48),selected?accent:0x282828);
      bool fx=e.fxColumn();
      label(x+16,70,312,tab==0?(e.column==4?"OFFSET":fx?"DRY/WET MIX":"VALUE"):(e.column==0||fx)&&tab==1?"LENGTH":tab==(e.column==0||fx?2:1)?"CONDITION":noteParameterName(tab-3),30,selected?BG:FG);
    }
    std::string value,description;
    if(e.lengthTab()&&e.fxColumn()){
      auto fx=e.pending.fx[e.column-2];int n=fx.length;
      label(248,20,488,fxName(fx.type),32,FG);
      value=std::to_string(n)+(n==1?" STEP":" STEPS");
      description=ringingEffect(fx.type)?"THEN ITS TAIL FADES OUT":"A NEW EFFECT REPLACES IT";
    }
    else if(e.lengthTab()){
      int n=e.pending.length;value=std::to_string(n)+(n==1?" STEP":" STEPS");
      description="REACHING THE NEXT NOTE: LEGATO";
    }
    else if(e.lockTab()>=0){
      int p=e.lockTab();auto v=e.pending.locks[p];int shown=v==Inherit?noteParameterDefault(e.project.tracks[e.track],p):v;
      value=p==NotePan?units::pan(shown):std::to_string(shown)+"%";
      description=v==Inherit?"TRACK DEFAULT":"STEP OVERRIDE";
    }
    else if(e.conditionTab()){if(e.fxColumn())label(248,20,488,fxName(e.pending.fx[e.column-2].type),32,FG);value=conditionText(eventCondition(e.pending,e.column));description="COLUMN CYCLE / "+std::to_string(eventCondition(e.pending,e.column).occurrence)+" OF "+std::to_string(eventCondition(e.pending,e.column).every);}
    else if(e.column==0) {value=noteName(e.pending.note);description="UP/DOWN: NOTE";}
    else if(e.column==1) {
      auto slot=effectiveInstrument(e.project.tracks[e.track],e.pending);
      value=std::to_string(slot+1)+" "+instrumentName(trackPatch(e.project,e.track,slot).engine);
      description=e.pending.instrument==NoInstrument?"TRACK DEFAULT / "+std::string(patchFamily(trackPatch(e.project,e.track,slot))):patchFamily(trackPatch(e.project,e.track,slot));
      if(e.pending.instrument!=NoInstrument&&e.pending.variation)description+=" / ROW VARIATION";
    } else if(e.column<4) {
      auto fx=e.pending.fx[e.column-2];
      label(248,20,488,fxName(fx.type),32,FG);
      value=units::wet(fx.value);
      description=fx.value==0?"DRY - LEFT/RIGHT: TYPE":"LEFT/RIGHT: TYPE   UP/DOWN: DRY/WET";
    } else {
      value=modifierText(e.pending.modifier);description="OFFSET IN SEMITONES";
    }
    panel.textFit(padding,X(136),contentWidth,value,X(88),FG);
    panel.textFit(padding,X(278),contentWidth,description,X(26),DIM);
    c.blit(panel,(c.w-panel.w)/2,X(220));
  }
  volumeOverlay();
}
void writePPM(const Canvas &c, const std::string &path) {
  std::ofstream f(path, std::ios::binary);
  f << "P6\n" << c.w << " " << c.h << "\n255\n";
  for (auto p : c.pixels) {
    f.put((p >> 16) & 255);
    f.put((p >> 8) & 255);
    f.put(p & 255);
  }
}
} // namespace amalthea
