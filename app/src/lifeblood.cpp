// LIFEBLOOD: generated effect networks, angiogenesis, sessions and controls.
#include "lifeblood.hpp"
#include "fx_rack.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>
namespace amalthea {
namespace {
float unit(uint32_t&s){return (randomNext(s)>>8)*(1.f/16777216.f);}
int between(uint32_t&s,int lo,int hi){return lo+int(randomNext(s)%uint32_t(hi-lo+1));}
bool chance(uint32_t&s,float p){return unit(s)<p;}
constexpr FxType Pool[]={FxType::Space,FxType::QDelay,FxType::ChowMatrix,FxType::Clouds,FxType::Rings,
 FxType::Filter,FxType::Frequalizer,FxType::AmpSim,FxType::LoFi,
 FxType::Wavefold,FxType::Fuzz,FxType::Rectify,FxType::Biztortion,FxType::CrushClone,FxType::Fracture2,FxType::Bitmurderer,FxType::CrazyIvan,
 FxType::Flanger,FxType::Phaser,FxType::Rotor,FxType::Tremolo,FxType::Stutter,
 FxType::Oops,FxType::BufferOverride,FxType::Livecut,FxType::SupaTrigga,FxType::BeatRepeat,FxType::TimeShaper,FxType::Jumblr,FxType::Scrubby,FxType::Glitchlets,FxType::Skidder,
 FxType::Choppr,FxType::GateShaper,FxType::TremoSquare,FxType::Beads,FxType::Softcut,FxType::Transverb,FxType::GlitchShifter,FxType::PitchNasty,FxType::Amalgam,FxType::Geometer,FxType::MultiBandDistortion,FxType::Disintegrate,FxType::BitGlitter,FxType::Pafnuty,
  FxType::Cyanide,FxType::Polarizer,FxType::Angr,FxType::Warps,FxType::FreqShifter,FxType::BrokenFft,FxType::Treemonster,FxType::RingMod,FxType::Combulator,FxType::Resonator,FxType::Vocoder,FxType::Smr,FxType::RezSynth,FxType::TalkBox,FxType::MdaVocoder,FxType::RePsycho};
// A recipe: how strongly each family is favoured, the chance of a shared reverb
// tail that every vessel drains into, the chance of a dry vessel straight from IN
// to OUT, and the harshness budget (each draw from {weights} picks a total).
struct Scene { float family[7]; float tail,dry; float harsh[5]; };
// Family order: Space, Time, Texture, Color, Drive, Motion, Rhythm.
const Scene Scenes[LifebloodCharacters]={
 {{1,1,1,1,1,1,1},.3f,.35f,{.4f,.4f,.2f,0,0}},                 // ANY (never used directly)
 {{1.6f,2.8f,.6f,1,.25f,2.8f,.3f},.35f,.4f,{.4f,.4f,.2f,0,0}},    // DRIFT: motion and echoes
 {{.3f,.6f,.4f,2.8f,2.8f,.8f,1},.18f,.3f,{0,0,.3f,.4f,.3f}},    // GRIT: drive into tone
 {{2.8f,2.8f,1.6f,1,.2f,1,.25f},.7f,.45f,{.4f,.4f,.2f,0,0}},       // SPACE: rooms and repeats
 {{.5f,1.6f,.3f,1.6f,.5f,2.8f,2.8f},.25f,.35f,{.3f,.4f,.3f,0,0}},  // PULSE: rhythmic movement
 {{1.6f,1.2f,2.8f,.8f,.2f,1,.3f},.35f,.5f,{.4f,.4f,.2f,0,0}},     // BLOOM: grains and resonance
 {{.2f,.8f,.4f,.8f,.6f,.4f,3.2f},.12f,.25f,{.3f,.4f,.3f,0,0}},   // CUT: stutters, slicers, repeaters
 {{.3f,.8f,.3f,1.6f,.6f,1.2f,2.8f},.18f,.3f,{.3f,.4f,.3f,0,0}},  // GATE: gates and chops
 {{.15f,.5f,.6f,.8f,2.8f,.4f,2.8f},.1f,.2f,{0,0,.3f,.4f,.3f}},   // MAYHEM: cuts into destruction
 {{.2f,.6f,.3f,2.2f,2.4f,.6f,2.4f},.12f,.2f,{0,.3f,.4f,.3f,0}},  // INDUSTRIAL: gated, driven, filtered
};
int drawHarshBudget(const Scene&s,uint32_t&seed){float u=unit(seed),acc=0;for(int i=0;i<5;i++){acc+=s.harsh[i];if(u<acc)return i;}return 2;}
// CPU ceiling for one network (sum of lifebloodCost), measured against the
// Brick's 5.8 ms block with room for a crossfade.
constexpr int CostCeiling=20;   // about 2 ms of the 5.8 ms block; a crossfade runs the old network on a worker
struct Draw {
 const Scene&scene;int character;const DeviceSettings&device;int path=PathRhythmic;
 std::array<int,int(FxType::Count)> used{};int harsh=0,budget=2,cost=0,costLimit=CostCeiling,reserve=0;   // reserve: cost kept for nodes still to come
 // Weighted choice of an effect for a vessel whose upstream families are given.
 FxType pick(uint32_t&seed,const std::vector<LifebloodFamily>&upstream,bool allowSpace){
  float weight[std::size(Pool)]{},total=0;
  for(size_t i=0;i<std::size(Pool);i++){
   auto t=Pool[i];if(!device.insertEnabled(t))continue;auto f=lifebloodFamily(t);
   if(t==FxType::Space&&(!allowSpace||used[int(t)]))continue;
   if((t==FxType::Clouds||t==FxType::ChowMatrix||t==FxType::Rings)&&used[int(t)])continue;
   if(harsh+lifebloodHarshness(t)>budget)continue;
   if(cost+lifebloodCost(t)+reserve>costLimit)continue;
   bool destructiveUpstream=std::find(upstream.begin(),upstream.end(),LifebloodFamily::Drive)!=upstream.end();
   if(f==LifebloodFamily::Drive&&destructiveUpstream)continue;
   float w=scene.family[int(f)];
   if(!upstream.empty()&&upstream.back()==f)w*=.22f;     // never the same character twice in a row
   if(used[int(t)])w*=.08f;                              // the exact same kind again: rare
   if(character==CharacterPulse&&(t==FxType::Tremolo||t==FxType::Stutter))w*=2;
   // Gates against cutters: GATE and INDUSTRIAL favour the rhythmic gates, CUT and MAYHEM the slicers.
   bool gate=t==FxType::Skidder||t==FxType::Tremolo||t==FxType::Choppr||t==FxType::GateShaper||t==FxType::TremoSquare;
   if((character==CharacterGate||character==CharacterIndustrial)&&gate)w*=3;
   if((character==CharacterCut||character==CharacterMayhem)&&lifebloodFamily(t)==LifebloodFamily::Rhythm&&!gate)w*=1.5f;
   if(t==FxType::Stutter&&character!=CharacterPulse&&!lifebloodRhythmic(character))w*=.25f;   // chopping to a pulse is a strong gesture
   if(t==FxType::CrazyIvan||t==FxType::Bitmurderer)w*=.35f;     // the wildest inserts stay occasional
   // The path (Vascular): RHYTHMIC leans on cuts, gates and clockable textures and keeps
   // long tails rare; ABSTRACT keeps cuts and gates occasional.
   if(path==PathRhythmic){if(f==LifebloodFamily::Rhythm)w*=1.6f;if(f==LifebloodFamily::Space)w*=.5f;if(t==FxType::Beads||t==FxType::Softcut||t==FxType::RePsycho)w*=1.6f;}
   else if(f==LifebloodFamily::Rhythm)w*=.3f;
   weight[i]=w;total+=w;
  }
  if(total<=0)return FxType::Frequalizer;   // smooth and among the cheapest (reserve keeps it affordable)
  float u=unit(seed)*total;for(size_t i=0;i<std::size(Pool);i++){if(u<weight[i])return Pool[i];u-=weight[i];}
  return FxType::Frequalizer;
 }
 void take(FxType t){used[int(t)]++;harsh+=lifebloodHarshness(t);cost+=lifebloodCost(t);}
 // The most likely affordable member of one family (None when there is none).
 FxType pickFamily(uint32_t&seed,LifebloodFamily family){
  FxType c[std::size(Pool)];int n=0;
  for(auto t:Pool)if(lifebloodFamily(t)==family&&device.insertEnabled(t)&&cost+lifebloodCost(t)+reserve<=costLimit)c[n++]=t;   // the rhythm a recipe asks for outranks its harshness budget
  return n?c[randomNext(seed)%n]:FxType::None;
 }
};
// The recipe X grows from: CHARACTER when it is on the PATH, else a random one of the PATH's.
int sceneFor(int character,int path,uint32_t&seed){
 if(lifebloodOnPath(character,path))return character;
 int c[LifebloodCharacters],n=0;for(int k=1;k<LifebloodCharacters;k++)if(lifebloodOnPath(k,path))c[n++]=k;
 return n?c[randomNext(seed)%n]:CharacterDrift;
}
// Everything reachable from `from` (LifebloodIn: from every live input) or, backward, reaching it.
void reach(const LifebloodNetwork&n,int from,bool forward,std::array<bool,LifebloodVertices>&seen,unsigned dormant){
 seen.fill(false);int stack[LifebloodVertices],top=0;
 if(from==LifebloodIn&&forward){for(int v=0;v<LifebloodVertices;v++)if(lifebloodInput(v)&&n.live(v)){stack[top++]=v;seen[v]=true;}}
 else{stack[top++]=from;seen[from]=true;}
 while(top){int v=stack[--top];for(int w=0;w<LifebloodVertices;w++){
  int a=forward?v:w,b=forward?w:v;if(!n.link[a][b]||seen[w]||!n.live(w))continue;if(lifebloodNodeVertex(w)&&(dormant>>(w-2)&1))continue;seen[w]=true;stack[top++]=w;}}
}
// A random input vessels may start from (the stereo IN, or a heard track).
int randomSource(const LifebloodNetwork&n,uint32_t&seed){
 int c[LifebloodVertices],m=0;for(int v=0;v<LifebloodVertices;v++)if(n.source(v))c[m++]=v;
 return m?c[randomNext(seed)%m]:LifebloodIn;
}
void layout(LifebloodNetwork&n){
 // Ranks follow the order of the active slots' current ranks (1 .. count).
 std::array<int,LifebloodMaxNodes> order{};int count=0;for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active)order[count++]=k;
 std::sort(order.begin(),order.begin()+count,[&](int a,int b){return n.node[a].rank<n.node[b].rank;});
 for(int i=0;i<count;i++)n.node[order[i]].rank=uint8_t(i+1);
}
// The recipes' standing rules for any vessel: up the flow, a reverb drains only
// into OUT, and two destructive nodes never feed each other directly.
bool allowedEdge(const LifebloodNetwork&n,int a,int b){
 if(a==b||a==LifebloodOut||lifebloodInput(b)||!n.live(a)||!n.live(b)||n.rank(a)>=n.rank(b))return false;
 if(lifebloodNodeVertex(a)&&n.node[a-2].effect.type==FxType::Space&&b!=LifebloodOut)return false;
 if(lifebloodNodeVertex(a)&&lifebloodNodeVertex(b)&&lifebloodFamily(n.node[a-2].effect.type)==LifebloodFamily::Drive&&lifebloodFamily(n.node[b-2].effect.type)==LifebloodFamily::Drive)return false;
 return true;
}
void clampField(LifebloodNode&k){k.x=std::clamp(k.x,.1f,.9f);k.y=std::clamp(k.y,.1f,.9f);}
}
const char*lifebloodPathName(int p){return p==PathAbstract?"ABSTRACT":"RHYTHMIC";}
const char*lifebloodCharacterName(int c){static const char*n[]={"ANY","DRIFT","GRIT","SPACE","PULSE","BLOOM","CUT","GATE","MAYHEM","INDUSTRIAL"};return n[std::clamp(c,0,LifebloodCharacters-1)];}
LifebloodFamily lifebloodFamily(FxType t){
 if(auto*n=nativeInfo(t))return LifebloodFamily(std::min<int>(n->family,6));
 switch(t){
  case FxType::Space:return LifebloodFamily::Space;
  case FxType::QDelay:case FxType::ChowMatrix:return LifebloodFamily::Time;
  case FxType::Clouds:case FxType::Rings:return LifebloodFamily::Texture;
  case FxType::Filter:case FxType::Frequalizer:case FxType::AmpSim:case FxType::LoFi:return LifebloodFamily::Color;
  case FxType::Flanger:case FxType::Phaser:case FxType::Rotor:case FxType::Tremolo:case FxType::Stutter:return LifebloodFamily::Motion;
  default:return LifebloodFamily::Drive;
 }
}
int lifebloodHarshness(FxType t){
 if(auto*n=nativeInfo(t))return n->harshness;
 switch(t){
  case FxType::Fuzz:case FxType::Rectify:case FxType::Bitmurderer:case FxType::CrazyIvan:case FxType::Fracture2:case FxType::Wavefold:return 2;
  case FxType::Biztortion:case FxType::CrushClone:case FxType::AmpSim:case FxType::LoFi:case FxType::Stutter:case FxType::Rings:return 1;
  default:return 0;
 }
}
// Worst of 20 random lifeblood draws, measured on the Brick (build 29, one 256-frame
// block), in units of about 100 us above the bare path: SPACE 1.2 ms, RINGS 0.87,
// CRUSHCLONE 0.83, MATRIX 0.79, LO-FI and PHASER 0.4, CLOUDS 0.35, BIZTORTION 0.34,
// QDELAY 0.28; the light inserts 0.01-0.2 ms.
int lifebloodCost(FxType t){
 if(auto*n=nativeInfo(t))return std::max<int>(1,n->cost);
 switch(t){
  case FxType::Space:return 12;case FxType::Rings:case FxType::CrushClone:case FxType::ChowMatrix:return 8;
  case FxType::LoFi:case FxType::Phaser:case FxType::Clouds:return 4;
  case FxType::Biztortion:case FxType::QDelay:return 3;
  case FxType::AmpSim:case FxType::Filter:case FxType::Fracture2:case FxType::CrazyIvan:return 2;
  default:return 1;
 }
}
const char*lifebloodLabel(FxType t){
 if(auto*n=nativeInfo(t))return n->label;
 switch(t){
  case FxType::Space:return "SPACE";case FxType::QDelay:return "QDELAY";case FxType::ChowMatrix:return "MATRIX";
  case FxType::Clouds:return "CLOUDS";case FxType::Rings:return "RINGS";case FxType::Filter:return "FILTER";
  case FxType::Frequalizer:return "EQ";case FxType::AmpSim:return "AMP";case FxType::LoFi:return "LO-FI";
  case FxType::Wavefold:return "FOLD";case FxType::Fuzz:return "FUZZ";case FxType::Rectify:return "RECT";
  case FxType::Biztortion:return "BIZ";case FxType::CrushClone:return "CRUSH";case FxType::Fracture2:return "FRACT";
  case FxType::Bitmurderer:return "BITS";case FxType::CrazyIvan:return "IVAN";case FxType::Flanger:return "FLANGE";
  case FxType::Phaser:return "PHASE";case FxType::Rotor:return "ROTOR";case FxType::Tremolo:return "TREM";
  case FxType::Stutter:return "STUTTER";default:return fxName(t);
 }
}
LifebloodNetwork blankLifeblood(){LifebloodNetwork n;n.link[LifebloodIn][LifebloodOut]=100;return n;}

