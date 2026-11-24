// caligula's designs: a kind of sound and a seed make the whole patch, and
// eight sliders move it about the patch as drawn.
#ifndef VASCULAR_CALIGULA_DESIGN_H
#define VASCULAR_CALIGULA_DESIGN_H

#include "CaligulaCore.h"

namespace vascular {
namespace caligula {

enum Kind { K_ETHER, K_GHOST, K_HAUNT, K_TEXTURE, K_SWARM, K_WASTELAND, K_INFERNO, K_STORM, K_COUNT };

/// How a recipe is played: the touches on its sensors while it drones, the
/// controls a player turns, how far, and a stutter now and then.
struct Playing {
  double touchEvery = 1;        // seconds between gestures, on average
  double hold = 0.4, tap = 0.3, flurry = 0.2, chord = 0.1;   // the gestures' odds
  double holdLength[2] = {1.5, 6};
  double knobEvery = 4;         // seconds between turns of a control, on average
  // The odds of each control being the one turned.
  double turnFm = 1, turnLfo = 0, turnSource = 0, turnTotal = 0, turnTime = 0, turnDelMod = 0, turnDrive = 0, turnRing = 0;
  // Where each may be turned to, in 0..127.
  double fm[2] = {70, 100}, lfo[2] = {40, 90}, time[2] = {60, 110}, drive[2] = {20, 70};
  double stutterEvery = 0, stutterRate = 8, stutterDepth = 0.4, stutterLength = 1.2;
};

/// A design: the controls, which sensors a drone starts with, how many a
/// strike opens, and how it is played.
struct Design {
  Params p;
  bool held[8] = {};
  int strikeVoices = 1;
  double level = 1;
  Playing play;
};

/// Each voice's range, in MIDI notes, as the engine maps its tuning.
static const double kVoiceLo[8] = {-16, -16, 7, 9, 20, 20, 33, 33};
static const double kVoiceHi[8] = {93, 93, 109, 107, 116.54, 116.54, 126.24, 131.22};

/// The tuning that puts voice k on a MIDI note, moved by octaves into its
/// range and never below 41 Hz.
inline double tuneFor(int k, double note) {
  const double lo = std::max(kVoiceLo[k], 28.0), hi = kVoiceHi[k];
  while (note < lo) note += 12;
  while (note > hi) note -= 12;
  if (note < lo) note = lo;
  return clampd(127 * (note - kVoiceLo[k]) / (kVoiceHi[k] - kVoiceLo[k]), 0, 127);
}

/// The pitch knob's setting that multiplies a side's voices by exactly `ratio`.
inline double pitchFor(double ratio) { return clampd((ratio - 0.01) / 1.99 * 127, 0, 127); }

/// The recipe a kind and a seed make. Each kind puts the instrument where
/// it misbehaves well -- the places a player finds by hand, and rarely --
/// and says how it is played there:
///
///   ether      the delay singing by itself, its time modulated by its own
///              signal: growls, chirps, pitch bending in the echoes
///   ghost      sparse touches into long, dark, near-singing echoes, the FM
///              deep enough to be strange
///   haunt      the pairs modulating each other at the edge of chaos, a
///              player pushing them over and back
///   texture    the hyper-LFO at audio rate as the FM's source: ring-like,
///              metallic, grainy
///   swarm      the hyper-LFO at rhythm rates as the FM's source: stepped,
///              glitching sequences, its rates turned now and then
///   wasteland  fast, sharp touches in a ring of FM: metallic, percussive,
///              into short, chirping echoes
///   inferno    the whole instrument feeding back on itself, tipped in and
///              out of chaos
///   storm      a bit of everything, every control turned
///
/// Voices sit on notes of a chord or at simple ratios, so what comes out of
/// the FM is a tone, not a cluster; the controls a player turns stay where
/// the instrument is interesting, never at a dead end.
inline Design design(int kind, uint64_t seed) {
  Design d;
  Rng r;
  r.seed(seed ^ (0xCA11ull + (uint64_t)kind * 7919));
  auto R = [&](double a, double b) { return a + (b - a) * r.unit(); };
  auto chance = [&](double p) { return r.unit() < p; };
  Params &p = d.p;
  Playing &g = d.play;
  const int kk = kind < 0 || kind >= K_COUNT ? 0 : kind;
  p.vibrato = 0;
  p.vol = 127;
  p.quantize = chance(0.35);

  // The tones: a root, and each pair on an interval of it, its partner on
  // the same note, an octave, a fifth or a tritone off.
  static const int intervals[K_COUNT][8] = {
    {0, 7, 12, 19, 5, 24, 10, 3},  {0, 12, 7, 19, 24, 5, 17, 3},
    {0, 3, 7, 12, 1, 6, 10, 15},   {0, 7, 12, 5, 14, 19, 1, 6},
    {0, 7, 12, 5, 3, 10, 19, 24},  {0, 6, 7, 13, 1, 18, 11, 12},
    {0, 1, 6, 7, 12, 13, 18, 19},  {0, 5, 7, 10, 12, 3, 6, 17},
  };
  static const double roots[K_COUNT][2] = {{36, 72}, {40, 76}, {30, 56}, {33, 70}, {33, 68}, {36, 74}, {28, 56}, {30, 70}};
  const double root = std::floor(R(roots[kk][0], roots[kk][1] + 1));
  static const double ratios[6] = {1, 2, 1.5, 0.5, 0.75, 1.3333333333333333};
  const double ratio = ratios[r.next() % 6];
  p.pitch[0] = pitchFor(1);
  p.pitch[1] = pitchFor(ratio);
  const double sideShift = 12 * std::log2(ratio);
  static const double partner[5] = {0, 12, 7, 6, -12};
  for (int i = 0; i < 4; ++i) {
    // Each pair an octave or two away from the root as often as not.
    static const double octaves[6] = {0, 0, 12, -12, 24, -24};
    const double iv = intervals[kk][r.next() % 8] + octaves[r.next() % 6];
    const double other = partner[r.next() % 5] + (chance(0.5) ? R(-0.08, 0.08) : 0);
    for (int j = 0; j < 2; ++j) {
      const int k = 2 * i + j;
      p.tune[k] = tuneFor(k, root + iv + (j ? other : 0) - (k >= 4 ? sideShift : 0));
    }
  }

  // Common ground, which each kind then moves.
  // The pulses anywhere from soft to sharp, each pair its own, and the FM
  // mostly in its top third, where the instrument comes alive.
  for (int i = 0; i < 4; ++i) { p.source[i] = 0; p.fast[i] = chance(0.35); p.sharp[i] = chance(0.5) ? R(0, 40) : R(40, 115); p.mod[i] = R(60, 104); }
  p.hold[0] = R(0, 65); p.hold[1] = R(0, 55);
  p.fa = R(30, 90); p.fb = R(30, 90); p.andor = chance(0.5); p.link = chance(0.6);
  p.time1 = R(70, 110); p.time2 = R(55, 105); p.mod1 = R(20, 70); p.mod2 = R(20, 70);
  p.feedback = R(50, 70); p.delMix = R(35, 65); p.delMod = 0; p.lfoWav = chance(0.4);
  p.drv = R(20, 70); p.dstMix = R(0, 45);
  p.switchRing = chance(0.4); p.totalFB = 0;
  d.held[0] = d.held[1] = true;
  g.touchEvery = R(0.8, 2.5); g.knobEvery = R(2.5, 6);
  g.fm[0] = 72; g.fm[1] = 104; g.lfo[0] = 40; g.lfo[1] = 100; g.time[0] = 60; g.time[1] = 112; g.drive[0] = 20; g.drive[1] = 75;
  switch (kk) {
    case K_ETHER:     // the delay sings, its time bent by its own signal
      p.delMod = 0; p.feedback = R(72, 84); p.mod1 = R(40, 95); p.mod2 = R(40, 95);
      p.time1 = R(35, 85); p.time2 = R(30, 80); p.delMix = R(50, 75);
      for (int i = 0; i < 4; ++i) p.mod[i] = R(60, 92);
      g.touchEvery = R(1.5, 4); g.hold = 0.2; g.tap = 0.5; g.flurry = 0.2; g.chord = 0.1;
      g.turnFm = 1; g.turnTime = 2; g.turnDelMod = 0.3; g.time[0] = 30; g.time[1] = 95;
      break;
    case K_GHOST:     // sparse touches into long dark echoes
      p.time1 = R(100, 120); p.time2 = R(94, 116); p.feedback = R(62, 74); p.delMix = R(60, 85);
      p.hold[0] = R(0, 25); p.hold[1] = R(0, 20); p.drv = R(0, 35); p.dstMix = R(0, 15);
      for (int i = 0; i < 4; ++i) { p.mod[i] = R(70, 98); p.sharp[i] = R(0, 50); }
      g.touchEvery = R(2, 5); g.hold = 0.3; g.tap = 0.5; g.flurry = 0.1; g.chord = 0.1; g.holdLength[1] = 4;
      g.turnFm = 1; g.turnTime = 1; g.turnLfo = 0.3; g.time[0] = 95; g.time[1] = 122;
      break;
    case K_HAUNT:     // cross-FM at the edge of chaos, pushed over and back
      for (int i = 0; i < 4; ++i) p.mod[i] = R(85, 108);
      p.switchRing = chance(0.5);
      g.turnFm = 3; g.turnRing = 0.5; g.turnTime = 0.5; g.fm[0] = 80; g.fm[1] = 114; g.knobEvery = R(1.5, 4);
      g.hold = 0.5; g.chord = 0.2;
      break;
    case K_TEXTURE:   // the hyper-LFO at audio rate as the FM's source
      p.fa = R(108, 125); p.fb = R(104, 125); p.link = chance(0.7);
      for (int i = 0; i < 4; ++i) { p.source[i] = chance(0.7) ? 2 : 0; p.mod[i] = R(75, 100); }
      p.source[0] = 2;
      g.turnLfo = 2; g.turnFm = 1; g.turnSource = 0.5; g.lfo[0] = 100; g.lfo[1] = 127;
      g.tap = 0.4; g.flurry = 0.3;
      g.stutterEvery = chance(0.5) ? R(6, 14) : 0;
      break;
    case K_SWARM:     // the hyper-LFO at rhythm rates: stepped, glitching sequences
      p.fa = R(82, 100); p.fb = R(72, 100); p.link = chance(0.7); p.andor = chance(0.5);
      for (int i = 0; i < 4; ++i) { p.source[i] = chance(0.6) ? 2 : 0; p.mod[i] = R(80, 106); }
      p.source[r.next() % 2] = 2;
      p.quantize = chance(0.6);
      p.time1 = R(75, 100); p.time2 = R(70, 98);
      d.held[2] = d.held[3] = true;
      g.turnLfo = 3; g.turnFm = 1; g.turnSource = 1; g.turnRing = 0.3; g.lfo[0] = 75; g.lfo[1] = 104;
      g.hold = 0.6; g.chord = 0.2; g.holdLength[0] = 3; g.holdLength[1] = 9;
      g.stutterEvery = chance(0.4) ? R(8, 16) : 0;
      break;
    case K_WASTELAND: // fast sharp touches in a ring of FM, short chirping echoes
      for (int i = 0; i < 4; ++i) { p.fast[i] = 1; p.sharp[i] = R(60, 110); p.mod[i] = R(84, 106); }
      p.switchRing = 1; p.time1 = R(40, 80); p.time2 = R(35, 75); p.feedback = R(60, 76);
      p.delMod = chance(0.6) ? 0 : 2; p.drv = R(40, 85); p.dstMix = R(20, 60);
      d.held[0] = d.held[1] = false; d.held[4] = true;
      g.touchEvery = R(0.4, 1.2); g.hold = 0.1; g.tap = 0.5; g.flurry = 0.4; g.chord = 0;
      g.turnFm = 1; g.turnTime = 1.5; g.turnDrive = 0.5; g.time[0] = 30; g.time[1] = 90;
      break;
    case K_INFERNO:   // the whole instrument feeding back on itself
      p.totalFB = 1;
      for (int i = 0; i < 4; ++i) { p.source[i] = chance(0.6) ? 2 : 0; p.mod[i] = R(70, 96); }
      p.source[r.next() % 4] = 2;
      p.drv = R(50, 95); p.dstMix = R(30, 70); p.feedback = R(55, 74);
      g.turnTotal = 1.5; g.turnFm = 2; g.turnDrive = 1; g.turnSource = 0.5; g.fm[0] = 65; g.fm[1] = 100;
      g.drive[0] = 40; g.drive[1] = 100; g.knobEvery = R(1.5, 4);
      g.stutterEvery = chance(0.4) ? R(6, 14) : 0;
      break;
    case K_STORM:     // a bit of everything, every control turned
      for (int i = 0; i < 4; ++i) { p.source[i] = (int)(r.next() % 3 == 0) * 2; p.mod[i] = R(75, 105); }
      p.delMod = (int)(r.next() % 3); p.fa = R(60, 110); p.fb = R(60, 110);
      g.knobEvery = R(1, 2.5); g.turnFm = 1; g.turnLfo = 1; g.turnSource = 0.7; g.turnTotal = 0.4;
      g.turnTime = 1; g.turnDelMod = 0.5; g.turnDrive = 0.5; g.turnRing = 0.5;
      g.flurry = 0.3; g.tap = 0.3; g.hold = 0.3; g.chord = 0.1;
      g.stutterEvery = chance(0.5) ? R(6, 14) : 0;
      break;
  }
  // Each kind brought to about 0.1 RMS as it is played, measured over many
  // seeds of each with the sliders dealt as the die deals them.
  static const double levels[K_COUNT] = {1.06, 1.17, 0.55, 0.48, 0.60, 1.24, 0.59, 0.76};
  d.level = levels[kk];
  return d;
}

/// The pitch slider: every voice moved together, in semitones, an octave
/// either way -- never the sides against each other.
inline double sliderTranspose(double slider) {
  return std::pow(2.0, std::floor((clampd(slider, 0, 1) - 0.5) * 24 + 0.5) / 12);
}

/// The eight sliders, their middles the design as drawn: pitch (below), sharp, fm,
/// hold, lfo, delay, feedback, drive.
inline Params applySliders(const Params &base, const double s[8]) {
  Params p = base;
  auto off = [](double v, double slider, double span) { return clampd(v + (slider - 0.5) * span, 0, 127); };
  // The pitch slider is sliderTranspose, applied by the opcode.
  for (int i = 0; i < 4; ++i) {
    p.sharp[i] = off(base.sharp[i], s[1], 127);
    p.mod[i] = off(base.mod[i], s[2], 70);
  }
  p.hold[0] = off(base.hold[0], s[3], 100);
  p.hold[1] = off(base.hold[1], s[3], 100);
  p.fa = off(base.fa, s[4], 80);
  p.fb = off(base.fb, s[4], 80);
  p.delMix = off(base.delMix, s[5], 127);
  p.feedback = off(base.feedback, s[6], 70);
  p.drv = off(base.drv, s[7], 100);
  p.dstMix = off(base.dstMix, s[7], 127);
  return p;
}

/// The voice as the opcode plays it: the engine, the recipe's controls as a
/// player has turned them, the sliders over them, the sensors as the player
/// touches them, and an ear on what comes out that steps in when it goes
/// silent, still, flat or runs away. The measuring harness plays the same.
class Player {
 public:
  Engine engine;
  Design design;
  double sliders[8] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
  double transpose = 1;
  bool playing = true;
  int mode = 1;                 // 0 clocked, 1 droning, 2 played

