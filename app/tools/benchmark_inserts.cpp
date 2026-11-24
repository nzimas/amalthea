// Synthetic, in-memory stress test. Does not read or write any session.
#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
using namespace amalthea;
int main(){
 for(auto type:{FxType::Clouds,FxType::AmpSim,FxType::Biztortion,FxType::Rotor}){
  Project p=demoProject();for(auto &t:p.tracks){for(auto &s:t.steps)for(auto &f:s.fx)f={type,100};for(auto &cfg:t.inserts){cfg.clouds[1]=100;cfg.clouds[2]=100;}}
  Audio audio;audio.submit(p,true);float data[512];std::vector<long long> times;double energy=0;auto begin=std::chrono::steady_clock::now();
  for(int frame=0;frame<44100*6;frame+=256){auto start=std::chrono::steady_clock::now();audio.render(data,256);times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());for(float v:data)energy+=v*v;}
  double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::sort(times.begin(),times.end());
  printf("%s: 12 tracks x2 slots; 6s rendered %.3fs; 256-frame p99 %lldus max %lldus; energy %.3f\n",fxName(type),elapsed,times[times.size()*99/100],times.back(),energy);fflush(stdout);
 }
}
