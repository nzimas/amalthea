// FX LENGTH, dry/wet step values, ringing tails and FUNC+F1/F2 row selection.
#include "audio.hpp"
#include "effects.hpp"
#include "ui.hpp"
#include "track_macros.hpp"
#include "ported_inserts.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
using namespace amalthea;
static void check(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static void tick(Audio&a,int n){float data[64];while(a.stats.advances[0].load()<uint64_t(n+1))a.render(data,32);}
static double power(Audio&a){float data[512];a.render(data,256);double p=0;for(float v:data)p+=v*v;return p;}
int main(int argc,char**argv){
  // Editor: FX tabs are DRY/WET MIX, LENGTH, CONDITION; a new effect starts half wet.
  {
    Editor e;e.project=blankProject();e.column=2;e.row=3;
    e.action(Action::Enter);check(e.eventTabCount()==3&&e.eventTab==0,"FX edit opens on DRY/WET MIX of three tabs");
    e.action(Action::Right);check(e.pending.fx[0].type!=FxType::None&&e.pending.fx[0].value==DefaultFxMix,"a newly chosen effect starts at 50% wet");
    e.action(Action::NextTrack);check(e.lengthTab()&&e.track==0,"F2 selects LENGTH");
    e.action(Action::Up);e.action(Action::Up);e.action(Action::Up);
    e.action(Action::NextTrack);check(e.conditionTab(),"then CONDITION");
    for(int i=0;i<200;i++){e.eventTab=1;e.action(Action::Up);}check(e.pending.fx[0].length==MaxFxLength,"length is bounded");
    for(int i=0;i<MaxFxLength-4;i++)e.action(Action::Down);
    e.action(Action::Enter);check(e.project.tracks[0].steps[3].fx[0].length==4,"Enter commits FX LENGTH");
    Canvas c(1024,768);AudioStats stats;e.action(Action::Enter);e.eventTab=1;drawUI(c,e,stats);
    if(argc>1)writePPM(c,std::string(argv[1])+"/fx-length.ppm");
    e.action(Action::Cancel);
    Project loaded;std::string error;
    check(decodeProject(loaded,encodeProject(e.project),error)&&loaded.tracks[0].steps[3].fx[0].length==4,"FX LENGTH survives a session round trip");
  }
  // Audio: an FX row holds its slot for LENGTH FX-column steps, then the slot is dry again.
  {
    Project p=blankProject();auto&t=p.tracks[0];t.length=8;
    for(int r=0;r<8;r++)t.steps[r].note=60;
    t.steps[0].fx[0]={FxType::Volume,0,3};
    Audio audio;audio.submit(p,true);
    std::array<double,8> level{};
    for(int step=0;step<8;step++){tick(audio,step);level[step]=power(audio);}
    check(level[0]==0&&level[1]==0&&level[2]==0,"a 3-step effect covers its row and the two empty rows after it");
    check(level[3]>0&&level[4]>0,"after its length the slot returns to dry");
  }
  // A new effect inside the length replaces the held one.
  {
    Project p=blankProject();auto&t=p.tracks[0];t.length=8;
    for(int r=0;r<8;r++)t.steps[r].note=60;
    t.steps[0].fx[0]={FxType::Volume,0,6};t.steps[2].fx[0]={FxType::Volume,100,1};
    Audio audio;audio.submit(p,true);std::array<double,8> level{};
    for(int step=0;step<8;step++){tick(audio,step);level[step]=power(audio);}
    check(level[0]==0&&level[1]==0&&level[2]>0&&level[3]>0,"a later effect row takes over the slot");
  }
  // In the tracker: a reverb row of LENGTH 1 still rings over the following empty rows.
  {
    auto run=[](FxType type){
      Project p=blankProject();auto&t=p.tracks[0];t.length=16;t.steps[0].note=60;t.steps[0].length=1;
      t.steps[0].fx[0]={type,100,1};
      Audio audio;audio.submit(p,true);double late=0;
      for(int step=0;step<10;step++){tick(audio,step);if(step>=4)late+=power(audio);}
      return late;
    };
    double dry=run(FxType::None),space=run(FxType::Space);
    check(space>dry*2+1e-9,"the reverb tail outlives its one-step effect");
  }
  // Built-in inserts: the step value is their dry/wet mix.
  {
    InsertEffect fx;float dry[64],wet[64];for(int i=0;i<64;i++)dry[i]=wet[i]=.4f*std::sin(i*.3f);
    fx.configure({FxType::Wavefold,0},120);fx.processBlock(wet,32);
    check(!std::memcmp(dry,wet,sizeof dry),"0% mix is the dry signal");
    for(int i=0;i<64;i++)wet[i]=dry[i];fx.configure({FxType::Wavefold,100},120);fx.processBlock(wet,32);
    double diff=0;for(int i=0;i<64;i++)diff+=std::abs(wet[i]-dry[i]);check(diff>.1,"100% mix is fully processed");
  }
  // Reverb tail: ending the effect (or replacing it) lets it ring out instead of cutting it.
  for(auto next:{FxType::None,FxType::Wavefold}){
    InsertEffect fx;fx.configure({FxType::Space,100},120);
    std::vector<float> block(512);
    for(int b=0;b<40;b++){for(int i=0;i<256;i++)block[2*i]=block[2*i+1]=b<4?.5f*std::sin(i*.7f+b):0;fx.processBlock(block.data(),256);}
    fx.configure({next,60},120);check(fx.tailCount()==1,"the reverb keeps ringing after its step ends");
    double early=0;std::fill(block.begin(),block.end(),0.f);fx.processBlock(block.data(),256);for(float v:block)early+=v*v;
    check(early>1e-8,"the tail is audible right after the effect ends");
    int blocks=0;while(fx.tailCount()&&blocks<44100*40/256){std::fill(block.begin(),block.end(),0.f);fx.processBlock(block.data(),256);blocks++;}
    check(fx.tailCount()==0,"the tail ends once it has decayed");
    fx.configure({FxType::Space,100},120);fx.configure({FxType::None,0},120);fx.configure({FxType::Space,100},120);
    check(fx.tailCount()==0,"the same effect returning continues its own tail");
  }
  // FUNC+F1/F2 selects rows; an edit writes only what it changed to every selected row.
  {
    Editor e;e.project=blankProject();auto&steps=e.project.tracks[0].steps;
    for(int r=0;r<8;r++)steps[r].note=48+r;
    e.row=2;e.column=0;
    e.action(Action::Shift2Down);e.action(Action::NextTrack);e.action(Action::NextTrack);e.action(Action::Shift2Up);
    check(e.track==0&&e.row==4&&e.selectionFirst()==2&&e.selectionLast()==4,"FUNC+F2 selects down from the cursor");
    e.action(Action::Enter);e.action(Action::NextTrack);e.action(Action::NextTrack);check(e.conditionTab(),"condition tab");
    e.action(Action::Up);e.action(Action::Up);e.action(Action::Enter);
    auto c=eventCondition(steps[4],0);
    for(int r=2;r<=4;r++)check(eventCondition(steps[r],0).every==c.every&&eventCondition(steps[r],0).occurrence==c.occurrence&&c.every>1,"the condition reaches every selected row");
    check(steps[2].note==50&&steps[3].note==51&&steps[4].note==52,"unchanged fields keep each row's own value");
    check(eventCondition(steps[1],0).every==1&&eventCondition(steps[5],0).every==1,"rows outside the selection are untouched");
    e.action(Action::Enter);e.action(Action::Up);e.action(Action::Enter);
    check(steps[2].note==53&&steps[3].note==53&&steps[4].note==53,"a changed note becomes the same note on every row");
    e.undo();check(steps[2].note==50&&steps[3].note==51&&steps[4].note==52,"one undo restores the whole selection");
    e.action(Action::Down);check(!e.hasSelection(),"moving the cursor clears the selection");
    e.row=6;e.action(Action::Shift2Down);e.action(Action::PrevTrack);e.action(Action::PrevTrack);e.action(Action::Shift2Up);
    check(e.selectionFirst()==4&&e.selectionLast()==6&&e.row==4,"FUNC+F1 selects up");
    Canvas canvas(1024,768);AudioStats stats;drawUI(canvas,e,stats);
    if(argc>1)writePPM(canvas,std::string(argv[1])+"/selection.ppm");
    e.action(Action::ShiftDown);e.action(Action::Enter);e.action(Action::ShiftUp);check(e.erasing,"erase mode");
    e.action(Action::Enter);check(steps[4].note<0&&steps[5].note<0&&steps[6].note<0&&steps[3].note>=0&&steps[7].note>=0,"ERASE clears every selected row");
    e.undo();check(steps[5].note==53,"erase over a selection undoes in one step");
    e.action(Action::Cancel);check(!e.erasing,"Cancel leaves erase mode");
    e.row=1;e.column=2;e.action(Action::Shift2Down);e.action(Action::NextTrack);e.action(Action::Shift2Up);
    e.action(Action::Enter);e.action(Action::Right);e.action(Action::Enter);
    check(steps[1].fx[0].type==steps[2].fx[0].type&&steps[1].fx[0].value==DefaultFxMix&&steps[2].fx[0].value==DefaultFxMix,"a chosen effect reaches every selected FX row");
    e.action(Action::Cancel);check(!e.hasSelection(),"Cancel clears the selection");
    e.action(Action::NextTrack);check(e.track==1,"plain F2 still changes track");
  }
  // Crazy Ivan at extreme feedback cannot run on by itself after its input stops (or after Stop).
  {
    InsertSettings cfg;   // the default profile self-oscillates on drum hits
    PortedProcessor ivan(FxType::CrazyIvan);PortedPointers ported{};ported[portedIndex(FxType::CrazyIvan)]=&ivan;
    InsertEffect fx;fx.attachPorted(ported);fx.setSettings(cfg);fx.configure({FxType::CrazyIvan,85},120);
    std::vector<float> block(512);double loud=0,late=0;
    for(int b=0;b<44100*12/256;b++){
      for(int i=0;i<256;i++){int n=b*256+i;block[2*i]=block[2*i+1]=n<4410?.6f*std::exp(-n/600.f)*std::sin(n*.09f):0;}
      fx.processBlock(block.data(),256);double e=0;for(float v:block)e+=v*v;
      if(b<20)loud+=e;if(b>=44100*10/256)late+=e;
    }
    check(loud>0&&late<1e-6*loud,"a self-oscillating Crazy Ivan dies away once its input stops");
  }
  // The CORROSION send cannot turn a quiet tail into a lasting loud one.
  {
    TrackMacros m;auto profile=defaultMacros();profile[1].value={0,0,0,50,0,100,100,100};m.configure(profile);   // fine bits, no slew, full drive
    // A loud hit, then its quiet tail: full drive would lift the tail by about 22 dB.
    double tail=0,dry=0;
    for(int i=0;i<44100*2;i++){float x=(i<2205?.5f:.005f*std::exp(-(i-2205)/30000.f))*std::sin(i*.05f),l=x,r=x;m.process(l,r,{0,1,0});if(i>22050){tail+=l*l;dry+=x*x;}}
    check(tail<4.5*dry,"a quiet tail through CORROSION stays within 6 dB of itself");
  }
  // Hold X in the FX editor: new parameters for the chosen effect; Shift+hold X: a new effect.
  {
    Editor e;e.project=blankProject();e.column=2;e.row=0;
    e.project.tracks[0].steps[0].fx[0]={FxType::Space,37,5};
    e.action(Action::Enter);auto before=e.project.tracks[0].inserts[0].space;
    e.action(Action::RandomizeColumn);
    check(e.pending.fx[0].type==FxType::Space&&e.pending.fx[0].value==37&&e.pending.fx[0].length==5,"hold X keeps the effect, its mix and its length");
    check(e.pendingInsertSet&&e.pendingInsert.space!=before,"hold X draws new parameters for it");
    check(e.project.tracks[0].inserts[0].space==before,"nothing is written before Enter");
    e.action(Action::Enter);check(e.project.tracks[0].inserts[0].space!=before,"Enter commits the parameters");
    e.action(Action::Enter);e.action(Action::ShiftDown);e.action(Action::RandomizeColumn);e.action(Action::ShiftUp);
    check(e.pending.fx[0].length==5&&e.pending.fx[0].type!=FxType::None,"Shift+hold X draws a new effect and keeps the length");
    e.action(Action::Cancel);e.row=1;e.action(Action::Enter);e.action(Action::RandomizeColumn);
    check(e.pending.fx[0].type!=FxType::None,"on an empty FX row hold X draws an effect");
  }
  // Hold X on an FX column: every effect placed also gets freshly drawn parameters.
  {
    Editor e;e.project=blankProject();e.column=2;e.density[0][2]=100;auto defaults=e.project.tracks[0].inserts[0];auto before=encodeProject(e.project);
    e.action(Action::RandomizeColumn);
    auto &t=e.project.tracks[0];int placed=0,changed=0;std::vector<FxType> seen;
    for(int r=0;r<columnLength(t,2);r++){auto type=t.steps[r].fx[0].type;if(type==FxType::None||type==FxType::Random)continue;placed++;
      if(std::find(seen.begin(),seen.end(),type)!=seen.end())continue;seen.push_back(type);
      Effect a{type,0},b{type,0};auto x=defaults,y=t.inserts[0];bool differs=false;
      for(int page=0;page<9;page++)for(int f=1;f<insertParameterCount(type,page,x);f++)differs|=insertParameter(a,x,f,page)!=insertParameter(b,y,f,page);
      changed+=differs||insertParameterCount(type,0,x)==1;}
    check(placed>0&&changed==int(seen.size()),"every effect placed by the column randomizer has drawn parameters");
    e.undo();check(encodeProject(e.project)==before,"one undo restores the column and its parameters");
  }
  puts("PASS: FX length, dry/wet mix, ringing tails, row selection");
}
