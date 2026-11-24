#include "usb_audio_out.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef AMALTHEA_USB_HOST
#include <alsa/asoundlib.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#endif
namespace amalthea {

DriftBridge::DriftBridge(int c,int capacity,int t):lanes(c),size(capacity),goal(t),ring(size_t(c)*capacity){}

int DriftBridge::fill()const{return int(written.load(std::memory_order_acquire)-consumed.load(std::memory_order_acquire));}

void DriftBridge::push(const float*in,int frames){
 auto w=written.load(std::memory_order_relaxed),r=consumed.load(std::memory_order_acquire);
 if(w-r+uint64_t(frames)>uint64_t(size)){droppedFrames.fetch_add(frames,std::memory_order_relaxed);return;}
 for(int i=0;i<frames;i++)std::memcpy(&ring[((w+i)%size)*lanes],in+size_t(i)*lanes,sizeof(float)*lanes);
 written.store(w+frames,std::memory_order_release);
}

void DriftBridge::restart(){
 auto w=written.load(std::memory_order_acquire),r=consumed.load(std::memory_order_relaxed);
 if(w-r>uint64_t(goal))consumed.store(w-goal,std::memory_order_release);
 // Keep the integral: it is the learned clock offset between the two devices.
 phase=0;filtered=0;primed=false;
}

bool DriftBridge::pull(float*out,int frames){
 auto silence=[&]{std::fill(out,out+size_t(frames)*lanes,0.f);return false;};
 auto w=written.load(std::memory_order_acquire);auto r=consumed.load(std::memory_order_relaxed);
 int avail=int(w-r);
 // Far above target only after the consumer stalled; old audio is useless.
 if(avail>goal*3+frames){r=w-goal;consumed.store(r,std::memory_order_release);avail=goal;phase=0;filtered=0;}
 if(!primed){if(avail<goal)return silence();primed=true;filtered=avail-goal;}
 // PI control of the fill level, critically damped at 0.05 rad/s. Block timing
 // makes the measured fill beat by up to one block; the slow loop and 2 s
 // smoothing keep the resulting pitch movement below half a cent.
 constexpr double Rate=44100,Kp=2.27e-6,Ki=5.67e-8,Limit=0.01;
 double dt=frames/Rate;
 filtered+=std::min(1.,dt/2.)*((avail-goal)-filtered);
 integral=std::clamp(integral+filtered*dt,-Limit/Ki,Limit/Ki);
 currentRatio=std::clamp(1+Kp*filtered+Ki*integral,1-Limit,1+Limit);
 // Cubic Hermite between frames idx+1 and idx+2 needs idx..idx+3.
 if(phase+currentRatio*frames+4>avail){underrunCount++;primed=false;return silence();}
 for(int i=0;i<frames;i++){
  double p=phase+currentRatio*i;uint64_t idx=uint64_t(p);float t=float(p-double(idx));uint64_t base=r+idx;
  for(int c=0;c<lanes;c++){
   float y0=at(base,c),y1=at(base+1,c),y2=at(base+2,c),y3=at(base+3,c);
   float a=-.5f*y0+1.5f*y1-1.5f*y2+.5f*y3,b=y0-2.5f*y1+2*y2-.5f*y3,cc=-.5f*y0+.5f*y2;
   out[size_t(i)*lanes+c]=((a*t+b)*t+cc)*t+y1;
  }
 }
 double advanced=phase+currentRatio*frames;uint64_t whole=uint64_t(advanced);phase=advanced-double(whole);
 consumed.store(r+whole,std::memory_order_release);
 return true;
}

uint8_t typeCModeValue(uint8_t current,TypeCMode mode){return uint8_t((current&~3u)|unsigned(mode));}

#ifdef AMALTHEA_USB_HOST
namespace {
constexpr auto Gadget="/sys/kernel/config/usb_gadget/g1";
constexpr auto Udc="5100000.udc-controller";
constexpr auto Card="UAC2Gadget";
std::string readLine(const std::string&path){std::ifstream f(path);std::string s;std::getline(f,s);return s;}
bool writeText(const std::string&path,const std::string&value){FILE*f=fopen(path.c_str(),"w");if(!f)return false;bool ok=fputs(value.c_str(),f)>=0;if(fclose(f))ok=false;return ok;}
double seconds(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
bool moduleLoaded(){std::ifstream f("/proc/modules");std::string line;while(std::getline(f,line))if(line.rfind("usb_f_uac2 ",0)==0)return true;return false;}

// Adds the UAC2 audio function next to ADB. The BSP adbd rebinds the UDC
// itself after any unbind, so stage the link while bound, then unbind once.
// Returns true when finished (or nothing to do on this system).
bool provisionGadget(std::string&message){
 namespace fs=std::filesystem;std::error_code e;
 std::string g=Gadget,f=g+"/functions/uac2.usb0",l=g+"/configs/c.1/uac2.usb0";
 if(!fs::exists(g+"/UDC",e)){message="No configfs gadget; USB audio output unavailable";return true;}
 auto matches=[&]{return readLine(f+"/p_chmask")=="3"&&readLine(f+"/p_srate")==std::to_string(UsbAudioOut::Rate)&&readLine(f+"/p_ssize")=="2"&&readLine(f+"/c_chmask")=="0";};
 if(fs::is_symlink(l,e)&&matches())return true;
 if(readLine("/sys/devices/platform/soc/usbc0/otg_role")!="usb_device"){message="USB port in host mode; gadget audio deferred";return false;}
 // A function left with other settings (an earlier trial) must be unlinked
 // before its attributes can change; configfs unbinds the gadget for that.
 if(fs::is_symlink(l,e)&&unlink(l.c_str())!=0){message=std::string("USB audio function unlink failed: ")+strerror(errno);return true;}
 if(!fs::exists(f,e)){
  if(!moduleLoaded()){
   auto module=fs::read_symlink("/proc/self/exe",e).parent_path()/"usb_f_uac2.ko";
   int fd=open(module.c_str(),O_RDONLY|O_CLOEXEC);
   if(fd<0){message="USB audio module missing: "+module.string();return true;}
   long loaded=syscall(SYS_finit_module,fd,"",0);int error=errno;close(fd);
   if(loaded!=0&&error!=EEXIST){message=std::string("USB audio module load failed: ")+strerror(error);return true;}
  }
  if(mkdir(f.c_str(),0755)!=0&&errno!=EEXIST){message=std::string("USB audio function create failed: ")+strerror(errno);return true;}
 }
 bool ok=writeText(f+"/p_chmask","3")&&writeText(f+"/p_srate",std::to_string(UsbAudioOut::Rate))&&writeText(f+"/p_ssize","2")&&writeText(f+"/c_chmask","0");
 if(!ok||!matches()){message="USB audio function configuration failed";return true;}
 if(symlink(f.c_str(),l.c_str())!=0){message=std::string("USB audio function link failed: ")+strerror(errno);return true;}
 // Composite device with an interface association.
 writeText(g+"/bDeviceClass","0xEF");writeText(g+"/bDeviceSubClass","0x02");writeText(g+"/bDeviceProtocol","0x01");
 writeText(g+"/UDC","\n");
 for(int i=0;i<20&&readLine(g+"/UDC").empty();i++)usleep(100000);
 if(readLine(g+"/UDC").empty())writeText(g+"/UDC",Udc);
 message="USB audio function added; UDC "+readLine(g+"/UDC");
 return true;
}
}

struct UsbAudioOut::Impl {
 DriftBridge bridge{Channels,16384,1536};
 std::atomic<bool> alive{true};std::atomic<int> current{int(UsbAudioState::Unavailable)};
 std::thread worker;
 Impl(){worker=std::thread([this]{run();});}
 ~Impl(){alive=false;if(worker.joinable())worker.join();}
 void set(UsbAudioState s){current.store(int(s),std::memory_order_relaxed);}
 void run(){
  sched_param param{};param.sched_priority=11;pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);
  constexpr int Period=256;
  std::vector<float> frames(Period*Channels);std::vector<int16_t> samples(Period*Channels);
  snd_pcm_t*pcm=nullptr;bool provisioned=false,idle=true;double nextScan=0,lastProgress=0,nextAlive=0;std::string message,lastMessage;
  snd_pcm_status_t*status;snd_pcm_status_alloca(&status);
  auto closePcm=[&]{if(pcm)snd_pcm_close(pcm);pcm=nullptr;};
  while(alive){
   double now=seconds();
   if(!pcm){
    if(now<nextScan){usleep(50000);continue;}
    nextScan=now+.5;
    if(!provisioned){provisioned=provisionGadget(message);if(message!=lastMessage){fprintf(stderr,"%s\n",message.c_str());lastMessage=message;}}
    if(snd_card_get_index(Card)<0){set(UsbAudioState::Unavailable);continue;}
    if(snd_pcm_open(&pcm,"hw:CARD=UAC2Gadget,DEV=0",SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK)<0){pcm=nullptr;set(UsbAudioState::Error);continue;}
    snd_pcm_hw_params_t*hw;snd_pcm_hw_params_alloca(&hw);snd_pcm_hw_params_any(pcm,hw);
    snd_pcm_uframes_t period=Period,buffer=Period*4;unsigned rate=Rate;
    bool ok=snd_pcm_hw_params_set_access(pcm,hw,SND_PCM_ACCESS_RW_INTERLEAVED)>=0&&snd_pcm_hw_params_set_format(pcm,hw,SND_PCM_FORMAT_S16_LE)>=0&&
     snd_pcm_hw_params_set_channels(pcm,hw,Channels)>=0&&snd_pcm_hw_params_set_rate(pcm,hw,rate,0)>=0&&
     snd_pcm_hw_params_set_period_size_near(pcm,hw,&period,nullptr)>=0&&snd_pcm_hw_params_set_buffer_size_near(pcm,hw,&buffer)>=0&&snd_pcm_hw_params(pcm,hw)>=0;
    snd_pcm_sw_params_t*sw;snd_pcm_sw_params_alloca(&sw);
    ok=ok&&snd_pcm_sw_params_current(pcm,sw)>=0&&snd_pcm_sw_params_set_start_threshold(pcm,sw,Period)>=0&&snd_pcm_sw_params_set_avail_min(pcm,sw,Period)>=0&&snd_pcm_sw_params(pcm,sw)>=0;
    if(!ok){fprintf(stderr,"USB audio output: cannot configure %s\n",Card);closePcm();set(UsbAudioState::Error);continue;}
    fprintf(stderr,"USB audio output open: %d ch %u Hz period %lu buffer %lu\n",Channels,rate,(unsigned long)period,(unsigned long)buffer);
    bridge.restart();idle=true;lastProgress=now;set(UsbAudioState::Waiting);
   }
   snd_pcm_wait(pcm,100);
   // Every unplug rebuilds the gadget and replaces the card. A disconnected PCM
   // keeps a frozen mmap status, so ask the kernel (an ioctl) whether it is alive.
   if(seconds()>=nextAlive){nextAlive=seconds()+.25;
    if(snd_pcm_status(pcm,status)<0||snd_pcm_state(pcm)==SND_PCM_STATE_DISCONNECTED){fprintf(stderr,"USB audio output closed: card disconnected\n");closePcm();set(UsbAudioState::Waiting);continue;}}
   auto avail=snd_pcm_avail_update(pcm);
   if(avail<0){
    if(avail==-EPIPE||avail==-ESTRPIPE){snd_pcm_prepare(pcm);bridge.restart();continue;}
    // Card disconnected (cable unplugged or gadget rebound): reopen later.
    fprintf(stderr,"USB audio output closed: %s\n",snd_strerror(int(avail)));closePcm();set(UsbAudioState::Waiting);continue;
   }
   now=seconds();
   if(avail<Period){if(!idle&&now-lastProgress>.25){idle=true;set(UsbAudioState::Idle);}continue;}
   // The host started (or resumed) reading: begin from fresh audio.
   if(idle){bridge.restart();idle=false;}
   lastProgress=now;set(UsbAudioState::Streaming);
   while(avail>=Period){
    bridge.pull(frames.data(),Period);
    for(int i=0;i<Period*Channels;i++)samples[i]=int16_t(std::lrint(std::clamp(frames[i],-1.f,1.f)*32767));
    auto n=snd_pcm_writei(pcm,samples.data(),Period);
    if(n==-EAGAIN)break;
    if(n<0){if(snd_pcm_recover(pcm,int(n),1)<0){closePcm();set(UsbAudioState::Waiting);}break;}
    avail-=n;
   }
  }
  closePcm();
 }
};
UsbAudioOut::UsbAudioOut():impl(std::make_unique<Impl>()){}
UsbAudioOut::~UsbAudioOut()=default;
void UsbAudioOut::push(const float*in,int frames){impl->bridge.push(in,frames);}
UsbAudioState UsbAudioOut::state()const{return UsbAudioState(impl->current.load(std::memory_order_relaxed));}
#else
struct UsbAudioOut::Impl{};
UsbAudioOut::UsbAudioOut(){}
UsbAudioOut::~UsbAudioOut()=default;
void UsbAudioOut::push(const float*,int){}
UsbAudioState UsbAudioOut::state()const{return UsbAudioState::Unavailable;}
#endif
}
