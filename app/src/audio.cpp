#include "audio.hpp"
#include "fx_rack.hpp"
#include "native_engines.hpp"
#include "walk.h"
#include "effects.hpp"
#include "track_macros.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif
extern "C" {
#include "eng.h"
extern const eng_t ENG_VAP, ENG_PERC, ENG_CHIP, ENG_FIBER, ENG_HIVE, ENG_OP4, ENG_ANIMA, ENG_RUSSOLO, ENG_BAYAREA, ENG_RESURGENT, ENG_CALIGULA, ENG_FMDRUM;
int32_t eng_bpm10 = 1200;
void amalthea_patch(int, uint32_t, int32_t *);
void amalthea_patch_variant(int, uint32_t, uint32_t, int32_t *);
}
namespace amalthea {
static const eng_t *engines[] = {&ENG_VAP,&ENG_PERC,&ENG_CHIP,&ENG_FIBER,&ENG_HIVE,&ENG_OP4,&ENG_ANIMA,&ENG_RUSSOLO,&ENG_BAYAREA,&ENG_RESURGENT,&ENG_CALIGULA,&ENG_FMDRUM};
struct Audio::Impl {
  struct Patch {
    alignas(16) uint8_t data[ENG_CK_MAX]{};
    size_t used=0;
    Patch()=default;
    Patch(const Patch&o){*this=o;}
    Patch &operator=(const Patch&o){if(this!=&o){used=o.used;std::memcpy(data,o.data,used);}return *this;}
  };
  struct Voice {
    alignas(16) uint8_t data[ENG_V_MAX]{};
    NativeVoicePtr native;
    bool active = false, released = false, audition = false;
    int fadeOut = 0;   // samples left of a click-free kill fade (0: none)
    int engine = 0, track = 0, note = 60, instrumentSlot=-1;
    int64_t gate = 0;
    uint64_t age = 0;
    float volume = 1, pan = 0;
    Patch patch;
    uint32_t patchSeed=0,variation=0;
  };
  // Row variations (FUNC + hold X on the INSTRUMENT column): each varied (slot, variation)
  // of a track points at a patch cooked on the producer thread into the variant pool. The
  // pool's buffers are immutable while any message the audio may still read refers to them.
  static constexpr int VariantRefs=2*VariationsPerTrack;   // (headroom for edited / pasted rows)
  struct VariantRef { uint8_t slot=NoInstrument; uint32_t variation=0; const Patch *patch=nullptr; };
  using VariantTable=std::array<std::array<VariantRef,VariantRefs>,Tracks>;
  struct VariantBuffer { std::unique_ptr<Patch> patch; uint8_t engine=0; uint32_t seed=0,variation=0; unsigned lastRef=0; bool valid=false; };
  static constexpr size_t VariantPoolMax=Tracks*VariantRefs*2;
  std::vector<VariantBuffer> variantPool;   // producer only
  // The oldest message sequence the audio may still read (its current one, or a queued swap).
  alignas(64) std::atomic<unsigned> liveFloor{0};
  unsigned appliedSeq=0,pendingSeq=0;      // audio only
  VariantTable variants{};                  // audio: the current message's table
  const Patch *variantPatch(int t,int slot,uint32_t variation)const{
    if(!variation)return nullptr;
    for(const auto &r:variants[t])if(r.patch&&r.slot==slot&&r.variation==variation)return r.patch;
    return nullptr;
  }
  // RANDOM: per track FX slot, complete effect configurations drawn on the producer thread
  // (with their processors prepared); the audio thread only picks one at each trigger.
  static constexpr int RandomDraws=8;
  struct RandomPool { int count=0; std::array<Effect,RandomDraws> effect{}; std::array<InsertSettings,RandomDraws> settings{}; };
  struct Message {
    Project project;
    DeviceSettings device;
    bool playing = false;
    int auditionTrack=-1;Step auditionStep{};bool endAudition=false;
    std::array<Patch, Tracks*Instruments> patches{};
    std::array<PortedPointers,Tracks*2> ported{};
    std::array<RandomPool,Tracks*2> random{};
    VariantTable variants{};
    unsigned seq=0;
    PatternSync sync{};
  };
  std::array<Message, 8> messages{};
  // A queued pattern: held here until the quantized bar, then applied like a message.
  Message pendingSwap{};
  bool pendingValid=false;
  uint32_t generation=0;
  uint8_t quantize=QuantizeLongest;
  // Only the producer constructs these. Owners outlive every callback pointer.
  std::array<std::array<std::unique_ptr<PortedProcessor>,PortedTypes>,Tracks*2> preparedPorted{};
  std::array<RandomPool,Tracks*2> preparedRandom{},random{};
  std::array<uint32_t,Tracks*2> preparedRandomSeed{};
  std::array<InsertSettings,Tracks*2> insertKeep{};   // a RANDOM row's current draw (kept across edits)
  uint32_t randomPick=0x2f6b1d3u;
  // A RANDOM step resolved to one of its slot's drawn configurations: the insert takes the draw's
  // settings, and its amount is the draw's scaled by the step's AMOUNT (0 bypasses).
  Effect resolveRandom(int t,int slot,Effect f){
    if(f.type!=FxType::Random)return f;
    const auto &pool=random[t*2+slot];
    if(!pool.count||!f.value)return {};
    randomPick^=randomPick<<13;randomPick^=randomPick>>17;randomPick^=randomPick<<5;
    int k=int(randomPick%uint32_t(pool.count));
    inserts[t][slot].setSettings(pool.settings[k]);insertKeep[t*2+slot]=pool.settings[k];
    Effect e=pool.effect[k];e.value=uint8_t(std::max(1,e.value*f.value/100));
    return e;
  }
  alignas(64) std::atomic<unsigned> head{0}, tail{0};
  std::array<Patch, Tracks*Instruments> patches{}, prepared{};
  std::array<InstrumentPatch, Tracks*Instruments> preparedIdentity{};
  bool preparedValid = false;
  std::vector<Voice> voices;
  Project project;
  DeviceSettings device;
  std::array<bool,Tracks> audible=[] {std::array<bool,Tracks>a{};a.fill(true);return a;}();
  bool playing = false;
  struct Transport {
    AmaltheaWalk walk{};
    int row=-1, advances=0;
    double remaining=0;
    uint64_t cycle=0,currentCycle=0;
  };
  std::array<std::array<Transport,Columns>,Tracks> transport{};
  double stepFrames(int t,int c=0) const {return 44100.*60/(project.bpm*4*clockRate(columnClock(project.tracks[t],c)));}
  // FX LENGTH: the effect a FX row triggered keeps its slot for that many FX-column
  // steps; empty rows inside it keep it running, a new effect replaces it.
  std::array<std::array<Effect,2>,Tracks> heldFx{};
  std::array<std::array<int,2>,Tracks> heldLeft{};
  void resetColumn(int t,int c,uint32_t seed){
    transport[t][c]=Transport{};
    if(c==2||c==3)heldLeft[t][c-2]=0;
    // Equal inherited lanes retain the old track traversal, including random directions.
    amalthea_walk_start(&transport[t][c].walk,seed^(0x9e3779b9u*(t+1)));
  }
  void resetTrack(int t,uint32_t seed){for(int c=0;c<Columns;c++)resetColumn(t,c,seed);}
  void resetStats(int t,int c,AudioStats &stats){
    stats.columnRow[t][c].store(-1);stats.columnCycle[t][c].store(0);stats.columnAdvances[t][c].store(0);
    if(c==0){stats.trackRow[t].store(-1);stats.trackCycle[t].store(0);stats.advances[t].store(0);if(t==0){stats.row.store(-1);stats.cycle.store(0);}}
  }
  Step resolvedStep(int t)const {
    const auto &tr=project.tracks[t];
    auto at=[&](int c)->const Step&{return tr.steps[std::max(0,transport[t][c].row)];};
    auto enabled=[&](int c){return conditionMatches(eventCondition(at(c),c),transport[t][c].currentCycle);};
    Step step;step.note=at(0).note;step.condition=at(0).condition;step.length=at(0).length;
    if(conditionMatches(step.condition,transport[t][0].currentCycle))step.locks=at(0).locks;
    step.instrument=enabled(1)?at(1).instrument:NoInstrument;step.variation=enabled(1)?at(1).variation:0;
    for(int slot=0;slot<2;slot++){
      auto f=enabled(slot+2)?at(slot+2).fx[slot]:Effect{};
      step.fx[slot]=f.type!=FxType::None?f:heldLeft[t][slot]>0?heldFx[t][slot]:Effect{};
    }
    for(auto &fx:step.fx)if(!device.insertEnabled(fx.type))fx={};
    if(enabled(4))step.modifier=at(4).modifier;
    return step;
  }
  // The end of the current loop of the reference track: the longest (or shortest) loop
  // among the audible tracks holding notes (all tracks when none do). True in the block
  // where that track would advance into a new cycle -- every lane then restarts together.
  bool barBoundary() const {
    auto hasNotes=[&](int tr){const auto &track=project.tracks[tr];for(int row=0;row<columnLength(track,0);row++)if(track.steps[row].note>=0)return true;return false;};
    bool any=false;for(int tr=0;tr<Tracks;tr++)any|=audible[tr]&&hasNotes(tr);
    int ref=-1;double best=0;
    for(int tr=0;tr<Tracks;tr++){
      if(any&&!(audible[tr]&&hasNotes(tr)))continue;
      const auto &track=project.tracks[tr];
      double loop=amalthea_walk_period(columnDirection(track,0),columnLength(track,0))*stepFrames(tr,0);
      if(ref<0||(quantize==QuantizeShortest?loop<best:loop>best)){ref=tr;best=loop;}
    }
    const auto &lane=transport[ref][0];
    return lane.row>=0&&lane.advances==0&&lane.remaining<=0;
  }
  MasterEffects master{true};
  std::array<TrackMacros,Tracks> macros{};
  std::array<std::array<InsertEffect, 2>, Tracks> inserts{};
  std::array<std::array<float, ENG_BLK * 2>, Tracks> trackAudio{};
  std::array<bool,Tracks> auditionActive{},auditionMixActive{};
  std::array<Step,Tracks> auditionSteps{};
  std::array<TrackMix,Tracks> currentMix{};
  std::array<float,Tracks> currentLeft{},currentRight{};
  uint64_t age = 0;
  float block[ENG_BLK * 2]{};
  int cursor = ENG_BLK;
  explicit Impl(size_t capacity) : voices(capacity) {
    for (int i = 0; i < SynthEngines; i++) {
      auto e = engines[i];
      if (e->v_size > ENG_V_MAX || e->ck_size > ENG_CK_MAX)
        throw std::runtime_error("engine exceeds voice storage");
      static std::once_flag initialized[SynthEngines];
      std::call_once(initialized[i],[e]{if(e->init)e->init();});
    }
    for(auto &v:voices)v.native=makeNativeVoice();
    startInsertWorker();
  }
  // Three persistent DSP workers partition tracks across the Brick's four cores.
  // Creation/allocation happens before audio starts; each track owns its DSP state.
  std::array<std::thread,3> insertWorkers;
  uint64_t insertGeneration=0;int insertRemaining=0;
  std::mutex insertMutex;
  std::condition_variable insertReady,insertDone;
  bool insertJob=false,insertExit=false,synthJob=false;
  // Monophonic tracks keep a few natural release tails, not dozens: past TrackTails the oldest
  // tail fades out over KillFade samples (no click) and frees its voice.
  static constexpr int TrackTails=3, KillFade=256;
  void limitTails(int t,const Voice *keep){
    for(;;){
      int n=0;Voice *oldest=nullptr;
      for(auto &v:voices)if(v.active&&!v.audition&&v.track==t&&v.released&&!v.fadeOut&&&v!=keep){n++;if(!oldest||v.age<oldest->age)oldest=&v;}
      if(n<=TrackTails||!oldest)return;
      oldest->fadeOut=KillFade;
    }
  }
  void renderVoices(int parity){
    for (auto &v : voices) {
      if(parity>=0&&v.track%4!=parity)continue;
      if(!v.active)continue;
      int32_t l[ENG_BLK]{}, r[ENG_BLK]{};
      if (v.gate <= 0)
        release(v);
      v.gate -= ENG_BLK;
      if (v.engine < SynthEngines) {
        auto e = engines[v.engine];
        int active = e->render(v.data, v.patch.data, l, r);
        if (!active)
          v.active = false;
        if (!e->stereo)
          std::copy(l, l + ENG_BLK, r);
      }
      // Muted sequencer voices still advance their envelopes; audition alone
      // may feed the muted track's output path.
      if(!audible[v.track]&&!v.audition){if(v.fadeOut){v.fadeOut=std::max(0,v.fadeOut-ENG_BLK);if(!v.fadeOut)v.active=false;}continue;}
      float gl=v.volume, gr=v.volume;
      const int fading=v.fadeOut;
      for (int i = 0; i < ENG_BLK; i++) {
        float a, b;
        a = l[i] / 32768.f;
        b = r[i] / 32768.f;
        float g=1.f;
        if(fading){int left=fading-i;g=left>0?float(left)/KillFade:0.f;}
        a *= gl*g;
        b *= gr*g;
        trackAudio[v.track][2 * i] += a;
        trackAudio[v.track][2 * i + 1] += b;
      }
      if(fading){v.fadeOut=std::max(0,fading-ENG_BLK);if(!v.fadeOut)v.active=false;}
    }
  }
  void processTrackInserts(int tr) {
    if(!audible[tr]&&!auditionActive[tr])return;
    for(auto &insert:inserts[tr]){
      auto type=insert.setting().type;
      if(!device.insertEnabled(type))continue;
      if(type!=FxType::Delay&&type!=FxType::Reverb&&type!=FxType::Pan&&type!=FxType::Volume)insert.processBlock(trackAudio[tr].data(),ENG_BLK);
    }
    // The same disjoint-track worker covers the new macros. Complete each
    // track's inserts, gain/pan and macro chain before the shared master sum.
    for(int frame=0;frame<ENG_BLK;frame++){
      float &l=trackAudio[tr][frame*2],&r=trackAudio[tr][frame*2+1];
      l*=currentLeft[tr];r*=currentRight[tr];macros[tr].process(l,r,currentMix[tr].macro);
    }
  }
  void startInsertWorker(){
    for(int lane=1;lane<4;lane++)insertWorkers[lane-1]=std::thread([this,lane]{
#ifdef __linux__
      sched_param param{};param.sched_priority=12;
      pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);
#endif
#if defined(__aarch64__)
      uint64_t fpcr;asm volatile("mrs %0, fpcr":"=r"(fpcr));fpcr|=uint64_t(1)<<24;asm volatile("msr fpcr, %0"::"r"(fpcr));
#endif
      std::unique_lock<std::mutex> lock(insertMutex);uint64_t seen=0;
      while(true){
        insertReady.wait(lock,[this,&seen]{return insertGeneration!=seen||insertExit;});if(insertExit)return;seen=insertGeneration;bool synth=synthJob;
        lock.unlock();if(synth)renderVoices(lane);else for(int tr=lane;tr<Tracks;tr+=4)processTrackInserts(tr);lock.lock();
        if(--insertRemaining==0){insertJob=false;insertDone.notify_one();}
      }
    });
  }
  ~Impl(){
    {std::lock_guard<std::mutex> lock(insertMutex);insertExit=true;}
    insertReady.notify_all();for(auto &worker:insertWorkers)if(worker.joinable())worker.join();
  }
  void release(Voice &v) {
    if (v.released)
      return;
    v.released = true;
    if (v.engine < SynthEngines)
      engines[v.engine]->off(v.data, v.patch.data, v.note * 65536);
  }
  void advanceColumn(int t,int c,AudioStats &stats){
    auto &clock=transport[t][c];const auto &tr=project.tracks[t];
    clock.row=amalthea_walk_next(&clock.walk,columnDirection(tr,c),columnLength(tr,c));
    clock.currentCycle=clock.cycle;
    stats.columnRow[t][c].store(clock.row);stats.columnCycle[t][c].store(clock.currentCycle);stats.columnAdvances[t][c].fetch_add(1);
    if(c==0){stats.trackRow[t].store(clock.row);stats.trackCycle[t].store(clock.currentCycle);stats.advances[t].fetch_add(1);if(t==0){stats.row.store(clock.row);stats.cycle.store(clock.currentCycle);}}
    if(++clock.advances>=amalthea_walk_period(columnDirection(tr,c),columnLength(tr,c))){clock.advances=0;clock.cycle++;}
    clock.remaining+=stepFrames(t,c);
    if(c==2 || c==3){
      int slot=c-2;auto f=conditionMatches(eventCondition(tr.steps[clock.row],c),clock.currentCycle)?tr.steps[clock.row].fx[slot]:Effect{};
      if(heldLeft[t][slot]>0)heldLeft[t][slot]--;
      bool trigger=f.type!=FxType::None;
      if(trigger){heldFx[t][slot]=f;heldLeft[t][slot]=std::clamp(int(f.length),1,MaxFxLength);}
      if(!auditionMixActive[t]){
        auto &insert=inserts[t][slot];
        if(trigger){insert.setSettings(tr.inserts[slot]);f=resolveRandom(t,slot,f);if(!device.insertEnabled(f.type))f={};insert.configure(f,project.bpm);}
        else if(heldLeft[t][slot]<=0&&insert.setting().type!=FxType::None){insert.setSettings(tr.inserts[slot]);insert.configure({},project.bpm);}   // the effect ends; its tail rings out
      }
    }
  }
  void startVoice(int t,int note,int instrument,int64_t gate,bool preview,AudioStats &stats,uint32_t variation=0){
    if(preview)for(auto &v:voices)if(v.active&&v.audition){if(v.released)v.active=false;else release(v);}
      const auto &identity=trackPatch(project,t,instrument);
      // These cores are complete played instruments, with internal fingers,
      // LPGs, feedback/delay state and envelopes. Retrigger that track's own
      // instrument rather than stacking another full appliance on every row.
      auto it=voices.end();
      // PERC is a whole kit in one voice (its pieces ring over each other inside it): a note
      // retriggers the track's kit voice instead of stacking another complete kit.
      const bool kit=identity.engine==1;
      const Patch *varied=variantPatch(t,instrument,variation);
      if(!varied)variation=0;   // (not prepared: the row plays the instrument as it is)
      if((nativeEngine(identity.engine)||kit)&&!preview)it=std::find_if(voices.begin(),voices.end(),[&](const Voice&v){return v.active&&!v.audition&&!v.fadeOut&&v.track==t&&v.instrumentSlot==instrument&&v.engine==identity.engine&&v.patchSeed==identity.seed&&v.variation==variation;});
      bool retrigger=it!=voices.end();
      // A different variation of a retriggered instrument is a fresh voice: the one playing
      // goes into its own release (its tail stays bounded by limitTails) instead of stacking.
      if(!retrigger&&!preview&&(nativeEngine(identity.engine)||kit))
        for(auto &o:voices)if(o.active&&!o.audition&&!o.released&&!o.fadeOut&&o.track==t&&o.instrumentSlot==instrument&&o.engine==identity.engine)release(o);
      if(!retrigger)it=std::find_if(voices.begin(),voices.end(),[](const Voice&v){return !v.active;});
      if (it == voices.end()) {
        it = std::min_element(
            voices.begin(), voices.end(),
            [](const Voice &a, const Voice &b) { return a.age < b.age; });
        stats.steals.fetch_add(1, std::memory_order_relaxed);
      }
      auto &v = *it;
      v.active = true;
      v.released = false;
      v.fadeOut = 0;
      v.audition=preview;
      v.engine = trackPatch(project,t,instrument).engine;
      v.patch = varied?*varied:patches[t*Instruments+instrument];
      v.patchSeed = trackPatch(project,t,instrument).seed;v.variation=variation;
      v.track = t;v.instrumentSlot=instrument;
      v.note = note;
      if(preview){stats.auditions.fetch_add(1);stats.auditionNote.store(note);}
      else{stats.triggers[t].fetch_add(1, std::memory_order_relaxed);stats.lastNote[t].store(note, std::memory_order_relaxed);}
      v.age = ++age;
      if(!preview)limitTails(t,&v);
      v.gate = gate;
      v.volume = 1;
      stats.lastInstrument[t].store(instrument);
      if (v.engine < SynthEngines) {
        auto e = engines[v.engine];
        if(retrigger&&kit)e->on(v.data,v.patch.data,v.note*65536,127);
        else if(retrigger)retriggerNativeVoice(v.data,v.note,127);
        else {if(nativeEngine(v.engine))bindNativeVoice(v.data,v.native.get());e->reset(v.data);e->on(v.data,v.patch.data,v.note*65536,127);}
      }
  }
  void triggerNote(int t,AudioStats &stats) {
      auto &clock=transport[t][0];const auto &tr=project.tracks[t];
      auto s=resolvedStep(t);uint64_t cycle=clock.currentCycle;
      if (!audible[t])
        return;
      if (!conditionMatches(s.condition, cycle))
        return;
      if (s.note == -2) {
        for (auto &v : voices)
          if (v.active && v.track == t && !v.audition)
            release(v);
        return;
      }
      if (s.note < 0)
        return;
      int note = triggeredNote(s, cycle);
      if (note < 0)
        return;
      float volume = 1;
      const int period=amalthea_walk_period(columnDirection(tr,0),columnLength(tr,0));
      int ties = 1;
      auto lookahead=clock.walk;
      while(ties<period && tr.steps[amalthea_walk_next(&lookahead,columnDirection(tr,0),columnLength(tr,0))].note==-3)ties++;
      // LENGTH (or the tie rows, whichever is longer); the next onset (a note or a note-off) on
      // this track's walk decides whether the note ends on its own or runs legato into it.
      const int length=std::max<int>(s.length,ties);
      int distance=1;
      auto ahead=clock.walk;
      while(distance<period){int n=tr.steps[amalthea_walk_next(&ahead,columnDirection(tr,0),columnLength(tr,0))].note;if(n>=0||n==-2)break;distance++;}
      const bool legato=length>=distance;
      // Monophonic tracks: a note still held when this one starts goes into its own envelope
      // release (never a hard cut: no click), and this one starts with its own attack.
      for(auto &v:voices)if(v.active&&!v.audition&&!v.released&&v.track==t&&!nativeEngine(v.engine))release(v);
      int instrument=effectiveInstrument(tr,s);
      startVoice(t,note,instrument,int64_t(stepFrames(t)*(legato?length:length-.2)),false,stats,s.instrument==NoInstrument?0:s.variation);
  }
  void makeBlock(AudioStats &stats) {
    auto t = tail.load(std::memory_order_relaxed);
    auto h = head.load(std::memory_order_acquire);
    while (t != h) {
      auto &m = messages[t % messages.size()];
      t++;
      if(m.sync.deferred){
        if(m.sync.generation>generation){pendingSwap=m;pendingValid=true;pendingSeq=m.seq;quantize=m.sync.quantize;}
        continue;
      }
      if(m.sync.generation<generation)continue;   // the editor has not yet taken the swapped-in pattern
      if(m.sync.generation>generation){generation=m.sync.generation;pendingValid=false;}
      if(!m.sync.keepPending)pendingValid=false;
      quantize=m.sync.quantize;
      applyMessage(m,stats);
    }
    tail.store(t, std::memory_order_release);
    if(pendingValid&&(!playing||barBoundary())){
      pendingValid=false;generation=pendingSwap.sync.generation;
      pendingSwap.playing=playing;pendingSwap.auditionTrack=-1;pendingSwap.endAudition=false;
      applyMessage(pendingSwap,stats);
      stats.patternGeneration.store(generation,std::memory_order_release);
    }
    liveFloor.store(pendingValid?std::min(appliedSeq,pendingSeq):appliedSeq,std::memory_order_release);
    renderBlock(stats);
  }
  void applyMessage(const Message &m,AudioStats &stats) {
    {
      bool start = m.playing && !playing;
      if(start)nativeTransportRestart();   // rhythmic inserts realign to the downbeat
      if(start)auditionMixActive.fill(false);   // (playback owns every track's mix and FX again)
      bool cleared=false;
      std::array<std::array<bool,Columns>,Tracks> changed{};
      for(int tr=0;tr<Tracks;tr++) {
        const auto &before=project.tracks[tr], &after=m.project.tracks[tr];
        bool wasEmpty=true,isEmpty=true;
        for(int row=0;row<columnLength(before,0);row++)wasEmpty &= before.steps[row].note==-1;
        for(int row=0;row<columnLength(after,0);row++)isEmpty &= after.steps[row].note==-1;
        bool explicitReset=project.trackReset[tr]!=m.project.trackReset[tr];
        bool becameEmpty=!wasEmpty&&isEmpty, rebuilt=wasEmpty&&!isEmpty;
        if(explicitReset || becameEmpty || rebuilt){
          cleared |= explicitReset || becameEmpty;
          for(auto &v:voices)if(v.track==tr){v.active=false;v.released=false;v.gate=0;}
          for(auto &insert:inserts[tr])insert.clear();
          macros[tr].clear();
          stats.meters[tr].store(0);
        }
        for(int c=0;c<Columns;c++){
          double oldPeriod=stepFrames(tr,c);
          double newPeriod=44100.*60/(m.project.bpm*4*clockRate(columnClock(after,c)));
          bool noteRestart=(becameEmpty||rebuilt) && columnLength(after,c)==columnLength(after,0) && columnClock(after,c)==columnClock(after,0) && columnDirection(after,c)==columnDirection(after,0);
          changed[tr][c]=explicitReset||noteRestart||columnLength(before,c)!=columnLength(after,c)||columnDirection(before,c)!=columnDirection(after,c)||columnClock(before,c)!=columnClock(after,c);
          if(explicitReset || noteRestart || columnLength(before,c)!=columnLength(after,c) || columnDirection(before,c)!=columnDirection(after,c)){
            resetColumn(tr,c,m.project.seed);resetStats(tr,c,stats);
          }else transport[tr][c].remaining*=newPeriod/oldPeriod;
        }
      }
      // A column joins an existing lane with identical musical instructions. Use
      // an unchanged lane first, so editing one column cannot restart its peers.
      for(int tr=0;tr<Tracks;tr++)for(int c=0;c<Columns;c++)if(changed[tr][c]){
        const auto &track=m.project.tracks[tr];int source=-1;
        for(int k=0;k<Columns;k++)if(k!=c && (!changed[tr][k] || k<c) &&
          columnLength(track,k)==columnLength(track,c) && columnClock(track,k)==columnClock(track,c) && columnDirection(track,k)==columnDirection(track,c)){
          source=k;if(!changed[tr][k])break;
        }
        if(source>=0){
          transport[tr][c]=transport[tr][source];
          stats.columnRow[tr][c].store(stats.columnRow[tr][source].load());
          stats.columnCycle[tr][c].store(stats.columnCycle[tr][source].load());
          stats.columnAdvances[tr][c].store(stats.columnAdvances[tr][source].load());
          if(c==0){stats.trackRow[tr].store(transport[tr][c].row);stats.trackCycle[tr].store(transport[tr][c].currentCycle);stats.advances[tr].store(stats.columnAdvances[tr][c].load());if(tr==0){stats.row.store(transport[tr][c].row);stats.cycle.store(transport[tr][c].currentCycle);}}
        }
      }
      auto changedInserts=device.disabledInserts^m.device.disabledInserts;
      if(changedInserts.any())for(int tr=0;tr<Tracks;tr++)for(int slot=0;slot<2;slot++){
        auto type=inserts[tr][slot].setting().type;
        auto raw=m.project.tracks[tr].steps[std::max(0,transport[tr][slot+2].row)].fx[slot].type;
        if(changedInserts[int(type)]||changedInserts[int(raw)])inserts[tr][slot].clear();
      }
      device=m.device;
      if(m.endAudition){for(auto &v:voices)if(v.active&&v.audition)release(v);auditionMixActive.fill(false);}
      project = m.project;
      bool anySolo=false;for(const auto&t:project.tracks)anySolo|=t.solo;
      for(int tr=0;tr<Tracks;tr++)audible[tr]=!project.tracks[tr].mute&&(!anySolo||project.tracks[tr].solo);
      patches = m.patches;random=m.random;variants=m.variants;appliedSeq=m.seq;
      for(int tr=0;tr<Tracks;tr++)for(int slot=0;slot<2;slot++)inserts[tr][slot].attachPorted(m.ported[tr*2+slot]);
      for(int tr=0;tr<Tracks;tr++)for(int slot=0;slot<2;slot++){
        auto &insert=inserts[tr][slot];insert.setSettings(project.tracks[tr].inserts[slot]);
        if(!device.insertEnabled(insert.setting().type))insert.configure({},project.bpm);
        int row=transport[tr][slot+2].row;
        if(!auditionMixActive[tr]&&((m.playing&&!start&&row>=0)||m.endAudition)){
          auto f=resolvedStep(tr).fx[slot],before=insert.setting();
          if(f.type==FxType::Random){if(before.type!=FxType::None)insert.setSettings(insertKeep[tr*2+slot]);}   // (keeps the current draw)
          else if(f.type!=before.type||f.value!=before.value)insert.configure(f,project.bpm);
        }
        // A stopped settings update must not re-engage a stored granular freeze.
        if(!m.playing)insert.release();
      }
      for(int tr=0;tr<Tracks;tr++)macros[tr].configure(effectiveMacros(project,tr));
      master.configure(project.master, project.bpm);
      if(cleared) {
        bool silent=true;
        for(const auto &track:project.tracks)for(int row=0;row<columnLength(track,0);row++)silent &= track.steps[row].note<0;
        for(const auto &voice:voices)silent &= !voice.active;
        // The last cleared source must not leave delay/filter/compressor history
        // poisoning its replacement. Other playing tracks keep the shared buses.
        if(silent)master.clear(project.bpm);
      }
      if (start) {
        for(int tr=0;tr<Tracks;tr++) {resetTrack(tr,project.seed);for(int c=0;c<Columns;c++)resetStats(tr,c,stats); }
        stats.cycle.store(0);
      }
      if (!m.playing && playing) {
        // Stop the transport, release gates, and keep rendering every effect tail.
        for (auto &v : voices)if(v.active)release(v);
        stats.row.store(-1);
        for(auto &r:stats.trackRow)r.store(-1);
        for(auto &tr:stats.columnRow)for(auto &r:tr)r.store(-1);
      }
      if(m.auditionTrack>=0&&m.auditionTrack<Tracks&&m.auditionStep.note<0){
        for(auto &v:voices)if(v.active&&v.audition&&v.track==m.auditionTrack)release(v);
        auditionMixActive[m.auditionTrack]=false;
      }
      if(m.auditionTrack>=0&&m.auditionTrack<Tracks&&m.auditionStep.note>=0){
        int tr=m.auditionTrack;
        // Stopped: the audition carries the edited step's own mix and FX on its track. Playing:
        // the edited step already plays in the pattern (the preview project), so the track keeps
        // each row's own locks -- holding the whole track on the edited row's pan / volume / sends
        // until the audition's tail died made a fresh column randomization sound late.
        if(!m.playing){
          auditionSteps[tr]=m.auditionStep;auditionMixActive[tr]=true;
          for(int slot=0;slot<2;slot++){auto f=resolveRandom(tr,slot,m.auditionStep.fx[slot]);inserts[tr][slot].configure(device.insertEnabled(f.type)?f:Effect{},project.bpm);}
        }else if(auditionMixActive[tr]){
          auditionMixActive[tr]=false;
          for(int slot=0;slot<2;slot++)inserts[tr][slot].configure(resolveRandom(tr,slot,resolvedStep(tr).fx[slot]),project.bpm);
        }
        startVoice(tr,m.auditionStep.note,effectiveInstrument(project.tracks[tr],m.auditionStep),8820,true,stats,m.auditionStep.instrument==NoInstrument?0:m.auditionStep.variation);
      }
      playing = m.playing;
      eng_bpm10 = project.bpm * 10;
    }
  }
  void renderBlock(AudioStats &stats) {
    if(playing)for(int tr=0;tr<Tracks;tr++) {
      bool noteDue=false;
      // Advance every due value lane before sampling them for a simultaneous NOTE tick.
      for(int c=0;c<Columns;c++){
        auto &lane=transport[tr][c];
        if(lane.remaining<=0){advanceColumn(tr,c,stats);if(c==0)noteDue=true;}
        lane.remaining-=ENG_BLK;
      }
      if(noteDue)triggerNote(tr,stats);
    }
    std::fill(std::begin(block), std::end(block), 0.f);
    std::array<float, Tracks> peak{};
    auto &mix=currentMix;auto &leftGain=currentLeft;auto &rightGain=currentRight;
    for(int tr=0;tr<Tracks;tr++) {
      const auto &track=project.tracks[tr];
      mix[tr]=resolveTrackMix(track,auditionMixActive[tr]?auditionSteps[tr]:resolvedStep(tr));
      leftGain[tr]=mix[tr].level*std::sqrt((1-mix[tr].pan)*.5f);
      rightGain[tr]=mix[tr].level*std::sqrt((1+mix[tr].pan)*.5f);
    }
    for (auto &t : trackAudio)
      t.fill(0);
    int n = 0,nativeWeight=0;auditionActive.fill(false);
    for(const auto &v:voices)if(v.active){n++;if(nativeEngine(v.engine))nativeWeight+=3;if(v.audition)auditionActive[v.track]=true;}
    for(int tr=0;tr<Tracks;tr++)if(auditionMixActive[tr]&&!auditionActive[tr]){
      auditionMixActive[tr]=false;for(int slot=0;slot<2;slot++)inserts[tr][slot].configure(resolveRandom(tr,slot,resolvedStep(tr).fx[slot]),project.bpm);
    }
    if(n+nativeWeight>=24){
      {std::lock_guard<std::mutex> lock(insertMutex);synthJob=true;insertJob=true;insertRemaining=3;++insertGeneration;}insertReady.notify_all();
      renderVoices(0);
      std::unique_lock<std::mutex> lock(insertMutex);insertDone.wait(lock,[this]{return !insertJob;});
    }else renderVoices(-1);
    int cloudCount=0;
    for(int tr=0;tr<Tracks;tr++)if(audible[tr])for(auto &insert:inserts[tr])if(insert.setting().type==FxType::Clouds&&insert.setting().value)++cloudCount;
    int grainLimit=std::clamp(128/std::max(1,cloudCount),4,32);
    for(auto &track:inserts)for(auto &insert:track)if(insert.setting().type==FxType::Clouds)insert.setGrainLimit(grainLimit);
    int heavy=0;
    for(int tr=0;tr<Tracks;tr++)if(audible[tr])for(auto &insert:inserts[tr])if(insert.setting().value&&insert.setting().type>=FxType::Clouds&&(insert.setting().type<=FxType::Biztortion||insert.setting().type==FxType::Space||insert.setting().type==FxType::Filter||portedIndex(insert.setting().type)>=0))++heavy;
    for(int tr=0;tr<Tracks;tr++)if(audible[tr]||auditionActive[tr]){
      bool source=false;for(const auto&v:voices)source|=v.active&&v.track==tr;
      bool send=false;for(float amount:mix[tr].macro)send|=amount>0;
      if(macros[tr].active()||(source&&send))++heavy;
    }
    if(heavy>=2){
      {std::lock_guard<std::mutex> lock(insertMutex);synthJob=false;insertJob=true;insertRemaining=3;++insertGeneration;}insertReady.notify_all();
      for(int tr=0;tr<Tracks;tr+=4)processTrackInserts(tr);
      std::unique_lock<std::mutex> lock(insertMutex);insertDone.wait(lock,[this]{return !insertJob;});
    }else for(int tr=0;tr<Tracks;tr++)processTrackInserts(tr);
    const int key=master.sidechain()?master.sidechainSource():-1;
    for (int frame = 0; frame < ENG_BLK; frame++) {
      float dl = 0, dr = 0, rl = 0, rr = 0, kl = 0, kr = 0;
      for (int tr = 0; tr < Tracks; tr++) {
        if(!audible[tr]&&!auditionActive[tr])continue;
        float l = trackAudio[tr][frame * 2], r = trackAudio[tr][frame * 2 + 1];
        dl+=l*mix[tr].delay;dr+=r*mix[tr].delay;
        rl+=l*mix[tr].reverb;rr+=r*mix[tr].reverb;
        block[frame * 2] += l;
        block[frame * 2 + 1] += r;
        if(tr==key){kl=l;kr=r;}
        peak[tr] = std::max(peak[tr], std::max(std::abs(l), std::abs(r)));
      }
      master.process(block[frame*2],block[frame*2+1],dl,dr,rl,rr,kl,kr);
    }
    for (int frame=0;frame<ENG_BLK;frame++) {
      block[2*frame] = std::clamp(block[2*frame], -.95f, .95f);
      block[2*frame+1] = std::clamp(block[2*frame+1], -.95f, .95f);
    }
    for (int t = 0; t < Tracks; t++) {
      float old = stats.meters[t].load(std::memory_order_relaxed);
      stats.meters[t].store(std::max(peak[t], old * .97f),
                            std::memory_order_relaxed);
    }
    stats.voices.store(n, std::memory_order_relaxed);
    stats.blocks.fetch_add(1, std::memory_order_relaxed);
    cursor = 0;
  }
};
Audio::Audio(size_t n)
    : impl(std::make_unique<Impl>(std::max<size_t>(n, 12))) {for(auto &r:stats.trackRow)r.store(-1); }
