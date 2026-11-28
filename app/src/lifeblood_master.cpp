// LIFEBLOOD's mastering stage (lifeblood_master.hpp).
//
// EQ: Cytomic's trapezoidal state-variable filter (Andrew Simper, "SvfLinearTrapOptimised2"):
// the bilinear response of an RBJ biquad, but stable and quiet while its gain glides, and
// precise at 31 Hz where a direct-form biquad's poles crowd z = 1. Bells from 62 Hz to 8 kHz,
// a low shelf at 31 Hz and a high shelf at 16 kHz (a 16 kHz bell at 44.1 kHz would be
// squeezed against Nyquist). Proportional Q: broad at small gains, an octave at full gain.
// A band at 0 dB is exactly unity (m1 = m2 = 0) and is skipped.
//
// Compressor: feedforward, the gain computed in dB (Giannoulis, Massberg and Reiss, "Digital
// Dynamic Range Compressor Design", JAES 2012): soft knee, then their smooth decoupled peak
// detector on the gain reduction. Stereo linked on the louder channel; a high-pass on the
// detector only keeps kicks and sub from pumping the mix. AUTO release adds a slow branch
// that builds up under sustained compression and lets go slowly, while bursts on top of
// it still recover fast. log2 / exp2 are fast approximations, on the control path only.
#include "lifeblood_master.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace amalthea {
namespace {
constexpr float Rate=44100,Pi=3.14159265f;
constexpr float StageStep=1.f/882;   // 20 ms stage crossfade
// Profiles. COMP: threshold, ratio, attack, release, knee, makeup (0 AUTO), sidechain, mix.
struct CompValues { int8_t threshold;uint8_t ratio,attack,release,knee,makeup,sidechain,mix; };
const CompValues CompTable[CompProfiles]={
 {-16,1,7,0,6,0,3,100},   // CUSTOM (never loaded)
 {-16,1,7,0,6,0,3,100},   // GLUE: 2:1, 30 ms, AUTO, SC 90 Hz
 {-18,3,5,3,4,0,3,100},   // PUNCH: 4:1, 10 ms lets the hits through, 100 ms
 {-24,4,3,4,2,0,0,100},   // PUMP: 6:1, 3 ms, 200 ms, the kick drives it (SC off)
 {-28,6,1,2,0,0,2,100},   // WALL: 10:1, 0.3 ms, 50 ms, hard knee
 {-32,7,2,3,0,0,3,35},    // SMASH: 20:1 crushed under the dry (35%)
 {-24,0,8,8,12,0,1,100},  // DRONE: 1.5:1, 50 ms, 1.2 s, wide knee
 {-8,2,4,0,8,1,1,100},    // ACOUSMATIC: only the loudest gestures, no makeup
};
const char*CompNames[CompProfiles]={"CUSTOM","GLUE","PUNCH","PUMP","WALL","SMASH","DRONE","ACOUSMATIC"};
const char*CompUses[CompProfiles]={"YOUR OWN SETTINGS","BUS GLUE: TECHNO, DARK SYNTHWAVE","INDUSTRIAL TECHNO: HITS COME THROUGH",
 "DARK SYNTHWAVE, TECHNO: THE KICK PUMPS","POWER NOISE: A DENSE WALL","RHYTHMIC NOISE: PARALLEL CRUSH",
 "DARK AMBIENT, DRONES: SLOW LEVELLING","ACOUSMATIC: KEEPS THE DYNAMICS"};
// EQ: 31 Hz .. 16 kHz, dB.
const int8_t EqTable[EqProfiles][MasterBands]={
 {0,0,0,0,0,0,0,0,0,0},          // CUSTOM (never loaded)
 {0,0,0,0,0,0,0,0,0,0},          // FLAT
 {-2,3,1,-2,-2,0,1,2,1,1},       // INDUSTRIAL: kick weight, less mud, bite
 {-3,2,2,1,0,-1,-2,-3,-2,0},     // POWER NOISE: body, the fatiguing 2-8 kHz tamed
 {-1,3,2,-1,-2,0,2,1,-1,-1},     // RHYTHMIC: kick and snap
 {0,2,1,-1,-2,-1,0,1,2,3},       // SYNTHWAVE: deep, open top
 {1,2,2,1,0,-1,-2,-3,-3,-4},     // DARK AMBIENT: warm and dark
 {-4,-1,0,0,0,0,0,0,1,2},        // ACOUSMATIC: no rumble, a little air
 {-2,-1,0,0,1,2,2,2,1,0},        // TEXTURE: detail in the mids and highs
};
const char*EqNames[EqProfiles]={"CUSTOM","FLAT","INDUSTRIAL","POWER NOISE","RHYTHMIC","SYNTHWAVE","DARK AMBIENT","ACOUSMATIC","TEXTURE"};
const char*EqUses[EqProfiles]={"YOUR OWN SETTINGS","NO CHANGE","KICK WEIGHT, LESS MUD, BITE","BODY, HARSH 2-8 KHZ TAMED",
 "KICK AND SNAP","DEEP LOWS, OPEN TOP","WARM AND DARK","NO RUMBLE, A LITTLE AIR","DETAIL IN THE MIDS AND HIGHS"};
const char*CompFieldNames[MasterCompFields]={"COMPRESSOR","PROFILE","THRESHOLD","RATIO","ATTACK","RELEASE","KNEE","MAKEUP","SC FILTER","MIX"};
// Fast log2 and exp2 (well under 0.01 dB), for the control path.
inline float fastLog2(float x){
 uint32_t b;std::memcpy(&b,&x,4);int e=int((b>>23)&255)-127;b=(b&0x007fffffu)|0x3f800000u;float m;std::memcpy(&m,&b,4);
 if(m>1.41421356f){m*=.5f;e++;}
 float s=(m-1)/(m+1),s2=s*s;   // ln m = 2 atanh s, |s| < 0.172
 return float(e)+2.8853901f*s*(1+s2*(.33333333f+s2*(.2f+s2*.14285714f)));
}
inline float fastExp2(float x){
 x=std::clamp(x,-126.f,126.f);float i=std::floor(x),f=x-i;
 float p=1+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*(.00133336f+f*.00015404f)))));
 uint32_t b=uint32_t(int(i)+127)<<23;float s;std::memcpy(&s,&b,4);return s*p;
}
float coefficient(float ms){return ms<=0?0.f:std::exp(-1.f/(ms*.001f*Rate));}
}
void applyCompProfile(LifebloodMaster&m,int p){
 p=std::clamp(p,1,CompProfiles-1);auto&v=CompTable[p];m.compProfile=uint8_t(p);
 m.threshold=v.threshold;m.ratio=v.ratio;m.attack=v.attack;m.release=v.release;m.knee=v.knee;m.makeup=v.makeup;m.sidechain=v.sidechain;m.mix=v.mix;
}
void applyEqProfile(LifebloodMaster&m,int p){
 p=std::clamp(p,1,EqProfiles-1);m.eqProfile=uint8_t(p);for(int b=0;b<MasterBands;b++)m.band[b]=EqTable[p][b];
}
const char*compProfileName(int p){return CompNames[std::clamp(p,0,CompProfiles-1)];}
const char*eqProfileName(int p){return EqNames[std::clamp(p,0,EqProfiles-1)];}
const char*compProfileUse(int p){return CompUses[std::clamp(p,0,CompProfiles-1)];}
const char*eqProfileUse(int p){return EqUses[std::clamp(p,0,EqProfiles-1)];}
bool masterValid(const LifebloodMaster&m){
 bool ok=m.compOn<=1&&m.compProfile<CompProfiles&&m.threshold>=CompThresholdMin&&m.threshold<=0&&m.ratio<CompRatioCount&&m.attack<CompAttackCount
  &&m.release<CompReleaseCount&&m.knee<=CompKneeMax&&m.makeup<=CompMakeupMax+1&&m.sidechain<CompSidechainCount&&m.mix<=100&&m.eqOn<=1&&m.eqProfile<EqProfiles;
 for(auto b:m.band)ok&=b>=-MasterBandRange&&b<=MasterBandRange;
 return ok;
}
bool sameMaster(const LifebloodMaster&a,const LifebloodMaster&b){
 return a.compOn==b.compOn&&a.compProfile==b.compProfile&&a.threshold==b.threshold&&a.ratio==b.ratio&&a.attack==b.attack&&a.release==b.release&&a.knee==b.knee
  &&a.makeup==b.makeup&&a.sidechain==b.sidechain&&a.mix==b.mix&&a.eqOn==b.eqOn&&a.eqProfile==b.eqProfile&&a.band==b.band;
}
const char*masterCompFieldName(int f){return CompFieldNames[std::clamp(f,0,MasterCompFields-1)];}
static std::string number(float v){char s[16];if(v<1)std::snprintf(s,sizeof s,"%.1f",v);else std::snprintf(s,sizeof s,"%g",v);return s;}
std::string masterCompValue(const LifebloodMaster&m,int f){
 switch(f){
  case 0:return m.compOn?"ON":"OFF";
  case 1:return compProfileName(m.compProfile);
  case 2:return std::to_string(m.threshold)+" DB";
  case 3:return number(CompRatios[m.ratio])+":1";
  case 4:return number(CompAttacksMs[m.attack])+" MS";
  case 5:return CompReleasesMs[m.release]?(CompReleasesMs[m.release]>=1000?number(CompReleasesMs[m.release]/1000.f)+" S":std::to_string(CompReleasesMs[m.release])+" MS"):"AUTO";
  case 6:return m.knee?std::to_string(m.knee)+" DB":"HARD";
  case 7:return m.makeup?"+"+std::to_string(m.makeup-1)+" DB":"AUTO";
  case 8:return CompSidechainHz[m.sidechain]?std::to_string(CompSidechainHz[m.sidechain])+" HZ":"OFF";
  default:return std::to_string(m.mix)+"%";
 }
}
std::string masterBandName(int b){int hz=MasterBandHz[std::clamp(b,0,MasterBands-1)];return hz>=1000?std::to_string(hz/1000)+" KHZ":std::to_string(hz)+" HZ";}
bool stepMasterComp(LifebloodMaster&m,int f,int d,bool coarse){
 auto before=m;auto stepped=[&](auto&v,int lo,int hi,int step){v=decltype(v)(std::clamp(int(v)+d*step,lo,hi));};
 switch(f){
  case 0:m.compOn=d>0;break;
  case 1:{int p=m.compProfile;p=p==CompCustom?(d>0?1:CompProfiles-1):1+(p-1+d+(CompProfiles-1))%(CompProfiles-1);bool on=m.compOn;applyCompProfile(m,p);m.compOn=on;return true;}
  case 2:stepped(m.threshold,CompThresholdMin,0,coarse?6:1);break;
  case 3:stepped(m.ratio,0,CompRatioCount-1,1);break;
  case 4:stepped(m.attack,0,CompAttackCount-1,1);break;
  case 5:stepped(m.release,0,CompReleaseCount-1,1);break;
  case 6:stepped(m.knee,0,CompKneeMax,coarse?6:1);break;
  case 7:stepped(m.makeup,0,CompMakeupMax+1,coarse?6:1);break;
  case 8:stepped(m.sidechain,0,CompSidechainCount-1,1);break;
  default:stepped(m.mix,0,100,coarse?10:1);break;
 }
 if(sameMaster(before,m))return false;
 if(f>=2)m.compProfile=CompCustom;
 return true;
}
bool stepMasterEq(LifebloodMaster&m,int f,int d,bool coarse){
 auto before=m;
 if(f==0)m.eqOn=d>0;
 else if(f==1){int p=m.eqProfile;p=p==EqCustom?(d>0?1:EqProfiles-1):1+(p-1+d+(EqProfiles-1))%(EqProfiles-1);applyEqProfile(m,p);return true;}
 else{auto&b=m.band[std::clamp(f-2,0,MasterBands-1)];b=int8_t(std::clamp(int(b)+d*(coarse?3:1),-MasterBandRange,MasterBandRange));}
 if(sameMaster(before,m))return false;
 if(f>=2)m.eqProfile=EqCustom;
 return true;
}

