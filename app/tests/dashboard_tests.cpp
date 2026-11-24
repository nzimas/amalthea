#include "sessions.hpp"
#include "ui.hpp"
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
using namespace amalthea;
static void check(bool b,const char*s){if(!b){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
 Editor e;e.track=7;e.row=3;e.column=2;e.playing=true;e.editing=true;e.pending.note=72;
 auto original=encodeProject(e.project);e.action(Action::Dashboard);
 check(e.page==Page::Dashboard&&!e.editing&&e.playing&&e.track==7&&e.row==3&&e.column==2&&e.visibleRows==16,"Dashboard discards pending cell and preserves cursor, transport and row size");
 const Page targets[]={Page::Pattern,Page::Mixer,Page::Master,Page::GlobalInserts,Page::Device};
 for(int tile=0;tile<5;tile++){e.page=Page::Dashboard;e.dashboardItem=tile;e.action(Action::Enter);check(e.page==targets[tile],"five shortcuts in requested order");e.action(Action::Home);check(e.page==Page::Pattern&&e.track==7,"Home always returns current track");}
 e.page=Page::Dashboard;e.dashboardItem=0;e.action(Action::Left);check(e.dashboardItem==4,"dashboard wraps to Device last");e.action(Action::Right);check(e.dashboardItem==0,"dashboard wraps to Track first");e.action(Action::Down);check(e.dashboardItem==3,"grid down keeps column");e.action(Action::Up);check(e.dashboardItem==0,"grid up keeps column");
 e.page=Page::GlobalInserts;
 for(int n=0;n<GlobalInsertCount;n++){e.globalInsertItem=n;auto type=globalInsertTypes()[n];check(e.device.insertEnabled(type),"all inserts initially enabled");e.action(Action::Enter);check(!e.device.insertEnabled(type),"Enter disables selected global insert");e.action(Action::Right);check(e.device.insertEnabled(type),"Right explicitly enables");e.action(Action::Left);check(!e.device.insertEnabled(type),"Left explicitly disables");}
 check(encodeProject(e.project)==original&&!e.dirty,"global settings never mutate musical workspace");
 e.action(Action::Cancel);check(e.page==Page::Dashboard,"global inserts Back returns dashboard");e.page=Page::Device;e.action(Action::Left);check(e.device.brightness==95,"brightness is editable as percentage");e.action(Action::Right);check(e.device.brightness==100,"brightness maximum100");
 auto dir=std::filesystem::temp_directory_path()/("amalthea-device-test-"+std::to_string(getpid()));std::filesystem::create_directories(dir);auto file=(dir/"prefs").string();std::string error;
 check(saveDeviceSettings(e.device,file,error),"write synthetic device preferences");DeviceSettings loaded;check(loadDeviceSettings(loaded,file,error)&&loaded==e.device,"brightness and all insert switches roundtrip");
 std::fstream corrupt(file,std::ios::binary|std::ios::in|std::ios::out);corrupt.seekp(7);corrupt.put('x');corrupt.close();auto before=loaded;check(!loadDeviceSettings(loaded,file,error)&&loaded==before,"CRC failure leaves current preferences unchanged");
 e.savePattern(2);SessionBank bank((dir/"session.amalthea").string());check(bank.save(0,e,error),"synthetic session fixture");Editor recall;recall.device.brightness=35;recall.device.disabledInserts=std::bitset<128>().set(int(FxType::Fuzz));auto preferences=recall.device;check(bank.load(0,recall,error)&&recall.device==preferences,"session recall cannot replace global device preferences");
 // Every listed processor bypasses globally, with exact dry output from both slots.
 for(auto type:globalInsertTypes())for(int slot=0;slot<2;slot++){
  Project p=blankProject();p.master.busMix.fill(0);p.patches[0].engine=2;p.tracks[0].length=1;p.tracks[0].level=10;p.tracks[0].steps[0].note=60;p.tracks[0].steps[0].fx[slot]={type,75};auto dry=p;dry.tracks[0].steps[0].fx[slot]={};DeviceSettings off;off.disabledInserts=std::bitset<128>().set(int(type));
  Audio a,b;a.submit(p,true,-1,nullptr,&off);b.submit(dry,true);std::vector<float>x(4096*2),y(x.size());a.render(x.data(),4096);b.render(y.data(),4096);check(x==y,"all14 global bypasses equal dry signal on both slots");
 }
 // Toggle during a held note, then while stopped. Stored locks stay intact.
 Project p=blankProject();p.master.busMix.fill(0);p.patches[0].engine=2;p.tracks[0].level=10;p.tracks[0].steps[0].note=60;p.tracks[0].steps[0].fx[0]={FxType::Fuzz,80};auto dry=p;dry.tracks[0].steps[0].fx[0]={};DeviceSettings on,off;off.disabledInserts=std::bitset<128>().set(int(FxType::Fuzz));
 Audio a,b;a.submit(p,true,-1,nullptr,&on);b.submit(dry,true);std::vector<float>x(4096*2),y(x.size());a.render(x.data(),4096);b.render(y.data(),4096);check(x!=y,"enabled insert audibly transforms sound");
 a.submit(p,true,-1,nullptr,&off);b.submit(dry,true);a.render(x.data(),512);b.render(y.data(),512);check(std::equal(x.begin(),x.begin()+1024,y.begin()),"disabling live bypasses held FX immediately");
 a.submit(p,true,-1,nullptr,&on);b.submit(dry,true);a.render(x.data(),512);b.render(y.data(),512);check(!std::equal(x.begin(),x.begin()+1024,y.begin()),"reenabling live restores stored lock without editing it");
 a.submit(p,false,-1,nullptr,&off);b.submit(dry,false);a.render(x.data(),512);b.render(y.data(),512);check(std::equal(x.begin(),x.begin()+1024,y.begin()),"disabled insert cannot continue after Stop");
 AudioStats stats;for(int width:{640,1024})for(auto page:{Page::Dashboard,Page::GlobalInserts,Page::Device})for(int selected=0;selected<5;selected++){Editor view;view.page=page;view.dashboardItem=selected;view.globalInsertItem=13;Canvas c(width,width*3/4);drawUI(c,view,stats);for(auto pixel:c.pixels)check((pixel>>24)==255,"dashboard and global menus render opaque at both resolutions");}
 std::filesystem::remove_all(dir);puts("PASS: dashboard shortcuts/navigation, brightness/preferences isolation, all28 insert bypass paths, live and stopped toggles");
}
