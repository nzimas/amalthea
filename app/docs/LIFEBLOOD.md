# Amalthea LIFEBLOOD

LIFEBLOOD is the third appliance on the home screen. It takes the FM1's audio
over USB (the same host capture as FX RACK) and passes it through a generated
network of the shared Amalthea inserts. The field shows the network in the
manner of Vascular: circular nodes, vessels between them and corpuscles that
travel along each vessel in the direction of flow.

Startup is blank and stopped: only IN and OUT, joined directly, so the input
passes dry. Nothing is recalled until a session is loaded deliberately.

## Controls

| Control | Field | Node parameters |
| --- | --- | --- |
| X, short press (or hold) | Grow a new network from the session's CHARACTER | A new effect for this node, with new parameters |
| Y | New parameters for every node; nodes and vessels stay | New parameters for this node |
| B | Start / stop angiogenesis | Back to the field |
| Cross | Select the nearest node in that direction | Up/down: parameter; left/right: value (Shift: ×10) |
| F1 / F2 | Previous / next vertex in flow order | Previous / next page (MATRIX, EQ, BIZTORTION) |
| Enter | Open the selected node's parameters | — |
| A | Rewire: new vessels between the same nodes, parameters kept | Back to the field |
| Shift + A | Session settings (from any LIFEBLOOD page) | Session settings |
| Volume keys | Volume overlay, as in the Tracker | Same |
| Play | Bypass: crossfade to the dry input and back | Same |
| Bottom-middle, tap | Snapshots | Snapshots |
| Shift + bottom-middle | Sessions | Sessions |
| R2 | Return to the field | Return to the field |
| L2 | Back | Back |
| FUNC then Shift | Exit, with the save prompt | Same |

Snapshots (32 per session) and sessions (64 slots) use the Tracker's bank
gestures: the cross selects a slot, a tap on the bottom-middle button loads it,
a 0.8 s hold stores or saves it, Enter opens the session settings. A stored
snapshot is written into the loaded session file at once; with no session
loaded it stays in memory and the bar says so, until a session is saved.
Session files live in `lifeblood-session-slots/` beside the other banks, in
format AMLB2 (AMLB1 files load with one stereo IN and the PATH their CHARACTER implies).
Installs back these files up with the other banks.

### Session settings

- **TEMPO** — for tempo-synced delays and modulation. While the FM1's MIDI clock is
  locked the row shows `EXT` and the FM1's tempo, the effects follow it, and the session's
  own value cannot be dialled (it returns when the clock stops). The clock's tempo is
  counted over the last two beats of pulses and rounded to a whole BPM, so pulses that
  USB delivers in bursts do not read slow.
