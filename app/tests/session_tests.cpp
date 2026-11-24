#include "audio.hpp"
#include "effects.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace amalthea;
static void require(bool ok,const char *s) { if(!ok){fprintf(stderr,"FAIL: %s\n",s);std::exit(1);} }
int main() {
  Editor e; e.project=demoProject();e.column=2;
  e.action(Action::Enter);e.action(Action::Right);
  require(e.pending.fx[0].type==FxType::Wavefold && e.effectBanner,"right chooses effect and announces it");
  e.action(Action::Up);e.action(Action::Up);e.action(Action::Left);
  require(e.pending.fx[0].type==FxType::None && e.pending.fx[0].value==DefaultFxMix+2,"a new effect starts half wet; left chooses type without destroying the mix");
  e.action(Action::Left);
  require(e.pending.fx[0].type==FxType::Random,"effect selection wraps (RANDOM is last)");
  for(int i=0;i<150;i++)e.action(Action::Up);
  require(e.pending.fx[0].value==100,"percentage upper bound");
  e.action(Action::Cancel);
  require(e.project.tracks[0].steps[0].fx[0].type==FxType::None,"cancel does not apply preview");
  e.action(Action::Session);e.action(Action::Enter);e.action(Action::Enter);e.action(Action::Right);e.action(Action::Down);e.action(Action::Enter);e.action(Action::Right);
  require(e.project.bpm==121 && e.project.scale==2,"session tempo and scale controls");
  e.action(Action::Down);e.action(Action::Enter);e.action(Action::Right);
  require(e.project.root==1,"session root control");
  e.action(Action::Master);auto tr=e.track;e.action(Action::NextTrack);
  require(e.masterModule==3 && e.track==tr,"master module navigation preserves track");
  e.action(Action::Right);require(e.project.master.macros[1].value[0]==66,"master percentage control");
  e.action(Action::ShiftDown);e.action(Action::ShiftUp);require(!e.shift,"modifier releases on settings pages");
  e.action(Action::Play);require(e.playing,"transport works on settings page");
  e.action(Action::Play);e.action(Action::Cancel);e.column=1;
  for(int engine=0;engine<Instruments;engine++) {
    // X redraws the TRACK SETTINGS instrument, whatever the column held, and writes it there.
    e.project.tracks[0].instrument=engine;e.project.tracks[0].steps[0].instrument=(engine+1)%Instruments;
    for(int iteration=0;iteration<8;iteration++) {
      auto prior=e.project;
      e.action(Action::RandomizeColumn);
      require(trackPatch(e.project,0,engine).engine==trackPatch(prior,0,engine).engine,"X retains selected engine");
      require(trackPatch(e.project,0,engine).seed!=trackPatch(prior,0,engine).seed,"X draws a fresh family patch");
      require(e.project.tracks[0].steps[0].instrument==engine,"X writes the track instrument into the column");
      require(e.notice=="INSTRUMENT "+std::to_string(engine+1)+" RANDOMIZED"&&e.noticeDetailSerial==e.noticeSerial,"X shows the instrument overlay");
      for(int row=0;row<16;row++)require(e.project.tracks[0].steps[row].instrument==engine,"patch draw writes the track instrument on every row");
      Project p;p.patches[engine]=trackPatch(e.project,0,engine);p.tracks[0].steps[0].instrument=engine;p.tracks[0].steps[0].note=engine==6?36:60;
      Audio a,b;a.submit(p,true);b.submit(p,true);
      // Caligula can place its complete output behind an internal delay.
      int frames=engine>=6?88200:22050;
      std::vector<float> x(frames*2),y(x.size());a.render(x.data(),frames);b.render(y.data(),frames);
      require(x==y,"seeded patch audio repeats");
      double power=0;for(float v:x){require(std::isfinite(v)&&std::abs(v)<.951f,"family render finite and limited");power+=v*v;}
      if(power<=1e-10)fprintf(stderr,"silent engine=%d iteration=%d seed=%u\n",engine,iteration,p.patches[engine].seed);
      require(power>1e-10,"family patch produces audio");
    }
  }
  auto patchBefore=e.project.patches;
  auto defaultBefore=e.project.tracks[e.track].instrument;
  e.action(Action::ShiftDown);bool crossed=false;
  for(int i=0;i<16;i++) {
    e.action(Action::RandomizeColumn);
    for(int r=0;r<16;r++)crossed|=e.project.tracks[e.track].steps[r].instrument!=7;
  }
  require(crossed,"Shift X assigns different step instrument slots");
  require(e.project.tracks[e.track].instrument==defaultBefore,"Shift X preserves track default");
  for(int i=0;i<Instruments;i++)require(e.project.patches[i].engine==patchBefore[i].engine&&e.project.patches[i].seed==patchBefore[i].seed,"Shift X leaves patches unchanged");
  e.action(Action::ShiftUp);
  std::string error;require(saveProject(e.project,"session-v3.amalthea",error),"save v3");Project loaded;
  require(loadProject(loaded,"session-v3.amalthea",error),"load v3");
  require(loaded.root==1&&loaded.scale==2&&loaded.bpm==121&&loaded.master.macros[1].value[0]==66&&loaded.patches[7].seed==e.project.patches[7].seed,"v3 preserves session master and patch state");remove("session-v3.amalthea");
  Project p=demoProject();Audio all;all.submit(p,true);std::vector<float> x(88200*2);all.render(x.data(),88200);
  for(int t=0;t<Tracks;t++)require(!p.tracks[t].mute&&all.stats.triggers[t].load()==4,"every starter track plays");
  for(int module=7;module<MasterModules-1;module++) {
    MasterEffects dry,wet;MasterSettings a,b;a.amount.fill(0);a.amount[10]=100;a.extra[7][0]=0;b=a;b.amount[module]=85;
    if(module==7){b.amount[7]=20;b.extra[7][0]=80;}
    dry.configure(a,120);wet.configure(b,120);double difference=0;
    for(int n=0;n<44100;n++) {
      float l=.65f*std::sin(n*.071f),r=.6f*std::sin(n*.093f),dl=l,dr=r;
      dry.process(dl,dr,0,0,module==9?dl*.8f:0,module==9?dr*.8f:0);wet.process(l,r,0,0,module==9?l*.8f:0,module==9?r*.8f:0);
      require(std::isfinite(l)&&std::isfinite(r)&&std::abs(l)<=.951f&&std::abs(r)<=.951f,"master finite and limited");
      difference+=std::abs(l-dl)+std::abs(r-dr);
    }
    require(difference>1,"each master module changes audio");
  }
  puts("PASS: direct FX editing, session/master controls, family patch audio, all tracks, v3 persistence");
}
