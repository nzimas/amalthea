#pragma once
#include <string>
namespace amalthea {
enum class UsbRoleAction { None, Host, Device };
// Pure policy: never switch while a laptop is attached. Host ownership is scoped to FX.
struct UsbRolePolicy {
 bool owned=false;double detachedSince=-1;
 static int status(bool active,const std::string&role,const std::string&link){
  if(role=="usb_host")return 3;
  if(!active)return 0;
  // Device mode by itself does not mean a laptop is connected.
  return link=="not attached"?2:link.empty()?0:1;
 }
 UsbRoleAction update(bool active,const std::string&role,const std::string&link,double now){
  if(!active){detachedSince=-1;return owned&&role=="usb_host"?UsbRoleAction::Device:UsbRoleAction::None;}
  if((role!="usb_device"&&role!="usb_null")||link!="not attached"){detachedSince=-1;return UsbRoleAction::None;}
  if(detachedSince<0)detachedSince=now;
  return now-detachedSince>=3?UsbRoleAction::Host:UsbRoleAction::None;
 }
};
class BrickUsbRole {
 UsbRolePolicy policy;double next=0;int previousPower=0;std::string error;
 int typeC=-1; // applied AXP2202 CC mode; -2 when this board has none
 bool restore();
 void applyTypeC(bool hostCapable);
public:
 int state=0; // 0 idle/unsupported, 1 device link present, 2 settling before host, 3 host, 4 error
 BrickUsbRole();~BrickUsbRole();
 void poll(bool active,double now);
 const std::string&failure()const{return error;}
};
}