- **PATH** — ANY (the default), ABSTRACT or RHYTHMIC (after Vascular's guidance paths).
  ANY takes either path, at even odds, each time X grows a network (a chosen CHARACTER
  sets the path it belongs to); the network then follows that path. RHYTHMIC grows
  cuts, gates, permutations and clockable textures (granular and loopers lean in; any
  effect with a SYNC switch is synced), keeps long reverb tails rare and keeps every
  effect at least 72% wet. ABSTRACT grows textures, spaces and colour, with cuts and
  gates only occasional.
- **CHARACTER** — the recipe X grows from, within the PATH: ANY (a different one each
  time); ABSTRACT: DRIFT, GRIT, SPACE, BLOOM; RHYTHMIC: PULSE, CUT (stutters, slicers,
  repeaters), GATE (gates and chops), MAYHEM (cuts into destruction), INDUSTRIAL
  (gated, driven, filtered). A rhythmic recipe always opens its lead vessel with a cut
  or a gate; every cut and gate follows the external clock or the session tempo.
- **NODES** — the largest network X may grow and angiogenesis may reach (3–7).
- **GROWTH** — angiogenesis speed: 6–120 mutations a minute; drift speeds up too.
- **MIX** — the whole network's wet share; the rest is the dry input. A new session starts at 50%.

Session settings have three tabs, two shown at a time: **GENERAL** (the rows above),
**COMP** and **EQ**. F1 and F2 move between them; UP/DOWN choose a row, LEFT/RIGHT change
it (with Shift: coarse steps).

### Mastering: EQ, then COMP

Mastering runs on the whole output, after the network, MIX and the FM1's mirrored returns,
and before the output limiter: the 10-band EQ, then the compressor. Corrective EQ comes
first, so the compressor responds to the balance you hear. Both stages are **off** by
default (an FM1 firmware usually has its own compressor). Each stage fades in and out over
20 ms, and a stage that is off costs nothing. Play (bypass) compares the network with the
dry input; mastering stays on for both, so the comparison is level-fair. Sessions keep both
tabs (AMLB3); older sessions load with mastering off.

Each tab has a **PROFILE** row. Choosing a profile loads its values, and any value changed
by hand afterwards turns the profile into CUSTOM. Profiles are starting points for the music
LIFEBLOOD is made for:

| COMP profile | For | Settings |
|---|---|---|
| GLUE (default) | bus glue: techno, dark synthwave | -16 dB, 2:1, 30 ms, AUTO, knee 6, SC 90 Hz |
| PUNCH | industrial techno: the hits come through | -18 dB, 4:1, 10 ms, 100 ms, knee 4, SC 90 Hz |
| PUMP | dark synthwave, techno: the kick pumps the mix | -24 dB, 6:1, 3 ms, 200 ms, knee 2, SC off |
| WALL | power noise: a dense wall | -28 dB, 10:1, 0.3 ms, 50 ms, hard, SC 60 Hz |
| SMASH | rhythmic noise: parallel crush | -32 dB, 20:1, 1 ms, 100 ms, hard, SC 90 Hz, MIX 35% |
| DRONE | dark ambient, drones: slow levelling | -24 dB, 1.5:1, 50 ms, 1.2 s, knee 12, SC 40 Hz |
| ACOUSMATIC | electroacoustic: only the loudest gestures | -8 dB, 3:1, 5 ms, AUTO, knee 8, SC 40 Hz, makeup 0 |

| EQ profile | Character | 31 / 62 / 125 / 250 / 500 / 1K / 2K / 4K / 8K / 16K (dB) |
|---|---|---|
| FLAT (default) | no change | all 0 |
| INDUSTRIAL | kick weight, less mud, bite | -2 +3 +1 -2 -2 0 +1 +2 +1 +1 |
| POWER NOISE | body; the fatiguing 2–8 kHz tamed | -3 +2 +2 +1 0 -1 -2 -3 -2 0 |
| RHYTHMIC | kick and snap | -1 +3 +2 -1 -2 0 +2 +1 -1 -1 |
| SYNTHWAVE | deep lows, open top | 0 +2 +1 -1 -2 -1 0 +1 +2 +3 |
| DARK AMBIENT | warm and dark | +1 +2 +2 +1 0 -1 -2 -3 -3 -4 |
| ACOUSMATIC | no rumble, a little air | -4 -1 0 0 0 0 0 0 +1 +2 |
| TEXTURE | detail in the mids and highs | -2 -1 0 0 +1 +2 +2 +2 +1 0 |

**COMP** rows: COMPRESSOR (ON/OFF), PROFILE, THRESHOLD (-40–0 dB), RATIO (1.5:1–20:1),
ATTACK (0.1–100 ms), RELEASE (AUTO or 20 ms–2 s), KNEE (HARD–24 dB), MAKEUP (AUTO or
0–24 dB), SC FILTER (OFF or 40–200 Hz), MIX (0–100%, parallel compression). The tab's
header shows the gain reduction live.

- Feedforward design with the gain computed in dB and a soft knee. The detector is the
  smooth decoupled peak detector from Giannoulis, Massberg and Reiss, "Digital Dynamic
  Range Compressor Design — A Tutorial and Analysis" (JAES 2012), applied to the gain
  reduction.
- Stereo linked on the louder channel, so the image never shifts.
- SC FILTER is a 12 dB/octave high-pass on the detector only. Kicks and sub then stop
  pumping the mix, and the audio itself gets no phase shift.
- AUTO release adds a slow branch (400 ms in, 1.5 s out) under the fast 60 ms one.
  Sustained compression lets go slowly, while bursts on top of it still recover fast.
- AUTO makeup gives back half the reduction a full-scale signal would get, at most +12 dB.
- log2/exp2 run only on the control path, as fast approximations.

**EQ** rows: EQ (ON/OFF), PROFILE, then ten faders at the ISO octaves, ±12 dB each.

- The filters are Andrew Simper's (Cytomic) trapezoidal state-variable filters. They have
  the same response as RBJ biquads, but stay quiet while a gain glides and stay precise at
  31 Hz, where a direct-form biquad loses accuracy.
- 62 Hz–8 kHz are bells; 31 Hz is a low shelf and 16 kHz a high shelf. A 16 kHz bell
  would be squeezed against Nyquist at 44.1 kHz, and shelves are the useful shape for sub
  and air.
- Q is proportional: broad at small gains, one octave at ±12 dB.
- A band at 0 dB is exactly unity and is skipped, so FLAT is bit-exact.
- A changed gain glides 1 dB per 64 frames, and coefficients are recomputed only while
  a gain is moving.

Measured on the Brick (build 56, `benchmark_lifeblood master`): with every band in use
and the compressor on AUTO with SC FILTER, the stage takes 163 µs per 256-frame block
(p99 172 µs), about 3% of the 5805 µs deadline. Bands at 0 dB and stages that are off
cost nothing.

## Recipes

Generation is targeted, after Vascular's patch weaver:

- One to three vessels leave IN, each one to three nodes deep; some vessels
  merge again (35% per later node) and some networks keep a dry vessel from IN
  to OUT. A recipe may add a shared reverb tail that every vessel drains into.
- Each recipe favours effect families (space, time, texture, colour, drive,
  motion) by weight. The same family twice in a row in one vessel is rare; the
  same kind twice in a network rarer still.
- A harshness budget (0–2, or 2–4 for GRIT) limits abrasive inserts such as
  FUZZ, RECTIFY, BITS and IVAN; destructive inserts never follow one another in
  a vessel.
- A reverb comes last and drains only into OUT; at most one SPACE, CLOUDS,
  MATRIX and RINGS per network.
- Node mixes follow the effect's character: destructive 34–58%, motion and
  texture 46–72%, the rest 58–90% (FLANGER and PHASER keep their own high mix).
  Parameters come from the shared playable draws; network delays keep feedback
  at 60% or less and CLOUDS never freezes.
- Levels are set to be heard: node mixes run 50–100% (cuts and gates 85–100%), vessel
  levels stay within 2 dB of each other, dry vessels are rarer and quieter, and
  vessels into OUT sum louder (by the square root of their count) with the output
  limiter catching peaks. Several vessels into one node are averaged. Each node, and
  then the whole network, is loudness-matched to its input (see the Build 35 notes), so
  engaging the network modifies the sound without ducking it.
- Every network is strictly feed-forward: vessels only run up the flow order,
  so no feedback loop can form, in a generated network or during angiogenesis.
- A CPU weight ceiling keeps each network within the Brick's audio budget. In a
  network QDELAY is drawn as a tape and stereo delay (timing, repeats, width, swing,
  modulation, tape; its pre-distortion, diffusion, crusher, shifter, phaser, ducking
  and EQ stay off) and MATRIX keeps its two delay nodes on the default delay type.

Every value changed on a LIFEBLOOD page (node parameters, session settings) is shown
large in an overlay while it is dialled, as in the Tracker.

## Rewire (A)

After Vascular's rewire: the nodes take a new random flow order (reverbs last), each
hangs from IN or a node already placed, 35% of later nodes also drink from a second
earlier one, and every loose end drains into OUT at the vessel role levels. Recipe rules
still hold (reverb only into OUT, no destructive node into another). Node types and
parameters are untouched, so no processor is rebuilt: vessels crossfade over 120 ms.

## Angiogenesis

As in Vascular, motion and rewiring are separate. Nodes drift and bounce off
the field's walls. At the GROWTH rate, one mutation happens at a time: a vessel
is cut (longer ones more readily) or sprouts toward a near partner; a node buds
from a host; a node is absorbed, its inputs joined to its outputs before its
vessels wither; or one node redraws its parameters. Every node always lies on a
path from IN to OUT, and the network never shrinks below two effects.

Signal handling: vessels fade in and out over 120 ms; a node that redraws its
parameters dips to dry over 25 ms, changes, and returns over 120 ms. A new
network (X, a snapshot or a session) crossfades from the old processors to
fresh ones over 0.5 s, so reverb and delay tails of the old network fade
naturally; the next new network waits until that crossfade has finished.

## Inputs

A firmware sending a stereo mix gives one IN. A firmware sending separate tracks (six,
or three as Norilsk does) gives one input per track, drawn down the left edge and
numbered; the output is always one stereo OUT. Each track enters at unity, placed where
the FM1 pans it with the FM1's own mixer law (0.707 at the centre), so the dry sum is the
FM1's own mix bus. The FM1 takes its USB stems before its pan,
so LIFEBLOOD reads the pans from the FM1's MIDI: CC 10 on channel 1–6 is the pan of
track 1–6 (BIGORNA sends it with GLOBAL SETUP > MIDI > SEND CC on and the default OUT
channels), with the FM1's constant-power law, gliding over 50 ms. A track whose pan has
not been received is centred. Pans received stay known while the appliance closes and
reopens; another firmware starts centred again.

LIFEBLOOD listens: a track counts as heard after any sample above -70 dBFS within the
last 30 seconds, and silent tracks are dimmed. X and A route only heard tracks: each
vessel drinks from one to three of them at random, and about a third of the heard
tracks pass straight to OUT untouched, so only some tracks of the FM1 are processed
at any moment. A track that starts sounding later joins straight to OUT, and
angiogenesis never routes a silent track. When the firmware's layout changes (stereo,
six or three tracks), the network moves onto the new inputs without being rebuilt.

