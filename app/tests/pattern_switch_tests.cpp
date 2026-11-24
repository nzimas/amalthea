// Pattern switching while playing waits for the quantized bar; the session die rolls the
// scale and root note; the scale list after Tonitor and Bigorna.
#include "audio.hpp"
#include "sessions.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <unistd.h>
#include <vector>
using namespace amalthea;
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
// The UI loop's protocol (main.cpp): the deferred next pattern first, then the editor's own state;
// the editor takes the queued pattern once the audio has swapped it in.
static void sync(Editor &e,Audio &a){
  if(e.queuedPattern>=0&&e.queueSubmitted&&a.stats.patternGeneration.load()==e.queuedGeneration)e.adoptQueuedPattern();
  if(e.queuedPattern>=0&&!e.queueSubmitted)
    e.queueSubmitted=a.submit(*e.queuedProject,true,-1,nullptr,&e.device,false,PatternSync{e.queuedGeneration,true,false,e.sessionDefaults.quantize});
  a.submit(e.project,e.playing,-1,nullptr,&e.device,false,PatternSync{e.patternGeneration,false,e.queuedPattern>=0,e.sessionDefaults.quantize});
}
// Two patterns: track 1 a 16-step bar at 1X, track 2 four steps at 1/8 (two bars a loop).
static Editor twoPatterns(){
  Editor e;e.project=blankProject();
  auto &fast=e.project.tracks[0],&slow=e.project.tracks[1];
  fast.length=16;fast.clock=3;slow.length=4;slow.clock=0;
  for(int r=0;r<16;r+=4)fast.steps[r].note=60;slow.steps[0].note=48;
  require(e.savePattern(0),"save pattern 1");
  e.project.tracks[0].steps[0].note=72;require(e.savePattern(1),"save pattern 2");
  require(e.loadPattern(0),"load pattern 1");
  return e;
}
// Frames from the start of playback until the queued pattern (queued at frame `at`) sounds.
static long switchFrame(uint8_t quantize,long at){
  Editor e=twoPatterns();e.sessionDefaults.quantize=quantize;e.playing=true;
  Audio a;sync(e,a);std::vector<float> block(32*2);
  for(long frame=0;frame<400000;frame+=32){
    if(frame==at){require(e.queuePattern(1),"queue pattern 2");require(e.queuedPattern==1&&e.activePattern==0,"queued while playing: the current pattern keeps playing");}
    sync(e,a);a.render(block.data(),32);sync(e,a);
    if(e.queuedPattern<0&&frame>=at){
      require(e.activePattern==1&&e.project.tracks[0].steps[0].note==72,"the editor takes the swapped-in pattern");
      require(a.stats.columnRow[0][0].load()==0&&a.stats.columnRow[1][0].load()==0,"every track restarts on its first step");
      return frame;
    }
  }
  return -1;
}
int main(){
  const double step=44100.*60/(120*4);   // 1X at 120 BPM
  // LONGEST: the slow track's two-bar loop ends; SHORTEST: the 16-step bar ends.
  long longest=switchFrame(QuantizeLongest,20000),shortest=switchFrame(QuantizeShortest,20000);
  printf("longest %ld (loop %.0f), shortest %ld (bar %.0f)\n",longest,32*step,shortest,16*step);
  require(longest>0&&std::abs(longest-32*step)<=64,"LONGEST waits for the end of the longest track's loop");
  require(shortest>0&&std::abs(shortest-16*step)<=64,"SHORTEST switches at the end of the shortest track's loop");
  // Queued late in the first bar: SHORTEST waits for that bar, not the next.
  long late=switchFrame(QuantizeShortest,80000);
  require(std::abs(late-16*step)<=64,"a pattern queued in a bar switches at the end of that bar");
  // Queued in the second bar of the slow loop: LONGEST still ends with that loop.
  long second=switchFrame(QuantizeLongest,120000);
  require(std::abs(second-32*step)<=64,"LONGEST ends with the current loop");

  // Stopped: a pattern loads at once; stopping with a pattern queued loads it at once.
  {Editor e=twoPatterns();require(e.queuePattern(1)&&e.queuedPattern<0&&e.activePattern==1,"stopped: immediate load");}
  {Editor e=twoPatterns();e.playing=true;require(e.queuePattern(1)&&e.queuedPattern==1,"playing: queued");
   e.action(Action::Play);require(!e.playing&&e.queuedPattern<0&&e.activePattern==1,"stop loads the queued pattern");}
  {Editor e=twoPatterns();e.playing=true;require(!e.queuePattern(7)&&e.queuedPattern<0,"an empty slot never queues");}
  // A second choice before the bar replaces the first.
  {Editor e=twoPatterns();e.project.tracks[0].steps[0].note=50;require(e.savePattern(2),"save pattern 3");require(e.loadPattern(0),"back to 1");
   e.playing=true;Audio a;sync(e,a);std::vector<float> block(32*2);
   for(long frame=0;frame<200000&&e.activePattern==0;frame+=32){
     if(frame==10016)e.queuePattern(1);
     if(frame==40000)e.queuePattern(2);
     sync(e,a);a.render(block.data(),32);
   }
   sync(e,a);require(e.activePattern==2&&e.project.tracks[0].steps[0].note==50,"the latest queued pattern wins");}

  // SESSION settings: four fields, the quantize mode, the die beside SCALE and ROOT NOTE.
  {Editor e;e.project=blankProject();e.page=Page::Session;e.sessionField=3;
   require(e.sessionDefaults.quantize==QuantizeLongest,"LONGEST by default");
   e.action(Action::Right);require(e.sessionDefaults.quantize==QuantizeLongest,"not in edit mode: RIGHT changes nothing");
   e.action(Action::Enter);require(e.sessionEdit,"START: edit mode");
   e.action(Action::Right);require(e.sessionDefaults.quantize==QuantizeShortest,"SWITCH QUANTIZE: SHORTEST");
   e.action(Action::Right);require(e.sessionDefaults.quantize==QuantizeLongest,"SWITCH QUANTIZE wraps");
   std::string name,value;require(e.focusedSetting(name,value)&&name=="SWITCH QUANTIZE"&&value=="LONGEST TRACK","quantize popup");
   e.action(Action::Cancel);require(!e.sessionEdit&&e.page==Page::Session,"A leaves edit mode, not the page");
   e.action(Action::Enter);e.action(Action::Enter);require(!e.sessionEdit,"START again leaves edit mode");
   // The die: RIGHT from SCALE or ROOT NOTE, LEFT back to that row; UP / DOWN leave it.
   e.sessionField=2;e.action(Action::Right);require(e.sessionField==SessionDie,"RIGHT from ROOT NOTE reaches the die");
   e.action(Action::Left);require(e.sessionField==2,"LEFT returns to ROOT NOTE");
   e.action(Action::Up);e.action(Action::Right);require(e.sessionField==SessionDie,"RIGHT from SCALE reaches the die");
   e.action(Action::Up);require(e.sessionField==0,"UP from the die: TEMPO");
   e.sessionField=SessionDie;e.action(Action::Down);require(e.sessionField==3,"DOWN from the die: SWITCH QUANTIZE");
   e.action(Action::Right);require(e.sessionField==3,"no die beside SWITCH QUANTIZE");
   require(e.sessionDefaults.scale==1&&e.sessionDefaults.root==0,"navigation never rolls");
   e.sessionField=1;e.action(Action::Enter);require(e.sessionEdit&&e.sessionDefaults.scale==1,"START on SCALE edits, never rolls");e.action(Action::Enter);
   std::set<int> scales,roots;
   for(int roll=0;roll<200;roll++){
     e.sessionField=roll%3?SessionDie:roll%2?1:2;auto before=e.sessionDefaults;
     e.action(roll%3?Action::Enter:Action::RandomizeColumn);
     require(e.sessionField==(roll%3?SessionDie:roll%2?1:2)&&!e.sessionEdit,"rolling keeps the place");
     require(e.sessionDefaults.scale!=before.scale&&e.sessionDefaults.root!=before.root,"each roll changes scale and root");
     require(e.sessionDefaults.scale>0&&e.sessionDefaults.scale<Scales,"never CHROMATIC");
     require(e.project.scale==e.sessionDefaults.scale&&e.project.root==e.sessionDefaults.root,"the roll applies to the session");
     scales.insert(e.sessionDefaults.scale);roots.insert(e.sessionDefaults.root);
   }
   require(int(scales.size())>Scales/2&&roots.size()==12,"rolls spread over the scales and every root");
   auto rolled=e.sessionDefaults;e.undo();require(e.sessionDefaults.scale!=rolled.scale||e.sessionDefaults.root!=rolled.root,"a roll is undoable");}

  // Scales: the first seven keep their notes; every scale holds its root and is distinct.
  {std::set<uint16_t> masks;std::set<std::string> names;
   for(int i=0;i<Scales;i++){require(scaleMask(i)&1,"every scale holds its root");masks.insert(scaleMask(i));names.insert(scaleName(i));}
   require(int(masks.size())==Scales&&int(names.size())==Scales,"every scale distinct");
   require(Scales>=30,"a large scale list");
   require(std::string(scaleName(17))=="DOUBLE HARMONIC"&&scaleMask(14)==0x555,"Tonitor scales present");}

  // The session file keeps the quantize mode and every new scale.
  {auto dir=std::filesystem::temp_directory_path()/("amalthea-switch-"+std::to_string(getpid()));std::filesystem::create_directories(dir);
   SessionBank bank((dir/"session.amalthea").string());std::string error;
   Editor e=twoPatterns();e.sessionDefaults.quantize=QuantizeShortest;e.sessionDefaults.scale=Scales-1;e.applyMusicalSettings();
   e.patternOverrides.scale=Scales-2;require(e.savePattern(3),"pattern with a new scale");
   require(bank.save(0,e,error),"save session");Editor loaded;require(bank.load(0,loaded,error),error.c_str());
   require(loaded.sessionDefaults.quantize==QuantizeShortest&&loaded.sessionDefaults.scale==Scales-1,"quantize and scale round-trip");
   require(loaded.patterns[3].overrides.scale==Scales-2,"pattern scale override round-trips");
   std::filesystem::remove_all(dir);}
  puts("PASS: bar-quantized pattern switching, session die, scales");
}
