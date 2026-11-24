// SPDX-License-Identifier: GPL-3.0-only
// Text for effect parameter values in the units the DSP actually uses.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>
namespace amalthea::units {
inline std::string format(const char *f,double v){char b[32];std::snprintf(b,sizeof b,f,v);return b;}
inline std::string hz(double f){
 if(f>=10000)return format("%.1f kHz",f/1000);
 if(f>=1000)return format("%.2f kHz",f/1000);
 if(f>=100)return format("%.0f Hz",f);
 if(f>=10)return format("%.1f Hz",f);
 return format("%.2f Hz",f);
}
inline std::string seconds(double s){
 if(s<=0)return "0 ms";
 if(s>=10)return format("%.1f s",s);
 if(s>=1)return format("%.2f s",s);
 double ms=s*1000;
 if(ms>=100)return format("%.0f ms",ms);
 if(ms>=10)return format("%.1f ms",ms);
 return format("%.2f ms",ms);
}
inline std::string decibels(double db){
 if(db<-120)return "-INF dB";
 if(std::fabs(db)<.05)return "0.0 dB";
 return format(std::fabs(db)>=10?"%+.1f dB":"%+.2f dB",db);
}
inline std::string gain(double linear){return linear<=1e-6?"-INF dB":decibels(20*std::log10(linear));}
inline std::string semitones(double st){if(std::fabs(st)<.05)return "0 st";return format(std::fabs(st-std::round(st))<.05?"%+.0f st":"%+.1f st",st);}
inline std::string ratio(double r){return format(r>=10?"%.1f:1":"%.2f:1",r);}
inline std::string wet(int v){return v<=0?"DRY":v>=100?"100% WET":std::to_string(v)+"% WET";}
inline std::string plain(double v){return format(std::fabs(v-std::round(v))<1e-3?"%.0f":"%.2f",v);}
// A bipolar position: "0" at the centre, signed elsewhere.
inline std::string bipolar(double v,int decimals=0){double e=decimals?.005:.5;if(std::fabs(v)<e)return decimals?"0.00":"0";return format(decimals?"%+.2f":"%+.0f",v);}
inline std::string bits(double b){return format("%.0f bit",b);}
inline std::string degrees(double d){return format("%.0f deg",d);}
inline std::string multiple(double x){return format(x>=10?"x%.1f":"x%.2f",x);}
// Position in the stereo image from a 0..100 setting: -50 L ... C ... +50 R.
inline std::string pan(int v){if(v==50)return "C";return v<50?"-"+std::to_string(50-v)+" L":"+"+std::to_string(v-50)+" R";}
}