## CPU on the Brick (build 29)

`tools/benchmark_lifeblood` measures each insert over 20 random Lifeblood draws,
then 60 grown networks including their crossfades, on one 256-frame block (5805 us).
Before the QDELAY and MATRIX bounds, random draws reached 2.7 ms and 7.4 ms for
those two alone, and networks 12 ms. With the bounds and the calibrated weights:

| | p99 | worst block |
| --- | ---: | ---: |
| Networks, steady | 2149 us | |
| Including 0.5 s crossfades | 2602 us | 4946 us |

The outgoing network of a crossfade renders on a DSP worker in parallel. These are
synthetic-signal measurements with the appliance idle beside them, not a guarantee
for every possible network; hand-edited node parameters can exceed the recipes' bounds.

## Input and output fidelity (build 40)

Bypassed (Play), LIFEBLOOD passes the FM1's input as it arrives; nothing else is in the path.

- The USB input is read at a fractional position, since the FM1's clock and the Brick's
  differ by some ppm (and a 48 kHz FM1 is converted). Linear interpolation did this until
  build 39: a low-pass that moved with the position, -2.4 dB at 10 kHz and -11 dB at 18 kHz
  half way between samples, swept several times a second. A 32-tap Kaiser-windowed sinc
  (beta 6, 512 phases) now stays within 0.005 dB of flat to 18 kHz at every position, 0.35 dB
  at 20 kHz. The FX RACK appliance reads its input the same way.
