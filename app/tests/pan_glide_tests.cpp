// NOTE-column pan / volume: randomized only on rows with notes, held from a note's start until the
// next note, and changed without a step in the signal.
#include "audio.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
extern "C" int amalthea_obrador_family_index(uint32_t);
using namespace amalthea;
static void require(bool ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
  // The randomizer on the PAN tab writes only rows that hold a note.
  {
    Editor e;e.project=blankProject();auto &t=e.project.tracks[0];
    for(int r=0;r<16;r++)t.steps[r].note=r%4==0?48:r%4==1?-3:r%4==2?-2:-1;
    e.column=0;e.eventTab=2+NotePan;
    e.randomizeEventParameter(true);
    int set=0;
    for(int r=0;r<16;r++){
      if(t.steps[r].note>=0)set+=t.steps[r].locks[NotePan]!=Inherit;
      else require(t.steps[r].locks[NotePan]==Inherit,"rows without a note (empty, tie, note-off) keep no pan");
    }
    require(set==4,"every row with a note gets a pan");
  }
  // A held note panned hard left, the next row (no note) carrying hard right, then a silent note
  // hard right: the held note does not move until the next note, and then its gain glides out
  // (5 ms) instead of stepping.
  {
    uint32_t seed=1;while(amalthea_obrador_family_index(seed)!=19)seed++;   // SLOW SWELL: a release of seconds
    auto render=[&](bool second){
      Project p=blankProject();for(auto &pt:p.patches)pt={uint8_t(ObradorEngine),seed};
      for(int t=1;t<Tracks;t++)p.tracks[t].mute=true;
      auto &t=p.tracks[0];t.length=4;
      t.steps[0].note=48;t.steps[0].instrument=0;t.steps[0].length=4;t.steps[0].locks[NotePan]=0;
      t.steps[1].locks[NotePan]=100;            // an empty row with a pan: must not move the note
      if(second){t.steps[2].note=48;t.steps[2].instrument=0;t.steps[2].locks[NotePan]=100;t.steps[2].locks[NoteVolume]=0;}
      Audio a(48);a.submit(p,true);std::vector<float> x(5512*4*2);a.render(x.data(),5512*4);return x;};
    const int step=5512;   // 120 BPM, sixteenths
    auto x=render(true),ref=render(false);
    double right=0,left=0;
    for(int i=step;i<2*step-200;i++){left+=std::fabs(x[2*i]);right+=std::fabs(x[2*i+1]);}
    require(right<left*1e-3,"the empty row's pan does not move the sounding note");
    // The left gain after the change: |x| / |ref| over 1 ms windows (the waveform cancels).
    // Find the change: the first window where they differ.
    int at=-1;for(int i=step;i<3*step&&at<0;i++)if(x[2*i]!=ref[2*i])at=i;
    require(at>0,"the second note changes the mix");
    auto gain=[&](int k){double a=0,b=0;for(int i=at+k*44;i<at+(k+1)*44;i++){a+=std::fabs(x[2*i]);b+=std::fabs(ref[2*i]);}return a/(b+1e-12);};
    const double g0=gain(0),g1=gain(1),g10=gain(10);
    require(g0>.7&&g1>.5,"a hard pan change glides (no step)");
    require(g10==0,"and is complete within 10 ms");
    printf("pan: empty rows untouched; held note keeps its pan; change glides (gain %.2f, %.2f after 1-2 ms, %.2f at 10 ms)\n",g0,g1,g10);
  }
}
