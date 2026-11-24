// Dependency-free, oversampled trapezoidal-integrator state-variable filter.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
class MultimodeFilter {
 // Matched 15-tap half-band filters suppress interpolation images and
 // nonlinear aliases. Their combined latency is seven base-rate samples.
 struct Halfband {
  std::array<double,16> history{};int at=0;
  double tick(double x){
   static constexpr double c[]={-.003651453990227176,.01617925339249947,-.06841177678814651,.3049475173410872,.3049475173410872,-.06841177678814653,.01617925339249948,-.003651453990227176};
   history[at]=x;double y=history[(at+16-7)&15]*.5018729200895738;
   for(int i=0;i<8;i++)y+=history[(at+16-2*i)&15]*c[i];at=(at+1)&15;return y;
  }
 };
 struct Channel {double ic1=0,ic2=0;Halfband up,down;std::array<double,8> dry{};int dryAt=0;};
 std::array<Channel,2> state{};
 std::array<double,6> weights{{1,0,0,0,0,0}};
 double g=.1,k=1,targetG=.1,targetK=1,gain=1,targetGain=1,level=1,targetLevel=1;
 int mode=0;
public:
 void clear(){state={};weights={};weights[mode]=1;g=targetG;k=targetK;gain=targetGain;level=targetLevel;}
 void configure(const std::array<uint8_t,5>&s){
  mode=std::clamp((int(s[0])+10)/20,0,5);
  double hz=20*std::pow(900.,s[1]*.01);
  targetG=std::tan(3.141592653589793*hz/88200.);
  double q=.70710678118*std::pow(12/.70710678118,s[2]*.01);
  targetK=1/q;targetGain=std::pow(10.,s[3]*.018);targetLevel=std::pow(10.,(int(s[4])-50)*.012);
 }
 void process(float&left,float&right,float mix){
  // 3ms coefficient/mode smoothing removes stepping during live edits.
  constexpr double smooth=.0075;
  g+=(targetG-g)*smooth;k+=(targetK-k)*smooth;gain+=(targetGain-gain)*smooth;level+=(targetLevel-level)*smooth;
  for(int i=0;i<6;i++)weights[i]+=((i==mode?1.:0.)-weights[i])*smooth;
  const double a1=1/(1+g*(g+k));
  const double inputWeight=weights[2]+weights[3]+weights[4]-weights[5];
  const double bandWeight=k*(weights[1]-weights[2]-weights[3]-2*weights[4]+weights[5]);
  const double lowWeight=weights[0]-weights[2]+2*weights[5];
  const double driveScale=gain>1.00001?1/std::tanh(gain):1;
  float *out[]={&left,&right};
  for(int c=0;c<2;c++){
   auto &s=state[c];double dry=*out[c],wet=0;
   double delayed=s.dry[(s.dryAt+1)&7];s.dry[s.dryAt]=dry;s.dryAt=(s.dryAt+1)&7;
   for(int sub=0;sub<2;sub++){
    double input=s.up.tick(sub?0:dry*2);
    if(gain>1.00001)input=std::tanh(input*gain)*driveScale;
    double v3=input-s.ic2,v1=a1*(s.ic1+g*v3),v2=s.ic2+g*v1;
    s.ic1=2*v1-s.ic1;s.ic2=2*v2-s.ic2;
    // LP has unity DC gain for every Q: resonance never steals the bass.
    double filtered=s.down.tick(inputWeight*input+bandWeight*v1+lowWeight*v2);
    if(sub==0)wet=filtered;
   }
   double value=mix<=0?dry:delayed+(wet*level-delayed)*mix;
   if(!std::isfinite(value)){s={};value=0;}
   *out[c]=float(std::clamp(value,-8.,8.));
  }
 }
};
}
