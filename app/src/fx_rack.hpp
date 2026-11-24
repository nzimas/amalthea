#pragma once
#include "core.hpp"
#include "walk.h"
#include <bitset>
namespace amalthea {
constexpr int FxChannels=6,FxSlots=12,FxLocks=128;
struct FxRackStep {bool enabled=false;std::bitset<FxLocks> locks{};std::array<uint8_t,FxLocks> values{};};
struct FxRackSlot {
 Effect effect{};InsertSettings settings{};
 uint8_t length=16,clock=3,direction=0,density=45,complexity=50;bool sequenced=false;
 uint16_t tempo=0; // internal-clock BPM for this slot; 0 follows the session tempo
 std::array<FxRackStep,16> steps{};
};
struct FxRack {
 std::array<FxRackSlot,FxSlots> slots{};
 std::array<uint8_t,FxChannels> levels{{80,80,80,80,80,80}};
 std::array<bool,FxChannels> bypass{}; // true: both slots skipped, channel passes dry at its level
 uint32_t seed=0x179adb41;
 uint16_t tempo=120;bool running=false;uint64_t reset=0;
};
int fxParameterKey(FxType,int module,int field);
void resolveFxStep(const FxRackSlot&,int step,Effect&,InsertSettings&);
bool randomizeFxSequence(FxRack&,int slot);
int fxSlotTempo(const FxRack&,int slot);
// Name and displayed value of one row of the slot parameter page (row 0 is
// EFFECT in default edit, TRIGGER in step edit; row n is parameter field n-1).
void fxParameterDisplay(const FxRackSlot&,bool defaultEdit,int step,int page,int row,std::string&name,std::string&value);
void randomizeFxSlot(FxRack&,int slot,bool parametersOnly,const DeviceSettings&);
// A playable random value and parameter set for an effect (its type unchanged).
void randomizeFxParameters(Effect&,InsertSettings&,uint32_t&seed);
// Keep a random draw within the Brick's audio budget. Measured there with random
// parameters, QDELAY reached 2.7 ms and MATRIX 7.4 ms of a 5.8 ms block. QDELAY becomes
// a tape and stereo delay (timing, repeats, width, swing, modulation and tape stay as
// drawn; pre-distortion, diffusion, crusher, shifter, phaser, ducking and EQ return to
// their defaults, off); MATRIX keeps its two delay nodes on its default delay type.
void boundInsertCost(Effect&,InsertSettings&);
struct MidiClock {
 double bpm=0,last=0,interval=0;uint64_t ticks=0;bool locked=false,running=false,reset=false,stopped=false;int pulse=0;
 void byte(uint8_t,double seconds);
 void expire(double seconds);
};
struct UsbMapping {int count=0,offset=0;bool stereo=false;};
UsbMapping fm1Mapping(int firmware,int channels);
const char* fm1Name(int firmware);
int fm1Firmware(const std::string&);
bool saveFxRack(const FxRack&,const std::string&,std::string&);
std::string fxSessionPath(const std::string&root,int slot);
std::array<uint8_t,64> scanFxSessions(const std::string&root);
bool loadFxRack(FxRack&,const std::string&,std::string&);
}
