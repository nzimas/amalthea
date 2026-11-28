#include "legacy_project.hpp"
#include "audio.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
using namespace amalthea;
static void check(bool v,const char *s){if(!v){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static std::vector<float> render(Project p){Audio a(12);a.submit(p,true);std::vector<float> x(22050*2);a.render(x.data(),22050);return x;}
int main(){
 // Every engine is directly reachable in the instrument editor on a fresh session.
 Editor bank;bank.project=blankProject();bank.column=1;bank.action(Action::Enter);
 for(int slot=0;slot<Instruments;slot++){
  bank.action(Action::Up);check(bank.pending.instrument==slot,"all fifteen instrument slots selectable");
  check(trackPatch(bank.project,0,slot).engine==slot,"unique default engine per instrument");
 }
 bank.action(Action::Enter);check(bank.project.tracks[0].steps[0].instrument==14&&trackPatch(bank.project,0,14).engine==PlaitsEngine,"instrument fifteen (PLAITS) commits");
 check(trackPatch(bank.project,0,11).engine==FmDrumEngine&&trackPatch(bank.project,0,12).engine==ObradorEngine&&trackPatch(bank.project,0,13).engine==RingsEngine,"slots 12-14: FM DRUM, OBRADOR, RINGS");
 for(int track=0;track<Tracks;track++)for(int slot=0;slot<Instruments;slot++){
  auto &patch=editTrackPatch(bank.project,track,slot);patch.seed=1000+track*Instruments+slot;
 }
 Project complete;std::string bankError;
 check(decodeProject(complete,encodeProject(bank.project),bankError),"eleven-slot per-track banks decode");
 check(encodeProject(complete)==encodeProject(bank.project),"high ownership bits and all private patches persist");
 // AMAL14 retains saved slot identities and initializes the newly added slots.
 auto old=blankProject();old.patches[6]={3,123};old.patches[7]={5,456};editTrackPatch(old,2,7).seed=789;
 check(decodeProject(complete,legacyBankBytes(encodeProject(old)),bankError),"AMAL14 eight-slot import");
 check(trackPatch(complete,2,7).seed==789&&trackPatch(complete,1,7).seed==456,"legacy private patch ownership retained");
 check(complete.patches[6].engine==3&&complete.patches[7].engine==5&&complete.patches[10].engine==10,"legacy identities preserved alongside new defaults");

 for(int engine=0;engine<SynthEngines;engine++)for(bool sameSlot:{false,true}){
  Editor e;e.project=blankProject();e.track=1;e.column=1;
  for(auto &p:e.project.patches)p={uint8_t(engine),12345};
  e.project.tracks[0].instrument=0;e.project.tracks[1].instrument=sameSlot?0:1;
  e.project.tracks[0].steps[0].note=48;e.project.tracks[1].steps[0].note=60;
  e.project.tracks[0].solo=true;auto before=e.project;auto reference=render(before);
  e.action(Action::RandomizeColumn);
  check(trackPatch(e.project,1,sameSlot?0:1).seed!=12345,"selected track patch changes");
  for(int t=0;t<Tracks;t++)if(t!=1)for(int i=0;i<Instruments;i++){auto a=trackPatch(before,t,i),b=trackPatch(e.project,t,i);check(a.engine==b.engine&&a.seed==b.seed,"other track identities unchanged");}
  check(render(e.project)==reference,"other track audio is sample-exact after patch redraw, same or different slot");
  auto changed=e.project;e.action(Action::Undo);check(encodeProject(e.project)==encodeProject(before),"undo restores local patch bank");e.action(Action::Redo);check(encodeProject(e.project)==encodeProject(changed),"redo restores isolated patch");
  std::string error;Project loaded;check(decodeProject(loaded,encodeProject(changed),error),"local patches decode");check(encodeProject(loaded)==encodeProject(changed),"all track patches roundtrip");
 }
 Editor e;e.project=blankProject();e.track=3;e.column=1;e.project.tracks[3].instrument=2;e.project.tracks[3].steps[0].instrument=5;e.action(Action::RandomizeColumn);
 check(trackPatch(e.project,3,2).seed!=0&&trackPatch(e.project,3,5).seed==0&&trackPatch(e.project,2,2).seed==0&&e.project.tracks[3].steps[0].instrument==2,"X redraws the track settings instrument of the current track only and writes it to the column");
 auto before=e.project;e.action(Action::ShiftDown);e.action(Action::RandomizeColumn);check(e.project.localPatchMask==before.localPatchMask&&std::memcmp(e.project.trackPatches.data(),before.trackPatches.data(),sizeof(before.trackPatches))==0,"Shift randomizer changes assignments, never patches");
 e.action(Action::ShiftUp);check(e.savePattern(0),"synthetic pattern captures patch bank");e.action(Action::RandomizeColumn);check(e.loadPattern(0),"synthetic pattern restores patch bank");check(e.project.localPatchMask==before.localPatchMask,"pattern retains patch ownership");
 // Even identical patches in two slots retain separate played instruments.
 auto twoSlots=blankProject();twoSlots.patches[0]=twoSlots.patches[1]={6,0};twoSlots.tracks[0].steps[0].note=60;twoSlots.tracks[0].steps[0].instrument=0;twoSlots.tracks[0].steps[1].note=64;twoSlots.tracks[0].steps[1].instrument=1;
 Audio slotAudio(12);slotAudio.submit(twoSlots,true);std::vector<float> samples(6615*2);slotAudio.render(samples.data(),6615);check(slotAudio.stats.voices>=2,"same-engine identical-seed slots keep separate running state");
 // Import the previous global-bank format, then edit one track locally.
 auto legacy=legacyBankBytes(encodeProject(blankProject()));legacy.resize(legacy.size()-LegacyTrackPatchBytes);legacy[4]=13;uint32_t crc=~0u;for(size_t i=12;i<legacy.size();i++){crc^=legacy[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}crc=~crc;for(int j=0;j<4;j++)legacy[8+j]=(crc>>(8*j))&255;
 Editor migrated;std::string error;check(decodeProject(migrated.project,legacy,error),"AMAL13 global bank migration");migrated.track=1;migrated.column=1;migrated.project.tracks[1].instrument=0;migrated.action(Action::RandomizeColumn);check(trackPatch(migrated.project,1,0).seed!=0&&trackPatch(migrated.project,0,0).seed==0,"legacy templates detach per track on first edit");
 puts("Per-track patch identities, real audio isolation, override, undo, serialization and pattern recall pass");
}
