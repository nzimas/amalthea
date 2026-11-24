// SPDX-License-Identifier: GPL-3.0-only
// BUFFER OVERRIDE: a one-to-one recreation of Destroy FX Buffer Override (C) 2001-2026
// Sophia Poirier, GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/bufferoverride),
// including its LFO from the Destroy FX library (C) Sophia Poirier and Tom Murphy 7.
// The input is recorded into a tempo-synced "forced buffer"; the DIVISOR splits it into
// minibuffers, and the first minibuffer of each forced buffer is repeated until the
// forced buffer is full. BUFFER INTERRUPT lets the divisor change mid forced buffer.
// Two LFOs modulate the divisor and the forced buffer size (0..2 around 1, the buffer
// LFO inverted), SMOOTH crossfades each minibuffer into the previous one's tail
// (sine/cosine), DURATION shortens the audible part of each minibuffer, and DECAY is
// the original's gain decay with the ramp shape (positive fades later minibuffers).
// Omitted: MIDI nudge/trigger modes and pitchbend, the free-running (ms and Hz) rates,
// the internal tempo and dry/wet (the host provides them), the decay filter modes and
// shapes, the random duration minimum, and the GUI. The host's song position is
// replaced by a bar counter (4/4) that restarts on transport Start, where the forced
// buffers and both LFOs realign to the downbeat. The LFOs' random shapes use a seeded
// generator. DIVISOR spans 1..256 exponentially (25 = 4, 50 = 16; below 2 it is off)
// instead of 1.92..222 squared. The buffer is capped at 8 s, so very long divisions at slow tempi clamp.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const lfoShapes[]={"SINE","TRIANGLE","SQUARE","SAW","REV SAW","THORN","RANDOM","RANDOM SMOOTH"};
const NativeParam params[]={
 {"DIVISOR",25,NativeUnit::Range,13,50,nullptr,0,1,256,true,""},
 {"BUFFER SIZE",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(2),nativeDivisionValue(12)},
 {"BUFFER INTERRUPT",100,NativeUnit::Toggle,0,100},
 {"SMOOTH",9,NativeUnit::Percent,3,25},
 {"DIV LFO RATE",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(0),nativeDivisionValue(10)},
 {"DIV LFO DEPTH",0,NativeUnit::Percent,0,70},
 {"DIV LFO SHAPE",0,NativeUnit::Choice,0,100,lfoShapes,8},
 {"BUF LFO RATE",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(0),nativeDivisionValue(10)},
 {"BUF LFO DEPTH",0,NativeUnit::Percent,0,50},
 {"BUF LFO SHAPE",0,NativeUnit::Choice,0,100,lfoShapes,8},
 {"DURATION",100,NativeUnit::Percent,40,100,nullptr,0,0,1,false,"% DUTY"},
 {"DECAY",50,NativeUnit::Bipolar,30,75},
};
constexpr long MaxFrames=44100*8;            // 2.8 MB stereo
constexpr float ActiveDivisorMinimum=2;     // below this the divisor behaves as one

// dfx::LFO: a 0..1 waveform times depth, advanced in steps of cycles per sample.
struct Lfo {
 enum{Sine,Triangle,Square,Saw,ReverseSaw,Thorn,Random,RandomInterpolating};
 double depth=0,position=0,step=1,random=0,prevRandom=0;int shape=0;uint32_t seed;
 explicit Lfo(uint32_t s):seed(s){reset();}
 double next(){return nativeUnit(seed);}
 void reset(){position=0;step=1;prevRandom=next();random=next();}
 void syncToTheBeat(double samplesToBar){
  double cycle=1/step,countdown=std::fmod(samplesToBar,cycle);
  position=std::max((cycle-countdown)*step,0.);position-=std::floor(position);
 }
 void updatePosition(long steps){
  position+=step*double(steps);
  if(position>=1){position-=std::floor(position);prevRandom=random;random=next();}
  else if(position<0)position=0;
 }
 static double triangle(double p){return p<.5?p*2:1-(p-.5)*2;}
 double process()const{
  double v;
  switch(shape){
   case Sine:v=(std::sin((position-.25)*2*3.14159265358979)+1)*.5;break;
   case Triangle:v=triangle(position);break;
   case Square:v=position<.5?1:0;break;
   case Saw:v=position;break;
   case ReverseSaw:v=1-position;break;
   case Thorn:v=std::pow(triangle(position),2.);break;
   case Random:v=random;break;
   default:v=prevRandom+(random-prevRandom)*position;break;
  }
  return v*depth;
 }
 double processZeroToTwo()const{return process()*2-depth+1;}
};

