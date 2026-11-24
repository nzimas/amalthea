// SPDX-License-Identifier: GPL-3.0-only
// TALKBOX: a one-to-one recreation of mda TalkBox, Copyright (c) 1999-2000, 2008 Paul Kellett
// (maxim digital audio), MIT licence (https://github.com/elk-audio/mda-vst2).
// An LPC talkbox: the voice is pre-emphasised, cut into 50%-overlapping Hanning frames
// at half the sample rate, analysed by autocorrelation and Levinson-Durbin, and its
// reflection coefficients drive a lattice filter over the carrier; the halves are
// half-band filtered on the way down and up exactly as in the original.
// Changed: the input (L+R) is always the voice, and the carrier, which was the other
// input channel, is internal: a band-limited saw or pulse chord (or drone) on ROOT, or
// noise, that steps through a chord MOVE at the tempo-synced CHANGE division and realigns
// on transport Start. The carrier channel swap is replaced by those carrier parameters.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
namespace {
const char*const carriers[]={"SAW","PULSE","NOISE"};
const char*const roots[]={"C2","C#2","D2","D#2","E2","F2","F#2","G2","G#2","A2","A#2","B2",
 "C3","C#3","D3","D#3","E3","F3","F#3","G3","G#3","A3","A#3","B3"};
const char*const chords[]={"DRONE","FIFTH","MAJOR","MINOR","SUS4","MAJ7","MIN7","DOM7"};
const int chordNotes[8][4]={{0,12,12,0},{0,7,12,0},{0,4,7,12},{0,3,7,12},{0,5,7,12},{0,4,7,11},{0,3,7,10},{0,4,7,10}};
const int chordCount[8]={2,3,4,4,4,4,4,4};
const char*const moves[]={"HOLD","I-IV","I-V","I-BVII","I-BVI-BVII","I-VI-IV-V"};
const int moveSteps[6][4]={{0},{0,5},{0,7},{0,-2},{0,-4,-2,-2},{0,-3,5,7}};
const int moveCount[6]={1,2,2,2,4,4};
const NativeParam params[]={
 {"WET",50,NativeUnit::Percent,35,70},
 {"DRY",0,NativeUnit::Percent,0,20},
 {"QUALITY",100,NativeUnit::Percent,50,100},
 {"CARRIER",0,NativeUnit::Choice,0,50,carriers,3},
 {"ROOT",nativeChoiceValue(12,24),NativeUnit::Choice,0,100,roots,24},
 {"CHORD",nativeChoiceValue(3,8),NativeUnit::Choice,0,100,chords,8},
 {"MOVE",nativeChoiceValue(4,6),NativeUnit::Choice,0,100,moves,6},
 {"CHANGE",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(0),nativeDivisionValue(4)},
 {"DETUNE",30,NativeUnit::Percent,0,60},
};
// The internal carrier: up to four chord notes of two detuned band-limited oscillators.
struct Carrier {
 int wave=0,root=12,chord=3,move=4,division=2;float detune=.3f;
 double spb=22050,position=0;uint32_t seed=0x7a1c0b0e;
 std::array<double,8> phase{};std::array<double,8> inc{};int voices=8,step=-1;
 static float blep(double t,double dt){if(t<dt){double x=t/dt;return float(x+x-x*x-1);}if(t>1-dt){double x=(t-1)/dt;return float(x*x+x+x+1);}return 0;}
 void tune(int s){
  step=s;int offset=moveSteps[move][s%moveCount[move]],n=chordCount[chord];voices=n*2;
  for(int v=0;v<n;v++){double midi=36+root+offset+chordNotes[chord][v],cents=detune*25;
   for(int k=0;k<2;k++){double hz=440*std::pow(2.0,(midi-69+(k?cents:-cents)*.01)/12);inc[size_t(v*2+k)]=std::min(hz/44100,.45);}}
 }
 void reset(){position=0;step=-1;}
 float next(){
  double stepLength=std::max(64.0,nativeDivisionBeats(division)*spb);
  int s=int(position/stepLength)%moveCount[move];if(s!=step)tune(s);
  position+=1;if(position>=stepLength*moveCount[move])position-=stepLength*moveCount[move];
  if(wave==2)return (nativeUnit(seed)*2-1)*.6f;
  float sum=0;
  for(int v=0;v<voices;v++){double&t=phase[size_t(v)];double dt=inc[size_t(v)];
   if(wave==0)sum+=float(2*t-1)-blep(t,dt);
   else{double h=t+.5;if(h>=1)h-=1;sum+=(t<.5?1.f:-1.f)+blep(t,dt)-blep(h,dt);}
   t+=dt;if(t>=1)t-=1;}
  return sum*(.6f/std::sqrt(float(voices)));
 }
};
constexpr int BufMax=1600,OrdMax=50;
class TalkBoxFx final:public NativeFx{
 std::array<float,BufMax> car0{},car1{},window{},buf0{},buf1{};
 float emphasis=0,wet=.125f,dry=0,FX=0;int K=0,N=1,O=22,pos=0;
 float d0=0,d1=0,d2=0,d3=0,d4=0,u0=0,u1=0,u2=0,u3=0,u4=0;
 Carrier carrier;uint32_t epoch=0;
 void lpc(float*buf,const float*car,int n,int o){
  float z[OrdMax],r[OrdMax],k[OrdMax],G,x;int nn=n;
  for(int j=0;j<=o;j++,nn--){z[j]=r[j]=0;for(int i=0;i<nn;i++)r[j]+=buf[i]*buf[i+j];}   // autocorrelation
  r[0]*=1.001f;   // stability fix
  if(r[0]<.00001f){for(int i=0;i<n;i++)buf[i]=0;return;}
  durbin(r,o,k,&G);
  for(int i=0;i<=o;i++)k[i]=std::clamp(k[i],-.995f,.995f);
  for(int i=0;i<n;i++){x=G*car[i];for(int j=o;j>0;j--){x-=k[j]*z[j-1];z[j]=z[j-1]+k[j]*x;}buf[i]=z[0]=x;}   // lattice
 }
 static void durbin(const float*r,int p,float*k,float*g){
  float a[OrdMax],at[OrdMax],e=r[0];
  for(int i=0;i<=p;i++)a[i]=at[i]=k[i]=0;
  for(int i=1;i<=p;i++){
   k[i]=-r[i];
   for(int j=1;j<i;j++){at[j]=a[j];k[i]-=a[j]*r[i-j];}
   if(std::fabs(e)<1e-20f){e=0;break;}
   k[i]/=e;a[i]=k[i];
   for(int j=1;j<i;j++)a[j]=at[j]+k[i]*at[i-j];
   e*=1-k[i]*k[i];
  }
  if(e<1e-20f)e=0;*g=std::sqrt(e);
 }
public:
 TalkBoxFx(){
  N=int(.01633f*44100);if(N>BufMax)N=BufMax;
  float dp=2*NativePi/float(N),p=0;for(int n=0;n<N;n++){window[size_t(n)]=.5f-.5f*std::cos(p);p+=dp;}
 }
 void configure(const uint8_t*v,float bpm)override{
  float w=v[0]*.01f,d=v[1]*.01f,q=v[2]*.01f;
  wet=.5f*w*w;dry=2*d*d;O=int((.0001f+.0004f*q)*44100);
  carrier.spb=nativeSamplesPerBeat(bpm);
  int wave=nativeChoice(v[3],3),root=nativeChoice(v[4],24),chord=nativeChoice(v[5],8),move=nativeChoice(v[6],6);float detune=v[8]*.01f;
  if(root!=carrier.root||chord!=carrier.chord||move!=carrier.move||detune!=carrier.detune)carrier.step=-1;
  carrier.wave=wave;carrier.root=root;carrier.chord=chord;carrier.move=move;carrier.detune=detune;carrier.division=nativeDivision(v[7]);
 }
 void clear()override{
  pos=K=0;emphasis=0;FX=0;u0=u1=u2=u3=u4=0;d0=d1=d2=d3=d4=0;
  buf0.fill(0);buf1.fill(0);car0.fill(0);car1.fill(0);carrier.reset();
 }
 void process(float*io,int frames)override{
  auto ep=nativeTransportEpoch();if(ep!=epoch){epoch=ep;carrier.reset();}   // downbeat
  int p0=pos,p1=(pos+N/2)%N;float e=emphasis,w,o,x,dr,fx=FX,p,q;const float h0=.3f,h1=.77f;
  for(int i=0;i<frames;i++){
   o=.5f*(io[i*2]+io[i*2+1]);x=carrier.next();dr=o;
   p=d0+h0*x;d0=d1;d1=x-h0*p;q=d2+h1*d4;d2=d3;d3=d4-h1*q;d4=x;x=p+q;   // half-band down
   if(K++){
    K=0;car0[size_t(p0)]=car1[size_t(p1)]=x;   // carrier input
    x=o-e;e=o;   // 6 dB/oct pre-emphasis
    w=window[size_t(p0)];fx=buf0[size_t(p0)]*w;buf0[size_t(p0)]=x*w;   // 50% overlapping Hanning windows
    if(++p0>=N){lpc(buf0.data(),car0.data(),N,O);p0=0;}
    w=1-w;fx+=buf1[size_t(p1)]*w;buf1[size_t(p1)]=x*w;
    if(++p1>=N){lpc(buf1.data(),car1.data(),N,O);p1=0;}
   }
   p=u0+h0*fx;u0=u1;u1=fx-h0*p;q=u2+h1*u4;u2=u3;u3=u4-h1*q;u4=fx;x=p+q;   // half-band up
   o=nativeClean(wet*x+dry*dr);io[i*2]=io[i*2+1]=o;
  }
  emphasis=e;pos=p0;FX=fx;
  const float den=1e-10f;
  for(float*s:{&d0,&d1,&d2,&d3,&u0,&u1,&u2,&u3})if(std::fabs(*s)<den)*s=0;   // anti-denormal
 }
};
}
const NativeInfo talkBoxInfo{FxType::TalkBox,"TALKBOX","TALKBX","TK",2,0,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeTalkBox(){return std::make_unique<TalkBoxFx>();}
}
