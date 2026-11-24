// NorniOS stand-in for ChowMatrix's DelayStore (Vendor/chowmatrix/src/dsp/Delay/DelayStore.h; docs/adr/0087):
// upstream keeps eight delay lines of 2^19 samples building on other threads, so that a node added from its editor
// is ready at once; each is nine delay lines of eleven seconds, some 24 MB. A node here reaches 1.5 s and 0.25 s of
// modulation, and nodes are made before the audio starts, so a delay is made when it is asked for, 2^17 samples long
// (2.7 s at 48 kHz): the same VariableDelay, a quarter of the memory.
#pragma once

#include "../src/dsp/Delay/VariableDelay.h"

class DelayStore
{
public:
    VariableDelay* getNextDelay()
    {
        auto newDelay = std::make_unique<VariableDelay> (1 << 17);
        newDelay->prepare ({ 48000.0, 512, 1 });
        newDelay->reset();
        return newDelay.release();
    }
};
