// Minimal host contract for the unchanged Nornios Biztortion DSP. GPL-3.0-only.
#pragma once
#include <memory>
#include <string>
#include <vector>
namespace nornios {
struct CommandDef {std::string name,format;};
struct PollDef {std::string name;bool periodic;};
struct EngineCommand {int id=0;float args[8]{};};
class Engine {
public:
 virtual ~Engine()=default;
 virtual const char *name()const=0;
 virtual const std::vector<CommandDef>&commands()const=0;
 virtual const std::vector<PollDef>&polls()const=0;
 virtual void prepare(double,int)=0;
 virtual void command(const EngineCommand&)=0;
 virtual void render(const float*,const float*,float*,float*,int)=0;
 virtual bool pollValue(int,float&)=0;
 virtual int pollData(int,float*,int){return 0;}
};
std::unique_ptr<Engine> makeBiztortionEngine();
std::unique_ptr<Engine> makeQDelayEngine();
std::unique_ptr<Engine> makeChowMatrixEngine();
std::unique_ptr<Engine> makeCrushCloneEngine();
std::unique_ptr<Engine> makeFrequalizerEngine();
}
