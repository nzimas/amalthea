// SPDX-License-Identifier: GPL-3.0-only
// TREEMONSTER: a one-to-one recreation of Surge XT's Treemonster, from sst-effects
// (TreeMonster.h / effects-shared/TreemonsterCore.h), Copyright 2018-2023 various authors
// (Surge Synth Team), GPL-3.0-or-later (https://github.com/surge-synthesizer/sst-effects,
// https://github.com/surge-synthesizer/surge), with its helpers from sst-basic-blocks
// (SurgeQuadrOsc) and sst-filters (BiquadFilter coeff_HP / coeff_LP2B), same licence.
// A pitch-tracking ring modulator: each channel's positive zero crossings (above the
// THRESHOLD, after an optional low/high cut on the detection copy only) measure the
// wavelength, which glides at SPEED; a quadrature sine follows that pitch (+ PITCH
// semitones) and is either multiplied with the input (RING MOD 100) or scaled by a
// 5 ms / 500 ms envelope follower (RING MOD 0), crossfaded between the two. WIDTH
// scales the side signal. As upstream, the envelope-scaled sine of both channels uses
// the left envelope, and the follower tracks the signed (not rectified) input. Also as
// upstream, THRESHOLD is compared with the first sample after the crossing, so it gates
// low notes early; the default here is -48 dB (upstream -24 dB) so ordinary levels track.
// Changed: the per-32-sample block glide runs per host block (closed form of the same
// one-pole steps), LOW CUT at its minimum and HIGH CUT at its maximum stand for the
// plugin's deactivated filters, WIDTH is the linear side gain of the original Surge
// Treemonster (-100..+100) rather than sst-effects' dB reading of that value, and the
// plugin's MIX is left to the host's MIX. GUI and streaming code are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const NativeParam params[]={
 {"THRESHOLD",50,NativeUnit::Range,25,70,nullptr,0,-96,0,false," dB"},
 {"SPEED",50,NativeUnit::Percent,20,90},
 {"LOW CUT",0,NativeUnit::Range,0,40,nullptr,0,13.75f,22351,true," Hz"},
 {"HIGH CUT",100,NativeUnit::Range,60,100,nullptr,0,13.75f,22351,true," Hz"},
 {"PITCH",50,NativeUnit::Range,38,62,nullptr,0,-50,50,false," st"},
 {"RING MOD",50,NativeUnit::Percent,30,100},
 {"WIDTH",100,NativeUnit::Bipolar,60,100},
};
constexpr double Rate=44100,TwoPi=6.283185307179586;

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
// sst-basic-blocks SurgeQuadrOsc: a rotating unit vector, renormalised on every rate change.
struct QuadrOsc {
 float r=0,i=-1,dr=1,di=0;
 void setRate(float w){dr=std::cos(w);di=std::sin(w);double n=1/std::sqrt(double(r)*r+double(i)*i);r=float(r*n);i=float(i*n);}
 void setPhase(float w){r=std::sin(w);i=-std::cos(w);}
 void process(){float lr=r,li=i;r=dr*lr-di*li;i=dr*li+di*lr;}
};

class TreemonsterFx final:public NativeFx{
 float thres=.063f,logSpeed=0,pitchRatio=1,ringTarget=.5f,ring=.5f,widthTarget=1,width=1;
 bool hpOn=false,lpOn=false,first=true;
 SurgeBiquad hp,lp;QuadrOsc osc[2];
 float length[2],lastval[2],lengthTarget[2],lengthSmooth[2],envV[2];bool firstThresh[2];
 double envA=0,envR=0;
public:
 TreemonsterFx(){envA=std::pow(.01,1.0/(5*Rate*.001));envR=std::pow(.01,1.0/(500*Rate*.001));clear();}
 void configure(const uint8_t*v,float)override{
  thres=std::pow(10.f,(-96+.96f*v[0])/20);
  float qs=std::clamp(v[1]*.01f,0.f,1.f);qs*=qs*qs*qs;logSpeed=std::log(.9999f-qs*.0999f/128);
  hpOn=v[2]>0;lpOn=v[3]<100;
  auto hz=[](uint8_t x){return 13.75*std::pow(22351/13.75,x*.01);};
  hp.highpass(TwoPi*hz(v[2])/Rate,.707);lp.lowpass2B(TwoPi*hz(v[3])/Rate,.707);
  pitchRatio=std::pow(2.f,(v[4]-50)/12.f);
  ringTarget=v[5]*.01f;widthTarget=(v[6]*2-100)*.01f;
  if(first){ring=ringTarget;width=widthTarget;first=false;}
 }
 void clear()override{
  hp.clear();lp.clear();
  for(int c=0;c<2;c++){length[c]=lengthTarget[c]=lengthSmooth[c]=100;lastval[c]=0;envV[c]=0;firstThresh[c]=true;}
  osc[0].setRate(0);osc[1].setRate(0);osc[0].setPhase(0);osc[1].setPhase(NativePi/2);
 }
 void process(float*io,int frames)override{
  constexpr float smallestWavelength=16;
  // Upstream steps the glide blockSize*48000/sampleRate times per block, before the block.
  float k=std::exp(logSpeed*float(frames*48000/Rate));
  for(int c=0;c<2;c++)lengthSmooth[c]=lengthTarget[c]+(lengthSmooth[c]-lengthTarget[c])*k;
  for(int c=0;c<2;c++)osc[c].setRate(float(TwoPi/std::max(2.f,lengthSmooth[c]))*pitchRatio);
  float ringStep=(ringTarget-ring)/frames,widthStep=(widthTarget-width)/frames;
  for(int n=0;n<frames;n++){
   float in[2]={io[n*2],io[n*2+1]},det[2]={in[0],in[1]};
   for(int c=0;c<2;c++){
    if(hpOn)det[c]=hp.process(c,det[c]);
    if(lpOn)det[c]=lp.process(c,det[c]);
    double e=envV[c],x=in[c];e=x>e?envA*(e-x)+x:envR*(e-x)+x;envV[c]=std::fabs(e)<1e-20?0.f:float(e);
    if(lastval[c]<0&&det[c]>=0){   // positive zero crossing
     if(det[c]>thres&&length[c]>smallestWavelength){
      lengthTarget[c]=length[c]>lengthSmooth[c]*10?lengthSmooth[c]:length[c];
      if(firstThresh[c])lengthSmooth[c]=length[c];
      firstThresh[c]=false;
     }
     length[c]=0;
    }
    osc[c].process();
   }
   ring+=ringStep;width+=widthStep;
   float out[2];
   for(int c=0;c<2;c++){
    float sine=osc[c].r,scaled=sine*envV[0],ringed=sine*in[c];   // upstream scales both by the left envelope
    out[c]=scaled*(1-ring)+ringed*ring;
    length[c]+=1;lastval[c]=det[c];
   }
   float m=.5f*(out[0]+out[1]),s=.5f*(out[0]-out[1])*width;
   io[n*2]=nativeClean(m+s);io[n*2+1]=nativeClean(m-s);
  }
  ring=ringTarget;width=widthTarget;
 }
};
}
const NativeInfo treemonsterInfo{FxType::Treemonster,"TREEMONSTER","TREE","TX",2,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeTreemonster(){return std::make_unique<TreemonsterFx>();}
}
