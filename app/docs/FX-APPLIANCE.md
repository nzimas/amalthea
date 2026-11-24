# Amalthea FX — 0.21.3

For the current laptop monitoring baseline, source clock measurements and corrected
packet diagnostic status, see [the 22 November investigation](USB-MONITOR-INVESTIGATION.md).
Historical channel-1 comparisons below preceded the owner's instruction to use
track 4; both current diagnostic executables now use channel 4.

Choose FX on appliance home and press Enter. Startup is blank and stopped; no
session is automatically recalled. The phase iris represents six shared signal
paths around a common core. UI typography is Oxanium SemiBold (SIL OFL), from
Google Fonts; editable identity vectors are in assets/identity.

## Controls

- Cross: choose channel and FX1, FX2, level or BYPASS; Enter opens the selected
  editor, toggles level editing, or toggles channel bypass.
- BYPASS (foot of each strip): skips both slots and passes the channel dry at its
  level, with a 10 ms crossfade. Slot sequencers keep running so re-enabling stays
  in phase. Bypass state is stored in FX sessions (AFXR4; older files load enabled).
- Hold X on FX1/FX2: choose a new effect type for the selected slot only and
  randomize its parameters. The other slot is untouched.
- Shift + hold X on FX1/FX2: retain the selected slot's type and randomize only its
  parameters. On the level or bypass row, hold X does nothing.
- Enter on an effect: its 16-step sequencer, displayed as two rows of eight.
- Y (or short X) in a slot sequencer: randomizer settings. Up/down selects
  DENSITY (0–100% of the length, initially 45%) or COMPLEXITY (initially 50%:
  0 = all generated steps share one random setting, 100 = every step independent).
- While transport runs, a sequenced slot is heard only on triggered steps; empty
  steps pass dry. Stopped or with SEQUENCER OFF, the slot processes continuously.
- Hold X in the sequencer or density page: generate triggers and parameter locks
  for this slot only, retaining effect type and defaults. Count is rounded from
  density × current length. Hidden steps beyond that length are untouched.
  This enables sequencing and starts internal transport; external transport retains priority.
- Enter on a step: parameter locks; up/down selects, left/right adjusts.
- B in the sequencer: slot default parameters. F1/F2 page detailed parameters.
- Shift + left in step parameters: return that parameter to the slot default.
- A in the sequencer: length, clock division, TEMPO, direction and sequencer enable.
  TEMPO is per slot under internal clock: SESSION follows the session tempo, or
  30–300 BPM. External MIDI clock overrides it.
- L2: Back to the previous page (or out of level edit). R2: return to FX mixer.
  Middle button: FX sessions.
- Sessions: cross selects one of 64 slots; middle tap loads, middle hold saves.
  Enter opens tempo, Load and Save controls. Loaded slot has a blue marker.
- Play toggles internal transport. External MIDI transport has priority while locked.
- FUNC then Shift, holding both: exit confirmation, then explicit save choice.

Each slot has its own length, clock and direction. Step locks override its defaults;
unlocked parameters inherit. Steps without a trigger use the slot defaults.
Stopping transport also returns to defaults while audio processing and tails continue.
The playhead lights the currently sounding step across the two rows independently
of the navigation cursor. Disable sequencing for a continuous insert. Track and FX session banks are separate.

### Random draws and CPU (build 29)

Insert cost on the Brick depends on parameters. Measured over random draws, one QDELAY
reached 2.7 ms and one MATRIX 7.4 ms of a 5.8 ms block. Hold X, Shift+hold X and sequence
generation therefore draw QDELAY as a tape and stereo delay (pre-distortion, diffusion,
crusher, shifter, phaser, ducking and EQ stay off) and MATRIX with two delay nodes on its
default delay type. Twelve slots of such draws measured 3858 us p99, 5464 us worst
(`tools/benchmark_lifeblood`). Parameters edited by hand are not limited.

## Clock and USB routing

Incoming MIDI F8 establishes tempo; Start/Continue/Stop control transport. Without
clock for 750 ms, sequencing uses the FX session tempo and internal Play state.
Per-slot divisions remain relative to whichever source is active. Tempo is saved
with an explicit FX session save; loading starts stopped.

Open FX before swapping the single cable. While the laptop is attached, the screen
shows USB: UNPLUG LAPTOP. After a stable three-second disconnection, the bottom
port switches automatically to host mode and supplies power. Connect the FM1 after
five seconds. There is no trial timeout. To reconnect the laptop, exit FX through
FUNC then Shift, confirm exit and answer the save prompt; the appliance dashboard
restores device mode. R2 returns to the FX mixer and does not exit the appliance.
A supervisor restart also recovers a host role owned by a dead appliance process.

The installed Brick kernel already has USB audio host and raw MIDI support. The
backend scans class-compliant FM1 devices on the host port, opens their native
capture format, maps advertised channels automatically and reconnects after removal.
44.1/48 kHz inputs are converted to the internal 44.1 kHz clock with drift compensation.
No audio server, plugin framework or USB gadget reconfiguration is required.

