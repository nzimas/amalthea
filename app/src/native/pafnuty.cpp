// SPDX-License-Identifier: GPL-3.0-only
// PAFNUTY: a one-to-one recreation of Airwindows Pafnuty2,
// Copyright (c) airwindows (Chris Johnson), MIT licence (https://github.com/airwindows/airwindows).
// Chebyshev waveshaping: the second to tenth Chebyshev polynomials of the input,
// each with its own bipolar amount (squared, with Airwindows' per-order scaling and
// sign), are summed and added to the input by INV/WET, which can also subtract the
// harmonics to cancel ones already in the signal. Changes: the plugin's defaults are
// all neutral, so here SECOND, THIRD and INV/WET start engaged; the output is clamped
// to +/-4 (high orders explode above full scale); the floating-point dither and
// denormal noise are removed, so silence stays exactly silent.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <cmath>
namespace amalthea {
namespace {
const NativeParam params[]={
 {"SECOND",75,NativeUnit::Bipolar,30,80},
 {"THIRD",70,NativeUnit::Bipolar,30,75},
 {"FOURTH",50,NativeUnit::Bipolar,35,65},
 {"FIFTH",50,NativeUnit::Bipolar,35,65},
 {"SIXTH",50,NativeUnit::Bipolar,40,60},
 {"SEVENTH",50,NativeUnit::Bipolar,40,60},
 {"EIGHTH",50,NativeUnit::Bipolar,42,58},
 {"NINTH",50,NativeUnit::Bipolar,42,58},
 {"TENTH",50,NativeUnit::Bipolar,42,58},
 {"INV/WET",85,NativeUnit::Bipolar,60,95},
};
class PafnutyFx final:public NativeFx{
 double k[11]{},amount=0;
 static double shape(double v,double scale){double x=((v*.02)-1.0)*scale;return x*std::fabs(x);}
public:
 void configure(const uint8_t*v,float)override{
  k[2]=shape(v[0],1.0);k[3]=shape(v[1],-0.60);k[4]=shape(v[2],-0.60);
  k[5]=shape(v[3],0.45);k[6]=shape(v[4],0.45);k[7]=shape(v[5],-0.38);
  k[8]=shape(v[6],-0.38);k[9]=shape(v[7],0.35);k[10]=shape(v[8],0.35);
  amount=shape(v[9],1.0);
 }
 void clear()override{}
 void process(float*io,int frames)override{
  for(int i=0;i<frames*2;i++){
   double x=io[i],effxct=0;
   double p2=x*x,p3=p2*x,p4=p3*x,p5=p4*x,p6=p5*x,p7=p6*x,p8=p7*x,p9=p8*x,p10=p9*x;
   if(k[2]!=0.0)effxct+=(2*p2)*k[2];
   if(k[3]!=0.0)effxct+=((4*p3)-(3*x))*k[3];
   if(k[4]!=0.0)effxct+=((8*p4)-(8*p2))*k[4];
   if(k[5]!=0.0)effxct+=((16*p5)-(20*p3)+(5*x))*k[5];
   if(k[6]!=0.0)effxct+=((32*p6)-(48*p4)+(18*p2))*k[6];
   if(k[7]!=0.0)effxct+=((64*p7)-(112*p5)+(56*p3)-(7*x))*k[7];
   if(k[8]!=0.0)effxct+=((128*p8)-(256*p6)+(160*p4)-(32*p2))*k[8];
   if(k[9]!=0.0)effxct+=((256*p9)-(576*p7)+(432*p5)-(120*p3)+(9*x))*k[9];
   if(k[10]!=0.0)effxct+=((512*p10)-(1280*p8)+(1120*p6)-(400*p4)+(50*p2))*k[10];
   io[i]=nativeClean(float(x+(effxct*amount)));
  }
 }
};
}
const NativeInfo pafnutyInfo{FxType::Pafnuty,"PAFNUTY","PAFNTY","PF",3,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makePafnuty(){return std::make_unique<PafnutyFx>();}
}
