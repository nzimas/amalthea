#include "ported_inserts.hpp"
#include "discovery_inserts.hpp"
#include "modulation_inserts.hpp"
#include "../third_party/nornios/EngineRegistry.hpp"
#include "../third_party/nornios_ports/PortsMetadata.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace amalthea {
int portedIndex(FxType t){if(nativeIndex(t)>=0)return 11+nativeIndex(t);return t>=FxType::QDelay&&t<=FxType::LoFi?int(t)-int(FxType::QDelay):t==FxType::Flanger?9:t==FxType::Phaser?10:-1;}
FxType portedType(int k){if(k>=11)return nativeType(k-11);return k<9?FxType(int(FxType::QDelay)+k):k==9?FxType::Flanger:FxType::Phaser;}
struct PortedProcessor::Impl {
 FxType type;std::unique_ptr<nornios::Engine> engine;std::unique_ptr<NativeFx> native;int nativeK=-1;InsertSettings previous{};int tempo=-1;bool valid=false;
 float inL[32]{},inR[32]{},outL[32]{},outR[32]{};
 void send(int cmd,float a,float b=0,float c=0){nornios::EngineCommand v;v.id=cmd;v.args[0]=a;v.args[1]=b;v.args[2]=c;engine->command(v);}
 explicit Impl(FxType t):type(t){nativeK=nativeIndex(t);if(nativeK>=0){native=makeNativeFx(t);return;}engine=t==FxType::Flanger||t==FxType::Phaser?makeModulationInsert(t):t>=FxType::Rings?makeDiscoveryInsert(t):t==FxType::QDelay?nornios::makeQDelayEngine():t==FxType::ChowMatrix?nornios::makeChowMatrixEngine():t==FxType::CrushClone?nornios::makeCrushCloneEngine():nornios::makeFrequalizerEngine();engine->prepare(44100,32);}
 void configure(const InsertSettings&s,int bpm){
  if(native){if(!valid||bpm!=tempo||s.native[nativeK]!=previous.native[nativeK])native->configure(s.native[nativeK].data(),float(bpm));previous=s;valid=true;tempo=bpm;return;}
  if(type==FxType::Flanger||type==FxType::Phaser){configureModulationInsert(*engine,type,s);
  }else if(type>=FxType::Rings){configureDiscoveryInsert(*engine,type,s);
  }else if(type==FxType::QDelay){
   if(bpm!=tempo)send(2,bpm);
   for(int i=0;i<nornios::qdelayParameterCount();i++)if(!valid||s.qdelay[i]!=previous.qdelay[i])send(1,i+1,i==10?1:nornios::qdelayParameterValue(i,s.qdelay[i]));
  }else if(type==FxType::ChowMatrix){
   if(!valid){send(3,1,-60);send(3,2,0);}
   if(!valid||s.matrix[0]!=previous.matrix[0])send(3,5,std::lround(s.matrix[0]*.08f));
   if(!valid||s.matrix[1]!=previous.matrix[1])send(3,4,s.matrix[1]*.01f);
   if(!valid||s.matrix[2]!=previous.matrix[2])send(3,6,s.matrix[2]>=50);
   if(!valid||s.matrix[3]!=previous.matrix[3])send(3,8,(int(s.matrix[3])-50)*.24f);
   if(!valid||s.matrix[4]!=previous.matrix[4])send(3,3,s.matrix[4]>=50);
   if(bpm!=tempo)send(3,7,bpm);
   for(int node=0;node<8;node++){
    auto&n=s.nodes[node];if(!valid||n[0]!=previous.nodes[node][0])send(2,node,std::lround(n[0]*(node+2)*.01f)-1);
    for(int p=0;p<13;p++)if(!valid||n[p+1]!=previous.nodes[node][p+1]){
     float u=n[p+1]*.01f,value=u;
     switch(p){case 0:value=1500*u*u;break;case 1:case 12:value=2*u-1;break;case 2:value=.97f*u;break;case 3:value=(u-.5f)*24;break;case 4:value=200*std::pow(100.f,u);break;case 5:value=20*std::pow(100.f,u);break;case 6:value=(u-.5f)*24;break;case 9:value=1000*u;break;case 10:value=5*u*u;break;}
     send(1,node,p+1,value);
    }
   }
  }else if(type==FxType::CrushClone){
   for(int p=0;p<12;p++){
    if(p==10){if(!valid)send(1,11,1);continue;}
    int i=p>10?p-1:p;if(valid&&s.crush[i]==previous.crush[i])continue;
    float u=s.crush[i]*.01f,v=u;
    if(p==3)v=20*std::pow(1000.f,u);if(p==2||p==5||p==7||p==8)v=u>=.5f;if(p==9)v=(u-.5f)*36;if(p==11)v=u*36;
    send(1,p+1,v);
   }
  }else{
   if(!valid||s.frequalizer[0]!=previous.frequalizer[0])send(1,1,s.frequalizer[0]*.02f);
   if(!valid||s.frequalizer[1]!=previous.frequalizer[1])send(2,std::lround(s.frequalizer[1]*.06f));
   for(int band=0;band<6;band++)for(int f=0;f<5;f++){
    int i=2+band*5+f;if(valid&&s.frequalizer[i]==previous.frequalizer[i])continue;float u=s.frequalizer[i]*.01f,v=u;
    if(f==0)v=std::lround(u*11);if(f==1)v=20*std::pow(1000.f,u);if(f==2)v=.1f*std::pow(100.f,u);if(f==3)v=std::pow(10.f,(u-.5f)*2.4f);if(f==4)v=u>=.5f;send(1,i,v);
   }
  }
  previous=s;valid=true;tempo=bpm;
 }
};
PortedProcessor::PortedProcessor(FxType t):impl(std::make_unique<Impl>(t)){}
PortedProcessor::~PortedProcessor()=default;
void PortedProcessor::configure(const InsertSettings&s,int bpm){impl->configure(s,bpm);}
void PortedProcessor::clear(){if(impl->native)impl->native->clear();else impl->send(4,0);}
void PortedProcessor::process(float *data,int frames,float mix){auto &p=*impl;
 if(p.native){float dry[64];for(int at=0;at<frames;at+=32){int n=std::min(32,frames-at);float*x=data+at*2;std::copy(x,x+n*2,dry);p.native->process(x,n);
  for(int i=0;i<n*2;i++){float v=dry[i]+(x[i]-dry[i])*mix;x[i]=std::isfinite(v)?std::clamp(v,-4.f,4.f):0.f;}}return;}
for(int at=0;at<frames;at+=32){int n=std::min(32,frames-at);for(int i=0;i<n;i++){p.inL[i]=data[2*(at+i)];p.inR[i]=data[2*(at+i)+1];}p.engine->render(p.inL,p.inR,p.outL,p.outR,n);for(int i=0;i<n;i++){float l=p.inL[i]+(p.outL[i]-p.inL[i])*mix,r=p.inR[i]+(p.outR[i]-p.inR[i])*mix;data[2*(at+i)]=std::isfinite(l)?std::clamp(l,-4.f,4.f):0;data[2*(at+i)+1]=std::isfinite(r)?std::clamp(r,-4.f,4.f):0;}}}
}