  void prepare(int kind, uint64_t designSeed, uint64_t seed, double sr, int mode_, bool playing_) {
    design = vascular::caligula::design(kind, designSeed);
    cur_ = design.p;
    sr_ = sr; mode = mode_; playing = playing_;
    r_.seed(seed ^ 0x51CA11ull);
    if (mode != 0 && playing) for (int k = 0; k < 8; ++k) engine.sensor[k] = design.held[k];
    for (int k = 0; k < 8; ++k) until_[k] = engine.sensor[k] ? 4 + 4 * r_.unit() : 0;
    engine.p = controls();
    engine.prepare(sr, seed);
    nextTouch_ = 0.5 + design.play.touchEvery * r_.unit();
    nextKnob_ = design.play.knobEvery * (0.5 + r_.unit());
    nextStutter_ = design.play.stutterEvery * (0.3 + r_.unit());
  }

  void setPlaying(bool on) {
    if (on == playing) return;
    playing = on;
    for (int k = 0; k < 8; ++k) { engine.sensor[k] = on && mode != 0 && design.held[k]; until_[k] = engine.sensor[k] ? 3 : 0; }
  }

  /// A step of the clock: a few sensors opened, the last ones closed.
  void strike() {
    for (int k = 0; k < 8; ++k) engine.sensor[k] = false;
    const int count = std::max(1, std::min(8, design.strikeVoices));
    for (int n = 0; n < count; ++n) engine.sensor[r_.next() % 8] = true;
  }