Source firmware inspection:

| Firmware | Multitrack USB format | Clock in current local source |
| --- | --- | --- |
| Bigorna (mvave-digirust) | Six mono tracks; drum subtracks aggregated | MIDI clock/transport |
| Tonitor (mvave-fm1) | Six mono tracks | No emitted MIDI clock |
| Lilliput (mvave-liliput) | Six mono tracks | No emitted MIDI clock |
| Norilsk (molecular) | Three mono synth tracks | No emitted MIDI clock |

A firmware advertising its stereo mode produces one stereo strip; software cannot
recover separate tracks from a stereo mix. A legacy eight-channel descriptor maps
six tracks after its stereo master pair. The number of visible strips follows the
actual descriptor, rather than inventing channels.

## Validation and limits

29 Mac and ARM regression suites pass, including synthetic USB lane mapping,
clock loss/priority, independent sequencers, real insert processing and temporary
session round trips. No live session was saved, loaded or restored during QA.
The owner confirmed successful direct Bigorna-to-Brick audio during the bottom-port
host trial. Other firmware variants, hotplug and external clock still need physical
verification. This release captures FM1 audio and plays
through the Brick codec; it does not implement a duplex USB gadget sound card.

Channel DSP runs across three cores. Grain and Space line budgets are shared under
heavy simultaneous use; Space keeps full line count with up to three instances.
Twelve heavy inserts can still exceed real-time capacity for some combinations:
measured 256-frame stress blocks reached about 6.9 ms for twelve CrushClone instances
against a 5.8 ms block deadline. The output buffer absorbs short scheduling spikes,
but does not remove a sustained overload. These are measured limits, not a guarantee
that every twelve-effect combination can run without underruns.

## USB audio continuity — 0.21.2

Capture runs at FIFO priority 13, ahead of codec playback (12) and DSP workers
(11). The capture buffer request is 64 ms and codec playback stays at its stable
80 ms setting across appliance transitions; switching FX no longer drops and
reconfigures that PCM. The ring waits for 1536 input frames before starting,
then uses a slow, bounded drift correction around a 1792-frame target. Recovery
ramps avoid a hard edge on starvation/re-prime. More buffering adds monitoring
latency; this pass prioritizes continuity over minimum latency.

`/tmp/amalthea-audio-health` reports captured frames, capture xruns, ring overflow,
input starvation, maximum capture gap, output xruns and maximum FX render time.
The passive logger retains these with USB evidence. An unplug can produce a final
capture-recovery error, so assess counter increases while the source is connected.
Synthetic 35 ms delivery bursts remain continuous without starvation or overflow.
Physical clean-audio confirmation after this change remains an owner test.

## Unresolved crackling: diagnostic capture — 0.21.3

The owner's 0.21.2 test still crackled. While connected, the retained snapshot
showed 2,973,212 captured frames, zero capture xruns, zero input starvation, zero
ring overflow, zero output xruns, 1652 frames of input fill and a 177 us maximum
FX render time. The later one capture recovery/starvation coincides with removal.
These measurements do not support buffer starvation or DSP overload during this test.

For this investigation only, create `/tmp/amalthea-audio-probe.enabled` before an
application restart. Startup consumes the marker and allocates the probe; normal
startup has no recorder. After one second of settling and the first non-silent
input, it records five seconds at both points. Raw USB is before ASRC/inserts/mix;
codec input is the final software stereo signal. A separate normal-priority writer
creates `logs/audio/raw-usb.wav`, `codec-input.wav` and `capture.txt`. No session
is saved, loaded or reconstructed. Raw mono tracks remain separate WAV channels;
stereo input remains stereo. Align the files for ingress buffering before comparing.
The codec-input recording is not an analog measurement of the headphone output.

### Follow-up evidence — 21 November 2026

The five-second Brick capture contains signal on USB channel 4 only. Raw input
peaks at 0.734 full scale; software output peaks at 0.572, with no full-scale
clipping in either recording. Comparing raw channel 4 at the default 80% strip
level with the software output in 2048-frame windows, searching ingress delays
of 1600–1800 frames, gives a median correlation of 0.99876 (minimum 0.98849).
This is a local waveform comparison, not a sample-exact or analog-output test;
periodic waveforms can give misleading unconstrained correlation peaks.

The owner reports that FM1 recordings made in Audacity are clean, while live
monitoring can crackle. That prioritizes investigation of live playback and clock
handoff, but does not establish that the Brick's incoming USB capture is clean.
A separate native CoreAudio capture from the named six-channel Bigorna input
received valid audio during a brief MIDI note audition on channel 4. Its different
notes and transport state prevent a direct comparison with the Brick sequence.
Neither the FM1 firmware nor its persistent settings were changed.

