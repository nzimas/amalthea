// SPDX-License-Identifier: GPL-3.0-only
// COMBULATOR: a one-to-one recreation of the Surge XT Combulator effect, Copyright
// 2018-2024 various authors (Surge Synth Team), GPL-3.0-or-later; Surge was (C) 2004-2018
// Claes Johanson / Vember Audio (https://github.com/surge-synthesizer/surge), with its
// comb filter, sinc table and half-band filters from sst-filters and sst-basic-blocks
// (Surge Synth Team, GPL-3.0-or-later).
// Three tuned feedback combs run at 2x oversampling (Surge's 12-pole steep half-band
// up/down filters, including their -6 dB up-sampling gain): each comb reads its delay
// line through the 12-tap windowed-sinc interpolator, soft-clips the feedback sum and
// outputs the delayed signal only. Comb 2 and 3 are tuned as offsets from the centre
// and panned with Surge's pan law; EXTRA NOISE adds white noise scaled by the (signed)
// 5 ms envelope follower of the input; the sum is tanh-shaped (Surge's 1024-point
// table) and then TONE sets a low-pass (below 0) or high-pass (above 0) biquad.
// Parameters glide through Surge's one-pole lags and the comb coefficients through its
// per-block coefficient smoothing. Changes: the delay lines are 8192 samples (lowest
// comb about 11 Hz instead of 0.7 Hz) to stay well under 3 MB; feedback is limited to
// +-97% so the combs never sustain on their own; the absolute-frequency mode for combs
// 2 and 3, the tone "deactivate" switch and the tuning-table support are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const NativeParam params[]={
 {"EXTRA NOISE",0,NativeUnit::Percent,0,30},
 {"CENTER",50,NativeUnit::Range,20,75,nullptr,0,32.703f,2093.f,true," Hz"},
 {"OFFSET 2",51,NativeUnit::Range,25,75,nullptr,0,-12,12,false," st"},
 {"OFFSET 3",49,NativeUnit::Range,25,75,nullptr,0,-12,12,false," st"},
 {"FEEDBACK",88,NativeUnit::Bipolar,15,97},
 {"TONE",50,NativeUnit::Bipolar,25,75},
 {"COMB 1",79,NativeUnit::Percent,50,100},
 {"COMB 2",79,NativeUnit::Percent,40,100},
 {"COMB 3",79,NativeUnit::Percent,40,100},
 {"PAN 2",62,NativeUnit::Bipolar,10,90},
 {"PAN 3",38,NativeUnit::Bipolar,10,90},
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
 // Up: input sample -> two output samples (gain 1/2, as Surge's process_block_U2).
 void up(int c,float x,float out[2]){
  float s[2]={x,0};
  for(int k=0;k<2;k++){float ya=a[c].run(A,s[k]),yb=b[c].run(B,s[k]);out[k]=.5f*(ya+oldB[c]);oldB[c]=yb;}
 }
 // Down: two input samples -> one output sample.
 float down(int c,const float in[2]){
  float b0=b[c].run(B,in[0]);a[c].run(A,in[0]);b[c].run(B,in[1]);float a1=a[c].run(A,in[1]);
  return .5f*(b0+a1);
 }
};
constexpr float HalfBand::A[];constexpr float HalfBand::B[];
constexpr int CombSize=8192,SincN=12,SincOffset=6;
class CombulatorFx final:public NativeFx{
 // Parameters.
 float noiseTarget=0,freqTarget[3]={-9,-9,-9},gainTarget[3]={.5f,.5f,.5f},fbTarget=.75f,toneTarget=0,pan2Target=.25f,pan3Target=-.25f;
 Lag freq[3],gain[3],feedback,tone,pan2,pan3,noisemix;
 // Comb state.
 std::vector<float> line;   // [3][2][CombSize+SincN]
 int wp[3][2]={};
 float C0[3],C1[3],tC0[3],tC1[3];bool coeffFirst=true;
 std::vector<float> sinc,panL,panR,soft;
 HalfBand hbIn,hbOut;Biquad lp[2],hp[2];
 float envA,envV[2]={0,0},noiseGen[2][2]={};uint32_t seed=0xc0b0a7e5;bool configured=false;
 float*delay(int e,int c){return line.data()+size_t(e*2+c)*(CombSize+SincN);}
 float lookupSoft(float x)const{x=x*32+512;int e=int(x);float a=x-float(e);if(e>0x3fd)return soft[1023];if(e<1)return soft[0];return (1-a)*soft[e]+a*soft[e+1];}
 static float softclip(float x){x=std::clamp(x,-1.5f,1.5f);return x-(4.f/27.f)*x*x*x;}
 static float combDelay(float note){return std::clamp(88200.f/(440.f*std::exp2(note/12.f)),float(SincN),float(CombSize-SincN));}
public:
 CombulatorFx():line(size_t(6)*(CombSize+SincN),0.f),sinc(size_t(257)*SincN),panL(4096),panR(4096),soft(1024){
  // sst-filters SincTable (first half of each row: the 12 taps; cutoff 0.455).
  auto blackman=[](double i,int n){i-=n/2;return .42-.5*std::cos(2*M_PI*i/n)+.08*std::cos(4*M_PI*i/n);};
  auto sincf=[](double x){return x==0?1.0:std::sin(M_PI*x)/(M_PI*x);};
  for(int j=0;j<=256;j++)for(int i=0;i<SincN;i++){double t=-double(i)+SincN/2.0+j/256.0-1.0;sinc[size_t(j)*SincN+i]=float(blackman(t,SincN)*.455*sincf(.455*t));}
  for(int i=0;i<4096;i++){double h=M_PI/2,p=double(i)/4095*h;panL[i]=float(std::sqrt((h-p)/h*std::cos(p)));panR[i]=float(std::sqrt(p*std::sin(p)/h));}
  for(int i=0;i<1024;i++)soft[i]=float(std::tanh((i-512.0)/32.0));
  envA=float(std::pow(.01,1.0/(5*88200*.001)));
 }
 void configure(const uint8_t*v,float)override{
  noiseTarget=v[0]*.01f;
  float hz=32.703f*std::pow(2093.f/32.703f,v[1]*.01f);float center=12*std::log2(hz/440.f);
  freqTarget[0]=center;freqTarget[1]=center+(v[2]*.24f-12);freqTarget[2]=center+(v[3]*.24f-12);
  fbTarget=std::clamp((v[4]*2-100)*.01f,-.97f,.97f);toneTarget=(v[5]*2-100)*.01f;
  for(int i=0;i<3;i++){float g=v[6+i]*.01f;gainTarget[i]=g*g*g;}
  pan2Target=(v[9]*2-100)*.01f;pan3Target=(v[10]*2-100)*.01f;
  configured=true;
 }
 void clear()override{
  std::fill(line.begin(),line.end(),0.f);for(auto&w:wp)w[0]=w[1]=0;coeffFirst=true;
  hbIn.reset();hbOut.reset();for(int c=0;c<2;c++){lp[c].clear();hp[c].clear();}
  envV[0]=envV[1]=0;for(auto&n:noiseGen)n[0]=n[1]=0;
  for(Lag*l:{&freq[0],&freq[1],&freq[2],&gain[0],&gain[1],&gain[2],&feedback,&tone,&pan2,&pan3,&noisemix})l->first=true;
 }
 void process(float*io,int frames)override{
  if(!configured||frames<=0)return;
  // setvars(false)
  for(int i=0;i<3;i++){freq[i].set(freqTarget[i]);gain[i].set(gainTarget[i]);}
  noisemix.set(noiseTarget);feedback.set(fbTarget);tone.set(toneTarget);pan2.set(pan2Target);pan3.set(pan3Target);
  {float clo=-12,cmid=67,chi=-33,hpCut=chi,lpCut=cmid;
   if(tone.v>0)hpCut=tone.v*(cmid-chi)+chi;else lpCut=-tone.v*(clo-cmid)+cmid;
   for(int c=0;c<2;c++){lp[c].set(0,110*std::exp2(lpCut/12),.707f);hp[c].set(1,110*std::exp2(hpCut/12),.707f);}}
  // Comb coefficients (FilterCoefficientMaker::FromDirect smoothing, ramped over the block).
  float fbs=(feedback.v<0?-1.f:1.f)*std::sqrt(std::fabs(feedback.v));
  int os=frames*2;float inv=1.f/os,dC0[3],dC1[3];
  for(int e=0;e<3;e++){float n0=combDelay(freq[e].v),n1=std::clamp(fbs,-2.f,2.f);
   if(coeffFirst){C0[e]=tC0[e]=n0;C1[e]=tC1[e]=n1;dC0[e]=dC1[e]=0;}
   else{tC0[e]=.8f*tC0[e]+.2f*n0;tC1[e]=.8f*tC1[e]+.2f*n1;dC0[e]=(tC0[e]-C0[e])*inv;dC1[e]=(tC1[e]-C1[e])*inv;}}
  coeffFirst=false;
  for(int f=0;f<frames;f++){
   float up[2][2];hbIn.up(0,io[f*2],up[0]);hbIn.up(1,io[f*2+1],up[1]);
   float outOS[2][2];
   for(int s=0;s<2;s++){
    float noise[2];
    for(int c=0;c<2;c++){float x=up[c][s],e=envV[c];e=x>e?envA*(e-x)+x:envA*(e-x)+x;envV[c]=e;
     // correlated_noise_o2mk2 with zero correlation is plain white noise.
     float r=nativeUnit(seed)*2-1;noiseGen[c][1]=r;noiseGen[c][0]=r;noise[c]=noisemix.v*3.f*envV[c]*r;}
    int p2=int((std::clamp(pan2.v,-1.f,1.f)+1)*2047)&4095,p3=int((std::clamp(pan3.v,-1.f,1.f)+1)*2047)&4095;
    float t[3][2];
    for(int e=0;e<3;e++){C0[e]+=dC0[e];C1[e]+=dC1[e];
     long q=std::lrint(C0[e]*256);int dt=int(q>>8),se=255-int(q&255);const float*k=&sinc[size_t(se)*SincN];
     for(int c=0;c<2;c++){float*db=delay(e,c);int rp=(wp[e][c]-dt-SincOffset)&(CombSize-1);
      float read=0;for(int i=0;i<SincN;i++)read+=db[rp+i]*k[i];
      float d=softclip(up[c][s]+noise[c]+read*C1[e]);db[wp[e][c]]=d;if(wp[e][c]<SincN)db[wp[e][c]+CombSize]=d;
      wp[e][c]=(wp[e][c]+1)&(CombSize-1);t[e][c]=read;}}
    float ml=0,mr=0;
    for(int i=0;i<3;i++){float pl=.59f,pr=.59f;if(i==1){pl=panL[p2];pr=panR[p2];}else if(i==2){pl=panL[p3];pr=panR[p3];}
     ml+=t[i][0]*gain[i].v*pl/.59f;mr+=t[i][1]*gain[i].v*pr/.59f;}
    outOS[0][s]=lookupSoft(ml);outOS[1][s]=lookupSoft(mr);
    if(s==0){for(int i=0;i<3;i++){freq[i].process();gain[i].process();}feedback.process();pan2.process();pan3.process();tone.process();noisemix.process();}
   }
   for(int c=0;c<2;c++){float y=hbOut.down(c,outOS[c]);y=hp[c].process(lp[c].process(y));io[f*2+c]=nativeClean(y);}
  }
 }
};
}
const NativeInfo combulatorInfo{FxType::Combulator,"COMBULATOR","COMBUL","CO",2,1,5,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeCombulator(){return std::make_unique<CombulatorFx>();}
}