  /// The controls as the player and the sliders have them, and the
  /// transport: stopped, the voice dies away.
  Params controls() const {
    Params p = applySliders(cur_, sliders);
    p.transpose = transpose * sliderTranspose(sliders[0]);
    if (!playing) {
      p.hold[0] = p.hold[1] = 0;
      p.feedback = std::min(p.feedback, 20.0);
      p.totalFB = 0;
    }
    return p;
  }

  /// One block of `n` samples into `out`.
  template <typename T> void render(T *out, int n) {
    const double secs = n / sr_;
    if (playing) { listen(secs); if (mode == 1) touch(secs); turn(secs); }
    engine.p = controls();
    engine.applyParams();
    double e = 0; int z = 0;
    for (int i = 0; i < n; ++i) {
      double y = engine.sample() * design.level * stutter();
      if (!std::isfinite(y)) y = 0;
      out[i] = (T)y;
      e += y * y;
      z += (y >= 0) != (last_ >= 0);
      last_ = y;
    }
    blockDb_ = 10 * std::log10(e / std::max(1, n) + 1e-12);
    blockZcr_ = (double)z / std::max(1, n);
  }

 private:
  Params cur_;
  double sr_ = 48000;
  Rng r_;
  double until_[8] = {};
  double nextTouch_ = 1, nextKnob_ = 3, nextStutter_ = -1;
  double flurryLeft_ = 0, flurryGap_ = 0.1, flurryNext_ = 0;
  double burstLeft_ = 0, burstAll_ = 1, gapPh_ = 0, smooth_ = 1;
  double last_ = 0, blockDb_ = -120, blockZcr_ = 0;
  double slowDb_ = -60, activity_ = 3, zcr_ = 0;
  double quiet_ = 0, still_ = 0, flat_ = 0, loud_ = 0;

