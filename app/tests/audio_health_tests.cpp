#include "audio_health.hpp"
#include <cstdlib>
#include <cstring>
#include <string>
using namespace amalthea;
static void require(bool ok,const char*message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
int main(){
 AudioHealthHistory<3> trace;require(trace.empty(),"starts empty");
 for(int i=0;i<5;i++){AudioHealthRecord r;r.seconds=i;r.event=i==3?"capture_error":"health";r.status=i==3?-32:0;r.frames=i*44100;r.captureXruns=i>=3;trace.push(r);}
 FILE*f=tmpfile();require(f&&trace.write(f),"bounded trace serializes");rewind(f);char line[1024];require(fgets(line,sizeof line,f),"CSV header exists");
 std::string rows;while(fgets(line,sizeof line,f))rows+=line;fclose(f);
 require(rows.find("0.000000000,")==std::string::npos&&rows.find("1.000000000,")==std::string::npos,"evicts oldest rows");
 auto a=rows.find("2.000000000,health"),b=rows.find("3.000000000,capture_error,-32,132300,1"),c=rows.find("4.000000000,health");
 require(a!=std::string::npos&&a<b&&b<c,"retains chronological fault and cumulative counter context after wrap");
 trace.clear();require(trace.empty(),"connection traces can be separated");require(!trace.write(nullptr),"invalid output reported");
 puts("PASS: bounded fault history preserves ordering, errno and counter context");
}
