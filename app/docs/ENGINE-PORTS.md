# Native engine bank — Amalthea 0.16.1

The eleven independent instrument slots default to VAP, PERC, CHIP, FIBER, HIVE,
OP4, ANIMA, RUSSOLO, BAY AREA, RESURGENT and CALIGULA, in that order. Every
track owns its own patches and running voices for those instrument numbers. ACD, FAT, WTFM, DIRT and the demonstration sample
player have been removed from the application.

## Sources and adaptation

- VAP, PERC and CHIP retain the Fugitive snapshot and seeded family recipes.
  PERC draws a whole kit: its different voices are addressed by pitch.
- FIBER uses Nuno Zimas's `mvave-fiber` build 28, commit
  `29e5936ffc1033ad63f9e2b1943f1a405dc7e50f`: the original two-oscillator virtual
  250 kHz wavetable core, polyphase resampler, filters, envelopes, modulation
  matrix, dirt processes and family generator. The generated tables are vendored.
  The original offline level measurement runs when preparing a patch, never in
  the audio callback. Its measurement scratch is local rather than shared static
  storage. The standalone firmware's track ensemble is not part of this voice
  adapter; Amalthea's insert/master effects remain separate.
- HIVE and OP4 use `mvave-digirust`, whose Git origin is
  `git@github.com:nzimas/bigorna.git`, commit
  `1e2985922de4fefe30e9dcb46a8fc6f495871bdc`. HIVE is its original seven-oscillator
  swarm. OP4 is its four-operator/eight-algorithm FM core. Both retain Bigorna's
  filter/amp chain and seeded family recipes. The source subset excludes hardware,
  sequencer and UI dependencies. Generated waveform tables are read directly as
  immutable data, avoiding initialization copies and shared writes.
- Bigorna recipe LFOs retain their rate, fade, waveform, routing and four-block
  update laws, adapted to private voice state so one instrument slot cannot alter
  another track. In contrast to the firmware's per-track FREE LFO, each new voice
  starts its own phase. HIVE's internal swarm animation remains original. Patch
  sends, pan and MANGLE from the source firmware are not applied by the engine
  adapter: Amalthea's track/step settings and insert chain own that processing.

All vendored files retain GPL-3.0-only notices; each source directory includes its
original LICENSE. Wrapper code is in `src/engine_fiber.c` and
`src/engine_bigorna.c`. The original unused Fugitive recipe branches remain in
its historical generator snapshot; the removed DSP cores are absent and cannot
be selected or linked.

## Runtime cost and isolation

All voice/cooked storage is fixed and bounds checked at compile time. Recipes and
Fiber's measurement run on the producer thread and are cached by engine/seed.
No allocation, table generation or offline normalization occurs in playback.
At 24 active voices the existing persistent DSP worker renders odd tracks while
the callback renders even tracks. They own disjoint voice state and track buffers,
join before inserts, and use the same worker for heavy inserts afterward.

Synthetic device benchmark: 12 active tracks, six seconds of audio, default patch
per engine, no inserts. Before/after optimization energies match for every engine.

| Engine | Render seconds | 256-frame p99, microseconds |
| --- | ---: | ---: |
| VAP | 1.803 | 1902 |
| PERC | 0.776 | 1284 |
| CHIP | 0.695 | 732 |
| FIBER | 1.223 | 1276 |
| HIVE | 3.088 | 3365 |
| OP4 | 2.078 | 2762 |

Hive previously took 5.592 seconds, p99 6102 microseconds. The optimization brings
its p99 below the 5805-microsecond period. OS scheduling produced occasional
6.8–7.1 ms maxima; these are synthetic throughput measurements, not a guarantee
that every combination of 128 voices and 24 inserts fits. Long HIVE/OP4 releases
exhausted the 128-voice pool in this sustained 16th-note test (460 oldest-voice
replacements); they are included in the measurements. The app retains its 80 ms
ALSA buffer. Eight Fiber patch preparations took 78 ms total on the Brick.

