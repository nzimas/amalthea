#include "ported_inserts.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

using namespace amalthea;
static std::atomic<size_t> allocations{0};
void *operator new(size_t n){allocations++;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
static void check(bool c,const char*s){if(!c){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){setvbuf(stdout,nullptr,_IONBF,0);
 Project project=blankProject();
 for(int track=0;track<Tracks;track++)for(int slot=0;slot<2;slot++){
  auto &profile=project.tracks[track].inserts[slot];
  for(int k=0;k<PortedTypes;k++){auto type=portedType(k);Effect fx{type,73};int pages=type==FxType::ChowMatrix?9:type==FxType::Frequalizer?6:1;
   for(int page=0;page<pages;page++)for(int field=1;field<insertParameterCount(type,page,profile);field++)insertParameter(fx,profile,field,page)=(track*13+slot*7+page*17+field)%101;
  }
  project.tracks[track].steps[0].fx[slot]={portedType((track*2+slot)%PortedTypes),73};
 }
 auto bytes=encodeProject(project);Project restored;std::string error;
 check(decodeProject(restored,bytes,error)&&encodeProject(restored)==bytes,"all nine profiles persist independently on all tracks and slots");

 for(int kind=0;kind<PortedTypes;kind++){
  auto type=portedType(kind);PortedProcessor a(type),b(type);InsertSettings settings;
  a.configure(settings,120);b.configure(settings,120);float data[64]{};a.process(data,32,1);b.process(data,32,1);
  for(float v:data)check(v==0,"fresh effect preserves digital silence");
  double difference=0,energy=0;auto before=allocations.load();
  for(int block=0;block<2500;block++){
   float other[64];for(int i=0;i<32;i++){float x=.17f*std::sin((block*32+i)*.039f)+.08f*std::sin((block*32+i)*.17f);data[2*i]=other[2*i]=x;data[2*i+1]=other[2*i+1]=x*.7f;}
   a.process(data,32,1);b.process(other,32,1);
   for(int i=0;i<64;i++){check(std::isfinite(data[i])&&std::abs(data[i])<=4,"bounded finite output");check(data[i]==other[i],"independent instances are deterministic");float input=i%2?.7f*(.17f*std::sin((block*32+i/2)*.039f)+.08f*std::sin((block*32+i/2)*.17f)):.17f*std::sin((block*32+i/2)*.039f)+.08f*std::sin((block*32+i/2)*.17f);difference+=std::abs(data[i]-input);energy+=data[i]*data[i];}
  }
  printf("%s: energy %.2f, transformation %.2f, render allocations %zu\n",fxName(type),energy,difference,allocations.load()-before);
  check(allocations==before,"steady-state render allocates nothing");check(energy>1e-5&&difference>1,"default profile produces audible transformation");
  // Every exposed setting is addressable; extremes must remain finite.
  auto start=allocations.load();for(int pass=0;pass<3;pass++){Effect e{type,100};int pages=type==FxType::ChowMatrix?9:type==FxType::Frequalizer?6:1;
   for(int page=0;page<pages;page++)for(int f=1;f<insertParameterCount(type,page,settings);f++){
    insertParameter(e,settings,f,page)=pass==0?0:pass==1?100:50;a.configure(settings,pass==1?30:300);
    for(int i=0;i<64;i++)data[i]=.15f*std::sin(i*.32f);a.process(data,32,1);for(float v:data)check(std::isfinite(v)&&std::abs(v)<=4,"parameter extremes finite");
   }
  }
  printf("  configure/render sweep allocations %zu\n",allocations.load()-start);check(allocations==start,"live parameter changes allocate nothing");
  a.clear();settings=InsertSettings{};a.configure(settings,120);for(int block=0;block<200;block++){std::fill(data,data+64,0);a.process(data,32,1);for(float v:data)check(std::abs(v)<1e-6f,"clear removes all stale sound");}
 }
}
