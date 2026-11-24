// Native, bounded Bigorna monitoring diagnostic; captures only the named USB input.
// Routes USB channel 4 to built-in speakers at 20% gain. Never opens a microphone.
#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
constexpr size_t Ring=65536,TraceCapacity=32768;
struct Trace {double sample=0,scalar=0;uint64_t host=0,callback=0;unsigned frames=0,flags=0;int fill=0;double correction=0;};
struct State {
 AudioStreamBasicDescription in{},out{};bool adaptive=true;
 std::vector<float>raw,monitor;std::vector<Trace>inputTrace{TraceCapacity},outputTrace{TraceCapacity};
 std::array<float,Ring>ring{};std::atomic<uint64_t>written{0},consumed{0};
 std::atomic<unsigned>inputOverloads{0},outputOverloads{0};
 size_t rawFrames=0,monitorFrames=0,inputCount=0,outputCount=0;
 uint64_t overflow=0,starvation=0;double read=0,filteredFill=4096,integral=0,correction=0;bool primed=false;float gain=0,last=0;
};
static double hostSeconds(uint64_t ticks){static mach_timebase_info_data_t scale=[](){mach_timebase_info_data_t s;mach_timebase_info(&s);return s;}();return double(ticks)*scale.numer/scale.denom/1e9;}
static void trace(std::vector<Trace>&log,size_t&count,const AudioTimeStamp*t,unsigned frames,int fill=0,double correction=0){if(count>=log.size())return;auto&v=log[count++];v.callback=mach_absolute_time();v.frames=frames;v.fill=fill;v.correction=correction;if(t){v.sample=t->mSampleTime;v.scalar=t->mRateScalar;v.host=t->mHostTime;v.flags=t->mFlags;}}
static OSStatus inputCallback(AudioDeviceID,const AudioTimeStamp*,const AudioBufferList*in,const AudioTimeStamp*t,AudioBufferList*,const AudioTimeStamp*,void*context){
 auto&s=*static_cast<State*>(context);if(!in||!in->mNumberBuffers)return 0;
 auto&first=in->mBuffers[0];unsigned frames=first.mDataByteSize/(std::max(1u,first.mNumberChannels)*4);
 size_t keep=std::min<size_t>(frames,s.raw.size()/6-s.rawFrames);auto w=s.written.load(std::memory_order_relaxed),r=s.consumed.load(std::memory_order_acquire);
 for(unsigned i=0;i<frames;i++){
  int channel=0;float selected=0;
  for(unsigned b=0;b<in->mNumberBuffers;b++){auto&buffer=in->mBuffers[b];auto*data=static_cast<const float*>(buffer.mData);
   for(unsigned c=0;c<buffer.mNumberChannels;c++,channel++){float value=data?data[i*buffer.mNumberChannels+c]:0;if(channel==3)selected=value;if(i<keep&&channel<6)s.raw[(s.rawFrames+i)*6+channel]=value;}
  }
  if(w-r<Ring-4)s.ring[w++%Ring]=selected;else s.overflow++;
 }
 s.rawFrames+=keep;s.written.store(w,std::memory_order_release);trace(s.inputTrace,s.inputCount,t,frames);return 0;
}
static OSStatus outputCallback(AudioDeviceID,const AudioTimeStamp*,const AudioBufferList*,const AudioTimeStamp*,AudioBufferList*out,const AudioTimeStamp*t,void*context){
 auto&s=*static_cast<State*>(context);if(!out||!out->mNumberBuffers)return 0;
 for(unsigned b=0;b<out->mNumberBuffers;b++)if(out->mBuffers[b].mData)memset(out->mBuffers[b].mData,0,out->mBuffers[b].mDataByteSize);
 auto&first=out->mBuffers[0];unsigned frames=first.mDataByteSize/(std::max(1u,first.mNumberChannels)*4);
 auto available=s.written.load(std::memory_order_acquire);
 if(!s.primed&&double(available)-s.read>=4096){s.primed=true;s.read=double(available)-4096;}
 double fill=double(available)-s.read,dt=frames/s.out.mSampleRate;
 if(s.primed&&s.adaptive){s.filteredFill+=(fill-s.filteredFill)*(1-std::exp(-dt));double error=s.filteredFill-4096;s.integral=std::clamp(s.integral+error*dt*1e-7,-.004,.004);s.correction=std::clamp(error*2e-6+s.integral,-.005,.005);}
 double ratio=s.in.mSampleRate/s.out.mSampleRate*(1+s.correction);
 size_t keep=std::min<size_t>(frames,s.monitor.size()/2-s.monitorFrames);
 for(unsigned i=0;i<frames;i++){
  if(s.primed&&uint64_t(s.read)+2<available){auto index=uint64_t(s.read);float f=s.read-index;
   float a=s.ring[(index?index-1:0)%Ring],b=s.ring[index%Ring],c=s.ring[(index+1)%Ring],d=s.ring[(index+2)%Ring];
   s.last=b+.5f*f*(c-a+f*(2*a-5*b+4*c-d+f*(3*(b-c)+d-a)));s.read+=ratio;s.gain=std::min(1.f,s.gain+1.f/128);
  }else{if(s.primed){s.primed=false;s.starvation++;s.read=available;}s.gain=std::max(0.f,s.gain-1.f/128);}
  float value=std::clamp(s.last*s.gain*.2f,-.95f,.95f);
  for(unsigned b=0;b<out->mNumberBuffers;b++){auto&buffer=out->mBuffers[b];auto*data=static_cast<float*>(buffer.mData);if(data)for(unsigned c=0;c<buffer.mNumberChannels;c++)data[i*buffer.mNumberChannels+c]=value;}
  if(i<keep)s.monitor[(s.monitorFrames+i)*2]=s.monitor[(s.monitorFrames+i)*2+1]=value;
 }
 s.monitorFrames+=keep;s.consumed.store(uint64_t(s.read),std::memory_order_release);trace(s.outputTrace,s.outputCount,t,frames,int(fill),s.correction);return 0;
}
static OSStatus overload(AudioObjectID,UInt32,const AudioObjectPropertyAddress[],void*context){static_cast<std::atomic<unsigned>*>(context)->fetch_add(1);return 0;}
template<class T>static bool property(AudioObjectID device,AudioObjectPropertySelector selector,AudioObjectPropertyScope scope,T&value){AudioObjectPropertyAddress p{selector,scope,kAudioObjectPropertyElementMain};UInt32 size=sizeof value;return AudioObjectGetPropertyData(device,&p,0,nullptr,&size,&value)==0;}
static AudioDeviceID namedDevice(const char*wanted){AudioObjectPropertyAddress p{kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};UInt32 size=0;AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&p,0,nullptr,&size);std::vector<AudioDeviceID>devices(size/sizeof(AudioDeviceID));AudioObjectGetPropertyData(kAudioObjectSystemObject,&p,0,nullptr,&size,devices.data());
 for(auto device:devices){CFStringRef name=nullptr;if(!property(device,kAudioObjectPropertyName,kAudioObjectPropertyScopeGlobal,name))continue;char text[256]{};if(name){CFStringGetCString(name,text,sizeof text,kCFStringEncodingUTF8);CFRelease(name);}if(!strcmp(text,wanted))return device;}return 0;
}
static bool wav(const std::string&path,const std::vector<float>&data,size_t frames,unsigned channels,unsigned rate){FILE*f=fopen(path.c_str(),"wb");if(!f)return false;auto num=[&](uint32_t value,int bytes){while(bytes--){fputc(value&255,f);value>>=8;}};uint32_t bytes=frames*channels*2;fwrite("RIFF",1,4,f);num(bytes+36,4);fwrite("WAVEfmt ",1,8,f);num(16,4);num(1,2);num(channels,2);num(rate,4);num(rate*channels*2,4);num(channels*2,2);num(16,2);fwrite("data",1,4,f);num(bytes,4);for(size_t i=0;i<frames*channels;i++)num(uint16_t(int16_t(std::clamp(data[i],-1.f,1.f)*32767)),2);bool ok=!ferror(f);return fclose(f)==0&&ok;}
static void saveTrace(const std::string&path,const std::vector<Trace>&trace,size_t count){FILE*f=fopen(path.c_str(),"w");if(!f)return;fputs("callback,frames,sample_time,host_seconds,callback_seconds,rate_scalar,flags,fill,correction\n",f);for(size_t i=0;i<count;i++){auto&t=trace[i];fprintf(f,"%zu,%u,%.9f,%.9f,%.9f,%.9f,%u,%d,%.9f\n",i,t.frames,t.sample,hostSeconds(t.host),hostSeconds(t.callback),t.scalar,t.flags,t.fill,t.correction);}fclose(f);}
int main(int argc,char**argv){
 if(argc!=4){fprintf(stderr,"Usage: mac_usb_monitor output-prefix seconds fixed|adaptive\n");return 2;}
 int duration=atoi(argv[2]);if(duration<5||duration>60||(strcmp(argv[3],"fixed")&&strcmp(argv[3],"adaptive")))return 2;
 auto input=namedDevice("FM-1 Bigorna"),output=namedDevice("MacBook Air Speakers");if(!input||!output){fprintf(stderr,"Expected Bigorna input or built-in speakers absent; nothing opened\n");return 3;}
 State s;s.adaptive=!strcmp(argv[3],"adaptive");
 if(!property(input,kAudioDevicePropertyStreamFormat,kAudioDevicePropertyScopeInput,s.in)||!property(output,kAudioDevicePropertyStreamFormat,kAudioDevicePropertyScopeOutput,s.out)||s.in.mChannelsPerFrame!=6||s.in.mBitsPerChannel!=32||s.out.mBitsPerChannel!=32||!(s.in.mFormatFlags&kAudioFormatFlagIsFloat)||!(s.out.mFormatFlags&kAudioFormatFlagIsFloat)){fprintf(stderr,"Unsupported native format; nothing opened\n");return 4;}
 double actualIn=0,actualOut=0;property(input,kAudioDevicePropertyActualSampleRate,kAudioObjectPropertyScopeGlobal,actualIn);property(output,kAudioDevicePropertyActualSampleRate,kAudioObjectPropertyScopeGlobal,actualOut);
 printf("Bigorna USB: %.6f Hz nominal / %.6f actual; %u channels, flags %u\nSpeakers: %.6f Hz nominal / %.6f actual; %u channels, flags %u\nTrack 4 only, 20%% software gain, %s drift correction; no device settings changed\n",s.in.mSampleRate,actualIn,s.in.mChannelsPerFrame,s.in.mFormatFlags,s.out.mSampleRate,actualOut,s.out.mChannelsPerFrame,s.out.mFormatFlags,argv[3]);fflush(stdout);
 s.raw.resize(size_t((duration+2)*s.in.mSampleRate)*6);s.monitor.resize(size_t((duration+2)*s.out.mSampleRate)*2);
 AudioDeviceIOProcID inProc=nullptr,outProc=nullptr;AudioObjectPropertyAddress p{kAudioDeviceProcessorOverload,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};
 auto err=AudioDeviceCreateIOProcID(input,inputCallback,&s,&inProc);if(err){fprintf(stderr,"Input create %d\n",err);return 5;}
 err=AudioDeviceCreateIOProcID(output,outputCallback,&s,&outProc);if(err){AudioDeviceDestroyIOProcID(input,inProc);fprintf(stderr,"Output create %d\n",err);return 6;}
 AudioObjectAddPropertyListener(input,&p,overload,&s.inputOverloads);AudioObjectAddPropertyListener(output,&p,overload,&s.outputOverloads);
 bool inputStarted=false,outputStarted=false;
 err=AudioDeviceStart(input,inProc);if(!err){inputStarted=true;err=AudioDeviceStart(output,outProc);if(!err)outputStarted=true;}
 if(err)fprintf(stderr,"Device start %d\n",err);else{printf("MONITOR_READY\n");fflush(stdout);std::this_thread::sleep_for(std::chrono::seconds(duration));}
 if(outputStarted)AudioDeviceStop(output,outProc);if(inputStarted)AudioDeviceStop(input,inProc);
 AudioDeviceDestroyIOProcID(input,inProc);AudioDeviceDestroyIOProcID(output,outProc);AudioObjectRemovePropertyListener(input,&p,overload,&s.inputOverloads);AudioObjectRemovePropertyListener(output,&p,overload,&s.outputOverloads);
 if(err)return 7;
 std::string prefix=argv[1];bool a=wav(prefix+"-input.wav",s.raw,s.rawFrames,6,unsigned(s.in.mSampleRate)),b=wav(prefix+"-monitor.wav",s.monitor,s.monitorFrames,2,unsigned(s.out.mSampleRate));saveTrace(prefix+"-input.csv",s.inputTrace,s.inputCount);saveTrace(prefix+"-output.csv",s.outputTrace,s.outputCount);
 printf("Input frames %zu; output frames %zu; ring overflow %llu; starvation %llu; input overloads %u; output overloads %u; final fill %.3f; correction %.3f ppm; WAVs saved %d %d\n",s.rawFrames,s.monitorFrames,(unsigned long long)s.overflow,(unsigned long long)s.starvation,s.inputOverloads.load(),s.outputOverloads.load(),double(s.written.load())-s.read,s.correction*1e6,a,b);
 return a&&b?0:8;
}
