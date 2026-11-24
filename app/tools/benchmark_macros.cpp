// Synthetic tracker load; no live session access.
#include "audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace amalthea;
int main(){for(int mode=-1;mode<4;mode++){
 auto p=blankProject();for(int t=0;t<Tracks;t++){p.tracks[t].reverbSend=35;for(int r=0;r<16;r++)p.tracks[t].steps[r].note=48+r%5;
 for(int m=0;m<3;m++)if(mode==m||mode==3)p.tracks[t].macroSend[m]=80;}
 Audio a;a.submit(p,true);float data[512];std::vector<long long> timing;double power=0;auto begin=std::chrono::steady_clock::now();
 for(int frame=0;frame<44100*6;frame+=256){auto start=std::chrono::steady_clock::now();a.render(data,256);timing.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());for(float v:data){if(!std::isfinite(v))return 1;power+=v*v;}}
 double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::sort(timing.begin(),timing.end());printf("12 tracks %s + REVERB:6s audio %.3fs p99 %lldus max %lldus energy %.2f\n",mode<0?"DRY":mode==3?"ALL MACROS":macroName(mode),seconds,timing[timing.size()*99/100],timing.back(),power);fflush(stdout);
}}
