// SPDX-License-Identifier: GPL-3.0-only
// SOFTCUT: a micro-looper on a one-to-one recreation of monome softcut-lib's voice engine,
// (C) 2017-2024 Ezra Buchla and monome contributors, GPL-3.0
// (https://github.com/monome/softcut-lib, commit 14241f2). Ported here as plain C++:
// Voice (pre/post state-variable filters, log ramps for rate, pre and rec levels),
// ReadWriteHead (two crossfaded subheads, loop points, queued cuts, record-once),
// SubHead (hermite read, resampled write with soft clip, pre/rec fade curves),
// FadeCurves, Resampler, SoftClip and Svf.
// Controller (new, softcut has none): per channel a writer voice records the live input
// around a 5.9 s ring; a player voice shares that buffer. Every TRIGGER, with a seeded
// CHANCE, the player captures the last LOOP of audio and loops it for HOLD at a rate
// drawn from RATE (REVERSE may flip it), gliding by RATE SLEW; TAPE STOP may slew the
// rate to zero over the second half of the hold. PRE is the loop's overdub feedback
// (at most 0.9, so it dies away) and OVERDUB the live input written back into it. FADE
// is softcut's crossfade time and the live/loop crossfade; FILTER drives the post SVF
// (below centre lowpass, above highpass). The pattern realigns on transport Start.
// Changes: the "Raised" rec-fade curve starts its sine at pi so it rises 0..1 (the
// reference's start at 0 gives a -sin curve that inverts every recording); the playback
// is scaled by 1/1.2 to undo the soft clipper's gain; the writer's pre filter is set to
// dry, as norns scripts commonly do. Omitted: phase quantization/polls, test buffers,
// the Softcut multi-voice wrapper and its OSC/host control.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
namespace sc {
using sample_t=float;using phase_t=double;using rate_t=double;
inline float fsign(float x){return x>0.f?1.f:-1.f;}
inline float tau2pole(float t,float sr,float ref=-6.9f){return std::exp(ref/(t*sr));}
inline float smooth1pole(float x,float x0,float b){return x+(x0-x)*b;}
template<typename T> inline T hermite(T x,T y0,T y1,T y2,T y3){
 return (((T(.5)*(y3-y0)+T(1.5)*(y1-y2))*x+(y0-T(2.5)*y1+T(2)*y2-T(.5)*y3))*x+T(.5)*(y2-y0))*x+y1;
}
template<typename T,int N> inline T tabLinear(const T*buf,float x){
 const float fi=x*(N-2);auto i=static_cast<unsigned>(fi);const float a=buf[i],b=buf[i+1],c=fi-float(i);return a+c*(b-a);
}
// Logarithmic interpolator (1-pole lowpass); `time` converges within -60 dB.
class LogRamp{
 float sampleRate,time,b=1,x0=0,y0=0;
public:
 explicit LogRamp(float sr=48000,float t=.05f):sampleRate(sr),time(t){setTime(t);}
 void setSampleRate(float sr){sampleRate=sr;setTime(time);}
 void setTime(float t){time=t;b=tau2pole(t,sampleRate);}
 void setTarget(float x){x0=x;}
 float update(){y0=smooth1pole(x0,y0,b);return y0;}
 float value()const{return y0;}
 void reset(float x){x0=x;y0=x;}
};
// Two-stage quadratic soft clipper.
class SoftClip{
 float t,g,a=0,b=1;
 void calcCoeffs(){float t_1=t-1.f;if(t_1<0.f){a=g/(2.f*t_1);b=g*t-(a*t_1*t_1);}else{a=0.f;b=1.f;}}
public:
 SoftClip(float t_=.68f,float g_=1.2f):t(t_),g(g_){calcCoeffs();}
 float processSample(float x){float ax=std::fabs(x);const float sx=fsign(x);if(ax>1.f)ax=1.f;
  if(ax<t)return x*g;const float q=ax-1.f;return sx*((a*q*q)+b);}
};
// Ultra-simple resampler: one input frame in, 0..n output frames.
class Resampler{
 enum{IN_BUF_FRAMES=4,IN_BUF_MASK=3,OUT_BUF_FRAMES=64};
 rate_t rate_=1;phase_t phi_=1,phase_=0;sample_t inBuf_[IN_BUF_FRAMES]{};unsigned inBufIdx_=0;sample_t outBuf_[OUT_BUF_FRAMES]{};
 void pushInput(sample_t x){inBufIdx_=(inBufIdx_+1)&IN_BUF_MASK;inBuf_[inBufIdx_]=x;}
 sample_t interpolate(phase_t f){unsigned i0=(inBufIdx_+1)&IN_BUF_MASK,i1=(inBufIdx_+2)&IN_BUF_MASK,i2=(inBufIdx_+3)&IN_BUF_MASK,i3=inBufIdx_;
  return sample_t(hermite<phase_t>(f,inBuf_[i0],inBuf_[i1],inBuf_[i2],inBuf_[i3]));}
 int writeUp(){
  phase_t p=phase_+rate_;auto nf=static_cast<unsigned>(p);if(nf>OUT_BUF_FRAMES)nf=OUT_BUF_FRAMES;
  phase_t f=(1.0-phase_)*phi_;unsigned i=0;outBuf_[i]=interpolate(f);
  while(i<nf){f+=phi_;outBuf_[i]=interpolate(f);i++;}
  phase_=p-phase_t(static_cast<unsigned>(p));return int(nf);
 }
 int writeDown(){
  phase_t p=phase_+rate_;auto nf=static_cast<unsigned>(p);
  if(nf>0){phase_t f=(1.0-phase_)*phi_;outBuf_[0]=interpolate(f);phase_=p-phase_t(nf);}else phase_=p;
  return int(nf);
 }
public:
 int processFrame(sample_t x){pushInput(x);return rate_>1.0?writeUp():writeDown();}
 void setRate(rate_t r){rate_=r;phi_=1.0/r;}
 void setPhase(phase_t p){phase_=p;}
 const sample_t*output()const{return outBuf_;}
};
// Crossfade curves for pre (erase) and rec levels during subhead fades.
class FadeCurves{
public:
 enum Shape{Linear=0,Sine=1,Raised=2};
 void init(){setPreShape(Linear);setRecShape(Raised);setMinPreWindowFrames(0);setMinRecDelayFrames(0);setPreWindowRatio(1.f/8);setRecDelayRatio(1.f/(8*16));}
 void setRecDelayRatio(float x){recDelayRatio=x;calcRecFade();}
 void setPreWindowRatio(float x){preWindowRatio=x;calcPreFade();}
 void setMinRecDelayFrames(unsigned x){recDelayMinFrames=x;calcRecFade();}
 void setMinPreWindowFrames(unsigned x){preWindowMinFrames=x;calcPreFade();}
 void setPreShape(Shape x){preShape=x;calcPreFade();}
 void setRecShape(Shape x){recShape=x;calcRecFade();}
 float getRecFadeValue(float x)const{return tabLinear<float,fadeBufSize>(recFadeBuf,x);}
 float getPreFadeValue(float x)const{return tabLinear<float,fadeBufSize>(preFadeBuf,x);}
private:
 static constexpr unsigned fadeBufSize=1001;static constexpr float fpi=3.1415926535898f;
 float recDelayRatio=1.f/128,preWindowRatio=1.f/8;unsigned recDelayMinFrames=0,preWindowMinFrames=0;
 float recFadeBuf[fadeBufSize]{},preFadeBuf[fadeBufSize]{};Shape recShape=Raised,preShape=Linear;
 void calcRecFade(){
  float buf[fadeBufSize];unsigned n=fadeBufSize-1;
  unsigned ndr=std::max(recDelayMinFrames,static_cast<unsigned>(recDelayRatio*fadeBufSize));unsigned nr=n-ndr,i=0;
  if(recShape==Sine){const float phi=fpi/nr;float x=fpi,y=0.f;while(i<ndr)buf[i++]=y;while(i<n){y=std::cos(x)*.5f+.5f;buf[i++]=y;x+=phi;}buf[n]=1.f;}
  else if(recShape==Linear){const float phi=1.f/nr;float x=0.f;while(i<ndr)buf[i++]=x;while(i<n){buf[i++]=x;x+=phi;}buf[n]=1.f;}
  else if(recShape==Raised){const float phi=fpi/(nr*2);float x=fpi,y=0.f;   // x starts at pi: -sin rises 0..1
   while(i<ndr)buf[i++]=y;while(i<n){y=std::sin(x)*-1.f;buf[i++]=y;x+=phi;}buf[n]=1.f;}
  else return;
  std::copy(buf,buf+fadeBufSize,recFadeBuf);
 }
 void calcPreFade(){
  float buf[fadeBufSize];unsigned nwp=std::max(preWindowMinFrames,static_cast<unsigned>(preWindowRatio*fadeBufSize));unsigned i=0;float x=0.f;
  if(preShape==Sine){const float phi=fpi/float(nwp);while(i<nwp){buf[i++]=std::cos(x)*.5f+.5f;x+=phi;}}
  else if(preShape==Linear){const float phi=1.f/float(nwp);while(i<nwp){buf[i++]=1.f-x;x+=phi;}}
  else if(recShape==Raised){const float phi=fpi/(float(nwp*2));while(i<nwp){buf[i++]=std::cos(x);x+=phi;}}
  else return;
  while(i<fadeBufSize)buf[i++]=0.f;
  std::copy(buf,buf+fadeBufSize,preFadeBuf);
 }
};
// State variable filter (after Hal Chamberlin, Andy Simper).
class Svf{
 static constexpr float MAX_NORM_FC=.4f;
 float lpMix=0,hpMix=0,bpMix=0,brMix=0,minFc=10,maxFc=19200,pi_sr=float(M_PI/48000),sr=48000,fc=1000,rq=1;
 float g=0,g1=0,g2=0,g3=0,g4=0,v0=0,v1=0,v2=0,v0z=0,v1z=0,v2z=0,v3=0,lp=0,hp=0,bp=0,br=0;
 void calcWarp(){g=float(std::tan(fc*pi_sr));}
 void calcCoeffs(){g1=g/(1.f+g*(g+rq));g2=2.f*(g+rq)*g1;g3=g*g1;g4=2.f*g1;}
 void update(float in){v0=in;v1z=v1;v2z=v2;v3=v0+v0z-2.f*v2z;v1+=g1*v3-g2*v1z;v2+=g3*v3+g4*v1z;v0z=v0;lp=v2;bp=v1;hp=v0-rq*v1-v2;br=v0-rq*v1;}
public:
 float getNextSample(float x){update(x);return lp*lpMix+hp*hpMix+bp*bpMix+br*brMix;}
 void setSampleRate(float s){sr=s;pi_sr=float(M_PI/sr);minFc=10.f;maxFc=sr*MAX_NORM_FC;calcWarp();calcCoeffs();}
 void setFc(float f){fc=f>maxFc?maxFc:f;fc=fc<minFc?minFc:fc;calcWarp();calcCoeffs();}
 void setRq(float r){rq=r;calcCoeffs();}
 void setLpMix(float m){lpMix=m;}void setHpMix(float m){hpMix=m;}void setBpMix(float m){bpMix=m;}void setBrMix(float m){brMix=m;}
 void reset(){v0z=0;v1=0;v2=0;}
};
enum State{Playing=0,Stopped=1,FadeIn=2,FadeOut=3};
enum Action{None,Stop,LoopPos,LoopNeg};
// One half of a crossfaded read/write head.
class SubHead{
 friend class ReadWriteHead;
 Resampler resamp_;SoftClip clip_;FadeCurves*fadeCurves=nullptr;
 sample_t*buf_=nullptr;int wrIdx_=0;int bufFrames_=0;unsigned bufMask_=0;
 State state_=Stopped;rate_t rate_=1;int inc_dir_=1;phase_t phase_=0;float fade_=0,trig_=0;bool active_=false;int recOffset_=-8;
 float preFade_=1,recFade_=0;
 unsigned wrapBufIndex(int x)const{x+=bufFrames_;return unsigned(x)&bufMask_;}
 void init(FadeCurves*fc){fadeCurves=fc;phase_=0;fade_=0;trig_=0;state_=Stopped;resamp_.setPhase(0);inc_dir_=1;recOffset_=-8;}
 Action updatePhase(phase_t start,phase_t end,bool loop){
  Action res=None;trig_=0.f;phase_t p;
  switch(state_){
   case FadeIn:case FadeOut:case Playing:
    p=phase_+rate_;
    if(active_){
     if(p>end||p<start){
      if(loop){trig_=1.f;res=rate_>0.f?LoopPos:LoopNeg;}
      else{state_=FadeOut;res=Stop;}
     }
    }
    phase_=p;break;
   case Stopped:default:;
  }
  return res;
 }
 void updateFade(float inc){
  switch(state_){
   case FadeIn:fade_+=inc;if(fade_>1.f){fade_=1.f;state_=Playing;}break;
   case FadeOut:fade_-=inc;if(fade_<0.f){fade_=0.f;state_=Stopped;}break;
   default:;
  }
 }
 void poke(float in,float pre,float rec){
  int nframes=resamp_.processFrame(in);
  if(state_==Stopped)return;
  preFade_=pre+(1.f-pre)*fadeCurves->getPreFadeValue(fade_);
  recFade_=rec*fadeCurves->getRecFadeValue(fade_);
  const sample_t*src=resamp_.output();
  for(int i=0;i<nframes;++i){
   sample_t y=clip_.processSample(src[i]);
   buf_[wrIdx_]*=preFade_;buf_[wrIdx_]+=y*recFade_;
   wrIdx_=int(wrapBufIndex(wrIdx_+inc_dir_));
  }
 }
 float peek()const{
  int phase1=static_cast<int>(phase_),phase0=phase1-1,phase2=phase1+1,phase3=phase1+2;
  float y0=buf_[wrapBufIndex(phase0)],y1=buf_[wrapBufIndex(phase1)],y3=buf_[wrapBufIndex(phase3)],y2=buf_[wrapBufIndex(phase2)];
  auto x=static_cast<float>(phase_-float(phase1));
  return hermite<float>(x,y0,y1,y2,y3);
 }
 void setState(State s){state_=s;if(s==Stopped)fade_=0.f;if(s==Playing)fade_=1.f;}
 void setPhase(phase_t p){phase_=p;wrIdx_=int(wrapBufIndex(static_cast<int>(phase_)+(inc_dir_*recOffset_)));}
 void setBuffer(sample_t*b,unsigned frames){buf_=b;bufFrames_=int(frames);bufMask_=frames-1;}   // frames: a power of two
 void setRate(rate_t r){rate_=r;inc_dir_=int(fsign(float(r)));resamp_.setRate(std::fabs(r));}
 void setRecOffsetSamples(int d){recOffset_=d;}
};
class ReadWriteHead{
 SubHead head[2];float sr=44100;phase_t start=0,end=0,queuedCrossfade=0;bool queuedCrossfadeFlag=false;
 float fadeTime=.1f,fadeInc=0;int active=0;bool loopFlag=true;float pre=0,rec=0;
 bool recOnceFlag=false,recOnceDone=false;int recOnceHead=-1;rate_t rate=1;
 void takeAction(Action a){if(a==LoopPos)enqueueCrossfade(start);else if(a==LoopNeg)enqueueCrossfade(end);}
 void enqueueCrossfade(phase_t pos){queuedCrossfade=pos;queuedCrossfadeFlag=true;}
 void dequeueCrossfade(){State s=head[active].state_;if(!(s==FadeIn||s==FadeOut)){if(queuedCrossfadeFlag)cutToPhase(queuedCrossfade);queuedCrossfadeFlag=false;}}
 void cutToPhase(phase_t pos){
  State s=head[active].state_;if(s==FadeIn||s==FadeOut)return;   // never while fading
  int newActive=active^1;if(s!=Stopped)head[active].setState(FadeOut);
  if(recOnceHead==newActive){recOnceHead=-1;recOnceDone=true;}
  if(recOnceFlag){recOnceFlag=false;recOnceHead=newActive;}
  head[newActive].setState(FadeIn);head[newActive].setPhase(pos);
  head[active].active_=false;head[newActive].active_=true;active=newActive;
 }
 static sample_t mixFade(sample_t x,sample_t y,float a,float b){return x*std::sin(a*float(M_PI_2))+y*std::sin(b*float(M_PI_2));}
 void calcFadeInc(){fadeInc=float(std::fabs(rate))/std::max(1.f,fadeTime*sr);fadeInc=std::max(0.f,std::min(fadeInc,1.f));}
 void write(sample_t in){
  if(recOnceFlag||recOnceDone||recOnceHead>-1){if(recOnceHead>-1)head[recOnceHead].poke(in,pre,rec);}
  else{head[0].poke(in,pre,rec);head[1].poke(in,pre,rec);}
 }
 void advance(){
  takeAction(head[0].updatePhase(start,end,loopFlag));takeAction(head[1].updatePhase(start,end,loopFlag));
  head[0].updateFade(fadeInc);head[1].updateFade(fadeInc);dequeueCrossfade();
 }
public:
 void init(FadeCurves*fc){start=0;end=0;active=0;rate=1;setFadeTime(.1f);queuedCrossfade=0;queuedCrossfadeFlag=false;head[0].init(fc);head[1].init(fc);setRecOnceFlag(false);}
 void processSample(sample_t in,sample_t*out){*out=mixFade(head[0].peek(),head[1].peek(),head[0].fade_,head[1].fade_);write(in);advance();}
 void processSampleNoRead(sample_t in,sample_t*){write(in);advance();}
 void processSampleNoWrite(sample_t,sample_t*out){*out=mixFade(head[0].peek(),head[1].peek(),head[0].fade_,head[1].fade_);advance();}
 void setSampleRate(float s){sr=s;}
 void setBuffer(sample_t*b,unsigned size){head[0].setBuffer(b,size);head[1].setBuffer(b,size);}
 void setRate(rate_t x){rate=x;calcFadeInc();head[0].setRate(x);head[1].setRate(x);}
 void setLoopStartSeconds(float x){start=x*sr;queuedCrossfadeFlag=false;}
 void setLoopEndSeconds(float x){end=x*sr;queuedCrossfadeFlag=false;}
 void setFadeTime(float secs){fadeTime=secs;calcFadeInc();}
 void setLoopFlag(bool v){loopFlag=v;}
 void setRecOnceFlag(bool v){recOnceFlag=v;recOnceDone=false;recOnceHead=-1;}
 bool getRecOnceDone()const{return recOnceDone;}
 bool getRecOnceActive()const{return recOnceDone||recOnceFlag||recOnceHead>-1;}
 void setRec(float x){rec=x;}void setPre(float x){pre=x;}
 void cutToPos(float seconds){auto s=head[active].state_;if(s==FadeIn||s==FadeOut)enqueueCrossfade(seconds*sr);else cutToPhase(seconds*sr);}
 void stop(){head[0].setState(Stopped);head[1].setState(Stopped);}
 void run(){head[active].setState(Playing);}
 void setRecOffsetSamples(int d){head[0].setRecOffsetSamples(d);head[1].setRecOffsetSamples(d);}
 phase_t getActivePhase()const{return head[active].phase_;}
 rate_t getRate()const{return rate;}
};
class Voice{
 float sampleRate=44100;FadeCurves fadeCurves;ReadWriteHead sch;Svf svfPre,svfPost;
 LogRamp rateRamp{48000,.1f},preRamp{48000,.1f},recRamp{48000,.1f};
 float svfPreFcBase=16000,svfPreFcMod=1,svfPreDryLevel=1,svfPostDryLevel=1;bool playFlag=false,recFlag=false;
 void updatePreSvfFc(){float fcMod=std::min(svfPreFcBase,svfPreFcBase*std::fabs(float(sch.getRate())));fcMod=svfPreFcBase+svfPreFcMod*(fcMod-svfPreFcBase);svfPre.setFc(fcMod);}
public:
 Voice(){setSampleRate(44100);reset();}
 void reset(){
  fadeCurves.init();
  svfPre.reset();svfPre.setLpMix(1);svfPre.setHpMix(0);svfPre.setBpMix(0);svfPre.setBrMix(0);svfPre.setRq(4);svfPre.setFc(svfPreFcBase);svfPreFcMod=1;svfPreDryLevel=0;
  svfPost.reset();svfPost.setLpMix(0);svfPost.setHpMix(0);svfPost.setBpMix(0);svfPost.setBrMix(0);svfPost.setRq(4);svfPost.setFc(12000);svfPostDryLevel=1;
  rateRamp.reset(1);recRamp.reset(0);preRamp.reset(0);
  setFadeTime(.01f);setRecPreSlewTime(.001f);setRateSlewTime(.001f);sch.setRecOffsetSamples(-8);
  recFlag=false;playFlag=false;sch.init(&fadeCurves);
 }
 float processSample(float in){
  float x=svfPre.getNextSample(in)+in*svfPreDryLevel,y=0;
  sch.setRate(rateRamp.update());sch.setPre(preRamp.update());sch.setRec(recRamp.update());
  if(playFlag){if(recFlag)sch.processSample(x,&y);else sch.processSampleNoWrite(x,&y);}
  else if(recFlag)sch.processSampleNoRead(x,&y);
  float out=svfPost.getNextSample(y)+y*svfPostDryLevel;
  if(recFlag&&sch.getRecOnceDone()){recFlag=false;sch.setRecOnceFlag(false);}
  return out;
 }
 void setSampleRate(float hz){sampleRate=hz;rateRamp.setSampleRate(hz);preRamp.setSampleRate(hz);recRamp.setSampleRate(hz);sch.setSampleRate(hz);svfPre.setSampleRate(hz);svfPost.setSampleRate(hz);}
 void setBuffer(float*b,unsigned n){sch.setBuffer(b,n);}
 void setRate(float r){rateRamp.setTarget(r);updatePreSvfFc();}
 void resetRate(float r){rateRamp.reset(r);}   // controller: start a glide from r
 float currentRate()const{return rateRamp.value();}
 void setLoopStart(float s){sch.setLoopStartSeconds(s);}
 void setLoopEnd(float s){sch.setLoopEndSeconds(s);}
 void setLoopFlag(bool v){sch.setLoopFlag(v);}
 void setFadeTime(float s){sch.setFadeTime(s);}
 void cutToPos(float s){sch.cutToPos(s);}
 void setRecLevel(float a){recRamp.setTarget(a);}
 void setPreLevel(float a){preRamp.setTarget(a);}
 void setRecFlag(bool v){
  if(recFlag){if(!(v||playFlag))sch.stop();}else if(v&&!playFlag)sch.run();
  recFlag=v;if(!v&&sch.getRecOnceActive())sch.setRecOnceFlag(false);
 }
 void setPlayFlag(bool v){if(playFlag){if(!(v||recFlag))sch.stop();}else if(v&&!recFlag)sch.run();playFlag=v;}
 void setPreFilterDry(float x){svfPreDryLevel=x;}
 void setPreFilterLp(float x){svfPre.setLpMix(x);}
 void setPostFilterFc(float x){svfPost.setFc(x);}
 void setPostFilterRq(float x){svfPost.setRq(x);}
 void setPostFilterLp(float x){svfPost.setLpMix(x);}
 void setPostFilterHp(float x){svfPost.setHpMix(x);}
 void setPostFilterDry(float x){svfPostDryLevel=x;}
 void setRecPreSlewTime(float d){recRamp.setTime(d);preRamp.setTime(d);}
 void setRateSlewTime(float d){rateRamp.setTime(d);}
 double getActivePhase()const{return sch.getActivePhase();}
 void stop(){sch.stop();}
};
}

