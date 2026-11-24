#include "audio_probe.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
namespace amalthea {
namespace {
constexpr int Seconds=5,MaxFrames=48000*Seconds;
bool wav(const std::string&path,const float*data,size_t frames,int channels,int rate){
 FILE*f=fopen((path+".next").c_str(),"wb");if(!f)return false;
 auto text=[&](const char*s){fwrite(s,1,4,f);};auto number=[&](uint32_t v,int bytes){while(bytes--){fputc(v&255,f);v>>=8;}};
 uint32_t bytes=frames*channels*2;text("RIFF");number(36+bytes,4);text("WAVE");text("fmt ");number(16,4);number(1,2);number(channels,2);number(rate,4);number(rate*channels*2,4);number(channels*2,2);number(16,2);text("data");number(bytes,4);
 std::array<unsigned char,8192>buffer{};size_t samples=frames*channels;
 for(size_t start=0;start<samples;start+=4096){auto count=std::min<size_t>(4096,samples-start);
  for(size_t j=0;j<count;j++){float value=std::isfinite(data[start+j])?data[start+j]:0;auto v=uint16_t(int16_t(std::clamp(value,-1.f,1.f)*32767));buffer[j*2]=v&255;buffer[j*2+1]=v>>8;}
  if(fwrite(buffer.data(),2,count,f)!=count){fclose(f);return false;}
 }
 bool ok=!ferror(f);if(fclose(f))ok=false;if(ok)ok=rename((path+".next").c_str(),path.c_str())==0;return ok;
}
}
AudioProbe::AudioProbe(std::string destination):root(std::move(destination)),raw(MaxFrames*6),processed(44100*Seconds*2){
 writer=std::thread([this]{while(alive.load()){if(inputReady.load(std::memory_order_acquire)&&outputReady.load(std::memory_order_acquire)){save();return;}std::this_thread::sleep_for(std::chrono::milliseconds(50));}
  if(inputReady.load(std::memory_order_acquire)&&outputReady.load(std::memory_order_acquire))save();});
}
AudioProbe::~AudioProbe(){alive=false;if(writer.joinable())writer.join();}
void AudioProbe::capture(const float*data,int frames,int lanes,int sampleRate){
 if(inputReady.load(std::memory_order_relaxed)||lanes<1||lanes>6||sampleRate<8000||sampleRate>48000)return;
 if(!started.load(std::memory_order_relaxed)){
  skipped+=frames;if(skipped<size_t(sampleRate))return;
  bool signal=false;for(int i=0;i<frames*lanes*2;i++)if(std::abs(data[i])>.0001f){signal=true;break;}if(!signal)return;
  channels=lanes==1?2:lanes;rate=sampleRate;started.store(true,std::memory_order_release);
 }
 if(sampleRate!=rate||channels!=(lanes==1?2:lanes))return;
 int count=std::min<size_t>(frames,rate*Seconds-inputCount);
 for(int i=0;i<count;i++)for(int c=0;c<channels;c++)raw[(inputCount+i)*channels+c]=data[i*lanes*2+(lanes==1?c:c*2)];
 inputCount+=count;if(inputCount==size_t(rate*Seconds))inputReady.store(true,std::memory_order_release);
}
void AudioProbe::output(const float*data,int frames){
 if(!started.load(std::memory_order_acquire)||outputReady.load(std::memory_order_relaxed))return;
 int count=std::min<size_t>(frames,44100*Seconds-outputCount);std::copy(data,data+count*2,processed.begin()+outputCount*2);outputCount+=count;
 if(outputCount==44100*Seconds)outputReady.store(true,std::memory_order_release);
}
void AudioProbe::save(){
 std::error_code ec;std::filesystem::create_directories(root,ec);if(ec)return;
 bool a=wav(root+"/raw-usb.wav",raw.data(),inputCount,channels,rate),b=wav(root+"/codec-input.wav",processed.data(),outputCount,2,44100);
 FILE*f=fopen((root+"/capture.txt").c_str(),"w");if(f){fprintf(f,"Five-second instrument-audio diagnostic; no session data.\nRaw: %d channels at %d Hz; %zu frames; written %d\nCodec input: stereo 44100 Hz; %zu frames; written %d\nInput/output start together in wall time; align for ingress/output buffering when comparing.\n",channels,rate,inputCount,a,outputCount,b);fclose(f);}
}
}
