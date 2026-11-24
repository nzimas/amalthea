// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <array>
#include <cstdint>
#include "../third_party/nornios_ports/PortsMetadata.hpp"
#include "native_fx.hpp"
namespace amalthea {
// Track/slot-local profiles; step FX value is the dry/wet lock.
struct BizSlot {
  uint8_t module=0; // 0 empty,20 shaper,40 crush,60 slew,80 filter,100 scope/pass-through
  std::array<uint8_t,36> value{{0,0,100,0,46,50,9,0, 0,100,50,50,100,0,0,0,0,
                             0,100,50,50,100,100,0,0, 0,100,50,50,0,0,100,0,0,0,0}};
};
struct InsertSettings {
  std::array<uint8_t,128> qdelay=nornios::defaultQDelayValues();
  // type,insanity,tempo sync,level,wet compensation,reserved
  std::array<uint8_t,6> matrix{{25,0,0,50,100,0}};
  // parent,delay,pan,feedback,gain,LP,HP,pitch,diffusion,drive,reverse,mod rate,delay mod,pan mod
  std::array<std::array<uint8_t,14>,8> nodes=[] {std::array<std::array<uint8_t,14>,8>a{};for(auto &n:a)n={0,25,50,35,50,100,0,50,20,20,0,10,10,50};a[0][0]=50;a[1][0]=67;a[0][2]=20;a[1][2]=80;return a;}();
  // tube,mechanical,dist on,cutoff,resonance,filter on,comp,phat,comp on,master,trim
  std::array<uint8_t,11> crush{{60,45,100,90,20,100,25,0,100,50,65}};
  // output,solo, then type/frequency/Q/gain/active for each band
  std::array<uint8_t,32> frequalizer{{50,0,9,0,42,50,100,27,37,42,50,100,73,47,42,50,100,73,57,42,50,100,82,80,42,50,100,100,93,42,50,100}};

  // Rings: tuning,structure,brightness,damping,position,modes,level.
  std::array<uint8_t,7> rings{{40,35,65,65,45,100,50}};
  // Fracture2: drive,fracture stages,threshold,level.
  std::array<uint8_t,4> fracture{{35,25,60,60}};
  // CrazyIvan: murder,mask,AND,XOR,power,freq,diff,vibrato,vib depth,feedback,
  // flanger rate,min delay,max delay,distortion,damping,flanger feedback,limiter,attack,release,level.
  std::array<uint8_t,20> ivan{{0,25,0,100,0,50,35,40,35,35,25,0,3,30,35,35,70,5,40,70}};
  // Bitmurderer: 15 bit switches (high to low),AND,XOR,sigmoid,level.
  std::array<uint8_t,19> murder{{0,0,100,0,100,0,0,100,0,0,0,0,0,0,0,0,100,0,50}};
  // Lo-Fi: drive,tone,wow,noise,level.
  std::array<uint8_t,5> lofi{{60,45,45,15,70}};

  // mode, cutoff, resonance, drive, level; the step FX value controls dry/wet.
  std::array<uint8_t,5> filter{{0,70,30,0,50}};
  // Original CloudSeed master profile: decay,damping,size,predelay,width,motion,diffusion.
  std::array<uint8_t,7> space{{65,35,65,33,100,35,85}};
  // position,size,density,texture,spread,feedback,reverb,freeze,pitch
  std::array<uint8_t,9> clouds{{50,50,33,50,50,0,0,0,50}};
  // drive,room,bass,mid,treble,presence,level
  std::array<uint8_t,7> amp{{50,30,55,45,45,30,50}};
  // wave,rate,noise,width,invert,level
  std::array<uint8_t,6> rotor{{0,60,0,50,0,50}};
  std::array<uint8_t,2> bizLevel{{50,50}};
  // Surge XT flanger: mode,wave,rate,depth,voices,base pitch,spacing,feedback,damping,width,comb mix.
  std::array<uint8_t,11> flanger{{0,0,31,100,100,47,0,60,10,50,90}};
  // ChowPhaser: stages,feedback,modulation,LFO rate,LFO depth,rate x10,skew,drive 1-3,stereo.
  std::array<uint8_t,11> phaser{{41,63,50,25,84,0,50,18,18,18,50}};
  std::array<BizSlot,8> biz = [] {std::array<BizSlot,8>a{};a[0].module=20;a[0].value[8]=30;return a;}();
  // AMAL23: one profile per native effect (native_fx.hpp).
  NativeProfiles native=nativeDefaults();
};
// Legacy base payload; v10 appends Space profiles after the v9 data.
constexpr int InsertSettingsBytes=9+7+6+2+8*37;
}