- The output limiter (FX RACK and LIFEBLOOD) looks 1.5 ms ahead, so its gain glides down before a
  peak instead of jumping on it, and recovers over 150 ms. It engages only above -0.2 dBFS;
  below that the output is untouched. Until build 39 it cut in at 0.96 within one sample and
  recovered in about 20 ms, which pumped and dulled hot mixes.
- With separate tracks, the FM1 sends each track before its pan and before its DELAY, REVERB
  and CHORUS sends and its master compressor. BIGORNA (build 122) broadcasts their state, and
  LIFEBLOOD runs them itself after the network (below); other firmwares give the dry mix.

## The FM1's effects after the network (build 41)

BIGORNA's own CHORUS, DELAY, REVERB, master compressor and output ceiling run at the end of
LIFEBLOOD's chain, as output infrastructure: never nodes of the network, never rewired. Their
code is BIGORNA's `fx.c`, compiled unchanged (`third_party/bigorna`, `src/bigorna_master.c`),
in its own fixed point and 32-sample blocks, and their settings follow the FM1 as it plays:

- BIGORNA, in USB TRACKS mode, sends SysEx `F0 7D 46 42 kind .. F7` (see `MidiFxMirror`):
  1, the effects' parameters and the tempo, on change, at START and every second; 2, each
  track's three sends in force (step locks included; a track's subtracks weighted by level),
  at most every 10 ms; 3, the returns' sidechain ducking, at most every 4 ms while a track ducks.
- Each message is stamped with the USB input position it arrived at and applied when the read
  reaches that position, so changes land with the audio they belong to.
- Sends: each stem by its sends, as on the FM1 (after level, before pan). Through the network,
  each node sends as the blend of what feeds it, weighted by its vessels' gains; MIX and bypass
  blend the sends as they blend the audio, so a bypassed LIFEBLOOD is the FM1's own mix with
  its own effects, compressor and ceiling.
- The header shows `+ FX` while the stage runs. Three seconds without BIGORNA's state, it stops
  (another firmware, or USB STEREO, whose mix already holds the effects).
- Cost on the Brick: `tools/benchmark_master` (see the build 41 notes).

