## Build 39 — LIFEBLOOD follows the FM1's pans and clock

- LIFEBLOOD with separate FM1 tracks: each track is placed where the FM1 pans it, instead
  of a fixed spread by track number (track 1 was well to the left). The FM1's stems are
  taken before its pan, so LIFEBLOOD reads each track's pan from CC 10 on MIDI channels
  1–6 (BIGORNA build 121 sends it in USB TRACKS mode), with the FM1's constant-power
  law, gliding over 50 ms. A track whose pan has not been received is centred. Tracks
  keep their place whether they pass straight to OUT or through the network.
- The FM1's MIDI clock tempo is counted over the last two beats of pulses and rounded to
  a whole BPM. Pulses that USB delivers in bursts no longer read the tempo slow, and
  119.8 is no longer cut to 119, so synced effects run at the FM1's exact tempo. A tempo
  change on the FM1 is followed within about two beats.
- LIFEBLOOD's TEMPO setting shows `EXT` and the FM1's tempo while its clock is locked,
  and cannot be dialled then; the session's own tempo returns when the clock stops.
  FX's clock display uses the same whole-BPM tempo.

## Build 38 — MOD TRANSPOSE, empty MOD cells, FX draws

- MOD cells are empty by default (`---`) instead of `+00`. Older sessions load their zero
  offsets as empty cells; other offsets are unchanged.
- New MOD type TRANSPOSE (`T+n`, -24..+24): from its row to the end of the MOD column's
  cycle (or the next TRANSPOSE) every note is shifted, offsets included. Each MOD cycle
  starts untransposed; a cycle condition on a TRANSPOSE decides on which cycles it
  applies. Left/Right in the MOD edit view picks OFFSET, TRANSPOSE or empty. The MOD
  column generator places mostly offsets and about one transpose in six.
- Shift + X on an FX row in edit view no longer repeats the same effects in the same
  order: each edit draws from fresh entropy, and a draw never repeats the current effect.
- Sessions are AMAL25 (the MOD type byte); AMAL24 and older load.

## Build 37 — Instrument row variations

- FUNC + hold X on the INSTRUMENT column gives every row holding an instrument its own
  random variation of that instrument's sound: the same patch family with redrawn
  non-pitch parameters (timbre, filter, envelopes, modulation and effect amounts), for
  every engine. Pitch, tuning, detune, FM ratios, glide, pitch envelopes / modulation and
  drum tuning never change, nor does the output level. Rows with an instrument can hold
  one instrument or several. A small square marks a varied row.
- With rows selected (FUNC + F1 / F2) only those rows vary; while editing an INST cell,
  FUNC + hold X varies that row only. At most 16 distinct variations per track: rows past
  the 16th reuse their instrument's earlier ones. Undo restores; hold X on INST (a new
  track sound) clears the column's variations; ERASE or choosing another instrument
  clears a row's.
- Each variation is cooked once on the UI thread into a bounded pool shared across
  tracks; native and PERC voices start a fresh voice for a different variation and the
  previous one releases (tails stay bounded per track).
- Sessions are AMAL24 (the per-row variation seeds); AMAL23 and older load without
  variations.

## Build 36 — LIFEBLOOD snapshot repair

- A snapshot stored while angiogenesis was absorbing a node kept vessels to that node,
  so the session could not be loaded again (builds 29-35). Storing now removes them,
  and such sessions load with those vessels dropped and the flow repaired.

## Build 35 — Loudness matching

- Every insert in every appliance now keeps the level of what goes into it: engaging an
  effect at any DRY/WET MIX changes the sound, not its loudness. Energy before and after
  the effect is averaged over about 0.3 s and the difference glides in (-24 dB to +9 dB),
  holding while the input is quiet so tails decay naturally. LEVEL, PAN and the send
  effects are left as they are. Measured at 85% mix on a steady signal, every insert
  stays within 2 dB of its input (most within 0.4 dB).
- LIFEBLOOD also matches the whole network's output to its input before MIX.

## Build 34 — LIFEBLOOD track inputs, PATH, louder recipes, volume overlay

- LIFEBLOOD takes the FM1's separate tracks (six, or three for Norilsk) as separate
  inputs into one stereo output. Only tracks heard in the last 30 s are routed; X and A
  spread them over the vessels at random, some passing straight to OUT; tracks that
  start later join OUT; silent tracks are dimmed and never routed.
