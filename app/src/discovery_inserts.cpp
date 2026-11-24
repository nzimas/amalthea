// Native insert adapters. See docs/INSERT-PORTS.md for provenance and adaptations.
#include "discovery_inserts.hpp"
#include "../third_party/rings/rings/dsp/resonator.h"
#include "../third_party/smartelectronix/crazyivan/Flanger.h"
#include "../third_party/smartelectronix/crazyivan/OnePoleLP.h"
#include "../third_party/smartelectronix/crazyivan/EnvFollower.h"
#include <algorithm>
#include <cmath>
#include <array>
namespace amalthea {
namespace {
constexpr float SR=44100.f, PI=3.14159265358979323846f;
float clean(float x){return std::isfinite(x)?std::clamp(x,-4.f,4.f):0;}
float bitwise(float input,int mask,bool And,bool Xor,bool power){
 // XOR must never manufacture an idle signal at digital silence.
 if(std::abs(input)<1e-10f)return 0;
 float x=std::min(std::abs(input),1.f);if(power)x=std::cbrt(x);
 int bits=int(x*32767);if(Xor)bits^=mask;if(And)bits&=mask;
 x=float(bits&32767)/32767;if(power)x=x*x*x;
 return std::copysign(x,input);
}
struct IvanChannel {
 CMovingDelay delay{500002};CFlanger flange;COnePoleLP low;CTriOsc sweep,vibrato;EnvFollower env;float output=0;
 IvanChannel(){sweep.SampleRateF=vibrato.SampleRateF=flange.T.SampleRateF=SR;clear();}
 void clear(){delay.suspend();flange.suspend();flange.T.suspend();low.suspend();sweep.suspend();vibrato.suspend();env.suspend();output=0;}
 void configure(const std::array<float,20>&p){
  low.SetParams(1-p[14]);float f=300*p[5]*p[5]*p[5],delta=f*p[6];
  sweep.SetParams(std::min(500000.f,SR/std::max(.01f,std::abs(f-delta))),std::min(500000.f,SR/std::max(.01f,std::abs(f+delta))));
  flange.SetParams(p[10],p[15]*.99f,1,p[13]*30,p[11]*10,p[12]*15000);env.SetParams(p[17]*200,p[18]*200);
 }
 float process(float in,const std::array<float,20>&p){
  if(p[0]>=.5f)in=bitwise(in,int(p[1]*32768*.49f),p[2]>=.5f,p[3]>=.5f,p[4]>=.5f);
  float v=vibrato.GetVal(10*p[7]*p[7]*p[7]),v2=v*v;
  float sine=v*(1.569799213f+.5f*(-1.279196852f+.139598426f*v2)*v2);
  float fb=p[9]*.99f;
  output=clean(flange.GetVal(delay.GetVal(low.GetVal(fb*output+(1-fb)*in),sweep.GetVal(sine*10*p[8]*p[8]*p[8]))));
  float e=env.GetVal(output),lim=p[16]*.5f;
  output*=e<1?1-lim*e:(1-2*lim)+lim/e;
  return clean(output*2*p[19]*p[19]);
 }
};
struct LoFiChannel {
 std::array<float,4096> delay{};int write=0;float phase=0,env=0,hpIn=0,hpOut=0,lp=0,toneHP=0,last=0,clock=0;
 uint32_t rng=0x619287u;
 float process(float x,const std::array<float,20>&p){
  float drive=p[0],tone=p[1],wow=p[2],noise=p[3];
  float high=x-hpIn+.996445f*hpOut;hpIn=x;hpOut=high;
  float a=std::exp(-1/(SR*(std::abs(high)>env?.1f:1.f)));env=a*env+(1-a)*std::abs(high);
  float threshold=.8f-.47f*drive,ratio=.15f*std::pow(.01f/.15f,drive);
  float gain=env>threshold?std::pow(std::max(env/threshold,1.f),ratio-1):1;
  high*=gain/(threshold+(1-threshold)*ratio);
  float rate=.5f*std::pow(8.f,wow),depth=std::pow(2.f,(1+29*wow)/1200)-1;
  phase+=rate/SR;if(phase>=1)phase-=1;
  delay[write]=high;
  float offset=2+SR*depth/(4*rate)*(1+std::sin(phase*2*PI));
  float read=write-offset;while(read<0)read+=delay.size();int at=int(read);float t=read-at;
  auto get=[&](int i){return delay[(i+int(delay.size()))%delay.size()];};
  float y0=get(at-1),y1=get(at),y2=get(at+1),y3=get(at+2);
  float y=y1+.5f*t*(y2-y0+t*(2*y0-5*y1+4*y2-y3+t*(3*(y1-y2)+y3-y0)));
  write=(write+1)%delay.size();
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  float n=(float(rng&65535)/32767.5f-1)*noise*.12f*std::min(1.f,env*12);
  y=std::tanh(y*std::pow(2.5f,drive)+n);
  float cut=2500*std::pow(4.f,tone),alpha=1-std::exp(-2*PI*cut/SR);lp+=alpha*(y-lp);
  clock+=1.f/std::max(1.f,std::ceil(noise*3));if(clock>=1){clock-=1;float bits=24*std::pow(.25f,noise),q=std::pow(2.f,bits-1);last=std::round(lp*q)/q;}
  y=.7f*lp+.3f*last;
  float hp=40*std::pow(42.25f,tone);toneHP+=(1-std::exp(-2*PI*hp/SR))*(y-toneHP);
  return clean((y-toneHP)*(1-.34f*drive)*p[4]*2);
 }
};
class Discovery final:public nornios::Engine {
 FxType type;std::array<float,20> p{};
 std::unique_ptr<std::array<IvanChannel,2>> ivan;
 std::unique_ptr<std::array<rings::Resonator,2>> rings;
 std::unique_ptr<std::array<LoFiChannel,2>> lofi;
 float oldDrive=0,oldThreshold=0,oldLevel=0;
public:
 const char* name()const override{return fxName(type);}
 const std::vector<nornios::CommandDef>&commands()const override{static const std::vector<nornios::CommandDef> c;return c;}
 const std::vector<nornios::PollDef>&polls()const override{static const std::vector<nornios::PollDef> c;return c;}
 bool pollValue(int,float&)override{return false;}
 explicit Discovery(FxType t):type(t){if(t==FxType::CrazyIvan)ivan=std::make_unique<std::array<IvanChannel,2>>();if(t==FxType::Rings)rings=std::make_unique<std::array<rings::Resonator,2>>();if(t==FxType::LoFi)lofi=std::make_unique<std::array<LoFiChannel,2>>();}
 void prepare(double,int)override{clear();}
 void clear(){if(ivan)for(auto&c:*ivan)c.clear();if(rings)for(auto&r:*rings)r.Init();if(lofi)*lofi={};oldDrive=oldThreshold=oldLevel=0;}
 void command(const nornios::EngineCommand&c)override{if(c.id==4){clear();return;}if(c.id!=1)return;int i=int(c.args[0]);if(i>=0&&i<int(p.size()))p[i]=std::clamp(c.args[1],0.f,1.f);}
 void render(const float*L,const float*R,float*outL,float*outR,int n)override{
  if(rings){for(auto&r:*rings){r.set_frequency((30*std::pow(100.f,p[0]))/SR);r.set_structure(std::min(p[1],.999999f));r.set_brightness(p[2]);r.set_damping(std::min(p[3],.999999f));r.set_position(.001f+.998f*p[4]);r.set_resolution(2+2*int(p[5]*31));}float aux[32]{};(*rings)[0].Process(L,outL,aux,n);(*rings)[1].Process(R,outR,aux,n);for(int i=0;i<n;i++){outL[i]=clean(outL[i]*p[6]*2);outR[i]=clean(outR[i]*p[6]*2);}return;}
  if(ivan)for(auto&c:*ivan)c.configure(p);
  int mask=0;if(type==FxType::Bitmurderer)for(int j=0;j<15;j++)mask=(mask<<1)|(p[j]>=.5f);
  for(int i=0;i<n;i++)for(int ch=0;ch<2;ch++){
   float x=ch?R[i]:L[i],y=x;
   if(ivan)y=(*ivan)[ch].process(x,p);
   else if(lofi)y=(*lofi)[ch].process(x,p);
   else if(type==FxType::Bitmurderer)y=bitwise(x,mask,p[15]>=.5f,p[16]>=.5f,p[17]>=.5f)*p[18]*2;
   else if(type==FxType::Fracture2){
    float blend=float(i+1)/n,density=oldDrive+(p[0]*10-oldDrive)*blend,threshold=oldThreshold+(p[2]-oldThreshold)*blend,level=oldLevel+(p[3]-oldLevel)*blend;
    double v=x*density;for(int stage=0;stage<int(p[1]*8);stage++)v=std::clamp(v*(std::abs(v)+1),-1.e12,1.e12);
    y=std::sin(v);if(v>PI*.5)y=y*threshold+1-threshold;else if(v<-PI*.5)y=y*threshold-1+threshold;
    y*=level;
   }
   (ch?outR:outL)[i]=clean(y);
  }
  oldDrive=p[0]*10;oldThreshold=p[2];oldLevel=p[3];
 }
};
}
std::unique_ptr<nornios::Engine> makeDiscoveryInsert(FxType t){return std::make_unique<Discovery>(t);}
void configureDiscoveryInsert(nornios::Engine&e,FxType t,const InsertSettings&s){
 auto send=[&](const auto&values){for(int i=0;i<int(values.size());i++){nornios::EngineCommand c;c.id=1;c.args[0]=i;c.args[1]=values[i]*.01f;e.command(c);}};
 switch(t){case FxType::Rings:send(s.rings);break;case FxType::Fracture2:send(s.fracture);break;case FxType::CrazyIvan:send(s.ivan);break;case FxType::Bitmurderer:send(s.murder);break;case FxType::LoFi:send(s.lofi);break;default:break;}
}
}
