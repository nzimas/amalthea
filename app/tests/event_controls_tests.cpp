#include "audio.hpp"
#include "effects.hpp"
#include "ui.hpp"
#include "legacy_project.hpp"
#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cmath>
using namespace amalthea;
static void check(bool v,const char*m){if(!v){fprintf(stderr,"FAIL: %s\n",m);exit(1);}}
static double energy(const std::vector<float>&x,int channel=-1){double e=0;for(size_t i=0;i<x.size();i++)if(channel<0||int(i%2)==channel)e+=double(x[i])*x[i];return e;}
static std::vector<float> preview(const Editor&e){Audio a(12);auto p=e.previewProject();auto s=e.previewStep();a.submit(p,false,e.track,&s);std::vector<float>x(22050*2);a.render(x.data(),22050);return x;}
static void crc(std::vector<uint8_t>&b){uint32_t c=~0u;for(size_t i=12;i<b.size();i++){c^=b[i];for(int n=0;n<8;n++)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}c=~c;for(int n=0;n<4;n++)b[8+n]=c>>(n*8);}
static double filterTone(int mode,int resonance,double hz){InsertEffect f(false);InsertSettings s;s.filter={uint8_t(mode*20),uint8_t(std::lround(std::log(1000./20)/std::log(900.)*100)),uint8_t(resonance),0,50};f.setSettings(s);f.configure({FxType::Filter,100},120);double e=0;for(int n=0;n<44100;n++){float l=.01f*std::sin(6.28318530718*hz*n/44100),r=l;f.process(l,r);check(std::isfinite(l)&&std::isfinite(r),"filter finite");if(n>22050)e+=l*l;}return e;}
int main(){
 for(auto f:selectableEffects())check(f==FxType::None||f==FxType::Random||std::find(globalInsertTypes().begin(),globalInsertTypes().end(),f)!=globalInsertTypes().end(),"FX catalog contains only active inserts");
 for(auto f:{FxType::Pan,FxType::Volume,FxType::Delay,FxType::Reverb,FxType::Abrasion,FxType::Corrosion,FxType::Modulation,FxType::Diode,FxType::Rat,FxType::Lowpass,FxType::Highpass})check(std::find(selectableEffects().begin(),selectableEffects().end(),f)==selectableEffects().end(),"sends and retired processors absent from FX selection");
 Editor e;e.project=blankProject();e.project.patches[0].engine=2;e.project.master.busMix.fill(0);auto original=encodeProject(e.project);e.action(Action::Enter);auto dry=preview(e);check(energy(dry)>0,"note preview audible");
 AudioStats stats;for(int tab=0;tab<10;tab++){
  check(e.eventTab==tab,"F2 reaches every note tab without changing track");
  for(int width:{640,1024}){Canvas c(width,width*3/4);drawUI(c,e,stats);for(auto pixel:c.pixels)check(pixel>>24==255,"two-tab window remains bounded and opaque");}
  if(tab==1){e.action(Action::Up);check(e.pending.length==2,"LENGTH steps up");for(int n=0;n<80;n++)e.action(Action::Up);check(e.pending.length==MaxNoteLength,"LENGTH capped");for(int n=0;n<80;n++)e.action(Action::Down);check(e.pending.length==1,"LENGTH floor is one step");}
  if(tab>=3){auto serial=e.auditionSerial;e.action(Action::Up);check(e.auditionSerial>serial,"each note parameter requests immediate audition");check(e.pending.locks[tab-3]<=100,"note override percentage");e.action(Action::Left);check(e.pending.locks[tab-3]==Inherit,"left restores track inheritance");}
  e.action(Action::NextTrack);
 }
 check(e.eventTab==0&&encodeProject(e.project)==original,"tab wrap and previews never commit");
 e.eventTab=2;e.pending.locks[NotePan]=0;auto left=preview(e);check(energy(left,0)>0&&energy(left,1)==0,"pan override heard before commit");e.pending.locks[NotePan]=100;auto right=preview(e);check(energy(right,1)>0&&energy(right,0)==0,"opposite pan audible before commit");
 e.pending.locks[NotePan]=Inherit;e.pending.locks[NoteVolume]=0;check(energy(preview(e))==0,"explicit zero volume differs from inheritance");e.action(Action::Cancel);check(encodeProject(e.project)==original,"cancel discards all preview locks");
 e.column=2;e.action(Action::Enter);e.action(Action::Right);e.pending.fx[0].value=80;auto fx=preview(e);e.pending.fx[0]={};check(energy(fx)>0&&fx!=preview(e),"uncommitted FX event audibly transforms its preview");e.action(Action::Cancel);check(encodeProject(e.project)==original,"FX cancellation preserves session");
 // Each new send lock reaches its real bus, and explicit zero defeats a track send.
 e.column=0;e.action(Action::Enter);e.project.master.busMix.fill(100);auto reference=preview(e);
 for(int parameter=NoteDelay;parameter<NoteParameters;parameter++){
  e.pending.locks.fill(Inherit);e.pending.locks[parameter]=100;
  auto sent=preview(e);check(sent!=reference,"each note send changes real audio before commit");
  auto mix=resolveTrackMix(e.project.tracks[0],e.pending);
  check(parameter<NoteAbrasion||(mix.macro[parameter-NoteAbrasion]==1),"macro note lock reaches its own bus");
 }
 e.pending.locks.fill(Inherit);e.project.tracks[0].reverbSend=100;e.pending.locks[NoteReverb]=0;
 check(resolveTrackMix(e.project.tracks[0],e.pending).reverb==0,"zero note send overrides nonzero track send");
 e.project.tracks[0].reverbSend=0;e.project.master.busMix.fill(0);e.action(Action::Cancel);
 // Audition and transient playback preserve musical clock progress.
 Audio live(12);live.submit(e.project,true);float scratch[1024]{};live.render(scratch,512);auto advances=live.stats.advances[0].load();
 e.action(Action::Enter);e.action(Action::Up);auto transient=e.previewProject();auto audition=e.previewStep();live.submit(transient,true,e.track,&audition);live.render(scratch,512);
 check(live.stats.auditions==1&&live.stats.advances[0]>=advances,"live edit auditions without restarting transport");
 e.action(Action::Cancel);live.submit(e.project,true,-1,nullptr,nullptr,true);live.render(scratch,512);check(encodeProject(e.project)==original,"cancel after live edit restores stored pattern");
 // All note fields are in the same pattern snapshot/undo transaction.
 e.column=0;e.action(Action::Enter);for(int p=0;p<NoteParameters;p++)e.pending.locks[p]=10+p*13;e.action(Action::Enter);e.project.tracks[0].inserts[1].filter={40,12,98,65,60};Project loaded;std::string error;check(decodeProject(loaded,encodeProject(e.project),error)&&encodeProject(loaded)==encodeProject(e.project),"AMAL16 note locks and filter profile roundtrip");
 check(e.savePattern(0),"synthetic pattern captures locks");e.project.tracks[0].steps[0].locks.fill(Inherit);check(e.loadPattern(0)&&e.project.tracks[0].steps[0].locks[0]==10,"pattern restores note automation");
 // Prior formats migrate send locks out of the FX slots.
 auto old=blankProject();old.tracks[0].steps[0].fx={Effect{FxType::Pan,23},Effect{FxType::Reverb,75}};check(decodeProject(loaded,legacyBankBytes(encodeProject(old)),error),"old pan/send fixture imports");check(loaded.tracks[0].steps[0].locks[NotePan]==23&&loaded.tracks[0].steps[0].locks[NoteReverb]==75&&loaded.tracks[0].steps[0].fx[0].type==FxType::None,"old automation moved into note event");
 // Old eleven-slot format15 also remains readable.
 {auto v17=elevenSlotBytes(encodeProject(blankProject()));Project eleven;check(decodeProject(eleven,v17,error),"AMAL17 eleven-slot session loads");
  check(eleven.patches[11].engine==FmDrumEngine&&eleven.patches[11].seed==0&&eleven.tracks[11].instrument==10,"eleven-slot session gains FM DRUM as instrument 12");}
 auto v15=elevenSlotBytes(encodeProject(blankProject()));v15.resize(v15.size()-NoteLockBytes-FilterSettingsBytes-PortSettingsBytes);v15[4]=15;crc(v15);check(decodeProject(loaded,v15,error),"AMAL15 imports with inherited note controls");
 for(int seed=1;seed<=100;seed++){
  Editor r;r.project=blankProject();r.project.seed=seed;r.project.tracks[0].columns[0].length=7;r.density[0][0]=45;auto baseline=r.project;r.shift=true;r.action(Action::RandomizeColumn);int notes=0;
  for(int row=0;row<7;row++){auto&s=r.project.tracks[0].steps[row];if(s.note>=0){notes++;for(auto v:s.locks)check(v<=100,"Shift NOTE randomizes all event controls");check(s.locks[NoteVolume]>=10,"random volume never below10 percent");}else for(auto v:s.locks)check(v==Inherit,"empty rows inherit");}
  check(notes==3,"Shift retains density relative to seven-step column");check(r.project.tracks[0].steps[7].locks==baseline.tracks[0].steps[7].locks,"hidden note locks untouched");
  auto locks=r.project.tracks[0].steps[0].locks;r.shift=false;r.action(Action::RandomizeColumn);check(r.project.tracks[0].steps[0].locks==locks,"plain NOTE randomization preserves note controls");r.action(Action::Undo);r.action(Action::Undo);check(encodeProject(r.project)==encodeProject(baseline),"undo restores all generated controls and seed");
 }
 auto low0=filterTone(0,0,50),low100=filterTone(0,100,50);check(low100/low0>.95&&low100/low0<1.1,"maximum resonance preserves sub-cutoff bass power");check(filterTone(0,35,5000)<low0*.02,"low pass rejects highs");check(filterTone(2,35,50)<filterTone(2,35,5000)*.002,"high pass rejects lows");
 for(int mode=0;mode<6;mode++)check(filterTone(mode,100,1000)>1e-6,"every filter mode responds at resonance");
 InsertEffect filter(false);InsertSettings settings;settings.filter={0,2,100,100,75};filter.setSettings(settings);filter.configure({FxType::Filter,100},120);for(int n=0;n<44100;n++){float l=n==0?1:0,r=0;filter.process(l,r);check(std::isfinite(l)&&std::abs(l)<=8&&r==0,"extreme resonant drive bounded, silent channel stays silent");}
 puts("PASS: note tabs, transactional audible previews, inherited locks, pattern/migration, guarded Shift randomizer, bass-preserving resonant filter");
}
