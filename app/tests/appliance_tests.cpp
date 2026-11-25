#include "core.hpp"
#include "ui.hpp"
#include "sessions.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <unistd.h>
using namespace amalthea;
static void check(bool b,const char*m){if(!b){fprintf(stderr,"FAIL: %s\n",m);exit(1);}}
static void chosen(const Step&from,Step&to,int col,int tab){
 bool fx=col==2||col==3;
 if(tab==(col==0||fx?2:1))eventCondition(to,col)=eventCondition(from,col);
 else if(col==0&&tab==1)to.length=from.length;
 else if(fx&&tab==1)to.fx[col-2].length=from.fx[col-2].length;
 else if(col==0&&tab>=3)to.locks[tab-3]=from.locks[tab-3];
 else if(col==0)to.note=from.note;
 else if(col==1)to.instrument=from.instrument;
 else if(col<4)to.fx[col-2].value=from.fx[col-2].value;
 else to.modifier=from.modifier;
}
static void openExit(Editor&e){e.action(Action::Shift2Up);e.action(Action::ShiftUp);e.action(Action::Shift2Down);e.action(Action::ShiftDown);}
int main(int argc,char**argv){
 for(int col=0;col<Columns;col++)for(int tab=0;tab<(col==0?10:col==2||col==3?3:2);tab++){
  Editor e;e.project=blankProject();auto&t=e.project.tracks[0];t.columns[col].length=7;
  for(int r=0;r<MaxRows;r++){auto&s=t.steps[r];s.note=60+r%12;s.instrument=1;s.fx[0]={FxType::Rings,40};s.fx[1]={FxType::LoFi,70};s.modifier={7,ModifierType::Offset};}
  e.column=col;e.action(Action::Enter);e.eventTab=tab;auto before=e.project;auto pending=e.pending;auto serial=e.auditionSerial;
  auto encoded=encodeProject(e.project);e.action(Action::RandomizerOptions);
  check(e.editing&&e.page==Page::Pattern&&(col==0||col==4)==(e.auditionSerial>serial),"tap stays in editor; only the NOTE column auditions");
  check(encodeProject(e.project)==encoded,"single parameter randomization remains transactional");
  auto expected=pending;chosen(e.pending,expected,col,tab);check(!memcmp(&expected,&e.pending,sizeof(Step)),"tap changes only selected parameter");
  if(col==0&&tab==4)check(e.pending.locks[NoteVolume]>=10,"volume floor10");
  e.action(Action::Cancel);check(encodeProject(e.project)==encoded,"cancel discards random value and PRNG changes");
  e.action(Action::Enter);e.eventTab=tab;e.action(Action::ShiftDown);serial=e.auditionSerial;e.action(Action::RandomizeColumn);
  if(col==2||col==3){   // in the FX editor Shift+hold X draws a new pending effect instead
   check(e.editing&&e.undoStack.empty()&&encodeProject(e.project)==encodeProject(before),"Shift+hold X on FX stays pending");
   e.action(Action::ShiftUp);e.action(Action::Cancel);continue;
  }
  check(e.editing&&(col==0||col==4)==(e.auditionSerial>serial)&&e.undoStack.size()==1,"shift hold is one undoable bulk edit and stays editing");
  for(int tr=0;tr<Tracks;tr++)for(int r=0;r<MaxRows;r++){
   auto expected=before.tracks[tr].steps[r];if(tr==0&&r<7)chosen(e.project.tracks[tr].steps[r],expected,col,tab);
   check(!memcmp(&expected,&e.project.tracks[tr].steps[r],sizeof(Step)),"bulk affects only parameter within current column length");
  }
  e.action(Action::ShiftUp);e.action(Action::Undo);check(encodeProject(e.project)==encodeProject(before),"undo restores all values and seed");
 }
 // Edge pitches remain in the selected scale/root even at the bottom of the note range.
 for(int root=0;root<12;root++)for(int scale=0;scale<7;scale++){
  Editor e;e.project=blankProject();e.project.root=root;e.project.scale=scale;e.project.tracks[0].steps[0].note=0;e.action(Action::Enter);
  static const unsigned masks[]={0xfff,0x6ad,0x5ad,0xab5,0x5ab,0x4a9,0x9ad};
  for(int n=0;n<40;n++){e.action(Action::RandomizerOptions);check(masks[scale]&(1<<((e.pending.note-root+120)%12)),"random notes respect root/scale at edge");}
 }
 Editor e;e.project=blankProject();e.project.tracks[0].steps[0].note=72;e.playing=true;
 e.action(Action::ShiftDown);e.action(Action::Shift2Down);check(e.exitStage==Editor::ExitStage::None,"wrong modifier order never exits");
 openExit(e);check(e.exitStage==Editor::ExitStage::Confirm&&!e.exitChoice&&e.playing,"FUNC then Shift opens cancel-first prompt without stopping");
 e.action(Action::Home);check(e.exitStage==Editor::ExitStage::Confirm,"Home cannot bypass exit confirmation");
 e.action(Action::Enter);check(e.exitStage==Editor::ExitStage::None&&e.playing,"Cancel keeps session playing");
 openExit(e);e.action(Action::Right);e.action(Action::Enter);check(e.exitStage==Editor::ExitStage::Save&&!e.exitChoice&&!e.playing,"OK opens No-first save prompt and stops");
 e.action(Action::Enter);check(e.page==Page::Appliances&&!e.playing&&e.project.tracks[0].steps[0].note==-1,"No exits to blank shell");
 e.action(Action::Play);check(!e.playing,"shell cannot start hidden tracker");e.action(Action::Right);e.action(Action::Enter);check(e.page==Page::FxAppliance,"FX placeholder selected by Enter");e.leaveAppliance();e.action(Action::Enter);check(e.page==Page::Pattern&&!e.playing,"Tracker enters blank/stopped");
 e.project.tracks[0].steps[0].note=67;openExit(e);e.action(Action::Right);e.action(Action::Enter);e.action(Action::Right);e.action(Action::Enter);
 check(e.exitSaveRequested&&e.exitStage==Editor::ExitStage::Saving&&e.project.tracks[0].steps[0].note==67,"Yes requests save before destroying session");
 // Synthetic bank only: exercise save/recall without touching the device's live bank.
 auto dir=std::filesystem::temp_directory_path()/("amalthea-shell-"+std::to_string(getpid()));SessionBank bank((dir/"session.amalthea").string());std::string error;
 check(bank.save(0,e,error),"exit state saves synthetic session");e.leaveAppliance();Editor loaded;check(bank.load(0,loaded,error)&&loaded.project.tracks[0].steps[0].note==67,"saved content survives appliance unload");std::filesystem::remove_all(dir);
 AudioStats stats;for(int width:{640,1024})for(int view=0;view<5;view++){
  Editor preview;preview.page=view==2?Page::FxAppliance:Page::Appliances;preview.splash=view==0;preview.applianceItem=view%2;
  if(view>=3){preview.page=Page::Pattern;preview.exitStage=view==3?Editor::ExitStage::Confirm:Editor::ExitStage::Save;}
  Canvas canvas(width,width*3/4);drawUI(canvas,preview,stats);for(auto pixel:canvas.pixels)check(pixel>>24==255,"shell/splash/prompts opaque at both sizes");
  if(argc>1&&width==1024)writePPM(canvas,std::string(argv[1])+"-"+std::to_string(view)+".ppm");
 }
 puts("PASS: parameter isolation, bulk range/undo, scale/volume guards, ordered exit, synthetic save, appliance shell and splash");
}
