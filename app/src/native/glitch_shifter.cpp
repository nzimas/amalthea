// SPDX-License-Identifier: GPL-3.0-only
// GLITCHSHIFTER: a one-to-one recreation of Airwindows GlitchShifter,
// Copyright (c) 2016 airwindows (Chris Johnson), MIT licence (https://github.com/airwindows/airwindows).
// A pitch shifter that runs a delay tap through a rotating 24-bit integer buffer and,
// whenever the tap catches the buffer end or the dry tap, splices to the logged zero
// crossing that best matches the current trajectory (biased towards recent crossings
// by TIGHTEN, which also shortens the buffer), so it glitches rather than smears.
// The 'air' stage restores the highs lost to interpolation; FEEDBACK feeds the tap
// back into the buffer. Changes: Dry/Wet is the host's MIX (default 50, as in the
// plugin); NOTE defaults to +7 so the default setting shifts; the floating-point
// dither and denormal noise are removed, so silence stays exactly silent.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <cmath>
#include <cstdlib>
#include <vector>
namespace amalthea {
namespace {
const char*const notes[]={"-12","-11","-10","-9","-8","-7","-6","-5","-4","-3","-2","-1","0",
 "+1","+2","+3","+4","+5","+6","+7","+8","+9","+10","+11","+12"};
const NativeParam params[]={
 {"NOTE",nativeChoiceValue(19,25),NativeUnit::Choice,0,100,notes,25},
 {"TRIM",50,NativeUnit::Bipolar,46,54},
 {"TIGHTEN",50,NativeUnit::Percent,20,80},
 {"FEEDBACK",0,NativeUnit::Percent,0,40},
};
struct Channel {
 std::vector<int32_t> p=std::vector<int32_t>(131076,0);
 int32_t offset[258]{},pastzero[258]{},previous[258]{},third[258]{},fourth[258]{};
 int32_t temp=0,lasttemp=0,thirdtemp=0,fourthtemp=0,sincezerocross=0;
 int crosses=0,realzeroes=0;double position=0;bool splicing=false;
 double airPrev=0,airEven=0,airOdd=0,airFactor=0;
 void clear(){
  std::fill(p.begin(),p.end(),0);
  for(int i=0;i<258;i++)offset[i]=pastzero[i]=previous[i]=third[i]=fourth[i]=0;
  temp=lasttemp=thirdtemp=fourthtemp=sincezerocross=0;crosses=realzeroes=0;position=0;splicing=false;
  airPrev=airEven=airOdd=airFactor=0;
 }
 double air(double x,bool flip){
  airFactor=airPrev-x;
  if(flip){airEven+=airFactor;airOdd-=airFactor;airFactor=airEven;}
  else{airOdd+=airFactor;airEven-=airFactor;airFactor=airOdd;}
  airOdd=(airOdd-((airOdd-airEven)/256.0))/1.0001;
  airEven=(airEven-((airEven-airOdd)/256.0))/1.0001;
  if(std::fabs(airOdd)<1e-30)airOdd=0;if(std::fabs(airEven)<1e-30)airEven=0;
  airPrev=x;return x+airFactor;
 }
 void log(int count,int countone,int counttwo){
  if((p[countone]>0&&p[count]<0)||(p[countone]<0&&p[count]>0)){   // source crossed zero
   crosses++;realzeroes++;
   if(crosses>256)crosses=0;        // wrap crosses to keep adding new crosses
   if(realzeroes>256)realzeroes=256; // don't wrap realzeroes, full buffer, use all
   offset[crosses]=count;pastzero[crosses]=p[count];previous[crosses]=p[countone];third[crosses]=p[counttwo];
  }
 }
 void splice(double bias){
  // The closest match to the current rate of zero cross and positioning.
  position=0;double diff=99999999.0;int best=0;
  for(int scan=realzeroes-1;scan>=0;scan--){
   int scanone=scan+crosses;if(scanone>256)scanone-=256;
   double howdiff=double((temp-pastzero[scanone])+(lasttemp-previous[scanone])+(thirdtemp-third[scanone])+(fourthtemp-fourth[scanone]));
   howdiff-=double(scan*bias);
   if(howdiff<diff){diff=howdiff;best=scanone;}
  }
  position=offset[best]-sincezerocross;crosses=0;realzeroes=0;splicing=true;
 }
 void move(double speed,int width,double bias){
  position-=speed;
  if(position>width){   // caught up to the buffer end
   if(realzeroes>0)splice(bias);else{position-=width;crosses=0;realzeroes=0;splicing=true;}
  }
  if(position<0){       // caught up to the dry tap
   if(realzeroes>0)splice(bias);else{position+=width;crosses=0;realzeroes=0;splicing=true;}
  }
 }
 void tap(int gcount,int width){
  int count=gcount-int(std::floor(position));
  while(count<0)count+=width;
  while(count>width)count-=width;
  double frac=position-std::floor(position);
  temp=0;
  temp+=int32_t(p[count]*(1-frac));
  temp+=p[count+1];
  temp+=int32_t(p[count+2]*frac);
  temp-=int32_t(((p[count]-p[count+1])-(p[count+1]-p[count+2]))/50);   // interpolation hacks
  temp/=2;
  if(std::abs(temp)>8388352)temp=lasttemp+(lasttemp-thirdtemp);   // stick with the trajectory
  sincezerocross++;
  if(sincezerocross<0||sincezerocross>width)sincezerocross=0;
  if(splicing){temp=(temp+(lasttemp+(lasttemp-thirdtemp)))/2;splicing=false;}
  if((lasttemp>0&&temp<0)||(lasttemp<0&&temp>0))sincezerocross=0;   // delay tap crossed zero
 }
 void shift(){fourthtemp=thirdtemp;thirdtemp=lasttemp;lasttemp=temp;}
};
class GlitchShifterFx final:public NativeFx{
 Channel ch[2];
 int gcount=0,lastwidth=16386,width=32768;bool flip=false;double speed=0,bias=.125,feedback=0;
public:
 void configure(const uint8_t*v,float)override{
  int note=nativeChoice(v[0],25)-12;double trim=v[1]*.02-1.0;
  speed=((1.0/12.0)*note)+trim;
  if(speed<0)speed*=0.5;   // maximum pitch lowering cannot be to 0 Hz
  double C=v[2]*.01;width=int(65536-((1-std::pow(1-C,2))*65530.0));bias=std::pow(C,3);
  feedback=(v[3]*.01)/1.5;
 }
 void clear()override{for(auto&c:ch)c.clear();gcount=0;lastwidth=16386;flip=false;}
 void process(float*io,int frames)override{
  for(int i=0;i<frames;i++){
   double in[2]={ch[0].air(io[i*2],flip),ch[1].air(io[i*2+1],flip)};
   flip=!flip;
   if(lastwidth!=width){for(auto&c:ch){c.crosses=0;c.realzeroes=0;}lastwidth=width;}   // global reset
   gcount++;
   if(gcount<0||gcount>width)gcount=0;
   int count=gcount,countone=count-1,counttwo=count-2;
   while(countone<0)countone+=width;
   while(counttwo<0)counttwo+=width;
   for(int c=0;c<2;c++){
    auto&h=ch[c];
    h.p[count+width]=h.p[count]=int32_t((in[c]*8388352.0)+double(h.lasttemp*feedback));   // 24-bit linear space
    h.log(count,countone,counttwo);
   }
   for(auto&h:ch)h.move(speed,width,bias);
   for(auto&h:ch){h.tap(gcount,width);h.shift();}
   for(int c=0;c<2;c++){double y=double(ch[c].lasttemp)/8388352.0;io[i*2+c]=nativeClean(float(y));}
  }
 }
};
}
const NativeInfo glitchShifterInfo{FxType::GlitchShifter,"GLITCHSHIFTER","GSHIFT","GT",2,1,1,50,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeGlitchShifter(){return std::make_unique<GlitchShifterFx>();}
}
