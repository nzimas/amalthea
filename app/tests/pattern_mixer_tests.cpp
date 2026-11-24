// Synthetic data only: never save or recall a live device session.
#include "sessions.hpp"
#include "ui.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unistd.h>
using namespace amalthea;
static void check(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
 Editor e;SessionButton button;
 auto tap=[&](uint64_t time){button.press(time,e);return button.release(time+50);};
 check(tap(0)==Action::Patterns,"plain middle opens patterns");e.shift=true;button.press(100,e);e.shift=false;check(button.release(150)==Action::Session,"shift sampled on press");
 e.shift2=true;button.press(200,e);e.shift2=false;check(button.release(250)==Action::Mixer,"FUNC sampled on press");
 for(auto page:{Page::Patterns,Page::PatternSettings,Page::Sessions,Page::Session}){
  e.page=page;bool pattern=page==Page::Patterns||page==Page::PatternSettings;
  check(tap(300)==(pattern?Action::LoadPattern:Action::LoadSession),"middle tap loads correct bank");
  button.press(400,e);e.page=Page::Pattern;
  check(button.tick(1200)==(pattern?Action::SavePattern:Action::SaveSession)&&button.release(1250)==Action::Reserved,"hold retains bank after page changes and never loads");
 }
 e.page=Page::Pattern;button.press(1300,e);check(button.tick(2100)==Action::Reserved&&button.release(2200)==Action::Reserved,"holding outside bank never overwrites anything");
 e.project=blankProject();e.sessionDefaults={133,2,3};e.applyMusicalSettings();
 for(int slot=0;slot<32;slot++){
  e.project.tracks[0].steps[0].note=50+slot;e.project.tracks[5].columns[4]={7,1,18};e.project.tracks[7].inserts[1].amp[0]=slot;
  e.density[5][2]=slot;
  e.project.master.value(9,0)=slot;e.project.tracks[3].solo=slot%2;e.patternOverrides={};
  if(slot==31)e.patternOverrides={177,8,6};
  e.applyMusicalSettings();check(e.savePattern(slot),"all32 slots store complete snapshots");
 }
 auto saved=encodeProject(*e.patterns[5].state);e.project.tracks[0].steps[0].note=119;check(encodeProject(*e.patterns[5].state)==saved,"working edits do not mutate saved pattern");
 check(e.loadPattern(5)&&e.project.bpm==133&&e.project.root==2&&e.project.scale==3,"pattern inherits each session setting");
 check(e.density[5][2]==5,"randomizer preferences follow the pattern");
 auto n=e.project.tracks[0].steps[0].note;e.sessionDefaults.root=4;e.sessionDefaults.bpm=144;e.applyMusicalSettings();check(e.project.bpm==144&&e.project.tracks[0].steps[0].note==n+2,"session root transposes inherited pattern");
 check(e.loadPattern(31)&&e.project.bpm==177&&e.project.root==8&&e.project.scale==6,"explicit pattern overrides session");
 check(!e.loadPattern(-1)&&!e.savePattern(32),"pattern bounds checked");
 auto dir=std::filesystem::temp_directory_path()/("amalthea-pattern-test-"+std::to_string(getpid()));std::filesystem::create_directories(dir);SessionBank bank((dir/"session.amalthea").string());std::string error;
 check(bank.save(3,e,error),"session container saves complete bank");Editor loaded;check(bank.load(3,loaded,error),"session container reloads");
 check(loaded.activePattern==31&&loaded.project.bpm==177&&loaded.sessionDefaults.bpm==144&&!loaded.playing,"working state, inheritance and stopped session load");
 for(int slot=0;slot<32;slot++)check(loaded.patterns[slot].state&&loaded.patterns[slot].density==e.patterns[slot].density&&encodeProject(*loaded.patterns[slot].state)==encodeProject(*e.patterns[slot].state),"all32 complete snapshots survive disk roundtrip including solo and DSP");
 auto before=encodeProject(loaded.project);std::fstream file(bank.path(3),std::ios::binary|std::ios::in|std::ios::out);file.seekp(100);file.put('x');file.close();check(!bank.load(3,loaded,error)&&encodeProject(loaded.project)==before,"corruption does not partially replace state");
 Editor undo;undo.project.tracks[0].steps[0].note=60;undo.page=Page::Session;undo.sessionField=2;undo.sessionEdit=true;undo.action(Action::Right);check(undo.project.tracks[0].steps[0].note==61,"root edits transpose working notes");undo.action(Action::Undo);check(undo.project.root==0&&undo.sessionDefaults.root==0&&undo.project.tracks[0].steps[0].note==60,"undo restores notes and inherited setting together");undo.action(Action::Redo);check(undo.project.root==1&&undo.sessionDefaults.root==1,"redo preserves hierarchy");
 // Mixer navigation edits only selected channel and supports both boolean axes.
 e=Editor{};e.action(Action::Mixer);check(e.page==Page::Mixer,"mixer page opens");e.action(Action::NextTrack);check(e.mixerPage==1&&e.mixerTrack==6,"F2 pages six channels");
 e.action(Action::Right);e.action(Action::Enter);e.action(Action::Up);check(e.project.tracks[7].level==91&&e.project.tracks[6].level==90,"selected fader up edits one track");e.action(Action::Enter);
 e.action(Action::Down);e.action(Action::Enter);e.action(Action::Left);check(e.project.tracks[7].mute,"mute toggles horizontal");e.action(Action::Down);check(!e.project.tracks[7].mute,"mute toggles vertical");e.action(Action::Enter);e.action(Action::Down);e.action(Action::Enter);e.action(Action::Up);
 check(e.project.tracks[7].solo&&trackAudible(e.project,7)&&!trackAudible(e.project,0),"solo isolates channel");e.project.tracks[7].mute=true;check(!trackAudible(e.project,7),"mute wins over solo");
 // Real audio: an excluded source must be digitally silent, not merely dimmed.
 Project p=blankProject();p.tracks[0].steps[0].note=60;p.tracks[1].solo=true;Audio audio;audio.submit(p,true);std::vector<float>out(44100*2);audio.render(out.data(),44100);for(float v:out)check(v==0,"solo exclusion reaches actual audio path");
 AudioStats stats;e=Editor{};e.page=Page::Sessions;e.activeSession=3;e.sessionSlot=4;e.sessionState[3]=1;Canvas c(1024,768);drawUI(c,e,stats);auto color=c.pixels[220*1024+400];e.track=5;Canvas other(1024,768);drawUI(other,e,stats);check(color==0xff315aca&&other.pixels[220*1024+400]==color,"loaded bank color independent of track");
 for(auto page:{Page::Mixer,Page::Patterns,Page::PatternSettings})for(int width:{640,1024}){e.page=page;Canvas view(width,width*3/4);drawUI(view,e,stats);for(auto v:view.pixels)check((v>>24)==255,"new pages render opaque at both resolutions");}
 std::filesystem::remove_all(dir);puts("PASS: bank gestures,32 snapshots, hierarchy and transposition, atomic container validation, mixer navigation, audible solo isolation");
}
