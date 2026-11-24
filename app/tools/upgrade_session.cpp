#include "core.hpp"
#include <cstdio>
using namespace amalthea;
int main(int argc,char **argv) {
  if(argc!=3){fprintf(stderr,"usage: upgrade_session input output (unmutes all tracks)\n");return 2;}
  Project p;std::string error;
  if(!loadProject(p,argv[1],error)){fprintf(stderr,"%s\n",error.c_str());return 1;}
  for(auto &t:p.tracks)t.mute=false;
  if(!saveProject(p,argv[2],error)){fprintf(stderr,"%s\n",error.c_str());return 1;}
  puts("Upgraded session; notes, FX and conditions retained; all tracks unmuted.");
}
