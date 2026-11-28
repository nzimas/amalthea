#include "ui.hpp"
#include <filesystem>
#include <fstream>
using namespace amalthea;
// render_identity OUTPUT_DIRECTORY: splash and home PPMs plus the SVG masters of
// the logo and the appliance icons, all from the geometry in src/brand.cpp.
int main(int argc,char**argv){
  if(argc!=2)return 2;std::string dir=argv[1];std::filesystem::create_directories(dir);
  AudioStats stats;Editor e;e.battery=78;Canvas c(1024,768);
  e.splash=true;drawUI(c,e,stats);writePPM(c,dir+"/splash.ppm");
  e.splash=false;e.page=Page::Appliances;drawUI(c,e,stats);writePPM(c,dir+"/home.ppm");
  std::ofstream(dir+"/amalthea.svg")<<amaltheaMarkSVG(0,0xe8603c,"Amalthea horn of plenty");
  std::ofstream(dir+"/tracker.svg")<<amaltheaMarkSVG(1,0x6ee7dc,"Amalthea Tracker");
  std::ofstream(dir+"/fx.svg")<<amaltheaMarkSVG(2,0xb39aff,"Amalthea FX Rack");
}
