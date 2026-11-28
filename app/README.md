# Amalthea

First playable tracker prototype for the TRIMUI Brick and native Apple Silicon macOS.
The source project lives here; the earlier FM-1 project remains separate in `../fugitive`.

Version 0.21.3 has independent 1–16-step patterns on 12 tracks, one five-column editor (NOTE, INST, FX1, FX2, MOD), 16 or eight
visible steps, a mixer, playback, transactional editing, seeded track randomizers,
undo/redo and explicit session saving. Six native synth engines are integrated: VAP, PERC, CHIP, FIBER, HIVE and OP4. This is an incremental implementation, not completion of
all milestones in the foundational PDF.

The FX RACK appliance now shares the insert processors, with automatic FM1 USB capture,
twelve independent effect sequencers and a separate 64-slot session bank.
In a slot sequencer, tap X for density and hold X to generate parameter-lock events;
the moving step highlight follows playback independently of the cursor. External
MIDI clock takes priority; session tempo is the fallback.
With FX RACK open, unplugging the laptop automatically enables bottom-port USB host
mode after three seconds. Exit FX RACK before reconnecting the laptop; no timed arming
is needed. See [FX RACK appliance](docs/FX-APPLIANCE.md).

LIFEBLOOD, the third appliance, sends the FM1's USB audio through a generated network
of the same inserts: X grows a network, Y redraws its parameters, B starts angiogenesis.
See [LIFEBLOOD](docs/LIFEBLOOD.md).

## On the Brick

Amalthea starts through the existing tracker profile. No firmware partition was flashed.
The launch command and project are under `/mnt/UDISK/tracker-linux`.

