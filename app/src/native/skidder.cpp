// SPDX-License-Identifier: GPL-3.0-only
// SKIDDER: a one-to-one recreation of Destroy FX Skidder (C) 2000-2026 Sophia Poirier,
// GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/skidder).
// A tempo-synced rhythmic gate: each cycle is slope in, plateau, slope out and a
// valley at the floor gain. Rate, pulse width and floor may be randomized per cycle
// between their random minimum and their value; each cycle pans randomly within the
// stereo spread; RUPTURE fills the valley with noise scaled by the last pulse's RMS.
// A crossover can confine the skidding to the lows or highs. MIDI modes are omitted
// (the Tracker sequences the insert instead); the cycle realigns on transport Start.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const crossoverModes[]={"ALL","LOW","HIGH"};
const NativeParam params[]={
 {"RATE",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(14)},
 {"RATE RANDOM MIN",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(14)},
 {"PULSE WIDTH",50,NativeUnit::Percent,15,85,nullptr,0,0,1,false,"% DUTY"},
 {"PULSE RANDOM MIN",50,NativeUnit::Percent,10,85,nullptr,0,0,1,false,"% DUTY"},
 {"SLOPE",10,NativeUnit::Range,2,45,nullptr,0,0,300,false," ms"},
 {"FLOOR",0,NativeUnit::Percent,0,40},
 {"FLOOR RANDOM MIN",0,NativeUnit::Percent,0,30},
 {"STEREO SPREAD",0,NativeUnit::Percent,0,80},
 {"RUPTURE",0,NativeUnit::Percent,0,35},
 {"CROSSOVER",0,NativeUnit::Choice,0,100,crossoverModes,3},
 {"CROSSOVER FREQ",62,NativeUnit::Range,30,85,nullptr,0,20,20000,true," Hz"},
};
class Skidder final:public NativeFx{
 enum class State{Valley,SlopeIn,Plateau,SlopeOut} state=State::Valley;
 double spb=22050;int rate=12,rateMin=12,mode=0;float pw=.5f,pwMin=.5f,slopeSeconds=.003f,floorGain=0,floorMin=0,pan=0,noise=0,crossover=3000;
 long valley=0,plateau=0,slope=0,slopeDur=1;float slopeStep=1,amp=1,randomFloor=0,randomRange=1,gainL=1,gainR=1;
 double rms=0;long rmsCount=0;uint32_t epoch=0,seed=0x5c1dde12;
 LinkwitzRiley xover[2];
 float floorValue(uint8_t v){float u=v*.01f;return u*u*u;}   // cubed gain curve
public:
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);rate=nativeDivision(v[0]);rateMin=nativeDivision(v[1]);
  pw=std::clamp(v[2]*.01f,.001f,.999f);pwMin=std::clamp(v[3]*.01f,.001f,.999f);
  float s=v[4]*.01f;slopeSeconds=.3f*s*s;floorGain=floorValue(v[5]);floorMin=floorValue(v[6]);pan=v[7]*.01f;
  float n=v[8]*.01f;noise=n*n;mode=nativeChoice(v[9],3);crossover=20*std::pow(1000.f,v[10]*.01f);
  for(auto&x:xover)x.setFrequency(crossover);
 }
 void clear()override{state=State::Valley;valley=0;amp=1;rms=0;rmsCount=0;for(auto&x:xover)x.clear();}
 void newCycle(){
  // Division indices run from long to short: a random rate lies between RATE and its minimum.
  int lo=std::min(rate,rateMin),hi=std::max(rate,rateMin);int index=lo+int(nativeRandom(seed)%uint32_t(hi-lo+1));
  long cycle=std::max<long>(2,std::lround(nativeDivisionBeats(index)*spb));
  float width=pwMin<pw?pwMin+(pw-pwMin)*nativeUnit(seed):pw;
  long pulse=std::max<long>(1,std::lround(cycle*width));valley=cycle-pulse;
  slope=std::lround(44100*slopeSeconds);slopeDur=slope;plateau=pulse-2*slope;
  if(plateau<1){slope=std::lround(pulse/3.f);slopeDur=slope;plateau=pulse-2*slope;}
  slopeStep=slopeDur>0?1.f/slopeDur:1;state=slopeDur>0?State::SlopeIn:State::Plateau;
  float r=(nativeUnit(seed)*2-1)*pan+1;gainL=r;gainR=2-r;rms=0;rmsCount=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;state=State::Valley;valley=0;}   // downbeat
  for(int i=0;i<frames;i++){
   float in[2]={io[i*2],io[i*2+1]},keep[2]={0,0};
   if(mode){for(int c=0;c<2;c++){float low,high;xover[c].process(in[c],low,high);if(mode==1){keep[c]=high;in[c]=low;}else{keep[c]=low;in[c]=high;}}}
   switch(state){
    case State::SlopeIn:{float b=float(slopeDur-slope)*slopeStep;b*=b;amp=b*randomRange+randomFloor;if(--slope<=0)state=State::Plateau;break;}
    case State::Plateau:amp=1;rms+=double(in[0])*in[0]+double(in[1])*in[1];rmsCount+=2;
     if(--plateau<=0){rms=rmsCount?std::min(1.0,2*std::sqrt(rms/rmsCount)):0;
      float lo=std::min(floorMin,floorGain);randomFloor=lo+(floorGain-lo)*nativeUnit(seed);randomRange=1-randomFloor;
      if(slopeDur>0){state=State::SlopeOut;slope=slopeDur;}else state=State::Valley;}
     break;
    case State::SlopeOut:{float b=float(slope)*slopeStep;b*=b;amp=b*randomRange+randomFloor;if(--slope<=0)state=State::Valley;break;}
    case State::Valley:amp=randomFloor;if(--valley<=0)newCycle();break;
   }
   float out[2];
   if(state==State::Valley&&noise>0){for(int c=0;c<2;c++)out[c]=(nativeUnit(seed)*2-1)*(c?gainR:gainL)*noise*float(rms);}
   else{
    // Skidder's stereo pan: a gain below 1 keeps part of one side, above 1 folds in the other.
    out[0]=gainL<=1?in[0]*gainL*amp:(in[0]+in[1]*(gainL-1))*amp;
    out[1]=gainR<=1?in[1]*gainR*amp:(in[1]+in[0]*(gainR-1))*amp;
   }
   io[i*2]=out[0]+keep[0];io[i*2+1]=out[1]+keep[1];
  }
 }
};
}
const NativeInfo skidderInfo{FxType::Skidder,"SKIDDER","SKID","SK",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeSkidder(){return std::make_unique<Skidder>();}
}
