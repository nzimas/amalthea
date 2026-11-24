// SPDX-License-Identifier: GPL-3.0-only
// OOPS: a native recreation of B.Oops, the glitch effect sequencer,
// Copyright (C) 2020-2021 by Sven Jähnichen, GPL-3.0-or-later (https://github.com/sjaehn/BOops).
// Its Galactic and Infinity reverbs and the Banger's XRegion filter are Airwindows code,
// Copyright (c) 2016-2018 Chris Johnson (airwindows), MIT licence, as adapted in B.Oops; REVERB is the ACE reverb,
// Copyright (C) 2003-2004 Fredrik Kilander, 2008-2016 Robin Gareus, 2012 Will Panther,
// 2016 Damien Zammit, GPL-2.0-or-later, as adapted in B.Oops.
// A tempo-synced step sequencer whose pads apply one of 30 B.Oops effects to the live
// input: the Fx classes are ported one to one (their init draws, per-pad randomization,
// ring-buffer reads with Hermite interpolation, attack/sustain/release pad envelope, slot pan
// and slot mix). Without B.Oops's GUI the pattern is drawn from a seed: each step may start
// a pad (DENSITY) spanning 1..PAD LENGTH steps, whose effect comes from the POOL (B.Oops's own
// menu groups) and whose effect settings are seeded within musical ranges; VARIATION is the
// amount of every effect's own per-pad random parameter. MUTATE redraws steps at each cycle
// end; LENGTH restarts the pattern on the bar. Changes: one pad plays at a time (one shared
// history instead of chained slots, so JUMBLE picks among all pads); the drawn shapes of
// SCRATCH, WAH and WAVESHAPER are seeded six-node curves; the Airwindows reverbs use float
// delay lines without dither or denormal injection, and XRegion updates its coefficients every
// fourth sample; VARIATION cannot push an output level above its seeded range; the history holds about 4.5 s, so reads
// further back are clamped; after five seconds of silent input the output is silent and the
// reverb lines are cleared. Omitted: the Oops sample player (needs its WAV file), Surprise
// box (the seeded pattern replaces it), Pitch and Tape start (unfinished in B.Oops), shape
// and MIDI-key slot modes, sample source, page changes. Pattern realigns on transport Start.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace amalthea {
namespace {
const char*const poolNames[]={"ALL","CUTS","PATTERN","TEMPORAL","TAPE","VINYL","DESTROY","FILTER","MODULATE","BASIC"};
const char*const stepNames[]={"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"};
const NativeParam params[]={
 {"STEP",nativeDivisionValue(12),NativeUnit::Division,nativeDivisionValue(10),nativeDivisionValue(14)},
 {"STEPS",nativeChoiceValue(15,16),NativeUnit::Choice,nativeChoiceValue(7,16),100,stepNames,16},
 {"LENGTH",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(1),nativeDivisionValue(2)},
 {"PATTERN",0,NativeUnit::Percent,0,100},
 {"DENSITY",50,NativeUnit::Percent,25,75},
 {"POOL",nativeChoiceValue(1,10),NativeUnit::Choice,0,100,poolNames,10},
 {"PAD LENGTH",nativeChoiceValue(1,8),NativeUnit::Choice,0,nativeChoiceValue(3,8),stepNames,8},
 {"VARIATION",25,NativeUnit::Percent,0,60},
 {"MUTATE",0,NativeUnit::Percent,0,40},
 {"CHANCE",100,NativeUnit::Percent,60,100,nullptr,0,0,1,false,"% CHANCE"},
 {"FADE",40,NativeUnit::Range,10,80,nullptr,0,0,50,false,"% STEP"},
 {"PAN SPREAD",0,NativeUnit::Percent,0,60},
};
constexpr double Rate=44100;
constexpr double Pi=3.14159265358979323846;
template<typename T> T lim(T x,T lo,T hi){return x>hi?hi:(x<lo?lo:x);}
inline double db2co(double x){return std::pow(10.0,.05*x);}
inline float sgn(double x){return float((0<x)-(x<0));}

struct S{float l=0,r=0;
 S()=default;S(float a,float b):l(a),r(b){}
 S operator+(S o)const{return {l+o.l,r+o.r};}S operator-(S o)const{return {l-o.l,r-o.r};}
 S operator*(S o)const{return {l*o.l,r*o.r};}S operator*(float f)const{return {l*f,r*f};}
 S&operator+=(S o){l+=o.l;r+=o.r;return *this;}
};
// BUtilities::mix: t1*ratio + t0*(1-ratio).
inline S mixS(S a,S b,float f){return b*f+a*(1-f);}

// B.Oops's RingBuffer<Stereo>: [0] is the newest frame, [n] n frames ago.
struct Ring{
 std::vector<float> d;long n,pos=0;
 explicit Ring(long frames):d(size_t(frames)*2,0.f),n(frames){}
 void push(S s){pos=pos?pos-1:n-1;d[size_t(pos)*2]=s.l;d[size_t(pos)*2+1]=s.r;}
 float*at(long k){k=std::clamp(k,0L,n-1);long i=pos+k;if(i>=n)i-=n;return &d[size_t(i)*2];}
 S get(long k){float*p=at(k);return {p[0],p[1]};}
 void clear(){std::fill(d.begin(),d.end(),0.f);}
 void setFront(S s){d[size_t(pos)*2]=s.l;d[size_t(pos)*2+1]=s.r;}
 // Fx::getSample: linear below one frame, else 4-point 3rd-order Hermite.
 S sample(double frame){
  frame=std::clamp(frame,0.0,double(n-4));const float x=float(std::fmod(frame,1.0));
  if(x==0)return get(long(frame));
  if(frame<1)return mixS(get(long(frame)),get(long(frame)+1),x);
  const long f=long(frame);S m=get(f-1),a=get(f),b=get(f+1),c=get(f+2);
  S c1=(b-m)*.5f,c2=m-a*2.5f+b*2.f-c*.5f,c3=(c-m)*.5f+(a-b)*1.5f;
  return ((c3*x+c2)*x+c1)*x+a;
 }
};

// ButterworthLow/HighPassFilter (cascaded sections, stereo).
struct Butter{
 int o2=0;float f1=2,c0[8]{},c1[8]{},c2[8]{};S b0[8],b1[8],b2[8];
 void clear(){for(int i=0;i<8;i++)b0[i]=b1[i]=b2[i]=S();}
 void set(bool high,double cutoff,int order,const float*rs){
  o2=std::clamp(order/2,0,8);f1=high?-2.f:2.f;const double a=std::tan(Pi*cutoff/Rate),a2=a*a;
  for(int i=0;i<o2;i++){const double r=rs[i],s=a2+2*a*r+1;c0[i]=float(high?1/s:a2/s);c1[i]=float(2*(1-a2)/s);c2[i]=float(-(a2-2*a*r+1)/s);}
 }
 S push(S out){for(int i=0;i<o2;i++){b0[i]=b1[i]*c1[i]+b2[i]*c2[i]+out;out=(b0[i]+b1[i]*f1+b2[i])*c0[i];b2[i]=b1[i];b1[i]=b0[i];}return out;}
};
// BiquadPeakFilter (earlevel), stereo.
struct Peak{
 float fc=.5f,q=.7071f,a0=1,a1=0,a2=0,b1=0,b2=0;S z1,z2;
 void build(float gain){
  const float V=std::pow(10.f,std::fabs(gain)/20),K=std::tan(float(Pi)*fc);float norm;
  if(gain>=0){norm=1/(1+1/q*K+K*K);a0=(1+V/q*K+K*K)*norm;a1=2*(K*K-1)*norm;a2=(1-V/q*K+K*K)*norm;b1=a1;b2=(1-1/q*K+K*K)*norm;}
  else{norm=1/(1+V/q*K+K*K);a0=(1+1/q*K+K*K)*norm;a1=2*(K*K-1)*norm;a2=(1-1/q*K+K*K)*norm;b1=a1;b2=(1-V/q*K+K*K)*norm;}
 }
 S process(S in){S out=in*a0+z1;z1=in*a1+z2-out*b1;z2=in*a2-out*b2;return out;}
};
// AllPassFilter (phaser stage).
struct AllPass{float f=1,m=0;void setDelay(float d){f=(1-d)/(1+d);}float process(float in){const float r=m-in*f;m=in+r*f;return r;}};

// ACE reverb (setBfree a_reverb, as AceReverb in B.Oops), wet only.
struct Ace{
 std::vector<float> line[2][7];int idx[2][7]{},endi[2][7]{};float gain[7]{},yy1[2]{},y1[2]{};
 const float inputGain=std::pow(10.f,.05f*-20.f),fbk=-.015f;
 Ace(){const int ends[7]={1687,1601,2053,2251,347,113,37};
  for(int c=0;c<2;c++)for(int i=0;i<7;i++){int e=int((ends[i]+c*7)*Rate/25000.0)|1;line[c][i].assign(size_t(e+2),0.f);endi[c][i]=e+1;}
  gain[4]=gain[5]=gain[6]=std::sqrt(.5f);setRoomSize(.75f);}
 void setRoomSize(float rs){gain[0]=.773f*rs;gain[1]=.802f*rs;gain[2]=.753f*rs;gain[3]=.733f*rs;}
 void clearState(){yy1[0]=yy1[1]=y1[0]=y1[1]=0;}
 S process(S in){
  float xo[2]={in.l,in.r},out[2];
  for(int c=0;c<2;c++){
   if(!std::isfinite(xo[c])||std::fabs(xo[c])>10.f)xo[c]=0;
   const float x=y1[c]+inputGain*xo[c];float xa=0,y;int j=0;
   for(;j<4;j++){float&cell=line[c][j][size_t(idx[c][j])];y=cell;cell=x+gain[j]*y;if(++idx[c][j]>=endi[c][j])idx[c][j]=0;xa+=y;}
   for(;j<7;j++){float&cell=line[c][j][size_t(idx[c][j])];y=cell;cell=gain[j]*(xa+y);if(++idx[c][j]>=endi[c][j])idx[c][j]=0;xa=y-xa;}
   y=.5f*(xa+yy1[c]);yy1[c]=y;y1[c]=fbk*xa;out[c]=y;   // wet 1, dry 0
   if(!std::isfinite(y1[c]))y1[c]=0;if(!std::isfinite(yy1[c]))yy1[c]=0;
  }
  return {out[0],out[1]};
 }
};

// Airwindows Galactic as adapted in B.Oops (44.1 kHz: one reverb sample per input sample).
struct Galactic{
 enum{I,J,K,L,A,B,C,D,E,F,G,H,M,Lines};
 std::vector<float> a[Lines][2];int count[Lines],delay[Lines];
 double iirA[2]{},iirB[2]{},fb[4][2]{},vibM=3,oldfpd=429496.7295;uint32_t fpd=0x6a09e667u;
 double regen=.09375,attenuate=.333,lowpass=.25,drift=0;
 Galactic(){const int sizes[Lines]={6480,3660,1720,680,9700,6000,2320,940,15220,8460,4540,3200,3111};
  for(int i=0;i<Lines;i++){a[i][0].assign(size_t(sizes[i]),0.f);a[i][1].assign(size_t(sizes[i]),0.f);count[i]=1;delay[i]=1;}set(.5f,.5f,.5f);}
 void clearState(){for(auto&x:iirA)x=0;for(auto&x:iirB)x=0;for(auto&f:fb)f[0]=f[1]=0;}
 // params: replace, brightness, bigness (B.Oops feeds bigness to the drift too; detune is unused).
 void set(float replace,float brightness,float bigness){
  regen=.0625+((1.0-replace)*.0625);attenuate=(1.0-(regen/.125))*1.333;
  const double p1=1.00001-(1.0-brightness);lowpass=p1*p1;drift=double(bigness)*bigness*.001;
  const double size=(bigness*1.77)+.1;const int base[Lines]={3407,1823,859,331,4801,2909,1153,461,7607,4217,2269,1597,0};
  for(int i=0;i<M;i++)delay[i]=int(base[i]*size);delay[M]=256;
 }
 float rd(int line,int c){const int n=count[line];return a[line][c][size_t(n-((n>delay[line])?delay[line]+1:0))];}
 void step(int line){count[line]++;if(count[line]<0||count[line]>delay[line])count[line]=0;}
 S process(S in){
  double x[2]={in.l,in.r};
  vibM+=oldfpd*drift;
  if(vibM>Pi*2){vibM=0;oldfpd=.4294967295+(fpd*.0000000000618);}
  a[M][0][size_t(count[M])]=float(x[0]*attenuate);a[M][1][size_t(count[M])]=float(x[1]*attenuate);step(M);
  const double off[2]={(std::sin(vibM)+1)*127,(std::sin(vibM+Pi/2))*127+127};
  for(int c=0;c<2;c++){
   const int w=count[M]+int(off[c]);const double fr=off[c]-std::floor(off[c]);const int dm=delay[M];
   double v=a[M][c][size_t(w-((w>dm)?dm+1:0))]*(1-fr);v+=a[M][c][size_t(w+1-((w+1>dm)?dm+1:0))]*fr;x[c]=v;
   if(std::fabs(iirA[c])<1.18e-37)iirA[c]=0;iirA[c]=(iirA[c]*(1-lowpass))+(x[c]*lowpass);x[c]=iirA[c];
  }
  for(int c=0;c<2;c++){const int o=1-c;   // the first block feeds back across channels
   a[I][c][size_t(count[I])]=float(x[c]+fb[0][o]*regen);a[J][c][size_t(count[J])]=float(x[c]+fb[1][o]*regen);
   a[K][c][size_t(count[K])]=float(x[c]+fb[2][o]*regen);a[L][c][size_t(count[L])]=float(x[c]+fb[3][o]*regen);}
  step(I);step(J);step(K);step(L);
  double outI[2],outJ[2],outK[2],outL[2];
  for(int c=0;c<2;c++){outI[c]=rd(I,c);outJ[c]=rd(J,c);outK[c]=rd(K,c);outL[c]=rd(L,c);
   a[A][c][size_t(count[A])]=float(outI[c]-(outJ[c]+outK[c]+outL[c]));a[B][c][size_t(count[B])]=float(outJ[c]-(outI[c]+outK[c]+outL[c]));
   a[C][c][size_t(count[C])]=float(outK[c]-(outI[c]+outJ[c]+outL[c]));a[D][c][size_t(count[D])]=float(outL[c]-(outI[c]+outJ[c]+outK[c]));}
  step(A);step(B);step(C);step(D);
  for(int c=0;c<2;c++){const double oA=rd(A,c),oB=rd(B,c),oC=rd(C,c),oD=rd(D,c);
   a[E][c][size_t(count[E])]=float(oA-(oB+oC+oD));a[F][c][size_t(count[F])]=float(oB-(oA+oC+oD));
   a[G][c][size_t(count[G])]=float(oC-(oA+oB+oD));a[H][c][size_t(count[H])]=float(oD-(oA+oB+oC));}
  step(E);step(F);step(G);step(H);
  for(int c=0;c<2;c++){const double oE=rd(E,c),oF=rd(F,c),oG=rd(G,c),oH=rd(H,c);
   fb[0][c]=oE-(oF+oG+oH);fb[1][c]=oF-(oE+oG+oH);fb[2][c]=oG-(oE+oF+oH);fb[3][c]=oH-(oE+oF+oG);
   x[c]=(oE+oF+oG+oH)/8.0;
   if(std::fabs(iirB[c])<1.18e-37)iirB[c]=0;iirB[c]=(iirB[c]*(1-lowpass))+(x[c]*lowpass);x[c]=iirB[c];}
  fpd^=fpd<<13;fpd^=fpd>>17;fpd^=fpd<<5;   // the dither's generator also drives the vibrato reseed
  return {float(x[0]),float(x[1])};
 }
};

// Airwindows Infinity2 as adapted in B.Oops (wet 1).
struct Infinity{
 enum{A,B,C,D,E,F,G,H,I,J,K,L,Lines};
 std::vector<float> a[Lines][2];int count[Lines],delay[Lines];
 double bqA[11]{},bqB[11]{},bqC[11]{},fb[8][2]{};double damping=0,rawPass=1,feedback=1;
 Infinity(){const int sizes[Lines]={8111,7511,7311,6911,6311,6111,5511,4911,4511,4311,3911,3311};const int d[Lines]={79,73,71,67,61,59,53,47,43,41,37,31};
  for(int i=0;i<Lines;i++){a[i][0].assign(size_t(sizes[i]),0.f);a[i][1].assign(size_t(sizes[i]),0.f);count[i]=1;delay[i]=d[i];}set(1,.5f,0,1,1);}
 void clearState(){for(int i=7;i<11;i++)bqA[i]=bqB[i]=bqC[i]=0;for(auto&f:fb)f[0]=f[1]=0;}
 void set(float filter,float sizeP,float damp,float allpass,float feedbackP){
  const double size=(sizeP*sizeP*99.0)+1.0;damping=damp*damp*.5;rawPass=allpass;feedback=1.0-std::pow(1.0-feedbackP,4);
  bqC[0]=bqB[0]=bqA[0]=((filter*filter*9900.0)+100.0)/Rate;bqA[1]=.618033988749894848204586;bqB[1]=(filter*.5)+.118033988749894848204586;bqC[1]=.5;
  const double Kk=std::tan(Pi*bqA[0]),KK=Kk*Kk;
  for(double*q:{bqA,bqB,bqC}){const double norm=1.0/(1.0+Kk/q[1]+KK);q[2]=KK*norm;q[3]=2.0*q[2];q[4]=q[2];q[5]=2.0*(KK-1.0)*norm;q[6]=(1.0-Kk/q[1]+KK)*norm;}
  const int d[Lines]={79,73,71,67,61,59,53,47,43,41,37,31};for(int i=0;i<Lines;i++)delay[i]=int(d[i]*size);
 }
 static double biquad(double*q,double x,int c){const int s=7+c*2;const double t=(x*q[2])+q[s];q[s]=(x*q[3])-(t*q[5])+q[s+1];q[s+1]=(x*q[4])-(t*q[6]);return t;}
 void step(int line){count[line]++;if(count[line]<0||count[line]>delay[line])count[line]=0;}
 double tap(int line,int c,int off){const int n=count[line]+off;return a[line][c][size_t(n-((n>delay[line])?delay[line]+1:0))];}
 S process(S in){
  double out[2];const double dry[2]={in.l,in.r};double ap[2][4];
  for(int c=0;c<2;c++){const double x=biquad(bqA,dry[c],c);for(int k=0;k<4;k++)ap[c][k]=x;}
  for(int k=0;k<4;k++){const int line=I+k;int t=count[line]+1;if(t<0||t>delay[line])t=0;
   for(int c=0;c<2;c++){ap[c][k]-=a[line][c][size_t(t)]*.5;a[line][c][size_t(count[line])]=float(ap[c][k]);ap[c][k]*=.5;}
   step(line);for(int c=0;c<2;c++)ap[c][k]+=a[line][c][size_t(count[line])];}
  if(rawPass!=1.0)for(int c=0;c<2;c++)for(int k=0;k<4;k++)ap[c][k]=(ap[c][k]*rawPass)+(dry[c]*(1.0-rawPass));
  for(int c=0;c<2;c++)for(int k=0;k<8;k++)a[A+k][c][size_t(count[A+k])]=float(ap[c][k&3]+(fb[k][c]*feedback));
  for(int k=0;k<8;k++)step(A+k);
  const double fr=damping-std::floor(damping);
  for(int c=0;c<2;c++){
   double inf[8];for(int k=0;k<8;k++)inf[k]=tap(A+k,c,0)*(1-fr)+tap(A+k,c,1)*fr;
   double dA=.5,dE=.5,dD=.5;
   if(std::fabs(inf[0])>.4)dA-=((std::fabs(inf[0])-.4)*.2);if(std::fabs(inf[4])>.4)dE-=((std::fabs(inf[4])-.4)*.2);if(std::fabs(dry[c])>.4)dD-=((std::fabs(dry[c])-.4)*.2);
   fb[0][c]=(inf[0]-(inf[1]+inf[2]+inf[3]))*dA;fb[1][c]=(inf[1]-(inf[0]+inf[2]+inf[3]))*dD;
   fb[2][c]=(inf[2]-(inf[0]+inf[1]+inf[3]))*dD;fb[3][c]=(inf[3]-(inf[0]+inf[1]+inf[2]))*dD;
   fb[4][c]=(inf[4]-(inf[5]+inf[6]+inf[7]))*dE;fb[5][c]=(inf[5]-(inf[4]+inf[6]+inf[7]))*dD;
   fb[6][c]=(inf[6]-(inf[4]+inf[5]+inf[7]))*dD;fb[7][c]=(inf[7]-(inf[4]+inf[5]+inf[6]))*dD;
   double x=(inf[0]+inf[1]+inf[2]+inf[3]+inf[4]+inf[5]+inf[6]+inf[7])/8.0;
   x=biquad(bqB,x,c);x=lim(x,-1.0,1.0);x=std::asin(x);out[c]=biquad(bqC,x,c);
  }
  return {float(out[0]),float(out[1])};
 }
};

// Airwindows XRegion as adapted in B.Oops (the Banger's filter); wet 1, pan 0.
struct XRegion{
 float bq[5][15]{},comp[5]{},clip[5]{},gain=1;int age=0;
 void clear(){for(auto&b:bq)for(int i=7;i<15;i++)b[i]=0;age=0;}
 S process(S in,const float*p){
  const float high=p[1],low=p[2],mid=(high+low)*.5f,spread=1.001f-std::fabs(high-low),nuke=p[3];
  const float f[5]={high,.5f*(high+mid),mid,.5f*(mid+low),low};
  // Coefficients every fourth sample (B.Oops: every sample).
  if(age++%4==0)for(int s=0;s<5;s++){gain=std::pow(p[0]+.5f,4);float*b=bq[s];b[0]=f[s]*f[s]*f[s]*20000.f/float(Rate);if(b[0]<.00009f)b[0]=.00009f;comp[s]=std::sqrt(b[0])*6.4f*spread;clip[s]=.75f+(b[0]*nuke*37.f);
   const float K=std::tan(float(Pi)*b[0]),norm=1.f/(1.f+K/.7071f+K*K);b[2]=K/.7071f*norm;b[4]=-b[2];b[5]=2.f*(K*K-1.f)*norm;b[6]=(1.f-K/.7071f+K*K)*norm;}
  float wets[5]={1,1,1,1,p[3]*4.f};float&aW=wets[1],&bW=wets[2],&cW=wets[3],&dW=wets[4];
  if(dW<1.f){aW=dW;bW=0;cW=0;dW=0;}else if(dW<2.f){bW=dW-1.f;cW=0;dW=0;}else if(dW<3.f){cW=dW-2.f;dW=0;}else dW-=3.f;
  float x[2]={in.l*gain,in.r*gain},nl[2];   // gain != 1 multiplies; equal to 1 leaves it unchanged
  for(int s=0;s<5;s++){
   if(s>0&&!(wets[s]>0))continue;float*b=bq[s];
   for(int c=0;c<2;c++){const int o=c?11:7;float y=x[c]*clip[s];y=lim(y,-1.57079633f,1.57079633f);y=std::sin(y);
    const float outS=b[2]*y+b[4]*b[o+1]-b[5]*b[o+2]-b[6]*b[o+3];b[o+1]=b[o];b[o]=y;b[o+3]=b[o+2];b[o+2]=outS;
    y=outS/comp[s];if(s>0)y=(y*wets[s])+(nl[c]*(1.f-wets[s]));nl[c]=y;x[c]=y;}
  }
  for(float&v:x){v=lim(v,-1.57079633f,1.57079633f);v=std::sin(v);}
  return {x[0],x[1]};
 }
};

enum Fx:uint8_t{Amp,Balance,Width,Reverb,GalacticFx,InfinityFx,Delay,Reverse,Tremolo,Chop,Stutter,Jumble,TapeStop,TapeSpeed,WowFlutter,
 Scratch,Noise,Crackles,Crush,Decimate,Distortion,Waveshaper,Filter,EQ,Flanger,Phaser,RingMod,Wah,Banger,Tesla,FxCount};
// B.Oops's default slot mix per effect (FxDefaults.hpp).
float slotMix(int fx){return fx==Noise||fx==Flanger||fx==Phaser?.5f:fx==Tesla?.75f:1.f;}
const uint8_t poolAll[FxCount]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29},
 poolCuts[]={Reverse,Chop,Stutter,Jumble,TapeStop,TapeSpeed,Scratch},poolPattern[]={Tremolo,Chop,Stutter,Jumble},
 poolTemporal[]={Reverb,GalacticFx,InfinityFx,Delay,Reverse},poolTape[]={TapeStop,TapeSpeed,WowFlutter},poolVinyl[]={Scratch,Noise,Crackles},
 poolDestroy[]={Crush,Decimate,Distortion,Waveshaper,Tesla},poolFilter[]={Filter,EQ},poolMod[]={Flanger,Phaser,RingMod,Wah,Banger},poolBasic[]={Amp,Balance,Width};

