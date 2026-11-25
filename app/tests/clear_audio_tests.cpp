#include "audio.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace amalthea;
static void require(bool ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
  const FxType histories[]={FxType::Stutter,FxType::Lowpass,FxType::Tremolo,FxType::Distortion,FxType::Delay,FxType::Reverb,FxType::Volume,FxType::Redux,FxType::Abrasion,FxType::Corrosion,FxType::Modulation};
  for(auto effect:histories)for(uint32_t seed:{0u,1234u}){
    Editor e;e.project=blankProject();e.project.patches[0].seed=seed;
    auto &track=e.project.tracks[0];
    for(int r=0;r<16;r++){
      track.steps[r].note=60+r%5;track.steps[r].condition={1,3};track.steps[r].modifier={7,ModifierType::Offset};
      track.steps[r].fx={Effect{effect,uint8_t(effect==FxType::Volume?1:100)},Effect{FxType::Lowpass,100}};
    }
    Audio used;std::vector<float> history(176384*2),reused(16384*2),fresh(16384*2);
    used.submit(e.project,true);used.render(history.data(),176384);
    e.action(Action::ShiftDown);e.action(Action::TrackSettings);e.action(Action::ShiftUp);
    require(e.project.trackReset[0]==1,"clear notifies audio even without a transport restart");
    used.submit(e.project,true);used.render(history.data(),1024);
    for(int i=0;i<2048;i++)require(history[i]==0,"clearing last track removes its voice, insert and bus history");
    e.notice.clear();e.column=0;e.action(Action::RandomizeColumn);
    e.column=1;e.action(Action::RandomizeColumn);
    used.submit(e.project,true);used.render(reused.data(),16384);
    Audio clean;clean.submit(e.project,true);clean.render(fresh.data(),16384);
    require(reused==fresh,"rebuilt VAP envelopes and level match a fresh engine sample for sample");
    double energy=0;for(float x:reused)energy+=x*x;
    require(energy>1e-8&&used.stats.trackCycle[0].load()==0,"new patches are audible in the first rebuilt cycle");
  }
  // Reset one track without resetting another track's clock or notes.
  Editor e;e.project=demoProject();Audio audio;std::vector<float> out(16384*2);
  audio.submit(e.project,true);audio.render(out.data(),16384);
  auto advances=audio.stats.advances[1].load(),triggers=audio.stats.triggers[1].load();
  e.action(Action::ShiftDown);e.action(Action::TrackSettings);e.action(Action::ShiftUp);
  audio.submit(e.project,true);audio.render(out.data(),16384);
  require(audio.stats.advances[1].load()>advances&&audio.stats.triggers[1].load()>triggers,"clearing one track leaves other tracks running");
  // Clearing an already empty track still resets its local cycle phase.
  e.project=blankProject();audio.submit(e.project,true);audio.render(out.data(),16384);audio.render(out.data(),16384);
  require(audio.stats.advances[0].load()>1,"empty track clock has advanced");
  e.action(Action::ShiftDown);e.action(Action::TrackSettings);e.action(Action::ShiftUp);
  audio.submit(e.project,true);audio.render(out.data(),32);
  require(audio.stats.advances[0].load()==1&&audio.stats.trackCycle[0].load()==0,"explicit clear resets an already empty track");
  puts("PASS: clear/rebuild VAP matches fresh audio after all effect histories; unrelated tracks continue");
}
