#pragma once
#include <algorithm>
#include <array>
#include <bitset>
#include "insert_settings.hpp"
#include "macro_settings.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
namespace amalthea {
constexpr int Tracks = 12, MaxRows = 256, Instruments = 15, PatternRows = 16, Columns = 5;
constexpr int EventConditionBytes=Tracks*MaxRows*(Columns-1)*2;
constexpr uint8_t NoInstrument = 255;
constexpr int Directions = 19;
enum class FxType : uint8_t {
  None,
  Reverb,
  Delay,
  Distortion,
  Saturation,
  Overdrive,
  Bitcrush,
  Redux,
  Wavefold,
  Fuzz,
  Rectify,
  Diode,
  Rat,
  Lowpass,
  Highpass,
  Tremolo,
  Stutter,
  Pan,
  Volume,
  Clouds,
  AmpSim,
  Biztortion,
  Rotor,
  Space,
  Abrasion,
  Corrosion,
  Modulation,
  Filter,
  QDelay,
  ChowMatrix,
  CrushClone,
  Frequalizer,
  Rings, Fracture2, CrazyIvan, Bitmurderer, LoFi,
  Random,   // each trigger loads a different effect with its own parameters (AMOUNT scales it)
  Flanger, Phaser,   // AMAL22: appended after RANDOM so stored effect numbers keep their meaning
  // AMAL23: native effects (native_fx.hpp), batch 1. Append only.
  Oops, BufferOverride, Livecut, SupaTrigga, BeatRepeat, TimeShaper, Jumblr, Scrubby, Glitchlets, Skidder,
  // Batch 3 follows batch 2. Append only.
  Choppr, GateShaper, TremoSquare, Beads, Softcut, Transverb, GlitchShifter, PitchNasty, Amalgam, Geometer, MultiBandDistortion, Disintegrate, BitGlitter, Pafnuty,
  Cyanide, Polarizer, Angr, Warps, FreqShifter, BrokenFft, Treemonster, RingMod, Combulator, Resonator, Vocoder, Smr, RezSynth, TalkBox, MdaVocoder, RePsycho,
  Count
};
constexpr int GlobalInsertCount=22+NativeCount;
constexpr int NoteParameters=7;
constexpr uint8_t Inherit=255;
constexpr int NoteLockBytes=Tracks*MaxRows*NoteParameters;
constexpr int FilterSettingsBytes=Tracks*2*5;
constexpr int PortSettingsBytes=Tracks*2*(128+6+8*14+11+32+7+4+20+19+5);
constexpr int ModulationSettingsBytes=Tracks*2*(11+11);   // AMAL22: FLANGER and PHASER profiles
enum NoteParameter { NotePan,NoteVolume,NoteDelay,NoteReverb,NoteAbrasion,NoteCorrosion,NoteModulation };
const std::array<FxType,GlobalInsertCount>& globalInsertTypes();
struct DeviceSettings {
  uint8_t brightness=100;
  // One bit per FxType (the native effects take the numbers past 63).
  std::bitset<128> disabledInserts{};
  bool insertEnabled(FxType type)const{return !disabledInserts[int(type)];}
  bool operator==(const DeviceSettings&o)const{return brightness==o.brightness&&disabledInserts==o.disabledInserts;}
  bool operator!=(const DeviceSettings&o)const{return !(*this==o);}
};
bool saveDeviceSettings(const DeviceSettings &,const std::string &,std::string &);
bool loadDeviceSettings(DeviceSettings &,const std::string &,std::string &);
// A trigger condition: the OCCURRENCE-th of every EVERY cycles of its column (1:1 always).
struct Condition {
  uint8_t occurrence = 1, every = 1;
  bool always() const {return occurrence==1&&every==1;}
};
struct Effect {
  FxType type = FxType::None;
  uint8_t value = 0;   // DRY/WET MIX: 0 dry .. 100 fully wet
  uint8_t length = 1;  // LENGTH in FX-column steps (1 .. MaxFxLength); tails then fade out
  // RANDOM (step editor): each time the step plays (its condition allows it), the chosen
  // effect gets a newly drawn parameter set; the step's DRY/WET MIX stays as set.
  bool random = false;
  // Each FX parameter's own condition, on the FX column's cycle. Not met: LENGTH plays as one
  // step, RANDOM as NO. (The effect itself, with its DRY/WET MIX, uses the cell's condition.)
  Condition lengthCondition{}, randomCondition{};
};
constexpr int MaxFxLength = 64;
constexpr uint8_t DefaultFxMix = 50; // a newly chosen effect starts half wet
// FLANGER and PHASER blend their own dry path to form the notches, so they start fully wet.
inline uint8_t defaultFxMix(FxType t){return t==FxType::Flanger||t==FxType::Phaser?100:DefaultFxMix;}
// Effects with memory (reverb, delay, granular, resonator): their tail rings out when they end.
// Every insert's output is also held to a decaying memory of its input (see InsertEffect),
// so no effect can sustain itself: ringing ones fade within RingingTailSeconds, others
// within ShortTailSeconds of their input going quiet.
constexpr float RingingTailSeconds=6,ShortTailSeconds=1.5f;
constexpr bool ringingEffect(FxType t){return t==FxType::Space||t==FxType::Clouds||t==FxType::QDelay||t==FxType::ChowMatrix||t==FxType::Rings;}
// MOD column cell. OFFSET shifts its own row's note; TRANSPOSE shifts every note from its
// row to the end of the MOD column's cycle (or the next TRANSPOSE), offsets included.
// RANDOM redraws up to three of the NOTE column's parameters of its row's note each time
// it is applied (its condition met). Append only (sessions store the number).
enum class ModifierType : uint8_t { None, Offset, Transpose, Random };
constexpr int ModifierTypes = 4;
// RANDOM targets. ANY (first target only) draws one of the parameters each time and leaves
// the other two targets unused; otherwise every chosen target is drawn. NOTE draws a note
// of the session scale and root note within an octave of the row's note. Stored numbers.
enum RandomTarget : uint8_t { TargetNone, TargetAny, TargetNote, TargetLength, TargetLock, RandomTargets = TargetLock + NoteParameters };
constexpr int ModifierTargets = 3;
const char *randomTargetName(int);
struct Modifier {
  int8_t semitones = 0;
  ModifierType type = ModifierType::None;
  std::array<uint8_t,ModifierTargets> targets{{TargetAny,TargetNone,TargetNone}};
};
constexpr bool validTargets(const std::array<uint8_t,ModifierTargets> &t){
  if(t[0]==TargetNone||t[0]>=RandomTargets)return false;
  for(int i=1;i<ModifierTargets;i++){if(t[i]>=RandomTargets||t[i]==TargetAny)return false;if(t[0]==TargetAny&&t[i]!=TargetNone)return false;
    for(int j=0;j<i;j++)if(t[i]!=TargetNone&&t[i]==t[j])return false;}
  return true;
}
struct Step {
  int16_t note = -1;
  uint8_t instrument = NoInstrument;
  std::array<Effect, 2> fx{};
  Condition condition{}; // NOTE condition; retained in the original file payload
  std::array<Condition,Columns-1> eventConditions{};
  Modifier modifier{};
  std::array<uint8_t,NoteParameters> locks{{Inherit,Inherit,Inherit,Inherit,Inherit,Inherit,Inherit}};
  // NOTE LENGTH in steps (1 .. 64). Reaching the next note makes a click-free legato into it.
  uint8_t length = 1;
  // Each NOTE parameter's own condition, on the NOTE column's cycle, independent of the
  // others (the note itself uses `condition`). Not met: LENGTH plays as one step, a lock
  // as the track's value.
  Condition lengthCondition{};
  std::array<Condition,NoteParameters> lockConditions{};
  // INSTRUMENT column: a per-row variation of the row's instrument sound (0: the patch as
  // it is). The seed redraws the patch's non-pitch parameters; pitch and tuning never change.
  uint32_t variation = 0;
};
// FUNC + hold X on the INSTRUMENT column draws at most this many distinct variations per
// track; later rows of a longer column reuse earlier rows' variations of the same instrument.
constexpr int VariationsPerTrack = 16;
constexpr int MaxNoteLength = 64;
// On-screen / cursor order of the columns: NOTE, MOD (it modifies the note), INST, FX 1, FX 2.
// Stored column numbers (sessions, settings, densities) are unchanged.
constexpr int ColumnOrder[Columns]={0,4,1,2,3};
constexpr int columnPosition(int c){return c==0?0:c==4?1:c+1;}
struct ColumnSettings {
  uint8_t length=0,clock=255,direction=255; // inherit the corresponding track value
};
// Step volumes (locks, Volume FX) are written against the default fader: a step
// plays at its own value while the track sits at DefaultTrackLevel, and the
// fader scales every step proportionally (step x level / DefaultTrackLevel).
constexpr int DefaultTrackLevel = 90;
struct Track {
  std::array<Step, MaxRows> steps{};
  uint8_t level = DefaultTrackLevel;
  int8_t pan = 0;
  bool mute = false, solo = false;
  uint8_t length = PatternRows, clock = 3, direction = 0, instrument = 0;
  uint8_t delaySend = 0, reverbSend = 0;
  std::array<uint8_t,3> macroSend{};
  uint8_t macroOverride=0;
  std::array<MacroSettings,3> macros=defaultMacros();
  std::array<InsertSettings,2> inserts{};
  std::array<ColumnSettings,Columns> columns{};
};
constexpr int MasterModules = 11, MasterParameters = 4;
// Instrument slots retain their identity while the contained engine patch changes.
constexpr int SynthEngines = 15;
constexpr int FmDrumEngine = 11, ObradorEngine = 12, RingsEngine = 13, PlaitsEngine = 14;
// Vascular / Noisferatu engines, and the Mutable Instruments RINGS and PLAITS, run in their own
// native voice storage: one running instrument per track / slot / patch, retriggered by its notes.
constexpr bool nativeEngine(int e){return (e>=6&&e<=10)||e==RingsEngine||e==PlaitsEngine;}
struct InstrumentPatch {
  uint8_t engine = 0;
  uint32_t seed = 0; // zero keeps the engine's original default patch
};
struct MasterSettings {
  // saturation, overdrive, distortion, crush, redux, flanger, phaser,
  // compressor, delay, reverb, output. Reverb is last in the DSP chain.
  std::array<uint8_t, MasterModules> amount{{0,0,0,0,0,0,0,70,0,100,100}};
  std::array<std::array<uint8_t,3>,MasterModules> extra{{
    {{40,100,100}}, {{50,100,100}}, {{80,100,100}}, {{0,100,100}},
    {{0,100,100}}, {{35,50,50}}, {{35,30,50}}, {{13,43,44}},
    {{55,50,100}}, {{35,80,65}}, {{50,100,50}}
  }};
  std::array<uint8_t,4> reverbSpace{{10,100,20,75}}; // TAL predelay, width, low frequency, high frequency
  std::array<uint8_t,4> busMix{{100,100,100,100}}; // compressor, delay, reverb, output
  std::array<uint8_t,2> sidechain{{0,0}}; // compressor SIDECHAIN (0 off, 1 on), SOURCE track (0 .. 11)
  std::array<MacroSettings,3> macros=defaultMacros();
  uint8_t &value(int module,int parameter) { if(macroIndex(module)>=0)return macros[macroIndex(module)].value[parameter];if(module==7&&parameter>=5)return sidechain[parameter-5];return module>=7&&parameter==(module==9?8:4) ? busMix[module-7] : module==9&&parameter>=4 ? reverbSpace[parameter-4] : parameter ? extra[module][parameter-1] : amount[module]; }
  uint8_t value(int module,int parameter) const { if(macroIndex(module)>=0)return macros[macroIndex(module)].value[parameter];if(module==7&&parameter>=5)return sidechain[parameter-5];return module>=7&&parameter==(module==9?8:4) ? busMix[module-7] : module==9&&parameter>=4 ? reverbSpace[parameter-4] : parameter ? extra[module][parameter-1] : amount[module]; }
};
enum class Page { Pattern, Session, Master, Track, Randomizer, Sessions, Column, Macro, Patterns, PatternSettings, Mixer, Dashboard, GlobalInserts, Device, Appliances, FxAppliance, FxSequencer, FxParameters, FxPreferences, FxSessions, FxSessionSettings, FxRandomizer,
  LifebloodField, LifebloodNode, LifebloodSnapshots, LifebloodSessions, LifebloodSettings, Performance };
struct Project {
  uint32_t seed = 0x414d414c;
  uint16_t bpm = 120, length = PatternRows;
  std::array<Track, Tracks> tracks{};
  std::array<InstrumentPatch, Instruments> patches = [] {
    std::array<InstrumentPatch, Instruments> a{};
    for(int i=0;i<Instruments;i++) a[i].engine=i;
    return a;
  }();
  // Legacy bank supplies initial values; edits detach one track/slot.
  std::array<std::array<InstrumentPatch,Instruments>,Tracks> trackPatches{};
  std::array<uint16_t,Tracks> localPatchMask{};
  uint8_t root = 0, scale = 1;
  MasterSettings master{};
  // Runtime clear notifications; intentionally excluded from the session format.
  std::array<uint64_t,Tracks> trackReset{};
};
enum class Action {
  Up,
  Down,
  Left,
  Right,
  Enter,
  Cancel,
  Home,
  Dashboard,
  RandomizeColumn,
  RandomizerOptions,
  Reserved,
  TrackSettings,
  ColumnSettings,
  PowerPrompt,
  PowerConfirm,
  ShiftDown,
  ShiftUp,
  Shift2Down,
  Shift2Up,
  PrevTrack,
  NextTrack,
  Play,
  Mixer,
  Session,
  Master,
  Patterns,
  Rows,
  Undo,
  Redo,
  Save,
  Load,
  SavePattern,
  LoadPattern,
  SaveSession,
  LoadSession,
  Clear,
  Copy,
  Paste,
  TempoUp,
  TempoDown,
  Back
};
// SWITCH QUANTIZE: while playing, a pattern change waits for the end of the current loop
// of the longest (or the shortest) sounding track; every track then restarts together.
enum PatternQuantize : uint8_t { QuantizeLongest, QuantizeShortest, PatternQuantizeModes };
const char *patternQuantizeName(int);
constexpr int SessionFields=4;   // SESSION settings: TEMPO, SCALE, ROOT NOTE, SWITCH QUANTIZE
constexpr int SessionDie=4;      // the die beside SCALE and ROOT NOTE (a place on the page, not a value)
struct MusicalSettings { uint16_t bpm=120; uint8_t root=0,scale=1; uint8_t quantize=QuantizeLongest; };
struct PatternOverrides { uint16_t bpm=0; uint8_t root=255,scale=255; };
// Randomizer settings per track/column: density 0..100 in the low 7 bits, and the
// distribution MODE in bit 7 (set: EUCLIDEAN, clear: RANDOM). Older sessions hold
// plain percentages, so they load as RANDOM.
using DensitySettings=std::array<std::array<uint8_t,Columns>,Tracks>;
constexpr uint8_t EuclideanMode=0x80;
constexpr int densityPercent(uint8_t v){return v&0x7f;}
constexpr bool euclideanMode(uint8_t v){return v&EuclideanMode;}

struct PatternSlot { std::shared_ptr<const Project> state; PatternOverrides overrides{}; DensitySettings density{}; };
struct UndoState { Project project; MusicalSettings sessionDefaults; PatternOverrides overrides; DensitySettings density; };
bool trackAudible(const Project &,int);
struct FxRack;
struct Performance;
struct LifebloodSession;
struct Editor {
  Project project{};
  DeviceSettings device{};
  int dashboardItem=0,globalInsertItem=0;
  int applianceItem=0;
  std::shared_ptr<FxRack> fxRack;
  // PERFORMANCE mode (tracker, Y; F1 leaves): temporary, never written into the project
  // (performance.hpp). The serial changes with every press, for the audio and the screen.
  std::shared_ptr<Performance> performance;uint64_t performanceSerial=0;
  void enterPerformance();void leavePerformance();void performanceAction(Action);
  int fxVisibleChannels=6;
  int fxChannel=0,fxRow=0,fxStep=0,fxParameter=0,fxPage=0,fxPreference=0;
  bool fxLevelEdit=false,fxDefaultEdit=false;int fxRandomizerField=0;
  int fxSessionSlot=0,fxActiveSession=-1,fxSessionField=0,fxSessionRequest=0;
  std::array<uint8_t,64> fxSessionState{};
  bool inFx()const{return page==Page::FxAppliance||page==Page::FxSequencer||page==Page::FxParameters||page==Page::FxPreferences||page==Page::FxSessions||page==Page::FxSessionSettings||page==Page::FxRandomizer;}
  void fxAction(Action);
  // LIFEBLOOD appliance (lifeblood_editor.cpp). lbVertex: selected vertex (0 IN, 1 OUT, 2+ nodes).
  std::shared_ptr<LifebloodSession> lifeblood;
  int lbVertex=0,lbParameter=0,lbPage=0,lbSnapshotSlot=0,lbSessionSlot=0,lbActiveSession=-1,lbSettingsField=0,lbSettingsTab=0;
  // Requests the main loop carries out: 1 load / 2 save the selected session; 3 persist after a snapshot store.
  int lbSessionRequest=0;
  std::array<uint8_t,64> lbSessionState{};
  bool lbBypass=false;           // Play: the input passes dry (runtime only)
  int lbInputs=-1;               // FM1 inputs: -1 none yet, 0 stereo, 1..6 tracks (from the USB layout)
  uint8_t lbHeard=0;             // tracks heard recently (bit per track; from the audio)
  int lbClockBpm=0;              // the FM1's MIDI clock tempo while it is locked, else 0 (from the audio)
  uint32_t lbGeneration=0;       // runtime topology counter
  float lbClock=0;               // animation time, seconds
  bool inLifeblood()const{return page==Page::LifebloodField||page==Page::LifebloodNode||page==Page::LifebloodSnapshots||page==Page::LifebloodSessions||page==Page::LifebloodSettings;}
  bool inAppliance()const{return inFx()||inLifeblood();}
  void lifebloodAction(Action);
  // Advance animation and angiogenesis; true when the network changed.
  bool lifebloodTick(float seconds);
  void openLifeblood();
  enum class ExitStage { None, Confirm, Save, Saving };
  ExitStage exitStage=ExitStage::None;
  bool exitChoice=false,exitSaveRequested=false,splash=false;
  // Battery charge from the power supply (-1: unknown, not drawn). Runtime only.
  int battery=-1; bool charging=false;
  uint32_t eventRandomSeed=0;
  // Runtime entropy mixed into generated material (0 in tests: fully seeded).
  // Never stored in sessions; the project seed alone still drives undo/redo.
  uint32_t entropy=0;
  uint32_t freshSeed();
  void requestAudition();
  // The note an OFFSET (MOD column) acts on: the row's own, else the nearest earlier note.
  int modifierBaseNote() const;
  // Hold X while editing an FX row: a random effect for that row only.
  void randomizePendingEffect();
  // Hold X while editing an FX row: randomize the chosen effect's parameters only.
  void randomizePendingParameters();
  // The focused row of a settings page without its own slider: its name and its value
  // as the page shows it. False elsewhere.
  bool focusedSetting(std::string &name,std::string &value) const;
  bool pendingInsertSet=false;
  InsertSettings pendingInsert{};
  void randomizeBlankPatches();
  // FUNC + hold X on the INSTRUMENT column: every row holding an instrument gets its own
  // variation of that instrument's non-pitch parameters (editing: the edited row only).
  void varyInstrumentRows();
  void varyPendingInstrument();
  bool eventRandomized=false;
  void leaveAppliance();
  void randomizeEventParameter(bool wholeColumn);
  int track = 0, row = 0, column = 0, visibleRows = 16, fxPart = 0, eventTab=0;
  bool editing = false, mixer = false, playing = false, dirty = false;
  // ERASE mode (Shift+Enter on the spreadsheet): navigate freely; Enter clears the
  // selected cell. Shift+Enter or Cancel leaves it, as does leaving the page.
  bool erasing = false;
  void eraseCell();
  // Shift+F1 / F2 on the spreadsheet: rotate the whole track sequence one step
  // backward / forward. Every column wraps within its own active length.
  void rotateColumn(int direction);
  // FUNC+F1 / F2 on the spreadsheet: select rows up / down from the cursor in the
  // current column. Edits (and ERASE) then apply to every selected row: only the
  // fields that the edit changed are written to the other rows.
  int selectAnchor = -1;
  bool hasSelection() const {return selectAnchor>=0;}
  int selectionFirst() const {return hasSelection()?std::min(selectAnchor,row):row;}
  int selectionLast() const {return hasSelection()?std::max(selectAnchor,row):row;}
  bool rowSelected(int r) const {return hasSelection()&&r>=selectionFirst()&&r<=selectionLast();}
  Step editOrigin{};
  void applyToSelection();
  bool powerPrompt = false, powerYes = false, poweringOff = false;
  bool shift = false, shift2 = false, trackBanner = false;
  Page page = Page::Pattern;
  // Randomizer settings follow the active pattern, independently per track/column.
  DensitySettings density = [] {
    std::array<std::array<uint8_t,Columns>,Tracks> d{};
    for(auto &row:d)row.fill(45);
    return d;
  }();
  MusicalSettings sessionDefaults{};
  PatternOverrides patternOverrides{};
  std::array<PatternSlot,32> patterns{};
  int patternSlot=0,activePattern=-1,mixerPage=0,mixerTrack=0,mixerField=0;
  void applyMusicalSettings();
  bool savePattern(int);
  bool loadPattern(int);
  // The project loading this slot makes (musical settings applied, every track restarted).
  Project patternProject(int slot) const;
  // While playing: queue the slot; the audio swaps it in at the quantized bar, then the
  // editor adopts it (adoptQueuedPattern). Stopped: loads at once. False for an empty slot.
  bool queuePattern(int slot);
  void adoptQueuedPattern();
  void cancelQueuedPattern(){queuedPattern=-1;queuedProject.reset();queueSubmitted=false;}
  int queuedPattern=-1;
  std::shared_ptr<const Project> queuedProject;
  bool queueSubmitted=false;
  // Pattern generations order the audio's swaps against the editor's messages (runtime only).
  uint32_t patternGeneration=0,queuedGeneration=0,generationCounter=0;
  // SESSION settings: the die rolls the session scale and root note together.
  void rollScaleAndRoot();
  bool sessionEdit=false;   // SESSION settings: LEFT / RIGHT change the selected value
  int sessionDieFrom=1;     // the row LEFT returns to from the die
  int sessionSlot=0, activeSession=-1, insertField=0, insertModule=0;
  std::array<uint8_t,64> sessionState{}; // 0 empty,1 saved,2 damaged
  int columnField=0,macroModule=0,macroField=0;
  int randomizerField=1; // 0 MODE, 1 DENSITY (opens on DENSITY)
  int trackField = 0, sessionField = 0, masterModule = 0, masterParameter = 0;
  std::string notice;
  // A plain message: never carries a stale large value from a setting popup.
  void say(std::string text){notice=std::move(text);noticeDetail.clear();}
  uint64_t noticeSerial = 0;
  int noticeMs = 1200; // display time for the next notice; reset after use
  std::string noticeDetail; uint64_t noticeDetailSerial = 0; // large value line, shown only with its own notice
  bool effectBanner = false, volumeBanner = false;
  int volume = 60;
  uint64_t effectAnnouncement = 0;
  uint64_t revision = 0;
  uint64_t auditionSerial = 0; // UI event only; never stored in a session
  bool auditionRoot = false;   // the next audition plays the track instrument on the root note, octave 4
  Step pending{};
  std::string status = "READY - START TO EDIT";
  std::vector<UndoState> undoStack, redoStack;
  std::array<Step, MaxRows> clipboard{};
  bool copied = false;
  Project previewProject() const;
  Step previewStep() const;
  // Event tabs. NOTE: VALUE, LENGTH, CONDITION, then its locks (PAN, VOLUME, sends).
  // FX: DRY/WET MIX, LENGTH, RANDOM. INST / MOD: VALUE.
  // Every tab holds its value field(s) and then the parameter's own CONDITION: FX DRY/WET MIX
  // holds EFFECT and DRY/WET, MOD holds TYPE and SEMITONES, every other tab one VALUE. LEFT /
  // RIGHT select a field, UP / DOWN change it, START commits the step, A cancels it.
  bool fxColumn() const {return column==2||column==3;}
  // MOD: TYPE / SEMITONES, and for a RANDOM cell its TARGETS (three targets, then CONDITION;
  // the second and third are greyed out and skipped while the first is ANY).
  int eventTabCount() const {return column==0?2+NoteParameters:fxColumn()?3:column==4&&pending.modifier.type==ModifierType::Random?2:1;}
  int fieldCount() const {return targetsTab()?ModifierTargets+1:(fxColumn()&&eventTab==0)||column==4?3:2;}
  bool targetsTab() const {return column==4&&eventTab==1;}
  bool targetGreyed(int field) const {return targetsTab()&&field>0&&field<ModifierTargets&&pending.modifier.targets[0]==TargetAny;}
  // A field LEFT / RIGHT skip: the greyed targets, and a RANDOM cell's SEMITONES.
  bool fieldGreyed(int field) const {return targetGreyed(field)||(column==4&&eventTab==0&&field==1&&pending.modifier.type==ModifierType::Random);}
  bool randomTab() const {return fxColumn()&&eventTab==2;}
  bool lengthTab() const {return (column==0||fxColumn())&&eventTab==1;}
  bool conditionField() const {return eventField==fieldCount()-1;}
  int lockTab() const {return column==0&&eventTab>=2?eventTab-2:-1;}
  int eventField=0;          // the selected field: value field(s), then CONDITION
  void closeEdit();
  void action(Action);
  // Back (L2) returns to the previous page. History stays inside the current
  // appliance: entering or leaving one clears it, so Back never exits an appliance.
  struct Location { Page page; bool mixer; bool operator==(const Location&o)const{return page==o.page&&mixer==o.mixer;} };
  std::vector<Location> history;
  void dispatch(Action);
  void back();
  void checkpoint();
  void randomizeColumn();
  void undo(bool redo = false);
};
int columnLength(const Track &,int column);
int columnClock(const Track &,int column);
int columnDirection(const Track &,int column);
int trackDisplayLength(const Track &);
const char *columnName(int);
int insertParameterStep(FxType,int field,int module,const InsertSettings &);
int insertParameterCount(FxType,int module,const InsertSettings &);
const char *insertParameterName(FxType,int field,int module,const InsertSettings &);
uint8_t &insertParameter(Effect &,InsertSettings &,int field,int module);
bool steppedInsertParameter(FxType,int field,int module,const InsertSettings &);
std::string insertParameterDescription(FxType,int field,int module,const InsertSettings &,int value);
uint32_t randomNext(uint32_t &);
constexpr int TrackPatchBytes=Tracks*(2+Instruments*5);
const InstrumentPatch &trackPatch(const Project &,int track,int slot);
InstrumentPatch &editTrackPatch(Project &,int track,int slot);
Project blankProject();
Project demoProject();
// Capture modifier/page intent at press; a save hold never also loads on release.
struct SessionButton {
  bool down=false, fired=false;
  uint64_t pressedAt=0;
  Action tap=Action::Session, hold=Action::Save;
  void press(uint64_t now) { if(!down) {down=true;fired=false;pressedAt=now;} }
  void press(uint64_t now,const Editor &e) {
    if(down)return;
    bool pattern=e.page==Page::Patterns||e.page==Page::PatternSettings||e.page==Page::LifebloodSnapshots;
    bool session=e.page==Page::Sessions||e.page==Page::Session||e.page==Page::FxSessions||e.page==Page::FxSessionSettings||e.page==Page::LifebloodSessions;
    tap=e.shift2?Action::Mixer:e.shift?Action::Session:pattern?Action::LoadPattern:session?Action::LoadSession:Action::Patterns;
    hold=e.shift2||e.shift?Action::Reserved:pattern?Action::SavePattern:session?Action::SaveSession:Action::Reserved;
    press(now);
  }
  Action tick(uint64_t now) { if(down && !fired && now-pressedAt>=800) {fired=true;return hold;} return Action::Reserved; }
  Action release(uint64_t now) { if(!down)return Action::Reserved; auto a=tick(now); down=false; return a!=Action::Reserved?a:!fired?tap:Action::Reserved; }
};
// Tap opens options; holding executes exactly once, without an options tap on release.
struct RandomizerButton {
  bool down=false, fired=false;
  uint64_t pressedAt=0;
  void press(uint64_t now) {if(!down){down=true;fired=false;pressedAt=now;}}
  Action tick(uint64_t now) {
    if(down && !fired && now-pressedAt>=800){fired=true;return Action::RandomizeColumn;}
    return Action::Reserved;
  }
  Action release(uint64_t now) {
    if(!down)return Action::Reserved;
    auto a=tick(now);down=false;
    return a==Action::RandomizeColumn?a:!fired?Action::RandomizerOptions:Action::Reserved;
  }
};
// A long press opens the prompt. Its release never confirms it.
// Confirmation requires a separate short press/release.
struct PowerButton {
  bool down=false, fired=false;
  uint64_t pressedAt=0;
  void press(uint64_t now) {if(!down){down=true;fired=false;pressedAt=now;}}
  Action tick(uint64_t now) {
    if(down && !fired && now-pressedAt>=800){fired=true;return Action::PowerPrompt;}
    return Action::Reserved;
  }
  Action release(uint64_t now) {
    if(!down)return Action::Reserved;
    auto action=tick(now);down=false;
    return action==Action::PowerPrompt?action:!fired?Action::PowerConfirm:Action::Reserved;
  }
};
std::vector<uint8_t> encodeProject(const Project &);
bool decodeProject(Project &,const std::vector<uint8_t> &,std::string &);
bool saveProject(const Project &, const std::string &, std::string &);
bool loadProject(Project &, const std::string &, std::string &);
std::string noteName(int);
std::string fxText(Effect);
const char *fxName(FxType);
std::string conditionText(Condition);
std::string modifierText(Modifier);
constexpr int CycleConditions=36;
Condition cycleCondition(int index);
int cycleConditionIndex(Condition);
bool conditionMatches(Condition, uint64_t cycle);
Condition &eventCondition(Step &,int column);
Condition eventCondition(const Step &,int column);
// The condition of one parameter (step editor tab) of a cell.
Condition &parameterCondition(Step &,int column,int tab);
Condition parameterCondition(const Step &,int column,int tab);
// The effect an FX cell plays on its column's cycle: nothing if the cell's condition is not met,
// and LENGTH / RANDOM at their defaults where their own conditions are not met.
Effect playedEffect(const Step &,int slot,uint64_t cycle);
int triggeredNote(const Step &, uint64_t cycle, int transpose = 0);
// A RANDOM MOD cell applied to the note it plays with: draws its targets into `note`
// (already transposed and offset), `step.length` and `step.locks`.
void applyRandomModifier(const Modifier &, Step &step, int &note, const Project &, uint32_t &seed);
const char *instrumentName(int);
const char *patchFamily(const InstrumentPatch &);
const char *masterName(int);
int masterParameterCount(int module);
const char *masterParameterName(int module,int parameter);
// A master parameter's value as shown: a percentage, or ON/OFF, TRACK n.
std::string masterValueText(int module,int parameter,int value);
constexpr int Scales = 38;   // (new scales are appended: sessions store the number)
const char *scaleName(int);
int scaleNote(int note, const Project &);
uint16_t scaleMask(int scale);
int effectiveInstrument(const Track &, const Step &);
double clockRate(int);
const char *clockName(int);
struct TrackMix { float level, pan, delay, reverb;std::array<float,3> macro{}; };
TrackMix resolveTrackMix(const Track &, const Step &);
int noteParameterDefault(const Track &,int);
const char *noteParameterName(int);
std::array<MacroSettings,3> effectiveMacros(const Project &,int track);
const std::vector<FxType>& selectableEffects();
} // namespace amalthea
