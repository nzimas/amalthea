// SPDX-License-Identifier: GPL-3.0-only
// BROKENFFT: a one-to-one recreation of Destroy FX BrokenFFT 2 (C) 2001-2025 Tom Murphy 7,
// GPL-2.0-or-later (https://github.com/sophiapoirier/destroyfx/tree/master/brokenfft).
// The "Super Destroy FX Windowing System": frames of SIZE samples hop by half a frame,
// each frame is transformed (Don Cross style full complex FFT, "dc" method), mangled
// bin by bin, transformed back, and its real part is cross-faded into the output with
// the "linear" (square-root) envelope. The spectral operations keep the original order
// and arithmetic: operation bq (BIN HOLD, including its integer-cube quirk), very
// special (SWAP bins at random), ROTATE (real parts only), compress, spike (keep the
// loudest bins and boost the real parts to the lost loudness), the EO echo that runs
// through an 8192-bin ring across frames, first moment, postrot (SHIFT up, or mirror
// down) and anorm (NORMALISE the peak bin).
// Changes: FFTW is replaced by a self-written radix-2 FFT, so the "west" methods are
// omitted; the FFT work of each frame is spread over the next half frame (adds half a
// frame of latency; total 1.5 x SIZE). Sizes are 64..8192. Stereo runs two mono
// copies. The spike partition is a resumable Hoare quickselect with a random pivot
// instead of tqsort (same kept set, no quadratic run on equal bins). Echo feedback is
// capped at 0.9. NORMALISE aims at a peak bin of SIZE/8 (a quarter-scale sine) instead
// of 1000, which clipped hard at every size, and is suspended on frames whose input is
// silent; 0/0 in spike and moments is guarded, so tails decay to silence. Parameters fixed at their neutral defaults
// and omitted: shape (linear), operation q, perturb, M.U.G., spikehold, echo
// modulation and band limits, bride, blow, lowp, afterlow, convolve, harm.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const sizes[]={"64","128","256","512","1024","2048","4096","8192"};
const char*const holds[]={"OFF","PAIRS","EIGHTHS"};
const NativeParam params[]={
 {"SIZE",nativeChoiceValue(4,8),NativeUnit::Choice,nativeChoiceValue(2,8),nativeChoiceValue(6,8),sizes,8},
 {"SWAP",0,NativeUnit::Percent,0,15},
 {"BIN HOLD",0,NativeUnit::Choice,0,100,holds,3},
 {"ROTATE",0,NativeUnit::Percent,0,10},
 {"COMPRESS",0,NativeUnit::Percent,0,40},
 {"SPIKE",60,NativeUnit::Percent,20,80},
 {"ECHO MIX",35,NativeUnit::Percent,0,70,nullptr,0,0,1,false,"% WET"},
 {"ECHO TIME",50,NativeUnit::Percent,10,100},
 {"ECHO FEEDBACK",50,NativeUnit::Percent,0,80},
 {"SHIFT",50,NativeUnit::Bipolar,35,65},
 {"MOMENTS",0,NativeUnit::Toggle,0,0},
 {"NORMALISE",0,NativeUnit::Percent,0,60},
};
constexpr int MaxBits=13,MaxN=1<<MaxBits,MaxEcho=8192;

struct Channel {
 float ring[2*MaxN];                          // input history; a frame is its last SIZE samples
 float fr[MaxN],fi[MaxN],wr[MaxN],wi[MaxN];    // spectrum and inverse work area
 float prevmix[MaxN/2],play[2][MaxN/2];        // windowed tail; playing and pending output
 struct Ampl{float a;int i;} ampl[MaxN];
 float echor[MaxEcho],echoc[MaxEcho];int echoctr=0;
 float sampler=0,samplei=0;int samplesleft=0,stopat=1;
 float peak=0;uint32_t seed=0;
 void reset(){
  std::fill(std::begin(ring),std::end(ring),0.f);echoctr=0;sampler=samplei=0;samplesleft=0;stopat=1;peak=0;
  std::fill(std::begin(prevmix),std::end(prevmix),0.f);for(auto&p:play)std::fill(std::begin(p),std::end(p),0.f);
  std::fill(std::begin(echor),std::end(echor),0.f);std::fill(std::begin(echoc),std::end(echoc),0.f);
 }
};

