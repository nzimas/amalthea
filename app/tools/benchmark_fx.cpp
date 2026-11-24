#include "fx_audio.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
using namespace amalthea;
int main(){for(auto type:{FxType::Filter,FxType::QDelay,FxType::ChowMatrix,FxType::CrushClone,FxType::Rings,FxType::Space}){
 AudioStats stats;FxAudio audio(stats);FxRack rack;for(auto&s:rack.slots)s.effect={type,70};audio.submit(rack,DeviceSettings{});
 float input[256*12],out[512];std::vector<long long>time;double energy=0;
 for(int block=0;block<44100*2/256;block++){for(int i=0;i<256;i++)for(int c=0;c<12;c++)input[i*12+c]=.025f*std::sin((block*256+i)*(.02f+c*.003f));audio.feed(input,256,6,44100);
 auto at=std::chrono::steady_clock::now();audio.render(out,256);time.push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-at).count());for(float v:out){if(!std::isfinite(v))return 1;energy+=v*v;}}
 std::sort(time.begin(),time.end());printf("%s 12 slots: p99 %lldus, max %lldus, deadline5805us, energy %.2f\n",fxName(type),time[time.size()*99/100],time.back(),energy);fflush(stdout);
}}
