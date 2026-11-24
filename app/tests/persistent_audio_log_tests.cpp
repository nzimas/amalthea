#include "persistent_audio_log.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
using namespace amalthea;
static void check(bool ok,const char*s){if(!ok){fprintf(stderr,"FAIL %s\n",s);exit(1);}}
static std::string csv(const std::string&dir){std::string s;for(auto&i:std::filesystem::directory_iterator(dir))if(i.path().extension()==".csv"){std::ifstream f(i.path());s.append(std::istreambuf_iterator<char>(f),{});}return s;}
int main(){char dir[]="/tmp/amalthea-durable-XXXXXX";check(mkdtemp(dir),"temporary directory");
 {PersistentAudioLog log(dir);for(int n=0;n<100;n++){AudioHealthRecord r;r.seconds=n;r.event="test_event";r.frames=n;check(log.push(r,"safe,detail\ntext"),"queue capacity");}check(!log.dropped(),"no dropped events");}
 auto first=csv(dir);check(first.find("99.000000000,test_event")!=std::string::npos,"clean exit drains queued tail");check(first.find("safe detail text")!=std::string::npos,"CSV details sanitized");
 pid_t pid=fork();check(pid>=0,"fork crash simulation");if(!pid){PersistentAudioLog log(dir);AudioHealthRecord r;r.seconds=123;r.event="before_abrupt_power";log.push(r);for(;;)pause();}
 bool checkpoint=false;for(int n=0;n<40;n++){std::this_thread::sleep_for(std::chrono::milliseconds(100));if(csv(dir).find("before_abrupt_power")!=std::string::npos){checkpoint=true;break;}}
 kill(pid,SIGKILL);int status=0;waitpid(pid,&status,0);check(checkpoint,"checkpoint exists before crash");check(csv(dir).find("before_abrupt_power")!=std::string::npos,"unclosed process retains checkpoint");
 {PersistentAudioLog log(dir);AudioHealthRecord r;r.event="after_restart";log.push(r);}
 auto last=csv(dir);check(last.find("after_restart")!=std::string::npos&&last.find("before_abrupt_power")!=std::string::npos,"new run preserves previous run");
 std::filesystem::remove_all(dir);puts("PASS durable logs survive unclean process termination and separate restarts");}
