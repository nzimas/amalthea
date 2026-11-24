// SPDX-License-Identifier: GPL-3.0-only
// FLANGER (Surge XT) and PHASER (ChowPhaser) inserts. Provenance: docs/INSERT-PORTS.md.
#pragma once
#include "core.hpp"
#include "../third_party/nornios/EngineRegistry.hpp"
#include "../third_party/surge_flanger/SurgeFlanger.h"
#include "../third_party/chowphaser/ChowPhaserCore.h"
#include <cmath>
namespace amalthea {
// Stored 0..100 settings to each core's own units. The value text uses the same maps.
inline int flangerChoice(int v,int count){return std::clamp(int(std::lround(v*(count-1)/100.f)),0,count-1);}
inline surge_flanger::Params flangerParams(const InsertSettings&s){
 auto u=[&](int i){return s.flanger[i]*.01f;};surge_flanger::Params p;
 p.mode=flangerChoice(s.flanger[0],4);p.wave=flangerChoice(s.flanger[1],6);p.rate=-7+16*u(2);p.depth=u(3);
 p.voices=1+3*u(4);p.basePitch=std::round(127*u(5));p.spacing=12*u(6);p.feedback=u(7);p.damping=u(8);
 p.widthDb=-24+48*u(9);p.mix=2*u(10)-1;return p;
}
// JUCE setSkewForCentre(12) on 1..50 stages and (4) on 0..16 Hz.
inline float phaserStages(int v){static const float skew=std::log(.5f)/std::log(11.f/49);return 1+49*std::pow(v*.01f,1/skew);}
inline chowphaser::Params phaserParams(const InsertSettings&s){
 auto u=[&](int i){return s.phaser[i]*.01f;};chowphaser::Params p;
 p.stages=phaserStages(s.phaser[0]);p.feedback=.95f*u(1);p.mod=u(2);p.lfoFreq=16*u(3)*u(3);p.lfoDepth=.95f*u(4);
 p.freqMult=s.phaser[5]>=50;p.skew=-3+6*u(6);p.d1=.1f+4.9f*u(7);p.d2=.1f+4.9f*u(8);p.d3=.1f+4.9f*u(9);p.stereo=.5f*u(10);return p;
}
std::unique_ptr<nornios::Engine> makeModulationInsert(FxType);
void configureModulationInsert(nornios::Engine&,FxType,const InsertSettings&);
}
