// SPDX-License-Identifier: GPL-3.0-only
// GLITCHLETS: a native reimplementation of "glitchlets" v0.2.1 for norns by Zack Scholl
// (schollz), Copyright (c) 2020 Zack, MIT licence (https://github.com/schollz/glitchlets):
// the Lua voice/clock/probability logic over softcut and the Warb SuperCollider engine.
// There is no C++ upstream, so the behaviour is rebuilt from glitchlets.lua and
// lib/Engine_Warb.sc.
// A record head writes the mono sum of the input into a loop of LOOP length (softcut voice 1,
// overwrite, 8 kHz low-pass). Five glitchlets each own a slice on a 16-step grid of the loop
// (starts at 0, 1/4, 1/2, 3/4 and a random step; lengths in sixteenths of LENGTH). On the
// beat/16 clock, when the record head is within two ticks of a slice's end, the glitchlet
// rolls its probability once per loop; on a hit it loops its slice for one tick plus
// length x repeats with one of the eight original rate gestures (1, 1->1.5, -1->2, 2->-0.125,
// 1->2, 1->-1 slow, 1->-1 fast, 1->-0.5, slews scaled by SLEW), crossfaded at the loop points,
// panned, and fires a Warb voice: a 1-beat circular recording retriggered at 80..130 Hz
// sliding to 10..60 Hz, through a sine-swept resonant low-pass (SC RLPF), the SC Compander
// and a linen envelope. In the gated modes the dry input is muted while glitches play; in
// the solo modes only one glitchlet plays at a time (the original defaults).
// Changed or omitted: the screen, keys, encoders, OSC beat-tracker sync and pset storage are
// omitted. The per-glitchlet settings come from a seeded "quick start" (deterministic) and
// MUTATE gives every glitchlet the original's last-voice randomize-after-glitch, by chance.
// VARIETY and REVERSE weight the original uniform choice of the eight rate gestures (the
// defaults reproduce it). The slice playback is normalised to unity for a centred mono
// source; the Warb compander runs on the mono sum it is panned from. A slice that would
// end past the loop is moved inside it, crossfades are at most a quarter of the slice, and
// starts/stops have short declicks. The loop is halved until it fits 10 s of buffer; the
// Warb ring holds at most 1.5 s. Softcut's rate slew is modelled as a one-pole lag (-60 dB
// in the slew time). The dry gate reopens when no glitch plays. Transport Start realigns
// the record head to the downbeat and stops playing glitches.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const modes[]={"LAYER","SOLO","LAYER GATE","SOLO GATE"};
const NativeParam params[]={
 {"VOICES",100,NativeUnit::Range,50,100,nullptr,0,1,5,false,""},
 {"CHANCE",30,NativeUnit::Percent,15,60,nullptr,0,0,1,false,"% CHANCE"},
 {"LOOP",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(1),nativeDivisionValue(4)},
 {"LENGTH",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(4),nativeDivisionValue(10)},
 {"REPEATS",45,NativeUnit::Range,18,64,nullptr,0,1,12,false,"x"},
 {"VARIETY",88,NativeUnit::Percent,40,100},
 {"REVERSE",71,NativeUnit::Percent,20,90},
 {"SLEW",50,NativeUnit::Percent,10,90},
 {"MUTATE",20,NativeUnit::Percent,0,60},
 {"WOBBLE",20,NativeUnit::Percent,0,45},
 {"SPREAD",100,NativeUnit::Percent,30,100},
 {"MODE",nativeChoiceValue(3,4),NativeUnit::Choice,0,100,modes,4},
};
constexpr float SR=44100;
constexpr int LoopCap=44100*10,RingCap=66150,Declick=128;
const float wobbles[]={1/8.f,1/4.f,1/2.f,1,2};
const float endHz[]={10,20,30,40,50,60};
const float startHz[]={90,100,110,120,130,80};
const float sixtyDb=6.9077553f;   // ln(1000)

