// SPDX-License-Identifier: GPL-3.0-only
// TRANSVERB: a one-to-one recreation of Destroy FX Transverb (C) 2001-2026 Tom Murphy 7 and
// Sophia Poirier, GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/transverb),
// including the IIR filter, FIR filter, Kaiser window, Hermite interpolation and smoothed values
// of the Destroy FX Library (C) 2002-2026 Sophia Poirier, GPL-2.0-or-later.
// Each channel writes its input into a buffer of BUFFER SIZE; two read heads travel through
// it at their own speeds (semitones; 0 is a plain delay of DISTANCE x buffer) and feed back
// into their own buffer. Each read is smoothed for 42 samples when the reader and writer
// cross. QUALITY: DIRT-FI reads raw samples, HI-FI Hermite-interpolates, ULTRA HI-FI also
// highpasses slow heads (39 Hz / speed) and lowpasses fast ones (IIR below 5x, windowed
// 23-tap FIR with make-up gain from 5x). TOMSOUND is Tom's original double-stepping bug.
// Speed, mix and feedback glide over 30 ms as in the original.
// Changed: the dry mix is omitted (the host blends MIX); FREEZE and "attenuate feedback by
// mix level" are off; the speed spans -36..+64 semitones (the original -3..+6 octaves); the
// MIDI learn, speed-mode GUI and presets are omitted, and the random preset's leanings
// (small buffers, slow speeds) live in the random draw ranges. To guarantee the tail dies
// within about twelve seconds, FEEDBACK scales up to a ceiling of 0.95 that falls for long
// buffers; the FIR make-up gain is kept out of the feedback path, and ULTRA HI-FI feeds back
// at 0.8 of the setting (its filtered Hermite reads would otherwise self-oscillate at slow speeds).
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>
namespace amalthea {
namespace {
const char*const qualityModes[]={"DIRT-FI","HI-FI","ULTRA HI-FI"};
const NativeParam params[]={
 {"BUFFER SIZE",71,NativeUnit::Range,45,90,nullptr,0,1,3000,true," ms"},
 {"SPEED 1",36,NativeUnit::Range,12,60,nullptr,0,-36,64,false," st"},
 {"FEEDBACK 1",30,NativeUnit::Percent,0,70},
 {"DISTANCE 1",90,NativeUnit::Percent,50,100},
 {"MIX 1",100,NativeUnit::Percent,50,100},
 {"SPEED 2",48,NativeUnit::Range,12,60,nullptr,0,-36,64,false," st"},
 {"FEEDBACK 2",20,NativeUnit::Percent,0,70},
 {"DISTANCE 2",10,NativeUnit::Percent,0,100},
 {"MIX 2",70,NativeUnit::Percent,0,100},
 {"QUALITY",100,NativeUnit::Choice,0,100,qualityModes,3},
 {"TOMSOUND",0,NativeUnit::Toggle,0,100},
};
constexpr double SampleRate=44100;
constexpr int MaxBuffer=int(3000*0.001*SampleRate);       // 3 s, as the original's maximum
constexpr int SmoothingDur=42;                             // kAudioSmoothingDur_samples
constexpr double HighpassCutoff=39,FirSpeedThreshold=5,UnitySpeed=1,ShelfStartLowpass=.333;
constexpr int FirTaps=23;
// Hermite's taps sum to 1.25 in absolute value: around a loop of filtered (or, in TOMSOUND,
// interleaved) reads that can exceed unity, so ULTRA HI-FI feeds back at 0.8 of the setting.
constexpr float UltraFeed=.8f;
constexpr int SpeedStride=std::max(int(SampleRate)/11025,1);   // GetFrequencyBasedSmoothingStride
enum Quality{DirtFi,HiFi,UltraHiFi};
enum class FilterMode{None,Highpass,LowpassIIR,LowpassFIR};

inline int modBipolar(int value,int modulo){while(value<0)value+=modulo;return value%modulo;}
inline double fmodBipolar(double value,double modulo){while(value<0)value+=modulo;return std::fmod(value,modulo);}
inline float clampDenormal(float x){return std::fabs(x)<1e-18f?0.f:x;}
inline float cleanWrite(float x){x=nativeClean(x);return std::fabs(x)<1e-20f?0.f:x;}
// Laurent de Soras' 4-point Hermite (dfx::math::InterpolateHermite).
inline float hermite(float pm1,float p0,float p1,float p2,float t){
 float c=(p1-pm1)*.5f,cmn=p0-p1,w=c+cmn,a=w+cmn+(p2-p0)*.5f,bneg=w+a;
 return (((a*t)-bneg)*t+c)*t+p0;
}
inline float fract(double x){return float(x-double(long(x)));}

// dfx::SmoothedValue: linear glide over 30 ms; the first value is taken at once.
struct Smoothed {
 double current=0,target=0,step=0;int count=0,dur=int(.030*SampleRate);bool reinit=true;
 void setNow(double v){current=target=v;count=dur;reinit=false;}
 void set(double v){
  if(std::exchange(reinit,false)){setNow(v);return;}
  if(v==target)return;target=v;step=(target-current)/dur;current+=step;count=0;
 }
 bool smoothing()const{return count<dur;}
 void inc(){if(smoothing())count++;if(smoothing())current+=step;else current=target;}
 double value()const{return current;}
};

// dfx::IIRFilter restricted to the LP/HP optimisation Transverb uses, with its
// pre-Hermite output cache.
struct IIR {
 float cIn=0,cPrevIn=0,cPrevOut=0,cPrevPrevOut=0;
 float prevIn=0,prevPrevIn=0,prevOut=0,prevPrevOut=0,prevPrevPrevOut=0,currentOut=0;
 void reset(){prevIn=prevPrevIn=prevOut=prevPrevOut=prevPrevPrevOut=currentOut=0;}
 void setCoefficients(bool highpass,double hz){
  double omega=2*3.14159265358979323846*hz/SampleRate,sn=std::sin(omega),cs=std::cos(omega),alpha=sn/(2*.70710678118654752);
  double b0=1+alpha,in=highpass?(1+cs)*.5:(1-cs)*.5,pin=highpass?-1-cs:1-cs;
  cIn=float(in/b0);cPrevIn=float(pin/b0);cPrevOut=float(-2*cs/b0);cPrevPrevOut=float((1-alpha)/b0);
 }
 void setLowpass(double hz){setCoefficients(false,hz);}
 void setHighpass(double hz){setCoefficients(true,std::min(hz,SampleRate));}
 void process(float in){
  prevPrevOut=prevOut;prevOut=currentOut;
  currentOut=clampDenormal((in+prevPrevIn)*cIn+prevIn*cPrevIn-prevOut*cPrevOut-prevPrevOut*cPrevPrevOut);
  prevPrevIn=prevIn;prevIn=in;
 }
 void processToCache(float in){prevPrevPrevOut=prevPrevOut;process(in);}
 void processToCacheH1(float in){
  prevPrevPrevOut=prevPrevOut;prevPrevOut=prevOut;prevOut=currentOut;
  currentOut=clampDenormal((in+prevPrevIn)*cIn+prevIn*cPrevIn-prevOut*cPrevOut-prevPrevOut*cPrevPrevOut);
  prevPrevIn=prevIn;prevIn=in;
 }
 void processToCacheH2(const float*a,int size,int pos){
  float in0=a[pos],in1=a[(pos+1)%size];
  prevPrevPrevOut=prevPrevOut;prevPrevOut=prevOut;prevOut=currentOut;
  currentOut=(in0+prevPrevIn)*cIn+prevIn*cPrevIn-prevOut*cPrevOut-prevPrevOut*cPrevPrevOut;
  prevPrevPrevOut=prevPrevOut;prevPrevOut=prevOut;prevOut=currentOut;
  currentOut=(in1+prevIn)*cIn+in0*cPrevIn-prevOut*cPrevOut-prevPrevOut*cPrevPrevOut;
  currentOut=clampDenormal(currentOut);prevPrevIn=in0;prevIn=in1;
 }
 void processToCacheH3(const float*a,int size,int pos){
  float in0=a[pos],in1=a[(pos+1)%size],in2=a[(pos+2)%size];
  prevPrevPrevOut=(in0+prevPrevIn)*cIn+prevIn*cPrevIn-currentOut*cPrevOut-prevOut*cPrevPrevOut;
  prevPrevOut=(in1+prevIn)*cIn+in0*cPrevIn-prevPrevPrevOut*cPrevOut-currentOut*cPrevPrevOut;
  prevOut=(in2+in0)*cIn+in1*cPrevIn-prevPrevOut*cPrevOut-prevPrevPrevOut*cPrevPrevOut;
  currentOut=clampDenormal(prevOut);prevOut=prevPrevOut;prevPrevOut=prevPrevPrevOut;prevPrevPrevOut=currentOut;
  prevPrevIn=in1;prevIn=in2;
 }
 void processToCacheH4(const float*a,int size,int pos){
  float in0=a[pos],in1=a[(pos+1)%size],in2=a[(pos+2)%size],in3=a[(pos+3)%size];
  prevPrevPrevOut=(in0+prevPrevIn)*cIn+prevIn*cPrevIn-currentOut*cPrevOut-prevOut*cPrevPrevOut;
  prevPrevOut=(in1+prevIn)*cIn+in0*cPrevIn-prevPrevPrevOut*cPrevOut-currentOut*cPrevPrevOut;
  prevOut=(in2+in0)*cIn+in1*cPrevIn-prevPrevOut*cPrevOut-prevPrevPrevOut*cPrevPrevOut;
  currentOut=clampDenormal((in3+in1)*cIn+in2*cPrevIn-prevOut*cPrevOut-prevPrevOut*cPrevPrevOut);
  prevPrevIn=in2;prevIn=in3;
 }
 float hermitePostFilter(double pos)const{return hermite(prevPrevPrevOut,prevPrevOut,prevOut,currentOut,fract(pos));}
};

// dfx::FIRFilter pieces: Kaiser window, ideal (sinc) lowpass, circular convolution.
float besselIZero(float x){float sum=1,half=x*.5f,den=1,num=1;for(int m=1;m<=32;m++){num*=half;den*=float(m);float t=num/den;sum+=t*t;}return sum;}
std::array<float,FirTaps> kaiserWindow(float attenuation){
 std::array<float,FirTaps> w;w.fill(1.f);float beta=0;
 if(attenuation>=50)beta=.1102f*(attenuation-8.71f);
 else if(attenuation>=21)beta=.5842f*std::pow(attenuation-21,.4f)+.07886f*(attenuation-21);
 int half=(FirTaps+1)/2;float inv=1/besselIZero(beta);
 for(int n=0;n<half;n++){w[n]*=besselIZero(beta*std::sqrt(1-std::pow(1-(2.f*n)/float(FirTaps-1),2.f)))*inv;w[FirTaps-1-n]=w[n];}
 return w;
}
void idealLowpass(double cutoff,float*out,const std::array<float,FirTaps>&window){
 double corner=(cutoff/(SampleRate*.5))*3.14159265358979323846;int middle=(FirTaps-1)/2;
 out[middle]=float(corner/3.14159265358979323846);
 double halfLength=(FirTaps-1)*.5,numerator=-halfLength*corner,denominator=-halfLength*3.14159265358979323846;
 for(int n=0;n<middle;n++,numerator+=corner,denominator+=3.14159265358979323846){out[n]=float(std::sin(numerator)/denominator);out[FirTaps-1-n]=out[n];}
 for(int n=0;n<FirTaps;n++)out[n]*=window[n];
}
float firProcess(const float*a,int size,const float*c,int pos){
 float r=0;int ci=0;
 for(int remaining=FirTaps;remaining>0;){int sub=std::min(remaining,size-pos);for(int k=0;k<sub;k++)r+=c[ci+k]*a[pos+k];ci+=sub;pos=0;remaining-=sub;}
 return r;
}

struct Head {
 Smoothed speed,mix,feed;double read=0;std::vector<float> buf;IIR filter;std::array<float,FirTaps> fir{};
 bool speedHasChanged=true;int smoothcount=0;float smoothstep=0,lastdelayval=0;
 void reset(){smoothcount=0;lastdelayval=0;filter.reset();speedHasChanged=true;std::fill(buf.begin(),buf.end(),0.f);}
};

// One TransverbDSP core (the original runs one per channel).
struct Core {
 int bsize=1,writer=0;std::array<Head,2> heads;
 Core(){for(auto&h:heads)h.buf.assign(size_t(MaxBuffer),0.f);}
 void reset(){for(auto&h:heads)h.reset();}
 void inc(){for(auto&h:heads){h.speed.inc();h.mix.inc();h.feed.inc();}}
 // TransverbDSP::interpolateHermite: neighbours the writer is about to overwrite are repeated.
 float hermiteAround(const float*d,double address)const{
  int pos=int(address);float f=fract(address);int pm1,pp1,pp2;
  switch(modBipolar(writer-pos,bsize)){
   case 0:pm1=pos;pp1=(pos+1)%bsize;pp2=(pos+2)%bsize;break;
   case 1:pm1=pos==0?bsize-1:pos-1;pp1=pp2=pos;break;
   case 2:pm1=pos==0?bsize-1:pos-1;pp1=pp2=(pos+1)%bsize;break;
   default:pm1=pos==0?bsize-1:pos-1;pp1=(pos+1)%bsize;pp2=(pos+2)%bsize;break;
  }
  return hermite(d[pm1],d[pos],d[pp1],d[pp2],f);
 }
 void process(const float*in,float*out,int n,int quality,bool tomsound,const std::array<float,FirTaps>&window){
  std::array<float,2> delayvals{};const double bsizeF=bsize;
  if(!tomsound){
   std::array<int,2> speedInts{},lowpasspos{};std::array<FilterMode,2> modes{FilterMode::None,FilterMode::None};std::array<float,2> mugs{1.f,1.f};
   for(int i=0;i<n;i++){
    bool first=i==0,strideHit=i%SpeedStride==0;float delaysum=0;
    for(int h=0;h<2;h++){
     Head&hd=heads[h];const float*buf=hd.buf.data();int readInt=int(hd.read);double sp=hd.speed.value();
     if(quality==UltraHiFi&&(first||(hd.speed.smoothing()&&strideHit))){
      lowpasspos[h]=readInt;
      if(sp>UnitySpeed){
       modes[h]=FilterMode::LowpassIIR;speedInts[h]=int(sp);
       if(sp>=FirSpeedThreshold){
        modes[h]=FilterMode::LowpassFIR;mugs[h]=float(std::pow(sp/FirSpeedThreshold,.78));
        if(std::exchange(hd.speedHasChanged,false)){idealLowpass((SampleRate/sp)*ShelfStartLowpass,hd.fir.data(),window);hd.filter.reset();}
       }else if(std::exchange(hd.speedHasChanged,false))hd.filter.setLowpass((SampleRate/sp)*ShelfStartLowpass);
      }else{modes[h]=FilterMode::Highpass;if(std::exchange(hd.speedHasChanged,false))hd.filter.setHighpass(HighpassCutoff/sp);}
     }
     float feedScale=quality==UltraHiFi?UltraFeed:1.f;   // FIR: keeps the make-up gain out of the loop
     switch(quality){
      case DirtFi:default:delayvals[h]=buf[readInt];break;
      case HiFi:delayvals[h]=hermiteAround(buf,hd.read);break;
      case UltraHiFi:
       switch(modes[h]){
        case FilterMode::Highpass:case FilterMode::LowpassIIR:delayvals[h]=hd.filter.hermitePostFilter(hd.read);break;
        case FilterMode::LowpassFIR:{
         float lp1=firProcess(buf,bsize,hd.fir.data(),modBipolar(readInt-FirTaps,bsize));
         float lp2=firProcess(buf,bsize,hd.fir.data(),modBipolar(readInt-FirTaps+1,bsize));
         float f=fract(hd.read);delayvals[h]=(lp1+(lp2-lp1)*f)*mugs[h];feedScale=1/mugs[h];break;}
        default:delayvals[h]=hermiteAround(buf,hd.read);break;
       }
       break;
     }
     if(hd.smoothcount>0){float pos=hd.smoothstep*float(hd.smoothcount);delayvals[h]+=(hd.lastdelayval-delayvals[h])*pos;hd.smoothcount--;}
     hd.buf[size_t(writer)]=cleanWrite(in[i]+delayvals[h]*feedScale*float(hd.feed.value()));
     delaysum+=delayvals[h]*float(hd.mix.value());
     int nextRead=int(hd.read+sp),nextWrite=writer+1;
     bool ahead=readInt<writer&&nextRead>=nextWrite,behind=readInt>=writer&&nextRead<=nextWrite;
     if((ahead||behind)&&sp!=UnitySpeed&&hd.smoothcount<=0){
      hd.lastdelayval=delayvals[h];int dur=std::min(int(bsizeF/sp),SmoothingDur);dur=std::max(dur,1);
      hd.smoothstep=1.f/float(dur);hd.smoothcount=dur;
     }
     hd.read+=sp;if(hd.read>=bsizeF)hd.read=fmodBipolar(hd.read,bsizeF);
     if(modes[h]==FilterMode::LowpassIIR){
      int count=0;int&lp=lowpasspos[h];
      while(count<speedInts[h]){
       switch(speedInts[h]-count){
        case 1:hd.filter.processToCacheH1(buf[lp]);lp=(lp+1)%bsize;count++;break;
        case 2:hd.filter.processToCacheH2(buf,bsize,lp);lp=(lp+2)%bsize;count+=2;break;
        case 3:hd.filter.processToCacheH3(buf,bsize,lp);lp=(lp+3)%bsize;count+=3;break;
        default:hd.filter.processToCacheH4(buf,bsize,lp);lp=(lp+4)%bsize;count+=4;break;
       }
      }
      int nextReadInt=int(hd.read);
      if((lp<nextReadInt&&lp+1==nextReadInt)||(lp==bsize-1&&nextReadInt==0)){hd.filter.processToCacheH1(buf[lp]);lp=(lp+1)%bsize;}
     }else if(modes[h]==FilterMode::Highpass){if(int(hd.read)!=readInt)hd.filter.processToCache(buf[readInt]);}
     hd.speedHasChanged|=hd.speed.smoothing();
    }
    out[i]=delaysum;
    writer=(writer+1)%bsize;
    inc();
   }
  }else{
   // TOMSOUND: readers and writer step twice per frame; the writer wraps on an odd size.
   int wrap=bsize-((bsize%2)?0:1);const float tomFeed=quality==UltraHiFi?UltraFeed:1.f;
   for(int i=0;i<n;i++){
    for(int h=0;h<2;h++){
     const Head&hd=heads[h];const float*buf=hd.buf.data();int pos=int(hd.read);
     switch(quality){
      case DirtFi:default:delayvals[h]=buf[pos];break;
      case HiFi:{float a=buf[pos],b=buf[(pos+1)%bsize];delayvals[h]=a+(b-a)*fract(hd.read);break;}
      case UltraHiFi:delayvals[h]=hermite(buf[pos==0?bsize-1:pos-1],buf[pos],buf[(pos+1)%bsize],buf[(pos+2)%bsize],fract(hd.read));break;
     }
    }
    for(int h=0;h<2;h++)heads[h].buf[size_t(writer)]=cleanWrite(in[i]+tomFeed*float(heads[h].feed.value())*delayvals[h]);
    writer+=2;if(writer>=bsize)writer%=wrap;
    for(auto&hd:heads){hd.read+=hd.speed.value()*2;if(hd.read>=bsizeF)hd.read=std::fmod(hd.read,bsizeF);}
    float o=0;for(int h=0;h<2;h++)o+=float(heads[h].mix.value())*delayvals[h];
    out[i]=o;
    inc();
   }
  }
 }
};

class TransverbFx final:public NativeFx{
 Core cores[2];std::array<float,FirTaps> window=kaiserWindow(60.f);
 uint8_t last[NativeParams];bool fresh=true;int quality=UltraHiFi;bool tomsound=false;
 float feedCeiling(int bsize)const{
  // a pass through the loop takes at most one buffer: keep it 60 dB down within ~12 s
  double loop=bsize/SampleRate;return float(std::min(.95,std::pow(.001,loop/12)));
 }
public:
 TransverbFx(){std::fill(std::begin(last),std::end(last),uint8_t(255));}
 void configure(const uint8_t*v,float)override{
  auto changed=[&](int i){return fresh||last[i]!=v[i];};
  static const int speedIdx[2]={1,5},feedIdx[2]={2,6},distIdx[2]={3,7},mixIdx[2]={4,8};
  bool bsizeChanged=changed(0);int newB=0;
  if(bsizeChanged){double ms=std::pow(3000.0,v[0]*.01);newB=std::clamp(int(ms*SampleRate*.001),1,MaxBuffer);}
  for(auto&c:cores){
   for(int h=0;h<2;h++){
    Head&hd=c.heads[h];
    if(changed(mixIdx[h])){float m=v[mixIdx[h]]*.01f;hd.mix.set(double(m*m));}
    if(changed(speedIdx[h])){hd.speed.set(std::pow(2.0,(-36+v[speedIdx[h]])/12.0));hd.speedHasChanged=true;}
   }
   if(bsizeChanged){
    int entry=std::exchange(c.bsize,newB);
    if(c.bsize<entry&&c.writer>=c.bsize)c.writer%=c.bsize;
    for(auto&hd:c.heads)hd.read=fmodBipolar(hd.read,double(c.bsize));
   }
   for(int h=0;h<2;h++){
    Head&hd=c.heads[h];
    if(changed(feedIdx[h])||bsizeChanged)hd.feed.set(double(v[feedIdx[h]]*.01f*feedCeiling(c.bsize)));
    if(changed(distIdx[h]))hd.read=fmodBipolar(double(c.writer)-v[distIdx[h]]*.01*c.bsize,double(c.bsize));
   }
   if(changed(9)||changed(10))for(auto&hd:c.heads)hd.speedHasChanged=true;
  }
  quality=nativeChoice(v[9],3);tomsound=v[10]>=50;
  std::copy(v,v+NativeParams,last);fresh=false;
 }
 void process(float*io,int frames)override{
  float in[32],out[32];
  for(int c=0;c<2;c++){
   for(int i=0;i<frames;i++)in[i]=io[i*2+c];
   cores[c].process(in,out,frames,quality,tomsound,window);
   for(int i=0;i<frames;i++)io[i*2+c]=nativeClean(out[i]);
  }
 }
 void clear()override{for(auto&c:cores)c.reset();}
};
}
const NativeInfo transverbInfo{FxType::Transverb,"TRANSVERB","TRANSV","TV",1,1,2,50,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeTransverb(){return std::make_unique<TransverbFx>();}
}
