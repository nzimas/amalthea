// SPDX-License-Identifier: GPL-3.0-only
// Engine adapters for the FLANGER and PHASER inserts (stereo in, fully wet out; the
// insert's DRY/WET MIX blends it with the dry signal).
#include "modulation_inserts.hpp"
#include <algorithm>
#include <cstring>
#include <memory>
namespace amalthea {
namespace {
float clean(float x){return std::isfinite(x)?std::clamp(x,-4.f,4.f):0;}
class Modulation final:public nornios::Engine{
public:
 explicit Modulation(FxType t):type(t){if(t==FxType::Flanger)flanger=std::make_unique<surge_flanger::Flanger>(44100);else phaser.prepare(44100);}
 const char*name()const override{return fxName(type);}
 const std::vector<nornios::CommandDef>&commands()const override{static const std::vector<nornios::CommandDef> c;return c;}
 const std::vector<nornios::PollDef>&polls()const override{static const std::vector<nornios::PollDef> c;return c;}
 bool pollValue(int,float&)override{return false;}
 void prepare(double,int)override{clear();}
 void clear(){if(flanger)flanger->initialize();else phaser.clear();}
 void command(const nornios::EngineCommand&c)override{if(c.id==4)clear();}
 void configure(const InsertSettings&s){if(flanger)fp=flangerParams(s);else pp=phaserParams(s);}
 void render(const float*L,const float*R,float*outL,float*outR,int n)override{
  if(flanger){std::memcpy(outL,L,n*sizeof(float));std::memcpy(outR,R,n*sizeof(float));flanger->process(outL,outR,n,fp);}
  else phaser.process(L,R,outL,outR,n,pp);
  for(int i=0;i<n;i++){outL[i]=clean(outL[i]);outR[i]=clean(outR[i]);}
 }
private:
 FxType type;
 std::unique_ptr<surge_flanger::Flanger> flanger;surge_flanger::Params fp;
 chowphaser::StereoPhaser phaser;chowphaser::Params pp;
};
}
std::unique_ptr<nornios::Engine> makeModulationInsert(FxType t){auto e=std::make_unique<Modulation>(t);e->configure(InsertSettings{});return e;}
void configureModulationInsert(nornios::Engine&e,FxType,const InsertSettings&s){static_cast<Modulation&>(e).configure(s);}
}
