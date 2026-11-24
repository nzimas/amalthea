#pragma once
#include <memory>
namespace amalthea {
struct MasterSettings;
// Original TAL-Reverb-III DSP, native stereo wet bus at 44.1 kHz.
class TalReverbBus {
public:
 TalReverbBus();~TalReverbBus();
 void configure(const MasterSettings&);
 void clear();
 void process(float,float,float&,float&);
private:
 struct Impl;std::unique_ptr<Impl> impl;
};
}