- Session settings gain PATH: ABSTRACT or RHYTHMIC (Vascular's paths); CHARACTER offers
  the path's recipes. RHYTHMIC leans on cuts, gates and clockable textures, syncs what
  can sync and keeps effects at least 72% wet.
- Recipes are more audible: wetter nodes, closer vessel levels, fewer and quieter dry
  vessels, and a louder sum into OUT.
- LIFEBLOOD and FX show the volume overlay when the volume keys are pressed.
- LIFEBLOOD sessions are AMLB2 (AMLB1 loads), and installs now back up the LIFEBLOOD bank.

## Build 33 — Native effects, batch 3: shapers, cross-modulation, spectral, resonators, voices

- Sixteen new inserts in every appliance, plain C++ recreations: CYANIDE, POLARIZER, ANGR,
  WARPS, FREQ SHIFTER, BROKENFFT, TREEMONSTER, RING MOD, COMBULATOR, RESONATOR, VOCODER,
  SMR, REZ SYNTH, TALKBOX, MDA VOCODER, REPSYCHO. Forty native effects in total.
  See docs/NATIVE-EFFECTS.md.

## Build 32 — Native effects, batch 2: gates, loopers, buffer bending, destruction

- Fourteen new inserts in every appliance, plain C++ recreations: CHOPPR (B.Choppr),
  GATE SHAPER (GATE-12), TREMOSQUARE, BEADS (Mutable Instruments), SOFTCUT (monome engine,
  synced micro-looper), TRANSVERB, GLITCHSHIFTER, PITCHNASTY, AMALGAM (Valley), GEOMETER,
  MULTIBAND DIST, DISINTEGRATE, BITGLITTER, PAFNUTY (Airwindows). See docs/NATIVE-EFFECTS.md.
- LIFEBLOOD's GATE and INDUSTRIAL recipes favour the new gates; LIFEBLOOD sessions now
  store native effect parameters (they were lost on reload in build 31).
- The Tracker shows FLANGER and PHASER with their own codes (FL, PH) instead of RN.

## Build 31 — Native effects, batch 1: cuts, repeats and gates

- Ten new inserts in every appliance, recreated one to one as plain C++ (no framework or
  library): OOPS (B.Oops), BUFFER OVERRIDE, LIVECUT, SUPATRIGGA, BEAT REPEAT (Bespoke),
  TIME SHAPER (TIME-12), JUMBLR (B.Jumblr), SCRUBBY, GLITCHLETS, SKIDDER. All follow the
  FM1's MIDI clock or the session tempo in musical divisions and realign on Start.
  See docs/NATIVE-EFFECTS.md.
- LIFEBLOOD: a RHYTHM family and four rhythmic recipes: CUT, GATE, MAYHEM, INDUSTRIAL.
- Tracker sessions are AMAL23 and device settings AMDV3; older files load unchanged.

## Build 30 — LIFEBLOOD rewire and value overlay

- LIFEBLOOD: A rewires the network (new vessels between the same nodes, parameters kept,
  120 ms crossfades); Shift+A opens the session settings from any LIFEBLOOD page.
- Every value dialled in LIFEBLOOD (session settings, node parameters) shows the large
  name/value overlay, as in the Tracker.

## Build 29 — LIFEBLOOD: generated effect networks

- A third appliance on the home screen, **LIFEBLOOD**: the FM1's USB audio through a
  generated network of the shared inserts, drawn as breathing nodes, swaying vessels and
  travelling corpuscles. Starts blank and stopped: IN straight to OUT, dry.
- Short X grows a new network from the session's CHARACTER (ANY, DRIFT, GRIT, SPACE,
  PULSE, BLOOM); Y redraws every node's parameters, keeping nodes and vessels; B starts
  and stops **angiogenesis** (drift, sprouting and withering vessels, budding and absorbed
  nodes, single-node redraws). Enter edits the selected node; Play bypasses to dry.
- Recipes are targeted: favoured families per character, a harshness budget, reverb last,
  no destructive insert into another, character-dependent mixes, bounded delay feedback,
  a CPU ceiling, and strictly feed-forward vessels (no feedback loops).
- 32 snapshots per session and 64 sessions (AMLB1, `lifeblood-session-slots/`), with the
  Tracker's bank gestures. Session settings: TEMPO, CHARACTER, NODES, GROWTH, MIX.
- New networks crossfade over 0.5 s on their own processors; vessels fade over 120 ms;
  redrawn nodes dip to dry for 25 ms and return over 120 ms. See docs/LIFEBLOOD.md.
- FX appliance: random QDELAY and MATRIX draws (hold X, Shift+hold X, sequence generation)
  now stay within the Brick's audio budget. Measured with random parameters, one QDELAY
  reached 2.7 ms and one MATRIX 7.4 ms of the 5.8 ms block. QDELAY draws are now a tape
  and stereo delay (its pre-distortion, diffusion, crusher, shifter, phaser, ducking and EQ
  stay off); MATRIX draws keep two delay nodes on the default delay type. Twelve such slots
  measure 3.9 ms p99, 5.5 ms worst. Hand edits and the Tracker's FX columns are unchanged.

## Build 27 — Bar-quantized pattern switching, scale die, 38 scales

