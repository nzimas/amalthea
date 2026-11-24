#include "usb_role.hpp"
#include "usb_audio_out.hpp"
#include <fstream>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#ifdef AMALTHEA_USB_HOST
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#endif
namespace amalthea {
namespace {
constexpr auto Role="/sys/devices/platform/soc/usbc0/otg_role";
constexpr auto Link="/sys/class/udc/5100000.udc-controller/state";
constexpr auto Power="/sys/devices/platform/soc/usbc0/axp_5v";
constexpr auto Host="/sys/devices/platform/soc/usbc0/usb_host";
constexpr auto Device="/sys/devices/platform/soc/usbc0/usb_device";
constexpr auto Owner="/tmp/amalthea-usb-role-owner";
std::string readLine(const char*path){std::ifstream f(path);std::string s;std::getline(f,s);return s;}
bool writeValue(const char*path,int value){FILE*f=fopen(path,"w");if(!f)return false;bool ok=fprintf(f,"%d\n",value)>0;if(fclose(f))ok=false;return ok;}
// Vendor ABI: reading these two files executes the mode change. Never probe them.
bool trigger(const char*path){FILE*f=fopen(path,"r");if(!f)return false;char result[128];while(fread(result,1,sizeof result,f)>0){}bool ok=!ferror(f);if(fclose(f))ok=false;return ok;}
#ifdef AMALTHEA_USB_HOST
// AXP2202 on i2c-6 at 0x34 owns the USB-C CC pins (AXP717 layout: 0xE3 mode).
// Returns 1 applied, 0 failed, -1 no such PMIC on this board.
int setTypeC(TypeCMode mode){
 if(readLine("/sys/bus/i2c/devices/6-0034/name")!="axp2202")return -1;
 int fd=open("/dev/i2c-6",O_RDWR|O_CLOEXEC);if(fd<0)return 0;
 auto transfer=[&](char direction,uint8_t reg,i2c_smbus_data*d){i2c_smbus_ioctl_data a{direction,reg,I2C_SMBUS_BYTE_DATA,d};return ioctl(fd,I2C_SMBUS,&a)==0;};
 i2c_smbus_data d{};int result=0;
 if(ioctl(fd,I2C_SLAVE_FORCE,0x34)==0&&transfer(I2C_SMBUS_READ,0xe3,&d)){
  uint8_t want=typeCModeValue(d.byte,mode);
  if(d.byte==want)result=1;
  else{i2c_smbus_data w{};w.byte=want;if(transfer(I2C_SMBUS_WRITE,0xe3,&w)&&transfer(I2C_SMBUS_READ,0xe3,&d)&&d.byte==want){result=1;fprintf(stderr,"USB-C port mode: %s\n",mode==TypeCMode::Sink?"sink":"dual-role");}}
 }
 close(fd);return result;
}
#endif
}
BrickUsbRole::BrickUsbRole(){
#ifdef AMALTHEA_USB_HOST
 std::ifstream f(Owner);int pid=0,power=0;if(f>>pid>>power){
  // Recover only a dead previous process's role, never another live appliance.
  if(pid>0&&kill(pid,0)<0&&errno==ESRCH){policy.owned=true;previousPower=power==1;}
 }
#endif
}
BrickUsbRole::~BrickUsbRole(){
 if(policy.owned)restore();
 // The USB-C mode is deliberately left as is: the next app instance wants the
 // same mode, restoring here would force two USB renegotiations per restart,
 // and the board resets the AXP2202 CC mode to dual-role at every boot.
}
void BrickUsbRole::applyTypeC(bool hostCapable){
#ifdef AMALTHEA_USB_HOST
 // A phone only hosts a pure sink; FX hosting the FM1 needs dual-role.
 auto want=hostCapable?TypeCMode::Drp:TypeCMode::Sink;
 if(typeC==-2||typeC==int(want))return;
 int r=setTypeC(want);typeC=r==1?int(want):r<0?-2:typeC;
#else
 (void)hostCapable;
#endif
}
bool BrickUsbRole::restore(){
 bool power=writeValue(Power,previousPower),mode=trigger(Device);
 if(power&&mode){policy.owned=false;remove(Owner);error.clear();return true;}
 error="Cannot restore USB device mode";state=4;return false;
}
void BrickUsbRole::poll(bool active,double now){
#ifdef AMALTHEA_USB_HOST
 if(now<next)return;next=now+.25;
 applyTypeC(active||policy.owned);
 auto role=readLine(Role);if(role.empty()){state=0;return;}
 auto link=readLine(Link);auto action=policy.update(active,role,link,now);
 if(policy.owned&&role=="usb_device"&&!active){
  if(writeValue(Power,previousPower)){policy.owned=false;remove(Owner);error.clear();state=0;}
  else {error="Cannot restore USB port power";state=4;}
  return;
 }
 if(action==UsbRoleAction::Device){restore();state=error.empty()?0:4;return;}
 if(action==UsbRoleAction::Host){
  auto power=readLine(Power);previousPower=power=="1";
  // Save ownership before a mode change, so supervisor restart can recover it.
  FILE*f=fopen(Owner,"w");if(!f){error="Cannot record USB role ownership";state=4;next=now+2;return;}
  bool recorded=fprintf(f,"%d %d\n",int(getpid()),previousPower)>0;if(fclose(f))recorded=false;
  if(!recorded){remove(Owner);error="Cannot record USB role ownership";state=4;next=now+2;return;}
  policy.owned=true;
  if(!trigger(Host)||!writeValue(Power,1)||readLine(Role)!="usb_host"){restore();error="Cannot enable USB host mode";state=4;next=now+2;return;}
  error.clear();state=3;policy.detachedSince=-1;return;
 }
 state=!error.empty()?4:UsbRolePolicy::status(active,role,link);
#else
 (void)active;(void)now;
#endif
}
}
