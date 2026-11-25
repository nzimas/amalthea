#include "fx_rack.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
namespace amalthea {
int fxParameterKey(FxType type,int page,int field){
 if(!field)return 0;
 if(type==FxType::ChowMatrix)return page?6+(page-1)*14+field-1:field;
 if(type==FxType::Biztortion)return field<3?field:3+page*11+field-3;
 if(type==FxType::Frequalizer)return field<3?field:3+page*5+field-3;
 return field;
}
void resolveFxStep(const FxRackSlot&s,int step,Effect&e,InsertSettings&settings){
 e=s.effect;settings=s.settings;if(step<0||step>=s.length||!s.sequenced)return;
 const auto&lock=s.steps[step];if(!lock.enabled)return;
 int pages=e.type==FxType::ChowMatrix?9:e.type==FxType::Frequalizer?6:e.type==FxType::Biztortion?8:1;
 for(int page=0;page<pages;page++)for(int f=0;f<insertParameterCount(e.type,page,settings);f++){
  int key=fxParameterKey(e.type,page,f);if(key<FxLocks&&lock.locks[key])insertParameter(e,settings,f,page)=lock.values[key];
 }
}
void randomizeFxParameters(Effect&e,InsertSettings&settings,uint32_t&seed){
 auto type=e.type;if(type==FxType::None)return;
 // Native effects draw each value inside its own playable range.
 if(auto*n=nativeInfo(type)){
  e.value=uint8_t(std::clamp(int(n->mix)-20+int(randomNext(seed)%31),40,100));
  for(int i=0;i<n->count;i++){auto&p=n->params[i];int lo=std::min(p.lo,p.hi),hi=std::max(p.lo,p.hi);int v=lo+int(randomNext(seed)%uint32_t(hi-lo+1));
   int f=i+1,step=insertParameterStep(type,f,0,settings);if(step>1)v=std::clamp(int(std::lround(float(v)/step))*step,0,100);settings.native[nativeIndex(type)][i]=uint8_t(v);}
  return;
 }
 e.value=55+randomNext(seed)%46;
 int pages=type==FxType::ChowMatrix?9:type==FxType::Frequalizer?6:type==FxType::Biztortion?8:1;
 for(int page=0;page<pages;page++)for(int f=1;f<insertParameterCount(type,page,settings);f++){
  auto name=std::string(insertParameterName(type,f,page,settings));int value=randomNext(seed)%101;
  int step=insertParameterStep(type,f,page,settings);if(step>1)value=std::clamp(int(std::lround(float(value)/step))*step,0,100);
  if(name.find("FEEDBACK")!=std::string::npos)value=std::min(value,75);
  if(name.find("LEVEL")!=std::string::npos||name=="INPUT TRIM")value=40+randomNext(seed)%26;
  if(name=="MODULE")value=page==0?20:int(randomNext(seed)%5)*20;
  if(name=="FREEZE")value=0;
  if(name=="NOISE")value%=25;
  insertParameter(e,settings,f,page)=value;
 }
 // FLANGER and PHASER draw inside their sweeping ranges, so a random draw is always a
 // clearly moving flange or phase and never a frozen comb or an audio-rate buzz.
 auto span=[&](uint8_t&v,int lo,int hi){v=uint8_t(lo+randomNext(seed)%(hi-lo+1));};
 if(type==FxType::Flanger){auto&f=settings.flanger;e.value=70+randomNext(seed)%31;
  span(f[2],25,63);span(f[3],45,100);span(f[5],22,60);span(f[7],30,80);span(f[8],0,40);span(f[9],35,75);span(f[10],60,100);}
 if(type==FxType::Phaser){auto&p=settings.phaser;e.value=70+randomNext(seed)%31;
  span(p[0],30,80);span(p[1],35,90);span(p[2],45,100);span(p[3],15,55);span(p[4],50,100);p[5]=0;span(p[6],25,75);span(p[10],15,100);}
}
void boundInsertCost(Effect&e,InsertSettings&settings){
 if(e.type==FxType::QDelay){
  static const char*drawn[]={"MODE","SYNC L","SYNC R","RATE L","RATE R","RATE SYNC L","RATE SYNC R","FEEDBACK","PIPO WIDTH","HAAS WIDTH","STEREO","SWING","FEEL","MOD DEPTH","MOD RATE","TAPE AMT","FLUTTER RATE","FLUTTER DEPTH","WOW RATE","WOW DEPTH","LINK"};
  InsertSettings plain{};
  for(int f=1;f<insertParameterCount(e.type,0,settings);f++){
   std::string name=insertParameterName(e.type,f,0,settings);bool keep=false;for(auto d:drawn)keep|=name==d;
   if(!keep){Effect a=e,b=e;insertParameter(a,settings,f,0)=insertParameter(b,plain,f,0);}
  }
 }
 if(e.type==FxType::ChowMatrix){settings.matrix[0]=InsertSettings{}.matrix[0];for(int node=2;node<8;node++)settings.nodes[node][0]=0;}
}
void randomizeFxSlot(FxRack&r,int slot,bool parametersOnly,const DeviceSettings&device){
 if(slot<0||slot>=FxSlots)return;
 auto&s=r.slots[slot];
 if(!parametersOnly){std::array<FxType,64> types{};int n=0;for(auto type:selectableEffects())if(type!=FxType::None&&device.insertEnabled(type))types[n++]=type;if(!n){s.effect={};return;}s.effect.type=types[randomNext(r.seed)%n];s.settings=InsertSettings{};for(auto&step:s.steps)step={};}
 randomizeFxParameters(s.effect,s.settings,r.seed);boundInsertCost(s.effect,s.settings);
}
void fxParameterDisplay(const FxRackSlot&s,bool defaultEdit,int step,int page,int row,std::string&name,std::string&value){
 if(row==0){name=defaultEdit?"EFFECT":"TRIGGER";value=defaultEdit?fxName(s.effect.type):s.steps[step].enabled?"ON":"OFF";return;}
 int field=row-1;name=insertParameterName(s.effect.type,field,page,s.settings);Effect effect=s.effect;auto settings=s.settings;int v=insertParameter(effect,settings,field,page);
 int k=fxParameterKey(s.effect.type,page,field);bool locked=!defaultEdit&&k<FxLocks&&s.steps[step].locks[k];
 if(!defaultEdit&&!locked){value="INHERIT";return;}
 if(locked)v=s.steps[step].values[k];
 value=insertParameterDescription(s.effect.type,field,page,s.settings,v);
}
int fxSlotTempo(const FxRack&r,int slot){
 int t=slot>=0&&slot<FxSlots?r.slots[slot].tempo:0;return t?t:r.tempo;
}
bool randomizeFxSequence(FxRack&r,int slot){
 if(slot<0||slot>=FxSlots)return false;auto&s=r.slots[slot];
 if(s.effect.type==FxType::None)return false;
 std::array<int,16>order{};for(int i=0;i<s.length;i++){order[i]=i;s.steps[i]={};}
 for(int i=s.length-1;i>0;i--)std::swap(order[i],order[randomNext(r.seed)%(i+1)]);
 int count=(int(s.length)*s.density+50)/100;
 // Complexity 0: every generated step shares one random parameter set.
 // Complexity 100: every step is drawn independently. In between, each step is
 // pulled from the shared set towards its own random set by that fraction.
 Effect base=s.effect;InsertSettings baseSettings=s.settings;randomizeFxParameters(base,baseSettings,r.seed);boundInsertCost(base,baseSettings);
 int pages=base.type==FxType::ChowMatrix?9:base.type==FxType::Frequalizer?6:base.type==FxType::Biztortion?8:1;
 for(int i=0;i<count;i++){
  auto&step=s.steps[order[i]];step.enabled=true;
  Effect own=s.effect;InsertSettings ownSettings=s.settings;randomizeFxParameters(own,ownSettings,r.seed);boundInsertCost(own,ownSettings);
  for(int page=0;page<pages;page++)for(int f=0;f<insertParameterCount(base.type,page,baseSettings);f++){
   int key=fxParameterKey(base.type,page,f);if(key>=FxLocks)continue;
   int from=insertParameter(base,baseSettings,f,page),to=insertParameter(own,ownSettings,f,page);
   int value=from+int(std::lround((to-from)*s.complexity/100.f));
   int quantum=insertParameterStep(base.type,f,page,baseSettings);
   if(quantum>1)value=int(std::lround(float(value)/quantum))*quantum;
   step.locks[key]=true;step.values[key]=uint8_t(std::clamp(value,0,100));
  }
 }
 s.sequenced=true;
 // Generating is an explicit request to audition the sequence. External Stop still wins.
 r.running=true;
 return true;
}
void MidiClock::byte(uint8_t b,double now){
 if(b==0xfa){ticks=0;running=true;stopped=false;reset=true;return;}
 if(b==0xfb){running=true;stopped=false;return;}if(b==0xfc){running=false;stopped=true;return;}
 if(b!=0xf8)return;
 if(!stopped)running=true;
 // A gap of a quarter second: a new clock; the count starts again.
 if(last>0&&now-last>.25){count=0;settle=0;locked=false;tempo=0;}
 last=now;ticks++;
 times[head]=now;head=(head+1)%Window;count=std::min(count+1,Window);
 if(count<25)return;   // a beat at least
 double first=times[(head-count+Window)%Window],span=now-first;if(span<=0)return;
 double estimate=60*(count-1)/(24*span);
 // A tempo change on the FM1: the last beat no longer agrees with the window. Until a whole
 // beat of pulses has come at the new tempo, the count covers only the last beat.
 double beat=now-times[(head-25+Window)%Window],recent=beat>0?60/beat:estimate;
 if(count>25&&std::abs(recent-estimate)>std::max(2.0,estimate*.03))settle=24;
 if(settle>0){settle--;count=25;estimate=recent;}
 bpm=estimate;locked=true;
 if(!tempo||std::abs(bpm-tempo)>.6)tempo=int(std::lround(bpm));
}
void MidiClock::expire(double now){if(last&&now-last>.75){locked=false;running=false;count=0;tempo=0;}}
bool MidiTrackPan::byte(uint8_t b){
 if(b>=0xf8)return false;                       // real time: between any bytes, no effect
 if(b==0xf0){sysex=true;status=0;return false;}
 if(b>=0x80){sysex=false;status=b<0xf0?b:0;second=false;return false;}   // a status (system common ends running status)
 if(sysex||!status)return false;
 int kind=status>>4,need=kind==0xc||kind==0xd?1:2;
 if(need==1)return false;
 if(!second){data=b;second=true;return false;}
 second=false;   // running status: the next data byte starts a new message
 int channel=status&15;
 if(kind!=0xb||data!=10||channel>=6)return false;
 if(pan[channel]==int8_t(b))return false;
 pan[channel]=int8_t(b);return true;
}
float midiPanPosition(int v){return std::clamp(.5f+.5f*(v-64)/63.f,0.f,1.f);}
const char*fm1Name(int f){static const char*n[]={"FM1","BIGORNA","TONITOR","NORILSK","LILLIPUT"};return n[std::clamp(f,0,4)];}
int fm1Firmware(const std::string&s){std::string v=s;for(auto&c:v)c=std::tolower(static_cast<unsigned char>(c));if(v.find("bigorna")!=v.npos)return 1;if(v.find("tonitor")!=v.npos)return 2;if(v.find("norilsk")!=v.npos)return 3;if(v.find("lilliput")!=v.npos||v.find("liliput")!=v.npos)return 4;return 0;}
UsbMapping fm1Mapping(int firmware,int channels){if(channels==2)return {1,0,true};if(channels==3)return {3,0,false};if(channels==6)return {6,0,false};if(channels==8&&(firmware==1||firmware==2||firmware==4))return {6,2,false};return {};}
void Editor::fxAction(Action a){
 if(!fxRack)fxRack=std::make_shared<FxRack>();auto&r=*fxRack;
 if(a==Action::Home){page=Page::FxAppliance;fxLevelEdit=false;return;}
 if(a==Action::RandomizeColumn&&page==Page::FxAppliance){
  // Hold X / Shift+hold X act on the focused slot only; level and bypass rows have no slot.
  if(fxRow>1){notice="CHOOSE AN FX SLOT";++noticeSerial;return;}
  auto&slot=r.slots[fxChannel*2+fxRow];
  if(shift&&slot.effect.type==FxType::None){notice="CHOOSE AN EFFECT FIRST";++noticeSerial;return;}
  randomizeFxSlot(r,fxChannel*2+fxRow,shift,device);revision++;
  // Brief overlay naming the result, since the strip shows only an abbreviation.
  notice=slot.effect.type==FxType::None?"NO EFFECTS ENABLED":fxName(slot.effect.type);noticeMs=1500;++noticeSerial;return;
 }
 if(a==Action::Play){r.running=!r.running;revision++;return;}
 if(a==Action::Patterns||a==Action::Session){page=Page::FxSessions;return;}
 if(a==Action::Mixer){page=Page::FxAppliance;return;}
 if(page==Page::FxSessions){
  if(a==Action::Left)fxSessionSlot=(fxSessionSlot+63)%64;if(a==Action::Right)fxSessionSlot=(fxSessionSlot+1)%64;
  if(a==Action::Up)fxSessionSlot=(fxSessionSlot+56)%64;if(a==Action::Down)fxSessionSlot=(fxSessionSlot+8)%64;
  if(a==Action::Enter){page=Page::FxSessionSettings;fxSessionField=0;}return;
 }
 if(page==Page::FxSessionSettings){
  if(a==Action::Cancel||a==Action::TrackSettings){page=Page::FxSessions;return;}
  if(a==Action::Up||a==Action::Down)fxSessionField=(fxSessionField+(a==Action::Down?1:2))%3;
  if(fxSessionField==0&&(a==Action::Left||a==Action::Right)){r.tempo=std::clamp(int(r.tempo)+(a==Action::Right?1:-1)*(shift?10:1),30,300);revision++;}
  if(a==Action::Enter&&fxSessionField>0)fxSessionRequest=fxSessionField;return;
 }

 auto&s=r.slots[fxChannel*2+std::min(fxRow,1)];int pages=s.effect.type==FxType::ChowMatrix?9:s.effect.type==FxType::Frequalizer?6:s.effect.type==FxType::Biztortion?8:1;
 if(page==Page::FxSequencer||page==Page::FxRandomizer){
  // Y (or tap X) toggles the randomizer settings page.
  if(a==Action::RandomizerOptions||a==Action::Master){page=page==Page::FxSequencer?Page::FxRandomizer:Page::FxSequencer;return;}
  if(a==Action::RandomizeColumn){if(randomizeFxSequence(r,fxChannel*2+std::min(fxRow,1))){revision++;page=Page::FxSequencer;}else {notice="CHOOSE AN EFFECT FIRST";++noticeSerial;}return;}
 }
 if(page==Page::FxRandomizer){
  if(a==Action::Enter||a==Action::Cancel||a==Action::TrackSettings){page=Page::FxSequencer;return;}
  if(a==Action::Up||a==Action::Down){fxRandomizerField=1-fxRandomizerField;return;}
  if(a==Action::Left||a==Action::Right){auto&v=fxRandomizerField?s.complexity:s.density;v=std::clamp(int(v)+(a==Action::Right?1:-1)*(shift?10:1),0,100);revision++;}return;
 }
 if(a==Action::RandomizerOptions||a==Action::RandomizeColumn)return;
 if(page==Page::FxAppliance){
  if(a==Action::Enter){if(fxRow==3){r.bypass[fxChannel]=!r.bypass[fxChannel];revision++;}else if(fxRow==2)fxLevelEdit=!fxLevelEdit;else {page=Page::FxSequencer;fxStep=0;}return;}
  if(fxLevelEdit){if(a==Action::Up||a==Action::Down)r.levels[fxChannel]=std::clamp(int(r.levels[fxChannel])+(a==Action::Up?1:-1),0,100);return;}
  if(a==Action::Left||a==Action::Right)fxChannel=(fxChannel+(a==Action::Right?1:fxVisibleChannels-1))%fxVisibleChannels;
  if(a==Action::Up||a==Action::Down)fxRow=(fxRow+(a==Action::Down?1:3))%4;
  return;
 }
 if(page==Page::FxSequencer){
  if(a==Action::ColumnSettings){page=Page::FxPreferences;fxPreference=0;return;}
  if(a==Action::TrackSettings){page=Page::FxParameters;fxDefaultEdit=true;fxParameter=fxPage=0;return;}
  if(a==Action::Enter){page=Page::FxParameters;fxDefaultEdit=false;fxParameter=fxPage=0;return;}
  if(a==Action::Left||a==Action::Right)fxStep=(fxStep+(a==Action::Right?1:15))%16;
  if(a==Action::Up||a==Action::Down)fxStep=(fxStep+8)%16;
  if(a==Action::PrevTrack||a==Action::NextTrack){fxRow=1-fxRow;fxStep=0;}
  return;
 }
 if(page==Page::FxPreferences){
  if(a==Action::ColumnSettings||a==Action::Cancel||a==Action::TrackSettings){page=Page::FxSequencer;return;}
  if(a==Action::Up||a==Action::Down)fxPreference=(fxPreference+(a==Action::Down?1:4))%5;
  int d=a==Action::Right?1:a==Action::Left?-1:0;
  if(d){if(fxPreference==0)s.length=std::clamp(int(s.length)+d,1,16);if(fxPreference==1)s.clock=std::clamp(int(s.clock)+d,0,5);
   // Internal tempo per slot: 0 follows the session tempo; leaving it starts from that tempo.
   if(fxPreference==2){int t=s.tempo?s.tempo:r.tempo;if(!s.tempo&&d<0)t=0;else if(s.tempo){t+=d*(shift?10:1);if(t<30)t=0;}s.tempo=uint16_t(t?std::min(t,300):0);}
   if(fxPreference==3)s.direction=(s.direction+d+Directions)%Directions;if(fxPreference==4)s.sequenced=d>0;revision++;}return;
 }
 if(a==Action::TrackSettings||a==Action::Cancel){page=Page::FxSequencer;return;}
 if(a==Action::PrevTrack||a==Action::NextTrack){fxPage=(fxPage+(a==Action::NextTrack?1:pages-1))%pages;fxParameter=0;return;}
 int count=1+insertParameterCount(s.effect.type,fxPage,s.settings);
 if(a==Action::Up||a==Action::Down)fxParameter=(fxParameter+(a==Action::Down?1:count-1))%count;
 int d=a==Action::Right?1:a==Action::Left?-1:0;if(!d)return;
 if(!fxParameter){
  if(fxDefaultEdit){const auto&list=selectableEffects();auto at=std::find(list.begin(),list.end(),s.effect.type);int i=at==list.end()?0:int(at-list.begin());s.effect.type=list[(i+d+list.size())%list.size()];s.effect.value=std::max<uint8_t>(80,defaultFxMix(s.effect.type));fxPage=0;s.settings=InsertSettings{};for(auto&step:s.steps)step={};}
  else {s.sequenced=true;s.steps[fxStep].enabled=d>0;}
 }else{
  int field=fxParameter-1;int key=fxParameterKey(s.effect.type,fxPage,field);auto &v=insertParameter(s.effect,s.settings,field,fxPage);
  int step=insertParameterStep(s.effect.type,field,fxPage,s.settings);
  if(fxDefaultEdit)v=std::clamp(int(v)+d*step,0,100);
  else if(key<FxLocks){auto &lock=s.steps[fxStep];if(shift&&d<0)lock.locks[key]=false;else {int value=lock.locks[key]?lock.values[key]:v;lock.values[key]=std::clamp(value+d*step,0,100);lock.locks[key]=true;lock.enabled=true;s.sequenced=true;}}
 }
 // Large readable popup of the value just set; repeated presses keep it up.
 fxParameterDisplay(s,fxDefaultEdit,fxStep,fxPage,fxParameter,notice,noticeDetail);noticeMs=1000;noticeDetailSerial=++noticeSerial;
 revision++;
}
}
namespace amalthea {
namespace {
uint32_t rackCrc(const std::vector<uint8_t>&b,size_t count){uint32_t c=~0u;for(size_t i=0;i<count;i++){c^=b[i];for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
}
bool saveFxRack(const FxRack&r,const std::string&path,std::string&error){
 Project p=blankProject();for(int c=0;c<6;c++)for(int k=0;k<2;k++){auto&s=r.slots[c*2+k];p.tracks[c].steps[0].fx[k]=s.effect;p.tracks[c].inserts[k]=s.settings;}
 auto project=encodeProject(p);std::vector<uint8_t>b={'A','F','X','R',5};auto put=[&](uint32_t v,int n){while(n--){b.push_back(v&255);v>>=8;}};
 put(project.size(),4);b.insert(b.end(),project.begin(),project.end());put(r.tempo,2);put(r.seed,4);for(auto v:r.levels)put(v,1);
 for(auto&s:r.slots){put(s.length,1);put(s.clock,1);put(s.direction,1);put(s.sequenced,1);put(s.density,1);for(auto&step:s.steps){put(step.enabled,1);for(int byte=0;byte<16;byte++){int v=0;for(int bit=0;bit<8;bit++)v|=int(step.locks[byte*8+bit])<<bit;put(v,1);}for(auto v:step.values)put(v,1);}}
 for(auto v:r.bypass)put(v,1);
 for(auto&s:r.slots){put(s.complexity,1);put(s.tempo,2);}
 put(rackCrc(b,b.size()),4);std::error_code ec;auto dir=std::filesystem::path(path).parent_path();if(!dir.empty())std::filesystem::create_directories(dir,ec);if(ec){error="CANNOT CREATE FX BANK";return false;}auto tmp=path+".tmp";FILE*f=fopen(tmp.c_str(),"wb");if(!f){error="CANNOT OPEN FX SESSION";return false;}bool ok=fwrite(b.data(),1,b.size(),f)==b.size()&&fflush(f)==0&&fsync(fileno(f))==0;if(fclose(f))ok=false;if(ok)ok=rename(tmp.c_str(),path.c_str())==0;if(!ok){remove(tmp.c_str());error="FX SAVE FAILED";}return ok;
}
bool loadFxRack(FxRack&out,const std::string&path,std::string&error){
 std::ifstream f(path,std::ios::binary);if(!f){error="NO FX SESSION";return false;}f.seekg(0,std::ios::end);auto size=f.tellg();if(size<13||size>300000){error="INVALID FX SIZE";return false;}f.seekg(0);std::vector<uint8_t>b(size);if(!f.read(reinterpret_cast<char*>(b.data()),size))return false;
 size_t at=5;bool ok=true;auto get=[&](int n){uint32_t v=0;for(int i=0;i<n;i++){if(at>=b.size()){ok=false;return v;}v|=uint32_t(b[at++])<<(i*8);}return v;};
 if(std::string(b.begin(),b.begin()+4)!="AFXR"||(b[4]<1||b[4]>5)){error="INVALID FX HEADER";return false;}
 auto n=get(4);if(n>200000||n>b.size()-at){error="INVALID FX PROFILE";return false;}Project p;std::vector<uint8_t>project(b.begin()+at,b.begin()+at+n);at+=n;if(!decodeProject(p,project,error))return false;
 FxRack r;if(b[4]>=2)r.tempo=get(2);ok&=r.tempo>=30&&r.tempo<=300;r.seed=get(4);for(auto&v:r.levels){v=get(1);ok&=v<=100;}
 for(int k=0;k<12;k++){auto&s=r.slots[k];s.effect=p.tracks[k/2].steps[0].fx[k%2];s.settings=p.tracks[k/2].inserts[k%2];s.length=get(1);s.clock=get(1);s.direction=get(1);auto seq=get(1);s.sequenced=seq;if(b[4]>=3)s.density=get(1);ok&=s.density<=100;ok&=s.length>=1&&s.length<=16&&s.clock<6&&s.direction<Directions&&seq<2;
  for(auto&step:s.steps){auto enabled=get(1);step.enabled=enabled;ok&=enabled<2;for(int byte=0;byte<16;byte++){auto v=get(1);for(int bit=0;bit<8;bit++)step.locks[byte*8+bit]=(v>>bit)&1;}for(auto&v:step.values){v=get(1);ok&=v<=100;}}
 }
 if(b[4]>=4)for(auto&v:r.bypass){auto stored=get(1);v=stored;ok&=stored<2;}
 if(b[4]>=5)for(auto&s:r.slots){s.complexity=get(1);s.tempo=get(2);ok&=s.complexity<=100&&(s.tempo==0||(s.tempo>=30&&s.tempo<=300));}
 auto crc=rackCrc(b,b.size()-4);auto stored=get(4);if(!ok||at!=b.size()||crc!=stored){error="INVALID FX CONTENT";return false;}r.reset=out.reset+1;out=std::move(r);return true;
}
}

namespace amalthea {
std::string fxSessionPath(const std::string&root,int slot){if(slot<0||slot>=64)return {};char name[32];snprintf(name,sizeof name,"slot-%02d.amalthea",slot+1);return (std::filesystem::path(root)/name).string();}
std::array<uint8_t,64>scanFxSessions(const std::string&root){std::array<uint8_t,64>state{};for(int i=0;i<64;i++){auto path=fxSessionPath(root,i);std::error_code ec;if(std::filesystem::exists(path,ec)){FxRack rack;std::string err;state[i]=loadFxRack(rack,path,err)?1:2;}}return state;}
}
