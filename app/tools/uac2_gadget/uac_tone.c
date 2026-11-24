/* Feasibility probe: plays a stereo test tone into the UAC2 gadget's ALSA playback
 * device, which the USB host records. 440 Hz left, 660 Hz right, -12 dBFS. */
#include <alsa/asoundlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char**argv){
  const char*dev=argc>1?argv[1]:"hw:CARD=UAC2Gadget,DEV=0";
  int seconds=argc>2?atoi(argv[2]):30; unsigned rate=48000; int ch=2;
  snd_pcm_t*p; int e=snd_pcm_open(&p,dev,SND_PCM_STREAM_PLAYBACK,0);
  if(e<0){fprintf(stderr,"open %s: %s\n",dev,snd_strerror(e));return 1;}
  e=snd_pcm_set_params(p,SND_PCM_FORMAT_S16_LE,SND_PCM_ACCESS_RW_INTERLEAVED,ch,rate,0,100000);
  if(e<0){fprintf(stderr,"params: %s\n",snd_strerror(e));return 1;}
  snd_pcm_uframes_t buf,per; snd_pcm_get_params(p,&buf,&per);
  printf("device=%s rate=%u channels=%d buffer=%lu period=%lu\n",dev,rate,ch,buf,per);fflush(stdout);
  enum{N=480}; short b[N*2]; double ph[2]={0,0}, f[2]={440,660}, a=32767*0.25;
  long total=(long)seconds*rate, done=0; int xruns=0;
  while(done<total){
    for(int i=0;i<N;i++)for(int c=0;c<2;c++){b[i*2+c]=(short)lrint(a*sin(ph[c]));ph[c]+=2*M_PI*f[c]/rate;if(ph[c]>2*M_PI)ph[c]-=2*M_PI;}
    snd_pcm_sframes_t w=snd_pcm_writei(p,b,N);
    if(w<0){if(w==-EPIPE)xruns++;w=snd_pcm_recover(p,(int)w,1);if(w<0){fprintf(stderr,"write: %s\n",snd_strerror((int)w));return 1;}continue;}
    done+=w;
  }
  snd_pcm_drain(p);snd_pcm_close(p);
  printf("frames=%ld xruns=%d\n",done,xruns);return 0;
}