- While playing, loading a pattern queues it: the slot shows an amber NEXT frame and
  the header reads `> NEXT n`. The switch happens at the end of the current loop of the
  reference track, and every track restarts on its first step together. Stopped, a pattern
  loads at once, as before. Stopping with a pattern queued loads it at once; choosing
  another pattern before the bar replaces the queued one.
- SESSION settings gain **SWITCH QUANTIZE**: **LONGEST TRACK** (the default: wait for the
  slowest/longest sounding loop) or **SHORTEST TRACK** (switch at the end of the shortest
  loop, without waiting for a slow track to finish).
- SESSION settings separate moving from editing: the cross moves between TEMPO, SCALE,
  ROOT NOTE, SWITCH QUANTIZE and the die (a framed selection); START puts the selected value
  in edit mode (filled, `< EDIT >`), where LEFT / RIGHT change it; START or A leave edit mode,
  and UP / DOWN leave it and move on.
- A die beside SCALE and ROOT NOTE, bracketed to both: RIGHT from either row selects it,
  LEFT returns. START on the die (or hold X on the die, SCALE or ROOT NOTE) rolls a new
  scale and root note together (never CHROMATIC; undoable).
- 38 scales, after Tonitor (FM-1) and Bigorna plus further modes and world scales: melodic
  minor, lydian, mixolydian, locrian, major pentatonic, blues, major blues, whole tone,
  hungarian minor/major, double harmonic, phrygian dominant, lydian dominant, altered,
  neapolitan minor/major, persian, enigmatic, prometheus, augmented, octatonic (H-W, W-H),
  bebop dominant, japanese in, hirajoshi, iwato, kumoi, yo, ryukyu, pelog, egyptian.
  The first seven keep their numbers; PENTATONIC now reads MINOR PENTATONIC.
- Session banks are AMSB3 (they store SWITCH QUANTIZE); AMSB1/AMSB2 load unchanged.

## Build 26 — FLANGER and PHASER in the FX appliance

- The FX appliance offers FLANGER and PHASER like every other shared insert: choose
  them in a slot, draw them with hold X, sequence their parameters, save and load them
  in FX sessions. Chosen in a slot they start fully wet (other effects start at 80%).

## Build 25 — FLANGER and PHASER inserts

- New FX1/FX2 inserts: **FLANGER**, a port of the Surge XT flanger (up to four tuned comb
  voices, feedback, six LFO shapes, arpeggiated comb modes, width, bipolar comb mix), and
  **PHASER**, a port of ChowPhaser (1–50 all-pass stages on a light-dependent-resistor
  sweep, saturated feedback, skew, three drives, stereo LFO offset).
- Both start at 100% WET and their random draws stay inside sweeping ranges.
- Sessions are AMAL22 (both profiles per track and slot); older sessions load unchanged.

## Build 24 — Instrument randomize overlay, root audition, stereo pan positions

- Hold X on the INSTRUMENT column draws a new sound for the instrument chosen in TRACK
  SETTINGS and writes that instrument on every row of the column, whatever numbers it
  held before. An overlay names the instrument and its new sound.
- FUNC + short X on the INSTRUMENT column auditions the sound hold X drew, on the
  root note of the scale in octave 4 (D4 when the root is D). It changes nothing.
- Shift + hold X on the INSTRUMENT column shows an overlay with how many instruments
  were drawn on how many rows (their sounds are unchanged, as before).
- Pan reads as a stereo position, -50 L ... C ... +50 R, in track settings, note pan
  overrides, master output balance and effect pan parameters. Same range as before.

## Build 23 — Effect parameters in real units

- Master effect pages, track macro pages (Abrasion, Corrosion, Modulation) and the
  insert effect edit views show each parameter in the units its DSP uses (Hz, ms, dB,
  Q, ratio, bits, semitones, note divisions, named choices) instead of 0–100%.
  The values are computed from the same formulas the audio code applies to the
  stored setting; nothing about the sound or the saved sessions changes.
- QDelay choices show their names (sync mode, paths, distortion type, EQ band modes)
  instead of "CHOICE n"; ChowMatrix node DELAY shows the note division when TEMPO
  SYNC is on.

## Build 22 — FX column randomization draws parameters

- Hold X (and Shift+hold X) on FX1 / FX2 now also draws fresh parameters for every
  effect it places, instead of leaving them at the slot's stored (usually factory)
  settings. Rows of the column that share an effect share one draw; RANDOM keeps
  drawing its own per trigger. One undo step restores the column and its parameters.

## Build 21 — No runaway effects, hold X for effect parameters

- Fixed effects that kept sounding: Crazy Ivan's feedback could self-oscillate at full
  level indefinitely, even after Stop. Every insert's output now stays within 6 dB of a
  fading memory of its input (60 dB over 6 s for reverb/delay/granular/resonator, 1.5 s
  for everything else), so nothing sustains itself. Crazy Ivan is no longer treated as a
  ringing effect. ABRASION / CORROSION send returns follow their input the same way
  (0.5 s; MODULATION 1.5 s): high drive cannot lift a quiet tail into a loud one.