void reseedLifebloodNode(LifebloodNetwork&n,int slot,uint32_t&seed,int path){
 if(slot<0||slot>=LifebloodMaxNodes||!n.node[slot].active)return;
 auto&k=n.node[slot];auto type=k.effect.type;
 // A fresh playable draw (shared with the FX appliance), with one parameter in
 // five kept at the effect's own default, so redraws stay near known ground.
 Effect drawn{type,0};InsertSettings fresh{};randomizeFxParameters(drawn,fresh,seed);
 Effect plain{type,0};InsertSettings defaults{};
 int pages=type==FxType::ChowMatrix?9:type==FxType::Frequalizer?6:type==FxType::Biztortion?8:1;
 for(int page=0;page<pages;page++)for(int f=1;f<insertParameterCount(type,page,fresh);f++){
  if(steppedInsertParameter(type,f,page,fresh)||!chance(seed,.2f))continue;
  auto name=std::string(insertParameterName(type,f,page,fresh));if(name=="MODULE")continue;
  insertParameter(drawn,fresh,f,page)=insertParameter(plain,defaults,f,page);
 }
 k.settings=fresh;
 // Depth by character (after Vascular, made clearly audible): destructive inserts
 // a strong blend, volatile ones mostly wet, cuts and gates fully. FLANGER and PHASER
 // form their notches against their own dry path, so they keep their own high mix.
 // On the RHYTHMIC path no effect sits below 72% (Vascular's rhythmic processor level).
 auto f=lifebloodFamily(type);
 if(type==FxType::Flanger||type==FxType::Phaser)k.effect.value=drawn.value;
 else if(f==LifebloodFamily::Drive)k.effect.value=uint8_t(between(seed,50,80));
 else if(f==LifebloodFamily::Motion||f==LifebloodFamily::Texture)k.effect.value=uint8_t(between(seed,60,90));
 else if(f==LifebloodFamily::Rhythm)k.effect.value=uint8_t(between(seed,85,100));
 else k.effect.value=uint8_t(between(seed,70,100));
 if(path==PathRhythmic)k.effect.value=std::max<uint8_t>(k.effect.value,72);
 // RHYTHMIC: anything that can follow the clock does (Beads' CLOCK SYNC, SYNC toggles).
 if(path==PathRhythmic&&nativeIndex(type)>=0){auto*info=nativeInfo(type);for(int i=0;i<info->count;i++){std::string name=info->params[i].name;
  if(info->params[i].unit==NativeUnit::Toggle&&(name=="SYNC"||name=="CLOCK SYNC"))k.settings.native[nativeIndex(type)][i]=100;}}
 // Inside a network a delay's repeats feed further processors: keep them finite.
 if(type==FxType::QDelay||type==FxType::ChowMatrix){
  for(int page=0;page<pages;page++)for(int fld=1;fld<insertParameterCount(type,page,k.settings);fld++)
   if(std::string(insertParameterName(type,fld,page,k.settings)).find("FEEDBACK")!=std::string::npos){auto&v=insertParameter(k.effect,k.settings,fld,page);v=std::min<uint8_t>(v,60);}
 }
 if(type==FxType::Clouds)k.settings.clouds[7]=0;   // never frozen: a network must follow its input
 boundInsertCost(k.effect,k.settings);   // within the Brick's budget (fx_rack.hpp)
}
void reseedLifeblood(LifebloodNetwork&n,uint32_t&seed,int path){for(int k=0;k<LifebloodMaxNodes;k++)reseedLifebloodNode(n,k,seed,path);}