class BrokenFftFx final:public NativeFx{
 // Job phases: each loops with a cursor so the work is spread over calls.
 enum Phase{Load,Forward,Bins,SpikeSum,SpikeSelect,SpikeCut,SpikeBoost,Echo,MomentSum,Shift,NormMax,NormApply,InvLoad,Inverse,Window,Done};
 Channel ch[2];
 float cosT[MaxN/2],sinT[MaxN/2];uint16_t rev[MaxN];
 int bits=10,n=1024,third=512,pendingBits=10;bool resetPending=true;
 float destruct=1,rotate=0,compress=1,spike=1,echomix=0,echotime=.5f,echofb=.5f,postrot=.5f,norm=0,binquant=0;bool moments=false;
 int playing=0,playPos=0;bool job=false,started=false;           // play[playing] sounds; play[playing^1] is being computed
 int jc=0,phase=Done,cur=0,stage=0;double loudness=0,newloudness=0;float mtr=0,mti=0,magr=0,magi=0,mar=0,mai=0;
 double budgetRate=0,credit=0;int wp=0,untilFrame=0,frameStart=0;
 int qlo=0,qhi=0,qi=0,qj=0,qk=0,qstate=0;float qp=0;   // resumable quickselect (Hoare, random pivot)
 // Work units (about one butterfly each) of one channel's job with the current settings.
 int binCost()const{return 2+(destruct<1?2:0)+(compress<.9999999f?5:0);}
 void schedule(){
  double e=n*(1+bits/2.0+binCost()+(spike<.999999f?10:0)+2+(moments?2:0)+1+(norm>.0001f?2:0)+1+bits/2.0+1);
  budgetRate=2*e/third*1.25;   // both channels inside the half frame before the next one
 }

