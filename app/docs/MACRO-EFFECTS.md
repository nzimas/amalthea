# Abrasion, Corrosion, Modulation and note audition

Version0.12.0, 21 November2026.

## Reference work

The owner selected Nornios's **D** processor as an abrasion reference. Its actual
`Bundled/dust/code/d/lib/Engine_D.sc` combines decimation, separate low/high-band
weighting, companding, a nonlinear transfer function and deliberate hiss. The
upstream is [justmat/d](https://github.com/justmat/d), recorded by Nornios as
commit23d010c0ddb74ac546f99d0ef9a70f8b3261f321. The macro takes the band-weighting
and strong waveshaping approach; none of D's code, random transfer table or noise
sources is copied. Hiss is intentionally absent.

Also inspected locally:

- Vascular iOS `Sources/CVascularOpcodes/src/DistorterCore.h`: modular tanh/sine
  shaping, waveform asymmetry, quantization/holding and filters; graph macro design.
  Repository commit304a112a8e954dd6cf4378863334ae4040ebc8a3.
- Vascular desktop `DestructiveEffects.swift` and `Vascular.csd`: separate controls
  for saturation drive/curve/tone/body and crusher bits/rate/jitter/tone.
- Nornios `Vendor/crushclone/Source/DSP/Distortion.cpp`: asymmetric tube saturation
  with DC compensation and emphasized hard clipping; fixed oversampling frontend.
- Nornios `Vendor/portedplugins/ChowDSP/AnalogVintageDistortion.cpp`: weighted shelf
  input, substantial drive and bias, oversampled clipping and DC blocking.
  Nornios repository commit3d139e841db0491498d9ebce53177bac28147734.

Primary GitHub comparison:

- [Airwindows Drive](https://github.com/airwindows/airwindows/blob/master/plugins/WinVST/Drive/DriveProc.cpp): input highpass, staged nonlinear drive and dry/wet blending.
  Inspected its processing source; denormal-noise and output-dither paths are not
  adopted. No claim that this new macro reproduces Drive's transfer function.
- [ChowDSP BYOD](https://github.com/Chowdhury-DSP/BYOD), commit1cf22b6ac802b9dc33cfc9f8dd6af5b3c3e40bc9: inspected MXRDistortion/WDF sources,
  Flanger's offset/amount/feedback/dry-wet model and Phaser8's modulated stages.
  These are mature alternatives; importing their full plugin frameworks and circuit
  infrastructure is unnecessary for these native experimental macros.

The implementation is new compact DSP informed by these sources, not a copied
plugin or a claim of equivalent circuit modeling. It links no new third-party
framework. Sound quality remains an artistic judgment for the owner's device trial.

## Chain, sends and inheritance

The visible master chain is now **Abrasion → Corrosion → Modulation → Compressor
→ Delay → Reverb → Output**. The eleven old serialization indices remain internal
for readable legacy files; seven pages are visible. The reverb's sound is unchanged,
with its display name simply REVERB.

The three macros own independent stereo state **per track**. Inserts run first,
then track gain/pan, the three macros in order, and the shared compressor/delay/
reverb/output chain. Each macro send feeds a portion of that track into its
processor; its wet return replaces that portion according to Mix. Unsent audio
stays dry. Macro returns feed later stages, including that track's delay/reverb sends.
Track sends default0%. New step commands AB/CR/MD override only their matching
send, including explicit0%; independent column clocks hold those commands normally.
The old DS/SA/OD/BC/SR choices are removed from the selectable/randomizer menu.

Y edits shared macro defaults. B opens an11-row track settings list at the original
34px label/38px value sizes. Only send rows are exposed for macros, in chain order:
Abrasion, Corrosion, Modulation, Delay, Reverb. No profile-settings links appear in
Track Settings. Existing saved local profiles remain supported by the DSP and file
codec. F1/F2 switches tracks in Track Settings and modules in Master Effects.
No footer shortcuts were added.

## Audible controls

| Macro | Parameters | Processing |
| --- | --- | --- |
| Abrasion | Drive, Character, Crossover, Body, Edge, Fold, Tone, Mix | Independently weighted complementary low/high bands; up to181x drive; biased soft/hard clipping blend and folding; two waveshaping substeps, tone filter and DC removal. Bias compensation keeps a zero input at zero. |
| Corrosion | Bit Crush, Rate Reduce, Jitter, Asymmetry, Slew, Tone, Drive, Mix | Signed mid-tread quantization,16 to3 effective bits; held sample-rate reduction44.1kHz to100Hz, timing jitter, positive/negative weighting, held-edge slew and tone/DC filters. No dither or added noise. |
| Modulation | Flange, Phaser, Rate, Feedback, Stereo, Color, Tone, Mix | Actual dry/delayed comb interference and eight-stage allpass interference, slow-to-fast LFO, signed feedback, stereo phase offset, delay/sweep coloration and tone/DC filters. Both paths produce audible cancellation, not merely allpass phase changes. |

All controls display0–100%. Drive deliberately makes a strong transformation at
ordinary synth levels. Corrosion quantizes audio, rather than merely adding noise.
The Abrasion substeps reduce abruptness but are not advertised as a full bandlimited
oversampling filter or an alias-free circuit emulation. Industrial aliasing remains
part of the aggressive settings. The existing final output safety clamp stays active.

## Silence, tails and performance

The prior master bitcrusher unconditionally generated dither on its input, including
silence. That entire old seven-module master path is removed. The new macros neither
inject noise nor use a gate to hide it: all-zero input/state produces exact zero.
Corrosion's random generator affects hold timing only. Filters and modulation tails
continue after Stop; long-quiet states suspend after two seconds below1e-10 output.
Clear-track explicitly resets its macros, preventing old state from poisoning new notes.

DSP uses fixed storage (~0.6MiB total for12 tracks), cached coefficients and a shared
immutable sine table prepared before audio. No allocation occurs during processing,
configuration or clearing. The existing disjoint-track worker now covers inserts,
track gain/pan and macros when multiple heavy tracks are active, then joins before
the shared master sum. No new thread pool/framework is introduced.

19 suites cover24 audible controls, meaningful default wet/dry differences, exact
Mix0 and Mix50 blends, zero-input silence at parameter endpoints, separate flanger/
phaser audibility, real audio track isolation and0% locks, shared/local profiles,
scrolling, clear/rebuild, persistence and existing sequencing/engines/inserts/tails.
Targeted ASan cases pass. Device benchmarks and runtime evidence: validation.json.

## Note audition

Entering NOTE edit on an empty cell starts at C4 and requests an audition. Up/down
requests the newly displayed pitch immediately, through the chosen step instrument
or the track default. The note remains pending until Enter commits it. The UI sends
an explicit event through the existing audio queue; cursor movement, session values
and selected notes are not polled by the audio callback.

Audition bypasses trigger conditions/modifiers and does not start/advance clocks or
write any pattern data. It works while stopped or playing and can be heard on a
muted track; muted sequencer voices still advance silently. Preview voices have a
200ms gate and normal engine release. Rapid browsing keeps at most two preview
voices, without stealing/releasing ordinary sequence notes. A sequence note-off
cannot release an audition voice. Queue retries retain the latest displayed request.
Actual output latency includes the existing ALSA buffer; no live-latency claim below
that hardware buffer is made. All six engines have audible preview regressions.

## Persistence

Format11 appends360 bytes:24 shared-profile bytes and28 per track (three sends,
one override mask,24 profile bytes). Versions1–10 remain readable. Prior enabled
whole-mix color settings map approximately to the corresponding new macro and100%
sends; old DS/SA/OD step commands become AB, and BC/SR become CR. This is a change
of algorithm, not a promise of identical legacy effect audio. No file is rewritten
on read. Source code attribution stays in the repository; the reverb UI is unbranded.

Development and device restarts remain blank and stopped. Only synthetic fixture
files are saved/loaded in tests; live Save/Load controls are never invoked in QA.

Final physical checks: both macro profile inheritance/overrides and all scrolled
pages exactly matched native renders. With all three sends at100%, empty playback
sampled357,376 real ALSA frames at exact zero. Uncommitted C4–C5 browsing while STOP
sampled339,200 frames at RMS0.275912/0.291748; afterward357,632 frames were again
zero. Runtime counted26 auditions at last MIDI72, zero sequencer triggers, zero
xruns and zero steals. No notes were committed. Final restart was blank/stopped
with an exact framebuffer match and unchanged saved-file checksum.
