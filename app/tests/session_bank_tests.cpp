// Tests use synthetic fixtures only; never touch a user's live session.
#include "sessions.hpp"
#include "ui.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace amalthea;
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
 auto temp=std::filesystem::temp_directory_path()/("amalthea-bank-test-"+std::to_string(getpid()));std::filesystem::create_directories(temp);
 auto legacy=(temp/"session.amalthea").string();SessionBank bank(legacy);Project original=demoProject(),loaded;std::string error;
 for(auto state:bank.scan())require(state==0,"new bank empty without creating directories");
 for(int i=0;i<64;i++){
  original.bpm=60+i;original.tracks[0].inserts[1].clouds[3]=i;original.tracks[0].steps[0].fx[1]={FxType::Clouds,uint8_t(i)};
  original.tracks[4].inserts[0].biz[7].module=80;original.tracks[4].inserts[0].biz[7].value[0]=i;
  require(bank.save(i,original,error),"all 64 synthetic slots save");
 }
 for(int i=0;i<64;i++){
  require(bank.scan()[i]==1&&bank.load(i,loaded,error),"all64 slots recognized and load");
  require(loaded.bpm==60+i&&loaded.tracks[0].inserts[1].clouds[3]==i&&loaded.tracks[4].inserts[0].biz[7].value[0]==i,"slots retain independent session and insert state");
 }
 std::ofstream(bank.path(63),std::ios::binary)<<"broken";auto untouched=loaded.bpm;
 require(bank.scan()[63]==2&&!bank.load(63,loaded,error)&&loaded.bpm==untouched,"corrupt slot fails without altering live workspace");
 require(!bank.save(64,original,error)&&!bank.load(-1,loaded,error),"slot bounds");
 std::filesystem::remove(bank.path(0));require(saveProject(original,legacy,error),"synthetic legacy fixture");require(bank.load(0,loaded,error),"legacy single-session file offered in slot1");
 auto bytes=std::filesystem::file_size(legacy);require(bank.save(0,loaded,error)&&std::filesystem::file_size(legacy)==bytes,"slot1 save leaves legacy file intact");
 Editor e;e.project=blankProject();e.action(Action::Session);require(e.page==Page::Sessions&&!e.playing,"session button opens grid without load");
 e.action(Action::Left);require(e.sessionSlot==7,"horizontal wrap within grid row");e.action(Action::Up);require(e.sessionSlot==63,"vertical wrap");
 e.action(Action::Enter);require(e.page==Page::Session&&e.sessionSlot==63,"enter opens selected slot settings");
 e.action(Action::Right);require(e.project.bpm==120,"the cross alone never changes a value");
 e.action(Action::Enter);e.action(Action::Right);e.action(Action::Down);require(e.project.bpm==121&&e.sessionField==1&&!e.sessionEdit,"moving leaves edit mode");
 e.action(Action::Right);require(e.sessionField==SessionDie&&e.project.scale==1,"RIGHT from SCALE selects the die");e.action(Action::Left);
 e.action(Action::Enter);e.action(Action::Right);require(e.project.bpm==121&&e.project.scale==2,"slot settings retain tempo and scale editing");e.action(Action::Enter);
 e.action(Action::Down);e.action(Action::Down);require(e.sessionField==3,"fourth field: switch quantize");e.action(Action::Down);require(e.sessionField==0,"settings hold four fields");
 e.action(Action::Cancel);require(e.page==Page::Sessions,"back returns grid");e.action(Action::Session);require(e.page==Page::Pattern,"session tap exits grid");
 AudioStats stats;for(int width:{640,1024})for(int slot=0;slot<64;slot++){
  e.page=Page::Sessions;e.sessionSlot=slot;e.sessionState=bank.scan();Canvas c(width,width*3/4);drawUI(c,e,stats);for(auto pixel:c.pixels)require((pixel>>24)==255,"all64 cells render opaque on both screens");
 }
 Editor boot;boot.project=blankProject();for(auto &t:boot.project.tracks)for(auto &s:t.steps)require(s.note==-1,"startup workspace blank even when slots exist");
 std::filesystem::remove_all(temp);puts("PASS: 64 isolated slots, CRC failure, legacy fallback, grid and slot settings");
}