LifebloodNetwork growLifeblood(const LifebloodRecipe&recipe,uint32_t&seed,const DeviceSettings&device){
 if(!seed)seed=1;
 LifebloodNetwork n;int character=sceneFor(recipe.character,recipe.path,seed);n.character=uint8_t(character);
 const Scene&scene=Scenes[character];Draw draw{scene,character,device,recipe.path};draw.budget=drawHarshBudget(scene,seed);
 int maxNodes=std::clamp(int(recipe.maxNodes),3,LifebloodMaxNodes);int total=between(seed,3,maxNodes);
 bool tail=chance(seed,scene.tail*(recipe.path==PathRhythmic?.5f:1.f))&&device.insertEnabled(FxType::Space);
 int vesselNodes=total-(tail?1:0);
 // One to three vessels from IN; each one to three nodes deep.
 int vessels=vesselNodes<=1?1:std::clamp(chance(seed,.3f)?1:chance(seed,.7f)?2:3,(vesselNodes+2)/3,std::min(3,vesselNodes));
 std::array<int,3> length{};for(int v=0;v<vessels;v++)length[v]=1;
 for(int left=vesselNodes-vessels;left>0;){int v=between(seed,0,vessels-1);if(length[v]<3){length[v]++;left--;}}
 int depth=*std::max_element(length.begin(),length.begin()+vessels);
 // Slots in flow order: depth by depth, vessel by vessel, then the tail.
 std::array<std::array<int,3>,3> slot{};int next=0;
 for(int d=0;d<depth;d++)for(int v=0;v<vessels;v++)if(d<length[v])slot[v][d]=next++;
 std::array<std::vector<LifebloodFamily>,3> upstream{};int drawn=0;
 // Draw vessel by vessel so each vessel's own history steers its next choice.
 for(int v=0;v<vessels;v++)for(int d=0;d<length[v];d++){
  bool last=d==length[v]-1;draw.reserve=(vesselNodes-++drawn)+(tail?lifebloodCost(FxType::Space):0);FxType t=draw.pick(seed,upstream[v],last&&!tail);
  // A rhythmic recipe opens its lead vessel with a cut or a gate, so the network always moves in time.
  if(lifebloodRhythmic(character)&&v==0&&d==0&&lifebloodFamily(t)!=LifebloodFamily::Rhythm){FxType r=draw.pickFamily(seed,LifebloodFamily::Rhythm);if(r!=FxType::None)t=r;}
  draw.take(t);upstream[v].push_back(lifebloodFamily(t));
  auto&k=n.node[slot[v][d]];k.active=true;k.effect={t,0};k.rank=uint8_t(slot[v][d]+1);
  float lane=(v+1.f)/(vessels+1);k.x=.2f+(tail?.48f:.62f)*(depth>1?float(d)/(depth-1):.5f);k.y=lane+(unit(seed)-.5f)*.1f;clampField(k);
 }
 // Reverbs come last in a vessel (Vascular's spaceLast): move a drawn SPACE to the end.
 for(int v=0;v<vessels;v++)for(int d=0;d+1<length[v];d++){
  auto&a=n.node[slot[v][d]];auto&b=n.node[slot[v][d+1]];if(a.effect.type==FxType::Space)std::swap(a.effect,b.effect);
 }
 int tailVertex=-1;
 if(tail){auto&k=n.node[next];k.active=true;k.effect={FxType::Space,0};k.rank=uint8_t(next+1);k.x=.78f;k.y=.5f+(unit(seed)-.5f)*.16f;draw.take(FxType::Space);tailVertex=2+next;next++;}
 for(int k=0;k<LifebloodMaxNodes;k++)reseedLifebloodNode(n,k,seed,recipe.path);
 // Vessel levels after Vascular's roles (lead, body, motion), kept close so every vessel is heard.
 const int level[3]={100,90,80};
 for(int v=0;v<vessels;v++){
  int g=std::clamp(level[v]+between(seed,-4,4),40,100);
  n.link[LifebloodIn][2+slot[v][0]]=100;
  for(int d=0;d+1<length[v];d++)n.link[2+slot[v][d]][2+slot[v][d+1]]=100;
  int end=2+slot[v][length[v]-1];
  if(tailVertex>=0){n.link[end][tailVertex]=uint8_t(g);if(v==0&&chance(seed,.5f))n.link[end][LifebloodOut]=uint8_t(between(seed,55,80));}
  else n.link[end][LifebloodOut]=uint8_t(g);
 }
 if(tailVertex>=0)n.link[tailVertex][LifebloodOut]=100;
 // Vessels merge again: a later node also drinks from an earlier node of another vessel.
 for(int v=0;v<vessels;v++)for(int d=1;d<length[v];d++){
  if(!chance(seed,.35f))continue;int other=between(seed,0,vessels-1);if(other==v)continue;int od=between(seed,0,std::min(d,length[other])-1);
  // A reverb only ever drains toward OUT; two destructive nodes never meet head on.
  auto from=n.node[slot[other][od]].effect.type,to=n.node[slot[v][d]].effect.type;
  if(from==FxType::Space||(lifebloodFamily(from)==LifebloodFamily::Drive&&lifebloodFamily(to)==LifebloodFamily::Drive))continue;
  n.link[2+slot[other][od]][2+slot[v][d]]=uint8_t(between(seed,45,80));
 }
 // A dry vessel now and then (rarer and quieter than before: the network should be heard).
 if(chance(seed,scene.dry*.5f))n.link[LifebloodIn][LifebloodOut]=uint8_t(between(seed,15,35));
 layout(n);
 if(recipe.inputs){n.heard=recipe.heard;lifebloodSetInputs(n,recipe.inputs,recipe.heard,seed);}
 return n;
}

