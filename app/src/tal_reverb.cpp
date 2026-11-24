#include "tal_reverb.hpp"
#include "core.hpp"
#include <cmath>
#include "../third_party/tal-reverb-iii/Reverb.h"
namespace amalthea {
struct TalReverbBus::Impl {
 TalReverb core{44100};float width=1;bool active=false;int quiet=0;
 Impl(){core.setStereoMode(true);}
};
TalReverbBus::TalReverbBus():impl(std::make_unique<Impl>()){configure(MasterSettings{});}
TalReverbBus::~TalReverbBus()=default;
void TalReverbBus::configure(const MasterSettings&s){
 auto &a=*impl;AudioUtils scale;
 // Preserve both scaling stages used by TAL's original ReverbEngine wrapper.
 a.core.setDecayTime(scale.getLogScaledValueInverted(s.value(9,1)*.01f));
 a.core.setLowShelfGain(s.value(9,2)*.01f);a.core.setHighShelfGain(s.value(9,3)*.01f);
 a.core.setPreDelay(s.value(9,4)*.01f);a.width=s.value(9,5)*.01f;
 a.core.setLowShelfFrequency(s.value(9,6)*.01f);a.core.setHighShelfFrequency(s.value(9,7)*.01f);
}
void TalReverbBus::clear(){impl->core.clear();impl->active=false;impl->quiet=0;}
void TalReverbBus::process(float l,float r,float &ol,float &orr){
 if(std::abs(l)+std::abs(r)>1e-12f){impl->active=true;impl->quiet=0;}
 if(!impl->active){ol=orr=0;return;}
 impl->core.process(&l,&r);float m=(l+r)*.5f,s=(l-r)*.5f*impl->width;ol=m+s;orr=m-s;
 if(std::abs(l)+std::abs(r)<1e-10f){if(++impl->quiet>88200)impl->active=false;}else impl->quiet=0;
}
}
