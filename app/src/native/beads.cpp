// SPDX-License-Identifier: GPL-3.0-only
// BEADS: a one-to-one recreation of the Mutable Instruments Beads texture processor,
// Copyright 2017-2021 Emilie Gillet, MIT licence
// (https://github.com/pichenettes/eurorack/tree/master/beads); the few stmlib helpers it
// needs (Svf, soft clip, parameter interpolator, hysteresis quantizer and filter,
// polyphase resampler, Griesinger reverb engine) are by the same author, MIT licence.
// The input is recorded (16-bit, 12-bit or mu-law 8-bit, by QUALITY) into a stereo
// buffer that FREEZE stops writing, with Beads' crossfaded freeze tail. A trigger
// generator seeds up to 32 grains (regular below noon of DENSITY, random above, none
// at noon); each grain reads the buffer at TIME with Hermite interpolation, at PITCH,
// reversed for SIZE below a third, under SHAPE's envelope, with Beads' voice stealing
// and equal-power panning spread by randomness. SIZE at maximum crossfades into the
// delay mode: the multitap delay controller (TIME picks the repeat multiplier, or the
// slice when frozen; DENSITY the delay time) with its pitch-shifting taps and the
// SIZE-driven bandwidth filter. Feedback goes through Beads' soft limiter; the wet
// signal then runs through the pre-reverb limiter, the Griesinger reverb and the
// output limiter, as on the module. RANDOM is the time/size/pitch attenuverter (with
// no CV patched: positive draws every grain at random, negative draws sporadically).
// CLOCK SYNC stands in for a clock patched into SEED: pulses on the shared division
// table (realigned on transport Start) drive Beads' clock divider / probability mode
// and its clocked delay ratios.
// Changed: everything runs at 44.1 kHz. HI-FI keeps 16 bits and 4 s; LO-FI keeps 12 bits
// and 8 s at 44.1 kHz with a 15 kHz low-pass standing in for the 32 kHz codec; CLEAN TAPE
// and DIRTY TAPE run the core at half rate (22.05 kHz) through Beads' 2x resamplers,
// DIRTY TAPE in Beads' optional 2x-decimated build (mu-law buffer, wow and flutter at the
// full rate); the tape buffers are capped at 8 s. Reverb delays are rescaled from each
// mode's codec rate. Omitted: the dry path and DRY/WET (the host MIX replaces them, so
// the reverb sees only the wet signal), the input gain control, mono input, the SEED
// button taps, the gate output, the wavetable synth mode, buffer backup, CV, UI and
// calibration.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>
namespace amalthea {
namespace {
const char*const qualities[]={"HI-FI","LO-FI","CLEAN TAPE","DIRTY TAPE"};
const NativeParam params[]={
 {"TIME",30,NativeUnit::Percent,5,90},
 {"SIZE",70,NativeUnit::Percent,40,92},
 {"SHAPE",50,NativeUnit::Percent,20,90},
 {"PITCH",50,NativeUnit::Range,25,75,nullptr,0,-24,24,false," st"},
 {"DENSITY",78,NativeUnit::Percent,15,90},
 {"FEEDBACK",20,NativeUnit::Percent,0,45},
 {"REVERB",30,NativeUnit::Percent,0,60},
 {"QUALITY",0,NativeUnit::Choice,0,100,qualities,4},
 {"FREEZE",0,NativeUnit::Toggle,0,0},
 {"RANDOM",50,NativeUnit::Bipolar,25,85},
 {"CLOCK SYNC",0,NativeUnit::Toggle,0,100},
 {"CLOCK",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(14)},
};
constexpr float Rate=44100;
constexpr int Grains=32;                     // Beads' grain count
constexpr int MaxBlock=32;
constexpr int MaxDelayFrames=352800;         // 8 s at 44.1 kHz (the largest mode)

// ---- stmlib pieces -------------------------------------------------------------
struct Rng { uint32_t s=0xbead5eedu; float get(){return nativeUnit(s);} };
inline float semitonesToRatio(float s){return std::exp2(s*(1.f/12));}
inline float smoothStep(float x){return x*x*(3-2*x);}
inline float softClip(float x){return x<-3?-1:x>3?1:x*(27+x*x)/(27+9*x*x);}
inline float interpolate(const float*t,float index,float size){index*=size;int i=int(index);float f=index-float(i);return t[i]+(t[i+1]-t[i])*f;}
inline void onePole(float&o,float in,float c){o+=c*(in-o);}
inline void slope(float&o,float in,float up,float down){float e=in-o;o+=(e>0?up:down)*e;}
inline int16_t clip16(int32_t x){return int16_t(std::clamp(x,-32768,32767));}
struct Frame { float l=0,r=0; };
struct Interpolator {                     // stmlib::ParameterInterpolator
 float*state;float increment;
 Interpolator(float&s,float target,int n):state(&s),increment((target-s)/float(std::max(n,1))){}
 float next(){*state+=increment;return *state;}
};
struct Svf {                              // stmlib::Svf, FREQUENCY_FAST
 float g=0,r=0,h=0,s1=0,s2=0;
 void init(){s1=s2=0;setFQ(.01f,100);}
 void setFQ(float f,float q){f=std::min(f,.497f);const float pi=NativePi,a=3.260e-01f*pi*pi*pi,b=1.823e-01f*pi*pi*pi*pi*pi;float f2=f*f;
  g=f*(pi+f2*(a+b*f2));r=1/q;h=1/(1+r*g+g*g);}
 void copyFrom(const Svf&o){g=o.g;r=o.r;h=o.h;}
 template<bool lowpass> float process(float in){float hp=(in-r*s1-g*s1-s2)*h,bp=g*hp+s1;s1=g*hp+bp;float lp=g*bp+s2;s2=g*bp+lp;return lowpass?lp:hp;}
};
struct HysteresisQuantizer {              // stmlib::HysteresisQuantizer2
 int steps=2,value=0;float hysteresis=0,scale=2,offset=-.5f;
 void init(int n,float h,bool symmetric){steps=n;hysteresis=h;scale=float(symmetric?n-1:n);offset=symmetric?0:-.5f;value=0;}
 int process(float v){v=v*scale+offset;float sign=v>float(value)?-1.f:1.f;int q=int(v+sign*hysteresis+.5f);q=std::clamp(q,0,steps-1);value=q;return q;}
};
struct HysteresisFilter {
 float value=0,hysteresis=0;
 void init(float h){value=0;hysteresis=h;}
 float process(float in){float e=in-value;if(e>hysteresis)value=in-hysteresis;else if(e<-hysteresis)value=in+hysteresis;return value;}
};
struct SineOscillator {
 float phase=0,frequency=0;
 void init(float f=0){phase=0;frequency=f;}
 float render(){phase+=frequency;if(phase>=1)phase-=1;return .5f+.5f*std::sin(2*NativePi*phase);}
 float render(float f){frequency=f;return render();}
};
enum GateFlag:uint8_t{GateLow=0,GateHigh=1,GateRising=2,GateFalling=4};
inline uint8_t extractGateFlags(uint8_t previous,bool current){
 previous&=GateHigh;if(current)return previous?GateHigh:uint8_t(GateRising|GateHigh);return previous?GateFalling:GateLow;}
// Polyphase 2x resampler with Beads' 48-tap remez filters.
const float srcUpHalf[24]={
 -4.287457573e-04f,-8.226487157e-04f, 7.100052364e-04f, 2.247177610e-03f, 4.027580292e-04f,-4.271779932e-03f,
 -3.893072135e-03f, 4.843915649e-03f, 1.019934854e-02f,-9.694387912e-04f,-1.727659358e-02f,-1.027548927e-02f,
  2.001194882e-02f, 2.929003471e-02f,-1.083867877e-02f,-5.164495731e-02f,-1.821391107e-02f, 6.671043028e-02f,
  7.404344683e-02f,-5.604828388e-02f,-1.683376729e-01f,-2.385313356e-02f, 3.910061488e-01f, 7.674091912e-01f};
struct Resampler {
 float h[48]{},x[96]{};int at=0;
 void init(float scale){for(int i=0;i<24;i++)h[i]=h[47-i]=srcUpHalf[i]*scale;clear();}
 void clear(){std::fill(x,x+96,0.f);at=0;}
 void push(float v){at=at==0?47:at-1;x[at]=x[at+48]=v;}
 float filter(int phase,int stride)const{float s=0;for(int k=phase;k<48;k+=stride)s+=h[k]*x[at+(k-phase)/stride];return s;}
};
struct Limiter {
 float peak=.5f,peakLp=.5f,decay=0,maxPeak=1;
 void init(float d,float m){peak=peakLp=.5f;decay=d;maxPeak=m;}
 void process(Frame&f){float p=std::min(std::max(std::fabs(f.l),std::fabs(f.r)),40.f);slope(peak,p,.1f,decay);slope(peakLp,peak,.5f,decay);
  float g=peakLp<=maxPeak?1:maxPeak/peakLp;f.l*=g;f.r*=g;}
};

// ---- Random parameters ---------------------------------------------------------
struct RandomWalk {
 float phase=0,frequency=0,from=0,to=1;
 void init(float f){frequency=f;phase=0;from=0;to=1;}
 float value(Rng&rng,float attenuverter,int step){phase+=(attenuverter<=0?.25f:4.f)*frequency*float(step);
  if(phase>=1){phase-=1;from=to;to=2*rng.get()-1;}return from+(to-from)*smoothStep(phase);}
};
struct RandomizedParameter {
 float base=0,attenuverter=0;
 float randomness()const{return std::fabs(attenuverter);}
 float value(Rng&rng,RandomWalk*walk,int step,float range,bool clamp)const{
  float result=base;
  if(walk)result+=attenuverter*range*walk->value(rng,attenuverter,step);
  else{float blend=attenuverter>0?attenuverter:std::min(1.f,attenuverter*rng.get()*-3.f);
   float r=!clamp?(2*rng.get()-1)*range:rng.get()*range-result;result+=r*blend;}
  if(clamp)result=std::clamp(result,0.f,1.f);
  return result;
 }
 float value(Rng&rng)const{return value(rng,nullptr,0,1,true);}
 float pitchValue(Rng&rng)const{return semitonesToRatio(value(rng,nullptr,0,24,false));}
};
struct Parameters { RandomizedParameter time,size,shape,pitch; };

// ---- Delay line (one shared buffer, quantized by resolution) -------------------
enum Resolution{Res8,Res12,Res16};
inline uint8_t lin2MuLaw(int16_t pcm){
 int16_t mask,seg;pcm=int16_t(pcm>>2);if(pcm<0){pcm=int16_t(-pcm);mask=0x7f;}else mask=0xff;
 if(pcm>8159)pcm=8159;pcm=int16_t(pcm+(0x84>>2));
 if(pcm<=0x3f)seg=0;else if(pcm<=0x7f)seg=1;else if(pcm<=0xff)seg=2;else if(pcm<=0x1ff)seg=3;else if(pcm<=0x3ff)seg=4;
 else if(pcm<=0x7ff)seg=5;else if(pcm<=0xfff)seg=6;else if(pcm<=0x1fff)seg=7;else seg=8;
 if(seg>=8)return uint8_t(0x7f^mask);
 return uint8_t(((seg<<4)|((pcm>>(seg+1))&0x0f))^mask);
}
struct DelayLine {
 static constexpr int Tail=4,FreezeTail=256;
 std::vector<int16_t> data;Frame freezeTail[FreezeTail];float muLawTable[256];
 int maxDelay=176400;Resolution res=Res16;
 int writePtr=0,tailWritePtr=0,tailXfade=0,writtenSinceFrozen=0,durationFrames=0;bool frozenState=false;
 DelayLine():data(size_t(MaxDelayFrames+Tail)*2,0){
  for(int u=0;u<256;u++){int v=~u&0xff;int t=((v&0xf)<<3)+0x84;t<<=(unsigned(v)&0x70)>>4;muLawTable[u]=float((v&0x80)?(0x84-t):(t-0x84))/32768.f;}
 }
 void configure(int frames,Resolution r){maxDelay=std::min(frames,MaxDelayFrames);res=r;reset();}
 void reset(){writePtr=tailWritePtr=tailXfade=writtenSinceFrozen=0;frozenState=false;durationFrames=maxDelay;
  int16_t zero=res==Res8?int16_t(lin2MuLaw(0)):0;std::fill(data.begin(),data.end(),zero);for(auto&f:freezeTail)f=Frame{};}
 void store(int i,Frame s){int16_t*p=&data[size_t(i)*2];
  int32_t l=int32_t(s.l*32768.f),r=int32_t(s.r*32768.f);
  switch(res){case Res16:p[0]=clip16(l);p[1]=clip16(r);break;
   case Res12:p[0]=int16_t(clip16(l)>>4);p[1]=int16_t(clip16(r)>>4);break;
   default:p[0]=lin2MuLaw(clip16(l));p[1]=lin2MuLaw(clip16(r));break;}}
 Frame load(int i)const{const int16_t*p=&data[size_t(i)*2];
  switch(res){case Res16:return {p[0]/32768.f,p[1]/32768.f};case Res12:return {p[0]/2048.f,p[1]/2048.f};
   default:return {muLawTable[uint8_t(p[0])],muLawTable[uint8_t(p[1])]};}}
 void write(Frame in){store(writePtr,in);if(writePtr<Tail)store(writePtr+maxDelay,in);
  writePtr=writePtr==0?maxDelay-1:writePtr-1;if(++writtenSinceFrozen>=maxDelay)writtenSinceFrozen=0;if(++durationFrames>=maxDelay)durationFrames=maxDelay;}
 void write(Frame in,bool frozen){
  if(frozen){if(!frozenState)tailWritePtr=0;if(tailWritePtr<FreezeTail)freezeTail[tailWritePtr++]=in;writtenSinceFrozen=0;}
  else{if(frozenState&&tailWritePtr)tailXfade=tailWritePtr;
   if(tailWritePtr){int position=tailXfade-tailWritePtr;float g=smoothStep(float(tailWritePtr)/float(tailXfade));
    in.l+=(freezeTail[position].l-in.l)*g;in.r+=(freezeTail[position].r-in.r)*g;--tailWritePtr;}
   write(in);}
  frozenState=frozen;
 }
 Frame readHermite(int integral,float t)const{
  integral+=maxDelay-1;int i=(writePtr+integral)%maxDelay;
  Frame x0=load(i),x1=load(i+1),x2=load(i+2),x3=load(i+3),out;
  auto h=[t](float a,float b,float c2,float d){float c=(c2-a)*.5f,v=b-c2,w=c+v,aa=w+v+(d-b)*.5f,bn=w+aa;return (((aa*t)-bn)*t+c)*t+b;};
  out.l=h(x0.l,x1.l,x2.l,x3.l);out.r=h(x0.r,x1.r,x2.r,x3.r);return out;
 }
 Frame readHermite(int32_t delay)const{return readHermite(delay>>10,float(delay&1023)/1024.f);}
 bool frozen()const{return frozenState;}
 int duration()const{return durationFrames;}
};

// ---- Grains --------------------------------------------------------------------
struct GrainEnvelope {
 enum Stage{Attack,Sustain,Release,Done,Last};
 Stage stage=Done;float phase=0,increment[Last]{.5f,.5f,.5f,0},smoothness=0,minAttack=0,minRelease=0;
 void init(float sr){stage=Done;phase=0;increment[0]=increment[1]=increment[2]=.5f;increment[3]=0;smoothness=0;minAttack=.0005f*sr;minRelease=.010f*sr;}
 void start(float duration,float shape,float fractional){
  stage=Attack;phase=0;
  float sustain=std::max(1-3*shape,0.f);shape=(shape-.33333333f)*1.5f;
  if(shape<0)shape=0;else if(shape<.5f)shape=shape*shape*shape*4;
  float attack=shape,release=(1-attack)*(1-sustain);
  increment[Attack]=1/std::max(duration*attack,minAttack*(1+3*sustain));
  increment[Sustain]=1/std::max(duration*sustain,2.f);
  increment[Release]=1/std::max(duration*release,minRelease);
  smoothness=attack<.5f?2*attack:2-2*attack;
  phase+=increment[stage]*fractional;wrap();
 }
 void stop(){stage=Done;}
 void wrap(){if(phase>=1){float t=(phase-1)/increment[stage];stage=Stage(int(stage)+1);phase=t*increment[stage];}}
 float render(){if(stage==Done)return 0;float value;
  if(stage==Sustain)value=1;else{bool a=stage==Attack;float t=a?phase:1-phase,base=a?t:(t*t)*(t*t);value=base+(smoothStep(t)-base)*smoothness;}
  phase+=increment[stage];wrap();return value;}
 bool done()const{return stage>=Done;}
};
struct VoiceStealingEnvelope {
 float value=1,decrement=0;
 void resetAndPause(){value=1;decrement=0;}
 int start(float duration){decrement=1/duration;return int(value*duration);}
 float render(){value-=decrement;return std::max(value,0.f);}
 bool done()const{return value<=0;}
};
struct DelayGrain {
 static constexpr int SafetyZone=256;
 int remaining=0;GrainEnvelope envelope;VoiceStealingEnvelope stealing;
 float gain[2][2]{{.5f,.5f},{.5f,.5f}};int32_t delay=0,speed=1024;
 void init(float sr){delay=0;speed=1024;gain[0][0]=gain[0][1]=gain[1][0]=gain[1][1]=.5f;envelope.init(sr);stealing.resetAndPause();remaining=0;}
 bool done()const{return envelope.done()||stealing.done();}
 void fadeOut(int duration){if(remaining>duration)remaining=stealing.start(float(duration));}
 static bool compare(DelayGrain*const&a,DelayGrain*const&b){return a->remaining<b->remaining;}
 void start(const DelayLine&line,float d,float sp,float duration,float shape,float g,float pan,float fractional){
  float consumption=sp-(line.frozen()?0.f:1.f);d-=fractional*consumption;
  float maxDelay=float(line.maxDelay);
  if(consumption!=0)duration=std::min(duration,maxDelay/std::fabs(consumption));
  if(consumption>0)d=std::max(d,duration*consumption);else if(consumption<0)d=std::min(d,maxDelay+duration*consumption);
  delay=int32_t(d*1024.f);speed=int32_t(sp*1024.f);
  if(pan<.5f){pan*=2;gain[0][0]=g;gain[1][0]=0;gain[1][1]=pan*(2-pan)*g;gain[0][1]=(1-pan)*(1+pan)*g;}
  else{pan=(1-pan)*2;gain[1][1]=g;gain[0][1]=0;gain[0][0]=pan*(2-pan)*g;gain[1][0]=(1-pan)*(1+pan)*g;}
  envelope.start(duration,shape,fractional);remaining=int(duration);stealing.resetAndPause();
 }
 void render(const DelayLine&line,Frame&io){   // HEAD_CROSSING_POLICY_STOP with fade out
  if(done())return;
  delay-=speed;if(!line.frozen())delay+=1024;
  const int32_t tail=int32_t(line.maxDelay)<<10,fadeZone=SafetyZone<<10;
  if(delay<0||delay>=tail){delay=0;envelope.stop();}
  int32_t dd=std::abs(delay>=tail/2?delay-tail:delay);
  float ramp=dd<fadeZone?float(dd)/float(fadeZone):1;
  Frame s=line.readHermite(delay>>10,float(delay&1023)/1024.f);
  float e=smoothStep(ramp)*envelope.render()*stealing.render();s.l*=e;s.r*=e;
  io.l+=s.l*gain[0][0]+s.r*gain[0][1];io.r+=s.l*gain[1][0]+s.r*gain[1][1];
  if(--remaining,done())remaining=0;
 }
};
struct TriggerOut { bool trigger=false,gate=false;float fractional=0,regularity=0,phase=0; };
struct GranularPlayer {
 DelayGrain grain[Grains];DelayGrain*priority[Grains];
 int preemptive=0,instant=0,simpleThreshold=0,interval=1;float maxDuration=0,sampleRate=Rate;
 void init(float sr){instant=int(sr*.005f);preemptive=int(sr*.1f);simpleThreshold=int(sr*.025f);interval=1;maxDuration=sr*4;sampleRate=sr;reset();}
 void reset(){for(int i=0;i<Grains;i++){grain[i].init(sampleRate);priority[i]=&grain[i];}}
 float duration(float d)const{bool reverse=d<.3333333f;float c=reverse?d*3*(2-d*3):1-1.5f*(d-.3333333f);return semitonesToRatio(std::min(1.f,1.025f*c)*-108.f)*maxDuration;}
 int active()const{int n=0;for(auto&g:grain)n+=!g.done();return n;}
 void sortList(){
  int fade[4];
  if(interval<simpleThreshold){std::rotate(&priority[0],&priority[1],&priority[Grains]);
   fade[0]=interval+1;fade[1]=instant;fade[2]=instant;fade[3]=preemptive;if(interval>instant)std::swap(fade[0],fade[2]);}
  else{std::partial_sort(&priority[0],&priority[4],&priority[Grains],DelayGrain::compare);
   fade[0]=instant;fade[1]=instant;fade[2]=interval;fade[3]=preemptive;if(interval>preemptive)std::swap(fade[2],fade[3]);}
  for(int i=0;i<4;i++)priority[i]->fadeOut(fade[i]);
  interval=0;
 }
 void render(Rng&rng,const DelayLine&line,const TriggerOut&t,const Parameters&p,Frame&out){
  if(t.trigger){
   float d=p.size.value(rng);bool reverse=d<.3333333f;
   float n=float(active()+1),amplitude=1/std::sqrt(n);
   float spread=std::min(1.f,p.time.randomness()+p.size.randomness()+p.shape.randomness()+p.pitch.randomness()+1-t.regularity);
   float u=rng.get(),v=rng.get(),pan=.5f+(u-.5f)*std::min(.5f*n*spread*v,1.f);
   const int safety=DelayGrain::SafetyZone+1;int maxDelay=line.duration()-2*safety;
   float time=p.time.value(rng),pitch=p.pitch.pitchValue(rng),shape=p.shape.value(rng);
   priority[0]->start(line,time*float(maxDelay)+float(safety),(reverse?-1.f:1.f)*pitch,duration(d),shape,amplitude,pan,t.fractional);
   sortList();
  }
  for(auto&g:grain)g.render(line,out);
  ++interval;
 }
};

// ---- Trigger generator -----------------------------------------------------------
struct TriggerGenerator {
 float phase=0,frequency=0,targetFrequency=0,lpCoefficient=1,ratio=1,oneHertz=0,maxPulse=0;
 int division=1,audioRateInterval=0,pulseCount=0,elapsed=0,dividerReset=0,defaultReset=0;
 bool audioRate=false,acceptPulse=true;uint8_t previousFlags=GateLow;HysteresisQuantizer dividerQuantizer;
 void init(float sr){oneHertz=1/sr;maxPulse=130.81278f/sr;dividerReset=defaultReset=int(sr*4);audioRateInterval=int(sr/48);audioRate=false;
  dividerQuantizer.init(8,.075f,false);acceptPulse=true;pulseCount=0;elapsed=int(sr/2);phase=0;targetFrequency=frequency=1/float(elapsed);
  lpCoefficient=1;division=1;ratio=1;previousFlags=GateLow;}
 static float logOneMinusX(float x){return -x*(1+x*(.5f+x*(.3333f+x*.25f)));}
 void pulseTrain(Rng&rng,const uint8_t*flags,float f0,bool randomize,TriggerOut*out,int n){
  if(!randomize)ratio=1;
  for(int i=0;i<n;i++){float f=std::clamp(f0*ratio,.000001f,.25f);bool gate=flags[i]&GateHigh,trig=flags[i]&GateRising;
   phase+=gate?f:0;float t=0;bool reset=false;
   if(trig){phase=0;reset=true;}else if(phase>=1){phase-=1;t=phase/f;reset=true;}
   if(reset&&randomize)ratio=-1/logOneMinusX(.1f+.9f*rng.get());
   out[i].fractional=t;out[i].trigger=reset&&gate;out[i].gate=phase<.5f&&gate;out[i].regularity=randomize?0.f:1.f;out[i].phase=phase;}
 }
 void divider(Rng&rng,const uint8_t*flags,float density,TriggerOut*out,int n){
  static const int ratios[8]={1,2,3,4,6,8,12,16};
  int div=density<.48f?ratios[dividerQuantizer.process(density*2.15f)]:1;
  float ps=density<.48f?1.01f:std::max((density-.52f)*2.15f,0.f),probability=ps*ps,regularity=probability>=1?1.f:0.f;
  for(int i=0;i<n;i++){
   onePole(frequency,targetFrequency,lpCoefficient);phase+=frequency;
   bool gate=flags[i]&GateHigh,trig=flags[i]&GateRising;
   if(trig){
    if(elapsed>=dividerReset){pulseCount=0;phase=0;}
    dividerReset=std::max(defaultReset,4*elapsed);
    int threshold=audioRateInterval+(audioRate?8:0);audioRate=elapsed<threshold&&probability>=1;
    acceptPulse=rng.get()<probability&&(pulseCount%div)==0;
    if(acceptPulse){float period=float(elapsed),fr=1/period;
     targetFrequency=probability>=1?fr/float(div):fr*probability;
     if(audioRate){bool noGlide=div!=division;division=div;float up=(1.02f+2*fr)*frequency,down=(.98f-2*fr)*frequency;
      noGlide|=targetFrequency>up||targetFrequency<down;lpCoefficient=noGlide?1:period*oneHertz;}
     else{phase=0;lpCoefficient=1;}}
    else if(probability<1)targetFrequency*=1-probability;
    pulseCount=(pulseCount+1)%48;elapsed=0;
   }
   ++elapsed;
   if(audioRate){float t=0;bool reset=phase>=1;if(reset){phase-=1;t=phase/frequency;}out[i].fractional=t;out[i].trigger=reset;out[i].gate=phase<.5f;}
   else{if(phase>=1)phase=1;out[i].fractional=0;out[i].trigger=acceptPulse&&trig;out[i].gate=acceptPulse&&gate;}
   out[i].regularity=regularity;out[i].phase=phase;
  }
 }
 // The SEED button is locked (Beads' power-on state): an external clock selects the divider.
 void render(Rng&rng,const uint8_t*external,float density,TriggerOut*out,int n){
  if(external){divider(rng,external,std::clamp(density,0.f,1.f),out,n);return;}
  uint8_t flags[MaxBlock];for(int i=0;i<n;i++)flags[i]=previousFlags=extractGateFlags(previousFlags,true);
  if(density<.48f)pulseTrain(rng,flags,maxPulse*semitonesToRatio(-density*192),false,out,n);
  else if(density>.52f)pulseTrain(rng,flags,.5f*maxPulse*semitonesToRatio(-(1-density)*192),true,out,n);
  else for(int i=0;i<n;i++){out[i]=TriggerOut{};out[i].phase=phase;}
 }
};

// ---- Delay controller ------------------------------------------------------------
struct DelayTap { int32_t delay=0;float gain=0; };
constexpr int NumIntervals=2,NumRatios=2,NumHeads=4,NumTaps=8;
struct TapsOut { DelayTap tap[NumTaps];bool gate=false; };
inline float triangle(float phase,float pw){return phase<pw?phase/pw:1-(phase-pw)/(1-pw);}
const float pulseWidths[6]={0,.001f,.01f,.5f,.75f,.999f};
struct PlayHead {
 int start=0,remaining=0,mid=0;float phase=0,increment=0,gain=0;
 void init(){start=remaining=mid=0;phase=increment=gain=0;}
 bool done()const{return remaining==0;}
 bool halfWay()const{return remaining&&remaining==mid;}
 void begin(int s,int duration,float g){start=s;remaining=duration;mid=duration>>1;phase=0;increment=1/float(duration);gain=g;}
 void render(DelayTap&tap,float shape,bool slave){
  if(done()){tap.delay=0;tap.gain=0;return;}
  float env;
  if(slave){const float e[6]={0,0,0,triangle(phase,.5f),0,0};env=interpolate(e,shape,5);}
  else if(shape<.2f){float sq=std::min(100*triangle(phase,.5f),1.f),tr=triangle(phase,pulseWidths[1]);env=sq+(tr-sq)*shape*5;}
  else env=triangle(phase,interpolate(pulseWidths,shape,5));
  tap.delay=int32_t(start+remaining)<<10;tap.gain=gain*env;--remaining;phase+=increment;
 }
};
struct TapEnvelope {
 float phase=0,increment=0;
 void init(){phase=increment=0;}
 float render(float shape){float env;
  if(shape<.2f){float tr=triangle(phase,pulseWidths[1]);env=1+(tr-1)*shape*5;}else env=triangle(phase,interpolate(pulseWidths,shape,5));
  phase+=increment;if(phase>=1)phase-=1;return env;}
};
struct DelayClock {
 float interval_[2]{},average=0,fade=1,fadeIncrement=0,maxFadeIncrement=0,minFadeIncrement=0,jitter=0,sensitivity=1;
 bool scheduleChange=false;int next=0,maxInterval=0,rotate=0;
 void init(float sr){average=interval_[0]=interval_[1]=sr*.25f;maxInterval=int(sr*4);fade=1;minFadeIncrement=1/(sr*.25f);maxFadeIncrement=1/(sr*.02f);
  fadeIncrement=maxFadeIncrement;jitter=sr*.001f;sensitivity=1;scheduleChange=false;next=0;rotate=0;}
 void process(bool tick){
  ++next;fade+=fadeIncrement;if(fade>=1)fade=1;
  if(tick){
   if(next<maxInterval){float iv=float(next);bool big=std::fabs(iv-interval_[1])>jitter,small=std::fabs(average-interval_[1])>sensitivity*jitter;
    if(big){sensitivity=1;average=iv;}else{onePole(average,iv,.1f);sensitivity=std::max(.05f,sensitivity*.95f);}
    scheduleChange=scheduleChange||big||small;}
   next=0;
  }
  if(scheduleChange&&fade>=1){++rotate;interval_[0]=interval_[1];interval_[1]=average;fade=0;
   fadeIncrement=std::clamp(2/average,minFadeIncrement,maxFadeIncrement);scheduleChange=false;}
 }
 void update(float iv,float c){onePole(interval_[0],iv,c);interval_[1]=interval_[0];fade=1;rotate=0;}
 float interval(int i)const{return interval_[(i+rotate)%2];}
 float gain(int i)const{i=(i+rotate)%2;return i?fade:1-fade;}
 bool retrig()const{return fade==0&&!next;}
 int newIntervalIndex()const{return (rotate+1)%2;}
};
const float delayRatios[22]={1/16.f,1/16.f,1/8.f,1/8.f,1/4.f,1/4.f,1/2.f,1/2.f,1,1,1,1,1/2.f,1/3.f,1/4.f,1/6.f,1/8.f,1/10.f,1/12.f,1/16.f,1/16.f,1/16.f};
struct DelayController {
 static constexpr int MinDelay=64+4;
 int maxDelay=0;DelayClock clock;PlayHead head[NumHeads],slave[NumHeads];HysteresisQuantizer numerator[NumHeads];TapEnvelope envelope[NumTaps];
 RandomWalk positionWalk,shapeWalk;SineOscillator tapModulation;HysteresisFilter densityFilter,sliceFilter;
 float multiplierLp=0,shape_=0,density_=0;bool frozen=false;
 void init(float sr,int md){maxDelay=md;clock.init(sr);
  for(int i=0;i<NumHeads;i++){numerator[i].init(2,.05f,false);head[i].init();slave[i].init();}
  for(auto&e:envelope)e.init();
  positionWalk.init(.1f/sr);shapeWalk.init(.1f/sr);tapModulation.init(.17f/sr);
  densityFilter.init(.0025f);sliceFilter.init(.0025f);multiplierLp=shape_=density_=0;frozen=false;}
 // Unsynchronized when the SEED button is locked with nothing patched; synchronized to a clock.
 void render(Rng&rng,const uint8_t*external,const bool*freeze,float density,const RandomizedParameter&position,const RandomizedParameter&shapeParam,TapsOut*out,int n){
  const bool synchronized=external!=nullptr;
  density=std::clamp(density,0.f,1.f);
  float sliceModulation=1.005f*position.value(rng,&positionWalk,n,1,true);
  Interpolator modShape(shape_,shapeParam.value(rng,&shapeWalk,n,1,true)*.995f,n);
  Interpolator modDensity(density_,densityFilter.process(density),n);
  for(int s=0;s<n;s++){
   float tapRatio[NumRatios],tapGain[NumRatios];float d=modDensity.next();
   if(synchronized){
    clock.process(external[s]&GateRising);
    float ratio=d*20;int ri=int(ratio);float rf=ratio-float(ri);rf=std::clamp((rf-.5f)*(d<.5f?4.f:1.5f)+.5f,0.f,1.f);
    int flip=ri&1;tapRatio[flip]=delayRatios[ri];tapRatio[1-flip]=delayRatios[ri+1];tapGain[flip]=1-rf;tapGain[1-flip]=rf;
   }else{
    float iv=std::max(float(MinDelay),float(maxDelay)*semitonesToRatio(-std::fabs(d-.5f)*216));
    clock.update(iv,std::clamp(10/iv,.005f,.7f));
    tapRatio[0]=1;tapRatio[1]=.77f;
    float odd=std::clamp((d-.5f)*25,0.f,1.f);odd*=.25f+.75f*tapModulation.render();
    tapGain[0]=1-odd*.2f;tapGain[1]=odd*.8f;
   }
   float shape=modShape.next();TapsOut&o=out[s];
   if(freeze[s]){
    if(!frozen)for(int i=0;i<NumHeads;i++){head[i].init();slave[i].init();}
    if(clock.retrig()){int st=clock.newIntervalIndex()*NumRatios;for(int i=st;i<st+NumRatios;i++){head[i].init();slave[i].init();}}
    float slice=sliceFilter.process(sliceModulation);
    for(int i=0;i<NumHeads;i++){
     PlayHead*h=head[i].done()?&head[i]:head[i].halfWay()?&slave[i]:nullptr;
     int iv=i/NumRatios,ra=i%NumRatios;
     if(h){int sliceDuration=std::max(1,int(clock.interval(iv)*tapRatio[ra]));int slices=std::max(1,maxDelay/sliceDuration);
      if(numerator[i].steps!=slices)numerator[i].init(slices,.05f/5*float(slices),false);
      h->begin(sliceDuration*numerator[i].process(slice)+MinDelay,sliceDuration,tapGain[ra]*clock.gain(iv));}
     head[i].render(o.tap[2*i],shape,false);slave[i].render(o.tap[2*i+1],shape,true);
    }
    o.gate=head[clock.newIntervalIndex()*NumRatios].phase<.5f;
   }else{
    if(frozen)for(auto&e:envelope)e.init();
    if(clock.retrig()){int st=clock.newIntervalIndex()*NumHeads;for(int i=st;i<st+NumHeads;i++)envelope[i].init();}
    onePole(multiplierLp,sliceModulation,.01f);
    float m=std::clamp(multiplierLp*15+1,1.f,16.f);int mi=int(m);float mf=std::clamp(.5f+8*(m-float(mi)-.5f),0.f,1.f);
    for(int t=0;t<NumTaps;t++){
     int iv=t/NumHeads,ra=(t/NumIntervals)%NumRatios,tp=t&1;
     int i=(2*ra+((mi&1)?tp:(1-tp)))+iv*NumHeads;
     float time=std::clamp(clock.interval(iv)*tapRatio[ra]*float(mi+tp),float(MinDelay),float(maxDelay));
     envelope[i].increment=1/time;o.tap[i].delay=int32_t(time*1024.f);
     float base=tapGain[ra]*(tp?mf:1-mf)*clock.gain(iv);o.tap[i].gain=base*envelope[i].render(shape);
    }
    o.gate=false;
   }
   frozen=freeze[s];
  }
 }
};

// ---- Multitap delay --------------------------------------------------------------
struct PitchTap {
 int32_t phase=0;
 void render(const DelayLine&line,int32_t delay,float gain,float speed,Frame&out,int window){
  if(delay==0)return;
  if(window==1){if(gain==0)return;Frame s=line.readHermite(delay);out.l+=gain*s.l;out.r+=gain*s.r;return;}
  const int32_t full=1024*window,half=full>>1;
  phase+=int32_t(int(1024.f*(speed-1)));if(phase>=full)phase-=full;else if(phase<0)phase+=full;
  if(gain==0)return;   // silent tap: skip the reads (the phase still advances)
  delay=std::max(full+1,delay+half);
  float p=float(phase)/float(full),tri=p<.5f?2*p:2-2*p;
  Frame a=line.readHermite(-phase+delay),b=line.readHermite(-((phase+half)%full)+delay);
  out.l+=(b.l+(a.l-b.l)*tri)*gain;out.r+=(b.r+(a.r-b.r)*tri)*gain;
 }
};
struct MultitapDelay {
 PitchTap taps[NumTaps],direct[NumTaps];Svf filter[2];
 void init(){reset();filter[0].init();filter[1].init();}
 void reset(){for(auto&t:taps)t.phase=0;for(auto&t:direct)t.phase=0;}
 void render(const DelayLine&line,const TapsOut&t,float speed,float bandwidth,Frame&out){
  float gain=std::clamp(100*(std::fabs(speed-1)-.01f),0.f,1.f);Frame mix;
  for(int i=0;i<NumTaps;i++){taps[i].render(line,t.tap[i].delay,t.tap[i].gain*gain,speed,mix,2048);direct[i].render(line,t.tap[i].delay,t.tap[i].gain*(1-gain),speed,mix,1);}
  float amount=std::fabs(2*bandwidth-1),octave=bandwidth<.5f?-7+bandwidth*10:-8+(bandwidth-.5f)*10;
  float f=.49f*semitonesToRatio(12*octave),q=1+16*amount*f,ws=amount*(2-amount),wet=ws*(2-ws);
  filter[0].setFQ(f,q);filter[1].copyFrom(filter[0]);
  Frame w;if(bandwidth<.5f){w.l=filter[0].process<true>(mix.l);w.r=filter[1].process<true>(mix.r);}else{w.l=filter[0].process<false>(mix.l);w.r=filter[1].process<false>(mix.r);}
  out.l+=mix.l+(w.l-mix.l)*wet;out.r+=mix.r+(w.r-mix.r)*wet;
 }
};

// ---- Reverb (Griesinger topology on a 16-bit FxEngine), lengths scaled per mode --
struct Reverb {
 static constexpr int Size=32768,Mask=Size-1,Lines=10;
 static constexpr int lengths[Lines]={150,214,319,527,2182,2690,4501,2525,2197,6312};
 std::vector<int16_t> buffer;int base[Lines]{},length[Lines]{};int writePtr=0;
 float scale=1,lfoValue[2]{},lfoPhase[2]{},lfoFrequency[2]{};
 float mix=0,wet_=0,dry_=0,send_=0,time=.5f,lp=.7f,lp1=0,lp2=0;
 float acc=0,previousRead=0;
 Reverb():buffer(Size,0){}
 void configure(float codecRate){scale=Rate/codecRate;int b=0;for(int i=0;i<Lines;i++){length[i]=int(lengths[i]*scale+.5f);base[i]=b;b+=length[i]+1;}reset();}
 void reset(){std::fill(buffer.begin(),buffer.end(),int16_t(0));writePtr=0;lfoFrequency[0]=.5f/Rate*32;lfoFrequency[1]=.3f/Rate*32;
  lfoPhase[0]=lfoPhase[1]=0;lfoValue[0]=lfoValue[1]=1;lp1=lp2=0;mix=wet_=dry_=send_=0;}
 static int16_t compress(float x){return clip16(int32_t(x*32768.f));}
 float at(int index)const{return buffer[size_t(index&Mask)]/32768.f;}
 void read(int line,int offset,float k){float r=at(writePtr+base[line]+(offset<0?length[line]-1:offset));previousRead=r;acc+=r*k;}
 void write(int line,int offset,float k){buffer[size_t((writePtr+base[line]+(offset<0?length[line]-1:offset))&Mask)]=compress(acc);acc*=k;}
 void writeAllPass(int line,float k){write(line,0,k);acc+=previousRead;}
 void interp(int line,float offset,int lfo,float amplitude,float k){
  offset=(offset+amplitude*lfoValue[lfo])*scale;int i=int(offset);float f=offset-float(i);
  float a=at(writePtr+i+base[line]),b=at(writePtr+i+base[line]+1),x=a+(b-a)*f;previousRead=x;acc+=x*k;}
 void process(Frame*io,int n){
  const float kap=.625f,klp=lp,krt=time;
  float inputGain=.2f*(1-.333f*time),outputGain=.4f/inputGain;
  float ss=mix*(2-mix),send=ss*(2-ss),dry=(1-mix)*(1+mix);
  Interpolator wetM(wet_,outputGain,n),dryM(dry_,dry,n),sendM(send_,send*inputGain,n);
  for(int s=0;s<n;s++){
   float da=dryM.next(),wa=wetM.next(),sa=sendM.next(),wet,apout;
   if(--writePtr<0)writePtr+=Size;
   if((writePtr&31)==0)for(int k=0;k<2;k++){lfoPhase[k]+=lfoFrequency[k];if(lfoPhase[k]>=1)lfoPhase[k]-=1;lfoValue[k]=.5f+.5f*std::cos(2*NativePi*lfoPhase[k]);}
   acc=0;previousRead=0;
   interp(0,10,0,80,1);write(0,int(100*scale+.5f),0);
   acc+=(io[s].l+io[s].r)*sa;
   read(0,-1,kap);writeAllPass(0,-kap);read(1,-1,kap);writeAllPass(1,-kap);
   read(2,-1,kap);writeAllPass(2,-kap);read(3,-1,kap);writeAllPass(3,-kap);apout=acc;
   acc=apout;interp(9,6261,1,50,krt);lp1+=klp*(acc-lp1);acc=lp1;
   read(4,-1,-kap);writeAllPass(4,kap);read(5,-1,kap);writeAllPass(5,-kap);write(6,0,1);wet=acc;
   io[s].l=io[s].l*da+wet*wa;
   acc=apout;interp(6,4460,0,40,krt);lp2+=klp*(acc-lp2);acc=lp2;
   read(7,-1,kap);writeAllPass(7,-kap);read(8,-1,-kap);writeAllPass(8,kap);write(9,0,1);wet=acc;
   io[s].r=io[s].r*da+wet*wa;
  }
 }
};

// ---- Wow and flutter (DIRTY TAPE) ------------------------------------------------
struct WowAndFlutter {
 static constexpr int Length=256;
 float line[2][Length]{};int writePtr=0;
 float oneHertz=1/Rate,delay_=0,lp=1,randomFrequency=0,scale=1;Frame state;SineOscillator random,wow,flutter;
 void init(float codecRate,float codecLp){std::fill(&line[0][0],&line[0][0]+2*Length,0.f);writePtr=0;random.init();wow.init();flutter.init();
  randomFrequency=.125f*oneHertz;delay_=0;scale=Rate/codecRate;lp=1-std::pow(1-codecLp,codecRate/Rate);state=Frame{};}
 float read(int c,float d)const{int i=int(d);float f=d-float(i);   // after the write pointer moved: 1 = newest
  float a=line[c][(writePtr+i)%Length],b=line[c][(writePtr+i+1)%Length];return a+(b-a)*f;}
 void process(Rng&rng,Frame*io,int n){
  float previous=random.phase,r=random.render(randomFrequency*float(n));
  if(random.phase>=.25f&&previous<.25f)randomFrequency=oneHertz*(.05f+rng.get()*.2f);
  float fm=.5f+1.5f*r*r,amount=1-.4f*r;
  float w=wow.render(oneHertz*.23f*float(n)*fm),f=flutter.render(oneHertz*1.99f*float(n));
  Interpolator dm(delay_,(1+(105*w+17*f)*amount)*scale,n);
  for(int s=0;s<n;s++){float d=dm.next();
   line[0][writePtr]=io[s].l;line[1][writePtr]=io[s].r;writePtr=writePtr==0?Length-1:writePtr-1;
   onePole(state.l,read(0,d),lp);onePole(state.r,read(1,d),lp);io[s]=state;}
 }
};

// ---- The whole chain -------------------------------------------------------------
struct Mode { Resolution res;int ratio;int maxDelay;float codecRate; };
const Mode modes[4]={
 {Res16,1,176400,48000},   // HI-FI: 16-bit, 4 s
 {Res12,1,352800,32000},   // LO-FI: 12-bit, 8 s
 {Res12,2,176400,48000},   // CLEAN TAPE: 12-bit at half rate, 8 s
 {Res8,2,176400,48000},    // DIRTY TAPE: mu-law at half rate, 8 s
};
class BeadsFx final:public NativeFx{
 Rng rng;DelayLine line;GranularPlayer player;MultitapDelay multitap;TriggerGenerator triggers;DelayController controller;
 Reverb reverb;WowAndFlutter wowFlutter;Limiter inputLimiter,preReverbLimiter,outputLimiter;
 Svf dcBlocker[2];Resampler down[2],up[2];RandomWalk pitchWalk;Biquad lofi[2][2];
 Parameters p;float density=.78f,feedbackAmount=.2f,reverbAmount=.3f;bool freezeOn=false,clockSync=false;int mode=-1,clockDivision=12;
 double spb=22050,clockPos=0;uint32_t epoch=0;uint8_t clockFlags=GateLow;
 float feedback_=0,speed_=1,bandwidth_=0,wet_=0,transition=-1.5f;Frame previous;
 // Half-rate pairing state.
 int pairPhase=0;TriggerOut pairTrigger;TapsOut pairTaps;bool pairFreeze=false;Frame pending;
 // Block constants for the core.
 float lpCoefficient=.999f,softLimitGain=0,maxFeedbackGain=3;bool delayMode=false;
 void setMode(int m){
  mode=m;const Mode&md=modes[m];float sr=Rate/float(md.ratio);
  line.configure(md.maxDelay,md.res);player.init(sr);multitap.init();
  for(auto&d:dcBlocker){d.init();d.setFQ(40/sr,.5f);}
  for(int c=0;c<2;c++){down[c].init(.5f);up[c].init(1);}
  pitchWalk.init(.2f/sr);inputLimiter.init(1/sr,.95f);
  preReverbLimiter.init(1/(Rate*4),.9f);outputLimiter.init(1/(Rate*6),.9f);
  triggers.init(Rate);controller.init(Rate,md.maxDelay*md.ratio);reverb.configure(md.codecRate);
  wowFlutter.init(48000,.6f);
  for(auto&ch:lofi)for(auto&b:ch){b.set(0,15000,.70710678f);b.clear();}
  feedback_=0;speed_=1;bandwidth_=0;previous=Frame{};pairPhase=0;pending=Frame{};
  const float lps[3]={.75f,.9f,.999f},gains[3]={1.75f,1.3f,0};lpCoefficient=lps[md.res];softLimitGain=gains[md.res];
 }
 void core(const TriggerOut*t,const TapsOut*taps,bool freeze,float feedback,float speed,float bandwidth,Frame in,Frame&out){
  Frame s=in;
  if(softLimitGain){float g=maxFeedbackGain/softLimitGain;s.l+=previous.l*feedback*g;s.r+=previous.r*feedback*g;}
  else{s.l+=previous.l*feedback*feedback*maxFeedbackGain;s.r+=previous.r*feedback*feedback*maxFeedbackGain;}
  inputLimiter.process(s);
  if(softLimitGain){s.l=softClip(s.l*softLimitGain);s.r=softClip(s.r*softLimitGain);s.l=in.l+feedback*(s.l-in.l);s.r=in.r+feedback*(s.r-in.r);}
  out=Frame{};line.write(s,freeze);
  if(t)player.render(rng,line,*t,p,out);
  if(taps)multitap.render(line,*taps,speed,bandwidth,out);
  onePole(previous.l,dcBlocker[0].process<false>(out.l),lpCoefficient);
  onePole(previous.r,dcBlocker[1].process<false>(out.r),lpCoefficient);
 }
 static float attenuverter(uint8_t v){float u=v*.01f-.5f;return std::clamp((2/.47f)*u*std::max(std::fabs(u)-.03f,0.f),-1.f,1.f);}
public:
 BeadsFx(){setMode(0);}
 void configure(const uint8_t*v,float bpm)override{
  p.time.base=v[0]*.01f;p.size.base=v[1]*.01f;p.shape.base=v[2]*.01f;p.pitch.base=-24+48*v[3]*.01f;density=v[4]*.01f;
  feedbackAmount=std::min(1.f,v[5]*.01f*1.005f);reverbAmount=std::min(1.f,v[6]*.01f*1.005f);
  int m=nativeChoice(v[7],4);if(m!=mode)setMode(m);
  freezeOn=v[8]>=50;float a=attenuverter(v[9]);p.time.attenuverter=p.size.attenuverter=p.pitch.attenuverter=a;p.shape.attenuverter=0;
  clockSync=v[10]>=50;clockDivision=nativeDivision(v[11]);spb=nativeSamplesPerBeat(bpm);
 }
 void clear()override{int m=mode;mode=-1;setMode(m);reverb.reset();transition=-1.5f;wet_=0;clockPos=0;clockFlags=GateLow;}
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;clockPos=0;clockFlags=GateLow;}
  const Mode&md=modes[mode];int n=std::min(frames,MaxBlock);
  // Delay mode when SIZE is at its maximum, with Beads' 1.5 s transition and fade.
  bool wasDelay=transition>0;float threshold=wasDelay?.98f:.99f;
  transition=std::clamp(transition+(p.size.base>threshold?1.f:-1.f)*float(n)/Rate,-1.5f,1.5f);
  delayMode=wasDelay;maxFeedbackGain=delayMode?1.5f:3.f;
  // The clock standing in for a patched SEED input.
  uint8_t clock[MaxBlock];
  if(clockSync){double period=std::max(2.0,nativeDivisionBeats(clockDivision)*spb);
   for(int i=0;i<n;i++){if(clockPos>=period)clockPos-=period*std::floor(clockPos/period);clockFlags=extractGateFlags(clockFlags,clockPos<period*.5);clock[i]=clockFlags;clockPos+=1;}}
  bool freeze[MaxBlock];std::fill(freeze,freeze+n,freezeOn);
  TriggerOut trig[MaxBlock];TapsOut taps[MaxBlock];
  if(delayMode)controller.render(rng,clockSync?clock:nullptr,freeze,density,p.time,p.shape,taps,n);
  else triggers.render(rng,clockSync?clock:nullptr,density,trig,n);
  // Core parameters, interpolated over the core samples of this block.
  int coreCount=md.ratio==1?n:(pairPhase+n)/2;int step=std::max(coreCount,1);
  float speed=semitonesToRatio(p.pitch.value(rng,&pitchWalk,step,24,false));
  float bandwidth=std::clamp(.5f*(1+p.size.attenuverter),0.f,1.f);
  Interpolator fbM(feedback_,feedbackAmount,step),spM(speed_,speed,step),bwM(bandwidth_,bandwidth,step);
  Frame wet[MaxBlock];
  for(int i=0;i<n;i++){
   Frame in{io[i*2],io[i*2+1]};
   if(md.ratio==1){core(delayMode?nullptr:&trig[i],delayMode?&taps[i]:nullptr,freeze[i],fbM.next(),spM.next(),bwM.next(),in,wet[i]);continue;}
   down[0].push(in.l);down[1].push(in.r);
   if(pairPhase==0){   // first of a pair: keep its trigger, taps and freeze
    pairTrigger=trig[i];pairTaps=taps[i];pairFreeze=freeze[i];pairPhase=1;wet[i]=pending;continue;}
   TriggerOut t=pairTrigger;
   if(!delayMode){if(t.trigger)t.fractional*=.5f;else if(trig[i].trigger){t.trigger=true;t.fractional=(trig[i].fractional+1)*.5f;}t.gate|=trig[i].gate;}
   TapsOut tp=pairTaps;for(auto&tap:tp.tap)tap.delay/=2;
   Frame d{down[0].filter(0,1),down[1].filter(0,1)},o;
   core(delayMode?nullptr:&t,delayMode?&tp:nullptr,pairFreeze||freeze[i],fbM.next(),spM.next(),bwM.next(),d,o);
   up[0].push(o.l);up[1].push(o.r);
   wet[i]={up[0].filter(0,2),up[1].filter(0,2)};pending={up[0].filter(1,2),up[1].filter(1,2)};pairPhase=0;
  }
  // Wet level (DRY/WET fully wet) with the delay/granular crossfade.
  float fade=smoothStep(std::min(1.f,std::fabs(transition)/.75f));
  Interpolator wetM(wet_,1.5f*fade,n);
  for(int i=0;i<n;i++){float g=wetM.next();wet[i].l*=g;wet[i].r*=g;}
  if(mode==3)wowFlutter.process(rng,wet,n);
  if(mode==1)for(int i=0;i<n;i++){wet[i].l=lofi[0][1].process(lofi[0][0].process(wet[i].l));wet[i].r=lofi[1][1].process(lofi[1][0].process(wet[i].r));}
  for(int i=0;i<n;i++)preReverbLimiter.process(wet[i]);
  bool caveman=mode!=0;float rt=reverbAmount*reverbAmount*reverbAmount;
  reverb.mix=reverbAmount*reverbAmount*.9f;reverb.time=caveman?.45f+.5f*rt:.3f+.6f*rt;reverb.lp=caveman?.4f:.6f;
  if(mode==1)reverb.lp=.75f;
  reverb.process(wet,n);
  for(int i=0;i<n;i++){outputLimiter.process(wet[i]);io[i*2]=nativeClean(wet[i].l);io[i*2+1]=nativeClean(wet[i].r);}
  if(frames>n)process(io+n*2,frames-n);
 }
};
}
const NativeInfo beadsInfo{FxType::Beads,"BEADS","BEADS","BD",2,0,3,50,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeBeads(){return std::make_unique<BeadsFx>();}
}
