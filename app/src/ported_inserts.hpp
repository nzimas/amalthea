#pragma once
#include "core.hpp"
#include <memory>
namespace amalthea {
constexpr int PortedTypes=11+NativeCount;   // the eleven ports, then every native effect
int portedIndex(FxType);
FxType portedType(int kind);   // inverse of portedIndex
class PortedProcessor {
public:
 explicit PortedProcessor(FxType);~PortedProcessor();
 void configure(const InsertSettings&,int bpm);
 void clear();
 void process(float*interleaved,int frames,float mix);
private:
 struct Impl;std::unique_ptr<Impl> impl;
};
using PortedPointers=std::array<PortedProcessor*,PortedTypes>;
}
