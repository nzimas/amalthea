#include "effects.hpp"
#include "ui.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
using namespace amalthea;
static void require(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
static std::vector<float> source(){std::vector<float>x(44100*2);for(int i=0;i<44100;i++){x[2*i]=.12f*std::sin(i*.031f)+.06f*std::sin(i*.159f);x[2*i+1]=.11f*std::sin(i*.047f);}return x;}
int main(){
 auto input=source();
 for(auto type:{FxType::Clouds,FxType::AmpSim,FxType::Biztortion,FxType::Rotor}){
  InsertEffect a,b;InsertSettings cfg;a.setSettings(cfg);b.setSettings(cfg);a.configure({type,100},120);b.configure({type,100},120);
  auto x=input,y=input;for(int n=0;n<44100;n+=32){int count=std::min(32,44100-n);a.processBlock(x.data()+2*n,count);b.processBlock(y.data()+2*n,count);}
  require(x==y,"each instance deterministic and isolated");double energy=0,difference=0;
  for(int i=0;i<int(x.size());i++){require(std::isfinite(x[i]),"finite wet output");energy+=x[i]*x[i];difference+=std::abs(x[i]-input[i]);}
  require(energy>.01&&difference>10,"fully wet processor actually produces transformed audio");
  a.clear();a.setSettings(cfg);a.configure({type,100},120);auto rebuilt=input;
  for(int n=0;n<44100;n+=32)a.processBlock(rebuilt.data()+2*n,std::min(32,44100-n));require(rebuilt==x,"clear/rebuild equals fresh processor, no stale envelopes or buffers");
  a.configure({type,0},120);auto dry=input;a.processBlock(dry.data(),44100);require(dry==input,"zero mix is exact bypass");
  {Step st;st.fx[0]={type,75};InsertSettings settings{};int count=insertParameterCount(type,0,settings);require(count>=7,"all requested processors have multiple parameters");
   for(int field=0;field<count;field++){auto &v=insertParameter(st.fx[0],settings,field,0);v=uint8_t(std::min(100,int(v)+1));require(insertParameter(st.fx[0],settings,field,0)<=100,"parameters stay in range");}}
  printf("%s wet energy %.5f, difference %.2f\n",fxName(type),energy,difference);
 }
 // Boundary values exercise the original granular LUTs and circular buffers.
 for(int field=0;field<9;field++)for(int value:{0,100}){
  InsertSettings cfg;cfg.clouds[field]=value;InsertEffect effect;effect.setSettings(cfg);effect.configure({FxType::Clouds,100},120);auto x=input;effect.processBlock(x.data(),44100);for(float v:x)require(std::isfinite(v),"Clouds parameter endpoints remain in bounds and finite");
 }
 // Exercise every Biztortion module in every chain position, with its actual parameter list.
 for(int module=0;module<=100;module+=20){InsertSettings cfg;for(auto &s:cfg.biz)s.module=module;InsertEffect effect;effect.setSettings(cfg);effect.configure({FxType::Biztortion,100},120);auto x=input;effect.processBlock(x.data(),44100);for(float v:x)require(std::isfinite(v)&&std::abs(v)<=4,"all eight Biztortion positions bounded");}
 // Loudest settings must remain finite through the actual full tracker chain.
 for(auto type:{FxType::Clouds,FxType::AmpSim,FxType::Biztortion,FxType::Rotor}){Project p=demoProject();for(auto &t:p.tracks){for(auto &s:t.steps)s.fx[0]={type,100};}Audio a;a.submit(p,true);std::vector<float>x(44100*2);a.render(x.data(),44100);for(float v:x)require(std::isfinite(v)&&std::abs(v)<=.951f,"12-track insert output finite and limited");}
 puts("PASS: original DSP wet audio, isolation, clear, bypass, parameter edits, full-chain integration");
}
