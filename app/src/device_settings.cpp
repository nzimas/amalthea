// Device preferences are independent of every live/saved musical session.
#include "core.hpp"
#include <cstdio>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
namespace amalthea {
const std::array<FxType,GlobalInsertCount>&globalInsertTypes(){
 static const std::array<FxType,GlobalInsertCount>types={FxType::Clouds,FxType::AmpSim,FxType::Biztortion,FxType::Rotor,FxType::Space,FxType::Wavefold,FxType::Fuzz,FxType::Rectify,FxType::Filter,FxType::Tremolo,FxType::Stutter,FxType::QDelay,FxType::ChowMatrix,FxType::CrushClone,FxType::Frequalizer,FxType::Rings,FxType::Fracture2,FxType::CrazyIvan,FxType::Bitmurderer,FxType::LoFi,FxType::Flanger,FxType::Phaser,
  FxType::Oops,FxType::BufferOverride,FxType::Livecut,FxType::SupaTrigga,FxType::BeatRepeat,FxType::TimeShaper,FxType::Jumblr,FxType::Scrubby,FxType::Glitchlets,FxType::Skidder,
  FxType::Choppr,FxType::GateShaper,FxType::TremoSquare,FxType::Beads,FxType::Softcut,FxType::Transverb,FxType::GlitchShifter,FxType::PitchNasty,FxType::Amalgam,FxType::Geometer,FxType::MultiBandDistortion,FxType::Disintegrate,FxType::BitGlitter,FxType::Pafnuty,
  FxType::Cyanide,FxType::Polarizer,FxType::Angr,FxType::Warps,FxType::FreqShifter,FxType::BrokenFft,FxType::Treemonster,FxType::RingMod,FxType::Combulator,FxType::Resonator,FxType::Vocoder,FxType::Smr,FxType::RezSynth,FxType::TalkBox,FxType::MdaVocoder,FxType::RePsycho};return types;
}
namespace {
uint32_t checksum(const uint8_t*b,int count){uint32_t c=~0u;for(int n=0;n<count;n++){c^=b[n];for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
std::bitset<128> mask(){std::bitset<128> m;for(auto type:globalInsertTypes())m.set(int(type));return m;}
}
// AMDV3: brightness, then the disabled-insert bits (16 bytes); AMDV1/AMDV2 stored 4/8 bytes.
bool saveDeviceSettings(const DeviceSettings&s,const std::string&path,std::string&error){
 if(s.brightness>100||(s.disabledInserts&~mask()).any()){error="INVALID DEVICE SETTINGS";return false;}
 std::array<uint8_t,26>b{{'A','M','D','V',3,s.brightness}};for(int n=0;n<128;n++)if(s.disabledInserts[n])b[6+n/8]|=uint8_t(1<<(n%8));
 auto crc=checksum(b.data(),22);for(int n=0;n<4;n++)b[22+n]=(crc>>(8*n))&255;
 auto dir=std::filesystem::path(path).parent_path();std::error_code ec;if(!dir.empty())std::filesystem::create_directories(dir,ec);if(ec){error="CANNOT CREATE DEVICE SETTINGS";return false;}
 auto tmp=path+".tmp";FILE*f=fopen(tmp.c_str(),"wb");if(!f){error="CANNOT OPEN DEVICE SETTINGS";return false;}
 bool ok=fwrite(b.data(),1,b.size(),f)==b.size()&&fflush(f)==0&&fsync(fileno(f))==0;if(fclose(f))ok=false;if(ok)ok=rename(tmp.c_str(),path.c_str())==0;
 if(!ok){remove(tmp.c_str());error="DEVICE SETTINGS SAVE FAILED";return false;}
 int fd=open(dir.empty()?".":dir.c_str(),O_RDONLY);if(fd>=0){fsync(fd);close(fd);}return true;
}
bool loadDeviceSettings(DeviceSettings&out,const std::string&path,std::string&error){
 FILE*f=fopen(path.c_str(),"rb");if(!f){error="CANNOT OPEN DEVICE SETTINGS";return false;}
 std::array<uint8_t,26>b{};size_t length=fread(b.data(),1,b.size(),f);bool ok=fgetc(f)==EOF;fclose(f);
 int bytes=b[4]==1?4:b[4]==2?8:16, payload=6+bytes;
 uint32_t stored=0;for(int n=0;n<4;n++)stored|=uint32_t(b[payload+n])<<(8*n);
 DeviceSettings s;s.brightness=b[5];for(int n=0;n<bytes*8;n++)if(b[6+n/8]>>(n%8)&1)s.disabledInserts.set(n);
 std::bitset<128> retired;for(auto t:{FxType::Diode,FxType::Rat,FxType::Lowpass,FxType::Highpass})retired.set(int(t));
 if(!ok||length!=size_t(payload+4)||b[0]!='A'||b[1]!='M'||b[2]!='D'||b[3]!='V'||b[4]<1||b[4]>3||stored!=checksum(b.data(),payload)||s.brightness>100||(s.disabledInserts&~(mask()|retired)).any()){error="INVALID DEVICE SETTINGS";return false;}
 s.disabledInserts&=mask();
 out=s;return true;
}
}
