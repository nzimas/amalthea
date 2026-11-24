#include "audio.hpp"
#include "effects.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace amalthea;
static void require(bool x,const char *why) {if(!x){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main() {
  SessionButton button;
  button.press(0);require(button.tick(799)==Action::Reserved,"hold threshold not early");
  require(button.release(799)==Action::Session,"tap opens session only on release");
  button.press(1000);require(button.tick(1800)==Action::Save,"800ms holds save");
  require(button.tick(2400)==Action::Reserved && button.release(2600)==Action::Reserved,"hold fires once and suppresses tap");
  button.press(3000);require(button.release(3900)==Action::Save,"late release still saves without a timer tick");
  Editor e;e.project=blankProject();
  for(auto &t:e.project.tracks)for(auto &s:t.steps)require(s.note==-1 && s.fx[0].type==FxType::None && s.fx[1].type==FxType::None,"blank startup has no events or effects");
  e.project=demoProject();e.track=2;e.column=4;e.project.tracks[2].pan=65;e.project.tracks[2].level=12;e.project.tracks[2].mute=true;
  e.project.tracks[2].steps[200].fx[0]={FxType::Fuzz,88};e.project.tracks[2].steps[0].condition={2,3};e.project.tracks[2].steps[0].modifier={7,false};
  auto before=e.project;
  e.action(Action::Enter);e.action(Action::ShiftDown);e.action(Action::TrackSettings);e.action(Action::ShiftUp);
  auto &t=e.project.tracks[2];require(!e.editing&&t.level==90&&t.pan==0&&!t.mute,"clear resets track mixing and editing");
  for(auto &s:t.steps)require(s.note==-1&&s.instrument==NoInstrument&&s.fx[0].type==FxType::None&&s.fx[1].type==FxType::None&&s.condition.every==1&&s.condition.occurrence==1&&s.modifier.semitones==0&&s.modifier.conditionalOnly,"clear resets all 256 stored steps");
  for(int tr=0;tr<Tracks;tr++)if(tr!=2)require(std::memcmp(&before.tracks[tr],&e.project.tracks[tr],sizeof(Track))==0,"clear leaves other tracks unchanged");
  e.action(Action::Undo);require(std::memcmp(&before.tracks[2],&e.project.tracks[2],sizeof(Track))==0,"single undo restores entire track");
  e.page=Page::Master;e.masterModule=8;
  auto time=e.project.master.value(8,1);e.action(Action::Down);e.action(Action::Right);
  require(e.masterParameter==1&&e.project.master.value(8,1)==time+1,"up/down selects parameter and left/right edits it");
  std::string error;require(saveProject(e.project,"roundtrip-v4.amalthea",error),"save expanded parameters");Project loaded;
  require(loadProject(loaded,"roundtrip-v4.amalthea",error)&&loaded.master.value(8,1)==time+1,"v4 restores parameters");remove("roundtrip-v4.amalthea");
  {
    // SIDECHAIN: the source's hits duck the rest of the mix; the source itself passes unducked.
    MasterSettings s;s.amount.fill(0);s.amount[10]=100;s.amount[7]=10;s.extra[7][0]=90;s.sidechain={1,0};s.busMix[3]=0;
    MasterEffects keyed,plain;keyed.configure(s,120);auto off=s;off.sidechain={0,0};plain.configure(off,120);
    double duckedHit=0,openGap=0,plainHit=0;
    for(int n=0;n<44100;n++){
      bool hit=n%11025<2205;float kick=hit?.8f*std::sin(n*.02f):0.f,pad=.2f*std::sin(n*.31f);
      float l=pad+kick,r=pad+kick,pl=pad,pr=pad;keyed.process(l,r,0,0,0,0,kick,kick);plain.process(pl,pr);
      float rest=l-kick;if(n%11025>1000&&n%11025<2205)duckedHit+=std::abs(rest);if(n%11025>9000)openGap+=std::abs(rest);if(n%11025>1000&&n%11025<2205)plainHit+=std::abs(pl);
    }
    require(duckedHit<plainHit*.6&&openGap>duckedHit*1.5,"sidechain ducks the mix on the source's hits and lets it open between them");
    require(masterValueText(7,5,1)=="ON"&&masterValueText(7,6,3)=="TRACK 4","sidechain settings read as ON/OFF and a track");
  }
  for(int module=7;module<MasterModules;module++)for(int parameter=0;parameter<masterParameterCount(module);parameter++) {
    if(module==7&&parameter>=5)continue;   // SIDECHAIN / SOURCE: switches, tested above with a key signal
    MasterSettings a,b;a.amount.fill(0);a.amount[10]=100;a.extra[7][0]=0;
    if(module<7)a.amount[module]=65;
    if(module==7){a.amount[7]=30;a.extra[7][0]=65;}
    if(module==8||module==9)a.amount[module]=80;
    a.value(module,parameter)=15;b=a;b.value(module,parameter)=85;
    MasterEffects low,high;low.configure(a,120);high.configure(b,120);
    double diff=0;
    for(int n=0;n<88200;n++) {
      float envelope=n%11025<2205?1.f:.03f;
      float l=envelope*(.7f*std::sin(n*.047f)+.6f*std::sin(n*.421f));
      float r=envelope*(.6f*std::sin(n*.063f)+.6f*std::sin(n*.313f));
      float ll=l,rr=r;low.process(l,r,0,0,module==9?l*.8f:0,module==9?r*.8f:0);high.process(ll,rr,0,0,module==9?ll*.8f:0,module==9?rr*.8f:0);
      require(std::isfinite(l)&&std::isfinite(r)&&std::isfinite(ll)&&std::isfinite(rr),"all parameter settings finite");
      require(std::abs(l)<=.951f&&std::abs(r)<=.951f&&std::abs(ll)<=.951f&&std::abs(rr)<=.951f,"output limiter bounds every parameter");
      diff+=std::abs(l-ll)+std::abs(r-rr);
    }
    if(diff<=.0001){fprintf(stderr,"NO DSP CHANGE: %s / %s diff=%g\n",masterName(module),masterParameterName(module,parameter),diff);return 1;}
  }
  // Clearing a running track must cut its still-active voices too.
  Project playing;playing.tracks[0].steps[0].note=60;Audio audio;audio.submit(playing,true);
  std::vector<float> output(4096*2);audio.render(output.data(),4096);
  playing.tracks[0]=Track{};audio.submit(playing,true);audio.render(output.data(),4096);audio.render(output.data(),4096);
  float peak=0;for(float v:output)peak=std::max(peak,std::abs(v));require(peak<1e-8f,"cleared track stops sounding");
  puts("PASS: hold/tap, blank startup, complete undoable clear, all master effect controls affect DSP, v4 persistence");
}
