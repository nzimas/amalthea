// Bounded USBFS ingress diagnostic. Temporarily bypasses snd-usb-audio, not OHCI.
// No firmware, patch, session or transport writes. See docs/FX-APPLIANCE.md.
#include <linux/usbdevice_fs.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
static volatile sig_atomic_t interrupted=0;
static void stop(int){interrupted=1;}
static double seconds(){using namespace std::chrono;return duration<double>(steady_clock::now().time_since_epoch()).count();}
static std::string line(const std::filesystem::path&p){std::ifstream f(p);std::string s;std::getline(f,s);return s;}
constexpr int PacketBytes=552,PacketsPerUrb=8,Requests=8;
struct Packet {int status=0,urbStatus=0,start=0;unsigned bytes=0;bool received=false;double completed=0;std::array<unsigned char,PacketBytes>data{};};
struct Request {
 alignas(usbdevfs_urb) std::array<unsigned char,sizeof(usbdevfs_urb)+PacketsPerUrb*sizeof(usbdevfs_iso_packet_desc)>storage{};
 std::array<unsigned char,PacketBytes*PacketsPerUrb>data{};int sequence=0;bool pending=false;
 usbdevfs_urb*urb(){return reinterpret_cast<usbdevfs_urb*>(storage.data());}
};
struct Device {
 int fd=-1;std::array<bool,3>claimed{},originallyBound{},detached{};
 bool acquire(){
  // ALSA can bind each interface separately. Check every owner before detaching
  // any, then recheck as detaching one may also release companion interfaces.
  for(unsigned i=0;i<3;i++){
   usbdevfs_getdriver driver{};driver.interface=i;
   int result=ioctl(fd,USBDEVFS_GETDRIVER,&driver);
   if(result==0){fprintf(stderr,"Interface %u owner: %s\n",i,driver.driver);
    if(strcmp(driver.driver,"snd-usb-audio")){fprintf(stderr,"Unexpected interface owner; refusing detach\n");return false;}
    originallyBound[i]=true;
   }else if(errno!=ENODATA||i==0){perror("preflight interface owner");return false;}
  }
  for(unsigned i=0;i<3;i++){
   usbdevfs_getdriver driver{};driver.interface=i;
   if(ioctl(fd,USBDEVFS_GETDRIVER,&driver)==0){
    if(strcmp(driver.driver,"snd-usb-audio")){fprintf(stderr,"Interface owner changed; aborting\n");return false;}
    usbdevfs_disconnect_claim claim{};claim.interface=i;claim.flags=USBDEVFS_DISCONNECT_CLAIM_IF_DRIVER;strcpy(claim.driver,"snd-usb-audio");
    if(ioctl(fd,USBDEVFS_DISCONNECT_CLAIM,&claim)<0){perror("disconnect/claim interface");return false;}
    detached[i]=true;
   }else if(errno!=ENODATA||ioctl(fd,USBDEVFS_CLAIMINTERFACE,&i)<0){perror("claim unbound interface");return false;}
   claimed[i]=true;fprintf(stderr,"Interface %u claimed\n",i);
  }
  return true;
 }
 bool note(int note,bool on){unsigned char data[]={static_cast<unsigned char>(on?9:8),static_cast<unsigned char>(on?0x93:0x83),static_cast<unsigned char>(note),static_cast<unsigned char>(on?85:0)};
  usbdevfs_bulktransfer transfer{1,4,500,data};return ioctl(fd,USBDEVFS_BULK,&transfer)==4;
 }
 ~Device(){if(fd<0)return;
  // Stop ISO traffic before releasing interface claims and reconnecting ALSA.
  if(claimed[2]){usbdevfs_setinterface alt{2,0};if(ioctl(fd,USBDEVFS_SETINTERFACE,&alt)<0)perror("restore alt 0");}
  for(int i=2;i>=0;i--)if(claimed[i]){unsigned interface=i;if(ioctl(fd,USBDEVFS_RELEASEINTERFACE,&interface)<0)perror("release interface");}
  if(detached[0]||detached[1]||detached[2])for(int i=0;i<3;i++)if(originallyBound[i]){
   usbdevfs_getdriver driver{};driver.interface=i;
   if(ioctl(fd,USBDEVFS_GETDRIVER,&driver)==0)continue;
   usbdevfs_ioctl reconnect{i,int(USBDEVFS_CONNECT),nullptr};if(ioctl(fd,USBDEVFS_IOCTL,&reconnect)<0)perror("reconnect snd-usb-audio");else fprintf(stderr,"ALSA interface %d reconnected\n",i);
  }
  close(fd);
 }
};
static bool supported(const std::filesystem::path&p){
 std::ifstream f(p/"descriptors",std::ios::binary);std::vector<unsigned char>d{std::istreambuf_iterator<char>(f),{}};
 int interface=-1,alt=-1;bool format=false,endpoint=false,midi=false;
 for(size_t at=0;at+2<=d.size();){unsigned n=d[at],type=d[at+1];if(n<2||at+n>d.size())return false;auto*q=d.data()+at;
  if(type==4&&n>=9){interface=q[2];alt=q[3];}
  if(interface==2&&alt==1&&type==0x24&&n==11&&q[2]==2&&q[3]==1&&q[4]==6&&q[5]==2&&q[6]==16&&q[7]==1&&q[8]==0x44&&q[9]==0xac&&q[10]==0)format=true;
  if(interface==2&&alt==1&&type==5&&n>=7&&q[2]==0x84&&(q[3]&3)==1&&(q[4]|(q[5]<<8))==PacketBytes&&q[6]==1)endpoint=true;
  if(interface==1&&type==5&&n>=7&&q[2]==1&&(q[3]&3)==2)midi=true;
  at+=n;
 }
 return format&&endpoint&&midi;
}
static bool save(const char*path,const std::vector<Packet>&packets,int count){
 FILE*f=fopen(path,"wb");if(!f)return false;uint32_t bytes=0;
 for(int i=0;i<count;i++)if(packets[i].received&&!packets[i].status&&!packets[i].urbStatus)bytes+=packets[i].bytes/12*12;
 auto number=[&](uint32_t v,int n){while(n--){fputc(v&255,f);v>>=8;}};
 fwrite("RIFF",1,4,f);number(bytes+36,4);fwrite("WAVEfmt ",1,8,f);number(16,4);number(1,2);number(6,2);number(44100,4);number(44100*12,4);number(12,2);number(16,2);fwrite("data",1,4,f);number(bytes,4);
 for(int i=0;i<count;i++)if(packets[i].received&&!packets[i].status&&!packets[i].urbStatus)fwrite(packets[i].data.data(),1,packets[i].bytes/12*12,f);
 bool ok=!ferror(f);return fclose(f)==0&&ok;
}
int main(int argc,char**argv){
 if(argc<2||argc>4){fprintf(stderr,"Usage: fm1_packet_probe output.wav [seconds:5..180] [passive|audition]\n");return 2;}
 int duration=8;bool audition=true;
 if(argc>=3){char*end=nullptr;long n=strtol(argv[2],&end,10);if(!end||*end||n<5||n>180)return 2;duration=int(n);}
 if(argc==4){if(!strcmp(argv[3],"passive"))audition=false;else if(strcmp(argv[3],"audition"))return 2;}
 const int maxPackets=(duration+1)*1000+Requests*PacketsPerUrb;
 fprintf(stderr,"Capture %d seconds; MIDI audition %s; no session or transport writes\n",duration,audition?"on":"off");
 signal(SIGTERM,stop);signal(SIGINT,stop);
 std::filesystem::path path;
 for(auto&e:std::filesystem::directory_iterator("/sys/bus/usb/devices"))if(line(e.path()/"idVendor")=="1209"&&line(e.path()/"product")=="FM-1 Bigorna"&&supported(e.path())){path=e.path();break;}
 if(path.empty()){fprintf(stderr,"Expected six-channel Bigorna absent; nothing changed\n");return 3;}
 int bus=std::stoi(line(path/"busnum")),address=std::stoi(line(path/"devnum"));char node[64];snprintf(node,sizeof node,"/dev/bus/usb/%03d/%03d",bus,address);
 // Requests outlive Device, whose destructor cancels traffic and closes the fd.
 std::array<Request,Requests>requests;Device dev;dev.fd=open(node,O_RDWR);if(dev.fd<0){perror("open USB device");return 4;}
 if(!dev.acquire())return 6;
 usbdevfs_setinterface alt{2,1};if(ioctl(dev.fd,USBDEVFS_SETINTERFACE,&alt)<0){perror("stream alt 1");return 8;}
 std::vector<Packet>packets(maxPackets);int next=0,count=0,event=0,errors=0;bool failed=false;bool held[3]{};const int notes[]={60,64,67};
 auto submit=[&](Request&r){auto*u=r.urb();memset(u,0,r.storage.size());r.data.fill(0xa5);u->type=USBDEVFS_URB_TYPE_ISO;u->endpoint=0x84;u->flags=USBDEVFS_URB_ISO_ASAP;u->buffer=r.data.data();u->buffer_length=r.data.size();u->number_of_packets=PacketsPerUrb;u->usercontext=&r;
  for(int i=0;i<PacketsPerUrb;i++)u->iso_frame_desc[i].length=PacketBytes;
  r.sequence=next;next+=PacketsPerUrb;if(next>maxPackets)return false;
  if(ioctl(dev.fd,USBDEVFS_SUBMITURB,u)<0){perror("submit ISO");return false;}r.pending=true;return true;
 };
 double begin=seconds();for(auto&r:requests)if(!submit(r)){failed=true;break;}
 while(!failed&&!interrupted&&seconds()-begin<duration){
  double elapsed=seconds()-begin;
  while(audition&&event<6&&elapsed>=1.+(event/2)*1.8+(event%2)*1.5){int n=event/2;bool on=event%2==0;if(on)held[n]=true;if(!dev.note(notes[n],on)){perror("MIDI bulk");failed=true;break;}if(!on)held[n]=false;fprintf(stderr,"%.6f note %d %s\n",elapsed,notes[n],on?"on":"off");event++;}
  if(failed)break;
  usbdevfs_urb*u=nullptr;if(ioctl(dev.fd,USBDEVFS_REAPURBNDELAY,&u)<0){if(errno==EAGAIN||errno==EINTR){std::this_thread::sleep_for(std::chrono::microseconds(200));continue;}perror("reap ISO");failed=true;break;}
  auto&r=*static_cast<Request*>(u->usercontext);r.pending=false;
  for(int i=0;i<PacketsPerUrb;i++){auto&packet=packets[r.sequence+i];auto&desc=u->iso_frame_desc[i];packet.received=true;packet.completed=seconds()-begin;packet.status=int(desc.status);packet.urbStatus=u->status;packet.start=u->start_frame+i;packet.bytes=desc.actual_length;
   if(packet.bytes>PacketBytes){failed=true;packet.bytes=0;}
   memcpy(packet.data.data(),r.data.data()+i*PacketBytes,packet.bytes);if(packet.status||packet.urbStatus||packet.bytes%12)errors++;
  }count=std::max(count,r.sequence+PacketsPerUrb);if(!submit(r))failed=true;
 }
 for(int n=0;n<3;n++)if(held[n])dev.note(notes[n],false);
 for(auto&r:requests)if(r.pending)ioctl(dev.fd,USBDEVFS_DISCARDURB,r.urb());
 bool saved=save(argv[1],packets,count);std::string csv=std::string(argv[1])+".packets.csv";FILE*f=fopen(csv.c_str(),"w");
 if(f){fputs("packet,start_frame,received,urb_status,packet_status,bytes,completed_seconds\n",f);for(int i=0;i<count;i++){auto&p=packets[i];fprintf(f,"%d,%d,%d,%d,%d,%u,%.9f\n",i,p.start,int(p.received),p.urbStatus,p.status,p.bytes,p.completed);}fclose(f);}
 // Preserve errored payloads for correlation; each CSV packet indexes one
 // fixed 552-byte block, with unused bytes zero padded. WAV excludes errors.
 std::string payload=std::string(argv[1])+".payload.bin";FILE*pf=fopen(payload.c_str(),"wb");
 if(pf){for(int i=0;i<count;i++)fwrite(packets[i].data.data(),1,PacketBytes,pf);fclose(pf);}
 fprintf(stderr,"USBFS %s: %d packet slots, %d errors; WAV saved %d; interrupted %d\n",node,count,errors,saved,int(interrupted));
 return failed||interrupted||!saved||errors?9:0;
}
