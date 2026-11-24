// SPDX-License-Identifier: GPL-3.0-only
// JUMBLR: a one-to-one recreation of the B.Jumblr engine, a pattern-controlled audio
// stream re-sequencer, (C) 2018-2020 Sven Jähnichen, GPL-3.0-or-later
// (https://github.com/sjaehn/BJumblr).
// The live stream is written to a ring buffer one loop (STEPS x STEP SIZE) long. On
// every step each active pad of the step's column plays the buffer from where that
// pad's row lies relative to the step, so the diagonal plays the stream unchanged and
// any other pad replays a recent (or the previous loop's later) step. SPEED shifts the
// progression by SPEED-1 steps per step and offsets the reads to match, as in the
// original; each step starts with a FADE crossfade from the previous step's pads.
// Changed: the drawn pad grid, its pages and MIDI page switching are replaced by a
// deterministic seeded pattern (PATTERN, VARIATION, JUMP DENSITY, MAX OFFSET, GAPS,
// LAYERS, LEVEL SPREAD) that takes over at the next step start, as a page change did;
// the fixed 10 ms fade became FADE. Omitted: the sample source, the GUI waveform,
// manual progression delay, seconds/bars step bases (steps use the shared division
// table) and host-transport position (the loop restarts on transport Start instead).
// The buffer holds 8 s; longer loops keep their timing but read within the 8 s, as
// the original clamps its buffer size.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
constexpr int MaxSteps=32;
constexpr int Capacity=44100*8;     // frames per channel (2.8 MB stereo)
const char*const stepCounts[]={"2","3","4","6","8","12","16","24","32"};
const int stepValues[]={2,3,4,6,8,12,16,24,32};
const char*const patterns[]={"STRAIGHT","REVERSE","SWAP HALVES","STUTTER","HALF TIME","SHUFFLE","RANDOM"};
const char*const numbers[]={"0","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16",
 "17","18","19","20","21","22","23","24","25","26","27","28","29","30","31"};
