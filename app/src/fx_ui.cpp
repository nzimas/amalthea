#include "ui.hpp"
#include "fx_rack.hpp"
#include <algorithm>
namespace amalthea {
void drawFxUI(Canvas&c,const Editor&e,const AudioStats&audio){
 c.rect(0,0,c.w,c.h,0);float scale=c.w/1024.f;auto X=[&](int n){return int(n*scale);};
 auto box=[&](int x,int y,int w,int h,uint32_t color){c.rect(X(x),X(y),X(w),X(h),color);};
 auto text=[&](int x,int y,int width,const std::string&s,int size,uint32_t color=0xe8edf1){c.textFit(X(x),X(y),X(width),s,X(size),color);};
 constexpr uint32_t accent=0xc4a0ff,dim=0x95a4b6,ink=0x0c1018;static const FxRack empty{};const auto&r=e.fxRack?*e.fxRack:empty;
 drawAmaltheaMark(c,X(24),X(12),X(76),accent,2);text(120,25,600,"AMALTHEA / FX RACK",38);
 drawBattery(c,X(868),X(32),X(24),e.battery,e.charging,dim);
 int state=audio.usbState.load(),channels=audio.usbChannels.load();
 std::string input=state==0?(audio.usbPort==1?"USB: COMPUTER LINK":audio.usbPort==2?"USB: STARTING HOST":audio.usbPort==3?"USB HOST: WAITING":audio.usbPort==4?"USB: PORT ERROR":"USB: WAITING"):state==1?std::string(fm1Name(audio.usbFirmware))+" / "+std::to_string(channels)+" TRACKS":state==4?std::string(fm1Name(audio.usbFirmware))+" / STEREO MIX":state==2?"USB: FORMAT UNSUPPORTED":state==5?"USB: NO AUDIO INPUT":"USB: OPEN FAILED";
 text(24,114,590,input,30,dim);
 bool clock=audio.usbClock.load();std::string clockText=clock?std::string(audio.usbRunning?"EXT ":"EXT STOP ")+std::to_string(int(audio.usbBpm.load()+.5f))+" BPM":std::string(r.running?"INT ":"INT STOP ")+std::to_string(r.tempo)+" BPM";
 text(630,114,370,clockText,30,clock?accent:dim);box(24,169,976,2,0x28313e);
 if(e.page==Page::FxSessions){
  text(24,187,976,"FX RACK SESSIONS",38,accent);
  for(int i=0;i<64;i++){int x=24+(i%8)*122,y=252+(i/8)*58;bool selected=e.fxSessionSlot==i,active=e.fxActiveSession==i;
   box(x,y,112,49,active?0x73b5ff:selected?accent:0x293648);box(x+3,y+3,106,43,selected?0x45305e:0x101822);
   text(x+12,y+7,90,std::to_string(i+1),29,e.fxSessionState[i]?0xf0f4fa:dim);
   if(active)box(x+91,y+7,10,10,0x73b5ff);
  }return;
 }
 if(e.page==Page::FxSessionSettings){
  text(24,187,976,"FX RACK SESSION "+std::to_string(e.fxSessionSlot+1),38,accent);
  const char*name[]={"SESSION TEMPO","LOAD","SAVE"};for(int f=0;f<3;f++){int y=290+f*115;bool selected=e.fxSessionField==f;if(selected)box(24,y-10,976,90,accent);text(48,y,580,name[f],40,selected?ink:dim);if(!f)text(650,y,320,std::to_string(r.tempo)+" BPM",40,selected?ink:0xf0f4fa);}return;
 }
 int ch=std::clamp(e.fxChannel,0,5),slot=std::min(e.fxRow,1),key=ch*2+slot;const auto&s=r.slots[key];
 if(e.page==Page::FxAppliance){
  // No header line: the highlighted strip already identifies the channel.
  int shown=state==1||state==4?std::clamp(channels,1,6):6;int width=(976-16*(shown-1))/shown;
  for(int channel=0;channel<shown;channel++){
   int x=24+channel*(width+16);bool selected=channel==ch,live=state==0||channel<channels,bypassed=r.bypass[channel];
   box(x,190,width,520,selected?accent:0x263342);box(x+2,192,width-4,516,0x0b111a);
   text(x+15,205,120,std::to_string(channel+1),38,live?0xf3f5fa:0x48505b);
   for(int k=0;k<2;k++){bool focus=selected&&e.fxRow==k;int y=268+k*81;
    box(x+8,y,width-16,65,focus?accent:0x192330);auto effect=r.slots[channel*2+k].effect;
    text(x+16,y+13,width-32,effect.type==FxType::None?"EMPTY":fxText(effect),30,focus?ink:live&&!bypassed?0xe8edf1:0x48505b);
   }
   bool volume=selected&&e.fxRow==2,editing=volume&&e.fxLevelEdit;
   // Edit mode (Enter on the level): thick accent frame, tinted body, bold knob
   // and the value on an accent pill, so it is unmistakable from plain focus.
   if(editing){box(x+8,439,width-16,193,accent);box(x+13,444,width-26,183,0x2b2140);}
   else box(x+8,439,width-16,193,volume?0x253046:0x101822);
   int height=r.levels[channel]*120/100;box(x+width/2-5,456,10,124,editing?0x6a5a8a:0x394553);
   if(editing)box(x+width/2-22,573-height,44,12,accent);else box(x+width/2-15,576-height,30,6,volume?accent:0xe8edf1);
   int meter=std::min(120,int(audio.fxMeters[channel].load()*120));box(x+width-32,576-meter,8,meter,accent);
   if(editing){box(x+16,586,width-32,38,accent);text(x+24,590,width-48,std::to_string(r.levels[channel])+"%",30,ink);}
   else text(x+20,590,width-40,std::to_string(r.levels[channel])+"%",30,volume?accent:dim);
   constexpr uint32_t amber=0xffb35c;bool focus=selected&&e.fxRow==3;
   box(x+8,642,width-16,56,focus?accent:bypassed?amber:0x263342);box(x+11,645,width-22,50,bypassed?amber:0x101822);
   text(x+20,655,width-40,"BYPASS",28,bypassed?ink:focus?accent:dim);
  }return;
 }
 text(24,187,976,"CH "+std::to_string(ch+1)+" / FX "+std::to_string(slot+1)+" / "+fxName(s.effect.type),34,accent);
 if(e.page==Page::FxRandomizer){
  // Randomizer settings used by hold X: how many steps trigger, and how much
  // their parameters differ from each other.
  const char*names[]={"DENSITY","COMPLEXITY"};int values[]={s.density,s.complexity};
  std::string notes[]={std::to_string((int(s.length)*s.density+50)/100)+" OF "+std::to_string(s.length)+" STEPS",
   s.complexity==0?"ALL STEPS SHARE ONE SETTING":s.complexity==100?"EVERY STEP INDEPENDENT":"STEP-TO-STEP VARIATION"};
  for(int f=0;f<2;f++){int y=250+f*235;bool selected=e.fxRandomizerField==f;
   box(24,y,976,215,selected?accent:0x263342);box(27,y+3,970,209,0x0b111a);
   text(48,y+22,500,names[f],40,selected?accent:dim);text(700,y+12,280,std::to_string(values[f])+"%",64,selected?0xf3f5fa:dim);
   box(48,y+110,928,16,0x263342);box(48,y+110,928*values[f]/100,16,selected?accent:0x5a6577);
   text(48,y+150,928,notes[f],30,dim);
  }return;
 }
 if(e.page==Page::FxSequencer){
  for(int step=0;step<16;step++){
   int x=24+(step%8)*122,y=275+(step/8)*192;bool selected=step==e.fxStep,within=step<s.length,on=s.steps[step].enabled,playing=audio.fxStep[key]==step;
   box(x,y,112,154,selected?accent:within?0x354257:0x171d28);box(x+3,y+3,106,148,playing?0x665184:on?0x322442:0x0b111a);
   if(playing){box(x+4,y+4,104,14,0xe4cbff);box(x+4,y+136,104,14,0xe4cbff);}
   text(x+14,y+30,88,std::to_string(step+1),44,within?0xe8edf1:0x46505e);
   text(x+14,y+100,88,on?"ON":"--",26,on?accent:dim);
  }return;
 }
 if(e.page==Page::FxPreferences){
  std::string tempo=s.tempo?std::to_string(s.tempo)+" BPM":"SESSION "+std::to_string(r.tempo);if(clock)tempo+=" (EXT)";
  const char*names[]={"LENGTH","CLOCK","TEMPO","DIRECTION","SEQUENCER"};std::string values[]={std::to_string(s.length),clockName(s.clock),tempo,amalthea_direction_name(s.direction),s.sequenced?"ON":"OFF"};
  for(int f=0;f<5;f++){int y=262+f*96;bool selected=e.fxPreference==f;if(selected)box(24,y-8,976,88,accent);text(48,y,450,names[f],40,selected?ink:dim);text(490,y,476,values[f],40,selected?ink:0xe8edf1);}return;
 }
 int count=1+insertParameterCount(s.effect.type,e.fxPage,s.settings),base=e.fxParameter/6*6;
 text(24,235,976,(e.fxDefaultEdit?std::string("DEFAULT PARAMETERS"):"STEP "+std::to_string(e.fxStep+1)+" OVERRIDES")+((s.effect.type==FxType::ChowMatrix||s.effect.type==FxType::Frequalizer||s.effect.type==FxType::Biztortion)?" / PAGE "+std::to_string(e.fxPage+1):""),28,dim);
 for(int f=base;f<std::min(base+6,count);f++){
  int y=295+(f-base)*72;bool selected=f==e.fxParameter;uint32_t color=selected?ink:0xe8edf1;if(selected)box(24,y-5,976,65,accent);
  std::string name,value;fxParameterDisplay(s,e.fxDefaultEdit,e.fxStep,e.fxPage,f,name,value);
  text(40,y+6,500,name,40,color);text(560,y+6,420,value,40,color);
 }
}
}
