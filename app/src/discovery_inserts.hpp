#pragma once
#include "core.hpp"
#include "../third_party/nornios/EngineRegistry.hpp"
namespace amalthea {
std::unique_ptr<nornios::Engine> makeDiscoveryInsert(FxType);
void configureDiscoveryInsert(nornios::Engine&, FxType, const InsertSettings&);
}
