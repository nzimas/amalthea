// Synthetic in-memory performance check for OBRADOR, RINGS and PLAITS: never reads or writes a
// live session. For each engine, every family in turn (one seed each) on `tracks` tracks of dense
// 16th notes at 120 BPM, no inserts; reports the slowest family's p99 and maximum per 256-frame
// block (5805 us is real time) and the patch preparation time.
//   benchmark_instruments [engine 12..14] [seconds] [tracks]
#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
extern "C" {
int amalthea_obrador_family_count(void);
int amalthea_obrador_family_index(uint32_t);
int amalthea_rings_family_count(void);
int amalthea_rings_family_index(unsigned);
int amalthea_plaits_family_count(void);
int amalthea_plaits_family_index(unsigned);
}
using namespace amalthea;
int main(int argc, char **argv) {
  int first = argc > 1 ? std::atoi(argv[1]) : ObradorEngine, last = argc > 1 ? first + 1 : PlaitsEngine + 1;
  int duration = argc > 2 ? std::max(1, std::atoi(argv[2])) : 3;
  int tracks = argc > 3 ? std::clamp(std::atoi(argv[3]), 1, Tracks) : Tracks;
  for (int e = first; e < last; e++) {
    int count = e == ObradorEngine ? amalthea_obrador_family_count() : e == RingsEngine ? amalthea_rings_family_count() : amalthea_plaits_family_count();
    auto index = [&](uint32_t s) { return e == ObradorEngine ? amalthea_obrador_family_index(s) : e == RingsEngine ? amalthea_rings_family_index(s) : amalthea_plaits_family_index(s); };
    long long worstP99 = 0, worstMax = 0; double worstSeconds = 0, cookTotal = 0; const char *worstName = "";
    for (int f = 0; f < count; f++) {
      uint32_t seed = 1; while (index(seed) != f) seed++;
      Project p = blankProject();
      for (auto &patch : p.patches) patch = {uint8_t(e), seed};
      for (int t = 0; t < tracks; t++) for (int r = 0; r < 16; r++) { auto &s = p.tracks[t].steps[r]; s.note = 36 + t * 2 + (r % 4) * 3; s.instrument = 0; }
      Audio a;
      auto prep = std::chrono::steady_clock::now();
      a.submit(p, true);
      double cook = std::chrono::duration<double>(std::chrono::steady_clock::now() - prep).count();
      cookTotal += cook;
      float out[512]; std::vector<long long> times; double energy = 0;
      auto begin = std::chrono::steady_clock::now();
      for (int i = 0; i < 44100 * duration; i += 256) {
        auto start = std::chrono::steady_clock::now();
        a.render(out, 256);
        times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count());
        for (float v : out) { if (!std::isfinite(v)) return 1; energy += v * v; }
      }
      double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
      std::sort(times.begin(), times.end());
      long long p99 = times[times.size() * 99 / 100], mx = times.back();
      printf("%s %-14s %d tracks; %ds audio in %.3fs; p99 %lldus max %lldus; prep %.3fs; energy %.1f; steals %llu\n", instrumentName(e), patchFamily({uint8_t(e), seed}),
             tracks, duration, seconds, p99, mx, cook, energy, (unsigned long long)a.stats.steals.load());
      fflush(stdout);
      if (p99 > worstP99) worstP99 = p99, worstName = patchFamily({uint8_t(e), seed});
      worstMax = std::max(worstMax, mx), worstSeconds = std::max(worstSeconds, seconds);
    }
    printf("== %s: %d families; slowest p99 %lldus (%s), worst max %lldus, slowest render %.3fs for %ds; mean prep %.3fs; budget 5805us a 256-frame block\n",
           instrumentName(e), count, worstP99, worstName, worstMax, worstSeconds, duration, cookTotal / count);
    fflush(stdout);
  }
}
