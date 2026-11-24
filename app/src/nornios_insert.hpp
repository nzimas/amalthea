// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "core.hpp"
#include <memory>
namespace amalthea {
class NorniosInsert {
public:
 NorniosInsert();~NorniosInsert();
 void clear();
 void setGrainLimit(int);
 void configure(FxType,const InsertSettings &);
 void process(FxType,float mix,float *interleaved,int frames);
private:
 struct Impl;std::unique_ptr<Impl> impl;
};
}
