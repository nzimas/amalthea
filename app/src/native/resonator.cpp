// SPDX-License-Identifier: GPL-3.0-only
// RESONATOR: a one-to-one recreation of the Surge XT Resonator effect, Copyright 2018-2024
// various authors (Surge Synth Team), GPL-3.0-or-later; Surge was (C) 2004-2018 Claes
// Johanson / Vember Audio (https://github.com/surge-synthesizer/surge), with its filter
// models from sst-filters (Surge Synth Team, GPL-3.0-or-later).
// Three resonant bands run in parallel at 2x oversampling (Surge's 12-pole steep
// half-band filters). The input first passes Surge's asymmetric waveshaper (it mimics the
// Polymoog's single-rail supply); each band is one of Surge's "Driven" 2-pole models: a
// coupled-form state-space biquad whose state is scaled by a clip gain computed from
// the last output (LOWPASS and HIGHPASS use the 12 dB Driven coefficients; BANDPASS uses
// the legacy driven band-pass whose 12 and 24 dB coefficient sets both pass through the
// coefficient smoother, so the filter settles on a blend of the two, exactly as Surge
// does; BANDPASS N inverts band 2). Resonance is scaled per model (0.75/0.9/0.9/0.75)
// to keep the bands from self-oscillating. The bands are summed one by one through
// Surge's tanh table (so the soft clip nests, as in the original), then the output gain
// is applied. Cutoff, resonance and band gain glide through Surge's one-pole lags.
// Changes: the band frequency ranges are this port's own (Surge's own limits were not
// at hand); extended resonance and tuning-table support are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const modes[]={"LOWPASS","BANDPASS","BANDPASS N","HIGHPASS"};
const NativeParam params[]={
 {"FREQUENCY 1",41,NativeUnit::Range,20,70,nullptr,0,40,1000,true," Hz"},
 {"RESONANCE 1",75,NativeUnit::Percent,40,95},
 {"GAIN 1",79,NativeUnit::Percent,50,100},
 {"FREQUENCY 2",39,NativeUnit::Range,20,70,nullptr,0,200,5000,true," Hz"},
 {"RESONANCE 2",75,NativeUnit::Percent,40,95},
 {"GAIN 2",79,NativeUnit::Percent,50,100},
 {"FREQUENCY 3",45,NativeUnit::Range,20,70,nullptr,0,1000,16000,true," Hz"},
 {"RESONANCE 3",75,NativeUnit::Percent,40,95},
 {"GAIN 3",79,NativeUnit::Percent,50,100},
 {"MODE",nativeChoiceValue(1,4),NativeUnit::Choice,0,100,modes,4},
 {"GAIN",50,NativeUnit::Range,40,55,nullptr,0,-24,24,false," dB"},
};
// Surge's one-pole parameter lag (rate 0.004 per base-rate sample).
struct Lag {
 float v=0,target=0;bool first=true;
 void set(float x){target=x;if(first){v=x;first=false;}}
 void process(){v=v*(1-.004f)+target*.004f;}
};
// sst-filters HalfRateFilter(6, steep): two cascades of 2nd-order (z^-2) allpasses.
struct HalfBand {
 static constexpr int M=6;
 static constexpr float A[M]={0.036681502163648017f,0.2746317593794541f,0.56109896978791948f,0.769741833862266f,0.8922608180038789f,0.962094548378084f};
 static constexpr float B[M]={0.13654762463195771f,0.42313861743656667f,0.6775400499741616f,0.839889624849638f,0.9315419599631839f,0.9878163707328971f};
 struct Chain{float x0[M],x1[M],x2[M],y0[M],y1[M],y2[M];
  float run(const float*k,float u){for(int j=0;j<M;j++){x2[j]=x1[j];x1[j]=x0[j];x0[j]=u;y2[j]=y1[j];y1[j]=y0[j];y0[j]=x2[j]+(x0[j]-y2[j])*k[j];u=y0[j];}return u;}};
 Chain a[2],b[2];float oldB[2]={0,0};
 void reset(){for(auto*c:{a,b})for(int i=0;i<2;i++)c[i]=Chain{};oldB[0]=oldB[1]=0;}
 HalfBand(){reset();}
 void up(int c,float x,float out[2]){
  float s[2]={x,0};
  for(int k=0;k<2;k++){float ya=a[c].run(A,s[k]),yb=b[c].run(B,s[k]);out[k]=.5f*(ya+oldB[c]);oldB[c]=yb;}
 }
 float down(int c,const float in[2]){
  float b0=b[c].run(B,in[0]);a[c].run(A,in[0]);b[c].run(B,in[1]);float a1=a[c].run(A,in[1]);
  return .5f*(b0+a1);
 }
};
constexpr float HalfBand::A[];constexpr float HalfBand::B[];
// FilterCoefficientMaker state for one band: coefficients C, their per-sample ramp dC
// and the smoothed target tC (smooth = 0.2 per block).
struct Coeffs {
 float C[8]={},dC[8]={},tC[8]={};bool first=true;float blockInv=1.f/64;
 void fromDirect(const double(&N)[8]){
  if(first){for(int i=0;i<8;i++){C[i]=tC[i]=float(N[i]);dC[i]=0;}first=false;}
  else for(int i=0;i<8;i++){tC[i]=.8f*tC[i]+.2f*float(N[i]);dC[i]=(tC[i]-C[i])*blockInv;}
 }
 void toCoupledForm(double a0inv,double a1,double a2,double b0,double b1,double b2,double g){
  b0*=a0inv;b1*=a0inv;b2*=a0inv;a1*=a0inv;a2*=a0inv;
  double sq=a1*a1-4.0*a2,ar=.5*-a1;sq=std::min(0.0,sq);double ai=std::max(.5*std::sqrt(-sq),8.0*1.192092896e-07);
  double bb1=b1-a1*b0,bb2=b2-a2*b0;
  double N[8]={ar,ai,1,0,bb1,(bb1*ar+bb2)/ai,b0,g};fromDirect(N);
 }
};
inline double map2Pole(double reso,double f){reso*=std::max(0.0,1.0-std::max(0.0,(f-58)*.05));return 1.0-1.05*std::clamp(1-(1-reso)*(1-reso),.001,1.0);}
inline double map4Pole(double reso,double f){reso*=std::max(0.0,1.0-std::max(0.0,(f-58)*.05));return 1.0-1.05*std::clamp(reso,.001,1.0);}
inline float clipscale(float f){return (1.f/64.f)*std::pow(10.f,.05f*f*.55f);}
// kind 0 LP12, 1 HP12, 2 BP12, 3 BP24 (all st_Driven), at the 88.2 kHz oversampled rate.
void makeDriven(Coeffs&k,int kind,float freq,float reso){
 float gain=1-.5f*reso*reso;if(kind>=2)gain*=2;
 freq=std::clamp(freq,-55.f,75.f);
 float arg=2*NativePi*std::min(.5f,440*std::exp2(freq/12)/88200.f),sinu=std::sin(arg),cosi=std::cos(arg);
 double q2inv=kind==3?map4Pole(reso,freq):map2Pole(reso,freq),alpha=sinu*q2inv;
 alpha=std::min(alpha,std::sqrt(1.0-double(cosi)*cosi)-.0001);
 double a0=1+alpha,a0inv=1/a0,a1=-2*cosi,a2=1-alpha,b0,b1,b2;
 if(kind==0){b0=(1-cosi)*.5;b1=1-cosi;b2=b0;}
 else if(kind==1){b0=(1+cosi)*.5;b1=-(1+cosi);b2=b0;}
 else{double Q=.5/q2inv;b0=Q*alpha;b1=0;b2=-Q*alpha;}
 k.toCoupledForm(a0inv,a1,a2,b0*gain,b1*gain,b2*gain,clipscale(freq));
}
// IIR12CFCquad: state-space with clip gain, one lane.
struct Driven {
 float R[3]={0,0,0};
 float run(float*C,const float*dC,float in){
  C[0]+=dC[0];C[1]+=dC[1];C[2]+=dC[2];C[4]+=dC[4];C[5]+=dC[5];C[6]+=dC[6];
  float y=C[4]*R[0]+C[6]*in+C[5]*R[1];
  float s1=in*C[2]+(C[0]*R[0]-C[1]*R[1]),s2=C[1]*R[0]+C[0]*R[1];
  R[0]=s1*R[2];R[1]=s2*R[2];C[7]+=dC[7];R[2]=std::max(.1f,1-C[7]*y*y);
  return y;
 }
};
class ResonatorFx final:public NativeFx{
 float freqT[3]={},resT[3]={},gainT[3]={},outGainT=1,outGain=1;int mode=1;bool configured=false,gainFirst=true;
 Lag cutoff[3],resonance[3],bandGain[3];
 Coeffs coeff[3][2];Driven filt[3][2];HalfBand hbIn,hbOut;
 std::vector<float> asym,soft;
 static float lookup(const std::vector<float>&t,float x){x=x*32+512;int e=int(x);float a=x-float(e);if(e>0x3fd)return t[1023];if(e<1)return t[0];return (1-a)*t[e]+a*t[e+1];}
public:
 ResonatorFx():asym(1024),soft(1024){
  auto shafted=[](double x){return float((std::exp(x)-std::exp(-x*1.2))/(std::exp(x)+std::exp(-x)));};
  float zero=shafted(.5);
  for(int i=0;i<1024;i++){double x=(i-512.0)/32.0;soft[i]=float(std::tanh(x));asym[i]=shafted(x+.5)-zero;}
 }
 void configure(const uint8_t*v,float)override{
  static const float lo[3]={40,200,1000},hi[3]={1000,5000,16000};
  for(int i=0;i<3;i++){float hz=lo[i]*std::pow(hi[i]/lo[i],v[i*3]*.01f);freqT[i]=12*std::log2(hz/440.f);
   resT[i]=v[i*3+1]*.01f;float g=v[i*3+2]*.01f;gainT[i]=g*g*g;}
  mode=nativeChoice(v[9],4);outGainT=std::pow(10.f,(v[10]*.48f-24)/20);configured=true;
 }
 void clear()override{
  for(auto&r:coeff)for(auto&c:r)c=Coeffs{};for(auto&r:filt)for(auto&f:r)f=Driven{};hbIn.reset();hbOut.reset();
  for(int i=0;i<3;i++){cutoff[i].first=resonance[i].first=bandGain[i].first=true;}gainFirst=true;
 }
 void process(float*io,int frames)override{
  if(!configured||frames<=0)return;
  for(int i=0;i<3;i++){cutoff[i].set(freqT[i]);resonance[i].set(resT[i]);bandGain[i].set(gainT[i]);}
  static const float rescomp[4]={.75f,.9f,.9f,.75f};
  int kind=mode==0?0:mode==3?1:2;float binv=1.f/(frames*2);
  for(int e=0;e<3;e++)for(int c=0;c<2;c++){auto&k=coeff[e][c];k.blockInv=binv;float r=resonance[e].v*rescomp[mode];
   makeDriven(k,kind,cutoff[e].v,r);if(kind==2)makeDriven(k,3,cutoff[e].v,r);}   // st_bp12_LegacyDriven
  float g0=gainFirst?outGainT:outGain,dg=(outGainT-g0)/frames;gainFirst=false;
  for(int f=0;f<frames;f++){
   float up[2][2],out[2][2];hbIn.up(0,io[f*2],up[0]);hbIn.up(1,io[f*2+1],up[1]);
   for(int s=0;s<2;s++){
    float x[2]={lookup(asym,up[0][s]),lookup(asym,up[1][s])},t[3][2];
    for(int e=0;e<3;e++)for(int c=0;c<2;c++)t[e][c]=filt[e][c].run(coeff[e][c].C,coeff[e][c].dC,x[c]);
    float ml=0,mr=0;
    for(int i=0;i<3;i++){
     if(mode==2&&i==1){ml-=t[i][0]*bandGain[i].v;mr-=t[i][1]*bandGain[i].v;}
     else{ml+=t[i][0]*bandGain[i].v;mr+=t[i][1]*bandGain[i].v;}
     ml=lookup(soft,ml);mr=lookup(soft,mr);
     if(s==0){cutoff[i].process();resonance[i].process();bandGain[i].process();}
    }
    out[0][s]=ml;out[1][s]=mr;
   }
   float g=g0+dg*(f+1);
   for(int c=0;c<2;c++)io[f*2+c]=nativeClean(hbOut.down(c,out[c])*g);
  }
  outGain=outGainT;
 }
};
}
const NativeInfo resonatorInfo{FxType::Resonator,"RESONATOR","RESON","RE",2,0,5,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeResonator(){return std::make_unique<ResonatorFx>();}
}
