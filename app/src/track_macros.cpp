// Native Amalthea macro DSP. Design references and provenance: docs/MACRO-EFFECTS.md.
#include "track_macros.hpp"
#include <algorithm>
#include <cmath>
#include "param_units.hpp"
namespace amalthea {
const char *macroName(int m){static const char*n[]={"ABRASION","CORROSION","MODULATION"};return n[m];}
const char *macroParameterName(int m,int p){static const char*n[3][8]={
 {"DRIVE","CHARACTER","CROSSOVER","BODY","EDGE","FOLD","TONE","MIX"},
 {"BIT CRUSH","RATE REDUCE","JITTER","ASYMMETRY","SLEW","TONE","DRIVE","MIX"},
 {"FLANGE","PHASER","RATE","FEEDBACK","STEREO","COLOR","TONE","MIX"}};return n[m][p];}
// The value text follows the coefficients configure() derives below.
std::string macroValueText(int m,int p,int v){
 using namespace units;float u=v*.01f;
 if(p==7)return wet(v);
 if(m==0){switch(p){
  case 0:return gain(1+180*u*u);case 2:return hz(300*std::pow(30.f,u));
  case 3:case 4:return gain(.1f+1.9f*u);case 6:return hz(400*std::pow(45.f,u));
  case 1:return v==0?"SOFT CLIP":v==100?"HARD CLIP":plain(v);
  default:return v==0?"OFF":plain(v);}}
 if(m==1){switch(p){
  case 0:return bits(std::round(15-13*u)+1);case 1:return hz(44100*std::pow(1.f/441,u));
  case 2:return v==0?"OFF":"+/-"+format("%.0f%% RATE",80*u);
  case 3:{float a=(u-.5f)*1.6f;return v==50?"SYMMETRIC":decibels(20*std::log10((1+a)/(1-a)));}
  case 4:{if(v==0)return "OFF";float a=std::pow(.01f,u);return hz(-std::log(1-a)*44100/6.28318530718);}
  case 5:return hz(400*std::pow(45.f,u));default:return gain(1+12*u*u);}}
 switch(p){
  case 0:return v==0?"OFF":seconds(350*u/44100);case 2:return hz(.025f*std::pow(400.f,u));
  case 3:{float f=(u-.5f)*1.7f;return v==50?"OFF":gain(std::fabs(f))+(f<0?" INV":"");}
  case 4:return degrees(180*u);case 6:return hz(400*std::pow(45.f,u));
  default:return v==0?"OFF":plain(v);}
}
static const std::array<float,2049>&sines(){static const auto t=[] {std::array<float,2049>a{};for(int n=0;n<=2048;n++)a[n]=std::sin(n*6.28318530718/2048);return a;}();return t;}
static float sine(float p){p-=std::floor(p);float x=p*2048;int i=int(x);const auto&t=sines();return t[i]+(t[i+1]-t[i])*(x-i);}
static float lowpass(float hz){return 1-std::exp(-6.28318530718f*hz/44100);}
static float softclip(float x){x=std::clamp(x,-3.f,3.f);float x2=x*x;return x*(27+x2)/(27+9*x2);}
static float fold(float x){float z=x+1;z-=4*std::floor(z*.25f);return 1-std::abs(z-2);}
TrackMacros::TrackMacros(){sines();settings=defaultMacros();for(auto &s:settings)s.value.fill(255);configure(defaultMacros());}
void TrackMacros::configure(const std::array<MacroSettings,3>&s){for(int m=0;m<3;m++){
 if(settings[m].value==s[m].value)continue;settings[m]=s[m];auto &c=coeff[m];float p[8];for(int n=0;n<8;n++)p[n]=s[m].value[n]*.01f;c.mix=p[7];c.tone=lowpass(400*std::pow(45.f,p[6]));
 if(m==0){c.drive=1+180*p[0]*p[0];c.character=p[1];c.cross=lowpass(300*std::pow(30.f,p[2]));c.body=.1f+1.9f*p[3];c.edge=.1f+1.9f*p[4];c.fold=p[5];}
 if(m==1){c.bits=std::pow(2.f,std::round(15-13*p[0]));c.rate=std::pow(1.f/441,p[1]);c.jitter=p[2];c.asym=(p[3]-.5f)*1.6f;c.slew=std::pow(.01f,p[4]);c.tone=lowpass(400*std::pow(45.f,p[5]));c.drive=1+12*p[6]*p[6];}
 if(m==2){c.flange=p[0];c.phaser=p[1];c.lfoStep=.025f*std::pow(400.f,p[2])/44100;c.feedback=(p[3]-.5f)*1.7f;c.spread=p[4]*.5f;c.color=p[5];}
}}
void TrackMacros::clear(){state={};}
void TrackMacros::wet(int m,float l,float r,float&ol,float&orr){auto &s=state[m];const auto&c=coeff[m];float x[2]={l,r},y[2]{};
 if(m==0)for(int ch=0;ch<2;ch++){
  s.low[ch]+=c.cross*(x[ch]-s.low[ch]);float v=(s.low[ch]*c.body+(x[ch]-s.low[ch])*c.edge)*c.drive;
  auto shape=[&](float u){float bias=.12f*c.character;float a=softclip(u+bias)-softclip(bias);float hard=std::clamp(u,-1.f,1.f);float z=a+(hard-a)*c.character;return .8f*(z+(fold(u*.55f)-z)*c.fold);};
  float z=(shape((v+s.last[ch])*.5f)+shape(v))*.5f;s.last[ch]=v;
  s.tone[ch]+=c.tone*(z-s.tone[ch]);y[ch]=s.tone[ch];
 }
 if(m==1){
  if(s.holdPhase>=1){s.holdPhase-=std::floor(s.holdPhase);for(int ch=0;ch<2;ch++){float v=x[ch]*c.drive*(x[ch]<0?1-c.asym:1+c.asym);s.held[ch]=std::round(std::clamp(v,-2.f,2.f)*c.bits)/c.bits;}}
  s.random^=s.random<<13;s.random^=s.random>>17;s.random^=s.random<<5;
  float jitter=(int(s.random&65535)-32768)/32768.f;s.holdPhase+=c.rate*(1+c.jitter*.8f*jitter);
  for(int ch=0;ch<2;ch++){s.slew[ch]+=c.slew*(s.held[ch]-s.slew[ch]);s.tone[ch]+=c.tone*(s.slew[ch]-s.tone[ch]);y[ch]=s.tone[ch];}
 }
 if(m==2){for(int ch=0;ch<2;ch++){
  float sweep=.5f+.5f*sine(s.phase+ch*c.spread),delay=8+200*c.color+350*c.flange*sweep;int n=int(delay);float frac=delay-n;
  int a=(s.position+2048-n)&2047,b=(a+2047)&2047;float tap=s.delay[2*a+ch]+frac*(s.delay[2*b+ch]-s.delay[2*a+ch]);
  s.delay[2*s.position+ch]=std::clamp(x[ch]+tap*c.feedback,-4.f,4.f);
  float flanged=x[ch]+(.5f*(x[ch]+tap)-x[ch])*c.flange;
  float v=std::clamp(flanged+s.last[ch]*c.feedback,-4.f,4.f),a1=.97f-(.25f+.65f*c.color)*sweep;
  for(int k=0;k<8;k++){float z=-a1*v+s.allpass[ch][k];s.allpass[ch][k]=v+a1*z;v=z;}
  s.last[ch]=v;float z=flanged+(.5f*(flanged+v)-flanged)*c.phaser;
  s.tone[ch]+=c.tone*(z-s.tone[ch]);y[ch]=s.tone[ch];
 }s.position=(s.position+1)&2047;s.phase+=c.lfoStep;if(s.phase>=1)s.phase-=1;}
 for(int ch=0;ch<2;ch++){float hp=y[ch]-s.dcX[ch]+.997f*s.dcY[ch];s.dcX[ch]=y[ch];s.dcY[ch]=std::abs(hp)<1e-15f?0:hp;y[ch]=s.dcY[ch];}
 ol=y[0];orr=y[1];
}
void TrackMacros::process(float&l,float&r,const std::array<float,3>&send){for(int m=0;m<3;m++){
 auto &s=state[m];float inputL=l*send[m],inputR=r*send[m];if(std::abs(inputL)+std::abs(inputR)>1e-12f){s.active=true;s.quiet=0;}
 if(!s.active)continue;float wl,wr;wet(m,inputL,inputR,wl,wr);float mix=coeff[m].mix;
 // The wet return stays within 6 dB of a decaying memory of what was sent (-60 dB over
 // 0.5 s; 1.5 s for MODULATION's feedback): drive and crushing cannot blow a quiet tail
 // up into a lasting loud one, and nothing sustains itself after the send stops.
 static const float decay[3]={std::pow(.001f,1/(.5f*44100)),std::pow(.001f,1/(.5f*44100)),std::pow(.001f,1/(1.5f*44100))};
 s.memory=std::max(std::max(std::abs(inputL),std::abs(inputR)),s.memory*decay[m]);
 float peak=std::max(std::abs(wl),std::abs(wr)),target=peak>2*s.memory?2*s.memory/peak:1;
 s.guard=target<s.guard?target:s.guard+(target-s.guard)*.002f;wl*=s.guard;wr*=s.guard;
 l=l*(1-send[m]*mix)+wl*mix;r=r*(1-send[m]*mix)+wr*mix;
 if(!std::isfinite(l)||!std::isfinite(r)){l=r=0;s=State{};}
 if(std::abs(wl)+std::abs(wr)<1e-10f){if(++s.quiet>88200)s=State{};}else s.quiet=0;
}}
}
