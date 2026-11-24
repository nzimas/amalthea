// SPDX-License-Identifier: GPL-3.0-only
// GEOMETER: a one-to-one recreation of Destroy FX Geometer (C) 2002-2026 Tom Murphy 7 and
// Sophia Poirier, GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/geometer).
// Starring the Super Destroy FX Windowing System: the input is cut into windows that
// overlap by half; in each window Geometer finds landmark points (extremities and
// crossings, every Nth sample, random places, a magnitude-driven span, slope changes or
// level crossings), runs them through three point operations (x2, 1/2, 1/4, longpass,
// shortpass, slow, fast, none) and rebuilds the wave between them (polygon, wrongygon,
// smoothie, reversi, pulse, friends, sing, shuffle). Windows are enveloped (linear,
// arrow, wedge or cosine "best") and overlap-added, as in the original, channel by channel.
// Changed: each mode keeps one shared parameter per slot instead of a parameter per mode.
// The window work runs as a resumable job spread over the next half window, so the
// latency is 1.5 windows instead of one and no block does a whole window's work at once.
// "Randomly" uses the intended (1-param)*size point count (the original's cast makes it
// 0 or all), sorted with a counting sort. "Sing" scales its sine by the window's peak
// so silence stays silent. Fixed seeds replace entropic randomness; samples a mode
// leaves unwritten are zero instead of stale; degenerate point lists keep one point.
// The GUI waveform view, MIDI learn and presets are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <vector>
namespace amalthea {
namespace {
constexpr int WindowSizes[14]={4,8,16,32,64,128,256,512,1024,2048,4096,8192,16384,32768};
constexpr int MaxFrame=32768,MaxThird=MaxFrame/2,MaxPoints=MaxFrame*2+3,OutCap=MaxFrame*2+16;
enum{POINT_EXTNCROSS,POINT_FREQ,POINT_RANDOM,POINT_SPAN,POINT_DYDX,POINT_LEVEL};
enum{INTERP_POLYGON,INTERP_WRONGYGON,INTERP_SMOOTHIE,INTERP_REVERSI,INTERP_PULSE,INTERP_FRIENDS,INTERP_SING,INTERP_SHUFFLE};
enum{OP_DOUBLE,OP_HALF,OP_QUARTER,OP_LONGPASS,OP_SHORTPASS,OP_SLOW,OP_FAST,OP_NONE};
enum{WINDOW_TRIANGLE,WINDOW_ARROW,WINDOW_WEDGE,WINDOW_COS};
const char*const sizeNames[]={"4","8","16","32","64","128","256","512","1024","2048","4096","8192","16384","32768"};
const char*const shapeNames[]={"LINEAR","ARROW","WEDGE","BEST"};
const char*const pointNames[]={"EXT N CROSS","AT FREQ","RANDOMLY","SPAN","DY/DX","AT LEVEL"};
const char*const interpNames[]={"POLYGON","WRONGYGON","SMOOTHIE","REVERSI","PULSE","FRIENDS","SING","SHUFFLE"};
const char*const opNames[]={"X2","1/2","1/4","LONGPASS","SHORTPASS","SLOW","FAST","NONE"};
const NativeParam params[]={
 {"WINDOW SIZE",nativeChoiceValue(9,14),NativeUnit::Choice,nativeChoiceValue(7,14),nativeChoiceValue(12,14),sizeNames,14},
 {"WINDOW SHAPE",nativeChoiceValue(WINDOW_TRIANGLE,4),NativeUnit::Choice,0,100,shapeNames,4},
 {"POINT MODE",nativeChoiceValue(POINT_EXTNCROSS,6),NativeUnit::Choice,0,100,pointNames,6},
 {"POINT PARAM",0,NativeUnit::Percent,0,60},
 {"INTERP MODE",nativeChoiceValue(INTERP_POLYGON,8),NativeUnit::Choice,0,100,interpNames,8},
 {"INTERP PARAM",0,NativeUnit::Percent,0,80},
 {"OP 1",nativeChoiceValue(OP_NONE,8),NativeUnit::Choice,0,100,opNames,8},
 {"OP 1 PARAM",50,NativeUnit::Percent,0,70},
 {"OP 2",nativeChoiceValue(OP_NONE,8),NativeUnit::Choice,0,100,opNames,8},
 {"OP 2 PARAM",50,NativeUnit::Percent,0,70},
 {"OP 3",nativeChoiceValue(OP_NONE,8),NativeUnit::Choice,0,100,opNames,8},
 {"OP 3 PARAM",50,NativeUnit::Percent,0,70},
};
struct Settings{int pointstyle=POINT_EXTNCROSS,interpstyle=INTERP_POLYGON,op[3]={OP_NONE,OP_NONE,OP_NONE};float pointparam=0,interparam=0,oppar[3]={.5f,.5f,.5f};};

// A resumable step: spend one work unit, or save the place and return when the budget is gone.
// (Protothread style: every job variable is a member, and no switch nests inside run().)
#define GEOMETER_SPEND_COST(n) do{if((budget-=(n))<0){pc=__LINE__;return false;}case __LINE__:;}while(0)
#define GEOMETER_SPEND() GEOMETER_SPEND_COST(1)

class Geometer final:public NativeFx{
 std::vector<float> ring[2],ready[2][2],prevmix[2],jin,jout,env,py,ty;
 std::vector<int> px,tx;
 int sizeIndex=-1,shape=-1,framesize=0,third=0,fill=0,slot=0,front=0;long completed=0;
 bool active=false;long rate=64,learned=0,used=0,budget=0;
 Settings set,job;
 uint32_t seed=0x6e0e7e12;
 // job state (processw of the original, made resumable)
 int pc=-1,c=0,sA=0,sB=1,samples=0,maxpts=0,numpts=0,i=0,q=0,t=0,cc=0,u=0,z=0,x=0,j=0,w=0,rep=0,outi=0,np=0,times=0,tt=0;
 int st=0,extx=0,ctr=0,nth=0,span=0,stretch=0,offset=0,intervals=0,cpos=0,seg=0,segFrom=0,k=0,wid=0,opslot=0,pop=0,sizeleft=0,sizeright=0,tgtlen=0,count=0,draws=0;
 float ext=0,level=0,lasts=0,pp=0,peak=0,denom=0,minterp=0,factor=0,inv=0,onediv=0,opp=0;bool lastsign=false,above=false;
 const float*in=nullptr;float*out=nullptr;
 int randomInt(int lo,int hi){return lo+int(nativeRandom(seed)%uint32_t(hi-lo+1));}
 void push(int at,float y){px[numpts]=at;py[numpts]=y;numpts++;}

