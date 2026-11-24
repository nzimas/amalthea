// LIFEBLOOD CPU: each insert alone, then many grown networks including their
// crossfades, against the 5805 us deadline of a 256-frame block.
#include "fx_audio.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace amalthea;
static long long frame=0;
static void block(FxAudio&a,std::vector<long long>*time){
 float in[512],out[512];for(int i=0;i<256;i++){float v=.25f*std::sin(.031f*(frame+i))+.1f*std::sin(.0071f*(frame+i));in[i*2]=in[i*2+1]=v;}frame+=256;
 a.feed(in,256,1,44100);auto t=std::chrono::steady_clock::now();a.render(out,256);
 if(time)time->push_back(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t).count());
}
int main(){
 DeviceSettings device;uint32_t generation=1;
 // One engine for the whole run: on the Brick, destroying it restores the USB role.
 AudioStats stats;FxAudio audio(stats);stats.usbState=4;
 {
  // Cost depends on the parameters, so each insert is measured over random draws.
  for(auto type:selectableEffects()){if(type==FxType::None||type==FxType::Random)continue;
   std::vector<long long> medians;uint32_t seed=0x1234+int(type);
   for(int draw=0;draw<20;draw++){
    auto n=blankLifeblood();n.link={};n.node[0].active=true;n.node[0].effect={type,70};n.node[0].rank=1;n.link[LifebloodIn][2]=100;n.link[2][LifebloodOut]=100;
    reseedLifebloodNode(n,0,seed);n.generation=++generation;while(!audio.submit(n,120,100,false,device))block(audio,nullptr);audio.activate(true);
    for(int i=0;i<100;i++)block(audio,nullptr);   // past the crossfade
    std::vector<long long> t;for(int i=0;i<60;i++)block(audio,&t);std::sort(t.begin(),t.end());medians.push_back(t[t.size()/2]);
   }
   std::sort(medians.begin(),medians.end());long long sum=0;for(auto m:medians)sum+=m;
   printf("%-16s cost %2d  mean %5lld us  p90 %5lld us  max %5lld us\n",fxName(type),lifebloodCost(type),sum/(long long)medians.size(),medians[medians.size()*9/10],medians.back());fflush(stdout);
  }}
 {std::vector<long long> all,steady;long long worst=0;int worstCost=0;
  for(int i=0;i<60;i++){uint32_t seed=i*2654435761u+11;auto n=growLifeblood({uint8_t(1+i%5),7},seed,device);n.generation=++generation;
   while(!audio.submit(n,120,100,false,device))block(audio,&all);audio.activate(true);
   std::vector<long long> t;for(int b=0;b<260;b++)block(audio,&t);   // 1.5 s: crossfade then steady
   long long m=*std::max_element(t.begin(),t.end());int c=0;for(auto&k:n.node)if(k.active)c+=lifebloodCost(k.effect.type);
   if(m>worst){worst=m;worstCost=c;}all.insert(all.end(),t.begin(),t.end());steady.insert(steady.end(),t.begin()+100,t.end());}
  std::sort(all.begin(),all.end());std::sort(steady.begin(),steady.end());
  fflush(stdout);printf("60 networks: steady p99 %lld us; with crossfades p99 %lld us, max %lld us (cost %d); deadline 5805 us\n",steady[steady.size()*99/100],all[all.size()*99/100],worst,worstCost);fflush(stdout);
 }
 // The FX appliance: twelve slots of random QDELAY and MATRIX draws.
 {DeviceSettings only;for(auto t:selectableEffects())if(t!=FxType::QDelay&&t!=FxType::ChowMatrix&&t!=FxType::None)only.disabledInserts.set(int(t));
  std::vector<long long> t;for(int round=0;round<10;round++){FxRack r;r.seed=round*7919+1;for(int k=0;k<12;k++)randomizeFxSlot(r,k,false,only);
   while(!audio.submit(r,only))block(audio,nullptr);for(int b=0;b<120;b++)block(audio,b>20?&t:nullptr);}
  std::sort(t.begin(),t.end());printf("FX 12 random QDELAY/MATRIX slots: p99 %lld us, max %lld us; deadline 5805 us\n",t[t.size()*99/100],t.back());fflush(stdout);}
}
