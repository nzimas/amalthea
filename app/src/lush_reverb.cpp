// CloudSeed DSP Copyright (c) 2024 Ghost Note Engineering Ltd, MIT.
// Native profile/adapter by Amalthea; see docs/REVERB-RESEARCH.md.
#include "lush_reverb.hpp"
#include "core.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif
#define BUFFER_SIZE 32
#include "../third_party/cloudseed/DSP/ReverbController.h"
namespace amalthea {
struct LushReverb::Impl {
  Cloudseed::ReverbController core{44100};
  std::array<double,Cloudseed::Parameter::COUNT> params;
  float input[2][32]{},output[2][32]{};
  std::mutex mutex;
  std::condition_variable ready,done;
  std::thread worker;
  bool parallel=false,job=false,exit=false;
  void finish(){if(parallel){std::unique_lock<std::mutex> lock(mutex);done.wait(lock,[this]{return !job;});}}
  void run(){
    if(!parallel){core.Process(input[0],input[1],output[0],output[1],32);return;}
    {std::lock_guard<std::mutex> lock(mutex);job=true;}ready.notify_one();
  }
  int cursor=0,quiet=0;
  float width=1;bool active=false;
  void set(int p,double v){if(params[p]!=v){core.SetParameter(p,v);params[p]=v;}}
  Impl(bool async):parallel(async){
    params.fill(-1);
    using namespace Cloudseed::Parameter;
    // Every parameter is initialized explicitly; no plugin, preset or GUI runtime.
    for(int i=0;i<COUNT;i++)set(i,0);
    set(Interpolation,1);set(LowCutEnabled,1);set(HighCutEnabled,1);
    set(LowCut,.40);set(HighCut,.87);set(InputMix,.35);
    set(DryOut,0);set(EarlyOut,0);set(LateOut,.94);
    set(TapEnabled,0);set(TapLength,.1);
    set(EarlyDiffuseEnabled,1);set(EarlyDiffuseCount,.26);
    set(EarlyDiffuseDelay,.24);set(EarlyDiffuseFeedback,.68);
    set(EarlyDiffuseModRate,.13);set(EarlyDiffuseModAmount,.08);
    set(LateMode,1);set(LateLineCount,1);
    set(LateDiffuseEnabled,1);set(LateDiffuseFeedback,.68);
    set(LateLineModRate,.18);set(LateDiffuseModRate,.14);
    set(EqHighShelfEnabled,1);set(EqLowShelfEnabled,0);
    set(EqHighFreq,.58);set(EqLowFreq,.25);set(EqLowGain,.91);
    set(EqLowpassEnabled,1);set(EqCutoff,.85);
    set(SeedDelay,.2181);set(SeedDiffusion,.185);set(SeedPostDiffusion,.3653);
    if(parallel)worker=std::thread([this]{
#ifdef __linux__
      sched_param param{};param.sched_priority=12;pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);
#endif
#if defined(__aarch64__)
      uint64_t fpcr;asm volatile("mrs %0, fpcr":"=r"(fpcr));fpcr|=uint64_t(1)<<24;asm volatile("msr fpcr, %0"::"r"(fpcr));
#endif
      std::unique_lock<std::mutex> lock(mutex);
      while(true){ready.wait(lock,[this]{return job||exit;});if(exit)return;lock.unlock();core.Process(input[0],input[1],output[0],output[1],32);lock.lock();job=false;done.notify_one();}
    });
  }
  ~Impl(){if(parallel){finish();{std::lock_guard<std::mutex> lock(mutex);exit=true;}ready.notify_one();worker.join();}}
};
LushReverb::LushReverb(bool parallel):impl(std::make_unique<Impl>(parallel)){configure(MasterSettings{});clear();}
LushReverb::~LushReverb()=default;
void LushReverb::configure(const MasterSettings&s){
  using namespace Cloudseed::Parameter;
  auto &a=*impl;a.finish();
  const double decay=s.value(9,1)/100.,damp=s.value(9,2)/100.,size=s.value(9,3)/100.;
  // Musical RT60 .5..45s, native line sizes 60..420ms. Invert CloudSeed's laws.
  double rt=.5*std::pow(90.,decay);
  auto inverse=[](double y,double decades){return std::log10(1+y*(std::pow(10.,decades)-1))/decades;};
  a.set(LateLineDecay,inverse((rt-.05)/59.95,3));
  a.set(LateLineSize,inverse((60+360*size-20)/980.,2));
  a.set(LateDiffuseDelay,(18+42*size-10)/90.);
  a.set(EqHighGain,.99-.18*damp);
  a.set(EqCutoff,.96-.40*damp);
  a.set(TapPredelay,inverse(.4*std::pow(s.value(9,4)/100.,2),1));
  // Width is applied to the wet return, without reseeding or disturbing the tail.
  a.set(EqCrossSeed,0);a.width=s.value(9,5)/100.f;
  a.set(LateLineModAmount,s.value(9,6)*.004);
  a.set(LateDiffuseModAmount,s.value(9,6)*.0024);
  a.set(LateDiffuseCount,.13+.74*s.value(9,7)/100.);
}
void LushReverb::setLineLimit(int lines){auto&a=*impl;a.finish();a.set(Cloudseed::Parameter::LateLineCount,(std::clamp(lines,1,12)-.5)/11.999);}
void LushReverb::clear(){auto&a=*impl;a.finish();a.core.ClearBuffers();std::memset(a.input,0,sizeof a.input);std::memset(a.output,0,sizeof a.output);a.cursor=0;a.active=false;a.quiet=0;}
void LushReverb::process(float l,float r,float &ol,float &orr){
  auto&a=*impl;
  if(std::abs(l)+std::abs(r)>1e-12f){a.active=true;a.quiet=0;}
  if(!a.active){ol=orr=0;return;}
  int i=a.cursor;if(i==0)a.finish();float mid=(a.output[0][i]+a.output[1][i])*.5f;
  float side=(a.output[0][i]-a.output[1][i])*.5f*a.width;ol=mid+side;orr=mid-side;
  a.input[0][i]=l;a.input[1][i]=r;
  if(std::abs(ol)+std::abs(orr)<1e-10f){if(++a.quiet>88200)a.active=false;}else a.quiet=0;
  if(++a.cursor==32){a.run();a.cursor=0;}
}
size_t LushReverb::memoryBytes()const{return sizeof(Impl);}
}
