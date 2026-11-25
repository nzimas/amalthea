// MOD column: empty by default, OFFSET and TRANSPOSE cells, and the FX-row random draws.
#include "core.hpp"
#include "audio.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace amalthea;
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static uint32_t crc(const std::vector<uint8_t>&b,size_t from){uint32_t c=~0u;for(size_t i=from;i<b.size();i++){c^=b[i];for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
// The notes track 0 plays over the first `steps` NOTE steps (one note per row).
static std::vector<int> played(const Project&p,int steps){
  Audio a;a.submit(p,true);std::vector<int> notes;std::vector<float> x(64*2);unsigned seen=0;
  for(int guard=0;guard<200000&&int(notes.size())<steps;guard++){a.render(x.data(),64);
    if(a.stats.triggers[0]!=seen){seen=a.stats.triggers[0];notes.push_back(a.stats.lastNote[0]);}}
  return notes;
}
static Project scale16(){Project p=blankProject();auto&t=p.tracks[0];t.length=16;for(int r=0;r<16;r++)t.steps[r].note=60+r%4;return p;}
int main(){
  // A blank MOD column is empty, like every other column.
  {Project p=blankProject();for(auto&t:p.tracks)for(auto&s:t.steps)require(s.modifier.type==ModifierType::None,"blank MOD cells are empty");
   require(modifierText({})=="---","an empty MOD cell shows ---");
   require(modifierText({2,ModifierType::Offset})=="+02"&&modifierText({-5,ModifierType::Transpose})=="T-5"&&modifierText({12,ModifierType::Transpose})=="T+12","offset and transpose texts");}
  // Older sessions: their default +00 cells load empty, other offsets stay offsets.
  {Project p=blankProject();p.tracks[0].steps[3].modifier={5,ModifierType::Offset};p.tracks[0].steps[4].modifier={0,ModifierType::Offset};
   auto b=encodeProject(p);require(b[4]==25,"sessions are AMAL25");
   for(auto&t:p.tracks)for(auto&s:t.steps)if(s.modifier.type==ModifierType::None)s.modifier.type=ModifierType::Offset;   // as AMAL24 wrote them
   auto old=encodeProject(p);old[4]=24;auto c=crc(old,12);for(int j=0;j<4;j++)old[8+j]=(c>>(8*j))&255;
   Project q;std::string err;require(decodeProject(q,old,err),"AMAL24 still loads");
   require(q.tracks[0].steps[3].modifier.type==ModifierType::Offset&&q.tracks[0].steps[3].modifier.semitones==5,"an old offset stays an offset");
   require(q.tracks[0].steps[4].modifier.type==ModifierType::None&&q.tracks[5].steps[100].modifier.type==ModifierType::None,"an old +00 loads as an empty cell");}
  // AMAL25 keeps every type, including a TRANSPOSE of 0 (which cancels an earlier one).
  {Project p=blankProject();p.tracks[2].steps[0].modifier={-7,ModifierType::Transpose};p.tracks[2].steps[1].modifier={0,ModifierType::Transpose};p.tracks[2].steps[2].modifier={3,ModifierType::Offset};
   Project q;std::string err;require(decodeProject(q,encodeProject(p),err),"AMAL25 round trip");
   for(int r=0;r<3;r++)require(q.tracks[2].steps[r].modifier.type==p.tracks[2].steps[r].modifier.type&&q.tracks[2].steps[r].modifier.semitones==p.tracks[2].steps[r].modifier.semitones,"MOD cells round trip");
   require(q.tracks[2].steps[3].modifier.type==ModifierType::None,"empty cells stay empty");}
  // Editing: UP/DOWN on an empty cell makes an OFFSET; LEFT/RIGHT picks the type.
  {Editor e;e.project=blankProject();e.column=4;e.row=2;
   e.action(Action::Enter);require(e.pending.modifier.type==ModifierType::None,"an edit opens on the empty cell");
   e.action(Action::Up);require(e.pending.modifier.type==ModifierType::Offset&&e.pending.modifier.semitones==1,"UP makes a +01 offset");
   e.action(Action::Right);require(e.pending.modifier.type==ModifierType::Transpose&&e.pending.modifier.semitones==1,"RIGHT turns it into a transpose");
   e.action(Action::Up);e.action(Action::Enter);require(e.project.tracks[0].steps[2].modifier.type==ModifierType::Transpose&&e.project.tracks[0].steps[2].modifier.semitones==2,"T+2 committed");
   e.action(Action::Enter);e.action(Action::Right);require(e.pending.modifier.type==ModifierType::None,"RIGHT again empties the cell");e.action(Action::Left);require(e.pending.modifier.type==ModifierType::Transpose,"LEFT wraps back");
   e.action(Action::Cancel);
   e.action(Action::Undo);require(e.project.tracks[0].steps[2].modifier.type==ModifierType::None,"undo empties it again");}
  // TRANSPOSE on step 1 with no other changes transposes the whole sequence.
  {auto p=scale16();p.tracks[0].steps[0].modifier={1,ModifierType::Transpose};auto n=played(p,32);
   for(int i=0;i<32;i++)require(n[i]==60+i%4+1,"T+1 on step 1 shifts every note, every cycle");}
  // From step 9 on only; offsets add to the transposition.
  {auto p=scale16();p.tracks[0].steps[8].modifier={3,ModifierType::Transpose};p.tracks[0].steps[10].modifier={2,ModifierType::Offset};p.tracks[0].steps[2].modifier={2,ModifierType::Offset};
   auto n=played(p,32);
   for(int i=0;i<32;i++){int r=i%16,want=60+r%4+(r>=8?3:0)+(r==10||r==2?2:0);require(n[i]==want,"transpose holds from its row to the end of the cycle, offsets ride on it");}}
  // A later TRANSPOSE replaces the earlier one; T+0 returns to the written notes.
  {auto p=scale16();p.tracks[0].steps[0].modifier={5,ModifierType::Transpose};p.tracks[0].steps[4].modifier={-2,ModifierType::Transpose};p.tracks[0].steps[12].modifier={0,ModifierType::Transpose};
   auto n=played(p,16);for(int r=0;r<16;r++)require(n[r]==60+r%4+(r<4?5:r<12?-2:0),"each transpose holds until the next");}
  // A conditioned TRANSPOSE: 1 of 2 transposes cycles 1, 3, ...; cycles 2, 4 play as written.
  {auto p=scale16();p.tracks[0].steps[0].modifier={7,ModifierType::Transpose};eventCondition(p.tracks[0].steps[0],4)={1,2};
   auto n=played(p,64);for(int i=0;i<64;i++){int cycle=i/16;require(n[i]==60+i%4+(cycle%2==0?7:0),"a conditioned transpose follows its cycle condition");}}
  // A shorter MOD column restarts its transposition at each of its own cycles.
  {auto p=scale16();p.tracks[0].columns[4].length=4;p.tracks[0].steps[2].modifier={4,ModifierType::Transpose};
   auto n=played(p,16);for(int r=0;r<16;r++)require(n[r]==60+r%4+(r%4>=2?4:0),"transposition resets with the MOD column's cycle");}
  // The column generator places OFFSET and TRANSPOSE cells and leaves the others empty.
  {Editor e;e.project=blankProject();e.column=4;e.density[0][4]=50;e.action(Action::RandomizeColumn);int offsets=0,transposes=0,empty=0;
   for(int r=0;r<16;r++){auto m=e.project.tracks[0].steps[r].modifier;offsets+=m.type==ModifierType::Offset;transposes+=m.type==ModifierType::Transpose;empty+=m.type==ModifierType::None;}
   require(offsets+transposes>0&&empty>0,"generated MOD column has filled and empty cells");}
  // Shift + X on an FX row in edit view: each draw differs from the effect it replaces,
  // and two edits from the same session state draw different effects (fresh entropy).
  {Editor e;e.entropy=0x1234567u;e.project=blankProject();e.column=2;e.row=0;
   std::vector<FxType> first,second;
   for(auto*list:{&first,&second}){e.action(Action::Enter);e.shift=true;
     for(int i=0;i<8;i++){auto before=e.pending.fx[0].type;e.action(Action::RandomizeColumn);require(e.pending.fx[0].type!=before&&e.pending.fx[0].type!=FxType::None,"a new random effect differs from the last");list->push_back(e.pending.fx[0].type);}
     e.shift=false;e.action(Action::Cancel);}
   require(first!=second,"a cancelled edit does not replay the same draws next time");}
  puts("modifier tests passed");
}
