// Input-only CoreAudio capture of a named device to raw interleaved int16 (exact for 16-bit sources).
#include <CoreAudio/CoreAudio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <atomic>
#include <unistd.h>
struct St{std::vector<int16_t>buf;std::atomic<size_t>n{0};unsigned ch=2;double lastSample=-1;std::atomic<unsigned>jumps{0},calls{0};};
static OSStatus cb(AudioDeviceID,const AudioTimeStamp*,const AudioBufferList*in,const AudioTimeStamp*t,AudioBufferList*,const AudioTimeStamp*,void*c){
 auto&s=*(St*)c;if(!in||!in->mNumberBuffers)return 0;auto&b=in->mBuffers[0];unsigned ch=b.mNumberChannels;unsigned fr=b.mDataByteSize/(ch*4);
 const float*d=(const float*)b.mData;size_t n=s.n.load();
 for(unsigned i=0;i<fr&&n+2<=s.buf.size();i++,n+=2){for(unsigned k=0;k<2;k++){float v=d[i*ch+k]*32768.f;s.buf[n+k]=(int16_t)lrintf(fmaxf(-32768,fminf(32767,v)));}}
 s.n.store(n);
 if(t&&(t->mFlags&kAudioTimeStampSampleTimeValid)){if(s.lastSample>=0&&fabs(t->mSampleTime-s.lastSample)>0.5)s.jumps++;s.lastSample=t->mSampleTime+fr;}
 s.calls++;return 0;}
int main(int argc,char**argv){
 if(argc<4){fprintf(stderr,"usage: name seconds out.raw\n");return 2;}
 AudioObjectPropertyAddress a{kAudioHardwarePropertyDevices,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};UInt32 sz=0;
 AudioObjectGetPropertyDataSize(kAudioObjectSystemObject,&a,0,0,&sz);std::vector<AudioDeviceID>ids(sz/sizeof(AudioDeviceID));AudioObjectGetPropertyData(kAudioObjectSystemObject,&a,0,0,&sz,ids.data());
 AudioDeviceID dev=0;for(auto id:ids){CFStringRef nm=0;UInt32 s2=sizeof nm;AudioObjectPropertyAddress na{kAudioObjectPropertyName,kAudioObjectPropertyScopeGlobal,kAudioObjectPropertyElementMain};if(AudioObjectGetPropertyData(id,&na,0,0,&s2,&nm))continue;char t[256]={0};CFStringGetCString(nm,t,256,kCFStringEncodingUTF8);CFRelease(nm);if(!strcmp(t,argv[1]))dev=id;}
 if(!dev){fprintf(stderr,"device not found\n");return 1;}
 St s;int secs=atoi(argv[2]);s.buf.resize((size_t)secs*48000*2);
 AudioDeviceIOProcID pid;AudioDeviceCreateIOProcID(dev,cb,&s,&pid);AudioDeviceStart(dev,pid);
 for(int i=0;i<secs*10&&s.n.load()<s.buf.size()-4096;i++)usleep(100000);
 AudioDeviceStop(dev,pid);AudioDeviceDestroyIOProcID(dev,pid);
 FILE*f=fopen(argv[3],"wb");fwrite(s.buf.data(),2,s.n.load(),f);fclose(f);
 printf("frames=%zu callbacks=%u timeline_jumps=%u\n",s.n.load()/2,s.calls.load(),s.jumps.load());return 0;}