void lifebloodRepairMasked(LifebloodNetwork&,uint32_t&,unsigned);
void rewireLifeblood(LifebloodNetwork&n,uint32_t&seed,unsigned dormant){
 if(!seed)seed=1;int character=n.character>CharacterAny?n.character:between(seed,1,LifebloodCharacters-1);
 std::array<int,LifebloodMaxNodes> order{};int count=0,parked=0;std::array<int,LifebloodMaxNodes> rest{};
 for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active){if(dormant>>k&1)rest[parked++]=k;else order[count++]=k;}
 for(int i=count-1;i>0;i--)std::swap(order[i],order[randomNext(seed)%(i+1)]);
 // Reverbs last, so they can drain into OUT.
 std::stable_partition(order.begin(),order.begin()+count,[&](int k){return n.node[k].effect.type!=FxType::Space;});
 for(int i=0;i<count;i++)n.node[order[i]].rank=uint8_t(i+1);
 for(int i=0;i<parked;i++)n.node[rest[i]].rank=uint8_t(count+i+1);
 for(auto&row:n.link)row.fill(0);
 // Built from the stereo IN, then spread over the heard tracks below.
 int inputs=n.inputs;n.inputs=0;
 std::array<int,LifebloodVertices> placed{};int m=0;placed[m++]=LifebloodIn;
 for(int i=0;i<count;i++){
  int v=2+order[i],options[LifebloodVertices],o=0;
  for(int j=0;j<m;j++)if(allowedEdge(n,placed[j],v))options[o++]=placed[j];
  int parent=i==0||!o?LifebloodIn:options[randomNext(seed)%o];
  n.link[parent][v]=uint8_t(between(seed,70,100));
  // Chains merge again now and then.
  if(i>=1&&chance(seed,.35f)){int from=placed[randomNext(seed)%m];if(from!=parent&&allowedEdge(n,from,v))n.link[from][v]=uint8_t(between(seed,45,80));}
  placed[m++]=v;
 }
 // Loose ends are outlets, at Vascular's role levels.
 const int level[4]={100,80,64,64};int outlets=0;
 for(int i=count-1;i>=0;i--){int v=2+order[i];bool out=false;for(int w=0;w<LifebloodVertices;w++)out|=n.link[v][w]>0;
  if(!out)n.link[v][LifebloodOut]=uint8_t(std::clamp(level[std::min(outlets++,3)]+between(seed,-4,4),40,100));}
 if(!count||chance(seed,Scenes[character].dry*.5f))n.link[LifebloodIn][LifebloodOut]=uint8_t(count?between(seed,15,35):100);
 lifebloodRepairMasked(n,seed,dormant);
 if(inputs)lifebloodSetInputs(n,inputs,n.heard,seed);
}
void lifebloodSetInputs(LifebloodNetwork&n,int inputs,uint8_t heard,uint32_t&seed){
 inputs=std::clamp(inputs,0,LifebloodTracks);if(!seed)seed=1;
 // Gather every input's vessels onto the stereo IN.
 for(int t=0;t<LifebloodTracks;t++){auto&row=n.link[LifebloodTrackBase+t];for(int w=0;w<LifebloodVertices;w++){n.link[LifebloodIn][w]=std::max(n.link[LifebloodIn][w],row[w]);row[w]=0;}}
 n.inputs=uint8_t(inputs);n.heard=heard;
 if(!inputs){lifebloodRepairMasked(n,seed,0);return;}
 // Spread them over the heard tracks: each vessel drinks from one to three of them.
 int c[LifebloodTracks],m=0;for(int t=0;t<inputs;t++)if(n.source(LifebloodTrackBase+t))c[m++]=t;
 std::array<uint8_t,LifebloodVertices> from=n.link[LifebloodIn];n.link[LifebloodIn].fill(0);
 int entries[LifebloodVertices],e=0;
 for(int w=0;w<LifebloodVertices;w++){
  if(!from[w]||w==LifebloodOut)continue;entries[e++]=w;
  int take=std::min(m,1+chance(seed,.45f)+chance(seed,.2f));
  for(int i=m-1;i>0;i--)std::swap(c[i],c[randomNext(seed)%(i+1)]);
  for(int i=0;i<take;i++)n.link[LifebloodTrackBase+c[i]][w]=uint8_t(std::clamp(int(from[w])-between(seed,0,20),60,100));
 }
 // Every heard track goes somewhere: into an entry of the network, or (about a third)
 // straight to OUT, so it stays in the mix untouched.
 for(int i=0;i<m;i++){
  auto&row=n.link[LifebloodTrackBase+c[i]];bool any=false;for(auto v:row)any|=v>0;if(any)continue;
  if(!e||chance(seed,.35f))row[LifebloodOut]=uint8_t(between(seed,70,95));
  else row[entries[randomNext(seed)%e]]=uint8_t(between(seed,70,100));
 }
 // The old dry vessel becomes one heard track passing straight through.
 if(from[LifebloodOut]&&m){auto&g=n.link[LifebloodTrackBase+c[randomNext(seed)%m]][LifebloodOut];g=std::max(g,from[LifebloodOut]);}
 lifebloodRepairMasked(n,seed,0);
}
bool lifebloodConnectHeard(LifebloodNetwork&n,uint32_t&seed){
 bool added=false;
 for(int t=0;t<n.inputs;t++){int v=LifebloodTrackBase+t;if(!(n.heard>>t&1))continue;bool any=false;for(auto g:n.link[v])any|=g>0;
  if(!any){n.link[v][LifebloodOut]=uint8_t(between(seed,70,90));added=true;}}
 return added;
}
bool lifebloodValid(const LifebloodNetwork&n){
 for(int a=0;a<LifebloodVertices;a++)for(int b=0;b<LifebloodVertices;b++)if(n.link[a][b]){
  if(!n.live(a)||!n.live(b)||a==LifebloodOut||lifebloodInput(b)||n.rank(a)>=n.rank(b))return false;
 }
 std::array<bool,LifebloodVertices> from{},to{};reach(n,LifebloodIn,true,from,0);reach(n,LifebloodOut,false,to,0);
 if(!from[LifebloodOut])return false;
 for(int k=0;k<LifebloodMaxNodes;k++){
  if(!n.node[k].active)continue;bool linked=false;for(int v=0;v<LifebloodVertices;v++)linked|=n.link[v][2+k]||n.link[2+k][v];
  if(linked&&(!from[2+k]||!to[2+k]))return false;   // isolated (withering) nodes are allowed
 }
 return true;
}
void lifebloodRepairMasked(LifebloodNetwork&n,uint32_t&seed,unsigned dormant){
 for(int a=0;a<LifebloodVertices;a++)for(int b=0;b<LifebloodVertices;b++)
  if(n.link[a][b]&&!allowedEdge(n,a,b))n.link[a][b]=0;
 std::array<int,LifebloodMaxNodes> order{};int count=0;
 for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active&&!(dormant>>k&1))order[count++]=k;
 std::sort(order.begin(),order.begin()+count,[&](int a,int b){return n.node[a].rank<n.node[b].rank;});
 auto near=[&](int v,const std::array<bool,LifebloodVertices>&ok,bool lower){
  // Prefer a near partner: weight 1 / (distance + 0.12).
  float w[LifebloodVertices]{},total=0;
  for(int u=0;u<LifebloodVertices;u++){if(!ok[u]||u==v||!n.live(u))continue;if(lifebloodNodeVertex(u)&&(dormant>>(u-2)&1))continue;
   if(lifebloodInput(u)&&!n.source(u))continue;   // never from a silent track
   if(lower?!allowedEdge(n,u,v):!allowedEdge(n,v,u))continue;
   w[u]=1/(std::hypot(n.x(u)-n.x(v),n.y(u)-n.y(v))+.12f);total+=w[u];}
  if(total<=0)return lower?randomSource(n,seed):LifebloodOut;
  float r=unit(seed)*total;for(int u=0;u<LifebloodVertices;u++){if(r<w[u])return u;r-=w[u];}return lower?randomSource(n,seed):LifebloodOut;
 };
 std::array<bool,LifebloodVertices> from{},to{};
 for(int i=0;i<count;i++){reach(n,LifebloodIn,true,from,dormant);int v=2+order[i];if(from[v])continue;int u=near(v,from,true);n.link[u][v]=uint8_t(between(seed,60,90));}
 for(int i=count-1;i>=0;i--){reach(n,LifebloodOut,false,to,dormant);int v=2+order[i];if(to[v])continue;int w=near(v,to,false);n.link[v][w]=uint8_t(between(seed,60,90));}
 reach(n,LifebloodIn,true,from,dormant);if(!from[LifebloodOut])n.link[randomSource(n,seed)][LifebloodOut]=100;
}
void lifebloodRepair(LifebloodNetwork&n,uint32_t&seed){lifebloodRepairMasked(n,seed,0);}

