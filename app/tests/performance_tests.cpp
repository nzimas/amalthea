// PERFORMANCE mode: Y enters, F1 leaves, X / B / A toggle a chain, its parameters and its
// LFOs, and nothing of it ever reaches the project.
#include "audio.hpp"
#include "lifeblood.hpp"
#include "performance.hpp"
#include "ui.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>
using namespace amalthea;
extern "C" int amalthea_patch_movable(int,const int32_t*,uint32_t);
static bool amalthea_patch_movable_for_test(int e,const int32_t*v,uint32_t j){return amalthea_patch_movable(e,v,j)!=0;}
static void check(bool b,const char*m){if(!b){fprintf(stderr,"FAIL: %s\n",m);exit(1);}}
static Project busy(){
  Project p=blankProject();p.master.amount[9]=0;
  for(int t:{0,3,7}){auto&k=p.tracks[t];k.instrument=0;p.patches[size_t(t*Instruments)]={0,0};for(int r=0;r<16;r++)if(r%2==0)k.steps[r].note=48+r%7;}
  return p;
}
int main(int argc,char**argv){
  // Y enters from the tracker, SHIFT + Y opens the master effects, Y there goes back.
  {Editor e;e.project=busy();auto stored=encodeProject(e.project);
   e.action(Action::Master);check(e.page==Page::Performance&&e.performance,"Y enters PERFORMANCE");
   e.action(Action::PrevTrack);check(e.page==Page::Pattern,"F1 leaves");
   e.action(Action::ShiftDown);e.action(Action::Master);e.action(Action::ShiftUp);check(e.page==Page::Master,"SHIFT + Y: master effects");
   e.action(Action::Master);check(e.page==Page::Pattern,"Y on the master effects returns to the pattern");
   check(encodeProject(e.project)==stored,"nothing written");}
  // X / B / A toggles; F1 drops everything; the project never changes.
  {Editor e;e.project=busy();e.entropy=1234;auto stored=encodeProject(e.project);
   e.action(Action::Master);auto&p=*e.performance;
   e.action(Action::RandomizerOptions);
   check(p.chainOn&&p.stages>=1&&p.stages<=PerfStages&&p.targets>=1&&p.targets<=PerfTargets,"X: a chain on 1 .. 3 tracks");
   std::set<int> holding{0,3,7};int cost=0;for(int i=0;i<p.stages;i++)cost+=lifebloodCost(p.effect[size_t(i)].type);
   check(cost*p.targets<=PerfBudget,"the chain fits the Brick budget on every target");
   for(int k=0;k<p.targets;k++){auto&t=p.target[size_t(k)];check(holding.count(t.track)&&t.length>=1&&t.length<=16&&t.start<16,"targets are tracks holding notes, on a stretch of rows");}
   auto generation=p.generation;
   e.action(Action::RandomizerOptions);check(!p.chainOn&&p.generation==generation,"X again bypasses");
   e.action(Action::RandomizeColumn);check(p.chainOn&&p.generation==generation+1,"X again draws a new chain (hold X counts too)");
   auto base=p.base;
   e.action(Action::TrackSettings);check(p.paramsOn,"B: new parameters");
   auto frame=perfFrame(p,0);bool differs=false;for(int i=0;i<p.stages;i++)differs|=memcmp(&frame.settings[size_t(i)],&base[size_t(i)],sizeof base[0])!=0;
   check(differs,"B's draw differs from the chain's own");
   e.action(Action::TrackSettings);{auto f=perfFrame(p,0);check(!p.paramsOn&&!memcmp(&f.settings,&base,sizeof base),"B again restores the chain's own");}
   e.action(Action::ColumnSettings);check(p.fxLfoOn,"A: LFOs on the chain");
   int lfos=0;for(auto&l:p.lfo)lfos+=l.stage>=0;check(lfos>=1&&lfos<=PerfLfoCount,"up to six LFOs");
   bool moved=false;auto first=perfFrame(p,0);
   for(int n=0;n<200;n++){auto f=perfFrame(p,.02);moved|=memcmp(&f.settings,&first.settings,sizeof first.settings)!=0;}
   check(moved,"the LFOs move the chain's parameters");
   e.action(Action::ColumnSettings);{auto f=perfFrame(p,.02);check(!p.fxLfoOn&&!memcmp(&f.settings,&base,sizeof base),"A again stops them");}
   e.action(Action::Up);e.action(Action::Enter);e.action(Action::Undo);
   check(e.page==Page::Performance&&encodeProject(e.project)==stored,"other keys do nothing in the mode");
   Canvas c(1024,768);AudioStats stats;e.action(Action::ColumnSettings);drawUI(c,e,stats);
   for(auto px:c.pixels)check(px>>24==255,"the tiles draw opaque");
   if(argc>1)writePPM(c,std::string(argv[1])+"/performance.ppm");
   e.action(Action::PrevTrack);
   check(e.page==Page::Pattern&&!p.chainOn&&!p.paramsOn&&!p.fxLfoOn&&encodeProject(e.project)==stored,"F1: everything dropped, the pattern as stored");
   e.action(Action::Master);e.action(Action::RandomizerOptions);e.action(Action::Home);
   check(e.page==Page::Pattern&&!e.performance->chainOn,"HOME also leaves and drops it");}
  // Many draws: always within the budget, never an empty chain, always tracks with notes.
  {Performance p;p.seed=99;Project pr=busy();DeviceSettings d;std::set<std::string> combos;
   for(int n=0;n<400;n++){p.chainOn=false;perfChain(p,pr,d);check(p.chainOn&&p.stages>=1,"a chain every time");
     int cost=0;for(int i=0;i<p.stages;i++)cost+=lifebloodCost(p.effect[size_t(i)].type);check(cost*p.targets<=PerfBudget,"budget");combos.insert(perfComboName(p.combo));}
   check(combos.size()>=5,"the combos vary");
   Performance none;Project empty=blankProject();check(perfChain(none,empty,d)=="NO TRACK HOLDS NOTES"&&!none.chainOn,"no notes: no chain");}
  // Audio: the chain changes what the targets sound like, stays bounded, and stops when left.
  {Project pr=busy();Performance p;p.seed=7;DeviceSettings d;
   auto render=[&](bool on,int seconds){Audio a(64);a.submit(pr,true);Performance q=p;
     if(on){q.chainOn=false;perfChain(q,pr,d);perfFxLfos(q);}
     std::vector<float> out(size_t(44100*seconds*2));
     for(size_t at=0;at<out.size()/2;at+=882){auto f=perfFrame(q,.02);f.on=on;a.perform(f);a.render(out.data()+at*2,int(std::min<size_t>(882,out.size()/2-at)));}
     return out;};
   auto dry=render(false,3),wet=render(true,3);double diff=0;
   for(size_t i=0;i<dry.size();i++){check(std::isfinite(wet[i])&&std::abs(wet[i])<=.951f,"bounded");diff+=std::abs(wet[i]-dry[i]);}
   check(diff>1,"the chain is heard");
   // Left: the frame goes off and the pattern sounds as without the mode once the tails are gone.
   Audio a(64);a.submit(pr,true);Performance q=p;q.chainOn=false;perfChain(q,pr,d);std::vector<float> tmp(882*2);
   for(int n=0;n<150;n++){a.perform(perfFrame(q,.02));a.render(tmp.data(),882);}
   PerfFrame off;a.perform(off);for(int n=0;n<500;n++)a.render(tmp.data(),882);
   check(true,"left without fault");}
  // Y: instrument LFOs. VAP moves live parameters; PLAITS (a recipe engine) steps through variations.
  {Project pr=busy();editTrackPatch(pr,3,pr.tracks[3].instrument)={uint8_t(PlaitsEngine),777u};
   Editor e;e.project=pr;e.entropy=5;auto stored=encodeProject(e.project);
   e.action(Action::Master);auto&p=*e.performance;
   bool sawLive=false,sawMorph=false;
   for(int n=0;n<12&&!(sawLive&&sawMorph);n++){
     e.action(Action::Master);check(p.engineLfoOn&&p.instruments>=1,"Y: instrument LFOs on");
     for(auto&l:p.engineLfo)if(l.target>=0){
       const auto&in=p.instrument[size_t(l.target)];
       check(in.track==0||in.track==3||in.track==7,"targets hold notes");
       check(l.morph==(in.engine>=3),"recipe engines morph, VAP / PERC / CHIP move parameters");
       check(!perfEngineLfoName(p,l).empty(),"named for the screen");
       sawLive|=!l.morph;sawMorph|=l.morph;}
     e.action(Action::Master);check(!p.engineLfoOn,"Y again: off");}
   check(sawLive&&sawMorph,"both kinds drawn");
   e.action(Action::Master);
   std::set<uint32_t> variations;bool moved=false;auto first=perfFrame(p,0);
   for(int n=0;n<600;n++){auto f=perfFrame(p,.02);
     for(int k=0;k<p.instruments;k++){const auto&in=f.instrument[size_t(k)];if(in.track<0)continue;
       if(in.live)moved|=in.params!=first.instrument[size_t(k)].params;else variations.insert(in.variation);
       check(in.live==(in.engine<3),"live only for VAP / PERC / CHIP");}}
   bool anyLive=false,anyMorph=false;for(auto&l:p.engineLfo)if(l.target>=0){anyLive|=!l.morph;anyMorph|=l.morph;}
   check(!anyLive||moved,"live parameters move");check(!anyMorph||variations.size()>=2,"the variation bank is walked");
   // Pitch and level never move: the parameters a row variation may not touch stay as drawn.
   for(auto&l:p.engineLfo)if(l.target>=0&&!l.morph){const auto&in=p.instrument[size_t(l.target)];
     check(amalthea_patch_movable_for_test(in.engine,in.base.data(),l.param),"only movable parameters");}
   // Audio: the instruments sound different with Y on, bounded, the project untouched.
   auto render=[&](bool on){Audio a(64);a.submit(pr,true);Performance q=p;q.engineLfoOn=on;std::vector<float> out(44100*3*2);
     for(size_t at=0;at<out.size()/2;at+=882){a.perform(perfFrame(q,.02));a.render(out.data()+at*2,int(std::min<size_t>(882,out.size()/2-at)));}return out;};
   auto plain=render(false),moving=render(true);double diff=0;
   for(size_t i=0;i<plain.size();i++){check(std::isfinite(moving[i])&&std::abs(moving[i])<=.951f,"bounded");diff+=std::abs(moving[i]-plain[i]);}
   check(diff>1,"the instrument LFOs are heard");
   e.action(Action::RandomizerOptions);e.action(Action::TrackSettings);e.action(Action::ColumnSettings);for(int n=0;n<20;n++)perfFrame(p,.05);
   {Canvas c(1024,768);AudioStats stats;drawUI(c,e,stats);for(auto px:c.pixels)check(px>>24==255,"all four tiles draw opaque");if(argc>1)writePPM(c,std::string(argv[1])+"/performance-all.ppm");}
   e.action(Action::PrevTrack);check(!p.engineLfoOn&&!p.chainOn&&encodeProject(e.project)==stored,"F1 drops them; the project untouched");}
  puts("PASS: performance mode - Y / SHIFT+Y / F1, X B A toggles, budget, LFOs, untouched project, audio");
}