  double R(double a, double b) { return a + (b - a) * r_.unit(); }

  void open(int k, double secs) { engine.sensor[k] = true; until_[k] = secs; }

  /// The player's hands on the sensors, droning: holds, taps, flurries,
  /// chords, each sensor let go when its time is up.
  void touch(double secs) {
    const Playing &g = design.play;
    for (int k = 0; k < 8; ++k)
      if (engine.sensor[k] && until_[k] > 0 && (until_[k] -= secs) <= 0) engine.sensor[k] = false;
    if (flurryLeft_ > 0 && (flurryNext_ -= secs) <= 0) {
      open(r_.next() % 8, R(0.04, 0.12));
      flurryNext_ = flurryGap_ * R(0.6, 1.4);
      flurryLeft_ -= 1;
    }
    if ((nextTouch_ -= secs) > 0) return;
    nextTouch_ = g.touchEvery * R(0.4, 1.6);
    gesture();
  }

  void gesture() {
    const Playing &g = design.play;
    const double total = g.hold + g.tap + g.flurry + g.chord;
    double x = r_.unit() * (total > 0 ? total : 1);
    if ((x -= g.hold) < 0) { open(r_.next() % 8, R(g.holdLength[0], g.holdLength[1])); return; }
    if ((x -= g.tap) < 0) { open(r_.next() % 8, R(0.06, 0.3)); return; }
    if ((x -= g.flurry) < 0) { flurryLeft_ = 3 + r_.next() % 6; flurryGap_ = R(0.06, 0.25); flurryNext_ = 0; return; }
    const int n = 2 + r_.next() % 2;
    const double len = R(g.holdLength[0], g.holdLength[1]);
    for (int i = 0; i < n; ++i) open(r_.next() % 8, len);
  }

