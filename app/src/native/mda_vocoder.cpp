// SPDX-License-Identifier: GPL-3.0-only
// MDA VOCODER: a one-to-one recreation of mda Vocoder, Copyright (c) 2002, 2008 Paul Kellett
// (maxim digital audio), MIT licence (https://github.com/elk-audio/mda-vst2).
// An 8 or 16 band channel vocoder: pre-emphasised modulator and carrier run through
// matched 4th-order resonator banks at half the sample rate, each carrier band scaled
// by its modulator band's envelope follower, plus a full-rate high band (envelope of
// the modulator's top end over the differentiated carrier) and a high thru of the voice.
// Changed: the input (L+R) is always the modulator, and the carrier, which was the other
// input channel, is internal: a band-limited saw or pulse chord (or drone) on ROOT, or
// noise, that steps through a chord MOVE at the tempo-synced CHANGE division and realigns
// on transport Start. The input select is replaced by those carrier parameters, and the
// envelope FREEZE setting is omitted (with a held carrier it would never fall silent).
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
namespace {
const char*const bandModes[]={"8 BAND","16 BAND"};
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
 {"OUTPUT",50,NativeUnit::Range,40,60,nullptr,0,-20,20,false," dB"},
 {"HI THRU",40,NativeUnit::Percent,0,60},
 {"HI BAND",40,NativeUnit::Percent,0,70},
 {"ENVELOPE",16,NativeUnit::Range,5,50,nullptr,0,10,10000,true," ms"},
 {"FILTER Q",55,NativeUnit::Percent,30,80},
 {"MID FREQ",67,NativeUnit::Range,40,85,nullptr,0,200,1600,true," Hz"},
 {"BANDS",0,NativeUnit::Choice,0,100,bandModes,2},
 {"CARRIER",0,NativeUnit::Choice,0,50,carriers,3},
 {"ROOT",nativeChoiceValue(12,24),NativeUnit::Choice,0,100,roots,24},
 {"CHORD",nativeChoiceValue(3,8),NativeUnit::Choice,0,100,chords,8},
 {"MOVE",nativeChoiceValue(5,6),NativeUnit::Choice,0,100,moves,6},
 {"CHANGE",nativeDivisionValue(2),NativeUnit::Division,nativeDivisionValue(0),nativeDivisionValue(4)},
};
// The internal carrier: up to four chord notes of two slightly detuned band-limited oscillators.
struct VocoderCarrier {
 int wave=0,root=12,chord=3,move=5,division=2;
 double spb=22050,position=0;uint32_t seed=0x51c0de44;
 std::array<double,8> phase{},inc{};int voices=8,step=-1;
 static float blep(double t,double dt){if(t<dt){double x=t/dt;return float(x+x-x*x-1);}if(t>1-dt){double x=(t-1)/dt;return float(x*x+x+x+1);}return 0;}
 void tune(int s){
  step=s;int offset=moveSteps[move][s%moveCount[move]],n=chordCount[chord];voices=n*2;
  for(int v=0;v<n;v++){double midi=36+root+offset+chordNotes[chord][v];
   for(int k=0;k<2;k++){double hz=440*std::pow(2.0,(midi-69+(k?.06:-.06))/12);inc[size_t(v*2+k)]=std::min(hz/44100,.45);}}
 }
 void reset(){position=0;step=-1;}
 float next(){
  double stepLength=std::max(64.0,nativeDivisionBeats(division)*spb);
  int s=int(position/stepLength)%moveCount[move];if(s!=step)tune(s);
  position+=1;if(position>=stepLength*moveCount[move])position-=stepLength*moveCount[move];
  if(wave==2)return (nativeUnit(seed)*2-1)*.3f;
  float sum=0;
  for(int v=0;v<voices;v++){double&t=phase[size_t(v)];double dt=inc[size_t(v)];
   if(wave==0)sum+=float(2*t-1)-blep(t,dt);
   else{double h=t+.5;if(h>=1)h-=1;sum+=(t<.5?1.f:-1.f)+blep(t,dt)-blep(h,dt);}
   t+=dt;if(t>=1)t-=1;}
  return sum*(.3f/std::sqrt(float(voices)));
 }
};
constexpr int Bands=16;
class MdaVocoderFx final:public NativeFx{
 float gain=0,thru=0,high=0,kout=0;int kval=0,nbnd=8;
 float f[Bands][13]{};   // [band][0 1 2 reson | 3 4 5 6 carrier | 7 8 9 10 modulator | 11 envelope | 12 rate]
 VocoderCarrier carrier;uint32_t epoch=0;
 void suspend(){for(int i=0;i<Bands;i++)for(int j=3;j<12;j++)f[i][j]=0;kout=0;kval=0;}
public:
 void configure(const uint8_t*v,float bpm)override{
  float p1=v[0]*.01f,p2=v[1]*.01f,p3=v[2]*.01f,p4=v[3]*.01f,p5=v[4]*.01f,p6=v[5]*.01f;
  const double tpofs=6.2831853/44100;double rr,th;float sh;
  gain=std::pow(10.f,2*p1-3*p5-2);
  thru=std::pow(10.f,.5f+2*p1);high=p3*p3*p3*thru;thru*=p2*p2*p2;
  int bands=nativeChoice(v[6],2)?16:8;
  if(bands!=nbnd){nbnd=bands;suspend();}
  if(nbnd==8){
   const float hz[8]={0,3000,2200,1500,1080,700,390,190};for(int i=1;i<8;i++)f[i][2]=hz[i];
  }else{
   const float hz[16]={0,5000,4000,3250,2750,2300,2000,1750,1500,1250,1000,750,540,350,195,95};for(int i=1;i<16;i++)f[i][2]=hz[i];
  }
  f[0][12]=float(std::pow(10.0,-1.7-2.7f*p4));   // envelope speed
  rr=.022f/float(nbnd);   // minimum proportional to frequency to stop distortion
  for(int i=1;i<nbnd;i++){f[i][12]=float(.025-rr*double(i));if(f[0][12]<f[i][12])f[i][12]=f[0][12];}
  f[0][12]=.5f*f[0][12];   // only the top band is at full rate
  rr=1-std::pow(10.f,-1-1.2f*p5);
  sh=std::pow(2.f,3*p6-1);   // filter bank range shift
  for(int i=1;i<nbnd;i++){
   f[i][2]*=sh;
   th=std::acos((2*rr*std::cos(tpofs*f[i][2]))/(1+rr*rr));
   f[i][0]=float(2*rr*std::cos(th));f[i][1]=float(-rr*rr);
   f[i][2]*=.96f;   // shift the second stage slightly to stop high resonance peaks
   th=std::acos((2*rr*std::cos(tpofs*f[i][2]))/(1+rr*rr));
   f[i][2]=float(2*rr*std::cos(th));
  }
  carrier.spb=nativeSamplesPerBeat(bpm);
  int wave=nativeChoice(v[7],3),root=nativeChoice(v[8],24),chord=nativeChoice(v[9],8),move=nativeChoice(v[10],6);
  if(root!=carrier.root||chord!=carrier.chord||move!=carrier.move)carrier.step=-1;
  carrier.wave=wave;carrier.root=root;carrier.chord=chord;carrier.move=move;carrier.division=nativeDivision(v[11]);
 }
 void clear()override{suspend();carrier.reset();}
 void process(float*io,int frames)override{
  auto ep=nativeTransportEpoch();if(ep!=epoch){epoch=ep;carrier.reset();}   // downbeat
  float a,b,o=0,aa,bb,oo=kout,g=gain,ht=thru,hh=high,tmp;int k=kval,nb=nbnd;
  for(int n=0;n<frames;n++){
   a=.5f*(io[n*2]+io[n*2+1]);b=carrier.next();   // speech, synth
   tmp=a-f[0][7];f[0][7]=a;a=tmp;   // integrate modulator for HF band and filter bank pre-emphasis
   if(tmp<0)tmp=-tmp;
   f[0][11]-=f[0][12]*(f[0][11]-tmp);   // high band envelope
   o=f[0][11]*(ht*a+hh*(b-f[0][3]));   // high band + high thru
   f[0][3]=b;   // integrate carrier for HF band
   if(++k&1){   // this block runs at half sample rate
    oo=0;
    aa=a+f[0][9]-f[0][8]-f[0][8];f[0][9]=f[0][8];f[0][8]=a;   // apply zeros here instead of in each reson
    bb=b+f[0][5]-f[0][4]-f[0][4];f[0][5]=f[0][4];f[0][4]=b;
    for(int i=1;i<nb;i++){   // filter bank: 4th-order band pass
     tmp=f[i][0]*f[i][3]+f[i][1]*f[i][4]+bb;f[i][4]=f[i][3];f[i][3]=tmp;
     tmp+=f[i][2]*f[i][5]+f[i][1]*f[i][6];f[i][6]=f[i][5];f[i][5]=tmp;
     tmp=f[i][0]*f[i][7]+f[i][1]*f[i][8]+aa;f[i][8]=f[i][7];f[i][7]=tmp;
     tmp+=f[i][2]*f[i][9]+f[i][1]*f[i][10];f[i][10]=f[i][9];f[i][9]=tmp;
     if(tmp<0)tmp=-tmp;
     f[i][11]-=f[i][12]*(f[i][11]-tmp);
     oo+=f[i][5]*f[i][11];
    }
   }
   o+=oo*g;
   float out=nativeClean(o);io[n*2]=io[n*2+1]=out;
  }
  kout=oo;kval=k&1;
  if(std::fabs(f[0][11])<1e-10f)f[0][11]=0;   // catch HF envelope denormal
  for(int i=1;i<nb;i++)if(std::fabs(f[i][3])<1e-10f||std::fabs(f[i][7])<1e-10f)for(int j=3;j<12;j++)f[i][j]=0;   // reson & envelope denormals
  if(std::fabs(o)>10)suspend();   // catch instability
 }
};
}
const NativeInfo mdaVocoderInfo{FxType::MdaVocoder,"MDA VOCODER","MDAVOC","MV",2,0,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeMdaVocoder(){return std::make_unique<MdaVocoderFx>();}
}