const char*const rateModes[]={"1","2","1/2","-1","-2","-1/2","OCTAVES","OCT +-"};
const NativeParam params[]={
 {"TRIGGER",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(2),nativeDivisionValue(7)},
 {"CHANCE",60,NativeUnit::Percent,30,90,nullptr,0,0,1,false,"% CHANCE"},
 {"LOOP",nativeDivisionValue(14),NativeUnit::Division,nativeDivisionValue(8),nativeDivisionValue(15)},
 {"HOLD",nativeDivisionValue(10),NativeUnit::Division,nativeDivisionValue(5),nativeDivisionValue(12)},
 {"RATE",nativeChoiceValue(6,8),NativeUnit::Choice,0,100,rateModes,8},
 {"REVERSE",25,NativeUnit::Percent,0,50,nullptr,0,0,1,false,"% CHANCE"},
 {"RATE SLEW",54,NativeUnit::Range,0,80,nullptr,0,1,2000,true," ms"},
 {"TAPE STOP",15,NativeUnit::Percent,0,40,nullptr,0,0,1,false,"% CHANCE"},
 {"PRE",60,NativeUnit::Percent,20,90},
 {"OVERDUB",0,NativeUnit::Percent,0,40},
 {"FADE",39,NativeUnit::Range,20,60,nullptr,0,1,200,true," ms"},
 {"FILTER",50,NativeUnit::Bipolar,30,70},
};
constexpr int BufferFrames=1<<18;   // per channel, a power of two (5.9 s, 1 MB)
constexpr float Sr=44100;
class SoftcutFx final:public NativeFx{
 std::vector<float> buffer[2];sc::Voice writer[2],player[2];
 double spb=22050,beats=0;long lastTrigger=-1;uint32_t epoch=0,seed=0x0c0ffee5;
 int trigger=7,loopDiv=14,holdDiv=10,rateMode=6;float chance=.6f,reverse=.25f,slew=.06f,tapeStop=.15f,pre=.54f,overdub=0,fade=.008f;
 long recorded=0,holdLeft=0,holdTotal=0;bool holding=false,stopping=false,playerRunning=false;
 float wet=0,wetStep=1;
 float drawRate(){
  static const float oct[3]={.5f,1,2};float r;
  switch(rateMode){case 0:r=1;break;case 1:r=2;break;case 2:r=.5f;break;case 3:r=-1;break;case 4:r=-2;break;case 5:r=-.5f;break;
   case 6:r=oct[nativeRandom(seed)%3];break;default:r=oct[nativeRandom(seed)%3]*(nativeRandom(seed)&1?1:-1);break;}
  if(nativeUnit(seed)<reverse)r=-r;
  return r;
 }
 void capture(){
  long L=std::clamp<long>(std::lround(nativeDivisionBeats(loopDiv)*spb),64,BufferFrames/2-64);
  if(recorded<L+64)return;
  long H=std::clamp<long>(std::lround(nativeDivisionBeats(holdDiv)*spb),L,BufferFrames-L-8192);
  float rate=drawRate();bool stopTape=nativeUnit(seed)<tapeStop;
  double w=writer[0].getActivePhase();double end=w-16,start=end-L;   // behind the writer's rec offset
  float fadeSec=std::clamp(fade,.0005f,float(L)/(3*Sr));
  for(int c=0;c<2;c++){
   auto&p=player[c];
   if(!holding)p.resetRate(rate>0?1.f:-1.f);   // glide in from tape speed
   p.setRateSlewTime(slew);p.setRate(rate);p.setFadeTime(fadeSec);
   p.setLoopStart(float(start/Sr));p.setLoopEnd(float(end/Sr));
   p.setPreLevel(pre);p.setRecLevel(overdub*std::max(.2f,1-pre));
   p.cutToPos(float((rate>0?start:end)/Sr));
  }
  holding=true;stopping=stopTape;holdLeft=holdTotal=H;playerRunning=true;
  wetStep=1.f/std::max(1.f,fadeSec*Sr);
 }
public:
 SoftcutFx(){
  for(int c=0;c<2;c++){
   buffer[c].assign(BufferFrames,0.f);
   auto&w=writer[c];w.setBuffer(buffer[c].data(),BufferFrames);w.setPreFilterDry(1);w.setPreFilterLp(0);
   w.setLoopStart(0);w.setLoopEnd(BufferFrames/Sr);w.setLoopFlag(true);w.setFadeTime(.001f);w.setRate(1);
   w.setPreLevel(0);w.setRecLevel(1);w.setRecFlag(true);w.cutToPos(0);
   auto&p=player[c];p.setBuffer(buffer[c].data(),BufferFrames);p.setLoopFlag(true);p.setPreLevel(1);p.setRecLevel(0);
   p.setPlayFlag(true);p.setRecFlag(true);p.stop();
  }
 }
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);trigger=nativeDivision(v[0]);chance=v[1]*.01f;loopDiv=nativeDivision(v[2]);holdDiv=nativeDivision(v[3]);
  rateMode=nativeChoice(v[4],8);reverse=v[5]*.01f;slew=.001f*std::pow(2000.f,v[6]*.01f);tapeStop=v[7]*.01f;
  pre=.9f*v[8]*.01f;overdub=v[9]*.01f;fade=.001f*std::pow(200.f,v[10]*.01f);
  float f=(int(v[11])*2-100)*.01f;
  for(auto&p:player){
   if(std::fabs(f)<.02f){p.setPostFilterDry(1);p.setPostFilterLp(0);p.setPostFilterHp(0);}
   else if(f<0){p.setPostFilterDry(0);p.setPostFilterLp(1);p.setPostFilterHp(0);p.setPostFilterFc(18000*std::pow(.012f,-f));}
   else{p.setPostFilterDry(0);p.setPostFilterLp(0);p.setPostFilterHp(1);p.setPostFilterFc(20*std::pow(200.f,f));}
   p.setPostFilterRq(1.f);
   if(holding){p.setPreLevel(pre);p.setRecLevel(overdub*std::max(.2f,1-pre));}
  }
 }
 void clear()override{
  for(int c=0;c<2;c++){std::fill(buffer[c].begin(),buffer[c].end(),0.f);player[c].stop();}
  recorded=0;holding=false;playerRunning=false;wet=0;holdLeft=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;beats=0;lastTrigger=-1;holdLeft=std::min(holdLeft,1L);}   // downbeat
  const double trigBeats=nativeDivisionBeats(trigger),step=1/spb;
  for(int i=0;i<frames;i++){
   long t=long(std::floor(beats/trigBeats+1e-9));
   if(t!=lastTrigger){lastTrigger=t;if(nativeUnit(seed)<chance)capture();}
   beats+=step;
   if(holding){
    if(stopping&&holdLeft==holdTotal/2){for(auto&p:player){p.setRateSlewTime(std::max(slew,.35f*holdLeft/Sr));p.setRate(0);}}
    if(--holdLeft<=0){holding=false;for(auto&p:player){p.setPreLevel(1);p.setRecLevel(0);}}
   }
   wet=holding?std::min(1.f,wet+wetStep):std::max(0.f,wet-wetStep);
   if(!holding&&wet<=0&&playerRunning){for(auto&p:player)p.stop();playerRunning=false;}
   float gw=std::sin(wet*NativePi*.5f),gd=std::cos(wet*NativePi*.5f);
   for(int c=0;c<2;c++){
    float in=io[i*2+c];writer[c].processSample(in);
    float y=player[c].processSample(in)*(1/1.2f);
    io[i*2+c]=nativeClean(in*gd+y*gw);
   }
   if(recorded<BufferFrames)recorded++;
  }
 }
};
}
const NativeInfo softcutInfo{FxType::Softcut,"SOFTCUT","SOFTCUT","SF",6,1,4,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeSoftcut(){return std::make_unique<SoftcutFx>();}
}
