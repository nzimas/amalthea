// SPDX-License-Identifier: GPL-3.0-only
// PITCHNASTY: a one-to-one recreation of Airwindows PitchNasty,
// Copyright (c) 2016 airwindows (Chris Johnson), MIT licence (https://github.com/airwindows/airwindows).
// A primitive, nasty pitch shifter: a read tap runs through a tiny loop buffer at
// NOTE plus BEND, and the loop length is a pitch of its own (GRIND, semitones around
// middle C), so the splices become a buzzing tone. At each wrap it crossfades back
// towards the live write point wherever the two signals meet. FEEDBACK feeds the
// output back into the loop. Changes: Dry/Wet is the host's MIX; FEEDBACK tops out at
// 0.96 (the plugin's 1.0 can sustain forever); the floating-point dither and denormal
// noise are removed, so silence stays exactly silent.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <cmath>
namespace amalthea {
namespace {
const char*const notes[]={"-12","-11","-10","-9","-8","-7","-6","-5","-4","-3","-2","-1","0",
 "+1","+2","+3","+4","+5","+6","+7","+8","+9","+10","+11","+12"};
const char*const grinds[]={"-36","-35","-34","-33","-32","-31","-30","-29","-28","-27","-26","-25","-24",
 "-23","-22","-21","-20","-19","-18","-17","-16","-15","-14","-13","-12","-11","-10","-9","-8","-7","-6",
 "-5","-4","-3","-2","-1","0","+1","+2","+3","+4","+5","+6","+7","+8","+9","+10","+11","+12","+13","+14",
 "+15","+16","+17","+18","+19","+20","+21","+22","+23","+24","+25","+26","+27","+28","+29","+30","+31",
 "+32","+33","+34","+35","+36"};
const NativeParam params[]={
 {"NOTE",0,NativeUnit::Choice,0,100,notes,25},
 {"BEND",0,NativeUnit::Range,0,100,nullptr,0,-12,12,false," st"},
 {"GRIND",nativeChoiceValue(18,73),NativeUnit::Choice,10,60,grinds,73},
 {"FEEDBACK",0,NativeUnit::Percent,0,45},
 {"OUTPUT",100,NativeUnit::Percent,70,100},
};
class PitchNastyFx final:public NativeFx{
 double d[2][10002]{};int inCount=1;double outCount=1.0;bool switchTransition=false;
 double switchAmount[2]{},feedbackSample[2]{};
 double speed=.25,width=168,feedback=0,trim=1;
public:
 void configure(const uint8_t*v,float)override{
  double note=nativeChoice(v[0],25)-12;        // -12 to +12
  double bend=(v[1]*.01*24.0)-12.0;            // -12 to +12
  speed=std::pow(2,note/12.0)*std::pow(2,bend/12.0);
  double grindRef=(1.0/261.6)*44100;           // samples per middle C cycle
  double grindNote=std::pow(2,(nativeChoice(v[2],73)-36)/12.0);
  width=grindRef/grindNote;if(width>9990)width=9990;
  feedback=v[3]*.01*.96;trim=v[4]*.01;
 }
 void clear()override{
  for(auto&c:d)std::fill(std::begin(c),std::end(c),0.0);
  inCount=1;outCount=1.0;switchTransition=false;
  for(int c=0;c<2;c++)switchAmount[c]=feedbackSample[c]=0;
 }
 void process(float*io,int frames)override{
  for(int i=0;i<frames;i++){
   double x[2]={io[i*2],io[i*2+1]};
   for(int c=0;c<2;c++){
    x[c]+=feedbackSample[c]*feedback;
    if(std::fabs(feedbackSample[c])>1.0)x[c]/=std::fabs(feedbackSample[c]);
    d[c][inCount]=x[c];
   }
   inCount++;outCount+=speed;
   if(outCount>inCount&&switchTransition){switchAmount[0]=switchAmount[1]=1.0;switchTransition=false;}
   while(inCount>width)inCount=int(inCount-width);   // (the plugin subtracts once; live changes need a loop)
   if(inCount<0)inCount=0;
   if(outCount>width){outCount-=width;switchTransition=true;}   // reset the transition point
   while(outCount>width)outCount-=width;
   int count=int(std::floor(outCount)),arrayWidth=int(std::floor(width));
   double frac=outCount-std::floor(outCount);
   int a=count-((count>arrayWidth)?arrayWidth+1:0),b=count+1-((count+1>arrayWidth)?arrayWidth+1:0);
   for(int c=0;c<2;c++){
    double y=d[c][a]*(1-frac)+d[c][b]*frac;
    double live=d[c][inCount];
    if(switchAmount[c]>std::fabs(y-live)*2.0)switchAmount[c]=(switchAmount[c]*0.5)+std::fabs(y-live);
    y=(live*switchAmount[c])+(y*(1.0-switchAmount[c]));
    feedbackSample[c]=y;
    io[i*2+c]=nativeClean(float(y*trim));
   }
  }
 }
};
}
const NativeInfo pitchNastyInfo{FxType::PitchNasty,"PITCHNASTY","PNASTY","PY",2,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makePitchNasty(){return std::make_unique<PitchNastyFx>();}
}
