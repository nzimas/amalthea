#pragma once
#include "audio.hpp"
#include "fx_rack.hpp"
#include "lifeblood.hpp"
namespace amalthea {
class FxAudio {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 explicit FxAudio(AudioStats&);~FxAudio();
 void activate(bool);bool active()const;
 bool submit(const FxRack&,const DeviceSettings&);
 // LIFEBLOOD: the summed stereo input through a network. A new generation
 // crossfades from the previous network's processors to fresh ones; within a
 // generation, edges fade in and out and redrawn nodes dip briefly.
 // master: the session's EQ and compressor, on the whole output (kept under bypass).
 bool submit(const LifebloodNetwork&,int tempo,int mix,bool bypass,const DeviceSettings&,const LifebloodMaster&master=LifebloodMaster{});
 void render(float*,int);
 // Synthetic transport entry point, also used by USB capture: packed stereo lanes.
 void feed(const float*,int frames,int channels,int rate);
 // Drop stale ingress and re-prime after a recovered capture discontinuity.
 void inputDiscontinuity();
 void midi(uint8_t,double seconds);
 // LIFEBLOOD: BIGORNA's effects (its state received in the last 3 s) run after the network.
 bool mirroring()const;
};
}