LifebloodMasterDsp::LifebloodMasterDsp(){reset();set(target);}
void LifebloodMasterDsp::reset(){
 for(auto&s:eq){std::memset(s.ic1,0,sizeof s.ic1);std::memset(s.ic2,0,sizeof s.ic2);}
 std::memset(hp.ic1,0,sizeof hp.ic1);std::memset(hp.ic2,0,sizeof hp.ic2);
 peakFast=peakSlow=smoothed=reductionDb=peakReductionDb=0;eqWet=target.eqOn;compWet=target.compOn;
 for(int b=0;b<MasterBands;b++){bandDb[b]=target.band[b];bandCoefficients(b,bandDb[b]);}
}
void LifebloodMasterDsp::bandCoefficients(int b,float db){
 auto&s=eq[b];
 if(std::abs(db)<1e-3f){s.a1=1;s.a2=s.a3=0;s.m0=1;s.m1=s.m2=0;return;}
 float A=std::pow(10.f,db/40),q=.9f+.51f*std::min(1.f,std::abs(db)/MasterBandRange),k=1/q,g=std::tan(Pi*MasterBandHz[b]/Rate);
 if(b==0){g/=std::sqrt(A);s.m0=1;s.m1=k*(A-1);s.m2=A*A-1;}                        // low shelf
 else if(b==MasterBands-1){g*=std::sqrt(A);s.m0=A*A;s.m1=k*(1-A)*A;s.m2=1-A*A;}         // high shelf
 else{k=1/(q*A);s.m0=1;s.m1=k*(A*A-1);s.m2=0;}                                           // bell
 s.a1=1/(1+g*(g+k));s.a2=g*s.a1;s.a3=g*s.a2;
}
void LifebloodMasterDsp::sidechainCoefficients(int hz){
 hpHz=hz;if(!hz)return;float g=std::tan(Pi*hz/Rate),k=1.41421356f;   // Butterworth
 hp.a1=1/(1+g*(g+k));hp.a2=g*hp.a1;hp.a3=g*hp.a2;hp.m0=1;hp.m1=-k;hp.m2=-1;   // high-pass: v0 - k v1 - v2
}
void LifebloodMasterDsp::set(const LifebloodMaster&m){
 if(!fresh&&sameMaster(m,target))return;fresh=false;target=m;
 thresholdDb=m.threshold;slope=1-1/CompRatios[m.ratio];kneeDb=m.knee;mixAmount=m.mix*.01f;
 // AUTO makeup: half the reduction a full-scale signal would get (the whole of it over-boosts dense material).
 makeupDb=m.makeup?float(m.makeup-1):std::min(12.f,-thresholdDb*slope*.5f);
 attackCoef=coefficient(CompAttacksMs[m.attack]);
 autoRelease=!CompReleasesMs[m.release];
 releaseCoef=coefficient(autoRelease?60.f:float(CompReleasesMs[m.release]));
 autoSlowAttack=coefficient(400);autoSlowRelease=coefficient(1500);
 int hz=CompSidechainHz[m.sidechain];if(hz!=hpHz){sidechainCoefficients(hz);if(!hz){std::memset(hp.ic1,0,sizeof hp.ic1);std::memset(hp.ic2,0,sizeof hp.ic2);}}
}
float LifebloodMasterDsp::peakReduction(){float v=peakReductionDb;peakReductionDb=reductionDb;return v;}
void LifebloodMasterDsp::process(float*lr,int frames){
 const float eqTarget=target.eqOn,compTarget=target.compOn;
 // EQ: band gains glide 1 dB per 64 frames (coefficients redrawn only while moving).
 if(eqTarget>0||eqWet>0){
  for(int at=0;at<frames;at+=64){int n=std::min(64,frames-at);float*x=lr+at*2;
   bool active[MasterBands];
   for(int b=0;b<MasterBands;b++){float t=target.band[b];if(bandDb[b]!=t){bandDb[b]=t>bandDb[b]?std::min(t,bandDb[b]+1.f):std::max(t,bandDb[b]-1.f);bandCoefficients(b,bandDb[b]);}
    active[b]=bandDb[b]!=0;if(!active[b]){std::memset(eq[b].ic1,0,sizeof eq[b].ic1);std::memset(eq[b].ic2,0,sizeof eq[b].ic2);}}
   for(int i=0;i<n;i++){
    eqWet=eqTarget>eqWet?std::min(eqTarget,eqWet+StageStep):std::max(eqTarget,eqWet-StageStep);
    for(int c=0;c<2;c++){
     float dry=x[i*2+c],v=dry;
     for(int b=0;b<MasterBands;b++){if(!active[b])continue;auto&s=eq[b];
      float v3=v-s.ic2[c],v1=s.a1*s.ic1[c]+s.a2*v3,v2=s.ic2[c]+s.a2*s.ic1[c]+s.a3*v3;
      s.ic1[c]=2*v1-s.ic1[c];s.ic2[c]=2*v2-s.ic2[c];v=s.m0*v+s.m1*v1+s.m2*v2;}
     x[i*2+c]=dry+eqWet*(v-dry);
    }
   }
  }
 }
 if(compTarget<=0&&compWet<=0){reductionDb=peakReductionDb=0;peakFast=peakSlow=smoothed=0;return;}
 const float knee=kneeDb,lowEdge=thresholdDb-knee/2;
 const float quiet=std::pow(10.f,lowEdge/20);   // below the knee and nothing to release: no log needed
 for(int i=0;i<frames;i++){
  float l=lr[i*2],r=lr[i*2+1],dl=l,dr=r;
  if(hpHz){
   for(int c=0;c<2;c++){float v0=c?r:l,v3=v0-hp.ic2[c],v1=hp.a1*hp.ic1[c]+hp.a2*v3,v2=hp.ic2[c]+hp.a2*hp.ic1[c]+hp.a3*v3;
    hp.ic1[c]=2*v1-hp.ic1[c];hp.ic2[c]=2*v2-hp.ic2[c];(c?dr:dl)=v0+hp.m1*v1+hp.m2*v2;}
  }
  float peak=std::max(std::abs(dl),std::abs(dr)),want=0;
  if(peak>quiet){
   float x=6.0205999f*fastLog2(peak+1e-20f),over=x-thresholdDb;
   if(2*over>knee)want=over*slope;
   else if(knee>0&&2*over>-knee){float t=over+knee/2;want=slope*t*t/(2*knee);}
   want=std::min(want,40.f);
  }
  // Smooth decoupled peak detector on the reduction (dB).
  peakFast=std::max(want,releaseCoef*peakFast+(1-releaseCoef)*want);
  float held=peakFast;
  if(autoRelease){float a=want>peakSlow?autoSlowAttack:autoSlowRelease;peakSlow=a*peakSlow+(1-a)*want;held=std::max(held,peakSlow);}
  smoothed=attackCoef*smoothed+(1-attackCoef)*held;
  compWet=compTarget>compWet?std::min(compTarget,compWet+StageStep):std::max(compTarget,compWet-StageStep);
  float gain=fastExp2((makeupDb-smoothed)*0.16609640f),amount=compWet*mixAmount;
  lr[i*2]=l+amount*(l*gain-l);lr[i*2+1]=r+amount*(r*gain-r);
 }
 if(!std::isfinite(smoothed)){smoothed=peakFast=peakSlow=0;std::memset(hp.ic1,0,sizeof hp.ic1);std::memset(hp.ic2,0,sizeof hp.ic2);}
 reductionDb=smoothed*compWet;peakReductionDb=std::max(peakReductionDb,reductionDb);
}
}
