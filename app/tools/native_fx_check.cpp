// Checks one native effect in isolation: silence, determinism, audible default,
// no allocation while rendering or reconfiguring, bounded output, CPU per block.
// Build: see docs/NATIVE-EFFECTS.md. Usage: native_fx_check "EFFECT NAME"
#include "native_fx.hpp"
#include "core.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
using namespace amalthea;
static std::atomic<size_t> allocations{0};
void*operator new(size_t n){allocations++;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete(void*p,size_t)noexcept{std::free(p);}
static int failures=0;
static void check(bool x,const char*what){if(!x){printf("FAIL: %s\n",what);failures++;}}
static float input(long i,int c){float x=.17f*std::sin(i*.039f)+.08f*std::sin(i*.17f)+(i%11025<300?.3f*std::sin(i*.6f):0);return c?x*.7f:x;}
int main(int argc,char**argv){
 if(argc<2){puts("usage: native_fx_check NAME");return 2;}
 int k=-1;for(int i=0;i<NativeCount;i++)if(!strcmp(nativeInfo(i).name,argv[1]))k=i;
 if(k<0){puts("unknown effect");return 2;}
 auto&info=nativeInfo(k);auto type=info.type;
 uint8_t values[NativeParams]{};for(int i=0;i<info.count;i++)values[i]=info.params[i].def;
 check(info.count<=NativeParams,"at most NativeParams parameters");
 for(int i=0;i<info.count;i++){char b[48];for(int v:{0,50,100}){const char*t=nativeValueText(info.params[i],v,b,sizeof b);check(t&&*t,"every value has text");}}
 auto a=makeNativeFx(type),b=makeNativeFx(type);a->configure(values,120);b->configure(values,120);
 float data[64]{},other[64]{};a->process(data,32);b->process(other,32);for(float v:data)check(v==0,"silence in gives silence out at first");
 double difference=0,energy=0;auto before=allocations.load();long frame=0;
 for(int block=0;block<2500;block++){
  for(int i=0;i<32;i++){data[2*i]=other[2*i]=input(frame+i,0);data[2*i+1]=other[2*i+1]=input(frame+i,1);}
  a->process(data,32);b->process(other,32);
  for(int i=0;i<64;i++){check(std::isfinite(data[i])&&std::abs(data[i])<=4,"bounded finite output");check(data[i]==other[i],"two instances are deterministic");
   difference+=std::abs(data[i]-input(frame+i/2,i%2));energy+=data[i]*data[i];}
  frame+=32;
 }
 check(allocations==before,"rendering allocates nothing");check(energy>1e-5,"default passes sound");check(difference>1,"default audibly transforms");
 // Every parameter across its range, live, at two tempi.
 before=allocations.load();
 for(int p=0;p<info.count;p++)for(int v=0;v<=100;v+=10){uint8_t w[NativeParams];memcpy(w,values,sizeof w);w[p]=uint8_t(v);a->configure(w,v<50?92:174);
  for(int r=0;r<40;r++){for(int i=0;i<32;i++){data[2*i]=input(frame+i,0);data[2*i+1]=input(frame+i,1);}frame+=32;a->process(data,32);for(float x:data)check(std::isfinite(x)&&std::abs(x)<=4,"bounded across every parameter");}}
 check(allocations==before,"live parameter changes allocate nothing");
 // Silence after input stops: the output must settle (no self-sustaining output beyond a few bars).
 a->configure(values,120);for(int r=0;r<44100*12/32;r++){std::fill(data,data+64,0.f);a->process(data,32);}
 float tail=0;for(int r=0;r<100;r++){std::fill(data,data+64,0.f);a->process(data,32);for(float x:data)tail=std::max(tail,std::abs(x));}
 check(tail<1e-3f,"silence returns within twelve seconds of input stopping");
 a->clear();
 // CPU: random-ish worst of many profiles, 256-frame blocks.
 uint32_t s=0x1234;double worst=0,sum=0;int profiles=0;
 for(int d=0;d<20;d++){uint8_t w[NativeParams];for(int i=0;i<info.count;i++){s^=s<<13;s^=s>>17;s^=s<<5;int lo=info.params[i].lo,hi=info.params[i].hi;w[i]=uint8_t(lo+s%uint32_t(std::max(1,hi-lo+1)));}
  a->configure(w,120);float block[512];double best=1e9;
  for(int r=0;r<60;r++){for(int i=0;i<256;i++){block[2*i]=input(frame+i,0);block[2*i+1]=input(frame+i,1);}frame+=256;
   auto t=std::chrono::steady_clock::now();for(int c=0;c<256;c+=32)a->process(block+c*2,32);best=std::min(best,std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count());}
  worst=std::max(worst,best);sum+=best;profiles++;}
 printf("%s: transformation %.1f, energy %.1f, cpu per 256-frame block mean %.1f us, worst %.1f us\n",info.name,difference,energy,sum/profiles,worst);
 if(failures)printf("%d FAILURES\n",failures);else puts("PASS");
 return failures?1:0;
}
