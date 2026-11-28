#include "performance.hpp"
#include <memory>
#include "fx_rack.hpp"
#include "lifeblood.hpp"
#include "native_fx.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
extern "C" {
void amalthea_patch(int, uint32_t, int32_t *);
int amalthea_patch_movable(int, const int32_t *, uint32_t);
int32_t amalthea_patch_move(int, const int32_t *, uint32_t, int32_t);
const char *amalthea_patch_parameter_name(int, uint32_t);
}
namespace amalthea {
namespace {
int between(uint32_t&s,int lo,int hi){return lo+int(randomNext(s)%uint32_t(hi-lo+1));}
float unit(uint32_t&s){return float(randomNext(s)%1000001u)/1000000.f;}
bool chance(uint32_t&s,float p){return unit(s)<p;}
using F=LifebloodFamily;
// COMBOS (after Vascular): fixed stage orders, each stage a family to draw from and the
// chance it is there at all. The parts are drawn at random every time.
struct ComboStage { F family; float chance; };
struct Combo { const char *name; int count; ComboStage stage[PerfStages]; };
const Combo Combos[]={
  {"GRIND",  3,{{F::Drive,1},{F::Color,.85f},{F::Motion,.6f}}},
  {"ACID",   3,{{F::Color,1},{F::Drive,1},{F::Motion,.5f}}},
  {"CAVERN", 3,{{F::Texture,.7f},{F::Time,.7f},{F::Space,1}}},
  {"HAUNT",  3,{{F::Texture,1},{F::Motion,1},{F::Space,.7f}}},
  {"LURCH",  3,{{F::Rhythm,1},{F::Motion,.6f},{F::Time,.5f}}},
  {"CORRODE",4,{{F::Drive,1},{F::Rhythm,.7f},{F::Color,1},{F::Motion,.4f}}},
};
constexpr int ComboCount=int(sizeof Combos/sizeof Combos[0]);
int pages(FxType t){return t==FxType::ChowMatrix?9:t==FxType::Frequalizer?6:t==FxType::Biztortion?8:1;}
// A stage's parameters (as LIFEBLOOD draws a node): a playable random set, a clearly
// audible mix by family, finite repeats, never frozen, within the Brick's budget.
void drawStage(Effect&e,InsertSettings&s,uint32_t&seed){
  s=InsertSettings{};randomizeFxParameters(e,s,seed);
  auto type=e.type;auto f=lifebloodFamily(type);
  if(type==FxType::Flanger||type==FxType::Phaser){}
  else if(f==F::Drive)e.value=uint8_t(between(seed,50,80));
  else if(f==F::Motion||f==F::Texture)e.value=uint8_t(between(seed,60,90));
  else if(f==F::Rhythm)e.value=uint8_t(between(seed,85,100));
  else e.value=uint8_t(between(seed,70,100));
  if(type==FxType::QDelay||type==FxType::ChowMatrix)
    for(int page=0;page<pages(type);page++)for(int fld=1;fld<insertParameterCount(type,page,s);fld++)
      if(std::string(insertParameterName(type,fld,page,s)).find("FEEDBACK")!=std::string::npos){auto&v=insertParameter(e,s,fld,page);v=std::min<uint8_t>(v,60);}
  if(type==FxType::Clouds)s.clouds[7]=0;
  boundInsertCost(e,s);
}
bool tracksHolding(const Project&p,int t){
  const auto&k=p.tracks[t];if(k.mute)return false;
  for(int r=0;r<columnLength(k,0);r++)if(k.steps[r].note>=0)return true;
  return false;
}
// A parameter an LFO may move: continuous, not a switch, level or feedback.
bool movable(FxType type,int field,int page,const InsertSettings&s){
  if(steppedInsertParameter(type,field,page,s))return false;
  std::string n=insertParameterName(type,field,page,s);
  for(const char*no:{"MODULE","FREEZE","LEVEL","TRIM","FEEDBACK","MODE","SYNC","TYPE","OUTPUT","GAIN","VOLUME","MIX","DRY","WET","SOLO"})
    if(n.find(no)!=std::string::npos)return false;
  return !n.empty();
}
}
const char *perfComboName(int c){return Combos[std::clamp(c,0,ComboCount-1)].name;}

std::string perfChain(Performance&p,const Project&project,const DeviceSettings&device){
  if(p.chainOn){p.chainOn=false;return "CHAIN BYPASSED";}
  // Targets: one to three of the tracks holding notes, each on a stretch of its NOTE column.
  std::vector<int> holding;for(int t=0;t<Tracks;t++)if(tracksHolding(project,t))holding.push_back(t);
  if(holding.empty())return "NO TRACK HOLDS NOTES";
  auto&seed=p.seed;
  for(size_t i=holding.size();i>1;i--)std::swap(holding[i-1],holding[randomNext(seed)%i]);
  p.targets=std::min<int>(int(holding.size()),between(seed,1,PerfTargets));
  p.target.fill({});
  for(int k=0;k<p.targets;k++){
    int t=holding[size_t(k)],length=columnLength(project.tracks[t],0);
    p.target[size_t(k)]={int8_t(t),uint8_t(between(seed,0,length-1)),uint8_t(std::clamp(between(seed,length/4,length*3/4),1,length))};
  }
  // The chain: a combo, each stage drawn from its family within the budget per track.
  const int budget=PerfBudget/p.targets;
  for(int attempt=0;attempt<8;attempt++){
    p.combo=uint8_t(between(seed,0,ComboCount-1));const auto&combo=Combos[p.combo];
    int used=0;p.stages=0;
    for(int i=0;i<combo.count;i++){
      const auto&stage=combo.stage[i];
      bool mandatory=stage.chance>=1;
      if(!mandatory&&!chance(seed,stage.chance))continue;
      std::vector<FxType> pool;
      for(auto t:selectableEffects())if(t!=FxType::None&&t!=FxType::Random&&device.insertEnabled(t)&&lifebloodFamily(t)==stage.family&&used+lifebloodCost(t)<=budget){
        bool again=false;for(int j=0;j<p.stages;j++)again|=p.effect[size_t(j)].type==t;
        if(!again)pool.push_back(t);
      }
      if(pool.empty())continue;
      auto&e=p.effect[size_t(p.stages)];e={pool[randomNext(seed)%pool.size()],0};
      drawStage(e,p.base[size_t(p.stages)],seed);
      used+=lifebloodCost(e.type);p.stages++;
    }
    if(p.stages>=2)break;
  }
  if(p.stages<1)return "NO EFFECT FITS THE BUDGET";
  p.drawn=p.base;p.paramsOn=false;
  p.chainOn=true;++p.generation;
  if(p.fxLfoOn){p.fxLfoOn=false;perfFxLfos(p);}   // the LFOs follow the new chain
  std::string text=std::string(perfComboName(p.combo))+":";
  for(int i=0;i<p.stages;i++)text+=std::string(i?" > ":" ")+fxName(p.effect[size_t(i)].type);
  return text;
}
std::string perfParams(Performance&p){
  if(!p.stages)return "PRESS X FOR A CHAIN FIRST";
  if(p.paramsOn){p.paramsOn=false;return "CHAIN PARAMETERS RESTORED";}
  for(int i=0;i<p.stages;i++){Effect e=p.effect[size_t(i)];drawStage(e,p.drawn[size_t(i)],p.seed);}
  p.paramsOn=true;return "NEW CHAIN PARAMETERS";
}
std::string perfFxLfos(Performance&p){
  if(p.fxLfoOn){p.fxLfoOn=false;return "CHAIN LFOS OFF";}
  if(!p.stages)return "PRESS X FOR A CHAIN FIRST";
  auto&seed=p.seed;int placed=0;
  for(auto&l:p.lfo){
    l={};
    for(int attempt=0;attempt<24&&l.stage<0;attempt++){
      int stage=between(seed,0,p.stages-1);auto type=p.effect[size_t(stage)].type;const auto&s=p.base[size_t(stage)];
      int page=between(seed,0,pages(type)-1),count=insertParameterCount(type,page,s);if(count<2)continue;
      int field=between(seed,1,count-1);if(!movable(type,field,page,s))continue;
      bool taken=false;for(auto&o:p.lfo)taken|=&o!=&l&&o.stage==stage&&o.field==field&&o.page==page;
      if(taken)continue;
      l.stage=int8_t(stage);l.field=uint8_t(field);l.page=uint8_t(page);
    }
    if(l.stage<0)continue;
    l.hold=chance(seed,.35f);
    l.rate=.05f*std::pow(2.f,unit(seed)*6.3f);   // 0.05 .. 4 Hz
    l.depth=.1f+.35f*unit(seed);
    l.phase=unit(seed);l.held=unit(seed)*2-1;placed++;
  }
  if(!placed)return "NO PARAMETER TO MOVE";
  p.fxLfoOn=true;return std::to_string(placed)+" LFOS ON THE CHAIN";
}
float perfLfoValue(const PerfLfo&l){return l.hold?l.held:std::sin(6.2831853f*l.phase);}
float perfLfoValue(const PerfEngineLfo&l){return l.hold?l.held:std::sin(6.2831853f*l.phase);}
std::string perfEngineLfos(Performance&p,const Project&project){
  if(p.engineLfoOn){p.engineLfoOn=false;return "INSTRUMENT LFOS OFF";}
  std::vector<int> holding;for(int t=0;t<Tracks;t++)if(tracksHolding(project,t))holding.push_back(t);
  if(holding.empty())return "NO TRACK HOLDS NOTES";
  auto&seed=p.seed;
  for(size_t i=holding.size();i>1;i--)std::swap(holding[i-1],holding[randomNext(seed)%i]);
  p.instruments=std::min<int>(int(holding.size()),between(seed,1,PerfTargets));p.instrument.fill({});
  for(int k=0;k<p.instruments;k++){
    auto&in=p.instrument[size_t(k)];int t=holding[size_t(k)];const auto&id=trackPatch(project,t,project.tracks[t].instrument);
    in.track=int8_t(t);in.slot=uint8_t(project.tracks[t].instrument);in.engine=id.engine;in.seed=id.seed;
    if(id.engine<3)amalthea_patch(id.engine,id.seed,in.base.data());
  }
  int placed=0;
  for(auto&l:p.engineLfo){
    l={};
    for(int attempt=0;attempt<32&&l.target<0;attempt++){
      int k=between(seed,0,p.instruments-1);const auto&in=p.instrument[size_t(k)];
      if(in.engine>=3){   // a recipe engine: one LFO through its variations
        bool has=false;for(auto&o:p.engineLfo)has|=&o!=&l&&o.target==k;
        if(has)continue;
        l.target=int8_t(k);l.morph=true;l.variations=randomNext(seed)|1u;
      } else {
        int j=between(seed,0,PerfParameters-1);
        if(!amalthea_patch_movable(in.engine,in.base.data(),uint32_t(j)))continue;
        bool taken=false;for(auto&o:p.engineLfo)taken|=&o!=&l&&o.target==k&&!o.morph&&o.param==j;
        if(taken)continue;
        l.target=int8_t(k);l.param=uint8_t(j);
      }
    }
    if(l.target<0)continue;
    l.hold=chance(seed,.35f);
    l.rate=.05f*std::pow(2.f,unit(seed)*6.3f);
    l.depth=l.morph?.4f+.6f*unit(seed):.25f+.55f*unit(seed);
    l.phase=unit(seed);l.held=unit(seed)*2-1;placed++;
  }
  if(!placed)return "NO INSTRUMENT PARAMETER TO MOVE";
  p.engineLfoOn=true;
  std::string text=std::to_string(placed)+" LFOS ON";
  for(int k=0;k<p.instruments;k++)text+=" T"+std::to_string(p.instrument[size_t(k)].track+1);
  return text;
}
std::string perfEngineLfoName(const Performance&p,const PerfEngineLfo&l){
  if(l.target<0)return "";
  const auto&in=p.instrument[size_t(l.target)];
  return "T"+std::to_string(in.track+1)+"  "+(l.morph?std::string("VARIATIONS"):std::string(amalthea_patch_parameter_name(in.engine,l.param)));
}
PerfFrame perfFrame(Performance&p,double seconds){
  PerfFrame f;
  for(auto&l:p.lfo){
    if(l.stage<0)continue;
    l.phase+=float(seconds)*l.rate;
    if(l.phase>=1){l.phase-=std::floor(l.phase);uint32_t s=p.seed^uint32_t(l.field*977+l.page*131+l.stage*7919);s^=uint32_t(l.held*1e6f);l.held=float(randomNext(s)%2001u)/1000.f-1;}
  }
  for(auto&l:p.engineLfo){
    if(l.target<0)continue;
    l.phase+=float(seconds)*l.rate;
    if(l.phase>=1){l.phase-=std::floor(l.phase);uint32_t s=p.seed^uint32_t(l.param*977+l.target*7919+13);s^=uint32_t(l.held*1e6f);l.held=float(randomNext(s)%2001u)/1000.f-1;}
  }
  if(p.engineLfoOn)for(int k=0;k<p.instruments;k++){
    const auto&in=p.instrument[size_t(k)];auto&out=f.instrument[size_t(k)];
    out.track=in.track;out.slot=in.slot;out.engine=in.engine;out.seed=in.seed;out.live=in.engine<3;out.params=in.base;
    for(auto&l:p.engineLfo){
      if(l.target!=k)continue;float v=perfLfoValue(l);
      if(l.morph){   // the bank: 0 the sound itself, 1 .. span-1 its variations
        int span=std::clamp(int(std::lround(2+l.depth*(PerfVariations-2))),2,PerfVariations);
        int index=std::clamp(int(std::lround((v*.5f+.5f)*float(span-1))),0,span-1);
        out.variation=index?(l.variations*0x9e3779b1u+uint32_t(index)*0x85ebca77u)|1u:0;
      } else out.params[l.param]=amalthea_patch_move(in.engine,in.base.data(),l.param,int32_t(std::lround(l.depth*v*32767)));
    }
  }
  f.on=p.chainOn&&p.stages>0;f.generation=p.generation;f.stages=p.stages;f.effect=p.effect;f.target=p.target;
  f.settings=p.paramsOn?p.drawn:p.base;
  if(p.fxLfoOn)for(auto&l:p.lfo){
    if(l.stage<0||l.stage>=p.stages)continue;
    Effect e=p.effect[size_t(l.stage)];auto&s=f.settings[size_t(l.stage)];
    auto&v=insertParameter(e,s,l.field,l.page);
    v=uint8_t(std::clamp(int(std::lround(v+100*l.depth*perfLfoValue(l))),0,100));
  }
  return f;
}
// ---------------------------------------------------------------- the editor's side
void Editor::enterPerformance(){
  editing=mixer=trackBanner=effectBanner=false;selectAnchor=-1;erasing=false;
  if(!performance)performance=std::make_shared<Performance>();
  uint32_t seed=performance->seed^project.seed;if(entropy){randomNext(entropy);seed^=entropy*0x9e3779b9u;}
  *performance=Performance{};performance->seed=seed?seed:1;
  page=Page::Performance;++performanceSerial;
  say("PERFORMANCE - F1 LEAVES");noticeMs=900;++noticeSerial;
}
void Editor::leavePerformance(){
  if(performance){auto seed=performance->seed;*performance=Performance{};performance->seed=seed;}
  ++performanceSerial;
}
void Editor::performanceAction(Action a){
  if(!performance)performance=std::make_shared<Performance>();
  auto&p=*performance;
  switch(a){
    case Action::ShiftDown:shift=true;return;
    case Action::ShiftUp:shift=false;return;
    case Action::Shift2Down:shift2=true;return;
    case Action::Shift2Up:shift2=false;return;
    case Action::Play:playing=!playing;return;
    case Action::RandomizerOptions:case Action::RandomizeColumn:say(perfChain(p,project,device));break;   // X
    case Action::TrackSettings:say(perfParams(p));break;                                               // B
    case Action::ColumnSettings:say(perfFxLfos(p));break;                                              // A
    case Action::Master:say(perfEngineLfos(p,project));break;                                          // Y
    default:return;
  }
  noticeMs=1400;++noticeSerial;++performanceSerial;
}
}
