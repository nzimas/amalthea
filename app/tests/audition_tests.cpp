#include "audio.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace amalthea;
static void check(bool x,const char*s){if(!x){fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){for(int engine=0;engine<SynthEngines;engine++){
 Editor e;e.project=blankProject();e.project.patches[0].engine=engine;auto before=e.project;e.action(Action::Enter);check(e.editing&&e.pending.note==60&&e.auditionSerial==1,"blank note starts at C4 and requests audition");
 e.action(Action::Up);check(e.pending.note==61&&e.auditionSerial==2,"up requests next note immediately");e.action(Action::Down);check(e.pending.note==60&&e.auditionSerial==3,"down requests audition");check(std::memcmp(&before,&e.project,sizeof(Project))==0,"preview does not commit note or change session");
 Audio a;check(a.submit(e.project,false,e.track,&e.pending),"queue note audition while stopped");std::vector<float>x(11025*2);a.render(x.data(),11025);double power=0;for(float v:x){check(std::isfinite(v)&&std::abs(v)<=.951f,"preview bounded");power+=v*v;}
 check(power>1e-8&&a.stats.auditions.load()==1&&a.stats.auditionNote.load()==60,"each engine audibly previews chosen pitch");check(a.stats.triggers[0].load()==0&&a.stats.advances[0].load()==0&&a.stats.columnRow[0][0].load()==-1,"audition cannot start clock or emit sequence events");
 e.action(Action::Cancel);check(!e.editing&&e.project.tracks[0].steps[0].note==-1,"cancel leaves note empty");
 }
 Project p=blankProject();p.patches[3].engine=2;p.tracks[0].instrument=3;p.tracks[0].mute=true;Step note;note.note=72;Audio muted;muted.submit(p,false,0,&note);float x[2048];muted.render(x,1024);double power=0;for(float v:x)power+=v*v;check(power>0&&muted.stats.lastInstrument[0].load()==3,"muted-track audition still hears the track's default instrument");
 Audio scan;scan.submit(p,true);scan.render(x,1024);auto advances=scan.stats.advances[0].load();for(int n=0;n<20;n++){note.note=48+n;scan.submit(p,true,0,&note);scan.render(x,256);}check(scan.stats.auditions.load()==20&&scan.stats.auditionNote.load()==67,"rapid browsing follows latest selection without losing events");check(scan.stats.advances[0].load()>=advances,"audition preserves running transport");check(scan.stats.voices.load()<=2,"rapid scanning uses bounded preview voices");
 // Hold X draws the sound; FUNC + short X then auditions it on the root, octave 4, without redrawing.
 {Editor f;f.project=blankProject();f.project.root=2;f.project.tracks[0].instrument=4;f.column=1;
  f.action(Action::RandomizeColumn);auto drawn=f.project;check(f.page==Page::Pattern,"hold X stays on the pattern");
  f.action(Action::Shift2Down);f.action(Action::RandomizerOptions);f.action(Action::Shift2Up);
  auto ev=f.previewStep();check(encodeProject(f.project)==encodeProject(drawn)&&f.page==Page::Pattern,"FUNC+X changes nothing and opens no page");
  check(f.auditionSerial==1&&ev.note==62&&ev.instrument==4,"FUNC+X auditions the track instrument on D4 in D");
  check(f.notice=="INSTRUMENT 5 ON D-4","FUNC+X overlay names the instrument and root note");
  Audio h;check(h.submit(f.previewProject(),false,f.track,&ev),"queue root audition");std::vector<float>y(11025*2);h.render(y.data(),11025);double pw=0;for(float v:y)pw+=v*v;
  check(pw>1e-8&&h.stats.auditionNote.load()==62&&h.stats.lastInstrument[0].load()==4,"the new sound is heard on D4");
  f.action(Action::Enter);check(!f.auditionRoot,"editing a cell auditions the cell again");}
 puts("PASS: all6 engines audition uncommitted notes, stopped/running clocks, inherited instrument, mute bypass, bounded rapid scans");
}
