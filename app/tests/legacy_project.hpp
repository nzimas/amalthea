#pragma once
#include "core.hpp"
namespace amalthea {
constexpr int LegacyTrackPatchBytes=Tracks*(1+8*5);
inline std::vector<uint8_t> legacyBankBytes(std::vector<uint8_t> b) {
 if(b[4]>=24)b.resize(b.size()-2); // row variations (AMAL24; fixtures hold none)
 if(b[4]>=23)b.resize(b.size()-2-Tracks*2*NativeCount*NativeParams); // native profiles (AMAL23)
 if(b[4]>=22)b.resize(b.size()-ModulationSettingsBytes); // flanger/phaser profiles
 if(b[4]>=21)b.resize(b.size()-Tracks*MaxRows*2); // FX lengths
 if(b[4]>=20)b.resize(b.size()-Tracks*MaxRows);   // note lengths
 if(b[4]>=19)b.resize(b.size()-2);   // compressor sidechain
 if(b[4]>=17)b.resize(b.size()-PortSettingsBytes);
 if(b[4]>=16)b.resize(b.size()-NoteLockBytes-FilterSettingsBytes);
 const size_t tail=b.size()-TrackPatchBytes;
 std::vector<uint8_t> banks;
 for(int t=0;t<Tracks;t++) {
  auto i=tail+t*(2+Instruments*5);
  banks.push_back(b[i]);
  banks.insert(banks.end(),b.begin()+i+2,b.begin()+i+2+8*5);
 }
 b.resize(tail);b.insert(b.end(),banks.begin(),banks.end());
 const size_t patches=12+8+Tracks*(3+MaxRows*11)+2;
 b.erase(b.begin()+patches+8*5,b.begin()+patches+Instruments*5);
 // Old fixtures have only eight instrument numbers.
 for(int t=0;t<Tracks;t++)for(int r=0;r<MaxRows;r++){
  auto i=12+8+t*(3+MaxRows*11)+3+r*11+2;
  if(b[i]!=NoInstrument)b[i]%=8;
 }
 const size_t settings=patches+8*5+MasterModules*4;
 for(int t=0;t<Tracks;t++)b[settings+t*6+3]%=8;
 b[4]=14;
 uint32_t crc=~0u;for(size_t i=12;i<b.size();i++){crc^=b[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}
 crc=~crc;for(int j=0;j<4;j++)b[8+j]=(crc>>(8*j))&255;
 return b;
}
// A current (AMAL18, 12-slot) payload rewritten as AMAL17 with eleven instrument slots.
inline std::vector<uint8_t> elevenSlotBytes(std::vector<uint8_t> b) {
 if(b[4]>=24)b.resize(b.size()-2); // row variations (AMAL24; fixtures hold none)
 if(b[4]>=23)b.resize(b.size()-2-Tracks*2*NativeCount*NativeParams); // native profiles (AMAL23)
 if(b[4]>=22)b.resize(b.size()-ModulationSettingsBytes); // flanger/phaser profiles (AMAL22)
 if(b[4]>=21)b.resize(b.size()-Tracks*MaxRows*2); // FX lengths (AMAL21)
 if(b[4]>=20)b.resize(b.size()-Tracks*MaxRows);   // note lengths (AMAL20)
 if(b[4]>=19)b.resize(b.size()-2);   // compressor sidechain (AMAL19)
 const size_t after=NoteLockBytes+FilterSettingsBytes+PortSettingsBytes;
 const size_t tp=b.size()-after-TrackPatchBytes;
 for(int t=Tracks-1;t>=0;t--){
  size_t i=tp+t*(2+Instruments*5);
  b[i+1]&=uint8_t(~(1u<<(11-8)));
  b.erase(b.begin()+i+2+11*5,b.begin()+i+2+12*5);
 }
 const size_t patches=12+8+Tracks*(3+MaxRows*11)+2;
 b.erase(b.begin()+patches+11*5,b.begin()+patches+12*5);
 for(int t=0;t<Tracks;t++)for(int r=0;r<MaxRows;r++){
  auto i=12+8+t*(3+MaxRows*11)+3+r*11+2;
  if(b[i]!=NoInstrument&&b[i]>=11)b[i]=10;
 }
 const size_t settings=patches+11*5+MasterModules*4;
 for(int t=0;t<Tracks;t++)if(b[settings+t*6+3]>=11)b[settings+t*6+3]=10;
 b[4]=17;
 uint32_t crc=~0u;for(size_t i=12;i<b.size();i++){crc^=b[i];for(int j=0;j<8;j++)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}
 crc=~crc;for(int j=0;j<4;j++)b[8+j]=(crc>>(8*j))&255;
 return b;
}
}
