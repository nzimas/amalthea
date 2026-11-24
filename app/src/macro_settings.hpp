#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace amalthea {
constexpr int MacroCount=3,MacroParameters=8;
struct MacroSettings {std::array<uint8_t,MacroParameters> value{};};
inline std::array<MacroSettings,MacroCount> defaultMacros(){return {{{{{40,50,45,60,65,20,75,100}}},{{{65,55,15,50,0,85,10,100}}},{{{65,65,40,65,70,45,85,100}}}}};}
inline int macroIndex(int module){return module==0?0:module==3?1:module==5?2:-1;}
constexpr int MasterPages=7,TrackFields=12;
inline int masterPageModule(int page){constexpr int m[]={0,3,5,7,8,9,10};return m[page];}
inline int masterPageIndex(int module){for(int i=0;i<MasterPages;i++)if(masterPageModule(i)==module)return i;return 0;}
const char *macroName(int);
const char *macroParameterName(int,int);
// The macro parameter value in its own units (Hz, dB, bit, ms...).
std::string macroValueText(int module,int parameter,int value);
}