constexpr int MaxSteps=16,HistoryFrames=196608;
constexpr long SilentGate=long(Rate*5);
struct Step{bool start=false;int size=1;uint8_t fx=0;float base[12]{},shape[6]{},pan=0;};
struct Crackle{double t0=0,level=0,freq=0;};

class Oops final:public NativeFx{
 Ring ring{HistoryFrames};Ace ace;Galactic galactic;Infinity infinity;XRegion xregion;
 std::array<Step,MaxSteps> pattern{};uint32_t gen[MaxSteps]{};
 // settings
 double spb=22050,stepBeats=.25,cycleBeats=4;int steps=16,padMax=2,pool=1;float density=.5f,variation=.25f,mutate=0,chance=1,fade=.1f,panSpread=0;
 int patternSeed=-1;bool drawn=false;
 // clock
 double beat=0;long curG=-1,activeKey=-1;double padSize=1;uint32_t epoch=0,rnd=0x0b00b5u;long silent=SilentGate;
 size_t scrubLine=0,scrubAt=0;bool scrubbed=true;
 // the playing pad (Fx::init state)
 Step pad;bool playing=false;S panf{1,1},unpanf{0,0};double fps=5512.5;
 float amp=1,balance=0,width=1,f1=0,f2=0,f3=0;double d1=0,d2=0,d3=0,d4=0;int i1=1,env=0;float eval=0,chops[8]{};
 Butter lp,hp;float rtab[9][8]{};Peak eq[6];AllPass apL[10],apR[10];S lastSample,decStack,decLive;double decCount=0;
 Crackle crackles[16];int crackleCount=0;long crackleClock=0;
 float lsign=0,rsign=0,lt=0,rt=0,lpow=0,rpow=0;bool lexc=false,rexc=false;
 float xparams[4][4]{},bSpeed=0,bSpin=0,nspeed=0,nspin=0,ang=0,xcur=0,ycur=0,speedB=.5f,spinB=.5f;double bCount=0;
 float unid(){return nativeUnit(rnd);}
 float bid(){return nativeUnit(rnd)*2-1;}
 // A seeded base value within lo..hi; with B.Oops's per-pad randomization of VARIATION.
 float P(int k,float lo,float hi)const{return lo+(hi-lo)*pad.base[k];}
 float V(int k,float lo,float hi){const float r=bid();return lim(P(k,lo,hi)+r*variation,0.f,1.f);}
 // As V, for levels: VARIATION never lifts them above the seeded range.
 float Vc(int k,float lo,float hi){const float r=bid();return lim(P(k,lo,hi)+r*variation,std::max(0.f,lo-.15f),hi);}
 float shapeAt(double x)const{
  x=lim(x,0.0,1.0)*5;int k=std::min(4,int(x));float t=float(x-k);t=t*t*(3-2*t);
  return pad.shape[k]+(pad.shape[k+1]-pad.shape[k])*t;
 }
 const uint8_t*poolList(int&count)const{
  switch(pool){
   case 1:count=int(std::size(poolCuts));return poolCuts;case 2:count=int(std::size(poolPattern));return poolPattern;
   case 3:count=int(std::size(poolTemporal));return poolTemporal;case 4:count=int(std::size(poolTape));return poolTape;
   case 5:count=int(std::size(poolVinyl));return poolVinyl;case 6:count=int(std::size(poolDestroy));return poolDestroy;
   case 7:count=int(std::size(poolFilter));return poolFilter;case 8:count=int(std::size(poolMod));return poolMod;
   case 9:count=int(std::size(poolBasic));return poolBasic;default:count=FxCount;return poolAll;
  }
 }
 void draw(int i){
  uint32_t s=0x9e3779b9u*uint32_t(patternSeed+1)^(uint32_t(i)+1)*0x85ebca6bu^gen[i]*0xc2b2ae35u;
  for(int k=0;k<4;k++)nativeRandom(s);
  Step&st=pattern[size_t(i)];int count;const uint8_t*list=poolList(count);
  st.start=nativeUnit(s)<density;st.size=1+std::min(padMax-1,int(nativeUnit(s)*padMax));
  st.fx=list[std::min(count-1,int(nativeUnit(s)*count))];
  for(float&b:st.base)b=nativeUnit(s);for(float&v:st.shape)v=nativeUnit(s);st.pan=nativeUnit(s)*2-1;
  // The drawn shapes: SCRATCH 0..-1..0, WAH -1..1, WAVESHAPER a bent identity.
  if(st.fx==Scratch){st.shape[0]=st.shape[5]=0;for(int k=1;k<5;k++)st.shape[k]=-st.shape[k];}
  else if(st.fx==Wah){for(float&v:st.shape)v=v*2-1;}
  else if(st.fx==Waveshaper){for(int k=0;k<6;k++)st.shape[k]=lim(k/5.f+(st.shape[k]-.5f)*.5f,0.f,1.f);st.shape[0]=0;st.shape[5]=1;}
 }
 void drawAll(){for(int i=0;i<MaxSteps;i++){gen[i]=0;draw(i);}}
 float adsr(double position,double size)const{
  if(position<0||position>=size)return 0;
  const float A=fade,D=.1f,Su=1,R=fade;float adr=A+D+R;if(adr<1)adr=1;
  if(position<A/adr)return float(position/(A/adr));
  if(position<(A+D)/adr)return float(1-(1-Su)*(position-(A/adr))/(D/adr));
  if(position>size-R/adr)return float(Su*(size-position)/(R/adr));
  return Su;
 }
 void endPad(){playing=false;activeKey=-1;}
 void initPad(int start){
  pad=pattern[size_t(start)];fps=stepBeats*spb;
  playing=unid()<chance;   // the pad's gate
  const float pn=pad.pan*panSpread;panf=S(1-pn*(pn>0),pn*(pn<0)+1);unpanf=S(1,1)-panf;
  switch(pad.fx){
   case Amp:{const float r=bid();amp=lim(2*(P(0,0,.9f)+r*variation),0.f,2.f);break;}
   case Balance:{const float r=bid();balance=lim(2*(P(0,0,1)+r*variation)-1,-1.f,1.f);break;}
   case Width:{const float r=bid();width=float(std::pow(2*lim(P(0,0,.6f)+r*variation,0.f,1.f),6.64385619));break;}
   case Reverb:{const float r=bid();ace.setRoomSize(.5f+.5f*lim(P(0,0,1)+r*variation,0.f,1.f));break;}
   case GalacticFx:{const float a=V(0,.3f,1),b=V(1,.2f,.8f);V(2,.2f,.8f);const float c=V(3,.3f,1);galactic.set(a,b,c);break;}
   case InfinityFx:{const float a=V(0,.3f,1),b=V(1,.2f,.8f),c=V(2,0,.5f),d=V(3,.5f,1),e=V(4,.6f,1);infinity.set(a,b,c,d,e);break;}
   case Delay:{const float r1=bid(),r2=bid();d1=std::floor(lim(1.0+P(0,0,.1f)*32,1.0,double(std::max(1,steps-1))));
    f1=lim(P(1,.25f,1)+r1*variation,0.f,1.f);f2=lim(P(2,0,.6f)+r2*variation,0.f,1.f);break;}
   case Reverse:break;
   case Tremolo:{const float r1=bid();d1=1+19*lim(P(0,.1f,.9f)+r1*variation,0.f,1.f);const float r2=bid();f1=lim(P(1,.4f,1)+r2*variation,0.f,1.f);env=std::min(4,int(pad.base[2]*5));break;}
   case Chop:{i1=int(lim(1+8*P(0,.1f,.9f),1.f,8.f));f1=P(1,.05f,.4f);for(int i=0;i<i1;i++){const float r=bid();chops[i]=lim(pad.base[2+i]+r*variation,0.f,1.f);}
    d1=1+lim(32*P(10,0,.1f),0.f,31.f);break;}
   case Stutter:{f1=P(1,.05f,.3f);i1=int(lim(2+7*P(0,0,1),2.f,8.f));d1=fps/double(i1);break;}
   case Jumble:{int blocks=0;for(int i=0;i<steps;i++)if(pattern[size_t(i)].start)++blocks;
    const int r1=int(unid()*double(blocks));const int dblock=lim(r1,0,blocks-1);blocks=0;i1=0;
    for(int i=0;i<steps;i++){if(pattern[size_t(i)].start)++blocks;if(blocks>=dblock){i1=(start+steps-i)%steps;break;}}break;}
   case TapeStop:{const float r1=bid();d1=lim(P(0,.2f,1)+r1*variation,0.f,1.f);const float r2=bid();d2=lim(1.0+9.0*(P(1,0,.6f)+r2*variation),1.0,10.0);break;}
   case TapeSpeed:{const float r=bid();d1=lim(P(0,.25f,.9f)+r*variation,0.f,1.f);break;}
   case WowFlutter:d1=.01*V(0,.2f,1);d2=.0625+.9375*V(1,.3f,1);d3=.01*V(2,0,.3f);d4=1+15*V(3,0,.6f);break;
   case Scratch:{const float r=bid();d1=lim(P(0,.3f,1)+r*variation,0.f,1.f);d2=1+lim(32*P(1,0,.1f),0.f,31.f);break;}
   case Noise:amp=float(db2co(-90.0+102.0*V(0,.45f,.72f)));break;
   case Crackles:{const float a=float(db2co(-36.0+48.0*Vc(0,.3f,.7f)));f1=200*V(1,.1f,.5f);f2=a*V(2,.3f,1);f3=10*V(3,.1f,.5f);crackleClock=0;crackleCount=0;break;}
   case Crush:{const float r1=bid();f1=lim(.01f+1.99f*P(0,.3f,.7f)+1.99f*r1*variation,.3f,1.4f);const float r2=bid();
    i1=int(lim(1+32*(P(1,.06f,.3f)+r2*variation),2.f,32.f));f2=std::pow(2.f,float(i1-1));break;}
   case Decimate:{const float r=bid();f1=lim(.01f+.99f*(P(0,.02f,.4f)+r*variation),.01f,1.f);decStack=decLive=S();decCount=0;break;}
   case Distortion:env=std::min(4,int(pad.base[0]*5));d1=db2co(-30.0+100.0*V(1,.3f,.6f));d2=db2co(-70.0+100.0*Vc(2,.55f,.68f));break;
   case Waveshaper:f1=float(db2co(-30.0+100.0*V(0,.3f,.5f)));f2=float(db2co(-70.0+100.0*Vc(1,.6f,.68f)));i1=pad.base[2]<.25f?1:0;
    if(i1)for(float&y:pad.shape)y*=.75f;   // in dB mode the seeded curve tops out at 0 dBFS
    break;
   case Filter:{const double low=20+19980*std::pow(V(0,0,.45f),4.0),high=20+19980*std::pow(V(1,.45f,.9f),4.0);const int order=2*int(lim(1+8*P(2,0,.4f),1.f,8.f));
    lp.set(false,high,order,rtab[order/2]);hp.set(true,low,order,rtab[order/2]);lp.clear();hp.clear();break;}
   case EQ:for(int i=0;i<6;i++)eq[i].build(72*lim(V(i,.4f,.6f),.35f,.65f)-36);break;
   case Flanger:d1=.01*V(0,0,.3f);d2=.01*V(1,.1f,.6f);d3=10*2*Pi*std::pow(V(2,.2f,.5f),3.0);d4=2*Pi*V(3,0,.5f);f2=2*V(4,.3f,.8f)-1;break;
   case Phaser:{const double lo=20+19980*std::pow(V(0,.1f,.4f),3.0),hi=20+19980*std::pow(V(1,.4f,.8f),3.0);d3=10*2*Pi*std::pow(V(2,.2f,.5f),3.0);d4=2*Pi*V(3,0,.5f);
    f2=2*V(4,.3f,.8f)-1;i1=int(1+lim(10*P(5,.2f,.9f),0.f,9.f));d1=.5*lo/Rate;d2=hi>lo?.5*hi/Rate-d1:0;
    for(int i=0;i<10;i++){apL[i]=AllPass();apR[i]=AllPass();}lastSample=S();break;}
   case RingMod:{f1=V(0,.3f,1);d1=20000*std::pow(V(1,.2f,.5f),4.0);env=std::min(4,int(pad.base[2]*5));break;}
   case Wah:{d1=20+19980*std::pow(V(0,.2f,.35f),3.0);f1=V(1,.3f,.8f);f2=V(2,.15f,.5f);i1=2*int(lim(1+8*P(3,.1f,.5f),0.f,8.f));d2=1+lim(32*P(4,0,.1f),0.f,31.f);
    const double f=d1*(1+f1*shapeAt(0));lp.set(false,lim(f*(1+.5*f2),1.0,20000.0),i1,rtab[i1/2]);hp.set(true,lim(f*(1-.5*f2),0.0,20000.0),i1,rtab[i1/2]);lp.clear();hp.clear();break;}
   case Banger:{const float bases[4]={P(0,.6f,.8f),P(1,.2f,.5f),P(2,.5f,.8f),P(3,.3f,.8f)},rands[4]={.25f,.333f,.333f,.25f};
    for(int i=0;i<4;i++)for(int j=0;j<4;j++){const float r=bid();xparams[i][j]=lim(bases[j]+r*rands[j],0.f,1.f);}speedB=P(4,.3f,.7f);spinB=P(5,.3f,.7f);break;}
   case Tesla:f1=float(db2co(-30.0+100.0*V(0,.55f,.8f)));f2=float(db2co(-70.0+100.0*Vc(1,.5f,.65f)));lsign=rsign=0;lexc=rexc=false;lt=rt=lpow=rpow=0;break;
  }
 }
 static float wave(int e,double t,double freq){
  const double p=std::fmod(t*freq,1.0);
  switch(e){case 0:return float(std::sin(2*Pi*t*freq));case 1:return float(p<.5?4*p-1:3-4*p);case 2:return p<.5?1.f:-1.f;case 3:return float(2*p-1);default:return float(1-2*p);}
 }
 S fxProcess(double position){
  const S s0=ring.get(0);const double pos=std::min(position,32.0);
  switch(pad.fx){
   case Amp:return s0*amp;
   case Balance:return {balance<0?s0.l+(0-balance)*s0.r:(1-balance)*s0.l,balance<0?(balance+1)*s0.r:s0.r+balance*s0.l};
   case Width:{const float m=(s0.l+s0.r)/2,x=(s0.l-s0.r)*width/2;return {m+x,m-x};}
   case Reverb:return ace.process(s0);
   case GalacticFx:return galactic.process(s0);
   case InfinityFx:return infinity.process(s0);
   case Delay:return ring.sample(stepBeats*spb*d1*f1);
   case Reverse:return ring.get(long(2*fps*pos));
   case Tremolo:{const float f=wave(env,position*fps/Rate,d1);const float k=1.f/(.005f*float(Rate));eval+=lim(f-eval,-k,k);return s0*(1-f1*.5f*(eval+1));}
   case Chop:{const double ph=std::fmod(position/d1,1.0);const int p=int(i1*ph);const double frac=double(i1)*ph-double(p);const int st=p%i1;
    S s1=s0*chops[st];
    if(frac<.5*f1)s1=mixS(s1,s0*chops[(p+i1-1)%i1],float(.5-frac/f1));else if(frac>1-.5*f1)s1=mixS(s1,s0*chops[(p+1)%i1],float(.5-(1-frac)/f1));
    return s1;}
   case Stutter:{const long nr=long(pos*double(i1));const double frac=std::fmod(position,1.0/double(i1));S s1=ring.sample(double(nr)*d1);
    if(frac<.5*f1)s1=mixS(s1,ring.sample(nr>0?double(nr-1)*d1:double(nr)*d1),float(.5-frac/f1));
    else if(frac>1-.5*f1)s1=mixS(s1,ring.sample(double(nr+1)*d1),float(.5-(1-frac)/f1));
    return s1;}
   case Jumble:return ring.get(long(fps*i1));
   case TapeStop:return ring.sample((std::log(std::exp(d2*pos)+std::exp(d2*d1)-1)/d2-d1)*fps);
   case TapeSpeed:return ring.sample((1-d1)*fps*pos);
   case WowFlutter:{const double w=(.5-.5*std::cos(2*Pi*position*d2))*d1,fl=(.5-.5*std::cos(2*Pi*position*d4))*d3;return ring.sample(fps*(w+fl));}
   case Scratch:{const double f=shapeAt(std::fmod(position/d2,1.0));return ring.sample(fps*d1*(-lim(f,-1.0,0.0)));}
   case Noise:return {unid()*amp,unid()*amp};
   case Crackles:{
    const double t=double(crackleClock)/Rate;++crackleClock;
    if(unid()<f1/fps&&crackleCount<16){const double r1=unid(),r2=bid();crackles[crackleCount++]={t,f2*std::pow(r1,double(f3)),10000+r2*2000};}
    S s1=s0;
    for(int i=0;i<crackleCount;){const Crackle&c=crackles[i];const double u=(t-c.t0)*c.freq,den=1+(u-2)*(u-2);
     const float cr=float(c.level*std::sin(2*Pi*std::sqrt(u))/den);s1+=S(cr,cr);
     if(1/den<.000316228){crackles[i]=crackles[--crackleCount];}else ++i;}
    return s1;}
   case Crush:{auto q=[&](float x){const float a=lim(x+f1,0.f,2*f1)/(2*f1),b=std::round(a*f2);return (b-.5f*f2)*2*f1/f2;};return {q(s0.l),q(s0.r)};}
   case Decimate:{
    if(decCount+1.0>=1.0/f1){const double c0=1.0/f1-decCount;decStack+=s0*float(c0);decLive=decStack*f1;decCount=1.0-c0;decStack=s0*float(decCount);}
    else{++decCount;decStack+=s0;}
    return decLive;}
   case Distortion:{
    const double level=d2;double ch[2]={s0.l*d1/level,s0.r*d1/level};
    for(double&x:ch){const double ax=std::fabs(x);
     switch(env){
      case 0:x=lim(x*level,-level,level);break;
      case 1:x=sgn(x)*level*std::sqrt(x*x/(1.0+x*x));break;
      case 2:x=ax<=1?level*x:(sgn(x)*level*float(2*(int((ax+1)/2)%2)-1)*(1.0-std::fmod(ax+1.0,2.0)));break;
      case 3:x=ax<1.0/3?2*level*x:(ax<2.0/3?sgn(x)*level*(3-(2-3*ax)*(2-3*ax))/3:level*sgn(x));break;
      default:x=sgn(x)*level*(1.0-std::exp(-ax));break;
     }}
    return {float(ch[0]),float(ch[1])};}
   case Waveshaper:{
    float v[2]={s0.l,s0.r};
    for(float&x:v){float y;
     if(i1==0){y=float(shapeAt(lim(std::fabs(x*f1),0.f,1.f)));}
     else{const float u=lim((90.f+20.f*std::log10(std::fabs(.000031623f+x*f1)))/120.f,0.f,1.f);y=float(db2co(-90.0+shapeAt(u)*120.0));}
     x=sgn(x)*y*f2;}
    return {v[0],v[1]};}
   case Filter:return hp.push(lp.push(s0));
   case EQ:{S s=s0;for(auto&e:eq)s=e.process(s);return s;}
   case Flanger:{const double t=position*fps/Rate,dl=d1+(.5-.5*std::cos(d3*t))*d2,dr=d1+(.5-.5*std::cos(d4+d3*t))*d2;
    return {ring.get(long(dl*Rate)).l,ring.get(long(dr*Rate)).r};}
   case Phaser:{const double t=position*fps/Rate,dl=d1+(.5-.5*std::cos(d3*t))*d2,dr=d1+(.5-.5*std::cos(d4+d3*t))*d2;
    for(int i=0;i<i1;i++){apL[i].setDelay(float(dl));apR[i].setDelay(float(dr));}
    S s1=s0+lastSample*f2;for(int i=i1-1;i>=0;--i){s1.l=apL[i].process(s1.l);s1.r=apR[i].process(s1.r);}lastSample=s1;return s1;}
   case RingMod:{const float f=wave(env,position*fps/Rate,d1);return mixS(s0,s0*f,f1);}
   case Wah:{const float m=shapeAt(std::fmod(position/d2,1.0));const double f=d1*(1.0+f1*m);
    lp.set(false,lim(f*(1.0+f2),0.0,20000.0),i1,rtab[i1/2]);hp.set(true,lim(f*(1.0-f2),0.0,20000.0),i1,rtab[i1/2]);return hp.push(lp.push(s0));}
   case Banger:{
    const double rate=Rate;
    if(bCount>=rate){nspeed=lim(speedB+bid()*.25f,0.f,1.f);nspin=2*lim(spinB+bid()*.1f,0.f,1.f)-1;bCount=0;}else bCount++;
    bSpeed=float((1-1/rate)*bSpeed+(1/rate)*nspeed);bSpin=float((1-1/rate)*bSpin+(1/rate)*nspin);ang+=float(2*Pi*(10/rate)*bSpin);
    const float dx=std::sin(ang),dy=std::cos(ang);xcur+=float(dx*(1/rate)*4*bSpeed*bSpeed);ycur+=float(dy*(1/rate)*4*bSpeed*bSpeed);
    if(xcur<0){xcur=0;if(dx<0){ang=-ang;bSpin=0;}}else if(xcur>1){xcur=1;if(dx>0){ang=-ang;bSpin=0;}}
    if(ycur<0){ycur=0;if(dy<0){ang=float(-Pi)-ang;bSpin=0;}}else if(ycur>1){ycur=1;if(dy>0){ang=float(-Pi)-ang;bSpin=0;}}
    float xp[4];for(int i=0;i<4;i++)xp[i]=(1-xcur)*(1-ycur)*xparams[0][i]+(1-xcur)*ycur*xparams[1][i]+xcur*ycur*xparams[2][i]+xcur*(1-ycur)*xparams[3][i];
    return xregion.process(s0,xp);}
   case Tesla:{
    S s1;const float ls=sgn(s0.l),rs=sgn(s0.r);
    if(lsign!=ls){lexc=false;lsign=ls;}if(rsign!=rs){rexc=false;rsign=rs;}
    if(!lexc&&s0.l*f1>1){lpow=f2;lt=0;lexc=true;}if(!rexc&&s0.r*f1>1){rpow=f2;rt=0;rexc=true;}
    if(lpow>0){s1.l=lpow*std::sin(2*float(Pi)*lt);lt+=float(6000/Rate)*(1+.25f*bid());lpow*=.875f;if(lpow<.0001f)lpow=0;}
    if(rpow>0){s1.r=rpow*std::sin(2*float(Pi)*rt);rt+=float(6000/Rate)*(1+.25f*bid());rpow*=.875f;if(rpow<.0001f)rpow=0;}
    return s1;}
  }
  return s0;
 }
 void onStep(long g){
  const int ps=int(g%steps);int start=-1;
  for(int i=ps;i>=0;--i)if(pattern[size_t(i)].start){if(i+pattern[size_t(i)].size>ps)start=i;break;}
  if(start<0){endPad();return;}
  const long key=g-(ps-start);if(key==activeKey)return;
  endPad();activeKey=key;
  padSize=std::min<double>({double(pattern[size_t(start)].size),double(steps-start),cycleBeats/stepBeats-double(key)});
  initPad(start);
 }
 void cycleEnd(){endPad();curG=-1;if(mutate>0)for(int i=0;i<MaxSteps;i++)if(unid()<mutate){gen[i]++;draw(i);}}
 void scrub(){   // clears the reverb lines a slice at a time while the input stays silent
  std::vector<float>*lines[64];int n=0;
  for(auto&c:ace.line)for(auto&l:c)lines[n++]=&l;
  for(auto&l:galactic.a)for(auto&c:l)lines[n++]=&c;
  for(auto&l:infinity.a)for(auto&c:l)lines[n++]=&c;
  size_t budget=8192;
  while(budget&&scrubLine<size_t(n)){auto&v=*lines[scrubLine];size_t k=std::min(budget,v.size()-scrubAt);std::fill(v.begin()+long(scrubAt),v.begin()+long(scrubAt+k),0.f);
   scrubAt+=k;budget-=k;if(scrubAt>=v.size()){scrubAt=0;scrubLine++;}}
  if(scrubLine>=size_t(n)){scrubbed=true;ace.clearState();galactic.clearState();infinity.clearState();xregion.clear();lp.clear();hp.clear();
   for(auto&e:eq)e.z1=e.z2=S();lastSample=S();eval=0;}
 }
public:
 Oops(){
  for(int o=1;o<=8;o++)for(int i=0;i<o;i++)rtab[o][i]=float(std::sin(Pi*(2.0*i+1.0)/(2.0*(2*o))));
  const float fr[6]={30,80,300,1500,4000,15000},q[6]={.70710678f,1,.70710678f,1,1,1};
  for(int i=0;i<6;i++){eq[i].fc=fr[i]/float(Rate);eq[i].q=q[i];eq[i].build(0);}
 }
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);stepBeats=nativeDivisionBeats(nativeDivision(v[0]));steps=nativeChoice(v[1],16)+1;
  cycleBeats=nativeDivisionBeats(nativeDivision(v[2]));
  const int seed=v[3];const float dens=v[4]*.01f;const int pl=nativeChoice(v[5],10),pm=nativeChoice(v[6],8)+1;
  variation=v[7]*.0035f;mutate=v[8]*.01f;chance=v[9]*.01f;fade=v[10]*.005f;panSpread=v[11]*.01f;
  if(!drawn||seed!=patternSeed||dens!=density||pl!=pool||pm!=padMax){patternSeed=seed;density=dens;pool=pl;padMax=pm;drawAll();drawn=true;}
 }
 void clear()override{
  ring.clear();endPad();curG=-1;beat=0;silent=SilentGate;scrubLine=scrubAt=0;scrubbed=false;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;beat=0;endPad();curG=-1;}   // downbeat
  if(!scrubbed&&silent>=SilentGate)scrub();
  for(int i=0;i<frames;i++){
   const S in{io[i*2],io[i*2+1]};
   if(std::fabs(in.l)+std::fabs(in.r)>1e-9f){silent=0;scrubbed=false;scrubLine=scrubAt=0;}else if(silent<SilentGate)silent++;
   ring.push(in);
   S out=in;
   const double sp=beat/stepBeats;const long g=long(sp);
   if(g!=curG){curG=g;onStep(g);}
   if(silent>=SilentGate)out=S();
   else if(activeKey>=0&&playing){
    const double rel=sp-double(activeKey);
    if(rel<padSize){
     const S s0=ring.get(0),s1=fxProcess(rel);
     out=mixS(s0,panf*s1+unpanf*s0,slotMix(pad.fx)*adsr(rel,padSize));
     if(pad.fx==Delay||pad.fx==Flanger)ring.setFront(mixS(out,s0,1-f2));   // their feedback
    }
   }
   io[i*2]=nativeClean(out.l);io[i*2+1]=nativeClean(out.r);
   beat+=1/spb;if(beat>=cycleBeats){beat-=cycleBeats;if(beat>=cycleBeats)beat=0;cycleEnd();}
  }
 }
};
}
const NativeInfo oopsInfo{FxType::Oops,"OOPS","OOPS","OO",6,1,2,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeOops(){return std::make_unique<Oops>();}
}
