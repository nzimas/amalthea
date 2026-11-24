# Native effects

Effects recreated one to one from open-source code as plain C++ in `src/native/`: no
framework, library or extra layer reaches the device. Each is a `NativeFx`
(`src/native_fx.hpp`) with a profile of up to 12 values, available as an insert in every
appliance: Tracker FX columns, FX slots and LIFEBLOOD nodes (and their recipes). The
source file of each effect begins with its provenance, licence and what was changed or
omitted (GUI, MIDI and host features are replaced by parameters or seeded behaviour).

## Tempo

Every rhythmic time base is a division or multiple of the reference beat: the FM1's
external MIDI clock when present, else the session tempo. The shared division table
runs 4 BARS, 2 BARS, 1 BAR, 1/2., 1/2, 1/4., 1/2T, 1/4, 1/8., 1/4T, 1/8, 1/8T, 1/16,
1/16T, 1/32, 1/64. External Start, internal Play and Tracker playback realign every
rhythmic effect to the downbeat.

## Batch 1 (build 31): stutter, repeat, slicing, shuffling, gates

| Effect | After | Licence | Brick cost (worst random draw) |
| --- | --- | --- | ---: |
| OOPS | B.Oops (Sven Jähnichen): seeded step sequencer of 30 glitch effects in pools | GPL-3.0+ | 0.18 ms |
| BUFFER OVERRIDE | Destroy FX Buffer Override (Sophia Poirier): forced buffers re-read in minibuffers, two LFOs | GPL-2.0+ | 0.10 ms |
| LIVECUT | Livecut (Remy Muller) / BBCut (Nick Collins): CutProc11, WarpCut, SQPusher | GPL-2.0+ | 0.12 ms |
| SUPATRIGGA | Smartelectronix SupaTrigga (Bram de Jong): per-bar slice rearranging, reverse, slow, repeat | GPL-3.0 | 0.10 ms |
| BEAT REPEAT | Bespoke Synth Stutter (Ryan Challinor): 16 repeat modes, triggered on the grid | GPL-3.0+ | 0.13 ms |
| TIME SHAPER | TIME-12 (tiagolr): 63 preset time curves (stutter, tape stop, scratch, reverse) | GPL-3.0 | 0.18 ms |
| JUMBLR | B.Jumblr (Sven Jähnichen): re-sequenced buffer jumps from seeded patterns | GPL-3.0+ | 0.17 ms |
| SCRUBBY | Destroy FX Scrubby: tempo-synced random scrubbing, robot / DJ speeds, note sets | GPL-2.0+ | 0.13 ms |
| GLITCHLETS | glitchlets for norns (Zack Scholl), reimplemented from its Lua and SuperCollider | MIT | 0.30 ms |
| SKIDDER | Destroy FX Skidder: rhythmic gate with random rate, pulse width, floor, pan, rupture | GPL-2.0+ | 0.12 ms |

## Batch 2 (build 32): gates, loopers, buffer bending, destruction

| Effect | After | Licence | Brick cost (worst random draw) |
| --- | --- | --- | ---: |
| CHOPPR | B.Choppr (Sven Jähnichen): step gate with shaped edges, swing, pan, seeded patterns | GPL-3.0+ | 0.11 ms |
| GATE SHAPER | GATE-12 (tiagolr): 34 preset gate curves, synced, audio/drum triggers | GPL-3.0 | 0.21 ms |
| TREMOSQUARE | Airwindows TremoSquare: square chop switching at zero crossings, synced | MIT | 0.09 ms |
| BEADS | Mutable Instruments Beads: granular/delay texture, four quality modes, clock sync | MIT | 0.36 ms |
| SOFTCUT | monome softcut engine with a synced micro-loop controller (holds, rates, tape stops) | GPL-3.0 | 0.44 ms |
| TRANSVERB | Destroy FX Transverb: two varispeed read heads with feedback | GPL-2.0+ | 0.20 ms |
| GLITCHSHIFTER | Airwindows GlitchShifter: raw splice pitch shifter | MIT | 0.16 ms |
| PITCHNASTY | Airwindows PitchNasty: grinding primitive pitch shifter | MIT | 0.10 ms |
| AMALGAM | Valley Amalgam: 17 ring/bitwise/float-bit cross-mangling modes against an internal source | GPL-3.0+ | 0.18 ms |
| GEOMETER | Destroy FX Geometer: rebuilds the wave from landmark points | GPL-2.0+ | 0.33 ms |
| MULTIBAND DIST | Airwindows MultiBandDistortion | MIT | 0.29 ms |
| DISINTEGRATE | Airwindows Disintegrate: signal-modulated delay layers | MIT | 0.39 ms |
| BITGLITTER | Airwindows BitGlitter: old-sampler crushing | MIT | 0.16 ms |
| PAFNUTY | Airwindows Pafnuty2: Chebyshev harmonic generator | MIT | 0.14 ms |

