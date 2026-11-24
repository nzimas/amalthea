#pragma once
#include "core.hpp"
#include "nornios_insert.hpp"
#include "lush_reverb.hpp"
#include "tal_reverb.hpp"
#include "multimode_filter.hpp"
#include "ported_inserts.hpp"
#include <array>
#include <vector>
namespace amalthea {
// Loudness matching: the processed signal keeps the level of what went in, so engaging
// an effect (at any DRY/WET MIX) modifies the sound instead of ducking it. Energy before
// and after is averaged over about 0.3 s; the correction (-24 dB .. +9 dB) glides, and
// holds while the input is quiet, so tails decay naturally rather than being pumped up.
struct LevelMatch {
  float in=0,out=0,gain=1;
  void reset(){in=out=0;gain=1;}
  void apply(const float *dry,float *data,int frames);
};
// One preallocated stereo insert per track/slot. All setters/processors are
// bounded.
class InsertEffect {
public:
  InsertEffect():InsertEffect(true){}
  explicit InsertEffect(bool enableSpace);
  void release();
  void attachPorted(PortedPointers p){ported=p;}
  void configure(Effect, int bpm);
  void setGrainLimit(int limit){nornios.setGrainLimit(limit);}
  void setSpaceLineLimit(int lines){if(space)space->setLineLimit(lines);}
  void setSettings(const InsertSettings &);
  void processBlock(float *interleaved,int frames);
  void clear();
  void process(float &left, float &right);
  void setRedux(float jitter,float slew) { reduxJitter=jitter;reduxSlew=slew; }
  Effect setting() const { return effect; }
  // Effects with memory (reverb, delay, granular, resonator) ring out after their
  // step ends or another effect replaces them, rendered from silence on their own
  // processor and added to the slot output until they decay.
  static bool ringing(FxType);
  int tailCount() const;

private:
  std::unique_ptr<LushReverb> space;
  void configureSpace();
  // Input-follow guard: output may exceed a decaying memory of the input by at most 6 dB.
  struct Guard {
    float memory=0,gain=1;
    void remember(float inputPeak,int frames,FxType type);
    void apply(float *data,int frames);
  };
  Guard guard;
  LevelMatch level;
  static bool levelMatched(FxType);
  struct Tail { FxType type=FxType::None; float mix=0,gain=1; bool fading=false; int quiet=0,age=0; Guard guard; };
  std::array<Tail,3> tails{};
  static int processorOf(FxType);
  void startTail(Effect);
  void renderTails(float *data,int frames);
  void renderMain(float *data,int frames);
  NorniosInsert nornios;
  MultimodeFilter filter;
  PortedPointers ported{};int portedTempo=120;
  InsertSettings settings{};
  Effect effect{};
  float reduxJitter=0,reduxSlew=1;
  std::array<float,2> reduxOut{};
  uint32_t randomState=0x12345678;
  float stutterTail=1;bool releasing=false;
  float amount = 0, coefficient = .1f, phase = 0, phaseStep = 0;
  std::array<float, 2> low{}, highInput{}, highOutput{}, held{};
  int decimate = 0, stutterPos = 0, stutterLength = 1024, recorded = 0;
  std::array<float, 16384> stutter{};
};
class EffectBuses {
public:
  explicit EffectBuses(bool parallel=false);
  void setTempo(int bpm);
  void configureMaster(const MasterSettings &);
  void process(float delayL, float delayR, float reverbL, float reverbR,
               float &outL, float &outR);
  void clear();
  void delay(float l,float r,float &ol,float &orr);
  void reverb(float l,float r,float &ol,float &orr);

private:
  std::vector<float> echo;
  size_t echoPos = 0;
  int echoLength = 11025;
  float echoFeedback=.45f,echoTone=1;
  std::array<float,2> echoLow{};
  TalReverbBus space;
};
class MasterEffects {
public:
  explicit MasterEffects(bool parallel=false);
  void configure(const MasterSettings &,int bpm);
  // keyL/keyR: the sidechain source track's part of l/r (exempt from ducking, and the detector's input when SIDECHAIN is on)
  void process(float &,float &,float delayL=0,float delayR=0,float reverbL=0,float reverbR=0,float keyL=0,float keyR=0);
  bool sidechain()const{return settings.sidechain[0];}
  int sidechainSource()const{return settings.sidechain[1];}
  void clear(int bpm=120);
private:
  MasterSettings settings{};
  EffectBuses buses;
  std::array<float,2> outputGain{{1,1}};
  float outputCeiling=.95f;
  float envelope=0,limiterGain=1,attack=.01f,release=.001f,ratio=1,threshold=.7f,limiterRelease=.001f;
  uint32_t randomState=0x12345678;

};
} // namespace amalthea
