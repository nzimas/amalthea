# ChowMatrix

Jatin Chowdhury's [ChowMatrix](https://github.com/Chowdhury-DSP/ChowMatrix) (BSD-3, `LICENSE`), a delay effect whose
delays grow as trees from the input channels, for NorniOS's native `ChowMatrix` engine
(`Engines/ChowMatrixEngine.cpp`, docs/adr/0087-chowmatrix.md). Upstream: commit 40d8e0e.

- `src/dsp/` — upstream's delay processing, compiled **unmodified**: DelayProc, VariableDelay (its nine delay types),
  Diffusion, Distortion and its lookup tables, PitchShiftWrapper, Reverser, TempoSyncUtils, ParamHelpers.h (its
  constants), ProcessorChain.h. `DelayStore.h` there is upstream's too, but not used (see nornios/).
- `chowdsp_dsp/` — the chowdsp_utils (GPL-3, `chowdsp_dsp/LICENSE`, commit c34df55) it was written against, unmodified:
  delay lines and their interpolations, the BBD models, the pitch shifter, IIR filters, panner, sine LFO, SIMD helpers.
- `polylogarithm/` — Alexander Voigt's Li2 (MIT), which the distortion's anti-aliasing tables use, as upstream
  vendors it.
- `nornios/pch.h` — a stand-in for upstream's precompiled header: JUCE (Vendor/juce-shim), the chowdsp pieces above,
  and upstream's shared headers.
- `nornios/DelayStore.h` — NorniOS's: upstream's keeps eight 2^19-sample delays building on other threads for its
  editor; here a delay is made when a node is, 2^17 samples long.
- `reference/` — the nodes, the tree, the plugin and its controls (Insanity, delay type, sync), which the engine
  carries over statement for statement. **Not compiled.**

NorniOS change to upstream's DSP: `src/dsp/Delay/VariableDelay.cpp` skips copying state out of a BBD delay when the
delay type changes (a BBD keeps no buffer to copy, and the copy freed the buffer the new line still used; marked
NorniOS, docs/adr/0088-chowmatrix-clicks.md).
