#include "core.hpp"
#include <cstdio>
using namespace amalthea;
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  Project p = demoProject();
  for(int slot=0;slot<Instruments;slot++) p.patches[slot].seed=0x123400u+slot;
  p.master.amount={{40,25,10,20,15,60,65,70,45,50,100}};
  const FxType types[] = {
      FxType::Distortion, FxType::Saturation, FxType::Overdrive,
      FxType::Rat,        FxType::Wavefold,   FxType::Fuzz,
      FxType::Bitcrush,   FxType::Redux,      FxType::Tremolo,
      FxType::Stutter,    FxType::Diode,      FxType::Rectify};
  for (int t = 0; t < Tracks; t++) {
    p.tracks[t].mute = false;
    p.tracks[t].level = 25;
    for (int row = 0; row < 16; row++) {
      auto &s = p.tracks[t].steps[row];
      s.fx[0] = {types[t], 65};
      s.fx[1] = {t % 2 ? FxType::Reverb : FxType::Delay, 60};
      s.condition = {uint8_t(1 + t % 2), uint8_t(1 + t % 3)};
      if (s.condition.occurrence > s.condition.every)
        s.condition.occurrence = 1;
      s.modifier = {int8_t(t % 3 - 1), false};
    }
  }
  std::string error;
  if (!saveProject(p, argv[1], error)) {
    fprintf(stderr, "%s\n", error.c_str());
    return 1;
  }
  puts("Created separate 12-track effects stress project");
}
