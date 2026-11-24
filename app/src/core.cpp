#include "core.hpp"
#include "melody.hpp"
#include "fx_rack.hpp"
#include "walk.h"
#include "param_units.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
namespace amalthea {
bool trackAudible(const Project &p,int tr){
  if(p.tracks[tr].mute)return false;
  bool solo=false;for(const auto&t:p.tracks)solo|=t.solo;
  return !solo||p.tracks[tr].solo;
}
static void applyMusical(Project &project,const MusicalSettings &session,const PatternOverrides &o){
  int root=o.root==255?session.root:o.root;
  int delta=root-project.root;
  if(delta)for(auto&t:project.tracks)for(auto&s:t.steps)if(s.note>=0)s.note=std::clamp(int(s.note)+delta,0,119);
  project.bpm=o.bpm?o.bpm:session.bpm;
  project.root=root;
  project.scale=o.scale==255?session.scale:o.scale;
}
void Editor::applyMusicalSettings(){applyMusical(project,sessionDefaults,patternOverrides);}
const char *patternQuantizeName(int i){static const char *names[]={"LONGEST TRACK","SHORTEST TRACK"};return names[std::clamp(i,0,PatternQuantizeModes-1)];}
bool Editor::savePattern(int slot){
  if(slot<0||slot>=32)return false;
  patterns[slot]={std::make_shared<Project>(project),patternOverrides,density};activePattern=slot;dirty=true;return true;
}
Project Editor::patternProject(int slot)const{
  Project p=*patterns[slot].state;applyMusical(p,sessionDefaults,patterns[slot].overrides);
  for(int t=0;t<Tracks;t++)p.trackReset[t]=project.trackReset[t]+1;
  return p;
}
bool Editor::loadPattern(int slot){
  if(slot<0||slot>=32||!patterns[slot].state)return false;
  project=patternProject(slot);patternOverrides=patterns[slot].overrides;density=patterns[slot].density;
  cancelQueuedPattern();patternGeneration=++generationCounter;
  activePattern=slot;editing=false;undoStack.clear();redoStack.clear();row=std::min(row,columnLength(project.tracks[track],column)-1);dirty=true;return true;
}
bool Editor::queuePattern(int slot){
  if(slot<0||slot>=32||!patterns[slot].state)return false;
  if(!playing)return loadPattern(slot);
  queuedPattern=slot;queuedProject=std::make_shared<Project>(patternProject(slot));
  queuedGeneration=++generationCounter;queueSubmitted=false;return true;
}
// The audio swapped the queued pattern in at the bar: the editor takes the very project it
// plays (its restart counters included), so the next message changes nothing audible.
void Editor::adoptQueuedPattern(){
  if(queuedPattern<0||!queuedProject)return;
  int slot=queuedPattern;
  project=*queuedProject;patternOverrides=patterns[slot].overrides;density=patterns[slot].density;
  applyMusicalSettings();   // (session settings changed while it waited)
  patternGeneration=queuedGeneration;cancelQueuedPattern();
  activePattern=slot;editing=false;undoStack.clear();redoStack.clear();row=std::min(row,columnLength(project.tracks[track],column)-1);dirty=true;
}
void Editor::rollScaleAndRoot(){
  checkpoint();uint32_t seed=freshSeed()|1u;
  int scale=sessionDefaults.scale,root=sessionDefaults.root;
  // A different scale and root each roll; CHROMATIC (0) is no scale, so it is never drawn.
  while(scale==sessionDefaults.scale)scale=1+int(randomNext(seed)%uint32_t(Scales-1));
  while(root==sessionDefaults.root)root=int(randomNext(seed)%12u);
  sessionDefaults.scale=uint8_t(scale);sessionDefaults.root=uint8_t(root);applyMusicalSettings();dirty=true;
  notice=std::string(scaleName(scale));noticeDetail="ROOT "+noteName(60+root).substr(0,2);
  noticeMs=1400;noticeDetailSerial=++noticeSerial;
}
uint32_t randomNext(uint32_t &s) {
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
int columnLength(const Track&t,int c){return t.columns[c].length?t.columns[c].length:t.length;}
int columnClock(const Track&t,int c){return t.columns[c].clock==255?t.clock:t.columns[c].clock;}
int columnDirection(const Track&t,int c){return t.columns[c].direction==255?t.direction:t.columns[c].direction;}
int trackDisplayLength(const Track&t){int length=1;for(int c=0;c<Columns;c++)length=std::max(length,columnLength(t,c));return length;}
const char *columnName(int c){static const char *names[]={"NOTE","INSTRUMENT","FX 1","FX 2","MODIFIER"};return names[c];}
int effectiveInstrument(const Track &t,const Step &s) { return s.instrument==NoInstrument?t.instrument:s.instrument; }
double clockRate(int i) { static const double rates[]={.125,.25,.5,1,2,4};return rates[i]; }
const char *clockName(int i) { static const char *names[]={"1/8","1/4","1/2","1X","2X","4X"};return names[i]; }
int noteParameterDefault(const Track&t,int parameter){
 switch(parameter){case NotePan:return (t.pan+100)/2;case NoteVolume:return DefaultTrackLevel;case NoteDelay:return t.delaySend;case NoteReverb:return t.reverbSend;default:return t.macroSend[parameter-NoteAbrasion];}
}
const char *noteParameterName(int p){static const std::array<const char*,NoteParameters> names={"PAN","VOLUME","DELAY SEND","REVERB SEND","ABRASION SEND","CORROSION SEND","MODULATION SEND"};return names[p];}
Project Editor::previewProject() const {auto p=project;if(editing&&page==Page::Pattern){p.tracks[track].steps[row]=pending;if(pendingInsertSet&&(column==2||column==3))p.tracks[track].inserts[column-2]=pendingInsert;}return p;}
void Editor::randomizePendingEffect(){
  auto &t=project.tracks[track];int slot=column-2;
  std::vector<FxType> types;for(auto type:selectableEffects())if(type!=FxType::None&&device.insertEnabled(type))types.push_back(type);
  if(types.empty())return;
  Effect f{types[randomNext(eventRandomSeed)%types.size()],0,pending.fx[slot].length};
  InsertSettings cfg=pendingInsertSet?pendingInsert:t.inserts[slot];
  randomizeFxParameters(f,cfg,eventRandomSeed);
  // The slot's deeper parameters are shared by every row of this column that uses the
  // same effect: they are drawn only when no other row uses it, so nothing else changes.
  bool shared=false;for(int r=0;r<columnLength(t,column);r++)if(r!=row&&t.steps[r].fx[slot].type==f.type)shared=true;
  if(!shared){pendingInsert=cfg;pendingInsertSet=true;}
  pending.fx[slot]=f;eventRandomized=true;
  effectBanner=true;++effectAnnouncement;
  notice=std::string("RANDOM ")+fxName(f.type)+(shared?" - SHARED SETTINGS KEPT":"");++noticeSerial;
}
void Editor::randomizePendingParameters(){
  // Hold X while editing an FX row: new parameters for the effect it already has.
  // Its DRY/WET MIX and LENGTH stay; an effect without deeper parameters gets a new mix.
  auto &t=project.tracks[track];int slot=column-2;auto &f=pending.fx[slot];
  if(f.type==FxType::None){randomizePendingEffect();return;}   // nothing chosen yet: draw one
  if(f.type==FxType::Random){say("RANDOM DRAWS ITS OWN PARAMETERS");++noticeSerial;return;}
  InsertSettings cfg=pendingInsertSet?pendingInsert:t.inserts[slot];
  Effect drawn=f;randomizeFxParameters(drawn,cfg,eventRandomSeed);
  bool deep=insertParameterCount(f.type,0,cfg)>1;
  if(!deep)f.value=drawn.value;
  else {pendingInsert=cfg;pendingInsertSet=true;}
  int shared=0;for(int r=0;r<columnLength(t,column);r++)if(r!=row&&t.steps[r].fx[slot].type==f.type)shared++;
  eventRandomized=true;
  say(std::string(fxName(f.type))+(deep?" PARAMETERS RANDOMIZED":" MIX RANDOMIZED")+(deep&&shared?" - ALSO ON "+std::to_string(shared)+(shared==1?" ROW":" ROWS"):""));
  ++noticeSerial;requestAudition();
}
// Notes sound only from the NOTE column, and only for a row that holds a note
// (a blank row being edited holds the C4 it starts on).
int Editor::modifierBaseNote() const {
  const auto &t=project.tracks[track];
  if(pending.note>=0)return pending.note;
  int n=columnLength(t,0);
  for(int k=1;k<=n;k++){int r=((row-k)%n+n)%n;if(t.steps[r].note>=0)return t.steps[r].note;}
  return -1;
}
// The MOD column auditions the note its OFFSET produces, so a change can be heard in context.
void Editor::requestAudition(){auditionRoot=false;if((column==0&&pending.note>=0)||(column==4&&modifierBaseNote()>=0))++auditionSerial;}
Step Editor::previewStep() const {
  if(auditionRoot){Step s{};s.note=60+project.root%12;s.instrument=project.tracks[track].instrument;return s;}   // C4 = 60
  auto s=pending;if(column!=0&&s.note<0)s.note=60;if(column==4){int base=modifierBaseNote();s.note=base<0?-1:std::clamp(base+int(s.modifier.semitones),0,119);}return s;}
TrackMix resolveTrackMix(const Track &t,const Step &s) {
  TrackMix m{t.level/100.f,t.pan/100.f,t.delaySend/100.f,t.reverbSend/100.f};
  for(int i=0;i<3;i++)m.macro[i]=t.macroSend[i]*.01f;
  for(auto f:s.fx) {
    if(f.type==FxType::Volume)m.level=f.value/100.f*t.level/float(DefaultTrackLevel);
    if(f.type==FxType::Pan)m.pan=f.value/50.f-1;
    if(f.type==FxType::Delay)m.delay=f.value/100.f;
    if(f.type==FxType::Reverb)m.reverb=f.value/100.f;
    if(f.type>=FxType::Abrasion&&f.type<=FxType::Modulation)m.macro[int(f.type)-int(FxType::Abrasion)]=f.value*.01f;
  }
  if(s.locks[NotePan]!=Inherit)m.pan=s.locks[NotePan]/50.f-1;
  if(s.locks[NoteVolume]!=Inherit)m.level=s.locks[NoteVolume]*.01f*t.level/float(DefaultTrackLevel);
  if(s.locks[NoteDelay]!=Inherit)m.delay=s.locks[NoteDelay]*.01f;
  if(s.locks[NoteReverb]!=Inherit)m.reverb=s.locks[NoteReverb]*.01f;
  for(int i=0;i<3;i++)if(s.locks[NoteAbrasion+i]!=Inherit)m.macro[i]=s.locks[NoteAbrasion+i]*.01f;
  return m;
}
std::array<MacroSettings,3> effectiveMacros(const Project&p,int track){auto s=p.master.macros;for(int i=0;i<3;i++)if(p.tracks[track].macroOverride&(1<<i))s[i]=p.tracks[track].macros[i];return s;}
const std::vector<FxType>&selectableEffects(){static const std::vector<FxType> list={FxType::None,FxType::Wavefold,FxType::Fuzz,FxType::Rectify,FxType::Tremolo,FxType::Stutter,FxType::Clouds,FxType::AmpSim,FxType::Biztortion,FxType::Rotor,FxType::Space,FxType::Filter,FxType::QDelay,FxType::ChowMatrix,FxType::CrushClone,FxType::Frequalizer,FxType::Rings,FxType::Fracture2,FxType::CrazyIvan,FxType::Bitmurderer,FxType::LoFi,FxType::Flanger,FxType::Phaser,
  FxType::Oops,FxType::BufferOverride,FxType::Livecut,FxType::SupaTrigga,FxType::BeatRepeat,FxType::TimeShaper,FxType::Jumblr,FxType::Scrubby,FxType::Glitchlets,FxType::Skidder,
  FxType::Choppr,FxType::GateShaper,FxType::TremoSquare,FxType::Beads,FxType::Softcut,FxType::Transverb,FxType::GlitchShifter,FxType::PitchNasty,FxType::Amalgam,FxType::Geometer,FxType::MultiBandDistortion,FxType::Disintegrate,FxType::BitGlitter,FxType::Pafnuty,
  FxType::Cyanide,FxType::Polarizer,FxType::Angr,FxType::Warps,FxType::FreqShifter,FxType::BrokenFft,FxType::Treemonster,FxType::RingMod,FxType::Combulator,FxType::Resonator,FxType::Vocoder,FxType::Smr,FxType::RezSynth,FxType::TalkBox,FxType::MdaVocoder,FxType::RePsycho,
  FxType::Random};return list;}
const InstrumentPatch &trackPatch(const Project&p,int track,int slot){return p.localPatchMask[track]&(1u<<slot)?p.trackPatches[track][slot]:p.patches[slot];}
InstrumentPatch &editTrackPatch(Project&p,int track,int slot){if(!(p.localPatchMask[track]&(1u<<slot))){p.trackPatches[track][slot]=p.patches[slot];p.localPatchMask[track]|=1u<<slot;}return p.trackPatches[track][slot];}
const char *instrumentName(int i) {
  static const char *n[] = {"VAP", "PERC", "CHIP", "FIBER", "HIVE", "OP4", "ANIMA", "RUSSOLO", "BAY AREA", "RESURGENT", "CALIGULA", "FM DRUM"};
  return n[std::clamp(i, 0, SynthEngines-1)];
}
std::string noteName(int n) {
  if (n == -3)
    return "TIE";
  if (n == -1)
    return "---";
  if (n == -2)
    return "OFF";
  static const char *ns[] = {"C-", "C#", "D-", "D#", "E-", "F-",
                             "F#", "G-", "G#", "A-", "A#", "B-"};
  return std::string(ns[n % 12]) + std::to_string(n / 12 - 1);
}
const char *fxName(FxType type) {
  if(auto*n=nativeInfo(type))return n->name;
  static const char *names[] = {
      "NONE",      "REVERB SEND", "DELAY SEND",  "DISTORTION", "SATURATION",
      "OVERDRIVE", "BITCRUSH",    "RATE REDUCE", "WAVEFOLD",   "FUZZ",
      "RECTIFY",   "DIODE",       "RAT DRIVE",   "LOWPASS",    "HIGHPASS",
      "TREMOLO",   "STUTTER",     "PAN",         "LEVEL", "CLOUDS GRANULAR", "AMP SIMULATOR", "BIZTORTION", "ROTOR", "CLOUDSEED SPACE", "ABRASION SEND", "CORROSION SEND", "MODULATION SEND", "MULTIMODE FILTER", "QDELAY", "CHOWMATRIX", "CRUSHCLONE", "FREQUALIZER", "RINGS", "FRACTURE 2", "CRAZY IVAN", "BITMURDERER", "LO-FI", "RANDOM", "FLANGER", "PHASER"};
  return names[std::clamp(int(type), 0, int(FxType::Count) - 1)];
}
std::string fxText(Effect e) {
  if (e.type == FxType::None)
    return "-----";
  static const char *codes[] = {"--", "RV", "DL", "DS", "SA", "OD", "BC",
                                "SR", "WF", "FZ", "RX", "DI", "RT", "LP",
                                "HP", "TM", "ST", "PN", "VL", "CL", "AM", "BZ", "RO", "CS", "AB", "CR", "MD", "MF", "QD", "CM", "CC", "FQ", "RG", "FR", "CI", "BM", "LF", "RN", "FL", "PH"};
  char b[16];
  auto*native=nativeInfo(e.type);
  snprintf(b, sizeof b, "%s%03d", native?native->code:codes[std::min<int>(int(e.type),int(std::size(codes))-1)], e.value);
  return b;
}
std::string conditionText(Condition c) {
  char b[16];
  snprintf(b, sizeof b, "%02d:%02d", c.occurrence, c.every);
  return b;
}
std::string modifierText(Modifier m) {
  char b[16];
  snprintf(b, sizeof b, "%+03d", m.semitones);
  return b;
}
bool conditionMatches(Condition c, uint64_t cycle) {
  return c.every >= 1 && c.occurrence >= 1 && c.occurrence <= c.every &&
         cycle % c.every == c.occurrence - 1;
}
Condition cycleCondition(int index){
  index=std::clamp(index,0,CycleConditions-1);
  for(int every=1;every<=8;every++)for(int occurrence=1;occurrence<=every;occurrence++)
    if(index--==0)return {uint8_t(occurrence),uint8_t(every)};
  return {};
}
int cycleConditionIndex(Condition c){
  for(int i=0;i<CycleConditions;i++){auto candidate=cycleCondition(i);if(candidate.every==c.every&&candidate.occurrence==c.occurrence)return i;}
  return 0; // Older saved periods remain playable until explicitly edited.
}
Condition &eventCondition(Step&s,int column){return column?s.eventConditions[column-1]:s.condition;}
Condition eventCondition(const Step&s,int column){return column?s.eventConditions[column-1]:s.condition;}
int triggeredNote(const Step&s,uint64_t cycle){
  if(s.note<0)return s.note;
  if(!conditionMatches(s.condition,cycle))return -1;
  return std::clamp(int(s.note)+(conditionMatches(eventCondition(s,4),cycle)?s.modifier.semitones:0),0,119);
}
const char *masterName(int i) {
  static const char *names[] = {"ABRASION", "LEGACY", "LEGACY", "CORROSION", "LEGACY", "MODULATION", "LEGACY", "COMPRESSOR", "DELAY", "REVERB", "OUTPUT LEVEL"};
  return names[std::clamp(i,0,MasterModules-1)];
}
// Scales after Tonitor (M-VAVE FM-1) and Bigorna, plus further modes and world scales.
// The first seven keep their numbers (stored in sessions); new ones are appended.
namespace {
constexpr uint16_t degrees(std::initializer_list<int> d){uint16_t m=0;for(int v:d)m=uint16_t(m|(1<<v));return m;}
struct ScaleDef { const char *name; uint16_t mask; };
constexpr ScaleDef ScaleTable[Scales] = {
  {"CHROMATIC",degrees({0,1,2,3,4,5,6,7,8,9,10,11})},
  {"DORIAN",degrees({0,2,3,5,7,9,10})},
  {"MINOR",degrees({0,2,3,5,7,8,10})},
  {"MAJOR",degrees({0,2,4,5,7,9,11})},
  {"PHRYGIAN",degrees({0,1,3,5,7,8,10})},
  {"MINOR PENTATONIC",degrees({0,3,5,7,10})},
  {"HARMONIC MINOR",degrees({0,2,3,5,7,8,11})},
  {"MELODIC MINOR",degrees({0,2,3,5,7,9,11})},
  {"LYDIAN",degrees({0,2,4,6,7,9,11})},
  {"MIXOLYDIAN",degrees({0,2,4,5,7,9,10})},
  {"LOCRIAN",degrees({0,1,3,5,6,8,10})},
  {"MAJOR PENTATONIC",degrees({0,2,4,7,9})},
  {"BLUES",degrees({0,3,5,6,7,10})},
  {"MAJOR BLUES",degrees({0,2,3,4,7,9})},
  {"WHOLE TONE",degrees({0,2,4,6,8,10})},
  {"HUNGARIAN MINOR",degrees({0,2,3,6,7,8,11})},
  {"HUNGARIAN MAJOR",degrees({0,3,4,6,7,9,10})},
  {"DOUBLE HARMONIC",degrees({0,1,4,5,7,8,11})},
  {"PHRYGIAN DOMINANT",degrees({0,1,4,5,7,8,10})},
  {"LYDIAN DOMINANT",degrees({0,2,4,6,7,9,10})},
  {"ALTERED",degrees({0,1,3,4,6,8,10})},
  {"NEAPOLITAN MINOR",degrees({0,1,3,5,7,8,11})},
  {"NEAPOLITAN MAJOR",degrees({0,1,3,5,7,9,11})},
  {"PERSIAN",degrees({0,1,4,5,6,8,11})},
  {"ENIGMATIC",degrees({0,1,4,6,8,10,11})},
  {"PROMETHEUS",degrees({0,2,4,6,9,10})},
  {"AUGMENTED",degrees({0,3,4,7,8,11})},
  {"OCTATONIC H-W",degrees({0,1,3,4,6,7,9,10})},
  {"OCTATONIC W-H",degrees({0,2,3,5,6,8,9,11})},
  {"BEBOP DOMINANT",degrees({0,2,4,5,7,9,10,11})},
  {"JAPANESE IN",degrees({0,1,5,7,8})},
  {"HIRAJOSHI",degrees({0,2,3,7,8})},
  {"IWATO",degrees({0,1,5,6,10})},
  {"KUMOI",degrees({0,2,3,7,9})},
  {"YO",degrees({0,2,5,7,9})},
  {"RYUKYU",degrees({0,4,5,7,11})},
  {"PELOG",degrees({0,1,3,7,8})},
  {"EGYPTIAN",degrees({0,2,5,7,10})},
};
static_assert(ScaleTable[0].mask==0xfff&&ScaleTable[1].mask==0x6ad&&ScaleTable[2].mask==0x5ad&&ScaleTable[3].mask==0xab5&&
  ScaleTable[4].mask==0x5ab&&ScaleTable[5].mask==0x4a9&&ScaleTable[6].mask==0x9ad,"stored scale numbers keep their notes");
}
const char *scaleName(int i) {return ScaleTable[std::clamp(i,0,Scales-1)].name;}
uint16_t scaleMask(int scale) {
  return ScaleTable[std::clamp(scale,0,Scales-1)].mask;
}
int scaleNote(int note, const Project &p) {
  int relative = note - p.root;
  for (int d=0;d<12;d++) if (scaleMask(p.scale) & (1 << ((relative-d+120)%12)))
    return std::clamp(note-d,0,119);
  return note;
}
extern "C" const char *amalthea_family(int, uint32_t);
const char *patchFamily(const InstrumentPatch &p) { return amalthea_family(p.engine, p.seed); }
int masterParameterCount(int module){if(macroIndex(module)>=0)return MacroParameters;return module==9?9:module==7?7:module>=7?5:MasterParameters;}
// Master values in the units the bus DSP derives from them (effects.cpp, tal_reverb.cpp).
std::string masterValueText(int module,int parameter,int value){
  using namespace units;float u=value*.01f;
  if(macroIndex(module)>=0)return macroValueText(macroIndex(module),parameter,value);
  if(module==7)switch(parameter){
    case 0:return decibels(20*std::log10(.02f+.93f*u))+"FS";case 1:return ratio(1+19*u);
    case 2:return seconds(.0001*std::pow(1000.,u));case 3:return seconds(.01*std::pow(200.,u));
    case 4:return wet(value);case 5:return value?"ON":"OFF";default:return "TRACK "+std::to_string(value+1);}
  if(module==8)switch(parameter){
    case 1:return seconds(std::clamp(44100*.02*std::pow(100.,u),256.,88200.)/44100);
    case 2:return gain(.009*value);case 3:return hz(200*std::pow(90.,u));default:return gain(u);}
  if(module==9)switch(parameter){
    case 1:{ // TAL comb RT60 from its feedback and length at the mean comb delay; damping shortens it.
      auto inverted=[](double x){return 1-(std::pow(20.,1-x)-1)/19;};
      double d=inverted(inverted(u))*.9+.1;
      double g=.99*d,t=.972*d*.0786;return g<=0?"0 s":"~"+seconds(-3*t/std::log10(g));}
    case 2:case 3:return decibels(-18+.18*value);
    case 4:return seconds((std::pow(20.,u)-1)/19);
    case 5:return value==0?"MONO":"SIDE "+gain(u);
    case 6:case 7:return hz(100+9900*(std::pow(20.,u)-1)/19);
    default:return gain(u);}
  if(module==10)switch(parameter){
    case 0:return gain(2.8*std::pow(u,1.5));case 1:return pan(value);
    case 2:return gain(.1+.85*u)+"FS";case 3:return seconds(.01*std::pow(100.,u));default:return wet(value);}
  return plain(value);
}
const char *masterParameterName(int module,int parameter) {
  if(macroIndex(module)>=0)return macroParameterName(macroIndex(module),parameter);
  if(module>=7&&parameter==(module==9?8:4))return "MIX";
  if(module==7&&parameter==5)return "SIDECHAIN";
  if(module==7&&parameter==6)return "SOURCE";
  if(module==9&&parameter>=4){static const char *extra[]={"PREDELAY","WIDTH","LOW FREQ","HIGH FREQ"};return extra[parameter-4];}
  static const char *names[MasterModules][MasterParameters]={
    {"DRIVE","WARMTH","TONE","MIX"},
    {"DRIVE","BIAS","TONE","MIX"},
    {"DRIVE","THRESHOLD","TONE","MIX"},
    {"REDUCTION","DITHER","TONE","MIX"},
    {"RATE REDUCE","JITTER","SLEW","MIX"},
    {"DEPTH","RATE","FEEDBACK","MIX"},
    {"DEPTH","RATE","FEEDBACK","MIX"},
    {"THRESHOLD","RATIO","ATTACK","RELEASE"},
    {"SEND","TIME","FEEDBACK","TONE"},
    {"RETURN","DECAY","LOW TONE","HIGH TONE"},
    {"LEVEL","BALANCE","CEILING","RELEASE"}
  };
  return names[module][parameter];
}
Project blankProject() {
  Project p;
  for(int t=0;t<Tracks;t++) p.tracks[t].instrument=t%Instruments;
  return p;
}
Project demoProject() {
  Project p;
  for (int t = 0; t < Tracks; t++) {
    for (int r = 0; r < PatternRows; r++) {
      auto &s = p.tracks[t].steps[r];
      s.instrument = t % Instruments;
      if (r % 4 == 0) {
        s.note = 48 + (t % 3) * 12 + (r / 4 % 4) * 2;
        if (t % 8 == 6)
          s.note = 36 + (r / 4 % 4) * 2;
      }
    }
    p.tracks[t].mute = false;
  }
  return p;
}
void Editor::checkpoint() {
  revision++;
  undoStack.push_back({project,sessionDefaults,patternOverrides,density});
  if (undoStack.size() > 64)
    undoStack.erase(undoStack.begin());
  redoStack.clear();
  dirty = true;
}
void Editor::undo(bool redo) {
  editing = false;
  auto &from = redo ? redoStack : undoStack;
  auto &to = redo ? undoStack : redoStack;
  if (from.empty()) {
    status = "NOTHING TO UNDO";
    return;
  }
  to.push_back({project,sessionDefaults,patternOverrides,density});
  revision++;
  project = from.back().project;
  sessionDefaults=from.back().sessionDefaults;patternOverrides=from.back().overrides;density=from.back().density;
  from.pop_back();
  row = std::min(row, columnLength(project.tracks[track],column) - 1);
  dirty = true;
  status = redo ? "REDONE" : "UNDONE";
}
uint32_t Editor::freshSeed() {
  uint32_t s=randomNext(project.seed);
  if(entropy){randomNext(entropy);s^=entropy*0x9e3779b9u;}
  return s?s:0x2545f491u;
}
void Editor::randomizeBlankPatches() {
  // A blank session opens with every track's instrument already drawn.
  for(int t=0;t<Tracks;t++)editTrackPatch(project,t,project.tracks[t].instrument).seed=freshSeed();
}
void Editor::randomizeColumn() {
  checkpoint();
  auto &t = project.tracks[track];
  int length=columnLength(t,column);
  if (column == 1 && !shift) {
    // A new sound for the track's own instrument (TRACK SETTINGS), written to every row of
    // the column, so the column always follows the instrument chosen there.
    auto slot = int(t.instrument);
    auto &patch = editTrackPatch(project,track,slot);
    patch.seed = freshSeed();
    for(int r=0;r<length;r++)t.steps[r].instrument=uint8_t(slot);
    status = std::string(instrumentName(patch.engine)) + " / " + patchFamily(patch) + " - UNDO AVAILABLE";
    notice="INSTRUMENT "+std::to_string(slot+1)+" RANDOMIZED";
    noticeDetail=std::string(instrumentName(patch.engine))+" / "+patchFamily(patch);
    noticeMs=1600;noticeDetailSerial=++noticeSerial;
    return;
  }
  // An exact, seeded number of populated rows (rounded to the nearest step).
  // Prefer the motif's original onsets before filling gaps, retaining its pitch contour.
  const bool euclid=euclideanMode(density[track][column]);
  int count=(length*densityPercent(density[track][column])+50)/100;
  int16_t notes[PatternRows];
  const char *idiom="";
  if(column==0){
    MelodyRequest q;q.seed=freshSeed();q.length=length;q.count=count;q.root=project.root;q.scaleMask=scaleMask(project.scale);
    q.kit=trackPatch(project,track,t.instrument).engine==1;   // PERC: a drum pattern on the kit's note map
    q.hits=trackPatch(project,track,t.instrument).engine==FmDrumEngine;   // FM DRUM: one voice's rhythm
    q.euclid=euclid;
    idiom=generateMelody(q,notes);
  }
  std::array<int,PatternRows> order{};
  for(int r=0;r<length;r++)order[r]=r;
  for(int i=length-1;i>0;i--)std::swap(order[i],order[randomNext(project.seed)%(i+1)]);
  std::array<bool,PatternRows> active{};
  if(euclid){bool on[PatternRows]{};euclideanRows(length,count,int(randomNext(project.seed)%std::max(1,length)),on);for(int r=0;r<length;r++)active[r]=on[r];}
  else for(int i=0;i<count;i++)active[order[i]]=true;
  if(column==0)for(int r=0;r<length;r++)active[r]=notes[r]>=0;
  for(int r=0;r<length;r++) {
    auto &s=t.steps[r];
    if(column==0) {
      s.note=notes[r];
      s.instrument=t.instrument;
      if(shift){
        s.condition=active[r]?cycleCondition(randomNext(project.seed)%CycleConditions):Condition{};
        for(int p=0;p<NoteParameters;p++)s.locks[p]=!active[r]?Inherit:p==NoteVolume?10+randomNext(project.seed)%91:randomNext(project.seed)%101;
      }
    } else if(column==1) {
      s.instrument=active[r]?randomNext(project.seed)%Instruments:NoInstrument;
    } else if(column<4) {
      auto &f=s.fx[column-2];f={};
      if(active[r]) {
        std::vector<FxType> choices;for(auto type:selectableEffects())if(type!=FxType::None&&device.insertEnabled(type))choices.push_back(type);
        if(!choices.empty()){f.type=choices[randomNext(project.seed)%choices.size()];f.value=defaultFxMix(f.type)==100?70+randomNext(project.seed)%31:25+randomNext(project.seed)%66;}
      }
      if(shift)eventCondition(s,column)=active[r]?cycleCondition(randomNext(project.seed)%CycleConditions):Condition{};
    } else {
      s.modifier={};
      if(active[r]) {
        static const int8_t shifts[]={-12,-7,-5,-3,-2,-1,1,2,3,5,7,12};
        s.modifier.semitones=shifts[randomNext(project.seed)%12];
      }
    }
  }
  // Every effect placed gets freshly drawn parameters (its row keeps the drawn mix). They
  // belong to the FX column, so rows sharing an effect share one draw. RANDOM draws its own.
  int drawn=0;
  if(column==2||column==3){
    std::vector<FxType> placed;
    for(int r=0;r<length;r++){auto type=t.steps[r].fx[column-2].type;if(type!=FxType::None&&type!=FxType::Random&&std::find(placed.begin(),placed.end(),type)==placed.end())placed.push_back(type);}
    for(auto type:placed){Effect e{type,0};randomizeFxParameters(e,t.inserts[column-2],project.seed);}
    drawn=int(placed.size());
  }
  if(column==0)for(int r=0;r<columnLength(t,1);r++)t.steps[r].instrument=t.instrument;
  if(column==1){
    // Shift draws which instruments play; their sounds stay as they are.
    std::array<bool,Instruments> used{};for(int r=0;r<length;r++)if(t.steps[r].instrument<Instruments)used[t.steps[r].instrument]=true;
    int n=0,rows=0;for(int i=0;i<Instruments;i++)n+=used[i];for(int r=0;r<length;r++)rows+=t.steps[r].instrument<Instruments;
    notice="INSTRUMENT COLUMN RANDOMIZED";
    noticeDetail=std::to_string(n)+(n==1?" INSTRUMENT ON ":" INSTRUMENTS ON ")+std::to_string(rows)+(rows==1?" ROW":" ROWS");
    noticeMs=1600;noticeDetailSerial=++noticeSerial;
  }
  const std::string pulse=euclid?"E("+std::to_string(count)+","+std::to_string(length)+") ":"";
  status = column == 0 ? pulse + idiom + " GENERATED - UNDO AVAILABLE"
                       : drawn ? pulse + "FX AND " + std::to_string(drawn) + (drawn==1?" PARAMETER SET":" PARAMETER SETS") + " RANDOMIZED - UNDO AVAILABLE"
                       : pulse + "SELECTED COLUMN RANDOMIZED - UNDO AVAILABLE";
}
void Editor::rotateColumn(int direction){
  // Only the column under the cursor turns, wrapping within its own length.
  auto &t=project.tracks[track];
  auto rotated=t.steps;int c=column,length=columnLength(t,c);
  for(int r=0;r<length;r++){
    const Step &from=t.steps[r];Step &to=rotated[((r+direction)%length+length)%length];
    if(c==0){to.note=from.note;to.length=from.length;to.locks=from.locks;}
    else if(c==1)to.instrument=from.instrument;
    else if(c<4)to.fx[c-2]=from.fx[c-2];
    else to.modifier=from.modifier;
    eventCondition(to,c)=eventCondition(from,c);
  }
  if(std::memcmp(rotated.data(),t.steps.data(),sizeof(Step)*t.steps.size())==0){status="COLUMN UNCHANGED";return;}
  checkpoint();t.steps=rotated;
  status=direction>0?"COLUMN ROTATED FORWARD":"COLUMN ROTATED BACKWARD";
}
void Editor::eraseCell(){
  auto &t=project.tracks[track];auto before=t.steps;
  for(int r=selectionFirst();r<=selectionLast();r++){
    Step &s=t.steps[r];
    if(column==0){s.note=-1;s.length=1;s.locks.fill(Inherit);}
    else if(column==1)s.instrument=NoInstrument;
    else if(column<4)s.fx[column-2]={};
    else s.modifier={};
    eventCondition(s,column)={};
  }
  if(std::memcmp(before.data(),t.steps.data(),sizeof(Step)*t.steps.size())==0){status="ERASE MODE - CELL ALREADY EMPTY";return;}
  auto after=t.steps;t.steps=before;checkpoint();t.steps=after;
  bool many=hasSelection();selectAnchor=-1;
  status=many?"ROWS ERASED - UNDO AVAILABLE":"CELL ERASED - UNDO AVAILABLE";
}
void Editor::applyToSelection(){
  // Write only what this edit changed (against the row as it opened) to every selected row.
  auto &t=project.tracks[track];const Step &o=editOrigin,&p=pending;
  for(int r=selectionFirst();r<=selectionLast();r++){
    if(r==row)continue;Step &s=t.steps[r];
    if(column==0){
      if(p.note!=o.note)s.note=p.note;
      if(p.length!=o.length)s.length=p.length;
      for(int k=0;k<NoteParameters;k++)if(p.locks[k]!=o.locks[k])s.locks[k]=p.locks[k];
    }else if(column==1){if(p.instrument!=o.instrument)s.instrument=p.instrument;}
    else if(column<4){
      int k=column-2;
      if(p.fx[k].type!=o.fx[k].type)s.fx[k].type=p.fx[k].type;
      if(p.fx[k].value!=o.fx[k].value||(p.fx[k].type!=o.fx[k].type&&s.fx[k].value==0))s.fx[k].value=p.fx[k].value;
      if(p.fx[k].length!=o.fx[k].length)s.fx[k].length=p.fx[k].length;
    }else{
      if(p.modifier.semitones!=o.modifier.semitones||p.modifier.conditionalOnly!=o.modifier.conditionalOnly)s.modifier=p.modifier;
    }
    auto pc=eventCondition(p,column),oc=eventCondition(o,column);
    if(pc.occurrence!=oc.occurrence||pc.every!=oc.every)eventCondition(s,column)=pc;
  }
}
void Editor::leaveAppliance(){
  auto preferences=device;int output=volume,rows=visibleRows;
  auto resets=project.trackReset;auto serial=revision;
  auto noise=entropy;
  *this=Editor{};project=blankProject();device=preferences;volume=output;visibleRows=rows;
  entropy=noise;if(entropy){project.seed^=randomNext(entropy);if(!project.seed)project.seed=1;randomizeBlankPatches();}
  for(int t=0;t<Tracks;t++)project.trackReset[t]=resets[t]+1;
  revision=serial+1;page=Page::Appliances;
}
void Editor::randomizeEventParameter(bool wholeColumn){
  auto &t=project.tracks[track];
  if(wholeColumn)checkpoint();
  uint32_t &seed=wholeColumn?project.seed:eventRandomSeed;
  auto apply=[&](Step &s,bool emptyNote){
    auto next=[&](){return randomNext(seed);};
    if(conditionTab())eventCondition(s,column)=cycleCondition(next()%CycleConditions);
    else if(lengthTab()){static const uint8_t lengths[]={1,1,1,2,2,2,3,3,4,4,6,8,12,16};auto n=lengths[next()%14];if(column==0)s.length=n;else s.fx[column-2].length=n;}
    else if(lockTab()>=0){int parameter=lockTab();s.locks[parameter]=parameter==NoteVolume?10+next()%91:next()%101;}
    else if(column==0){if(s.note>=0||emptyNote){int center=s.note>=0?s.note:60;std::array<int,25> choices{};int count=0;
      for(int n=std::max(0,center-12);n<=std::min(119,center+12);n++)if(scaleNote(60+n%12,project)==60+n%12)choices[count++]=n;
      if(count)s.note=choices[next()%count];}}
    else if(column==1)s.instrument=next()%Instruments;
    else if(column<4){auto &f=s.fx[column-2];if(f.type!=FxType::None)f.value=next()%101;}
    else s.modifier.semitones=int(next()%49)-24;
  };
  auto copy=[&](const Step &from,Step &to){
    if(conditionTab())eventCondition(to,column)=eventCondition(from,column);
    else if(lengthTab()){if(column==0)to.length=from.length;else to.fx[column-2].length=from.fx[column-2].length;}
    else if(lockTab()>=0)to.locks[lockTab()]=from.locks[lockTab()];
    else if(column==0)to.note=from.note;
    else if(column==1)to.instrument=from.instrument;
    else if(column<4)to.fx[column-2].value=from.fx[column-2].value;
    else to.modifier.semitones=from.modifier.semitones;
  };
  if(wholeColumn){
    for(int r=0;r<columnLength(t,column);r++)apply(t.steps[r],false);
    copy(t.steps[row],pending);eventRandomSeed=project.seed;
    notice="COLUMN PARAMETER RANDOMIZED";++noticeSerial;
  }else {apply(pending,true);eventRandomized=true;}
  requestAudition();
}
bool Editor::focusedSetting(std::string &name,std::string &value) const {
  const auto &t=project.tracks[track];auto pct=[](int v){return std::to_string(v)+"%";};
  if(page==Page::Track){
    static const char *names[]={"LENGTH","CLOCK","DIRECTION","INSTRUMENT","VOLUME","PAN","ABRASION SEND","CORROSION SEND","MODULATION SEND","DELAY SEND","REVERB SEND","ENGINE"};
    if(trackField<0||trackField>=12)return false;
    name=names[trackField];
    switch(trackField){
      case 0:value=std::to_string(t.length)+" STEPS";break;
      case 1:value=clockName(t.clock);break;
      case 2:value=amalthea_direction_name(t.direction);break;
      case 3:value=std::to_string(t.instrument+1)+" "+instrumentName(trackPatch(project,track,t.instrument).engine);break;
      case 4:value=pct(t.level);break;
      case 5:value=units::pan((int(t.pan)+100)/2);break;
      case 6:case 7:case 8:value=pct(t.macroSend[trackField-6]);break;
      case 9:value=pct(t.delaySend);break;
      case 10:value=pct(t.reverbSend);break;
      default:value=instrumentName(trackPatch(project,track,t.instrument).engine);break;
    }
    return true;
  }
  if(page==Page::Column){
    const auto &cfg=t.columns[column];
    static const char *names[]={"LENGTH","CLOCK","DIRECTION"};
    if(columnField<0||columnField>2)return false;
    name=std::string(columnName(column))+" "+names[columnField];
    value=columnField==0?std::string(cfg.length?"":"TRACK / ")+std::to_string(columnLength(t,column))+" STEPS":
          columnField==1?std::string(cfg.clock==255?"TRACK / ":"")+clockName(columnClock(t,column)):
          std::string(cfg.direction==255?"TRACK / ":"")+amalthea_direction_name(columnDirection(t,column));
    return true;
  }
  if(page==Page::Master){
    if(masterParameter>=masterParameterCount(masterModule))return false;
    name=std::string(masterName(masterModule))+" "+masterParameterName(masterModule,masterParameter);
    value=masterValueText(masterModule,masterParameter,project.master.value(masterModule,masterParameter));
    return true;
  }
  if(page==Page::Macro){
    bool local=t.macroOverride&(1<<macroModule);auto profile=effectiveMacros(project,track)[macroModule];
    name=macroField?macroParameterName(macroModule,macroField-1):"SOURCE";
    value=macroField?macroValueText(macroModule,macroField-1,profile.value[macroField-1]):local?"TRACK":"MASTER";
    return true;
  }
  if(page==Page::Session||page==Page::PatternSettings){
    static const char *names[]={"TEMPO","SCALE","ROOT NOTE","SWITCH QUANTIZE"};
    bool session=page==Page::Session;const auto &d=sessionDefaults;
    if(sessionField<0||sessionField>(session?3:2))return false;
    if(sessionField==3){name=names[3];value=patternQuantizeName(d.quantize);return true;}
    auto o=(patternSlot==activePattern||(activePattern<0&&patternSlot==0))?patternOverrides:patterns[patternSlot].overrides;
    name=names[sessionField];
    if(sessionField==0)value=session?std::to_string(d.bpm)+" BPM":o.bpm?std::to_string(o.bpm)+" BPM":"SESSION / "+std::to_string(d.bpm);
    else if(sessionField==1)value=session?scaleName(d.scale):o.scale==255?std::string("SESSION / ")+scaleName(d.scale):scaleName(o.scale);
    else value=session?noteName(60+d.root).substr(0,2):o.root==255?"SESSION / "+noteName(60+d.root).substr(0,2):noteName(60+o.root).substr(0,2);
    return true;
  }
  return false;
}
void Editor::action(Action a) {
  if(a==Action::Back){back();return;}
  Location before{page,mixer};bool wasFx=inFx(),wasLifeblood=inLifeblood();int trackBefore=track,columnBefore=column;
  std::string settingName,settingValue;bool setting=focusedSetting(settingName,settingValue);
  dispatch(a);
  if(page!=Page::Session)sessionEdit=false;
  // Stopping while a pattern waits for its bar: it loads at once.
  if(!playing&&queuedPattern>=0)loadPattern(queuedPattern);
  if(erasing&&(page!=Page::Pattern||mixer||editing))erasing=false;
  if(hasSelection()&&(page!=Page::Pattern||mixer||track!=trackBefore||column!=columnBefore||a==Action::Up||a==Action::Down)&&!editing&&!shift)selectAnchor=-1;
  // A value dialled on a settings page without its own slider: a large popup of it.
  if(setting&&page==before.page&&!mixer){
    std::string name,value;
    if(focusedSetting(name,value)&&name==settingName&&value!=settingValue){
      notice=name;noticeDetail=value;noticeMs=1000;noticeDetailSerial=++noticeSerial;
    }
  }
  Location after{page,mixer};
  if(after==before)return;
  if(before.page==Page::Appliances||after.page==Page::Appliances||wasFx!=inFx()||wasLifeblood!=inLifeblood()||splash){history.clear();return;}
  history.push_back(before);if(history.size()>32)history.erase(history.begin());
}
void Editor::back() {
  if(powerPrompt||poweringOff||exitStage!=ExitStage::None||page==Page::Appliances||splash)return;
  // Leave an in-place edit mode first; otherwise step back one page.
  if(inFx()&&fxLevelEdit){fxLevelEdit=false;return;}
  if(history.empty())return;
  auto to=history.back();history.pop_back();
  page=to.page;mixer=to.mixer;editing=fxLevelEdit=false;
  trackBanner=effectBanner=volumeBanner=false;notice.clear();
}
void Editor::dispatch(Action a) {
  if(poweringOff)return;
  // The appliance shell captures navigation before tracker shortcuts.
  if(page==Page::Appliances){
    if(a==Action::PowerPrompt||a==Action::PowerConfirm||powerPrompt){}
    else {
      if(a==Action::ShiftUp)shift=false;if(a==Action::Shift2Up)shift2=false;

      // Three appliances: TRACKER, FX, LIFEBLOOD.
      if(a==Action::Left||a==Action::Up)applianceItem=(applianceItem+2)%3;
      if(a==Action::Right||a==Action::Down)applianceItem=(applianceItem+1)%3;
      if(a==Action::Enter){
        if(applianceItem==2)openLifeblood();
        else {page=applianceItem?Page::FxAppliance:Page::Pattern;if(applianceItem&&!fxRack)fxRack=std::make_shared<FxRack>();}
        splash=false;}
      return;
    }
  }
  if(exitStage!=ExitStage::None){
    if(a==Action::ShiftUp)shift=false;if(a==Action::Shift2Up)shift2=false;
    if(exitStage==ExitStage::Saving)return;
    if(a==Action::Left||a==Action::Up)exitChoice=false;
    if(a==Action::Right||a==Action::Down)exitChoice=true;
    if(a==Action::Cancel){exitStage=ExitStage::None;return;}
    if(a==Action::Enter){
      if(exitStage==ExitStage::Confirm){if(!exitChoice)exitStage=ExitStage::None;else {exitStage=ExitStage::Save;exitChoice=false;playing=false;}}
      else if(!exitChoice)leaveAppliance();
      else {exitSaveRequested=true;exitStage=ExitStage::Saving;}
    }
    return;
  }
  if((a==Action::Home||a==Action::Dashboard)&&inLifeblood()){lifebloodAction(Action::Home);return;}
  if(a==Action::Home&&inFx()){fxAction(a);return;}
  if(a==Action::Dashboard&&inFx()){fxAction(Action::Home);return;}
  if(a==Action::Home){
    page=Page::Pattern;editing=mixer=powerPrompt=powerYes=false;
    trackBanner=effectBanner=volumeBanner=false;notice.clear();return;
  }
  if(a==Action::Dashboard){
    page=page==Page::Dashboard?Page::Pattern:Page::Dashboard;
    editing=mixer=powerPrompt=powerYes=trackBanner=effectBanner=volumeBanner=false;notice.clear();return;
  }
  if(a==Action::PowerPrompt) {
    if(!powerPrompt){powerPrompt=true;powerYes=false;trackBanner=false;}
    return;
  }
  if(a==Action::PowerConfirm) {
    if(powerPrompt) {
      if(powerYes){poweringOff=true;playing=false;}
      else powerPrompt=false;
    }
    return;
  }
  if(powerPrompt) {
    if(a==Action::Left || a==Action::Up)powerYes=true;
    if(a==Action::Right || a==Action::Down)powerYes=false;
    if(a==Action::ShiftUp)shift=false;
    if(a==Action::Shift2Up)shift2=false;
    return;
  }
  if (a == Action::ShiftDown || a == Action::ShiftUp) {
    if(a==Action::ShiftDown&&!shift&&shift2){
      exitStage=ExitStage::Confirm;exitChoice=false;trackBanner=effectBanner=volumeBanner=false;notice.clear();
    }
    shift = a == Action::ShiftDown;
    return;
  }
  if (a == Action::Shift2Down || a == Action::Shift2Up) {
    shift2 = a == Action::Shift2Down;
    return;
  }

  if(inFx()){fxAction(a);return;}
  if(inLifeblood()){lifebloodAction(a);return;}

  if(shift && page==Page::Pattern && !mixer && (a==Action::Up || a==Action::Down)) {
    const int delta=a==Action::Up?12:-12;
    auto &t=project.tracks[track];bool any=false;
    auto fits=[&](int note) {return note<0 || (note+delta>=0 && note+delta<=119);};
    for(int r=0;r<columnLength(t,0);r++) {
      any |= t.steps[r].note>=0;
      if(!fits(t.steps[r].note)){status="OCTAVE LIMIT - NOTES UNCHANGED";return;}
    }
    if(editing && row<columnLength(t,0)) {
      any |= pending.note>=0;
      if(!fits(pending.note)){status="OCTAVE LIMIT - NOTES UNCHANGED";return;}
    }
    if(!any){status="NO NOTES TO TRANSPOSE";return;}
    checkpoint();
    for(int r=0;r<columnLength(t,0);r++)if(t.steps[r].note>=0)t.steps[r].note+=delta;
    if(editing && row<columnLength(t,0) && pending.note>=0)pending.note+=delta;
    status=delta>0?"TRACK OCTAVE UP - UNDO AVAILABLE":"TRACK OCTAVE DOWN - UNDO AVAILABLE";
    return;
  }
  if (a == Action::TrackSettings && shift) {
    checkpoint();
    project.tracks[track]=Track{};
    ++project.trackReset[track];
    editing=effectBanner=false;
    notice="TRACK "+std::to_string(track+1)+" CLEARED";
    ++noticeSerial;
    status="TRACK CLEARED - UNDO AVAILABLE";
    return;
  }
  if (a == Action::Play) {
    playing = !playing;
    status = playing ? "PLAYING" : "STOPPED";
    return;
  }
  if(a==Action::ColumnSettings) {
    if(page==Page::Pattern && !mixer){page=Page::Column;columnField=0;editing=effectBanner=trackBanner=false;return;}
    if(page==Page::Column){page=Page::Pattern;return;}
    a=Action::Cancel;
  }
  if(a==Action::Play){playing=!playing;return;}
  if(a==Action::Mixer){page=page==Page::Mixer?Page::Pattern:Page::Mixer;mixer=page==Page::Mixer;editing=trackBanner=effectBanner=false;mixerPage=mixerTrack/6;return;}
  if (a == Action::Session || a == Action::Master || a==Action::Patterns) {
    Page target = a == Action::Session ? Page::Sessions : a==Action::Patterns?Page::Patterns:Page::Master;
    page = a==Action::Session && page==Page::Session?Page::Sessions:page == target ? Page::Pattern : target;
    editing = mixer = trackBanner = effectBanner = false;
    return;
  }
  if(a==Action::Undo || a==Action::Redo) {undo(a==Action::Redo);return;}
  if(page==Page::Dashboard){
    if(a==Action::Cancel){page=Page::Pattern;return;}
    if(a==Action::Left)dashboardItem=(dashboardItem+4)%5;
    if(a==Action::Right)dashboardItem=(dashboardItem+1)%5;
    if(a==Action::Up||a==Action::Down){int target=dashboardItem+(dashboardItem<3?3:-3);if(target<5)dashboardItem=target;}
    if(a==Action::Enter){
      const Page targets[]={Page::Pattern,Page::Mixer,Page::Master,Page::GlobalInserts,Page::Device};
      page=targets[dashboardItem];editing=false;mixer=page==Page::Mixer;
      if(mixer)mixerPage=mixerTrack/6;
    }return;
  }
  if(page==Page::GlobalInserts){
    if(a==Action::Cancel){page=Page::Dashboard;return;}
    if(a==Action::Up||a==Action::Down)globalInsertItem=(globalInsertItem+(a==Action::Down?1:GlobalInsertCount-1))%GlobalInsertCount;
    int bit=int(globalInsertTypes()[globalInsertItem]);
    if(a==Action::Enter)device.disabledInserts.flip(bit);
    if(a==Action::Left)device.disabledInserts.set(bit);
    if(a==Action::Right)device.disabledInserts.reset(bit);
    return;
  }
  if(page==Page::Device){
    if(a==Action::Cancel){page=Page::Dashboard;return;}
    if(a==Action::Left||a==Action::Right||a==Action::Up||a==Action::Down){int d=a==Action::Right||a==Action::Up?1:-1;device.brightness=std::clamp(int(device.brightness)+d*(shift?10:5),0,100);}return;
  }
  if(page==Page::Mixer){
    if(a==Action::Cancel){if(editing)editing=false;else {page=Page::Pattern;mixer=false;}return;}
    if(a==Action::Enter){editing=!editing;return;}
    if(a==Action::PrevTrack||a==Action::NextTrack){mixerPage=1-mixerPage;mixerTrack=mixerPage*6+mixerTrack%6;editing=false;return;}
    if(editing){
      auto &t=project.tracks[mixerTrack];
      if(a==Action::Up||a==Action::Down||a==Action::Left||a==Action::Right){
        checkpoint();if(mixerField==0){if(a==Action::Up||a==Action::Down)t.level=std::clamp(int(t.level)+(a==Action::Up?1:-1)*(shift?10:1),0,100);}
        else if(mixerField==1)t.mute=!t.mute;else t.solo=!t.solo;
      }
    }else{
      if(a==Action::Left||a==Action::Right)mixerTrack=mixerPage*6+(mixerTrack%6+(a==Action::Right?1:5))%6;
      if(a==Action::Up||a==Action::Down)mixerField=(mixerField+(a==Action::Down?1:2))%3;
    }return;
  }
  if(page==Page::Patterns){
    if(a==Action::Cancel){page=Page::Pattern;return;}
    if(a==Action::Left)patternSlot=patternSlot/8*8+(patternSlot+7)%8;
    if(a==Action::Right)patternSlot=patternSlot/8*8+(patternSlot+1)%8;
    if(a==Action::Up)patternSlot=(patternSlot+24)%32;
    if(a==Action::Down)patternSlot=(patternSlot+8)%32;
    if(a==Action::Enter){page=Page::PatternSettings;sessionField=0;}return;
  }
  if(page==Page::PatternSettings){
    if(a==Action::Cancel){page=Page::Patterns;return;}
    if(a==Action::Up||a==Action::Down)sessionField=(sessionField+(a==Action::Down?1:2))%3;
    if(a==Action::Left||a==Action::Right){
      if(patternSlot==activePattern||(activePattern<0&&patternSlot==0))checkpoint();
      int d=a==Action::Right?1:-1;auto &o=(patternSlot==activePattern||(activePattern<0&&patternSlot==0))?patternOverrides:patterns[patternSlot].overrides;
      if(sessionField==0){int v=o.bpm?o.bpm-29:0;v=(v+d+272)%272;o.bpm=v?v+29:0;}
      if(sessionField==1){int v=o.scale==255?0:o.scale+1;v=(v+d+Scales+1)%(Scales+1);o.scale=v?v-1:255;}
      if(sessionField==2){int v=o.root==255?0:o.root+1;v=(v+d+13)%13;o.root=v?v-1:255;}
      if(patternSlot==activePattern||(activePattern<0&&patternSlot==0))applyMusicalSettings();dirty=true;
    }return;
  }
  if(page==Page::Sessions) {
    if(a==Action::Cancel){page=Page::Pattern;return;}
    if(a==Action::Left)sessionSlot=(sessionSlot/8)*8+(sessionSlot+7)%8;
    if(a==Action::Right)sessionSlot=(sessionSlot/8)*8+(sessionSlot+1)%8;
    if(a==Action::Up)sessionSlot=(sessionSlot+56)%64;
    if(a==Action::Down)sessionSlot=(sessionSlot+8)%64;
    if(a==Action::Enter){page=Page::Session;sessionField=0;sessionEdit=false;}
    return;
  }
  // ERASE mode: Shift+Enter on any column of the spreadsheet.
  if(page==Page::Pattern&&!editing&&!mixer&&shift&&a==Action::Enter){
    erasing=!erasing;status=erasing?"ERASE MODE - ENTER CLEARS  SHIFT+ENTER EXITS":"ERASE MODE OFF";return;
  }
  if(erasing&&page==Page::Pattern&&!mixer){
    if(a==Action::Cancel){erasing=false;status="ERASE MODE OFF";return;}
    if(a==Action::Enter){eraseCell();return;}
  }
  // FUNC + short X on the instrument column: hear the sound hold X drew for the track
  // instrument, on the root note of the scale in octave 4 (D4 in D). Nothing is redrawn.
  if(shift2&&!shift&&!editing&&!mixer&&page==Page::Pattern&&column==1&&a==Action::RandomizerOptions){
    auto slot=int(project.tracks[track].instrument);const auto &patch=trackPatch(project,track,slot);
    auditionRoot=true;++auditionSerial;
    notice="INSTRUMENT "+std::to_string(slot+1)+" ON "+noteName(60+project.root%12);
    noticeDetail=std::string(instrumentName(patch.engine))+" / "+patchFamily(patch);
    noticeMs=1200;noticeDetailSerial=++noticeSerial;return;
  }
  if(editing&&page==Page::Pattern&&(a==Action::RandomizerOptions||a==Action::RandomizeColumn)){
    if(a==Action::RandomizerOptions)randomizeEventParameter(false);
    else if(fxColumn()){if(shift)randomizePendingEffect();else randomizePendingParameters();}
    else if(shift)randomizeEventParameter(true);
    return;
  }
  if(a==Action::RandomizerOptions) {
    page=page==Page::Randomizer?Page::Pattern:Page::Randomizer;
    editing=mixer=effectBanner=trackBanner=false;return;
  }
  if(page==Page::Randomizer) {
    if(a==Action::Cancel || a==Action::Enter){page=Page::Pattern;return;}
    if(a==Action::Up || a==Action::Down)randomizerField^=1;
    if(a==Action::Left || a==Action::Right) {
      auto &value=density[track][column];
      if(randomizerField==0)value^=EuclideanMode;
      else value=uint8_t((value&EuclideanMode)|std::clamp(densityPercent(value)+(a==Action::Right?1:-1)*(shift?10:1),0,100));
    }
    if(a==Action::RandomizeColumn){page=Page::Pattern;randomizeColumn();}
    return;
  }
  if(a==Action::TrackSettings) {
    page=page==Page::Track?Page::Pattern:Page::Track;
    editing=mixer=effectBanner=trackBanner=false;return;
  }
  if(editing&&page==Page::Pattern&&(a==Action::PrevTrack||a==Action::NextTrack)){
    eventTab=(eventTab+(a==Action::NextTrack?1:eventTabCount()-1))%eventTabCount();effectBanner=false;return;
  }
  if(shift2&&!shift&&!editing&&!mixer&&page==Page::Pattern&&(a==Action::PrevTrack||a==Action::NextTrack)){
    int last=columnLength(project.tracks[track],column)-1;
    if(!hasSelection())selectAnchor=row;
    row=std::clamp(row+(a==Action::NextTrack?1:-1),0,last);
    status=std::to_string(selectionLast()-selectionFirst()+1)+" ROWS SELECTED - ENTER EDITS ALL";return;
  }
  if(shift&&!editing&&!mixer&&page==Page::Pattern&&(a==Action::PrevTrack||a==Action::NextTrack)){
    rotateColumn(a==Action::NextTrack?1:-1);return;
  }
  if((a==Action::PrevTrack || a==Action::NextTrack) && page!=Page::Master) {
    editing=false; track=(track+(a==Action::PrevTrack?Tracks-1:1))%Tracks;
    row=std::min(row,columnLength(project.tracks[track],column)-1);trackBanner=true;return;
  }
  if(page==Page::Column) {
    if(a==Action::Cancel || a==Action::Enter){page=Page::Pattern;return;}
    if(a==Action::Up || a==Action::Down)columnField=(columnField+(a==Action::Down?1:2))%3;
    if(a==Action::Left || a==Action::Right){
      checkpoint();auto &t=project.tracks[track];auto &s=t.columns[column];int d=a==Action::Right?1:-1;
      if(columnField==0)s.length=(int(s.length)+d+17)%17;
      if(columnField==1){int i=s.clock==255?0:s.clock+1;i=(i+d+7)%7;s.clock=i?i-1:255;}
      if(columnField==2){int i=s.direction==255?0:s.direction+1;i=(i+d+Directions+1)%(Directions+1);s.direction=i?i-1:255;}
      row=std::min(row,columnLength(t,column)-1);
    }
    return;
  }
  if(page==Page::Track) {
    if(a==Action::Cancel) {page=Page::Pattern;return;}
    if(a==Action::Up || a==Action::Down) trackField=(trackField+(a==Action::Down?1:TrackFields-1))%TrackFields;
    if(a==Action::Left || a==Action::Right) {
      checkpoint();auto &t=project.tracks[track];int d=a==Action::Right?1:-1;
      auto change=[&](uint8_t &v,int lo,int hi,int delta) {v=std::clamp(int(v)+delta,lo,hi);};
      switch(trackField) {
        case 0: change(t.length,1,PatternRows,d);row=std::min(row,columnLength(t,column)-1);break;
        case 1: change(t.clock,0,5,d);break;
        case 2: t.direction=(t.direction+d+Directions)%Directions;break;
        case 3: change(t.instrument,0,Instruments-1,d);break;
        case 4: change(t.level,0,100,d*(shift?10:1));break;
        case 5: t.pan=std::clamp(int(t.pan)+d*(shift?20:2),-100,100);break;
        case 6:case 7:case 8: change(t.macroSend[trackField-6],0,100,d*(shift?10:1));break;
        case 9: change(t.delaySend,0,100,d*(shift?10:1));break;
        case 10: change(t.reverbSend,0,100,d*(shift?10:1));break;
        case 11: {auto &p=editTrackPatch(project,track,t.instrument);p.engine=(p.engine+d+SynthEngines)%SynthEngines;p.seed=0;project.trackReset[track]++;break;}
      }
    }
    return;
  }
  if(page==Page::Macro){
    if(a==Action::Cancel||a==Action::Enter||a==Action::TrackSettings){page=Page::Track;return;}
    if(a==Action::Up||a==Action::Down)macroField=(macroField+(a==Action::Down?1:8))%9;
    if(a==Action::Left||a==Action::Right){
      checkpoint();auto &t=project.tracks[track];int mask=1<<macroModule;
      if(macroField==0){if(a==Action::Left)t.macroOverride&=~mask;else{if(!(t.macroOverride&mask))t.macros[macroModule]=project.master.macros[macroModule];t.macroOverride|=mask;}}
      else{if(!(t.macroOverride&mask))t.macros[macroModule]=project.master.macros[macroModule];t.macroOverride|=mask;auto &v=t.macros[macroModule].value[macroField-1];v=std::clamp(int(v)+(a==Action::Right?1:-1)*(shift?10:1),0,100);}
    }return;
  }
  if (page != Page::Pattern) {
    if (a == Action::Cancel && page==Page::Session && sessionEdit) { sessionEdit = false; return; }
    if (a == Action::Cancel) { page = page==Page::Session?Page::Sessions:Page::Pattern; return; }
    int d = (a == Action::Up || a == Action::Right || a == Action::NextTrack) ? 1 : -1;
    if (page == Page::Master) {
      if (a == Action::NextTrack || a == Action::PrevTrack)
        {masterModule = masterPageModule((masterPageIndex(masterModule)+d+MasterPages)%MasterPages);masterParameter=std::min(masterParameter,masterParameterCount(masterModule)-1);}
      else if (a == Action::Up || a == Action::Down)
        masterParameter=(masterParameter+(a==Action::Down?1:masterParameterCount(masterModule)-1))%masterParameterCount(masterModule);
      else if (a == Action::Left || a == Action::Right) {
        checkpoint();
        auto &v = project.master.value(masterModule,masterParameter);
        if(masterModule==7&&masterParameter==5)v=d>0;                                  // SIDECHAIN: OFF / ON
        else if(masterModule==7&&masterParameter==6)v=uint8_t((int(v)+d+Tracks)%Tracks); // SOURCE: tracks 1 .. 12
        else v = std::clamp(int(v) + d * (shift ? 10 : 1), 0, 100);
      }
    } else {
      // The cross moves between TEMPO, SCALE, ROOT NOTE, SWITCH QUANTIZE and the die beside
      // SCALE / ROOT NOTE (RIGHT from either). START puts the selected value in edit mode,
      // where LEFT / RIGHT change it, and START or A leave it; START on the die rolls.
      bool dieRow = sessionField == 1 || sessionField == 2 || sessionField == SessionDie;
      if (a == Action::RandomizeColumn && dieRow) { sessionEdit = false; rollScaleAndRoot(); return; }
      if (sessionEdit && a == Action::Enter) { sessionEdit = false; return; }
      if (a == Action::Up || a == Action::Down) {
        sessionEdit = false;
        if (sessionField == SessionDie) sessionField = a == Action::Up ? 0 : 3;
        else sessionField = (sessionField - d + SessionFields) % SessionFields;
        return;
      }
      if (!sessionEdit) {
        if (a == Action::Enter) { if (sessionField == SessionDie) rollScaleAndRoot(); else sessionEdit = true; }
        else if (a == Action::Right && (sessionField == 1 || sessionField == 2)) { sessionDieFrom = sessionField; sessionField = SessionDie; }
        else if (a == Action::Left && sessionField == SessionDie) sessionField = sessionDieFrom;
        return;
      }
      if (a == Action::Left || a == Action::Right) {
        checkpoint();
        if (sessionField == 0) sessionDefaults.bpm = std::clamp(int(sessionDefaults.bpm) + d, 30, 300);
        if (sessionField == 1) sessionDefaults.scale = (sessionDefaults.scale + d + Scales) % Scales;
        if (sessionField == 2) sessionDefaults.root = (sessionDefaults.root + d + 12) % 12;
        if (sessionField == 3) sessionDefaults.quantize = uint8_t((sessionDefaults.quantize + d + PatternQuantizeModes) % PatternQuantizeModes);
        applyMusicalSettings();
      }
    }
    return;
  }
  if (a == Action::RandomizeColumn) {
    editing = false;
    randomizeColumn();
    return;
  }
  if (a == Action::PrevTrack || a == Action::NextTrack) {
    editing = false;
    track = (track + (a == Action::PrevTrack ? 11 : 1)) % 12;
    trackBanner = true;
    return;
  }

  if (a == Action::Cancel) {
    if(!editing&&hasSelection()){selectAnchor=-1;status="SELECTION CLEARED";return;}
    editing = false;
    status = "EDIT CANCELLED";
    return;
  }
  if (editing) {
    Step before=pending;
    if (a == Action::Enter) {
      checkpoint();
      if(hasSelection())applyToSelection();
      project.tracks[track].steps[row] = pending;
      if(pendingInsertSet&&(column==2||column==3))project.tracks[track].inserts[column-2]=pendingInsert;
      pendingInsertSet=false;
      if(eventRandomized)project.seed=eventRandomSeed;
      editing = false;
      status = "VALUE COMMITTED";
    } else if (a == Action::Left || a == Action::Right) {
      if(lockTab()>=0){
        auto &v=pending.locks[lockTab()];v=a==Action::Left?Inherit:uint8_t(noteParameterDefault(project.tracks[track],lockTab()));
      } else if (eventTab==0&&(column == 2 || column == 3)) {
        auto &f = pending.fx[column - 2];
        int d = a == Action::Right ? 1 : -1;
        const auto &choices=selectableEffects();auto found=std::find(choices.begin(),choices.end(),f.type);int at=found==choices.end()?0:int(found-choices.begin());
        bool fresh=f.type==FxType::None;auto before=f.type;
        f.type=choices[(at+d+choices.size())%choices.size()];
        // A mix still at the previous effect's default follows the new effect's default.
        if(f.type!=FxType::None&&((fresh&&f.value==0)||(!fresh&&f.value==defaultFxMix(before))))f.value=defaultFxMix(f.type);
        effectBanner = true;
        ++effectAnnouncement;
      }
    } else if (a == Action::Up || a == Action::Down) {
      int d = a == Action::Up ? 1 : -1;
      if(lockTab()>=0){auto &v=pending.locks[lockTab()];int value=v==Inherit?noteParameterDefault(project.tracks[track],lockTab()):v;v=std::clamp(value+d*(shift?10:1),0,100);}
      else if(lengthTab()&&column==0)pending.length=uint8_t(std::clamp(int(pending.length)+d*(shift?4:1),1,MaxNoteLength));
      else if(lengthTab()){auto &n=pending.fx[column-2].length;n=uint8_t(std::clamp(int(n)+d*(shift?4:1),1,MaxFxLength));}
      else if(conditionTab()){auto &c=eventCondition(pending,column);c=cycleCondition(std::clamp(cycleConditionIndex(c)+d,0,CycleConditions-1));}
      else if (column == 0){
        pending.note = std::clamp(int(pending.note) + d, -3, 119);
      }
      else if (column == 1)
        pending.instrument =
            uint8_t(std::clamp((pending.instrument==NoInstrument?-1:int(pending.instrument)) + d, -1, Instruments - 1));
      else if (column == 4) {
        pending.modifier.semitones=std::clamp(int(pending.modifier.semitones)+d,-24,24);
        pending.modifier.conditionalOnly=true;
      } else {
        auto &f = pending.fx[column - 2];
        f.value = std::clamp(int(f.value) + d * (shift ? 10 : 1), 0, 100);
      }
    }
    if(std::memcmp(&before,&pending,sizeof pending)!=0)requestAudition();
    return;
  }
  switch (a) {
  case Action::Up:
    if (mixer) {
      checkpoint();
      auto &l = project.tracks[track].level;
      l = std::min(100, int(l) + 5);
    } else
      row = (row + columnLength(project.tracks[track],column) - 1) % columnLength(project.tracks[track],column);
    break;
  case Action::Down:
    if (mixer) {
      checkpoint();
      auto &l = project.tracks[track].level;
      l = std::max(0, int(l) - 5);
    } else
      row = (row + 1) % columnLength(project.tracks[track],column);
    break;
  case Action::Left:
    if (mixer)
      track = (track + 11) % 12;
    else
      {column = ColumnOrder[(columnPosition(column) + Columns-1) % Columns];row=std::min(row,columnLength(project.tracks[track],column)-1);}
    break;
  case Action::Right:
    if (mixer)
      track = (track + 1) % 12;
    else
      {column = ColumnOrder[(columnPosition(column) + 1) % Columns];row=std::min(row,columnLength(project.tracks[track],column)-1);}
    break;
  case Action::Enter:
    if (mixer) {
      checkpoint();
      project.tracks[track].mute = !project.tracks[track].mute;
    } else {
      pending = project.tracks[track].steps[row];
      eventRandomSeed=project.seed;eventRandomized=false;pendingInsertSet=false;
      if(column==0&&pending.note<0)pending.note=60;
      editOrigin=pending;
      requestAudition();
      editing = true;
      fxPart = eventTab = 0;
      status = hasSelection()?"EDITING "+std::to_string(selectionLast()-selectionFirst()+1)+" ROWS":"UP/DOWN VALUE  F1/F2 TABS";
    }
    break;
  case Action::RandomizeColumn:
    randomizeColumn();
    break;
  case Action::Reserved:
    break;
  case Action::PrevTrack:
    track = (track + 11) % 12;
    break;
  case Action::NextTrack:
    track = (track + 1) % 12;
    break;
  case Action::Play:
    playing = !playing;
    status = playing ? "PLAYING" : "STOPPED";
    break;
  case Action::Mixer:
    mixer = !mixer;
    break;
  case Action::Rows:
    visibleRows = visibleRows == 16 ? 8 : 16;
    break;
  case Action::Undo:
    undo();
    break;
  case Action::Redo:
    undo(true);
    break;
  case Action::Clear:
    checkpoint();
    project.tracks[track].steps[row] = Step{};
    break;
  case Action::Copy:
    clipboard = project.tracks[track].steps;
    copied = true;
    status = "TRACK COPIED";
    break;
  case Action::Paste:
    if (copied) {
      checkpoint();
      project.tracks[track].steps = clipboard;
      status = "TRACK PASTED";
    }
    break;
  case Action::TempoUp:
    checkpoint();
    project.bpm = std::min(300, int(project.bpm) + 1);
    break;
  case Action::TempoDown:
    checkpoint();
    project.bpm = std::max(30, int(project.bpm) - 1);
    break;
  default:
    break;
  }
}
// Portable little-endian format with CRC32, strict bounds and atomic
// replacement.
static uint32_t crc(const std::vector<uint8_t> &b) {
  uint32_t c = ~0u;
  for (uint8_t x : b) {
    c ^= x;
    for (int i = 0; i < 8; i++)
      c = (c >> 1) ^ (0xedb88320u & uint32_t(-int(c & 1)));
  }
  return ~c;
}
static void put(std::vector<uint8_t> &b, uint32_t x, int n) {
  while (n--) {
    b.push_back(x & 255);
    x >>= 8;
  }
}
std::vector<uint8_t> encodeProject(const Project &p) {
  std::vector<uint8_t> b;
  put(b, p.seed, 4);
  put(b, p.bpm, 2);
  put(b, PatternRows, 2);
  for (const auto &t : p.tracks) {
    put(b, t.level, 1);
    put(b, uint8_t(t.pan), 1);
    put(b, t.mute | (t.solo?2:0), 1);
    for (const auto &s : t.steps) {
      put(b, uint16_t(s.note), 2);
      put(b, s.instrument, 1);
      for (auto f : s.fx) {
        put(b, int(f.type), 1);
        put(b, f.value, 1);
      }
      put(b, s.condition.occurrence, 1);
      put(b, s.condition.every, 1);
      put(b, uint8_t(s.modifier.semitones), 1);
      put(b, s.modifier.conditionalOnly, 1);
    }
  }
  put(b, p.root, 1);
  put(b, p.scale, 1);
  for (const auto &patch : p.patches) { put(b, patch.engine, 1); put(b, patch.seed, 4); }
  for (auto amount : p.master.amount) put(b, amount, 1);
  for(const auto &module:p.master.extra) for(auto value:module) put(b,value,1);
  for(const auto &t:p.tracks) for(auto v:{t.length,t.clock,t.direction,t.instrument,t.delaySend,t.reverbSend}) put(b,v,1);
  for(const auto &t:p.tracks)for(const auto &s:t.inserts) {
    for(auto v:s.clouds)put(b,v,1);for(auto v:s.amp)put(b,v,1);for(auto v:s.rotor)put(b,v,1);for(auto v:s.bizLevel)put(b,v,1);
    for(const auto &m:s.biz){put(b,m.module,1);for(auto v:m.value)put(b,v,1);}
  }
  // Keep the six legacy geometry positions so old Project payloads stay readable.
  for(const auto &t:p.tracks)for(int old=0;old<6;old++){auto c=old==4?ColumnSettings{}:t.columns[old==5?4:old];put(b,c.length,1);put(b,c.clock,1);put(b,c.direction,1);}
  for(auto v:p.master.reverbSpace)put(b,v,1);
  for(const auto &t:p.tracks)for(const auto &s:t.inserts)for(auto v:s.space)put(b,v,1);
  for(auto v:p.master.busMix)put(b,v,1);
  for(const auto&m:p.master.macros)for(auto v:m.value)put(b,v,1);
  for(const auto&t:p.tracks){for(auto v:t.macroSend)put(b,v,1);put(b,t.macroOverride,1);for(const auto&m:t.macros)for(auto v:m.value)put(b,v,1);}
  for(const auto&t:p.tracks)for(const auto&s:t.steps)for(auto c:s.eventConditions){put(b,c.occurrence,1);put(b,c.every,1);}
  for(int tr=0;tr<Tracks;tr++){put(b,p.localPatchMask[tr],2);for(const auto &patch:p.trackPatches[tr]){put(b,patch.engine,1);put(b,patch.seed,4);}}
  for(const auto&t:p.tracks)for(const auto&s:t.steps)for(auto v:s.locks)put(b,v,1);
  for(const auto&t:p.tracks)for(const auto&i:t.inserts)for(auto v:i.filter)put(b,v,1);
  for(const auto&t:p.tracks)for(const auto&i:t.inserts){for(auto v:i.qdelay)put(b,v,1);for(auto v:i.matrix)put(b,v,1);for(const auto&n:i.nodes)for(auto v:n)put(b,v,1);for(auto v:i.crush)put(b,v,1);for(auto v:i.frequalizer)put(b,v,1);for(auto v:i.rings)put(b,v,1);for(auto v:i.fracture)put(b,v,1);for(auto v:i.ivan)put(b,v,1);for(auto v:i.murder)put(b,v,1);for(auto v:i.lofi)put(b,v,1);}
  for(auto v:p.master.sidechain)put(b,v,1);
  for(const auto&t:p.tracks)for(const auto&s:t.steps)put(b,s.length,1);
  for(const auto&t:p.tracks)for(const auto&s:t.steps)for(const auto&f:s.fx)put(b,f.length,1);
  for(const auto&t:p.tracks)for(const auto&i:t.inserts){for(auto v:i.flanger)put(b,v,1);for(auto v:i.phaser)put(b,v,1);}
  // AMAL23: native effect profiles, preceded by their count and width so later builds
  // with more effects (or wider profiles) still read this payload.
  put(b,NativeCount,1);put(b,NativeParams,1);
  for(const auto&t:p.tracks)for(const auto&i:t.inserts)for(const auto&profile:i.native)for(auto v:profile)put(b,v,1);
  std::vector<uint8_t> h={'A','M','A','L',23,0,0,0};put(h,crc(b),4);h.insert(h.end(),b.begin(),b.end());return h;
}
bool saveProject(const Project &p,const std::string &path,std::string &err){
  auto b=encodeProject(p);
  std::string tmp = path + ".tmp";
  FILE *f = fopen(tmp.c_str(), "wb");
  if (!f) {
    err = "CANNOT OPEN SAVE";
    return false;
  }
  bool ok = fwrite(b.data(), 1, b.size(), f) == b.size() && fflush(f) == 0 &&
            fsync(fileno(f)) == 0;
  if (fclose(f) != 0)
    ok = false;
  if (ok)
    ok = rename(tmp.c_str(), path.c_str()) == 0;
  if (ok) {
    auto dir = std::filesystem::path(path).parent_path();
    int fd = open(dir.empty() ? "." : dir.c_str(), O_RDONLY);
    if (fd >= 0) {
      fsync(fd);
      close(fd);
    }
  } else {
    remove(tmp.c_str());
    err = "SAVE FAILED";
  }
  return ok;
}
bool loadProject(Project &out, const std::string &path, std::string &err) {
  std::ifstream f(path, std::ios::binary);
  f.seekg(0,std::ios::end);auto size=f.tellg();if(size<0||size>200000){err="INVALID PROJECT SIZE";return false;}
  f.seekg(0);std::vector<uint8_t>b(size);if(!f.read((char*)b.data(),size)){err="PROJECT READ FAILED";return false;}
  return decodeProject(out,b,err);
}
bool decodeProject(Project &out,const std::vector<uint8_t>&b,std::string &err){
  auto size=std::streamoff(b.size());
  constexpr size_t legacySize = 12 + 8 + Tracks * (3 + MaxRows * 11);
  constexpr size_t extra = 2 + Instruments * 5 + MasterModules;
  constexpr size_t macroBytes=MacroCount*MacroParameters+Tracks*(MacroCount+1+MacroCount*MacroParameters);
  if(size<std::streamoff(legacySize)||size>std::streamoff(legacySize+extra+MasterModules*3+Tracks*6+Tracks*2*InsertSettingsBytes+Tracks*6*3+4+Tracks*2*7+4+macroBytes+EventConditionBytes+TrackPatchBytes+NoteLockBytes+FilterSettingsBytes+PortSettingsBytes+2+Tracks*MaxRows+Tracks*MaxRows*2+ModulationSettingsBytes+2+Tracks*2*255*255)){
    err="INVALID PROJECT SIZE";return false;
  }
  if (b.size() != size || std::string(b.begin(), b.begin() + 4) != "AMAL" ||
      (b[4] < 1 || b[4] > 23) || b[5] || b[6] || b[7]) {
    err = "INVALID PROJECT OR VERSION";
    return false;
  }
  // AMAL18: 12 instrument slots (FM DRUM); AMAL15..17: 11; older: 8.
  const int storedInstruments=b[4]>=18?Instruments:b[4]>=15?11:8;
  const size_t storedExtra=2+storedInstruments*5+MasterModules;
  const size_t storedTrackPatchBytes=Tracks*((b[4]>=15?2:1)+storedInstruments*5);
  const size_t versionSize=legacySize + (b[4] >= 3 ? storedExtra : 0) + (b[4] >= 4 ? MasterModules*3 : 0) + (b[4] >= 5 ? Tracks*6 : 0) + (b[4] >= 6 ? Tracks*2*InsertSettingsBytes : 0) + (b[4] >= 7 ? Tracks*6*3 : 0) + (b[4] >= 9 ? 4 : 0) + (b[4] >= 10 ? Tracks*2*7+4 : 0) + (b[4] >= 11 ? macroBytes : 0)+(b[4]>=13?EventConditionBytes:0)+(b[4]>=14?storedTrackPatchBytes:0)+(b[4]>=16?NoteLockBytes+FilterSettingsBytes:0)+(b[4]>=17?PortSettingsBytes:0)+(b[4]>=19?2:0)+(b[4]>=20?Tracks*MaxRows:0)+(b[4]>=21?Tracks*MaxRows*2:0)+(b[4]>=22?ModulationSettingsBytes:0);
  // AMAL23 ends with the native profiles: their count and width, then the values.
  size_t nativeCount=0,nativeWidth=0;
  if(b[4]>=23){if(versionSize+2>b.size()){err="VERSION SIZE MISMATCH";return false;}nativeCount=b[versionSize];nativeWidth=b[versionSize+1];}
  if (size != std::streamoff(versionSize+(b[4]>=23?2+Tracks*2*nativeCount*nativeWidth:0))) {
    err = "VERSION SIZE MISMATCH"; return false;
  }
  size_t i = 8;
  auto get = [&](int n) {
    uint32_t v = 0;
    for (int j = 0; j < n; j++)
      v |= uint32_t(b[i++]) << (8 * j);
    return v;
  };
  auto sum = get(4);
  if (crc(std::vector<uint8_t>(b.begin() + 12, b.end())) != sum) {
    err = "PROJECT CHECKSUM FAILED";
    return false;
  }
  Project p;
  p.seed = get(4);
  p.bpm = get(2);
  auto storedLength = get(2);
  p.length = PatternRows;
  bool ok = p.seed && p.bpm >= 30 && p.bpm <= 300 && storedLength >= 1 &&
            storedLength <= MaxRows;
  for (auto &t : p.tracks) {
    t.level = get(1);
    t.pan = int8_t(get(1));
    auto mute = get(1);
    t.mute = mute&1;t.solo=b[4]>=12 && (mute&2);
    ok &= t.level <= 100 && t.pan >= -100 && t.pan <= 100 && mute <= (b[4]>=12?3:1);
    for (auto &s : t.steps) {
      s.note = int16_t(get(2));
      s.instrument = get(1);
      ok &= s.note >= -3 && s.note <= 119 && (s.instrument < storedInstruments || (b[4]>=5 && s.instrument==NoInstrument));
      if (b[4] == 1) {
        for (int slot = 0; slot < 4; slot++) {
          auto type = get(1), value = get(1);
          ok &= type <= 5 && value <= 100;
          if (slot < 2 && type <= 2)
            s.fx[slot] = {type == 1 ? FxType::Volume
                                    : type == 2 ? FxType::Pan : FxType::None,
                          uint8_t(value)};
        }
      } else {
        for (auto &x : s.fx) {
          x.type = FxType(get(1));
          x.value = get(1);
          ok &= int(x.type) < (b[4]>=23?int(FxType::Count):b[4]>=22?int(FxType::Phaser)+1:b[4]>=17?int(FxType::Random)+1:b[4]>=16?28:b[4]>=11?27:b[4]>=10?24:b[4]>=6?23:19) && x.value <= 100;
        }
        s.condition.occurrence = get(1);
        s.condition.every = get(1);
        s.modifier.semitones = int8_t(get(1));
        auto mode = get(1);
        s.modifier.conditionalOnly = mode;
        ok &= s.condition.every >= 1 && s.condition.every <= 16 &&
              s.condition.occurrence >= 1 &&
              s.condition.occurrence <= s.condition.every &&
              s.modifier.semitones >= -24 && s.modifier.semitones <= 24 &&
              mode <= 1;
      }
    }
  }
  if (b[4] >= 3) {
    p.root = get(1); p.scale = get(1);
    ok &= p.root < 12 && p.scale < Scales;
    for (int slot=0;slot<storedInstruments;slot++) { auto &patch=p.patches[slot]; patch.engine = get(1); patch.seed = get(4); ok &= patch.engine < (b[4]>=8?SynthEngines:8); }
    for (auto &amount : p.master.amount) { amount = get(1); ok &= amount <= 100; }
  }
  if(b[4]<3)for(int i=0;i<8;i++)p.patches[i].engine=i;
  if(b[4]<8){
    // Retained voices keep their recipes; removed engines receive a fresh replacement.
    static const uint8_t map[]={3,4,0,5,4,2,1,3};
    for(int slot=0;slot<8;slot++){auto &patch=p.patches[slot];auto old=patch.engine;patch.engine=old<8?map[old]:0;if(old!=2&&old!=5&&old!=6)patch.seed=0;}
  }
  if (b[4] >= 4) {
    for(auto &module:p.master.extra) for(auto &value:module) { value=get(1); ok &= value<=100; }
  } else if(b[4] == 3) {
    auto old=p.master.amount[7];
    p.master.amount[7]=uint8_t((.85f-.7f*old/100.f-.02f)/.93f*100);
    p.master.extra[7][0]=uint8_t(old*7/19);
  }
  if(b[4]>=5) for(auto &t:p.tracks) {
    t.length=get(1);t.clock=get(1);t.direction=get(1);t.instrument=get(1);t.delaySend=get(1);t.reverbSend=get(1);
    ok &= t.length>=1 && t.length<=PatternRows && t.clock<6 && t.direction<Directions && t.instrument<storedInstruments && t.delaySend<=100 && t.reverbSend<=100;
  }
  else for(int t=0;t<Tracks;t++)p.tracks[t].instrument=t%storedInstruments;
  if(b[4]>=6)for(auto &t:p.tracks)for(auto &s:t.inserts) {
    auto read=[&](auto &a){for(auto &v:a){v=get(1);ok &= v<=100;}};
    read(s.clouds);read(s.amp);read(s.rotor);read(s.bizLevel);
    for(auto &m:s.biz){m.module=get(1);ok &= m.module<=100;read(m.value);}
  }
  if(b[4]>=7)for(auto &t:p.tracks)for(int old=0;old<6;old++){
    ColumnSettings c;c.length=get(1);c.clock=get(1);c.direction=get(1);
    ok &= c.length<=PatternRows && (c.clock==255 || c.clock<6) && (c.direction==255 || c.direction<Directions);
    if(old!=4)t.columns[old==5?4:old]=c;
  }
  if(b[4]>=9)for(auto &v:p.master.reverbSpace){v=get(1);ok &= v<=100;}
  else p.master.amount[9]=100; // Former whole-mix send becomes a track-only wet return.
  if(b[4]>=10){
    for(auto &t:p.tracks)for(auto &s:t.inserts)for(auto &v:s.space){v=get(1);ok &= v<=100;}
    for(auto &v:p.master.busMix){v=get(1);ok &= v<=100;}
  }else {
    // Earlier master space settings belong to a different algorithm.
    auto defaults=MasterSettings{};p.master.extra[9]=defaults.extra[9];p.master.reverbSpace=defaults.reverbSpace;
  }
  if(b[4]>=11){
    for(auto&m:p.master.macros)for(auto&v:m.value){v=get(1);ok &= v<=100;}
    for(auto&t:p.tracks){for(auto&v:t.macroSend){v=get(1);ok &= v<=100;}t.macroOverride=get(1);ok &= t.macroOverride<8;for(auto&m:t.macros)for(auto&v:m.value){v=get(1);ok &= v<=100;}}
  }else{
    const auto &a=p.master.amount;
    if(a[0]||a[1]||a[2]){p.master.macros[0].value[0]=std::max({a[0],a[1],a[2]});for(auto&t:p.tracks)t.macroSend[0]=100;}
    if(a[3]||a[4]){p.master.macros[1].value[0]=a[3];p.master.macros[1].value[1]=a[4];for(auto&t:p.tracks)t.macroSend[1]=100;}
    if(a[5]||a[6]){p.master.macros[2].value[0]=a[5];p.master.macros[2].value[1]=a[6];for(auto&t:p.tracks)t.macroSend[2]=100;}
    for(auto&t:p.tracks)for(auto&s:t.steps)for(auto&f:s.fx){
      if(f.type>=FxType::Distortion&&f.type<=FxType::Overdrive)f.type=FxType::Abrasion;
      if(f.type==FxType::Bitcrush||f.type==FxType::Redux)f.type=FxType::Corrosion;
    }
  }
  if(b[4]>=13)for(auto&t:p.tracks)for(auto&s:t.steps)for(auto&c:s.eventConditions){c.occurrence=get(1);c.every=get(1);ok &= c.every>=1&&c.every<=8&&c.occurrence>=1&&c.occurrence<=c.every;}
  if(b[4]>=14)for(int tr=0;tr<Tracks;tr++){p.localPatchMask[tr]=get(b[4]>=15?2:1);ok &= p.localPatchMask[tr]<(1u<<storedInstruments);for(int slot=0;slot<storedInstruments;slot++){auto &patch=p.trackPatches[tr][slot];patch.engine=get(1);patch.seed=get(4);ok &= patch.engine<SynthEngines;}}
  if(b[4]>=16){
    for(auto&t:p.tracks)for(auto&s:t.steps)for(auto &v:s.locks){v=get(1);ok &= v<=100||v==Inherit;}
    for(auto&t:p.tracks)for(auto&i:t.inserts)for(auto &v:i.filter){v=get(1);ok &= v<=100;}
  }else{
    // Transfer old automation out of the two insert lanes without losing values.
    for(auto&t:p.tracks)for(auto&s:t.steps)for(auto &f:s.fx){
      int lock=f.type==FxType::Pan?NotePan:f.type==FxType::Volume?NoteVolume:f.type==FxType::Delay?NoteDelay:f.type==FxType::Reverb?NoteReverb:f.type>=FxType::Abrasion&&f.type<=FxType::Modulation?NoteAbrasion+int(f.type)-int(FxType::Abrasion):-1;
      if(lock>=0){s.locks[lock]=f.value;f={};}
      if(f.type==FxType::Diode||f.type==FxType::Rat)f={};
      if(f.type==FxType::Lowpass||f.type==FxType::Highpass){auto&cfg=t.inserts[&f-s.fx.data()];cfg.filter[0]=f.type==FxType::Highpass?40:0;f.type=FxType::Filter;}
    }
  }
  if(b[4]>=17)for(auto&t:p.tracks)for(auto&i:t.inserts){auto read=[&](auto&a){for(auto&v:a){v=get(1);ok &= v<=100;}};read(i.qdelay);read(i.matrix);for(auto&n:i.nodes)read(n);read(i.crush);read(i.frequalizer);read(i.rings);read(i.fracture);read(i.ivan);read(i.murder);read(i.lofi);}
  if(b[4]>=19){p.master.sidechain[0]=get(1);p.master.sidechain[1]=get(1);ok &= p.master.sidechain[0]<=1&&p.master.sidechain[1]<Tracks;}
  if(b[4]>=20)for(auto&t:p.tracks)for(auto&s:t.steps){s.length=get(1);ok &= s.length>=1&&s.length<=MaxNoteLength;}
  // AMAL21: FX LENGTH per FX cell; older payloads keep one-step effects.
  if(b[4]>=21)for(auto&t:p.tracks)for(auto&s:t.steps)for(auto&f:s.fx){f.length=get(1);ok &= f.length>=1&&f.length<=MaxFxLength;}
  // AMAL22: FLANGER and PHASER profiles; older payloads keep the defaults.
  if(b[4]>=22)for(auto&t:p.tracks)for(auto&i:t.inserts){for(auto&v:i.flanger){v=get(1);ok &= v<=100;}for(auto&v:i.phaser){v=get(1);ok &= v<=100;}}
  // AMAL23: native profiles; effects or values this build lacks are skipped, missing ones keep defaults.
  if(b[4]>=23){get(2);for(auto&t:p.tracks)for(auto&ins:t.inserts)for(size_t k=0;k<nativeCount;k++)for(size_t v=0;v<nativeWidth;v++){
    auto x=get(1);ok &= x<=100;if(k<size_t(NativeCount)&&v<size_t(NativeParams))ins.native[k][v]=uint8_t(x);}}
  if (!ok) {
    err = "PROJECT VALUES OUT OF RANGE";
    return false;
  }
  out = p;
  return true;
}
} // namespace amalthea
