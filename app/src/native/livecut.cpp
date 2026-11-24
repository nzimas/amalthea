// SPDX-License-Identifier: GPL-3.0-only
// LIVECUT: a one-to-one recreation of Livecut (C) 2003-2004 Remy Muller, a live port of
// Nick Collins' BBCut library, GPL-2.0-or-later (https://github.com/mdsp/Livecut).
// The BBCutter walks the bar in SUBDIV units: at a bar start it picks a phrase of
// PHRASE MIN..MAX bars, and whenever a block ends the cut procedure chooses the next
// block and its cuts. The live player records the block's input as its first cut plays
// and replays it for the repeats, with an exponential fade, a per-cut amplitude and a
// rotation-matrix pan; a bit crusher follows each block and cut.
// CUTPROC11 plays odd unit cuts with REPEATS and an end-of-phrase stutter (STUTTER
// AREA, STUTTER CHANCE); WARPCUT plays straight blocks (STRAIGHT) or regular and
// accelerating/ritardando rolls; SQPUSHER plays quavers or semiquaver pairs weighted by
// bar position (ACTIVITY) with a fill from its thirteen cut sequences in the last bar.
// The original's quirks are kept (SQPusher block sizes assume 8 units per bar, the two
// empty fill steps, the crusher's start-plus-phase-times-end interpolation).
// Changed or omitted: the tempo is the reference bpm in 4/4 and the cutter realigns to
// the downbeat on transport Start instead of reading the host play head; the comb and
// SQPAmp (not on the original panel) and pitch detune (default 0) are omitted; fade is
// fixed at 2 ms, duty and fill duty at 1, minimum repeats at 0, maximum amplitude at 1,
// WarpCut regular/ritard/speed at their defaults 0.5/0.5/0.9; the seed knob is replaced
// by a fixed seed; CRUSHER is one amount setting the bit and sample-rate ranges.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace amalthea {
namespace {
const char*const procNames[]={"CUTPROC11","WARPCUT","SQPUSHER"};
const char*const subdivNames[]={"6","8","12","16","18","24","32"};
const int subdivValues[]={6,8,12,16,18,24,32};
const char*const barNames[]={"1 BAR","2 BARS","3 BARS","4 BARS","5 BARS","6 BARS","7 BARS","8 BARS"};
const char*const repeatNames[]={"0","1","2","3","4"};
const NativeParam params[]={
 {"CUT PROC",nativeChoiceValue(0,3),NativeUnit::Choice,0,100,procNames,3},
 {"SUBDIV",nativeChoiceValue(1,7),NativeUnit::Choice,nativeChoiceValue(1,7),nativeChoiceValue(3,7),subdivNames,7},
 {"PHRASE MIN",nativeChoiceValue(0,8),NativeUnit::Choice,nativeChoiceValue(0,8),nativeChoiceValue(1,8),barNames,8},
 {"PHRASE MAX",nativeChoiceValue(3,8),NativeUnit::Choice,nativeChoiceValue(1,8),nativeChoiceValue(3,8),barNames,8},
 {"STUTTER CHANCE",80,NativeUnit::Percent,30,100},
 {"STUTTER AREA",50,NativeUnit::Percent,20,80},
 {"REPEATS",nativeChoiceValue(1,5),NativeUnit::Choice,nativeChoiceValue(0,5),nativeChoiceValue(2,5),repeatNames,5},
 {"STRAIGHT",30,NativeUnit::Percent,10,70},
 {"ACTIVITY",50,NativeUnit::Percent,20,80},
 {"AMP MIN",100,NativeUnit::Percent,50,100},
 {"PAN SPREAD",20,NativeUnit::Percent,0,60},
 {"CRUSHER",0,NativeUnit::Percent,0,25},
};
// SQPusher's fill cut sequences, in beats, exactly as sqpusherinit() builds them.
struct FillStep{int n;double beats[6];};
struct Fill{int n;FillStep steps[5];};
const Fill fills[13]={
 {2,{{4,{.75,.75,.75,.75}},{1,{1}}}},
 {3,{{2,{.5,1}},{1,{1}},{2,{1,.5}}}},
 {3,{{1,{.5}},{3,{1,1,1}},{1,{.5}}}},
 {5,{{1,{.571429}},{2,{.571429,.571429}},{2,{.571429,.571429}},{1,{.571429}},{2,{.285714,.285716}}}},
 {3,{{2,{1,.5}},{2,{1,.5}},{2,{.5,.5}}}},
 {3,{{2,{.5,.5}},{3,{.66,.67,.67}},{1,{1}}}},
 {4,{{1,{.34}},{3,{.33,.33,2.33}},{2,{.34,.33}},{0,{}}}},
 {4,{{1,{1.4}},{2,{.4,.4}},{2,{.6,.2}},{1,{1}}}},
 {3,{{6,{.167,.167,.166,1,1,.5}},{1,{1}},{0,{}}}},
 {2,{{3,{1.5,.5,1}},{4,{.25,.25,.25,.25}}}},
 {4,{{2,{.2,.2}},{2,{.4,.4}},{2,{.4,.4}},{1,{2}}}},
 {2,{{3,{.75,.75,1}},{6,{.25,.25,.25,.25,.25,.25}}}},
 {5,{{2,{.5,1}},{1,{.5}},{4,{.125,.125,.125,.125}},{1,{1}},{3,{.167,.167,.166}}}},
};
struct CutInfo{long size=0,length=0;float pan=0,amp=1;};
constexpr int MaxCuts=256;          // the end-of-phrase stutter tops out at 31 units x 8
constexpr int Capacity=1<<18;       // recorded frames: one bar down to ~40 bpm (2 MB)
constexpr float PiOver4=NativePi/4;

class LivecutFx final:public NativeFx{
 // parameters
 int proc=0,subdiv=8;long minphrase=1,maxphrase=4,minrepeats=1,maxrepeats=2;
 float stutterchance=.8f,stutterarea=.5f,straightchance=.3f,regularchance=.5f,ritardchance=.5f,accel=.9f,activity=.5f;
 float minamp=1,maxamp=1,minpan=-.2f,maxpan=.2f,dutycycle=1,filldutycycle=1;
 double spb=22050;uint32_t seed=0x11ec0717,epoch=0;
 // BBCutter state
 long unitsdone=0,totalunits=0,barsnow=0,unitsinblock=0,unitsinsideblock=0;
 double position=0;long lastUnit=-1;
 // SQPusher state
 bool fill=false;long fillnumber=0,fillpos=0;
 // LivePlayer state
 std::vector<float> input;std::array<CutInfo,MaxCuts> cuts,next;int cutCount=0;
 long currentcut=0,inputindex=0,readindex=0,maxcutlength=0;float ll=0,lr=0,rl=0,rr=0,fade=88;
 // BitCrusher state
 bool crush=false;float minbits=32,maxbits=32,startbits=32,endbits=32,multiplier=4294967296.f,divider=1/4294967296.f;
 float minfreq=44100,maxfreq=44100,startfreq=44100,endfreq=44100,lag=1,count=0,memory1=0,memory2=0;

