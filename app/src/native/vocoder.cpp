// SPDX-License-Identifier: GPL-3.0-only
// VOCODER: a one-to-one recreation of the Surge XT Vocoder effect, Copyright 2018-2024
// various authors (Surge Synth Team), GPL-3.0-or-later; Surge was (C) 2004-2018 Claes
// Johanson / Vember Audio (https://github.com/surge-synthesizer/surge), including its
// VectorizedSVFilter band filters.
// The input (summed to mono, Surge's default "mono" input mode) is the modulator. It is
// raised by GAIN + 24 dB and split by up to 20 bands spaced evenly in pitch between MIN
// and MAX FREQ (Surge's VSM-201 derived defaults, 180 Hz..7.5 kHz). Each band is two
// cascaded Chamberlin state-variable band-passes stagger-tuned by +-0.4/Q around the
// band centre (Q = 20..30). A band's squared output (limited to 6, zeroed below GATE)
// feeds a one-pole envelope whose rate ENV FOLLOW sets (0.001..0.016 per sample); its
// square root scales the carrier before the carrier's own matching band filter, and the
// bands are summed with Surge's x4 output gain. FORMANT SHIFT and FORMANT RANGE move and
// stretch the modulator's bands against the carrier's (Surge's modulator Center and
// Range), which shifts the formants.
// Changes: Surge vocodes the synth's own voice with the audio input; here the effect
// input is the modulator and the carrier is generated inside: a band-limited (polyBLEP)
// saw or square, the same as a root-fifth-octave chord, stereo white noise, or a
// tempo-synced impulse train (shared division table, realigned on transport Start), at
// PITCH. For tonal carriers the left and right carriers are identical, so one carrier
// filter bank serves both channels (the output is the same as running two). The left/
// right/stereo modulator input modes are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const carriers[]={"SAW","SAW CHORD","SQUARE","SQUARE CHORD","NOISE","PULSE"};
const char*const bandCounts[]={"4","8","12","16","20"};
const NativeParam params[]={
 {"CARRIER",nativeChoiceValue(1,6),NativeUnit::Choice,0,100,carriers,6},
 {"PITCH",44,NativeUnit::Range,25,65,nullptr,0,32.703f,523.25f,true," Hz"},
 {"RATE",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(10),nativeDivisionValue(14)},
 {"GAIN",50,NativeUnit::Range,40,62,nullptr,0,-24,24,false," dB"},
 {"GATE",0,NativeUnit::Range,0,40,nullptr,0,-96,0,false," dB"},
 {"ENV FOLLOW",0,NativeUnit::Percent,0,60},
 {"Q",50,NativeUnit::Bipolar,20,80},
 {"BANDS",nativeChoiceValue(4,5),NativeUnit::Choice,nativeChoiceValue(2,5),100,bandCounts,5},
 {"MIN FREQ",29,NativeUnit::Range,15,45,nullptr,0,55,3520,true," Hz"},
 {"MAX FREQ",82,NativeUnit::Range,65,95,nullptr,0,440,14080,true," Hz"},
 {"FORMANT SHIFT",50,NativeUnit::Bipolar,25,75},
 {"FORMANT RANGE",50,NativeUnit::Bipolar,30,70},
};
constexpr int Bands=20;
// VectorizedSVFilter, one lane: two cascaded Chamberlin SVF band-passes.
struct SVF {
 float F1=0,F2=0,Q=0,L1=0,B1=0,L2=0,B2=0;
 void set(float omega,float quality,float spread){Q=1.f/quality;F1=2*std::sin(NativePi*omega*(1-spread));F2=2*std::sin(NativePi*omega*(1+spread));}
 void copy(const SVF&o){F1=o.F1;F2=o.F2;Q=o.Q;}
 void reset(){L1=B1=L2=B2=0;}
 void flush(){for(float*x:{&L1,&B1,&L2,&B2})if(std::fabs(*x)<1e-20f)*x=0;}
 float run(float in){
  L1=F1*B1+L1;float H1=(in*Q-L1)-Q*B1;B1=F1*H1+B1;
  L2=F2*B2+L2;float H2=(B1*Q-L2)-Q*B2;B2=F2*H2+B2;
  return B2;
 }
};
inline float polyBlep(float t,float dt){if(t<dt){t/=dt;return t+t-t*t-1;}if(t>1-dt){t=(t-1)/dt;return t*t+t+t+1;}return 0;}
class VocoderFx final:public NativeFx{
 SVF carL[Bands],carR[Bands],mod[Bands];float env[Bands]={};
 int active=20,carrier=1,division=12;float pitch=110,gainLin=15.85f,gateLevel=0,rate=.001f;
 double spb=22050,pulsePos=0;float phase[3]={0,0,0};uint32_t seed=0x70c0de25,epoch=0;bool configured=false;
 float osc(int i,float hz,bool square){
  float dt=hz/44100.f,t=phase[i],y;
  if(square){y=(t<.5f?1.f:-1.f)+polyBlep(t,dt);float t2=t+.5f;if(t2>=1)t2-=1;y-=polyBlep(t2,dt);}
  else y=2*t-1-polyBlep(t,dt);
  t+=dt;if(t>=1)t-=1;phase[i]=t;return y;
 }
public:
 void configure(const uint8_t*v,float bpm)override{
  carrier=nativeChoice(v[0],6);pitch=32.703f*std::pow(16.f,v[1]*.01f);division=nativeDivision(v[2]);spb=nativeSamplesPerBeat(bpm);
  float gain=v[3]*.48f-24+24.f;gainLin=std::pow(10.f,gain/20);
  float gate=std::pow(10.f,(v[4]*.96f-96+gain)/20);gateLevel=gate*gate;
  rate=.001f*std::pow(2.f,4.f*v[5]*.01f);
  // setvars()
  float q=(v[6]*2-100)*.01f,Q=20.f*(1.f+.5f*q),spread=.4f/Q;
  active=(nativeChoice(v[7],5)+1)*4;
  float flo=std::clamp(12*std::log2(55*std::pow(64.f,v[8]*.01f)/440.f),-36.f,36.f);
  float fhi=std::clamp(12*std::log2(440*std::pow(32.f,v[9]*.01f)/440.f),0.f,60.f);
  if(flo>fhi)std::swap(flo,fhi);
  float df=(fhi-flo)/(active-1),fb=440.f*std::pow(2.f,flo/12),dhz=std::pow(2.f,df/12),mb=fb,mdhz=dhz;
  float mC=(v[10]*2-100)*.01f,mX=(v[11]*2-100)*.01f;bool sep=mC!=0||mX!=0;
  if(sep){float dist=fhi-flo,half=dist/2,mid=half+flo+.3f*mC*half,mLo=mid-half*(1+.7f*mX),dM=half*2*(1+.7f*mX)/(active-1);
   if(mLo+dM*(active-1)>60)dM=(60-mLo)/(active-1);mb=440.f*std::pow(2.f,mLo/12);mdhz=std::pow(2.f,dM/12);}
  for(int i=0;i<active;i++){carL[i].set(fb/44100.f,Q,spread);carR[i].copy(carL[i]);
   if(sep)mod[i].set(mb/44100.f,Q,spread);else mod[i].copy(carL[i]);fb*=dhz;mb*=mdhz;}
  configured=true;
 }
 void clear()override{for(int i=0;i<Bands;i++){carL[i].reset();carR[i].reset();mod[i].reset();env[i]=0;}pulsePos=0;phase[0]=phase[1]=phase[2]=0;}
 void process(float*io,int frames)override{
  if(!configured)return;
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;pulsePos=0;}   // downbeat
  double period=std::max(1.0,nativeDivisionBeats(division)*spb);
  bool stereo=carrier==4;
  for(int k=0;k<frames;k++){
   // Carrier.
   float cl=0,cr;
   switch(carrier){
    case 0:cl=osc(0,pitch,false);break;
    case 1:cl=.5f*(osc(0,pitch,false)+osc(1,pitch*1.4983071f,false)+osc(2,pitch*2,false));break;
    case 2:cl=osc(0,pitch,true);break;
    case 3:cl=.5f*(osc(0,pitch,true)+osc(1,pitch*1.4983071f,true)+osc(2,pitch*2,true));break;
    case 4:cl=nativeUnit(seed)*2-1;break;
    default:if(pulsePos<=0){cl=48;pulsePos+=period;}pulsePos-=1;break;
   }
   cr=stereo?nativeUnit(seed)*2-1:cl;
   float in=(io[k*2]+io[k*2+1])*gainLin,L=0,R=0;
   for(int j=0;j<active;j++){
    float m=mod[j].run(in);m=std::min(m*m,6.f);if(!(m>=gateLevel))m=0;
    env[j]=env[j]*(1-rate)+rate*m;if(env[j]<1e-30f)env[j]=0;
    float g=std::sqrt(env[j]);
    L+=carL[j].run(cl*g);if(stereo)R+=carR[j].run(cr*g);
   }
   if(!stereo)R=L;
   io[k*2]=nativeClean(L*4);io[k*2+1]=nativeClean(R*4);
  }
  for(int j=0;j<active;j++){carL[j].flush();carR[j].flush();mod[j].flush();}
 }
};
}
const NativeInfo vocoderInfo{FxType::Vocoder,"VOCODER","VOCODR","VO",2,0,5,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeVocoder(){return std::make_unique<VocoderFx>();}
}
