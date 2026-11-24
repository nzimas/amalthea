// SPDX-License-Identifier: GPL-3.0-only
// RING MOD: a one-to-one recreation of Surge XT's Ring Modulator (RingModulatorEffect),
// Copyright 2005-2023 various authors (Surge Synth Team), GPL-3.0-or-later
// (https://github.com/surge-synthesizer/surge), with its helpers: the Sine oscillator's
// 32 wave shapes (SineOscillator.cpp), fastsin/fastcos (sst-basic-blocks), the steep
// 12th-order half-rate filter (sst-filters HalfRateFilter) and BiquadFilter coeff_HP /
// coeff_LP2B (sst-filters), same licence and authors.
// The diode ring modulator of Parker's DAFx-11 paper: at 2x oversampling each unison
// carrier voice c and the halved input v feed four diode branches, (A=v/2+c, B=c-v/2)
// f(A)+f(-A)-f(B)-f(-B), where the diode is off below FORWARD BIAS, quadratic up to
// LINEAR REGION and linear above it. Voices are spread by UNISON DETUNE (semitone
// offsets -1..+1 times the detune), start at evenly spread phases and pan from right
// to left; the sum is scaled by 0.4+0.6/sqrt(voices), soft-clipped (1.5x-0.5x^3),
// decimated, then low and high cut. As upstream, both input channels' products are
// summed into each output, so a single voice gives a mono result.
// Changed: unison is bounded to 8 voices (Surge allows 16) for the Brick's CPU, the
// "audio input" carrier and absolute detune are omitted, the frequency is set in Hz
// (8.18 Hz..12.5 kHz, upstream's MIDI note 0..127), and SYNC adds a tempo carrier: the
// carrier then runs one cycle per SYNC RATE division (LFO rates give rhythmic
// amplitude modulation) and its phases realign on transport Start. Filter coefficients
// change per host block instead of being smoothed; the plugin's MIX is left to the host.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const shapes[]={"WAVE 1","WAVE 2","WAVE 3","WAVE 4","WAVE 5","WAVE 6","WAVE 7","WAVE 8",
 "WAVE 9","WAVE 10","WAVE 11","WAVE 12","WAVE 13","WAVE 14","WAVE 15","WAVE 16",
 "WAVE 17","WAVE 18","WAVE 19","WAVE 20","WAVE 21","WAVE 22","WAVE 23","WAVE 24",
 "WAVE 25","WAVE 26","WAVE 27","WAVE 28","WAVE 29","WAVE 30","WAVE 31","WAVE 32"};
const char*const voiceNames[]={"1","2","3","4","5","6","7","8"};
constexpr int MaxUnison=8;
const NativeParam params[]={
 {"SHAPE",0,NativeUnit::Choice,0,100,shapes,32},
 {"FREQUENCY",47,NativeUnit::Range,25,70,nullptr,0,8.1758f,12543.85f,true," Hz"},
 {"SYNC",0,NativeUnit::Toggle,0,100},
 {"SYNC RATE",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(13)},
 {"UNISON DETUNE",20,NativeUnit::Range,0,40,nullptr,0,0,100,false," ct"},
 {"UNISON VOICES",0,NativeUnit::Choice,0,43,voiceNames,MaxUnison},
 {"FORWARD BIAS",30,NativeUnit::Percent,10,50},
 {"LINEAR REGION",70,NativeUnit::Percent,40,90},
 {"LOW CUT",0,NativeUnit::Range,0,45,nullptr,0,13.75f,22351,true," Hz"},
 {"HIGH CUT",100,NativeUnit::Range,60,100,nullptr,0,13.75f,22351,true," Hz"},
};
constexpr double Rate=44100,TwoPi=6.283185307179586;

// sst-basic-blocks rational approximations (valid on -pi..pi).
inline float fastsin(float x){float x2=x*x;
 return -x*(-11511339840.f+x2*(1640635920.f+x2*(-52785432.f+x2*479249.f)))/(11511339840.f+x2*(277920720.f+x2*(3177720.f+x2*18361.f)));}
inline float fastcos(float x){float x2=x*x;
 return -(-39251520.f+x2*(18471600.f+x2*(-1075032.f+14615.f*x2)))/(39251520.f+x2*(1154160.f+x2*(16632.f+x2*127.f)));}

