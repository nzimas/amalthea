// SPDX-License-Identifier: GPL-3.0-only
// CYANIDE: a one-to-one recreation of Smartelectronix Cyanide 2 (C) Bram de Jong,
// GPL-3.0 (https://github.com/bdejong/smartelectronix, Cyanide2).
// Pre gain, a pre filter, the spline waveshaper (optionally 16x oversampled through
// Cyanide's cascaded polyphase allpass half-band stages), a post filter and post gain.
// The filters are Cyanide's 3rd-order Butterworth allpass-sum crossover, morphing from
// low pass to high pass, gliding to a new cutoff one table step per four samples. The
// shaper is the same 1024-entry table built from the Bezier (under 4 points) or cubic
// B-spline (4 or more) through the curve's squared points, with Cyanide's linear
// extension above full scale.
// Changes: the drawn curve becomes SHAPE, Cyanide's 16 factory preset curves (only
// their curves; their knob settings are not applied), and MORPH blends from a straight
// line (no shaping) to that curve. Gains are dB ranges rather than Cyanide's scaled
// knob; DRY/WET is the host MIX (linear, not Cyanide's equal-power law); the denormal
// noise is replaced by flushing filter state (so silence stays exact zeros). MIDI CC,
// GUI, program and state handling are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
namespace amalthea {
namespace {
constexpr int TableSize=1024,PreCount=1024,MaxPoints=24,Shapes=16;
constexpr float MaxError=.000001f;
inline bool tiny(float x){return std::fabs(x)<.000001f;}   // IS_DENORMAL

// ---- Spline.cpp ----
struct SplinePoint{float x,y;};
struct KnoopVector{long I;float i;};
SplinePoint recursiveSpline(const SplinePoint*d,long i,long j,float t){
 if(j==1)return {d[i].x+t*(d[i+1].x-d[i].x),d[i].y+t*(d[i+1].y-d[i].y)};
 SplinePoint a=recursiveSpline(d,i,j-1,t),b=recursiveSpline(d,i+1,j-1,t);a.x+=t*(b.x-a.x);a.y+=t*(b.y-a.y);return a;
}
float blend1(long k,long t,const KnoopVector*u,float v){
 if(t==1)return (u[k].i<=v&&v<u[k+1].i)?1.f:0.f;
 if(u[k+t-1].I==u[k].I&&u[k+t].I==u[k+1].I)return 0;
 if(u[k+t-1].I==u[k].I)return (u[k+t].i-v)/(u[k+t].i-u[k+1].i)*blend1(k+1,t-1,u,v);
 if(u[k+t].I==u[k+1].I)return (v-u[k].i)/(u[k+t-1].i-u[k].i)*blend1(k,t-1,u,v);
 return (v-u[k].i)/(u[k+t-1].i-u[k].i)*blend1(k,t-1,u,v)+(u[k+t].i-v)/(u[k+t].i-u[k+1].i)*blend1(k+1,t-1,u,v);
}
void computeIntervals(KnoopVector*u,long n,long t){
 for(long j=0;j<=n+t;j++){if(j<t)u[j].I=0;else if(j<=n)u[j].I=j-t+1;else u[j].I=n-t+2;u[j].i=float(u[j].I);}
}
SplinePoint computePoint(const KnoopVector*u,long n,long t,float v,const SplinePoint*control){
 SplinePoint o{0,0};long index=long(v);
 for(long k=index;k<index+t;k++){float b=blend1(k,t,u,v);o.x+=control[k].x*b;o.y+=control[k].y*b;}
 (void)n;return o;
}

// ---- Shaper.cpp: the transfer table of one curve ----
struct ShapeTable{std::array<float,TableSize+1> data{};float a=1,b=1;};
void buildShape(const SplinePoint*p,long n,ShapeTable&out){
 constexpr long degree=4;SplinePoint p2[MaxPoints];
 for(long i=0;i<n;i++){float x=p[i].x,y=1.f-p[i].y;p2[i]={x*x,y*y};}   // FillP2
 KnoopVector u[MaxPoints+degree+1];if(n>=degree)computeIntervals(u,n-1,degree);
 float dx=1.f/TableSize,wanted=dx;out.data[0]=0;
 for(long i=1;i<TableSize;i++,wanted+=dx){
  float lo=0,hi=n>=degree?float(n-degree+1):1.f,mid=(lo+hi)*.5f,error=10;long iter=0;SplinePoint P{0,0};
  while(error>MaxError&&iter<300){
   P=n>=degree?computePoint(u,n-1,degree,mid,p2):recursiveSpline(p2,0,n-1,mid);
   if(P.x<=wanted){lo=mid;error=wanted-P.x;}
   if(P.x>=wanted){hi=mid;error=P.x-wanted;}
   float next=(lo+hi)*.5f;iter++;
   if(next==mid)break;   // the bisection is stuck: later iterations would repeat this exact point
   mid=next;
  }
  out.data[size_t(i)]=P.y;
 }
 out.data[TableSize]=2*out.data[TableSize-1]-out.data[TableSize-2];
 float x1=p2[n-2].x,y1=p2[n-2].y,x2=p2[n-1].x,y2=p2[n-1].y,temp=x1-x2;
 out.a=temp==0?(y2>y1?1.f:-1.f):(y1-y2)/temp;out.a=std::clamp(out.a,-2.f,2.f);out.b=y2;
}

// The factory curves of Cyanide 2's 16 programs (Presets.h), in their stored order.
struct Curve{int n;SplinePoint p[MaxPoints];};
const Curve curves[Shapes]={
 {3,{{0,1},{.37562f,.27805f},{1,.28293f}}},
 {4,{{0,1},{.28293f,.34634f},{.28294f,.34146f},{1,.36585f}}},
 {5,{{0,1},{.21463f,.40976f},{.21951f,.40976f},{.21952f,.40488f},{1,.40976f}}},
 {11,{{0,1},{1e-5f,.80488f},{.2f,.80488f},{.20001f,.60488f},{.40488f,.60976f},{.40489f,.4f},{.6f,.38537f},{.60001f,.21951f},{.78537f,.21463f},{.81951f,0},{1,0}}},
 {5,{{0,1},{.30244f,.74146f},{.49268f,0},{.70244f,.74146f},{1,1}}},
 {4,{{0,1},{.49268f,.15122f},{.6f,0},{1,0}}},
 {3,{{0,1},{1e-5f,0},{1,1}}},
 {5,{{0,.64878f},{.10732f,.7122f},{.23415f,.57561f},{.41951f,.77561f},{1,0}}},
 {2,{{0,.49756f},{1,.49268f}}},
 {24,{{0,1},{1e-5f,.92683f},{3e-5f,.85854f},{.0878f,.85366f},{.15122f,.85366f},{.15123f,.77073f},{.15124f,.69268f},{.29268f,.69268f},
  {.30732f,.69756f},{.30733f,.57561f},{.3122f,.54146f},{.4439f,.55122f},{.45366f,.55122f},{.45367f,.39512f},{.46829f,.38537f},{.61463f,.39024f},
  {.62927f,.38049f},{.62928f,.23415f},{.62929f,.22927f},{.78537f,.22927f},{.79512f,.22439f},{.79513f,0},{.79514f,0},{1,0}}},
 {6,{{0,.5561f},{.21951f,.2f},{.45366f,.5561f},{.45853f,.5561f},{.45854f,.5561f},{1,0}}},
 {3,{{0,1},{.52195f,.3561f},{1,.14146f}}},
 {5,{{0,0},{.43415f,1},{.51707f,1},{.60488f,1},{1,0}}},
 {5,{{0,1},{.1561f,1},{.34634f,1},{.40977f,.30732f},{1,0}}},
 {5,{{0,0},{1e-5f,1},{.05854f,1},{.69268f,1},{1,1}}},
 {24,{{0,1},{.04878f,.92683f},{.04879f,.92683f},{.1561f,.91707f},{.16098f,.9122f},{.19024f,.8f},{.19025f,.8f},{.33658f,.80488f},
  {.33659f,.79024f},{.36098f,.62439f},{.36585f,.62439f},{.49754f,.6439f},{.49755f,.62927f},{.49756f,.48293f},{.49757f,.46341f},{.64877f,.43415f},
  {.64878f,.42439f},{.69268f,.25366f},{.69269f,.2439f},{.80487f,.23415f},{.80488f,.23415f},{.81951f,.09756f},{.81952f,.09268f},{1,0}}},
};
const char*const shapeNames[Shapes]={"SOFT CLIP","HARDER CLIP","BADASS CLIP","FUZZY","BARK DOGGIE","LOUDNESS","ONLY BAZZ","BROKEN",
 "BROKEN 2","3 BITS","HIGH FIVE","COLD TURKEY","SOS","GATE","INV PSYCHO","MORE FUZZ"};
// Built once (deterministic constant data): the B-spline bisection is too slow per instance.
const std::vector<ShapeTable>&shapeTables(){
 static const std::vector<ShapeTable> tables=[]{std::vector<ShapeTable> t(Shapes);for(int s=0;s<Shapes;s++)buildShape(curves[s].p,curves[s].n,t[size_t(s)]);return t;}();
 return tables;
}

// ---- ButterXOver.cpp ----
struct XOverCoeffs{float aA1,aA2,bA2;};
struct XOverTable{std::array<XOverCoeffs,PreCount> pre{};
 XOverTable(){
  const float samplerate=44100;double freq=100,mfreq=std::pow((samplerate*.5f-10.f)/100.0,1.0/PreCount);long i;
  for(i=0;i<PreCount-1;i++){
   long double Wd=(2.0*3.141592653*freq)/samplerate,tmp=std::tan(Wd*.5f),tmp2=1.0/(1.0+tmp+tmp*tmp);
   pre[size_t(i)]={float((-1.0+tmp)/(1.0+tmp)),float((1-tmp+tmp*tmp)*tmp2),float((2.0*tmp*tmp-2.0)*tmp2)};freq*=mfreq;
  }
  long double Wd=(2.0*3.141592653*(samplerate*.5f-10.f))/samplerate,tmp=std::tan(Wd*.5f),tmp2=1.0+tmp+tmp*tmp;
  pre[size_t(i)]={float((-1.0+tmp)/(1.0+tmp)),float((1-tmp+tmp*tmp)/tmp2),float((2.0*tmp*tmp-2.0)/tmp2)};
 }
};
const XOverTable&xoverTable(){static const XOverTable t;return t;}
struct ButterXOver{
 const XOverCoeffs*pre=xoverTable().pre.data();
 float outA1=0,in_1=0,out_2_A2=0,out_1_A2=0,in_2=0,m1=.35f,m2=.35f;long freq=(PreCount-1)<<2;
 void suspend(){in_2=outA1=in_1=out_2_A2=out_1_A2=0;}
 void denormalise(){if(tiny(outA1))outA1=0;if(tiny(out_2_A2))out_2_A2=0;if(tiny(out_1_A2))out_1_A2=0;if(tiny(in_1))in_1=0;if(tiny(in_2))in_2=0;}
 void settype(float t){
  const float amt=4,tmp=.5f*amt;t=.5f*std::tanh(amt*t-tmp)/std::tanh(tmp)+.5f;
  float a=float(std::cos(t*3.141592*.5))*.7f,b=float(std::sin(t*3.141592*.5))*.7f;m2=(a-b)*.5f;m1=(a+b)*.5f;
 }
 void processchange(float*x,int n,long filter){   // in place
  long realfreq=freq>>2,add=filter>realfreq?1:-1;
  for(int i=0;i<n;i++){
   const auto&c=pre[realfreq];float in=x[i];
   float outA2=c.aA2*(in-out_2_A2);outA1=c.aA1*(in-outA1)+in_1;outA2+=c.bA2*(in_1-out_1_A2)+in_2;
   out_2_A2=out_1_A2;out_1_A2=outA2;in_2=in_1;in_1=in;x[i]=m2*outA2+m1*outA1;
   if(realfreq!=filter){freq+=add;realfreq=freq>>2;}
  }
 }
};

// ---- Polyphase.cpp: one 2x half-band stage ----
struct Polyphase{
 float uout[3]{},uin_1[3]{},lout[3]{},lin_1[3]{};
 void suspend(){for(int i=0;i<3;i++)uout[i]=uin_1[i]=lout[i]=lin_1[i]=0;}
 void denormalise(){for(int i=0;i<3;i++){if(tiny(lout[i]))lout[i]=0;if(tiny(uout[i]))uout[i]=0;if(tiny(lin_1[i]))lin_1[i]=0;if(tiny(uin_1[i]))uin_1[i]=0;}}
 void upsample(const float*in,float*out,int n){
  for(int i=0,o=0;i<n;i++,o+=2){
   lout[0]=(in[i]-lout[0])*0.039151597734460045f+lin_1[0];uout[0]=(in[i]-uout[0])*0.147377113601046600f+uin_1[0];uin_1[0]=lin_1[0]=in[i];
   uout[1]=(uout[0]-uout[1])*0.48246854276970014f+uin_1[1];lout[1]=(lout[0]-lout[1])*0.30264684832849340f+lin_1[1];lin_1[1]=lout[0];uin_1[1]=uout[0];
   out[o]=lout[2]=(lout[1]-lout[2])*0.6746159185469639f+lin_1[2];out[o+1]=uout[2]=(uout[1]-uout[2])*0.8830050257693731f+uin_1[2];
   uin_1[2]=uout[1];lin_1[2]=lout[1];
  }
 }
 void downsample(const float*in,float*out,int n){
  for(int i=0,x=0;i<n;i++,x+=2){
   uout[0]=(in[x]-uout[0])*0.1473771136010466f+uin_1[0];lout[0]=(in[x+1]-lout[0])*0.039151597734460045f+lin_1[0];lin_1[0]=in[x+1];uin_1[0]=in[x];
   uout[1]=(uout[0]-uout[1])*0.48246854276970014f+uin_1[1];lout[1]=(lout[0]-lout[1])*0.3026468483284934f+lin_1[1];lin_1[1]=lout[0];uin_1[1]=uout[0];
   uout[2]=(uout[1]-uout[2])*0.8830050257693731f+uin_1[2];lout[2]=(lout[1]-lout[2])*0.6746159185469639f+lin_1[2];lin_1[2]=lout[1];uin_1[2]=uout[1];
   out[i]=(lout[2]+uout[2])*.5f;
  }
 }
};

const NativeParam params[]={
 {"SHAPE",0,NativeUnit::Choice,0,100,shapeNames,Shapes},
 {"MORPH",100,NativeUnit::Percent,45,100},
 {"PRE GAIN",80,NativeUnit::Range,66,96,nullptr,0,-40,10,false," dB"},
 {"PRE FILTER",100,NativeUnit::Range,45,100,nullptr,0,100,22040,true," Hz"},
 {"PRE LP-HP",0,NativeUnit::Percent,0,100},
 {"POST GAIN",80,NativeUnit::Range,64,84,nullptr,0,-40,10,false," dB"},
 {"POST FILTER",79,NativeUnit::Range,40,100,nullptr,0,100,22040,true," Hz"},
 {"POST LP-HP",0,NativeUnit::Percent,0,30},
 {"OVERSAMPLE",100,NativeUnit::Toggle,0,100},
};
class CyanideFx final:public NativeFx{
 static constexpr int MaxFrames=32;
 ShapeTable table;int shape=-1,morph=-1;
 ButterXOver pre[2],post[2];Polyphase up[2][4],down[2][4];
 std::array<float,MaxFrames*16> buffer1{},buffer2{};
 float preGain=1,postGain=1;long preIndex=PreCount-1,postIndex=PreCount-1;bool oversample=true,previousOversample=true;
 void shapeProcess(float*x,int n)const{
  float a=table.a,ab=table.a-table.b;
  for(int i=0;i<n;i++){
   float in=x[i],scaled=std::fabs(in)*TableSize;unsigned index=unsigned(std::min(scaled,float(TableSize)));float frac=scaled-float(index);
   if(index<unsigned(TableSize)){float y=table.data[index]+frac*(table.data[index+1]-table.data[index]);x[i]=in>=0?y:-y;}
   else x[i]=in>=0?a*in-ab:a*in+ab;
  }
 }
public:
 CyanideFx(){shapeTables();}
 void configure(const uint8_t*v,float)override{
  int s=nativeChoice(v[0],Shapes),m=v[1];
  if(s!=shape||m!=morph){
   shape=s;morph=m;const auto&src=shapeTables()[size_t(s)];float k=m*.01f;
   for(int i=0;i<=TableSize;i++)table.data[size_t(i)]=float(i)/TableSize*(1-k)+src.data[size_t(i)]*k;   // the straight line has a = b = 1
   table.a=1-k+src.a*k;table.b=1-k+src.b*k;
  }
  preGain=std::pow(10.f,(-40+.5f*v[2])/20);postGain=std::pow(10.f,(-40+.5f*v[5])/20);
  preIndex=long(v[3]*.01f*(PreCount-1.f));postIndex=long(v[6]*.01f*(PreCount-1.f));
  for(auto&f:pre)f.settype(v[4]*.01f);for(auto&f:post)f.settype(v[7]*.01f);
  oversample=v[8]>=50;
 }
 void clear()override{
  for(int c=0;c<2;c++){pre[c].suspend();post[c].suspend();for(int i=0;i<4;i++){up[c][i].suspend();down[c][i].suspend();}}
 }
 void process(float*io,int frames)override{
  frames=std::min(frames,MaxFrames);
  if(previousOversample!=oversample){for(auto&ch:up)for(auto&p:ch)p.suspend();for(auto&ch:down)for(auto&p:ch)p.suspend();previousOversample=oversample;}
  float*b1=buffer1.data(),*b2=buffer2.data();
  for(int c=0;c<2;c++){
   for(int i=0;i<frames;i++)b1[i]=io[i*2+c]*preGain;
   pre[c].processchange(b1,frames,preIndex);
   if(!oversample)shapeProcess(b1,frames);
   else{
    auto&u=up[c];auto&d=down[c];
    u[0].upsample(b1,b2,frames);u[1].upsample(b2,b1,frames*2);u[2].upsample(b1,b2,frames*4);u[3].upsample(b2,b1,frames*8);
    shapeProcess(b1,frames*16);
    d[0].downsample(b1,b2,frames*8);d[1].downsample(b2,b1,frames*4);d[2].downsample(b1,b2,frames*2);d[3].downsample(b2,b1,frames);
    for(auto&p:u)p.denormalise();for(auto&p:d)p.denormalise();
   }
   post[c].processchange(b1,frames,postIndex);
   pre[c].denormalise();post[c].denormalise();
   for(int i=0;i<frames;i++)io[i*2+c]=nativeClean(b1[i]*postGain);
  }
 }
};
}
const NativeInfo cyanideInfo{FxType::Cyanide,"CYANIDE","CYANID","CY",4,2,8,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeCyanide(){return std::make_unique<CyanideFx>();}
}