Audio::~Audio() = default;
bool Audio::submit(const Project &p, bool play,int auditionTrack,const Step *auditionStep,const DeviceSettings *device,bool endAudition,PatternSync sync) {
  auto &a = *impl;
  auto h = a.head.load(std::memory_order_relaxed);
  if (h - a.tail.load(std::memory_order_acquire) >= a.messages.size())
    return false;
  // Recipe generation and cooking stay on the producer/UI thread.
  for (int i=0;i<Tracks*Instruments;i++) {
    const auto &id=trackPatch(p,i/Instruments,i%Instruments);
    if (!a.preparedValid || id.engine != a.preparedIdentity[i].engine || id.seed != a.preparedIdentity[i].seed) {
      bool copied=false;
      for(int j=0;j<i;j++)if(trackPatch(p,j/Instruments,j%Instruments).engine==id.engine&&trackPatch(p,j/Instruments,j%Instruments).seed==id.seed){a.prepared[i]=a.prepared[j];copied=true;break;}
      if (!copied && id.engine < SynthEngines) {
        int32_t parameters[ENG_NPAR]{};
        amalthea_patch(id.engine,id.seed,parameters);
        engines[id.engine]->cook(a.prepared[i].data,parameters);
        a.prepared[i].used=engines[id.engine]->ck_size;
      }
      a.preparedIdentity[i]=id;
    }
  }
  // Discover processors used by each active lane before publishing any message.
  // Large delay stores are never constructed by the audio callback.
  for(int tr=0;tr<Tracks;tr++)for(int slot=0;slot<2;slot++)for(int row=0;row<columnLength(p.tracks[tr],slot+2);row++){
    auto type=p.tracks[tr].steps[row].fx[slot].type;int kind=portedIndex(type);
    if(kind>=0&&(!device||device->insertEnabled(type))&&!a.preparedPorted[tr*2+slot][kind])a.preparedPorted[tr*2+slot][kind]=std::make_unique<PortedProcessor>(type);
  }
  // RANDOM pools: drawn here (allocation and parameter naming stay off the audio thread),
  // redrawn when the project seed changes (any randomize action), processors prepared.
  for(int tr=0;tr<Tracks;tr++)for(int slot=0;slot<2;slot++){
    bool used=auditionStep&&auditionTrack==tr&&auditionStep->fx[slot].type==FxType::Random;
    for(int row=0;row<columnLength(p.tracks[tr],slot+2);row++)used|=p.tracks[tr].steps[row].fx[slot].type==FxType::Random;
    auto &pool=a.preparedRandom[tr*2+slot];
    if(!used){pool.count=0;continue;}
    uint32_t seed=(p.seed^(0x9e3779b9u*uint32_t(tr*2+slot+1)))|1u;
    if(pool.count&&a.preparedRandomSeed[tr*2+slot]==seed)continue;
    a.preparedRandomSeed[tr*2+slot]=seed;
    std::vector<FxType> types;for(auto type:selectableEffects())if(type!=FxType::None&&type!=FxType::Random&&(!device||device->insertEnabled(type)))types.push_back(type);
    pool.count=0;if(types.empty())continue;
    for(int k=0;k<int(pool.effect.size());k++){
      Effect e{types[randomNext(seed)%types.size()],0};InsertSettings cfg=p.tracks[tr].inserts[slot];
      randomizeFxParameters(e,cfg,seed);
      pool.effect[k]=e;pool.settings[k]=cfg;pool.count++;
      int kind=portedIndex(e.type);
      if(kind>=0&&!a.preparedPorted[tr*2+slot][kind])a.preparedPorted[tr*2+slot][kind]=std::make_unique<PortedProcessor>(e.type);
    }
  }
  if(auditionStep&&auditionTrack>=0&&auditionTrack<Tracks)for(int slot=0;slot<2;slot++){auto type=auditionStep->fx[slot].type;int kind=portedIndex(type);if(kind>=0&&!a.preparedPorted[auditionTrack*2+slot][kind])a.preparedPorted[auditionTrack*2+slot][kind]=std::make_unique<PortedProcessor>(type);}
  a.preparedValid=true;
  // Row variations: cook each varied (slot, variation) once into the pool, reusing a buffer
  // no message the audio may still read refers to. A full pool leaves a row as it is.
  Impl::VariantTable table{};
  {
    if(a.variantPool.capacity()<Impl::VariantPoolMax)a.variantPool.reserve(Impl::VariantPoolMax);
    const unsigned floor=a.liveFloor.load(std::memory_order_acquire);
    for(int tr=0;tr<Tracks;tr++){
      auto &refs=table[tr];int n=0;
      auto consider=[&](const Step &s){
        if(!s.variation||s.instrument>=Instruments||n>=Impl::VariantRefs)return;
        for(int k=0;k<n;k++)if(refs[k].slot==s.instrument&&refs[k].variation==s.variation)return;
        const auto &id=trackPatch(p,tr,s.instrument);
        if(id.engine>=SynthEngines)return;
        Impl::VariantBuffer *buffer=nullptr;
        for(auto &b:a.variantPool)if(b.valid&&b.engine==id.engine&&b.seed==id.seed&&b.variation==s.variation){buffer=&b;break;}
        if(!buffer){
          for(auto &b:a.variantPool)if(!b.valid||int(b.lastRef-floor)<0){buffer=&b;break;}
          if(!buffer&&a.variantPool.size()<Impl::VariantPoolMax){a.variantPool.emplace_back();a.variantPool.back().patch=std::make_unique<Impl::Patch>();buffer=&a.variantPool.back();}
          if(!buffer)return;
          int32_t parameters[ENG_NPAR]{};
          amalthea_patch_variant(id.engine,id.seed,s.variation,parameters);
          engines[id.engine]->cook(buffer->patch->data,parameters);
          buffer->patch->used=engines[id.engine]->ck_size;
          buffer->engine=id.engine;buffer->seed=id.seed;buffer->variation=s.variation;buffer->valid=true;
        }
        buffer->lastRef=h;
        refs[n++]={s.instrument,s.variation,buffer->patch.get()};
      };
      const auto &track=p.tracks[tr];
      for(int row=0;row<columnLength(track,1);row++)consider(track.steps[row]);
      if(auditionStep&&auditionTrack==tr)consider(*auditionStep);
    }
  }
  auto &m=a.messages[h % a.messages.size()];
  m.project=p; m.playing=play; m.patches=a.prepared;m.random=a.preparedRandom;m.device=device?*device:DeviceSettings{};
  for(int slot=0;slot<Tracks*2;slot++)for(int kind=0;kind<PortedTypes;kind++)m.ported[slot][kind]=a.preparedPorted[slot][kind].get();
  m.endAudition=endAudition;m.sync=sync;m.variants=table;m.seq=h;
  m.auditionTrack=auditionStep?auditionTrack:-1;if(auditionStep)m.auditionStep=*auditionStep;
  a.head.store(h + 1, std::memory_order_release);
  return true;
}
void Audio::render(float *stereo, int frames) {
#if defined(__aarch64__)
  // Subnormal filter/reverb tails can stall Cortex-A53 audio. Scope FTZ to this
  // callback and restore the caller's floating-point mode afterwards.
  uint64_t previousFpcr;
  asm volatile("mrs %0, fpcr" : "=r"(previousFpcr));
  uint64_t realtimeFpcr=previousFpcr|(uint64_t(1)<<24);
  asm volatile("msr fpcr, %0" :: "r"(realtimeFpcr));
#endif
  auto &a = *impl;
  for (int i = 0; i < frames; i++) {
    if (a.cursor == ENG_BLK)
      a.makeBlock(stats);
    stereo[2 * i] = a.block[2 * a.cursor];
    stereo[2 * i + 1] = a.block[2 * a.cursor + 1];
    a.cursor++;
  }
#if defined(__aarch64__)
  asm volatile("msr fpcr, %0" :: "r"(previousFpcr));
#endif
}
void writeWav(const std::string &path, const float *data, size_t n) {
  std::ofstream f(path, std::ios::binary);
  auto put = [&](uint32_t v, int count) {
    while (count--) {
      f.put(v & 255);
      v >>= 8;
    }
  };
  f.write("RIFF", 4);
  put(36 + n * 4, 4);
  f.write("WAVEfmt ", 8);
  put(16, 4);
  put(1, 2);
  put(2, 2);
  put(44100, 4);
  put(176400, 4);
  put(4, 2);
  put(16, 2);
  f.write("data", 4);
  put(n * 4, 4);
  for (size_t i = 0; i < n * 2; i++)
    put(uint16_t(int16_t(std::clamp(data[i], -1.f, 1.f) * 32767)), 2);
}
} // namespace amalthea
