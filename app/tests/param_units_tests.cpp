// Effect parameter values read in their own units, not percentages.
#include "core.hpp"
#include "macro_settings.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using namespace amalthea;
static void check(bool b,const std::string&s){if(!b){fprintf(stderr,"FAIL: %s\n",s.c_str());exit(1);}}
static void same(const std::string&a,const char*b,const char*what){check(a==b,std::string(what)+": got '"+a+"' want '"+b+"'");}
// A bare percentage is what this replaces; "% WET", "% DUTY", "% RATE", "% CHANCE", "% STEP" and "% LENGTH" are real quantities.
static bool bare(const std::string&v){auto p=v.find('%');return p!=std::string::npos&&v.find("% WET")==std::string::npos&&v.find("% DUTY")==std::string::npos&&v.find("% RATE")==std::string::npos&&v.find("% CHANCE")==std::string::npos&&v.find("% STEP")==std::string::npos&&v.find("% LENGTH")==std::string::npos;}
int main(int argc,char**argv){
  bool dump=argc>1&&!strcmp(argv[1],"--dump");
  int checked=0;
  for(auto type:selectableEffects()){
    if(type==FxType::None||type==FxType::Random)continue;
    InsertSettings s;int pages=type==FxType::ChowMatrix?9:type==FxType::Frequalizer?6:type==FxType::Biztortion?8:1;
    for(int page=0;page<pages;page++){
      if(type==FxType::Biztortion)s.biz[page].module=uint8_t(page%5*20+20);
      for(int f=0;f<insertParameterCount(type,page,s);f++)for(int v:{0,25,50,75,100}){
        auto text=insertParameterDescription(type,f,page,s,v);
        if(dump&&(v==0||v==50||v==100))printf("%s p%d %s @%d = %s\n",fxName(type),page,insertParameterName(type,f,page,s),v,text.c_str());
        check(!text.empty(),std::string(fxName(type))+" "+insertParameterName(type,f,page,s)+" has a value");
        check(!bare(text),std::string(fxName(type))+" "+insertParameterName(type,f,page,s)+" is not a bare percentage: "+text);
        checked++;
      }
    }
  }
  for(int m=0;m<MacroCount;m++)for(int p=0;p<MacroParameters;p++)for(int v:{0,50,100}){
    auto text=macroValueText(m,p,v);if(dump)printf("%s %s @%d = %s\n",macroName(m),macroParameterName(m,p),v,text.c_str());
    check(!text.empty()&&!bare(text),std::string(macroName(m))+" "+macroParameterName(m,p)+": "+text);checked++;
  }
  for(int page=0;page<MasterPages;page++){int module=masterPageModule(page);
    for(int p=0;p<masterParameterCount(module);p++)for(int v:{0,50,100}){
      auto text=masterValueText(module,p,v);if(dump)printf("%s %s @%d = %s\n",masterName(module),masterParameterName(module,p),v,text.c_str());
      check(!text.empty()&&!bare(text),std::string(masterName(module))+" "+masterParameterName(module,p)+": "+text);checked++;
    }}
  // Spot values against the DSP formulas.
  InsertSettings s;
  same(insertParameterDescription(FxType::Filter,2,0,s,50),"600 Hz","filter cutoff 20*900^u");
  same(insertParameterDescription(FxType::Filter,5,0,s,0),"-12.0 dB","filter level");
  same(insertParameterDescription(FxType::Space,1,0,s,0),"500 ms","space decay");
  same(insertParameterDescription(FxType::Clouds,9,0,s,100),"+48 st","clouds pitch");
  same(insertParameterDescription(FxType::Frequalizer,6,0,s,100),"+24.0 dB","frequalizer band gain");
  same(insertParameterDescription(FxType::Rotor,2,0,s,0),"10.0 Hz","rotor rate");
  same(insertParameterDescription(FxType::Filter,0,0,s,35),"35% WET","dry/wet mix");
  same(masterValueText(7,1,50),"10.5:1","compressor ratio");
  same(masterValueText(7,2,0),"0.10 ms","compressor attack");
  same(masterValueText(8,1,100),"2.00 s","delay time");
  same(masterValueText(9,6,0),"100 Hz","reverb low frequency");
  same(masterValueText(10,1,50),"C","output balance");same(masterValueText(10,1,0),"-50 L","balance left");same(masterValueText(10,1,75),"+25 R","balance right");
  same(macroValueText(1,0,0),"16 bit","corrosion bit crush");
  same(macroValueText(2,2,100),"10.0 Hz","modulation rate");
  printf("%d effect values checked\n",checked);
  return 0;
}