 bool run(){
  if(pc<0)return true;
  switch(pc){
  case 0:
  for(c=0;c<2;c++){
   peak=0;
   for(i=0;i<framesize;i++){GEOMETER_SPEND();{float s=ring[c][size_t(i<third?sA*third+i:sB*third+i-third)];jin[i]=s;peak=std::max(peak,std::fabs(s));}}
   for(i=0;i<framesize+16;i++){GEOMETER_SPEND();jout[i]=0;}
   in=jin.data();out=jout.data();samples=framesize;maxpts=framesize*2;

   /* collect points. */
   px[0]=0;py[0]=in[0];numpts=1;pp=job.pointparam;
   if(job.pointstyle==POINT_EXTNCROSS){
    /* extremities and crossings */
    enum{SZ,SZC,SA,SB};
    ext=0;extx=0;st=SZ;
    for(i=0;i<samples;i++){GEOMETER_SPEND();{
     float s=in[i];
     if(st==SZ){
      if(s<=pp&&s>=-pp)st=SZC;
      else if(s<-pp){st=SB;ext=s;extx=i;}
      else{st=SA;ext=s;extx=i;}
     }else if(st==SZC){
      if(!(s<=pp&&s>=-pp)){
       if(numpts<maxpts-1)push(i>0?i-1:0,0);
       st=s<0?SB:SA;ext=s;extx=i;
      }
     }else if(st==SA){
      if(s<=pp){
       if(numpts<maxpts-1)push(extx,ext);
       if(s>=-pp){if(numpts<maxpts-1)push(i,0);st=SZ;}
       else{st=SB;ext=s;extx=i;}
      }else if(s>ext){ext=s;extx=i;}
     }else{
      if(s>=-pp){
       if(numpts<maxpts-1)push(extx,ext);
       if(s<=pp){if(numpts<maxpts-1)push(i,0);st=SZ;}
       else{st=SA;ext=s;extx=i;}
      }else if(s<ext){ext=s;extx=i;}
     }
    }}
   }else if(job.pointstyle==POINT_LEVEL){
    enum{ABOVE,BETWEEN,BELOW};
    st=BETWEEN;level=pp*.9999f+.00005f;numpts=1;px[0]=0;py[0]=in[0];
    for(i=0;i<samples;i++){GEOMETER_SPEND();
     if(in[i]>level){if(st!=ABOVE){push(i,in[i]);st=ABOVE;}}
     else if(in[i]<-level){if(st!=BELOW){push(i,in[i]);st=BELOW;}}
     else if(st!=BETWEEN){push(i,in[i]);st=BETWEEN;}
     if(numpts>samples-2)break;
    }
    push(samples-1,in[samples-1]);
   }else if(job.pointstyle==POINT_FREQ){
    nth=int(pp*pp*samples);ctr=nth;
    for(i=0;i<samples;i++){GEOMETER_SPEND();
     ctr--;
     if(ctr<=0){if(numpts<maxpts-1)push(i,in[i]);else break;ctr=nth;}
    }
   }else if(job.pointstyle==POINT_RANDOM){
    /* random places, sorted (a counting sort over the window, duplicates kept) */
    for(i=0;i<samples;i++){GEOMETER_SPEND();tx[i]=0;}
    tx[0]=1;count=1;draws=int((1.f-pp)*samples);
    for(k=0;k<draws;k++){GEOMETER_SPEND();
     if(count<maxpts-1){tx[randomInt(0,samples-1)]++;count++;}else break;
    }
    numpts=0;
    for(i=0;i<samples;i++){GEOMETER_SPEND();for(int n=tx[i];n>0;n--)push(i,in[i]);}
   }else if(job.pointstyle==POINT_SPAN){
    /* next x determined by sample magnitude (suggested by Bram) */
    span=int(pp*pp*samples);
    i=std::abs(int(py[0]*span))+1;
    while(i<samples){GEOMETER_SPEND();
     push(i,in[i]);i=i+std::abs(int(in[i]*span))+1;
    }
   }else if(job.pointstyle==POINT_DYDX){
    lastsign=false;lasts=in[0];px[0]=0;py[0]=in[0];numpts=1;
    if(pp>.5f){pp=pp-.5f;above=true;}else{pp=.5f-pp;above=false;}
    pp=std::pow(pp,2.7f);
    for(i=1;i<samples;i++){GEOMETER_SPEND();{
     bool sign=above?(in[i]-lasts)>pp:(in[i]-lasts)<pp;
     lasts=in[i];
     if(sign!=lastsign){push(i,in[i]);if(numpts>maxpts-1)break;}
     lastsign=sign;
    }}
   }
   /* always push final point for continuity (we saved room) */
   push(samples-1,in[samples-1]);

   /* modify the points according to the three slots and their parameters */
   for(opslot=0;opslot<3;opslot++){
    pop=job.op[opslot];opp=job.oppar[opslot];
    if(pop==OP_DOUBLE){
     /* x2 points */
     t=0;
     for(i=0;i<numpts-1&&t<maxpts-4;i++){GEOMETER_SPEND();
      tx[t]=px[i];ty[t]=py[i];t++;
      if(px[i+1]-px[i]>1){ty[t]=(opp*2.f-1.f)*py[i];tx[t]=(px[i]+px[i+1])>>1;t++;}
     }
     if(t>0&&numpts>0&&tx[t-1]!=px[numpts-1]){tx[t]=px[numpts-1];ty[t]=py[numpts-1];t++;}
     for(cc=0;cc<t;cc++){GEOMETER_SPEND();px[cc]=tx[cc];py[cc]=ty[cc];}
     numpts=t;
    }else if(pop==OP_HALF||pop==OP_QUARTER){
     times=pop==OP_QUARTER?2:1;
     for(tt=0;tt<times;tt++){
      /* cut points in half. never touch first or last. */
      q=1;i=1;
      for(;q<numpts-1;i++){GEOMETER_SPEND();px[i]=px[q];py[i]=py[q];q+=2;}
      px[i]=px[numpts-1];py[i]=py[numpts-1];numpts=i+1;
     }
    }else if(pop==OP_LONGPASS){
     /* drop any point that's not at least param*samples past the previous. */
     tx[0]=px[0];ty[0]=py[0];stretch=int(opp*opp*samples);np=1;
     for(i=1;i<numpts-1;i++){GEOMETER_SPEND();
      if(px[i]-tx[np-1]>stretch){tx[np]=px[i];ty[np]=py[i];np++;if(np==maxpts)break;}
     }
     for(cc=1;cc<np;cc++){GEOMETER_SPEND();px[cc]=tx[cc];py[cc]=ty[cc];}
     px[np]=px[numpts-1];py[np]=py[numpts-1];np++;numpts=np;
    }else if(pop==OP_SHORTPASS){
     /* if an interval is longer than the specified amount, zero the 2nd endpoint. */
     stretch=int(opp*opp*samples);
     for(i=1;i<numpts;i++){GEOMETER_SPEND();if(px[i]-px[i-1]>stretch)py[i]=0;}
    }else if(pop==OP_SLOW){
     /* stretch the points out so that the tail is lost, keeping their y values. */
     factor=1.f+opp;
     for(i=0;i<numpts-1;i++){GEOMETER_SPEND();
      px[i]=int(px[i]*factor);
      if(px[i]>samples){i--;break;}
     }
     px[i]=px[numpts-1];py[i]=py[numpts-1];numpts=i+1;
    }else if(pop==OP_FAST){
     factor=1.f+opp*3.f;inv=1.f/factor;times=int(factor+1.f);outi=0;
     for(rep=0;rep<times;rep++){
      offset=int(rep*(inv*samples));
      for(k=0;k<numpts;k++){GEOMETER_SPEND();{
       int destx=int(offset+px[k]*inv);
       if(destx>=samples)goto fastDone;
       if(!(outi>0&&tx[outi-1]==destx)){tx[outi]=destx;ty[outi]=py[k];outi++;}
       if(outi>maxpts-2)goto fastDone;
      }}
     }
     fastDone:
     /* the original saves the last point here but does not keep it */
     tx[outi]=px[numpts-1];ty[outi]=py[numpts-1];
     for(cc=1;cc<outi;cc++){GEOMETER_SPEND();px[cc]=tx[cc];py[cc]=ty[cc];}
     numpts=outi;
    }
    if(numpts<1){px[0]=0;py[0]=in[0];numpts=1;}
   }

   /* rebuild the wave */
   pp=job.interparam;replan();
   if(job.interpstyle==INTERP_SHUFFLE){
    /* mix around the intervals; the parameter sets how mobile an interval is. */
    px[numpts-1]=samples-1;intervals=numpts-1;
    for(k=0;k<intervals;k++){GEOMETER_SPEND();tx[k]=k;}
    for(z=0;z<intervals;z++){GEOMETER_SPEND();
     if(nativeUnit(seed)<pp){
      int range=int(pp*pp*.5f*float(intervals));
      int dest=std::clamp(z+randomInt(-range,range),0,intervals-1);
      std::swap(tx[z],tx[dest]);
     }
    }
    for(u=0,cpos=0;u<intervals;u++){
     seg=px[tx[u]+1]-px[tx[u]];segFrom=px[tx[u]];
     for(k=0;k<seg;k++){GEOMETER_SPEND();if(cpos+k<OutCap)out[cpos+k]=in[segFrom+k];}
     cpos+=seg;
    }
   }else if(job.interpstyle==INTERP_FRIENDS){
    /* bleed each segment into the next (its "friend"), suggested by jcreed (sorta). */
    if(numpts>2)for(k=px[numpts-2];k<px[numpts-1];k++){GEOMETER_SPEND();out[k]=in[k];}
    for(x=numpts-2;x>0;x--){
     sizeright=px[x+1]-px[x];sizeleft=px[x]-px[x-1];
     tgtlen=int(sizeleft+sizeright*pp);
     if(tgtlen>0){
      for(j=tgtlen-1;j>=0;j--){GEOMETER_SPEND();{
       int from=std::clamp(int(px[x-1]+sizeleft*(j/float(tgtlen))),0,samples-1),to=j+px[x-1];
       float wet=in[from];
       if(to>=0&&to<OutCap){
        if(to>px[x]){float pct=(j-sizeleft)/float(tgtlen-sizeleft);out[to]=wet*(1.f-pct)+out[to]*pct;}
        else out[to]=wet;
       }
      }}
     }
    }
   }else if(job.interpstyle==INTERP_POLYGON||job.interpstyle==INTERP_WRONGYGON){
    /* linear interpolation (wrongygon: the wrong direction); the parameter dims
       towards straight lines at the median. */
    for(u=1;u<numpts;u++){
     denom=float(px[u]-px[u-1]);minterp=pp*(py[u-1]+py[u])*.5f;
     for(z=px[u-1];z<px[u];z++){GEOMETER_SPEND();{
      float pct=float(z-px[u-1])/denom;
      float s=job.interpstyle==INTERP_POLYGON?py[u-1]*(1.f-pct)+py[u]*pct:py[u-1]*pct+py[u]*(1.f-pct);
      out[z]=minterp+(1.f-pp)*s;
     }}
    }
    out[samples-1]=in[samples-1];
   }else if(job.interpstyle==INTERP_SMOOTHIE){
    /* cosine up or down */
    for(u=1;u<numpts;u++){
     denom=float(px[u]-px[u-1]);
     for(z=px[u-1];z<px[u];z++){GEOMETER_SPEND_COST(12);{
      float pct=float(z-px[u-1])/denom;
      float p=.5f*(-std::cos(NativePi*pct)+1.f);
      p=pp>.5f?std::pow(p,(pp-.16666667f)*3.f):std::pow(p,pp*2.f);
      out[z]=py[u-1]*(1.f-p)+py[u]*p;
     }}
    }
    out[samples-1]=in[samples-1];
   }else if(job.interpstyle==INTERP_REVERSI){
    /* x-reverse the input samples of each segment */
    for(u=1;u<numpts;u++){
     if(px[u-1]<px[u])for(z=px[u-1];z<px[u];z++){GEOMETER_SPEND();out[z]=in[(px[u]-(z+1))+px[u-1]];}
    }
   }else if(job.interpstyle==INTERP_PULSE){
    wid=int(100.f*pp);onediv=1.f/float(wid+1);
    for(z=0;z<numpts;z++){GEOMETER_SPEND();
     out[px[z]]=std::fabs(out[px[z]])>std::fabs(py[z])?out[px[z]]:py[z];
     if(wid>0){
      /* w samples on the left, then on the right */
      w=wid;
      for(i=px[z]-1;i>=0&&w>0;i--,w--){GEOMETER_SPEND();{
       float sam=py[z]*(w*onediv);
       if((out[i]+sam)*(out[i]+sam)>sam*sam)out[i]=sam;else out[i]+=sam;
      }}
      w=wid;
      for(i=px[z]+1;i<samples&&w>0;i++,w--){GEOMETER_SPEND();out[i]=py[z]*(w*onediv);}
     }
    }
   }else if(job.interpstyle==INTERP_SING){
    minterp=pp*std::min(peak,1.f);
    for(u=1;u<numpts;u++){
     denom=1.f/float(px[u]-px[u-1]);
     for(z=px[u-1];z<px[u];z++){GEOMETER_SPEND_COST(6);{
      float wand=std::sin(2.f*NativePi*(float(z-px[u-1])*denom));
      out[z]=wand*minterp+(1.f-pp)*in[z]*wand;
     }}
    }
    out[samples-1]=in[samples-1];
   }

   /* window envelope, overlap-add with the previous window's tail */
   for(i=0;i<third;i++){GEOMETER_SPEND();{
    float*back=ready[c][front^1].data();
    back[i]=out[i]*env[i]+prevmix[c][i];
    prevmix[c][i]=out[third+i]*env[third+i];
   }}
  }
  }
  pc=-1;return true;
 }
 void runFor(long units){budget=units;run();used+=units-budget;}
 void boundary(){
  fill=0;
  if(active){runFor(LONG_MAX/4);active=false;front^=1;learned=used*5/(4L*third)+8;used=0;}
  if(completed>=1){job=set;sA=(slot+2)%3;sB=slot;pc=0;active=true;rate=std::max(learned,estimate());}
  completed++;slot=(slot+1)%3;
 }
 // Work units per input frame that finish a window job in 80% of its half window:
 // the last job's measured work, but never less than this estimate from the settings.
 long estimate()const{
  long n=framesize,units=4*n+third;
  for(int s=0;s<3;s++)if(job.op[s]!=OP_NONE)units+=3*n;
  switch(job.interpstyle){
   case INTERP_SMOOTHIE:units+=12*n;break;
   case INTERP_SING:units+=6*n;break;
   case INTERP_FRIENDS:units+=2*n;break;
   case INTERP_PULSE:units+=n*(1+long(100.f*job.interparam)/4);break;   // replan() corrects dense points
   default:units+=n;
  }
  return std::max(64L,2*units*5/(4L*third)+8);
 }
 long interpUnits(int points)const{
  long n=framesize;
  switch(job.interpstyle){
   case INTERP_SMOOTHIE:return 12*n;
   case INTERP_SING:return 6*n;
   case INTERP_FRIENDS:return 2*n;
   case INTERP_PULSE:return points*(2*long(100.f*job.interparam)+1);
   default:return n;
  }
 }
 // The points are known now: make sure what is left finishes before the half window ends.
 void replan(){
  long n=framesize,left=interpUnits(numpts)+third;
  if(c==0){left+=4*n+third+interpUnits(numpts);for(int s=0;s<3;s++)if(job.op[s]!=OP_NONE)left+=3*n;}
  rate=std::max(rate,left*5/(4L*std::max(1,third-fill))+8);
 }
 void resetWindow(){
  framesize=WindowSizes[sizeIndex];third=framesize/2;
  for(int ch=0;ch<2;ch++){
   std::fill_n(ring[ch].begin(),3*third,0.f);std::fill_n(prevmix[ch].begin(),third,0.f);
   for(auto&r:ready[ch])std::fill_n(r.begin(),third,0.f);
  }
  fill=0;slot=0;front=0;completed=0;active=false;pc=-1;used=0;learned=0;
 }
 void buildEnvelope(){
  float oneDivThird=1.f/float(third);
  for(int n=0;n<third;n++){
   float p=float(n)*oneDivThird;
   if(shape==WINDOW_ARROW)p*=p;
   else if(shape==WINDOW_WEDGE)p=std::sqrt(p);
   else if(shape==WINDOW_COS)p=.5f*(-std::cos(NativePi*p)+1.f);
   env[n]=p;env[n+third]=1.f-p;
  }
 }
public:
 Geometer(){
  for(int ch=0;ch<2;ch++){ring[ch].assign(3*MaxThird,0.f);prevmix[ch].assign(MaxThird,0.f);for(auto&r:ready[ch])r.assign(MaxThird,0.f);}
  jin.assign(MaxFrame+4,0.f);jout.assign(OutCap,0.f);env.assign(MaxFrame,0.f);
  py.assign(MaxPoints,0.f);ty.assign(MaxPoints,0.f);px.assign(MaxPoints,0);tx.assign(MaxPoints,0);
  sizeIndex=9;shape=WINDOW_TRIANGLE;resetWindow();buildEnvelope();
 }
 void configure(const uint8_t*v,float)override{
  int size=nativeChoice(v[0],14),sh=nativeChoice(v[1],4);
  bool sizeChanged=size!=sizeIndex;
  if(sizeChanged){sizeIndex=size;resetWindow();}
  if(sizeChanged||sh!=shape){shape=sh;buildEnvelope();}
  set.pointstyle=nativeChoice(v[2],6);set.pointparam=v[3]*.01f;
  set.interpstyle=nativeChoice(v[4],8);set.interparam=v[5]*.01f;
  for(int s=0;s<3;s++){set.op[s]=nativeChoice(v[6+2*s],8);set.oppar[s]=v[7+2*s]*.01f;}
 }
 void clear()override{resetWindow();}
 void process(float*io,int frames)override{
  int done=0;
  while(done<frames){
   int m=std::min(frames-done,third-fill);
   const float*r0=ready[0][front].data()+fill,*r1=ready[1][front].data()+fill;
   float*w0=ring[0].data()+size_t(slot)*third+fill,*w1=ring[1].data()+size_t(slot)*third+fill;
   for(int n=0;n<m;n++){float*f=io+2*(done+n);w0[n]=f[0];w1[n]=f[1];f[0]=nativeClean(r0[n]);f[1]=nativeClean(r1[n]);}
   fill+=m;done+=m;
   if(active)runFor(rate*m);
   if(fill==third)boundary();
  }
 }
};
#undef GEOMETER_SPEND
#undef GEOMETER_SPEND_COST
}
const NativeInfo geometerInfo{FxType::Geometer,"GEOMETER","GEOM","GE",4,2,3,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeGeometer(){return std::make_unique<Geometer>();}
}
