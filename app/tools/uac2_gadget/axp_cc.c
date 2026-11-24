/* AXP2202 Type-C CC control/monitor over /dev/i2c-6 (addr 0x34), SMBus byte ops.
 * axp_cc get            -> prints E3 (mode) and E7 (status)
 * axp_cc set <hex>      -> writes E3, prints old/new
 * axp_cc monitor <file> -> appends E3/E7 changes with uptime until /tmp/axp-cc-stop exists */
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static int fd;
static int rd(unsigned char reg){union i2c_smbus_data d;struct i2c_smbus_ioctl_data a={I2C_SMBUS_READ,reg,I2C_SMBUS_BYTE_DATA,&d};return ioctl(fd,I2C_SMBUS,&a)<0?-1:d.byte;}
static int wr(unsigned char reg,unsigned char v){union i2c_smbus_data d;d.byte=v;struct i2c_smbus_ioctl_data a={I2C_SMBUS_WRITE,reg,I2C_SMBUS_BYTE_DATA,&d};return ioctl(fd,I2C_SMBUS,&a);}
static const char*st(int s){static const char*n[16]={"DISABLE","UNATTACH_SNK","ATTACHWAIT_SNK","ATTACH_SNK","UNATTACH_SRC","ATTACHWAIT","ATTACH_SRC","AUDIO_ACSY","RSVD8","TRY_SRC","TRYWAIT_SNK","TRY_SNK","TRYWAIT_SRC","RSVD13","ERROR_RECOVERY","RSVD15"};return n[s&15];}
static double up(){FILE*f=fopen("/proc/uptime","r");double u=0;if(f){fscanf(f,"%lf",&u);fclose(f);}return u;}
int main(int c,char**v){
 if(c<2)return 2; fd=open("/dev/i2c-6",O_RDWR); if(fd<0){perror("i2c-6");return 1;}
 if(ioctl(fd,I2C_SLAVE_FORCE,0x34)<0){perror("slave");return 1;}
 if(!strcmp(v[1],"get")){int m=rd(0xe3),s=rd(0xe7);printf("E3=0x%02x E7=0x%02x state=%s power=%d\n",m,s,st(s),(s>>4)&3);return m<0||s<0;}
 if(!strcmp(v[1],"set")&&c>2){int old=rd(0xe3);unsigned nv=strtoul(v[2],0,16);if(old<0||wr(0xe3,nv)<0){perror("write");return 1;}usleep(1000);printf("E3 0x%02x -> 0x%02x (read back 0x%02x)\n",old,nv,rd(0xe3));return 0;}
 if(!strcmp(v[1],"monitor")&&c>2){int lm=-2,ls=-2;while(access("/tmp/axp-cc-stop",F_OK)){int m=rd(0xe3),s=rd(0xe7);if(m!=lm||s!=ls){FILE*f=fopen(v[2],"a");if(f){fprintf(f,"%.2f E3=0x%02x E7=0x%02x %s\n",up(),m,s,s<0?"?":st(s));fclose(f);}lm=m;ls=s;}usleep(50000);}return 0;}
 return 2;}
