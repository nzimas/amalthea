// SPDX-License-Identifier: GPL-3.0-only
// SUPATRIGGA: a one-to-one recreation of Smartelectronix SupaTrigga (C) Bram de Jong,
// dual licensed; used here under its GPL-3.0 licence (https://github.com/bdejong/smartelectronix).
// A tempo-locked stutterer: each bar is recorded while it plays and cut into 1..128
// slices. At every bar start a new per-slice sequence is drawn: REARRANGE moves a slice
// to play an earlier part of the bar (REPEAT makes it continue the previous slice
// instead), REVERSE plays it backwards, SLOW makes it play from its start with a
// decaying "tape stop" speed (Hermite interpolated; SLOW SPEED sets the decay time,
// 4 s down to 0.04 s), SILENCE mutes it. A 150-sample one-pole gain fades every splice.
// The INSTANT toggles force reverse, slow or repeat on every slice, as in the original.
// Changes: the host play head is replaced by a free-running bar of BAR LENGTH (shared
// tempo division table, bpm from configure()) that realigns to the downbeat on
// transport Start; the bar is limited to about 8 s of buffer (the original allowed
// 2,000,000 samples); std::rand is replaced by a seeded per-instance generator. The
// editor, state saving and pass-through when the host transport is stopped are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const sliceCounts[]={"1","2","4","8","16","32","64","128"};
const NativeParam params[]={
 {"BAR LENGTH",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(1),nativeDivisionValue(4)},
 {"SLICES",nativeChoiceValue(2,8),NativeUnit::Choice,nativeChoiceValue(2,8),nativeChoiceValue(4,8),sliceCounts,8},
 {"SLOW SPEED",25,NativeUnit::Percent,10,80},
 {"REVERSE PROB",15,NativeUnit::Percent,0,40,nullptr,0,0,1,false,"% CHANCE"},
 {"SLOW PROB",5,NativeUnit::Percent,0,20,nullptr,0,0,1,false,"% CHANCE"},
 {"REARRANGE PROB",95,NativeUnit::Percent,40,100,nullptr,0,0,1,false,"% CHANCE"},
 {"SILENCE PROB",0,NativeUnit::Percent,0,15,nullptr,0,0,1,false,"% CHANCE"},
 {"REPEAT PROB",40,NativeUnit::Percent,10,70,nullptr,0,0,1,false,"% CHANCE"},
 {"INSTANT REVERSE",0,NativeUnit::Toggle,0,0},
 {"INSTANT SLOW",0,NativeUnit::Toggle,0,0},
 {"INSTANT REPEAT",0,NativeUnit::Toggle,0,0},
};
constexpr int BitSlides=7,MaxSlides=1<<BitSlides,FadeTime=150;
constexpr unsigned long MaxSize=360000;   // frames per channel: 2.9 MB of stereo float
struct GlitchParams{unsigned long offset=0;bool reverse=false,stop=false,silence=false;};
// Hermite interpolation, inverse direction (as the original).
inline float hermiteInverse(const float*w,unsigned long n,float x){
 float y3=n==0?0.f:w[n-1],y2=w[n],y1=w[n+1],y0=w[n+2];x=1.f-x;
 float c0=y1,c1=.5f*(y2-y0),c2=y0-2.5f*y1+2.f*y2-.5f*y3,c3=1.5f*(y1-y2)+.5f*(y3-y0);
 return ((c3*x+c2)*x+c1)*x+c0;
}
class SupaTriggaFx final:public NativeFx{
 std::vector<float> left,right;
 GlitchParams sequencer[MaxSlides];
 unsigned long positionInMeasure=0,previousSliceIndex=0xffffffff,granularityMask=0,granularity=0,samplesInMeasure=88200;
 float gain=0,speed=0,position=0;bool first=true,instantReverse=false,instantSlow=false,instantRepeat=false,needRandomize=true;
 const float fadeCoeff=std::exp(std::log(.01f)/FadeTime);
 // Configured values.
 unsigned long granularityParam=2;float speedRaw=.25f,probReverse=.15f,probSpeed=.05f,probRearrange=.95f,probSilence=0,probRepeat=.4f;
 bool instantReverseParam=false,instantSlowParam=false,instantRepeatParam=false;
 uint32_t seed=0x5a9a7e11,epoch=0;
 int rnd(int m){return int(nativeRandom(seed)%uint32_t(m));}
 void randomize(){
  unsigned long g=granularityParam,maskTmp=(unsigned long)(0xffffffffu<<(BitSlides-g));
  for(unsigned long i=0;i<MaxSlides;i++){
   auto&s=sequencer[i];
   if(rnd(100)<int(probRearrange*101.f)){
    if(rnd(100)<int(probRepeat*101.f))s.offset=i?(sequencer[(i-1)&maskTmp].offset+(i-((i-1)&maskTmp)))&granularityMask:0;
    else s.offset=(i*(unsigned long)rnd(MaxSlides))/MaxSlides;
   }else s.offset=0;
   s.reverse=rnd(100)<int(probReverse*100.f)&&s.offset>0;
   s.stop=rnd(100)<int(probSpeed*101.f);
   s.silence=rnd(100)<int(probSilence*101.f);
  }
 }
public:
 SupaTriggaFx():left(MaxSize,0.f),right(MaxSize,0.f){}
 void configure(const uint8_t*v,float bpm)override{
  double beats=nativeDivisionBeats(nativeDivision(v[0]));
  samplesInMeasure=std::clamp<unsigned long>((unsigned long)std::ceil(nativeSamplesPerBeat(bpm)*beats),MaxSlides,MaxSize-4);
  granularityParam=(unsigned long)nativeChoice(v[1],8);speedRaw=v[2]*.01f;
  probReverse=v[3]*.01f;probSpeed=v[4]*.01f;probRearrange=v[5]*.01f;probSilence=v[6]*.01f;probRepeat=v[7]*.01f;
  instantReverseParam=v[8]>=50;instantSlowParam=v[9]>=50;instantRepeatParam=v[10]>=50;
 }
 void clear()override{
  std::fill(left.begin(),left.end(),0.f);std::fill(right.begin(),right.end(),0.f);
  positionInMeasure=0;previousSliceIndex=0xffffffff;granularityMask=0;granularity=0;gain=0;speed=0;first=true;needRandomize=true;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;positionInMeasure=0;}   // downbeat
  if(needRandomize){randomize();needRandomize=false;}
  if(positionInMeasure>=samplesInMeasure)positionInMeasure%=samplesInMeasure;  // bar shortened
  const float sr=44100.f;
  unsigned long granularityTmp=granularityParam,granularityMaskTmp=(unsigned long)(0xffffffffu<<(BitSlides-granularityTmp));
  float speedDiff=std::exp(std::log(.01f)/(((1.f-speedRaw)+.01f)*sr*4.f));
  const float*lb=left.data(),*rb=right.data();
  for(int i=0;i<frames;i++){
   float*o=io+2*i;
   left[positionInMeasure]=o[0];right[positionInMeasure]=o[1];
   unsigned long sliceIndex=((positionInMeasure*MaxSlides)/samplesInMeasure)&granularityMask;
   if(instantRepeat&&sliceIndex!=0)
    sequencer[sliceIndex].offset=(sequencer[(sliceIndex-1)&granularityMaskTmp].offset+(sliceIndex-((sliceIndex-1)&granularityMaskTmp)))&granularityMask;
   unsigned long displacement=sequencer[sliceIndex].offset&granularityMask;
   if(granularityMaskTmp!=granularityMask){
    unsigned long sliceIndexTmp=((positionInMeasure*MaxSlides)/samplesInMeasure)&granularityMaskTmp;
    if((granularityTmp<granularity&&sliceIndex==0)||(granularityTmp>granularity&&sliceIndexTmp==0)||(sliceIndex==0&&sliceIndexTmp==0)){
     granularityMask=granularityMaskTmp;granularity=granularityTmp;sliceIndex=sliceIndexTmp;displacement=sequencer[sliceIndex].offset&granularityMask;
    }
   }
   if(sliceIndex!=previousSliceIndex){instantRepeat=instantRepeatParam;instantReverse=instantReverseParam;instantSlow=instantSlowParam;previousSliceIndex=sliceIndex;}
   {  // splice gain
    unsigned long far=((((positionInMeasure+FadeTime)%samplesInMeasure)*MaxSlides)/samplesInMeasure)&granularityMask;
    unsigned long displacementFar=sequencer[far].offset&granularityMask;bool reverseFar=sequencer[far].reverse;
    float target=sequencer[sliceIndex].silence?0.f:1.f;
    if(displacementFar!=displacement||positionInMeasure+FadeTime>samplesInMeasure||reverseFar!=sequencer[sliceIndex].reverse)target=0;
    gain=fadeCoeff*gain+(1.f-fadeCoeff)*target;
   }
   const auto&s=sequencer[sliceIndex];
   if((s.reverse||instantReverse)&&displacement!=0){
    unsigned long sliceSize=samplesInMeasure>>granularity,sliceStart=(positionInMeasure/sliceSize)*sliceSize;
    unsigned long sliceEnd=(sliceStart+sliceSize)-(positionInMeasure-sliceStart),difference=(displacement*samplesInMeasure)/MaxSlides;
    unsigned long start=difference<=sliceEnd?sliceEnd-difference:0;
    if(!(s.stop||instantSlow)){
     if(start<MaxSize){o[0]=lb[start]*gain;o[1]=rb[start]*gain;}
     first=true;
    }else{
     if(first){position=float(start);speed=1.f/speedDiff;first=false;}
     speed*=speedDiff;position-=speed;if(position<0.f)position=0.f;
     unsigned long n=(unsigned long)std::floor(position);float alpha=position-float(n);
     if(n<MaxSize-3){o[0]=hermiteInverse(lb,n,alpha)*gain;o[1]=hermiteInverse(rb,n,alpha)*gain;}
    }
   }else{
    unsigned long difference=(displacement*samplesInMeasure)/MaxSlides;
    if(!(s.stop||instantSlow)||displacement==0){
     unsigned long n=positionInMeasure>difference?positionInMeasure-difference:0;
     if(n<MaxSize){o[0]=lb[n]*gain;o[1]=rb[n]*gain;}
     first=true;
    }else{
     if(first){position=float(positionInMeasure-difference);speed=1.f/speedDiff;first=false;}
     speed*=speedDiff;position+=speed;
     unsigned long n=(unsigned long)std::floor(position);float alpha=position-float(n);
     if(n<MaxSize-3){o[0]=hermiteInverse(lb,n,alpha)*gain;o[1]=hermiteInverse(rb,n,alpha)*gain;}
    }
   }
   o[0]=nativeClean(o[0]);o[1]=nativeClean(o[1]);
   if(++positionInMeasure>=samplesInMeasure){randomize();positionInMeasure=0;}
   if(std::fabs(gain)<1e-10f)gain=0.f;
  }
 }
};
}
const NativeInfo supaTriggaInfo{FxType::SupaTrigga,"SUPATRIGGA","TRIGGA","SU",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeSupaTrigga(){return std::make_unique<SupaTriggaFx>();}
}