  /// The player turning a control, now and then: within the places the
  /// recipe says are worth it.
  void turn(double secs) {
    if ((nextKnob_ -= secs) > 0) return;
    const Playing &g = design.play;
    nextKnob_ = g.knobEvery * R(0.4, 1.6);
    knob();
  }

  void knob() {
    const Playing &g = design.play;
    const double w[8] = {g.turnFm, g.turnLfo, g.turnSource, g.turnTotal, g.turnTime, g.turnDelMod, g.turnDrive, g.turnRing};
    double total = 0;
    for (double v : w) total += v;
    if (total <= 0) return;
    double x = r_.unit() * total;
    int which = 0;
    while (which < 7 && (x -= w[which]) >= 0) ++which;
    switch (which) {
      case 0: { const int i = r_.next() % 4; cur_.mod[i] = R(g.fm[0], g.fm[1]); break; }
      case 1: cur_.fa = R(g.lfo[0], g.lfo[1]); if (r_.unit() < 0.5) cur_.fb = R(g.lfo[0], g.lfo[1]);
              if (r_.unit() < 0.2) cur_.link = !cur_.link; if (r_.unit() < 0.2) cur_.andor = !cur_.andor; break;
      case 2: { const int i = r_.next() % 4; cur_.source[i] = cur_.source[i] == 2 ? 0 : 2; break; }
      case 3: cur_.totalFB = !cur_.totalFB; if (cur_.totalFB) cur_.source[r_.next() % 4] = 2; break;
      case 4: if (r_.unit() < 0.5) cur_.time1 = R(g.time[0], g.time[1]); else cur_.time2 = R(g.time[0], g.time[1]); break;
      case 5: cur_.delMod = cur_.delMod == 0 ? 2 : 0; break;
      case 6: cur_.drv = R(g.drive[0], g.drive[1]); break;
      case 7: cur_.switchRing = !cur_.switchRing; break;
    }
  }

