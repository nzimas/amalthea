// SPDX-License-Identifier: GPL-3.0-only
#include "native_fx.hpp"
#include "native_fx_list.hpp"
#include "core.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
namespace amalthea {
namespace {
struct Entry { const NativeInfo*info;std::unique_ptr<NativeFx>(*make)(); };
const std::array<Entry,NativeCount>&table(){
 static const std::array<Entry,NativeCount> t={{
  {&oopsInfo,makeOops},{&bufferOverrideInfo,makeBufferOverride},{&livecutInfo,makeLivecut},{&supaTriggaInfo,makeSupaTrigga},
  {&beatRepeatInfo,makeBeatRepeat},{&timeShaperInfo,makeTimeShaper},{&jumblrInfo,makeJumblr},{&scrubbyInfo,makeScrubby},
  {&glitchletsInfo,makeGlitchlets},{&skidderInfo,makeSkidder},
  {&chopprInfo,makeChoppr},
  {&gateShaperInfo,makeGateShaper},
  {&tremoSquareInfo,makeTremoSquare},
  {&beadsInfo,makeBeads},
  {&softcutInfo,makeSoftcut},
  {&transverbInfo,makeTransverb},
  {&glitchShifterInfo,makeGlitchShifter},
  {&pitchNastyInfo,makePitchNasty},
  {&amalgamInfo,makeAmalgam},
  {&geometerInfo,makeGeometer},
  {&multiBandDistortionInfo,makeMultiBandDistortion},
  {&disintegrateInfo,makeDisintegrate},
  {&bitGlitterInfo,makeBitGlitter},
  {&pafnutyInfo,makePafnuty},
  {&cyanideInfo,makeCyanide},
  {&polarizerInfo,makePolarizer},
  {&angrInfo,makeAngr},
  {&warpsInfo,makeWarps},
  {&freqShifterInfo,makeFreqShifter},
  {&brokenFftInfo,makeBrokenFft},
  {&treemonsterInfo,makeTreemonster},
  {&ringModInfo,makeRingMod},
  {&combulatorInfo,makeCombulator},
  {&resonatorInfo,makeResonator},
  {&vocoderInfo,makeVocoder},
  {&smrInfo,makeSmr},
  {&rezSynthInfo,makeRezSynth},
  {&talkBoxInfo,makeTalkBox},
  {&mdaVocoderInfo,makeMdaVocoder},
  {&rePsychoInfo,makeRePsycho},
 }};
 return t;
}
std::atomic<uint32_t> transportEpoch{0};
const char*divisionNames[NativeDivisions]={"4 BARS","2 BARS","1 BAR","1/2.","1/2","1/4.","1/2T","1/4","1/8.","1/4T","1/8","1/8T","1/16","1/16T","1/32","1/64"};
const double divisionBeats[NativeDivisions]={16,8,4,3,2,1.5,4/3.,1,.75,2/3.,.5,1/3.,.25,1/6.,.125,.0625};
}
int nativeIndex(FxType t){int k=int(t)-int(FxType::Oops);return k>=0&&k<NativeCount?k:-1;}
FxType nativeType(int k){return FxType(int(FxType::Oops)+k);}
const NativeInfo&nativeInfo(int k){return *table()[std::clamp(k,0,NativeCount-1)].info;}
const NativeInfo*nativeInfo(FxType t){int k=nativeIndex(t);return k<0?nullptr:table()[k].info;}
std::unique_ptr<NativeFx> makeNativeFx(FxType t){int k=nativeIndex(t);return k<0?nullptr:table()[k].make();}
NativeProfiles nativeDefaults(){
 NativeProfiles p{};
 for(int k=0;k<NativeCount;k++){auto&info=*table()[k].info;for(int i=0;i<info.count&&i<NativeParams;i++)p[k][i]=info.params[i].def;}
 return p;
}
const char*nativeDivisionName(int i){return divisionNames[std::clamp(i,0,NativeDivisions-1)];}
double nativeDivisionBeats(int i){return divisionBeats[std::clamp(i,0,NativeDivisions-1)];}
int nativeChoice(uint8_t v,int count){return count<2?0:std::clamp(int(std::lround(std::min<int>(v,100)*(count-1)/100.0)),0,count-1);}
int nativeDivision(uint8_t v){return nativeChoice(v,NativeDivisions);}
double nativeSamplesPerBeat(float bpm){return 44100.0*60/std::clamp(bpm,20.f,400.f);}
uint32_t nativeTransportEpoch(){return transportEpoch.load(std::memory_order_relaxed);}
void nativeTransportRestart(){transportEpoch.fetch_add(1,std::memory_order_relaxed);}
const char*nativeValueText(const NativeParam&p,int v,char*b,int size){
 v=std::clamp(v,0,100);
 switch(p.unit){
  // A percentage names its quantity ("% DUTY", "% WET"); without a suffix it is a plain amount.
  case NativeUnit::Percent:if(*p.suffix)snprintf(b,size,"%d%s",v,p.suffix);else snprintf(b,size,"%d",v);break;
  case NativeUnit::Bipolar:snprintf(b,size,"%+d",v*2-100);break;
  case NativeUnit::Choice:snprintf(b,size,"%s",p.choiceCount?p.choices[nativeChoice(v,p.choiceCount)]:"-");break;
  case NativeUnit::Division:snprintf(b,size,"%s",nativeDivisionName(nativeDivision(v)));break;
  case NativeUnit::Toggle:snprintf(b,size,"%s",v>=50?"ON":"OFF");break;
  case NativeUnit::Range:{float u=v*.01f,x=p.exponential&&p.min>0?p.min*std::pow(p.max/p.min,u):p.min+(p.max-p.min)*u;
   snprintf(b,size,std::fabs(x)>=100||x==std::round(x)?"%.0f%s":"%.2f%s",x,p.suffix);break;}
 }
 return b;
}
}
