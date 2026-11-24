#include "audio.hpp"
#include "effects.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
using namespace amalthea;
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
static double energy(const std::vector<float>&x,int first,int last){double v=0;for(int n=first*44100*2;n<last*44100*2;n++)v+=x[n]*x[n];return v;}
int main(){
 // Stopping in a gap must match the uninterrupted tail sample for sample.
 for(int mode=0;mode<3;mode++){
  Project p=blankProject();p.patches[0].engine=1;p.tracks[0].steps[0].note=60;
  if(mode==0)p.tracks[0].delaySend=90;
  if(mode==1)p.tracks[0].reverbSend=90;
  if(mode==2){p.tracks[0].columns[2].length=1;p.tracks[0].steps[0].fx[0]={FxType::Space,75};}
  Audio stopped,rolling;std::vector<float> warm(44100*2),a(16384*2),b(a.size());
  stopped.submit(p,true);rolling.submit(p,true);stopped.render(warm.data(),44100);rolling.render(warm.data(),44100);
  require(stopped.stats.voices.load()==0,"test stops in an actual note gap");auto triggers=stopped.stats.triggers[0].load();
  stopped.submit(p,false);stopped.render(a.data(),16384);rolling.render(b.data(),16384);
  require(a==b,"Stop preserves the exact ongoing delay/reverb/insert tail");
  double power=0;for(float v:a){require(std::isfinite(v)&&std::abs(v)<=.951f,"tail finite and bounded");power+=v*v;}require(power>1e-7,"tail remains audible after stop");
  std::vector<float> decay(24*44100*2);stopped.render(decay.data(),24*44100);
  require(stopped.stats.triggers[0].load()==triggers,"no new sequence events after stop");
  require(stopped.stats.columnRow[0][0].load()==-1,"stopped playhead hidden");
  require(energy(decay,20,24)<energy(decay,0,4)*.01+1e-10,"tail dies away naturally");
  stopped.submit(p,true);stopped.render(a.data(),256);require(stopped.stats.columnRow[0][0].load()==0,"Play restarts sequence at first step");
 }
 // Every master effect offers a real wet/dry blend. A fully dry module nulls
 // against bypass, while 50% lies halfway between dry and fully wet output.
 for(int module=7;module<MasterModules;module++){
  MasterSettings s;s.amount.fill(0);s.amount[10]=100;s.extra[7][0]=0;
  if(module<7)s.amount[module]=70;
  if(module==7){s.amount[7]=10;s.extra[7][0]=90;}
  if(module==8||module==9)s.amount[module]=80;
  if(module==10)s.amount[10]=20;
  else s.busMix[3]=0;   // (each module's Mix isolated: the driven output stage bypassed)
  int mix=0;for(int q=0;q<masterParameterCount(module);q++)if(std::string(masterParameterName(module,q))=="MIX")mix=q;require(std::string(masterParameterName(module,mix))=="MIX","all master modules have Mix");
  auto dry=s,half=s,wet=s;dry.value(module,mix)=0;half.value(module,mix)=50;wet.value(module,mix)=100;
  MasterEffects d,h,w;d.configure(dry,120);h.configure(half,120);w.configure(wet,120);double change=0;
  for(int n=0;n<44100;n++){
   float level=module==7?.3f:.001f;float l=level*std::sin(n*.037f),r=level*.75f*std::sin(n*.081f),dl=l,dr=r,hl=l,hr=r,wl=l,wr=r;
   float send=(module==8||module==9)?l:0,sendR=(module==8||module==9)?r:0;
   d.process(dl,dr,module==8?send:0,module==8?sendR:0,module==9?send:0,module==9?sendR:0);h.process(hl,hr,module==8?send:0,module==8?sendR:0,module==9?send:0,module==9?sendR:0);w.process(wl,wr,module==8?send:0,module==8?sendR:0,module==9?send:0,module==9?sendR:0);
   require(std::abs(dl-l)<1e-6&&std::abs(dr-r)<1e-6,"Mix0 is exactly dry");
   if(std::abs(hl-(dl+wl)*.5f)>=1e-5||std::abs(hr-(dr+wr)*.5f)>=1e-5){fprintf(stderr,"module%d frame%d dry%g half%g wet%g\n",module,n,dl,hl,wl);require(false,"Mix50 is parallel half blend");}
   change+=std::abs(wl-dl)+std::abs(wr-dr);
  }
  require(change>.01,"Mix changes audible transformation for each master module");
 }
 // New insert profiles and master mixes persist only in synthetic fixtures.
 Project p=blankProject();p.master.busMix={12,34,56,78};p.tracks[3].inserts[1].space={1,2,3,4,5,6,7};p.tracks[3].steps[0].fx[1]={FxType::Space,45};
 std::string error;std::string path="tail-test-fixture.amalthea";require(saveProject(p,path,error),"save synthetic v10 fixture");Project loaded;require(loadProject(loaded,path,error),"read v10 fixture");std::filesystem::remove(path);
 require(loaded.master.busMix==p.master.busMix&&loaded.tracks[3].inserts[1].space==p.tracks[3].inserts[1].space&&loaded.tracks[3].steps[0].fx[1].type==FxType::Space,"v10 preserves mixes and independent space profile");
 puts("PASS: natural stop tails, no new triggers, restart, all master dry/wet nulls and halfway blends, v10 persistence");
}
