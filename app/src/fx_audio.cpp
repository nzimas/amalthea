#include "fx_audio.hpp"
#include "effects.hpp"
#include "usb_role.hpp"
#include "audio_probe.hpp"
#include "audio_health.hpp"
#include "persistent_audio_log.hpp"
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#ifdef AMALTHEA_USB_HOST
#include <alsa/asoundlib.h>
#endif
namespace amalthea {
namespace {
constexpr int Ring=8192,Queue=4,PrimeFrames=1536,TargetFrames=1792;
[[maybe_unused]] constexpr long CaptureGuardFrames=96; // two full-speed packets
double nowSeconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
struct SlotDSP {
 InsertEffect effect;std::unique_ptr<PortedProcessor> port;InsertSettings last{};Effect lastEffect{};int lastBpm=-1;bool configured=false;
 void apply(Effect e,const InsertSettings&s,int bpm){if(!configured||e.type!=lastEffect.type||e.value!=lastEffect.value||bpm!=lastBpm||memcmp(&s,&last,sizeof s)){effect.configure(e,bpm);effect.setSettings(s);last=s;lastEffect=e;lastBpm=bpm;configured=true;}}
 explicit SlotDSP(FxType type):effect(type==FxType::Space){int k=portedIndex(type);if(k>=0){port=std::make_unique<PortedProcessor>(type);PortedPointers ptr{};ptr[k]=port.get();effect.attachPorted(ptr);}}
};
}
struct FxAudio::Impl {
 std::unique_ptr<AudioProbe> probe;
 AudioStats&stats;std::atomic<bool> enabled{false},alive{true};std::thread usb;
 std::array<std::array<float,12>,Ring> ring{};std::atomic<uint64_t> written{0},consumed{0};
 std::atomic<bool> resetRequested{false};float limiter=1;
 std::atomic<int> inputRate{44100},inputChannels{6};double read=0,drift=0;bool primed=false;float inputGain=0;std::array<float,12> lastInput{};
 struct Message {FxRack rack;DeviceSettings device;std::array<SlotDSP*,12> dsp{};
  // LIFEBLOOD messages carry a network instead of a rack.
  bool network=false;LifebloodNetwork net;int netTempo=120,netMix=100;bool netBypass=false;uint32_t netEpoch=0;std::array<SlotDSP*,LifebloodMaxNodes> netDsp{};};
 std::array<Message,Queue> messages{};std::atomic<uint64_t> head{0},tail{0},completed{0};
 std::array<std::unique_ptr<SlotDSP>,12> owners{};std::array<FxType,12> ownerTypes{};
 // A retired processor is freed once the audio has taken a later message and, for a
 // network's processors, once the crossfade away from them has finished (epoch).
 struct Retired {uint64_t after;std::unique_ptr<SlotDSP> ptr;uint32_t epoch=0;};std::vector<Retired> retired;
 // LIFEBLOOD, submit side (UI thread).
 std::array<std::unique_ptr<SlotDSP>,LifebloodMaxNodes> netOwners{};std::array<FxType,LifebloodMaxNodes> netOwnerTypes{};
 uint32_t netGeneration=0,netEpochCounter=0;
 // LIFEBLOOD, audio side.
 struct Graph {
  LifebloodNetwork net;std::array<SlotDSP*,LifebloodMaxNodes> dsp{};uint32_t epoch=0;
  std::array<std::array<float,LifebloodVertices>,LifebloodVertices> gain{};
  std::array<float,LifebloodMaxNodes> wet{};std::array<uint8_t,LifebloodMaxNodes> dip{};   // 1 dipping, 2 returning
  std::array<int,LifebloodVertices> order{};int count=0;
  void sort(){count=0;for(int v=0;v<LifebloodVertices;v++)if(!lifebloodInput(v)&&net.live(v))order[count++]=v;
   std::sort(order.begin(),order.begin()+count,[&](int a,int b){return net.rank(a)<net.rank(b);});}
 };
 Graph graph,fadingGraph;bool networkMode=false,crossfading=false;float crossfade=1,netWet=1;int netTempo=120,netMix=100;bool netBypass=false;
 std::atomic<uint32_t> settledEpoch{0};
 // Re-entering the appliance: the first new network starts clean instead of
 // crossfading from processors that were frozen while the appliance was closed.
 std::atomic<bool> freshStart{false};bool skipFade=false;
 float netBuffers[2][LifebloodVertices][128]{};
 LevelMatch netLevel;   // LIFEBLOOD: the network's output keeps the input's loudness
 float trackIn[LifebloodTracks][512]{};                 // this block's FM1 track lanes, stereo
 std::array<int64_t,LifebloodTracks> silentFor{{int64_t(1)<<40,int64_t(1)<<40,int64_t(1)<<40,int64_t(1)<<40,int64_t(1)<<40,int64_t(1)<<40}};
 // Render one graph over n <= 64 frames from in (interleaved stereo) into out.
 // in: the stereo IN for this chunk; tracks: each FM1 track's stereo lane, offset `at`.
 void renderGraph(Graph&g,const float*in,const float (*tracks)[512],int at,float*out,int n,int bpm,bool ramp){
  auto&buf=netBuffers[&g==&graph?0:1];std::copy(in,in+n*2,buf[LifebloodIn]);
  for(int t=0;t<LifebloodTracks;t++)std::copy(tracks[t]+at*2,tracks[t]+at*2+n*2,buf[LifebloodTrackBase+t]);
  constexpr float EdgeStep=1.f/5292,DipDown=1.f/1102,DipUp=1.f/5292;   // 120 ms vessels, 25 ms dip, 120 ms return
  for(int i=0;i<g.count;i++){
   int v=g.order[i];float*dst=buf[v];std::fill(dst,dst+n*2,0.f);
   float sum0=0,sum1=0;
   // OUT: separate FM1 tracks passing straight through are independent parts of the
   // mix: they keep their level and stay out of the normalisation below.
   for(int u=0;u<LifebloodVertices;u++){
    float target=g.net.link[u][v]*.01f,&cur=g.gain[u][v];if(!ramp)cur=target;
    float start=cur,end=cur;if(cur<target)end=std::min(target,cur+EdgeStep*n);else if(cur>target)end=std::max(target,cur-EdgeStep*n);cur=end;
    if(start<=0&&end<=0)continue;if(!(v==LifebloodOut&&u>=LifebloodTrackBase)){sum0+=start;sum1+=end;}const float*src=buf[u];
    for(int f=0;f<n;f++){float gn=start+(end-start)*(f+1)/n;dst[f*2]+=src[f*2]*gn;dst[f*2+1]+=src[f*2+1]*gn;}
   }
   // Several vessels into a node are averaged, never summed louder than one. Into OUT
   // they are summed louder (by the square root), so every vessel stays clearly heard;
   // the output limiter catches the rest.
   float n0=v==LifebloodOut?1/std::sqrt(std::max(1.f,sum0)):1/std::max(1.f,sum0),n1=v==LifebloodOut?1/std::sqrt(std::max(1.f,sum1)):1/std::max(1.f,sum1);
   for(int f=0;f<n;f++){float k=n0+(n1-n0)*(f+1)/n;dst[f*2]*=k;dst[f*2+1]*=k;}
   if(v==LifebloodOut)continue;
   int k=v-2;auto*dsp=g.dsp[k];if(!dsp){std::fill(dst,dst+n*2,0.f);continue;}
   auto&node=g.net.node[k];
   bool differs=node.effect.type!=dsp->lastEffect.type||node.effect.value!=dsp->lastEffect.value||memcmp(&node.settings,&dsp->last,sizeof node.settings);
   if(differs&&!g.dip[k])g.dip[k]=1;
   if(!differs&&bpm!=dsp->lastBpm)dsp->apply(node.effect,node.settings,bpm);
   float w0=g.wet[k];
   if(g.dip[k]==1){g.wet[k]=std::max(0.f,w0-DipDown*n);if(g.wet[k]<=0){dsp->apply(node.effect,node.settings,bpm);g.dip[k]=2;}}
   else if(g.dip[k]==2){g.wet[k]=std::min(1.f,w0+DipUp*n);if(g.wet[k]>=1)g.dip[k]=0;}
   float w1=g.wet[k];float input[128];bool blend=w0<1||w1<1;if(blend)std::copy(dst,dst+n*2,input);
   dsp->effect.processBlock(dst,n);
   if(blend)for(int f=0;f<n;f++){float w=w0+(w1-w0)*(f+1)/n;for(int c=0;c<2;c++){float&x=dst[f*2+c];x=input[f*2+c]+w*(x-input[f*2+c]);}}
  }
  std::copy(buf[LifebloodOut],buf[LifebloodOut]+n*2,out);
 }
 void adoptNetwork(const Message&m){
  netTempo=m.netTempo;netMix=m.netMix;netBypass=m.netBypass;
  if(m.netEpoch!=graph.epoch){
   // A new network: the current one fades out on its own processors.
   fadingGraph=graph;crossfading=fadingGraph.epoch!=0&&!skipFade;skipFade=false;if(!crossfading)fadingGraph=Graph{};crossfade=crossfading?0:1;
   graph=Graph{};graph.epoch=m.netEpoch;graph.wet.fill(1);graph.net=m.net;graph.dsp=m.netDsp;graph.sort();
   for(int a=0;a<LifebloodVertices;a++)for(int b=0;b<LifebloodVertices;b++)graph.gain[a][b]=graph.net.link[a][b]*.01f;
   if(!crossfading)settledEpoch.store(graph.epoch,std::memory_order_release);
   return;
  }
  graph.net=m.net;
  for(int k=0;k<LifebloodMaxNodes;k++){if(graph.dsp[k]!=m.netDsp[k]){graph.wet[k]=1;graph.dip[k]=0;}}
  graph.dsp=m.netDsp;graph.sort();
 }
 void renderNetwork(float (*lanes)[512],int count,float*out,int frames,int bpm){
  bool stereo=stats.usbState.load()==4;
  float in[512],peakIn=0;bool tracksIn=!stereo&&count>1;
  // Separate FM1 tracks: each at the FX strips' default 80%, spread gently across the
  // field. They feed the track inputs; their sum is the stereo IN.
  std::fill(&trackIn[0][0],&trackIn[0][0]+LifebloodTracks*512,0.f);
  for(int i=0;i<frames;i++){
   float l=0,r=0;
   if(!tracksIn){l=lanes[0][i*2];r=lanes[0][i*2+1];}
   else for(int c=0;c<count&&c<LifebloodTracks;c++){
    float pan=.2f+.6f*c/(count-1),x=lanes[c][i*2]*.8f*1.41421356f,tl=x*std::cos(pan*1.5707963f),tr=x*std::sin(pan*1.5707963f);
    trackIn[c][i*2]=tl;trackIn[c][i*2+1]=tr;l+=tl;r+=tr;
   }
   in[i*2]=l;in[i*2+1]=r;peakIn=std::max(peakIn,std::max(std::abs(l),std::abs(r)));
  }
  // Which tracks are sounding: any sample above -70 dBFS within the last 30 seconds.
  uint8_t heard=0;
  for(int c=0;c<LifebloodTracks;c++){
   float p=0;if(tracksIn&&c<count)for(int i=0;i<frames;i++)p=std::max(p,std::abs(lanes[c][i*2]));
   if(p>.0003f)silentFor[c]=0;else silentFor[c]=std::min<int64_t>(silentFor[c]+frames,int64_t(1)<<40);
   if(tracksIn&&c<count&&silentFor[c]<44100LL*30)heard|=uint8_t(1u<<c);
  }
  stats.lifebloodHeard.store(heard,std::memory_order_relaxed);
  int clouds=0,spaces=0;for(auto*g:{&graph,&fadingGraph}){if(g==&fadingGraph&&!crossfading)continue;for(auto&k:g->net.node)if(k.active){clouds+=k.effect.type==FxType::Clouds;spaces+=k.effect.type==FxType::Space;}}
  for(auto*g:{&graph,&fadingGraph})for(auto*d:g->dsp)if(d){d->effect.setGrainLimit(std::clamp(128/std::max(1,clouds),4,32));d->effect.setSpaceLineLimit(std::clamp(36/std::max(1,spaces),2,12));}
  constexpr float FadeStep=1.f/22050,WetStep=1.f/2205;   // 0.5 s network crossfade, 50 ms wet
  std::array<float,LifebloodVertices> peaks{};float wetNew[512],wetOld[512];
  // During a crossfade the outgoing network renders on a worker at the same time.
  bool parallel=crossfading;
  if(parallel){jobNetwork=true;jobNetIn=in;jobNetOut=wetOld;jobFrames=frames;jobBpm=bpm;
   {std::lock_guard<std::mutex>lock(jobMutex);remaining=2;generation++;}jobReady.notify_all();}
  for(int at=0;at<frames;at+=64){int n=std::min(64,frames-at);
   renderGraph(graph,in+at*2,trackIn,at,wetNew+at*2,n,bpm,true);
   for(int v=0;v<LifebloodVertices;v++){if(v==LifebloodIn)continue;float p=0;for(int f=0;f<n*2;f++)p=std::max(p,std::abs(netBuffers[0][v][f]));peaks[v]=std::max(peaks[v],p);}
  }
  if(parallel){std::unique_lock<std::mutex>lock(jobMutex);jobDone.wait(lock,[&]{return remaining==0;});jobNetwork=false;}
  // The new network crossfades in over the old; the whole network then keeps the input's
  // loudness (on top of each node's own match), and MIX / bypass blend it with the dry input.
  float wet[512];
  for(int i=0;i<frames;i++){
   float gNew=1,gOld=0;if(parallel){crossfade=std::min(1.f,crossfade+FadeStep);gNew=std::sin(crossfade*1.5707963f);gOld=std::cos(crossfade*1.5707963f);}
   for(int c=0;c<2;c++)wet[i*2+c]=wetNew[i*2+c]*gNew+(parallel?wetOld[i*2+c]*gOld:0);
  }
  netLevel.apply(in,wet,frames);
  for(int i=0;i<frames;i++){
   float target=netBypass?0.f:netMix*.01f;netWet=target>netWet?std::min(target,netWet+WetStep):std::max(target,netWet-WetStep);
   for(int c=0;c<2;c++){float dry=in[i*2+c];out[i*2+c]=dry+netWet*(wet[i*2+c]-dry);}
  }
  if(crossfading&&crossfade>=1){crossfading=false;fadingGraph=Graph{};settledEpoch.store(graph.epoch,std::memory_order_release);}
  peaks[LifebloodIn]=peakIn;float po=0;for(int i=0;i<frames*2;i++)po=std::max(po,std::abs(out[i]));peaks[LifebloodOut]=po;
  for(int v=0;v<LifebloodVertices;v++)stats.lifebloodLevels[v]=std::max(peaks[v],stats.lifebloodLevels[v].load()*.94f);
 }
 uint64_t submittedReset=0;
 FxRack rack{};DeviceSettings device{};std::array<SlotDSP*,12> dsp{};
 std::array<AmaltheaWalk,12> walks{};std::array<int,12> current{},lengths{},directions{};std::array<double,12> phase{};std::array<float,6> wetMix{{1,1,1,1,1,1}};std::array<float,12> stepGate{{1,1,1,1,1,1,1,1,1,1,1,1}};
 std::atomic<uint64_t> clockTicks{0},clockEpoch{0};uint64_t lastTicks=0,lastEpoch=0;MidiClock clock{};
 std::array<std::thread,2> workers;std::mutex jobMutex;std::condition_variable jobReady,jobDone;
 uint64_t generation=0;int remaining=0;bool workersExit=false;
 bool jobNetwork=false;const float*jobNetIn=nullptr;float*jobNetOut=nullptr;   // LIFEBLOOD crossfade job
 float (*jobAudio)[512]=nullptr;int jobFrames=0;double jobPulses=0;bool jobRunning=false,jobExternal=false;int jobBpm=120;
 void channel(int c){
  // Each channel is rendered by exactly one thread, so wetMix[c] needs no synchronisation.
  constexpr float BypassStep=1.f/441; // 10 ms crossfade between processed and dry
  constexpr float GateStep=1.f/132;   // 3 ms step-gate fade: audible rhythm, no clicks
  for(int at=0;at<jobFrames;at+=32){int n=std::min(32,jobFrames-at);auto*data=jobAudio[c]+at*2;
   float target=rack.bypass[c]?0.f:1.f;bool process=wetMix[c]>0||target>0;
   bool fading=process&&(wetMix[c]<1||target<1);
   float dry[64];if(fading)std::copy(data,data+n*2,dry);
   for(int slot=0;slot<2;slot++){
    int k=c*2+slot;auto&s=rack.slots[k];
    // Internal clock: each slot runs at its own tempo (0 follows the session).
    int bpm=jobExternal?jobBpm:fxSlotTempo(rack,k);
    double pulses=jobExternal?jobPulses:double(jobFrames)*bpm*24/(60*44100);
    // Sequencers keep their position while bypassed so re-enabling stays in phase.
    if(jobRunning){if(current[k]<0)current[k]=amalthea_walk_next(&walks[k],s.direction,s.length);phase[k]+=pulses*clockRate(s.clock)/6*n/jobFrames;while(phase[k]>=1){phase[k]-=1;current[k]=amalthea_walk_next(&walks[k],s.direction,s.length);}}
    stats.fxStep[k]=jobRunning?current[k]:-1;
    if(!process||!dsp[k]||s.effect.type==FxType::None||!device.insertEnabled(s.effect.type)){stepGate[k]=1;continue;}
    // A running sequence applies the effect only on triggered steps; empty steps
    // pass dry. Stopped or unsequenced slots process continuously at defaults.
    int step=jobRunning?current[k]:-1;
    float gate=jobRunning&&s.sequenced?(step>=0&&step<s.length&&s.steps[step].enabled?1.f:0.f):1.f;
    bool gated=gate<1||stepGate[k]<1;float input[64];if(gated)std::copy(data,data+n*2,input);
    Effect e;InsertSettings settings;resolveFxStep(s,step,e,settings);dsp[k]->apply(e,settings,bpm);
    dsp[k]->effect.processBlock(data,n);
    if(gated)for(int i=0;i<n;i++){
     stepGate[k]=gate>stepGate[k]?std::min(gate,stepGate[k]+GateStep):std::max(gate,stepGate[k]-GateStep);
     for(int ch=0;ch<2;ch++){float&v=data[i*2+ch];v=input[i*2+ch]+stepGate[k]*(v-input[i*2+ch]);}
    }
   }
   if(fading)for(int i=0;i<n;i++){
    wetMix[c]=target>wetMix[c]?std::min(target,wetMix[c]+BypassStep):std::max(target,wetMix[c]-BypassStep);
    for(int ch=0;ch<2;ch++){float&v=data[i*2+ch];v=dry[i*2+ch]+wetMix[c]*(v-dry[i*2+ch]);}
   }
  }
 }
 void startWorkers(){for(int group=0;group<2;group++)workers[group]=std::thread([this,group]{
#ifdef __linux__
  sched_param priority{};priority.sched_priority=11;pthread_setschedparam(pthread_self(),SCHED_FIFO,&priority);
#endif
#if defined(__aarch64__)
  uint64_t fpcr;asm volatile("mrs %0, fpcr":"=r"(fpcr));fpcr|=uint64_t(1)<<24;asm volatile("msr fpcr, %0"::"r"(fpcr));
#endif
  std::unique_lock<std::mutex>lock(jobMutex);uint64_t seen=0;
  while(true){jobReady.wait(lock,[&]{return generation!=seen||workersExit;});if(workersExit)return;seen=generation;
   lock.unlock();
   if(jobNetwork){if(group==0)for(int at=0;at<jobFrames;at+=64){int n=std::min(64,jobFrames-at);renderGraph(fadingGraph,jobNetIn+at*2,trackIn,at,jobNetOut+at*2,n,jobBpm,true);}}
   else {channel(group*2+2);channel(group*2+3);}
   lock.lock();if(--remaining==0)jobDone.notify_one();}
 });}
 explicit Impl(AudioStats&s):stats(s){
#ifdef AMALTHEA_USB_HOST
  if(std::filesystem::exists("/tmp/amalthea-audio-probe.enabled")){probe=std::make_unique<AudioProbe>("/mnt/UDISK/tracker-linux/logs/audio");std::remove("/tmp/amalthea-audio-probe.enabled");fprintf(stderr,"Five-second raw USB / codec-input audio diagnostic armed\n");}
#endif
  current.fill(-1);for(int i=0;i<12;i++)amalthea_walk_start(&walks[i],0x19852+i);startWorkers();usb=std::thread([this]{host();});}
 ~Impl(){alive=false;if(usb.joinable())usb.join();{std::lock_guard<std::mutex>lock(jobMutex);workersExit=true;}jobReady.notify_all();for(auto&worker:workers)if(worker.joinable())worker.join();}
 void push(const float*in,int frames,int channels,int rate){
  inputRate=rate;inputChannels=channels;auto w=written.load(std::memory_order_relaxed),r=consumed.load(std::memory_order_acquire);
  for(int i=0;i<frames;i++){
   if(w-r>=Ring-2){stats.usbOverruns++;break;}
   auto&frame=ring[w%Ring];for(int c=0;c<12;c++)frame[c]=c<channels*2?in[i*channels*2+c]:0;w++;
  }
  written.store(w,std::memory_order_release);
 }
 void byte(uint8_t b,double when){clock.byte(b,when);if(clock.reset){clockEpoch++;clock.reset=false;}clockTicks=clock.ticks;stats.usbBpm=clock.bpm;stats.usbClock=clock.locked;stats.usbRunning=clock.running;}
 void host(){
#ifdef AMALTHEA_USB_HOST
  PersistentAudioLog durable("/mnt/UDISK/tracker-linux/logs/audio");
  std::string lastDiagnostic;
  auto diagnostic=[&](const std::string&value){
   if(value==lastDiagnostic)return;
   lastDiagnostic=value;
   AudioHealthRecord event;event.seconds=nowSeconds();event.event="usb_status";durable.push(event,value.c_str());
   std::ofstream file("/tmp/amalthea-usb-status.next");file<<value<<"\n";file.close();
   std::rename("/tmp/amalthea-usb-status.next","/tmp/amalthea-usb-status");
   fprintf(stderr,"FX USB: %s\n",value.c_str());
  };
  sched_param capturePriority{};capturePriority.sched_priority=13;
  int captureRt=pthread_setschedparam(pthread_self(),SCHED_FIFO,&capturePriority);
  fprintf(stderr,"USB capture realtime scheduling: %s\n",captureRt?"unavailable":"enabled");
  auto record=[&](const char*event,int status=0,const char*detail=""){AudioHealthRecord r;
   r.seconds=nowSeconds();r.event=event;r.status=status;r.frames=stats.usbFrames.load();
   r.captureXruns=stats.usbCaptureXruns.load();r.starvation=stats.usbUnderruns.load();r.overflow=stats.usbOverruns.load();
   r.outputXruns=stats.overruns.load();r.maxGapUs=stats.usbCaptureMaxGapUs.load();r.maxRenderUs=stats.fxRenderMaxUs.load();
   r.fill=stats.usbFill.load();r.clock=stats.usbClock.load();r.running=stats.usbRunning.load();durable.push(r,detail);
  };
  double lastCapture=0,nextHealth=0,nextLease=0;bool packetLease=false;
  auto health=[&]{double now=nowSeconds();if(now<nextHealth)return;nextHealth=now+1;record("health");
   std::ofstream f("/tmp/amalthea-audio-health.next");
   f<<"Capture frames: "<<stats.usbFrames.load()<<"\nCapture xruns: "<<stats.usbCaptureXruns.load()
    <<"\nInput starvation: "<<stats.usbUnderruns.load()<<"\nRing overflow: "<<stats.usbOverruns.load()
    <<"\nInput fill frames: "<<stats.usbFill.load()<<"\nMaximum capture gap us: "<<stats.usbCaptureMaxGapUs.load()
    <<"\nOutput xruns: "<<stats.overruns.load()<<"\nMaximum FX render us: "<<stats.fxRenderMaxUs.load()
    <<"\nCapture buffer request us: 64000\nPlayback buffer request us: 80000\nInput prime frames: "<<PrimeFrames<<"\nPersistent logger dropped events: "<<durable.dropped()<<"\n";
   f.close();std::rename("/tmp/amalthea-audio-health.next","/tmp/amalthea-audio-health");
  };
  BrickUsbRole port;
  record("appliance_start");
  diagnostic("Host scanner ready; waiting for FX appliance");
  snd_pcm_t*pcm=nullptr;snd_rawmidi_t*midi=nullptr;int card=-1,deviceNumber=0,channels=0,rate=44100;UsbMapping mapping;auto next=std::chrono::steady_clock::now();
  snd_ctl_card_info_t*info;snd_ctl_card_info_alloca(&info);snd_pcm_hw_params_t*hw;snd_pcm_hw_params_alloca(&hw);
  std::array<int16_t,256*8> samples{};std::array<float,256*12> lanes{};
  auto close=[&]{if(pcm){record("capture_close");snd_pcm_close(pcm);}if(midi)snd_rawmidi_close(midi);pcm=nullptr;midi=nullptr;stats.usbState=0;stats.usbChannels=0;stats.usbClock=false;stats.usbRunning=false;};
  uint64_t previousStarvation=0,previousOverflow=0,previousOutput=0;double nextCounter=0;
  while(alive){
   health();double check=nowSeconds();
   if(check>=nextCounter){nextCounter=check+.1;auto a=stats.usbUnderruns.load(),b=stats.usbOverruns.load(),c=stats.overruns.load();
    if(a!=previousStarvation||b!=previousOverflow||c!=previousOutput){record("counter_change");previousStarvation=a;previousOverflow=b;previousOutput=c;}}
   if(!enabled){if(pcm){nextHealth=0;health();close();}lastCapture=0;port.poll(false,nowSeconds());stats.usbPort=port.state;diagnostic("FX appliance inactive; capture closed");std::this_thread::sleep_for(std::chrono::milliseconds(40));continue;}
   double leaseTime=nowSeconds();
   if(leaseTime>=nextLease){nextLease=leaseTime+.25;int pid=0;std::ifstream lease("/tmp/amalthea-packet-transition.pid");lease>>pid;
    packetLease=pid>0&&(kill(pid,0)==0||errno==EPERM);
   }
   if(packetLease){if(pcm){nextHealth=0;health();close();}stats.usbState=0;diagnostic("Packet diagnostic owns USB; FX capture paused");std::this_thread::sleep_for(std::chrono::milliseconds(40));continue;}
   port.poll(true,leaseTime);stats.usbPort=port.state;
   auto time=std::chrono::steady_clock::now();
   if(!pcm&&time>=next){
    next=time+std::chrono::milliseconds(500);card=-1;bool candidate=false;std::string failure;
    while(snd_card_next(&card)>=0&&card>=0){
     char ctlName[32];snprintf(ctlName,sizeof ctlName,"hw:%d",card);snd_ctl_t*ctl=nullptr;if(snd_ctl_open(&ctl,ctlName,SND_CTL_NONBLOCK)<0)continue;
     if(snd_ctl_card_info(ctl,info)<0){snd_ctl_close(ctl);continue;}
     std::string description=std::string(snd_ctl_card_info_get_name(info))+" "+snd_ctl_card_info_get_longname(info);int firmware=fm1Firmware(description);
     std::ifstream idFile("/proc/asound/card"+std::to_string(card)+"/usbid");std::string usbid;idFile>>usbid;
     if(!firmware&&usbid.rfind("1209:",0)!=0){snd_ctl_close(ctl);continue;}
     candidate=true;failure="FM1 ALSA card detected but no capture PCM: "+description+" ["+usbid+"]";deviceNumber=-1;
     while(snd_ctl_pcm_next_device(ctl,&deviceNumber)>=0&&deviceNumber>=0){
      char name[32];snprintf(name,sizeof name,"hw:%d,%d",card,deviceNumber);
      int opened=snd_pcm_open(&pcm,name,SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK);if(opened<0){failure=std::string(name)+" capture open: "+snd_strerror(opened);pcm=nullptr;stats.usbState=3;continue;}
      unsigned max=0;
      if(snd_pcm_hw_params_any(pcm,hw)<0||snd_pcm_hw_params_get_channels_max(hw,&max)<0||max>8){failure=std::string(name)+" unsupported capture channel range (max "+std::to_string(max)+")";stats.usbState=2;snd_pcm_close(pcm);pcm=nullptr;continue;}
      channels=max;mapping=fm1Mapping(firmware,channels);if(!mapping.count){failure=std::string(name)+" unsupported mapping for "+std::to_string(channels)+" channels";snd_pcm_close(pcm);pcm=nullptr;stats.usbState=2;continue;}
      int err=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,channels,44100,0,64000);rate=44100;
      if(err<0){err=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,channels,48000,0,64000);rate=48000;}
      if(err<0){failure=std::string(name)+" audio parameters: "+snd_strerror(err);snd_pcm_close(pcm);pcm=nullptr;stats.usbState=3;continue;}
      int started=snd_pcm_start(pcm);if(started<0){failure=std::string(name)+" capture start: "+snd_strerror(started);snd_pcm_close(pcm);pcm=nullptr;stats.usbState=3;continue;}stats.usbChannels=mapping.count;stats.usbFirmware=firmware;stats.usbState=mapping.stereo?4:1;
      char raw[32];snprintf(raw,sizeof raw,"hw:%d,0,0",card);snd_rawmidi_open(&midi,nullptr,raw,SND_RAWMIDI_NONBLOCK);
      lastCapture=0;record("capture_start",rate,description.c_str());
      snd_pcm_uframes_t bufferFrames=0,periodFrames=0;snd_pcm_get_params(pcm,&bufferFrames,&periodFrames);
      char geometry[160];snprintf(geometry,sizeof geometry,"card=%d device=%d channels=%d rate=%d buffer_frames=%lu period_frames=%lu",card,deviceNumber,channels,rate,(unsigned long)bufferFrames,(unsigned long)periodFrames);record("capture_parameters",0,geometry);nextHealth=0;resetRequested=true;clock=MidiClock{};clockEpoch++;diagnostic(description+": "+std::to_string(channels)+" channels at "+std::to_string(rate)+" Hz; MIDI "+(midi?"connected":"absent"));fprintf(stderr,"FX USB %s: %d channels at %d Hz, %d strips, MIDI %s\n",description.c_str(),channels,rate,mapping.count,midi?"connected":"absent");break;
     }
     snd_ctl_close(ctl);if(pcm)break;
    }
    if(!pcm){if(!candidate){stats.usbState=0;diagnostic(port.state==4?port.failure():port.state==1?"USB device link present; disconnect computer to enable host":port.state==2?"No USB device link; preparing host mode":"USB host active; waiting for FM1 ALSA card");}else {if(stats.usbState==0)stats.usbState=5;diagnostic(failure);}}
   }
   if(pcm){
    // The retained 4.9 snd-usb-audio advances hwptr for each iso packet before
    // copying that packet into the ring (retire_capture_urb). A reader on another
    // CPU can see the packet "available" before its bytes land and receive the
    // slot's contents from one buffer earlier. Only one packet (<=46 frames) is
    // ever in flight, so never consume the newest CaptureGuardFrames.
    // readi used to restart capture implicitly after snd_pcm_recover().
    if(snd_pcm_state(pcm)==SND_PCM_STATE_PREPARED)snd_pcm_start(pcm);
    snd_pcm_sframes_t ready=snd_pcm_avail(pcm),got;
    if(ready<0)got=ready;
    else if(ready<=CaptureGuardFrames)got=-EAGAIN;
    else got=snd_pcm_readi(pcm,samples.data(),std::min<snd_pcm_sframes_t>(256,ready-CaptureGuardFrames));
    if(got>0){double captured=nowSeconds();if(lastCapture>0){auto gap=uint64_t((captured-lastCapture)*1000000);stats.usbCaptureMaxGapUs=std::max(gap,stats.usbCaptureMaxGapUs.load());}lastCapture=captured;stats.usbFrames+=got;
     for(int i=0;i<got;i++)for(int c=0;c<mapping.count;c++){
      int index=i*channels+(mapping.stereo?0:mapping.offset+c);float l=samples[index]/32768.f,r=mapping.stereo?samples[index+1]/32768.f:l;
      lanes[i*mapping.count*2+c*2]=l;lanes[i*mapping.count*2+c*2+1]=r;
     }if(probe)probe->capture(lanes.data(),got,mapping.count,rate);push(lanes.data(),got,mapping.count,rate);
    }else if(got<0&&got!=-EAGAIN){if(got==-EPIPE||got==-ESTRPIPE)stats.usbCaptureXruns++;record("capture_error",int(got));int recovered=got==-ENODEV?int(got):snd_pcm_recover(pcm,got,1);
     record("capture_recovery",recovered);
     if(recovered<0){close();next=time;}else {resetRequested=true;lastCapture=0;}}
    if(midi){uint8_t bytes[256];auto n=snd_rawmidi_read(midi,bytes,sizeof bytes);for(int i=0;i<n;i++)byte(bytes[i],nowSeconds());}
    clock.expire(nowSeconds());stats.usbClock=clock.locked;stats.usbRunning=clock.running;
   }
   health();std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  close();record("appliance_stop");
#else
  while(alive)std::this_thread::sleep_for(std::chrono::milliseconds(20));
#endif
 }
 void configure(){
  auto t=tail.load(std::memory_order_relaxed),h=head.load(std::memory_order_acquire);
  while(t<h){auto&m=messages[t%Queue];
   if(!m.network&&m.rack.running&&!rack.running)nativeTransportRestart();   // internal Play: downbeat
   if(m.network){
    // A new network waits until the previous crossfade has finished.
    if(m.netEpoch!=graph.epoch&&crossfading)break;
    adoptNetwork(m);networkMode=true;device=m.device;t++;tail.store(t,std::memory_order_release);completed.store(t,std::memory_order_release);continue;
   }
   networkMode=false;
   if(rack.reset!=m.rack.reset||(!rack.running&&m.rack.running)){current.fill(-1);phase.fill(0);for(int k=0;k<12;k++)amalthea_walk_start(&walks[k],m.rack.seed+k);}
   rack=m.rack;device=m.device;dsp=m.dsp;
   for(int k=0;k<12;k++)if(lengths[k]!=rack.slots[k].length||directions[k]!=rack.slots[k].direction){amalthea_walk_start(&walks[k],rack.seed+k);current[k]=-1;phase[k]=0;lengths[k]=rack.slots[k].length;directions[k]=rack.slots[k].direction;}
   t++;tail.store(t,std::memory_order_release);completed.store(t,std::memory_order_release);
  }
 }
 void render(float*out,int frames){
  if(freshStart.exchange(false))skipFade=true;
  configure();if(resetRequested.exchange(false)){read=written.load();consumed.store(uint64_t(read));primed=false;inputGain=0;lastInput.fill(0);drift=0;}int count=std::clamp(inputChannels.load(),1,6);auto available=written.load(std::memory_order_acquire);
  if(!primed&&available-consumed.load()>=PrimeFrames){read=consumed.load();primed=true;}
  auto epoch=clockEpoch.load();if(epoch!=lastEpoch){lastEpoch=epoch;nativeTransportRestart();lastTicks=clockTicks.load();current.fill(-1);phase.fill(0);for(int k=0;k<12;k++)amalthea_walk_start(&walks[k],rack.seed+k);}
  auto ticks=clockTicks.load();uint64_t delta=ticks>=lastTicks?ticks-lastTicks:0;lastTicks=ticks;
  bool external=stats.usbClock.load();bool running=external?stats.usbRunning.load():rack.running;float bpm=external?stats.usbBpm.load():float(rack.tempo);if(bpm<30||bpm>300)bpm=rack.tempo;
  double pulses=external?double(delta):double(frames)*rack.tempo*24/(60*44100);
  double fill=double(available)-read;stats.usbFill=std::max(0,int(fill));
  if(primed)drift+=(std::clamp((fill-TargetFrames)*.000002,-.002,.002)-drift)*.005;
  double ratio=double(inputRate.load())/44100*(1+drift);
  float lanes[6][512]{};std::fill(out,out+frames*2,0);
  for(int i=0;i<frames;i++){
   if(primed&&uint64_t(read)+1<available){auto index=uint64_t(read);float mix=read-index;
    inputGain=std::min(1.f,inputGain+1.f/128);
    for(int c=0;c<count;c++)for(int ch=0;ch<2;ch++){
     int k=c*2+ch;lastInput[k]=ring[index%Ring][k]+mix*(ring[(index+1)%Ring][k]-ring[index%Ring][k]);lanes[c][i*2+ch]=lastInput[k]*inputGain;
    }read+=ratio;
   }else {
    if(primed){primed=false;read=available;stats.usbUnderruns++;}
    inputGain=std::max(0.f,inputGain-1.f/128);
    for(int c=0;c<count;c++)for(int ch=0;ch<2;ch++)lanes[c][i*2+ch]=lastInput[c*2+ch]*inputGain;
   }
  }
  if(networkMode){renderNetwork(lanes,count,out,frames,external?int(bpm):netTempo);}
  else {
  jobAudio=lanes;jobFrames=frames;jobPulses=pulses;jobRunning=running;jobExternal=external;jobBpm=int(bpm);
  int heavy=0,clouds=0,spaces=0;for(auto&s:rack.slots){if(s.effect.type>=FxType::Clouds)heavy++;if(s.effect.type==FxType::Clouds)clouds++;if(s.effect.type==FxType::Space)spaces++;}
  for(int k=0;k<12;k++)if(dsp[k]){dsp[k]->effect.setGrainLimit(std::clamp(128/std::max(1,clouds),4,32));dsp[k]->effect.setSpaceLineLimit(std::clamp(36/std::max(1,spaces),2,12));}
  if(heavy>=2){
   {std::lock_guard<std::mutex>lock(jobMutex);remaining=2;generation++;}jobReady.notify_all();channel(0);channel(1);
   std::unique_lock<std::mutex>lock(jobMutex);jobDone.wait(lock,[&]{return remaining==0;});
  }else for(int c=0;c<6;c++)channel(c);
  for(int c=0;c<6;c++){
   float peak=0,gain=rack.levels[c]*.01f;for(int i=0;i<frames*2;i++){float v=lanes[c][i]*gain;peak=std::max(peak,std::abs(v));if(c<count)out[i]+=v;}stats.fxMeters[c]=c<count?std::max(peak,stats.fxMeters[c].load()*.94f):0;
  }
  }
  consumed.store(uint64_t(read),std::memory_order_release);
  for(int i=0;i<frames;i++){
    float l=out[i*2],r=out[i*2+1];if(!std::isfinite(l))l=0;if(!std::isfinite(r))r=0;
    float peak=std::max(std::abs(l),std::abs(r)),target=peak>.96f?.96f/peak:1;
    limiter=target<limiter?target:std::min(target,limiter+.0002f);
    out[i*2]=l*limiter;out[i*2+1]=r*limiter;
  }
  if(probe)probe->output(out,frames);
 }
};
FxAudio::FxAudio(AudioStats&s):impl(std::make_unique<Impl>(s)){}
FxAudio::~FxAudio()=default;
void FxAudio::activate(bool v){if(v&&!impl->enabled.load()){impl->resetRequested=true;impl->freshStart=true;}impl->enabled=v;}
bool FxAudio::active()const{return impl->enabled;}
bool FxAudio::submit(const FxRack&r,const DeviceSettings&device){auto&p=*impl;auto h=p.head.load(),t=p.tail.load(std::memory_order_acquire);if(h-t>=Queue)return false;
 auto done=p.completed.load(std::memory_order_acquire);auto settled=p.settledEpoch.load(std::memory_order_acquire);p.retired.erase(std::remove_if(p.retired.begin(),p.retired.end(),[&](const auto&v){return done>v.after&&settled>=v.epoch;}),p.retired.end());
 bool reset=r.reset!=p.submittedReset;p.submittedReset=r.reset;
 auto&m=p.messages[h%Queue];for(int k=0;k<12;k++){auto type=r.slots[k].effect.type;if(type==FxType::None){m.dsp[k]=nullptr;continue;}
  if(reset||!p.owners[k]||p.ownerTypes[k]!=type){if(p.owners[k])p.retired.push_back({h,std::move(p.owners[k])});p.owners[k]=std::make_unique<SlotDSP>(type);p.ownerTypes[k]=type;p.owners[k]->apply(r.slots[k].effect,r.slots[k].settings,r.tempo);float silence[64]{};p.owners[k]->effect.processBlock(silence,32);}m.dsp[k]=p.owners[k].get();}
 m.network=false;m.rack=r;m.device=device;p.head.store(h+1,std::memory_order_release);return true;
}
bool FxAudio::submit(const LifebloodNetwork&n,int tempo,int mix,bool bypass,const DeviceSettings&device){
 auto&p=*impl;auto h=p.head.load(),t=p.tail.load(std::memory_order_acquire);if(h-t>=Queue)return false;
 auto done=p.completed.load(std::memory_order_acquire);auto settled=p.settledEpoch.load(std::memory_order_acquire);
 p.retired.erase(std::remove_if(p.retired.begin(),p.retired.end(),[&](const auto&v){return done>v.after&&settled>=v.epoch;}),p.retired.end());
 // Entering the appliance, or a new generation, starts fresh processors.
 bool fresh=!p.netEpochCounter||!p.enabled.load()||n.generation!=p.netGeneration;
 if(fresh){p.netGeneration=n.generation;p.netEpochCounter++;}
 auto&m=p.messages[h%Queue];
 for(int k=0;k<LifebloodMaxNodes;k++){
  auto&owner=p.netOwners[k];bool active=n.node[k].active;auto type=n.node[k].effect.type;
  if(owner&&(fresh||!active||p.netOwnerTypes[k]!=type)){p.retired.push_back({h,std::move(owner),fresh?p.netEpochCounter:0});}
  if(!active){m.netDsp[k]=nullptr;continue;}
  if(!owner){owner=std::make_unique<SlotDSP>(type);p.netOwnerTypes[k]=type;owner->apply(n.node[k].effect,n.node[k].settings,tempo);float silence[64]{};owner->effect.processBlock(silence,32);}
  m.netDsp[k]=owner.get();
 }
 m.network=true;m.net=n;m.netTempo=tempo;m.netMix=mix;m.netBypass=bypass;m.netEpoch=p.netEpochCounter;m.device=device;
 p.head.store(h+1,std::memory_order_release);return true;
}
void FxAudio::render(float*out,int frames){
#if defined(__aarch64__)
 uint64_t previous;asm volatile("mrs %0, fpcr":"=r"(previous));uint64_t realtime=previous|(uint64_t(1)<<24);asm volatile("msr fpcr, %0"::"r"(realtime));
#endif
 for(int at=0;at<frames;at+=256)impl->render(out+at*2,std::min(256,frames-at));
#if defined(__aarch64__)
 asm volatile("msr fpcr, %0"::"r"(previous));
#endif
}
void FxAudio::feed(const float*data,int frames,int channels,int rate){impl->push(data,frames,channels,rate);}
void FxAudio::inputDiscontinuity(){impl->resetRequested=true;}
void FxAudio::midi(uint8_t b,double seconds){impl->byte(b,seconds);}
}
