#include "lifeblood.hpp"
#include "fx_audio.hpp"
#include "ui.hpp"
#include "effects.hpp"
#include "ported_inserts.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace amalthea;
static void check(bool x,const char*s){if(!x){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static uint8_t pathOf(int c){return lifebloodRhythmic(c)?PathRhythmic:PathAbstract;}
static int harshness(const LifebloodNetwork&n){int h=0;for(auto&k:n.node)if(k.active)h+=lifebloodHarshness(k.effect.type);return h;}
static int cost(const LifebloodNetwork&n){int c=0;for(auto&k:n.node)if(k.active)c+=lifebloodCost(k.effect.type);return c;}
static bool recipeRules(const LifebloodNetwork&n){
 int spaces=0;for(int k=0;k<LifebloodMaxNodes;k++){auto&node=n.node[k];if(!node.active)continue;auto t=node.effect.type;
  spaces+=t==FxType::Space;
  for(int w=0;w<LifebloodVertices;w++)if(n.link[2+k][w]){
   if(t==FxType::Space&&w!=LifebloodOut)return false;   // reverb drains only into OUT
   if(w>=2&&lifebloodFamily(t)==LifebloodFamily::Drive&&lifebloodFamily(n.node[w-2].effect.type)==LifebloodFamily::Drive)return false;
  }
  if(t==FxType::Clouds&&node.settings.clouds[7]!=0)return false;   // never frozen
  if(t!=FxType::Flanger&&t!=FxType::Phaser&&lifebloodFamily(t)!=LifebloodFamily::Rhythm&&(node.effect.value<50||node.effect.value>100))return false;   // clearly audible mixes
 }
 return spaces<=1;
}
static float run(FxAudio&audio,int blocks,float freq,float*peakOut=nullptr,double*energy=nullptr){
 static long long frame=0;float input[256*2],out[512];float peak=0;
 for(int b=0;b<blocks;b++){for(int i=0;i<256;i++){float v=.3f*std::sin(6.2831853f*freq*(frame+i)/44100);input[i*2]=input[i*2+1]=v;}frame+=256;
  audio.feed(input,256,1,44100);audio.render(out,256);
  for(float v:out){if(!std::isfinite(v))return -1;peak=std::max(peak,std::abs(v));if(energy)*energy+=v*v;}}
 if(peakOut)*peakOut=peak;return peak;
}
int main(int argc,char**argv){
 DeviceSettings device;
 // Recipes: every character, every node ceiling, many seeds.
 for(int character=0;character<LifebloodCharacters;character++)for(int maxNodes=3;maxNodes<=LifebloodMaxNodes;maxNodes++)for(uint32_t i=1;i<=400;i++){
  uint32_t seed=i*2654435761u+character*977+maxNodes;auto n=growLifeblood({uint8_t(character),uint8_t(maxNodes),pathOf(uint8_t(character))},seed,device);
  check(lifebloodValid(n),"every grown network flows from IN to OUT without loops");
  check(n.count()>=3&&n.count()<=maxNodes,"node count within 3 .. NODES");
  check(character==CharacterAny?n.character>CharacterAny:n.character==character,"network keeps the recipe that grew it");
  check(harshness(n)<=4&&(n.character==CharacterGrit||n.character==CharacterMayhem||n.character==CharacterIndustrial||harshness(n)<=3),"harshness budget per recipe");
  if(lifebloodRhythmic(n.character)){bool rhythm=false;for(auto&k:n.node)rhythm|=k.active&&lifebloodFamily(k.effect.type)==LifebloodFamily::Rhythm;check(rhythm,"rhythmic recipes always carry a cut or gate");}
  check(cost(n)<=20,"CPU weight within the ceiling");
  check(recipeRules(n),"reverb last, no drive into drive, bounded mixes");
  for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active){auto t=n.node[k].effect.type;if(t==FxType::QDelay||t==FxType::ChowMatrix){
   for(int page=0;page<(t==FxType::ChowMatrix?9:1);page++)for(int f=1;f<insertParameterCount(t,page,n.node[k].settings);f++)
    if(std::string(insertParameterName(t,f,page,n.node[k].settings)).find("FEEDBACK")!=std::string::npos){auto e=n.node[k].effect;auto s=n.node[k].settings;check(insertParameter(e,s,f,page)<=60,"network delays keep finite repeats");}}}
 }
 {uint32_t a=77,b=77;auto x=growLifeblood({CharacterDrift,6,pathOf(CharacterDrift)},a,device),y=growLifeblood({CharacterDrift,6,pathOf(CharacterDrift)},b,device);check(!memcmp(&x.link,&y.link,sizeof x.link)&&x.count()==y.count(),"growth is deterministic for a seed");}
 {DeviceSettings off;for(auto t:{FxType::Space,FxType::Clouds,FxType::Fuzz})off.disabledInserts.set(int(t));
  for(uint32_t i=1;i<200;i++){uint32_t seed=i*7919;auto n=growLifeblood({CharacterSpace,7,pathOf(CharacterSpace)},seed,off);for(auto&k:n.node)if(k.active)check(off.insertEnabled(k.effect.type),"disabled inserts never grow");}}
 // Y: same nodes and vessels, new parameters.
 {uint32_t seed=99;auto n=growLifeblood({CharacterBloom,6,pathOf(CharacterBloom)},seed,device);auto before=n;reseedLifeblood(n,seed);
  bool differ=false;for(int k=0;k<LifebloodMaxNodes;k++){check(n.node[k].active==before.node[k].active&&n.node[k].effect.type==before.node[k].effect.type,"Y keeps every node's effect");differ|=memcmp(&n.node[k].settings,&before.node[k].settings,sizeof n.node[k].settings)!=0;}
  check(!memcmp(&n.link,&before.link,sizeof n.link)&&differ,"Y keeps the vessels and redraws parameters");}
 // Angiogenesis: twenty simulated minutes at full growth stay valid and bounded.
 for(int character=1;character<LifebloodCharacters;character++){
  uint32_t seed=character*31337;auto n=growLifeblood({uint8_t(character),6,pathOf(uint8_t(character))},seed,device);Angiogenesis g;g.active=true;g.seed=character*17;g.start(n);
  int changes=0,maxCount=0,minCount=99;auto first=n;bool topologyChanged=false;
  for(int i=0;i<30*60*20/LifebloodCharacters;i++){
   if(g.step(n,1/30.f,100,6,device))changes++;
   check(lifebloodValid(n),"angiogenesis keeps the network valid after every step");
   int live=0;for(int k=0;k<LifebloodMaxNodes;k++)if(n.node[k].active&&g.wither[k]<=0)live++;
   maxCount=std::max(maxCount,live);minCount=std::min(minCount,live);
   for(auto&k:n.node)if(k.active)check(k.x>=.08f&&k.x<=.92f&&k.y>=.08f&&k.y<=.92f,"nodes bounce inside the field");
   topologyChanged|=memcmp(&n.link,&first.link,sizeof n.link)!=0;
  }
  check(changes>100&&topologyChanged,"vessels sprout and wither");
  check(maxCount<=6&&minCount>=LifebloodFloor,"node count stays between the floor and NODES");
  check(cost(n)<=20,"angiogenesis respects the CPU ceiling");
 }
 // FX appliance draws share the bound: QDELAY's heavy stages off, MATRIX two nodes.
 {DeviceSettings only;for(auto t:selectableEffects())if(t!=FxType::QDelay&&t!=FxType::ChowMatrix&&t!=FxType::None)only.disabledInserts.set(int(t));
  FxRack r;InsertSettings plain{};for(int i=0;i<200;i++){int slot=i%12;randomizeFxSlot(r,slot,false,only);auto&s=r.slots[slot];
   if(s.effect.type==FxType::ChowMatrix){check(s.settings.matrix[0]==plain.matrix[0],"FX MATRIX keeps its default delay type");for(int n=2;n<8;n++)check(s.settings.nodes[n][0]==0,"FX MATRIX keeps two nodes");}
   if(s.effect.type==FxType::QDelay)for(int f=1;f<insertParameterCount(FxType::QDelay,0,s.settings);f++){std::string name=insertParameterName(FxType::QDelay,f,0,s.settings);
    if(name.rfind("PHASER",0)==0||name.rfind("CRUSH",0)==0||name.rfind("DIFF",0)==0||name.rfind("DIST",0)==0||name=="PITCH SHIFT"){Effect a=s.effect,b=s.effect;auto p2=plain;check(insertParameter(a,s.settings,f,0)==insertParameter(b,p2,f,0),"FX QDELAY heavy stages stay off");}}
   randomizeFxSequence(r,slot);for(int st=0;st<16;st++){Effect e;InsertSettings x;resolveFxStep(r.slots[slot],st,e,x);if(e.type==FxType::ChowMatrix)for(int n=2;n<8;n++)check(x.nodes[n][0]==0,"sequenced MATRIX locks keep two nodes");}
  }}
 // PATH: every recipe a path grows belongs to it; RHYTHMIC keeps every effect at least 72% wet.
 for(int path=0;path<LifebloodPaths;path++)for(uint32_t i=1;i<300;i++){uint32_t seed=i*40503u+path;auto n=growLifeblood({CharacterAny,7,uint8_t(path)},seed,device);
  check(lifebloodOnPath(n.character,path),"ANY grows a recipe of the chosen PATH");
  if(path==PathRhythmic)for(auto&k:n.node)if(k.active&&k.effect.type!=FxType::Flanger&&k.effect.type!=FxType::Phaser)check(k.effect.value>=72,"RHYTHMIC effects are clearly wet");}
 // FM1 tracks: vessels start only from heard tracks; every heard track is routed somewhere.
 for(uint32_t i=1;i<400;i++){uint32_t seed=i*7717u;int inputs=i%2?6:3;uint8_t heard=uint8_t((i*37)%((1u<<inputs)-1)+1);
  auto n=growLifeblood({CharacterAny,7,uint8_t(i%2),uint8_t(inputs),heard},seed,device);
  check(n.inputs==inputs&&lifebloodValid(n)&&!n.link[LifebloodIn][LifebloodOut],"track inputs replace the stereo IN");
  for(int t=0;t<LifebloodTracks;t++){bool any=false;for(auto g:n.link[LifebloodTrackBase+t])any|=g>0;
   if(t<inputs&&(heard>>t&1))check(any,"every heard track is routed");else check(!any,"silent and absent tracks carry no vessel");}
  for(int k=0;k<3;k++){rewireLifeblood(n,seed);check(lifebloodValid(n),"rewiring keeps track routing valid");
   for(int t=0;t<LifebloodTracks;t++){bool any=false;for(auto g:n.link[LifebloodTrackBase+t])any|=g>0;check(any==(t<inputs&&(heard>>t&1)),"rewires route only and all heard tracks");}}
  Angiogenesis g;g.active=true;g.seed=i;g.start(n);for(int f=0;f<600;f++){g.step(n,1/30.f,100,7,device);check(lifebloodValid(n),"angiogenesis keeps multi-input networks valid");}
  for(int t=0;t<LifebloodTracks;t++)if(!(t<inputs&&(heard>>t&1)))for(auto v:n.link[LifebloodTrackBase+t])check(!v,"angiogenesis never routes a silent track");
 }
 {uint32_t seed=5;auto n=growLifeblood({CharacterCut,6,PathRhythmic},seed,device);auto links=n.count();
  lifebloodSetInputs(n,6,0b101001,seed);check(n.inputs==6&&lifebloodValid(n)&&n.count()==links,"a stereo network moves onto six tracks");
  lifebloodSetInputs(n,3,0b011,seed);check(lifebloodValid(n),"onto three tracks");for(int t=3;t<6;t++)for(auto v:n.link[LifebloodTrackBase+t])check(!v,"tracks beyond the layout are unused");
  lifebloodSetInputs(n,0,0,seed);check(n.inputs==0&&lifebloodValid(n),"back onto the stereo IN");
  lifebloodSetInputs(n,6,0b000001,seed);n.heard=0b100001;check(lifebloodConnectHeard(n,seed)&&n.link[LifebloodTrackBase+5][LifebloodOut],"a newly heard track joins straight to OUT");}
 // Sessions: snapshots round trip; damage is rejected.
 auto dir=std::filesystem::temp_directory_path()/("amalthea-lifeblood-"+std::to_string(getpid()));std::string error;
 {LifebloodSession s;uint32_t seed=5;s.network=growLifeblood({CharacterSpace,7,pathOf(CharacterSpace)},seed,device);s.tempo=97;s.character=CharacterPulse;s.maxNodes=5;s.growth=70;s.mix=64;
  s.snapshots[3]=growLifeblood({CharacterGrit,4,pathOf(CharacterGrit)},seed,device);s.stored[3]=true;s.snapshots[31]=s.network;s.stored[31]=true;s.activeSnapshot=31;
  auto path=lifebloodPath(dir.string(),4);check(saveLifeblood(s,path,error),"session saves");
  LifebloodSession loaded;check(loadLifeblood(loaded,path,error),"session loads");
  check(loaded.tempo==97&&loaded.character==CharacterPulse&&loaded.maxNodes==5&&loaded.growth==70&&loaded.mix==64&&loaded.activeSnapshot==31,"settings survive");
  check(loaded.stored.count()==2&&loaded.stored[3]&&loaded.stored[31],"stored snapshots survive");
  for(int k=0;k<LifebloodMaxNodes;k++){auto&a=s.snapshots[3].node[k],&b=loaded.snapshots[3].node[k];check(a.active==b.active,"node slots survive");if(!a.active)continue;
   check(a.effect.type==b.effect.type&&a.effect.value==b.effect.value&&a.rank==b.rank&&!memcmp(&a.settings,&b.settings,sizeof a.settings)&&std::abs(a.x-b.x)<.001f,"node effect, parameters and place survive");}
  check(!memcmp(&loaded.network.link,&s.network.link,sizeof s.network.link),"vessels survive");
  auto states=scanLifeblood(dir.string());check(states[4]==1&&states[0]==0,"bank scan finds the saved slot");
  // A snapshot stored by builds 29-34 mid-absorption: vessels to a node no longer there.
  {LifebloodSession broken=s;auto&net=broken.snapshots[3];int k=0;while(!net.node[k].active)k++;net.node[k].active=false;
   check(!lifebloodValid(net),"fixture leaves vessels to a missing node");auto bp=lifebloodPath(dir.string(),9);check(saveLifeblood(broken,bp,error),"fixture saves");
   LifebloodSession healed;check(loadLifeblood(healed,bp,error)&&lifebloodValid(healed.snapshots[3])&&healed.snapshots[3].count()==net.count(),"such a session loads, its vessels repaired");}
  {std::fstream f(path,std::ios::in|std::ios::out|std::ios::binary);f.seekp(40);char x='\x7f';f.write(&x,1);}
  check(!loadLifeblood(loaded,path,error)&&scanLifeblood(dir.string())[4]==2,"a damaged session is refused and marked");
  std::filesystem::remove_all(dir);}
 // Gestures.
 {Editor e;e.page=Page::Appliances;e.action(Action::Right);e.action(Action::Right);check(e.applianceItem==2,"third tile is LIFEBLOOD");
  e.action(Action::Left);e.action(Action::Left);e.action(Action::Left);check(e.applianceItem==2,"home wraps across three tiles");
  e.action(Action::Enter);check(e.inLifeblood()&&e.page==Page::LifebloodField&&e.lifeblood,"Enter opens LIFEBLOOD");
  auto&s=*e.lifeblood;check(s.network.count()==0&&s.network.link[LifebloodIn][LifebloodOut]==100&&!s.angiogenesis.active,"starts blank: IN straight to OUT, stopped");
  e.action(Action::TrackSettings);check(!s.angiogenesis.active,"B on an empty network does nothing");
  auto gen=s.network.generation;e.action(Action::RandomizerOptions);check(s.network.count()>=3&&s.network.generation!=gen&&lifebloodValid(s.network),"short X grows a new network");
  auto grown=s.network;e.action(Action::Master);check(!memcmp(&s.network.link,&grown.link,sizeof grown.link)&&s.network.generation==grown.generation,"Y keeps the network");
  {auto before=s.network;e.action(Action::ColumnSettings);bool same=true;
   for(int k=0;k<LifebloodMaxNodes;k++){check(s.network.node[k].active==before.node[k].active&&s.network.node[k].effect.type==before.node[k].effect.type&&!memcmp(&s.network.node[k].settings,&before.node[k].settings,sizeof before.node[k].settings),"A keeps every node and its parameters");}
   for(int i=0;i<12&&same;i++){same=!memcmp(&s.network.link,&before.link,sizeof before.link);if(same)e.action(Action::ColumnSettings);}
   check(!same&&lifebloodValid(s.network)&&s.network.generation==before.generation&&e.page==Page::LifebloodField,"A rewires the vessels in place");}
  {uint32_t seed=21;for(int i=0;i<500;i++){auto n=growLifeblood({uint8_t(i%LifebloodCharacters),7,pathOf(uint8_t(i%LifebloodCharacters))},seed,device);rewireLifeblood(n,seed);check(lifebloodValid(n)&&recipeRules(n),"every rewire keeps the network valid and the recipe rules");}}
  e.action(Action::ShiftDown);e.action(Action::ColumnSettings);e.action(Action::ShiftUp);check(e.page==Page::LifebloodSettings,"Shift+A opens session settings");
  e.action(Action::Right);check(e.notice=="TEMPO"&&e.noticeDetail=="121 BPM"&&e.noticeDetailSerial==e.noticeSerial,"a changed setting shows a large value overlay");
  e.action(Action::Left);
  e.lbClockBpm=127;e.action(Action::Right);check(s.tempo==120&&e.noticeDetail=="FM1 CLOCK 127 BPM","the FM1 clock holds the tempo; dialling names it");e.lbClockBpm=0;
  e.action(Action::Cancel);
  e.action(Action::TrackSettings);check(s.angiogenesis.active,"B starts angiogenesis");
  bool changed=false;for(int i=0;i<600;i++)changed|=e.lifebloodTick(1/30.f);check(changed,"ticks advance angiogenesis");
  e.action(Action::TrackSettings);check(!s.angiogenesis.active,"B stops it");
  e.action(Action::NextTrack);check(e.lbVertex>=2,"F2 steps to the first node");
  e.action(Action::Enter);check(e.page==Page::LifebloodNode,"Enter opens the node's parameters");
  int slot=e.lbVertex-2;auto value=s.network.node[slot].effect.value;e.action(Action::Down);e.action(Action::Right);check(s.network.node[slot].effect.value==std::min(100,value+1),"node MIX adjusts");
  e.action(Action::Cancel);check(e.page==Page::LifebloodField,"Cancel returns to the field");
  e.action(Action::Patterns);check(e.page==Page::LifebloodSnapshots,"middle tap opens snapshots");
  e.action(Action::LoadPattern);check(e.notice=="EMPTY SNAPSHOT","an empty snapshot does not load");
  auto stored=s.network;e.action(Action::SavePattern);check(s.stored[0]&&s.activeSnapshot==0&&e.lbSessionRequest==0,"hold stores; no session to persist into yet");
  e.action(Action::Home);e.action(Action::RandomizerOptions);check(memcmp(&s.network.link,&stored.link,sizeof stored.link)||s.network.count()!=stored.count(),"a new network differs");
  e.action(Action::Patterns);gen=s.network.generation;e.action(Action::LoadPattern);
  check(!memcmp(&s.network.link,&stored.link,sizeof stored.link)&&s.network.generation!=gen&&s.activeSnapshot==0,"tap recalls the snapshot as a new generation");
  e.action(Action::ShiftDown);SessionButton b;b.press(0,e);check(b.tap==Action::Mixer||b.tap==Action::Session,"shift tap reaches sessions");e.action(Action::ShiftUp);
  e.action(Action::Session);check(e.page==Page::LifebloodSessions,"shift tap opens sessions");
  {SessionButton tap;tap.press(0,e);check(tap.tap==Action::LoadSession&&tap.hold==Action::SaveSession,"session bank: tap loads, hold saves");}
  e.action(Action::LoadSession);check(e.lbSessionRequest==1,"tap requests a session load");e.lbSessionRequest=0;
  e.action(Action::Enter);check(e.page==Page::LifebloodSettings,"Enter opens session settings");
  for(int f=0;f<LifebloodSettingsFields;f++){e.action(Action::Left);e.action(Action::Down);}
  check(s.tempo==119&&s.path==PathAbstract&&s.character==CharacterBloom&&s.maxNodes==5&&s.growth==39&&s.mix==99,"each setting adjusts; CHARACTER stays on the PATH");
  e.lbActiveSession=2;e.action(Action::Cancel);e.action(Action::Patterns);e.action(Action::Right);e.action(Action::SavePattern);check(e.lbSessionRequest==3,"with a session loaded, a stored snapshot is persisted");
  e.action(Action::Play);check(e.lbBypass,"Play bypasses");e.action(Action::Play);check(!e.lbBypass,"Play restores the network");
  e.leaveAppliance();check(e.page==Page::Appliances&&!e.lifeblood,"exit discards the workspace");
  e.applianceItem=2;e.action(Action::Enter);check(e.lifeblood->network.count()==0,"re-entry is blank");}
 // Audio: blank passes dry; networks stay finite and bounded through crossfades and angiogenesis.
 {AudioStats stats;FxAudio audio(stats);stats.usbState=4;
  auto blank=blankLifeblood();blank.generation=1;check(audio.submit(blank,120,100,false,device),"blank network accepted");audio.activate(true);
  float peak=0;run(audio,200,220,&peak);check(peak>.25f&&peak<.32f,"blank network passes the input dry");
  for(int round=0;round<8;round++){uint32_t seed=round*1013+7;auto n=growLifeblood({uint8_t(1+round%5),7,pathOf(uint8_t(1+round%5))},seed,device);n.generation=2+round;
   while(!audio.submit(n,120,100,false,device))run(audio,1,220);
   double energy=0;check(run(audio,180,330,&peak,&energy)>=0,"network output is finite");check(peak<=.961f,"output stays under the limiter");check(energy>1,"a network passes sound");}
  {uint32_t seed=4242;auto n=growLifeblood({CharacterDrift,6,pathOf(CharacterDrift)},seed,device);n.generation=50;Angiogenesis g;g.active=true;g.start(n);
   for(int i=0;i<30*40;i++){if(g.step(n,1/30.f,100,6,device))while(!audio.submit(n,120,100,false,device))run(audio,1,220);run(audio,6,180+i%40);}
   check(run(audio,20,200,&peak)>=0,"forty seconds of angiogenesis stay finite");}
  // Bypass fades to the dry input.
  {auto n=blankLifeblood();uint32_t seed=3;n=growLifeblood({CharacterGrit,5,pathOf(CharacterGrit)},seed,device);n.generation=99;while(!audio.submit(n,120,100,true,device))run(audio,1,220);
   run(audio,120,220);run(audio,40,220,&peak);check(peak>.25f&&peak<.32f,"bypass passes the input dry");}
 }
 {AudioStats stats;FxAudio audio(stats);stats.usbState=1;stats.usbChannels=6;uint32_t seed=11;
  auto n=growLifeblood({CharacterGate,6,PathRhythmic,6,0b010010},seed,device);n.generation=1;audio.submit(n,120,100,false,device);audio.activate(true);
  float input[256*12],out[512];double energy=0;
  for(int b=0;b<400;b++){for(int i=0;i<256;i++)for(int c=0;c<6;c++){float v=(c==1||c==4)?.3f*std::sin((b*256+i)*(.02f+c*.01f)):0;input[(i*6+c)*2]=input[(i*6+c)*2+1]=v;}
   audio.feed(input,256,6,44100);audio.render(out,256);for(float v:out){check(std::isfinite(v),"six-track network output finite");energy+=v*v;}}
  check(stats.lifebloodHeard.load()==0b010010,"the tracks sounding are detected (2 and 5)");check(energy>1,"six-track network passes sound");}
 // Separate tracks sit where the FM1 pans them (CC 10); a pan not yet heard is the centre.
 {AudioStats stats;FxAudio audio(stats);stats.usbState=1;stats.usbChannels=6;
  auto n=blankLifeblood();n.inputs=6;n.link[LifebloodIn][LifebloodOut]=0;for(int t=0;t<6;t++)n.link[LifebloodTrackBase+t][LifebloodOut]=100;n.generation=1;
  audio.submit(n,120,100,false,device);audio.activate(true);float input[256*12],out[512];
  auto balance=[&](int blocks){double l=0,r=0;for(int b=0;b<blocks;b++){for(int i=0;i<256;i++)for(int c=0;c<6;c++){float v=c==0?.3f*std::sin((b*256+i)*.03f):0;input[(i*6+c)*2]=input[(i*6+c)*2+1]=v;}
   audio.feed(input,256,6,44100);audio.render(out,256);if(b>=blocks/2)for(int i=0;i<256;i++){l+=out[i*2]*out[i*2];r+=out[i*2+1]*out[i*2+1];}}return std::pair{l,r};};
  auto [l0,r0]=balance(120);check(l0>1&&std::fabs(10*std::log10(l0/r0))<.1,"a track whose pan is unknown is centred (not spread by its number)");
  stats.usbPan[0]=0;auto [l1,r1]=balance(60);check(l1>1&&r1<l1*1e-4,"track 1 panned hard left on the FM1 stays left");
  stats.usbPan[0]=127;auto [l2,r2]=balance(60);check(r2>1&&l2<r2*1e-4,"and hard right stays right");
  stats.usbPan[0]=64;auto [l3,r3]=balance(60);check(std::fabs(10*std::log10(l3/r3))<.1,"CC 10 64 is the centre");}
 // Loudness matching: every insert engaged at 85% keeps the input's level within 2.5 dB.
 for(auto t:selectableEffects()){if(t==FxType::None||t==FxType::Random)continue;
  InsertEffect fx(t==FxType::Space);std::unique_ptr<PortedProcessor> port;int k=portedIndex(t);PortedPointers ptr{};if(k>=0){port=std::make_unique<PortedProcessor>(t);ptr[k]=port.get();fx.attachPorted(ptr);}
  fx.configure({t,85},120);fx.setSettings(InsertSettings{});double ein=0,eout=0;long f=0;uint32_t r=1;float b[512],in[512];
  for(int blk=0;blk<44100*6/256;blk++){for(int i=0;i<256;i++,f++){r^=r<<13;r^=r>>17;r^=r<<5;float x=.18f*std::sin(f*.0314f)+.08f*std::sin(f*.113f)+((r>>8)/16777216.f-.5f)*.1f;b[2*i]=x;b[2*i+1]=x*.8f;}
   std::copy(b,b+512,in);fx.processBlock(b,256);if(blk>44100*3/256)for(int i=0;i<512;i++){ein+=in[i]*in[i];eout+=b[i]*b[i];}}
  double db=10*std::log10(eout/ein);if(std::fabs(db)>2.5)fprintf(stderr,"%s %+.1f dB\n",fxName(t),db);check(std::fabs(db)<=2.5,"an engaged effect keeps the input's loudness");}
 // Every screen renders opaque at both sizes.
 {AudioStats stats;Editor e;e.applianceItem=2;e.page=Page::Appliances;
  for(int width:{640,1024})for(int view=0;view<6;view++){
   Editor v;v.page=Page::Appliances;v.applianceItem=2;if(view){v.action(Action::Enter);v.action(Action::RandomizerOptions);v.lbClock=view*.7f;}
   if(view==2){v.action(Action::NextTrack);v.action(Action::Enter);}if(view==3)v.action(Action::Patterns);if(view==4)v.action(Action::Session);if(view==5){v.action(Action::ShiftDown);v.action(Action::ColumnSettings);v.action(Action::ShiftUp);}
   if(view!=1)v.notice.clear();
   if(view==3){v.notice.clear();v.volumeBanner=true;}
   Canvas canvas(width,width*3/4);drawUI(canvas,v,stats);for(auto pixel:canvas.pixels)check(pixel>>24==255,"LIFEBLOOD screens are opaque");
   if(argc>1&&width==1024)writePPM(canvas,std::string(argv[1])+"-"+std::to_string(view)+".ppm");
  }}
 puts("PASS: recipes, angiogenesis, snapshots and sessions, gestures, network audio and screens");
}