  /// The ear: how loud the last blocks were, how much the level moved, how
  /// bright. Silent too long, a touch; still too long, a control turned;
  /// a flat, still, bright wall, the FM, the pulses and the drive taken
  /// down and the total feedback off; running away, the delay taken down.
  void listen(double secs) {
    const double a = 1 - std::exp(-secs / 1.5), b = 1 - std::exp(-secs / 4.0);
    slowDb_ += (blockDb_ - slowDb_) * a;
    activity_ += (std::fabs(blockDb_ - slowDb_) - activity_) * b;
    zcr_ += (blockZcr_ - zcr_) * a;
    quiet_ = blockDb_ < -55 ? quiet_ + secs : 0;
    still_ = activity_ < 0.8 ? still_ + secs : 0;
    flat_ = (zcr_ > 0.12 && activity_ < 1.5) ? flat_ + secs : 0;
    loud_ = slowDb_ > -6 ? loud_ + secs : 0;
    if (quiet_ > 2.5 && mode == 1) { quiet_ = 0; open(r_.next() % 8, R(1.5, 4)); }
    if (still_ > 4) { still_ = 0; knob(); if (mode == 1) gesture(); }
    if (flat_ > 3) {
      flat_ = 0;
      for (int i = 0; i < 4; ++i) { cur_.mod[i] = std::max(60.0, cur_.mod[i] - 15); cur_.sharp[i] = std::max(0.0, cur_.sharp[i] - 25); }
      cur_.drv = std::max(10.0, cur_.drv - 20); cur_.dstMix = std::max(0.0, cur_.dstMix - 15); cur_.totalFB = 0;
    }
    if (loud_ > 2) { loud_ = 0; cur_.feedback = std::max(40.0, cur_.feedback - 8); cur_.drv = std::max(10.0, cur_.drv - 10); }
  }

  /// Now and then, a short stutter: gaps at a few hertz, in and out over a
  /// quarter of a second, each gap's edges a few milliseconds long.
  double stutter() {
    const Playing &g = design.play;
    if (g.stutterEvery <= 0 || !playing) return 1;
    const double dt = 1 / sr_;
    if ((nextStutter_ -= dt) < 0) {
      if (burstLeft_ <= 0) { burstLeft_ = g.stutterLength * R(0.7, 1.3); burstAll_ = burstLeft_; }
      nextStutter_ = g.stutterEvery * R(0.5, 1.5);
    }
    double want = 1;
    if (burstLeft_ > 0) {
      burstLeft_ -= dt;
      const double edge = std::min(1.0, std::min(burstAll_ - burstLeft_, burstLeft_) / 0.25);
      gapPh_ += g.stutterRate * dt; gapPh_ -= std::floor(gapPh_);
      if (gapPh_ > 0.55) want = 1 - g.stutterDepth * std::max(0.0, edge);
    }
    smooth_ += (want - smooth_) * (1 - std::exp(-dt / 0.004));
    return smooth_;
  }
};

}  // namespace caligula
}  // namespace vascular

#endif
