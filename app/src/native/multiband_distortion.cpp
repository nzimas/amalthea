// SPDX-License-Identifier: GPL-3.0-only
// MULTIBAND DIST: a one-to-one recreation of Airwindows MultiBandDistortion,
// Copyright (c) 2016 airwindows (Chris Johnson), MIT licence (https://github.com/airwindows/airwindows).
// A one-pole split at FREQUENCY feeds two sine-breakup distortions (gain, then a
// threshold whose hardness is set by H/L HARD), mixed back by H/L MATRIX and STABLE
// (the clean signal). The distortion runs on the raw sample and on an interpolated
// half-way sample, and Airwindows' antialiasing filter applies the result as a
// difference against the dry input. Changes: the floating-point dither and denormal
// noise are removed (filter states are flushed to zero instead), so silence stays
// exactly silent.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <cmath>
namespace amalthea {
namespace {
const NativeParam params[]={
 {"FREQUENCY",50,NativeUnit::Percent,25,75},
 {"H GAIN",0,NativeUnit::Range,0,40,nullptr,0,0,48,false," dB"},
 {"L GAIN",0,NativeUnit::Range,0,40,nullptr,0,0,48,false," dB"},
 {"H HARD",0,NativeUnit::Percent,0,70},
 {"L HARD",0,NativeUnit::Percent,0,70},
 {"H MATRIX",100,NativeUnit::Percent,40,100},
 {"L MATRIX",100,NativeUnit::Percent,40,100},
 {"STABLE",100,NativeUnit::Percent,30,100},
 {"OUTPUT",75,NativeUnit::Range,60,85,nullptr,0,-48,0,false," dB"},
};
inline void flush(double&x){if(std::fabs(x)<1e-30)x=0;}
struct Channel {
 double last3=0,last2=0,last1=0,halfway=0,halfDry=0,halfDiff=0,a=0,b=0,c=0;
 double decay=0.915965594177219015;            // Catalan's constant, more adds focus and clarity
 double highTweak=0.0414213562373095048801688; // more adds treble to upsampling
 double drySample=0,diffSample=0,iirA=0,iirB=0;
 void clear(){last3=last2=last1=halfway=halfDry=halfDiff=a=b=c=drySample=diffSample=iirA=iirB=0;}
 double antialias(double x,bool flip){
  c=x;
  if(flip){a*=decay;b*=decay;a+=c;b-=c;c=a;}
  else{b*=decay;a*=decay;b+=c;a-=c;c=b;}
  flush(a);flush(b);return c*decay;
 }
};
class MultiBandDistortionFx final:public NativeFx{
 Channel ch[2];bool flip=false;
 double iirAmount=.125,gainH=1,gainL=1,thresholdH=0,thresholdL=0,hardnessH=1,hardnessL=1;
 double outputH=1,outputL=1,outputD=.597,outputGlobal=1;
 static double breakup(double t,double threshold,double hardness){
  if(std::fabs(t)>threshold){
   double bridgerectifier=(std::fabs(t)-threshold)*hardness;   // skip flat area if any, scale to distortion limit
   if(bridgerectifier>1.5707963267949)bridgerectifier=1.5707963267949;   // max value for sine function
   bridgerectifier=std::sin(bridgerectifier)/hardness;
   t=t>0?bridgerectifier+threshold:-(bridgerectifier+threshold);
  }
  return t;
 }
 double bands(double x,double&iir){
  double low=iir=(iir*(1-iirAmount))+(x*iirAmount);flush(iir);
  double high=x-iir;
  high=breakup(high*gainH,thresholdH,hardnessH);
  low=breakup(low*gainL,thresholdL,hardnessL);
  return (low*outputL)+(high*outputH);
 }
public:
 void configure(const uint8_t*v,float)override{
  double A=v[0]*.01;iirAmount=std::pow(A,3);
  gainH=std::pow(10.0,(v[1]*.01*48.0)/20);gainL=std::pow(10.0,(v[2]*.01*48.0)/20);
  thresholdH=v[3]*.01;hardnessH=thresholdH<1.0?1.0/(1.0-thresholdH):999999999999999999999.0;
  thresholdL=v[4]*.01;hardnessL=thresholdL<1.0?1.0/(1.0-thresholdL):999999999999999999999.0;
  outputH=v[5]*.01;outputL=v[6]*.01;outputD=v[7]*.01*0.597;
  double outtrim=outputH+outputL+outputD;outputH*=outtrim;outputL*=outtrim;outputD*=outtrim;
  outputGlobal=std::pow(10.0,((v[8]*.01-1.0)*48.0)/20);
 }
 void clear()override{for(auto&c:ch)c.clear();flip=false;}
 void process(float*io,int frames)override{
  for(int i=0;i<frames;i++){
   for(int k=0;k<2;k++){
    auto&h=ch[k];double x=io[i*2+k];
    h.drySample=x;
    h.halfDry=h.halfway=(x+h.last1+((-h.last2+h.last3)*h.highTweak))/2.0;
    h.last3=h.last2;h.last2=h.last1;h.last1=x;   // oversampled special antialiasing
    h.halfway=bands(h.halfway,h.iirA);           // interpolated sample
    x=bands(x,h.iirB);                           // raw sample
    h.halfDiff=h.antialias(h.halfway-h.halfDry,flip);
    h.diffSample=h.antialias(x-h.drySample,flip);
    x=h.drySample*outputD+(h.diffSample+h.halfDiff);   // processing as difference to the raw input
    io[i*2+k]=nativeClean(float(x*outputGlobal));
   }
   flip=!flip;
  }
 }
};
}
const NativeInfo multiBandDistortionInfo{FxType::MultiBandDistortion,"MULTIBAND DIST","MBDIST","MB",4,2,2,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeMultiBandDistortion(){return std::make_unique<MultiBandDistortionFx>();}
}
