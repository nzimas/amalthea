#include "ui.hpp"
#include "melody.hpp"
#include "sessions.hpp"
#include <algorithm>
#include <filesystem>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace amalthea;
static void require(bool ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
  Editor defaults;for(const auto&track:defaults.density)for(auto value:track)require(value==45,"all columns default to45 percent");
  RandomizerButton button;
  button.press(100);button.press(200);
  require(button.tick(899)==Action::Reserved,"no action before threshold or on repeat press");
  require(button.release(899)==Action::RandomizerOptions,"tap opens settings only");
  button.press(1000);
  require(button.tick(1800)==Action::RandomizeColumn,"hold executes at threshold");
  require(button.tick(3000)==Action::Reserved&&button.release(3100)==Action::Reserved,"hold fires once without tap on release");
  button.press(4000);require(button.release(4800)==Action::RandomizeColumn,"late release executes even without tick");
  require(button.release(4900)==Action::Reserved,"orphan release ignored");
  Editor options;options.project=blankProject();auto before=options.project;
  options.action(Action::RandomizerOptions);
  require(options.page==Page::Randomizer,"options screen opens");
  options.action(Action::Left);options.action(Action::ShiftDown);options.action(Action::Left);options.action(Action::ShiftUp);
  require(options.density[0][0]==34&&options.density[0][1]==45&&options.density[1][0]==45,"density scoped to column/track with fine/coarse adjustment");
  require(options.revision==0&&options.undoStack.empty()&&std::memcmp(&before,&options.project,sizeof(Project))==0,"settings do not mutate audio/session or undo");
  options.action(Action::RandomizerOptions);require(options.page==Page::Pattern,"second tap returns without randomizing");
  options.action(Action::RandomizerOptions);options.action(Action::RandomizeColumn);
  require(options.page==Page::Pattern&&options.undoStack.size()==1,"hold from options executes once and returns");
  for(bool overrideColumn:{false,true})for(int len:{1,5,7,16})for(int density:{0,20,25,50,75,100})for(int col=0;col<Columns;col++) {
    Editor e;e.project=demoProject();e.track=3;e.column=col;e.shift=col==1;
    e.project.tracks[3].length=overrideColumn?16:len;
    e.project.tracks[3].columns[col].length=overrideColumn?len:0;e.density[3][col]=density;
    auto original=e.project;e.action(Action::RandomizeColumn);
    auto &t=e.project.tracks[3];int count=0;
    for(int r=0;r<len;r++) {
      auto &s=t.steps[r];auto &a=original.tracks[3].steps[r];
      if(col==0){count+=s.note>=0;require(s.instrument==t.instrument,"generated notes carry default instrument");}
      if(col==1)count+=s.instrument!=NoInstrument;
      if(col==2||col==3)count+=s.fx[col-2].type!=FxType::None;
      if(col==4)count+=s.modifier.semitones!=0;
      auto normalized=s;
      if(col==0){normalized.note=a.note;normalized.instrument=a.instrument;}
      if(col==1)normalized.instrument=a.instrument;
      if(col==2||col==3)normalized.fx[col-2]=a.fx[col-2];
      if(col==4)normalized.modifier=a.modifier;

      require(std::memcmp(&normalized,&a,sizeof(Step))==0,"other step columns preserved");
    }
    require(count==(len*density+50)/100,"density uses current selected column length, including7 steps at20%, independent of track length");
    for(int tr=0;tr<Tracks;tr++)for(int r=0;r<MaxRows;r++)if(tr!=3||r>=len){
      auto actual=e.project.tracks[tr].steps[r];const auto &prior=original.tracks[tr].steps[r];
      // NOTE generation also fills its companion instrument lane with the track
      // default (existing behavior), independently of the NOTE density/length.
      if(tr==3&&col==0&&r<columnLength(t,1)){require(actual.instrument==t.instrument,"companion instrument lane has default");actual.instrument=prior.instrument;}
      require(std::memcmp(&actual,&prior,sizeof(Step))==0,"hidden selected-column rows/other tracks preserved");
    }
    e.action(Action::Undo);require(std::memcmp(&e.project.tracks[3],&original.tracks[3],sizeof(Track))==0&&e.project.seed==original.seed,"undo restores column and seed");
  }
  for(int col:{2,3})for(bool shifted:{false,true})for(int density:{0,20,65,100}){
    Editor e;e.project=demoProject();e.track=4;e.column=col;e.density[4][col]=density;
    e.project.tracks[4].columns[col].length=7;
    for(auto &track:e.project.tracks)for(auto &step:track.steps){step.condition={2,7};for(auto &condition:step.eventConditions)condition={2,3};}
    auto original=e.project;
    if(shifted)e.action(Action::ShiftDown);
    RandomizerButton gesture;gesture.press(0);e.action(gesture.tick(800));
    require(gesture.release(900)==Action::Reserved,"hold randomizes exactly once");
    bool changed=false;
    for(int tr=0;tr<Tracks;tr++)for(int row=0;row<MaxRows;row++){
      auto step=e.project.tracks[tr].steps[row];const auto &prior=original.tracks[tr].steps[row];
      if(tr==4&&row<7){
        auto condition=eventCondition(step,col);auto before=eventCondition(prior,col);
        changed|=condition.every!=before.every||condition.occurrence!=before.occurrence;
        if(shifted){require(condition.every>=1&&condition.every<=8&&condition.occurrence>=1&&condition.occurrence<=condition.every,"Shift FX conditions are positive and bounded");if(step.fx[col-2].type==FxType::None)require(condition.every==1&&condition.occurrence==1,"empty generated FX cells get default condition");}
        else require(condition.every==before.every&&condition.occurrence==before.occurrence,"plain FX hold preserves conditions even on cleared cells");
        step.fx[col-2]=prior.fx[col-2];eventCondition(step,col)=before;
      }
      require(std::memcmp(&step,&prior,sizeof(Step))==0,"FX+condition randomization isolates sibling cells, hidden rows and other tracks");
    }
    require(!shifted||changed,"Shift hold actually changes FX conditions");
    require(e.undoStack.size()==1,"effects and conditions share one undo transaction");
    e.action(Action::ShiftUp);e.action(Action::Undo);require(encodeProject(e.project)==encodeProject(original),"undo restores FX, conditions and random seed together");
  }
  Editor patch;patch.project=blankProject();patch.column=1;patch.density[0][1]=0;patch.action(Action::RandomizeColumn);
  require(trackPatch(patch.project,0,0).seed!=0&&patch.project.tracks[0].steps[0].instrument==patch.project.tracks[0].instrument,"plain patch redraw independent of step density, column follows the track instrument");
  {Editor s;s.project=blankProject();s.column=1;s.density[0][1]=100;s.action(Action::ShiftDown);s.action(Action::RandomizeColumn);
   require(s.notice=="INSTRUMENT COLUMN RANDOMIZED"&&s.noticeDetailSerial==s.noticeSerial,"Shift hold shows the instrument column overlay");
}
  AudioStats stats;
  for(int rows:{8,16})for(int width:{640,1024}){
    Editor e;e.visibleRows=rows;e.row=rows-1;e.column=4;Canvas c(width,width*3/4);drawUI(c,e,stats);
    auto at=[&](int x,int y){return c.pixels[(y*width/1024)*width+x*width/1024]&0xffffff;};
    require(at(330,738)==trackColor(0),"last row reaches reclaimed footer area");
    require(at(855,750)==0,"rows stay within screen");
    e.page=Page::Randomizer;drawUI(c,e,stats);
    for(auto pixel:c.pixels)require((pixel>>24)==255,"options render opaque");
  }
  // MODE: first row, toggled with left/right; density keeps its value; RANDOM is the default.
  {
    Editor e;e.project=blankProject();e.action(Action::RandomizerOptions);
    require(e.randomizerField==1&&!euclideanMode(e.density[0][0]),"randomizer opens on DENSITY in RANDOM mode");
    e.action(Action::Up);require(e.randomizerField==0,"MODE sits above DENSITY");
    e.action(Action::Right);require(euclideanMode(e.density[0][0])&&densityPercent(e.density[0][0])==45&&!euclideanMode(e.density[0][1]),"MODE toggles EUCLIDEAN for this column only");
    e.action(Action::Down);e.action(Action::Right);require(euclideanMode(e.density[0][0])&&densityPercent(e.density[0][0])==46,"density edits keep the mode");
    e.action(Action::Up);e.action(Action::Left);require(!euclideanMode(e.density[0][0])&&e.density[0][0]==46,"MODE returns to RANDOM");
  }
  // EUCLIDEAN: onsets are exactly an evenly spread rotation, on every column and generator.
  for(int engine:{0,1,FmDrumEngine})for(int col=0;col<Columns;col++)for(int len:{5,7,16})for(int pct:{25,45,70})for(uint32_t seed:{3u,99u}){
    Editor e;e.project=blankProject();e.project.seed=seed;e.entropy=seed*7;e.column=col;e.shift=col==1;
    editTrackPatch(e.project,0,e.project.tracks[0].instrument).engine=engine;
    e.project.tracks[0].length=len;e.density[0][col]=uint8_t(EuclideanMode|pct);
    e.action(Action::RandomizeColumn);
    const auto &t=e.project.tracks[0];bool on[16]{};int count=0;
    for(int r=0;r<len;r++){const auto &s=t.steps[r];on[r]=col==0?s.note>=0:col==1?s.instrument!=NoInstrument:col<4?s.fx[col-2].type!=FxType::None:s.modifier.semitones!=0;count+=on[r];}
    int want=(len*pct+50)/100;
    require(count==want,"euclidean count follows density");
    bool match=false;
    for(int rot=0;rot<len&&!match;rot++){bool ref[16]{};euclideanRows(len,want,rot,ref);match=std::equal(ref,ref+len,on);}
    require(match,"onsets form a rotated euclidean rhythm");
  }
  {
    bool on[16]{};euclideanRows(16,5,0,on);int gaps[5],n=0,last=-1,first=-1;
    for(int r=0;r<16;r++)if(on[r]){if(last>=0)gaps[n++]=r-last;else first=r;last=r;}
    gaps[n++]=16-last+first;for(int i=0;i<5;i++)require(gaps[i]==3||gaps[i]==4,"E(5,16) spreads onsets evenly");
  }
  // Older sessions store plain percentages and still load; the mode bit round-trips.
  {
    Editor saved;saved.density[2][3]=uint8_t(EuclideanMode|60);
    auto dir=std::filesystem::temp_directory_path()/("amalthea-mode-test-"+std::to_string(getpid()));std::filesystem::create_directories(dir);
    SessionBank bank((dir/"session.amalthea").string());std::string error;
    require(bank.save(1,saved,error),"save with euclidean mode");Editor loaded;require(bank.load(1,loaded,error),"load with euclidean mode");
    require(loaded.density==saved.density,"mode and density round-trip");std::filesystem::remove_all(dir);
  }
  // ERASE mode: Shift+Enter toggles it; navigation stays free; Enter clears only the selected cell.
  {
    Editor e;e.project=demoProject();e.track=0;e.row=0;e.column=0;
    auto &steps=e.project.tracks[0].steps;
    steps[0].note=60;steps[0].length=4;steps[0].locks[0]=30;steps[0].instrument=2;steps[0].modifier.semitones=5;
    steps[2].fx[0]={FxType::Volume,40};eventCondition(steps[2],2)=cycleCondition(5);
    e.action(Action::ShiftDown);e.action(Action::Enter);e.action(Action::ShiftUp);
    require(e.erasing&&!e.editing,"Shift+Enter on NOTE enters erase mode");
    Canvas c(1024,768);AudioStats stats;drawUI(c,e,stats);
    auto cell=[&](int x,int y){return c.pixels[y*1024+x]&0xffffff;};
    require(cell(90,166)==0xff3b30&&cell(100,175)==(trackColor(0)&0xffffff),"erase cursor: red frame around the track colour");
    e.action(Action::Enter);
    require(steps[0].note==-1&&steps[0].length==1&&steps[0].locks[0]==Inherit&&steps[0].instrument==2&&steps[0].modifier.semitones==5&&e.erasing,"Enter erases the note and its controls only");
    e.action(Action::Right);require(e.column==4,"navigation moves to MOD");e.action(Action::Enter);require(steps[0].modifier.semitones==0&&steps[0].instrument==2,"MOD cell erased");
    e.action(Action::Right);e.action(Action::Enter);require(steps[0].instrument==NoInstrument,"INST cell erased");
    e.action(Action::Right);e.action(Action::Down);e.action(Action::Down);e.action(Action::Enter);
    require(e.column==2&&steps[2].fx[0].type==FxType::None&&eventCondition(steps[2],2).every==Condition{}.every,"FX cell and its condition erased");
    size_t undo=e.undoStack.size();e.action(Action::Enter);require(e.undoStack.size()==undo,"empty cell adds no undo step");
    e.action(Action::ShiftDown);e.action(Action::Enter);e.action(Action::ShiftUp);
    require(!e.erasing&&e.page==Page::Pattern,"Shift+Enter on FX leaves erase mode");
    e.action(Action::ShiftDown);e.action(Action::Enter);e.action(Action::ShiftUp);
    require(e.erasing&&e.page==Page::Pattern,"Shift+Enter on FX enters erase mode too");
    e.action(Action::Cancel);e.action(Action::ShiftDown);e.action(Action::Enter);e.action(Action::ShiftUp);e.action(Action::Cancel);
    require(!e.erasing,"Cancel leaves erase mode");
    e.action(Action::ShiftDown);e.action(Action::Enter);e.action(Action::ShiftUp);e.action(Action::TrackSettings);
    require(!e.erasing,"leaving the spreadsheet leaves erase mode");
    e.action(Action::TrackSettings);e.undo();require(steps[2].fx[0].type==FxType::Volume,"erasures undo");
  }
  // Shift+F1 / F2: rotate only the column under the cursor, within its own length.
  {
    Editor e;e.project=blankProject();e.track=1;e.row=9;e.column=0;
    auto &t=e.project.tracks[1];auto &steps=t.steps;
    t.columns[4].length=4;
    steps[1].note=62;steps[1].length=3;steps[1].locks[1]=70;steps[15].note=50;eventCondition(steps[1],0)=cycleCondition(4);
    steps[0].fx[1]={FxType::Volume,20};steps[3].modifier.semitones=7;eventCondition(steps[3],4)=cycleCondition(4);
    e.action(Action::ShiftDown);e.action(Action::NextTrack);
    require(e.track==1&&e.row==9&&e.column==0,"Shift+F2 keeps the track and the cursor");
    require(steps[2].note==62&&steps[2].length==3&&steps[2].locks[1]==70&&steps[1].note==-1,"notes move forward one step with their controls");
    require(eventCondition(steps[2],0).every==cycleCondition(4).every,"conditions move with their events");
    require(steps[0].note==50,"the last row wraps to the first");
    require(steps[0].fx[1].type==FxType::Volume&&steps[3].modifier.semitones==7,"other columns stay where they are");
    e.column=3;e.action(Action::NextTrack);
    require(steps[1].fx[1].type==FxType::Volume&&steps[0].fx[1].type==FxType::None&&steps[2].note==62,"an FX column rotates on its own");
    e.column=4;e.action(Action::NextTrack);
    require(steps[0].modifier.semitones==7&&eventCondition(steps[0],4).every==cycleCondition(4).every,"a 4-step MOD column wraps within its own length");
    e.column=0;e.action(Action::PrevTrack);e.action(Action::PrevTrack);e.action(Action::ShiftUp);
    require(steps[0].note==62&&steps[14].note==50&&steps[1].fx[1].type==FxType::Volume,"Shift+F1 rotates backward");
    for(int i=0;i<5;i++)e.undo();
    require(steps[1].note==62&&steps[15].note==50&&steps[0].fx[1].type==FxType::Volume&&steps[3].modifier.semitones==7,"rotations undo");
    e.action(Action::NextTrack);require(e.track==2,"plain F2 still changes track");
  }
  // A plain message never shows the last dialled value under it.
  {
    Editor e;e.noticeDetail="1/4";e.noticeDetailSerial=e.noticeSerial;e.say("SAVING SESSION 1");
    require(e.notice=="SAVING SESSION 1"&&e.noticeDetail.empty(),"save messages stand alone");
  }
  puts("PASS: tap/hold, density population, scope, undo, full-height rows, euclidean and erase modes, rotation");
}
