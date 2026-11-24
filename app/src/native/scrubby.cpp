// SPDX-License-Identifier: GPL-3.0-only
// SCRUBBY: a one-to-one recreation of Destroy FX Scrubby (C) 2002-2026 Sophia Poirier,
// GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/scrubby).
// The input runs into a ring buffer; each seek cycle picks a random target within the
// seek range behind the write point and the reader travels there over the seek
// duration (a share of the cycle), then holds. ROBOT moves at a constant speed (which
// pitch constraint can snap to the semitones of a note set, within an octave range);
// DJ accelerates exponentially like a turntable, the target speed solved with
// Scrubby's Lambert W approximation. Reads are Hermite-interpolated and highpassed at
// 39 Hz; channels seek together or independently; freeze stops writing.
// Changed: the seek rate is always tempo-synced (shared division table, with a random
// minimum); the free Hz rate, user tempo and predelay are omitted, and the host MIX
// replaces dry/wet. MIDI note control of the pitch steps is replaced by a fixed note
// set. The seek range curve is exponential here (the original is squared). A
// transport Start realigns the cycles to the downbeat; a rate change resyncs to the bar.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>
namespace amalthea {
namespace {
const char*const speedModes[]={"ROBOT","DJ"};
const char*const noteSets[]={"OCTAVES","FIFTHS","MAJOR TRIAD","MINOR TRIAD","SUS4","PENTATONIC","MAJOR SCALE","MINOR SCALE","WHOLE TONE","CHROMATIC"};
constexpr int NoteSetCount=10;
constexpr uint16_t noteMasks[NoteSetCount]={   // bit n = semitone n above the octave
 0x001,0x081,0x091,0x089,0x0a1,0x295,0xab5,0x5ad,0x555,0xfff};
const char*const octaveMins[]={"NO MIN","-4","-3","-2","-1","0"};          // -5 (no limit) .. 0
const char*const octaveMaxes[]={"0","+1","+2","+3","+4","+5","+6","NO MAX"}; // 0 .. 7 (no limit)
constexpr long OctaveMinValue=-5,OctaveMaxValue=7;
const NativeParam params[]={
 {"SEEK RANGE",71,NativeUnit::Range,35,85,nullptr,0,.3f,6000,true," ms"},
 {"FREEZE",0,NativeUnit::Toggle,0,0},
 {"SEEK RATE",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(5),nativeDivisionValue(12)},
 {"RATE RANDOM MIN",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(5),nativeDivisionValue(12)},
 {"SEEK DURATION",100,NativeUnit::Range,20,100,nullptr,0,3,100,false,"% LENGTH"},
 {"DUR RANDOM MIN",100,NativeUnit::Range,20,100,nullptr,0,3,100,false,"% LENGTH"},
 {"SPEEDS",nativeChoiceValue(0,2),NativeUnit::Choice,0,100,speedModes,2},
 {"SPLIT CHANNELS",0,NativeUnit::Toggle,0,100},
 {"PITCH CONSTRAINT",0,NativeUnit::Toggle,0,100},
 {"NOTE SET",nativeChoiceValue(0,NoteSetCount),NativeUnit::Choice,0,100,noteSets,NoteSetCount},
 {"OCTAVE MIN",nativeChoiceValue(0,6),NativeUnit::Choice,0,100,octaveMins,6},
 {"OCTAVE MAX",nativeChoiceValue(7,8),NativeUnit::Choice,0,100,octaveMaxes,8},
};

// Principal branch of the Lambert W function (Scrubby's approximation).
double lambertW(double v){
 double x=std::fabs(v);
 if(x<=500.)return .665*(1.+.0195*std::log(x+1.))*std::log(x+1.)+.04;
 return std::log(x-4.)-(1.-1./std::log(x))*std::log(std::log(x));
}
// DJ mode: the speed b reached from speed a so that n exponentially accelerating steps cover k samples.
double targetSpeed(double a,double n,double k){
 a=std::fabs(a);n=std::fabs(n);k=std::fabs(k);
 double b=k*lambertW(n*a/k)/n;
 if(!std::isfinite(b))b=1.;
 if(b<1.)b=std::pow(b,.63);
 return b;
}
// Laurent de Soras' 4-point Hermite, wrapping around the ring buffer.
float hermite(const float*d,long size,double address){
 long p=long(address);float f=float(address-double(p));
 float xm1=d[p==0?size-1:p-1],x0=d[p],x1=d[(p+1)%size],x2=d[(p+2)%size];
 float c=(x1-xm1)*.5f,v=x0-x1,w=c+v,a=w+v+(x2-x0)*.5f,bn=w+a;
 return (((a*f)-bn)*f+c)*f+x0;
}

class Scrubby final:public NativeFx{
 static constexpr long BufferFrames=360000;   // 8.16 s per channel (2.9 MB stereo)
 std::vector<float> buffer[2];
 long writePos=0;
 double readPos[2]{},readStep[2]{},portamento[2]{};long moveCount[2]{},seekCount[2]{};bool needResync[2]{};
 Biquad highpass[2];
 // parameters
 double spb=22050,rangeSeconds=.333,seekDur=1,seekDurMin=1;int rateIndex=7,rateMinIndex=7;
 bool freeze=false,split=false,pitchConstraint=false,dj=false,useRateMin=false,useDurMin=false,configured=false;
 uint16_t pitchSteps=1;long octaveMin=OctaveMinValue,octaveMax=OctaveMaxValue;
 long barPosition=0;uint32_t epoch=0,seed=0x5c2bb1e5;
 long randomLong(long lo,long hi){return hi<=lo?lo:lo+long(nativeRandom(seed)%uint32_t(hi-lo+1));}
 double randomDouble(double lo,double hi){return lo+(hi-lo)*nativeUnit(seed);}
 double pitchConstrain(double step)const{
  double direction=step<0?-1.:1.;
  if(!pitchSteps)return 0.;
  static const double ln2twelfth=std::log(std::pow(2.,1./12.));
  long semitone=long(std::floor(std::log(std::fabs(step))/ln2twelfth)+.1);
  long quot=semitone/12,rem=semitone%12;if(rem<0){rem+=12;quot-=1;}
  semitone=rem;long i=rem;
  do{
   if(pitchSteps&(1u<<i)){semitone=i;break;}
   if(--i<0){i=11;quot--;}
  }while(i!=rem);
  if(octaveMin>OctaveMinValue&&quot<octaveMin)quot=octaveMin;
  else if(octaveMax<OctaveMaxValue&&quot>octaveMax)quot=octaveMax;
  semitone+=quot*12;
  return std::pow(2.,double(semitone)/12.)*direction;
 }
 void newTarget(int ch){
  // seek cycle length
  int index=useRateMin?int(randomLong(rateMinIndex,rateIndex)):rateIndex;
  double cycleSamples=nativeDivisionBeats(index)*spb;
  seekCount[ch]=std::lround(cycleSamples);
  double cycleDur=cycleSamples/44100.;
  if(needResync[ch]){
   long bar=std::max(1L,std::lround(4*spb)),untilBar=(bar-barPosition%bar)%bar;
   if(untilBar>0&&seekCount[ch]>0){
    seekCount[ch]=dj?(untilBar+seekCount[ch])%(seekCount[ch]*2):untilBar%seekCount[ch];
    cycleDur=double(seekCount[ch])/44100.;
   }
  }
  seekCount[ch]=std::max(seekCount[ch],1L);
  // movement length
  double dur=useDurMin?randomDouble(seekDurMin,seekDur):seekDur;
  moveCount[ch]=std::max(std::lround(cycleDur*dur*44100.),1L);
  // new target within the seek range behind the write point
  long range=std::min(std::lround(rangeSeconds*44100.),BufferFrames-64);
  long target=writePos-randomLong(0,range);
  long readInt=long(readPos[ch]);if(readInt>=writePos)readInt-=BufferFrames;
  long distance=target-readInt;if(distance==0)distance=1;
  double step=double(distance)/double(moveCount[ch]);
  if(pitchConstraint&&!dj)step=pitchConstrain(step);
  else{
   double direction=step<0?-1.:1.,a=std::fabs(step);
   if(octaveMin>OctaveMinValue&&a<1.){double m=std::pow(2.,double(octaveMin));if(a<m)step=m*direction;}
   else if(octaveMax<OctaveMaxValue&&a>1.){double m=std::pow(2.,double(octaveMax));if(a>m)step=m*direction;}
  }
  if(dj){
   bool backwards=step<0;double old=std::max(std::fabs(readStep[ch]),.001);
   double b=targetSpeed(old,double(moveCount[ch]),double(distance));
   portamento[ch]=std::fabs(std::pow(std::fabs(b)/old,1./double(moveCount[ch])));
   step=backwards?-old:old;
  }else portamento[ch]=1.;
  readStep[ch]=step;needResync[ch]=false;
 }
public:
 Scrubby(){for(auto&b:buffer)b.assign(size_t(BufferFrames),0.f);for(auto&h:highpass)h.set(1,39,.70710678f);clear();}
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);
  float u=v[0]*.01f;rangeSeconds=.0003*std::pow(6000./.3,double(u));
  freeze=v[1]>=50;
  int rate=nativeDivision(v[2]),rateMin=nativeDivision(v[3]);
  bool wasUsingMin=useRateMin;
  // Division indices run from long (slow) to short (fast): the random minimum is a longer division.
  useRateMin=rateMin<rate;
  if(configured&&(rate!=rateIndex||(wasUsingMin&&!useRateMin)))needResync[0]=needResync[1]=true;
  rateIndex=rate;rateMinIndex=rateMin;
  seekDur=(3+97*v[4]*.01)*.01;seekDurMin=(3+97*v[5]*.01)*.01;useDurMin=seekDurMin<seekDur;
  dj=nativeChoice(v[6],2)==1;split=v[7]>=50;pitchConstraint=v[8]>=50;
  pitchSteps=noteMasks[nativeChoice(v[9],NoteSetCount)];
  octaveMin=OctaveMinValue+nativeChoice(v[10],6);octaveMax=nativeChoice(v[11],8);
  configured=true;
 }
 void clear()override{
  for(auto&b:buffer)std::fill(b.begin(),b.end(),0.f);
  for(int c=0;c<2;c++){readPos[c]=.001;readStep[c]=1;portamento[c]=1;moveCount[c]=0;seekCount[c]=0;needResync[c]=true;highpass[c].clear();}
  writePos=0;barPosition=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();
  if(e!=epoch){epoch=e;barPosition=0;for(int c=0;c<2;c++){needResync[c]=true;seekCount[c]=0;}}   // downbeat
  for(int i=0;i<frames;i++){
   if(!freeze){buffer[0][size_t(writePos)]=io[i*2];buffer[1][size_t(writePos)]=io[i*2+1];}
   for(int c=0;c<2;c++)io[i*2+c]=nativeClean(highpass[c].process(hermite(buffer[c].data(),BufferFrames,readPos[c])));
   if(!freeze)writePos=(writePos+1)%BufferFrames;
   barPosition++;if(barPosition>=(1L<<40))barPosition=0;
   for(int c=0;c<2;c++){seekCount[c]--;moveCount[c]--;}
   if(seekCount[0]<0){
    newTarget(0);
    if(!split){readPos[1]=readPos[0];readStep[1]=readStep[0];portamento[1]=portamento[0];seekCount[1]=seekCount[0];moveCount[1]=moveCount[0];needResync[1]=needResync[0];}
   }
   if(split&&seekCount[1]<0)newTarget(1);
   for(int c=0;c<2;c++){
    if(moveCount[c]>=0){
     if(dj)readStep[c]*=portamento[c];
     readPos[c]+=readStep[c];
     if(readPos[c]>=double(BufferFrames))readPos[c]=std::fmod(readPos[c],double(BufferFrames));
     while(readPos[c]<0)readPos[c]+=double(BufferFrames);
     if(!std::isfinite(readPos[c]))readPos[c]=0;
    }
   }
  }
 }
};
}
const NativeInfo scrubbyInfo{FxType::Scrubby,"SCRUBBY","SCRUB","SC",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeScrubby(){return std::make_unique<Scrubby>();}
}