- FX editor: hold X draws new parameters for the chosen effect (mix and length kept);
  Shift+hold X draws a new effect with new parameters (length kept).

## Build 20 — FX LENGTH, dry/wet mix, ringing tails, row selection

- FX cells have a LENGTH (1–64 FX-column steps, default 1): the effect keeps its slot
  over the following rows until it ends or a new effect row replaces it.
- When an effect ends or is replaced, reverb / delay / granular / resonator effects
  (Space, Clouds, QDelay, ChowMatrix, Rings, Crazy Ivan) ring out and fade naturally.
- The FX step value is the DRY/WET MIX for every effect. Wavefold, Fuzz, Rectify,
  Tremolo and Stutter now have a fixed character that the mix blends in. A newly
  chosen effect starts at 50%.
- FX edit tabs: DRY/WET MIX, LENGTH, CONDITION (two shown at a time).
- FUNC+F1 / F2 select rows up / down from the cursor; an edit writes the fields it
  changed to every selected row, and ERASE clears all of them. One undo step each.
- Sessions are AMAL21 (FX lengths); older sessions load with one-step effects.

## Build 19 — Column rotation, clean save messages

- Shift+F1 / F2 now rotate only the column under the cursor (NOTE, INST, FX 1,
  FX 2 or MOD), one step backward / forward, wrapping within that column's own
  length; the other columns stay where they are. Each rotation is an undo step.
- Saving (and loading) shows only its own message. The overlay no longer keeps
  the last dialled value (a percentage, a clock division) under it.

## Build 18 — Sequence rotation

- Shift+F1 / F2 (the buttons below the screen) on the spreadsheet rotate the
  current track's whole sequence one step backward / forward, wherever the
  cursor is. Every column moves with its controls and conditions and wraps
  within its own active length; each rotation is an undo step.

## Build 17 — ERASE everywhere, insert page removed

- Shift+Enter enters ERASE mode on every spreadsheet column, FX 1 / FX 2 included.
- Removed the FX insert parameter page (formerly Shift+Enter on FX 1 / FX 2).
  Inserts keep their default or saved settings; the step value is still dry/wet.

## Build 16 — Horn of plenty, battery, EUCLIDEAN randomizer, ERASE mode

- New identity: the Amalthea mark is the goat's horn that became the cornucopia,
  a ribbed, curling horn (read also as a crescent: Amalthea is the red moon)
  pouring a fan of uneven rays from its ember mouth. The home screen identifies
  the appliances by icons: a tracker pattern screen with its cursor row, and a
  pedalboard of three stomp boxes. Both appliance top bars use their icon.
  SVG masters are generated from the same geometry by render_identity.
- Battery indicator (charge and charging state) in the Tracker top bar between
  TRACKER and the transport, top right in FX and on the home screen.
