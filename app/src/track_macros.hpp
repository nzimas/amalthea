#pragma once
#include "macro_settings.hpp"
#include <array>
namespace amalthea {
// Fixed-storage, independent track processors. Send replaces the sent portion
// with its wet return; unsent audio remains dry. Stages run in chain order.
class TrackMacros {
public:
 TrackMacros();
 void configure(const std::array<MacroSettings,MacroCount>&);
 void clear();
 bool active()const {for(const auto&s:state)if(s.active)return true;return false;}
 void process(float&,float&,const std::array<float,MacroCount>&);
private:
 struct State {
  std::array<float,2> low{},last{},tone{},dcX{},dcY{},held{},slew{};
  std::array<std::array<float,8>,2> allpass{};
  std::array<float,4096> delay{};
  float phase=0,holdPhase=1,memory=0,guard=1;int position=0,quiet=0;uint32_t random=0x6b8b4567;bool active=false;
 };
 std::array<State,MacroCount> state{};
 std::array<MacroSettings,MacroCount> settings{};
 struct Coeff {float drive=1,character=0,cross=0,body=1,edge=1,fold=0,tone=1,mix=1,bits=32768,rate=1,jitter=0,asym=0,slew=1,flange=0,phaser=0,lfoStep=0,feedback=0,spread=0,color=0;};
 std::array<Coeff,MacroCount> coeff{};
 void wet(int,float,float,float&,float&);
};
}