 int revOf(int i)const{return rev[i]>>(MaxBits-bits);}
 void applyReset(){
  bits=pendingBits;n=1<<bits;third=n/2;resetPending=false;
  for(int c=0;c<2;c++){ch[c].reset();ch[c].seed=0xbf0f7a11u+c*0x9e37u;}
  playing=0;playPos=0;job=false;started=false;phase=Done;credit=0;wp=0;untilFrame=n;schedule();
 }
 // One radix-2 butterfly pass: butterfly b of the stage with half size h = 1<<s.
 void butterflies(float*re,float*im,int s,float sign,int&budget){
  int h=1<<s,mask=h-1,stride=MaxN>>(s+1);
  for(;cur<n/2&&budget>0;cur++,budget--){
   int k=cur&mask,i=((cur>>s)<<(s+1))+k,j=i+h;float c=cosT[k*stride],sn=sign*sinT[k*stride];
   float tr=re[j]*c-im[j]*sn,ti=re[j]*sn+im[j]*c;
   re[j]=re[i]-tr;im[j]=im[i]-ti;re[i]+=tr;im[i]+=ti;
  }
 }
 void next(int p){phase=p;cur=0;stage=0;}
 // Advances the current job by `budget` units; true when both channels are finished.
 bool work(int budget){
  while(budget>0&&job){
   Channel&C=ch[jc];float*fr=C.fr,*fi=C.fi;
   switch(phase){
    case Load:   // the frame, bit-reversed into the spectrum
     if(!cur)C.peak=0;
     for(;cur<n&&budget>0;cur++,budget--){float x=C.ring[(frameStart+cur)&(2*n-1)];int r=revOf(cur);fr[r]=x;fi[r]=0;C.peak=std::max(C.peak,std::fabs(x));}
     if(cur==n)next(Forward);break;
    case Forward:butterflies(fr,fi,stage,-1,budget);if(cur==n/2){cur=0;if(++stage==bits)next(Bins);}break;
    case Bins:
     for(int cost=binCost();cur<n&&budget>0;cur++,budget-=cost){
      int i=cur;
      if(binquant>0&&(C.samplesleft-->0)){fr[i]=C.sampler;fi[i]=C.samplei;}            // operation bq
      else{C.sampler=fr[i];C.samplei=fi[i];C.samplesleft=1+int(binquant*binquant*binquant)*(n>>3);}
      if(destruct<1&&nativeUnit(C.seed)>destruct){                                       // very special
       int j=int(nativeUnit(C.seed)*(n-1));std::swap(fr[i],fr[j]);std::swap(fi[i],fi[j]);}
      fr[i]=fr[(i+int(rotate*n))%n];                                                     // rotate (real only)
      if(compress<.9999999f){fr[i]=(fr[i]<0?-1:1)*std::pow(std::fabs(fr[i]),compress);fi[i]=(fi[i]<0?-1:1)*std::pow(std::fabs(fi[i]),compress);}
     }
     if(cur==n)next(spike<.999999f?SpikeSum:Echo);break;
    case SpikeSum:
     if(!cur)loudness=0;
     for(;cur<n&&budget>0;cur++,budget--){int i=cur;float a=fr[i]*fr[i]+fi[i]*fi[i];loudness+=a;C.ampl[i]={a,i};}
     if(cur==n){next(SpikeSelect);C.stopat=1+int(spike*spike*spike*n);qk=C.stopat-1;qlo=0;qhi=n-1;qstate=0;}
     break;
    case SpikeSelect:   // partition so the first stopat-1 bins are the loudest
     while(budget>0&&phase==SpikeSelect){
      budget-=3;auto*a=C.ampl;
      if(qstate==0){
       if(qhi<=qlo||qk<=0||qk>=n){newloudness=loudness;next(SpikeCut);cur=std::max(0,C.stopat-1);break;}
       int p=qlo+int(nativeRandom(C.seed)%uint32_t(qhi-qlo+1));std::swap(a[qlo],a[p]);qp=a[qlo].a;qi=qlo-1;qj=qhi+1;qstate=1;
      }else if(qstate==1){if(a[++qi].a<=qp)qstate=2;}
      else if(a[--qj].a>=qp){
       if(qi>=qj){if(qk<=qj)qhi=qj;else qlo=qj+1;qstate=0;}else{std::swap(a[qi],a[qj]);qstate=1;}
      }
     }
     break;
    case SpikeCut:
     for(;cur<n&&budget>0;cur++,budget--){newloudness-=C.ampl[cur].a;fr[C.ampl[cur].i]=0;fi[C.ampl[cur].i]=0;}
     if(cur==n)next(SpikeBoost);break;
    case SpikeBoost:{
     float boost=newloudness>1e-12*loudness&&newloudness>0?float(loudness/newloudness):1.f;   // boost what remains
     for(;cur<n&&budget>0;cur++,budget--)fr[cur]*=boost;
     if(cur==n)next(Echo);break;}
    case Echo:   // EO FX: bins stream through the ring; echo time counts in bins
     for(;cur<n&&budget>0;cur++,budget-=2){
      int iy=cur;C.echor[C.echoctr]=fr[iy];C.echoc[C.echoctr++]=fi[iy];C.echoctr%=MaxEcho;
      if(echomix>.000001f){
       int echot=int(MaxEcho-MaxEcho*std::min(1.f,echotime));int xx=(C.echoctr+echot)%MaxEcho,last=(C.echoctr+MaxEcho-1)%MaxEcho;
       if(echofb>.00001f){C.echoc[last]+=C.echoc[xx]*echofb;C.echor[last]+=C.echor[xx]*echofb;
        if(std::fabs(C.echoc[last])<1e-20f)C.echoc[last]=0;if(std::fabs(C.echor[last])<1e-20f)C.echor[last]=0;}
       fr[iy]=(1-echomix)*fr[iy]+echomix*C.echor[xx];fi[iy]=(1-echomix)*fi[iy]+echomix*C.echoc[xx];
      }
     }
     if(cur==n)next(moments?MomentSum:Shift);break;
    case MomentSum:   // first moment: all of the spectrum in one bin
     if(!cur)mtr=mti=magr=magi=0;
     for(;cur<n&&budget>0;cur++,budget--){mtr+=fr[cur]*cur;mti+=fi[cur]*cur;magr+=fr[cur];magi+=fi[cur];}
     if(cur==n){
      mtr=magr!=0?mtr/magr:0;mti=magi!=0?mti/magi:0;
      std::fill(fr,fr+n,0.f);std::fill(fi,fi+n,0.f);budget-=n;
      int mx=int(std::fmod(std::min(std::fabs(mtr),1e9f),float(n))),my=int(std::fmod(std::min(std::fabs(mti),1e9f),float(n)));
      fr[std::clamp(mx,0,n-1)]=magr;fi[std::clamp(my,0,n-1)]=magi;next(Shift);
     }
     break;
    case Shift:
     if(postrot>.5f){int rotn=int((postrot-.5f)*n);
      if(!rotn){next(NormMax);break;}
      for(;cur<n&&budget>0;cur++,budget--){int v=n-1-cur;if(v<rotn)fr[v]=fi[v]=0;else{fr[v]=fr[v-rotn];fi[v]=fi[v-rotn];}}
     }else if(postrot<.5f){int rotn=int((.5f-postrot)*n);
      if(!rotn){next(NormMax);break;}
      for(;cur<n&&budget>0;cur++,budget--){int v=cur;if(v>n-rotn)fr[v]=fi[v]=0;else{fr[v]=fr[(n-rotn)-v];fi[v]=fi[(n-rotn)-v];}}
     }else cur=n;
     if(cur==n)next(norm>.0001f&&C.peak>1e-6f?NormMax:InvLoad);break;
    case NormMax:
     if(!cur)mar=mai=0;
     for(;cur<n&&budget>0;cur++,budget--){mar=std::max(mar,std::fabs(fr[cur]));mai=std::max(mai,std::fabs(fi[cur]));}
     if(cur==n){if(norm>.0001f&&C.peak>1e-6f)next(NormApply);else next(InvLoad);}break;
    case NormApply:{
     float target=.125f*n,facr=mar>.0001f?target/mar:1,faci=mai>.0001f?target/mai:1;   // a quarter-scale sine's bin
     for(;cur<n&&budget>0;cur++,budget--){fr[cur]=norm*facr*fr[cur]+(1-norm)*fr[cur];fi[cur]=norm*faci*fi[cur]+(1-norm)*fi[cur];}
     if(cur==n)next(InvLoad);break;}
    case InvLoad:
     for(;cur<n&&budget>0;cur++,budget--){int r=revOf(cur);C.wr[r]=fr[cur];C.wi[r]=fi[cur];}
     if(cur==n)next(Inverse);break;
    case Inverse:butterflies(C.wr,C.wi,stage,1,budget);if(cur==n/2){cur=0;if(++stage==bits)next(Window);}break;
    case Window:{   // real part / n, linear (square-root) envelope, mix in the previous tail
     float*out=C.play[playing^1],inv=1.f/n,oneDivThird=1.f/third;
     for(;cur<third&&budget>0;cur++,budget--){
      int z=cur;float p=std::sqrt(z*oneDivThird);
      float a=C.wr[z]*inv*p,b=C.wr[z+third]*inv*(1-p);
      out[z]=nativeClean(a+C.prevmix[z]);C.prevmix[z]=std::isfinite(b)?b:0;
     }
     if(cur==third){if(jc==0){jc=1;next(Load);}else{job=false;phase=Done;}}
     break;}
    default:job=false;break;
   }
  }
  return !job;
 }
public:
 BrokenFftFx(){
  for(int i=0;i<MaxN/2;i++){double a=2*3.14159265358979323846*i/MaxN;cosT[i]=float(std::cos(a));sinT[i]=float(std::sin(a));}
  for(int i=0;i<MaxN;i++){int r=0;for(int b=0;b<MaxBits;b++)if(i&(1<<b))r|=1<<(MaxBits-1-b);rev[i]=uint16_t(r);}
  applyReset();
 }
 void configure(const uint8_t*v,float)override{
  int b=6+nativeChoice(v[0],8);if(b!=bits){pendingBits=b;resetPending=true;}
  destruct=1-v[1]*.01f;binquant=std::array<float,3>{0,.5f,1}[size_t(nativeChoice(v[2],3))];
  rotate=v[3]*.01f;compress=1-v[4]*.01f;spike=1-v[5]*.01f;
  echomix=v[6]*.01f;echotime=v[7]*.01f;echofb=std::min(.9f,v[8]*.01f);
  postrot=.5f+(v[9]*2-100)*.005f;moments=v[10]>=50;norm=v[11]*.01f;
  if(!resetPending)schedule();
 }
 void clear()override{resetPending=true;pendingBits=bits;}
 void process(float*io,int frames)override{
  if(resetPending)applyReset();
  credit+=frames*budgetRate;int budget=int(credit);credit-=budget;
  for(int f=0;f<frames;f++){
   for(int c=0;c<2;c++)ch[c].ring[wp]=io[f*2+c];
   wp=(wp+1)&(2*n-1);
   if(--untilFrame==0){
    // Frame full: the pending job's output starts playing, the new frame becomes the job.
    if(job)work(1<<30);
    if(started)playing^=1;
    started=true;playPos=0;untilFrame=third;frameStart=(wp-n)&(2*n-1);
    job=true;jc=0;next(Load);
   }
   for(int c=0;c<2;c++)io[f*2+c]=playPos<third?ch[c].play[playing][playPos]:0.f;
   playPos++;
  }
  if(job)work(budget);
 }
};
}
const NativeInfo brokenFftInfo{FxType::BrokenFft,"BROKENFFT","BRKFFT","BF",2,2,7,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeBrokenFft(){return std::make_unique<BrokenFftFx>();}
}