Next discriminating check: listen to the Brick's raw-input and software-output
WAVs on the laptop. If both are clean, investigate the codec/playback path; if
both crackle, investigate ingress with a matched source test; if only output
crackles, investigate resampling/mixing. Large adjacent-sample differences alone
do not prove a fault, because synthesized waveforms and note attacks can contain
legitimate sharp transitions. No additional buffer change is justified yet by
the retained zero-xrun measurements.

The owner subsequently confirmed that both Brick WAVs audibly crackle. This
locates an audible defect at or before the raw capture tap; downstream buffering
or the headphone output cannot be its sole cause. It does not yet distinguish
FM1 packet generation from the Brick's USB host reception.

`fm1_capture_probe` is a standalone ALSA comparison tool, built only for Brick.
It requires a card named Bigorna with six input channels at native 44100 Hz and
exclusive capture access. It opens that card's MIDI output, auditions notes
60/64/67 on channel 1 at velocity 85 (1.5 seconds each, 0.3-second gaps), and
records eight seconds of native input to RAM before writing a WAV. It bypasses
the appliance, ingress resampler, insert DSP and codec playback. Capture errors
invalidate the test; the log records them. Normal exit, SIGINT and SIGTERM release
any test note still held. No patch, transport, session or firmware writes occur.
Keep Bigorna's sequencer stopped and its patch unchanged for both host tests.

`tools/usb_capture_comparison.sh` runs this comparison detached while the
appliance dashboard remains visible. It allows ten minutes for laptop removal,
then ten minutes for Bigorna detection. After the eight-second test (or a handled
error), it restores USB device mode and the original port power. It refuses to
start while FX owns the role. Results go to `logs/audio/comparison/`; reconnect
the laptop afterward to retrieve them. The runner must be explicitly armed;
normal startup and appliance operation are unchanged. Hardware verification is
pending; this diagnostic is not a crackling fix.

The owner found the channel-4 reference too dirty to judge reliably. A brief
audition of all six existing Bigorna voices identified channel 1 as a much
smoother reference: in a sustained segment its second-difference RMS divided by
signal RMS was 0.0008, versus 0.1517 on channel 4. These are waveform roughness
measurements, not a perceptual quality score. The matched test now uses channel 1;
no patch changes were required. The owner's further listening is not required
to compare the next matched captures numerically.

### Matched host comparison: ingress defect reproduced

The standalone Brick test completed at six channels / 44100 Hz, captured 352571
frames, released all three notes and restored device mode. ALSA reported zero
capture errors. Compare 0.18–0.75 seconds after each detected note onset (rather
than fixed wall-clock windows, because the laptop and Brick capture starts differ):

| Note | Laptop maximum second difference | Brick maximum second difference | Laptop / Brick samples above 0.01 |
| --- | ---: | ---: | ---: |
| 60 | 0.000702 | 0.076874 | 0 / 5 |
| 64 | 0.000702 | 0.088867 | 0 / 16 |
| 67 | 0.000763 | 0.000763 | 0 / 0 |

Signals are normalized to full scale. Corresponding window RMS values agree
within 0.3%; the abrupt defects are not explained by different output gain.
The Brick waveform also contains isolated zero substitutions and a 25-frame zero
run in the first note. This reproduces an ingress defect without the appliance,
ASRC, DSP or audio output. It does not yet identify whether the fault is in the
host controller/driver path or the FM1's interaction with that host.

The installed kernel has USB_MON, FTRACE, KPROBES and DYNAMIC_DEBUG disabled.
There is therefore no existing packet trace to enable. `fm1_packet_probe` uses
the Linux USBFS interface instead, with no additional library or kernel change.
It strictly checks the Bigorna product, six-channel/16-bit/44100-Hz format,
552-byte ISO endpoint 0x84 and MIDI OUT endpoint 0x01 before changing anything.
It temporarily detaches only the expected snd-usb-audio driver, claims interfaces,
records eight seconds using eight queued eight-packet ISO URBs, and sends the
same channel-1 note audition. All packet lengths, packet statuses and URB statuses
are retained in a bounded RAM buffer and written to a CSV beside the WAV.
Errored/missing packets are omitted from the WAV; use the CSV to interpret gaps.
Normal/handled exits release test notes, select audio alt 0, release interfaces
and request driver reconnection. Cable removal/reconnection also reprobes ALSA.
It does not bypass the OHCI host controller, so corrupted USBFS payloads would
still require host/controller versus device investigation.

Run the same explicit cable-swap runner with the `packets` argument for this
mode; the original ALSA WAV is preserved. Do not open FX during either test.
The underlying [USBFS ABI](https://github.com/torvalds/linux/blob/v4.9/include/uapi/linux/usbdevice_fs.h)
and [Linux USB audio capture implementation](https://github.com/torvalds/linux/blob/v4.9/sound/usb/pcm.c)
are the primary references. Packet-mode hardware verification is pending.
