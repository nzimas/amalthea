#include "ui.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace amalthea;
static void require(bool ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
  Editor e;e.project=blankProject();e.track=3;e.row=2;e.column=4;
  auto &t=e.project.tracks[3];t.length=5;t.instrument=2;
  t.steps[0].note=60;t.steps[1].note=-1;t.steps[2].note=-2;t.steps[3].note=-3;t.steps[4].note=67;
  t.steps[4].fx[0]={FxType::Delay,42};t.steps[4].condition={2,3};t.steps[4].modifier={5,false};t.steps[8].note=70;
  auto original=e.project;
  e.action(Action::ShiftDown);e.action(Action::Down);
  require(t.steps[0].note==48&&t.steps[4].note==55,"whole active sequence transposes down exactly twelve semitones");
  require(t.steps[1].note==-1&&t.steps[2].note==-2&&t.steps[3].note==-3&&t.steps[8].note==70,"special notes and hidden rows unchanged");
  require(e.row==2&&e.column==4&&e.track==3,"octave gesture leaves cursor fixed");
  require(t.steps[4].fx[0].value==42&&t.steps[4].condition.every==3&&t.steps[4].modifier.semitones==5,"transpose preserves locks, conditions and modifiers");
  for(int tr=0;tr<Tracks;tr++)if(tr!=3)require(std::memcmp(&original.tracks[tr],&e.project.tracks[tr],sizeof(Track))==0,"other tracks unchanged");
  e.action(Action::Down);require(t.steps[0].note==36,"repeated presses transpose further octaves");
  e.action(Action::Up);e.action(Action::Up);require(t.steps[0].note==60&&t.steps[4].note==67,"up reverses down without changing intervals");
  e.action(Action::Undo);require(t.steps[0].note==48,"one undo per octave gesture");
  e.action(Action::Redo);require(t.steps[0].note==60,"octave redo");
  t.steps[0].note=6;auto revisions=e.revision;e.action(Action::Down);
  require(t.steps[0].note==6&&t.steps[4].note==67&&e.revision==revisions,"range boundary rejects whole transposition without clipping intervals");
  t.steps[0].note=115;e.action(Action::Up);require(t.steps[0].note==115&&t.steps[4].note==67,"upper range guard");
  t.steps[0].note=60;e.editing=true;e.pending=t.steps[e.row];e.pending.note=72;e.action(Action::Up);
  require(e.editing&&e.pending.note==84&&t.steps[0].note==72,"pending editor follows whole-track transposition");
  e.action(Action::ShiftUp);e.action(Action::Cancel);e.column=1;e.density[e.track][1]=100;
  auto before=e.project;e.action(Action::ShiftDown);e.action(Action::RandomizeColumn);
  bool different=false;for(int r=0;r<t.length;r++){
    require(t.steps[r].instrument<Instruments,"random step instrument is valid");
    different|=t.steps[r].instrument!=t.instrument;
    require(t.steps[r].note==before.tracks[3].steps[r].note,"instrument randomizer preserves pitches");
  }
  require(different&&t.instrument==2,"per-step slots override but do not change track default");
  for(int slot=0;slot<Instruments;slot++)require(e.project.patches[slot].engine==before.patches[slot].engine&&e.project.patches[slot].seed==before.patches[slot].seed,"Shift X does not alter patches");
  require(t.steps[8].instrument==before.tracks[3].steps[8].instrument,"instrument randomizer preserves hidden rows");
  e.action(Action::Undo);require(std::memcmp(&t,&before.tracks[3],sizeof t)==0,"single undo restores all instrument cells");
  AudioStats stats;
  for(int width:{640,1024})for(int rows:{8,16})for(int tr=0;tr<Tracks;tr++){
    Editor view;view.track=tr;view.row=1;view.column=0;view.visibleRows=rows;view.playing=true;
    stats.trackRow[tr].store(2);stats.row.store(0);
    Canvas first(width,width*3/4),second(width,width*3/4);
    drawUI(first,view,stats);stats.trackRow[tr].store(3);drawUI(second,view,stats);
    auto at=[&](const Canvas &c,int x,int y){return c.pixels[int(y*width/1024.f)*width+int(x*width/1024.f)]&0xffffff;};
    int height=rows==8?72:36;
    auto dim=at(first,390,164+2*height+3),accent=trackColor(tr);
    require(dim!=0&&dim!=accent,"playhead row has a dim track-color background");
    require(at(first,994,164+2*height+3)==dim,"playhead spans all six columns");
    require(at(second,390,164+2*height+3)==0&&at(second,390,164+3*height+3)==dim,"highlight moves and old row clears");
    require(at(first,95,164+height+3)==accent&&at(second,95,164+height+3)==accent&&view.row==1,"bright editing cursor remains stationary");
    view.playing=false;drawUI(second,view,stats);require(at(second,390,164+3*height+3)==0,"stopped transport has no moving row highlight");
  }
  puts("PASS: octave gestures, instrument-cell randomization and stationary-cursor playhead");
}