// One Warb synth: PlayBuf retriggered by Impulse, RLPF swept by a SinOsc, Compander, linen.
struct Warb {
 bool on=false;long t=0;float att=1,sus=1,rel=1,amp=0,f0=100,fe=40,wobble=1,phase=0,gl=1,gr=1;
 int start=0;long read=0;float n0=0,n1=0;long nCount=0;uint32_t nseed=1;
 float y1=0,y2=0,a0=0,b1=0,b2=0,env=0;
 const float clampCoef=std::exp(std::log(.1f)/(.01f*SR));
 void begin(uint32_t&seed,float total,float level,float pan){
  on=level>0;t=0;att=std::max(1.f,total/10);sus=total/2;rel=std::max(1.f,total/2);amp=level;
  wobble=wobbles[nativeRandom(seed)%5];fe=endHz[nativeRandom(seed)%6];f0=startHz[nativeRandom(seed)%6];
  start=int(nativeRandom(seed)%21);phase=1;read=0;nseed=nativeRandom(seed)|1;n0=nativeUnit(nseed)*2-1;n1=nativeUnit(nseed)*2-1;nCount=0;
  float th=(pan+1)*NativePi/4;gl=std::cos(th);gr=std::sin(th);
 }
 void stop(){on=false;y1=y2=env=0;}
 void process(const float*ring,int ringLen,float&l,float&r){
  if(!on)return;
  float sec=t/SR;
  // freq = XLine(freq,endfreq,sustain/4) with sustain fixed at 1, +-1/12 semitone of LFNoise2(3).
  float f=f0*std::pow(fe/f0,std::min(sec,.25f)/.25f);
  long period=long(SR/3);if(++nCount>=period){nCount=0;n0=n1;n1=nativeUnit(nseed)*2-1;}
  float x=float(nCount)/period,nz=n0+(n1-n0)*(.5f-.5f*std::cos(NativePi*x));
  f*=std::exp2(nz/144.f);
  phase+=f/SR;if(phase>=1){phase-=std::floor(phase);read=0;}   // Impulse retriggers PlayBuf at startPos
  float in=2*ring[(start+read)%ringLen];read++;
  if((t&3)==0){
   // RLPF(cutoff = SinOsc(wobble/sustain).range(20000,80), rq = XLine(0.2,0.9,sustain)), SC coefficients.
   float fc=10040+9960*std::sin(2*NativePi*wobble*sec),rq=.2f*std::pow(4.5f,std::min(sec,1.f));
   float pf=fc*2*NativePi/SR,D=std::tan(pf*rq*.5f),C=(1-D)/(1+D);b1=(1+C)*std::cos(pf);b2=-C;a0=(1+C-b1)*.25f;
  }
  float y0=a0*in+b1*y1+b2*y2,y=y0+2*y1+y2;y2=y1;y1=std::fabs(y0)<1e-20f?0:y0;
  // Compander(thresh 0.1, below 1, above 0.5, clamp 10 ms, relax 10 ms) on itself, times 4.
  float v=std::fabs(y);env=v+(env-v)*clampCoef;
  float g=env>.1f?std::sqrt(.1f/env):1;
  float e=t<att?t/att:t<att+sus?1:std::max(0.f,1-(t-att-sus)/rel);
  float out=4*y*g*e*amp;l+=out*gl;r+=out*gr;
  if(++t>=long(att+sus+rel))stop();
 }
};

struct Glitchlet {
 int startIdx=0,k=4;float repU=.5f,panU=0,chance=1;bool mutated=false;
 bool playing=false,loopReset=true;int fadeOut=0;
 double a=0,b=0,lo=0,hi=1,len=1;bool bOn=false;float bGain=0;int fade=1,fadePos=0;
 long remain=0;float rate=1,target=1,coef=0,gl=1,gr=1;
 Warb warb;
};

