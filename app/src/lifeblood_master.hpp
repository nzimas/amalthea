#pragma once
// LIFEBLOOD's mastering stage, on the whole output after the network, the MIX and
// BIGORNA's mirrored returns: a 10-band EQ into a stereo bus compressor (corrective
// EQ first, so the compressor reacts to the balance that is heard). Both off by default.
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
namespace amalthea {
constexpr int MasterBands=10;
constexpr int MasterBandHz[MasterBands]={31,62,125,250,500,1000,2000,4000,8000,16000};
constexpr int MasterBandRange=12;   // dB, each way
// Compressor tables: the settings store an index.
constexpr float CompRatios[]={1.5f,2,3,4,6,8,10,20};
constexpr float CompAttacksMs[]={.1f,.3f,1,3,5,10,20,30,50,100};
constexpr int CompReleasesMs[]={0,20,50,100,200,300,500,800,1200,2000};   // 0: AUTO (program dependent)
constexpr int CompSidechainHz[]={0,40,60,90,120,150,200};                  // 0: OFF
constexpr int CompRatioCount=int(std::size(CompRatios)),CompAttackCount=int(std::size(CompAttacksMs)),
 CompReleaseCount=int(std::size(CompReleasesMs)),CompSidechainCount=int(std::size(CompSidechainHz));
constexpr int CompThresholdMin=-40,CompKneeMax=24,CompMakeupMax=24;
// Profiles: starting points for the appliance's music. CUSTOM (0) is shown once a
// value is changed by hand. Append only (sessions store the number).
enum CompProfile : uint8_t { CompCustom, CompGlue, CompPunch, CompPump, CompWall, CompSmash, CompDrone, CompAcousmatic, CompProfiles };
enum EqProfile : uint8_t { EqCustom, EqFlat, EqIndustrial, EqPowerNoise, EqRhythmic, EqSynthwave, EqDarkAmbient, EqAcousmatic, EqTexture, EqProfiles };
struct LifebloodMaster {
 // COMP
 uint8_t compOn=0,compProfile=CompGlue;
 int8_t threshold=-16;   // dB, CompThresholdMin .. 0
 uint8_t ratio=1,attack=7,release=0,knee=6;
 uint8_t makeup=0;       // 0: AUTO, else makeup-1 dB (0 .. CompMakeupMax)
 uint8_t sidechain=3,mix=100;
 // EQ
 uint8_t eqOn=0,eqProfile=EqFlat;
 std::array<int8_t,MasterBands> band{};   // dB, -MasterBandRange .. MasterBandRange
};
void applyCompProfile(LifebloodMaster&,int profile);
void applyEqProfile(LifebloodMaster&,int profile);
const char *compProfileName(int);
const char *eqProfileName(int);
const char *compProfileUse(int);   // what the profile is for, one line
const char *eqProfileUse(int);
bool masterValid(const LifebloodMaster&);
bool sameMaster(const LifebloodMaster&,const LifebloodMaster&);
// Settings rows of the COMP and EQ tabs.
constexpr int MasterCompFields=10,MasterEqFields=2+MasterBands;
const char *masterCompFieldName(int);
std::string masterCompValue(const LifebloodMaster&,int field);
std::string masterBandName(int band);   // "31 HZ" .. "16 KHZ"
// Step a COMP / EQ field by d (shift: coarse). A changed value makes the profile CUSTOM;
// the PROFILE row loads a profile. Returns false when nothing changed.
bool stepMasterComp(LifebloodMaster&,int field,int d,bool coarse);
bool stepMasterEq(LifebloodMaster&,int field,int d,bool coarse);

// The DSP, on the audio thread. Each stage crossfades in and out over 20 ms; a stage
// that is off and faded out costs nothing.
class LifebloodMasterDsp {
public:
 LifebloodMasterDsp();
 void set(const LifebloodMaster&);
 void reset();
 void process(float *lr,int frames);   // interleaved stereo, in place
 float reduction()const{return reductionDb;}   // the compressor's gain reduction now, dB (>= 0)
 float peakReduction();                         // the largest since the last call
private:
 struct Svf { float ic1[2]{},ic2[2]{}; float a1=1,a2=0,a3=0,m0=1,m1=0,m2=0; };
 void bandCoefficients(int b,float db);
 void sidechainCoefficients(int hz);
 LifebloodMaster target{};bool fresh=true;
 // EQ
 std::array<Svf,MasterBands> eq{};std::array<float,MasterBands> bandDb{};
 float eqWet=0;
 // Compressor
 Svf hp{};int hpHz=0;float compWet=0;
 float thresholdDb=-16,slope=.5f,kneeDb=6,makeupDb=0,mixAmount=1;
 float attackCoef=0,releaseCoef=0,autoFast=0,autoSlowAttack=0,autoSlowRelease=0;bool autoRelease=true;
 float peakFast=0,peakSlow=0,smoothed=0,reductionDb=0,peakReductionDb=0;
};
}
