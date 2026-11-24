#include "../third_party/vascular/AnimaCore.h"
#include "../third_party/vascular/AnimaDesign.h"
#include "reference/AnimaReferenceCore.h"
#include "reference/AnimaReferenceDesign.h"
#include <cstdio>
#include <cmath>
int main(){
 for(int family=0;family<8;family++)for(unsigned seed:{1u,17u,8191u}){
  auto d=vascular::anima::design(family,seed);vascular::anima::route(d,seed^77);
  auto ref=vascular_reference::anima::design(family,seed);vascular_reference::anima::route(ref,seed^77);
  vascular::anima::Engine optimized;vascular_reference::anima::Engine original;
  optimized.init(d,seed,44100,220);original.init(ref,seed,44100,220);optimized.strike();original.strike();
  for(int b=0;b<1500;b++){float a[32],z[32];optimized.process(a,32);original.process(z,32);for(int i=0;i<32;i++)if(a[i]!=z[i]){fprintf(stderr,"FAIL: Anima optimized differs from Vascular family%d seed%u block%d sample%d %.9g %.9g\n",family,seed,b,i,a[i],z[i]);return 1;}}
 }
 puts("Anima: 24 original-design renders match Vascular sample for sample after coefficient caching");
}
