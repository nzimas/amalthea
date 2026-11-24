# TAL master, CloudSeed insert, transport tails and Mix

Version0.11.0, 21 November2026.

## Source and character

The master uses the original TAL-Reverb-III DSP available in
[DISTRHO-Ports](https://github.com/DISTRHO/DISTRHO-Ports/tree/d3b62da2e83c69b0866af5bb2e29ac78dc8014cf/ports-juce5/tal-reverb-3/source/Engine).
This is the legacy open-source implementation, not the newer TAL-Reverb-3 v2.3
binary. TAL's [current Reverb4](https://tal-software.com/products/tal-reverb-4)
is a modulated vintage plate, delivered as desktop plugins (Linux x64).
No TAL4 ARM DSP source was located. III provides actual source for a native port.
The upstream DSP headers retain Patrick Kunz's copyright and GPL Version2 notice;
see third_party/tal-reverb-iii/LICENSE and SNAPSHOT.json. This source is retained
under its original terms; its headers have not been relicensed.

The four stereo comb paths, five serial allpasses, pre/post modulated diffusion,
colored shelf filters and feedback are TAL's original design. The two original
inverted-log decay stages are retained. Stereo processing is enabled; dry mixing,
meters, denormal noise injection and the plugin/UI/host wrapper are omitted.
The wrapper returns only the stereo wet bus at44.1kHz. It is not a new hall
algorithm labeled TAL. Default decay is35%, with moderate bass/treble attenuation.

The adaptation adds in-place allocation-free reset, fixes mismatched array deletes
and shelf ownership leaks, and uses unsigned arithmetic for reproducible noise.
Shelf coefficients are cached between changes instead of recalculated every sample;
redundant duplicate output calculations are removed. No external framework/library
is linked. Unexcited buses are idle; tails suspend only after two seconds below
1e-10, leaving audible decay intact. All reverb controls'0/100 endpoints were checked
with twelve-second impulses; even maximum decay stayed finite and lost energy.

## Master controls

| Control | Purpose |
| --- | --- |
| RETURN | Master wet-return level |
| DECAY | Original TAL double-scaled decay |
| LOW TONE | Bass shelf amount,0 cut /100 neutral |
| HIGH TONE | Treble shelf amount,0 cut /100 neutral |
| PREDELAY | Original curved0–1second predelay |
| WIDTH | Wet-only mono to full stereo |
| LOW FREQ | Bass shelf frequency100Hz–10kHz, curved |
| HIGH FREQ | Treble shelf frequency100Hz–10kHz, curved |
| MIX | Blend wet return against the dry mix |

Only per-track and per-step sends excite the reverb. A0% track remains dry unless
its own held step command overrides that value. A step Reverb0% replaces its track's
send. RETURN and MIX independently scale the wet return, never a whole-mix input.
All controls display0–100%; up/down pages four rows at a time.

All11 master modules now expose MIX. Existing saturation, overdrive, distortion,
bitcrush, rate reduction, flanger and phaser mixes stay in their original rows.
Compressor, delay and output add a fifth row; reverb adds a ninth. Compressor and
output linearly blend their stage against its input. Delay/reverb are auxiliary
buses: their mixes scale the returned wet signal without attenuating dry tracks.
The final safety clamp remains even when Output Mix bypasses its musical limiter.

## CloudSeed insert

The former master sound remains CS / CLOUDSEED SPACE in either FX column.
Controls (page removed in build 16): MIX, DECAY, DAMPING, SIZE, PREDELAY, WIDTH, MODULATION, DIFFUSION.
Defaults restore the exact former master profile65/35/65 +33/100/35/85.
Each track/slot owns independent DSP state; the step value locks its dry/wet mix.
All24 cores are prepared before audio starts (~204MiB total DSP storage), avoiding
allocation on effect selection or parameter changes. Color-only master inserts
omit this allocation. The existing persistent odd/even insert worker handles2+
heavy positions, including Space. Native NEON/delay optimizations remain in place.
CloudSeed is computationally expensive; measured device budgets are in validation.json.

## Stop and restart

Previously Stop deleted all voices and insert/master histories and skipped the
master processing call, hard-cutting tails. Stop now halts the transport, hides
playheads and sends note-offs, while voices, inserts, send buses and master chain
continue processing. No new sequenced events are emitted. Play restarts column
walks at their initial steps without clearing master tails. Stutter loops fade
exponentially after Stop; Clouds freeze is released. Explicit track-clear still
resets its track and clears shared buses when the final source is removed.
Fresh application/system starts remain blank and stopped.

The regression stops during a genuine note gap and compares its continued delay,
TAL and Space outputs sample for sample against uninterrupted playback in that gap.
It checks actual post-stop energy, natural later decay, frozen trigger counters and
first-step restart. Every master Mix0 nulls against dry and Mix50 matches the
midpoint of0/100 in isolated-stage tests. All controls have audible-DSP tests.

## Density and persistence

Density already uses columnLength(track, selectedColumn), not a fixed16 or the
track length when a column overrides it. Exact population rounds to nearest event:
7 steps x20% produces1;16 x20% produces3. Expanded tests cover every column,
track-inherited and explicit lengths1/5/7/16 and0/20/25/50/75/100%. The options title
shows the current effective column step count. Plain instrument patch redraw still
ignores density; Shift+holdX randomizes instrument events at the chosen density.

Format10 appends seven Space profile bytes per slot and four master mixes after
v9's data. Versions1–9 remain readable. Older master space parameters migrate to
TAL defaults, preserving wet-return level and track sends; no file is rewritten.
Only synthetic fixture files are saved/loaded in automated tests. Device QA and
deployment never invoke session Save/Load; the final application starts blank.

Physical ALSA-buffer sampling after Stop measured early stereo RMS0.049689/0.081622,
then both channels reached digital silence after a seven-second wait. The normal
play/stop QA run reported141,016 blocks of32 frames, zero xruns and zero voice steals.
The final framebuffer exactly matched a blank stopped native render.