 double randomfloat(double lo,double hi){return lo+(hi-lo)*double(nativeRandom(seed)>>8)/16777215.0;}
 long randominteger(long lo,long hi){return long(.5000001+randomfloat(double(lo),double(hi)));}
 template<class T>T wchoose(const T*values,const double*weights,int n){
  double total=0;for(int i=0;i<n;i++)total+=weights[i];double v=randomfloat(0,total),sum=0;
  for(int i=0;i<n;i++){sum+=weights[i];if(v<sum)return values[i];}return values[n-1];
 }
 double samplesPerUnit()const{return spb*4/subdiv;}
 void setPan(const CutInfo&c){
  float a=c.amp,p=c.pan;ll=a*(p<0?1.f:std::cos(p*2*PiOver4));lr=a*(p<0?0.f:std::sin(p*2*PiOver4));
  rl=a*(p>0?0.f:-std::sin(p*2*PiOver4));rr=a*(p>0?1.f:std::cos(p*2*PiOver4));
 }

 // ---- cut procedures
 long choosePhraseLength(){if(proc==2)fill=false;return randominteger(minphrase,maxphrase);}
 int cutProc11(double spu){
  const long unitsleft=totalunits-unitsdone;long unitsincut=1,repeats=1;
  if(float(unitsleft)/float(subdiv)<stutterarea&&randomfloat(0,1)<stutterchance){   // end of phrase stutter
   static const long numbers[]={1,2,3,4,6,8};static const double probs[]={.4,.3,.1,.1,.05,.05};
   long multiplier=wchoose(numbers,probs,6);repeats=std::min<long>(unitsleft*multiplier,MaxCuts);
   double unitsinthiscut=1.0/multiplier;unitsinblock=long(repeats*unitsinthiscut);
   float startpan=float(randomfloat(minpan,maxpan)),endpan=float(randomfloat(minpan,maxpan));
   float startamp=float(randomfloat(minamp,maxamp)),endamp=float(randomfloat(minamp,maxamp));
   for(int i=0;i<repeats;i++){float phase=float(i)/float(repeats);long cutlength=long(unitsinthiscut*spu);
    next[i]=CutInfo{};next[i].size=cutlength;next[i].length=long(float(cutlength)*filldutycycle);
    next[i].pan=startpan+(endpan-startpan)*phase;next[i].amp=startamp+(endamp-startamp)*phase;}
   return int(repeats);
  }
  unitsincut=2*long(randomfloat(0,float(subdiv)/4.f)+.5f)+1;   // odd units, 1..subdiv/2+1
  repeats=randominteger(minrepeats,maxrepeats);
  while(unitsincut>unitsleft)unitsincut-=2;if(unitsincut<0)unitsincut=0;
  while(repeats*unitsincut>unitsleft){
   if(repeats>1)repeats--;else if(unitsleft<=subdiv/2+1){unitsincut=unitsleft;repeats=1;}else{unitsincut=1;repeats=1;}
  }
  unitsinblock=repeats*unitsincut;
  for(int i=0;i<repeats;i++){next[i]=CutInfo{};next[i].size=long(unitsincut*spu);next[i].length=long(dutycycle*unitsincut*spu);next[i].amp=float(randomfloat(minamp,maxamp));}
  return int(repeats);
 }
 int warpCut(double spu){
  const long unitsleft=totalunits-unitsdone;long repeats=1;
  static const long blocks[]={1,2,4};static const double blockprobs[]={.5,.4,.1};
  unitsinblock=wchoose(blocks,blockprobs,3);if(unitsinblock>unitsleft)unitsinblock=unitsleft;
  if(randomfloat(0,1)<straightchance){
   double temp=double(unitsinblock)/double(repeats);
   next[0]=CutInfo{};next[0].size=long(spu*temp);next[0].length=long(double(std::max(long(dutycycle*temp),1L))*spu);next[0].amp=float(randomfloat(minamp,maxamp));
   return 1;
  }
  static const long repeatsarray[]={4,8,16,32};
  repeats=float(unitsinblock)/float(subdiv)<1.f?repeatsarray[randominteger(0,2)]:repeatsarray[randominteger(1,3)];
  float startpan=float(randomfloat(minpan,maxpan)),endpan=float(randomfloat(minpan,maxpan));
  float startamp=float(randomfloat(minamp,maxamp)),endamp=float(randomfloat(minamp,maxamp));
  bool regular=randomfloat(0,1)<regularchance;
  double temp=regular?double(unitsinblock)/double(repeats):unitsinblock*(1.0-double(accel))/(1.0-std::pow(double(accel),double(repeats)));
  for(int i=0;i<repeats;i++){
   float phase=float(i)/float(repeats);long l=regular?long(spu*temp+.5):long(spu*temp*std::pow(double(accel),double(i)));
   next[i]=CutInfo{};next[i].size=l;next[i].length=long(float(l)*filldutycycle);
   next[i].pan=startpan+(endpan-startpan)*phase;next[i].amp=startamp+(endamp-startamp)*phase;
  }
  if(!regular&&randomfloat(0,1)<ritardchance)std::reverse(next.begin(),next.begin()+repeats);
  return int(repeats);
 }
 int sqPusher(double spu){
  const long unitsleft=subdiv-(unitsdone%subdiv);   // one bar at a time
  double done=double(unitsdone)/double(subdiv);
  long beatpos=long(done*16.0)%4,quaver=long(done*8.0)%8;
  static const double sqweights[]={0,.3,0,.5,.7,.8,.9,.6};
  double sqchance=sqweights[quaver]*activity,beat=spu*double(subdiv)/4.0;
  if(totalunits-unitsdone==subdiv){fill=true;fillnumber=randominteger(0,12);fillpos=0;}
  if(fill){
   if(fillpos<fills[fillnumber].n){
    const FillStep&step=fills[fillnumber].steps[fillpos];
    for(int i=0;i<step.n;i++){next[i]=CutInfo{};next[i].size=long(beat*step.beats[i]);next[i].length=long(beat*step.beats[i]*filldutycycle);
     next[i].pan=float(randomfloat(minpan,maxpan));next[i].amp=float(randomfloat(minamp,maxamp));}
    if(unitsinblock>unitsleft)unitsinblock=unitsleft;   // the original keeps the previous block size here
    fillpos++;return step.n;
   }
   fill=false;fillpos=0;
  }
  long temp=1+2*randominteger(0,1);if(beatpos==2)temp=1;
  unitsinblock=long(double(temp*8.0)/double(subdiv));if(unitsinblock>unitsleft)unitsinblock=unitsleft;
  if(randomfloat(0,1)<sqchance){   // 2*temp semiquavers
   for(int i=0;i<temp*2;i++){next[i]=CutInfo{};next[i].size=long(.25*beat);next[i].pan=float(randomfloat(minpan,maxpan));next[i].amp=float(randomfloat(minamp,maxamp));next[i].length=long(.25*beat*dutycycle);}
   return int(temp*2);
  }
  for(int i=0;i<temp;i++){next[i]=CutInfo{};next[i].size=long(.5*beat);next[i].amp=float(randomfloat(minamp,maxamp));next[i].length=long(.5*beat*dutycycle);}
  return int(temp);   // or temp quavers, the same duration
 }