// Surge's Sine oscillator wave shapes, scalar forms of its SSE masks (and_ps(x, s>=0) is
// "x where s>=0, else 0").
inline float quadrant(float s,float c){int sl=s<=0,cl=c<=0;return float(3*sl+cl-2*sl*cl+1);}
float shape(int mode,float s,float c);
inline float shape1(float s){float c2x=1-2*s*s;return s>=0?.5f*(1-c2x):.5f*(c2x-1);}
inline float shape28(float s,float c){float sw=s>=0?1.f:-1.f,pm=s*c<0?1.f:-1.f;return sw+pm*c;}
float shape(int mode,float s,float c){
 float s2x=2*s*c,c2x=1-2*s*s;
 switch(mode){
  default:return s;
  case 1:return shape1(s);
  case 2:return s>=0?s:0;
  case 3:return s>=0?.5f*(1-c2x):0;
  case 4:return s>=0?s2x:0;
  case 5:return s>=0?shape1(s2x):0;
  case 6:return std::fabs(s>=0?s2x:0);
  case 7:return std::fabs(s>=0?shape1(s2x):0);
  case 8:return 2*(s>=0?s:0)-1;
  case 9:return s*c<=0?s:0;
  case 10:return s*c>=0?s:0;
  case 11:return 2*(s>=0?.5f*(1-c2x):0)-1;
  case 12:return s2x*(c>=0?1:-1);
  case 13:return s2x>=0?(s<=0?-1:1)*s2x:0;
  case 14:return s>=0?std::fabs(c2x):0;
  case 15:return c>=0?(s>=0?1-s:-1-s):0;
  case 16:return c>=0?(s>=0?1-s:c-1):0;
  case 17:return (s>=0?1:-1)-s;
  case 18:return c<=0?c:(s>=0?1:-1)*s2x;
  case 19:{float s4x=2*s2x*c2x;return s>=0?(c>=0?s2x:-s4x):s;}
  case 20:return s*c>=0?s:(s>=0?1:-1);
  case 21:return s*c>=0?(s>=0?1:-1):s;
  case 22:return c>=0?s:0;
  case 23:return c<=0?s:0;
  case 24:return s>=0?1-s:s;
  case 25:return s>=0?s2x/quadrant(s,c):0;
  case 26:return s<=0&&c<=0?0:s;
  case 27:return s2x/quadrant(s,c);
  case 28:return shape28(s,c);
  case 29:{if(s<0)return 0;float q2=c<=0?1.f:0.f;return 1+c*(-1+2*q2);}
  case 30:return s>=0?shape28(s2x,c2x):0;
  case 31:return std::fabs(s>=0?shape28(s2x,c2x):0);
 }
}

// sst-filters HalfRateFilter (M=6, steep): two cascades (coefficients a, b) of allpasses
// y[n] = x[n-2] + (x[n] - y[n-2]) k run on the full-rate stream. Each stage reaches back
// two samples, so the even and odd samples are independent streams: only the streams that
// reach the output are computed, bit-identical to upstream's running all of them.
//  U2 (zero-stuffed, level halved as upstream): out0 = A(x)/2, out1 = B(x)/2 (the odd
//     zero stream stays exactly zero). D2: out = (B(even) + A(odd))/2.
struct HalfRate {
 static constexpr int M=6;
 static constexpr float ca[M]={0.036681502163648017f,0.2746317593794541f,0.56109896978791948f,0.769741833862266f,0.8922608180038789f,0.962094548378084f};
 static constexpr float cb[M]={0.13654762463195771f,0.42313861743656667f,0.6775400499741616f,0.839889624849638f,0.9315419599631839f,0.9878163707328971f};
 struct Half{float x=0,y=0;};Half sa[2][M],sb[2][M];
 void reset(){for(int c=0;c<2;c++)for(int j=0;j<M;j++)sa[c][j]=sb[c][j]=Half{};}
 static float run(Half*st,const float*co,float x){
  for(int j=0;j<M;j++){auto&h=st[j];float y=h.x+(x-h.y)*co[j];if(std::fabs(y)<1e-30f)y=0;h.x=x;h.y=y;x=y;}
  return x;
 }
 void up(int c,float x,float out[2]){out[0]=.5f*run(sa[c],ca,x);out[1]=.5f*run(sb[c],cb,x);}
 float down(int c,const float in[2]){return .5f*(run(sb[c],cb,in[0])+run(sa[c],ca,in[1]));}
};
constexpr float HalfRate::ca[HalfRate::M];
constexpr float HalfRate::cb[HalfRate::M];

// sst-filters BiquadFilter: coefficients normalised by a0, transposed direct form II in double.
struct SurgeBiquad {
 double a1=0,a2=0,b0=1,b1=0,b2=0,reg0[2]{},reg1[2]{};
 void setCoef(double A0,double A1,double A2,double B0,double B1,double B2){double i=1/A0;a1=A1*i;a2=A2*i;b0=B0*i;b1=B1*i;b2=B2*i;}
 void highpass(double w,double q){
  if(w>NativePi){setCoef(1,0,0,0,0,0);return;}
  double c=std::cos(w),s=std::sin(w),al=s/(2*q);setCoef(1+al,-2*c,1-al,(1+c)*.5,-(1+c),(1+c)*.5);
 }
 void lowpass2B(double w,double q){
  if(w>NativePi){setCoef(1,0,0,1,0,0);return;}
  double pi2=double(NativePi)*NativePi,wsq=w*w,den=wsq*wsq+pi2*pi2+wsq*pi2*(1/q-2);
  double G1=std::min(1.0,std::sqrt(wsq*wsq/den)*.5),c=std::cos(w),s=std::sin(w),al=s/(2*q),A=2*std::sqrt(G1)*std::sqrt(2-G1);
  setCoef(1+al,-2*c,1-al,(1-c+G1*(1+c)+A*s)*.5,1-c-G1*(1+c),(1-c+G1*(1+c)-A*s)*.5);
 }
 void clear(){reg0[0]=reg0[1]=reg1[0]=reg1[1]=0;}
 float process(int ch,float x){
  double op=x*b0+reg0[ch];reg0[ch]=x*b1-a1*op+reg1[ch];reg1[ch]=x*b2-a2*op;
  if(std::fabs(reg0[ch])<1e-30)reg0[ch]=0;if(std::fabs(reg1[ch])<1e-30)reg1[ch]=0;return float(op);
 }
};

