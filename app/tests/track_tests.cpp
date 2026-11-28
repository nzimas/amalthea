#include "audio.hpp"
#include "ui.hpp"
#include "walk.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <vector>
using namespace amalthea;
static void require(bool b,const char *s) {if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main() {
  for(int length=1;length<=16;length++)for(int d=0;d<Directions;d++) {
    AmaltheaWalk a,b;amalthea_walk_start(&a,123);amalthea_walk_start(&b,123);
    std::set<int> visited;
    for(int n=0;n<1000;n++) {
      int r=amalthea_walk_next(&a,d,length);
      require(r>=0&&r<length,"all Fugitive directions stay within track length");
      require(r==amalthea_walk_next(&b,d,length),"direction seeds reproduce");
      if(n<length)visited.insert(r);
    }
    if(d==16)require(int(visited.size())==length,"shuffle visits each step once per cycle");
  }
  const std::vector<std::vector<int>> expected={{0,1,2,3,0,1,2,3},{3,2,1,0,3,2,1,0},{0,1,2,3,2,1,0,1},{0,1,2,3,3,2,1,0}};
  for(int d=0;d<4;d++) {AmaltheaWalk a;amalthea_walk_start(&a,1);for(int r:expected[d])require(amalthea_walk_next(&a,d,4)==r,"standard direction order");}
  Editor e;e.project=blankProject();e.action(Action::TrackSettings);
  require(e.page==Page::Track,"B opens track settings");
  e.action(Action::Left);require(e.project.tracks[0].length==15,"length edits");
  e.trackField=2;e.action(Action::Left);require(e.project.tracks[0].direction==18,"direction wraps");
  e.action(Action::NextTrack);require(e.track==1&&e.page==Page::Track,"F2 navigates tracks inside settings");
  e.action(Action::TrackSettings);e.column=1;e.action(Action::Enter);e.action(Action::Up);
  require(e.pending.instrument==0,"instrument up from inherited selects first slot");
  e.action(Action::Down);e.action(Action::Enter);
  require(e.project.tracks[1].steps[0].instrument==NoInstrument,"instrument down restores inheritance");
  auto &t=e.project.tracks[1];t.instrument=6;t.delaySend=70;t.reverbSend=80;t.level=20;t.pan=80;
  Step s;s.note=60;require(effectiveInstrument(t,s)==6,"default instrument resolves");
  s.instrument=3;require(effectiveInstrument(t,s)==3,"explicit instrument overrides");
  s.fx={Effect{FxType::Delay,0},Effect{FxType::Volume,90}};
  auto m=resolveTrackMix(t,s);
  require(m.delay==0&&std::abs(m.reverb-.8f)<.001f&&std::abs(m.level-.2f)<.001f&&std::abs(m.pan-.8f)<.001f,"locks replace only matching defaults including zero; step volume scales with the fader");
  {Track f;Step v;v.locks[NoteVolume]=60;require(std::abs(resolveTrackMix(f,v).level-.6f)<.001f,"step volume plays as written at the default fader");
   f.level=45;require(std::abs(resolveTrackMix(f,v).level-.3f)<.001f,"halving the fader halves the step");
   f.level=0;require(resolveTrackMix(f,v).level==0,"fader at zero silences programmed steps");
   Step inherit;f.level=45;require(std::abs(resolveTrackMix(f,inherit).level-.45f)<.001f,"inherited steps follow the fader");}
  s.fx={Effect{FxType::Pan,0},Effect{FxType::Reverb,0}};m=resolveTrackMix(t,s);
  require(m.pan==-1&&m.reverb==0&&std::abs(m.delay-.7f)<.001f,"pan and reverb override independently");
  std::string error;require(saveProject(e.project,"track-test.amalthea",error),"track settings save");Project loaded;
  require(loadProject(loaded,"track-test.amalthea",error),"track settings load");remove("track-test.amalthea");
  require(loaded.tracks[0].length==15&&loaded.tracks[0].direction==18&&loaded.tracks[1].instrument==6&&loaded.tracks[1].delaySend==70&&loaded.tracks[1].reverbSend==80&&loaded.tracks[1].steps[0].instrument==NoInstrument,"all new metadata and inheritance survive save");
  Project p=blankProject();p.bpm=120;
  for(int tr=0;tr<Tracks;tr++) {auto &t=p.tracks[tr];t.length=3+tr;t.clock=tr%6;t.instrument=7;for(int r=0;r<t.length;r++)t.steps[r].note=60+r;}
  Audio audio;require(audio.submit(p,true),"audio submit");std::vector<float> out(44100*2*2);audio.render(out.data(),44100*2);
  for(int tr=0;tr<Tracks;tr++) {
    int count=int(16*clockRate(tr%6));
    require(audio.stats.advances[tr].load()==unsigned(count),"independent clock ratios have no cumulative drift");
    require(audio.stats.trackRow[tr].load()==(count-1)%p.tracks[tr].length,"independent lengths create polymeters");
    require(audio.stats.trackCycle[tr].load()==unsigned((count-1)/p.tracks[tr].length),"local cycle counter");
    require(audio.stats.lastInstrument[tr].load()==7,"audio uses inherited instrument");
  }
  // Confirm the transport uses reverse traversal and local cycle conditions.
  p=blankProject();auto &r=p.tracks[0];r.length=3;r.direction=1;r.instrument=7;
  for(int n=0;n<3;n++){r.steps[n].note=60+n;r.steps[n].condition={1,2};}
  Audio reverse;reverse.submit(p,true);reverse.render(out.data(),44100);
  require(reverse.stats.triggers[0].load()==5&&reverse.stats.lastNote[0].load()==61,"reverse transport conditions use its own three-step cycles");
  // Long magnifier strings must not paint over the right edge of the panel.
  AudioStats stats;
  for(int width:{640,1024})for(int rows:{8,16})for(int col=0;col<Columns;col++) {
    Editor edit;edit.visibleRows=rows;edit.column=col;
    edit.project.tracks[0].instrument=7;edit.project.tracks[0].steps[0].fx[0]={FxType::Reverb,100};
    edit.fxPart=1;Canvas before(width,width*3/4),after(width,width*3/4);
    drawUI(before,edit,stats);edit.action(Action::Enter);edit.fxPart=1;drawUI(after,edit,stats);
    int left=int(128*width/1024.f),right=int(896*width/1024.f),top=int(244*width/1024.f),bottom=int(532*width/1024.f);
    for(int y=top;y<bottom;y++)for(int x=0;x<width;x++)
      if(x<left || x>=right)require(before.pixels[y*width+x]==after.pixels[y*width+x],"editing overlay preserves both side margins");
  }
  e=Editor{};e.project=loaded;e.page=Page::Track;e.track=1;e.trackBanner=false;
  Canvas screen(1024,768);drawUI(screen,e,stats);writePPM(screen,"track-settings.ppm");
  // Check the DSP path, rather than only parameter resolution, honors a level lock.
  p=blankProject();p.tracks[0].instrument=7;p.tracks[0].level=0;
  p.tracks[0].steps[0].note=60;p.tracks[0].steps[0].fx[0]={FxType::Volume,100};
  Audio lock;lock.submit(p,true);lock.render(out.data(),1024);double energy=0;
  for(int i=0;i<2048;i++)energy+=out[i]*out[i];
  require(energy<1e-9,"a zero fader silences programmed step volumes in the actual DSP");
  p.tracks[0].level=45;Audio scaled;scaled.submit(p,true);scaled.render(out.data(),1024);energy=0;
  for(int i=0;i<2048;i++)energy+=out[i]*out[i];
  require(energy>.001,"step volume sounds, scaled, once the fader is up");
  p.tracks[0].level=100;p.tracks[0].steps[0].fx[0].value=0;
  Audio zero;zero.submit(p,true);zero.render(out.data(),1024);energy=0;
  for(int i=0;i<2048;i++)energy+=out[i]*out[i];
  require(energy==0,"zero step volume silences nonzero track level in the actual DSP");
  // Generate from an empty track and check the actual engines, not only the UI cells.
  for(int slot=0;slot<Instruments;slot++) {
    Editor generated;generated.project=blankProject();generated.track=5;
    auto &track=generated.project.tracks[5];track.instrument=slot;track.length=7;
    track.steps[7].note=73;track.steps[7].instrument=(slot+1)%Instruments;
    track.steps[2].fx[1]={FxType::Pan,50};
    generated.action(Action::RandomizeColumn);
    for(int r=0;r<7;r++)require(track.steps[r].instrument==slot,"generated rows explicitly use the assigned track instrument");
    require(track.steps[7].note==73&&track.steps[7].instrument==(slot+1)%Instruments,"generation preserves hidden rows");
    require(track.steps[2].fx[1].type==FxType::Pan&&track.steps[2].fx[1].value==50,"generation preserves step effects");
    Audio generatedAudio;generatedAudio.submit(generated.project,true);generatedAudio.render(out.data(),44100);
    double energy=0;for(int i=0;i<88200;i++)energy+=out[i]*out[i];
    require(generatedAudio.stats.triggers[5].load()>0&&generatedAudio.stats.lastInstrument[5].load()==slot&&energy>1e-8,"generated melody triggers the selected engine and produces audio");
    generated.action(Action::Undo);
    require(track.steps[0].instrument==NoInstrument&&track.steps[0].note==-1,"one undo restores notes and instrument cells together");
  }
  p=demoProject();
  for(int tr=0;tr<Tracks;tr++) {
    auto &t=p.tracks[tr];t.length=3+tr;t.clock=tr%6;t.direction=(tr*3)%Directions;
    t.delaySend=20;t.reverbSend=10;
    for(int r=0;r<t.length;r++) {t.steps[r].note=48+(r%5)*2;t.steps[r].fx[0]={FxType::Saturation,30};}
  }
  require(saveProject(p,"polymeter-test.amalthea",error),"device stress fixture");
  {
    // While playing, auditioning an edited row must not hold the whole track on that row's pan.
    Project pp=blankProject();pp.master.amount[9]=0;auto &t=pp.tracks[0];t.instrument=0;pp.patches[0]={0,0};
    for(int r=0;r<16;r++){t.steps[r].note=48;t.steps[r].locks[NotePan]=r%2?100:0;}
    Audio live(64);live.submit(pp,true);std::vector<float> buf(5512*2*4);live.render(buf.data(),5512*2);
    Step edited=t.steps[2];edited.locks[NotePan]=6;live.submit(pp,true,0,&edited);
    live.render(buf.data(),5512*4);
    int alternations=0;
    for(int stepIndex=1;stepIndex<4;stepIndex++){double L=0,R=0;for(int i=stepIndex*5512+500;i<stepIndex*5512+4000;i++){L+=buf[2*i]*buf[2*i];R+=buf[2*i+1]*buf[2*i+1];}
      double prevL=0,prevR=0;for(int i=(stepIndex-1)*5512+500;i<(stepIndex-1)*5512+4000;i++){prevL+=buf[2*i]*buf[2*i];prevR+=buf[2*i+1]*buf[2*i+1];}
      alternations+=(L>R)!=(prevL>prevR);}
    require(alternations>=2,"rows keep their own pan straight after an audition during playback");
  }
  {
    // RANDOM: each trigger loads a drawn effect; AMOUNT 0 bypasses it.
    auto render=[&](uint8_t amount,bool randomFx){Project pr=blankProject();pr.master.amount[9]=0;auto &t=pr.tracks[0];t.instrument=0;pr.patches[0]={0,0};
      for(int r=0;r<16;r++){t.steps[r].note=48+r%5;if(randomFx)t.steps[r].fx[0]={FxType::Random,amount};}
      Audio a(64);a.submit(pr,true);std::vector<float> out(5512*16*2);a.render(out.data(),5512*16);return out;};
    auto dry=render(0,false),wet=render(100,true),bypass=render(0,true);
    double diff=0,same=0;for(size_t i=0;i<dry.size();i++){require(std::isfinite(wet[i])&&std::abs(wet[i])<=.951f,"RANDOM output finite and bounded");diff+=std::abs(wet[i]-dry[i]);same+=std::abs(bypass[i]-dry[i]);}
    require(diff>1.0,"RANDOM steps are processed by drawn effects");
    require(same<1e-3,"RANDOM at AMOUNT 0 leaves the track dry");
    require(std::string(fxName(FxType::Random))=="RANDOM"&&fxText({FxType::Random,50}).rfind("RN",0)==0,"RANDOM is named in the FX column");
  }
  {
    // FX RANDOM: each time the step plays, the chosen effect takes newly drawn parameters at the
    // step's own mix; a fixed step keeps the track's parameters; mix 0 still leaves the track dry.
    auto stepEnergies=[&](bool randomFx,uint8_t mix){Project pr=blankProject();pr.master.amount[9]=0;pr.master.busMix.fill(0);auto &t=pr.tracks[0];t.instrument=0;pr.patches[0]={0,0};
      for(int r=0;r<16;r++){t.steps[r].note=48;t.steps[r].fx[0]={FxType::Filter,mix};t.steps[r].fx[0].random=randomFx;}
      Audio a(64);a.submit(pr,true);std::vector<float> out(5512*16*2);a.render(out.data(),5512*16);
      std::vector<double> e;for(int k=2;k<15;k++){double x=0;for(int i=k*5512+300;i<k*5512+4000;i++)x+=out[2*i]*out[2*i]+out[2*i+1]*out[2*i+1];e.push_back(x);}
      for(float v:out)require(std::isfinite(v)&&std::abs(v)<=.951f,"RANDOM FX output finite and bounded");
      return e;};
    auto spread=[](const std::vector<double>&e){auto [lo,hi]=std::minmax_element(e.begin(),e.end());return *hi/std::max(1e-12,*lo);};
    auto fixed=stepEnergies(false,100),drawn=stepEnergies(true,100),dryFixed=stepEnergies(false,0),dryDrawn=stepEnergies(true,0);
    // (The instrument has its own motion, so each drawn step is measured against the fixed one.)
    std::vector<double> ratio;for(size_t k=0;k<fixed.size();k++)ratio.push_back(drawn[k]/std::max(1e-12,fixed[k]));
    require(spread(ratio)>2,"a RANDOM FX step sounds different on its repeats");
    for(size_t k=1;k<ratio.size();k++)require(std::abs(ratio[k]-ratio[k-1])>.01*std::max(ratio[k],ratio[k-1]),"a RANDOM FX step never repeats the previous draw");
    double d=0;for(size_t k=0;k<dryFixed.size();k++)d+=std::abs(dryFixed[k]-dryDrawn[k]);
    require(d<1e-6,"RANDOM at DRY/WET 0 leaves the track dry");
    Project lp=blankProject();lp.tracks[3].steps[9].fx[1]={FxType::Filter,40};lp.tracks[3].steps[9].fx[1].random=true;
    lp.tracks[3].steps[10].fx[0].random=true;   // (no effect: not stored)
    Project back;std::string err;
    require(decodeProject(back,encodeProject(lp),err)&&back.tracks[3].steps[9].fx[1].random&&!back.tracks[3].steps[9].fx[0].random&&!back.tracks[3].steps[10].fx[0].random&&!back.tracks[0].steps[0].fx[0].random,"RANDOM FX cells survive save/load");
    // Step editor: FX has three tabs; RANDOM toggles YES/NO and commits with the step.
    Editor ed;ed.project=blankProject();ed.column=2;ed.action(Action::Enter);ed.action(Action::Up);
    require(ed.eventTabCount()==3,"FX step editor has DRY/WET, LENGTH, RANDOM");
    for(int k=0;k<2;k++)ed.action(Action::NextTrack);
    require(ed.randomTab()&&!ed.pending.fx[0].random,"RANDOM tab opens on NO");
    ed.action(Action::Up);require(ed.pending.fx[0].random,"UP sets RANDOM to YES");
    ed.action(Action::Down);require(!ed.pending.fx[0].random,"DOWN sets it to NO");
    ed.action(Action::Up);ed.action(Action::Enter);
    require(ed.project.tracks[0].steps[0].fx[0].random&&ed.project.tracks[0].steps[0].fx[0].type!=FxType::None,"RANDOM commits with the step");
  }
  {
    // NOTE LENGTH: a held note sounds for its steps; lengths survive save/load.
    auto held=[&](uint8_t length){Project pr=blankProject();pr.master.amount[9]=0;auto &t=pr.tracks[0];t.instrument=0;pr.patches[0]={0,0};
      t.steps[0].note=48;t.steps[0].length=length;Audio a(64);a.submit(pr,true);std::vector<float> out(5512*6*2);a.render(out.data(),5512*6);
      double e=0;for(int i=5512*2;i<5512*3;i++)e+=out[2*i]*out[2*i];return e;};
    double shortNote=held(1),longNote=held(4);
    require(longNote>shortNote*4,"a 4-step note is still sounding on its third step");
    Project lp=blankProject();lp.tracks[2].steps[5].length=7;Project back;std::string err;
    require(decodeProject(back,encodeProject(lp),err)&&back.tracks[2].steps[5].length==7&&back.tracks[0].steps[0].length==1,"note lengths survive save/load");
  }
  {
    // Voices must not pile up on a busy track: PERC is one kit voice, other engines keep a few tails.
    for(int engine:{1,11,5}){
      Project pr=blankProject();auto &t=pr.tracks[0];t.instrument=0;pr.patches[0]={uint8_t(engine),12345u};
      for(int r=0;r<16;r++)t.steps[r].note=engine==1?60+(r%6)*2:60;
      Audio a(128);a.submit(pr,true);std::vector<float> out(256*2);int most=0;
      for(int b=0;b<44100*6/256;b++){a.render(out.data(),256);most=std::max(most,a.stats.voices.load());}
      require(most<=(engine==1?1:5)&&a.stats.steals.load()==0,"a busy track holds its note, three tails and one fading out at most (PERC: one kit voice)");
    }
  }
  puts("PASS: Fugitive directions, independent transport, track defaults/locks, persistence and overlay boundaries");
}
