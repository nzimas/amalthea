// SPDX-License-Identifier: GPL-3.0-only
// REPSYCHO: a one-to-one recreation of mda RePsycho!, Copyright (c) 1999-2000, 2008 Paul Kellett
// (maxim digital audio), MIT licence (https://github.com/elk-audio/mda-vst2).
// A transient-triggered slice repitcher: when L+R rises above the threshold, and the
// hold time since the last trigger has passed, a new half-second slice starts; it is
// recorded as it plays and read back at the tuned (lower) rate, crossfaded in over
// 80 samples from the previous slice, shaped by an exponential decay (or swell) and
// muted when the slice ends. LOW quality sums to mono without interpolation; HIGH
// keeps stereo and interpolates. Changed: MIX is left to the host; the last output
// sample carries across blocks for the crossfade (the original restarted it at zero
// each block); TRIGGER can add (or use only) tempo-synced retriggers at RATE, aligned
// to the downbeat on transport Start.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
#include <vector>
namespace amalthea {
namespace {
const char*const tunes[]={"-24 ST","-23 ST","-22 ST","-21 ST","-20 ST","-19 ST","-18 ST","-17 ST","-16 ST","-15 ST","-14 ST","-13 ST",
 "-12 ST","-11 ST","-10 ST","-9 ST","-8 ST","-7 ST","-6 ST","-5 ST","-4 ST","-3 ST","-2 ST","-1 ST","0 ST"};
const char*const qualities[]={"LOW","HIGH"};
const char*const triggers[]={"INPUT","TEMPO","BOTH"};
const NativeParam params[]={
 {"TUNE",nativeChoiceValue(12,25),NativeUnit::Choice,nativeChoiceValue(5,25),nativeChoiceValue(24,25),tunes,25},
 {"FINE",100,NativeUnit::Range,70,100,nullptr,0,-100,0,false," ct"},
 {"DECAY",50,NativeUnit::Bipolar,15,55},
 {"THRESH",60,NativeUnit::Range,40,80,nullptr,0,-30,0,false," dB"},
 {"HOLD",45,NativeUnit::Range,10,70,nullptr,0,10,260,false," ms"},
 {"QUALITY",0,NativeUnit::Choice,0,100,qualities,2},
 {"TRIGGER",0,NativeUnit::Choice,0,100,triggers,3},
 {"RATE",nativeDivisionValue(10),NativeUnit::Division,nativeDivisionValue(7),nativeDivisionValue(12)},
};
constexpr int Size=22050;
class RePsychoFx final:public NativeFx{
 std::vector<float> buffer,buffer2;
 float thr=.25f,env=1,gai=0,tun=.5f,wet=.5f,buf=0,buf2=0,x=0,x2=0;int tim=Size+1,dtim=441;bool high=false;
 int trigger=0,division=10;double spb=22050,clock=0;uint32_t epoch=0;
public:
 RePsychoFx():buffer(Size+2,0.f),buffer2(Size+2,0.f){}
 void configure(const uint8_t*v,float bpm)override{
  int semis=nativeChoice(v[0],25)-24;float fine=v[1]*.01f,decay=v[2]*.01f,thresh=v[3]*.01f,hold=v[4]*.01f;
  dtim=441+int(.5*Size*hold);
  thr=float(std::pow(10.0,1.5*thresh-1.5));
  env=decay>.5f?float(1+.003*std::pow(decay-.5,5.0)):float(1+.025*std::pow(decay-.5,5.0));
  tun=float((semis+(fine-1.0))/24.0);tun=float(std::pow(10.0,.60206*tun));
  high=nativeChoice(v[5],2)==1;wet=high?1.f:.5f;
  trigger=nativeChoice(v[6],3);division=nativeDivision(v[7]);spb=nativeSamplesPerBeat(bpm);
 }
 void clear()override{std::fill(buffer.begin(),buffer.end(),0.f);std::fill(buffer2.begin(),buffer2.end(),0.f);tim=Size+1;gai=0;x=x2=buf=buf2=0;clock=0;}
 void process(float*io,int frames)override{
  auto ep=nativeTransportEpoch();if(ep!=epoch){epoch=ep;clock=0;}   // downbeat
  float we=wet,tu=tun,en=env,ga=gai,xx=buf,xx2=buf2;int ti=tim,dti=dtim;
  double period=std::max(64.0,nativeDivisionBeats(division)*spb);
  for(int n=0;n<frames;n++){
   float a=io[n*2],b=io[n*2+1];
   bool beat=false;if(trigger){if(clock<1)beat=true;clock+=1;if(clock>=period)clock-=period;}
   if((trigger!=1&&a+b>thr&&ti>dti)||beat){ga=1;ti=0;}   // trigger
   if(ti<Size){   // play out
    if(high){
     buffer[size_t(ti)]=a;buffer2[size_t(ti)]=b;
     if(ti<80){   // fade in
      if(ti==0){xx=x;xx2=x2;}
      float y=buffer[size_t(int(ti*tu))],y2=buffer2[size_t(int(ti*tu))];
      x=float(xx*(1-.0125*ti)+y*.0125*ti);x2=float(xx2*(1-.0125*ti)+y2*.0125*ti);
     }else{
      float it1=float(ti*tu);int of1=int(it1),of2=of1+1;it1-=float(of1);float it2=1-it1;   // interpolation
      x=it2*buffer[size_t(of1)]+it1*buffer[size_t(of2)];x2=it2*buffer2[size_t(of1)]+it1*buffer2[size_t(of2)];
     }
    }else{
     buffer[size_t(ti)]=a+b;
     if(ti<80){if(ti==0)xx=x;float y=buffer[size_t(int(ti*tu))];x=float(xx*(1-.0125*ti)+y*.0125*ti);}
     else x=buffer[size_t(int(ti*tu))];
     x2=x;
    }
    ti++;ga*=en;
   }else ga=0;   // mute
   io[n*2]=nativeClean(x*ga*we);io[n*2+1]=nativeClean(x2*ga*we);
  }
  tim=ti;gai=ga;buf=xx;buf2=xx2;
 }
};
}
const NativeInfo rePsychoInfo{FxType::RePsycho,"REPSYCHO","PSYCHO","RP",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeRePsycho(){return std::make_unique<RePsychoFx>();}
}
