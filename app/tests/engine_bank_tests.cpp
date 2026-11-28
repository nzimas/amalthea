#include "audio.hpp"
#include "native_engines.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
extern "C" {
#include "eng.h"
extern const eng_t ENG_VAP,ENG_PERC,ENG_CHIP,ENG_FIBER,ENG_HIVE,ENG_OP4,ENG_ANIMA,ENG_RUSSOLO,ENG_BAYAREA,ENG_RESURGENT,ENG_CALIGULA,ENG_FMDRUM,ENG_OBRADOR,ENG_RINGS,ENG_PLAITS;
void amalthea_patch(int,uint32_t,int32_t*);
}
using namespace amalthea;
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
 const eng_t* bank[]={&ENG_VAP,&ENG_PERC,&ENG_CHIP,&ENG_FIBER,&ENG_HIVE,&ENG_OP4,&ENG_ANIMA,&ENG_RUSSOLO,&ENG_BAYAREA,&ENG_RESURGENT,&ENG_CALIGULA,&ENG_FMDRUM,&ENG_OBRADOR,&ENG_RINGS,&ENG_PLAITS};
 for(int e=0;e<SynthEngines;e++){
  auto E=bank[e];if(E->init)E->init();std::set<std::string> families;std::set<uint64_t> hashes;
  for(uint32_t seed=0;seed<unsigned(e<6?16:e==6?32:e==7?45:e==8?24:e==9?32:e==11?40:e>=12?48:24);seed++){
   alignas(16) unsigned char patch[ENG_CK_MAX]{},a[ENG_V_MAX]{},b[ENG_V_MAX]{};int32_t p[ENG_NPAR]{};
   auto nativeA=nativeEngine(e)?makeNativeVoice():NativeVoicePtr{},nativeB=nativeEngine(e)?makeNativeVoice():NativeVoicePtr{};if(nativeEngine(e)){bindNativeVoice(a,nativeA.get());bindNativeVoice(b,nativeB.get());}
   amalthea_patch(e,seed,p);E->cook(patch,p);E->reset(a);E->reset(b);E->on(a,patch,48*65536,127);E->on(b,patch,48*65536,127);
   uint64_t hash=1469598103934665603ull;double energy=0;bool alive=true;
   for(int block=0;block<1600;block++){
    int32_t l[32]{},r[32]{},other[32]{},right[32]{};
    int x=E->render(a,patch,l,r),y=E->render(b,patch,other,right);require(x==y,"private voice lifetime deterministic");
    for(int i=0;i<32;i++){require(l[i]==other[i]&&r[i]==right[i],"interleaved voices do not share oscillator/LFO state");require(std::abs(double(l[i]))<32768.*64,"native output bounded");energy+=double(l[i])*l[i];hash=(hash^uint32_t(l[i]))*1099511628211ull;}
    if(block==900){E->off(a,patch,48*65536);E->off(b,patch,48*65536);}alive=x;
   }
   (void)alive;if(energy<=1)fprintf(stderr,"silent engine=%d seed=%u family=%s\n",e,seed,patchFamily({uint8_t(e),seed}));require(energy>1,"every seeded native voice audible");hashes.insert(hash);families.insert(patchFamily({uint8_t(e),seed}));
  }
  require(hashes.size()>8,"randomization explores distinct patches");if(e!=1)require(families.size()>2,"engine exposes multiple original families"); // PERC is a randomized multi-voice KIT.
  printf("%s: all recipe families, %zu families, deterministic private voices\n",E->name,families.size());
 }
 // Exercise real four-lane scheduling with a complete 12-track machine.
 for(int e=0;e<SynthEngines;e++){
  auto p=blankProject();for(auto &patch:p.patches)patch={uint8_t(e),0};
  for(int t=0;t<Tracks;t++)for(int r=0;r<16;r++){p.tracks[t].steps[r].note=48+t+r%3;p.tracks[t].steps[r].instrument=0;}
  Audio a(48),b(48);a.submit(p,true);b.submit(p,true);std::vector<float>x(22050*2),y(x.size());a.render(x.data(),22050);b.render(y.data(),22050);require(x==y,"12-track output deterministic across four DSP lanes");
 }

}
