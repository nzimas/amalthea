// FLANGER (Surge XT) and PHASER (ChowPhaser): audible sweeps, bounded output, decay to silence.
#include "ported_inserts.hpp"
#include "modulation_inserts.hpp"
#include "fx_rack.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace amalthea;
static void check(bool c,const char*s){if(!c){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
// A steady tone through the effect at its default 100% mix: the moving
// notches must make its level swing, window to window.
static double swingDb(FxType type,const InsertSettings&s,float hz){
  PortedProcessor p(type);p.configure(s,120);std::vector<double> windows;double acc=0;int n=0;
  for(int block=0;block<44100*6/32;block++){float d[64];for(int i=0;i<32;i++){float x=.3f*std::sin(6.2831853f*hz*(block*32+i)/44100);d[2*i]=d[2*i+1]=x;}
    p.process(d,32,1);for(int i=0;i<64;i++){check(std::isfinite(d[i])&&std::abs(d[i])<=4,"bounded");acc+=d[i]*d[i];}
    if(++n==69){if(block>44100/32)windows.push_back(acc);acc=0;n=0;}}   // 50 ms windows after the first second
  auto [lo,hi]=std::minmax_element(windows.begin(),windows.end());return 10*std::log10(*hi/std::max(*lo,1e-12));
}
static float tailAfterSilence(FxType type,const InsertSettings&s){
  PortedProcessor p(type);p.configure(s,120);uint32_t r=1;float peak=0;
  for(int block=0;block<44100*3/32;block++){float d[64];for(int i=0;i<64;i++){r=r*1664525u+1013904223u;d[i]=block<44100/32?((r>>9)*(1.f/4194304.f)-1)*.5f:0;}
    p.process(d,32,1);if(block>44100*2.5/32)for(float v:d)peak=std::max(peak,std::abs(v));}
  return peak;
}
int main(){
  InsertSettings s;
  for(auto type:{FxType::Flanger,FxType::Phaser}){
    double sw=swingDb(type,s,1000);printf("%s default sweep at 1 kHz: %.1f dB\n",fxName(type),sw);
    check(sw>6,"default settings sweep audibly (over 6 dB level swing on a steady tone)");
    float tail=tailAfterSilence(type,s);printf("%s residue 1.5 s after the input stops: %.2e\n",fxName(type),tail);
    check(tail<1e-3f,"the effect decays to silence after the input stops");check(tail==0||tail>1e-30f,"the tail reaches true zero, never denormal residue");
    // Random profiles stay bounded and keep moving.
    uint32_t seed=0x1234;int moving=0;
    for(int k=0;k<12;k++){InsertSettings r;Effect e{type,0};randomizeFxParameters(e,r,seed);moving+=std::max({swingDb(type,r,300),swingDb(type,r,1000),swingDb(type,r,3000)})>4;}
    printf("%s random profiles with an audible sweep: %d of 12\n",fxName(type),moving);check(moving>=8,"most random profiles sweep audibly");
  }
  // Extreme settings: all maximum feedback and depth stay finite.
  InsertSettings x;x.flanger={0,0,100,100,100,0,100,100,0,100,100};x.phaser={100,100,100,100,100,100,100,100,100,100,100};
  for(auto type:{FxType::Flanger,FxType::Phaser}){PortedProcessor p(type);p.configure(x,120);uint32_t r=7;
    for(int block=0;block<4000;block++){float d[64];for(int i=0;i<64;i++){r=r*1664525u+1013904223u;d[i]=((r>>9)*(1.f/4194304.f)-1);}p.process(d,32,1);for(float v:d)check(std::isfinite(v)&&std::abs(v)<=4,"extreme settings stay finite and bounded");}}
  // Cost: seconds of CPU per second of stereo audio (default settings, one instance).
  for(auto type:{FxType::Flanger,FxType::Phaser}){PortedProcessor p(type);p.configure(InsertSettings{},120);float d[64]{};auto t0=std::chrono::steady_clock::now();
    for(int block=0;block<44100*10/32;block++){for(int i=0;i<64;i++)d[i]=.1f*std::sin(i*.3f+block);p.process(d,32,1);}
    double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();printf("%s CPU: %.2f%% of one core\n",fxName(type),s*10);}
  puts("PASS: flanger and phaser sweep audibly, stay bounded and decay to silence");
}
