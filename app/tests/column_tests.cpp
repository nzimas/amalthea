#include "legacy_project.hpp"
#include "audio.hpp"
#include "ui.hpp"
#include "walk.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <filesystem>
using namespace amalthea;
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static void render(Audio&a,int frames){std::vector<float>x(frames*2);a.render(x.data(),frames);}
static uint32_t crc(const std::vector<unsigned char>&b){uint32_t c=~0u;for(auto v:b){c^=v;for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
int main(){
 Editor e;e.project=blankProject();e.column=2;e.action(Action::ColumnSettings);
 require(e.page==Page::Column,"A opens selected column settings");e.action(Action::Right);require(e.project.tracks[0].columns[2].length==1,"column length overrides");
 e.action(Action::Down);e.action(Action::Right);require(e.project.tracks[0].columns[2].clock==0,"column clock overrides");
 e.action(Action::Down);e.action(Action::Right);require(e.project.tracks[0].columns[2].direction==0,"column direction overrides");
 e.action(Action::Left);require(e.project.tracks[0].columns[2].direction==255,"direction returns to track inheritance");
 e.action(Action::Undo);require(e.project.tracks[0].columns[2].direction==0,"column settings undo");e.action(Action::ColumnSettings);require(e.page==Page::Pattern,"A closes settings");
 e.action(Action::Session);e.action(Action::ColumnSettings);require(e.page==Page::Pattern,"A remains Back on other pages");
 auto &edit=e.project.tracks[0];edit.length=3;edit.columns[0].length=11;e.column=0;e.row=10;e.action(Action::Down);require(e.row==0,"cursor uses column length above reference track length");e.action(Action::Left);require(e.column==3&&e.row==0,"column navigation clamps cursor to destination (left of NOTE wraps to FX 2)");e.action(Action::Right);e.action(Action::Right);require(e.column==4,"MOD is the second column");e.action(Action::Left);
 e.column=0;e.density[0][0]=100;e.action(Action::RandomizeColumn);for(int r=0;r<11;r++)require(edit.steps[r].note>=0,"randomizer covers selected column effective length");require(edit.steps[11].note==-1,"randomizer leaves hidden pitches untouched");
 e.action(Action::ShiftDown);e.action(Action::Down);e.action(Action::ShiftUp);require(edit.steps[10].note>=0&&edit.steps[10].note<60,"octave gesture includes pitch rows beyond track length");
 edit.columns[3].length=16;edit.steps[14].note=80;e.column=3;e.row=14;e.action(Action::Enter);e.action(Action::ShiftDown);e.action(Action::Up);e.action(Action::ShiftUp);require(e.pending.note==80,"octave gesture preserves hidden pitch in an edited longer FX column");e.action(Action::Enter);require(edit.steps[14].note==80,"committing that FX cell cannot rewrite hidden pitch");
 Project p=blankProject();p.tracks[0].length=4;
 const int lengths[]={5,3,7,11,2,13},clocks[]={4,2,3,0,5,1};
 for(int c=0;c<Columns;c++)p.tracks[0].columns[c]={uint8_t(lengths[c]),uint8_t(clocks[c]),0};
 for(int r=0;r<16;r++){p.tracks[0].steps[r].note=60+r;p.tracks[0].steps[r].instrument=r%8;}
 Audio clocksAudio;clocksAudio.submit(p,true);render(clocksAudio,88200);
 for(int c=0;c<Columns;c++){int count=int(16*clockRate(clocks[c]));require(clocksAudio.stats.columnAdvances[0][c]==unsigned(count),"six independently divided clocks have expected counts");require(clocksAudio.stats.columnRow[0][c]==(count-1)%lengths[c],"each column wraps at its own length");require(clocksAudio.stats.columnCycle[0][c]==unsigned((count-1)/lengths[c]),"per-column cycle counts");}
 require(clocksAudio.stats.triggers[0]==32&&clocksAudio.stats.lastInstrument[0]==1,"NOTE triggers use current independent INST position");
 auto pitchAdv=clocksAudio.stats.columnAdvances[0][0].load();auto instAdv=clocksAudio.stats.columnAdvances[0][1].load();p.tracks[0].columns[2].length=2;clocksAudio.submit(p,true);render(clocksAudio,32);
 require(clocksAudio.stats.columnAdvances[0][0]==pitchAdv+1&&clocksAudio.stats.columnAdvances[0][1]==instAdv+1,"editing FX geometry does not reset other clocks");require(clocksAudio.stats.columnRow[0][2]==0,"edited FX lane restarts alone");
 // Creating notes during playback restarts NOTE without disturbing running value lanes.
 p=blankProject();p.tracks[0].columns[2].length=3;Audio adding;adding.submit(p,true);render(adding,9000);
 auto fxAdv=adding.stats.columnAdvances[0][2].load();auto fxRow=adding.stats.columnRow[0][2].load();p.tracks[0].steps[0].note=60;adding.submit(p,true);render(adding,32);
 require(adding.stats.triggers[0]==1&&adding.stats.columnRow[0][0]==0&&adding.stats.columnRow[0][2]==fxRow&&adding.stats.columnAdvances[0][2]==fxAdv,"note creation preserves other column phases");
 // Independent instrument/modifier sampling; each NOTE cell owns its condition.
 p=blankProject();auto &t=p.tracks[0];t.length=2;t.instrument=7;t.columns[1].length=3;t.columns[4]={3,2,0};
 t.steps[0].note=60;t.steps[1].note=62;t.steps[0].instrument=7;t.steps[1].instrument=6;t.steps[2].instrument=5;t.steps[0].condition={1,2};
 t.steps[0].modifier={0,true};t.steps[1].modifier={12,true};t.steps[2].modifier={-12,true};
 Audio combinations;combinations.submit(p,true);render(combinations,44100);
 require(combinations.stats.triggers[0]==6&&combinations.stats.lastNote[0]==62&&combinations.stats.lastInstrument[0]==6,"held condition and modifier combine with current pitch/instrument on NOTE ticks");
 // Matching settings join the currently sounding lane, including random traversal.
 for(int direction=0;direction<Directions;direction++){
  p=blankProject();p.tracks[0].direction=direction;Audio grouped;grouped.submit(p,true);render(grouped,9000);
  auto same=[&](){for(int c=1;c<Columns;c++)require(grouped.stats.columnRow[0][c]==grouped.stats.columnRow[0][0]&&grouped.stats.columnCycle[0][c]==grouped.stats.columnCycle[0][0],"equal musical instructions stay synchronized");};same();
  p.tracks[0].columns[2].length=3;p.tracks[0].columns[2].clock=5;grouped.submit(p,true);render(grouped,17000);
  require(grouped.stats.columnAdvances[0][2]>grouped.stats.columnAdvances[0][0],"faster FX playhead reflects actual lane ticks");
  p.tracks[0].columns[2]={};grouped.submit(p,true);render(grouped,32);same();render(grouped,12000);same();
 }
 // An FX clock changes an already sounding note, without retriggering NOTE.
 p=blankProject();p.tracks[0].instrument=7;p.tracks[0].columns[0]={1,0,0};p.tracks[0].columns[2]={2,3,0};p.tracks[0].steps[0].note=60;p.tracks[0].steps[0].fx[0]={FxType::Volume,100};p.tracks[0].steps[1].fx[0]={FxType::Volume,0};
 Audio fx;fx.submit(p,true);render(fx,6000);std::vector<float> silence(512*2);fx.render(silence.data(),512);double power=0;for(float v:silence)power+=v*v;
 require(fx.stats.triggers[0]==1&&fx.stats.columnRow[0][2]==1&&power<1e-6,"FX clock mutes held audio between NOTE ticks");
 // All directions remain bounded and deterministic with unequal lengths/rates.
 for(int d=0;d<Directions;d++){
  p=blankProject();for(int c=0;c<Columns;c++)p.tracks[0].columns[c]={uint8_t(1+(d+c)%16),uint8_t(c),uint8_t(d)};
  Audio a;a.submit(p,true);render(a,44100);
  for(int c=0;c<Columns;c++){AmaltheaWalk walk;amalthea_walk_start(&walk,p.seed^0x9e3779b9u);int count=a.stats.columnAdvances[0][c],expected=-1;for(int i=0;i<count;i++)expected=amalthea_walk_next(&walk,d,columnLength(p.tracks[0],c));require(a.stats.columnRow[0][c]==expected,"all19 independent column directions match traversal");}
 }
 // New session fields round-trip; strip the new tail to exercise actual v6 migration.
 std::string file="column-test.amalthea",error;require(saveProject(p,file,error),"v8 save");Project loaded;require(loadProject(loaded,file,error),"v8 load");for(int c=0;c<Columns;c++)require(loaded.tracks[0].columns[c].length==p.tracks[0].columns[c].length&&loaded.tracks[0].columns[c].clock==c&&loaded.tracks[0].columns[c].direction==18,"column profiles persisted");
 std::ifstream input(file,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});input.close();bytes=legacyBankBytes(bytes);bytes.resize(bytes.size()-EventConditionBytes-LegacyTrackPatchBytes-Tracks*6*3-4-Tracks*2*7-4-360);bytes[4]=6;auto sum=crc(std::vector<unsigned char>(bytes.begin()+12,bytes.end()));for(int i=0;i<4;i++)bytes[8+i]=(sum>>(8*i))&255;std::ofstream old(file,std::ios::binary);old.write((char*)bytes.data(),bytes.size());old.close();require(loadProject(loaded,file,error),"v6 session remains readable");for(auto &tr:loaded.tracks)for(int c=0;c<Columns;c++)require(tr.columns[c].length==0&&tr.columns[c].clock==255&&tr.columns[c].direction==255,"old sessions inherit unchanged track traversal");std::filesystem::remove(file);
 // v7 engine IDs migrate explicitly; only retained engines keep their patch seeds.
 p=blankProject();for(int slot=0;slot<Instruments;slot++)p.patches[slot]={0,12345};require(saveProject(p,file,error),"engine migration fixture");
 std::ifstream current(file,std::ios::binary);bytes.assign(std::istreambuf_iterator<char>(current),{});current.close();bytes=legacyBankBytes(bytes);bytes.resize(bytes.size()-EventConditionBytes-LegacyTrackPatchBytes-4-Tracks*2*7-4-360);bytes[4]=7;
 const size_t patchOffset=12+8+Tracks*(3+MaxRows*11)+2;
 for(int slot=0;slot<8;slot++)bytes[patchOffset+slot*5]=slot;
 sum=crc(std::vector<unsigned char>(bytes.begin()+12,bytes.end()));for(int j=0;j<4;j++)bytes[8+j]=(sum>>(8*j))&255;
 std::ofstream legacy(file,std::ios::binary);legacy.write((char*)bytes.data(),bytes.size());legacy.close();require(loadProject(loaded,file,error),"v7 engine migration loads");
 const int migrated[]={3,4,0,5,4,2,1,3};for(int slot=0;slot<8;slot++){require(loaded.patches[slot].engine==migrated[slot],"old engine mapped to selected bank");require(loaded.patches[slot].seed==unsigned(slot==2||slot==5||slot==6?12345:0),"retained recipes preserved; removed engine seeds reset");}
 bytes[patchOffset]=255;sum=crc(std::vector<unsigned char>(bytes.begin()+12,bytes.end()));for(int j=0;j<4;j++)bytes[8+j]=(sum>>(8*j))&255;
 std::ofstream corrupt(file,std::ios::binary);corrupt.write((char*)bytes.data(),bytes.size());corrupt.close();require(!loadProject(loaded,file,error),"invalid legacy engine ID rejected safely");std::filesystem::remove(file);
 AudioStats stats;for(int rows:{8,16})for(int width:{640,1024}){
  Editor view;view.project=blankProject();view.row=0;view.column=0;view.visibleRows=rows;view.playing=true;
  for(int c=0;c<Columns;c++)stats.columnRow[0][c].store(c+1);
  Canvas first(width,width*3/4),next(width,width*3/4);drawUI(first,view,stats);
  auto at=[&](const Canvas&canvas,int x,int y){return canvas.pixels[(y*width/1024)*width+x*width/1024]&0xffffff;};
  /* stored column order; MOD shown second */ const int left[]={92,460,621,851,276};int height=rows==8?72:36;
  for(int c=0;c<Columns;c++){require(at(first,left[c]+3,164+(c+1)*height+3)!=0,"each column has its own moving cell highlight");require(at(first,left[c]+3,164+((c+2)%7+1)*height+3)==0,"column playhead does not spill to unrelated rows");}
  for(int c=0;c<Columns;c++)stats.columnRow[0][c].store((c+2)%7+1);drawUI(next,view,stats);require(at(first,95,167)==trackColor(0)&&at(next,95,167)==trackColor(0)&&view.row==0,"editing cursor remains fixed while all five playheads move");
 }
 puts("PASS: A settings, inheritance, five clock/direction/length transports, held values/FX, migration and independent playheads");
}
