// Standalone synthetic audition. No application/session access.
#include "lush_reverb.hpp"
#include "tal_reverb.hpp"
#include "audio.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>
using namespace amalthea;
int main(int argc,char**argv){
 std::filesystem::path dir=argc>1?argv[1]:"build/reverb-audition";std::filesystem::create_directories(dir);
 for(int vast=0;vast<2;vast++){
  TalReverbBus hall;LushReverb rv;MasterSettings s;s.extra[9]={65,35,65};s.reverbSpace={33,100,35,85};if(vast){s.extra[9]={85,25,85};s.reverbSpace={30,100,45,100};}rv.configure(s);rv.clear();
  const int seconds=vast?32:24;std::vector<float> audio(seconds*44100*2);
  for(int n=0;n<seconds*44100;n++){
   double t=double(n)/44100;float input=0;
   // A dry reference chord, then the identical chord through the new space.
   double local=t<3?t:t-3;
   if(local<1.6){double env=(1-std::exp(-local*150))*std::exp(-local*2.2);for(int note:{48,55,60,63,67}){double f=440*std::pow(2.,(note-69)/12.);input+=float(.065*env*(std::sin(6.283185307*f*local)+.25*std::sin(6.283185307*f*2*local)));}}
   // Metallic short bursts demonstrate diffusion without a sustained synth masking it.
   for(double hit:{7.,9.})if(t>=hit&&t<hit+.25){double x=t-hit;input+=float(.2*std::exp(-x*40)*(std::sin(x*6283)+.5*std::sin(x*10093)+.2*std::sin(x*23371)));}
   float l=0,r=0;if(t>=3){if(vast)rv.process(input,input,l,r);else hall.process(input,input,l,r);}
   audio[2*n]=input*.75f+l*.85f;audio[2*n+1]=input*.75f+r*.85f;
  }
  auto path=dir/(vast?"vast-space.wav":"tal-iii-master.wav");writeWav(path.string(),audio.data(),audio.size()/2);printf("%s\n",path.c_str());
 }
}
