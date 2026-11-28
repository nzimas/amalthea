#pragma once
// PERFORMANCE mode (tracker, Y): temporary transformations of the playing pattern. Nothing
// here is ever written into the project: the audio applies it on top of the pattern, and
// leaving the mode (F1) drops it all, so the pattern plays exactly as saved.
//   X  a chain of 2 .. 4 effects (a COMBO, after Vascular) on stretches of 1 .. 3 tracks
//   B  new parameters for the chain's effects
//   A  six LFOs on the chain's parameters (sine or sample & hold)
//   Y  six LFOs on the instruments' parameters
// Every button is a toggle: on, then off (bypass), then on again with a new draw.
#include "core.hpp"
#include <array>
namespace amalthea {
constexpr int PerfTargets=3,PerfStages=4,PerfLfoCount=6;
// Brick budget for the whole chain, in LIFEBLOOD cost units (about 100 us a block each)
// times the number of tracks it plays on.
constexpr int PerfBudget=16;
struct PerfTarget { int8_t track=-1; uint8_t start=0,length=0; };   // NOTE-column rows start .. start+length-1 (wrapping)
struct PerfLfo {
  int8_t stage=-1; uint8_t field=0,page=0;
  bool hold=false;          // false: sine; true: sample & hold (a new random value each cycle)
  float rate=.5f;           // Hz
  float depth=.3f;          // of the parameter's 0 .. 100 range
  float phase=0,held=0;
};
// Y: instrument LFOs on the default instrument of 1 .. 3 tracks holding notes. VAP, PERC
// and CHIP: on their timbre parameters (those a row variation may move: never pitch, level
// or a switch), live. Recipe engines (every other): through a bank of the sound's own
// variations, one per note (their sound is set when a note starts).
constexpr int PerfVariations=8,PerfParameters=96;
struct PerfInstrument { int8_t track=-1; uint8_t slot=0,engine=0; uint32_t seed=0; std::array<int32_t,PerfParameters> base{}; };
struct PerfEngineLfo {
  int8_t target=-1;          // index into Performance::instrument
  uint8_t param=0;           // VAP / PERC / CHIP: the parameter; recipe engines: (morph)
  bool morph=false,hold=false;
  float rate=.5f,depth=.3f,phase=0,held=0;
  uint32_t variations=0;     // morph: the bank's seed
};
struct Performance {
  bool chainOn=false,paramsOn=false,fxLfoOn=false,engineLfoOn=false;
  uint32_t seed=0x51ed2701u;
  uint32_t generation=0;    // a new chain (X): the audio starts it on fresh processors
  uint8_t combo=0;int stages=0;
  std::array<Effect,PerfStages> effect{};
  std::array<InsertSettings,PerfStages> base{},drawn{};   // the chain's own parameters / B's draw
  std::array<PerfTarget,PerfTargets> target{};int targets=0;
  std::array<PerfLfo,PerfLfoCount> lfo{};
  std::array<PerfInstrument,PerfTargets> instrument{};int instruments=0;
  std::array<PerfEngineLfo,PerfLfoCount> engineLfo{};
};
const char *perfComboName(int);
// The button presses (toggles). Each returns the notice to show.
std::string perfChain(Performance&,const Project&,const DeviceSettings&);   // X
std::string perfParams(Performance&);                                       // B
std::string perfFxLfos(Performance&);                                       // A
std::string perfEngineLfos(Performance&,const Project&);                    // Y
// An instrument LFO's target, for the screen: "T3 CUTOFF", "T5 VARIATIONS".
std::string perfEngineLfoName(const Performance&,const PerfEngineLfo&);
// What the audio plays now: the chain with B's parameters and A's LFOs applied. Advances
// the LFOs by `seconds`.
struct PerfFrame {
  bool on=false;uint32_t generation=0;int stages=0;
  std::array<Effect,PerfStages> effect{};
  std::array<InsertSettings,PerfStages> settings{};
  std::array<PerfTarget,PerfTargets> target{};
  // Y: each instrument as it plays now (live: these parameters; else this variation).
  struct Instrument { int8_t track=-1; uint8_t slot=0,engine=0; uint32_t seed=0,variation=0; bool live=false; std::array<int32_t,PerfParameters> params{}; };
  std::array<Instrument,PerfTargets> instrument{};
};
PerfFrame perfFrame(Performance&,double seconds);
// The LFO's current output, -1 .. 1 (for the screen).
float perfLfoValue(const PerfLfo&);
float perfLfoValue(const PerfEngineLfo&);
}
