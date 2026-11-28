// LIFEBLOOD controls. The field page shows the network; X grows a new one, Y
// redraws every node's parameters, B starts or stops angiogenesis.
#include "lifeblood.hpp"
#include "fx_rack.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
int pagesOf(FxType t){return t==FxType::ChowMatrix?9:t==FxType::Frequalizer?6:t==FxType::Biztortion?8:1;}
// The vertex nearest to `from` in the given direction (dx, dy), favouring ones
// straight ahead; -1 when there is none.
int nearestInDirection(const LifebloodNetwork&n,int from,float dx,float dy){
 int best=-1;float score=1e9f;
 for(int v=0;v<LifebloodVertices;v++){
  if(v==from||!n.live(v))continue;float ax=n.x(v)-n.x(from),ay=(n.y(v)-n.y(from))*.75f;
  float along=ax*dx+ay*dy,across=std::abs(ax*dy-ay*dx);if(along<=.01f)continue;
  float s=along+2.2f*across;if(s<score){score=s;best=v;}
 }
 return best;
}
}
void Editor::openLifeblood(){
 if(!lifeblood)lifeblood=std::make_shared<LifebloodSession>();
 if(!lifeblood->network.generation)lifeblood->network.generation=++lbGeneration;
 page=Page::LifebloodField;lbVertex=LifebloodIn;
}
bool Editor::lifebloodTick(float seconds){
 lbClock+=seconds;if(!lifeblood)return false;auto&s=*lifeblood;
 auto&n=s.network;bool changed=false;
 // FM1 inputs: follow the firmware's channel layout and the tracks heard (lbInputs < 0: no FM1 yet).
 if(lbInputs>=0){
  uint8_t heard=lbInputs?lbHeard:0x3f;
  if(n.inputs!=lbInputs){lifebloodSetInputs(n,lbInputs,heard,s.seed);changed=true;}
  if(n.heard!=heard){n.heard=heard;changed|=lifebloodConnectHeard(n,s.seed);}
 }
 s.angiogenesis.path=s.path;
 changed|=s.angiogenesis.step(n,seconds,s.growth,s.maxNodes,device);
 if(!n.live(lbVertex)){lbVertex=LifebloodOut;for(int v=0;v<LifebloodVertices;v++)if(lifebloodInput(v)&&n.live(v)){lbVertex=v;break;}}
 if(changed&&page==Page::LifebloodNode&&!s.network.live(lbVertex))page=Page::LifebloodField;
 if(changed)revision++;
 return changed;
}
void Editor::lifebloodAction(Action a){
 if(!lifeblood)openLifeblood();auto&s=*lifeblood;auto&n=s.network;
 auto announce=[&](const std::string&text,int ms=1500){notice=text;noticeDetail.clear();noticeMs=ms;++noticeSerial;};
 auto reseedEntropy=[&]{if(entropy){randomNext(entropy);s.seed^=entropy*0x9e3779b9u;}if(!s.seed)s.seed=1;};
 auto grow=[&]{
  reseedEntropy();bool wasActive=s.angiogenesis.active;
  n=growLifeblood({s.character,s.maxNodes,s.path,uint8_t(std::max(0,lbInputs)),lbInputs>0?lbHeard:uint8_t(0x3f)},s.seed,device);n.generation=++lbGeneration;s.activeSnapshot=-1;
  if(wasActive)s.angiogenesis.start(n);
  lbVertex=LifebloodOut;for(int v=0;v<LifebloodVertices;v++)if(lifebloodInput(v)&&n.live(v)){lbVertex=v;break;}revision++;
  announce(std::string(lifebloodCharacterName(n.character))+" / "+std::to_string(n.count())+" NODES");
 };
 if(a==Action::Home){page=Page::LifebloodField;return;}
 if(a==Action::Patterns){page=Page::LifebloodSnapshots;return;}
 if(a==Action::Session){page=Page::LifebloodSessions;return;}
 if(a==Action::Mixer)return;
 // Shift+A, from any page: session settings.
 if(a==Action::ColumnSettings&&shift&&page!=Page::LifebloodSettings){page=Page::LifebloodSettings;lbSettingsField=lbSettingsTab=0;return;}
 if(a==Action::Play){lbBypass=!lbBypass;revision++;announce(lbBypass?"BYPASS: DRY INPUT":"NETWORK ON",1000);return;}
 if(page==Page::LifebloodSnapshots||page==Page::LifebloodSessions){
  bool snapshots=page==Page::LifebloodSnapshots;int&slot=snapshots?lbSnapshotSlot:lbSessionSlot;int count=snapshots?LifebloodSnapshots:LifebloodSessionSlots;
  if(a==Action::Left)slot=(slot+count-1)%count;if(a==Action::Right)slot=(slot+1)%count;
  if(a==Action::Up)slot=(slot+count-8)%count;if(a==Action::Down)slot=(slot+8)%count;
  if(a==Action::Enter){page=Page::LifebloodSettings;lbSettingsField=lbSettingsTab=0;}
  if(a==Action::Cancel||a==Action::ColumnSettings)page=Page::LifebloodField;
  // The main loop performs the file work and reports the result.
  if(!snapshots&&a==Action::LoadSession)lbSessionRequest=1;
  if(!snapshots&&a==Action::SaveSession)lbSessionRequest=2;
  if(snapshots&&a==Action::LoadPattern){
   if(!s.stored[slot]){announce("EMPTY SNAPSHOT");return;}
   n=s.snapshots[slot];n.generation=++lbGeneration;s.activeSnapshot=slot;
   if(s.angiogenesis.active)s.angiogenesis.start(n);
   if(!n.live(lbVertex))lbVertex=LifebloodOut;revision++;announce("SNAPSHOT "+std::to_string(slot+1)+" LOADED",1200);
  }
  if(snapshots&&a==Action::SavePattern){
   // A node still fading out of angiogenesis is not part of the stored network.
   auto copy=n;bool dropped=false;
   for(int k=0;k<LifebloodMaxNodes;k++)if(s.angiogenesis.wither[k]>0){copy.node[k].active=false;dropped=true;for(int v=0;v<LifebloodVertices;v++)copy.link[v][2+k]=copy.link[2+k][v]=0;}
   if(dropped){uint32_t seed=s.seed;lifebloodRepair(copy,seed);}
   s.snapshots[slot]=copy;s.stored[slot]=true;s.activeSnapshot=slot;revision++;
   if(lbActiveSession>=0){lbSessionRequest=3;announce("SNAPSHOT "+std::to_string(slot+1)+" STORED",1400);}
   else announce("SNAPSHOT "+std::to_string(slot+1)+" STORED / SAVE A SESSION TO KEEP IT",2200);
  }
  return;
 }
 if(page==Page::LifebloodSettings){
  if(a==Action::Cancel||a==Action::ColumnSettings||a==Action::Enter){page=Page::LifebloodField;return;}
  // F1 / F2: GENERAL, COMP, EQ.
  if(a==Action::PrevTrack||a==Action::NextTrack){lbSettingsTab=(lbSettingsTab+(a==Action::NextTrack?1:LifebloodSettingsTabs-1))%LifebloodSettingsTabs;lbSettingsField=0;return;}
  const int fields=lifebloodSettingsFieldCount(lbSettingsTab);
  if(a==Action::Up)lbSettingsField=(lbSettingsField+fields-1)%fields;
  if(a==Action::Down)lbSettingsField=(lbSettingsField+1)%fields;
  int d=a==Action::Right?1:a==Action::Left?-1:0;if(!d)return;
  if(lbSettingsTab==1){
   auto&m=s.master;if(!stepMasterComp(m,lbSettingsField,d,shift))return;
   notice=lbSettingsField==1?"COMP PROFILE":masterCompFieldName(lbSettingsField);
   noticeDetail=lbSettingsField==1?compProfileName(m.compProfile):masterCompValue(m,lbSettingsField);
   noticeMs=1000;noticeDetailSerial=++noticeSerial;revision++;return;
  }
  if(lbSettingsTab==2){
   auto&m=s.master;if(!stepMasterEq(m,lbSettingsField,d,shift))return;
   notice=lbSettingsField==0?"EQ":lbSettingsField==1?"EQ PROFILE":masterBandName(lbSettingsField-2);
   int b=m.band[std::clamp(lbSettingsField-2,0,MasterBands-1)];
   noticeDetail=lbSettingsField==0?(m.eqOn?"ON":"OFF"):lbSettingsField==1?eqProfileName(m.eqProfile):(b>0?"+":"")+std::to_string(b)+" DB";
   noticeMs=1000;noticeDetailSerial=++noticeSerial;revision++;return;
  }
  // The FM1's clock sets the tempo while it runs: the session's own value waits underneath.
  if(lbSettingsField==0&&lbClockBpm>0){notice="TEMPO";noticeDetail="FM1 CLOCK "+std::to_string(lbClockBpm)+" BPM";noticeMs=1200;noticeDetailSerial=++noticeSerial;return;}
  if(lbSettingsField==0)s.tempo=uint16_t(std::clamp(int(s.tempo)+d*(shift?10:1),30,300));
  // PATH chooses the recipes; CHARACTER offers ANY and the PATH's own.
  if(lbSettingsField==1){s.path=uint8_t((s.path+d+LifebloodPaths)%LifebloodPaths);if(!lifebloodOnPath(s.character,s.path))s.character=CharacterAny;}
  if(lbSettingsField==2){int c=s.character;do c=(c+d+LifebloodCharacters)%LifebloodCharacters;while(c!=CharacterAny&&!lifebloodOnPath(c,s.path));s.character=uint8_t(c);}
  if(lbSettingsField==3)s.maxNodes=uint8_t(std::clamp(int(s.maxNodes)+d,3,LifebloodMaxNodes));
  if(lbSettingsField==4)s.growth=uint8_t(std::clamp(int(s.growth)+d*(shift?10:1),0,100));
  if(lbSettingsField==5)s.mix=uint8_t(std::clamp(int(s.mix)+d*(shift?10:1),0,100));
  // The value just set, large, as in the Tracker.
  static const char*names[]={"TEMPO","PATH","CHARACTER","NODES","GROWTH","MIX"};notice=names[lbSettingsField];
  noticeDetail=lbSettingsField==0?std::to_string(s.tempo)+" BPM":lbSettingsField==1?lifebloodPathName(s.path):lbSettingsField==2?lifebloodCharacterName(s.character):lbSettingsField==3?"UP TO "+std::to_string(s.maxNodes):lbSettingsField==4?std::to_string(s.growth)+"%":std::to_string(s.mix)+"% WET";
  noticeMs=1000;noticeDetailSerial=++noticeSerial;revision++;return;
 }
 if(page==Page::LifebloodNode){
  int slot=lbVertex-2;
  if(!lifebloodNodeVertex(lbVertex)||!n.live(lbVertex)){page=Page::LifebloodField;return;}
  auto&k=n.node[slot];int pages=pagesOf(k.effect.type);
  if(a==Action::Cancel||a==Action::TrackSettings||a==Action::ColumnSettings){page=Page::LifebloodField;return;}
  if(a==Action::Master){reseedLifebloodNode(n,slot,s.seed,s.path);revision++;announce(std::string(lifebloodLabel(k.effect.type))+": NEW PARAMETERS",1000);return;}
  if(a==Action::RandomizerOptions||a==Action::RandomizeColumn){
   // A new effect for this node, from every enabled insert; the vessels stay.
   std::vector<FxType> types;for(auto t:selectableEffects())if(t!=FxType::None&&t!=FxType::Random&&device.insertEnabled(t))types.push_back(t);
   if(types.empty())return;reseedEntropy();k.effect={types[randomNext(s.seed)%types.size()],0};k.settings=InsertSettings{};
   reseedLifebloodNode(n,slot,s.seed,s.path);lbPage=lbParameter=0;revision++;announce(fxName(k.effect.type),1200);return;
  }
  if(a==Action::PrevTrack||a==Action::NextTrack){lbPage=(lbPage+(a==Action::NextTrack?1:pages-1))%pages;lbParameter=0;return;}
  int count=1+insertParameterCount(k.effect.type,lbPage,k.settings);
  if(a==Action::Up||a==Action::Down){lbParameter=(lbParameter+(a==Action::Down?1:count-1))%count;return;}
  int d=a==Action::Right?1:a==Action::Left?-1:0;if(!d)return;
  if(lbParameter==0){
   const auto&list=selectableEffects();std::vector<FxType> types;for(auto t:list)if(t!=FxType::None&&t!=FxType::Random)types.push_back(t);
   auto at=std::find(types.begin(),types.end(),k.effect.type);int i=at==types.end()?0:int(at-types.begin());
   k.effect.type=types[(i+d+int(types.size()))%int(types.size())];k.effect.value=defaultFxMix(k.effect.type);k.settings=InsertSettings{};lbPage=0;
   notice="EFFECT";noticeDetail=fxName(k.effect.type);
  }else{
   int field=lbParameter-1;auto&v=insertParameter(k.effect,k.settings,field,lbPage);
   int step=insertParameterStep(k.effect.type,field,lbPage,k.settings)*(shift&&!steppedInsertParameter(k.effect.type,field,lbPage,k.settings)?10:1);
   v=uint8_t(std::clamp(int(v)+d*step,0,100));
   notice=insertParameterName(k.effect.type,field,lbPage,k.settings);noticeDetail=insertParameterDescription(k.effect.type,field,lbPage,k.settings,v);
  }
  noticeMs=1000;noticeDetailSerial=++noticeSerial;revision++;return;
 }
 // The field.
 if(a==Action::RandomizerOptions||a==Action::RandomizeColumn){grow();return;}
 if(a==Action::Master){
  if(!n.count()){announce("PRESS X TO GROW A NETWORK");return;}
  reseedEntropy();reseedLifeblood(n,s.seed,s.path);s.activeSnapshot=-1;revision++;announce("NEW PARAMETERS",1000);return;
 }
 if(a==Action::TrackSettings){
  auto&g=s.angiogenesis;
  if(!g.active&&!n.count()){announce("PRESS X TO GROW A NETWORK");return;}
  g.active=!g.active;if(g.active){g.start(n);s.activeSnapshot=-1;}revision++;
  announce(g.active?"ANGIOGENESIS":"ANGIOGENESIS STOPPED",1000);return;
 }
 if(a==Action::ColumnSettings){
  // A: new vessels between the same nodes.
  if(!n.count()){announce("PRESS X TO GROW A NETWORK");return;}
  reseedEntropy();unsigned dormant=0;for(int k=0;k<LifebloodMaxNodes;k++)if(s.angiogenesis.wither[k]>0)dormant|=1u<<k;
  // A rewires the inputs too: which heard tracks feed which vessels, which pass straight.
  if(lbInputs>0)n.heard=lbHeard;rewireLifeblood(n,s.seed,dormant);s.activeSnapshot=-1;revision++;announce("REWIRED",900);return;
 }
 if(a==Action::Enter){
  if(lifebloodNodeVertex(lbVertex)&&n.live(lbVertex)){page=Page::LifebloodNode;lbParameter=lbPage=0;}
  else if(lbVertex>=LifebloodTrackBase){int t=lbVertex-LifebloodTrackBase;announce("FM1 TRACK "+std::to_string(t+1)+(n.source(lbVertex)&&(n.heard>>t&1)?" / HEARD":" / SILENT"),1200);}
  else announce(lbVertex==LifebloodIn?"IN: FM1 USB AUDIO":"OUT: HEADPHONES AND USB",1200);
  return;
 }
 if(a==Action::PrevTrack||a==Action::NextTrack){
  // F1 / F2 step through the vertices in flow order.
  std::vector<int> order;for(int v=0;v<LifebloodVertices;v++)if(lifebloodInput(v)&&n.live(v))order.push_back(v);
  std::vector<int> nodes;for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active)nodes.push_back(2+k);
  std::sort(nodes.begin(),nodes.end(),[&](int a,int b){return n.rank(a)<n.rank(b);});
  order.insert(order.end(),nodes.begin(),nodes.end());order.push_back(LifebloodOut);
  auto at=std::find(order.begin(),order.end(),lbVertex);int i=at==order.end()?0:int(at-order.begin());
  lbVertex=order[(i+(a==Action::NextTrack?1:int(order.size())-1))%int(order.size())];return;
 }
 float dx=a==Action::Left?-1:a==Action::Right?1:0,dy=a==Action::Up?-1:a==Action::Down?1:0;
 if(dx||dy){int v=nearestInDirection(n,lbVertex,dx,dy);if(v>=0)lbVertex=v;}
}
}
