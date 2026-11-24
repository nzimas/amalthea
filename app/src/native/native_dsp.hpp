// SPDX-License-Identifier: GPL-3.0-only
// Small DSP pieces shared by the native effects. No dynamic allocation after construction.
#pragma once
#include "../native_fx.hpp"
#include "../core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
namespace amalthea {
// The stored value (0..100) that selects division `index`.
constexpr uint8_t nativeDivisionValue(int index){return uint8_t((index*100+(NativeDivisions-1)/2)/(NativeDivisions-1));}
constexpr uint8_t nativeChoiceValue(int index,int count){return count<2?0:uint8_t((index*100+(count-1)/2)/(count-1));}
inline uint32_t nativeRandom(uint32_t&s){if(!s)s=0x9e3779b9u;s^=s<<13;s^=s>>17;s^=s<<5;return s;}
inline float nativeUnit(uint32_t&s){return (nativeRandom(s)>>8)*(1.f/16777216.f);}
inline float nativeClean(float x){return std::isfinite(x)?std::clamp(x,-4.f,4.f):0.f;}
constexpr float NativePi=3.14159265358979f;

// RBJ biquad (direct form I), one channel.
struct Biquad {
 float b0=1,b1=0,b2=0,a1=0,a2=0,x1=0,x2=0,y1=0,y2=0;
 void clear(){x1=x2=y1=y2=0;}
 void set(int kind,float hz,float q,float gainDb=0){   // 0 LP 1 HP 2 BP 3 notch 4 peak 5 allpass
  float w=2*NativePi*std::clamp(hz,10.f,20000.f)/44100,c=std::cos(w),s=std::sin(w),al=s/(2*std::max(q,.05f)),A=std::pow(10.f,gainDb/40);
  float B0,B1,B2,A0,A1,A2;
  switch(kind){
   case 0:B0=(1-c)/2;B1=1-c;B2=B0;A0=1+al;A1=-2*c;A2=1-al;break;
   case 1:B0=(1+c)/2;B1=-(1+c);B2=B0;A0=1+al;A1=-2*c;A2=1-al;break;
   case 2:B0=al;B1=0;B2=-al;A0=1+al;A1=-2*c;A2=1-al;break;
   case 3:B0=1;B1=-2*c;B2=1;A0=1+al;A1=-2*c;A2=1-al;break;
   case 4:B0=1+al*A;B1=-2*c;B2=1-al*A;A0=1+al/A;A1=-2*c;A2=1-al/A;break;
   default:B0=1-al;B1=-2*c;B2=1+al;A0=1+al;A1=-2*c;A2=1-al;break;
  }
  b0=B0/A0;b1=B1/A0;b2=B2/A0;a1=A1/A0;a2=A2/A0;
 }
 float process(float x){float y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;x2=x1;x1=x;y2=y1;y1=std::fabs(y)<1e-20f?0:y;return y1;}
};
// Linkwitz-Riley 4th-order crossover (two cascaded Butterworth sections per band).
struct LinkwitzRiley {
 Biquad lp[2],hp[2];
 void setFrequency(float hz){for(auto&b:lp)b.set(0,hz,.70710678f);for(auto&b:hp)b.set(1,hz,.70710678f);}
 void clear(){for(auto&b:lp)b.clear();for(auto&b:hp)b.clear();}
 void process(float x,float&low,float&high){low=lp[1].process(lp[0].process(x));high=hp[1].process(hp[0].process(x));}
};
// A stereo history buffer: write the input, read anywhere in the past (fractional).
struct History {
 std::vector<float> data;int size=0,at=0;
 explicit History(int frames=0){resize(frames);}
 void resize(int frames){size=std::max(2,frames);data.assign(size_t(size)*2,0.f);at=0;}
 void clear(){std::fill(data.begin(),data.end(),0.f);}
 void write(float l,float r){data[size_t(at)*2]=l;data[size_t(at)*2+1]=r;at=at+1==size?0:at+1;}
 // `ago` frames before the newest written frame (0 = newest).
 float read(int channel,double ago)const{
  double p=at-1-ago;while(p<0)p+=size;while(p>=size)p-=size;int i=int(p);float f=float(p-i);int j=i+1==size?0:i+1;
  float a=data[size_t(i)*2+channel],b=data[size_t(j)*2+channel];return a+(b-a)*f;
 }
};
// A capture buffer for slicing: records from a start, then plays back any position.
struct SliceBuffer {
 std::vector<float> data;int capacity=0;
 explicit SliceBuffer(int frames=0){data.assign(size_t(std::max(2,frames))*2,0.f);capacity=std::max(2,frames);}
 void clear(){std::fill(data.begin(),data.end(),0.f);}
 float&at(int frame,int channel){return data[size_t(std::clamp(frame,0,capacity-1))*2+channel];}
 float read(int channel,double frame)const{
  frame=std::clamp(frame,0.0,double(capacity-1));int i=int(frame);float f=float(frame-i);int j=std::min(i+1,capacity-1);
  return data[size_t(i)*2+channel]+(data[size_t(j)*2+channel]-data[size_t(i)*2+channel])*f;
 }
};
// Short raised-cosine edge for click-free splices: 0..1 over `length` frames.
inline float nativeFade(long position,long length){if(length<=0||position>=length)return 1;if(position<=0)return 0;return .5f-.5f*std::cos(NativePi*float(position)/float(length));}
}