 // ---- bit crusher listener
 void crusherOnCut(long cut,long numcuts){
  float bits=startbits+(float(cut)/float(numcuts))*endbits;multiplier=std::pow(2.f,bits);divider=1.f/multiplier;
  float freq=startfreq+(float(cut)/float(numcuts))*endfreq;lag=44100.f/freq;
 }
 void crusherOnBlock(){
  startbits=float(randomfloat(minbits,maxbits));endbits=float(randomfloat(minbits,maxbits));
  startfreq=float(randomfloat(minfreq,maxfreq));endfreq=float(randomfloat(minfreq,maxfreq));
 }

 // ---- BBCutter
 void playerOnBlock(int n){
  if(n<=0)return;
  std::copy(next.begin(),next.begin()+n,cuts.begin());cutCount=n;setPan(cuts[0]);
  inputindex=readindex=0;currentcut=0;crusherOnCut(0,n);
  maxcutlength=0;for(int i=0;i<n;i++)maxcutlength=std::max(maxcutlength,cuts[i].size);
  maxcutlength=std::min<long>(maxcutlength,Capacity);
 }
 void block(){
  unitsinsideblock=0;double spu=samplesPerUnit();
  int n=proc==0?cutProc11(spu):proc==1?warpCut(spu):sqPusher(spu);
  playerOnBlock(n);crusherOnBlock();
 }
 void unit(long sd){
  if(totalunits<=0||unitsdone>=totalunits||unitsdone<0){   // out of phrase: wait for a bar start
   if(sd!=0)return;
   barsnow=choosePhraseLength();totalunits=barsnow*subdiv;unitsdone=0;
  }
  if(unitsinsideblock>=unitsinblock||unitsinsideblock<0)block();
  unitsinsideblock++;unitsdone++;
 }
 void setPosition(long sd){long delta=sd-(unitsdone%subdiv);unitsinsideblock+=delta;unitsdone+=delta;unit(sd);}
 void realign(){position=0;lastUnit=-1;unitsdone=totalunits=unitsinblock=unitsinsideblock=0;fill=false;fillpos=0;cutCount=0;currentcut=0;}

