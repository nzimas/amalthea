// SPDX-License-Identifier: GPL-3.0-only
// Vascular DSP by Nuno Zimas, adapted directly without the iOS/Csound host.
#include "native_engines.hpp"
#include "eng.h"
#include "../third_party/vascular/AnimaCore.h"
#include "../third_party/vascular/AnimaDesign.h"
#include "../third_party/vascular/BayareaCore.h"
#include "../third_party/vascular/CaligulaDesign.h"
#include "../third_party/vascular/ResurgentCore.h"
#include "../third_party/noisferatu/NoisferatuVoice.h"
#include <array>
#include <cstring>
#include <type_traits>
#include <sys/mman.h>
#include <stdexcept>
namespace va=vascular::anima;
namespace vb=vascular::bayarea;
namespace vc=vascular::caligula;
namespace vr=vascular::resurgent;
namespace {
const char *animaNames[]={"HAZE","VOYAGE","GLASS","LILT","TOOL","OSTINATO","BELLS","GOLDEN","BREATH","GRIT","SWARM","TIDE","CHOIR","SHARDS","BOWL","DUST","MACHINE","PLUCK","REED","FOG","ORGAN","BITS","DRIFT","MURMUR","HOWL","SCRAPYARD","WIRE","ICE","EMBER","SPECTRAL","FRACTURE","RADIO"};
const char *russoloNames[]={"SPARSE GLITCH","MICROGLITCH","SPACE PULSES","JUMP GLITCH","WANDERING WINDOW","WINDOW SPRAY","NOISE SAW","HARMONIC DRONE","QUAD BITBEND","LATCHED NOISE","DUST","FM NOISE","NOISE GATES","SAW CLICKS","NOISE NOR","DUST BURST","HIGH NOISE","VINYL","BIT CHAOS","SPARSE BITS","DUAL BITS","BIT FREEZE","BIT PING","BIT MIRROR","TRIPLE BITS","BIT SWEEP","TRIPLE B","RANDOM TRI","HARMONIC TRI","FAST TRI","PHRYGIAN","RING MOD","NOISE SQUARE","MAJOR TRI","BERNOULLI","PENTATONIC","CASCADE","NOR SQUARE","TRI OR SAW","TRI NOR","TRI XOR","XNOR SQUARE","NAND SQUARE","TWO SAWS","OR SQUARE"};
const char *bayNames[]={"BUCHLA STRIKE","FOLDED BASS","METAL PLUCK","GLASS GATE","FM WIRE","BENT REED","DARK BELL","ELECTRIC WOOD","ACID GATE","RUSTED STRING","SINGING BOWL","FORMANT","DRY CIRCUIT","INDUSTRIAL LPG","SHATTER","SPARK","DENSE FM","WARM PULSE","CINEMA","SLOW BLOOM","HOLLOW","LOW GRINDER","BRIGHT SHARDS","AFTERIMAGE"};
const char *resNames[]={"OBSIDIAN PAD","RAZOR LEAD","IRON BASS","WIRE PLUCK","HOLLOW KEYS","ALLOY BELL","DUST TEXTURE","METAL DRUM","SYNTHWAVE","FILTER SWEEP","COMB ORGAN","FM GLASS","STRING SCULPTURE","WINDOW GRAIN","ALIAS CHOIR","TWIST MACHINE","DIODE BASS","FORMANT SIGNAL","PHASE RUIN","FEEDBACK REED","NEON BRASS","DARK SCORE","RING STRUCTURE","NOISE CHAMBER","DIGITAL WOUND","AUDIO RATE","HARMONIC STACK","ELECTRO HIT","BROKEN RADIO","FROZEN METAL","TWIN SCENE","CINEMA DRONE"};
const char *calNames[]={"ETHER","GHOST","HAUNT","TEXTURE","SWARM","WASTELAND","INFERNO","STORM","DARK CHAMBER","CERAMIC","COPPER","SUBTERRANEAN","NERVE","PLASMA","TURBINE","ARC","SLOW RUIN","WRAITH","METAL RAIN","FEEDBACK CLOUD","SCORCHED","QUAKE","VOLTAGE","ORBIT"};
int familyCount(int e){return e==6?32:e==7?45:e==8?24:e==9?32:24;}
// Family draw weights: 0 retires a family that does not fit the instrument's
// aesthetic, 2 favours the darker / heavier ones, 1 keeps the rest.
const uint8_t animaWeight[32]={2,0,1,0,2,1,1,0,1,2,2,1,1,2,1,2,2,1,1,2,0,1,2,2,2,2,2,1,2,2,2,2};
const uint8_t russoloWeight[45]={1,1,1,1,1,1,2,2,1,2,1,2,2,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,2,2,1,0,1,0,1,1,1,1,1,1,1,1,1};
const uint8_t bayWeight[24]={1,2,1,1,2,1,2,0,1,2,1,0,1,2,2,1,2,0,0,1,1,2,1,1};
const uint8_t resWeight[32]={2,1,2,1,0,1,2,2,2,1,1,1,1,1,2,1,2,1,2,1,0,2,2,2,2,1,1,1,2,2,1,2};
int familyOf(int e,uint32_t seed){
 if(e==10)return seed%familyCount(e);
 const uint8_t *w=e==6?animaWeight:e==7?russoloWeight:e==8?bayWeight:resWeight;int n=familyCount(e),total=0;
 for(int i=0;i<n;i++)total+=w[i];
 uint32_t h=seed*0x9e3779b1u;h^=h>>16;int x=int(h%uint32_t(total));
 for(int i=0;i<n;i++){if(x<w[i])return i;x-=w[i];}
 return 0;
}
struct Cooked {
 uint32_t seed=0;int family=0;float sliders[8]{};
 alignas(16) unsigned char recipe[48000]{};
 template<class T>T &as(){return *reinterpret_cast<T*>(recipe);}
 template<class T>const T &as()const{return *reinterpret_cast<const T*>(recipe);}
};
static_assert(sizeof(vr::Patch)<48000&&sizeof(Cooked)<=ENG_CK_MAX,"native recipe storage");
va::Design animaRecipe(int f,uint32_t seed,float *sliders){
 static const int base[]={0,1,2,3,4,5,6,7,0,4,5,0,1,2,6,0,5,3,1,0,5,4,1,0,7,4,7,2,4,1,2,5};
 auto d=va::design(base[f],seed);va::route(d,seed^0x517cc1u);va::Random r(seed);
 for(int i=0;i<8;i++)sliders[i]=r.in(.25f,.9f);
 sliders[0]=r.in(.35f,.85f);sliders[6]=r.in(.3f,.75f);sliders[7]=r.in(.4f,.95f);
 if(f>=8){
  d.fingers=2+r.below(5);d.motion=va::MotionStyle(r.below(6));d.travel=r.in(.08f,.8f);
  switch(f){
   case 8:case 11:case 19:case 23:d.noise=3;d.levelNoise=va::M(va::Size,.03f,.5f);d.noiseCut=va::M(va::X,180.f,5000.f);d.att=r.in(50.f,400.f);break;
   case 9:case 16:case 21:case 25:case 28:case 30:d.down=true;d.downRate=va::M(va::Speed,1800.f,22000.f);d.shaper=true;d.shaperCurve=1;d.shaperDrive=va::M(va::Size,1.5f,6.f);d.gran=true;d.levelGran=va::M(va::Y,.08f,.6f);break;
   case 10:case 13:case 27:case 29:d.gran=true;d.levelGran=va::M(va::Size,.2f,.8f);d.granSpread=r.in(.1f,.7f);d.granRate=va::M(va::Speed,12.f,100.f);d.granLen=va::M(va::Y,8.f,160.f);break;
   case 12:case 18:case 20:case 26:d.multi=true;d.multiCount=3+r.below(3);d.levelMulti=va::M(va::Size,.1f,.6f);d.filter=2;d.cutoff=va::M(va::X,400.f,4000.f);d.q=va::M(va::Y,.7f,5.f);break;
   default:d.fm=true;d.fmRatio=r.in(.7f,7.f);d.fmIndex=va::M(va::Speed,.3f,7.f);d.levelFM=va::M(va::Size,.2f,.7f);break;
  }
  if(f==14||f==17||f==22||f==24){d.resonator=true;d.resFeedback=va::M(va::Y,.65f,.94f);d.resMix=va::M(va::Size,.15f,.65f);}
  if(f==17){d.att=2;d.dec=250;d.sus=.1f;d.rel=350;}
  if(f==31){d.noise=4;d.ampMod=true;d.amRate=va::M(va::Speed,3.f,70.f);d.amDepth=va::M(va::Y,.3f,.95f);}
 }
 return va::applySliders(d,sliders);
}
vr::Patch resurgentRecipe(int f,uint32_t seed){
 vr::Patch p;va::Random r(seed);p.g[vr::G_volume]=-9;p.g[vr::G_scenemode]=f>=30?2:0;
 const int typeBank[]={0,1,5,6,7,8,9,10,11,2,3,4};
 for(int side=0;side<2;side++){
  auto &s=p.scene[side];auto &v=s.p;
  v[vr::P_volume]=.85f;v[vr::P_level_o1]=.75f;v[vr::P_level_o2]=r.in(.15f,.55f);v[vr::P_level_o3]=r.in(.05f,.35f);
  int type=typeBank[(f+side*3)%12];
  for(int o=0;o<3;o++){
   int k=vr::P_osc1_type+o*(vr::P_osc2_type-vr::P_osc1_type);v[k]=o?typeBank[(f+o+side)%12]:type;
   v[k+1]=0;v[k+2]=o==1?(f%3==0?7:0):o==2?12:0;
   for(int j=0;j<7;j++)v[k+5+j]=r.in(.1f,.85f);
  }
  v[vr::P_filter1_type]=1+(f%7);v[vr::P_filter1_cutoff]=r.in(-16.f,24.f);v[vr::P_filter1_resonance]=r.in(.15f,.65f);v[vr::P_filter1_keytrack]=.65f;v[vr::P_filter1_envmod]=r.in(8.f,36.f);
  v[vr::P_filter2_type]=f%4==0?6:0;v[vr::P_filter2_cutoff]=r.in(-6.f,30.f);
  v[vr::P_env1_attack]=(f%8==0||f>=29)?r.in(-3.f,-.5f):r.in(-10.f,-5.f);
  v[vr::P_env1_decay]=r.in(-3.f,0.f);v[vr::P_env1_sustain]=f%4==3?r.in(.1f,.35f):r.in(.45f,.9f);v[vr::P_env1_release]=r.in(-3.f,.2f);
  v[vr::P_env2_attack]=-8;v[vr::P_env2_decay]=r.in(-3.f,1.f);v[vr::P_env2_sustain]=.15f;
  v[vr::P_lfo1_shape]=f%7;v[vr::P_lfo1_rate]=r.in(-3.f,2.f);v[vr::P_drift]=r.in(.02f,.25f);
  s.routes[s.routeCount++]={vr::S_lfo1,vr::P_filter1_cutoff,r.in(3.f,12.f)};
  s.routes[s.routeCount++]={vr::S_feg,vr::P_osc1_param1,r.in(.1f,.4f)};
  if(f%3==0){v[vr::P_ws_type]=1+f%8;v[vr::P_ws_drive]=r.in(4.f,16.f);}
  if(f==11||f==19||f==22||f==25){v[vr::P_fm_switch]=1+f%3;v[vr::P_fm_depth]=r.in(-8.f,1.f);v[vr::P_level_ring12]=.2f;}
 }
 // A restrained native phaser on selected families, before tracker inserts.
 if(f%4==2){p.g[vr::G_fx1_type]=3;p.g[vr::G_fx1_p0]=-.2f;p.g[vr::G_fx1_p1]=.4f;p.g[vr::G_fx1_p3]=-1.5f;p.g[vr::G_fx1_p4]=.65f;p.g[vr::G_fx1_p6]=.25f;p.g[vr::G_fx1_p8]=4;p.g[vr::G_fx1_p9]=.4f;}
 return p;
}
template<int E>void cook(void *out,const int32_t *val){
 auto &c=*new(out)Cooked();c.seed=uint32_t(val[0]);c.family=familyOf(E,c.seed);va::Random r(c.seed);
 if constexpr(E==6)new(c.recipe)va::Design(animaRecipe(c.family,c.seed,c.sliders));
 if constexpr(E==8){auto p=vb::Params();p.drawHidden(c.seed);double sliders[8];for(auto &x:sliders)x=r.in(.15f,.9f);sliders[4]=r.in(.4f,.85f);sliders[5]=r.in(.15f,.65f);sliders[6]=r.in(.03f,.32f);sliders[7]=r.in(.25f,.8f);
  int f=c.family;if(f%6==0){sliders[1]=r.in(.6f,1.f);sliders[2]=r.in(.5f,.95f);}if(f%6==1){sliders[0]=r.in(.1f,.5f);sliders[5]=r.in(.5f,.85f);}if(f%6==2){sliders[3]=r.in(.5f,.95f);sliders[5]=r.in(.1f,.3f);}if(f>=18){p.attack=r.in(.06f,.4f);p.asr=true;}
  sliders[0]=std::clamp(sliders[0]*.5+(f%4)*.15,0.,1.);sliders[1]=std::clamp(sliders[1]*.5+(f/4%3)*.2,0.,1.);sliders[2]*=f%2?.9:.35;sliders[3]*=f%3==0?1.:.35;
  p.setSliders(sliders);new(c.recipe)vb::Params(p);}
 if constexpr(E==9)new(c.recipe)vr::Patch(resurgentRecipe(c.family,c.seed));
 if constexpr(E==10){for(int i=0;i<8;i++)c.sliders[i]=r.in(.3f,.8f);c.sliders[0]=.5f;int profile=c.family/8;if(profile==1){c.sliders[1]=r.in(.5f,.9f);c.sliders[2]=r.in(.6f,.95f);c.sliders[5]=r.in(.2f,.6f);}if(profile==2){c.sliders[3]=r.in(.1f,.5f);c.sliders[6]=r.in(.55f,.85f);c.sliders[7]=r.in(.55f,.9f);}}
 if constexpr(E==7){c.sliders[0]=r.in(.12f,.92f);c.sliders[1]=r.in(.12f,.92f);c.sliders[2]=r.in(.4f,.95f);c.sliders[3]=r.in(.1f,.8f);}
}
std::array<vr::Wavetable,6> tables;
void init(){
 (void)va::Tables::get();
 for(int k=0;k<6;k++)if(tables[k].frames==0){auto &t=tables[k];t.frameSize=512;t.frames=16;t.data.resize(t.frameSize*t.frames);
  for(int f=0;f<t.frames;f++)for(int i=0;i<t.frameSize;i++){float phase=float(i)/t.frameSize,y=0;for(int h=1;h<=48;h++){float tilt=.6f+float(f)/15*2;float weight=std::pow(float(h),-tilt);if(k==1&&h%2==0)weight=0;if(k==2)weight*=std::sin(h*1.7f+f*.2f);if(k==3)weight*=std::exp(-std::pow((h-3-f)*.15f,2.f));if(k==4)weight*=std::cos(h*h*.19f);if(k==5)weight*=h%3==0?.8f:.1f;y+=weight*std::sin(va::kTwoPi*h*phase);}t.data[f*t.frameSize+i]=std::tanh(y);}
  t.build();
 }
}
}
namespace amalthea {
struct NativeVoice {
 static constexpr size_t ArenaBytes=32*1024*1024;
 struct Reservation {
  unsigned char *p;
  Reservation(){int flags=MAP_PRIVATE|MAP_ANON;
#ifdef MAP_NORESERVE
   flags|=MAP_NORESERVE;
#endif
   auto result=mmap(nullptr,ArenaBytes,PROT_READ|PROT_WRITE,flags,-1,0);if(result==MAP_FAILED)throw std::bad_alloc();p=static_cast<unsigned char*>(result);}
  ~Reservation(){munmap(p,ArenaBytes);}
 } memory;
 amalthea_rt::Arena arena{memory.p,ArenaBytes};
 static constexpr size_t StateBytes=std::max({sizeof(va::Engine),sizeof(vb::Voice),sizeof(vc::Player),sizeof(vr::Voice),sizeof(Noisferatu)});
 alignas(16) unsigned char storage[StateBytes];
 int engine=-1,family=0,quiet=0;bool released=false;float hz=220,velocity=1,envelope=0,dc=0,held=0;double phase=1;uint32_t plaitsRandom=1;uint64_t frames=0;
 template<class T>T &as(){return *reinterpret_cast<T*>(storage);}
 void destroy(){switch(engine){case 6:as<va::Engine>().~Engine();break;case 7:as<Noisferatu>().~Noisferatu();break;case 8:as<vb::Voice>().~Voice();break;case 9:as<vr::Voice>().~Voice();break;case 10:as<vc::Player>().~Player();break;}engine=-1;}
 ~NativeVoice(){destroy();}
};
void NativeDeleter::operator()(NativeVoice*p)const{delete p;}
NativeVoicePtr makeNativeVoice(){return NativeVoicePtr(new NativeVoice);}
void bindNativeVoice(void *data,NativeVoice*p){std::memcpy(data,&p,sizeof p);}
void retriggerNativeVoice(void *data,int note,int velocity){
 NativeVoice *p;std::memcpy(&p,data,sizeof p);auto &v=*p;v.hz=440.f*std::exp2((note-69.f)/12.f);v.velocity=velocity/127.f;v.released=false;v.quiet=0;
 if(v.engine==6){v.as<va::Engine>().setNote(v.hz);v.as<va::Engine>().autonomous(true);v.as<va::Engine>().strike();}
 if(v.engine==8)v.as<vb::Voice>().noteOn(v.hz,v.velocity);
 if(v.engine==9)v.as<vr::Voice>().noteOn(v.velocity);
 if(v.engine==10){auto &s=v.as<vc::Player>();s.transpose=v.hz/110.;s.setPlaying(true);s.strike();}
}
size_t nativeVoiceArenaUsed(const NativeVoice*p){return p->arena.used;}
}
namespace {
amalthea::NativeVoice &voice(void *data){amalthea::NativeVoice*p;std::memcpy(&p,data,sizeof p);return *p;}
struct ArenaScope {amalthea_rt::Arena *old;explicit ArenaScope(amalthea_rt::Arena *a):old(amalthea_rt::current){amalthea_rt::current=a;}~ArenaScope(){amalthea_rt::current=old;}};
void reset(void*){}
template<int E>void on(void *data,const void *patch,int32_t note,int velocity){
 auto &v=voice(data);const auto &c=*static_cast<const Cooked*>(patch);v.destroy();v.arena.used=0;ArenaScope scope(&v.arena);
 v.engine=E;v.family=c.family;v.released=false;v.envelope=0;v.frames=0;v.quiet=0;v.dc=0;v.phase=1;v.held=0;v.velocity=velocity/127.f;v.hz=440.f*std::exp2((note/65536.f-69.f)/12.f);v.plaitsRandom=c.seed?c.seed:1;vascular_stmlib::Random::Seed(v.plaitsRandom);
 if constexpr(E==6){auto &s=*new(v.storage)va::Engine();s.init(c.as<va::Design>(),c.seed^0x517cc1u,44100,v.hz);s.setMotionScale(std::exp2((c.sliders[6]-.5f)*2.6f));s.setDensity(.25f+c.sliders[7]*1.75f);s.autonomous(true);s.strike();}
 if constexpr(E==7){auto &s=*new(v.storage)Noisferatu();s.setup(c.family,c.seed,c.sliders[0],c.sliders[1]);}
 if constexpr(E==8){auto &s=*new(v.storage)vb::Voice();s.params=c.as<vb::Params>();s.prepare(44100,c.seed);s.noteOn(v.hz,v.velocity);}
 if constexpr(E==9){auto &s=*new(v.storage)vr::Voice();s.update(c.as<vr::Patch>());s.prepare(44100,c.seed);for(int i=0;i<6;i++)s.setTable(i,&tables[(i+c.family)%6]);s.noteOn(v.velocity);}
 if constexpr(E==10){auto &s=*new(v.storage)vc::Player();for(int i=0;i<8;i++)s.sliders[i]=c.sliders[i];s.transpose=v.hz/110.;s.prepare(c.family%vc::K_COUNT,c.seed,c.seed^0xca11u,44100,2,true);s.strike();}
 v.plaitsRandom=vascular_stmlib::Random::state();
}
void off(void *data,const void*,int32_t){auto &v=voice(data);v.released=true;if(v.engine==6)v.as<va::Engine>().release();if(v.engine==8)v.as<vb::Voice>().noteOff();if(v.engine==9)v.as<vr::Voice>().noteOff();if(v.engine==10)v.as<vc::Player>().setPlaying(false);}
int render(void *data,const void *patch,int32_t *left,int32_t *right){
 auto &v=voice(data);const auto &c=*static_cast<const Cooked*>(patch);ArenaScope scope(&v.arena);float l[ENG_BLK]{},r[ENG_BLK]{};
 vascular_stmlib::Random::Seed(v.plaitsRandom);
 if(v.engine==6)v.as<va::Engine>().process(l,ENG_BLK);
 if(v.engine==8)v.as<vb::Voice>().render(l,r,ENG_BLK);
 if(v.engine==9){v.as<vr::Voice>().setTempo(eng_bpm10*.1f);v.as<vr::Voice>().render(l,ENG_BLK,v.hz);}
 if(v.engine==10)v.as<vc::Player>().render(l,ENG_BLK);
 for(int i=0;i<ENG_BLK;i++){
  if(v.engine==7){v.phase+=16000.*std::clamp(v.hz/130.8128f,.125f,8.f)/44100.;while(v.phase>=1){v.held=v.as<Noisferatu>().sample(v.family)/512.f;v.phase-=1;}int bits=std::clamp(int(10-c.sliders[3]*5),5,10);int q=int(v.held*512);q=(q>>(10-bits))<<(10-bits);l[i]=q/512.f*.65f;}
  if(v.engine==7||v.engine==10){v.envelope+=((v.released?0.f:1.f)-v.envelope)*(v.released?.00012f:.004f);l[i]*=v.envelope;}
  float x=std::isfinite(l[i])?l[i]*v.velocity:0;v.dc+=(x-v.dc)*.0005f;x-=v.dc;
  float y=v.engine==8?(std::isfinite(r[i])?r[i]*v.velocity:0):x;
  x=std::clamp(x,-4.f,4.f);y=std::clamp(y,-4.f,4.f);left[i]=int32_t(x*32768);right[i]=int32_t(y*32768);
 }
 v.plaitsRandom=vascular_stmlib::Random::state();v.frames+=ENG_BLK;
 float peak=0;for(int i=0;i<ENG_BLK;i++)peak=std::max(peak,std::max(std::fabs(l[i]),std::fabs(r[i])));
 if(v.released&&peak<1e-5f)v.quiet++;else v.quiet=0;
 // Count silent blocks only after release; slow attacks and sparse algorithms stay alive.
 return !v.released||v.quiet<128;
}
#define NATIVE_ENGINE(symbol,title,kind,stereo_) extern "C" const eng_t symbol={title,"NATIVE VASCULAR / NOISE",0,stereo_,0,0,nullptr,nullptr,nullptr,nullptr,sizeof(void*),uint16_t(offsetof(Cooked,recipe)+(kind==6?sizeof(va::Design):kind==8?sizeof(vb::Params):kind==9?sizeof(vr::Patch):0)),init,cook<kind>,reset,on<kind>,off,render};
NATIVE_ENGINE(ENG_ANIMA,"ANIMA",6,0)
NATIVE_ENGINE(ENG_RUSSOLO,"RUSSOLO",7,0)
NATIVE_ENGINE(ENG_BAYAREA,"BAY AREA",8,1)
NATIVE_ENGINE(ENG_RESURGENT,"RESURGENT",9,0)
NATIVE_ENGINE(ENG_CALIGULA,"CALIGULA",10,0)
}
extern "C" const char *amalthea_native_family(int e,unsigned seed){int f=familyOf(e,seed);return e==6?animaNames[f]:e==7?russoloNames[f]:e==8?bayNames[f]:e==9?resNames[f]:calNames[f];}
