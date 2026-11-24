#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <vector>
namespace amalthea {
// One bounded diagnostic capture. Callback paths only copy into preallocated RAM.
class AudioProbe {
 std::string root;std::vector<float> raw,processed;std::thread writer;
 std::atomic<bool> alive{true},started{false},inputReady{false},outputReady{false};
 int channels=0,rate=0;size_t skipped=0,inputCount=0,outputCount=0;
 void save();
public:
 explicit AudioProbe(std::string destination);~AudioProbe();
 void capture(const float*packedStereo,int frames,int lanes,int sampleRate);
 void output(const float*stereo,int frames);
};
}
