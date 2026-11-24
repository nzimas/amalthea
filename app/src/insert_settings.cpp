// SPDX-License-Identifier: GPL-3.0-only
#include "core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "param_units.hpp"
#include "modulation_inserts.hpp"
namespace amalthea {
static int moduleType(const InsertSettings&s,int m){return std::clamp((int(s.biz[m].module)+10)/20,0,5);}
static const int shape[]={8,9,10,11,12,13,14,15,16},crush[]={17,18,19,20,21,22,23,24},slew[]={25,26,27,28,29,30,31,32},filter[]={0,1,2,3,4,5,6,7};
static const int *indices(int type){return type==1?shape:type==2?crush:type==3?slew:filter;}
// Native effects: MIX, then their own profile (native_fx.hpp).
static int nativeStep(const NativeParam&p){
 int n=p.unit==NativeUnit::Toggle?2:p.unit==NativeUnit::Division?NativeDivisions:p.unit==NativeUnit::Choice?p.choiceCount:0;
 return n>=2?std::max(1,100/(n-1)):1;
}
int insertParameterCount(FxType t,int m,const InsertSettings&s){
 if(auto*n=nativeInfo(t))return 1+n->count;
 if(t==FxType::Flanger||t==FxType::Phaser)return 12;
 if(t==FxType::Rings)return 8;if(t==FxType::Fracture2)return 5;if(t==FxType::CrazyIvan)return 21;if(t==FxType::Bitmurderer)return 20;if(t==FxType::LoFi)return 6;
 if(t==FxType::QDelay)return nornios::qdelayParameterCount();
 if(t==FxType::ChowMatrix)return m?15:6;
 if(t==FxType::CrushClone)return 12;
 if(t==FxType::Frequalizer)return 8;
 if(t==FxType::Filter)return 6;
 if(t==FxType::Space)return 8;
 if(t==FxType::Clouds)return 10;if(t==FxType::AmpSim)return 8;if(t==FxType::Rotor)return 7;
 if(t==FxType::Biztortion){int mt=moduleType(s,m);return 4+(mt==1?9:mt>=2&&mt<=4?8:0);}return 1;
}
const char *insertParameterName(FxType t,int f,int m,const InsertSettings&s){
 if(auto*n=nativeInfo(t))return f<=0||f>n->count?"MIX":n->params[f-1].name;
 if(t==FxType::Flanger){static const char*n[]={"MIX","MODE","WAVEFORM","RATE","DEPTH","VOICES","BASE PITCH","SPACING","FEEDBACK","DAMPING","WIDTH","COMB MIX"};return n[f];}
 if(t==FxType::Phaser){static const char*n[]={"MIX","STAGES","FEEDBACK","MODULATION","LFO RATE","LFO DEPTH","RATE X10","SKEW","DRIVE 1","DRIVE 2","DRIVE 3","STEREO"};return n[f];}
 if(t==FxType::Rings){static const char*n[]={"MIX","TUNING","STRUCTURE","BRIGHTNESS","DAMPING","POSITION","MODES","LEVEL"};return n[f];}
 if(t==FxType::Fracture2){static const char*n[]={"MIX","DRIVE","FRACTURE","THRESHOLD","LEVEL"};return n[f];}
 if(t==FxType::CrazyIvan){static const char*n[]={"MIX","BITMURDERER","BITMASK","AND","XOR","POWER","FREQUENCY","DIFFERENCE","VIBRATO","VIB DEPTH","FEEDBACK","FLANGER RATE","MIN DELAY","MAX DELAY","DISTORTION","DAMPING","FLANGER FEEDBACK","LIMITER","ATTACK","RELEASE","LEVEL"};return n[f];}
 if(t==FxType::Bitmurderer){static const char*n[]={"MIX","BIT 14","BIT 13","BIT 12","BIT 11","BIT 10","BIT 9","BIT 8","BIT 7","BIT 6","BIT 5","BIT 4","BIT 3","BIT 2","BIT 1","BIT 0","AND","XOR","SIGMOID","LEVEL"};return n[f];}
 if(t==FxType::LoFi){static const char*n[]={"MIX","DRIVE","TONE","WOW","NOISE","LEVEL"};return n[f];}
 if(t==FxType::QDelay)return f?nornios::qdelayParameterName(f-1+(f>10)):"MIX";
 if(t==FxType::ChowMatrix){static const char*g[]={"MIX","DELAY TYPE","INSANITY","TEMPO SYNC","LEVEL","GAIN COMPENSATION"};static const char*n[]={"MIX","PARENT","DELAY","PAN","FEEDBACK","LEVEL","LOW PASS","HIGH PASS","PITCH SHIFT","DIFFUSION","DISTORTION","REVERSE","MOD RATE","DELAY MOD","PAN MOD"};return m?n[f]:g[f];}
 if(t==FxType::CrushClone){static const char*n[]={"MIX","TUBE","MECHANICAL","DISTORTION ON","CUTOFF","RESONANCE","FILTER ON","COMPRESSION","PHAT","COMPRESSOR ON","LEVEL","INPUT TRIM"};return n[f];}
 if(t==FxType::Frequalizer){static const char*n[]={"MIX","LEVEL","SOLO","TYPE","FREQUENCY","QUALITY","GAIN","ACTIVE"};return n[f];}
 if(t==FxType::Filter){static const char*n[]={"MIX","MODE","CUTOFF","RESONANCE","DRIVE","LEVEL"};return n[f];}
 static const char *sp[]={"MIX","DECAY","DAMPING","SIZE","PREDELAY","WIDTH","MODULATION","DIFFUSION"};
 if(t==FxType::Space)return sp[f];
 static const char *cl[]={"MIX","POSITION","SIZE","DENSITY","TEXTURE","STEREO","FEEDBACK","REVERB","FREEZE","PITCH"};
 static const char *am[]={"MIX","DRIVE","ROOM","BASS","MID","TREBLE","PRESENCE","LEVEL"};
 static const char *ro[]={"MIX","WAVEFORM","RATE","NOISE","PULSE WIDTH","INVERT","LEVEL"};
 static const char *bz[]={"LOW CUT","LOW SLOPE","HIGH CUT","HIGH SLOPE","PEAK FREQ","PEAK GAIN","PEAK Q","BYPASS",
 "DRIVE","MIX","SYMMETRY","BIAS","TANH AMOUNT","TANH SLOPE","SINE AMOUNT","SINE FREQ","BYPASS",
 "DRIVE","MIX","SYMMETRY","BIAS","RATE REDUCE","BIT DEPTH","DITHER","BYPASS",
 "DRIVE","MIX","SYMMETRY","BIAS","RISE","FALL","REMOVE DC","BYPASS"};
 if(t==FxType::Clouds)return cl[f];if(t==FxType::AmpSim)return am[f];if(t==FxType::Rotor)return ro[f];
 if(t==FxType::Biztortion){static const char *g[]={"MIX","INPUT LEVEL","OUTPUT LEVEL","MODULE"};return f<4?g[f]:bz[indices(moduleType(s,m))[f-4]];}return "MIX";
}
uint8_t &insertParameter(Effect &e,InsertSettings &s,int f,int m){
 if(f==0)return e.value;
 if(nativeIndex(e.type)>=0)return s.native[nativeIndex(e.type)][std::clamp(f-1,0,NativeParams-1)];
 if(e.type==FxType::Flanger)return s.flanger[f-1];if(e.type==FxType::Phaser)return s.phaser[f-1];
 if(e.type==FxType::Rings)return s.rings[f-1];if(e.type==FxType::Fracture2)return s.fracture[f-1];if(e.type==FxType::CrazyIvan)return s.ivan[f-1];if(e.type==FxType::Bitmurderer)return s.murder[f-1];if(e.type==FxType::LoFi)return s.lofi[f-1];
 if(e.type==FxType::QDelay)return s.qdelay[f-1+(f>10)];
 if(e.type==FxType::ChowMatrix)return m?s.nodes[m-1][f-1]:s.matrix[f-1];
 if(e.type==FxType::CrushClone)return s.crush[f-1];
 if(e.type==FxType::Frequalizer)return s.frequalizer[f<3?f-1:2+m*5+(f-3)];
 if(e.type==FxType::Filter)return s.filter[f-1];
 if(e.type==FxType::Space)return s.space[f-1];
 if(e.type==FxType::Clouds)return s.clouds[f-1];if(e.type==FxType::AmpSim)return s.amp[f-1];if(e.type==FxType::Rotor)return s.rotor[f-1];
 if(e.type==FxType::Biztortion){if(f<3)return s.bizLevel[f-1];if(f==3)return s.biz[m].module;return s.biz[m].value[indices(moduleType(s,m))[f-4]];}return e.value;
}
bool steppedInsertParameter(FxType t,int f,int m,const InsertSettings&s){
 if(auto*n=nativeInfo(t))return f>0&&f<=n->count&&nativeStep(n->params[f-1])>1;
 if(t==FxType::Flanger)return f==1||f==2;if(t==FxType::Phaser)return f==6;
 if(t==FxType::CrazyIvan)return f==1||f==3||f==4||f==5;
 if(t==FxType::Bitmurderer)return f>0&&f<19;
 if(t==FxType::QDelay)return f&&nornios::qdelayParameterStep(f-1+(f>10))>1;
 if(t==FxType::ChowMatrix)return m?f==1:f==1||f==3||f==5;
 if(t==FxType::CrushClone)return f==3||f==6||f==8||f==9;
 if(t==FxType::Frequalizer)return f==2||f==3||f==7;
 if(t==FxType::Filter)return f==1;
 if(t==FxType::Clouds)return f==8;if(t==FxType::Rotor)return f==1||f==5;
 if(t==FxType::Biztortion){if(f==3)return true;if(f<4)return false;int i=indices(moduleType(s,m))[f-4];return i==7||i==16||i==24||i==31||i==32;}return false;
}
// QDelay values come from its own parameter ranges; the name picks the unit.
static std::string qdelayText(int p,int v){
 using namespace units;std::string n=nornios::qdelayParameterName(p);float x=nornios::qdelayParameterValue(p,v);
 auto has=[&](const char*k){return n.find(k)!=std::string::npos;};auto ends=[&](const char*k){std::string e=k;return n.size()>=e.size()&&n.compare(n.size()-e.size(),e.size(),e)==0;};
 int step=nornios::qdelayParameterStep(p),i=int(std::lround(x));
 if(step>1){
  auto pick=[&](std::initializer_list<const char*>l){int k=0;for(auto c:l)if(k++==i)return std::string(c);return "CHOICE "+std::to_string(i+1);};
  if(n=="MODE")return pick({"NORMAL","PING-PONG","TAP","CLASSIC PING-PONG"});
  if(n=="SYNC L"||n=="SYNC R"||n=="PHASER SYNC")return pick({"FREE","STRAIGHT","TRIPLET","DOTTED"});
  if(n=="PHASER RATE SYNC")return pick({"1/64","1/32","1/16","1/8","1/4","1/2","1/1","2/1","4/1","8/1","16/1","32/1"});
  if(has("RATE SYNC"))return pick({"1/64","1/32","1/16","1/8","1/4","1/2","1/1"});
  if(n=="DIFF PATH")return pick({"PRE","POST"});if(n=="DIST PRE PATH")return pick({"PRE","FEEDBACK"});
  if(n=="DIST MODE")return pick({"TAPE","TUBE"});if(n=="CRUSH PATH")return pick({"PRE","POST","FEEDBACK"});
  if(n=="SHIFTER MODE")return pick({"PITCH","FREQUENCY"});if(n=="PITCH MODE")return pick({"DRUMS","GENERAL","SMOOTH"});
  if(n=="PITCH PATH"||n=="PHASER PATH")return pick({"FEEDBACK","POST"});if(n=="EQ PATH")return pick({"INPUT","OUTPUT"});
  if(ends("MODE")&&has("BAND1"))return pick({"HIGH PASS","LOW SHELF","HIGH PASS 6 dB"});
  if(ends("MODE")&&has("BAND4"))return pick({"LOW PASS","HIGH SHELF","LOW PASS 6 dB"});
  if(ends("MODE"))return pick({"BAND PASS","PEAK","PEAK"});
  if(step==100)return x>=.5f?"ON":"OFF";
  return "CHOICE "+std::to_string(i+1);
 }
 if(n=="RATE L"||n=="RATE R")return seconds(x);
 if(ends("GAIN")||n=="DIST TRIM")return decibels(x);
 if(n=="FEEDBACK")return std::fabs(x)<.005f?"OFF":gain(std::fabs(x))+(x<0?" INV":"");
 if(n=="HAAS WIDTH")return std::fabs(x)<.0005f?"0 ms":format("%+.2f ms",x*25);
 if(n=="PAN DRY"||n=="PAN WET")return pan(int(std::lround(x*100)));
 if(n=="STEREO")return x<=0?"MONO":"SIDE "+gain(x);
 if(n=="FREQ SHIFT")return std::fabs(x)<.05f?"0 Hz":format("%+.1f Hz",x);
 if(n=="CRUSH SRATE")return hz(44100/(1+std::round(31*x)));
 if(n=="PITCH SHIFT")return semitones(x);
 if(has("RATE")||has("CENTER")||ends("FREQ"))return hz(x);
 if(n=="DIST DRIVE")return decibels(20*x);
 if(n=="DIST COLOR")return hz(2100+2900*x);
 if(n=="CRUSH BITS")return format("%.1f bit",24-23*x);
 if(n=="DUCK THRES")return gain(1-x);
 if(n=="DUCK ATK"||n=="DUCK HLD"||n=="DUCK REL")return seconds(x/1000);
 if(n=="PHASER DEPTH")return format("%.1f st",x);
 if(ends(" Q"))return format("Q %.2f",x);
 if(n=="PITCH MIX"||n=="PHASER MIX")return wet(int(std::lround(x*100)));
 if(n=="PIPO WIDTH"||n=="SWING"||n=="FEEL"||n=="ACCENT")return bipolar(x*100);
 return plain(std::round(x*100));
}
static const char *chowRhythm(int v){
 static const char*n[]={"1/32","1/32 D","1/16 T","1/16","1/16 D","1/8 T","1/8","1/8 D","1/4 T","1/4","1/4 D","1/2 T","1/2","1/2 D","1/1 T","1/1","1/1 D","2/1 T","2/1"};
 float u=v*.01f,ms=1500*u*u,p=std::pow(ms/1500,.34401f);return n[std::clamp(int(18*std::pow(p,1.5f)),0,18)];
}
// The value of a parameter in the units its DSP uses. The formulas follow the
// code that consumes each byte (discovery_inserts, nornios_insert, ported_inserts,
// lush_reverb, multimode_filter); unitless morphs show their 0..100 position.
std::string insertParameterDescription(FxType t,int f,int m,const InsertSettings&s,int v){
 using namespace units;float u=v*.01f;
 if(f==0)return wet(v);
 if(auto*n=nativeInfo(t)){if(f>n->count)return plain(v);char b[48];return nativeValueText(n->params[f-1],v,b,sizeof b);}
 if(t==FxType::QDelay)return qdelayText(f-1+(f>10),v);
 if(t==FxType::Flanger){
  InsertSettings x=s;x.flanger[f-1]=uint8_t(v);auto p=flangerParams(x);
  switch(f){
   case 1:{static const char*n[]={"DRY + COMBS","COMBS ONLY","DRY + ARP COMBS","ARP COMBS ONLY"};return n[p.mode];}
   case 2:{static const char*n[]={"SINE","TRIANGLE","SAW","NOISE","SAMPLE & HOLD","SQUARE"};return n[p.wave];}
   case 3:return hz(std::pow(2.f,p.rate));case 4:return plain(v);
   case 5:return format("%.2f VOICES",p.voices);
   case 6:return noteName(int(p.basePitch))+" / "+hz(8.17579891564371*std::pow(2.,p.basePitch/12.));
   case 7:return semitones(p.spacing);case 8:return v==0?"OFF":plain(v);
   case 9:{float df=std::clamp(.4f*p.damping,.01f,.99f);return "LOW CUT "+hz(-std::log(1-df)*44100/6.28318530718);}
   case 10:return "SIDE "+gain(std::pow(10.f,p.widthDb/20)/3);
   default:return v==50?"OFF":bipolar(p.mix*100)+(p.mix<0?" INV":"");}
 }
 if(t==FxType::Phaser){
  InsertSettings x=s;x.phaser[f-1]=uint8_t(v);auto p=phaserParams(x);
  switch(f){
   case 1:return format("%.1f STAGES",p.stages);case 2:return v==0?"OFF":gain(p.feedback);
   case 3:return plain(v);case 4:return v==0?"STOPPED":hz(p.lfoFreq*(p.freqMult?10:1));
   case 5:return plain(v);case 6:return v>=50?"ON":"OFF";case 7:return bipolar(p.skew,2);
   case 8:return multiple(p.d1);case 9:return multiple(p.d2);case 10:return multiple(p.d3);
   default:return degrees(360*p.stereo);}
 }
 if(t==FxType::ChowMatrix){
  if(m&&f==1){int parent=std::lround(v*(m+1)*.01f)-1;return parent<0?"OFF":parent==0?"INPUT L":parent==1?"INPUT R":"NODE "+std::to_string(parent-1);}
  if(!m&&f==1){static const char*n[]={"NONE","LINEAR","LAGRANGE 3","LAGRANGE 5","SINC 16","SINC 32","BBD SHORT","BBD LONG","BBD ALIEN"};return n[std::clamp(int(std::lround(v*.08f)),0,8)];}
  if(!m)return f==4?decibels((v-50)*.24):f==2?plain(v):v>=50?"ON":"OFF";
  switch(f){
   case 2:return s.matrix[2]>=50?chowRhythm(v):seconds(1.5*u*u);
   case 3:return pan(v);case 4:return v==0?"OFF":gain(.97*u);case 5:return decibels((u-.5)*24);
   case 6:return hz(200*std::pow(100.,u));case 7:return hz(20*std::pow(100.,u));case 8:return semitones((u-.5)*24);
   case 11:return v==0?"OFF":seconds(u);case 12:return v==0?"OFF":hz(5*u*u);
   case 14:return bipolar((2*u-1)*100);default:return plain(v);}
 }
 if(t==FxType::Frequalizer){
  if(f==1)return gain(v*.02);
  if(f==2)return v==0?"NONE":"BAND "+std::to_string(int(std::lround(v*.06f)));
  if(f==3){static const char*n[]={"NONE","HIGH PASS","HIGH PASS 1","LOW SHELF","BAND PASS","ALL PASS","ALL PASS 1","NOTCH","PEAK","HIGH SHELF","LOW PASS 1","LOW PASS"};return n[std::clamp(int(std::lround(v*.11f)),0,11)];}
  if(f==4)return hz(20*std::pow(1000.,u));if(f==5)return format("Q %.2f",.1*std::pow(100.,u));if(f==6)return decibels(48*(u-.5));
  return v>=50?"ON":"OFF";
 }
 if(t==FxType::Filter){
  if(f==1){static const char*n[]={"LOW PASS","BAND PASS","HIGH PASS","NOTCH","ALL PASS","PEAK"};return n[std::clamp((v+10)/20,0,5)];}
  if(f==2)return hz(20*std::pow(900.,u));if(f==3)return format("Q %.2f",.7071*std::pow(12/.7071,u));
  if(f==4)return decibels(.36*v);return decibels(.24*(v-50));
 }
 if(t==FxType::Rotor){
  if(f==1){const char *n[]={"SINE","TRIANGLE","SAW","SQUARE","SEMI-SINE"};return n[std::clamp((v+12)/25,0,4)];}
  if(f==2)return hz(10*std::pow(1250.,u));if(f==4)return format("%.1f%% DUTY",std::clamp(int(1024*u),10,1014)/10.24);
  if(f==5)return v>=50?"ON":"OFF";if(f==6)return decibels(.24*(v-50));return plain(v);
 }
 if(t==FxType::Biztortion){
  if(f==1||f==2)return decibels((v-50)*.2);
  if(f==3){const char *n[]={"EMPTY","WAVESHAPER","BITCRUSHER","SLEW LIMITER","FILTER","SCOPE / PASS"};return n[moduleType(s,m)];}
  switch(indices(moduleType(s,m))[f-4]){
   case 0:case 2:case 4:return hz(20*std::pow(1000.,u));
   case 1:case 3:return std::to_string((int(std::lround(3*u))+1)*12)+" dB/OCT";
   case 5:return decibels(-24+48*u);case 6:return format("Q %.2f",.1+9.9*u);
   case 7:case 16:case 24:case 31:case 32:return v>=50?"ON":"OFF";
   case 8:case 17:case 25:return decibels(40*u);
   case 9:case 18:case 26:return wet(v);
   case 10:case 19:case 27:return bipolar(-100+200*u);
   case 11:case 20:case 28:return bipolar(-.9+1.8*u,2);
   case 13:return multiple(1+14*u);case 15:return multiple(.5+99.5*u);
   case 21:return hz(100*std::pow(441.,u));case 22:return format("%.1f bit",std::pow(16.,u));
   case 29:case 30:{double r=10000*std::pow(1e-5,u);return format(r>=100?"%.0f FS/s":r>=1?"%.1f FS/s":"%.2f FS/s",r);}
   default:return plain(v);}
 }
 if(t==FxType::CrushClone)switch(f){
  case 1:return gain(1+19*u);case 2:return gain(1+29*u);case 4:return hz(20*std::pow(1000.,u));
  case 5:return format("Q %.2f",.7071+3.5*u);case 7:return v==0?"OFF":decibels(-30*u)+" / "+ratio(1+7*u);
  case 10:return decibels(std::min(12.,(u-.5)*36));case 11:return decibels(36*u);
  default:return v>=50?"ON":"OFF";}
 if(t==FxType::Rings)switch(f){
  case 1:return hz(30*std::pow(100.,u));case 4:return "~"+seconds(.0249*std::pow(10.,4*u));
  case 6:return std::to_string(std::min(64,2+2*int(u*31)))+" MODES";case 7:return gain(2*u);default:return plain(v);}
 if(t==FxType::Fracture2)switch(f){
  case 1:return gain(10*u);case 2:{int n=int(u*8);return std::to_string(n)+(n==1?" STAGE":" STAGES");}
  case 4:return gain(u);default:return plain(v);}
 if(t==FxType::CrazyIvan)switch(f){
  case 1:case 3:case 4:case 5:return v>=50?"ON":"OFF";
  case 2:{char b[12];std::snprintf(b,sizeof b,"0x%04X",int(u*32768*.49f));return b;}
  case 6:return hz(300*u*u*u);
  case 7:return "+/-"+hz(300*std::pow(s.ivan[5]*.01,3)*u);
  case 8:return hz(10*u*u*u/2);case 9:return plain(v);
  case 10:case 16:return v==0?"OFF":gain(.99*u);
  case 11:return hz(u/2);case 12:return seconds(10*u/44100.);case 13:return seconds(15000*u/44100.);
  case 14:{double d=30*u;return v==0?"OFF":gain(d<=1?1+d:2*std::sqrt(d));}
  case 15:return v==0?"OFF":v==100?"0 Hz":hz(-44100*std::log(u)/6.28318530718);
  case 18:case 19:return seconds(std::max(.1,200.*u)/1000);
  case 20:return gain(2*u*u);default:return plain(v);}
 if(t==FxType::Bitmurderer)return f==19?gain(2*u):v>=50?"ON":"OFF";
 if(t==FxType::LoFi)switch(f){
  case 1:return gain(std::pow(2.5,u));case 2:return "LP "+hz(2500*std::pow(4.,u));
  case 3:return hz(.5*std::pow(8.,u));case 4:return v==0?"OFF":format("%.0f bit",24*std::pow(.25,u));
  default:return gain(2*u);}
 if(t==FxType::Space)switch(f){
  case 1:return seconds(.5*std::pow(90.,u));case 2:return hz(400+19600*(std::pow(16.,.96-.4*u)-1)/15);
  case 3:return seconds((60+360*u)/1000);case 4:return seconds(200*u*u/1000);
  case 5:return v==0?"MONO":"SIDE "+gain(u);case 6:return v==0?"OFF":seconds(.01*v/1000);
  default:{int n=int(1+(.13+.74*u)*7.999);return std::to_string(n)+" STAGES";}}
 if(t==FxType::Clouds)switch(f){
  case 1:return "~"+seconds(u*1.02)+" BACK";case 2:return seconds(1024*std::pow(16.,u)/32000);
  case 3:return v<47?"REGULAR "+std::to_string(47-v):v>53?"RANDOM "+std::to_string(v-53):"NONE";
  case 6:return v==0?"OFF":gain(u);case 8:return v>=50?"ON":"OFF";case 9:return semitones((v-50)*.96);
  default:return plain(v);}
 if(t==FxType::AmpSim)switch(f){
  case 1:return gain(1.5+2*u);case 2:return wet(int(std::lround(20*std::pow(4.,u))));
  case 3:case 4:case 5:return decibels((v-50)*.4);default:return decibels((v-50)*.24);}
 if(steppedInsertParameter(t,f,m,s))return v>=50?"ON":"OFF";
 return plain(v);
}
}

namespace amalthea {
int insertParameterStep(FxType t,int f,int m,const InsertSettings&s){
 if(auto*n=nativeInfo(t))return f>0&&f<=n->count?nativeStep(n->params[f-1]):1;
 if(t==FxType::Flanger&&f==1)return 33;if(t==FxType::Flanger&&f==2)return 20;
 if(t==FxType::QDelay&&f)return nornios::qdelayParameterStep(f-1+(f>10));
 if(t==FxType::ChowMatrix){if(m&&f==1)return std::max(1,int(std::lround(100.f/(m+1))));if(!m&&f==1)return 13;}
 if(t==FxType::Frequalizer){if(f==2)return 17;if(f==3)return 9;}
 if(!steppedInsertParameter(t,f,m,s))return 1;
 if(t==FxType::Filter&&f==1)return 20;if(t==FxType::Rotor&&f==1)return 25;if(t==FxType::Biztortion&&f==3)return 20;return 100;
}
}
