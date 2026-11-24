/* Writes a frame counter (L = low 16 bits, R = high 16 bits) to the UAC2 gadget. */
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static double now(){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(int argc,char**argv){
  const char*dev=argc>1?argv[1]:"hw:CARD=UAC2Gadget,DEV=0"; int seconds=argc>2?atoi(argv[2]):20;
  unsigned period=argc>3?atoi(argv[3]):960;
  snd_pcm_t*p;int e=snd_pcm_open(&p,dev,SND_PCM_STREAM_PLAYBACK,0);if(e<0){fprintf(stderr,"open: %s\n",snd_strerror(e));return 1;}
  snd_pcm_hw_params_t*h;snd_pcm_hw_params_alloca(&h);snd_pcm_hw_params_any(p,h);
  snd_pcm_hw_params_set_access(p,h,SND_PCM_ACCESS_RW_INTERLEAVED);snd_pcm_hw_params_set_format(p,h,SND_PCM_FORMAT_S16_LE);
  snd_pcm_hw_params_set_channels(p,h,2);unsigned rate=48000;snd_pcm_hw_params_set_rate_near(p,h,&rate,0);
  snd_pcm_uframes_t per=period,buf=period*4;snd_pcm_hw_params_set_period_size_near(p,h,&per,0);snd_pcm_hw_params_set_buffer_size_near(p,h,&buf);
  if((e=snd_pcm_hw_params(p,h))<0){fprintf(stderr,"hw_params: %s\n",snd_strerror(e));return 1;}
  printf("rate=%u period=%lu buffer=%lu\n",rate,per,buf);fflush(stdout);
  short b[960*2*4];unsigned long fr=0;int xruns=0;double t0=0;unsigned long f0=0;
  double start=now();
  while(now()-start<seconds){
    for(unsigned i=0;i<per;i++){unsigned long v=fr+i;b[2*i]=(short)(v&0xffff);b[2*i+1]=(short)((v>>16)&0xffff);}
    snd_pcm_sframes_t w=snd_pcm_writei(p,b,per);
    if(w<0){if(w==-EPIPE)xruns++;fprintf(stderr,"write err %s at frame %lu t=%.3f\n",snd_strerror((int)w),fr,now()-start);if(snd_pcm_recover(p,(int)w,1)<0)break;continue;}
    fr+=w; if(!t0&&fr>48000){t0=now();f0=fr;}
  }
  double el=now()-t0;printf("frames=%lu xruns=%d consumed_rate=%.1f\n",fr,xruns,(fr-f0)/el);
  snd_pcm_close(p);return 0;
}
