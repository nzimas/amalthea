#include "persistent_audio_log.hpp"
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>
#include <cerrno>
#include <sys/resource.h>
namespace amalthea {
namespace {
double uptime(){return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::string readText(const std::string&p){std::ifstream f(p);return std::string(std::istreambuf_iterator<char>(f),{});}
void copyText(FILE*f,const std::string&p){auto text=readText(p);if(!text.empty())fprintf(f,"--- %s ---\n%s\n",p.c_str(),text.c_str());}
}
struct PersistentAudioLog::Impl {
 struct Entry{AudioHealthRecord record;char detail[384]{};};
 static constexpr size_t Capacity=4096;
 std::array<Entry,Capacity> queue{};std::atomic<uint64_t> head{0},tail{0},lost{0};std::atomic<bool> alive{true};
 std::thread worker;std::string directory;
 explicit Impl(std::string dir):directory(std::move(dir)),worker() {worker=std::thread([this]{run();});}
 ~Impl(){alive=false;worker.join();}
 void run(){
  // Do not inherit the capture thread's SCHED_FIFO priority.
  sched_param p{};pthread_setschedparam(pthread_self(),SCHED_OTHER,&p);
  std::error_code ec;std::filesystem::create_directories(directory,ec);
  if(ec){fprintf(stderr,"Persistent audio log unavailable: %s\n",ec.message().c_str());return;}
  auto boot=readText("/proc/sys/kernel/random/boot_id");boot.erase(std::remove(boot.begin(),boot.end(),'\n'),boot.end());if(boot.empty())boot="unknown";
  std::string stem=directory+"/diagnostic-"+boot+"-XXXXXX";std::vector<char>name(stem.begin(),stem.end());name.push_back(0);
  int fd=mkstemp(name.data());if(fd<0){perror("Persistent audio log create");return;}::close(fd);stem=name.data();
  int kernel=open("/dev/kmsg",O_RDONLY|O_NONBLOCK);int kernelError=kernel<0?errno:0;unsigned part=0;off_t appOffset=0;
  while(true){
   // Bound this logger's storage to 256 MiB. Other diagnostic evidence is untouched.
   std::vector<std::filesystem::directory_entry> files;uintmax_t total=0;
   for(auto&i:std::filesystem::directory_iterator(directory,ec)){
    if(i.path().filename().string().rfind("diagnostic-",0)!=0||!i.is_regular_file(ec))continue;
    total+=i.file_size(ec);files.push_back(i);
   }
   std::sort(files.begin(),files.end(),[](auto&a,auto&b){return a.path().filename()<b.path().filename();});
   // Sort by modification time rather than random boot UUID.
   std::stable_sort(files.begin(),files.end(),[](auto&a,auto&b){std::error_code x,y;return a.last_write_time(x)<b.last_write_time(y);});
   for(auto&i:files){if(total<240*1024*1024)break;auto n=i.file_size(ec);if(std::filesystem::remove(i.path(),ec))total-=n;}
   std::string base=stem+"-"+std::to_string(part++);
   FILE*events=fopen((base+".csv").c_str(),"w"),*system=fopen((base+".system.log").c_str(),"w"),*messages=fopen((base+".kernel.log").c_str(),"w");
   if(!events||!system||!messages){perror("Persistent audio log open");if(events)fclose(events);if(system)fclose(system);if(messages)fclose(messages);break;}
   fprintf(events,"seconds,event,status,capture_frames,capture_xruns,input_starvation,ring_overflow,output_xruns,max_capture_gap_us,max_fx_render_us,fill_frames,midi_clock,transport_running,detail\n");
   fprintf(system,"boot_id=%s pid=%d wall_epoch=%lld monotonic=%.9f build=%d checkpoint_seconds=1\n",boot.c_str(),getpid(),(long long)time(nullptr),uptime(),AMALTHEA_BUILD);
   fprintf(system,"kernel_log_open_errno=%d\n",kernelError);
   copyText(system,"/proc/version");copyText(system,"/proc/cmdline");
   // Sync directory entries too, so abrupt power loss does not depend on clean exit.
   int dir=open(directory.c_str(),O_RDONLY);if(dir>=0){fsync(dir);::close(dir);}
   double nextSync=0,nextSnapshot=0;uint64_t reportedLoss=0;bool rotate=false;
   while(alive.load()||tail.load()!=head.load()){
    auto t=tail.load(),h=head.load(std::memory_order_acquire);
    while(t<h){auto&e=queue[t%Capacity];auto&r=e.record;
     fprintf(events,"%.9f,%s,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%d,%d,%d,%s\n",r.seconds,r.event,r.status,(unsigned long long)r.frames,(unsigned long long)r.captureXruns,(unsigned long long)r.starvation,(unsigned long long)r.overflow,(unsigned long long)r.outputXruns,(unsigned long long)r.maxGapUs,(unsigned long long)r.maxRenderUs,r.fill,r.clock,r.running,e.detail);
     ++t;tail.store(t,std::memory_order_release);
    }
    uint64_t losses=lost.load();if(losses!=reportedLoss){fprintf(system,"%.9f logger_queue_dropped_total=%llu\n",uptime(),(unsigned long long)losses);reportedLoss=losses;}
    double now=uptime();
    if(now>=nextSnapshot){nextSnapshot=now+5;fprintf(system,"\nSNAPSHOT monotonic=%.9f wall_epoch=%lld dropped=%llu\n",now,(long long)time(nullptr),(unsigned long long)losses);
     for(auto path:{"/proc/uptime","/proc/stat","/proc/loadavg","/proc/meminfo","/proc/asound/cards","/proc/asound/card0/pcm0p/sub0/status","/proc/asound/card0/pcm0p/sub0/hw_params","/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq","/sys/class/thermal/thermal_zone0/temp","/tmp/amalthea-usb-status","/sys/devices/platform/soc/usbc0/otg_role","/sys/class/udc/5100000.udc-controller/state","/sys/devices/platform/soc/usbc0/axp_5v"})copyText(system,path);
     copyText(system,"/proc/"+std::to_string(getpid())+"/status");
     FILE*app=fopen("/run/amalthea.log","r");if(app){fseeko(app,0,SEEK_END);if(ftello(app)<appOffset)appOffset=0;fseeko(app,appOffset,SEEK_SET);char b[4096];size_t n;while((n=fread(b,1,sizeof b,app)))fwrite(b,1,n,system);appOffset=ftello(app);fclose(app);}
     for(int c=1;c<8;c++){auto prefix="/proc/asound/card"+std::to_string(c);copyText(system,prefix+"/stream0");copyText(system,prefix+"/pcm0c/sub0/status");copyText(system,prefix+"/pcm0c/sub0/hw_params");}
    }
    if(kernel>=0){char buf[8192];for(int n=0;n<32;n++){auto got=read(kernel,buf,sizeof buf);if(got>0)fwrite(buf,1,got,messages);else {if(got<0&&errno==EPIPE)continue;break;}}}
    if(now>=nextSync){nextSync=now+1;for(auto*f:{events,system,messages}){if(fflush(f)||fsync(fileno(f)))fprintf(stderr,"Persistent audio checkpoint failed errno=%d\n",errno);}}
    if(ftell(events)>8*1024*1024||ftell(system)>8*1024*1024||ftell(messages)>8*1024*1024){rotate=true;break;}
    if(alive.load())std::this_thread::sleep_for(std::chrono::milliseconds(100));
   }
   fprintf(system,"SEGMENT_END monotonic=%.9f clean=%d\n",uptime(),!alive.load());
   for(auto*f:{events,system,messages}){fflush(f);fsync(fileno(f));fclose(f);}
   if(!rotate)break;
  }
  if(kernel>=0)::close(kernel);
 }
};
PersistentAudioLog::PersistentAudioLog(const std::string&dir):impl(new Impl(dir)){}
PersistentAudioLog::~PersistentAudioLog()=default;
bool PersistentAudioLog::push(const AudioHealthRecord&r,const char*detail){auto&i=*impl;auto h=i.head.load(std::memory_order_relaxed);if(h-i.tail.load(std::memory_order_acquire)>=Impl::Capacity){i.lost++;return false;}auto&e=i.queue[h%Impl::Capacity];e.record=r;snprintf(e.detail,sizeof e.detail,"%s",detail);for(char*p=e.detail;*p;p++)if(*p=='\n'||*p=='\r'||*p==',')*p=' ';i.head.store(h+1,std::memory_order_release);return true;}
uint64_t PersistentAudioLog::dropped()const{return impl->lost.load();}
}