| Control | Action |
| --- | --- |
| Cross | Navigate rows and columns |
| Bottom-right Start | Begin editing; press again to commit |
| Cross up/down while editing | Change the selected value |
| F1 / F2 while editing a cell | Browse event tabs (two visible); current track stays selected |
| Cross left/right while editing an effect | Choose effect type; a large name overlay appears |
| Shift+Enter (any column) | ERASE mode: navigate anywhere; Enter clears the selected cell; Shift+Enter or Cancel exits |
| X, short press | Open randomizer options (MODE, DENSITY) for the selected column; tap again to return |
| X, held 0.8 s | Randomize the selected column; NOTE uses Fugitive motifs |
| B (bottom face button) | Track settings / return to pattern |
| Shift+cross up/down | Transpose all active track notes up/down one octave per press |
| Hold X on FX1 / FX2 | Randomize the column's effects, mixes and every placed effect's parameters (rows sharing an effect share its parameters) |
| Shift+hold X on FX1 / FX2 | The same, plus positive trigger conditions |
| Hold X while editing an FX cell | New parameters for the chosen effect; Shift+hold X: a new effect with new parameters |
| Hold X on INST | New sound for the TRACK SETTINGS instrument, written on every row of the column; an overlay names it |
| Shift+hold X on INST | Randomize per-step instrument slots; keep the track default and patches |; an overlay shows how many were drawn |
| FUNC+short X on INST | Audition the track instrument's current sound on the scale's root note, octave 4 (D4 in D); changes nothing |
| FUNC+hold X on INST | Give every row holding an instrument its own variation of that instrument's sound: non-pitch parameters (timbre, filter, envelopes, modulation amounts) are redrawn per row; pitch, tuning, glide and level never change. A small square marks a varied row. With rows selected (FUNC+F1/F2), only those rows; while editing an INST cell, only that row. At most 16 distinct variations per track (later rows of a longer column reuse their instrument's). Hold X on INST clears them; ERASE or a new instrument clears a row's; undo restores |
| Shift+B (bottom face button) | Clear the entire current track |
| A | Selected column settings; Back on other pages |
| Y | Master effects / return to pattern |
| F1 / F2, below the screen | Previous / next track, with a full-screen track banner |
| FUNC+F1 / F2 on the spreadsheet | Select rows up / down from the cursor in the current column; Enter then edits all of them (only the changed fields are written), ERASE clears all of them; moving the cursor or Cancel clears the selection |
| Shift+F1 / F2 on the spreadsheet | Rotate the column under the cursor one step backward / forward (wraps within that column's length; other columns stay) |
| L1 / R1 shoulders | Hold Shift / FUNC |
| Bottom-middle, short press | Open Patterns; in a bank, load the selected slot |
| Shift + bottom-middle | Open Sessions |
| FUNC + bottom-middle | Open Mixer |
| Bottom-middle, held 0.8 s in a bank | Save to the selected pattern/session slot |
| Enter in a bank | Open tempo, scale and root settings |
| F1 / F2 in Mixer | Switch between two pages of six channels |
| Cross in Mixer | Navigate channels and Volume / Mute / Solo |
| Enter in Mixer | Begin editing / commit; volume uses up/down, toggles use either axis |
| L2 | Back: previous page in navigation history (leaves an edit mode first; never exits an appliance) |
| Shift+L2 / R2 | Dashboard / Home (current track spreadsheet from any page) |
| Bottom-left Menu | Play/stop |
| Power, held 0.8 s then released | Open power-off prompt, initially No |
| Cross in power prompt | Left/up selects Yes; right/down selects No |
| Power, short press in power prompt | Confirm the selected choice |

F1/F2 use virtual key codes 317/318. L2 (ABS_Z) is Back; Shift+L2 opens Dashboard;
R2 Home uses ABS_RZ; it cancels an uncommitted edit while preserving the current track, cursor and playback. The cursor uses the full track color with black text, and every step column has a persistent large editing overlay. The magnifier renders
into a clipped, centered surface with 128-pixel side margins on the Brick. The remaining unrequested mappings are provisional;
`input.map` can override button codes without rebuilding. The hardware power and volume
controls remain managed by the vendor services. Brightness starts at the vendor's
maximum normal setting (10/10), and the app restores the previous value on clean exit.

Development updates must also restart blank and stopped: do not save or resume the live session as part of deployment or QA.

Startup is blank and stopped: all 12 tracks have no notes or step effects. The saved session is preserved until explicitly saved over. The banks offer deliberate load/save gestures for each slot.
Instrument slots 01–15 start with VAP, PERC, CHIP, FIBER, HIVE, OP4, ANIMA, RUSSOLO, BAY AREA, RESURGENT, CALIGULA, FM DRUM, OBRADOR, RINGS and PLAITS, one engine per slot. ACD, FAT, WTFM, DIRT and SAMPLE have been removed. [Engine ports and performance](docs/ENGINE-PORTS.md) describe the original cores, adaptation and measured Brick costs. On INST, holding X redraws the TRACK SETTINGS instrument from its current engine’s families and writes that instrument on every row of the column; Shift+hold X instead assigns random instrument slots to the active rows; the track default and stored patches stay unchanged. All steps referencing that slot on the current track hear the new patch; other tracks keep their own instance. FUNC+hold X keeps the slots and patches but gives each row holding an instrument its own variation of it (sessions store them from AMAL24); the pitch-related parameters of every engine are excluded, so notes keep their pitch.
The spreadsheet uses the full available height, without its old three footer lines:
16 rows at 36 pixels with larger type, or eight rows at 72 pixels.
Randomizer MODE and density are independently remembered for each track/column (and
saved with patterns and sessions). Up/down selects MODE or DENSITY. MODE is RANDOM
(each generator places the events) or EUCLIDEAN (the events are spread as evenly as
possible over the column's steps, at a random rotation; NOTE keeps its melodic idiom,
PERC its kit and FM DRUM its accents on those rows). Density: left/right changes it by
1%, or 10% with Shift; hold X runs and returns to the pattern.

ERASE mode (Shift+Enter on any spreadsheet column) frames the cursor in red, keeping the
track colour inside, and shows ERASE MODE in the header. The cursor moves freely across
rows, columns and tracks; Enter clears the selected cell only (a note with its length,
controls and condition; an instrument; an effect and its condition; an offset), each
clearing an undo step. Shift+Enter or Cancel exits, as does leaving the spreadsheet.
The former FX insert parameter page (Shift+Enter on FX 1 / FX 2) was removed in build 16;
inserts keep their default or saved settings.

Editing an FX 1 / FX 2 cell shows three tabs, two at a time: DRY/WET MIX (up/down; left/right
chooses the effect, and a newly chosen effect starts at 50%), LENGTH and CONDITION. The mix
is the same for every effect: 0% is dry, 100% fully processed. Wavefold, Fuzz, Rectify,
Tremolo (sixteenth notes) and Stutter (sixteenth-note slice) now have a fixed character
that the mix blends in. LENGTH (1–64 steps of the FX column, default 1) keeps the effect on
its slot over the following rows; empty rows inside it keep it running and a new effect
row replaces it. When an effect ends or is replaced, effects with memory (CloudSeed Space,
Clouds, QDelay, ChowMatrix, Rings) ring out on their own processor and fade
naturally instead of being cut; at most two tails ring per slot. Clouds cannot ring while
Amp Sim, Biztortion or Rotor (the same processor) takes its slot. Sessions store FX
LENGTH (AMAL21); older sessions load with one-step effects.

No effect can sustain itself (build 21). Each insert's output stays within 6 dB of a
fading memory of its input, which falls 60 dB over 6 s for the ringing effects above and
over 1.5 s for every other effect, Crazy Ivan included; so a feedback loop dies away
once its input stops, also after Stop. The ABRASION and CORROSION send returns follow
their input the same way over 0.5 s (MODULATION over 1.5 s): high drive can no longer
lift a quiet tail into a loud, lasting one.

In the FX editor, holding X draws new parameters for the chosen effect (its DRY/WET MIX
and LENGTH stay; Wavefold, Fuzz, Rectify, Tremolo and Stutter, which have no further
parameters, get a new mix; on an empty row it draws an effect). Shift+hold X draws a new
effect with new parameters, keeping the LENGTH. An effect's parameters belong to its FX
column (slot), so rows of that column using the same effect share them; the notice says
how many other rows the new parameters reach. Enter commits, Cancel discards.

The battery charge (AXP2202 fuel gauge, read every 20 s) appears in the Tracker top bar
between TRACKER and the transport, at the top right of the FX RACK appliance and of the home
screen; it turns red at 15% or less and shows a bolt while charging.
The percentage selects an exact rounded number of populated active steps. Unselected
steps become rests (NOTE), inherit the track instrument (Shift+INST), or clear the
selected FX/modifier. Other columns and hidden rows stay unchanged, except
NOTE generation also fills INST with the track default. Note pitches retain Fugitive's
motif contour; original onset positions take priority before filling gaps. Defaults
are 45% for every column. Density affects step
instrument assignments with Shift; a plain instrument patch redraw always changes
the entire selected patch. A hold runs once and never opens options on release.
Generating a NOTE motif fills every active INST row with the track's assigned
instrument slot. Plain NOTE generation preserves existing controls. Shift+hold X
also generates note conditions, pan, volume and five sends; volume stays10–100%.
Existing FX, modifiers and hidden rows are preserved;
one undo restores both pitches and instrument cells.
Only a deliberate save writes the session. Unsaved edits are not written automatically or on exit. Pending step edits are not committed by saving.
Slots are `/mnt/UDISK/tracker-linux/session-slots/slot-01.amalthea` through `slot-64.amalthea`. The old `/mnt/UDISK/tracker-linux/session.amalthea` remains untouched and is available as slot 01 until a new slot-01 file exists.

Patterns: short bottom-middle opens an 8×4 bank of 32 slots. Shift+bottom-middle
opens the 8×8 bank of 64 sessions; FUNC (right shoulder)+bottom-middle opens Mixer.
In either bank, the cross moves the white selection outline. The loaded slot has
a dedicated blue fill and light underline, independent of track colors. `+` means
saved and `!` means damaged. A short bottom-middle loads the selected slot; a
0.8-second hold saves it once, without loading on release. Enter opens the selected
slot's musical settings; A returns to the bank, then to the editor. Bank loads leave
the bank visible. Empty/damaged slots report an error without replacing the workspace.

Every saved pattern captures tracks, columns, notes, instruments/patches, all insert
and master settings, mixer volume/mute/solo and randomizer densities. Tempo, scale
and root individually default to SESSION; explicit values override the session.
Changing an inherited root transposes note pitches by the root difference, bounded
to the note range. Pattern recall keeps the current play/stop state and restarts
column walks; session recall stops playback. While playing, a pattern load is
queued (amber NEXT) and switches at the end of the current loop of the longest or the
shortest sounding track, as SESSION > SWITCH QUANTIZE sets; all tracks then restart
together. In SESSION settings the cross only moves the selection; START puts the selected
value in edit mode (LEFT/RIGHT change it, START or A leave). RIGHT from SCALE or ROOT
NOTE selects the die beside them; START on it rolls a new scale and root note. Settings changes to the active pattern
edit its working state; save to commit them into its slot.

Sessions contain their defaults, current working state, active pattern and all32
saved patterns. Saving a pattern persists the bank to the active session slot
(slot01 in a new workspace). Saving a session copies the whole bank and workspace
to the selected session slot. Pending step edits are never committed by saving.
Writes use CRC checks, a temporary file, fsync, atomic rename and directory fsync.
Old single-project slot files remain readable. Startup is always blank/stopped;
there is no background save or automatic recall.

Mixer: two six-channel pages with track-colored geometric faders, level percentages,
meters, mute and solo. F1/F2 changes pages. The cross selects a channel/control;
Enter begins editing and commits. Volume uses up/down; mute/solo use either axis.
Multiple solos are supported, and mute takes precedence. Step volume locks retain
their normal precedence over the track fader. Note previews remain audible while
muted or excluded by solo. Master tails continue naturally after Stop.

Musical identities (BPM, notes, cycles and semitones) retain their musical units.
Effect parameters, both the master effects (Abrasion, Corrosion, Modulation,
compressor, delay, reverb, output) and every insert effect's edit view, show the
value the DSP actually uses: Hz/kHz, ms/s, dB, Q, ratio, bits, semitones, note
divisions and named choices. Dry/wet mixes read as "N% WET". Morph controls with no
physical unit (texture, structure, character...) show their 0–100 position without
a percent sign. Times marked "~" (reverb and Rings decay, Clouds position) are
derived from the algorithm's feedback and delay lengths. Mixer levels and sends
stay in percent.

New insert effects: **CL Clouds granular**, **AM Amp Sim**, **BZ Biztortion**, and
**RO Rotor** are selectable directly in FX1/FX2. Their step amount is dry/wet mix.
(Build 16 removed the detailed parameter page these inserts had; their other
parameters keep their defaults or saved values.) Mix locks belong to the selected step; other parameters belong
to that track and FX slot, independently for each processor. Undo includes all of
these controls. Existing effect-selection and value-editing gestures are unchanged.

Clouds has position, grain size, density, texture, stereo spread, feedback, reverb,
freeze and pitch controls. Amp Sim has drive, room, bass, mid, treble, presence and
output level. Rotor has waveform, rate, noise, pulse width, inversion and output
level. Biztortion retains eight chain positions; F1/F2 selects one, and its Module
parameter chooses empty, waveshaper, bitcrusher, slew limiter, filter or scope/pass.
The page exposes the chosen module's full DSP parameters plus input/output level.
All four processors have separate state per track/FX slot, preallocated before audio
starts. Stop releases notes and lets effect tails decay; track-clear resets its histories. See
[effect implementation and attribution](docs/EFFECTS-AND-REFERENCES.md).

Master pages are **Abrasion → Corrosion → Modulation → Compressor → Delay →
Reverb → Output**. The existing reverb sound stays; its name is simply REVERB.
Abrasion is strong band-weighted drive, clipping and folding. Corrosion combines
real bit-depth and sample-rate reduction without noise injection. Modulation
combines a flanger and eight-stage phaser. Each macro exposes eight controls,
including Mix, with shared defaults on Y and independent per-track profiles.

B opens a scrolling track-settings list at the same font size. After level/pan,
the sends follow chain order: Abrasion, Corrosion, Modulation, Delay, Reverb.
The final three SETTINGS rows open macro profiles. SOURCE MASTER inherits shared
defaults; editing makes a local TRACK profile. SOURCE left returns to MASTER.
F1/F2 switches tracks inside profiles. Note-event send tabs override the matching
track send, including0%. All sends are private to their track.

All master effects have Mix. Delay/reverb Mix scales auxiliary wet returns without
attenuating dry tracks. The original CloudSeed sound stays as CS / CLOUDSEED SPACE,
an independent heavier insert with eight controls (their page was removed in build 16). No live Save/Load
occurs during development. Every restart remains blank and stopped.

When editing a NOTE, up/down immediately auditions the displayed pitch without
committing it or starting playback. Empty cells begin at C4; the chosen row
instrument or track default plays. Enter commits; leaving edit mode keeps the
sequence unchanged. Audition also works during playback and on a muted track.

Stop releases notes and keeps processing natural tails; Play restarts column walks
without clearing them. Stutter fades on Stop and granular freeze is released.
See [macro DSP, references and audition](docs/MACRO-EFFECTS.md).

Column settings: short A from the spreadsheet opens LENGTH, CLOCK and DIRECTION
for the selected column (NOTE, INST, FX1, FX2 or MOD). Up/down selects a field;
left/right selects its value. A or Enter returns, and F1/F2 switches tracks.
Opening the page cancels any uncommitted step edit; Escape remains Cancel on Mac.
Each field starts at TRACK, inheriting that track's corresponding setting. Explicit
lengths 1–16, clock ratios 1/8, 1/4, 1/2, 1×, 2×, 4× and all 19 directions override
it independently. Column clock ratios are relative to the session clock, not
multiplied by the track ratio. Changing a track setting affects only inheriting
fields; it cannot truncate or slow an explicitly configured column.

All five columns have independent positions, clocks, traversal state and cycles.
Every cell has a positive cycle condition, initially1:1. Enter opens the Value tab;
F1/F2 browses tabs, displaying only two at a time. NOTE adds Pan, Volume, Delay
Send, Reverb Send, Abrasion Send, Corrosion Send and Modulation Send. Up/down traverses the36 choices in
Bigorna order:1:1,1:2,2:2,1:3,2:3,3:3,...8:8. Enter commits value and condition;
A/Cancel or Home discards pending changes. Every changed event previews immediately before Enter, while Cancel restores
the stored event. Note controls inherit the track by default; Up/down overrides
the percentage and Left restores inheritance. Outside the cell editor,
F1/F2 retains its current page's track/module/pagination function.

A:B means the event applies on cycle A of every B cycles of that particular column.
The former shared COND column is removed. NOTE conditions gate note attacks; INST
conditions choose the explicit instrument or the track default; inactive FX events
bypass that insert slot; an inactive OFFSET leaves its note as written, and an inactive
TRANSPOSE does not change the transposition on that cycle.
Conditions are independent, with no nesting against another cell's condition.
FX columns apply insert processors immediately at their own ticks, including to held audio.
When ticks coincide, all value columns advance before NOTE samples them.
Ties/gates follow the NOTE column's timing and direction. Clock changes retain
fractional phase; length/direction changes restart that column unless it matches
an existing lane, whose actual playback phase it joins. Columns with identical
effective length, rate and direction always remain synchronized, including random
directions. Adding notes to an empty playing sequence restarts NOTE and any matching
lanes together; differently programmed lanes keep moving;
Shift+B and transport restart reset all columns together.

Moving highlights follow each column separately; the bright editing cursor stays
fixed. Columns with matching instructions retain the full-row highlight. Differently configured columns keep separate highlights even when they momentarily meet on the same row. Dots mark inactive cells
below a column's end, and the spreadsheet extends to the longest active column.
Navigation and randomization respect the selected column's length; octave shifts
respect NOTE length. Hidden content is preserved. NOTE generation also assigns
the track's default instrument across active note/instrument cells.

Track settings: B opens the selected track's settings. Up/down selects one of eight rows;
left/right changes it. F1/F2 switches tracks in this page too. Length is 1–16 steps;
shortening keeps hidden notes so extending restores them. Clock offers 1/8, 1/4, 1/2,
1x, 2x and 4x relative to the session's sixteenth-note clock. Tracks keep separate
playheads and cycle counters. Tempo/rate changes preserve the remaining fraction
of a step; changing length/direction rebuilds traversal at its next tick.

Direction reuses Fugitive's original 19 walkers: forward, reverse, ping-pong,
pendulum, odd-even, rocking, cascade, pairs, blocks reverse, blocks retro,
converge, diverge, double, stride, drunk, meander, shuffle, random and fragments.
Ping-pong cycles take 2×length−2 advances (one at length 1); pendulum/double/rocking
use 2×length, cascade uses 3×length. Other directions count length advances per
condition cycle, including random walks and fragments. Playback restarts repeat
seeded walks; shuffle visits every step once per cycle.

The instrument row selects the track default. An INST cell showing `--` inherits
it; explicit slots 01–11 override it. While editing INST, move down below slot 01
to restore inheritance. Holding X redraws the track instrument's patch and writes it on every row. Shift+hold X fills the active INST column with random slots. Legacy sessions
keep their existing explicit instrument choices.

Volume, pan, delay send and reverb send are track defaults. Pan is 0% left, 50%
center, 100% right. Note-event Pan, Volume and send overrides replace only
the matching default, including explicit zero. Other defaults remain
active. Sends use the track's processed,
panned signal. Locks apply on the selected step until the next track advance.
All track parameters participate in undo, session save and recall.

Shift+cross up/down transposes the active sequence by exactly 12 semitones per
press, including its pending note edit. Rests, OFF, TIE, instruments, effects,
conditions, modifiers, hidden rows and other tracks stay unchanged. A gesture
that would take any pitch outside the supported range is rejected for the whole
sequence, preserving its intervals. Holding the cross does not repeat octave
jumps. Each gesture is undoable.

During playback, a dim track-colored band spans the entire playing row. The
selected cell stays bright and stationary; the band follows the selected track's
actual playhead, including its direction, length and clock rate.

Clearing a track now resets its voices, both insert histories and local cycle
phase immediately. Building a new sequence on an empty track starts it from its
first step. Clearing the last sounding track also clears shared effect histories;
other configured tracks keep the shared effects when they remain in use. VAP
clear/rebuild regression tests compare the resulting audio sample for sample
against a fresh engine after eight different step-effect histories.

Master FX: F1/F2 selects a module; up/down selects a parameter (four visible rows; nine controls for reverb); left/right changes that parameter from 0–100 (Shift gives larger steps). Chain: Abrasion → Corrosion → Modulation → compressor → delay → reverb. Track sends enter their corresponding
bus at the end of that chain. Output level and a stereo-linked safety limiter follow.
The macro implementations and reference work are documented in docs/MACRO-EFFECTS.md.
Volume keys remain controlled by the vendor service. Amalthea observes actual hardware
attenuation and displays a 1.5-second horizontal percentage bar. The A133 headphone control is reversed relative to its ALSA dB metadata: raw0 is
maximum output; raw7 heavily attenuates it. Version0.4.1 sets raw0 on launch. The owner
confirmed this corrects the low-output problem. Earlier releases incorrectly increased
the raw setting based on misleading driver labels. Software voice gain stays at unity.
See the [A133 headphone setting reference](https://github.com/knulli-cfw/distribution/pull/264).
The previous headphone setting is restored on clean exit; side-volume selection is retained.
While running, Amalthea selects the stock CPU performance governor to avoid audio
underruns during frequency ramp-up, and restores the previous governor on clean exit.
The kernel's thermal limits remain active.

Power: hold for roughly one second and release to open the prompt. The opening
release does not confirm. Select Yes/No with the cross, then short-press Power
again. Enter does not confirm. No preserves playback, page and pending edits.
Yes stops playback, displays POWERING OFF, syncs filesystems and invokes the
system's normal `/sbin/poweroff` shutdown path. Saving remains explicit; the
prompt reports actual unsaved work. An already requested save finishes before
shutdown proceeds.

The app exclusively reads the existing `axp2202-pek` power input device while it
runs, preventing the vendor service from also sleeping/shutting down on the same
press. The grab releases automatically on app exit. Vendor volume, charging and
thermal services continue running. PMIC, bootloader and kernel power-on settings
are unchanged: use Power normally to turn the device on from off. A short Power
press outside the dialog has no tracker action. The simulator never powers off
the laptop.

Project format13 stores all per-cell conditions, patch identities, column geometry,
insert profiles and master effects. Session container AMSB2 includes its32 complete
pattern snapshots and five-column randomizer settings. Project versions1–12 and
session container AMSB1 remain readable. Old shared conditions become NOTE-cell
conditions; other cells initially use1:1. The old COND lane is discarded and MOD
geometry/density is preserved. Existing save files are never rewritten by startup.


## Mac build and simulator

Requires CMake, a C++17 compiler and SDL2 (available through Homebrew).

```sh
./tools/build_mac.sh
open build/mac/Amalthea.app
```

The app shares the core, DSP and CPU renderer with the Brick. F1 selects the Brick's
1024×768 layout; F2 selects the RG35XX H's 640×480 layout. The side panel provides
clickable virtual buttons. It is a functional control diagram, not a finished housing simulation.

| Mac key | Action |
| --- | --- |
| Arrows / Return | Cross / Start |
| X / A / Y / Space | Randomizer / column settings or Back / master effects / play |
| P / B | Session grid / track settings |
| Left / right Shift | Shift / Shift2 |
| Q / E | Previous / next track |
| Home | Return directly to the current track spreadsheet |
| M / Tab / Z / R | Mixer / Dashboard / undo / redo |
| H | Help |
| F12, held then released / short press | Open power prompt / confirm (simulator closes itself on Yes) |
| S / L | Save to active slot / load selected slot |
| C / V | Copy / paste selected track |
| Backspace | Clear current step |
| + / - | Tempo up/down |

The Mac session is in `~/Library/Application Support/Amalthea/session.amalthea`.
Use `--project PATH` to select the save destination; add `--load` to explicitly recall it at launch for diagnostics. Normal startup never recalls saved notes automatically. The first Mac UI check changed its demo's first
note from C-3 to C#3 to verify edit/commit and saving.

```sh
build/mac/Amalthea.app/Contents/MacOS/Amalthea --headless --screenshot build/editor.ppm
build/mac/Amalthea.app/Contents/MacOS/Amalthea --headless --stress --render build/demo.wav --seconds 16
```

`--stress` unmutes all demo tracks for diagnostics. `--voices N` configures
the preallocated voice pool (128 by default, at least 12). The Brick uses an 80 ms ALSA buffer to retain audio continuity during saves and UI activity. No allocation occurs in playback. Patch recipes and engine cooking run on the UI thread; active voices retain their cooked patch until release.

## Brick build and deployment

```sh
./tools/build_brick.sh
python3 tools/deploy_brick.py
```

The ARM64 Docker build uses Ubuntu 20.04 / glibc 2.31 for compatibility with the Brick's
glibc 2.33. `build_brick.sh` assigns the next build number (`BUILD_NUMBER`).
`deploy_brick.py` pushes only the appliance binary to
`/mnt/UDISK/amalthea-linux/app/amalthea`, verifies its SHA-256 and restarts the app
blank and stopped (about 10 seconds). The root supervisor prefers that file over the
read-only `/usr/bin/amalthea`, and falls back to the built-in app after three failed
starts or when the file is removed. App changes never need a root image.

The two FX slots now select only insert processors. Pan, volume and all five
sends live in the note-event editor. Diode and Rat Drive are retired; the former
low/high-pass entries are replaced by a six-mode resonant multimode filter. [Effects and cycle modifier guide](docs/EFFECTS-AND-REFERENCES.md)
lists effect codes and per-cell cycle examples.

MOD cells start empty (`---`). Editing one, Up/Down sets semitones (an empty cell becomes
an OFFSET) and Left/Right chooses OFFSET, TRANSPOSE or empty. An OFFSET (`+02`) shifts its
own row's note. A TRANSPOSE (`T+2`) shifts every note from its row to the end of the MOD
column's cycle, or until the next TRANSPOSE (`T+0` returns to the written notes); offsets
add to it. Each MOD cycle starts untransposed, so a TRANSPOSE on step 1 transposes the
whole sequence, and one conditioned 1:2 transposes every other cycle. Sessions saved
before build 38 load their `+00` cells as empty.
A RANDOM cell (`RND`, TYPE after TRANSPOSE) redraws NOTE column parameters of its row's
note each time it is applied (its condition met). It adds a TARGETS tab (F2): TARGET 1 is
ANY (the default: one of NOTE, LENGTH, PAN, VOLUME and the four sends, drawn afresh each
time; TARGETS 2 and 3 are greyed out) or a parameter, and TARGETS 2 and 3 add up to two
more, every one redrawn each time. A random NOTE is a note of the session scale and root
note within an octave of the row's note.
Project format 2 imports version 1, activates its first 16 steps and preserves notes,
instruments, level/pan commands and archived rows. Removed probability/gate/transpose
commands and the former third/fourth FX slots are omitted during migration. Original
projects and the old executable were backed up under `backups/pre-0.2/`.

See [HANDOFF.md](HANDOFF.md) for architecture, verification and remaining work.

The header uses a large track-colored T1–T12 badge. Settings pages omit shortcut
footers and use larger controls. Moving playhead cells use a moderately brighter
track-color fill and independent-lane edge; the edit cursor remains fixed.

## Dashboard and device preferences

Shift+L2 (or Tab on the desktop) opens a five-tile dashboard: Track, Mixer, Master FX,
Insert FX, and Device. Use the cross to select and Enter to open. R2 remains Home.
Insert FX lists 11 processors; up/down selects, Enter toggles, left disables, and
right enables. Bypass applies across both slots on every track without deleting
stored effects. Disabled processors are excluded from new FX randomization.
The Device page adjusts brightness from 0–100% with the cross.

Brightness and global insert switches persist separately in
`device-settings.amalthea` beside the session path. They are device preferences,
not pattern/session state. Startup remains blank and stopped.
[USB audio findings](docs/USB-AUDIO.md) distinguish external host interfaces from
presenting this Brick as a bidirectional USB sound card to a computer.

## Native engines and track-local instruments

ANIMA, RUSSOLO, BAY AREA, RESURGENT and CALIGULA join the six existing engines.
Choose instruments 01–11 directly in INST, or choose the default instrument in
Track Settings. The ENGINE row can additionally replace a slot's engine on this track. Hold X on INST
draws a new patch for the selected slot on the current track. Shift+hold X still
changes step instrument assignments, preserving the patches and track default.

Each track has its own eleven instrument instances. Identical instrument numbers
on different tracks never share editable patches. Step overrides resolve within
the current track. Undo, patterns and sessions retain these independent banks.
Older sessions initialize each track from the old bank; edits detach locally. New
files use AMAL16, with the same explicit user-operated save/load controls.

The five new engines expose 157 named generative families in total. VAP, PERC,
CHIP, HIVE and OP4 also gain stronger discovery variants; Fiber's generator now
includes its experimental path. Zero-seed defaults stay unchanged. See
[engine implementation and limits](docs/ENGINE-PORTS.md).

The shared platform [roadmap](docs/PLATFORM-ROADMAP.md) records deferred
multichannel USB hosting for future appliances, separate from the tracker.

## Event controls and multimode filter (0.17.0)

NOTE has nine tabs, with only two visible at once: Value, Condition, Pan, Volume,
Delay Send, Reverb Send, Abrasion Send, Corrosion Send, Modulation Send. F1/F2
moves backward/forward. Up/down changes percentages; Left restores track
inheritance, Right copies the current track default as an explicit override.
Enter commits the event; Home or Cancel discards pending changes.

All event changes audition immediately. While editing, playback receives a
transient copy of the pending event; the stored pattern is changed only by Enter.
Shift+hold X on NOTE also draws its condition and all seven controls, with volume
restricted to10–100%. Ordinary hold X preserves those controls and draws pitches.
Density remains relative to the NOTE column's active length. Custom ranges remain
a future session feature.

MULTIMODE FILTER offers low pass, band pass, high pass, notch, all pass and peak.
Its controls are Mix, Mode, Cutoff, Resonance, Drive and Level (page removed in build 16). Its native stereo
TPT core has smoothed controls, 2x processing with matched half-band FIR filters,
and an aligned dry path. Low-pass DC gain stays1 at every resonance setting;
high-pass and band-pass modes intentionally reject low frequencies. AMAL16 saves
note overrides and per-slot filter profiles; AMAL1–15 remain readable.


## Nine additional inserts (0.18.0)

FX1 and FX2 now offer QDelay, ChowMatrix, CrushClone, Frequalizer, Rings,
Fracture 2, Crazy Ivan, Bitmurderer and Lo-Fi. Select an effect in the event editor;
Their detailed-controls page (removed in build 16) used F1/F2 to select ChowMatrix's global
page/eight nodes or Frequalizer's six bands. Each track and slot owns its settings
and DSP state. The event value remains dry/wet. Dashboard → Inserts enables or
disables each processor globally.

**FLANGER** (Surge XT) and **PHASER** (ChowPhaser) are FX1/FX2 inserts from build 25.
The flanger runs up to four tuned comb voices with feedback, six LFO shapes and arpeggiated
comb modes; the phaser runs 1–50 light-modulated all-pass stages with saturated feedback.
Both start fully wet, since they mix their own dry path.

See [port details and adaptations](docs/INSERT-PORTS.md). AMAL17 adds these
profiles while retaining AMAL1–16 reading; device preferences migrate from AMDV1
to AMDV2 for the expanded insert switches. Startup remains blank and stopped.


## Amalthea appliance shell (0.19.0)

Startup shows the Amalthea orbital-A splash and the appliance home grid. Choose
TRACKER or FX RACK with the cross and press the existing Enter button (315, bottom-right)
to open it. Tracker starts blank and stopped; FX RACK is an explicit development
placeholder, not a working USB audio processor. Back/Home leaves the FX RACK preview.
The existing tracker dashboard and Home shortcuts remain local to Tracker.

To leave Tracker, hold FUNC (right shoulder) first, then press and hold Shift (left
shoulder). The reverse order does nothing. The first prompt has CANCEL left and OK
right; choose with the cross and confirm with Enter. OK opens SAVE BEFORE EXIT,
with NO left and YES right. NO discards the current workspace and returns home.
YES atomically saves to the loaded session slot, or the selected session slot when
none is loaded, then returns home. A failed save keeps Tracker open with an error.
There are no automatic session writes on startup or exit without Yes.

Within an event editor, tap X to randomize only the currently shown parameter and
audition it before Enter commits. Cancel discards this local change, including its
PRNG state. Hold Shift and hold X for 0.8 seconds to randomize that parameter across
the current column length; it stays in the editor, is immediately audible, and is
one undoable operation. Other parameters and columns stay intact. Bulk note pitch
randomization preserves rests and note-off/tie events. Notes obey the session or
pattern scale/root, and randomized volume stays within10–100%. FX value
randomization preserves the selected insert type. Outside event editing, existing
X density/column gestures are unchanged.

Brand artwork is under `assets/identity`: SVG system/Tracker/FX RACK marks and the boot
PNG. The same native renderer draws the splash and home UI; `render_identity`
recreates the preview PPMs for conversion to PNG without additional runtime libraries.
