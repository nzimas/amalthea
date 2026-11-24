// Exercise actual diagnostic ownership/cleanup against independent kernel-style
// interface bindings, including the EBUSY failure seen on the physical Brick.
#define main packet_probe_entry
#include "../tools/fm1_packet_probe.cpp"
#undef main
#include <cstdarg>
namespace {
std::array<int,3>owners{};int failInterface=-1,detachCalls=0;
bool releaseCompanions=false,rebindCompanions=false;
void require(bool ok,const char*message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
void reset(){owners={1,1,1};failInterface=-1;detachCalls=0;releaseCompanions=rebindCompanions=false;}
}
extern "C" int __wrap_ioctl(int,unsigned long request,...){
 va_list args;va_start(args,request);void*arg=va_arg(args,void*);va_end(args);
 if(request==USBDEVFS_GETDRIVER){auto&p=*static_cast<usbdevfs_getdriver*>(arg);int owner=owners.at(p.interface);if(!owner){errno=ENODATA;return -1;}strcpy(p.driver,owner==1?"snd-usb-audio":owner==2?"usbfs":"unexpected-driver");return 0;}
 if(request==USBDEVFS_DISCONNECT_CLAIM){auto&p=*static_cast<usbdevfs_disconnect_claim*>(arg);if(int(p.interface)==failInterface){errno=EIO;return -1;}require(owners.at(p.interface)==1,"detach must target ALSA only");owners[p.interface]=2;detachCalls++;if(p.interface==0&&releaseCompanions)owners[1]=owners[2]=0;return 0;}
 if(request==USBDEVFS_CLAIMINTERFACE){unsigned i=*static_cast<unsigned*>(arg);if(owners.at(i)){errno=EBUSY;return -1;}if(int(i)==failInterface){errno=EIO;return -1;}owners[i]=2;return 0;}
 if(request==USBDEVFS_RELEASEINTERFACE){unsigned i=*static_cast<unsigned*>(arg);require(owners.at(i)==2,"release must target our own interface");owners[i]=0;return 0;}
 if(request==USBDEVFS_SETINTERFACE)return 0;
 if(request==USBDEVFS_IOCTL){auto&p=*static_cast<usbdevfs_ioctl*>(arg);require(p.ioctl_code==int(USBDEVFS_CONNECT),"cleanup only reconnects drivers");owners.at(p.ifno)=1;if(p.ifno==0&&rebindCompanions)owners[1]=owners[2]=1;return 0;}
 require(false,"unexpected ioctl");return -1;
}
int main(){
 reset();{Device d;d.fd=open("/dev/null",O_RDWR);require(d.acquire(),"independently bound interfaces can all be acquired");require(owners==std::array<int,3>{2,2,2},"all three interfaces owned by probe");require(detachCalls==3,"detach each independent ALSA binding");}require(owners==std::array<int,3>{1,1,1},"all independent drivers restored");
 reset();releaseCompanions=rebindCompanions=true;{Device d;d.fd=open("/dev/null",O_RDWR);require(d.acquire(),"companion interfaces released by first detach are claimable");require(detachCalls==1,"avoid detaching already-unbound companions");}require(owners==std::array<int,3>{1,1,1},"driver that reclaims companions restored without duplicate attach");
 reset();failInterface=1;{Device d;d.fd=open("/dev/null",O_RDWR);require(!d.acquire(),"partial acquisition failure reported");}require(owners==std::array<int,3>{1,1,1},"partial acquisition failure releases claims and restores drivers");
 reset();owners[2]=3;{Device d;d.fd=open("/dev/null",O_RDWR);require(!d.acquire(),"unexpected companion driver rejected before any detach");require(detachCalls==0,"unknown ownership causes no detach");}require(owners==std::array<int,3>{1,1,3},"unexpected driver left unchanged");
 reset();owners[1]=0;{Device d;d.fd=open("/dev/null",O_RDWR);require(d.acquire(),"initially unbound MIDI interface can be claimed");}require(owners==std::array<int,3>{1,0,1},"initially unbound interface remains unbound when driver does not reclaim it");
 puts("PASS: independent/companion interface ownership, rollback, unknown-owner refusal and restoration");
}