## Batch 3 (build 33): waveshapers, cross-modulation, spectral, resonators, voices

| Effect | After | Licence | Brick cost (worst random draw) |
| --- | --- | --- | ---: |
| CYANIDE | Smartelectronix Cyanide2: 16x oversampled spline waveshaper, 16 factory curves | GPL-3.0 | 0.84 ms |
| POLARIZER | Destroy FX Polarizer: flips every Nth sample (plus a synced leap sweep) | GPL-2.0+ | 0.09 ms |
| ANGR | B.Angr (Sven Jähnichen, Airwindows XRegion): four distortions crossfaded by motion | GPL-3.0+ | 0.37 ms |
| WARPS | Mutable Instruments Warps: cross-modulation algorithms, vocoder, frequency shifter | MIT | 0.51 ms |
| FREQ SHIFTER | Signalsmith frequency shifter (Hilbert IIR), synced barber-pole sweeps | MIT / 0BSD | 0.25 ms |
| BROKENFFT | Destroy FX brokenfft (Tom 7): spectral mangling with a built-in FFT | GPL-2.0+ | 0.76 ms |
| TREEMONSTER | Surge XT Treemonster: pitch-tracking ring modulator | GPL-3.0 | 0.12 ms |
| RING MOD | Surge XT Ring Modulator: diode model, 32 carrier shapes, unison, synced carrier | GPL-3.0 | 0.44 ms |
| COMBULATOR | Surge XT Combulator: three tuned combs with feedback and noise | GPL-3.0 | 0.52 ms |
| RESONATOR | Surge XT Resonator: three driven resonant bands | GPL-3.0 | 0.41 ms |
| VOCODER | Surge XT Vocoder: input as modulator, internal chord/noise/synced-pulse carrier | GPL-3.0 | 0.61 ms |
| SMR | 4ms Spectral Multiband Resonator: six resonant bands, 19 scale banks, synced rotation | MIT | 0.25 ms |
| REZ SYNTH | Destroy FX Rez Synth: resonant bands played by a synced internal arpeggio | GPL-2.0+ | 0.36 ms |
| TALKBOX | mda TalkBox: LPC talkbox on an internal chord carrier | MIT | 0.15 ms |
| MDA VOCODER | mda Vocoder: 8/16-band channel vocoder on an internal carrier | MIT | 0.20 ms |
| REPSYCHO | mda RePsycho!: transient-triggered slice repitcher, optional tempo retrigger | MIT | 0.10 ms |

The vocoders and the talkbox use the input as the voice and generate their carrier
(chords that move at a tempo division, noise or a synced pulse train), since an insert
has only one input.

GEOMETER's largest windows (16384–32768, set by hand) with very dense points can
exceed a 32-frame slice on the Brick; random draws stay at 16384 and below.

Costs include about 0.09 ms of bare path; measured with `tools/benchmark_lifeblood`
over 20 random draws each. Each buffer-based effect holds at most about 3 MB and clamps
longer divisions at slow tempi (noted in its source).

## Checks

`tools/native_fx_check "NAME"` verifies one effect: exact silence from silence, two
instances identical, an audible default, no allocation while rendering or changing any
parameter, bounded output, silence within 12 s of the input stopping, and CPU per block.
All ten also pass under AddressSanitizer/UBSan and the shared insert tests.

## Storage

Tracker sessions are AMAL23: the native profiles follow, preceded by their count and
width, so later builds with more effects still read them. Device settings are AMDV3
(128 insert bits). LIFEBLOOD stores profiles as append-only members.
