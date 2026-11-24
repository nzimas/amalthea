# Native insert ports, 0.18.0

The release adds nine inserts to both FX lanes. Settings and DSP instances belong
to one track/slot. Event values control the external dry/wet mix. The detailed
editing page was removed in build 16. Every new
processor has at least four independent sound controls in addition to mix.

| Insert | Implementation / source | Controls |
| --- | --- | --- |
| QDelay | Nornios adapter of [tiagolr/qdelay](https://github.com/tiagolr/qdelay), GPL-3.0 | All native parameter controls except internal mix; external step mix instead |
| ChowMatrix | Nornios eight-node adapter of [ChowMatrix](https://github.com/Chowdhury-DSP/ChowMatrix), BSD-3 core / GPL-3 DSP utilities | Global interpolation, insanity, sync, level, gain compensation; eight nodes with routing, delay, pan, feedback, filters, pitch, diffusion, distortion, reverse and modulation |
| CrushClone | Nornios adapter of [CrushClone](https://github.com/doxiedevops/CrushClone), MIT | Tube/mechanical distortion, resonant filter, compression, phat mode, level, trim and section switches |
| Frequalizer | Nornios native arrangement of [Frequalizer](https://github.com/ffAudio/Frequalizer), BSD-3 | Six bands, twelve filter types, frequency, Q, gain, active/solo and output level |
| Rings | Emilie Gillet's original [Rings modal resonator](https://github.com/pichenettes/eurorack/tree/master/rings/dsp), MIT, sourced from local Vascular vendor | Tuning, structure, brightness, damping, position, 2–64 modes, level |
| Fracture 2 | Native core from [Airwindows Fracture2](https://github.com/airwindows/airwindows/tree/master/plugins/LinuxVST/src/Fracture2), MIT | Drive, 0–8 fracture stages, threshold and level |
| Crazy Ivan | Native stereo wrapper around [Smartelectronix CrazyIvan](https://github.com/bdejong/smartelectronix/tree/master/crazyivan), GPL | Bitmask logic, companding, moving delay, vibrato, nonlinear feedback flanger, damping, limiter, attack/release, level |
| Bitmurderer | Native [Smartelectronix Bitmurderer](https://github.com/bdejong/smartelectronix/tree/master/bitmurderer), GPL | Fifteen individual bit switches, AND, XOR, sigmoid and level |
| Lo-Fi | Native adaptation of Nornios `Bundled/dust/code/pedalboard/lib/fx/LoFi.sc`, GPL-3.0 | Drive, tone, wow, source-gated noise and level |
| Flanger (build 25) | Plain C++ port of the [Surge XT flanger](https://github.com/surge-synthesizer/sst-effects/blob/main/include/sst/effects/Flanger.h) (sst-effects), GPL-3.0-or-later, `third_party/surge_flanger` | Mode (dry + combs, combs only, arpeggiated combs), six LFO shapes, rate, depth, 1–4 comb voices tuned from a base pitch with semitone spacing, feedback, feedback low cut, width, bipolar comb mix |
| Phaser (build 25) | Plain C++ port of [ChowPhaser](https://github.com/jatinchowdhury18/ChowPhaser), BSD-3, `third_party/chowphaser` | 1–50 fractional all-pass stages, saturated feedback, modulation blend, LFO rate (x10 range), LFO depth, light-curve skew, three feedback drives, stereo LFO offset |

## Deliberate adaptations

These are native insert DSP integrations, not hosted plugins. No plugin framework
or SuperCollider runtime is linked. The existing Nornios compatibility header
provides the required buffer/filter primitives. Vendor licenses are retained.

Rings is the external-input modal resonator, not the whole Rings synthesizer and
its internal exciter/string models. Its stereo channels have independent filters.
Crazy Ivan preserves its original moving-delay/flanger algorithm but uses two
independent channels instead of copying the left output to both channels. Delay
storage is bounded to the algorithm's maximum; oscillator rate and envelope state
are explicitly initialized. Upstream's UI-only randomize command is not exposed.
Bitmurderer's upstream label “OR” actually performs XOR; the tracker correctly
labels it XOR. Exact digital zero bypasses the bit operator so it cannot generate
idle DC. Fracture2 omits denormal noise/dither and bounds exponential intermediate
values to avoid overflow at extreme stages. Lo-Fi reimplements the SuperCollider
chain's compression, wow, saturation, noise, tone filtering and decimation with
native fixed storage; the filter/noise generator are adaptations, not bit-identical
SuperCollider emulations. Its noise follows input activity instead of running
continuously during silence.

QDelay tap time is bounded to six seconds (including synchronized times), with
pre-reserved 2x-rate storage. Long dotted values at slow tempos reach that ceiling.
ChowMatrix keeps its eight-node routing and nine interpolation modes; unused zeroed
interpolation stores are released from resident memory on Linux. The upstream
per-block filter allocations are replaced by coefficient updates in owned storage.
Frequalizer's analyser/plot computation is omitted from the audio path. CrushClone's
Phat parallel buffer is preallocated; clearing resets every filter and oversampler.

## Validation

The dedicated test exercises all nine effects with real stereo signals, independent
instances, silent input, extreme parameters, state clearing, and allocation counting
through both rendering and live parameter changes. AMAL17 roundtrips distinct
profiles across every track/slot. All 27 Mac and ARM Linux regression suites passed. The dedicated DSP tests also
passed on the Brick itself, and the final parameter sweep passed AddressSanitizer.
Rings table coordinates are bounded below the end sentinel at a displayed 100%.
Hardware listening remains the owner's final evaluation of the sound.

The update does not save or restore a live session. Application startup is blank
and stopped; existing saved sessions remain untouched.

## Flanger and Phaser (build 25)

Both run in the ported-insert family (one instance per track and slot, created when a
row uses it) and render fully wet; they start at 100% WET because each already blends
its own dry path to form the notches. The Surge flanger's 32-sample block maths is kept
by scaling every per-block rate to the call length. ChowPhaser's stereo pair shares one
parameter set, the right LFO offset by STEREO. Random draws stay inside sweeping ranges
(flanger 0.13–8 Hz, delays of roughly 2–30 ms; phaser 0.36–4.8 Hz with real depth) so
every draw moves audibly. `tests/modulation_insert_tests.cpp` checks the sweep depth on
steady tones, decay to silence, extreme settings and CPU cost. AMAL22 stores both
profiles per track and slot; older sessions load with the defaults.
