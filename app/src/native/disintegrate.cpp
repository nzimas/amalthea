// SPDX-License-Identifier: GPL-3.0-only
// DISINTEGRATE: a one-to-one recreation of Airwindows Disintegrate,
// Copyright (c) airwindows (Chris Johnson), MIT licence (https://github.com/airwindows/airwindows).
// A chain of up to 20 tiny clipped delay layers, each read at a position driven by
// a smoothed function of its own level, so louder material wobbles and tears apart.
// TOP DB sets the reference level the input is scaled to (higher is cleaner), BUFSIZE
// the delay buffers' length, LAYERS how many are chained and FILTER both the smoothing
// of the read position and the output lowpass. Changes: Dry/Wet is the host's MIX;
// the floating-point dither and denormal noise are removed, so silence stays exactly
// silent.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <cmath>
namespace amalthea {
namespace {
constexpr int dscBufMax=180,layersMax=22;
const char*const layerNames[]={"0","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","17","18","19","20"};
const NativeParam params[]={
 {"TOP DB",50,NativeUnit::Range,20,75,nullptr,0,60,140,false," dB"},
 {"BUFSIZE",50,NativeUnit::Percent,10,90},
 {"LAYERS",nativeChoiceValue(4,21),NativeUnit::Choice,5,60,layerNames,21},
 {"FILTER",50,NativeUnit::Percent,25,75},
};
struct Channel {
 double dBa[dscBufMax+5][layersMax]{};double dBaPos[layersMax]{},dBaPosB[layersMax]{};int dBaX[layersMax];double outFilter=0;
 Channel(){clear();}
 void clear(){
  for(auto&row:dBa)for(auto&x:row)x=0;
  for(int s=0;s<layersMax;s++){dBaPos[s]=0;dBaPosB[s]=0;dBaX[s]=1;}
  outFilter=0;
 }
 double run(double x,int layers,int dscBuf,double boost,double f,double topdB){
  x*=topdB;
  for(int l=0;l<layers;l++){
   x*=boost;
   if(x<-0.222)x=-0.222;
   if(x>0.222)x=0.222;
   dBa[dBaX[l]][l]=x;
   dBaPos[l]*=(1.0-f);dBaPos[l]+=(dBaPosB[l]*f);
   dBaPosB[l]*=(1.0-f);dBaPosB[l]+=std::fabs((x*((x*0.25)-0.5))*f);
   if(std::fabs(dBaPos[l])<1e-30)dBaPos[l]=0;if(std::fabs(dBaPosB[l])<1e-30)dBaPosB[l]=0;
   int dBdly=int(std::floor(dBaPos[l]*dscBuf));
   double dBi=(dBaPos[l]*dscBuf)-dBdly;
   x=dBa[dBaX[l]-dBdly+((dBaX[l]-dBdly<0)?dscBuf:0)][l]*(1.0-dBi);
   dBdly++;x+=dBa[dBaX[l]-dBdly+((dBaX[l]-dBdly<0)?dscBuf:0)][l]*dBi;
   dBaX[l]++;if(dBaX[l]<0||dBaX[l]>=dscBuf)dBaX[l]=0;
  }
  outFilter*=f;outFilter+=(x*(1.0-f));if(std::fabs(outFilter)<1e-30)outFilter=0;
  return outFilter/topdB;
 }
};
class DisintegrateFx final:public NativeFx{
 Channel ch[2];double topdB=.0064,boost=1,f=.25;int dscBuf=90,layers=4;
public:
 void configure(const uint8_t*v,float)override{
  double refdB=60.0+(v[0]*.01*80.0);
  topdB=0.000000064*std::pow(10.0,refdB/20.0);
  dscBuf=int((v[1]*.01*double(dscBufMax-1))+1);
  layers=nativeChoice(v[2],21);
  f=std::pow(v[3]*.01,2);
  boost=1.0+(f/(layers+1));
  if(f==0.0)f=0.000001;
  for(auto&c:ch)for(int l=0;l<layersMax;l++)if(c.dBaX[l]>=dscBuf)c.dBaX[l]=0;   // (live buffer shrink)
 }
 void clear()override{for(auto&c:ch)c.clear();}
 void process(float*io,int frames)override{
  for(int i=0;i<frames;i++)for(int k=0;k<2;k++)io[i*2+k]=nativeClean(float(ch[k].run(io[i*2+k],layers,dscBuf,boost,f,topdB)));
 }
};
}
const NativeInfo disintegrateInfo{FxType::Disintegrate,"DISINTEGRATE","DISINT","DG",4,2,3,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeDisintegrate(){return std::make_unique<DisintegrateFx>();}
}
