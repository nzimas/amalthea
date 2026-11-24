// Synthetic in-memory performance check: never reads or writes a live session.
#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdlib>
using namespace amalthea;
int main(int argc,char**argv){
 int first=argc>1?std::atoi(argv[1]):0,last=argc>1?first+1:SynthEngines;int duration=argc>2?std::max(1,std::atoi(argv[2])):6;int tracks=argc>3?std::clamp(std::atoi(argv[3]),1,Tracks):Tracks;
 for(int e=first;e<last;e++){
  Project p=blankProject();for(auto &patch:p.patches){patch.engine=e;patch.seed=0;}
  for(int t=0;t<tracks;t++)for(int r=0;r<16;r++){auto &s=p.tracks[t].steps[r];s.note=48+(r%4)*3;s.instrument=t%Instruments;}
  Audio a;auto prep=std::chrono::steady_clock::now();a.submit(p,true);double cook=std::chrono::duration<double>(std::chrono::steady_clock::now()-prep).count();
  float out[512];double energy=0;std::vector<long long> times;auto begin=std::chrono::steady_clock::now();
  for(int i=0;i<44100*duration;i+=256){auto start=std::chrono::steady_clock::now();a.render(out,256);times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());for(float v:out){if(!std::isfinite(v))return 1;energy+=v*v;}}
  auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::sort(times.begin(),times.end());
  printf("%s: %d tracks; %ds audio in %.3fs; p99 %lldus max %lldus; patch preparation %.3fs; energy %.3f; steals %llu\n",instrumentName(e),tracks,duration,seconds,times[times.size()*99/100],times.back(),cook,energy,(unsigned long long)a.stats.steals.load());fflush(stdout);
 }
}
