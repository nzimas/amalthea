// SPDX-License-Identifier: GPL-3.0-only
#include "sessions.hpp"
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
namespace amalthea {
std::string SessionBank::path(int slot)const {
 if(slot<0||slot>=64)return {};
 char file[32];snprintf(file,sizeof file,"slot-%02d.amalthea",slot+1);
 // Keep separate banks for distinct --project paths (including test fixtures).
 auto p=std::filesystem::path(legacy);
 return (p.parent_path()/(p.stem().string()+"-slots")/file).string();
}
std::string SessionBank::source(int slot)const {
 auto p=path(slot);std::error_code ec;
 if(slot==0&&!std::filesystem::exists(p,ec)&&std::filesystem::exists(legacy,ec))return legacy;
 return p;
}
std::array<uint8_t,64> SessionBank::scan()const {
 std::array<uint8_t,64> state{};
 for(int slot=0;slot<64;slot++) {
  auto p=source(slot);std::error_code ec;
  if(std::filesystem::exists(p,ec)){Editor scratch;std::string error;state[slot]=load(slot,scratch,error)?1:2;}
 }
 return state;
}
bool SessionBank::save(int slot,const Project&p,std::string&error)const {
 auto file=path(slot);if(file.empty()){error="INVALID SESSION SLOT";return false;}
 std::error_code ec;std::filesystem::create_directories(std::filesystem::path(file).parent_path(),ec);
 if(ec){error="CANNOT CREATE SESSION FOLDER";return false;}
 return saveProject(p,file,error);
}
bool SessionBank::load(int slot,Project&p,std::string&error)const {
 Editor e;if(!load(slot,e,error))return false;p=e.project;return true;
}
namespace {
uint32_t checksum(const std::vector<uint8_t>&b){uint32_t c=~0u;for(auto v:b){c^=v;for(int j=0;j<8;j++)c=(c>>1)^(0xedb88320u&-(c&1));}return ~c;}
void put(std::vector<uint8_t>&b,uint32_t v,int n){while(n--){b.push_back(v&255);v>>=8;}}
void options(std::vector<uint8_t>&b,PatternOverrides o){put(b,o.bpm,2);put(b,o.root,1);put(b,o.scale,1);}
}
bool SessionBank::save(int slot,const Editor &e,std::string &error)const{
 auto file=path(slot);if(file.empty()){error="INVALID SESSION SLOT";return false;}
 std::vector<uint8_t>b;put(b,e.sessionDefaults.bpm,2);put(b,e.sessionDefaults.root,1);put(b,e.sessionDefaults.scale,1);put(b,e.sessionDefaults.quantize,1);
 put(b,e.activePattern+1,1);options(b,e.patternOverrides);
 auto project=[&](const Project&p){auto bytes=encodeProject(p);put(b,bytes.size(),4);b.insert(b.end(),bytes.begin(),bytes.end());};
 auto density=[&](const DensitySettings&d){for(const auto &row:d)for(auto v:row)put(b,v,1);};
 project(e.project);density(e.density);
 for(const auto &slot:e.patterns){put(b,bool(slot.state),1);options(b,slot.overrides);if(slot.state){project(*slot.state);density(slot.density);}}
 std::vector<uint8_t>h={'A','M','S','B',3,0,0,0};put(h,checksum(b),4);h.insert(h.end(),b.begin(),b.end());
 std::error_code ec;std::filesystem::create_directories(std::filesystem::path(file).parent_path(),ec);
 if(ec){error="CANNOT CREATE SESSION FOLDER";return false;}
 auto tmp=file+".tmp";FILE*f=fopen(tmp.c_str(),"wb");if(!f){error="CANNOT OPEN SAVE";return false;}
 bool ok=fwrite(h.data(),1,h.size(),f)==h.size()&&fflush(f)==0&&fsync(fileno(f))==0;if(fclose(f))ok=false;
 if(ok)ok=rename(tmp.c_str(),file.c_str())==0;
 if(!ok){remove(tmp.c_str());error="SAVE FAILED";return false;}
 int fd=open(std::filesystem::path(file).parent_path().c_str(),O_RDONLY);if(fd>=0){fsync(fd);close(fd);}return true;
}
bool SessionBank::load(int slot,Editor &out,std::string &error)const{
 if(slot<0||slot>=64){error="INVALID SESSION SLOT";return false;}
 std::ifstream f(source(slot),std::ios::binary);if(!f){error="EMPTY SESSION SLOT";return false;}
 f.seekg(0,std::ios::end);auto size=f.tellg();if(size<12||size>4000000){error="INVALID SESSION SIZE";return false;}f.seekg(0);
 std::vector<uint8_t>b(size);if(!f.read((char*)b.data(),size)){error="SESSION READ FAILED";return false;}
 Editor e;
 if(std::string(b.begin(),b.begin()+4)=="AMAL"){
   if(!decodeProject(e.project,b,error))return false;
   e.sessionDefaults={e.project.bpm,e.project.root,e.project.scale};
 }else{
   if(std::string(b.begin(),b.begin()+4)!="AMSB"||(b[4]<1||b[4]>3)||b[5]||b[6]||b[7]){error="INVALID SESSION VERSION";return false;}
   size_t i=8;bool ok=true;
   // Explicit little-endian reader, with bounds checked before every field.
   auto read=[&](int n){uint32_t v=0;for(int j=0;j<n;j++){if(i>=b.size()){ok=false;return v;}v|=uint32_t(b[i++])<<(8*j);}return v;};
   auto sum=read(4);if(sum!=checksum(std::vector<uint8_t>(b.begin()+12,b.end()))){error="SESSION CHECKSUM FAILED";return false;}
   auto opt=[&](){PatternOverrides o;o.bpm=read(2);o.root=read(1);o.scale=read(1);ok&=(!o.bpm||(o.bpm>=30&&o.bpm<=300))&&(o.root==255||o.root<12)&&(o.scale==255||o.scale<Scales);return o;};
   auto project=[&](Project&p){auto n=read(4);if(!ok||n>200000||n>b.size()-i){ok=false;return;}std::vector<uint8_t>bytes(b.begin()+i,b.begin()+i+n);i+=n;ok&=decodeProject(p,bytes,error);};
   auto density=[&](DensitySettings&d){for(auto &row:d)for(int old=0;old<(b[4]==1?6:Columns);old++){auto v=read(1);ok&=densityPercent(v)<=100;if(b[4]>=2)row[old]=v;else if(old!=4)row[old==5?4:old]=v;}};
   e.sessionDefaults.bpm=read(2);e.sessionDefaults.root=read(1);e.sessionDefaults.scale=read(1);if(b[4]>=3)e.sessionDefaults.quantize=read(1);e.activePattern=int(read(1))-1;e.patternOverrides=opt();project(e.project);density(e.density);
   ok&=e.sessionDefaults.bpm>=30&&e.sessionDefaults.bpm<=300&&e.sessionDefaults.root<12&&e.sessionDefaults.scale<Scales&&e.sessionDefaults.quantize<PatternQuantizeModes&&e.activePattern>=-1&&e.activePattern<32;
   for(auto &slot:e.patterns){auto present=read(1);ok&=present<=1;slot.overrides=opt();if(present&&ok){auto p=std::make_shared<Project>();project(*p);density(slot.density);slot.state=p;}}
   if(!ok||i!=b.size()){error="INVALID SESSION CONTENT";return false;}
   e.applyMusicalSettings();
 }
 auto reset=out.project.trackReset;out.project=e.project;out.sessionDefaults=e.sessionDefaults;out.patterns=std::move(e.patterns);out.patternOverrides=e.patternOverrides;out.activePattern=e.activePattern;out.density=e.density;out.patternSlot=std::max(0,e.activePattern);
 for(int t=0;t<Tracks;t++)out.project.trackReset[t]=reset[t]+1;
 out.cancelQueuedPattern();out.playing=out.editing=out.dirty=false;out.undoStack.clear();out.redoStack.clear();out.row=std::min(out.row,columnLength(out.project.tracks[out.track],out.column)-1);return true;
}

}