## Session compatibility

Format 8 distinguishes the new engine IDs. Formats 1–7 remain readable. VAP, CHIP
and PERC retain their seeds and map to their new IDs. Removed engines map to fresh
replacement defaults: ACD/SAMPLE to FIBER, FAT/DIRT to HIVE, WTFM to OP4. Instrument
slot references and all other session content remain intact. No file is rewritten
merely by reading it. Normal startup remains blank and stopped.

## Vascular and Russolo expansion

Owner sources: vascular-ios commit304a112a8e954dd6cf4378863334ae4040ebc8a3;
Tonitor reference (directory tonitor-smk37)
commit1f5fde612538635b3b807de07692dd904e135d13. Only DSP headers/data are used;
the Csound opcode wrappers, Swift UI and application frameworks are excluded.
The owner's source is adapted into this GPL-3.0 appliance; vendored Plaits/stmlib
DSP retains its MIT notices in third_party/vascular/PLAITS_LICENSE and headers.

| Engine | Families | Preserved architecture |
|---|---:|---|
| Anima |32|Six simulated fingers;24 band-limited tables; paired oscillators, FM, five-oscillator stack, four grains per finger, noise, envelopes, motion routing, two filter stages, crusher/shaper, phaser and tuned resonator|
| Russolo |45|All five Noisferatu banks: generative buffers, noisy textures, nine bit-address modes, blips/tones, logic oscillators; sampled crackle included|
| Bay Area |24|Original wavefold/FM voice, low-pass gate and sustaining mode, control-rate interpolation, hidden LFO modulation, resonators, stereo reverb and limiter|
| Resurgent |32|Vascular's independent Surge-style two-scene core: three oscillators/scene, native filters, envelopes,12 LFOs/scene, matrix/MSEG/additive support and native effect chain|
| Caligula |24|Eight oscillators/four interacting pairs, sensors, hyper-LFO, two modulated delays, feedback, distortion and the simulated player|

Anima recipes include the original eight designs and new families informed by
Tonitor's breath/grit/swarm/tide/choir/shards/bowl/dust/machine vocabulary, plus
new industrial and cinematic variations. These are designs for the complete
Vascular core, rather than Tonitor's reduced fixed-point synthesis. Bay Area
uses24 constrained LPG/fold/FM/envelope profiles. Resurgent uses32 oscillator,
filter, envelope and modulation profiles; generated mipmapped waveforms feed
its wavetable/window oscillators. Caligula uses its eight original families
with three distinct slider profiles.

The requested Surge-derived engine was **Resurgent**, not Polylog. It preserves
Vascular's implementation and its existing differences from Surge XT: it is not
a bit-exact Surge port. External FXP/wavetable import is not exposed here.
Vascular's unsupported external-audio/vocoder/Airwindows modes remain outside
these generated recipes; no audio input backend is added in this pass.