class RingModFx final:public NativeFx{
 int mode=0,uni=1,lastUnison=-1,division=12;bool sync=false;
 float carrierHz=261.6f,detune=.2f,vb=.3f,vl=.7f;double spb=22050,carrierBase=261.6;
 float phase[MaxUnison]{},detuneOffset[MaxUnison]{},panL[MaxUnison]{},panR[MaxUnison]{},dphase[MaxUnison]{};
 HalfRate halfIn,halfOut;SurgeBiquad lp,hp;uint32_t epoch=0;
 float diode(float v)const{
  if(v<vb)return 0;
  if(v<vl){float d=v-vb;return d*d/(2*vl-2*vb);}
  float d=vl-vb;return v-vl+d*d/(2*vl-2*vb);
 }
 void setUnison(){
  lastUnison=uni;
  if(uni==1){detuneOffset[0]=0;panL[0]=panR[0]=1;phase[0]=0;return;}
  float bias=2.f/(uni-1.f);
  for(int u=0;u<uni;u++){phase[u]=u*1.f/uni;detuneOffset[u]=-1+bias*u;panL[u]=u/(uni-1.f);panR[u]=(uni-1.f-u)/(uni-1.f);}
 }
public:
 RingModFx(){clear();}
 void configure(const uint8_t*v,float bpm)override{
  mode=nativeChoice(v[0],32);carrierHz=8.1758f*std::pow(12543.85f/8.1758f,v[1]*.01f);
  sync=v[2]>=50;division=nativeDivision(v[3]);spb=nativeSamplesPerBeat(bpm);
  detune=v[4]*.01f;uni=nativeChoice(v[5],MaxUnison)+1;
  vb=v[6]*.01f;vl=std::max(v[7]*.01f,vb+.02f);
  auto hz=[](uint8_t x){return 13.75*std::pow(22351/13.75,x*.01);};
  hp.highpass(TwoPi*hz(v[8])/Rate,.707);lp.lowpass2B(TwoPi*hz(v[9])/Rate,.707);
  carrierBase=sync?Rate/(nativeDivisionBeats(division)*spb):carrierHz;
 }
 void clear()override{halfIn.reset();halfOut.reset();lp.clear();hp.clear();lastUnison=-1;}
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;if(sync)lastUnison=-1;}   // downbeat: realign carrier phases
  if(uni!=lastUnison)setUnison();
  // Carrier increment per oversampled frame, upstream's note_to_pitch(note + detune * offset).
  for(int u=0;u<uni;u++)dphase[u]=float(carrierBase*std::exp2(detune*detuneOffset[u]/12)/(2*Rate));
  float gscale=.4f+.6f/std::sqrt(float(uni));
  for(int n=0;n<frames;n++){
   float os[2][2];halfIn.up(0,io[n*2],os[0]);halfIn.up(1,io[n*2+1],os[1]);
   float res[2][2];
   for(int k=0;k<2;k++){
    float resL=0,resR=0;
    for(int u=0;u<uni;u++){
     float x=float(TwoPi)*(phase[u]-.5f),vc=shape(mode,fastsin(x),fastcos(x));
     phase[u]+=dphase[u];if(phase[u]>1)phase[u]-=int(phase[u]);
     for(int c=0;c<2;c++){
      float vin=os[c][k],A=.5f*vin+vc,B=vc-.5f*vin;
      float r=diode(A)+diode(-A)-diode(B)-diode(-B);
      resL+=r*panL[u];resR+=r*panR[u];
     }
    }
    float ol=gscale*resL,orr=gscale*resR;
    res[0][k]=1.5f*ol-.5f*ol*ol*ol;res[1][k]=1.5f*orr-.5f*orr*orr*orr;
   }
   for(int c=0;c<2;c++){float w=halfOut.down(c,res[c]);w=lp.process(c,w);w=hp.process(c,w);io[n*2+c]=nativeClean(w);}
  }
 }
};
}
const NativeInfo ringModInfo{FxType::RingMod,"RING MOD","RINGMD","RM",2,1,4,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeRingMod(){return std::make_unique<RingModFx>();}
}
