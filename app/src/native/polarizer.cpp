// SPDX-License-Identifier: GPL-3.0-only
// POLARIZER: a one-to-one recreation of Destroy FX Polarizer (C) 2001-2026 Sophia Poirier,
// GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/polarizer).
// Every LEAP-th sample is multiplied by the polarization scalar (0 keeps it, 50 silences
// it, 100 flips its polarity); the samples in between pass untouched. IMPLODE then
// folds every sample towards the opposite rail (1 - x above zero, -1 - x below).
// Changes: LEAP is shown on an exponential 1..81 sample scale instead of DFX's power
// curve; IMPLODE leaves exact zeros at zero (the original turns digital silence into
// full-scale DC); an optional tempo-synced triangle SWEEP of the leap, realigned on
// transport Start, is added (0 depth is the original). The scalar glides over 30 ms
// like DFX's smoothed value. GUI and host code are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const NativeParam params[]={
 {"LEAP",25,NativeUnit::Range,0,70,nullptr,0,1,81,true," SMP"},
 {"AMOUNT",50,NativeUnit::Percent,50,100},
 {"IMPLODE",0,NativeUnit::Toggle,0,55},
 {"SWEEP DEPTH",0,NativeUnit::Percent,0,60},
 {"SWEEP RATE",nativeDivisionValue(9),NativeUnit::Division,nativeDivisionValue(5),nativeDivisionValue(11)},
};
class PolarizerFx final:public NativeFx{
 long unaffected=0;            // DFX mUnaffectedSamples: the first sample is polarized
 float leapPosition=.25f,depth=0,target=0,ampValue=0,ampStep=0;int ampRamp=0;bool implode=false,first=true;
 double spb=22050,phase=0,cycle=44100;uint32_t epoch=0;
public:
 void configure(const uint8_t*v,float bpm)override{
  leapPosition=v[0]*.01f;
  target=(.5f-v[1]*.01f)*2;   // processparameters(): (0.5 - amount) * 2
  if(first){ampValue=target;ampRamp=0;first=false;}
  else{ampRamp=int(.03f*44100);ampStep=(target-ampValue)/ampRamp;}
  implode=v[2]>=50;depth=v[3]*.01f;
  spb=nativeSamplesPerBeat(bpm);cycle=std::max(2.0,nativeDivisionBeats(nativeDivision(v[4]))*spb);
 }
 void clear()override{unaffected=0;phase=0;ampValue=target;ampRamp=0;}
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;phase=0;}   // downbeat
  float u=leapPosition;
  if(depth>0){double t=phase/cycle;float tri=float(t<.5?4*t-1:3-4*t);u=std::clamp(u+depth*tri,0.f,1.f);
   phase+=frames;while(phase>=cycle)phase-=cycle;}
  long leap=std::clamp(std::lround(std::pow(81.f,u)),1l,81l);
  unaffected=std::min(unaffected,leap);   // catch up if leap size decreased
  for(int i=0;i<frames;i++){
   bool polarize=--unaffected<0;if(polarize)unaffected=leap;
   for(int c=0;c<2;c++){
    float x=io[i*2+c];if(polarize)x*=ampValue;
    if(implode&&x!=0)x=x>0?1-x:-1-x;
    io[i*2+c]=nativeClean(x);
   }
   if(ampRamp>0){ampValue+=ampStep;if(--ampRamp==0)ampValue=target;}
  }
 }
};
}
const NativeInfo polarizerInfo{FxType::Polarizer,"POLARIZER","POLAR","PZ",4,2,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makePolarizer(){return std::make_unique<PolarizerFx>();}
}