 bool tick(float&outL,float&outR,float inL,float inR){
  outL=outR=0;
  if(currentcut>=cutCount)return false;
  if(inputindex<maxcutlength){input[size_t(inputindex)*2]=inL;input[size_t(inputindex)*2+1]=inR;inputindex++;}
  const CutInfo&c=cuts[currentcut];
  if(readindex<c.length){   // duty cycle on
   float l=0,r=0;if(readindex<inputindex){l=input[size_t(readindex)*2];r=input[size_t(readindex)*2+1];}
   float i=float(readindex),size=float(c.length);
   float env=(1-std::exp(-5*i/fade))*(1-std::exp(5*(i-size)/fade));
   outL=env*(ll*l+rl*r);outR=env*(lr*l+rr*r);
  }
  readindex++;
  if(readindex>=c.size){   // end of cut
   currentcut++;readindex=0;
   if(currentcut>=cutCount){outL=outR=0;return false;}
   setPan(cuts[currentcut]);crusherOnCut(currentcut,cutCount);return true;
  }
  return false;
 }
public:
 LivecutFx():input(size_t(Capacity)*2,0.f){}
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);proc=nativeChoice(v[0],3);
  int s=subdivValues[nativeChoice(v[1],7)];
  if(s!=subdiv){position=position*s/subdiv;lastUnit=lastUnit<0?-1:long(std::floor(position));subdiv=s;}
  minphrase=1+nativeChoice(v[2],8);maxphrase=1+nativeChoice(v[3],8);
  stutterchance=v[4]*.01f;stutterarea=v[5]*.01f;
  minrepeats=1;maxrepeats=1+nativeChoice(v[6],5);   // occurrences, as SetMaxRepeats(v+1)
  straightchance=v[7]*.01f;activity=v[8]*.01f;minamp=v[9]*.01f;maxamp=1;
  float spread=v[10]*.01f;minpan=-spread;maxpan=spread;
  float a=v[11]*.01f;crush=a>0;
  minbits=16-14*a;maxbits=16-10*a;minfreq=44100*std::pow(1/40.f,a);maxfreq=44100*std::pow(1/10.f,a);
 }
 void clear()override{
  std::fill(input.begin(),input.end(),0.f);realign();count=0;memory1=memory2=0;
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;realign();}   // downbeat
  const double step=1.0/samplesPerUnit();
  for(int i=0;i<frames;i++){
   long u=long(std::floor(position));
   if(u!=lastUnit){lastUnit=u;setPosition(u%subdiv);}
   float l,r;tick(l,r,io[i*2],io[i*2+1]);
   if(crush){
    if(count>lag){memory1=std::floor(l*multiplier)*divider;memory2=std::floor(r*multiplier)*divider;while(count>lag)count-=lag;}
    count+=1.f;l=memory1;r=memory2;
   }
   io[i*2]=nativeClean(l);io[i*2+1]=nativeClean(r);
   position+=step;
  }
 }
};
}
const NativeInfo livecutInfo{FxType::Livecut,"LIVECUT","LIVECUT","LC",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeLivecut(){return std::make_unique<LivecutFx>();}
}
