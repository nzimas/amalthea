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

## OBRADOR, RINGS and PLAITS (build 44)

Three engines join the bank as instrument slots 13–15 (engines 12–14). Sessions are
AMAL26 (fifteen slots in the bank and in every track's local bank); AMAL18–25 load with
twelve slots and gain the three new ones as defaults, older formats as before. A blank
session's twelve tracks still start on slots 1–12.

### OBRADOR (engine 12)

Nuno Zimas's Oberheim-style polysynth voice from Tonitor for the M-VAVE FM-1
(`mvave-fm1`, HEAD `b1ffc18`, Tonitor build 75; `obrador.c` last changed in
`309fa36`). Vendored in `third_party/tonitor` with Tonitor's `dsp.h` and the part of its
generated tables it uses (regenerated by Tonitor's `gen_tables.py`, identical to Tonitor's
own build). One line changes: the level measurement's window is a local, not a static,
because Amalthea cooks patches on two threads. Tonitor and Amalthea both run at 44.1 kHz
in 32-frame blocks, so the fixed-point voice runs unchanged: two polyBLEP oscillators per
card (saw / pulse, sync, cross-modulation), noise, tanh drive, the SEM 12 dB state-variable
filter morphing low-pass → notch → high-pass (plus a second low-pass stage or band-pass
by kind), RC filter and amplifier envelopes, a delayed LFO with five shapes, the second
voice card and the vintage offsets and drift. `src/engine_obrador.c` is the adapter; one
Amalthea voice is one Obrador voice.

Families are Tonitor's ten kinds, each on one of its three paths (OPEN as drawn, AMBIENT
softer, slower and darker, EXP harder): OB STRINGS, DARK STRINGS, OB BRASS, HARD BRASS,
PWM PAD, NIGHT PWM, OB BASS, GRIT BASS, SYNC LEAD, TEAR LEAD, NOTCH SWEEP, PHASE FOG,
METAL, RUST BELL, DRONE, DEEP DRONE, POLY KEYS, DARK KEYS, SWELL, SLOW SWELL. The draw
weights favour the pads, swells and drones of dark ambient and the brass, basses and leads
of synthwave. Row variations move the cutoff (±10 semitones at most), morph, resonance,
filter envelope depth, the six envelope times, the sustains and the LFO's cutoff and morph
depths; never the waves, intervals, detunes, sync sweep, LFO pitch depth or rate. Tests
compare every family's output with the vendored code called as Tonitor calls it: they are
identical sample for sample.

### RINGS (engine 13)

