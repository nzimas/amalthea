#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>
namespace amalthea {

// Shared USB audio output for every appliance. The render thread (codec clock)
// pushes interleaved frames; the USB writer (host clock) pulls them through a
// fill-controlled resampler, so neither clock has to follow the other.
// Channel layout: 1-2 stereo mix, then optional stems.
class DriftBridge {
public:
 DriftBridge(int channels,int capacity,int target);
 // Producer (render thread). Never blocks; drops the block when full.
 void push(const float*interleaved,int frames);
 // Consumer (USB writer). Always fills `frames`; writes silence while priming.
 // Returns false when it produced silence.
 bool pull(float*interleaved,int frames);
 // Consumer: discard backlog (host was idle) and re-prime at the target fill.
 void restart();
 int channels()const{return lanes;}
 int target()const{return goal;}
 int fill()const;
 double ratio()const{return currentRatio;}
 uint64_t dropped()const{return droppedFrames.load(std::memory_order_relaxed);}
 uint64_t underruns()const{return underrunCount;}
private:
 int lanes,size,goal;
 std::vector<float> ring;
 std::atomic<uint64_t> written{0},consumed{0},droppedFrames{0};
 // Consumer-only state.
 double phase=0,filtered=0,integral=0,currentRatio=1;
 bool primed=false;uint64_t underrunCount=0;
 float at(uint64_t frame,int lane)const{return ring[(frame%size)*lanes+lane];}
};

enum class UsbAudioState{Unavailable,Waiting,Idle,Streaming,Error};

// Brick only: owns the UAC2 gadget function and its writer thread. Elsewhere the
// object exists but does nothing, so appliances can always call push().
class UsbAudioOut {
 struct Impl;std::unique_ptr<Impl> impl;
public:
 static constexpr int Rate=44100,Channels=2;
 UsbAudioOut();~UsbAudioOut();
 void push(const float*interleaved,int frames);
 UsbAudioState state()const;
};

// USB-C port mode on the TG3040 AXP2202 (register 0xE3 bits 1:0). A phone only
// takes the host role when the Brick is a pure sink; FX hosting needs DRP.
enum class TypeCMode{Sink=1,Drp=3};
uint8_t typeCModeValue(uint8_t current,TypeCMode mode);
}
