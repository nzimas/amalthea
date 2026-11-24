#include "legacy_project.hpp"
#include "lush_reverb.hpp"
#include "effects.hpp"
#include "ui.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <fstream>
#include <vector>
using namespace amalthea;
static std::atomic<size_t> allocations{0};
void *operator new(size_t n){allocations++;if(auto p=std::malloc(n))return p;throw std::bad_alloc();}
void *operator new[](size_t n){return ::operator new(n);}
void operator delete(void*p)noexcept{std::free(p);}
void operator delete[](void*p)noexcept{std::free(p);}
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
static std::vector<float> impulse(LushReverb&r,const MasterSettings&s,int seconds=12,float sign=1){
 r.configure(s);r.clear();std::vector<float> v(44100*seconds*2);
 for(size_t i=0;i<v.size()/2;i++)r.process(i?0:sign,i?0:sign,v[2*i],v[2*i+1]);
 return v;
}
static double energy(const std::vector<float>&v,double start,double end){double e=0;for(size_t i=size_t(start*44100)*2;i<std::min(v.size(),size_t(end*44100)*2);i++)e+=double(v[i])*v[i];return e;}
int main(){
 MasterSettings s;LushReverb rv;auto x=impulse(rv,s);auto negative=impulse(rv,s,12,-1);double power=0,l2=0,r2=0,lr=0;size_t dense=0,total=0;
 for(size_t i=0;i<x.size();i++){require(std::isfinite(x[i])&&std::abs(x[i])<1,"bounded finite impulse");require(std::abs(x[i]+negative[i])<1e-7f,"positive/negative tails are symmetric");}
 for(int i=22050;i<44100*3;i++){double l=x[2*i],r=x[2*i+1];l2+=l*l;r2+=r*r;lr+=l*r;power+=l*l+r*r;dense+=(std::abs(l)+std::abs(r)>1e-7f);total++;}
 double correlation=lr/std::sqrt(l2*r2);require(power>.01,"substantial spacious late tail");require(std::abs(correlation)<.65,"mono input produces decorrelated stereo space");require(double(dense)/total>.98,"late response densely filled rather than isolated ringing echoes");
 auto repeat=impulse(rv,s);require(x==repeat,"clear resets all tail and modulation state deterministically");
 LushReverb threaded(true);auto pipelined=impulse(threaded,s);require(x==pipelined,"pipelined worker preserves exact wet samples and latency");
 threaded.clear();float warmL,warmR;for(int i=0;i<64;i++)threaded.process(.1f,.1f,warmL,warmR);size_t threadedBefore=allocations.load();
 for(int i=0;i<1024;i++){float l,r;threaded.process(.1f,.1f,l,r);}threaded.configure(s);threaded.clear();require(allocations.load()==threadedBefore,"worker processing, configuration and clear allocate nothing");
 s.reverbSpace[1]=0;auto mono=impulse(rv,s,2);for(size_t i=0;i<mono.size();i+=2)require(mono[i]==mono[i+1],"width zero collapses wet return only");
 s=MasterSettings{};s.extra[9][0]=15;auto shortTail=impulse(rv,s,8);s.extra[9][0]=90;auto longTail=impulse(rv,s,8);
 require(energy(longTail,3,8)>20*energy(shortTail,3,8)+1e-7,"decay extends a real late tail");
 // Every added control changes the audible wet signal; predelay shifts first arrival.
 s=MasterSettings{};
 for(int p=4;p<8;p++){s.value(9,p)=10;auto a=impulse(rv,s,3);s.value(9,p)=90;auto b=impulse(rv,s,3);double diff=0;for(size_t i=0;i<a.size();i++)diff+=std::abs(a[i]-b[i]);require(diff>.01,"each added parameter affects sound");s=MasterSettings{};}
 rv.clear();size_t before=allocations.load();
 for(int n=0;n<101;n++){
  for(int p=1;p<8;p++)s.value(9,p)=uint8_t((n*(p+3))%101);rv.configure(s);
  for(int i=0;i<256;i++){float l,r;rv.process(i==0?.1f:0,0,l,r);require(std::isfinite(l)&&std::isfinite(r)&&std::abs(l)+std::abs(r)<8,"live parameter extremes remain bounded");}
 }
 require(allocations.load()==before,"configure and process allocate nothing");
 // The original send hierarchy and final master placement remain active.
 MasterSettings master;master.amount.fill(0);master.amount[10]=100;master.amount[9]=100;master.extra[7][0]=0;
 MasterEffects bus;bus.configure(master,120);double sent=0;
 for(int n=0;n<44100*3;n++){float l=0,r=0;bus.process(l,r,0,0,n==0?.2f:0,n==0?.2f:0);if(n>2205)sent+=l*l+r*r;}
 require(sent>1e-6,"per-track/step reverb sends reach master return");
 // Track 2's dry signal must never become part of track 1's wet return (routing only: the
 // output stage's soft knee bypassed, as it is deliberately non-linear).
 master.busMix[3]=0;
 MasterEffects mixed,wetOnly,noSends;mixed.configure(master,120);wetOnly.configure(master,120);noSends.configure(master,120);
 for(int n=0;n<44100*3;n++){
  float dry=n<44100?.05f*std::sin(n*.037f):0,send=n==0?.2f:0;
  float l=dry,r=dry,wl=0,wr=0,dl=dry,dr=dry;
  mixed.process(l,r,0,0,send,send);wetOnly.process(wl,wr,0,0,send,send);noSends.process(dl,dr);
  require(std::abs(l-dry-wl)<2e-7f&&std::abs(r-dry-wr)<2e-7f,"zero-send track cannot enter another track's reverb");
  require(dl==dry&&dr==dry,"100% master return never reverberates a zero-send mix");
 }
 // Actual sequencer routing: an empty track's send does not wet a dry sounding track.
 auto dryProject=blankProject();dryProject.tracks[0].reverbSend=90;dryProject.tracks[1].level=10;dryProject.tracks[1].instrument=0;dryProject.tracks[1].steps[0].note=60;
 auto reference=dryProject;reference.master.amount[9]=0;Audio routed,dryAudio;routed.submit(dryProject,true);dryAudio.submit(reference,true);std::vector<float> actual(44100*2),expected(actual.size());routed.render(actual.data(),44100);dryAudio.render(expected.data(),44100);require(actual==expected,"silent track send is isolated from other track audio");require(energy(actual,0,1)>1e-4,"dry-track routing test has real sounding audio");
 dryProject.tracks[1].reverbSend=100;dryProject.tracks[1].columns[2].length=1;dryProject.tracks[1].steps[0].fx[0]={FxType::Reverb,0};routed.submit(dryProject,false);dryAudio.submit(reference,false);float stop[64];routed.render(stop,32);dryAudio.render(stop,32);routed.submit(dryProject,true);dryAudio.submit(reference,true);routed.render(actual.data(),44100);dryAudio.render(expected.data(),44100);require(actual==expected,"step reverb 0% overrides its track's 100% send");
 Editor e;e.page=Page::Master;e.masterModule=9;e.masterParameter=3;e.action(Action::Down);require(e.masterParameter==4,"cross reaches second reverb parameter page");e.action(Action::Right);require(e.project.master.reverbSpace[0]==11,"extended control edits correct setting");
 Canvas c(1024,768);AudioStats stats;drawUI(c,e,stats);e.masterParameter=7;drawUI(c,e,stats);e.action(Action::NextTrack);require(e.masterParameter==4,"moving to five-parameter output module clamps selection");
 std::string error;auto p=blankProject();p.master.reverbSpace={11,22,33,44};require(saveProject(p,"reverb-fixture.amalthea",error),"save new reverb controls");Project loaded;require(loadProject(loaded,"reverb-fixture.amalthea",error)&&loaded.master.reverbSpace==p.master.reverbSpace,"v10 reverb settings roundtrip");// Emulate v8 by removing the four appended control bytes.
 std::ifstream input("reverb-fixture.amalthea",std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});input.close();bytes=legacyBankBytes(bytes);bytes.resize(bytes.size()-EventConditionBytes-LegacyTrackPatchBytes-4-Tracks*2*7-4-360);bytes[4]=8;uint32_t crc=~0u;for(size_t i=12;i<bytes.size();i++){crc^=bytes[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}crc=~crc;for(int j=0;j<4;j++)bytes[8+j]=(crc>>(8*j))&255;
 std::ofstream previous("reverb-fixture.amalthea",std::ios::binary);previous.write((char*)bytes.data(),bytes.size());previous.close();require(loadProject(loaded,"reverb-fixture.amalthea",error)&&loaded.master.reverbSpace==MasterSettings{}.reverbSpace&&loaded.master.amount[9]==100,"v8 migrates to track-only return and new space controls");
 std::remove("reverb-fixture.amalthea");
 printf("PASS: dense late tail %.2f%%, stereo correlation %.4f, energy %.4f; %zu bytes; no callback allocations; eight controls and sends\n",100.*dense/total,correlation,power,rv.memoryBytes());
}