class BufferOverrideFx final:public NativeFx{
 std::vector<float> buffers[2];
 float divisor=4,smoothPortion=.09f,decayDepth=0;double portion=1;bool interrupt=true;
 int sizeIndex=7,divRate=7,bufRate=7;double spb=22050;
 long forcedSize=1,writePos=1,miniSize=1,audible=1,prevMiniSize=0,readPos=1;
 float decayGain=1,prevDecayGain=0;
 long fadeInLength=0,fadeOutLength=0,fadeInCount=0,fadeOutCount=0;float fadeInStep=0,fadeOutStep=0;
 bool needResync=true;uint32_t epoch=0;long long sinceEpoch=0;
 Lfo divisorLfo{0x0b0e1f01u},bufferLfo{0x0b0e1f02u};
 static float stepAmount(long length){return 1.f/float(length+1);}
 long bufferFrames()const{return long(buffers[0].size());}
 void restart(){
  forcedSize=1;writePos=1;miniSize=1;prevMiniSize=0;fadeInLength=fadeInCount=0;fadeOutLength=fadeOutCount=0;needResync=true;
 }
 void updateBuffer(){
  bool doSmoothing=true,barSync=false;
  readPos=0;prevMiniSize=miniSize;
  long prevForcedSize=forcedSize,prevAudible=audible;
  bool forcedBeginning=writePos>=forcedSize,prevShortened=prevAudible<prevMiniSize;

  // the LFOs
  divisorLfo.updatePosition(prevMiniSize);bufferLfo.updatePosition(prevMiniSize);
  float divisorLfoValue=float(divisorLfo.processZeroToTwo());
  double bufferLfoValue=2-bufferLfo.processZeroToTwo();   // inverted: makes more pitch sense
  divisorLfo.step=1/(nativeDivisionBeats(divRate)*spb);bufferLfo.step=1/(nativeDivisionBeats(bufRate)*spb);

  // forced buffer size
  if(forcedBeginning){
   writePos=0;doSmoothing=prevAudible<prevForcedSize;
   forcedSize=std::lround(nativeDivisionBeats(sizeIndex)*spb);
   if(needResync)barSync=true;
   forcedSize=std::lround(double(forcedSize)*bufferLfoValue);
   forcedSize=std::clamp(forcedSize,2L,bufferFrames());
   needResync=false;
  }

  // divisor
  float currentDivisor=divisor;
  if(currentDivisor>=ActiveDivisorMinimum)currentDivisor=std::max(currentDivisor*divisorLfoValue,ActiveDivisorMinimum);

  // minibuffer size
  if(!forcedBeginning){
   if(interrupt)miniSize=std::lround(float(forcedSize)/currentDivisor);
   long remaining=forcedSize-writePos;
   if(miniSize*2>=remaining)miniSize=remaining;
  }else{
   double samplesPerBar=spb*4,samplesToNextBar=samplesPerBar-std::fmod(double(sinceEpoch),samplesPerBar);
   if(samplesToNextBar>=samplesPerBar)samplesToNextBar=0;
   samplesToNextBar=std::max(samplesToNextBar,0.);
   if(barSync){divisorLfo.syncToTheBeat(samplesToNextBar);bufferLfo.syncToTheBeat(samplesToNextBar);}
   if(currentDivisor<ActiveDivisorMinimum){
    long align=std::lround(samplesToNextBar)%forcedSize;
    if(barSync&&align>0)forcedSize=align;
    miniSize=forcedSize;
   }else{
    miniSize=std::lround(float(forcedSize)/currentDivisor);
    if(barSync){
     long countdown=std::lround(samplesToNextBar)%forcedSize;
     if(countdown<miniSize*2)forcedSize+=countdown;else forcedSize=countdown;
     forcedSize=std::min(forcedSize,bufferFrames());
    }
   }
  }
  miniSize=std::max(miniSize,1L);
  audible=std::max(std::lround(double(miniSize)*portion),1L);

  // decay (gain mode, ramp shape)
  prevDecayGain=decayGain;
  float positionNormalized=float(writePos)/float(forcedSize);
  float decay=1-std::fabs(decayDepth),t=decayDepth<0?positionNormalized:1-positionNormalized;
  decay=decay+(1-decay)*t;decayGain=decay*decay;

  // smoothing durations
  auto accelerate=[this](long&length,long&countDown){
   float progress=float(countDown)/float(length);countDown=audible;length=std::lround(float(countDown)/progress);
  };
  fadeInLength=std::lround(smoothPortion*float(miniSize));
  if(prevMiniSize==prevForcedSize&&prevShortened){
   float progress=fadeOutLength>0?float(fadeOutCount)/float(fadeOutLength):0.f;
   fadeInCount=std::lround((1-progress)*float(fadeInLength));
   if(fadeInCount>audible)accelerate(fadeInLength,fadeInCount);
  }else if(doSmoothing){
   long maxSmooth=forcedBeginning?prevForcedSize-prevMiniSize:bufferFrames()-prevMiniSize;
   fadeInLength=std::min({fadeInLength,maxSmooth,audible});fadeInCount=fadeInLength;
  }else fadeInLength=fadeInCount=0;
  fadeInStep=stepAmount(fadeInLength);
  if(!prevShortened){fadeOutLength=fadeInLength;fadeOutCount=fadeInCount;fadeOutStep=fadeInStep;}
  else if(fadeOutCount>audible){accelerate(fadeOutLength,fadeOutCount);fadeOutStep=stepAmount(fadeOutLength);}
 }
public:
 BufferOverrideFx(){for(auto&b:buffers)b.assign(size_t(MaxFrames),0.f);epoch=nativeTransportEpoch();}
 void configure(const uint8_t*v,float bpm)override{
  float u=v[0]*.01f;divisor=std::pow(256.f,u);   // 1..256 (original 1.92..222, squared)
  int size=nativeDivision(v[1]);if(size!=sizeIndex)needResync=true;sizeIndex=size;
  interrupt=v[2]>=50;smoothPortion=v[3]*.01f;
  divRate=nativeDivision(v[4]);divisorLfo.depth=v[5]*.01;divisorLfo.shape=nativeChoice(v[6],8);
  bufRate=nativeDivision(v[7]);bufferLfo.depth=v[8]*.01;bufferLfo.shape=nativeChoice(v[9],8);
  portion=std::clamp(v[10],uint8_t(1),uint8_t(100))*.01;decayDepth=(int(v[11])*2-100)*.01f;
  spb=nativeSamplesPerBeat(bpm);
 }
 void clear()override{
  for(auto&b:buffers)std::fill(b.begin(),b.end(),0.f);
  forcedSize=1;writePos=readPos=1;miniSize=audible=1;prevMiniSize=0;fadeInLength=fadeOutLength=fadeInCount=fadeOutCount=0;
  decayGain=1;prevDecayGain=0;divisorLfo.reset();bufferLfo.reset();needResync=true;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;sinceEpoch=0;restart();}   // downbeat
  constexpr float halfPi=NativePi/2;const long last=bufferFrames()-1;
  for(int i=0;i<frames;i++){
   if(readPos>=miniSize)updateBuffer();
   float in[2]={io[i*2],io[i*2+1]},out[2]={0,0};
   for(int c=0;c<2;c++)buffers[c][size_t(std::min(writePos,last))]=in[c];
   if(readPos==audible){
    fadeOutLength=std::min({long(std::lround(smoothPortion*float(miniSize))),bufferFrames()-audible,audible});
    fadeOutCount=fadeOutLength;fadeOutStep=stepAmount(fadeOutLength);
   }
   bool audiblePart=readPos<audible;
   if(audiblePart||fadeOutCount>0)for(int c=0;c<2;c++)out[c]=buffers[c][size_t(readPos)]*decayGain;
   if(fadeInCount>0){
    float g=std::sin(halfPi*float(fadeInLength-fadeInCount+1)*fadeInStep);out[0]*=g;out[1]*=g;fadeInCount--;
   }
   if(fadeOutCount>0){
    float g=std::cos(halfPi*float(fadeOutLength-fadeOutCount+1)*fadeOutStep);
    if(audiblePart){size_t at=size_t(std::min(readPos+prevMiniSize,last));for(int c=0;c<2;c++)out[c]+=buffers[c][at]*prevDecayGain*g;}
    else{out[0]*=g;out[1]*=g;}
    fadeOutCount--;
   }
   io[i*2]=nativeClean(out[0]);io[i*2+1]=nativeClean(out[1]);
   readPos++;writePos++;sinceEpoch++;
  }
 }
};
}
const NativeInfo bufferOverrideInfo{FxType::BufferOverride,"BUFFER OVERRIDE","BUFOVR","BO",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeBufferOverride(){return std::make_unique<BufferOverrideFx>();}
}
