#include "legacy_project.hpp"
#include "track_macros.hpp"
#include "audio.hpp"
#include "ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>
using namespace amalthea;
static void check(bool x,const char*s){if(!x){fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
static std::vector<float> render(int m,std::array<MacroSettings,3> p,float send=1){TrackMacros dsp;dsp.configure(p);std::vector<float>x(44100*2);std::array<float,3>s{};s[m]=send;for(int n=0;n<44100;n++){float l=.065f*std::sin(n*.037f)+.035f*std::sin(n*.271f),r=.07f*std::sin(n*.051f);dsp.process(l,r,s);check(std::isfinite(l)&&std::isfinite(r)&&std::abs(l)<4&&std::abs(r)<4,"bounded macro output");x[2*n]=l;x[2*n+1]=r;}return x;}
static double diff(const std::vector<float>&a,const std::vector<float>&b){double d=0;for(size_t i=0;i<a.size();i++)d+=std::abs(a[i]-b[i]);return d;}
static uint32_t crc(const std::vector<unsigned char>&b){uint32_t c=~0u;for(size_t i=12;i<b.size();i++){c^=b[i];for(int n=0;n<8;n++)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
int main(){
 for(int m=0;m<3;m++){
  auto p=defaultMacros();auto dry=render(m,p,0),wet=render(m,p);double d=diff(dry,wet);check(d>500,"each default macro makes a substantial audible transformation");printf("%s default absolute difference %.2f\n",macroName(m),d);
  p[m].value[7]=0;check(render(m,p)==dry,"Mix0 exactly dry");p[m].value[7]=50;auto half=render(m,p);for(size_t n=0;n<half.size();n++)check(std::abs(half[n]-(dry[n]+wet[n])*.5f)<1e-6,"Mix50 is true halfway blend");
  for(int f=0;f<8;f++){auto a=defaultMacros(),b=a;a[m].value[f]=10;b[m].value[f]=90;double change=diff(render(m,a),render(m,b));if(change<=1){fprintf(stderr,"inactive %s/%s %g\n",macroName(m),macroParameterName(m,f),change);check(false,"all macro parameters change audible audio");}}
  for(int value:{0,100}){p=defaultMacros();p[m].value.fill(value);TrackMacros quiet;quiet.configure(p);for(int n=0;n<88200;n++){float l=0,r=0;quiet.process(l,r,{1,1,1});check(l==0&&r==0,"no input means exact zero: no hiss, dither, LFO leakage or bias");}}
 }
 // Both modulation paths contribute; neither is merely an inaudible allpass.
 auto p=defaultMacros();p[2].value[0]=0;p[2].value[1]=0;auto none=render(2,p);p[2].value[0]=100;check(diff(none,render(2,p))>200,"flanger has audible comb cancellation");p[2].value[0]=0;p[2].value[1]=100;check(diff(none,render(2,p))>200,"phaser has audible notches");
 // Track-level sends and explicit zero step locks resolve independently.
 Track t;t.macroSend={70,80,90};Step step;step.fx={Effect{FxType::Abrasion,0},Effect{FxType::Modulation,25}};auto mix=resolveTrackMix(t,step);
 check(mix.macro[0]==0&&std::abs(mix.macro[1]-.8f)<1e-6&&std::abs(mix.macro[2]-.25f)<1e-6,"macro step locks replace only their matching track sends, including0");
 // Silent track sends must never process another track's sounding source.
 for(int m=0;m<3;m++){
  Project p=blankProject();p.patches[0].engine=2;p.tracks[1].steps[0].note=60;p.tracks[0].macroSend[m]=100;auto q=p;q.tracks[0].macroSend[m]=0;
  Audio a,b;a.submit(p,true);b.submit(q,true);std::vector<float>x(44100*2),y(x.size());a.render(x.data(),44100);b.render(y.data(),44100);check(x==y,"zero-send source isolated from another track's macro send");
  p.tracks[1].macroSend[m]=100;p.tracks[1].columns[2].length=1;p.tracks[1].steps[0].fx[0]={FxType(int(FxType::Abrasion)+m),0};Audio locked;locked.submit(p,true);locked.render(x.data(),44100);check(x==y,"held step0 overrides track100 in actual audio");
 }
 Editor e;e.project=blankProject();e.page=Page::Track;e.trackField=10;e.action(Action::Enter);check(e.page==Page::Track,"track contains sends only, no macro links");e.page=Page::Macro;e.action(Action::Down);e.action(Action::Right);check(e.project.tracks[0].macroOverride==1,"editing makes local override");int local=e.project.tracks[0].macros[0].value[0];e.project.master.macros[0].value[0]=95;
 check(effectiveMacros(e.project,0)[0].value[0]==local&&effectiveMacros(e.project,1)[0].value[0]==95,"local override isolated; other tracks follow master defaults");e.macroField=0;e.action(Action::Left);check(effectiveMacros(e.project,0)[0].value[0]==95,"Source MASTER rejoins shared defaults");
 e.page=Page::Track;e.trackField=0;for(int n=0;n<10;n++)e.action(Action::Down);check(e.trackField==10,"cross reaches all below-fold track settings");Canvas c(1024,768);AudioStats stats;drawUI(c,e,stats);writePPM(c,"macro-track-scroll.ppm");
 for(int page=0;page<MasterPages;page++){int module=masterPageModule(page);check(macroIndex(module)>=0||module>=7,"old seven color modules consolidated");}
 // v11 stores shared/local profiles, sends and override source.
 p=defaultMacros();e.project.master.macros=p;e.project.tracks[2].macroSend={11,22,33};e.project.tracks[2].macroOverride=5;e.project.tracks[2].macros[0].value[2]=17;
 std::string path="macro-fixture.amalthea",error;check(saveProject(e.project,path,error),"save synthetic v11");Project loaded;check(loadProject(loaded,path,error),"load v11");check(loaded.tracks[2].macroSend==e.project.tracks[2].macroSend&&loaded.tracks[2].macroOverride==5&&loaded.tracks[2].macros[0].value[2]==17&&loaded.master.macros[0].value==p[0].value,"macro data roundtrip");
 std::ifstream in(path,std::ios::binary);std::vector<unsigned char>bytes((std::istreambuf_iterator<char>(in)),{});in.close();bytes=legacyBankBytes(bytes);bytes.resize(bytes.size()-EventConditionBytes-LegacyTrackPatchBytes-360);bytes[4]=10;auto sum=crc(bytes);for(int n=0;n<4;n++)bytes[8+n]=(sum>>(8*n))&255;std::ofstream out(path,std::ios::binary);out.write((char*)bytes.data(),bytes.size());out.close();check(loadProject(loaded,path,error),"v10 remains readable");check(loaded.tracks[2].macroOverride==0&&loaded.tracks[2].macroSend==std::array<uint8_t,3>{},"v10 defaults to independent zero sends");std::filesystem::remove(path);
 puts("PASS:24 audible macro controls, silent zero-input, independent tracks and locks, source inheritance, scrolling, v11/v10");
}
