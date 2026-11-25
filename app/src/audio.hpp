#pragma once
#include "core.hpp"
#include <atomic>
#include <memory>
namespace amalthea {
// Orders pattern swaps: a deferred message holds the next pattern until the quantized bar;
// messages older than the pattern the audio swapped in are dropped until the editor takes it.
struct PatternSync { uint32_t generation=0; bool deferred=false, keepPending=false; uint8_t quantize=QuantizeLongest; };
struct AudioStats {
 std::atomic<uint32_t> patternGeneration{0};   // the last queued pattern the audio swapped in
 std::atomic<int> usbChannels{0},usbFirmware{0},usbState{0},usbPort{0};
 std::atomic<float> usbBpm{0};std::atomic<bool> usbClock{false},usbRunning{false};
 // Each FM1 track's pan as its CC 10 value (0 .. 127), -1 not heard yet (centred).
 std::array<std::atomic<int>,6> usbPan{{-1,-1,-1,-1,-1,-1}};
 std::array<std::atomic<int>,12> fxStep{};
 std::array<std::atomic<float>,6> fxMeters{};
 // LIFEBLOOD: decaying peak of each vertex's output (0 IN, 1 OUT, 2..8 nodes, 9..14 FM1 tracks),
 // and the FM1 tracks heard in the last 30 seconds (bit per track).
 std::array<std::atomic<float>,15> lifebloodLevels{};
 std::atomic<uint8_t> lifebloodHeard{0};
 std::atomic<uint64_t> usbOverruns{0},usbUnderruns{0},usbCaptureXruns{0},usbFrames{0},usbCaptureMaxGapUs{0},fxRenderMaxUs{0};
 std::atomic<int> usbFill{0};
  std::array<std::array<std::atomic<int>,Columns>,Tracks> columnRow{};
  std::array<std::array<std::atomic<uint64_t>,Columns>,Tracks> columnCycle{},columnAdvances{};
  AudioStats(){for(auto &track:columnRow)for(auto &row:track)row.store(-1);}
  std::atomic<uint64_t> cycle{0};
  std::atomic<uint64_t> auditions{0};
  std::atomic<int> auditionNote{-1};
  std::atomic<int> row{-1}, voices{0};
  std::atomic<uint64_t> steals{0}, blocks{0}, overruns{0};
  std::array<std::atomic<float>, Tracks> meters{};
  std::array<std::atomic<uint64_t>, Tracks> triggers{};
  std::array<std::atomic<int>, Tracks> lastNote{}, lastInstrument{}, trackRow{};
  std::array<std::atomic<uint64_t>, Tracks> trackCycle{}, advances{};
};
class Audio {
public:
  explicit Audio(size_t voiceBudget = 128);
  ~Audio();
  bool submit(const Project &, bool playing, int auditionTrack=-1, const Step *auditionStep=nullptr,const DeviceSettings *device=nullptr,bool endAudition=false,PatternSync sync={});
  void render(float *stereo, int frames);
  AudioStats stats;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
void writeWav(const std::string &, const float *, size_t frames);
} // namespace amalthea
