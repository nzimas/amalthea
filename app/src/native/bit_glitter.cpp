// SPDX-License-Identifier: GPL-3.0-only
// BITGLITTER: a one-to-one recreation of Airwindows BitGlitter,
// Copyright (c) 2016 airwindows (Chris Johnson), MIT licence (https://github.com/airwindows/airwindows).
// A sine-law saturator into two sample-and-hold decimators (one on the raw sample,
// one on a crude half-way sample at a golden-ratio rate) with fixed coarse quantizers
// that round away from zero and gate their smallest step to silence; the two are
// blended and darkened by a one-sample average. GLITTER lowers the hold rate.
// Changes: Dry/Wet is the host's MIX (the darkening stays at the plugin's fully wet
// 50%); the quantizers' subtraction loops are computed in closed form; the
// floating-point dither and denormal noise are removed, so silence stays exactly silent.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <cmath>
namespace amalthea {
namespace {
const NativeParam params[]={
 {"INPUT",50,NativeUnit::Range,40,75,nullptr,0,-18,18},
 {"GLITTER",0,NativeUnit::Percent,0,45},
 {"OUTPUT",50,NativeUnit::Range,35,60,nullptr,0,-18,18},
};
// Quantize away from zero to steps of rez, then gate the smallest step (the plugin's loops).
inline double derez(double x,double rez){
 if(x>0)x=std::ceil(x/rez)*rez;
 else if(x<0)x=std::floor(x/rez)*rez;
 x*=(1.0-rez);
 if(std::fabs(x)<rez)x=0.0;
 return x;
}
struct Channel {
 double ataLast=0,lastSample=0,heldA=0,heldB=0,positionA=0,positionB=0,lastOutput=0;
 void clear(){ataLast=lastSample=heldA=heldB=positionA=positionB=lastOutput=0;}
 static double hold(double x,double rate,double&position,double&held,double last){
  position+=rate;double out=held;
  if(position>1.0){
   position-=1.0;held=(last*position)+(x*(1-position));
   out=(out*0.5)+(held*0.5);   // softens the edge of the derez
  }
  return out;
 }
 double run(double x,double ingain,double outgain,double rateA,double rateB){
  x*=ingain;
  if(x>1.0)x=1.0;
  if(x<-1.0)x=-1.0;
  x*=1.2533141373155;   // clip to 1.2533141373155 to reach maximum output
  x=std::sin(x*std::fabs(x))/((std::fabs(x)==0.0)?1:std::fabs(x));
  double halfway=(x+ataLast)/2.0;ataLast=x;   // crude oversampling
  // lastSample is never updated in the plugin: the hold interpolates against zero.
  x=derez(hold(x,rateA,positionA,heldA,lastSample),0.0016666666666667);
  halfway=derez(hold(halfway,rateB,positionB,heldB,lastSample),0.0026666666666667);
  x=(x+halfway)/2.0;
  double out=(x*0.5)+(lastOutput*0.5);   // darken (fully wet: 50%)
  lastOutput=x;
  return out*outgain;
 }
};
class BitGlitterFx final:public NativeFx{
 Channel ch[2];double ingain=1,outgain=1,rateA=1/3.,rateB=1.61803398875/3;
public:
 void configure(const uint8_t*v,float)override{
  double factor=v[1]*.01+1.0;factor=std::pow(factor,7)+2.0;
  int divvy=int(factor);
  rateA=1.0/divvy;rateB=1.61803398875/divvy;
  ingain=std::pow(10.0,((v[0]*.01*36.0)-18.0)/14.0);
  outgain=std::pow(10.0,((v[2]*.01*36.0)-18.0)/14.0);
 }
 void clear()override{for(auto&c:ch)c.clear();}
 void process(float*io,int frames)override{
  for(int i=0;i<frames;i++)for(int k=0;k<2;k++)io[i*2+k]=nativeClean(float(ch[k].run(io[i*2+k],ingain,outgain,rateA,rateB)));
 }
};
}
const NativeInfo bitGlitterInfo{FxType::BitGlitter,"BITGLITTER","GLITTR","BG",4,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeBitGlitter(){return std::make_unique<BitGlitterFx>();}
}
