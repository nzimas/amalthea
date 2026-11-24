# Amalthea audio effects and references

Current behavior: version0.17.0,21 November2026. The archived sections below record
older implementations and should not be used as the current effect menu.

FX1 and FX2 exclusively select insert processors. Their independent column clocks
and conditions remain active. NONE bypasses a slot. Pan, volume and all five sends
are note-event controls, not insert types. Diode and Rat Drive are discontinued;
old low/high-pass entries are replaced by the new multimode filter.

| Code | Current insert | Step value |
| --- | --- | --- |
| WF | Wavefold | Dry/wet (fixed character) |
| FZ | Fuzz | Dry/wet (fixed character) |
| RX | Rectify | Dry/wet (fixed character) |
| TM | Tremolo | Dry/wet (fixed character) |
| ST | Stutter | Dry/wet (fixed character) |
| CL | Clouds granular | Dry/wet |
| AM | Amp simulator | Dry/wet |
| BZ | Biztortion | Dry/wet |
| RO | Rotor | Dry/wet |
| CS | CloudSeed Space | Dry/wet |
| MF | Multimode filter | Dry/wet |

Build 20: every FX step value is its dry/wet mix, and every FX cell has a LENGTH in
FX-column steps. Space, Clouds, QDelay, ChowMatrix, Rings and Crazy Ivan ring out
when their effect ends or is replaced (see the README).

