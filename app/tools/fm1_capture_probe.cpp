// Standalone eight-second USB ingress comparison. No appliance DSP or playback.
// Auditions C4/E4/G4 on MIDI channel 4; no patch/settings/transport messages.
#include <alsa/asoundlib.h>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static volatile sig_atomic_t interrupted=0;
static void stop(int){interrupted=1;}
static double seconds(){using namespace std::chrono;return duration<double>(steady_clock::now().time_since_epoch()).count();}
static bool wav(const char*path,const std::vector<int16_t>&data,int channels){
 FILE*f=fopen(path,"wb");if(!f)return false;
 auto number=[&](uint32_t v,int n){while(n--){fputc(v&255,f);v>>=8;}};
 uint32_t size=data.size()*2;fwrite("RIFF",1,4,f);number(size+36,4);fwrite("WAVEfmt ",1,8,f);
 number(16,4);number(1,2);number(channels,2);number(44100,4);number(44100*channels*2,4);number(channels*2,2);number(16,2);fwrite("data",1,4,f);number(size,4);
 // Explicit little endian, independent of host byte order.
 for(auto sample:data)number(uint16_t(sample),2);
 bool ok=!ferror(f);return fclose(f)==0&&ok;
}
int main(int argc,char**argv){
 if(argc!=2){fprintf(stderr,"Usage: fm1_capture_probe output.wav (Bigorna, stopped sequence, unchanged patch)\n");return 2;}
 signal(SIGTERM,stop);signal(SIGINT,stop);
 int card=-1,chosen=-1;char name[32];snd_ctl_card_info_t*info;snd_ctl_card_info_alloca(&info);
 while(snd_card_next(&card)>=0&&card>=0){
  snprintf(name,sizeof name,"hw:%d",card);snd_ctl_t*ctl=nullptr;
  if(snd_ctl_open(&ctl,name,SND_CTL_NONBLOCK)<0)continue;
  if(snd_ctl_card_info(ctl,info)>=0&&strstr(snd_ctl_card_info_get_name(info),"Bigorna"))chosen=card;
  snd_ctl_close(ctl);if(chosen>=0)break;
 }
 if(chosen<0){fprintf(stderr,"Bigorna absent; nothing changed\n");return 3;}
 snd_pcm_t*pcm=nullptr;snd_rawmidi_t*midi=nullptr;
 snprintf(name,sizeof name,"hw:%d,0",chosen);
 int err=snd_pcm_open(&pcm,name,SND_PCM_STREAM_CAPTURE,SND_PCM_NONBLOCK);
 if(err<0){fprintf(stderr,"Capture open: %s\n",snd_strerror(err));return 4;}
 snd_pcm_hw_params_t*params;snd_pcm_hw_params_alloca(&params);unsigned channels=0;
 if(snd_pcm_hw_params_any(pcm,params)<0||snd_pcm_hw_params_get_channels_max(params,&channels)<0||channels!=6){fprintf(stderr,"Expected six-channel Bigorna; refusing test\n");snd_pcm_close(pcm);return 5;}
 err=snd_pcm_set_params(pcm,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,channels,44100,0,64000);
 if(err<0){fprintf(stderr,"Capture setup: %s\n",snd_strerror(err));snd_pcm_close(pcm);return 6;}
 snprintf(name,sizeof name,"hw:%d,0,0",chosen);err=snd_rawmidi_open(nullptr,&midi,name,0);
 if(err<0){fprintf(stderr,"MIDI output: %s\n",snd_strerror(err));snd_pcm_close(pcm);return 7;}
 const int notes[]={60,64,67};bool held[3]{};int event=0,xruns=0;bool failed=false;
 std::vector<int16_t>data(44100*8*channels);size_t frames=0;
 if((err=snd_pcm_start(pcm))<0){fprintf(stderr,"Capture start: %s\n",snd_strerror(err));failed=true;}
 double begin=seconds();
 while(!failed&&!interrupted&&seconds()-begin<8){
  double elapsed=seconds()-begin;
  while(event<6&&elapsed>=1.+(event/2)*1.8+(event%2)*1.5){
   int n=event/2;bool on=event%2==0;unsigned char bytes[]={static_cast<unsigned char>(on?0x93:0x83),static_cast<unsigned char>(notes[n]),static_cast<unsigned char>(on?85:0)};
   if(on)held[n]=true;
   auto sent=snd_rawmidi_write(midi,bytes,3);if(sent!=3){fprintf(stderr,"MIDI write failed\n");failed=true;break;}
   if(!on)held[n]=false;
   fprintf(stderr,"%.6f note %d %s\n",elapsed,notes[n],on?"on":"off");event++;
  }
  if(failed)break;
  size_t remaining=data.size()/channels-frames;
  if(!remaining)break;
  auto got=snd_pcm_readi(pcm,data.data()+frames*channels,std::min<size_t>(256,remaining));
  if(got>0)frames+=got;
  else if(got<0&&got!=-EAGAIN){fprintf(stderr,"Capture error: %s\n",snd_strerror(got));xruns++;failed=true;}
  else std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 for(int n=0;n<3;n++)if(held[n]){unsigned char off[]={0x83,static_cast<unsigned char>(notes[n]),0};snd_rawmidi_write(midi,off,3);}
 snd_rawmidi_drain(midi);snd_rawmidi_close(midi);snd_pcm_close(pcm);
 data.resize(frames*channels);bool saved=frames&&wav(argv[1],data,channels);
 fprintf(stderr,"Native 44100 Hz / %u channels / %zu frames; capture errors %d; interrupted %d; WAV saved %d\n",channels,frames,xruns,int(interrupted),saved);
 return failed||interrupted||!saved?8:0;
}
