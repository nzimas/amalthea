// SPDX-License-Identifier: GPL-3.0-only
// BEAT REPEAT: a one-to-one recreation of the Stutter engine of Bespoke Synth's
// StutterControl, (C) 2012-2021 Ryan Challinor, GPL-3.0-or-later
// (https://github.com/BespokeSynth/BespokeSynth, Source/Stutter.cpp, StutterControl.cpp).
// The input is always recorded into a rolling buffer; a stutter captures the most recent
// interval of it and loops that capture at a speed that may ramp (Bespoke's Ramp), with a
// 3 ms blend in and out, the 100-sample wraparound blend at the loop end, the jump
// blender between stutters, the optional squared fade over each repeat and the
// subdivide divisor. The sixteen StutterControl types are kept with their intervals,
// speeds and ramp times (half .. 64th, reverse, ramp in/out, tumble up/down, half/double
// speed, dotted eighth, quarter triplets, free with its length and speed sliders).
// Changes: Bespoke starts stutters from checkboxes, keys, grids or MIDI notes; here they
// are triggered automatically on a tempo grid (TRIGGER EVERY) with a seeded PROBABILITY,
// held for LENGTH, the type drawn from a MODES pool (AUTO is Bespoke's own auto-stutter
// set). Like Bespoke's auto mode the downbeat may be left alone. Every trigger captures
// afresh (Bespoke continues an identical held stutter), the capture is copied
// incrementally instead of in one go, subdivide also shortens the loop (Bespoke divided
// only the capture), free mode captures its full length, and the capture is clamped to
// 3.5 s. The seeded pattern restarts on transport Start and optionally every few bars.
// GUI, buffer drawing, grid lights and the mutex-guarded stutter stack are omitted.
#include "../native_fx_list.hpp"
#include "native_dsp.hpp"
#include <algorithm>
#include <cmath>
namespace amalthea {
namespace {
const char*const modePools[]={"ALL","AUTO","GRID","SHORT","TUMBLE","SPEED","REVERSE","FREE"};
const char*const subdivisions[]={"1","2","3","4","5","6","7","8"};
const char*const loops[]={"OFF","1 BAR","2 BARS","4 BARS"};
const NativeParam params[]={
 {"TRIGGER EVERY",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(4),nativeDivisionValue(10)},
 {"PROBABILITY",50,NativeUnit::Percent,25,75,nullptr,0,0,1,false,"% CHANCE"},
 {"LENGTH",nativeDivisionValue(7),NativeUnit::Division,nativeDivisionValue(4),nativeDivisionValue(12)},
 {"MODES",nativeChoiceValue(1,8),NativeUnit::Choice,0,100,modePools,8},
 {"FADE",0,NativeUnit::Percent,0,60},
 {"SUBDIVIDE",0,NativeUnit::Choice,0,nativeChoiceValue(3,8),subdivisions,8},
 {"SKIP DOWNBEAT",100,NativeUnit::Toggle,0,100},
 {"FREE LENGTH",50,NativeUnit::Range,20,80,nullptr,0,5,250,true," ms"},
 {"FREE SPEED",50,NativeUnit::Range,30,70,nullptr,0,0,2,false,"x"},
 {"SEED",0,NativeUnit::Percent,0,100},
 {"PATTERN LOOP",0,NativeUnit::Choice,0,100,loops,4},
};
// StutterControl::GetStutter: interval in beats (0 = free), speed start/end, blend ms.
struct StutterParams{double beats;float speedStart,speedEnd,blendMs;};
const StutterParams stutterTypes[16]={
 {2,1,1,0},{1,1,1,0},{.5,1,1,0},{.25,1,1,0},{.125,1,1,0},{.0625,1,1,0},   // half .. 64th
 {2,-1,-1,0},                 // reverse
 {.5,0,1,500},{2,1,0,700},    // ramp in, ramp out
 {.5,1,10,10000},{.125,1,0,2000},   // tumble up, tumble down
 {.5,.5f,.5f,0},{.5,2,2,0},   // half speed, double speed
 {.75,1,1,0},{2/3.,1,1,0},    // dotted eighth, quarter triplets
 {0,1,1,0},                   // free
};
const uint8_t poolAll[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
const uint8_t poolAuto[]={0,1,2,3,4,5,6,11,12};   // Stutter::OnTimeEvent's randomStutters
const uint8_t poolGrid[]={0,1,2,3,4,5,13,14};
const uint8_t poolShort[]={3,4,5};
const uint8_t poolTumble[]={7,8,9,10};
const uint8_t poolSpeed[]={11,12,7,8};
const uint8_t poolReverse[]={6};
const uint8_t poolFree[]={15};
struct Pool{const uint8_t*types;int count;};
const Pool pools[8]={{poolAll,16},{poolAuto,9},{poolGrid,8},{poolShort,3},{poolTumble,4},{poolSpeed,4},{poolReverse,1},{poolFree,1}};
constexpr int BlendWrap=100;      // STUTTER_BLEND_WRAPAROUND_SAMPLES
constexpr int JumpBlend=100;      // JUMP_BLEND_SAMPLES
constexpr long StartBlend=132;    // STUTTER_START_BLEND_MS (3 ms)
constexpr int CaptureMax=154350;  // 3.5 s
constexpr int RecordSize=176400;  // 4 s: the capture stays intact while it is copied
constexpr int CopyRate=16;        // capture frames copied per sample
// Bespoke's Ramp: linear from the current value to a target over a time.
struct Ramp{
 float from=0,to=0;long len=0,t=0;
 float value()const{return t>=len?to:from+(to-from)*float(t)/float(len);}
 void set(float v){from=to=v;len=t=0;}
 void start(float a,float b,long n){from=a;to=b;len=std::max(0L,n);t=0;}
 void startTo(float b,long n){start(value(),b,n);}
 void tick(){if(t<len)t++;}
};
class BeatRepeatFx final:public NativeFx{
 std::vector<float> record,stutter;int write=0;
 int captureStart=0,captureLength=1,copied=0;
 bool stuttering=false;int current=0;float stutterPos=0;
 Ramp speed,blend,freeLength;float jump[2][JumpBlend]{};int jumpAt=JumpBlend;
 double spb=22050,beat=0;long step=-1,loopIndex=0;long hold=0;
 int every=7,length=7,pool=1,subdivide=1,loopBars=0;float probability=.5f,fade=0,freeSeconds=.05f,freeSpeed=1;bool skipDownbeat=true;
 uint32_t seed=0x57e77e12,seedBase=0;uint32_t epoch=0;
 void reseed(){uint32_t h=0x57e77e12u+seedBase*0x9e3779b9u;h^=h>>16;h*=0x85ebca6bu;h^=h>>13;h*=0xc2b2ae35u;h^=h>>16;seed=h?h:1;}
 int stutterLength()const{
  const auto&s=stutterTypes[current];
  if(s.beats<=0)return std::max(1,int(freeLength.value()));
  return std::max(1,int(s.beats*spb)/subdivide);
 }
 float captured(int i,int c)const{
  i=std::clamp(i,0,captureLength-1);
  if(i<copied)return stutter[size_t(i)*2+c];
  int p=captureStart+i;if(p>=RecordSize)p-=RecordSize;return record[size_t(p)*2+c];
 }
 // Stutter::GetStutterSampleWithWraparoundBlend (blendPos is clamped to the start, as there).
 float wrapped(int pos,int c,int L)const{
  if(pos>L-BlendWrap){float a=std::clamp(float(L-pos)/BlendWrap,0.f,1.f);return captured(pos,c)*a+captured(std::max(0,pos-L),c)*(1-a);}
  return captured(pos,c);
 }
 void capture(){
  const auto&s=stutterTypes[current];
  int n=s.beats>0?int(s.beats*spb)/subdivide:int(freeSeconds*44100);
  captureLength=std::clamp(n,1,CaptureMax);copied=0;
  captureStart=write-captureLength;if(captureStart<0)captureStart+=RecordSize;
 }
 void doStutter(int type){
  if(stuttering){   // blend from the prior stutter to this new one
   int L=stutterLength();
   for(int c=0;c<2;c++)for(int i=0;i<JumpBlend;i++)jump[c][i]=wrapped((int(stutterPos)%L+i)%L,c,L);
   jumpAt=0;
  }
  current=type;capture();stutterPos=0;stuttering=true;blend.startTo(1,StartBlend);
  const auto&s=stutterTypes[current];
  if(s.blendMs==0)speed.set(s.speedStart);else speed.start(s.speedStart,s.speedEnd,std::lround(s.blendMs*44.1));
  if(s.beats<=0){freeLength.set(freeSeconds*44100);speed.startTo(freeSpeed,882);}
 }
 void stopStutter(){if(stuttering){blend.startTo(0,StartBlend);stuttering=false;}}
 void onStep(){
  double start=step*nativeDivisionBeats(every),bar=std::fmod(start,4.0);
  float chance=nativeUnit(seed);const auto&p=pools[pool];int type=p.types[nativeRandom(seed)%uint32_t(p.count)];
  if(skipDownbeat&&(bar<1e-6||bar>4-1e-6))return;   // don't auto-stutter downbeat
  if(chance<probability){doStutter(type);hold=std::max(1L,std::lround(nativeDivisionBeats(length)*spb));}
 }
public:
 BeatRepeatFx():record(size_t(RecordSize)*2,0.f),stutter(size_t(CaptureMax)*2,0.f){blend.set(0);speed.set(1);reseed();}
 void configure(const uint8_t*v,float bpm)override{
  spb=nativeSamplesPerBeat(bpm);every=nativeDivision(v[0]);probability=v[1]*.01f;length=nativeDivision(v[2]);
  pool=nativeChoice(v[3],8);fade=v[4]*.01f;subdivide=nativeChoice(v[5],8)+1;skipDownbeat=v[6]>=50;
  freeSeconds=.005f*std::pow(50.f,v[7]*.01f);freeSpeed=2*v[8]*.01f;
  uint32_t base=v[9];if(base!=seedBase){seedBase=base;reseed();}
  static const int bars[4]={0,1,2,4};loopBars=bars[nativeChoice(v[10],4)];
 }
 void clear()override{
  std::fill(record.begin(),record.end(),0.f);std::fill(stutter.begin(),stutter.end(),0.f);
  write=0;copied=0;captureLength=1;stuttering=false;blend.set(0);speed.set(1);stutterPos=0;jumpAt=JumpBlend;hold=0;
  beat=0;step=-1;loopIndex=0;reseed();
 }
 void process(float*io,int frames)override{
  auto e=nativeTransportEpoch();if(e!=epoch){epoch=e;beat=0;step=-1;loopIndex=0;hold=0;reseed();stopStutter();}   // downbeat
  // Free stutter: Bespoke restarts its 20 ms length and speed ramps every buffer.
  if(stutterTypes[current].beats<=0&&(blend.to>0||blend.value()>0)){freeLength.startTo(freeSeconds*44100,882);speed.startTo(freeSpeed,882);}
  double everyBeats=nativeDivisionBeats(every);
  for(int i=0;i<frames;i++){
   float in[2]={io[i*2],io[i*2+1]};
   record[size_t(write)*2]=in[0];record[size_t(write)*2+1]=in[1];write=write+1==RecordSize?0:write+1;
   for(int k=0;k<CopyRate&&copied<captureLength;k++,copied++){
    int p=captureStart+copied;if(p>=RecordSize)p-=RecordSize;
    stutter[size_t(copied)*2]=record[size_t(p)*2];stutter[size_t(copied)*2+1]=record[size_t(p)*2+1];
   }
   // The trigger grid, from the transport downbeat.
   if(loopBars){long l=long(std::floor(beat/(4.0*loopBars)));if(l!=loopIndex){loopIndex=l;reseed();}}
   long s=long(std::floor(beat/everyBeats+1e-9));if(s!=step){step=s;onStep();}
   beat+=1/spb;
   if(hold>0&&--hold==0)stopStutter();
   if(blend.to>0||blend.value()>0){
    int L=stutterLength();
    float offset=stutterPos;if(offset>L)offset=std::fmod(offset,float(L));
    int pos=int(offset),next=int(offset+1)%L;float a=offset-pos;
    float f=1;if(fade>0){float o=offset/L;f-=fade*o*o;}
    float b=blend.value();
    for(int c=0;c<2;c++){
     float out=(1-a)*wrapped(pos,c,L)+a*wrapped(next,c,L);
     float y=out*b*f+in[c]*(1-b);
     if(jumpAt<JumpBlend){float j=float(jumpAt)/JumpBlend;y=j*y+(1-j)*jump[c][jumpAt];}
     io[i*2+c]=nativeClean(y);
    }
    if(jumpAt<JumpBlend)jumpAt++;
    stutterPos+=speed.value();
    if(stutterPos>L)stutterPos=std::fmod(stutterPos,float(L));
    if(stutterPos<0)stutterPos=std::fmod(stutterPos,float(L))+L;
    speed.tick();blend.tick();freeLength.tick();
   }
  }
 }
};
}
const NativeInfo beatRepeatInfo{FxType::BeatRepeat,"BEAT REPEAT","REPEAT","BR",6,1,1,100,int(std::size(params)),params};
std::unique_ptr<NativeFx> makeBeatRepeat(){return std::make_unique<BeatRepeatFx>();}
}
