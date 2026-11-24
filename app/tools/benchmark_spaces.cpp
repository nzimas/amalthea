// Synthetic source only: no live session access.
#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif
using namespace amalthea;
int main(){
#ifdef __linux__
 sched_param priority{};priority.sched_priority=12;pthread_setschedparam(pthread_self(),SCHED_FIFO,&priority);
#endif
for(int count:{0,1,2,4,8}){
 auto p=blankProject();for(int t=0;t<Tracks;t++){
  auto &tr=p.tracks[t];tr.reverbSend=40;
  for(int r=0;r<16;r++)tr.steps[r].note=48+r%5;
  if(t<count){tr.columns[2].length=1;tr.steps[0].fx[0]={FxType::Space,40};}
 }
 Audio a;a.submit(p,true);float data[512];double e=0;std::vector<long long> times;auto begin=std::chrono::steady_clock::now();
 for(int f=0;f<44100*6;f+=256){auto start=std::chrono::steady_clock::now();a.render(data,256);times.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());for(float v:data){if(!std::isfinite(v))return 1;e+=v*v;}}
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::sort(times.begin(),times.end());printf("12 tracks + TAL III + %d SPACE inserts: 6s audio %.3fs p99 %lldus max %lldus energy %.3f\n",count,seconds,times[times.size()*99/100],times.back(),e);fflush(stdout);
}}
