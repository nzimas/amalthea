#include "fx_rack.hpp"
#include "fx_audio.hpp"
#include "usb_role.hpp"
#include "audio_probe.hpp"
#include "ui.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
using namespace amalthea;
static void check(bool x,const char*s){if(!x){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(int argc,char**argv){
 UsbRolePolicy port;
 check(UsbRolePolicy::status(true,"usb_device","not attached")==2,"unplugged cold boot reports host preparation, not a laptop");
 check(UsbRolePolicy::status(true,"usb_device","configured")==1,"configured link reported as present");
 check(UsbRolePolicy::status(true,"usb_host","not attached")==3,"host waiting is distinct from device mode");
 check(port.update(true,"usb_device","configured",0)==UsbRoleAction::None,"connected laptop never switched to host");
 check(port.update(true,"usb_device","not attached",1)==UsbRoleAction::None,"disconnect starts settling interval");
 check(port.update(true,"usb_device","not attached",3.9)==UsbRoleAction::None,"host waits three seconds");
 check(port.update(true,"usb_device","configured",4)==UsbRoleAction::None,"reconnection cancels pending host switch");
 check(port.update(true,"usb_device","not attached",5)==UsbRoleAction::None&&port.update(true,"usb_device","not attached",8)==UsbRoleAction::Host,"FX automatically enters host after stable detach");
 port.owned=true;check(port.update(true,"usb_host","not attached",1000)==UsbRoleAction::None,"host stays active without trial timeout");
 check(port.update(false,"usb_host","not attached",1001)==UsbRoleAction::Device,"leaving FX restores owned device mode");
 port.owned=false;check(port.update(false,"usb_host","not attached",1002)==UsbRoleAction::None,"unrelated host configuration not taken over");
 check(port.update(false,"usb_device","not attached",1003)==UsbRoleAction::None,"dashboard never auto switches USB role");
 for(auto name:{"FM-1 Bigorna","FM-1 Tonitor","FM-1 Norilsk","FM-1 Lilliput"})check(fm1Firmware(name)>0,"all four product identities recognized");
 check(fm1Mapping(1,6).count==6&&fm1Mapping(3,3).count==3&&fm1Mapping(2,8).offset==2,"descriptor-based track routing");check(fm1Mapping(3,2).stereo&&fm1Mapping(3,2).count==1,"stereo mix never falsely split into separate tracks");
 MidiClock clock;clock.byte(0xfa,1);for(int n=1;n<=96;n++)clock.byte(0xf8,1+n*60./120/24);check(clock.locked&&clock.running&&std::abs(clock.bpm-120)<.01,"MIDI24ppqn clock locks to120");clock.byte(0xfc,3.1);check(!clock.running,"external Stop respected");clock.expire(5);check(!clock.locked,"missing clock expires for internal fallback");
 {// USB delivers pulses in bursts (several at one timestamp): the tempo is still exact, and whole.
  // Each pulse is stamped when the host next reads the port (every 2 ms), not when it was sent.
  auto read=[](double t){return std::ceil(t/.002)*.002;};
  MidiClock c;c.byte(0xfa,1);for(int n=1;n<=480;n++)c.byte(0xf8,read(1+n*60./127/24));
  check(c.locked&&c.tempo==127&&std::abs(c.bpm-127)<.5,"bursts of USB MIDI pulses keep the FM1's tempo");
  int held=c.tempo;double base=1+480*60./127/24;for(int n=1;n<=96;n++)c.byte(0xf8,read(base+n*60./127.4/24));check(c.tempo==held,"jitter under 0.6 BPM does not retune the effects");
  base+=96*60./127.4/24;for(int n=1;n<=60;n++)c.byte(0xf8,read(base+n*60./90/24));check(c.tempo==90,"a tempo change on the FM1 is followed within a beat or two");}
 {// The pan of each FM1 track arrives as CC 10 on channels 1 .. 6, running status and real time between.
  MidiTrackPan p;for(uint8_t b:{0xB0,10,64,0xF8,10,0,0xB5,0x07,100,10,127,0x93,60,100,0xBA,10,5})p.byte(b);
  check(p.pan[0]==0&&p.pan[5]==127&&p.pan[1]==-1&&p.pan[3]==-1,"track pans from CC 10, others not yet heard");
  check(midiPanPosition(64)==.5f&&midiPanPosition(0)==0&&midiPanPosition(127)==1,"CC 10 64 is the centre");}
 Editor e;e.page=Page::Appliances;e.applianceItem=1;e.action(Action::Enter);check(e.inFx()&&e.fxRack,"FX starts on channel strips");
 e.action(Action::RandomizeColumn);check(e.fxRack->slots[0].effect.type!=FxType::None&&e.fxRack->slots[1].effect.type==FxType::None,"plain hold chooses only the selected slot's effect type");
 check(e.notice==fxName(e.fxRack->slots[0].effect.type)&&e.noticeMs==1500,"slot randomize names the effect in a 1.5 s overlay");e.notice.clear();
 e.action(Action::Down);e.action(Action::RandomizeColumn);auto before=*e.fxRack;check(before.slots[1].effect.type!=FxType::None,"plain hold on FX2 chooses FX2");
 e.action(Action::ShiftDown);e.action(Action::RandomizeColumn);e.action(Action::ShiftUp);check(e.fxRack->slots[1].effect.type==before.slots[1].effect.type&&!memcmp(&e.fxRack->slots[0],&before.slots[0],sizeof before.slots[0]),"Shift hold retains FX2 identity and leaves FX1 untouched");
 e.action(Action::Down);auto atLevel=*e.fxRack;e.action(Action::RandomizeColumn);check(!memcmp(&e.fxRack->slots,&atLevel.slots,sizeof atLevel.slots)&&e.notice=="CHOOSE AN FX SLOT","hold X on level row changes nothing");e.notice.clear();
 e.action(Action::Enter);check(e.fxLevelEdit,"Enter edits level");e.action(Action::Back);check(!e.fxLevelEdit&&e.page==Page::FxAppliance,"Back leaves level edit first");
 e.action(Action::Back);check(e.page==Page::FxAppliance,"Back at the mixer with no history stays in FX");
 e.action(Action::Down);check(e.fxRow==3,"bypass row below level");e.action(Action::Enter);check(e.fxRack->bypass[0]&&!e.fxRack->bypass[1],"Enter bypasses selected channel");
 e.action(Action::Enter);check(!e.fxRack->bypass[0],"Enter re-enables channel");e.action(Action::Down);check(e.fxRow==0,"rows wrap to FX1");
 for(int i=2;i<12;i++)check(e.fxRack->slots[i].effect.type==FxType::None,"randomization isolated to chosen input channel");
 e.action(Action::Enter);check(e.page==Page::FxSequencer,"Enter opens per-slot sequencer");
 e.action(Action::RandomizerOptions);check(e.page==Page::FxRandomizer&&e.inFx()&&e.fxRack->slots[0].density==45,"tap X opens per-slot density at45percent");
 e.action(Action::Right);check(e.fxRack->slots[0].density==46&&e.fxRack->slots[1].density==45,"density isolated to selected slot");
 e.action(Action::Down);e.action(Action::Left);check(e.fxRack->slots[0].complexity==49&&e.fxRack->slots[0].density==46,"down selects complexity");e.action(Action::Up);
 e.action(Action::Enter);e.action(Action::Master);check(e.page==Page::FxRandomizer,"Y opens randomizer settings");e.action(Action::Master);check(e.page==Page::FxSequencer,"Y returns to sequencer");
 auto defaults=e.fxRack->slots[0].settings;auto defaultEffect=e.fxRack->slots[0].effect;
 e.fxRack->slots[0].length=7;e.fxRack->slots[0].density=20;
 e.fxRack->slots[0].steps[10].enabled=true;e.action(Action::RandomizeColumn);
 int triggers=0;for(int i=0;i<7;i++){auto&step=e.fxRack->slots[0].steps[i];triggers+=step.enabled;if(step.enabled)check(step.locks.any(),"trigger contains actual parameter overrides");else check(step.locks.none(),"empty steps have no stale overrides");}
 check(triggers==1&&e.fxRack->slots[0].steps[10].enabled,"density uses seven-step length and preserves hidden steps");
 check(e.fxRack->running&&e.fxRack->slots[0].sequenced,"hold X enables sequence and internal transport");
 check(defaultEffect.type==e.fxRack->slots[0].effect.type&&defaultEffect.value==e.fxRack->slots[0].effect.value&&!memcmp(&defaults,&e.fxRack->slots[0].settings,sizeof defaults),"sequence randomizer preserves effect identity and defaults");
 check(!e.fxRack->slots[1].sequenced,"other slot sequence untouched");
 {auto lockedValues=[&](int complexity){auto&slot=e.fxRack->slots[0];slot.complexity=complexity;slot.density=100;e.action(Action::RandomizeColumn);
   std::vector<std::vector<int>> rows;for(int i=0;i<slot.length;i++){std::vector<int> row;for(int k=0;k<FxLocks;k++)if(slot.steps[i].locks[k])row.push_back(slot.steps[i].values[k]);rows.push_back(row);}return rows;};
  auto same=lockedValues(0);bool identical=true;for(auto&row:same)identical&=row==same[0];check(identical&&same[0].size()>0,"complexity 0 gives every step the same random parameters");
  auto wild=lockedValues(100);bool varied=false;for(auto&row:wild)varied|=row!=wild[0];check(varied,"complexity 100 varies parameters per step");
  e.fxRack->slots[0].density=20;e.fxRack->slots[0].complexity=50;e.action(Action::RandomizeColumn);}
 e.fxRack->slots[0].density=0;e.action(Action::RandomizeColumn);for(int i=0;i<7;i++)check(!e.fxRack->slots[0].steps[i].enabled,"zero density clears active sequence");
 e.fxRack->slots[0].density=100;e.action(Action::RandomizeColumn);for(int i=0;i<7;i++)check(e.fxRack->slots[0].steps[i].enabled,"full density fills active sequence");
 e.fxRack->slots[0].length=16;e.fxRack->slots[0].density=45;
 e.action(Action::ColumnSettings);e.action(Action::Left);check(e.fxRack->slots[0].length==15,"A preferences changes length");
 e.action(Action::Down);e.action(Action::Down);check(e.fxPreference==2&&e.fxRack->slots[0].tempo==0,"tempo row follows session by default");
 e.action(Action::Right);check(e.fxRack->slots[0].tempo==e.fxRack->tempo,"leaving SESSION starts from the session tempo");e.action(Action::Right);check(e.fxRack->slots[0].tempo==e.fxRack->tempo+1&&e.fxRack->slots[1].tempo==0,"slot tempo independent");
 e.fxRack->slots[0].tempo=30;e.action(Action::Left);check(e.fxRack->slots[0].tempo==0,"below 30 returns to SESSION");e.fxRack->slots[0].tempo=97;e.fxRack->slots[0].complexity=63;
 e.action(Action::Up);e.action(Action::Up);e.action(Action::ColumnSettings);
 e.action(Action::Enter);check(e.page==Page::FxParameters&&!e.fxDefaultEdit,"step editor");e.action(Action::Right);check(e.fxRack->slots[0].steps[0].enabled,"step trigger editable");check(e.notice=="TRIGGER"&&e.noticeDetail=="ON"&&e.noticeDetailSerial==e.noticeSerial&&e.noticeMs==1000,"value change shows large popup");e.action(Action::Down);e.action(Action::Left);check(e.fxRack->slots[0].steps[0].locks[0],"step mix lock overrides default");
 auto&s=e.fxRack->slots[0];Effect effect;InsertSettings settings;resolveFxStep(s,0,effect,settings);check(effect.value==s.steps[0].values[0],"step lock applied by DSP resolver");
 s.steps[0].enabled=false;resolveFxStep(s,0,effect,settings);check(effect.value==s.effect.value&&!memcmp(&settings,&s.settings,sizeof settings),"non-triggered steps use slot defaults");s.steps[0].enabled=true;
 e.action(Action::Home);check(e.page==Page::FxAppliance,"R2 returns to FX mixer");
 {Editor nav;nav.page=Page::Appliances;nav.applianceItem=1;nav.action(Action::Enter);nav.action(Action::RandomizeColumn);nav.notice.clear();
  nav.action(Action::Enter);nav.action(Action::ColumnSettings);check(nav.page==Page::FxPreferences,"nav to preferences");
  nav.action(Action::Back);check(nav.page==Page::FxSequencer,"Back returns to sequencer");
  nav.action(Action::Enter);nav.action(Action::Back);check(nav.page==Page::FxSequencer,"Back from step parameters");
  nav.action(Action::Back);check(nav.page==Page::FxAppliance,"Back from sequencer to mixer");
  nav.action(Action::Back);check(nav.page==Page::FxAppliance&&nav.inFx(),"Back never leaves the appliance");}e.action(Action::Patterns);check(e.page==Page::FxSessions,"middle key opens FX sessions");e.action(Action::Right);e.action(Action::Enter);e.action(Action::Right);check(e.fxRack->tempo==121,"session tempo editable");
 SessionButton button;button.press(0,e);check(button.release(20)==Action::LoadSession,"session tap loads selected FX slot");button.press(100,e);check(button.tick(900)==Action::SaveSession&&button.release(950)==Action::Reserved,"session hold saves once without also loading");
 auto dir=std::filesystem::temp_directory_path()/("amalthea-fx-test-"+std::to_string(getpid()));auto path=fxSessionPath(dir.string(),11);std::string error;e.fxRack->tempo=177;e.fxRack->running=true;
 e.fxRack->bypass[4]=true;check(saveFxRack(*e.fxRack,path,error),"synthetic FX session saved atomically");FxRack loaded;check(loadFxRack(loaded,path,error),"synthetic FX session loads");check(loaded.tempo==177&&loaded.slots[0].density==45&&!loaded.running&&loaded.slots[0].effect.type==s.effect.type&&loaded.slots[0].steps[0].locks[0],"tempo/profiles/steps restored with transport stopped");check(loaded.bypass[4]&&!loaded.bypass[0]&&loaded.slots[0].tempo==97&&loaded.slots[0].complexity==63&&loaded.slots[1].tempo==0,"bypass, slot tempo and complexity restored");check(scanFxSessions(dir.string())[11]==1,"64-slot bank scans saved slot");{
  std::ifstream file(path,std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});
  uint32_t size=0;for(int i=0;i<4;i++)size|=uint32_t(bytes[5+i])<<(i*8);size_t header=9+size+2+4+6;
  // Reconstruct the previous on-disk layout without the new per-slot density byte.
  bytes.erase(bytes.end()-4-FxChannels-FxSlots*3,bytes.end()-4); // AFXR4 bypass + AFXR5 complexity/tempo
  for(int slot=11;slot>=0;slot--)bytes.erase(bytes.begin()+header+slot*(5+16*(1+16+FxLocks))+4);
  bytes[4]=2;uint32_t crc=~0u;for(size_t i=0;i<bytes.size()-4;i++){crc^=bytes[i];for(int bit=0;bit<8;bit++)crc=(crc>>1)^(0xedb88320u&-(crc&1));}crc=~crc;
  for(int i=0;i<4;i++)bytes[bytes.size()-4+i]=(crc>>(i*8))&255;
  auto legacy=fxSessionPath(dir.string(),12);std::ofstream old(legacy,std::ios::binary);old.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());old.close();
  FxRack previous;check(loadFxRack(previous,legacy,error)&&previous.slots[0].density==45&&previous.tempo==177&&!previous.bypass[4]&&previous.slots[0].complexity==50&&previous.slots[0].tempo==0,"AFXR2 sessions load with default density and no bypass");
 }
 std::filesystem::remove_all(dir);
 AudioStats stats;FxAudio audio(stats);FxRack rack;rack.tempo=120;rack.running=true;rack.slots[0].length=7;rack.slots[1].clock=4;check(audio.submit(rack,DeviceSettings{}),"FX snapshot queued");float out[512]{};
 for(int i=0;i<44100;i+=256)audio.render(out,std::min(256,44100-i));int slow=stats.fxStep[0],fast=stats.fxStep[1];check(slow>=0&&slow<7&&fast>=0&&fast<16&&slow!=fast,"internal clock runs independent lengths and divisions without USB clock");
 {AudioStats tempoStats;FxAudio tempoAudio(tempoStats);FxRack tempos;tempos.running=true;tempos.slots[2].tempo=60;tempos.slots[3].tempo=240;tempos.slots[2].direction=tempos.slots[3].direction=0;
  check(tempoAudio.submit(tempos,DeviceSettings{}),"slot tempo rack queued");float block[512];for(int i=0;i<44100;i+=256)tempoAudio.render(block,256);
  int slow=tempoStats.fxStep[2],fast=tempoStats.fxStep[3];check(slow>=0&&fast>=0&&slow!=fast,"each slot sequencer follows its own internal tempo");
  check(fxSlotTempo(tempos,2)==60&&fxSlotTempo(tempos,0)==tempos.tempo,"slot tempo 0 follows the session");}
 // External stopped clock overrides an internally running session.
 audio.midi(0xfa,1);for(int n=1;n<=48;n++)audio.midi(0xf8,1+n*60./90/24);audio.midi(0xfc,3);audio.render(out,256);check(stats.usbClock&&!stats.usbRunning&&stats.fxStep[0]==-1,"external stop takes precedence over internal run");
 stats.usbClock=false;audio.render(out,256);check(stats.fxStep[0]>=0,"external clock loss automatically resumes session tempo");
 // Actual stereo input passes through the per-channel router into the physical stereo mix.
 FxRack dry;dry.levels.fill(100);check(audio.submit(dry,DeviceSettings{}),"dry rack queued");std::vector<float> input(2048*12,0);for(int i=0;i<2048;i++){input[i*12+4]=.1f;input[i*12+5]=.2f;}audio.feed(input.data(),2048,6,44100);audio.render(out,256);double l=0,r=0;for(int i=0;i<256;i++){l+=out[i*2];r+=out[i*2+1];}check(l>15&&r>30&&std::abs(r/l-2)<.001,"input lane3 isolated and stereo preserved");
 FxRack wet=dry;wet.slots[4].effect={FxType::Fuzz,100};check(audio.submit(wet,DeviceSettings{}),"shared FX prepared on producer thread");audio.render(out,256);double changed=0;for(float v:out){check(std::isfinite(v),"processed output finite");changed+=std::abs(v);}check(changed>0,"real shared effect processes streamed input");
 // A running sequence applies the effect only on triggered steps: empty steps pass dry.
 {AudioStats dryStats,emptyStats,fullStats,plainStats;FxAudio dryAudio(dryStats),empty(emptyStats),full(fullStats),plainWet(plainStats);
  FxRack none=wet,all=wet;none.running=all.running=true;none.slots[4].sequenced=all.slots[4].sequenced=true;for(auto&step:all.slots[4].steps)step.enabled=true;
  check(dryAudio.submit(dry,DeviceSettings{})&&empty.submit(none,DeviceSettings{})&&full.submit(all,DeviceSettings{})&&plainWet.submit(wet,DeviceSettings{}),"gate racks queued");
  for(auto*x:{&dryAudio,&empty,&full,&plainWet})x->feed(input.data(),2048,6,44100);float a[512],b[512],c2[512],d[512];
  for(int n=0;n<2;n++){dryAudio.render(a,256);empty.render(b,256);full.render(c2,256);plainWet.render(d,256);}
  for(int i=0;i<512;i++)check(std::abs(a[i]-b[i])<1e-6,"empty steps of a running sequence pass dry");
  for(int i=0;i<512;i++)check(std::abs(c2[i]-d[i])<1e-6,"triggered steps apply the effect");}
 // Bypass fades the channel to its dry input; other channels keep processing.
 {AudioStats plainStats,bypassStats;FxAudio plain(plainStats),bypassed(bypassStats);FxRack skip=wet;skip.bypass[2]=true;
  check(plain.submit(dry,DeviceSettings{})&&bypassed.submit(skip,DeviceSettings{}),"bypass racks queued");
  plain.feed(input.data(),2048,6,44100);bypassed.feed(input.data(),2048,6,44100);float a[512],b[512];
  plain.render(a,256);bypassed.render(b,256);double diff=0;for(int i=0;i<512;i++)diff+=std::abs(a[i]-b[i]);check(diff>0,"bypass crossfades rather than switching instantly");
  for(int n=0;n<2;n++){plain.render(a,256);bypassed.render(b,256);}for(int i=0;i<512;i++)check(std::abs(a[i]-b[i])<1e-6,"bypassed channel passes dry after the 441-frame crossfade");}
 for(auto type:selectableEffects())if(type!=FxType::None){FxRack generated;auto&slot=generated.slots[0];slot.effect={type,80};slot.density=100;
  check(randomizeFxSequence(generated,0),"all selectable insert types support sequences");for(int i=0;i<16;i++){Effect resolved;InsertSettings profile;resolveFxStep(slot,i,resolved,profile);check(resolved.type==type&&slot.steps[i].enabled&&slot.steps[i].locks.any(),"all effects generate resolvable per-step overrides");}}
 // A recovered capture discontinuity must discard stale queued samples and re-prime.
 {AudioStats recoveryStats;FxAudio recovery(recoveryStats);FxRack clean;recovery.submit(clean,DeviceSettings{});
  std::vector<float> before(4096*2,.2f),after(2048*2,-.1f);float block[512];
  recovery.feed(before.data(),4096,1,44100);recovery.render(block,256);
  check(block[510]>.1f,"pre-fault input is audible");
  recovery.inputDiscontinuity();recovery.render(block,256);
  for(float v:block)check(v==0,"recovery drops stale queued ingress before re-priming");
  recovery.feed(after.data(),2048,1,44100);recovery.render(block,256);
  for(int i=128;i<512;i++)check(block[i]<-.02f,"post-recovery input resumes without stale pre-fault samples");
  check(recoveryStats.usbOverruns==0,"recovery does not overflow ingress");
 }
 // Simulate USB being delivered in 35 ms bursts, independently of codec callbacks.
 for(int rate:{44100,48000})for(double clock:{.9995,1.0005}){
  AudioStats jitterStats;FxAudio jitter(jitterStats);FxRack dryRack;jitter.submit(dryRack,DeviceSettings{});std::vector<float> burst(2048*2,.08f);float block[512];double pending=0;
  jitter.feed(burst.data(),2048,1,rate);
  for(int n=0;n<2400;n++){
   if(n&&n%6==0){pending+=1536.*rate/44100*clock;int frames=int(pending);pending-=frames;jitter.feed(burst.data(),frames,1,rate);}
   jitter.render(block,256);if(n>0)for(float v:block)check(v>.02f,"buffered USB bursts remain continuous at both sample rates and drifting clocks");
  }
  check(jitterStats.usbUnderruns==0&&jitterStats.usbOverruns==0,"bursty USB capture has no starvation or ring overflow");
 }

 {
  auto probePath=(std::filesystem::temp_directory_path()/("amalthea-probe-test-"+std::to_string(getpid()))).string();
  {AudioProbe probe(probePath);float raw[256*6],stereo[512];for(int i=0;i<256;i++){for(int c=0;c<3;c++)raw[i*6+c*2]=raw[i*6+c*2+1]=.1f*(c+1);stereo[i*2]=.2f;stereo[i*2+1]=.4f;}
   for(int n=0;n<1200;n++){probe.capture(raw,256,3,48000);probe.output(stereo,256);}}
  check(std::filesystem::file_size(probePath+"/raw-usb.wav")==44+48000*5*3*2,"raw diagnostic retains five seconds of three-channel native-rate PCM");
  check(std::filesystem::file_size(probePath+"/codec-input.wav")==44+44100*5*2*2,"codec diagnostic retains five seconds of stereo output PCM");
  std::ifstream f(probePath+"/raw-usb.wav",std::ios::binary);char header[44];f.read(header,44);check(std::string(header,4)=="RIFF"&&header[22]==3,"diagnostic WAV has valid multichannel header");
  std::filesystem::remove_all(probePath);
 }
 stats.fxStep[0]=9;e.notice.clear();
 for(int width:{640,1024})for(auto page:{Page::FxAppliance,Page::FxSequencer,Page::FxParameters,Page::FxPreferences,Page::FxSessions,Page::FxSessionSettings,Page::FxRandomizer}){
  e.page=page;Canvas c(width,width*3/4);drawUI(c,e,stats);if(width==1024&&page==Page::FxSequencer){check(c.pixels[471*1024+150]==0xffe4cbff,"playhead visibly reaches second row");check(c.pixels[275*1024+24]==0xffc4a0ff,"navigation cursor stays on selected first-row step");}for(auto pixel:c.pixels)check(pixel>>24==255,"FX views opaque at both screen sizes");if(argc>1&&width==1024)writePPM(c,std::string(argv[1])+"-"+std::to_string(int(page))+".ppm");
 }
 // FLANGER and PHASER are shared with the tracker: chosen, randomized, saved and heard here too.
 {
  auto&list=selectableEffects();check(std::count(list.begin(),list.end(),FxType::Flanger)==1&&std::count(list.begin(),list.end(),FxType::Phaser)==1,"appliance effect list offers FLANGER and PHASER");
  bool drewFlanger=false,drewPhaser=false;FxRack draws;for(int n=0;n<400&&!(drewFlanger&&drewPhaser);n++){randomizeFxSlot(draws,0,false,DeviceSettings{});drewFlanger|=draws.slots[0].effect.type==FxType::Flanger;drewPhaser|=draws.slots[0].effect.type==FxType::Phaser;}
  check(drewFlanger&&drewPhaser,"appliance hold X can draw FLANGER and PHASER");
  FxRack rack;rack.levels.fill(100);rack.slots[0].effect={FxType::Flanger,100};rack.slots[2].effect={FxType::Phaser,100};
  rack.slots[0].settings.flanger={33,20,40,90,0,40,0,70,10,50,100};rack.slots[2].settings.phaser={60,80,60,30,90,0,50,30,30,30,80};
  auto path=(std::filesystem::temp_directory_path()/("amalthea-fx-modulation-"+std::to_string(getpid())+".amalthea")).string();std::string err;FxRack back;
  check(saveFxRack(rack,path,err)&&loadFxRack(back,path,err),"appliance session with FLANGER and PHASER saves and loads");std::filesystem::remove(path);
  check(back.slots[0].effect.type==FxType::Flanger&&back.slots[2].effect.type==FxType::Phaser&&back.slots[0].settings.flanger==rack.slots[0].settings.flanger&&back.slots[2].settings.phaser==rack.slots[2].settings.phaser,"their profiles survive the appliance session");
  for(int slot:{0,2}){
   AudioStats ds,ws;FxAudio dryA(ds),wetA(ws);FxRack none;none.levels.fill(100);FxRack one=none;one.slots[slot]=rack.slots[slot];
   check(dryA.submit(none,DeviceSettings{})&&wetA.submit(one,DeviceSettings{}),"modulation racks queued");
   double diff=0,energy=0;std::vector<float> in(256*12);float a[512],b[512];
   for(int block=0;block<400;block++){for(int i=0;i<256;i++){float x=.2f*std::sin((block*256+i)*.06f)+.1f*std::sin((block*256+i)*.23f);for(int c=0;c<12;c++)in[i*12+c]=c/2==slot/2?x:0;}
    dryA.feed(in.data(),256,6,44100);wetA.feed(in.data(),256,6,44100);dryA.render(a,256);wetA.render(b,256);
    if(block>100)for(int i=0;i<512;i++){check(std::isfinite(b[i]),"appliance modulation output finite");diff+=(a[i]-b[i])*(a[i]-b[i]);energy+=a[i]*a[i];}}
   printf("appliance %s: change %.1f dB relative to dry\n",fxName(rack.slots[slot].effect.type),10*std::log10(diff/std::max(energy,1e-12)));
   check(energy>0&&diff>energy*.1,"FLANGER and PHASER audibly process appliance input (over -10 dB of change)");
  }
 }
 puts("PASS: FM1 mapping, clock fallback/priority, independent FX sequencers, locks, randomizers, session bank and actual audio path");
}
