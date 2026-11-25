#include "legacy_project.hpp"
#include "audio.hpp"
#include "sessions.hpp"
#include "ui.hpp"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace amalthea;
static void check(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static void tick(Audio&a,int n){float data[64];while(a.stats.advances[0].load()<uint64_t(n+1))a.render(data,32);}
int main(){
 int i=0;for(int b=1;b<=8;b++)for(int a=1;a<=b;a++){auto c=cycleCondition(i);check(c.occurrence==a&&c.every==b&&cycleConditionIndex(c)==i,"Bigorna positive ordering");i++;}
 check(i==36&&Columns==5,"36 positive choices and five visible columns");
 for(int column=0;column<Columns;column++){
  Editor e;e.project=blankProject();e.column=column;e.track=3;e.project.tracks[3].steps[0].note=60;
  auto original=encodeProject(e.project);e.action(Action::Enter);check(e.editing&&e.eventTab==0,"Enter opens Value tab");auto pending=e.pending;auto audition=e.auditionSerial;
  e.action(Action::NextTrack);if(column==0||column==2||column==3){check(e.lengthTab(),"F2 on NOTE / FX selects Length first");e.action(Action::NextTrack);}check(e.track==3&&e.editing&&e.conditionTab(),"F2 selects Condition without changing track");
  for(int option=0;option<36;option++){check(cycleConditionIndex(eventCondition(e.pending,column))==option,"tab traverses full positive catalog");e.action(Action::Up);}
  check((column==0||column==4)==(e.auditionSerial>audition)&&e.pending.note==pending.note,"condition changes preview only from the NOTE column, without changing pitch");
  for(int n=0;n<50;n++)e.action(Action::Down);e.action(Action::Up);
  check(eventCondition(e.pending,column).every==2&&encodeProject(e.project)==original,"pending condition is transactional");
  e.action(Action::PrevTrack);if(column==0||column==2||column==3){check(e.lengthTab(),"F1 on NOTE / FX steps back through Length");e.action(Action::PrevTrack);}check(e.track==3&&e.eventTab==0&&e.editing,"F1 returns to Value");
  e.action(Action::Enter);auto committed=e.project.tracks[3].steps[0];check(eventCondition(committed,column).every==2,"Enter commits selected cell condition");
  for(int other=0;other<Columns;other++)if(other!=column)check(eventCondition(committed,other).every==1,"sibling cells untouched");
  e.action(Action::Undo);check(encodeProject(e.project)==original,"one undo restores condition and value");
  e.action(Action::Enter);e.action(Action::NextTrack);e.action(Action::Up);e.action(Action::Home);check(encodeProject(e.project)==original&&!e.editing,"Home discards pending condition");
 }
 // Independent conditions: NOTE 1:2, INST 2:3, FX1 1:2, FX2 2:2, MOD 1:4.
 Project p=blankProject();auto&t=p.tracks[0];t.length=1;t.instrument=0;auto&s=t.steps[0];s.note=60;s.instrument=1;s.condition={1,2};s.modifier={12,ModifierType::Offset};eventCondition(s,1)={2,3};eventCondition(s,4)={1,4};
 Audio audio;audio.submit(p,true);int notes=0;
 for(int cycle=0;cycle<24;cycle++){tick(audio,cycle);if(cycle%2==0){notes++;check(audio.stats.lastNote[0]==(cycle%4==0?72:60),"MOD follows its own event condition, independent of NOTE");check(audio.stats.lastInstrument[0]==(cycle%3==1?1:0),"INST condition selects override or track default");}check(audio.stats.triggers[0]==unsigned(notes),"NOTE condition gates only notes");}
 // Distinct FX conditions restore matching track defaults without touching peers.
 p=blankProject();auto&r=p.tracks[0];r.length=1;r.steps[0].note=60;r.steps[0].fx={Effect{FxType::Volume,0},Effect{FxType::Pan,0}};eventCondition(r.steps[0],2)={1,2};eventCondition(r.steps[0],3)={1,3};
 Audio fx;fx.submit(p,true);float data[512];
 for(int cycle=0;cycle<8;cycle++){
  tick(fx,cycle);fx.render(data,256);double power=0,left=0,right=0;for(int n=0;n<256;n++){left+=data[2*n]*data[2*n];right+=data[2*n+1]*data[2*n+1];}power=left+right;
  if(cycle%2==0)check(power==0,"FX1 1:2 applies Volume0 on its own cycles");
  else if(cycle%3==0)check(left>.001&&right==0,"FX2 independently pans on its eligible cycle");
  else check(power>.001&&std::abs(left-right)<1e-5,"both inactive FX locks restore volume and pan defaults");
 }
 // Faster FX clock evaluates against its own cycle, between NOTE ticks.
 p=blankProject();p.tracks[0].columns[0]={1,0,0};p.tracks[0].columns[2]={1,3,0};p.tracks[0].steps[0].note=60;p.tracks[0].steps[0].fx[0]={FxType::Volume,0};eventCondition(p.tracks[0].steps[0],2)={2,2};
 Audio fast;fast.submit(p,true);fast.render(data,256);double before=0;for(float v:data)before+=v*v;check(before>.001,"FX cycle1 bypass despite NOTE cycle1");
 std::vector<float>buffer(6000*2);fast.render(buffer.data(),6000);fast.render(data,256);double after=0;for(float v:data)after+=v*v;check(fast.stats.triggers[0]==1&&after==0,"FX cycle2 changes held sound without retriggering NOTE");
 // Project and full session/pattern bank persistence, synthetic files only.
 Editor saved;saved.project=p;for(int c=0;c<Columns;c++){eventCondition(saved.project.tracks[2].steps[11],c)=cycleCondition(20+c);saved.project.tracks[2].columns[c]={uint8_t(c+3),uint8_t(c),uint8_t(c+1)};saved.density[2][c]=10+c;}
 saved.savePattern(7);auto dir=std::filesystem::temp_directory_path()/("amalthea-cell-test-"+std::to_string(getpid()));std::filesystem::create_directories(dir);SessionBank bank((dir/"session.amalthea").string());std::string error;check(bank.save(2,saved,error),"save synthetic AMSB2 bank");Editor loaded;check(bank.load(2,loaded,error),"load AMSB2 bank");check(encodeProject(saved.project)==encodeProject(loaded.project)&&saved.density==loaded.density,"cell conditions, geometry and density roundtrip");check(loaded.patterns[7].state&&encodeProject(*loaded.patterns[7].state)==encodeProject(*saved.patterns[7].state),"pattern snapshots preserve every cell condition");

 // Synthetic legacy AMSB1/AMAL12: drop v13 event metadata, keep old six-lane
 // geometry positions and density. The former shared condition migrates to NOTE.
 auto crc=[](const std::vector<uint8_t>&b,size_t first){uint32_t c=~0u;for(size_t n=first;n<b.size();n++){c^=b[n];for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;};
 auto put=[](std::vector<uint8_t>&b,uint32_t v,int n){while(n--){b.push_back(v&255);v>>=8;}};
 auto legacyProject=legacyBankBytes(encodeProject(saved.project));legacyProject.resize(legacyProject.size()-EventConditionBytes-LegacyTrackPatchBytes);legacyProject[4]=12;auto sum=crc(legacyProject,12);for(int n=0;n<4;n++)legacyProject[8+n]=(sum>>(8*n))&255;
 std::vector<uint8_t>old={'A','M','S','B',1,0,0,0,0,0,0,0};put(old,120,2);put(old,0,1);put(old,1,1);put(old,0,1);put(old,0,2);put(old,255,1);put(old,255,1);put(old,legacyProject.size(),4);old.insert(old.end(),legacyProject.begin(),legacyProject.end());
 for(const auto&density:saved.density)for(int c=0;c<6;c++)put(old,c==4?100:density[c==5?4:c],1);
 for(int slot=0;slot<32;slot++){put(old,0,1);put(old,0,2);put(old,255,1);put(old,255,1);}sum=crc(old,12);for(int n=0;n<4;n++)old[8+n]=(sum>>(8*n))&255;
 std::ofstream legacy(bank.path(1),std::ios::binary);legacy.write((char*)old.data(),old.size());legacy.close();Editor migrated;check(bank.load(1,migrated,error),"legacy AMSB1 and AMAL12 remain readable");
 check(migrated.density==saved.density&&migrated.project.tracks[2].columns[4].length==saved.project.tracks[2].columns[4].length,"legacy MOD geometry/density maps past removed condition lane");
 for(int c=1;c<Columns;c++)check(eventCondition(migrated.project.tracks[2].steps[11],c).every==1,"legacy non-NOTE cells start unconditional");
 AudioStats stats;for(int width:{640,1024})for(int c=0;c<Columns;c++)for(int tab=0;tab<2;tab++){Editor e;e.column=c;e.action(Action::Enter);if(tab)e.action(Action::NextTrack);Canvas view(width,width*3/4);drawUI(view,e,stats);for(auto pixel:view.pixels)check((pixel>>24)==255,"both event tabs render bounded and opaque");}
 std::filesystem::remove_all(dir);puts("PASS: five columns, event tabs, transaction/undo/Home, independent NOTE/INST/FX/MOD cycles, fast FX, bank persistence");
}