Russolo source: [Robert Heel's Noisferatu](https://github.com/rob-scape/noisferatu),
commit eeed0aa797b4e622bdf059920c87d776e7d08d8b, GPLv3. Arduino, display,
ADC, timer and DAC code are excluded. Original algos/params/sample files are
retained beside the instance adapter for review. Function-local statics become
per-instance fields. The16kHz/10-bit synthesis is resampled relative to the note,
with velocity, attack/release, additional seeded crushing and DC removal. Sparse
Bernoulli starts with a keyed event; the noise/saw window begins on nonzero
buffer content. Original stochastic behavior continues after those initial
conditions. Periodic buffer regeneration follows sample time rather than millis.

VAP/PERC/CHIP gain12 seeded discovery profiles, HIVE/OP4 eight, while Fiber
includes its existing experimental path. These favor spectral movement,
nonlinear filtering, metallic transient bodies, deeper timbre modulation and
controlled digital degradation without making every patch unpitched. Existing
nonzero seeds can therefore produce expanded-recipe sounds; seed0 preserves
each engine's initial patch.

### Ownership and resource policy

Eleven slots belong independently to each track. The UI and audio both resolve
track-local patches; there is no editable global instrument instance. Old bank
values are initialization templates only. AMAL15 persists all eleven local slots with 16-bit ownership masks; AMAL1–14 remain readable.

Native played instruments retain one running instance per track/slot/patch identity
while notes retrigger, preserving their internal motion, envelopes and delays.
Patch changes may overlap old releases. Their internal voice counts and DSP
paths are retained; the legacy engines keep the shared128-voice pool behavior.
Scratch is bounded by a32MiB virtual reservation per pool voice, created before
audio. It is not eagerly resident: tested native families use0 bytes (Anima and
Russolo),215944 (Bay Area),317120 (Resurgent) and2125072 (Caligula) of arena
storage per active instrument. Unsupported giant aggregate workloads still need
CPU/RAM measurements; no claim of arbitrary128-fold native polyphony is made.

Tests cover every new family, deterministic independent voices, finite output,
no allocations during note-on/render/release, and eventual release silence.
24 unmodified-reference Anima designs match the cached core sample-for-sample.
All11 engines pass real audio isolation tests for both same-slot and different-slot
tracks, override resolution, undo and persisted patch banks.

### Brick measurements (0.16.1)

Synthetic12-track, dense16-row,120 BPM, six seconds of audio, seed0, while the
foreground tracker remained running. Four-way disjoint-track DSP scheduling.

| Engine | Render seconds | p99 per256 frames | Maximum |
|---|---:|---:|---:|
| Anima |4.597|6403us|76202us|
| Russolo |0.917|987us|74197us|
| Bay Area |3.142|4777us|80240us|
| Resurgent |2.406|3761us|84417us|
| Caligula |3.996|7485us|101132us|

All five workloads have faster-than-real-time aggregate throughput and zero
voice steals. This is not a universal deadline guarantee: Anima/Caligula p99
exceed a256-frame period, and scheduling maxima reach76–101ms under competing
load. The actual ALSA period is882 frames with a3528-frame buffer. More complex
recipes and simultaneous heavy inserts require further workload-specific
profiling; no hidden reduced-quality mode is used. Earlier Anima per-note
stacking exceeded real time; per-track retriggering and exact coefficient caching
retain the complete synthesis core, and four-core scheduling reduces the final
six-second case from7.040s (two-core scheduler) to4.597s.

### 0.16.1 accessibility correction

All eleven engines are directly selectable in INST and as the track default.
AMAL15 expands the bank and ownership masks; importing AMAL1–14 preserves their
eight original slot identities and initializes the three additional slots.
Caligula initialization now clears stale gate-edge history after applying its
initial fast-envelope parameters, so a first strike cannot be mistaken for an
already-open sensor. Regression renders include the previously silent seed
2352265441 and allow its internal delay to decay.


## FM DRUM (build 10)

`src/fmdrum.c` is Amalthea's own floating-point DRUM machine, written from the
Digitone II behaviour recorded in `mvave-digirust/docs/dt2-curves.md`
(appendix C) and its parameter order (`third_party/bigorna/sound.h`, `FD_*`).
It is not a copy of the FM-1 `fmd.c`; it keeps what that version reduced:
per-sample envelopes and sweep at the DT2 laws, a 2x oversampled body with a
23-tap half-band decimator, the 33-wave set pre-blended for every OP.C / OP.AB
value, 32 synthesised transients crossfaded on 31 p, float ratios and band.
`engine_bigorna.c` wraps it as `ENG_FMDRUM` (engine 11) with Bigorna's voice
chain, LFO port and DRUM recipes (`gen_sound(.., ENG_FMD, ..)`). Engines 6 .. 10
remain the native (Vascular / Noisferatu) ones: `nativeEngine()` in `core.hpp`.
