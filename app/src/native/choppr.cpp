// SPDX-License-Identifier: GPL-3.0-only
// CHOPPR: a one-to-one recreation of B.Choppr (C) 2018-2026 Sven Jähnichen, GPL-3.0-or-later
// (https://github.com/sjaehn/BChoppr), the successor of B.Slizr. A tempo-synced step gate:
// one sequence of up to 16 steps is spread over SEQUENCE; each step has a level and a pan.
// Step markers sit at B.Choppr's automatic positions, pushed apart pairwise by SWING; AMP
// SWING trades the levels of even against odd steps. A rising step fades in over ATTACK (a
// fraction of the step), a step followed by a lower one fades out over DECAY, with linear
// or sinusoidal BLEND, for both level and pan (B.Choppr's play() is ported as is).
// Changed: the drawn step levels and pans are a pattern (musical presets or seeded random)
// shaped by DEPTH, VARIATION, PAN SPREAD and SEED; manual marker positions, MIDI, the GUI
// monitor and host transport are omitted (the sequence follows the reference tempo and
// realigns on transport Start). DRY/WET is the host's MIX.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
namespace {
const char*const stepNames[]={"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"};
const char*const patternNames[]={"STRAIGHT","GATE","TRESILLO","CINQUILLO","TRANCE","ACCENT","RAMP UP","RAMP DOWN","RANDOM"};
constexpr int Patterns=9;
const char*const blendNames[]={"LINEAR","SINE"};
const NativeParam params[]={
 {"STEPS",nativeChoiceValue(7,16),NativeUnit::Choice,nativeChoiceValue(3,16),100,stepNames,16},
 {"SEQUENCE",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(1),nativeDivisionValue(4)},
 {"PATTERN",nativeChoiceValue(4,Patterns),NativeUnit::Choice,0,100,patternNames,Patterns},
 {"DEPTH",100,NativeUnit::Percent,60,100},
 {"VARIATION",0,NativeUnit::Percent,0,50},
 {"ATTACK",20,NativeUnit::Percent,5,50,nullptr,0,0,1,false,"% STEP"},
 {"DECAY",20,NativeUnit::Percent,5,60,nullptr,0,0,1,false,"% STEP"},
 {"BLEND",100,NativeUnit::Choice,0,100,blendNames,2},
 {"SWING",50,NativeUnit::Range,45,72,nullptr,0,1/3.f,3,true," x"},
 {"AMP SWING",50,NativeUnit::Bipolar,38,62},
 {"PAN SPREAD",0,NativeUnit::Percent,0,70},
 {"SEED",0,NativeUnit::Percent,0,100},
};
constexpr int MaxSteps=16;
// A 16-step trance gate, sampled to the step count.
const float tranceGate[MaxSteps]={1,0,1,1,0,1,1,0,1,0,1,1,0,1,1,0};
class Choppr final:public NativeFx{
 std::array<float,MaxSteps> levels{},pans{};std::array<float,MaxSteps-1> positions{};
 int steps=8,blend=2;float attack=.2f,release=.2f,ampSwing=1;
 double spb=22050,sequenceBeats=4,phase=0;
 uint32_t prevStep=0,actStep=0,nextStep=1,epoch=0;
 // B.Choppr's recalculateAutoPositions() with every marker automatic.
 void autoPositions(float swing){
  const int markers=steps-1;if(markers<1)return;
  const float s=2*swing/(swing+1);const int start=0,i=markers-1;
  float anc=0;const float diff=1-anc;
  const float dist=float(i-start)+1+((i-start)&1?((start&1)?2-s:s):1.f);
  const float step=diff<0?0:diff/dist;
  for(int j=start;j<=i;j++){anc+=((j&1)?2-s:s)*step;positions[size_t(j)]=anc;}
 }
 static float pattern(int kind,int i,int n,uint32_t&seed){
  auto euclid=[&](int k){k=std::clamp(k,1,n);return float((i*k)%n<k);};
  switch(kind){
   case 0:return 1;
   case 1:return float(i%2==0);
   case 2:return euclid(int(std::lround(n*3/8.)));
   case 3:return euclid(int(std::lround(n*5/8.)));
   case 4:return tranceGate[i*MaxSteps/n];
   case 5:return i%4==0?1:i%2==0?.6f:.35f;
   case 6:return float(i+1)/n;
   case 7:return float(n-i)/n;
   default:return nativeUnit(seed)<.62f?.4f+.6f*nativeUnit(seed):0.f;
  }
 }
public:
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);
  steps=nativeChoice(v[0],16)+1;sequenceBeats=nativeDivisionBeats(nativeDivision(v[1]));
  int kind=nativeChoice(v[2],Patterns);float depth=v[3]*.01f,variation=v[4]*.01f;
  attack=std::max(.01f,v[5]*.01f);release=std::max(.01f,v[6]*.01f);blend=nativeChoice(v[7],2)+1;
  float swing=(1/3.f)*std::pow(9.f,v[8]*.01f);
  ampSwing=std::pow(10.f,3*(v[9]-50)/50.f);
  float spread=v[10]*.01f;
  uint32_t seed=0xc4099e5u^(uint32_t(v[11])*0x9e3779b1u);for(int k=0;k<4;k++)nativeRandom(seed);
  for(int i=0;i<MaxSteps;i++){
   float level=i<steps?pattern(kind,i,steps,seed):0;
   level*=1-variation*nativeUnit(seed);
   levels[size_t(i)]=std::clamp(1-depth*(1-level),0.f,1.f);
   pans[size_t(i)]=std::clamp(spread*((i&1)?1.f:-1.f)*(.5f+.5f*nativeUnit(seed)),-1.f,1.f);
  }
  autoPositions(swing);
 }
 void clear()override{phase=0;prevStep=0;actStep=0;nextStep=1;}
 float stepLevel(uint32_t s)const{return levels[s]*std::clamp(s%2==0?ampSwing:1/ampSwing,0.f,1.f);}
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;phase=0;}   // downbeat
  const double increment=1/std::max(1.0,sequenceBeats*spb);
  for(int f=0;f<frames;f++){
   const float pos=float(phase);phase+=increment;if(phase>=1)phase-=std::floor(phase);
   int iStep;for(iStep=0;iStep<steps-1&&positions[size_t(iStep)]<pos;iStep++){}
   const float steppos=iStep==0?0:positions[size_t(iStep-1)];
   const float nextpos=iStep==steps-1?1:positions[size_t(iStep)];
   const float stepsize=nextpos-steppos,frac=stepsize<=0?0:(pos-steppos)/stepsize;
   if(actStep!=uint32_t(iStep)){prevStep=actStep;actStep=uint32_t(iStep);nextStep=iStep<steps-1?uint32_t(iStep+1):0;}
   const float actLevel=stepLevel(actStep),prevLevel=stepLevel(prevStep),nextLevel=stepLevel(nextStep);
   const float actPan=pans[actStep],prevPan=pans[prevStep],nextPan=pans[nextStep];
   float level=actLevel,pan=actPan;
   if(frac<attack&&prevLevel<=actLevel){
    float k=frac/attack;if(blend==2)k=.5f*(std::sin(NativePi*(k-.5f))+1);
    if(level!=prevLevel)level=prevLevel+k*(level-prevLevel);
    if(pan!=prevPan)pan=prevPan+k*(pan-prevPan);
   }
   if(frac>1-release&&nextLevel<actLevel){
    float k=(1-frac)/release;if(blend==2)k=.5f*(std::sin(NativePi*(k-.5f))+1);
    level=nextLevel+k*(level-nextLevel);
    if(pan!=nextPan)pan=nextPan+k*(pan-nextPan);
   }
   io[f*2]=nativeClean(io[f*2]*level*(pan<=0?1:1-pan));
   io[f*2+1]=nativeClean(io[f*2+1]*level*(pan>=0?1:1+pan));
  }
 }
};
}
const NativeInfo chopprInfo{FxType::Choppr,"CHOPPR","CHOPPR","CH",6,0,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeChoppr(){return std::make_unique<Choppr>();}
}
