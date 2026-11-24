/* Device QA utility: inject one evdev key or cross press without grabbing input. */
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
int main(int argc,char **argv) {
  if(argc!=5 && argc!=6)return 2;
  int fd=open(argv[1],O_WRONLY);if(fd<0)return 1;
  struct input_event e={0};e.type=atoi(argv[2]);e.code=atoi(argv[3]);e.value=atoi(argv[4]);
  if(write(fd,&e,sizeof e)!=sizeof e)return 1;
  struct input_event sync={0};sync.type=EV_SYN;sync.code=SYN_REPORT;
  if(write(fd,&sync,sizeof sync)!=sizeof sync)return 1;
  usleep((argc==6?atoi(argv[5]):40)*1000);
  e.value=0;
  if(write(fd,&e,sizeof e)!=sizeof e || write(fd,&sync,sizeof sync)!=sizeof sync)return 1;
  close(fd);return 0;
}
