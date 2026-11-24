// SPDX-License-Identifier: GPL-3.0-only
// Rotor and Biztortion: adapted from Nornios native engines (GPL-3.0).
// Amp: Nornios Pedalboard AmpSimulator.sc chain, translated to fixed buffers.
// Granular DSP: original Clouds by Emilie Gillet (MIT), at its native 32 kHz.
#include "nornios_insert.hpp"
#include "clouds/dsp/granular_processor.h"
#include "stmlib/utils/random.h"
#include "EngineRegistry.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace amalthea {
namespace {
constexpr float pi=3.14159265358979323846f;
float db(float v){return std::pow(10.f,v*.05f);}
struct Biquad {
 float b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
 float process(float x){float y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
 void reset(){z1=z2=0;}
 void assign(float B0,float B1,float B2,float A0,float A1,float A2){b0=B0/A0;b1=B1/A0;b2=B2/A0;a1=A1/A0;a2=A2/A0;}
 void lowpass(float freq){float w=2*pi*freq/44100,c=std::cos(w),a=std::sin(w)/1.41421356f;assign((1-c)/2,1-c,(1-c)/2,1+a,-2*c,1-a);}
 void eq(float freq,float gain,int type,float rq=1){
  float w=2*pi*freq/44100,c=std::cos(w),sn=std::sin(w),A=std::sqrt(db(gain));
  if(type==0){float a=sn*rq*.5f;assign(1+a*A,-2*c,1-a*A,1+a/A,-2*c,1-a/A);}
  else {float beta=sn*std::sqrt(A)*std::sqrt(2.f); // shelf slope 1
   if(type<0)assign(A*((A+1)-(A-1)*c+beta),2*A*((A-1)-(A+1)*c),A*((A+1)-(A-1)*c-beta),(A+1)+(A-1)*c+beta,-2*((A-1)+(A+1)*c),(A+1)+(A-1)*c-beta);
   else assign(A*((A+1)+(A-1)*c+beta),-2*A*((A-1)+(A+1)*c),A*((A+1)+(A-1)*c-beta),(A+1)-(A-1)*c+beta,2*((A-1)-(A+1)*c),(A+1)-(A-1)*c-beta);
  }
 }
};
// Freeverb topology: eight damped parallel combs, four serial allpasses per channel.
struct Room {
 std::array<std::array<std::array<float,1640>,8>,2> comb{};
 std::array<std::array<std::array<float,580>,4>,2> ap{};
 int cp[2][8]{},apPos[2][4]{};float damped[2][8]{};
 void clear(){for(auto &c:comb)for(auto &b:c)b.fill(0);for(auto &c:ap)for(auto &b:c)b.fill(0);std::memset(cp,0,sizeof cp);std::memset(apPos,0,sizeof apPos);std::memset(damped,0,sizeof damped);}
 float process(float x,int ch,float room,float damping){
  static const int len[]={1116,1188,1277,1356,1422,1491,1557,1617},al[]={556,441,341,225};float sum=0;
  for(int i=0;i<8;i++){auto &p=cp[ch][i];auto &b=comb[ch][i];float y=b[p];damped[ch][i]=y*(1-damping)+damped[ch][i]*damping;b[p]=x*.015f+damped[ch][i]*(.7f+.28f*room);if(++p>=len[i]+ch*23)p=0;sum+=y;}
  for(int i=0;i<4;i++){auto &p=apPos[ch][i];auto &b=ap[ch][i];float y=b[p];b[p]=sum+.5f*y;sum=y-sum;if(++p>=al[i]+ch*23)p=0;}return sum;
 }
};
float ampCurve(float x){if(x<=-1)return -.9818f;if(x<-.08905f){float a=std::abs(x)-.029847f;return -.75f*(1-std::pow(1-a,12)+.333f*a)+.01f;}if(x<.320018f)return -6.153f*x*x+3.9375f*x;return .6140341f+.05f*x;}
}
struct NorniosInsert::Impl {
 InsertSettings settings{};FxType configured=FxType::None;
 std::unique_ptr<nornios::Engine> biz=nornios::makeBiztortionEngine();
 std::array<std::array<Biquad,9>,2> ampFilters{};
 Room room;float dcIn[2]{},dcOut[2]{},ampDrive=.5f,ampRoom=.3f,ampLevel=1;
 std::array<float,1024> rotorTable{};int grainLimit=32;
 float ampDryGain=.707f,ampWetGain=.707f,ampGain=2.5f,roomAmount=.3f,roomDamp=.3f;
 std::array<float,1025> curve{};std::array<float,4097> tanhCurve{};
 float phase=0,rate=1000,noise=0,rotorLevel=1;bool inverted=false;uint32_t rng=0x7135a14;
 clouds::GranularProcessor granular;
 alignas(16) std::array<uint8_t,118784> large{};
 alignas(16) std::array<uint8_t,65408> small{};
 clouds::ShortFrame in[32]{},out[32]{};
 std::array<std::array<float,2>,128> wetQueue{};
 int fill=0,write=0,read=0,available=0,srcPhase=0;
 float previousIn[2]{},previousOut[2]{},nextOut[2]{};
 std::array<std::array<Biquad,2>,2> downFilter{},upFilter{};
 uint32_t cloudSeed=0x9abcdef;
 Impl(){for(int i=0;i<=1024;i++)curve[i]=ampCurve(i/512.f-1);for(int i=0;i<=4096;i++)tanhCurve[i]=std::tanh(i/256.f-8);clear();}
 float shaper(float x){float index=std::clamp((x+1)*512,0.f,1024.f);int i=std::min(1023,int(index));return curve[i]+(index-i)*(curve[i+1]-curve[i]);}
 float tube(float x){float index=std::clamp((x+8)*256,0.f,4096.f);int i=std::min(4095,int(index));return tanhCurve[i]+(index-i)*(tanhCurve[i+1]-tanhCurve[i]);}
 void clear(){
  configured=FxType::None;grainLimit=32;phase=0;rng=0x7135a14;cloudSeed=0x9abcdef;
  room.clear();for(auto &c:ampFilters)for(auto &f:c)f.reset();
  std::memset(dcIn,0,sizeof dcIn);std::memset(dcOut,0,sizeof dcOut);
  large.fill(0);small.fill(0);std::memset(&granular,0,sizeof granular);
  granular.Init(large.data(),large.size(),small.data(),small.size());granular.set_playback_mode(clouds::PLAYBACK_MODE_GRANULAR);granular.set_quality(0);granular.Prepare();
  fill=write=read=available=srcPhase=0;std::memset(in,0,sizeof in);std::memset(out,0,sizeof out);wetQueue={};
  std::memset(previousIn,0,sizeof previousIn);std::memset(previousOut,0,sizeof previousOut);std::memset(nextOut,0,sizeof nextOut);
  for(auto *filters:{&downFilter,&upFilter})for(auto &c:*filters)for(auto &f:c){f.reset();f.lowpass(13500);}
  biz->prepare(44100,256);
 }
 void send(int cmd,float a,float b=0,float c=0){nornios::EngineCommand x;x.id=cmd;x.args[0]=a;x.args[1]=b;x.args[2]=c;biz->command(x);}
 void configure(FxType t,const InsertSettings&s){
  if(t==configured&&std::memcmp(&s,&settings,sizeof s)==0)return;
  settings=s;configured=t;
  if(t==FxType::Clouds){auto &p=*granular.mutable_parameters();auto &v=s.clouds;p.position=v[0]*.01f;p.size=v[1]*.01f;p.density=v[2]*.01f;p.texture=v[3]*.01f;p.stereo_spread=v[4]*.01f;p.feedback=v[5]*.01f;p.reverb=v[6]*.01f;p.freeze=v[7]>=50;p.pitch=(int(v[8])-50)*.96f;p.dry_wet=1;p.trigger=p.gate=false;}
  if(t==FxType::Rotor){const auto &v=s.rotor;int shape=std::clamp((v[0]+12)/25,0,4);rate=10*std::pow(1250.f,v[1]*.01f);noise=v[2]*.01f;inverted=v[4]>=50;rotorLevel=db((int(v[5])-50)*.24f);int pulse=std::clamp(int(v[3]*.01f*1024),10,1014);
   for(int i=0;i<1024;i++){float p=i/1024.f;rotorTable[i]=shape==0?std::sin(2*pi*p):shape==1?(i<512?2.f*i/512-1:2.f*(1023-i)/512-1):shape==2?2*p-1:shape==3?(i<pulse?1:-1):std::sin(pi*p);}
  }
  if(t==FxType::AmpSim){auto &v=s.amp;ampDrive=v[0]*.01f;ampRoom=v[1]*.01f;ampLevel=db((int(v[6])-50)*.24f);ampDryGain=std::cos(ampDrive*pi*.5f);ampWetGain=std::sin(ampDrive*pi*.5f);ampGain=1.5f+2*ampDrive;roomAmount=.2f*std::pow(4.f,ampRoom);roomDamp=.9f*std::pow(1.f/9,ampRoom)*.4f;
   for(auto &f:ampFilters){f[0].eq(720,-3.3f,-1);f[1].eq(320,-5,-1);f[2].eq(720,-6,-1);f[3].eq(100,(int(v[2])-50)*.4f,-1);f[4].eq(1700,(int(v[3])-50)*.4f,0,1.41421356f);f[5].eq(6500,(int(v[4])-50)*.4f,1);f[6].eq(3900,(int(v[5])-50)*.24f,0);f[7].eq(10000,-25,0);f[8].eq(60,-19,0);}
  }
  if(t==FxType::Biztortion){send(3,0,(int(s.bizLevel[0])-50)*.2f);send(3,1,(int(s.bizLevel[1])-50)*.2f);
   static const int types[]={0,4,5,6,2,3};
   static const float lo[]={20,0,20,0,20,-24,.1f,0,0,0,-100,-.9f,0,1,0,.5f,0,0,0,-100,-.9f,100,1,0,0,0,0,-100,-.9f,0,0,0,0,0,0,0};
   static const float hi[]={20000,3,20000,3,20000,24,10,1,40,100,100,.9f,100,15,100,100,1,40,100,100,.9f,44100,16,100,1,40,100,100,.9f,100,100,1,1,1,1,1};
   for(int m=0;m<8;m++){const auto &mod=s.biz[m];int type=std::clamp((mod.module+10)/20,0,5);send(1,m+1,types[type]);for(int k=0;k<36;k++){float p=mod.value[k]*.01f;float value=(k==0||k==2||k==4||k==21||k==22)?lo[k]*std::pow(hi[k]/lo[k],p):lo[k]+p*(hi[k]-lo[k]);if(k==1||k==3)value=std::round(value);send(2,m+1,k+1,value);}}
  }
 }
 void cloud(float &l,float &r){
  float x[2]={l,r};for(int c=0;c<2;c++)for(auto &f:downFilter[c])x[c]=f.process(x[c]);
  srcPhase+=320;
  if(srcPhase>=441){srcPhase-=441;float frac=1.f-srcPhase/320.f;
   in[fill].l=int16_t(std::clamp(previousIn[0]+frac*(x[0]-previousIn[0]),-.999f,.999f)*32767);
   in[fill].r=int16_t(std::clamp(previousIn[1]+frac*(x[1]-previousIn[1]),-.999f,.999f)*32767);
   if(++fill==32){uint32_t old=stmlib::Random::state();stmlib::Random::Seed(cloudSeed);granular.Prepare();granular.set_grain_limit(grainLimit);granular.Process(in,out,32);cloudSeed=stmlib::Random::state();stmlib::Random::Seed(old);fill=0;for(auto &v:out){wetQueue[write]={v.l/32768.f,v.r/32768.f};write=(write+1)%128;available++;}}
   previousOut[0]=nextOut[0];previousOut[1]=nextOut[1];
   if(available){nextOut[0]=wetQueue[read][0];nextOut[1]=wetQueue[read][1];read=(read+1)%128;available--;}else nextOut[0]=nextOut[1]=0;
  }
  previousIn[0]=x[0];previousIn[1]=x[1];
  l=previousOut[0]+srcPhase/441.f*(nextOut[0]-previousOut[0]);r=previousOut[1]+srcPhase/441.f*(nextOut[1]-previousOut[1]);
  for(auto &f:upFilter[0])l=f.process(l);for(auto &f:upFilter[1])r=f.process(r);
 }
 void process(FxType t,float mix,float *data,int frames){
  if(mix<=0)return;
  if(t==FxType::Biztortion){float l[256],r[256],ol[256],orr[256];for(int i=0;i<frames;i++){l[i]=data[2*i];r[i]=data[2*i+1];}biz->render(l,r,ol,orr,frames);for(int i=0;i<frames;i++){data[2*i]=l[i]+mix*(ol[i]-l[i]);data[2*i+1]=r[i]+mix*(orr[i]-r[i]);}return;}
  for(int i=0;i<frames;i++){
   float l=data[2*i],r=data[2*i+1],wet[2]={l,r};
   if(t==FxType::Clouds)cloud(wet[0],wet[1]);
   if(t==FxType::Rotor){float index=phase*1024;int ix=int(index);float mod=rotorTable[ix]+(index-ix)*(rotorTable[(ix+1)%1024]-rotorTable[ix]);if(inverted)mod=-mod;phase+=rate/44100;phase-=std::floor(phase);
    for(float &v:wet){float n=randomNext(rng)/4294967296.f;v*=mod*(1-noise+noise*n)*rotorLevel;}}
   if(t==FxType::AmpSim)for(int c=0;c<2;c++){auto &f=ampFilters[c];float x=f[1].process(f[0].process(wet[c]));x=x*ampDryGain+shaper(x)*ampWetGain;float y=x-dcIn[c]+.995f*dcOut[c];dcIn[c]=x;dcOut[c]=y;x=tube(f[2].process(y)*ampGain);for(int k=3;k<9;k++)x=f[k].process(x);wet[c]=(x*(1-roomAmount)+room.process(x,c,roomAmount,roomDamp)*roomAmount)*ampLevel;}
   data[2*i]=l+mix*(wet[0]-l);data[2*i+1]=r+mix*(wet[1]-r);
  }
 }
};
NorniosInsert::NorniosInsert():impl(new Impl){}
NorniosInsert::~NorniosInsert()=default;
void NorniosInsert::clear(){impl->clear();}
void NorniosInsert::setGrainLimit(int limit){impl->grainLimit=std::clamp(limit,1,32);}
void NorniosInsert::configure(FxType t,const InsertSettings&s){impl->configure(t,s);}
void NorniosInsert::process(FxType t,float mix,float *data,int n){for(int i=0;i<n;i+=256)impl->process(t,mix,data+2*i,std::min(256,n-i));}
}