void Angiogenesis::start(const LifebloodNetwork&n){
 if(!seed)seed=1;startCost=0;wither.fill(0);timer=0;
 for(int k=0;k<LifebloodMaxNodes;k++){
  float a=unit(seed)*6.2831853f,speed=.014f+unit(seed)*.066f;vx[k]=speed*std::cos(a);vy[k]=speed*std::sin(a)*4/3;
  if(n.node[k].active)startCost+=lifebloodCost(n.node[k].effect.type);
 }
}
bool Angiogenesis::step(LifebloodNetwork&n,float dt,int growth,int maxNodes,const DeviceSettings&device){
 if(!active||dt<=0)return false;dt=std::min(dt,.25f);if(!seed)seed=1;bool changed=false;
 // Motion: a slow bounce off the field's walls (4% margin).
 float pace=.6f+.8f*std::clamp(growth,0,100)/100.f;
 for(int k=0;k<LifebloodMaxNodes;k++){auto&node=n.node[k];if(!node.active)continue;
  node.x+=vx[k]*dt*pace;node.y+=vy[k]*dt*pace;
  if(node.x<.08f){node.x=.08f;vx[k]=std::abs(vx[k]);}if(node.x>.92f){node.x=.92f;vx[k]=-std::abs(vx[k]);}
  if(node.y<.08f){node.y=.08f;vy[k]=std::abs(vy[k]);}if(node.y>.92f){node.y=.92f;vy[k]=-std::abs(vy[k]);}
 }
 // A pruned node lingers until its vessels have faded, then its slot frees.
 unsigned dormant=0;
 for(int k=0;k<LifebloodMaxNodes;k++)if(wither[k]>0){wither[k]-=dt;if(wither[k]<=0){wither[k]=0;n.node[k].active=false;layout(n);changed=true;}else dormant|=1u<<k;}
 // Mutations: GROWTH 0..100 is 6..120 a minute.
 float rate=6+114*std::clamp(growth,0,100)/100.f;timer+=dt;
 if(timer<60/rate)return changed;timer=0;
 int character=n.character>CharacterAny?n.character:sceneFor(CharacterAny,path,seed);
 int active=0,cost=0,harsh=0;for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active&&!(dormant>>k&1)){active++;cost+=lifebloodCost(n.node[k].effect.type);harsh+=lifebloodHarshness(n.node[k].effect.type);}
 // Inputs take part only while heard; withering nodes not at all.
 auto live=[&](int v){return n.live(v)&&!(lifebloodNodeVertex(v)&&(dormant>>(v-2)&1))&&!(lifebloodInput(v)&&!n.source(v));};
 auto degree=[&](int v){int d=0;for(int u=0;u<LifebloodVertices;u++)d+=(n.link[u][v]>0)+(n.link[v][u]>0);return d;};
 auto randomLive=[&](bool effectsOnly){int c[LifebloodVertices],m=0;for(int v=0;v<LifebloodVertices;v++)if(live(v)&&(!effectsOnly||lifebloodNodeVertex(v)))c[m++]=v;return m?c[randomNext(seed)%m]:-1;};
 auto rewire=[&]{
  if(chance(seed,.4f)){
   // Cut a vessel, longer ones more readily.
   float w[LifebloodVertices*LifebloodVertices]{},total=0;
   for(int a=0;a<LifebloodVertices;a++)for(int b=0;b<LifebloodVertices;b++)if(n.link[a][b]){float d=std::hypot(n.x(a)-n.x(b),n.y(a)-n.y(b))+.05f;w[a*LifebloodVertices+b]=d;total+=d;}
   float r=unit(seed)*total;for(int i=0;i<LifebloodVertices*LifebloodVertices;i++){if(w[i]<=0)continue;if(r<w[i]){n.link[i/LifebloodVertices][i%LifebloodVertices]=0;break;}r-=w[i];}
  }else{
   // Sprout a vessel: one end is the less connected of two nodes, the other a near partner.
   int a=randomLive(false),b=randomLive(false);if(a<0||b<0)return;if(degree(b)<degree(a))std::swap(a,b);
   float w[LifebloodVertices]{},total=0;
   for(int u=0;u<LifebloodVertices;u++){if(u==a||!live(u)||n.rank(u)==n.rank(a))continue;if((lifebloodInput(a)&&u==LifebloodOut)||(a==LifebloodOut&&lifebloodInput(u))||(lifebloodInput(a)&&lifebloodInput(u)))continue;w[u]=1/(std::hypot(n.x(u)-n.x(a),n.y(u)-n.y(a))+.12f);total+=w[u];}
   if(total<=0)return;float r=unit(seed)*total;int to=-1;for(int u=0;u<LifebloodVertices;u++){if(w[u]<=0)continue;if(r<w[u]){to=u;break;}r-=w[u];}
   if(to<0)return;int from=a;if(n.rank(from)>n.rank(to))std::swap(from,to);if(!n.link[from][to]&&allowedEdge(n,from,to))n.link[from][to]=uint8_t(between(seed,50,90));
  }
 };
 auto grow=[&]{
  int slot=-1;for(int k=0;k<LifebloodMaxNodes;k++)if(!n.node[k].active){slot=k;break;}
  if(slot<0||active>=std::clamp(maxNodes,3,LifebloodMaxNodes))return false;
  Draw draw{Scenes[character],character,device,path};draw.budget=std::max(harsh,drawHarshBudget(Scenes[character],seed));draw.harsh=harsh;draw.cost=cost;
  draw.costLimit=std::min(CostCeiling,startCost+8);
  for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active)draw.used[int(n.node[k].effect.type)]++;
  int host=randomLive(false);if(host==LifebloodOut||host<0||(lifebloodNodeVertex(host)&&n.node[host-2].effect.type==FxType::Space))host=randomSource(n,seed);
  std::vector<LifebloodFamily> upstream;if(lifebloodNodeVertex(host))upstream.push_back(lifebloodFamily(n.node[host-2].effect.type));
  FxType t=draw.pick(seed,upstream,true);if(draw.cost+lifebloodCost(t)>draw.costLimit||harsh+lifebloodHarshness(t)>draw.budget)return false;
  auto&k=n.node[slot];k=LifebloodNode{};k.active=true;k.effect={t,0};
  // Bud just after the host in the flow.
  float r=lifebloodInput(host)?.5f:n.node[host-2].rank+.5f;
  for(auto&o:n.node)if(o.active&&&o!=&k&&o.rank>r)o.rank++;k.rank=uint8_t(r+.5f);layout(n);
  k.x=n.x(host)+(unit(seed)-.3f)*.16f;k.y=n.y(host)+(unit(seed)-.5f)*.21f;clampField(k);
  float a=unit(seed)*6.2831853f,speed=.014f+unit(seed)*.066f;vx[slot]=speed*std::cos(a);vy[slot]=speed*std::sin(a)*4/3;
  reseedLifebloodNode(n,slot,seed,path);
  if(!allowedEdge(n,host,2+slot))host=randomSource(n,seed);
  n.link[host][2+slot]=uint8_t(between(seed,70,100));
  int succ[LifebloodVertices],m=0;for(int w=0;w<LifebloodVertices;w++)if(n.link[host][w]&&w!=2+slot&&live(w)&&allowedEdge(n,2+slot,w))succ[m++]=w;
  n.link[2+slot][m?succ[randomNext(seed)%m]:LifebloodOut]=uint8_t(between(seed,60,90));
  return true;
 };
 auto prune=[&]{
  if(active<=LifebloodFloor)return false;int v=randomLive(true);if(v<0)return false;
  // Absorb the node: its inputs are joined to its outputs, then its vessels fade.
  for(int u=0;u<LifebloodVertices;u++)for(int w=0;w<LifebloodVertices;w++)
   if(n.link[u][v]&&n.link[v][w]&&allowedEdge(n,u,w)&&!(lifebloodInput(u)&&w==LifebloodOut&&n.link[u][w]))n.link[u][w]=std::max(n.link[u][w],std::min(n.link[u][v],n.link[v][w]));
  for(int u=0;u<LifebloodVertices;u++)n.link[u][v]=n.link[v][u]=0;
  wither[v-2]=.4f;dormant|=1u<<(v-2);return true;
 };
 switch(randomNext(seed)%8){
  case 0:case 1:rewire();break;
  case 2:if(!grow())prune();break;
  case 3:if(!grow())rewire();break;
  case 4:if(!prune())rewire();break;
  default:{int v=randomLive(true);if(v>=0)reseedLifebloodNode(n,v-2,seed,path);}break;
 }
 lifebloodRepairMasked(n,seed,dormant);
 return true;
}