class GlitchletsFx final:public NativeFx{
 std::vector<float> loop,ring;
 Glitchlet v[5];
 int recPos=0,loopLen=88200,ringPos=0,ringLen=22050,voices=5,mode=3;long lastTick=-1;
 double spb=22050,step=22050/16.,lengthBeats=1;
 float chance=.3f,variety=.88f,reverse=.71f,slew=1,mutate=.2f,wobble=.2f,spread=1,repeats=6,dry=1;
 uint32_t epoch=0,seed=0x611c7e75;
 Biquad pre,post[2];
 bool solo()const{return mode==1||mode==3;}
 bool gated()const{return mode>=2;}
 double sliceLen(const Glitchlet&g)const{return std::max(16.0,std::min(g.k/16.0*lengthBeats*spb,double(loopLen)));}
 double sliceStart(const Glitchlet&g,double len)const{
  double s=g.startIdx/16.0*loopLen;
  if(g.mutated)s=std::min(std::max(s,step),loopLen-4*len);   // util.clamp after randomize
  s=std::max(0.0,s);if(s+len>loopLen)s=std::max(0.0,loopLen-len);
  return s;
 }
 float sample(double p)const{
  double n=loopLen;p=std::fmod(p,n);if(p<0)p+=n;int i=int(p);if(i>=loopLen)i=0;float f=float(p-i);int j=i+1>=loopLen?0:i+1;
  return loop[i]+(loop[j]-loop[i])*f;
 }
 void setPan(Glitchlet&g){float th=(g.panU*spread+1)*NativePi/4;g.gl=1.41421356f*std::cos(th);g.gr=1.41421356f*std::sin(th);}
 void halt(Glitchlet&g){if(g.playing){g.playing=false;g.fadeOut=Declick;}}
 void trigger(Glitchlet&g,double start,double len){
  int reps=std::clamp(1+int(g.repU*repeats),1,12);
  g.playing=true;g.loopReset=false;g.fadeOut=0;g.lo=start;g.hi=start+len;g.len=len;g.a=start;g.bOn=false;
  g.fade=std::max(1,int(std::min(.2*SR,len/4)));g.fadePos=0;
  g.remain=std::lround(step+len*reps);setPan(g);
  g.warb.begin(seed,float(len*reps),wobble,g.panU*spread);
  // The eight rate gestures of glitch_softcut, each from rate 1 (reset by glitch_stop).
  int pick=0;
  if(nativeUnit(seed)<variety){
   static const int fwd[]={1,4},rev[]={2,3,5,6,7};
   pick=nativeUnit(seed)<reverse?rev[nativeRandom(seed)%5]:fwd[nativeRandom(seed)%2];
  }
  float L=float(len)/SR,from=1,to=1,slewSec=0;
  switch(pick){
   case 1:to=1.5f;slewSec=L*20;break;
   case 2:from=-1;to=2;slewSec=L*10;break;
   case 3:from=2;to=-.125f;slewSec=L*20;break;
   case 4:to=2;slewSec=L*20;break;
   case 5:to=-1;slewSec=L*10;break;
   case 6:to=-1;slewSec=L;break;
   case 7:to=-.5f;slewSec=L*5;break;
   default:break;
  }
  slewSec*=slew;g.rate=from;g.target=to;
  if(slewSec*SR<1)g.rate=to,g.coef=0;else g.coef=std::exp(-sixtyDb/(slewSec*SR));
 }
 void finish(Glitchlet&g){
  halt(g);
  if(nativeUnit(seed)<mutate){   // glitch_stop's randomize
   g.panU=nativeUnit(seed)*2-1;g.repU=nativeUnit(seed);g.k=1+int(nativeRandom(seed)%16);g.startIdx=int(nativeRandom(seed)%16);g.mutated=true;
  }
 }
 void tick(){
  for(int i=0;i<voices;i++){
   auto&g=v[i];if(!g.loopReset||g.playing)continue;
   if(solo()){bool busy=false;for(int j=0;j<5;j++)if(j!=i&&v[j].playing)busy=true;if(busy)continue;}
   double len=sliceLen(g),start=sliceStart(g,len),d=start+len-recPos;
   if(!(d>0&&d<2*step))continue;
   if(nativeUnit(seed)*100<std::min(100.f,chance*100*g.chance))trigger(g,start,len);
  }
 }
 void voice(Glitchlet&g,float&l,float&r){
  if(!g.playing&&!g.fadeOut)return;
  g.rate=g.target+(g.rate-g.target)*g.coef;
  float gA=nativeFade(g.fadePos,g.fade),m=gA*sample(g.a);
  if(g.bOn){m+=g.bGain*(1-gA)*sample(g.b);g.b+=g.rate;}
  if(g.fadePos<g.fade)g.fadePos++;else g.bOn=false;
  g.a+=g.rate;
  if((g.rate>0&&g.a>=g.hi)||(g.rate<0&&g.a<g.lo)){   // loop point: crossfade to the other end
   g.b=g.a;g.bOn=true;g.bGain=gA;g.a+=g.rate>0?-g.len:g.len;g.fadePos=0;
  }
  if(!g.playing){m*=float(g.fadeOut)/Declick;g.fadeOut--;}
  l+=m*g.gl;r+=m*g.gr;
  if(g.playing&&--g.remain<=0)finish(g);
 }
public:
 GlitchletsFx():loop(LoopCap,0.f),ring(RingCap,0.f){
  // A seeded "quick start": four slices at the quarters, one on a random step.
  for(int i=0;i<5;i++){auto&g=v[i];
   g.startIdx=i<4?i*4:int(nativeRandom(seed)%16);g.k=3+int(nativeRandom(seed)%6);
   g.chance=(21+int(nativeRandom(seed)%20))/30.f;g.repU=.5f+.5f*nativeUnit(seed);g.panU=nativeUnit(seed)*2-1;}
  pre.set(0,8000,.5f);for(auto&p:post)p.set(0,8000,.5f);
 }
 void configure(const uint8_t*p,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);step=spb/16;
  voices=std::clamp(int(std::lround(1+4*p[0]*.01f)),1,5);chance=p[1]*.01f;
  double len=nativeDivisionBeats(nativeDivision(p[2]))*spb;while(len>LoopCap)len*=.5;loopLen=std::max(64,int(len));
  if(recPos>=loopLen){recPos=0;lastTick=-1;for(auto&g:v)g.loopReset=true;}
  lengthBeats=nativeDivisionBeats(nativeDivision(p[3]));
  repeats=float(std::lround(1+11*p[4]*.01f));variety=p[5]*.01f;reverse=p[6]*.01f;slew=2*p[7]*.01f;
  mutate=p[8]*.01f;wobble=p[9]*.01f;spread=p[10]*.01f;mode=nativeChoice(p[11],4);
  ringLen=std::clamp(int(std::lround(spb)),64,RingCap);if(ringPos>=ringLen)ringPos=0;
 }
 void clear()override{
  std::fill(loop.begin(),loop.end(),0.f);std::fill(ring.begin(),ring.end(),0.f);
  recPos=ringPos=0;lastTick=-1;dry=1;pre.clear();for(auto&p:post)p.clear();
  for(auto&g:v){g.playing=false;g.fadeOut=0;g.loopReset=true;g.warb.stop();}
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();
  if(e!=epoch){epoch=e;recPos=0;lastTick=-1;for(auto&g:v){halt(g);g.loopReset=true;}}   // downbeat
  for(int n=0;n<frames;n++){
   float L=io[n*2],R=io[n*2+1],mono=.5f*(L+R);
   long tk=long(recPos/step);if(tk!=lastTick){lastTick=tk;tick();}
   loop[recPos]=pre.process(mono);ring[ringPos]=mono;
   float gl=0,gr=0,wl=0,wr=0;bool any=false;
   for(auto&g:v){voice(g,gl,gr);g.warb.process(ring.data(),ringLen,wl,wr);any|=g.playing;}
   float target=gated()&&any?0.f:1.f;dry+=std::clamp(target-dry,-1.f/220,1.f/220);
   io[n*2]=nativeClean(L*dry+post[0].process(gl)+wl);io[n*2+1]=nativeClean(R*dry+post[1].process(gr)+wr);
   if(++recPos>=loopLen){recPos=0;lastTick=-1;for(auto&g:v)g.loopReset=true;}
   if(++ringPos>=ringLen)ringPos=0;
  }
 }
};
}
const NativeInfo glitchletsInfo{FxType::Glitchlets,"GLITCHLETS","GLITCH","GL",6,1,2,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeGlitchlets(){return std::make_unique<GlitchletsFx>();}
}