- Randomizer MODE above DENSITY: RANDOM or EUCLIDEAN distribution of the events
  in the column, per track/column, saved with patterns and sessions (stored in
  the density byte's spare bit: older sessions load as RANDOM).
- ERASE mode: Shift+Enter on NOTE / MOD / INST; Enter clears the selected cell,
  the cursor keeps the track colour inside a red frame; Shift+Enter or Cancel
  exits.

## Build 15 — Voice pile-up fixed (PERC, FM DRUM and all engines)

- Fixed stutter / xruns on busy tracks: every note took a new voice and old ones
  kept rendering through their release tails, so a single track of 16ths could
  hold dozens of voices (PERC: dozens of complete kits). Measured on the Brick,
  one track of 16ths: PERC 7.1 -> 0.7 ms a 256-frame block, FM DRUM 8-12 ->
  1.2-1.4 ms (budget 5.8 ms); 12 busy mixed tracks now average 2.1 ms.
- PERC is one kit voice per track again: a note retriggers its piece inside the
  track's kit voice (the pieces still ring over each other).
- Each track keeps at most three release tails; an older tail fades out over
  6 ms (no click) and frees its voice.
- FM DRUM's decimator no longer divides per sample.

## Build 14 — Note LENGTH, MOD second, OFFSET audition

- NOTE events have a LENGTH tab (1 .. 64 steps; tabs: VALUE, LENGTH,
  CONDITION, then the locks). A note is gated for its length; when it reaches
  the next note (or a note-off) it runs legato into it: held up to that onset,
  where it moves into its own envelope release (never cut, so no click) while
  the new note starts with its own attack. Tie rows still extend notes; the
  longer of the two wins. Tap X randomizes one length, Shift + hold X the
  column. Session format AMAL20 (older sessions: every note 1 step).
- MOD is now the second column (NOTE, MOD, INST, FX 1, FX 2) on screen and for
  the cursor; stored column numbers, settings and sessions are unchanged.
- The modifier's parameter is called OFFSET. Dialling it auditions the note it
  produces (the row's note, or the nearest earlier note when the row has none).

## Build 13 — RANDOM effect, immediate column randomize, layout

- New FX type RANDOM (FX 1 / FX 2): each time the step plays it loads a
  different effect with its own random parameters. AMOUNT scales the drawn
  effect (0 bypasses). Eight complete configurations per track FX slot are drawn
  off the audio thread (processors prepared) and redrawn on any randomize
  action; the audio thread picks one per trigger.
- Fixed: Shift + X on a NOTE event tab (pan, volume, sends) sounded late. While
  playing, the audition of the edited row no longer holds the whole track on
  that row's locks until the audition's tail dies; every row plays its own
  values at once. Starting playback also releases a held audition mix.
- Track view: columns sized to their contents with equal spacing (no gap
  around INST).
- Session and pattern banks: slots with data have a lighter body, a bright top
  band and a white number; empty slots stay dark with a dimmed number.

## Build 12 — Louder output, sidechain, value popups, FX row randomize

- Output: OUTPUT LEVEL now drives the output stage (100 = +9 dB into the
  limiter, 50 = the former unity level, 0 = silence), followed by a soft knee
  that rounds peaks into the ceiling. A saved session went from -16.5 to
  -8.5 dBFS RMS, peaks at -0.7 dBFS; clean (< 1 % THD) up to -12 dBFS in,
  progressively saturated above. The output Mix at 0 still bypasses the stage.
- Compressor: SIDECHAIN (OFF / ON) and SOURCE (TRACK 1 .. 12). With sidechain on
  the source track drives the detector and passes unducked; the rest of the mix
  ducks on its hits. Session format AMAL19 (older sessions: sidechain off).
- Settings pages without their own slider (track, column, FX parameters,
  master, macro, session, pattern) show a large popup of the value being
  dialled: its name above, the value in very large type, 1 s, kept up by
  repeated presses.
- Hold X while editing an FX 1 / FX 2 row: a random effect and value for that
  row only, pending until Enter (Cancel discards it). The effect's deeper
  parameters are drawn too when no other row of that column uses the same
  effect (they are shared by the column's rows), otherwise they are kept.
- Notes sound only from the NOTE column, for a row that holds a note: editing
  an instrument, FX or modifier cell, or FX parameters, no longer plays C4.
- Deploy: `tools/backup_sessions.py` copies every saved session from the Brick
  to `backups/sessions/<timestamp>/` (SHA-256 verified) before each install.

## Build 11 — FM DRUM is instrument 12

- The instrument bank has 12 slots: slot 12 is FM DRUM, so every engine is an
  instrument of its own again. A blank session gives track 12 the FM DRUM.
- Session format AMAL18 stores the twelve slots. Older sessions (eleven or
  eight slots) load unchanged and gain FM DRUM as instrument 12.

## Build 10 — FM DRUM engine and relative step volume

- New instrument engine **FM DRUM** (12th engine; PERC stays). A floating-point
  re-implementation of the Digitone II's FM DRUM machine from DigiRust's
  curve notes (`mvave-digirust/docs/dt2-curves.md`), without the FM-1
  version's fixed-point compromises: per-sample envelopes and sweep, a 2x
  oversampled body (three operators, 7 algorithms, feedback, folder), the 33
  DT2 waves crossfaded on OP.C / OP.AB, 32 crossfadable transients
  (synthesised), noise grains and base-width band, NRM, NRST, PH.C and
  DigiRust's ABN. It runs through Bigorna's filter / amp / overdrive / MANGLE
  chain and LFOs and draws Bigorna's 27 DRUM recipes (kicks, snares, metal,
  SLAM, SCRAPE, NOISE ENGINE, MACHINE ...).
- A NOTE draw on an FM DRUM track writes a one-voice rhythm (euclidean,
  metric, offbeat, cluster, scatter, pulse) around the recipe's pitch, with
  octave ghosts, in-scale tom steps or a falling fill.
- Step volume is now relative to the track fader: a step plays at its own
  value while the fader is at its default (90 %), and the fader scales every
  step proportionally (step x fader / 90). Fader at 0 silences the track,
  programmed steps included. Sessions sound as before at the default fader.

## Build 9 — Tracker generators and sound recipes

- NOTE generator rewritten (`src/melody.cpp`). Each draw picks an idiom
  (ostinato, pedal point, drone, walk, lament, stab, call/answer, arp, spiral),
  a separate rhythm model (euclidean, metric, offbeat, cluster, scatter,
  anchored, optionally a repeating 3–8 step cell), a register and a modal
  progression. Long idioms tie into empty rows. Output stays in the project's
  root and scale; density still sets the exact number of notes. The Fugitive
  fugue-subject adapter and its copied opening cell are gone.
- A PERC track draws a drum pattern on the kit's note map (EBM, industrial,
  rhythmic noise, ritual, broken) instead of a melody.
- Boot entropy: the project seed starts from fresh entropy on each boot, and
  every draw mixes in runtime entropy, so the generators no longer repeat
  the same sequence each session. Undo/redo still restore exactly.
- A blank session (boot, or leaving the tracker without a session) starts
  with each track's instrument on a drawn patch instead of the engine default.
- VAP, CHIP and PERC recipes replaced by Amalthea's own book
  (`src/amalthea_recipes.c`): 18 VAP families (SUB PRESSURE, EBM BASS, ACID
  WOUND, ABYSS, MOURNING PAD, RUST SYNC, RING CORRODE, NOISE WALL, IMPACT ...),
  12 CHIP families (CRUSH BASS, ZAP HIT, LFSR STORM, DEAD BELL, SUPER DREAD ...),
  and 5 PERC kit characters with their own shaper and EQ. Formant / vowel and
  bright-bell families are retired; the random discovery overlay is removed
  for these engines. Family levels are matched to within about 3 dB.
- ANIMA, RUSSOLO, BAY AREA and RESURGENT families are now drawn with weights:
  soft families (VOYAGE, LILT, GOLDEN, ORGAN, MAJOR TRI, PENTATONIC, ELECTRIC
  WOOD, FORMANT, WARM PULSE, CINEMA, HOLLOW KEYS, NEON BRASS) are retired and
  the darker ones drawn twice as often. FIBER draws half experimental, a quarter
  ambient and a quarter open.
- Saved sessions keep their seeds; patches on these engines will sound
  different because the recipes behind the seeds changed.

## Build 8 — USB audio survives replugging

- Fixed: after the first unplug, USB hosts received a buzz instead of the
  appliance. Each unplug rebuilds the gadget and replaces its sound card; the
  writer kept the dead card open because a disconnected PCM's status never
  changes. It now checks liveness with the kernel every 250 ms and reopens.
- Module: stream state is cleared on every unbind, and an IN stream with no
  writer sends exact silence instead of stale data.
- The app no longer restores dual-role USB-C on exit (the board resets it at
  boot), removing two USB renegotiations per app restart.
- Builds 5 and 7 were intermediate builds.

## Build 6 — USB audio output (stereo mix)

- The Brick is now a class-compliant USB audio input ("Amalthea Audio", 2 ch,
  44.1 kHz, 16-bit) next to ADB. Computers and phones record it without drivers.
- Shared output layer for every appliance: the audio thread hands each rendered
  block to a non-blocking bridge; a USB writer follows the host's clock with
  fill-controlled drift correction (cubic interpolation, < 0.5 cent movement).
  USB carries the mix before the codec volume, like a line output.
- The app loads `usb_f_uac2.ko` from its own directory and adds the audio
  function to the gadget at start (one brief ADB reconnect per boot). The
  deployer installs the module with the binary.
- USB-C port: sink-only outside FX so phones take the host role; dual-role while
  FX is active so the FM1 is hosted as before; dual-role restored on exit.
- Multitrack stems are not yet exposed; channels 1–2 are the stereo mix.
- Build 5 was an intermediate build and was not installed.

## Build 4 — Back button and visible level edit

- L2 is now Back everywhere: returns to the previous page in navigation history,
  leaving an in-place edit (FX level) first. History is per appliance, so Back
  never exits one. Shift+L2 opens the tracker Dashboard (formerly plain L2).
- FX level edit mode: thick accent frame, tinted body, larger knob and the value
  on an accent pill.

## Build 3 — Larger FX parameter text and value popup

- B parameter page (defaults and step overrides) uses 40 px rows instead of 32.
- Every left/right change shows a 1 s popup: parameter name, then the value in
  very large type. Repeated presses keep it up.

## Build 2 — FX sequencer gating, complexity and per-slot tempo

- Randomizing a slot on the mixer shows the effect name in a 1.5 s overlay.
  FX notices now actually stay on screen (they previously cleared next frame).
- A running, sequenced slot applies its effect only on triggered steps; empty
  steps pass dry (3 ms fade). Stopped or unsequenced slots process continuously.
- Y in the slot sequencer opens randomizer settings: DENSITY (steps, relative to
  length) and new COMPLEXITY. 0 gives every generated step one shared random
  parameter set, 100 makes every step independent. Tap X still opens it too.
- New per-slot TEMPO in A preferences (internal clock only): SESSION follows the
  session tempo, otherwise 30–300 BPM per slot. External clock still wins.
- FX sessions AFXR5 store complexity and tempo; older files load 50% / SESSION.

## Build 1 — FX slot randomization and channel bypass

- Versioning is now a single build number: every Brick build increments
  `BUILD_NUMBER` (1, 2, 3, …). Releases below keep their old semantic numbers.
  The persistent audio log records `build=N` instead of `logger=X.Y.Z`.

- FX mixer: hold X / Shift+hold X now randomize only the selected slot (FX1 or
  FX2) instead of both slots of the channel. On the level/bypass rows they show
  CHOOSE AN FX SLOT.
- Removed the CH / FX / effect header line above the channel strips; strips moved up.
- New BYPASS button at the foot of every strip, toggled with Enter. Bypassed
  channels skip both slots and pass dry at their level via a 10 ms crossfade;
  slot sequencers keep their phase. Stored in FX sessions as AFXR4; AFXR1–3 load
  with all channels enabled.

## 0.21.8 — FM1 crackle fixed: stale USB capture packets

- Root cause: the retained Linux 4.9 snd-usb-audio `retire_capture_urb` advances
  the capture hardware pointer for each isochronous packet before copying the
  packet's samples. OHCI completions run on CPU 0; when the FX capture thread ran
  on another CPU, its 1-ms non-blocking reads could take a packet in that window
  and receive the ring slot's contents from one buffer (2822 frames) earlier.
  No xrun, error or rate change results, so all transport counters stayed clean.
- Thread placement is fixed at application start; a laptop attached at boot
  happened to place it on CPU 0 (clean), otherwise CPU 1 (crackle).
- Capture now never consumes the newest 96 frames (two full-speed packets) and
  explicitly restarts a PREPARED stream after recovery.
- Owner-verified clean with the capture thread forcibly pinned to CPU 1.

## 0.21.7 — Deterministic USB startup

- Accompanying OS update normalizes NULL → DEVICE → recreated DEVICE before
  binding the recovery gadget, matching the reinitialization seen after handoff.
- Correct detached-port UI status: STARTING HOST rather than UNPLUG LAPTOP.
- Retain persistent transport/kernel diagnostics and add role/link/power state.
- Remove input-level/RMS/jump summaries. No automatic waveform recording.
- Synth/DSP, buffering and resampling settings remain unchanged. The startup
  ordering is normalized; audible crackle resolution is not certified.

## 0.21.6 — Cold-boot connection status and signal evidence

- Distinguish a detached port's three-second host preparation from a connected
  computer. Do not display “Unplug laptop” merely because USB is in device mode.
- Automatic signal recording was rejected by the owner and removed before
  permanent installation. Existing explicitly armed tools remain available.
- Preserve existing persistent transport/kernel/system logs. No audio buffering,
  resampling, effect algorithms, kernel or FM1 firmware changes; crackle unresolved.

## 0.21.5 — Diagnostics across power cycles

- Always-on asynchronous diagnostic writer with boot UUID and unique run/segment
  identity; one-second flushed/fsynced checkpoints on internal storage.
- Record connection states, capture geometry, kernel messages, codec/USB status,
  CPU/memory/thermal state and incremental appliance messages across cable swaps.
- One-second transport health, 100-ms counter-change polling and per-channel
  input peak/RMS/clip/sample-jump summaries. These do not diagnose audible crackle
  automatically and do not constitute audio recordings.
- Fixed nonblocking producer queue with visible loss counts; background rotation
  and approximately 256-MiB retention for this logger only.
- No disk writes on the capture/audio threads. No changes to audio buffering,
  synths, effects, FM1 firmware, kernel or bootloader in this update.

## 0.21.4 — USB capture recovery and fault history

- Re-prime input after recovered capture faults; discard stale queued samples.
- Preserve timestamped bounded capture/recovery/health history on stream close.
- Prevent packet diagnostics and live FX capture from claiming USB concurrently.
- Supply the replacement Linux RawMIDI hardware definition for external clock.
- Extend passive packet diagnostics to a bounded 5–180 seconds with completion timing.
- ARM64 and macOS transport/recovery/history regressions pass. Full FM1 live
  validation of the new release remains pending; no original crackle root cause
  or performance certification is claimed.

# 0.21.3 — 21 November 2026

Diagnostic build for unresolved USB crackling. Retained 0.21.2 measurements show
zero capture/output xruns or ring starvation while connected, with a 177 us peak
render time. An explicitly armed, one-shot probe records five seconds of raw USB
samples and codec-input samples to separate WAV files. Real-time threads only
copy into preallocated RAM; a normal-priority worker writes the bounded recordings.
No claim of an audible fix in this release; recordings will locate the fault.

# 0.21.2 — 21 November 2026

Improve FX USB continuity: priority-13 capture, 64 ms capture buffer, stable 80 ms
codec output without mode-switch PCM resets, 1536-frame input priming, slower
clock-drift correction and ramped starvation recovery. Add separate capture,
input-ring and output xrun metrics to retained diagnostic logs. Regression covers
bursty USB delivery. Native Bigorna capture remains at its advertised 44.1 kHz.

# 0.21.1 — 21 November 2026

Fix repeat FM1 connection failures after the temporary host trial expired. FX now
switches the bottom port to host mode after three seconds of stable laptop
removal, without a test timer or laptop arming step. Exiting FX restores device
mode and prior port power; stale ownership is recovered after process restart.
USB UI distinguishes waiting for laptop removal, host waiting and port failure.

# 0.21.0 — 21 November 2026

FX slot sequence generation with hold X, per-slot density options on short X,
45% defaults and trigger counts relative to the active sequence length. Generated
steps retain the slot effect/defaults and store parameter overrides; inactive
steps and stopped transport use the defaults. Generation enables internal playback;
external clock/transport retain priority. Larger two-row playhead highlight.
AFXR3 saves density and reads earlier FX session formats. USB diagnostic logging
and a reversible ten-minute bottom-port host trial; owner confirmed Bigorna audio.

# 0.20.0 — 21 November 2026

FX appliance: automatic class-compliant FM1 USB capture, descriptor-based mono-track
mapping, two shared inserts per strip and independent 16-step sequencers with
parameter locks, clock divisions and directions. External MIDI clock priority,
internal session tempo fallback, 64 explicit FX session slots and blank/stopped
startup. Parallel channel DSP, bounded granular/reverb budgets and clock-drift
compensation. Physical FM1 streaming still requires an attached source for validation.
New phase-iris identity and licensed Oxanium typography replace the orbital A.

# 0.19.0 — 21 November 2026

Amalthea appliance home, shared orbital-A identity and boot/application splash;
Tracker entry and FX development placeholder. Ordered FUNC→Shift exit confirmation,
followed by an explicit save choice. Tap X in event editing randomizes only the
selected parameter; Shift+hold X applies it across the active column length with
one undo. Local edits remain transactional and immediately auditioned. Notes obey
scale/root and randomized volume has a10% floor. No automatic session recall/save.

# 0.18.0 — 21 November 2026

Nine track/slot-local inserts: QDelay, ChowMatrix, CrushClone, Frequalizer, Rings,
Fracture 2, Crazy Ivan, Bitmurderer and Lo-Fi. Detailed parameter pages with percentage
controls; ChowMatrix node and Frequalizer band paging. AMAL17 profile persistence,
AMDV2 global insert switches with AMDV1 reading, native DSP adapters, allocation-free
render/edit paths, and complete reset of CrushClone's state. Vendor DSP and licenses
retained. Blank/stopped startup remains enforced.

# 0.4.1 — 20 November2026

Fix reversed A133 headphone attenuation: set raw0 (maximum output), instead of7.
The driver's dB metadata was misleading. Live correction confirmed by the owner as
usable; current session preserved without restart. Persistent binary updated.

# 0.4.0 — 20 November 2026

Four independently controlled parameters per master module; vertical selection and horizontal editing;
bright track-color cursor and enlarged edit values; 0.8-second Session hold to save with visible feedback;
blank startup and explicit recall; undoable Shift+B whole-track clear; unity software/headphone gain;
format4 persistence; six integration suites; 80ms device buffer verified under simultaneous UI/save load.

# 0.3.0 — 20 November 2026

Direct FX type/amount control and large name overlay; bottom play/session controls;
Session tempo/scale/root; paged master coloration/compressor/delay/reverb; Fugitive
instrument family recipes with engine-preserving X and cross-engine Shift+X;
all-track playback fix; increased hardware/software gain; percentage volume overlay;
version-3 persistence and integration tests. Previous release backed up in backups/pre-0.3.

# Changes

## 0.2.0 — 2026-11-20

- Limited active patterns to 16 steps and changed columns to NOTE/INST/FX1/FX2/COND/MOD.
- Moved track switching to F1/F2, reserved shoulders as Shift/Shift2, retained L2 zoom.
- Added black background, bright-yellow track information, track colours and full-screen banners.
- Scoped X to the selected column and integrated Fugitive's musical subject generator.
- Added cycle occurrence/period conditions and conditional/fallback semitone modifiers.
- Implemented 18 actual audio effects/commands, real send tails and full-path effect tests.
- Migrated old projects with backups and documented omitted obsolete commands.
- Passed all four Mac/ARM suites and a 35-second, 12-track effects baseline with zero recoveries.

## 0.1.0 — 2026-11-20

- Added the first shared Mac/Brick tracker with 12 tracks, six columns and 16/eight-row layouts.
- Implemented cell editing/commit/cancel, mixer, transport, undo/redo and atomic autosave.
- Integrated seven recreated synth engines and a startup WAV sample player.
- Added seeded per-track pitch/effect randomizers using the owner's X/B mapping.
- Built and installed the native Brick application into the existing tracker startup profile.
- Increased the ALSA buffer after measured underruns; passed a 35-second 12-track baseline.
- Fixed transparent framebuffer output after the owner reported no visible interface;
  added a full-frame opacity regression test.