// Sessions: AMLB2 (AMLB1 also loads: one stereo IN, a 9 x 9 link matrix, no PATH).
// Header, CRC-32, then the body. InsertSettings are written as a
// list of (length, bytes) members in a fixed, append-only order, so a later build
// whose profiles grow still reads these files (missing members keep defaults).
namespace {
uint32_t crc(const std::vector<uint8_t>&b,size_t from){uint32_t c=~0u;for(size_t i=from;i<b.size();i++){c^=b[i];for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
template<class F> void members(InsertSettings&s,F f){
 f(s.qdelay.data(),s.qdelay.size());f(s.matrix.data(),s.matrix.size());
 for(auto&n:s.nodes)f(n.data(),n.size());
 f(s.crush.data(),s.crush.size());f(s.frequalizer.data(),s.frequalizer.size());f(s.rings.data(),s.rings.size());
 f(s.fracture.data(),s.fracture.size());f(s.ivan.data(),s.ivan.size());f(s.murder.data(),s.murder.size());f(s.lofi.data(),s.lofi.size());
 f(s.filter.data(),s.filter.size());f(s.space.data(),s.space.size());f(s.clouds.data(),s.clouds.size());f(s.amp.data(),s.amp.size());
 f(s.rotor.data(),s.rotor.size());f(s.bizLevel.data(),s.bizLevel.size());f(s.flanger.data(),s.flanger.size());f(s.phaser.data(),s.phaser.size());
 for(auto&b:s.biz){f(&b.module,1);f(b.value.data(),b.value.size());}
 // Native effect profiles, one member each, in effect order (later effects append).
 for(auto&profile:s.native)f(profile.data(),profile.size());
}
int memberCount(){InsertSettings s;int n=0;members(s,[&](uint8_t*,size_t){n++;});return n;}
struct Writer{std::vector<uint8_t>b;void put(uint32_t v,int n){while(n--){b.push_back(v&255);v>>=8;}}};
struct Reader{const std::vector<uint8_t>&b;size_t at;bool ok=true;
 uint32_t get(int n){uint32_t v=0;for(int i=0;i<n;i++){if(at>=b.size()){ok=false;return v;}v|=uint32_t(b[at++])<<(i*8);}return v;}};
void writeNetwork(Writer&w,const LifebloodNetwork&n){
 w.put(n.character,1);w.put(n.inputs,1);
 for(auto node:n.node){w.put(node.active,1);if(!node.active)continue;w.put(uint8_t(node.effect.type),1);w.put(node.effect.value,1);w.put(node.rank,1);
  w.put(uint32_t(std::lround(std::clamp(node.x,0.f,1.f)*65535)),2);w.put(uint32_t(std::lround(std::clamp(node.y,0.f,1.f)*65535)),2);
  w.put(memberCount(),1);members(node.settings,[&](uint8_t*p,size_t len){w.put(uint32_t(len),2);for(size_t i=0;i<len;i++)w.put(p[i],1);});}
 w.put(LifebloodVertices,1);for(auto&row:n.link)for(auto v:row)w.put(v,1);
}
bool readNetwork(Reader&r,LifebloodNetwork&n,int version){
 n=LifebloodNetwork{};n.character=uint8_t(r.get(1));r.ok&=n.character<LifebloodCharacters;
 if(version>=2){n.inputs=uint8_t(r.get(1));r.ok&=n.inputs<=LifebloodTracks;}
 for(auto&node:n.node){node.active=r.get(1);if(!node.active)continue;
  auto type=r.get(1);r.ok&=type>0&&type<uint32_t(FxType::Count)&&FxType(type)!=FxType::Random;node.effect.type=FxType(type);
  node.effect.value=uint8_t(r.get(1));r.ok&=node.effect.value<=100;node.rank=uint8_t(r.get(1));
  node.x=r.get(2)/65535.f;node.y=r.get(2)/65535.f;
  int stored=int(r.get(1)),index=0;std::vector<std::vector<uint8_t>> values;
  for(int i=0;i<stored&&r.ok;i++){auto len=r.get(2);if(len>4096){r.ok=false;break;}std::vector<uint8_t>v(len);for(auto&x:v)x=uint8_t(r.get(1));values.push_back(std::move(v));}
  members(node.settings,[&](uint8_t*p,size_t len){if(index<int(values.size())){auto&v=values[index];for(size_t i=0;i<std::min(len,v.size());i++)p[i]=std::min<uint8_t>(v[i],100);}index++;});
 }
 // AMLB1 stored 9 vertices (IN, OUT, seven nodes); AMLB2 stores its own count.
 int vertices=version>=2?int(r.get(1)):9;r.ok&=vertices>=9&&vertices<=64;
 for(int a=0;a<vertices&&r.ok;a++)for(int b=0;b<vertices;b++){auto v=r.get(1);r.ok&=v<=100;if(a<LifebloodVertices&&b<LifebloodVertices)n.link[a][b]=uint8_t(v);}
 if(!r.ok)return false;
 // Builds 29-34 could store a snapshot taken while angiogenesis was absorbing a node,
 // with vessels still leading to it. Such vessels are dropped and the flow repaired
 // (fixed seed, so the same file always loads the same way); nothing else changes.
 if(!lifebloodValid(n)){uint32_t seed=0x5a1e;lifebloodRepair(n,seed);}
 return lifebloodValid(n);
}
}
std::string lifebloodPath(const std::string&root,int slot){if(slot<0||slot>=LifebloodSessionSlots)return {};char name[32];snprintf(name,sizeof name,"slot-%02d.amalthea",slot+1);return (std::filesystem::path(root)/name).string();}
bool saveLifeblood(const LifebloodSession&s,const std::string&path,std::string&error){
 Writer w;w.b={'A','M','L','B',2,0,0,0,0};
 w.put(s.tempo,2);w.put(s.character,1);w.put(s.maxNodes,1);w.put(s.growth,1);w.put(s.mix,1);w.put(s.seed,4);w.put(uint32_t(s.activeSnapshot+1),1);w.put(s.path,1);
 writeNetwork(w,s.network);
 for(int i=0;i<LifebloodSnapshots;i++){w.put(s.stored[i],1);if(s.stored[i])writeNetwork(w,s.snapshots[i]);}
 uint32_t c=crc(w.b,9);for(int i=0;i<4;i++)w.b[5+i]=uint8_t(c>>(i*8));
 std::error_code ec;auto dir=std::filesystem::path(path).parent_path();if(!dir.empty())std::filesystem::create_directories(dir,ec);if(ec){error="CANNOT CREATE LIFEBLOOD BANK";return false;}
 auto tmp=path+".tmp";FILE*f=fopen(tmp.c_str(),"wb");if(!f){error="CANNOT OPEN LIFEBLOOD SESSION";return false;}
 bool ok=fwrite(w.b.data(),1,w.b.size(),f)==w.b.size()&&fflush(f)==0&&fsync(fileno(f))==0;if(fclose(f))ok=false;if(ok)ok=rename(tmp.c_str(),path.c_str())==0;
 if(!ok){remove(tmp.c_str());error="LIFEBLOOD SAVE FAILED";}return ok;
}
bool loadLifeblood(LifebloodSession&out,const std::string&path,std::string&error){
 std::ifstream f(path,std::ios::binary);if(!f){error="NO LIFEBLOOD SESSION";return false;}
 f.seekg(0,std::ios::end);auto size=f.tellg();if(size<9||size>4000000){error="INVALID LIFEBLOOD SIZE";return false;}f.seekg(0);
 std::vector<uint8_t>b(size);if(!f.read(reinterpret_cast<char*>(b.data()),size)){error="LIFEBLOOD READ FAILED";return false;}
 if(std::string(b.begin(),b.begin()+4)!="AMLB"||b[4]<1||b[4]>2){error="INVALID LIFEBLOOD HEADER";return false;}
 const int version=b[4];
 uint32_t stored=b[5]|b[6]<<8|b[7]<<16|uint32_t(b[8])<<24;if(crc(b,9)!=stored){error="LIFEBLOOD CHECKSUM";return false;}
 Reader r{b,9};LifebloodSession s;
 s.tempo=uint16_t(r.get(2));s.character=uint8_t(r.get(1));s.maxNodes=uint8_t(r.get(1));s.growth=uint8_t(r.get(1));s.mix=uint8_t(r.get(1));s.seed=r.get(4);s.activeSnapshot=int(r.get(1))-1;
 if(version>=2)s.path=uint8_t(r.get(1));
 else s.path=lifebloodRhythmic(s.character)?PathRhythmic:PathAbstract;   // AMLB1: the path its CHARACTER implies
 r.ok&=s.tempo>=30&&s.tempo<=300&&s.character<LifebloodCharacters&&s.maxNodes>=3&&s.maxNodes<=LifebloodMaxNodes&&s.growth<=100&&s.mix<=100&&s.activeSnapshot<LifebloodSnapshots&&s.path<LifebloodPaths;
 if(r.ok)r.ok=readNetwork(r,s.network,version);
 for(int i=0;i<LifebloodSnapshots&&r.ok;i++){s.stored[i]=r.get(1);if(s.stored[i])r.ok=readNetwork(r,s.snapshots[i],version);}
 if(!r.ok||r.at!=b.size()){error="INVALID LIFEBLOOD CONTENT";return false;}
 if(!s.seed)s.seed=1;
 s.network.generation=out.network.generation+1;out=std::move(s);return true;
}
std::array<uint8_t,LifebloodSessionSlots> scanLifeblood(const std::string&root){
 std::array<uint8_t,LifebloodSessionSlots> state{};
 for(int i=0;i<LifebloodSessionSlots;i++){auto p=lifebloodPath(root,i);std::error_code ec;if(std::filesystem::exists(p,ec)){LifebloodSession s;std::string e;state[i]=loadLifeblood(s,p,e)?1:2;}}
 return state;
}
}
