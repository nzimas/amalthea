# QDelay

tiagolr's [QDelay](https://github.com/tiagolr/qdelay), a dual delay plugin, for NorniOS's native `QDelay` engine
(`Engines/QDelayEngine.cpp`, docs/adr/0053-qdelay.md). GPL 3 (`LICENSE`).

Upstream: https://github.com/tiagolr/qdelay commit 3130de7bbbc676b1c69b4dcfc8baa63851b57035 (2026-09-17).

| here | what it is |
|---|---|
| `src/dsp/`, `src/Globals.h` | upstream's DSP, **unmodified**: the delay, diffusor, distortion, crusher, flutter, wow, follower, phaser, pitch and frequency shifters, filters |
| `presets/` | upstream's factory presets (`resources/presets`); `Tools/qdelay-presets.py` writes them as the Lua table the script loads |
| `src/PluginProcessor.h` | **NorniOS's**: upstream's processor class without JUCE's plugin framework — the parameter tree, tempo and transport the DSP reads |
| `../juce-shim/JuceHeader.h` | **NorniOS's**: the little of JUCE the DSP uses (maths constants, `Random`, `dsp::FFT`, the parameter listener interface), behaving as JUCE's |
| `shim/Oversampling2x.h` | **NorniOS's**: the 2x half-band polyphase IIR oversampler the plugin asks JUCE for, from JUCE's design, with its integer-latency allpass |

Left out: the editor (`src/ui`, `PluginEditor`), the preset manager, the settings file and the spectrum display's
buffer. The signal path of `PluginProcessor.cpp` (`prepareToPlay`, `onSlider`, `clearAll`, `processBlock`) is carried
over statement for statement in `Engines/QDelayEngine.cpp`.

To update: copy `src/dsp`, `src/Globals.h` and `resources/presets` from the new commit, compare upstream's
`createParameterLayout` and `processBlock` with the engine's, run `python3 Tools/qdelay-presets.py`, and
`NORNIOS_TEST_ONLY=qdelay make test`.
