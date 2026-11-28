# Amalthea manual

Build 56. Everything the TRIMUI Brick does under Amalthea. To install, see [the installation guide](README.md#installing).

## The Brick under Amalthea

### Buttons

Amalthea names the Brick's buttons by what they do. The same names are used throughout this manual.

| Button | Name in this manual | General use |
| --- | --- | --- |
| D-pad | Cross | Move the cursor; change values while editing |
| Bottom-right (Start) | Enter | Open; begin an edit; press again to commit |
| Bottom-left (Menu) | Play | Play / stop |
| Bottom-middle (Select) | Bank | Patterns, sessions, snapshots |
| L1 | Shift | Hold for the alternative function |
| R1 | FUNC | Hold for the second alternative function |
| L2 | Back | Previous page in your navigation history |
| R2 | Home | The main page of the current appliance |
| F1 / F2 (below the screen) | F1 / F2 | Previous / next track, page or tab (lit dim blue in the Tracker; F1 bright yellow in PERFORMANCE mode) |
| X | X | Draw at random: tap for options, hold 0.8 s to draw |
| Y, A, B | Y, A, B | Page shortcuts and PERFORMANCE mode (below) |
| Power | Power | Held 0.8 s and released: the power-off prompt |
| Volume keys | Volume | Headphone/speaker volume, with an on-screen bar |

"Hold" means 0.8 s unless stated otherwise. A held button acts once; letting it go does not act again.

### Starting, leaving and switching off

Power on: after the boot logo, the Amalthea home screen offers **TRACKER**, **FX RACK** and **LIFEBLOOD**. Choose one
with the cross and press **Enter**. The battery charge is shown top right (red at 15 % or less, a bolt while charging).

Every appliance **starts blank and stopped**. Nothing is recalled automatically and nothing is saved behind your
back: you load and save sessions yourself, from the bank pages.

To **leave an appliance**, hold **FUNC** first, then also hold **Shift**. (The reverse order does nothing.) A
prompt asks CANCEL / OK; OK asks SAVE BEFORE EXIT?, NO / YES. YES saves to the loaded session slot (or the
selected one) and returns home; NO discards the workspace and returns home.

To **switch off**, hold **Power** about a second and release it. POWER OFF? starts on NO; left/up selects YES,
right/down NO; a short press of Power confirms. The prompt warns when there are unsaved changes. A save already in
progress finishes before the Brick shuts down.

### Volume and output

The volume keys set the output level; a bar shows it for 1.5 s in every appliance. Output goes to the
headphone jack and the speaker as on the stock system.

**USB audio out.** While the USB-C port is not hosting an FM-1 (that is, outside FX RACK and LIFEBLOOD's capture),
the Brick presents itself to a connected computer or phone as a class-compliant audio input, *Amalthea Audio*
(stereo, 44.1 kHz, 16-bit): record the instrument's mix with any audio software, no driver needed. It carries
the mix before the volume keys, like a line output.

## TRACKER

A twelve-track generative tracker. Each track is a pattern of up to 16 steps in five columns, each column with its
own length, clock and direction; each track has fifteen instrument slots, one synthesis engine each, and plays
through two insert effects and the master chain.

### The pattern page

The page shows the current track (its number large, top right, in the track's colour), 16 rows (or 8 larger
rows) and five columns:

| Column | Holds |
| --- | --- |
| NOTE | A pitch (in the session's scale), OFF or TIE; plus length, pan, volume and the sends, each with its own condition |
| MOD | Empty (`---`), an OFFSET for that row's note (`+02`), a TRANSPOSE from that row on (`T+2`) or RANDOM (`RND`) |
| INST | The instrument slot for that row (`--` follows the track's instrument) |
| FX1 / FX2 | An insert effect and its dry/wet mix, with a LENGTH in steps, RANDOM and their conditions |

| To | Do this |
| --- | --- |
| Move | Cross |
| Previous / next track | F1 / F2 (a full-screen banner names the track) |
| Edit a cell | Enter; left / right select a field of the tab (its value, then its CONDITION), up / down change it; Enter commits, Back cancels |
| Browse the cell's tabs while editing | F1 / F2 (two tabs visible at a time; CONDITION stays selected if it was) |
| Choose an effect while editing FX1 / FX2 | Up / down on EFFECT (a large name overlay appears); DRY/WET sets the mix |
| Choose OFFSET, TRANSPOSE, RANDOM or empty while editing MOD | Up / down on TYPE; SEMITONES (−24 to +24) |
| Transpose the whole track an octave | Shift + up / down |
| Clear the whole track | Shift + B |
| ERASE mode | Shift + Enter; move anywhere, Enter clears the cell; Shift + Enter or Back leaves |
| Select several rows | FUNC + F1 / F2 (up / down from the cursor); Enter then edits all of them, ERASE clears them |
| Rotate the column one step | Shift + F1 / F2 (backward / forward; other columns stay) |
| Play / stop | Play |
| Track settings | B |
| Column settings | A |
| PERFORMANCE mode | Y (F1 leaves it) |
| Master effects | Shift + Y |
| Dashboard | Shift + Back |
| Patterns / sessions / mixer | Bank / Shift + Bank / FUNC + Bank |

While you edit a NOTE, scrolling the note itself auditions it at once, also while stopped or on a muted track
(LENGTH, the other parameters and the conditions change silently); nothing is stored until Enter. **FUNC + short X** on INST plays the track's instrument on the scale's root, octave 4.

**NOTE tabs:** the note, LENGTH, PAN, VOLUME, the five sends (DELAY, REVERB, ABRASION, CORROSION, MODULATION) and
the three macro sends. Each tab holds its value and then its own CONDITION. One step below 0 a lock returns to the
track's own value (TRACK); up from TRACK starts at the track's value. Step volume is relative to the track fader. A
note's PAN, VOLUME and sends hold from its start until the next note plays, and track level and pan glide over
5 ms, so panning consecutive notes apart does not click.

**FX tabs:** EFFECT with its DRY/WET MIX (0 % dry, 100 % fully processed; a newly chosen effect starts at 50 %),
LENGTH (1–64 steps: the effect stays on its slot over the following rows, empty rows keep it running, a new effect
row replaces it) and RANDOM (YES / NO): with YES the effect takes a newly drawn set of its own parameters every time
the step plays, never the same draw twice in a row, while its mix stays as set; a small square marks the cell. Each
tab has its own CONDITION. Effects with memory (reverbs, delays, granular, resonators) ring out on their own when
they end or are replaced. No effect can feed itself into a runaway: every insert stays within reach of a fading
memory of its input, so tails always die away.

**Conditions** A:B make a parameter act on cycle A of every B cycles of its own column, from 1:1 to 8:8
(36 choices). Every parameter of a step has its own condition, independent of the others: a parameter whose
condition is not met that cycle plays as if unset (a lock takes the track's value, LENGTH is one step, RANDOM is
NO), and no note or effect plays when its own condition is not met. INST and MOD keep one condition each: INST
chooses the slot or the track default; an inactive OFFSET adds nothing, and an inactive TRANSPOSE leaves the
transposition as it was on that cycle.

**MOD.** Every MOD cell starts empty. An **OFFSET** shifts only its own row's note. A **TRANSPOSE** shifts every
note from its row to the end of the MOD column's cycle, or until the next TRANSPOSE (`T+0` returns to the written
notes); offsets are added on top, so `+02` under `T+1` plays three semitones up. Each MOD cycle starts
untransposed: a TRANSPOSE on step 1 transposes the whole sequence, one on step 9 only what follows it, and a
TRANSPOSE with condition 1:2 transposes every other cycle while the others play as written. Up / down on an
empty cell makes an OFFSET. Sessions from before build 38 load their `+00` cells as empty.

A **RANDOM** cell (`RND`) redraws parameters of its row's note each time it is applied. Its TARGETS tab chooses
them: ANY (the default: one of NOTE, LENGTH, PAN, VOLUME and the sends, drawn afresh each time) or up to three
named parameters, all redrawn each time. A random NOTE stays in the session's scale and root, within an octave of
the written note.

### PERFORMANCE mode (Y)

Y enters PERFORMANCE mode; F1, lit yellow, leaves it. Nothing done in the mode is written to the pattern: leaving
drops all of it and the pattern plays as stored. Four tiles show what each button has done, placed as the buttons
are; the header shows the last press. Every button is a toggle: on, off (bypass), then on with a new draw.

| Button | Does |
| --- | --- |
| X | A chain of up to four effects (a COMBO: GRIND, ACID, CAVERN, HAUNT, LURCH, CORRODE) on one to three tracks that hold notes, after their own FX |
| B | New parameters for the chain's effects |
| A | Six LFOs (sine or sample and hold, each with its own rate and depth) on the chain's parameters |
| Y | Six LFOs on the instruments of one to three tracks: live on VAP, PERC and CHIP's timbre; on the other engines, a walk through up to eight variations, note by note |

Everything stays within the Brick's audio budget.

### Drawing at random

X is the generator, everywhere in the Tracker.

| To | Do this |
| --- | --- |
| Randomizer options for this column (MODE, DENSITY) | Tap X; tap again to return |
| Draw the column | Hold X |
| Draw NOTE with pan, volume, sends and a condition for each parameter | Shift + hold X on NOTE |
| New sound for the track's instrument (written on every INST row) | Hold X on INST; an overlay names it |
| Random instrument slots per step | Shift + hold X on INST |
| A variation of each row's instrument (pitch kept) | FUNC + hold X on INST |
| Draw the FX column: effects, mixes and their parameters | Hold X on FX1 / FX2 |
| The same, with positive trigger conditions | Shift + hold X on FX1 / FX2 |
| While editing a cell: new value for the selected field (value or condition) | Tap X (Enter keeps it) |
| While editing: that field over the whole column (rows that hold a note) | Shift + hold X |
| While editing an FX cell: new parameters / new effect | Hold X / Shift + hold X |

**MODE** is RANDOM (each generator places the events) or EUCLIDEAN (events spread as evenly as possible over the
column's steps, at a random rotation). **DENSITY** (left/right, Shift for ×10) is the exact share of active
steps that receive an event; 45 % to begin with. Both are remembered per track and column and saved with patterns.

The NOTE generator draws an idiom (ostinato, pedal point, drone, walk, lament, stab, call and answer, arpeggio,
spiral), a rhythm model (euclidean, metric, offbeat, cluster, scatter, anchored) and a register, always in the
session's root and scale. On a PERC track it writes a drum pattern on the kit's note map; on an FM DRUM track a
one-voice rhythm around the recipe's pitch. Each boot starts from fresh entropy: draws never repeat between
sessions.

### Track settings (B)

Up / down selects a row, left / right changes it, F1 / F2 switches tracks.

- **LENGTH** 1–16 steps (shortening keeps the hidden notes).
- **CLOCK** 1/8, 1/4, 1/2, 1×, 2×, 4× of the session's sixteenth-note clock.
- **DIRECTION**: forward, reverse, ping-pong, pendulum, odd-even, rocking, cascade, pairs, blocks reverse, blocks
  retro, converge, diverge, double, stride, drunk, meander, shuffle, random, fragments.
- **INSTRUMENT** (the default slot, 1–15) and **ENGINE** (replace that slot's engine on this track).
- **VOLUME**, **PAN** and the sends in chain order: ABRASION, CORROSION, MODULATION, DELAY, REVERB.
- Three macro profiles (ABRASION, CORROSION, MODULATION): SOURCE MASTER follows the shared master settings;
  editing makes a local TRACK profile; left on SOURCE returns to MASTER.

### Column settings (A)

LENGTH, CLOCK and DIRECTION for the selected column. Each starts at TRACK (following the track); an explicit
value overrides it for that column only. Columns with the same length, rate and direction stay in step.

### Instruments

Each track has its own fifteen instrument slots, one engine each; editing a sound on one track never changes
another track.

**Row variations.** FUNC + hold X on INST gives every row that holds an instrument its own variation of that
instrument's sound: the same family, with its timbre, filter, envelopes and modulation and effect amounts redrawn
for that row. Pitch, tuning, detune, FM ratios, glide, pitch envelopes and modulation, drum tuning and the output
level never change, so the notes play exactly as written. A small square after the slot number marks a varied
row. With rows selected (FUNC + F1 / F2) only those rows vary; while editing an INST cell, only that row. A track
holds up to 16 distinct variations (later rows of a longer column reuse their instrument's). Undo restores; hold
X on INST draws a new sound and clears the column's variations; ERASE or another instrument clears a row's.
Variations are saved with sessions and patterns.

| Slot | Engine | Character |
| --- | --- | --- |
| 01 | VAP | Analogue-style voice: 18 families (sub pressure, EBM bass, acid wound, mourning pad, noise wall, impact ...) |
| 02 | PERC | Drum kits in five characters (EBM, industrial, rhythmic noise, ritual, broken) |
| 03 | CHIP | 12 families: crush bass, zap hit, LFSR storm, dead bell ... |
| 04 | FIBER | The Fiber firmware's engine: experimental, ambient and open patches |
| 05 | HIVE | Bigorna's seven-oscillator swarm |
| 06 | OP4 | Bigorna's four-operator, eight-algorithm FM |
| 07 | ANIMA | Six simulated fingers on band-limited tables, FM, grains, filters and a tuned resonator (32 families) |
| 08 | RUSSOLO | The five Noisferatu banks: generative buffers, noisy textures, bit-address modes, logic oscillators (45 families) |
| 09 | BAY AREA | Wavefold and FM voice through a low-pass gate, with resonators and reverb (24 families) |
| 10 | RESURGENT | A two-scene Surge-style synthesizer: three oscillators per scene, filters, 12 LFOs (32 families) |
| 11 | CALIGULA | Eight oscillators in four interacting pairs, hyper-LFO, modulated delays and feedback (24 families) |
| 12 | FM DRUM | The Digitone II's FM drum machine, with Bigorna's 27 drum recipes |
| 13 | OBRADOR | Tonitor's Oberheim-style polysynth voice: strings, brass, PWM pads, basses, sync leads, drones (20 families) |
| 14 | RINGS | Mutable Instruments Rings: six resonator models, struck or bowed, chords, the Disastrous Peace effects (26 families) |
| 15 | PLAITS | Mutable Instruments Plaits 1.2: all 24 models through its low-pass gate (47 families) |

A blank session starts every track on a drawn patch. [Engine details](app/docs/ENGINE-PORTS.md).

### Master effects (Shift + Y)

F1 / F2 chooses the module; up / down a parameter; left / right changes it (Shift for larger steps). The chain is
**ABRASION** (band-weighted drive, clipping, folding) → **CORROSION** (bit-depth and sample-rate reduction) →
**MODULATION** (flanger and eight-stage phaser) → **COMPRESSOR** → **DELAY** → **REVERB** → **OUTPUT** with a
stereo-linked limiter. Each track sends into each bus; every module has a mix. Values are shown in real units
(Hz, ms, dB, ratio, note divisions). Tails ring out after Stop.

### Mixer (FUNC + Bank)

Two pages of six channels (F1 / F2): level, meters, mute and solo. Enter begins and commits an edit; volume uses
up / down, mute and solo either axis. Several solos may be on; mute wins.

### Patterns and sessions

**Bank** opens the 8 × 4 bank of 32 patterns of the current session; **Shift + Bank** the 8 × 8 bank of 64
sessions.

| To | Do this |
| --- | --- |
| Select a slot | Cross |
| Load it | Tap Bank |
| Save into it | Hold Bank 0.8 s |
| Its settings: tempo, scale, root note, switch quantize | Enter (A returns) |

`+` marks a saved slot, `!` a damaged one; the loaded slot is filled blue. A pattern holds everything about the
twelve tracks; tempo, scale and root default to the session's. While playing, a pattern load waits (amber NEXT)
until the end of the current loop of the longest or shortest track (SWITCH QUANTIZE). In the settings, START puts
a value in edit mode, and the die beside SCALE / ROOT NOTE rolls a new pair from 38 scales. A session holds its
32 patterns, its working state and defaults. Saving is atomic and checked; nothing is ever written except when you
save.

### Dashboard (Shift + Back)

Five tiles: TRACK, MIXER, MASTER FX, INSERT FX and DEVICE. INSERT FX lists the processors: switching one off
bypasses it on every track (without deleting it) and keeps it out of the draws. DEVICE sets the screen brightness.
Both are device preferences, kept apart from sessions.

## FX RACK

A twelve-slot effect rack for an M-VAVE FM-1. The Brick hosts the FM-1 over USB, takes its separate tracks and
runs each through two insert slots, every slot with its own 16-step sequencer of parameter locks.

### Connecting the FM-1

The Brick has one USB-C port. Open FX RACK first, with the computer (if any) still connected; the screen says
USB: UNPLUG LAPTOP. Unplug it: after three seconds the port switches to host mode and powers the FM-1. Connect
the FM-1 (after five seconds). To reconnect a computer, leave FX RACK first (FUNC then Shift).

Each FM-1 firmware gives its own strips: Bigorna, Tonitor and Lilliput send six mono tracks, Norilsk three; a
firmware in stereo mode gives one stereo strip. MIDI clock from the FM-1 (Bigorna sends it) drives tempo and
transport; without it, the FX RACK session tempo and Play are used. The FM-1's audio is read through a 32-tap windowed-sinc
interpolator, flat to 18 kHz while it follows the FM-1's clock, and the output limiter looks 1.5 ms ahead and acts
only above −0.2 dBFS.

### The rack

| To | Do this |
| --- | --- |
| Choose a strip and its FX1, FX2, level or BYPASS | Cross |
| Open the slot's sequencer / edit the level / toggle bypass | Enter |
| New effect type with new parameters, this slot only | Hold X on FX1 / FX2 |
| Same effect, new parameters | Shift + hold X on FX1 / FX2 |
| Play / stop the internal transport | Play |
| FX RACK sessions (64) | Bank |
| Back / rack | Back / Home |

BYPASS passes the strip dry, crossfaded over 10 ms; its sequencers keep running.

### Slot sequencer

Two rows of eight steps; the lit step follows playback.

| To | Do this |
| --- | --- |
| Parameter locks of a step | Enter on the step; up / down selects, left / right sets |
| Return a locked parameter to the default | Shift + left |
| Slot default parameters | B; F1 / F2 pages them |
| LENGTH, CLOCK, TEMPO, DIRECTION, SEQUENCER on/off | A |
| DENSITY and COMPLEXITY | Y (or tap X) |
| Generate triggers and locks | Hold X |

While the transport runs, a sequenced slot is heard only on triggered steps; empty steps pass dry. Stopped, or
with SEQUENCER OFF, it processes continuously. COMPLEXITY 0 gives every generated step the same random setting,
100 every step its own. TEMPO per slot follows the session, or 30–300 BPM; external clock overrides it.

## LIFEBLOOD

The FM-1's audio, through a generated network of the same effects, drawn as a living field: circular nodes,
vessels between them and corpuscles travelling along the flow. It starts with IN joined straight to OUT.

| Control | On the field | On a node's parameters |
| --- | --- | --- |
| X | Grow a new network from the session's CHARACTER | A new effect for this node |
| Y | New parameters for every node | New parameters for this node |
| A | Rewire: new vessels, same nodes and parameters | Back to the field |
| B | Start / stop angiogenesis | Back to the field |
| Cross | Nearest node in that direction | Up / down parameter, left / right value (Shift ×10) |
| F1 / F2 | Previous / next node in flow order | Previous / next page |
| Enter | Open the node | — |
| Play | Bypass: crossfade to the dry input and back | same |
| Bank / Shift + Bank | Snapshots (32) / sessions (64) | same |
| Shift + A | Session settings | same |

**Session settings** have three tabs, two shown at a time: GENERAL, COMP and EQ (F1 / F2 move between them; up /
down choose a row, left / right change it, Shift for coarse steps).

GENERAL: TEMPO (while the FM-1's clock is locked it shows `EXT` and the FM-1's tempo, and cannot be dialled); PATH,
ANY (the default: either path, at even odds, each time X grows a network), ABSTRACT (textures, spaces, colour) or
RHYTHMIC (cuts, gates, clockable textures, everything synced and at least 72 % wet); CHARACTER, the recipe within
the path (ANY; DRIFT, GRIT, SPACE, BLOOM; PULSE, CUT, GATE, MAYHEM, INDUSTRIAL); NODES, the largest network (3–7);
GROWTH, the angiogenesis rate (6–120 mutations a minute); MIX, the network's wet share (a new session starts at
50 %).

**Mastering.** COMP and EQ form a mastering stage on the whole output, after the network and its MIX and before the
output limiter: the 10-band EQ first, then the compressor, so the compressor reacts to the balance you hear. Both
are off by default (FM-1 firmwares usually have their own compressor). Each fades in and out over 20 ms; Play's
bypass keeps mastering on, so the comparison is fair. Sessions keep both; sessions from before build 56 load with
mastering off.

- **COMP:** COMPRESSOR on / off, PROFILE, THRESHOLD (−40 to 0 dB), RATIO (1.5:1 to 20:1), ATTACK (0.1–100 ms),
  RELEASE (20 ms – 2 s, or AUTO, program dependent), KNEE (0–24 dB), MAKEUP (AUTO or 0–24 dB), SC FILTER (a
  high-pass on the detector only, OFF or 40–200 Hz, so the sub does not pump the mix) and MIX (parallel
  compression). The tab shows the gain reduction live.
- **EQ:** EQ on / off, PROFILE and ten faders at 31, 62, 125, 250, 500 Hz, 1, 2, 4, 8 and 16 kHz, ±12 dB each;
  31 Hz and 16 kHz are shelves, the rest bells that narrow as they are pushed further.
- **Profiles** are starting points; changing any value afterwards shows CUSTOM.

| COMP profile | For |
| --- | --- |
| GLUE | Bus glue: techno, dark synthwave |
| PUNCH | Industrial techno: hits come through |
| PUMP | Dark synthwave, techno: the kick pumps |
| WALL | Power noise: a dense wall |
| SMASH | Rhythmic noise: parallel crush |
| DRONE | Dark ambient, drones: slow levelling |
| ACOUSMATIC | Acousmatic: keeps the dynamics |

| EQ profile | For |
| --- | --- |
| FLAT | No change |
| INDUSTRIAL | Kick weight, less mud, bite |
| POWER NOISE | Body, harsh 2–8 kHz tamed |
| RHYTHMIC | Kick and snap |
| SYNTHWAVE | Deep lows, open top |
| DARK AMBIENT | Warm and dark |
| ACOUSMATIC | No rumble, a little air |
| TEXTURE | Detail in the mids and highs |

**Angiogenesis:** nodes drift; one mutation at a time a vessel is cut or sprouts, a node buds or is absorbed, or a
node redraws its parameters. Vessels fade in and out over 120 ms; a new network crossfades from the old over half
a second, letting its tails ring. Networks are always feed-forward (no feedback loops) and stay within the
Brick's CPU budget.

With a multitrack firmware, each FM-1 track is its own IN, numbered down the left edge; only tracks heard in the
last 30 seconds are routed, and about a third pass straight to OUT. Each track sits where the FM-1 pans it,
through the network or straight to OUT: the FM-1 sends each track's pan as CC 10 on MIDI channels 1–6 (BIGORNA
from build 121, in USB TRACKS mode). A track whose pan has not arrived is centred. With BIGORNA from build 122,
LIFEBLOOD also runs the FM-1's own CHORUS, DELAY, REVERB, master compressor and ceiling after the network, from the
state BIGORNA sends (the FM-1's USB tracks are taken before its sends), and the header shows `+ FX`. Bypassed,
LIFEBLOOD is then the FM-1's own mix. Every insert, and the whole network, is
loudness-matched to its input: engaging it changes the sound, not the level.

## Effects

The same inserts serve every instrument: Tracker FX columns, FX RACK slots and LIFEBLOOD nodes. Every parameter is
shown in its real unit.

- **Drive and destruction:** DISTORTION, SATURATION, OVERDRIVE, FUZZ, RECTIFY, DIODE, RAT DRIVE, WAVEFOLD, AMP
  SIMULATOR, BIZTORTION, FRACTURE 2, CRAZY IVAN, BITMURDERER, BITCRUSH, RATE REDUCE, LO-FI, CRUSHCLONE, CYANIDE,
  POLARIZER, ANGR, MULTIBAND DIST, DISINTEGRATE, BITGLITTER, PAFNUTY.
- **Filters and colour:** LOWPASS, HIGHPASS, MULTIMODE FILTER, FREQUALIZER, COMBULATOR, RESONATOR, REZ SYNTH.
- **Time and space:** QDELAY, CHOWMATRIX, CLOUDSEED SPACE, CLOUDS GRANULAR, RINGS, BEADS, SOFTCUT, TRANSVERB.
- **Motion:** TREMOLO, PAN, ROTOR, FLANGER, PHASER, TREMOSQUARE, GATE SHAPER, CHOPPR, RING MOD, FREQ SHIFTER,
  WARPS.
- **Cuts, repeats and glitches:** STUTTER, OOPS, BUFFER OVERRIDE, LIVECUT, SUPATRIGGA, BEAT REPEAT, TIME SHAPER,
  JUMBLR, SCRUBBY, GLITCHLETS, SKIDDER, GLITCHSHIFTER, PITCHNASTY, AMALGAM, GEOMETER, REPSYCHO.
- **Spectral and voices:** BROKENFFT, TREEMONSTER, SMR, VOCODER, MDA VOCODER, TALKBOX.
- **RANDOM**: a different effect each time it is drawn.

Rhythmic effects follow the FM-1's MIDI clock or the session tempo and realign on Start. Their sources, licences
and adaptations: [native effects](app/docs/NATIVE-EFFECTS.md), [ported inserts](app/docs/INSERT-PORTS.md),
[effects and references](app/docs/EFFECTS-AND-REFERENCES.md).

## Updating, recovery and going back to stock

- **Update:** run the installer of a newer release while Amalthea is on the home screen. It copies your sessions
  to the computer first.
- **Sessions on the computer:** they live on the Brick in `/mnt/UDISK/tracker-linux` (`session-slots/`,
  `fx-session-slots/`, `lifeblood-session-slots/`). The installer copies them on every update; you can also
  `adb pull /mnt/UDISK/tracker-linux` at any time.
- **Recovery:** if an installation is interrupted, the Brick's boot manager never boots a half-written system: it
  stays in a RAM recovery shell reachable over USB (ADB). Run the installer again to finish, or the restore script.
- **Stock:** `restore.sh` / `restore.bat` writes back the stock backup the installer took.
