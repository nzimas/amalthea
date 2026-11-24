// SPDX-License-Identifier: GPL-3.0-only
// AMALGAM: a one-to-one recreation of Valley Audio Amalgam (C) 2018-2019 Dale Johnson,
// ValleyRackFree, GPL-3.0-or-later (https://github.com/ValleyAudio/ValleyRackFree,
// src/Amalgam; the source files also carry Dale Johnson's BSD-style notice).
// Two signals, X (the insert's input) and Y (an internal modulator), are each scaled
// as Rack voltages, driven through Valley's piecewise soft clipper (X/Y DRIVE) and
// cross-mangled by one of 17 modes: three ring modulators (plain/threshold, folded,
// folded with XOR blend), a diode ring modulator (Valley's VecDiode curve and makeup
// gain), min/max, two sign switches, crossfade, a random flip-flop, alpha PWM, integer
// bit AND/XOR/interleave/glitch, and float-bit AND/interleave/glitch. PARAM B also sets
// the sample clock of the bit modes, exactly as in the original. The Z output goes
// through the 1 Hz DC blocker (DC COUPLE bypasses it) and the 0.66 soft clip.
// Changes for an insert: the SSE lanes are plain scalar code (the original computed
// each channel twice in four lanes; only one copy per channel is kept). Y has no
// jack, so SOURCE picks it: a sine, square or sample-and-hold noise oscillator at FREQ
// (or at a tempo DIVISION with SYNC on, realigned on transport Start), the input
// delayed by one period, the other stereo channel, or the rectified input. The CV
// inputs, the AND/XOR and pulse outputs and the panel are omitted; the random seeds
// are fixed instead of time-seeded. Added: a TONE low-pass after Z, and a gate that
// follows the input level so the internal oscillator never sounds over silence.
// Audio is +-1 here, treated as +-5 V on the way in and scaled back on the way out.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace amalthea {
namespace {
const char*const modes[]={"RINGMOD1","RINGMOD2","RINGMOD3","DIODE-RM","MINIMAXI","SIGN-1","SIGN-2","X-FADE",
 "FLIPFLOP","ALPHAPWM","BITAND","BITXOR","BITINTLV","BITGLTCH","FL32-AND","FL32INTV","FL32HACK"};
const char*const sources[]={"SINE","SQUARE","NOISE","DELAY","CROSS","RECTIFY"};
constexpr int ModeCount=17,SourceCount=6;
const NativeParam params[]={
 {"MODE",0,NativeUnit::Choice,0,100,modes,ModeCount},
 {"SOURCE",0,NativeUnit::Choice,0,100,sources,SourceCount},
 {"SYNC",0,NativeUnit::Toggle,0,100},
 {"FREQ",63,NativeUnit::Range,30,85,nullptr,0,1,5000,true," Hz"},
 {"DIVISION",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(15)},
 {"PARAM A",0,NativeUnit::Percent,0,80},
 {"PARAM B",0,NativeUnit::Percent,0,60},
 {"X DRIVE",25,NativeUnit::Range,15,50,nullptr,0,0,4,false," x"},
 {"Y DRIVE",25,NativeUnit::Range,15,50,nullptr,0,0,4,false," x"},
 {"DC COUPLE",0,NativeUnit::Toggle,0,0},
 {"TONE",100,NativeUnit::Range,55,100,nullptr,0,200,20000,true," Hz"},
};
enum{RingMod1,RingMod2,RingMod3,DiodeRM,MinMax,Sign1,Sign2,XFade,FlipFlop,AlphaPWM,
 BitAnd,BitXor,BitInterleave,BitHack,BitAndFloat,BitInterleaveFloat,BitHackFloat};

inline uint32_t bitsOf(float x){uint32_t u;std::memcpy(&u,&x,4);return u;}
inline float floatOf(uint32_t u){float x;std::memcpy(&x,&u,4);return x;}
// _mm_cvttps_epi32: truncation, out-of-range and NaN give 0x80000000.
inline int32_t cvtt(float v){return v>-2147483648.f&&v<2147483648.f?int32_t(v):INT32_MIN;}
inline float frac(float v){return v-float(cvtt(v));}
inline float lerp(float a,float b,float f){return a+f*(b-a);}
inline uint32_t mwcRand(uint32_t&w,uint32_t&z){z=36969*(z&65535)+(z>>16);w=18000*(w&65535)+(w>>16);return (z<<16)+w;}
// Valley's vecDriveSignal / tanhDriveSignal: a piecewise quadratic soft clipper.
inline float drive(float x,float d){
 float xd=x*d;
 if(xd< -1.3f)return -1;if(xd< -.75f)return (xd*xd+2.6f*xd+1.69f)*.833333f-1;
 if(xd>1.3f)return 1;if(xd>.75f)return 1-(xd*xd-2.6f*xd+1.69f)*.833333f;
 return xd;
}
inline float mirror(float a,float f){
 float g=std::fabs(f)*9+1;float x=((a*2-1)*g+1)*.5f;float y=x;
 x=std::fabs(x)*.5f;x=frac(x);y=std::fabs(y);float z=frac(y);
 return x>.5f?1-z:z;
}
inline float varStep(float a,float f){
 float af=std::fabs(f),ff=64-af*64;float q=float(cvtt(a*ff))/ff;
 return lerp(a,q,std::clamp(af*100,0.f,1.f));
}
// VecDiode: the makeup gain table is the closed-form result of the original's search
// (hB * calcLin(1) settles at 0.9999985), read with the original's interpolation.
struct Diode {
 float vB=0,vLS=.001f,den=500,hB=1;
 static float tableGain(int i,int j){
  float vB=float(i)/511*.75f,vL=std::clamp(vB+float(j)/511*(1-vB),vB+.0001f,1.f);
  return .9999985f/(1-vL+(vL-vB)*.5f);
 }
 void setV(float b,float l){
  vB=.75f*b;float vL=std::clamp(l,0.f,1.f);vLS=std::clamp(vB+vL*(1-vB),vB+.001f,1.f);den=1/(2*(vLS-vB));
  float vBF=(vB-.001f)/(.75f-.001f)*511,vLF=(vLS-vB)/(1-vB)*511;
  int b1=std::clamp(int(vBF),0,511),l1=std::clamp(int(vLF),0,511),b2=std::min(b1+1,511),l2=std::min(l1+1,511);
  vBF-=float(b1);vLF-=float(l1);
  hB=lerp(lerp(tableGain(b1,l1),tableGain(b1,l2),vBF),lerp(tableGain(b2,l1),tableGain(b2,l2),vBF),vLF);
 }
 float process(float x)const{
  float o=x>vB?(x-vB)*(x-vB)*den:0;
  if(x>vLS)o=x-vLS+(vLS-vB)*(vLS-vB)*den;
  return o*hB;
 }
 float ring(float x,float y)const{float b=x*.5f,a=y+b;b=y-b;return (process(a)+process(-a))-(process(b)+process(-b));}
};
struct OnePoleHP {float a=0,b=0,z=0;void set(float hz){b=std::exp(-2*NativePi*hz/44100);a=1-b;}
 float process(float x){z=a*x+z*b;if(std::fabs(z)<1e-20f)z=0;return x-z;}};

class Amalgam final:public NativeFx{
 static constexpr int DelayFrames=88200;   // 2 s
 int mode=0,source=0;bool sync=false,dcCoupled=false,toneOn=false;float freqHz=220,paramA=0,paramB=0,xGain=1,yGain=1;
 double spb=22050;int division=12;uint32_t epoch=0;
 // Oscillator / modulator state.
 double phase=0;float held=0;uint32_t seed=0xa3a1e5u;
 History delay{DelayFrames};
 // VecAmalgam state, one lane per channel.
 float step=1;float xDS[2]{},yDS[2]{},zDS[2]{},zState[2]{},xPrev[2]{},yPrev[2]{};bool ffTarget[2]{};
 int32_t c32[2]{};uint32_t rw[2]={0x1f123bb5u,0x0badc0deu},rz[2]={0x5eed1234u,0x7a11a9e5u};
 Diode diode;OnePoleHP dc[2];Biquad tone[2];float env=0,attack=0,release=0;
 float process(int c,float x,float y,bool tick){
  float A=paramA,B=paramB;
  auto down=[&](float xs,float ys){if(tick){xDS[c]=xs;yDS[c]=ys;}};
  switch(mode){
   case RingMod1:{zState[c]=(y>B*1.066f||y<B*-1.066f)?y:0;return (x*(1-.5f*A)+A*.5f)*zState[c];}
   case RingMod2:{float xf=(mirror(x*.5f+.5f,A+.00001f)-.5f)*2,yf=(mirror(y*.5f+.5f,B+.00001f)-.5f)*2;return xf*yf;}
   case RingMod3:{float xf=(mirror(x*.5f+.5f,A+.00001f)-.5f)*2,yf=(mirror(y*.5f+.5f,A+.00001f)-.5f)*-2;
    zState[c]=((xf>0)!=(yf>0))?-1.f:1.f;return lerp(xf*yf,zState[c],B);}
   case DiodeRM:return diode.ring(x,y);
   case MinMax:{float mx=x>y?x:y,mn=x>y?y:x;zState[c]=lerp(mn,mx,A);return lerp(zState[c],zState[c]*(mx-mn),B);}
   case Sign1:{float mid=A*2-1;return (x>=mid+B?x:0.f)+(y<mid-B?y:0.f);}
   case Sign2:{float mid=A*2-1;zState[c]=x>mid+B?x:0.f;return y<mid-B?y:zState[c];}
   case XFade:return lerp(x,y,std::clamp(A+(x*.5f+.5f)*B,0.f,1.f));
   case FlipFlop:{
    bool chanceX=float(mwcRand(rz[c],rw[c]))/4294967296.f>A,chanceY=float(mwcRand(rz[c],rw[c]))/4294967296.f>1-A;
    bool xEdge=x>B&&xPrev[c]<=B&&chanceX,yEdge=y>B&&yPrev[c]<=B&&chanceY;
    if(xEdge)ffTarget[c]=false;if(yEdge)ffTarget[c]=true;xPrev[c]=x;yPrev[c]=y;return ffTarget[c]?y:x;}
   case AlphaPWM:{zState[c]=frac(std::fabs(x)*.5f*(A*16+1));return (zState[c]>1-B?0.f:x)*y;}
   case BitAnd:case BitXor:case BitInterleave:{
    float p=1-A*.8f;p*=p;down(varStep(x,1-p),varStep(y,1-p));
    int32_t a=cvtt(xDS[c]*2147483648.f),b=cvtt(yDS[c]*2147483648.f);
    if(mode==BitAnd)c32[c]=a&b;else if(mode==BitXor)c32[c]=a^b;
    else c32[c]=int32_t((uint32_t(a)&0x55555555u)|(uint32_t(b)&0xAAAAAAAAu));
    return float(c32[c])/2147483648.f;}
   case BitHack:{
    int32_t r=int32_t(mwcRand(rz[c],rw[c]));int32_t chance=float(r)/4294967296.f>(.5f-A*A*.5f)?r:-1;
    down(x,y);int32_t a=cvtt(xDS[c]*2147483648.f),b=cvtt(yDS[c]*2147483648.f);
    c32[c]=(c32[c]|(a&b))&chance;c32[c]&=~((~a&~b)&chance);
    zState[c]=float(c32[c])/2147483648.f;if(tick)zDS[c]=zState[c];return zDS[c];}
   case BitAndFloat:case BitHackFloat:{
    uint32_t r=mwcRand(rz[c],rw[c]);uint32_t chance=float(r)/4294967296.f>(.5f-A*A*.5f)?r:0xFFFFFFFFu;
    down(x,y);uint32_t xb=bitsOf(xDS[c]),yb=bitsOf(yDS[c]),z;
    if(mode==BitAndFloat)z=xb&yb&chance;
    else{z=(bitsOf(zState[c])|(xb&yb))&chance;z&=~((~xb&~yb)&chance);}
    zState[c]=floatOf(z);if(tick)zDS[c]=zState[c];return zDS[c];}
   default:{float p=1-A*.8f;p*=p;down(varStep(x,1-p),varStep(y,1-p));
    return floatOf((bitsOf(xDS[c])&0x55555555u)|(bitsOf(yDS[c])&0xAAAAAAAAu));}
  }
 }
public:
 Amalgam(){for(auto&f:dc)f.set(1);attack=1-std::exp(-1/(.001f*44100));release=1-std::exp(-1/(.08f*44100));}
 void configure(const uint8_t*v,float bpm)override{
  mode=nativeChoice(v[0],ModeCount);source=nativeChoice(v[1],SourceCount);sync=v[2]>=50;
  freqHz=std::pow(5000.f,v[3]*.01f);division=nativeDivision(v[4]);spb=nativeSamplesPerBeat(bpm);
  paramA=std::clamp(v[5]*.01f,0.f,1.f);paramB=std::clamp(v[6]*.01f,0.f,1.f);
  xGain=v[7]*.04f;yGain=v[8]*.04f;dcCoupled=v[9]>=50;
  toneOn=v[10]<100;float hz=200*std::pow(100.f,v[10]*.01f);for(auto&t:tone)t.set(0,hz,.70710678f);
  if(sync)freqHz=float(44100/(nativeDivisionBeats(division)*spb));
 }
 void clear()override{
  phase=0;held=0;delay.clear();step=1;
  for(int c=0;c<2;c++){xDS[c]=yDS[c]=zDS[c]=zState[c]=xPrev[c]=yPrev[c]=0;ffTarget[c]=false;c32[c]=0;dc[c].z=0;tone[c].clear();}
  env=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;if(sync)phase=0;}   // downbeat
  // Sample clock of the bit modes: 2^(5(1-B))/32 of the engine rate.
  float stepSize=std::pow(2.f,(1-paramB)*5)/32;
  double inc=freqHz/44100.0;int period=std::clamp(int(std::lround(44100.0/std::max(freqHz,.01f))),1,DelayFrames-1);
  diode.setV(paramA,paramB);
  for(int i=0;i<frames;i++){
   float in[2]={io[i*2],io[i*2+1]};
   float level=std::max(std::fabs(in[0]),std::fabs(in[1]));env+=(level-env)*(level>env?attack:release);if(env<1e-9f)env=0;
   delay.write(in[0],in[1]);
   bool wrapped=false;phase+=inc;if(phase>=1){phase-=std::floor(phase);wrapped=true;}
   if(wrapped)held=nativeUnit(seed)*2-1;
   float y[2];
   for(int c=0;c<2;c++){
    switch(source){
     case 0:y[c]=std::sin(2*NativePi*float(phase));break;
     case 1:y[c]=phase<.5?1.f:-1.f;break;
     case 2:y[c]=held;break;
     case 3:y[c]=delay.read(c,period);break;
     case 4:y[c]=in[1-c];break;
     default:y[c]=std::fabs(in[c]);break;
    }
   }
   bool tick=false;step+=stepSize;if(step>=1){step-=1;tick=true;}
   float gate=std::min(1.f,env*1000);
   for(int c=0;c<2;c++){
    // Rack voltage (x5), then Amalgam's 0.15 input scale, drive and 1.333 makeup.
    float x=drive(in[c]*.75f,xGain)*1.333333f,yy=drive(y[c]*.75f,yGain)*1.333333f;
    float z=process(c,x,yy,tick);
    if(!std::isfinite(z))z=0;
    float hp=dc[c].process(z);z=dcCoupled?z:hp;
    float out=drive(z,.66f)*1.1f;   // tanhDriveSignal(z,0.66)*5.5 V back to +-1
    if(toneOn)out=tone[c].process(out);
    io[i*2+c]=nativeClean(out*gate);
   }
  }
 }
};
}
const NativeInfo amalgamInfo{FxType::Amalgam,"AMALGAM","AMALGM","AG",4,2,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeAmalgam(){return std::make_unique<Amalgam>();}
}
