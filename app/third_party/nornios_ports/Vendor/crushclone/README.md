# CrushClone

doxiedevops' [CrushClone](https://github.com/doxiedevops/CrushClone), a recreation of CamelCrusher, for NorniOS's
native `CrushClone` engine (`Engines/CrushCloneEngine.cpp`, docs/adr/0061-crushclone.md). MIT (`LICENSE`).

Upstream: https://github.com/doxiedevops/CrushClone commit aaa5a0ce7de56a4ab4fab56be4a11c2fed79c772 (2026-04-18).

| here | what it is |
|---|---|
| `Source/DSP/` | upstream's DSP, **unmodified**: the tube and mechanical distortion with its 4x oversampler, the resonant TPT filter, and the one-knob compressor with its phat parallel mode. It compiles against `../juce-shim` |
| `Source/PluginProcessor.{h,cpp}` | upstream's processor, **for reference only: not compiled.** Its chain, presets, master, dry/wet and safety limiter are carried over in the engine file |

Left out: the editor (`Source/PluginEditor`, `CrushCloneThemeV3.jsx`) and the installers.

The engine adds what upstream has not: a trim in front of the plugin and as much off again after it. The plugin is
written for a DAW, where material sits near full scale — its distortion only saturates there, and its compressor's
threshold runs from 0 to -30 dBFS — while a module here is tens of dB quieter (docs/adr/0059-chow-trim.md).

To update: copy `Source/DSP` and `Source/PluginProcessor.*` from the new commit, compare the processor's chain and
presets with the engine's and the script's, and run `NORNIOS_TEST_ONLY=crushclone make test`.