Mutable Instruments Rings, every file of `rings/dsp` from eurorack `f4cfa76`, unchanged
(`third_party/rings`; the RINGS insert's resonator now builds from the same library).
`src/mi_rings.cpp` does what `rings.cc` and `cv_scaler.cc` do with the module's controls:

- V/OCT and STRUM patched (the tracker's notes), IN unpatched: the internal exciter,
  struck on each strum; or IN fed with a bow (noise following the note's gate through
  `rings.cc`'s own noise gate), with or without a pluck on the strum.
- All six resonator models, polyphony 1, 2, 3 and 4 (one Rings per track / slot / patch,
  so notes strum its own voices and let earlier strings ring), STRUCTURE, BRIGHTNESS,
  DAMPING, POSITION, the FREQUENCY knob (the tonic of the sympathetic strings), chord
  selection by STRUCTURE in the quantized model (`cv_scaler`'s rounding), FM from a slow
  sine on a third of the recipes, and the `Strummer`.
- The easter egg, Disastrous Peace: `StringSynthPart` with its six effects (FORMANT,
  CHORUS, REVERB, FORMANT 2, ENSEMBLE, REVERB 2), picked by the model as on the module.
- ODD left, EVEN right, with a recipe width.

Rings runs at its own 48 kHz in 24-sample blocks; `src/mi_resampler.hpp` converts to
44.1 kHz (32-tap Kaiser windowed sinc at 147 exact phases: flat within 1% at 1 kHz,
23.5 kHz −72 dB, 16 samples of latency). Its state (180 KB) lives in the native voice's
arena and is zeroed before `Init`, as the module's static memory is.

Families: GLASS BOWL, IRON BELL, DARK MALLET, TEMPLE GONG, BOWED METAL (modal); SITAR
HAZE, SYMPATHY (sympathetic strings); DARK HARP, NEON PLUCK, BOWED STRING, WIRE BASS
(string); FM TINE, FM GROWL, FM CHIME (FM voice); RING CHORDS, CHORD RAIN, CHORD DRONE
(quantized sympathetic); CATHEDRAL, SPACE PLUCK, GHOST STRING (string and reverb); PEACE
FORMANT, PEACE CHORUS, PEACE REVERB, PEACE VOWEL, PEACE ENSEMB, PEACE SPACE (easter egg).
Damping is kept below the settings where the reverb model or the easter egg never decay.
Row variations move brightness, damping, position, the bow and the width; STRUCTURE stays
(it tunes strings, picks chords and FM ratios, and sets inharmonicity).

### PLAITS (engine 14)

Mutable Instruments Plaits 1.2: the Vascular copy already vendored for BAY AREA
(`third_party/vascular/plaits`), which is upstream with its stmlib namespace renamed and
`kCorrectedSampleRate` set to 48 kHz. `src/mi_plaits.cpp` does what `plaits.cc` and
`cv_scaler.cc` do: the note into V/OCT (FREQUENCY sets the recipe's octave), the gate into
TRIGGER, and per recipe LEVEL, TIMBRE, MORPH and FM either unpatched or fed with a slow
sine. All 24 models; HARMONICS, TIMBRE, MORPH; the three attenuverters (they route the
internal decay envelope while their input is unpatched, scale the input while patched);
DECAY and LPG COLOUR; the LPG pinged (TRIGGER), following an attack / release on the gate
(LEVEL patched) or bypassed (nothing patched, an attack / release keeping notes apart);
OUT and AUX mixed and spread by recipe. In every mode a note ends at its length: from its
end the output falls to -60 dB in the recipe's release time (0.06–0.3 s for pinged models and
drums, up to 1.5 s for pads and drones; build 45). Plaits runs at 48 kHz in its own 12-sample blocks
(its trigger delay and envelopes count them) through the same resampler. One running
Plaits per track / slot / patch, retriggered by notes as the module is.

47 families, one model each, two on most models, weighted so that each of the 24 models
takes 2–7% of seed draws: VCF BASS, VCF SWEEP (VA-VCF); PD BRASS, PD RESONANT (phase
distortion); DX BANK A, DX SLOW A, DX BANK B, DX DARK B, DX BANK C, DX GLASS C (the three
six-op banks, HARMONICS on a patch's centre); TERRAIN DRIFT, TERRAIN LEAD; SOLINA DARK,
STR SWELL (string machine, dark chords); CHIP ARP, CHIP BASS; TWIN SAW (5–25 cents of
detune), SYNC LEAD (virtual analog); FOLD BASS, FOLD BLOOM; FM BELL, FM BASS, FM DRONE;
GRAIN VOX, GRAIN HOWL; DARK ORGAN, ADD BELLS; WT CRYPT, WT PLUCK; DARK CHORDS, CHORD STAB
(chords: 5, m, m7, m9, m11); VOWEL CHOIR, SAM WORDS, LPC WORDS (speech); SWARM DRONE,
SWARM HAZE; WIND, RESO NOISE; DUST RAIN, GLASS DUST (particles); DARK STRING, STEEL DRONE;
MODAL BOWL, MODAL WOOD; DEEP KICK; DARK SNARE; METAL HAT. Row variations leave HARMONICS
where it chooses (a chord, a patch, a bank, a ratio, a word bank, a detune) or spaces the
partials (string, modal), and never touch FM or FREQUENCY.

Both Mutable engines measure each patch's level when it is cooked (C3 held, then
released, two seconds at 48 kHz): the RMS is brought to 0.12, no more than keeps the peak
under 0.7. A released voice ends when it has stayed under −80 dBFS for 186 ms. Plaits'
deliberate +1 LSB output offset is removed by the same DC blocker the other native
engines use.

### Brick measurements (build 44)

`tools/benchmark_instruments` on the Brick while the installed build 44 was running: every family
in turn on twelve tracks of dense notes, three seconds of audio, no inserts. A 256-frame block
is 5805 us.

| Engine | Families | Slowest family p99 | Slowest render for 3 s | Patch preparation (mean) |
|---|---:|---|---:|---:|
| OBRADOR | 20 | 4134 us (DEEP DRONE) | 1.163 s | 0.008 s |
| RINGS | 26 | 6931 us (CHORD RAIN) | 2.085 s | 0.085 s |
| PLAITS | 47 | 4383 us (GRAIN HOWL) | 1.257 s | 0.032 s |

All three render faster than real time with no voice steals. With all twelve tracks on the same
heaviest RINGS family, the p99 is over one block: CHORD RAIN (four voices, quantized sympathetic
strings) 6931 us and SITAR HAZE 5883 us. Every other RINGS family, and every OBRADOR and PLAITS
family, is under 5805 us. Maxima of 76–83 ms for every engine are OS scheduling, as in the
earlier measurements. RINGS patch preparation is its level measurement (two seconds of Rings).
