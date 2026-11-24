// Synthetic state only: never reads or writes a live session.
#include "audio.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
using namespace amalthea;
int main(){
 for(bool drive:{false,true}){
 auto p=blankProject();for(int t=0;t<Tracks;t++){p.tracks[t].instrument=2;p.tracks[t].reverbSend=20;for(auto&i:p.tracks[t].inserts)i.filter={uint8_t((t%6)*20),65,80,uint8_t(drive?70:0),45};for(int r=0;r<16;r++){auto&s=p.tracks[t].steps[r];s.note=48+r%5;s.fx={Effect{FxType::Filter,100},Effect{FxType::Filter,75}};}}
 Audio a;a.submit(p,true);float data[512];std::vector<long long> timing;auto begin=std::chrono::steady_clock::now();double energy=0;
 for(int n=0;n<44100*6;n+=256){auto at=std::chrono::steady_clock::now();a.render(data,256);timing.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-at).count());for(auto v:data){if(!std::isfinite(v))return 1;energy+=v*v;}}
 std::sort(timing.begin(),timing.end());printf("24 stereo multimode filters drive%d: 6s audio %.3fs p99 %lldus max %lldus energy%.3f\n",drive,std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count(),timing[timing.size()*99/100],timing.back(),energy);fflush(stdout);
 }
}
