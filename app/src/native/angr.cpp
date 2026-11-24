// SPDX-License-Identifier: GPL-3.0-only
// ANGR: a one-to-one recreation of B.Angr (C) 2021-2026 Sven Jaehnichen, GPL-3.0-or-later
// (https://github.com/sjaehn/BAngr), with its distortion stage XRegion (C) 2016 airwindows /
// 2018 Chris Johnson, adapted 2021 by Sven Jaehnichen, MIT licence.
// One XRegion (five cascaded sine-clipped band-pass stages from FIRST to LAST, NUKE adding
// stages progressively) is fed parameters bilinearly crossfaded between four instances at
// the corners of a square. A cursor glides across the square at SPEED and turns at SPIN,
// reflecting off the edges; both are flexed by random drift or by the input level (full
// band, lows, mids or highs, from 8th-order Butterworth filters), as in the original.
// Changes: the four instance sets (6 x 4 controls) are compacted into a base GAIN, FIRST,
// LAST and NUKE plus an INSTANCE SET of corner offsets scaled by VARIATION; set ANGR at
// the default values reproduces the plugin's default instances (pans included). The
// speed/spin ranges are one FLEX amount (50 gives the original 0.25 / 0.1) sharing one
// FLEX SOURCE, with both flexibility amounts at the original 0.5. Each instance's own
// dry/wet is fixed at 1 (the host MIX blends). SYNC replaces the plugin's one-second time
// unit (random retarget clock, random flex time, glide and spin rate) by DIVISION, with the
// retargets aligned to the downbeat. XRegion's coefficients follow the cursor every 4
// samples instead of every sample. The GUI cursor, bypass fader and XRegion's denormal
// dither are omitted (silence stays exact zero); the random generator is a seeded member.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
namespace {
const char*const flexSources[]={"RANDOM","LEVEL","LOWS","MIDS","HIGHS"};
const char*const instanceSets[]={"ANGR","SET 2","SET 3","SET 4","SET 5","SET 6","SET 7","SET 8"};
constexpr int Sets=8;
const NativeParam params[]={
 {"GAIN",56,NativeUnit::Percent,35,75},
 {"FIRST",40,NativeUnit::Percent,15,80},
 {"LAST",55,NativeUnit::Percent,15,80},
 {"NUKE",68,NativeUnit::Percent,20,100},
 {"VARIATION",50,NativeUnit::Percent,20,90},
 {"INSTANCE SET",0,NativeUnit::Choice,0,100,instanceSets,Sets},
 {"SPEED",50,NativeUnit::Percent,20,80},
 {"SPIN",50,NativeUnit::Bipolar,30,70},
 {"FLEX SOURCE",0,NativeUnit::Choice,0,100,flexSources,5},
 {"FLEX",50,NativeUnit::Percent,20,80},
 {"SYNC",0,NativeUnit::Toggle,0,100},
 {"DIVISION",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(0),nativeDivisionValue(7)},
};
enum{Random,Level,Lows,Mids,Highs};
constexpr float Rate=44100;
constexpr float FlexTime[5]={1.f,.05f,.05f,.05f,.05f};
// B.Angr's default four instances: gain, first, last, nuke, pan (corner order as its FX1..FX4).
constexpr float AngrCorners[4][5]={{.5f,.2f,.1f,1.f,0},{.5f,.7f,.8f,1.f,0},{.75f,.5f,.8f,.5f,.5f},{.5f,.2f,.5f,.2f,-.5f}};

// B.Angr's Butterworth filters (cascaded second-order sections, order 8).
struct Butterworth {
 int o2=0;float f1=1;std::array<float,4> c0{},c1{},c2{},b0{},b1{},b2{};
 void set(bool high,double cutoff,int order){
  o2=order/2;f1=high?-2.f:2.f;const double a=std::tan(M_PI*cutoff/Rate),a2=a*a;
  for(int i=0;i<o2;i++){const double r=std::sin(M_PI*(2.0*i+1.0)/(2.0*order)),s=a2+2.0*a*r+1.0;
   c0[i]=float(high?1.0/s:a2/s);c1[i]=float(2.0*(1.0-a2)/s);c2[i]=float(-(a2-2.0*a*r+1.0)/s);}
 }
 void clear(){b0.fill(0);b1.fill(0);b2.fill(0);}
 float process(float x){
  for(int i=0;i<o2;i++){b0[i]=b1[i]*c1[i]+b2[i]*c2[i]+x;if(std::fabs(b0[i])<1e-20f)b0[i]=0;x=(b0[i]+b1[i]*f1+b2[i])*c0[i];b2[i]=b1[i];b1[i]=b0[i];}
  return x;
 }
};

// Airwindows XRegion as adapted for B.Angr: params gain, first(high), last(low), nuke, pan.
struct XRegion {
 struct Stage{float k2=0,k4=0,k5=0,k6=0,comp=1,clip=1;float s[8]{};};   // s: L x1 x2 y1 y2, R x1 x2 y1 y2
 Stage st[5];float gain=1,pan=0,w[4]{1,1,1,1};
 void clear(){for(auto&s:st)std::fill(std::begin(s.s),std::end(s.s),0.f);}
 static float clipSin(float x){return std::sin(std::clamp(x,-1.57079633f,1.57079633f));}
 static float run(Stage&g,float x,int c){
  float*z=g.s+c*4;x=clipSin(x*g.clip);
  float y=g.k2*x+g.k4*z[1]-g.k5*z[2]-g.k6*z[3];
  if(std::fabs(y)<1e-20f)y=0;z[1]=z[0];z[0]=x;z[3]=z[2];z[2]=y;
  return y/g.comp;
 }
 void setParameters(const float*p){
  gain=std::pow(p[0]+.5f,4.f);float high=p[1],low=p[2],mid=(high+low)*.5f,spread=1.001f-std::fabs(high-low),nuke=p[3];pan=p[4];
  const float f[5]={high,.5f*(high+mid),mid,.5f*(mid+low),low};
  for(int k=0;k<5;k++){
   float b=f[k]*f[k]*f[k]*20000.f/Rate;if(b<.00009f)b=.00009f;
   auto&g=st[k];g.comp=std::sqrt(b)*6.4f*spread;g.clip=.75f+b*nuke*37.f;
   float K=std::tan(NativePi*b),norm=1.f/(1.f+K/.7071f+K*K);
   g.k2=K/.7071f*norm;g.k4=-g.k2;g.k5=2.f*(K*K-1.f)*norm;g.k6=(1.f-K/.7071f+K*K)*norm;
  }
  w[0]=w[1]=w[2]=1;w[3]=nuke*4;   // four-stage progressive wet control
  if(w[3]<1){w[0]=w[3];w[1]=w[2]=w[3]=0;}else if(w[3]<2){w[1]=w[3]-1;w[2]=w[3]=0;}else if(w[3]<3){w[2]=w[3]-2;w[3]=0;}else w[3]-=3;
 }
 void process(float&l,float&r){
  float io[2]={l,r};
  for(int c=0;c<2;c++){
   float x=io[c]*gain;x=run(st[0],x,c);
   for(int k=0;k<4;k++)if(w[k]>0){float n=x;x=run(st[k+1],x,c);x=x*w[k]+n*(1-w[k]);}
   io[c]=clipSin(x);
  }
  l=io[0]*(1-(pan>0)*pan);r=io[1]*(1+(pan<0)*pan);
 }
};

class AngrFx final:public NativeFx{
 XRegion xr;Butterworth lp,hp,bpL,bpH;
 float corners[Sets][4][5];float base[4]{.56f,.4f,.55f,.68f},variation=1;int set=0;
 float speedBase=.5f,spinBase=0,speedRange=.25f,spinRange=.1f;int source=Random;bool sync=false;int division=2;
 double spb=22050,unit=Rate,count=0;
 float x=.5f,y=.5f,speed=0,speedrand=0,dspeedrand=0,speedflex=0,spin=0,spinrand=0,dspinrand=0,spinflex=0,spindir=1,ang=0;
 float speedlevel=0,speedmax=.1f,spinlevel=0,spinmax=.1f;
 uint32_t seed=0xa4c1b7e3,epoch=0,tick=0;
 float range(float lo,float hi){return lo+(hi-lo)*nativeUnit(seed);}
public:
 AngrFx(){
  lp.set(false,200,8);hp.set(true,4000,8);bpL.set(false,4000,8);bpH.set(true,200,8);
  // Corner offsets around their mean: set 1 is B.Angr's default instances, the others seeded.
  uint32_t s=0x5eedA9u;
  for(int k=0;k<Sets;k++){
   float raw[4][5];
   for(int c=0;c<4;c++)for(int p=0;p<5;p++)raw[c][p]=k?(p==4?(nativeUnit(s)*2-1)*.6f:.15f+.75f*nativeUnit(s)):AngrCorners[c][p];
   for(int p=0;p<5;p++){float m=p==4?0:(raw[0][p]+raw[1][p]+raw[2][p]+raw[3][p])*.25f;for(int c=0;c<4;c++)corners[k][c][p]=raw[c][p]-m;}
  }
  ang=2*NativePi*range(-1,1);
 }
 void configure(const uint8_t*v,float bpm)override{
  for(int i=0;i<4;i++)base[i]=v[i]*.01f;
  variation=v[4]*.02f;set=nativeChoice(v[5],Sets);speedBase=v[6]*.01f;spinBase=(v[7]-50)*.02f;
  source=nativeChoice(v[8],5);float f=v[9]*.02f;speedRange=std::min(1.f,.25f*f);spinRange=std::min(2.f,.1f*f);
  sync=v[10]>=50;division=nativeDivision(v[11]);spb=nativeSamplesPerBeat(bpm);
  unit=sync?std::max(64.0,nativeDivisionBeats(division)*spb):double(Rate);
 }
 void clear()override{
  xr.clear();lp.clear();hp.clear();bpL.clear();bpH.clear();
  x=y=.5f;speedrand=dspeedrand=speedflex=spinrand=dspinrand=spinflex=0;speedlevel=spinlevel=0;speedmax=spinmax=.1f;count=0;tick=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;if(sync)count=unit;}   // retarget on the downbeat
  const float u=float(unit);
  for(int i=0;i<frames;i++){
   float inL=io[i*2],inR=io[i*2+1];
   // Cursor update (B.Angr::play).
   if(count>=unit){dspeedrand=range(-1,1)*speedRange-speedrand;dspinrand=range(-1,1)*spinRange-spinrand;count=0;}else count++;
   float s=.5f*(inL+inR);
   if(source==Lows)s=lp.process(s);else if(source==Mids)s=bpH.process(bpL.process(s));else if(source==Highs)s=hp.process(s);
   float dspeedflex,dspinflex;
   if(source==Random){dspeedflex=dspeedrand/(FlexTime[Random]*u);dspinflex=dspinrand/(FlexTime[Random]*u);}
   else{
    const float a=std::fabs(s),k=1.f/(FlexTime[Level]*Rate),decay=1.f-1.f/(4*Rate);
    speedmax=a>=speedmax?a:decay*speedmax;speedmax=std::max(speedmax,.0001f);
    speedlevel=(1-k)*speedlevel+k*(2*a/speedmax);
    dspeedflex=(2*std::clamp(speedlevel,0.f,1.f)-1)*speedRange-speedflex;
    spinmax=a>=spinmax?a:decay*spinmax;spinmax=std::max(spinmax,.0001f);
    float n=(1-k)*spinlevel+k*(2*a/spinmax);
    if(spinlevel>=.2f&&n<.2f)spindir=range(-1,1)>=0?spindir:-spindir;
    spinlevel=n;dspinflex=spindir*std::clamp(spinlevel,0.f,1.f)*spinRange-spinflex;
   }
   speedrand+=dspeedrand/u;speedflex+=dspeedflex;
   speed=std::clamp(speedBase+.5f*speedflex+.5f*speedrand,0.f,1.f);
   spinrand+=dspinrand/u;spinflex+=dspinflex;
   spin=std::clamp(spinBase+.5f*spinflex+.5f*spinrand,-1.f,1.f);
   ang+=2*NativePi*(10.f/u)*spin;if(ang>1e4f||ang<-1e4f)ang=std::fmod(ang,2*NativePi);
   const float dx=std::sin(ang),dy=std::cos(ang);
   x+=dx/u*speed*speed;y+=dy/u*speed*speed;
   if(x<0){x=0;if(dx<0){ang=-ang;spin=0;}}else if(x>1){x=1;if(dx>0){ang=-ang;spin=0;}}
   if(y<0){y=0;if(dy<0){ang=-NativePi-ang;spin=0;}}else if(y>1){y=1;if(dy>0){ang=-NativePi-ang;spin=0;}}
   // Bilinear crossfade of the four instances at the cursor.
   if(!(tick++&3)){   // coefficients at a 4-sample control rate (the plugin: every sample)
   // Each corner's value is clamped to its control range before blending, as the plugin's controls are.
   const float wgt[4]={(1-x)*(1-y),(1-x)*y,x*y,x*(1-y)};float p[5];
   for(int k=0;k<5;k++){float a=0;
    for(int c=0;c<4;c++){float q=variation*corners[set][c][k];a+=wgt[c]*(k==4?std::clamp(q,-1.f,1.f):std::clamp(base[k]+q,0.f,1.f));}
    p[k]=a;}
   xr.setParameters(p);}
   float l=inL,r=inR;xr.process(l,r);
   io[i*2]=nativeClean(l);io[i*2+1]=nativeClean(r);
  }
 }
};
}
const NativeInfo angrInfo{FxType::Angr,"ANGR","ANGR","AN",4,2,3,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeAngr(){return std::make_unique<AngrFx>();}
}
