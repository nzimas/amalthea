// SPDX-License-Identifier: GPL-3.0-only
// FREQ SHIFTER: a one-to-one recreation of the Signalsmith Basics Frequency Shifter,
// Copyright 2025+ Signalsmith Audio Ltd. / Geraint Luff, MIT License
// (https://github.com/Signalsmith-Audio/basics), built on Signalsmith's Hilbert IIR,
// Copyright (c) 2024 Geraint Luff / Signalsmith Audio Ltd., 0BSD
// (https://github.com/Signalsmith-Audio/hilbert-iir).
// A Hilbert / Bode single-sideband modulator: a 12-pole complex IIR (the published
// coefficients and poles, scaled for 44.1 kHz with its 20 kHz lowpass) makes the
// analytic signal, which is rotated by a complex oscillator. REFLECT is the plugin's
// 0 Hz mode: a downward shift either drops what crosses 0 Hz (rotate before the
// Hilbert) or reflects it (rotate after), and an upward shift may duplicate the
// negative-frequency image. The plugin's MIX is left to the host's MIX, and its
// per-block linear smoothing became a one-pole glide per 32-frame block.
// Added here: the plugin's SHIFT curve became a magnitude (1..2000 Hz, exponential)
// with SIDEBAND choosing up, down or both (both sums the up and down shifts, a ring
// modulator without the carrier), FINE, an opposite L/R STEREO offset, a feedback
// path through a tempo-synced delay whose gain is limited so tails fall 60 dB within
// about 10 s, and a tempo-synced sweep of the shift (ramps give barber-pole sweeps)
// that realigns on transport Start. GUI and preset code are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
namespace {
const char*const sidebands[]={"UP","DOWN","BOTH"};
const char*const reflectModes[]={"NONE","REFLECT","DUPLICATE","BOTH"};
const char*const sweepShapes[]={"SINE","TRIANGLE","RAMP UP","RAMP DOWN"};
const NativeParam params[]={
 {"SHIFT",51,NativeUnit::Range,20,75,nullptr,0,1,2000,true," Hz"},
 {"FINE",50,NativeUnit::Range,35,65,nullptr,0,-10,10,false," Hz"},
 {"SIDEBAND",0,NativeUnit::Choice,0,100,sidebands,3},
 {"REFLECT",0,NativeUnit::Choice,0,100,reflectModes,4},
 {"FEEDBACK",0,NativeUnit::Percent,0,60},
 {"FB TIME",nativeDivisionValue(10),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(15)},
 {"STEREO",0,NativeUnit::Range,0,30,nullptr,0,0,20,false," Hz"},
 {"SWEEP RATE",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(0),nativeDivisionValue(8)},
 {"SWEEP DEPTH",0,NativeUnit::Range,0,30,nullptr,0,0,500,false," Hz"},
 {"SWEEP SHAPE",nativeChoiceValue(2,4),NativeUnit::Choice,0,100,sweepShapes,4},
};
constexpr int Order=12;
constexpr double Rate=44100,TwoPi=6.283185307179586;
constexpr int DelayFrames=4*44100;   // 4 s of feedback delay (1.4 MB stereo)

// Signalsmith Hilbert IIR (design.py output), complex-input form, unwrapped as upstream.
struct Hilbert {
 std::array<float,Order> cR,cI,pR,pI,sR{},sI{};float direct=0;
 Hilbert(){
  static const double coeffs[Order][2]={
   {-0.000224352093802,0.00543499018201},{0.0107500557815,-0.0173890685681},{-0.0456795873917,0.0229166931429},
   {0.11282500582,0.00278413661237},{-0.208067578452,-0.104628958675},{0.28717837501,0.33619239719},
   {-0.254675294431,-0.683033899655},{0.0481081835026,0.954061589374},{0.227861357867,-0.891273574569},
   {-0.365411839137,0.525088317271},{0.280729061131,-0.155131206606},{-0.0935061787728,0.00512245855404}};
  static const double poles[Order][2]={
   {-0.00495335976478,0.0092579876872},{-0.017859491302,0.0273493725543},{-0.0413714373155,0.0744756910287},
   {-0.0882148408885,0.178349677457},{-0.17922965812,0.39601340223},{-0.338261800753,0.829229533354},
   {-0.557688699732,1.61298538328},{-0.735157736148,2.79987398682},{-0.719057381172,4.16396166128},
   {-0.517871025209,5.29724826804},{-0.280197469471,5.99598602388},{-0.0852751354531,6.3048492377}};
  const double passbandGain=2,freqFactor=std::min(0.46,20000/Rate);
  direct=float(0.000262057212648*2*passbandGain*freqFactor);
  for(int i=0;i<Order;i++){
   cR[i]=float(coeffs[i][0]*freqFactor*passbandGain);cI[i]=float(coeffs[i][1]*freqFactor*passbandGain);
   double m=std::exp(poles[i][0]*freqFactor),a=poles[i][1]*freqFactor;pR[i]=float(m*std::cos(a));pI[i]=float(m*std::sin(a));
  }
 }
 void clear(){sR.fill(0);sI.fill(0);}
 void process(float xr,float xi,float&yr,float&yi){
  float r=xr*direct,im=xi*direct;
  for(int i=0;i<Order;i++){
   float nr=sR[i]*pR[i]-sI[i]*pI[i]+xr*cR[i]-xi*cI[i];
   float ni=sR[i]*pI[i]+sI[i]*pR[i]+xr*cI[i]+xi*cR[i];
   sR[i]=std::fabs(nr)<1e-25f?0:nr;sI[i]=std::fabs(ni)<1e-25f?0:ni;r+=sR[i];im+=sI[i];
  }
  yr=r;yi=im;
 }
};
// One channel of the Basics shifter: rotate before or after the Hilbert by the sign of the step.
struct Shifter {
 Hilbert hilbert;double phaseBefore=0,phaseAfter=0;
 float bR=1,bI=0,aR=1,aI=0;   // shiftBefore, shiftAfter
 void clear(){hilbert.clear();phaseBefore=phaseAfter=0;bR=1;bI=0;aR=1;aI=0;}
 // Renders `frames` samples of x with a constant step (cycles per sample) this block.
 void run(const float*x,float*out,int frames,double step,bool reflectDown,bool duplicateUp,float gain){
  bool before=step<0?!reflectDown:duplicateUp;
  double&phase=before?phaseBefore:phaseAfter;
  float sr=float(std::cos(TwoPi*step)),si=float(std::sin(TwoPi*step));
  float&rR=before?bR:aR,&rI=before?bI:aI;
  for(int i=0;i<frames;i++){
   float yr,yi;hilbert.process(x[i]*bR,x[i]*bI,yr,yi);
   out[i]+=gain*(aR*yr-aI*yi);
   float nr=rR*sr-rI*si,ni=rR*si+rI*sr;rR=nr;rI=ni;   // the next sample's rotor
  }
  phase+=step*frames;phase-=std::floor(phase);
  rR=float(std::cos(TwoPi*phase));rI=float(std::sin(TwoPi*phase));   // exact again at the block edge
 }
};
class FreqShifterFx final:public NativeFx{
 Shifter up[2],down[2];History delay{DelayFrames};
 int sideband=0,reflect=0,shape=2;float shiftHz=50,stereoHz=0,depthHz=0,fbGain=0;
 float glide[2]={-1,-1};   // current smoothed shift magnitude per channel (-1: not started)
 double sweepLength=88200,sweepPos=0;int delayLength=11025;uint32_t epoch=0;
public:
 FreqShifterFx(){delay.clear();}
 void configure(const uint8_t*v,float bpm)override{
  double spb=nativeSamplesPerBeat(bpm);
  shiftHz=float(std::pow(2000.0,v[0]*.01))+(v[1]*.2f-10);
  sideband=nativeChoice(v[2],3);reflect=nativeChoice(v[3],4);
  double length=std::clamp(nativeDivisionBeats(nativeDivision(v[5]))*spb,32.0,double(DelayFrames-2));delayLength=int(length);
  // FEEDBACK up to 0.9 per pass, limited so the loop falls 60 dB in about 10 s at long times.
  fbGain=std::min(.9f*v[4]*.01f,float(std::pow(.001,length/Rate/10)));
  stereoHz=v[6]*.2f;sweepLength=std::max(32.0,nativeDivisionBeats(nativeDivision(v[7]))*spb);
  depthHz=v[8]*5.f;shape=nativeChoice(v[9],4);
 }
 void clear()override{for(auto&s:up)s.clear();for(auto&s:down)s.clear();delay.clear();glide[0]=glide[1]=-1;sweepPos=0;}
 float sweep(){
  double u=std::fmod(sweepPos,sweepLength)/sweepLength;
  switch(shape){
   case 0:return float(.5-.5*std::cos(TwoPi*u));
   case 1:return float(u<.5?2*u:2-2*u);
   case 2:return float(u);
   default:return float(1-u);
  }
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;sweepPos=0;}   // downbeat
  frames=std::min(frames,32);
  float target=shiftHz+depthHz*sweep();sweepPos=std::fmod(sweepPos+frames,sweepLength);
  bool reflectDown=reflect==1||reflect==3,duplicateUp=reflect==2||reflect==3;
  float in[2][32],out[2][32]={};
  for(int i=0;i<frames;i++)for(int c=0;c<2;c++){
   // Feedback: the delayed wet signal, softly bounded, re-enters the shifter.
   float fb=fbGain>0?fbGain*std::tanh(delay.read(c,delayLength-1-i)):0;in[c][i]=io[i*2+c]+fb;
  }
  float gain=sideband==2?.70710678f:1;
  for(int c=0;c<2;c++){
   float hz=target+(c?-stereoHz:stereoHz);
   glide[c]=glide[c]<0?hz:glide[c]+(hz-glide[c])*.15f;
   double step=std::clamp(double(glide[c]),-2500.0,2500.0)/Rate;
   if(sideband!=1)up[c].run(in[c],out[c],frames,step,reflectDown,duplicateUp,gain);
   if(sideband!=0)down[c].run(in[c],out[c],frames,-step,reflectDown,duplicateUp,gain);
  }
  for(int i=0;i<frames;i++){
   float l=nativeClean(out[0][i]),r=nativeClean(out[1][i]);
   delay.write(l,r);io[i*2]=l;io[i*2+1]=r;
  }
 }
};
}
const NativeInfo freqShifterInfo{FxType::FreqShifter,"FREQ SHIFTER","FSHIFT","FS",5,0,2,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeFreqShifter(){return std::make_unique<FreqShifterFx>();}
}
