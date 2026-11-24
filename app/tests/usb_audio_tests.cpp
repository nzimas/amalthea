#include "usb_audio_out.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace amalthea;
static void check(bool x,const char*s){if(!x){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}

struct Result{double fillError,ratio,maxStep,minFill;uint64_t lateUnderruns,dropped;};

// Producer on the codec clock, consumer on the USB host clock, both 256-frame
// blocks. `ppm` is how much faster the producer runs than the consumer.
static Result simulate(double ppm,double seconds,double stallFrom=-1,double stallFor=0){
 DriftBridge b(2,16384,1536);
 const double rate=44100,producerRate=rate*(1+ppm*1e-6);const int block=256;
 double tp=0,tc=0,phase=0,last=0,prev=0,maxStep=0,ratioSum=0,fillSum=0,minFill=1e9;int seen=0;long ratioCount=0;uint64_t warmUnderruns=0;
 std::vector<float> in(block*2),out(block*2);
 while(tc<seconds){
  if(tp<=tc){
   for(int i=0;i<block;i++){float v=float(.5*std::sin(phase));in[2*i]=v;in[2*i+1]=-v;phase+=2*M_PI*441/rate;}
   b.push(in.data(),block);tp+=block/producerRate;
  } else {
   bool stalled=stallFrom>=0&&tc>=stallFrom&&tc<stallFrom+stallFor;
   if(!stalled){
    if(stallFrom>=0&&tc>=stallFrom+stallFor&&tc<stallFrom+stallFor+block/rate)b.restart();
    double before=b.fill()-b.target();
    bool ok=b.pull(out.data(),block);
    if(tc>seconds-30){ratioSum+=b.ratio();fillSum+=before;ratioCount++;}
    if(tc>20){
     if(!ok)warmUnderruns++;minFill=std::min(minFill,double(b.fill()));
     for(int i=0;i<block;i++){float v=out[2*i];check(std::fabs(out[2*i+1]+v)<1e-5f,"stereo lanes kept independent and aligned");
      if(seen>=2){double d2=std::fabs(v-2*last+prev);maxStep=std::max(maxStep,d2);}prev=last;last=v;seen++;}
    }
   }
   tc+=block/rate;
  }
 }
 return {ratioCount?fillSum/ratioCount:double(b.fill()-b.target()),ratioCount?ratioSum/ratioCount:b.ratio(),maxStep,minFill,warmUnderruns,b.dropped()};
}

int main(){
 check(typeCModeValue(0x13,TypeCMode::Sink)==0x11,"sink mode keeps prefer/current bits");
 check(typeCModeValue(0x11,TypeCMode::Drp)==0x13,"DRP restores the boot value");
 check(typeCModeValue(0xff,TypeCMode::Sink)==0xfd,"only port-mode bits change");

 // A clean 441 Hz sine at 0.5 has |second difference| <= 0.5*(2*pi*441/44100)^2 = 0.00197.
 const double clean=0.5*std::pow(2*M_PI*441/44100.,2)*1.5;
 for(double ppm:{0.,500.,-500.,2000.}){
  auto r=simulate(ppm,400);
  fprintf(stderr,"ppm %+6.0f fill error %+7.1f min fill %6.0f ratio-1 %+.6f max step %.5f underruns %llu dropped %llu\n",
   ppm,r.fillError,r.minFill,r.ratio-1,r.maxStep,(unsigned long long)r.lateUnderruns,(unsigned long long)r.dropped);
  check(std::fabs(r.fillError)<64,"average fill converges to target");
  check(std::fabs((r.ratio-1)-ppm*1e-6)<1e-5,"ratio learns the clock offset");
  check(r.minFill>256,"fill never approaches underrun while locking");
  check(r.lateUnderruns==0,"no underruns once locked");
  check(r.dropped==0,"producer never drops while consumer runs");
  check(r.maxStep<clean,"output stays continuous (no clicks)");
 }
 // USB host stops reading for 3 s (producer overflows and drops), then returns.
 auto s=simulate(300,200,100,3);
 check(s.dropped>0,"stalled consumer makes producer drop instead of blocking");
 check(std::fabs(s.fillError)<64,"fill returns to target after the host resumes");
 check(s.lateUnderruns<=2,"resume primes without repeated underruns");
 puts("usb audio bridge ok");
}
