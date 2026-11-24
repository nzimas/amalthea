/* Read-only PCM diagnostic: samples the ALSA write buffer of a running process.
 * Does not attach/stop the audio thread or modify its memory. Linux/AArch64. */
#define _FILE_OFFSET_BITS 64
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <linux/input.h>
#include <sys/ioctl.h>
struct Transfer {int64_t result;uint64_t buffer,frames;};
int main(int argc,char **argv) {
  if(argc!=3)return 2;
  int jack=open("/dev/input/event2",O_RDONLY|O_NONBLOCK);
  unsigned long switches=0;
  if(jack>=0) {
    if(ioctl(jack,EVIOCGSW(sizeof switches),&switches)>=0)
      printf("jack_switches=0x%lx headphone_inserted=%d microphone_inserted=%d\n",switches,!!(switches&(1ul<<SW_HEADPHONE_INSERT)),!!(switches&(1ul<<SW_MICROPHONE_INSERT)));
    close(jack);
  }
  char path[128];snprintf(path,sizeof path,"/proc/%s/mem",argv[1]);
  int memory=open(path,O_RDONLY);if(memory<0){perror("process memory");return 1;}
  snprintf(path,sizeof path,"/proc/%s/task/%s/syscall",argv[1],argv[2]);
  double l2=0,r2=0,side2=0,mid2=0,lr=0,peak=0;uint64_t frames=0,blocks=0;
  for(int i=0;i<1500;i++) {
    FILE *f=fopen(path,"r");if(!f){perror("thread syscall");return 1;}
    unsigned long call;unsigned long long fd,request,pointer;
    int n=fscanf(f,"%lu %llx %llx %llx",&call,&fd,&request,&pointer);fclose(f);
    if(n==4 && call==29 && request==0x40184150) {
      struct Transfer t;int16_t data[512];
      if(pread(memory,&t,sizeof t,(off_t)pointer)==sizeof t && t.frames>0 && t.frames<=256 &&
         pread(memory,data,t.frames*4,(off_t)t.buffer)==(ssize_t)t.frames*4) {
        for(unsigned k=0;k<t.frames;k++) {
          double l=data[2*k]/32768.,r=data[2*k+1]/32768.;
          l2+=l*l;r2+=r*r;lr+=l*r;mid2+=(l+r)*(l+r)*.25;side2+=(l-r)*(l-r)*.25;
          peak=fmax(peak,fmax(fabs(l),fabs(r)));frames++;
        }
        blocks++;
      }
    }
    usleep(2000);
  }
  close(memory);
  if(!frames){fprintf(stderr,"No active ALSA write buffers sampled\n");return 1;}
  printf("sampled_blocks=%llu sampled_frames=%llu peak=%.6f peak_dbfs=%.2f rms_l=%.6f rms_r=%.6f mid_rms=%.6f side_rms=%.6f correlation=%.6f\n",
    (unsigned long long)blocks,(unsigned long long)frames,peak,20*log10(fmax(peak,1e-12)),sqrt(l2/frames),sqrt(r2/frames),sqrt(mid2/frames),sqrt(side2/frames),lr/sqrt(fmax(l2*r2,1e-24)));
}
