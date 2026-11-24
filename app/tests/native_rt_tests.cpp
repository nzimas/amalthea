#include "audio.hpp"
#include "native_engines.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
extern "C" {
#include "eng.h"
extern const eng_t ENG_ANIMA,ENG_RUSSOLO,ENG_BAYAREA,ENG_RESURGENT,ENG_CALIGULA;
void amalthea_patch(int,uint32_t,int32_t*);
}
static std::atomic<size_t> allocations{0};
void *operator new(size_t n){allocations++;if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void *operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}void operator delete[](void*p)noexcept{std::free(p);}
static void check(bool v,const char*s){if(!v){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
using namespace amalthea;
int main(){
 const eng_t *engines[]={&ENG_ANIMA,&ENG_RUSSOLO,&ENG_BAYAREA,&ENG_RESURGENT,&ENG_CALIGULA};
 auto state=makeNativeVoice();alignas(16) unsigned char data[ENG_V_MAX]{},cooked[ENG_CK_MAX]{};bindNativeVoice(data,state.get());
 for(int e=0;e<5;e++){auto E=engines[e];if(E->init)E->init();size_t largest=0;double engineTail=0;
  for(unsigned seed=0;seed<unsigned(e==0?32:e==1?45:e==2?24:e==3?32:24);seed++){
   int32_t p[ENG_NPAR]{};amalthea_patch(e+6,seed,p);E->cook(cooked,p);size_t before=allocations;
   E->reset(data);E->on(data,cooked,60*65536,100);int32_t l[32],r[32];
   for(int i=0;i<400;i++)E->render(data,cooked,l,r);
   E->off(data,cooked,60*65536);bool died=false;double tail=0;
   for(int i=0;i<44100*12/32;i++){int alive=E->render(data,cooked,l,r);for(int j=0;j<32;j++)tail+=double(l[j])*l[j];if(!alive){died=true;break;}}
   check(allocations==before,"note-on, rendering, release and bounded buffers allocate nothing");check(died,"released native voice becomes silent");engineTail+=tail;largest=std::max(largest,nativeVoiceArenaUsed(state.get()));
  }
  check(engineTail>0,"engine retains audible release tails");
  printf("%s: all families, no callback allocation, natural release, arena high water %zu bytes\n",E->name,largest);
 }
}
