// NorniOS stand-in for ChowMatrix's precompiled header (Vendor/chowmatrix/reference/pch.h; docs/adr/0087-chowmatrix.md):
// JUCE (Vendor/juce-shim), the chowdsp_utils pieces ChowMatrix's delay uses (Vendor/chowmatrix/chowdsp_dsp,
// unmodified, in chowdsp_dsp.h's order), and what upstream's pch.h adds of its own. The plugin shell (ProcessorBase,
// an AudioProcessor) is not needed: the engine carries the nodes' processing itself.
#pragma once

#include <deque>
#include <future>
#include <random>
#include <unordered_map>

#include <JuceHeader.h>

using namespace juce;

#include "../chowdsp_dsp/SIMD/chowdsp_SampleTypeHelpers.h"
#include "../chowdsp_dsp/SIMD/chowdsp_SIMDUtils.h"
#include "../chowdsp_dsp/SIMD/chowdsp_SIMDComplex.h"
#include "../chowdsp_dsp/Delay/chowdsp_DelayInterpolation.h"
#include "../chowdsp_dsp/Delay/chowdsp_DelayLine.h"
#include "../chowdsp_dsp/Delay/chowdsp_BBDFilterBank.h"
#include "../chowdsp_dsp/Delay/chowdsp_BBDDelayLine.h"
#include "../chowdsp_dsp/Delay/chowdsp_BBDDelayWrapper.h"
#include "../chowdsp_dsp/Delay/chowdsp_PitchShift.h"
#include "../chowdsp_dsp/Filters/chowdsp_IIRFilter.h"
#include "../chowdsp_dsp/Filters/chowdsp_juce_IIRFilter.h"
#include "../chowdsp_dsp/Other/chowdsp_Panner.h"
#include "../chowdsp_dsp/Sources/chowdsp_SineWave.h"

#include "DelayStore.h"   // NorniOS: see DelayStore.h here
#include "../src/dsp/Distortion/LookupTables.h"
#include "../src/dsp/ProcessorChain.h"