const NativeParam params[]={
 {"STEPS",nativeChoiceValue(6,9),NativeUnit::Choice,nativeChoiceValue(2,9),nativeChoiceValue(6,9),stepCounts,9},
 {"STEP SIZE",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(10),nativeDivisionValue(14)},
 {"PATTERN",0,NativeUnit::Choice,0,100,patterns,7},
 {"VARIATION",0,NativeUnit::Percent,0,100},
 {"JUMP DENSITY",35,NativeUnit::Percent,10,60},
 {"MAX OFFSET",nativeChoiceValue(3,16),NativeUnit::Choice,0,nativeChoiceValue(7,16),numbers+1,16},
 {"GAPS",0,NativeUnit::Percent,0,25},
 {"LAYERS",0,NativeUnit::Percent,0,30},
 {"LEVEL SPREAD",0,NativeUnit::Percent,0,40},
 {"SPEED",25,NativeUnit::Range,25,50,nullptr,0,0,4,false,"x"},
 {"STEP OFFSET",0,NativeUnit::Choice,0,100,numbers,32},
 {"FADE",59,NativeUnit::Range,40,75,nullptr,0,1,50,true," ms"},
};
inline double floorfrac(double v){return v-std::floor(v);}
inline double floormod(double n,double d){return n-std::floor(n/d)*d;}
// One page of pads: up to two per column (the main pad and an optional layer).
struct Pattern {
 int row[MaxSteps][2];float level[MaxSteps][2];
 bool operator==(const Pattern&o)const{
  for(int c=0;c<MaxSteps;c++)for(int k=0;k<2;k++)if(row[c][k]!=o.row[c][k]||level[c][k]!=o.level[c][k])return false;
  return true;
 }
};
class Jumblr final:public NativeFx{
 std::vector<float> buffer1,buffer2;size_t counter=0,bufferSize=Capacity;
 Pattern scheduled{},play{},last{},fresh{};bool configured=false,pending=false,lastCurrent=true;
 int steps=16,stepOffset=0;double speed=1,stepFrames=5512.5,loopFrames=88200,fadeTime=.01;
 double position=0,progressionDelay=0,progressionDelayFrac=0;uint32_t epoch=0;
 void generate(const uint8_t*v,Pattern&p){
  int pattern=nativeChoice(v[2],7),maxOffset=std::min(nativeChoice(v[5],16)+1,std::max(1,steps-1));
  float density=v[4]*.01f,gaps=v[6]*.01f,layers=v[7]*.01f,spread=v[8]*.01f;
  // The same draws per column whatever the amounts, so turning an amount only adds or removes jumps.
  uint32_t s=0x6a75b1e5u^(uint32_t(v[3])*0x9e3779b1u)^(uint32_t(steps)*0x85ebca6bu);
  int perm[MaxSteps];for(int c=0;c<steps;c++)perm[c]=c;
  for(int c=steps-1;c>0;c--)std::swap(perm[c],perm[nativeRandom(s)%uint32_t(c+1)]);
  for(int c=0;c<MaxSteps;c++){p.row[c][0]=p.row[c][1]=-1;p.level[c][0]=p.level[c][1]=0;}
  for(int c=0;c<steps;c++){
   float uJump=nativeUnit(s),uOffset=nativeUnit(s),uSign=nativeUnit(s),uGap=nativeUnit(s),uLayer=nativeUnit(s),
    uLayerRow=nativeUnit(s),uLayerLevel=nativeUnit(s),uLevel=nativeUnit(s);int any=int(nativeRandom(s)%uint32_t(steps));
   int base=c;
   switch(pattern){
    case 1:base=steps-1-c;break;
    case 2:base=(c+steps/2)%steps;break;
    case 3:base=c-c%2;break;
    case 4:base=c/2;break;
    case 5:base=perm[c];break;
    case 6:base=any;break;
    default:break;
   }
   if(uJump<density){int d=1+std::min(maxOffset-1,int(uOffset*maxOffset));base=int(floormod(base+(uSign<.5f?-d:d),steps));}
   if(uGap<gaps)continue;
   p.row[c][0]=base;p.level[c][0]=1-spread*uLevel;
   if(steps>1&&uLayer<layers){p.row[c][1]=(base+1+std::min(steps-2,int(uLayerRow*(steps-1))))%steps;p.level[c][1]=.7f*(.5f+.5f*uLayerLevel);}
  }
 }
public:
 Jumblr():buffer1(Capacity,0.f),buffer2(Capacity,0.f){}
 void configure(const uint8_t*v,float bpm)override{
  steps=stepValues[nativeChoice(v[0],9)];
  stepFrames=nativeDivisionBeats(nativeDivision(v[1]))*nativeSamplesPerBeat(bpm);loopFrames=stepFrames*steps;
  bufferSize=size_t(std::clamp(loopFrames,0.0,double(Capacity)));
  speed=v[9]*.04;stepOffset=nativeChoice(v[10],32);
  fadeTime=.001*std::pow(50.0,v[11]*.01);
  generate(v,fresh);
  if(!configured){scheduled=play=last=fresh;configured=true;}
  else if(!(fresh==scheduled)){scheduled=fresh;pending=true;}
 }
 void clear()override{
  std::fill(buffer1.begin(),buffer1.end(),0.f);std::fill(buffer2.begin(),buffer2.end(),0.f);
  position=progressionDelay=progressionDelayFrac=0;last=play=scheduled;pending=false;lastCurrent=true;
 }
 float readStep(const Pattern&p,int column,double delay,int c)const{
  float out=0;
  for(int k=0;k<2;k++){int r=p.row[column][k];if(r<0)continue;
   int stepDiff=int(floormod(column-r-delay,steps));
   size_t frame=size_t(Capacity+counter-bufferSize*(double(stepDiff)/double(steps)))%Capacity;
   out+=p.level[column][k]*(c?buffer2[frame]:buffer1[frame]);}
  return out;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;position=progressionDelay=progressionDelayFrac=0;}   // downbeat
  double advance=1/std::max(1.0,loopFrames);
  for(int i=0;i<frames;i++){
   double delay=progressionDelay;
   double step=floormod(position*steps+stepOffset+delay,steps);int iStep=std::min(int(step),steps-1);
   buffer1[counter]=io[i*2];buffer2[counter]=io[i*2+1];
   double fracTime=(step-iStep)*stepFrames/44100;
   double fade=fracTime<fadeTime?fracTime/fadeTime:1.0;
   double prev[2]={0,0},now[2]={0,0};
   if(fade<1.0){
    if(fade<.1&&pending){play=scheduled;pending=false;lastCurrent=false;}   // a new pattern takes over at a step start
    int iPrev=(iStep+steps-1)%steps;
    for(int c=0;c<2;c++)prev[c]=readStep(last,iPrev,delay,c);
   }
   else if(!lastCurrent){last=play;lastCurrent=true;}
   for(int c=0;c<2;c++)now[c]=readStep(play,iStep,delay,c);
   io[i*2]=nativeClean(float(fade*now[0]+(1-fade)*prev[0]));io[i*2+1]=nativeClean(float(fade*now[1]+(1-fade)*prev[1]));
   position=floorfrac(position+advance);
   double next=floormod(position*steps+stepOffset+delay,steps);
   if(std::min(int(next),steps-1)!=iStep){
    progressionDelayFrac+=speed-1;double f=std::floor(progressionDelayFrac);
    progressionDelay+=f;progressionDelayFrac-=f;
   }
   counter=(counter+1)%Capacity;
  }
 }
};
}
const NativeInfo jumblrInfo{FxType::Jumblr,"JUMBLR","JUMBLR","JB",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeJumblr(){return std::make_unique<Jumblr>();}
}
