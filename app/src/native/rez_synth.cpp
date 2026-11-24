// SPDX-License-Identifier: GPL-3.0-only
// REZ SYNTH: a one-to-one recreation of Destroy FX Rez Synth (C) 2001-2026 Sophia Poirier,
// GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/rezsynth).
// Every note plays a bank of resonant two-pole, two-zero (radius) filters on the input:
// the first band sits on the note's pitch, further bands rise by the separation. Bandwidth
// follows the note (constant Q). Each note has its own attack/release envelope (linear,
// exponential or a low-pass gate), its own filter memories and smoothed band
// frequencies; the "careful" amp evener and the RMS / peak / no-scaling input gain
// modes keep levels even (the "foolish" modes skip the evener). Legato plays one voice
// that glides between pitches without new attacks. Port notes: MIDI input is replaced
// by an internal, tempo-synced sequencer: a fixed-seed 16-step arpeggio over the chord
// tones of ROOT and CHORD, spread over OCTAVES, one note-on per NOTE RATE, holding the
// last NOTES notes (older ones get note-offs), with seeded velocities (60% influence,
// curve 2, Rez Synth's defaults); it restarts on transport Start. Fixed at Rez Synth's
// defaults: Q bandwidth mode, the 2-zero (radius) algorithm, octave separation, no
// frequency aliasing, decay 30 ms / sustain 100%, no pitch bend, filtered gain 1 and
// between gain 0 (no dry input between notes). The dry/wet mix is the host's MIX.
// Bands are limited to 8 (the original allows 30) for the Brick's CPU.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
namespace {
const char*const rootNames[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
const char*const chordNames[]={"MAJ","MIN","MAJ7","MIN7","DOM7","SUS4","MAJ9","MIN9","DIM7","PENTA","MIN PENTA"};
const char*const notesNames[]={"LEGATO","1","2","3","4","5","6"};
const char*const octaveNames[]={"0","1","2","3"};
const char*const bandNames[]={"1","2","3","4","5","6","7","8"};
const char*const fadeNames[]={"LINEAR","EXPONENTIAL","LOW-PASS"};
const char*const gainNames[]={"RMS","PEAK","NONE","FOOLISH RMS","FOOLISH PEAK"};
constexpr int chordCount=11;
const int chordTones[chordCount][6]={{0,4,7,-1},{0,3,7,-1},{0,4,7,11,-1},{0,3,7,10,-1},{0,4,7,10,-1},{0,5,7,-1},
 {0,4,7,11,14,-1},{0,3,7,10,14,-1},{0,3,6,9,-1},{0,2,4,7,9,-1},{0,3,5,7,10,-1}};
const NativeParam params[]={
 {"ROOT",0,NativeUnit::Choice,0,100,rootNames,12},
 {"CHORD",nativeChoiceValue(3,chordCount),NativeUnit::Choice,0,100,chordNames,chordCount},
 {"NOTE RATE",nativeDivisionValue(10),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(12)},
 {"NOTES",nativeChoiceValue(3,7),NativeUnit::Choice,0,nativeChoiceValue(4,7),notesNames,7},
 {"OCTAVES",nativeChoiceValue(1,4),NativeUnit::Choice,0,nativeChoiceValue(2,4),octaveNames,4},
 {"BANDWIDTH Q",74,NativeUnit::Range,45,90,nullptr,0,1,300,true," Q"},
 {"BANDS",nativeChoiceValue(1,8),NativeUnit::Choice,0,nativeChoiceValue(3,8),bandNames,8},
 {"SEPARATION",25,NativeUnit::Range,0,75,nullptr,0,0,48,false," st"},
 {"ATTACK",14,NativeUnit::Range,0,60,nullptr,0,1,3000,true," ms"},
 {"RELEASE",71,NativeUnit::Range,40,80,nullptr,0,1,3000,true," ms"},
 {"FADES",nativeChoiceValue(1,3),NativeUnit::Choice,0,100,fadeNames,3},
 {"GAIN MODE",0,NativeUnit::Choice,0,nativeChoiceValue(1,5),gainNames,5},
};
constexpr double SampleRate=44100,PiDivSR=3.14159265358979323846/SampleRate,TwoPiDivSR=2*PiDivSR,Nyquist=SampleRate/2;
constexpr int MaxBands=8,NumNotes=128,NumVoices=NumNotes+1,LegatoVoice=NumNotes,Steps=16;
constexpr size_t FreqStride=4;   // GetFrequencyBasedSmoothingStride(44100)
constexpr double BandwidthHzMin=.1,BandwidthHzMax=300;   // the Hz bandwidth parameter's range (amp evener)
enum{AlgNoZero,Alg2ZeroR,Alg2Zero1};
enum{ScaleNone,ScaleRMS,ScalePeak};
enum{CurveLinear,CurveCubed,CurveLowpass};

// dfx::SmoothedValue (30 ms linear ramps).
struct Smoothed {
 double current=0,target=0,step=0;size_t count=1323,duration=1323;bool reinitialize=true;
 void set(double v){
  if(reinitialize){now(v);return;}
  if(v==target)return;
  target=v;step=(target-current)/double(duration);current+=step;count=0;
 }
 void now(double v){current=target=v;count=duration;reinitialize=false;}
 void snap(){now(target);}
 bool smoothing()const{return count<duration;}
 void inc(size_t n=1){if(smoothing())count+=n;if(smoothing())current+=step*double(n);else current=target;}
 double value()const{return current;}
};
// dfx::IIRFilter as the low-pass gate uses it.
struct Iir {
 float in=1,prevIn=0,prevPrevIn=0,prevOut=0,prevPrevOut=0;   // coefficients
 float xIn1=0,xIn2=0,y1=0,y2=0,y=0;
 void reset(){xIn1=xIn2=y1=y2=y=0;}
 float process(float x){
  y2=y1;y1=y;y=x*in+xIn1*prevIn+xIn2*prevPrevIn-y1*prevOut-y2*prevPrevOut;
  if(std::fabs(y)<1e-30f)y=0;xIn2=xIn1;xIn1=x;return y;
 }
};
struct GateCoefficients{float in=1,prevIn=0,prevPrevIn=0,prevOut=0,prevPrevOut=0;};
GateCoefficients lowpassGate(double level){
 double cutoff=20*std::pow(1000.0,level);   // DfxParam::expand(level, 20, 20000, Log)
 GateCoefficients c;if(cutoff>=std::min(Nyquist,20000.0))return c;
 double w=2*3.14159265358979323846*cutoff/SampleRate,cw=std::cos(w),alpha=std::sin(w)/(2*0.70710678118654752);
 double b0=1+alpha;
 c.in=c.prevPrevIn=float((1-cw)*.5/b0);c.prevIn=float((1-cw)/b0);c.prevOut=float(-2*cw/b0);c.prevPrevOut=float((1-alpha)/b0);
 if(level<.1){float g=float(level*10);c.in*=g;c.prevIn*=g;c.prevPrevIn*=g;}   // ApplyGateFadeOut
 return c;
}
// DfxEnvelope.
struct Envelope {
 enum class Phase{Attack,Decay,Sustain,Release,Dormant} phase=Phase::Dormant;
 double attack=0,decay=0,sustain=1,release=0,start=0,targetValue=1,last=0,lengthInv=1;size_t pos=0,length=0;int curve=CurveCubed;
 void setParameters(double a,double d,double s,double r){attack=std::max(a,0.0);decay=std::max(d,0.0);sustain=std::clamp(s,0.0,1.0);release=std::max(r,0.0);
  if(sustain>=1)decay=0;else if(sustain<=0)release=0;}
 bool active()const{return phase!=Phase::Dormant;}
 static size_t index(double x){return size_t(std::max(0LL,std::llround(x)));}
 double rise(double p)const{return curve==CurveCubed?p*p*p:p;}
 double fall(double p)const{double v=1-p;if(curve==CurveCubed)v=v*v*v;return v*(start-targetValue)+targetValue;}
 void beginAttack(){
  auto previous=phase;phase=Phase::Attack;pos=0;length=index(attack*SampleRate);
  if(length>0){lengthInv=1.0/double(length);start=0;targetValue=1;
   if(previous==Phase::Release)pos=index((curve==CurveCubed?std::cbrt(last):last)*double(length));}   // resumed attack mode
  else{lengthInv=1;(void)process();}
 }
 void beginRelease(){
  phase=Phase::Release;pos=0;length=index(release*SampleRate);
  if(length>0){lengthInv=1.0/double(length);start=last;targetValue=0;}else phase=Phase::Dormant;
 }
 double process(){
  double out=0;
  switch(phase){
   case Phase::Attack:out=rise(double(pos+1)*lengthInv);if(++pos>=length){pos=0;phase=Phase::Decay;length=index(decay*SampleRate);
     if(length>0){lengthInv=1.0/double(length);start=1;targetValue=sustain;}else phase=Phase::Sustain;}break;
   case Phase::Decay:out=fall(double(pos+1)*lengthInv);if(++pos>=length){pos=0;phase=Phase::Sustain;}break;
   case Phase::Sustain:out=sustain;if(sustain<=0)phase=Phase::Dormant;break;
   case Phase::Release:out=fall(double(pos+1)*lengthInv);if(++pos>=length){pos=0;phase=Phase::Dormant;}break;
   case Phase::Dormant:out=0;break;
  }
  last=out;return out;
 }
 // processLowpassGate: the post-filter gain and the gate level.
 float gateGain()const{
  if(phase==Phase::Dormant)return 0;
  if(phase==Phase::Release&&release>0){double fade=std::min(release,.003),reverse=1-double(pos)*lengthInv,portion=fade/release;if(reverse<portion)return float(reverse/portion);}
  return 1;
 }
};
struct Note{int velocity=0;float amp=0;Envelope envelope;};

class RezSynth final:public NativeFx{
 // parameters
 double q=69,sepOctaval=1;double attackSeconds=.003,releaseSeconds=.3;
 int numBands=2,scaleMode=ScaleRMS,fadeType=CurveCubed,held=3;bool wiseAmp=true,legato=false,configured=false;
 static constexpr int resonAlgorithm=Alg2ZeroR;
 // the sequencer
 int pattern[Steps]{},velocities[Steps]{},step=0;double stepLength=11025,untilStep=0;uint32_t epoch=0;
 // DfxMidi state
 std::array<Note,NumNotes> noteTable{};Note legatoVoice;int activeLegato=NumNotes/2;
 std::array<int,NumNotes> queue{};double noteFrequency[NumNotes]{};
 // per-voice state
 Smoothed ampEvener[NumVoices],baseFreq[NumVoices],bandCenter[NumVoices][MaxBands],bandBandwidth[NumVoices][MaxBands];
 bool activeLastRender[NumVoices]{};Iir gate[NumVoices][2];
 double inputAmp[MaxBands]{},prevOutCoeff[MaxBands]{},prevPrevOutCoeff[MaxBands]{},prevPrevInCoeff[MaxBands]{};
 double prevOut[2][NumVoices][MaxBands]{},prevPrevOut[2][NumVoices][MaxBands]{},prevIn[2][NumVoices]{},prevPrevIn[2][NumVoices]{};
 float in[64]{},out[64]{};

 Note&noteState(int n){return n==LegatoVoice?legatoVoice:noteTable[size_t(n)];}
 bool noteActive(int n){return noteState(n).velocity!=0;}
 double frequencyOf(int n)const{if(n==LegatoVoice)n=activeLegato;return n>=0&&n<NumNotes?noteFrequency[n]:0;}
 double bandwidthFor(double hz)const{return hz/q;}
 void clearBands(int from){for(auto&c:prevOut)for(auto&n:c)std::fill(n+from,n+MaxBands,0.0);for(auto&c:prevPrevOut)for(auto&n:c)std::fill(n+from,n+MaxBands,0.0);}
 void clearGates(){for(auto&v:gate)for(auto&g:v)g.reset();}
 template<class F> void forEachEnvelope(F f){for(auto&n:noteTable)f(n.envelope);f(legatoVoice.envelope);}
 // DfxMidi note queue
 void insertNote(int n){
  auto it=std::find(queue.begin(),queue.end(),n);
  if(it!=queue.begin()){if(it==queue.end())it=std::prev(queue.end());std::rotate(queue.begin(),it,std::next(it));queue.front()=n;activeLegato=n;}
 }
 void removeNote(int n){
  auto it=std::find(queue.begin(),queue.end(),n);
  if(it!=queue.end()){std::rotate(it,std::next(it),queue.end());queue.back()=-1;}
  if(queue.front()>=0)activeLegato=queue.front();
 }
 void turnOffNote(int n){auto&note=noteState(n);if(note.velocity>0){note.envelope.beginRelease();if(!note.envelope.active())note.velocity=0;}}
 void setLegato(bool on){
  if(legato==on)return;legato=on;
  if(on){for(int n=0;n<NumNotes;n++)turnOffNote(n);}else turnOffNote(LegatoVoice);
  queue.fill(-1);
 }
 void noteOn(int n,int velocity){
  // checkForNewNote: a fresh note starts with clear filter memories
  if(!noteActive(n))for(int c=0;c<2;c++){std::fill(prevOut[c][n],prevOut[c][n]+MaxBands,0.0);std::fill(prevPrevOut[c][n],prevPrevOut[c][n]+MaxBands,0.0);}
  insertNote(n);
  auto setAmp=[velocity](Note&note){note.velocity=velocity;float curved=std::pow(velocity/127.f,2.f);note.amp=curved*.6f+.4f;};
  if(legato){setAmp(legatoVoice);if(!legatoVoice.envelope.active()||legatoVoice.envelope.phase==Envelope::Phase::Release)legatoVoice.envelope.beginAttack();}
  else{setAmp(noteTable[size_t(n)]);noteTable[size_t(n)].envelope.beginAttack();}
 }
 void noteOff(int n){removeNote(n);if(!legato)turnOffNote(n);}
 void sequencerStep(){
  int note=pattern[step],velocity=velocities[step];step=(step+1)%Steps;
  int count=int(std::count_if(queue.begin(),queue.end(),[](int x){return x>=0;}));
  bool held_=std::find(queue.begin(),queue.end(),note)!=queue.end();
  int keep=legato?1:held;
  if(!held_)while(count>=keep&&count>0){noteOff(queue[size_t(count-1)]);count--;}   // the oldest notes end
  while(held_&&count>keep){noteOff(queue[size_t(count-1)]);count--;}
  noteOn(note,velocity);
 }
 double calculateAmpEvener(int v){
  if(!wiseAmp)return 1;
  double base=frequencyOf(v),bandwidth=bandwidthFor(base),e=1;
  if(scaleMode==ScaleNone){double contracted=std::max(0.0,std::log(bandwidth/BandwidthHzMin)/std::log(BandwidthHzMax/BandwidthHzMin));
   e=.0000000000009*std::pow(contracted+.72,1.8)*base*base/std::sqrt(double(numBands)*.0000003);}
  else if(scaleMode==ScaleRMS)e=.0003*base/std::sqrt(double(numBands)*.39);
  else e=.0072*base/((bandwidth-BandwidthHzMin+10)/3)/(double(numBands)*.03);
  if(resonAlgorithm!=AlgNoZero){e*=2;if(scaleMode==ScaleNone)e*=6;}
  return e;
 }
 int calculateCoefficients(int v){
  baseFreq[v].set(frequencyOf(v));double base=baseFreq[v].value();
  for(int b=0;b<numBands;b++){
   bandCenter[v][b].set(base*std::pow(2.0,sepOctaval*b));double center=bandCenter[v][b].value();
   if(center>Nyquist){clearBands(b);return b;}   // frequency aliasing is resisted
   bandBandwidth[v][b].set(bandwidthFor(center));double bw=bandBandwidth[v][b].value();
   inputAmp[b]=1;double r=std::exp(bw*-PiDivSR);
   switch(resonAlgorithm){
    case AlgNoZero:
     prevPrevOutCoeff[b]=std::exp((center/base)*bw*-TwoPiDivSR);
     prevOutCoeff[b]=prevPrevOutCoeff[b]*4*std::cos(center*TwoPiDivSR)/(prevPrevOutCoeff[b]+1);prevPrevInCoeff[b]=0;
     if(scaleMode==ScaleRMS)inputAmp[b]=std::sqrt(((prevPrevOutCoeff[b]+1)*(prevPrevOutCoeff[b]+1)-prevOutCoeff[b]*prevOutCoeff[b])*(1-prevPrevOutCoeff[b])/(prevPrevOutCoeff[b]+1));
     else if(scaleMode==ScalePeak)inputAmp[b]=(1-prevPrevOutCoeff[b])*std::sqrt(1-prevOutCoeff[b]*prevOutCoeff[b]/(prevPrevOutCoeff[b]*4));
     break;
    case Alg2ZeroR:
     prevOutCoeff[b]=2*r*std::cos(center*TwoPiDivSR);prevPrevOutCoeff[b]=r*r;prevPrevInCoeff[b]=r;
     if(scaleMode==ScalePeak)inputAmp[b]=1-r;else if(scaleMode==ScaleRMS)inputAmp[b]=std::sqrt(1-r);
     break;
    default:
     prevOutCoeff[b]=2*r*std::cos(center*TwoPiDivSR);prevPrevOutCoeff[b]=r*r;prevPrevInCoeff[b]=1;
     if(scaleMode==ScalePeak)inputAmp[b]=(1-prevPrevOutCoeff[b])*.5;else if(scaleMode==ScaleRMS)inputAmp[b]=std::sqrt((1-prevPrevOutCoeff[b])*.5);
     break;
   }
  }
  return numBands;
 }
 static double clampInfinity(double x){return std::isinf(x)?std::copysign(1e30,x):x;}
 void processFilterOuts(int offset,int frames,int v,int bands){
  float envAmp=1;Note&note=noteState(v);
  for(int i=offset;i<offset+frames;i++){
   float noteAmp=note.amp,evener=float(ampEvener[v].value());
   if(fadeType==CurveLowpass){
    if(size_t(i-offset)%FreqStride==0){envAmp=note.envelope.gateGain();auto c=lowpassGate(note.envelope.process());
     for(auto&g:gate[v]){g.in=c.in;g.prevIn=c.prevIn;g.prevPrevIn=c.prevPrevIn;g.prevOut=c.prevOut;g.prevPrevOut=c.prevPrevOut;}}
    else (void)note.envelope.process();
   }else envAmp=float(note.envelope.process());
   if(!note.envelope.active())note.velocity=0;   // postprocessEnvelope
   float total=noteAmp*envAmp;
   for(int c=0;c<2;c++){
    double x=in[i*2+c],sum=0,*po=prevOut[c][v],*ppo=prevPrevOut[c][v],ppi=prevPrevIn[c][v];
    for(int b=0;b<bands;b++){
     double y=inputAmp[b]*(x-prevPrevInCoeff[b]*ppi)+prevOutCoeff[b]*po[b]-prevPrevOutCoeff[b]*ppo[b];
     y=clampInfinity(y);if(std::fabs(y)<1e-30)y=0;
     sum+=y;ppo[b]=po[b];po[b]=y;
    }
    sum=clampInfinity(sum);float scaled=float(sum*evener);
    float entry=out[i*2+c];
    out[i*2+c]+=fadeType==CurveLowpass?gate[v][c].process(scaled)*total:scaled*total;
    if(std::isinf(out[i*2+c]))out[i*2+c]=entry;
    prevPrevIn[c][v]=prevIn[c][v];prevIn[c][v]=x;
   }
   ampEvener[v].inc();
  }
 }
 void render(int offset,int frames){
  for(int v=0;v<NumVoices;v++){
   if(noteActive(v)){
    ampEvener[v].set(calculateAmpEvener(v));
    for(int sub=0;sub<frames;){
     int bands=calculateCoefficients(v);
     if(!activeLastRender[v]&&sub==0){ampEvener[v].snap();baseFreq[v].snap();for(auto&s:bandCenter[v])s.snap();for(auto&s:bandBandwidth[v])s.snap();}
     bool smoothing=baseFreq[v].smoothing();
     for(int b=0;b<bands&&!smoothing;b++)smoothing=bandCenter[v][b].smoothing()||bandBandwidth[v][b].smoothing();
     int n=smoothing?std::min(int(FreqStride),frames-sub):frames-sub;
     processFilterOuts(offset+sub,n,v,bands);
     baseFreq[v].inc(size_t(n));for(auto&s:bandCenter[v])s.inc(size_t(n));for(auto&s:bandBandwidth[v])s.inc(size_t(n));
     sub+=n;
    }
   }
   if(!noteActive(v))for(auto&g:gate[v])g.reset();
   activeLastRender[v]=noteActive(v);
  }
 }
public:
 RezSynth(){
  for(int n=0;n<NumNotes;n++)noteFrequency[n]=440*std::pow(2.0,(n-69)/12.0);
  queue.fill(-1);
  for(int v=0;v<NumVoices;v++){double f=frequencyOf(v==LegatoVoice?NumNotes/2:v);baseFreq[v].now(f);
   for(auto&s:bandCenter[v])s.now(f);for(auto&s:bandBandwidth[v])s.now(bandwidthFor(f));ampEvener[v].now(0);}
 }
 void configure(const uint8_t*v,float bpm)override{
  int root=nativeChoice(v[0],12),chord=nativeChoice(v[1],chordCount),octaves=nativeChoice(v[4],4);
  int pool[24],size=0;
  for(int o=0;o<=octaves;o++)for(int t=0;t<6&&chordTones[chord][t]>=0;t++)pool[size++]=std::min(127,48+root+12*o+chordTones[chord][t]);
  uint32_t s=0x52e25e17u;int at=0;   // a fixed seed: the pattern depends only on the parameters
  for(int i=0;i<Steps;i++){
   if(i){float r=nativeUnit(s);at=r<.55f?(at+1)%size:r<.7f?(at+size-1)%size:int(nativeRandom(s)%uint32_t(size));}
   pattern[i]=pool[at];velocities[i]=i==0?127:i%4==0?110+int(nativeRandom(s)%18):70+int(nativeRandom(s)%50);
  }
  stepLength=std::max(1.0,nativeDivisionBeats(nativeDivision(v[2]))*nativeSamplesPerBeat(bpm));untilStep=std::min(untilStep,stepLength);
  int notes=nativeChoice(v[3],7);held=std::max(1,notes);setLegato(notes==0);
  q=std::clamp(1*std::pow(300.0,v[5]*.01),1.0,300.0);
  int bands=nativeChoice(v[6],8)+1;if(bands<numBands)clearBands(bands);numBands=bands;
  sepOctaval=48*v[7]*.01/12.0;
  double a=std::pow(3000.0,v[8]*.01)*.001,r=std::pow(3000.0,v[9]*.01)*.001;
  if(!configured||a!=attackSeconds||r!=releaseSeconds){attackSeconds=a;releaseSeconds=r;forEachEnvelope([&](Envelope&e){e.setParameters(a,.03,1,r);});}
  int fade=nativeChoice(v[10],3);
  if(!configured||fade!=fadeType){fadeType=fade;forEachEnvelope([&](Envelope&e){e.curve=fade==CurveLowpass?CurveLinear:fade;});if(fade==CurveLowpass)clearGates();}
  int gain=nativeChoice(v[11],5),scale=gain==0||gain==3?ScaleRMS:gain==1||gain==4?ScalePeak:ScaleNone;wiseAmp=gain<3;
  if(configured&&scale!=scaleMode){for(auto&c:prevOut)for(auto&n:c)std::fill(n,n+MaxBands,0.0);for(auto&c:prevPrevOut)for(auto&n:c)std::fill(n,n+MaxBands,0.0);}
  scaleMode=scale;configured=true;
 }
 void clear()override{
  for(auto&n:noteTable){n.velocity=0;n.envelope.phase=Envelope::Phase::Dormant;}legatoVoice.velocity=0;legatoVoice.envelope.phase=Envelope::Phase::Dormant;
  queue.fill(-1);std::fill(std::begin(activeLastRender),std::end(activeLastRender),false);
  clearBands(0);for(auto&c:prevIn)std::fill(c,c+NumVoices,0.0);for(auto&c:prevPrevIn)std::fill(c,c+NumVoices,0.0);clearGates();
  step=0;untilStep=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;step=0;untilStep=0;}   // downbeat
  frames=std::min(frames,32);
  std::copy(io,io+frames*2,in);std::fill(out,out+frames*2,0.f);
  for(int at=0;at<frames;){
   if(untilStep<=0){sequencerStep();untilStep+=stepLength;continue;}
   int n=std::min(frames-at,int(std::ceil(untilStep)));
   render(at,n);untilStep-=n;at+=n;
  }
  for(int i=0;i<frames*2;i++)io[i]=nativeClean(out[i]);
 }
};
}
const NativeInfo rezSynthInfo{FxType::RezSynth,"REZ SYNTH","REZSYN","RZ",2,1,3,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeRezSynth(){return std::make_unique<RezSynth>();}
}
