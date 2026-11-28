/* LIFEBLOOD's mirrored output stage (bigorna_master): time per 256-frame block, 20 s of a busy mix with
 * every effect on (BIGORNA's defaults, a long reverb), against the 5805 us block. */
#include "../src/bigorna_master.h"
#include <stdio.h>
#include <time.h>
#include <math.h>
int main(void){uint8_t ch[7]={40,30,10,100,0,30,90},dl[8]={31,0,64,50,0,100,30,90},rv[7]={8,100,90,80,10,110,80},cp[8]={107,107,55,3,16,14,0,127};
 bgm_reset();bgm_set(ch,dl,rv,cp,1200);int32_t l[32],r[32],s[32],duck[3]={65536,65536,65536};
 struct timespec a,b;int blocks=44100*20/32;double worst=0;clock_gettime(CLOCK_MONOTONIC,&a);
 for(int k=0;k<blocks;k++){for(int i=0;i<32;i++){l[i]=r[i]=(int32_t)(8000*sin((k*32+i)*.05));s[i]=l[i]/2;}bgm_block(l,r,s,s,s,duck);}
 clock_gettime(CLOCK_MONOTONIC,&b);double us=((b.tv_sec-a.tv_sec)*1e9+(b.tv_nsec-a.tv_nsec))/1e3;
 printf("%.1f us per 256-frame block (%.2f%% of 5805 us)\n",us/blocks*8,us/blocks*8/5805*100);return 0;}
