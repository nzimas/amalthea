// SPDX-License-Identifier: GPL-3.0-only
// TREMOSQUARE: a one-to-one recreation of Airwindows TremoSquare,
// Copyright (c) 2016 airwindows (Chris Johnson), MIT licence (https://github.com/airwindows/airwindows).
// A square-wave tremolo that only switches at zero crossings: each channel decides
// whether to mute or pass at the moment its own input changes polarity, using the
// square oscillator's current half, so the gating never clicks. Changes: the free
// Freq oscillator is replaced by a tempo-synced DIVISION (one full on/off cycle per
// division), realigned to the downbeat on transport Start, with the open half first
// so the downbeat sounds; Dry/Wet is the host's MIX; the floating-point dither and
// denormal noise are removed, so silence stays exactly silent.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <cmath>
namespace amalthea {
namespace {
const NativeParam params[]={
 {"DIVISION",nativeDivisionValue(10),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(14)},
};
class TremoSquareFx final:public NativeFx{
 double spb=22050,cycle=11025,pos=0;int division=10;uint32_t epoch=0;
 bool polarityL=false,muteL=false,polarityR=false,muteR=false;
public:
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);division=nativeDivision(v[0]);
  cycle=std::max(2.0,nativeDivisionBeats(division)*spb);if(pos>=cycle)pos=std::fmod(pos,cycle);
 }
 void clear()override{pos=0;polarityL=muteL=polarityR=muteR=false;}
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;pos=0;}   // downbeat
  for(int i=0;i<frames;i++){
   double inputSampleL=io[i*2],inputSampleR=io[i*2+1];
   // Our little oscillator: the second half of each cycle is the muted half.
   double osc=pos/cycle;pos+=1;if(pos>=cycle)pos-=cycle;
   bool closed=osc>=0.5;
   if(inputSampleL<0){if(polarityL)muteL=closed;polarityL=false;}
   else{if(!polarityL)muteL=closed;polarityL=true;}
   if(inputSampleR<0){if(polarityR)muteR=closed;polarityR=false;}
   else{if(!polarityR)muteR=closed;polarityR=true;}
   if(muteL)inputSampleL=0;
   if(muteR)inputSampleR=0;
   io[i*2]=float(inputSampleL);io[i*2+1]=float(inputSampleR);
  }
 }
};
}
const NativeInfo tremoSquareInfo{FxType::TremoSquare,"TREMOSQUARE","TRMSQ","TQ",6,0,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeTremoSquare(){return std::make_unique<TremoSquareFx>();}
}
