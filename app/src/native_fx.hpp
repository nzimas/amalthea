// SPDX-License-Identifier: GPL-3.0-only
// NATIVE EFFECTS: one-to-one recreations of open-source effects as plain C++ (no
// framework, no library): each is a NativeFx with a fixed profile of 0..100 values.
// Provenance and licences per effect: docs/NATIVE-EFFECTS.md.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
namespace amalthea {
enum class FxType : uint8_t;
constexpr int NativeParams=12;      // stored values per effect profile
constexpr int NativeCount=40;       // effects in this build (append-only order)
using NativeProfiles=std::array<std::array<uint8_t,NativeParams>,NativeCount>;
NativeProfiles nativeDefaults();

enum class NativeUnit : uint8_t {
 Percent,     // 0..100: a plain amount, or a percentage when `suffix` names it ("% DUTY")
 Bipolar,     // -100..+100
 Choice,      // one of `choices`
 Division,    // a tempo division (nativeDivisionBeats)
 Toggle,      // OFF / ON
 Range,       // min..max in `unit` text, linear or exponential
};
struct NativeParam {
 const char*name;uint8_t def;NativeUnit unit;
 uint8_t lo=0,hi=100;                    // the range random draws stay within
 const char*const*choices=nullptr;int choiceCount=0;
 float min=0,max=1;bool exponential=false;const char*suffix="";   // Range and Percent
};
// Recipe families (as LifebloodFamily): 0 space, 1 time, 2 texture, 3 colour, 4 drive, 5 motion, 6 rhythm.
struct NativeInfo {
 FxType type;const char*name,*label,*code;   // full name, field label, two-letter tracker code
 uint8_t family,harshness,cost;              // recipe family, 0..2, CPU weight (100 us units on the Brick)
 uint8_t mix;                                 // default DRY/WET MIX
 int count;const NativeParam*params;
};
int nativeIndex(FxType);                       // -1 when not native
FxType nativeType(int index);
const NativeInfo*nativeInfo(FxType);
const NativeInfo&nativeInfo(int index);

// Tempo divisions shared by every rhythmic effect: musically logical divisions and
// multiples of the reference beat (external MIDI clock, else the session tempo).
constexpr int NativeDivisions=16;
const char*nativeDivisionName(int index);
double nativeDivisionBeats(int index);          // length in quarter notes
int nativeChoice(uint8_t value,int count);      // stored 0..100 to an index
int nativeDivision(uint8_t value);              // stored 0..100 to a division index
double nativeSamplesPerBeat(float bpm);

// Transport restarts (external Start, internal Play): rhythmic effects realign their
// phase to the downbeat when this changes. Audio threads only read it.
uint32_t nativeTransportEpoch();
void nativeTransportRestart();

class NativeFx {
public:
 virtual ~NativeFx()=default;
 // values: NativeParams stored 0..100 values; bpm: the reference tempo.
 virtual void configure(const uint8_t*values,float bpm)=0;
 // Interleaved stereo, in place, fully wet. Bounded time, no allocation, no locks.
 virtual void process(float*io,int frames)=0;
 virtual void clear()=0;
};
std::unique_ptr<NativeFx> makeNativeFx(FxType);
// The stored value of a parameter as text (unit-aware).
const char*nativeValueText(const NativeParam&,int value,char*buffer,int size);
}
