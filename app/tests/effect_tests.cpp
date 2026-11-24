#include "audio.hpp"
#include "effects.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
using namespace amalthea;
static void check(bool ok, const char *msg) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
  }
}
int main(int argc, char **argv) {
  for (int type = 1; type < int(FxType::Count); type++) {
    if (FxType(type) >= FxType::Abrasion || FxType(type) == FxType::Delay || FxType(type) == FxType::Reverb)
      continue;
    InsertEffect insert;
    insert.configure({FxType(type), 85}, 120);
    double difference = 0;
    float max = 0;
    for (int i = 0; i < 44100; i++) {
      float dry = .12f * sin(i * .071f) + .07f * sin(i * .197f), l = dry,
            r = dry;
      insert.process(l, r);
      check(std::isfinite(l) && std::isfinite(r), "finite insert");
      difference += std::abs(l - dry) + std::abs(r - dry);
      max = std::max(max, std::max(std::abs(l), std::abs(r)));
    }
    check(difference > 1, "effect transforms audio");
    printf("%-14s difference=%.2f peak=%.3f\n", fxName(FxType(type)),
           difference, max);
  }
  for (bool reverb : {false, true}) {
    EffectBuses bus;
    bus.setTempo(120);
    double tail = 0;
    for (int i = 0; i < 88200; i++) {
      float impulse = i == 0 ? .2f : 0, l, r;
      bus.process(reverb ? 0 : impulse, reverb ? 0 : impulse,
                  reverb ? impulse : 0, reverb ? impulse : 0, l, r);
      check(std::isfinite(l) && std::isfinite(r), "finite wet tail");
      if (i > 1000)
        tail += l * l + r * r;
    }
    check(tail > 1e-6, "real delayed/reverberant tail");
    printf("%s tail energy %.8f\n", reverb ? "reverb" : "delay", tail);
  }
  // Full tracker output comparisons prove sends/inserts are wired into
  // playback.
  std::filesystem::path output = argc > 1 ? argv[1] : "";
  if (!output.empty())
    std::filesystem::create_directories(output);
  std::vector<float> dry(44100 * 4 * 2);
  Project p;
  p.tracks[0].steps[0].note = 60;
  p.tracks[0].steps[0].instrument = 5;
  Audio reference;
  reference.submit(p, true);
  reference.render(dry.data(), 44100 * 4);
  if (!output.empty())
    writeWav((output / "00-dry.wav").string(), dry.data(), 44100 * 4);
  for (int type = 1; type < int(FxType::Count); type++) {
    for (int row = 0; row < 16; row++)
      p.tracks[0].steps[row].fx[0] = {FxType(type), 80};
    Audio audio;
    audio.submit(p, true);
    std::vector<float> wet(dry.size());
    audio.render(wet.data(), 44100 * 4);
    double diff = 0;
    for (size_t i = 0; i < wet.size(); i++) {
      check(std::isfinite(wet[i]) && std::abs(wet[i]) <= .951f,
            "finite integrated FX");
      diff += std::abs(wet[i] - dry[i]);
    }
    check(diff > .01, "audio callback contains selected effect");
    audio.submit(p,false);float stopped[128]{};audio.render(stopped,64);
    for(float v:stopped)check(std::isfinite(v)&&std::abs(v)<=.951f,"stop releases notes and continues bounded effect tails");
    if(FxType(type)==FxType::Stutter){
      p.tracks[0].mute=true;audio.submit(p,true);audio.render(stopped,64);
      for(int i=64;i<128;i++)check(stopped[i]==0,"mute suppresses captured stutter");
      p.tracks[0].mute=false;audio.submit(p,false);
    }
    if (!output.empty())
      writeWav((output / (std::to_string(type) + "-" +
                          fxText({FxType(type), 80}) + ".wav"))
                   .string(),
               wet.data(), 44100 * 4);
  }
  puts("PASS: all insert effects, send tails and full audio integration");
}