The detailed insert page was removed in build 16. Multimode Filter offers Mix, Mode,
Cutoff, Resonance, Drive and Level. Modes are low pass, band pass, high pass,
notch, all pass and peak. Its native stereo TPT SVF has smoothed coefficients,
matched15-tap half-band FIRs around2x processing, and seven-sample aligned dry.
LP DC gain remains1 for everyQ; there is no resonance-dependent bass attenuation.
A resonant peak still needs headroom; the existing master limiter remains active.
HP/BP intentionally reject bass. The design follows the equations in
[Andrew Simper's primary SVF paper](https://www.cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf),
implemented directly without new runtime dependencies.

Enter on NOTE opens Value, Condition, Pan, Volume, Delay Send, Reverb Send,
Abrasion Send, Corrosion Send and Modulation Send. Only two tabs appear at once;
F1/F2 traverses them. Up/down sets percentages, Left restores track inheritance,
Right captures the track default as an explicit override. Every changed event
previews immediately, including instrument/FX/modifier edits. Enter commits;
Cancel/Home discards pending changes. Preview never starts the transport.

Ordinary hold X on NOTE generates a melodic sequence while preserving controls.
Shift+hold X generates note conditions and all seven controls too; generated volume
stays10–100%. Density uses the active NOTE-column length, and hidden rows remain
untouched. Configurable per-parameter random ranges are deferred.

See the [unported Nornios inventory and GitHub candidate research](EFFECT-CANDIDATES-2026-11-21.md)
for the next selection. No additional candidate ports have been chosen by the owner.

## Historical references and implementation notes

## Local references inspected

- `../mvave-digirust/firmware/src/voice.c`: Bigorna's valve,
  RAT and fold overdrive descriptions and implementations, including the tonal filtering,
  asymmetric stages and DC management. Amalthea's compact float algorithms are inspired by
  those approaches; they do not reproduce the full Bigorna circuits or every parameter.
- `../tonitor-smk37/firmware/src/`: local Tonitor source.
  Bigorna explicitly describes its valve design as following Tonitor's distortion.
- `../mvave-digirust/firmware/src/tracks.c`: cycle-counted A:B
  conditions. Amalthea implements occurrence/period conditions without probability.
- `../fugitive/firmware/src/fugue.c`: copied and compiled subject
  generator, with its rhythm cells, weighted intervals, strong-beat harmony, contour,
  leap recovery and cadence. The Amalthea adapter adds a repeated opening-cell answer.
  Four styles (lyrical, groove, minimal, baroque) are selected deterministically by seed;
  the current scale is C minor. This is not a Polyend melody-generator port.

## GitHub research and further candidates

Inspected primary source, rather than third-party plugin reviews:

- [Airwindows HardVacuum](https://github.com/airwindows/airwindows/blob/master/plugins/LinuxVST/src/HardVacuum/HardVacuumProc.cpp):
  multistage drive and signal-slope-dependent skew; a candidate for more animated industrial
  drive. Version 0.3 adapts its shaping section for master saturation, with fixed warmth/aura and a single 0–100 drive. VST plumbing, denormal noise and dither are omitted; this is not a bit-identical full plugin port. MIT notice is bundled at third_party/airwindows/LICENSE.
- [Airwindows DeRez3](https://github.com/airwindows/airwindows/blob/master/plugins/LinuxVST/src/DeRez3/DeRez3Proc.cpp):
  independent rate/bit degradation, parameter interpolation and wet/dry control; a useful
  next step beyond the current simple BC/SR processors. Source identifies MIT licensing.
  Not imported into this release.
- [Airwindows Galactic2](https://github.com/airwindows/airwindows/blob/master/plugins/LinuxVST/src/Galactic2/Galactic2Proc.cpp):
  a much larger feedback/diffusion reverb design, worth profiling for expansive ambience.
  Source identifies MIT licensing. Not imported into this release.
- [Surge SST Bonsai](https://github.com/surge-synthesizer/sst-effects/blob/main/include/sst/effects/Bonsai.h):
  an extensive processor with filtering/nonlinearity stages and smoothed parameters;
  a candidate for more developed coloration. Source identifies GPL-3.0-or-later licensing.
  It needs its SST dependencies and adaptation/profiling; not imported into this release.

## Conditional modifications

[Woovebox's quick-start guide](https://www.woovebox.com/support/guides--tutorials/quick-start/conditional-triggering)
and [its modification reference](https://www.woovebox.com/support/guides--tutorials/conditional-triggering/do-step-modifications)
distinguish conditional-only playback from normal playback with a change on matching cycles.
Amalthea adopts that distinction, without claiming the entire Woovebox command set:

Each NOTE, INST, FX1, FX2 or MOD cell has its own condition. Enter opens Value;
F2 opens Condition and F1 returns to Value. Up/down selects positive A:B conditions
through8:8; Enter commits, Cancel/Home discards. No shared COND column remains.

- NOTE1:2 plays on cycles1,3,5... of the NOTE column.
- INST2:3 applies its instrument on cycles2,5,8... of INST; otherwise track default.
- FX1:2 applies the selected effect on alternate FX-column cycles; otherwise track defaults.
- MOD+02 with2:2 transposes by two semitones on alternate MOD-column cycles.

Different column clocks/lengths/directions combine with independent conditions.
Conditions do not nest against NOTE occurrences. NOTE always follows its own gate;
legacy modifier mode cannot bypass it. Stop preserves effect tails. Play and clear
restart column cycles. Plain value randomizers preserve cell conditions. Shift+hold X on FX1 or FX2 also
randomizes that column's positive conditions; generated empty cells reset to1:1.

Future additions can include first-cycle, fill and other deterministic state conditions,
ratchets and further actions. Probability-based triggering is excluded from this implementation.

## Verification

`effect_tests` checks every processor for nonzero waveform change, bounded finite integrated
output, send tails after an impulse, and the complete tracker audio path. It can write listening
examples with `build/mac/effect_tests build/effect-auditions`. The dry example and each effect
use the same note/source. This is a numerical and integration check; it does not replace owner
listening tests. Workflow tests cover column isolation, legacy phases1–16, positive per-cell conditions and
fallback playback, actual transport triggering, motif repetition and project migration.


## Version 0.3 master chain and local references

Master modules run saturation (HardVacuum adaptation), overdrive, distortion, bitcrush,
rate reduction, flanger and phaser, then stereo-linked peak compressor, delay, reverb.
All controls are percentages; zero bypasses a coloration module. Output level and linked
limiter follow the effects. Track send signals enter delay/reverb at the appropriate stage.
The first version exposes one amount per module, with fixed delay feedback, reverb decay,
modulation rate and compressor timing. More detailed module parameter pages remain future work.

Inspected Nornios's `Bundled/dust/code/pedalboard/lib/fx/Flanger.sc`, `Phaser.sc` and
`Compressor.sc` under ../nornios. The flanger's modulated delay,
four-stage phaser and drive-oriented compressor controls informed the lightweight native
implementations. Those SuperCollider classes were not imported, and this is not a claim
of equivalence to their algorithms. Existing Bigorna-inspired track distortion modules remain.

INST generation snapshots Fugitive's GPL-3.0-only gen.c recipes and gen_perc.c, preserving
musically steady pitches. gen.c omits only the unrelated trailing gen_role_sound routine;
original source SHA and snapshot SHA are recorded in third_party/fugitive/GENERATOR-SNAPSHOT.json.
Recipe code and cooking run outside the audio callback. Instrument slots are shared, so
regenerating a slot changes every note referring to it, while existing ringing voices keep
playing their original cooked patch.


## Version 0.4 parameter controls

Each module has four independently effective parameters. Values are displayed from0–100.
F1/F2 selects the module; up/down selects a row; left/right edits it. Shift gives ten-unit steps.

| Module | Parameter1 | Parameter2 | Parameter3 | Parameter4 |
| --- | --- | --- | --- | --- |
| Saturation | Drive | Warmth | Tone | Mix |
| Overdrive | Drive | Bias | Tone | Mix |
| Distortion | Drive | Threshold | Tone | Mix |
| Bitcrush | Reduction | Dither | Tone | Mix |
| Rate reduction | Rate reduction | Jitter | Slew | Mix |
| Flanger | Depth | Rate | Feedback | Mix |
| Phaser | Depth | Rate | Feedback | Mix |
| Compressor | Threshold | Ratio | Attack | Release |
| Delay | Send | Time | Feedback | Tone |
| Reverb | Send | Decay | Damping | Size |
| Output | Level | Balance | Limiter ceiling | Limiter release |

Coloration amount/depth zero bypasses that module. Compressor ratio0 is1:1. Tone maps to200Hz–20kHz;
delay time20ms–2s, feedback0–0.9; reverb feedback0.2–0.95, damping progressively darkens tails,
size changes comb lengths. Compressor attack0.1–100ms, release10ms–2s; limiter release10ms–1s.
These engineering ranges are implementation details; the UI keeps percentage values.

Manual saving follows Fiber's Bigorna-derived short/long SAVE logic in
../mvave-fiber/firmware/src/ui.c and projects.c:800ms threshold,
short action on release, long action once, and render SAVING before storage work.
The source projects were read without modification. Amalthea uses its own atomic Linux session file,
not the FM-1 flash-slot implementation.

## Nornios processors (0.7.0)

CL (Clouds granular), AM (Amp Sim), BZ (Biztortion), and RO (Rotor) are appended to
the existing effect IDs, preserving old session mappings. Their step value is mix.
Track/slot-local parameter profiles are included in version 6 sessions; versions
1–5 load with default profiles. None of the live session is saved during updates.

- **Clouds**: original Mutable Instruments DSP by Emilie Gillet, MIT, from
  [eurorack](https://github.com/pichenettes/eurorack) commit
  `f4cfa7697095a28d292bff758beb51deb49b5a86`; stmlib pinned to
  `e3bd7c9cc00e4364166f9905c0509b6ffd0535ec`. Sources are in `third_party/clouds`,
  retaining their copyright/license headers. Granular mode, stereo 16-bit quality,
  original 118784/65408-byte memory regions, 32-sample blocks at 32 kHz. The host
  adapter converts 44.1 kHz with two lowpass biquads and linear interpolation in
  each direction, with fixed storage and roughly one DSP block of adapter latency.
  Other Clouds playback/quality modes are not exposed. Upstream DSP is unchanged
  apart from thread-local RNG storage, mix/envelope/size table endpoint guards, and
  a runtime grain-limit setter. The adapter preserves a separate random state
  per insert. A shared 128-grain budget retains 32 grains per instance for up to
  four simultaneous Clouds slots; heavier use divides the budget, down to five
  grains per instance at all 24 slots. Buffer capacity remains unchanged. No buffer sharing between tracks.
- **Biztortion**: Gabriel Bizzo, GPL-3.0, carried through Nornios's native port
  (`Engines/BiztortionEngine.cpp`, Nornios commit
  `3d139e841db0491498d9ebce53177bac28147734`). Retains all eight module positions
  and all audio module parameters. The original Nornios code is compiled with a
  minimal Engine host contract; scratch buffers are capped at 256 frames and reset
  restarts its deterministic dither clock. The oscilloscope position is audio
  pass-through; its visual analyzer is not included. Parameter percentages map
  onto the original ranges (log frequency/bit-depth and linear other controls).
- **Rotor**: blackboxaudio, GPL-3.0, adapted from the Nornios native engine at the
  same commit. Reuses its five 1024-point wavetable shapes, interpolation,
  multiplication/noise/mix formula, and stereo phase-continuity correction.
  The JUCE host, polling and display code are omitted. Its source reference is
  retained in `third_party/nornios/RotorEngine.reference.cpp`.
- **Amp Sim**: translated from Nornios/Pedalboard's `AmpSimulator.sc`, retained
  in `third_party/nornios`. Uses the JCM800-style asymmetric transfer curve,
  pre/post shelving, DC removal, second tanh stage, bass/mid/treble/presence EQ,
  60 Hz/10 kHz cuts and a Freeverb-topology room. This is a native graph
  translation, not a SuperCollider binary port: the transfer curve uses a 1025-point table; the second tanh uses a 4097-point
  table to avoid transcendental calls in the callback. Filters use RBJ coefficients and
  the room is a fixed-buffer Freeverb topology with stereo length offsets.
  A neutral output-level control is added. Credits in the original graph name
  Michel Buffa/Jerome Lebrun and Pakarinen/Yeh.

The insert page was removed in build 16; mix is per-step, the other controls per track
and slot. All values are percentages; waveform/module switches additionally show
the selected name. Global and slot mix/output controls initialize neutrally. No
JUCE GUI, SuperCollider runtime or heap allocation is used by the audio callback.

On heavy insert loads, a persistent second DSP worker processes odd-numbered
tracks while the main audio thread processes the others. Both states remain
track-local; synchronization completes before master mixing. Threads are created
before audio starts, and the worker uses the existing realtime scheduling priority
on Linux. ARM audio callbacks enable flush-to-zero for denormal tails and restore
the caller's FPCR; the worker enables it for its own thread. Native DSP builds use
O3 without fast-math, preserving infinity semantics used by the grain scheduler.

CS / CLOUDSEED SPACE is the former master space as an independent stereo insert.
See [TAL master, insert space and Stop behavior](TAL-REVERB-AND-TAILS.md) for current controls.

Since0.12, the master color stages are consolidated into Abrasion, Corrosion and
Modulation, with independent per-track sends/settings. AB/CR/MD step commands
override those sends. The old DS/SA/OD/BC/SR entries are no longer selectable.
See [current macros and references](MACRO-EFFECTS.md).
