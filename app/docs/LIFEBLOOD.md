# Amalthea LIFEBLOOD

LIFEBLOOD is the third appliance on the home screen. It takes the FM1's audio
over USB (the same host capture as FX) and passes it through a generated
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
- **PATH** — ABSTRACT or RHYTHMIC (after Vascular's guidance paths). RHYTHMIC grows
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
- **MIX** — the whole network's wet share; the rest is the dry input.

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
numbered; the output is always one stereo OUT. Each track enters at FX's default strip
level of 80%, placed where the FM1 pans it. The FM1 takes its USB stems before its pan,
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
