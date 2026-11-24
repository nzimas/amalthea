// Synthetic in-memory DSP benchmark; no sessions read, saved or recalled.
#include "lush_reverb.hpp"
#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace amalthea;
int main(){
 for(int preset=0;preset<2;preset++){
  LushReverb rv;MasterSettings s;if(preset){s.extra[9]={100,0,100};s.reverbSpace={100,100,100,100};}rv.configure(s);rv.clear();
  std::vector<long long> times;double energy=0;auto begin=std::chrono::steady_clock::now();
  for(int f=0;f<44100*12;f+=256){auto start=std::chrono::steady_clock::now();for(int i=0;i<256;i++){float l,r;float in=(f+i)%22050<882?.15f*std::sin((f+i)*.04f):0;rv.process(in,in,l,r);if(!std::isfinite(l)||!std::isfinite(r))return 1;energy+=l*l+r*r;}times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());}
  auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::sort(times.begin(),times.end());printf("%s: 12s audio %.3fs; 256-frame p99 %lldus max %lldus; memory %zu; energy %.3f\n",preset?"MAXIMUM SPACE":"DEFAULT LUSH",seconds,times[times.size()*99/100],times.back(),rv.memoryBytes(),energy);fflush(stdout);
 }
 // Twelve-track real engine load plus full-size master reverb.
 for(int engine:{3,4,5}){
  auto p=blankProject();p.master.amount[9]=50;p.master.extra[9]={100,0,100};p.master.reverbSpace={25,100,100,100};
  for(auto &patch:p.patches)patch.engine=engine;
  for(int t=0;t<Tracks;t++){p.tracks[t].reverbSend=50;for(int r=0;r<16;r++){p.tracks[t].steps[r].note=48+(r%4)*3;p.tracks[t].steps[r].instrument=t%8;}}
  Audio a;a.submit(p,true);float out[512];std::vector<long long> times;auto begin=std::chrono::steady_clock::now();
  for(int f=0;f<44100*6;f+=256){auto start=std::chrono::steady_clock::now();a.render(out,256);times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());}
  auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::sort(times.begin(),times.end());printf("12-track %s + MAX SPACE: 6s audio %.3fs; p99 %lldus max %lldus\n",instrumentName(engine),seconds,times[times.size()*99/100],times.back());fflush(stdout);
 }
}
