/*
 * anima -- a touch synthesizer played by simulated hands.
 *
 * Up to six fingers move over a virtual surface: they come down, hold,
 * drag, circle, scribble and lift, each with its own pressure. Every finger
 * is a voice -- wavetable oscillators (a pair, an FM oscillator, a stack of
 * up to five, a granular one), a filtered noise, a resonant filter, a
 * wavetable amplitude modulator and an envelope -- and what the finger does
 * moves the voice: where it is, how hard it presses, how fast it travels,
 * how far it is from the middle, how long it has been down. The voices are
 * summed through a downsampler, a waveshaper, a phaser, a second filter and
 * a tuned resonator.
 *
 * A finger takes its note when it comes down and keeps it until it lifts:
 * the motion moves timbre, level and space, never pitch.
 *
 * Everything is drawn from a design (AnimaDesign.h) and a motion seed; the
 * fingers' state is published for the interface to draw.
 */
#ifndef VASCULAR_ANIMA_CORE_H
#define VASCULAR_ANIMA_CORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace vascular {
namespace anima {

template <class T>
inline T clampTo(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kPhi = 1.61803398874989f;
constexpr int kFingers = 6;
constexpr int kTables = 24;
constexpr int kTableSize = 2048;
constexpr int kLevels = 4;

struct Random {
  uint64_t s;
  explicit Random(uint64_t seed = 1) : s(seed * 0x9E3779B97F4A7C15ULL + 0x2545F4914F6CDD1DULL) {}
  uint64_t next() {
    s += 0x9E3779B97F4A7C15ULL;
    uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
  }
  float unit() { return (float)((next() >> 11) * (1.0 / 9007199254740992.0)); }
  float in(float a, float b) { return a + (b - a) * unit(); }
  int below(int n) { return n <= 1 ? 0 : (int)(unit() * n) % n; }
  bool chance(float p) { return unit() < p; }
};

// --- the waveforms ----------------------------------------------------------
//
// Twenty-four single cycles, each built from its harmonics so it can be
// band-limited: four levels, by how many harmonics a pitch leaves room for.

struct Tables {
  float data[kTables][kLevels][kTableSize + 1];

  static const Tables &get() {
    static Tables *t = make();
    return *t;
  }

  /** Harmonic n's amplitude in table k (n from 1). */
  static float amp(int k, int n) {
    const float fn = (float)n;
    switch (k) {
      case 0: return n == 1 ? 1.f : 0.f;                                  // sine
      case 1: return n % 2 ? 1.f / (fn * fn) * ((n / 2) % 2 ? -1.f : 1.f) : 0.f;  // triangle
      case 2: return 1.f / std::pow(fn, 1.4f);                           // soft saw
      case 3: return n % 2 ? 1.f / std::pow(fn, 1.3f) : 0.f;             // soft square
      case 4: return std::sin(kPi * fn * 0.25f) / fn;                    // pulse 25 %
      case 5: {                                                           // organ
        static const float bars[9] = {0, 1, 0.8f, 0.6f, 0.5f, 0, 0.35f, 0, 0.3f};
        return n <= 8 ? bars[n] : 0.f;
      }
      case 6: return n % 2 ? 1.f / std::pow(fn, 1.6f) + (n == 9 ? 0.25f : 0.f) : 0.f;  // glass
      case 7: return std::exp(-std::pow((fn - 3.f) / 1.5f, 2.f)) + 0.6f * std::exp(-std::pow((fn - 8.f) / 2.f, 2.f)) + (n == 1 ? 0.4f : 0.f);  // vowel a
      case 8: case 9: case 10: {                                          // dense, three draws
        Random r(1000 + k * 77 + n);
        return r.in(0.1f, 1.f) / std::pow(fn, 0.8f) * (r.chance(0.5f) ? 1.f : -1.f);
      }
      case 11: {                                                          // noisy cycle
        Random r(5000 + n);
        return n <= 48 ? r.in(0.2f, 1.f) / std::pow(fn, 0.55f) * (r.chance(0.5f) ? 1.f : -1.f) : 0.f;
      }
      case 12: return (n == 1 ? 1.f : n == 3 ? 0.7f : n == 5 ? 0.5f : n == 8 ? 0.45f : n == 12 ? 0.3f : 0.f);  // bell set
      case 13: return 1.f / fn * (n > 4 && n < 9 ? 0.3f : 1.f);           // reed
      case 14: return (n == 2 || n == 4) ? 0.f : 1.f / std::pow(fn, 1.2f); // hollow
      case 15: return n % 3 == 1 ? 1.f / fn : 0.f;                        // comb
      case 16: return n == 1 ? 1.f : n == 3 ? 0.5f : 0.f;                  // sine and fifth
      case 17: return n == 1 ? 1.f : n == 2 ? 0.6f : 0.f;                  // sine and octave
      case 18: return 1.f / std::pow(fn, 0.5f) * (n <= 48 ? 1.f : 0.f);    // bright tilt
      case 19: return std::sin(fn * 1.3f) / std::pow(fn, 0.9f);           // folded
      case 20: return std::exp(-std::pow((fn - 2.f) / 1.2f, 2.f)) + 0.5f * std::exp(-std::pow((fn - 5.f) / 1.5f, 2.f)) + (n == 1 ? 0.5f : 0.f);  // vowel o
      case 21: return (n == 1 ? 0.6f : (n == 7 || n == 11 || n == 13) ? 0.8f : 0.05f / fn);  // metallic
      case 22: {                                                          // breath
        Random r(9000 + n);
        return n >= 6 && n <= 40 ? r.in(0.f, 0.5f) / std::pow(fn, 0.3f) : (n == 1 ? 0.3f : 0.f);
      }
      default: return std::sin(kPi * fn * 0.1f) / fn;                     // pulse 10 %
    }
  }

 private:
  static Tables *make() {
    Tables *t = new Tables();
    static const int harmonics[kLevels] = {96, 32, 10, 3};
    for (int k = 0; k < kTables; ++k) {
      for (int l = 0; l < kLevels; ++l) {
        float *d = t->data[k][l];
        for (int i = 0; i <= kTableSize; ++i) d[i] = 0.f;
        for (int n = 1; n <= harmonics[l]; ++n) {
          float a = amp(k, n);
          if (a == 0.f) continue;
          for (int i = 0; i < kTableSize; ++i)
            d[i] += a * std::sin(kTwoPi * (float)n * (float)i / kTableSize);
        }
        float peak = 0.f;
        for (int i = 0; i < kTableSize; ++i) peak = std::max(peak, std::fabs(d[i]));
        if (peak > 0.f) for (int i = 0; i < kTableSize; ++i) d[i] /= peak;
        d[kTableSize] = d[0];
      }
    }
    return t;
  }
};

/** Table k read at phase p (0..1), at a level the frequency leaves room for. */
inline float read(int k, float p, float hz) {
  const Tables &T = Tables::get();
  int level = hz < 200.f ? 0 : hz < 600.f ? 1 : hz < 2000.f ? 2 : 3;
  const float *d = T.data[clampTo(k, 0, kTables - 1)][level];
  float x = p * kTableSize;
  int i = (int)x;
  float f = x - (float)i;
  i &= kTableSize - 1;
  return d[i] + (d[i + 1] - d[i]) * f;
}

inline float wrap(float p) { return p - std::floor(p); }

// --- what a finger is --------------------------------------------------------

/** The controllers a finger yields, each nought to one. */
enum Source : uint8_t {
  Const, X, Y, Size, Speed, Angle, Distance, Age, GroupX, GroupY, Count, Lfo1, Lfo2, SourceCount
};

/** A parameter: the controller that moves it, over a range, along a slope
 * (-1 lower weighting, 0 linear, 1 upper). */
struct Map {
  uint8_t source = Const;
  float lo = 0.f, hi = 0.f, slope = 0.f;
  float at(const float *c) const {
    float v = clampTo(c[source], 0.f, 1.f);
    if (slope != 0.f) v = std::pow(v, std::exp2(-slope * 1.6f));
    return lo + (hi - lo) * v;
  }
};

enum MotionStyle : uint8_t { Taps, Drags, Orbits, Swarm, Sweeps, Holds };

enum Path : uint8_t { PathHold, PathLine, PathOrbit, PathScribble, PathSweep, PathTap,
                      DroneOrbit, DroneFigure, DroneScribble, DroneGlide };

struct Finger {
  bool down = false;
  float t = 0.f, dur = 1.f;
  uint8_t path = PathHold;
  float x0 = 0.5f, y0 = 0.5f, x1 = 0.5f, y1 = 0.5f;
  float cx = 0.5f, cy = 0.5f, r = 0.2f, w = 1.f, phase = 0.f;
  float nx = 0.f, ny = 0.f, nseed = 0.f;
  float x = 0.5f, y = 0.5f, px = 0.5f, py = 0.5f, size = 0.f, speed = 0.f;
  float pressure = 0.7f;
  int interval = 0;
  bool held = false;              // a drone's finger: down until the voice ends
  bool guest = false;             // a drone's visitor: down a while, dragging, then lifted
  float ax = 0.5f, ay = 0.5f;     // where its current figure began
  float figureAt = 0.f, figureFor = 15.f;
  float tx = 0.f, ty = 0.f;       // where the figure carries its anchor
  float w2 = 1.f, phase2 = 0.f;
};

/** The published state of a node's fingers, for the interface. */
struct Published {
  float x[kFingers], y[kFingers], size[kFingers], down[kFingers];
};

// --- the design ----------------------------------------------------------------

struct Design {
  bool oscA = true, oscB = false, fm = false, multi = false, gran = false;
  int noise = 0;                     // 0 none, 1 white, 2 low-pass, 3 band-pass, 4 high-pass
  int tableA = 0, tableB = 2, tableFM = 0, tableMulti = 2, tableGran = 11, tableAM = 0;
  int intervalB = 7;                 // osc B, whole semitones above A
  float detuneB = 0.f;               // cents, fixed: a chorus, not a wander
  float fmRatio = 2.f;
  bool golden = false;               // the FM and the stack on powers of the golden ratio
  int multiCount = 3;
  float granSpread = 0.f;            // the share of grains scattered off the note
  float multiStep = 12.f;            // semitones between the stacked copies
  int filter = 1;                    // 0 off, 1 low-pass, 2 band-pass, 3 high-pass
  bool ampMod = false;
  bool down = false, shaper = false, phaser = false, resonator = false;
  bool phaserByHand = false;         // the sweep placed by a gesture, not its LFO
  int groupFilter = 0;
  int shaperCurve = 0;
  float att = 20.f, hold = 0.f, dec = 400.f, sus = 0.7f, rel = 900.f;   // ms
  Map levelA, levelB, levelFM, levelMulti, levelGran, levelNoise;
  Map fmIndex, granRate, granLen, noiseCut, cutoff, q, amRate, amDepth, amp;
  Map downRate, shaperDrive, phaserRate, phaserDepth, groupCut, groupQ, resFeedback, resMix;
  Map phaserPlace;
  float lfo1 = 0.2f, lfo2 = 0.07f;   // Hz
  int lfo1Shape = 0, lfo2Shape = 1;  // 0 sine, 1 triangle, 2 smooth random
  int intervals[6] = {0, 0, 0, 0, 0, 0};
  int intervalCount = 1;
  int octave = 0;
  // motion
  MotionStyle motion = Drags;
  int fingers = 3;
  float spawn = 0.4f;                // touches per second while droning
  float durMin = 2.f, durMax = 6.f;  // seconds a touch lasts
  float travel = 0.15f;              // surface units per second
  float out = 0.3f;
};

// --- one voice ---------------------------------------------------------------

struct Svf {
  float ic1 = 0.f, ic2 = 0.f;
  float lastHz=-1,lastQ=-1,lastSr=-1,cachedG=0,cachedK=0;
  /** Low, band and high from one zero-delay state-variable step. */
  void run(float x, float g, float k, float &lp, float &bp, float &hp) {
    float a1 = 1.f / (1.f + g * (g + k));
    float v3 = x - ic2;
    float v1 = a1 * ic1 + g * a1 * v3;
    float v2 = ic2 + g * v1;
    ic1 = 2.f * v1 - ic1;
    ic2 = 2.f * v2 - ic2;
    lp = v2; bp = v1; hp = x - k * v1 - v2;
  }
  float filter(int type, float x, float hz, float q, float sr) {
    if(hz!=lastHz||q!=lastQ||sr!=lastSr){lastHz=hz;lastQ=q;lastSr=sr;cachedG=std::tan(kPi*clampTo(hz,20.f,sr*.45f)/sr);cachedK=1.f/clampTo(q,.5f,25.f);}
    float g=cachedG,k=cachedK;
    float lp, bp, hp;
    run(x, g, k, lp, bp, hp);
    return type == 1 ? lp : type == 2 ? bp : hp;
  }
};

struct Envelope {
  enum St { Idle, Attack, Hold, Decay, Sustain, Release } st = Idle;
  float v = 0.f, t = 0.f;
  float lastDec=-1,lastRel=-1,lastDt=-1,decCoeff=0,relCoeff=0;
  void on() { st = Attack; t = 0.f; }
  void off() { if (st != Idle) st = Release; }
  float tick(const Design &d, float dt) {
    if(d.dec!=lastDec||d.rel!=lastRel||dt!=lastDt){lastDec=d.dec;lastRel=d.rel;lastDt=dt;decCoeff=1.f-std::exp(-dt*4600.f/std::max(d.dec,1.f));relCoeff=std::exp(-dt*4600.f/std::max(d.rel,1.f));}
    switch (st) {
      case Attack: v += dt * 1000.f / std::max(d.att, 1.f); if (v >= 1.f) { v = 1.f; st = Hold; t = 0.f; } break;
      case Hold: t += dt; if (t * 1000.f >= d.hold) st = Decay; break;
      case Decay: v += (d.sus - v) * decCoeff; if (std::fabs(v - d.sus) < 1e-3f) st = Sustain; break;
      case Sustain: v = d.sus; break;
      case Release: v *= relCoeff; if (v < 1e-4f) { v = 0.f; st = Idle; } break;
      case Idle: v = 0.f; break;
    }
    return v;
  }
};

struct Grain { float phase = 0.f, pos = 0.f, len = 0.f, ratio = 1.f; bool on = false; };

struct Voice {
  float hz = 220.f;
  float pA = 0.f, pB = 0.f, pC = 0.f, pM = 0.f, pAM = 0.f;
  float pStack[5] = {};
  Grain grains[4];
  float grainClock = 0.f;
  Svf filter, noiseFilter;
  Envelope env;
  Random rand{7};
  // controller-rate values
  float lA = 0, lB = 0, lFM = 0, lMulti = 0, lGran = 0, lNoise = 0;
  float index = 0, gRate = 10, gLen = 0.05f, nCut = 2000, cut = 4000, q = 0.7f, amRate = 2, amDepth = 0, amp = 0;

  float lastHz=-1,lastB=0,lastDetune=0,lastStep=0,lastRatio=0;
  bool lastGolden=false;int lastCount=0;float bHz=0,ratio=0,stackHz[5]{},stackGain=1;
  float tick(const Design &d, float sr) {
    if(hz!=lastHz||d.intervalB!=lastB||d.detuneB!=lastDetune||d.multiStep!=lastStep||d.fmRatio!=lastRatio||d.golden!=lastGolden||d.multiCount!=lastCount){
      lastHz=hz;lastB=d.intervalB;lastDetune=d.detuneB;lastStep=d.multiStep;lastRatio=d.fmRatio;lastGolden=d.golden;lastCount=d.multiCount;
      bHz=hz*std::exp2((d.intervalB+d.detuneB*.01f)/12.f);ratio=d.golden?kPhi:d.fmRatio;
      for(int i=0;i<5;i++)stackHz[i]=d.golden?hz*std::pow(kPhi,float(i)*.5f):hz*std::exp2(d.multiStep*float(i)/12.f);
      stackGain=std::sqrt(float(clampTo(d.multiCount,1,5)));
    }
    if (env.st == Envelope::Idle) return 0.f;
    float s = 0.f;
    const float inc = hz / sr;
    if (d.oscA) { pA = wrap(pA + inc); s += lA * read(d.tableA, pA, hz); }
    if (d.oscB) {
      float hb = bHz;
      pB = wrap(pB + hb / sr); s += lB * read(d.tableB, pB, hb);
    }
    if (d.fm) {

      pM = wrap(pM + inc * ratio);
      float mod = std::sin(kTwoPi * pM) * index;
      pC = wrap(pC + inc);
      s += lFM * read(d.tableFM, wrap(pC + mod * 0.159155f), hz * (1.f + index * ratio));
    }
    if (d.multi) {
      float sum = 0.f;
      int n = clampTo(d.multiCount, 1, 5);
      for (int i = 0; i < n; ++i) {
        float f = stackHz[i];
        pStack[i] = wrap(pStack[i] + f / sr);
        sum += read(d.tableMulti, pStack[i], f) * (1.f / (1.f + 0.35f * i));
      }
      s += lMulti * sum / stackGain;
    }
    if (d.gran && lGran > 0.f) {
      grainClock += gRate / sr;
      if (grainClock >= 1.f) {
        grainClock -= 1.f;
        for (auto &g : grains) if (!g.on) {
          g.on = true; g.pos = 0.f; g.len = std::max(gLen, 0.004f); g.phase = rand.unit();
          // Scattered, when the design asks, onto fixed octaves and a fifth:
          // each grain holds its pitch from start to end.
          static const float scatter[4] = {2.f, 0.5f, 1.5f, 4.f};
          g.ratio = rand.unit() < d.granSpread ? scatter[(int)(rand.unit() * 3.999f)] : 1.f;
          break;
        }
      }
      float gs = 0.f;
      for (auto &g : grains) {
        if (!g.on) continue;
        float w = 0.5f - 0.5f * std::cos(kTwoPi * g.pos);
        g.phase = wrap(g.phase + inc * g.ratio);
        gs += w * read(d.tableGran, g.phase, hz * g.ratio);
        g.pos += 1.f / (g.len * sr);
        if (g.pos >= 1.f) g.on = false;
      }
      s += lGran * gs * 0.6f;
    }
    if (d.noise > 0 && lNoise > 0.f) {
      float n = rand.unit() * 2.f - 1.f;
      if (d.noise > 1) n = noiseFilter.filter(d.noise - 1, n, nCut, 1.2f, sr) * (d.noise == 3 ? 2.f : 1.f);
      s += lNoise * n;
    }
    if (d.filter > 0) s = filter.filter(d.filter, s, cut, q, sr);
    if (d.ampMod) {
      pAM = wrap(pAM + amRate / sr);
      s *= 1.f - amDepth * (0.5f - 0.5f * read(d.tableAM, pAM, amRate));
    }
    return s * amp;
  }
};

// --- the engine ----------------------------------------------------------------

class Engine {
 public:
  void init(const Design &d, uint64_t motionSeed, float sr, float baseHz) {
    d_ = d; sr_ = sr; base_ = baseHz;
    motion_ = Random(motionSeed);
    for (int i = 0; i < kFingers; ++i) voices_[i].rand = Random(motionSeed * 31 + i);
    spawnClock_ = 0.02f;  // the first touch as the voice starts
    std::memset(&pub_, 0, sizeof(pub_));
  }

  Design &design() { return d_; }
  void setNote(float hz){base_=hz;}
  void setMotionScale(float s) { motionScale_ = s; }
  void setDensity(float s) { density_ = s; }

  /** A touch, now: what a clock's step or a controller's note asks for. */
  void strike() {
    spawn();
    // Dense hands now and then put a second finger down with the first.
    if (density_ > 1.f && motion_.chance((density_ - 1.f) * 0.45f)) spawn();
  }
  void autonomous(bool on) { autonomous_ = on; }
  void release() { autonomous_=false;for(int i=0;i<kFingers;i++){fingers_[i].down=false;voices_[i].env.off();} }

  const Published &published() const { return pub_; }

  void process(float *out, int n) {
    for (int i = 0; i < n; ++i) {
      if (controlCount_ <= 0) { control(16.f / sr_); controlCount_ = 16; }
      --controlCount_;
      float s = 0.f;
      for (int v = 0; v < kFingers; ++v) {
        Voice &voice = voices_[v];
        float e = voice.env.tick(d_, 1.f / sr_);
        if (voice.env.st == Envelope::Idle) continue;
        s += voice.tick(d_, sr_) * e;
      }
      float y = group(s) * d_.out;
      // A slow level: designs drawn this freely differ by more than a kind's
      // gain can even out, so the output is brought towards one level over
      // seconds, never more than nine decibels either way -- slow enough that
      // a finger pressing harder is still heard pressing harder.
      power_ += (y * y - power_) * (1.f / (sr_ * 3.f));
      if (power_ > 1e-8f) {
        float want = clampTo(0.1f / std::sqrt(power_), 0.35f, 2.8f);
        level_ += (want - level_) * (1.f / (sr_ * 2.f));
      }
      // and a soft ceiling for a resonance pushed into its peak.
      out[i] = 0.9f * std::tanh(y * level_ * (1.f / 0.9f));
    }
  }

 private:
  // -- the hands --
  void spawn() {
    int slot = -1;
    for (int i = 0; i < kFingers; ++i)
      if (!fingers_[i].down && voices_[i].env.st == Envelope::Idle) { slot = i; break; }
    if (slot < 0) for (int i = 0; i < kFingers; ++i) if (!fingers_[i].down) { slot = i; break; }
    if (slot < 0) return;
    int allowed = clampTo((int)std::lround(d_.fingers * density_), 1, kFingers);
    int downCount = 0;
    for (auto &f : fingers_) downCount += f.down;
    if (downCount >= allowed) return;
    Finger &f = fingers_[slot];
    Random &r = motion_;
    f = Finger();
    f.down = true;
    f.t = 0.f;
    f.dur = r.in(d_.durMin, d_.durMax);
    f.x0 = r.in(0.1f, 0.9f); f.y0 = r.in(0.1f, 0.9f);
    f.pressure = r.in(0.45f, 1.f);
    f.nseed = r.in(0.f, 100.f);
    uint8_t style = d_.motion;
    switch (style) {
      case Taps: f.path = r.chance(0.7f) ? PathTap : PathHold; f.dur = r.in(0.15f, 0.6f); break;
      case Drags: f.path = r.chance(0.7f) ? PathLine : PathScribble; break;
      case Orbits: f.path = PathOrbit; break;
      case Swarm: f.path = r.chance(0.5f) ? PathScribble : PathOrbit; break;
      case Sweeps: f.path = PathSweep; break;
      default: f.path = r.chance(0.8f) ? PathHold : PathLine; break;
    }
    float reach = d_.travel * f.dur;
    float a = r.in(0.f, kTwoPi);
    f.x1 = clampTo(f.x0 + std::cos(a) * reach, 0.05f, 0.95f);
    f.y1 = clampTo(f.y0 + std::sin(a) * reach, 0.05f, 0.95f);
    f.cx = f.x0; f.cy = f.y0;
    f.r = r.in(0.05f, 0.25f);
    f.w = (r.chance(0.5f) ? 1.f : -1.f) * d_.travel / std::max(f.r, 0.05f);
    f.phase = r.in(0.f, kTwoPi);
    f.x = f.px = f.x0; f.y = f.py = f.y0;
    f.interval = d_.intervals[r.below(clampTo(d_.intervalCount, 1, 6))] + 12 * d_.octave;
    Voice &v = voices_[slot];
    v.hz = base_ * std::exp2((float)f.interval / 12.f);
    v.env.on();
  }

  /** A drone's finger: down for good, dragging one figure after another. */
  void spawnHeld(int slot, int order, bool guest = false) {
    Finger &f = fingers_[slot];
    Random &r = motion_;
    f = Finger();
    f.down = true; f.held = !guest; f.guest = guest;
    f.dur = guest ? r.in(3.f, 15.f) : 1e9f;
    f.x0 = f.x = f.px = f.ax = r.in(0.1f, 0.9f);
    f.y0 = f.y = f.py = f.ay = r.in(0.1f, 0.9f);
    f.pressure = r.in(0.55f, 0.9f);
    f.nseed = r.in(0.f, 100.f);
    newFigure(f);
    // The held finger is the node's note; a guest any of the design's.
    f.interval = (order == 0 ? 0 : d_.intervals[r.below(clampTo(d_.intervalCount, 1, 6))]) + 12 * d_.octave;
    Voice &v = voices_[slot];
    v.hz = base_ * std::exp2((float)f.interval / 12.f);
    v.env.on();
  }

  /** The next figure a held finger drags, begun from where it is now, so it
   * never jumps. */
  void newFigure(Finger &f) {
    Random &r = motion_;
    f.ax = f.x; f.ay = f.y;
    f.figureAt = f.t;
    const uint8_t figures[4] = {DroneOrbit, DroneFigure, DroneScribble, DroneGlide};
    f.path = figures[r.below(4)];
    // Most figures sweep a good part of the surface over several seconds;
    // one in five is a flick, small and quick, over in a moment.
    const bool flick = r.chance(0.2f);
    f.figureFor = flick ? r.in(1.5f, 4.f) : r.in(6.f, 18.f);
    f.r = flick ? r.in(0.08f, 0.18f) : r.in(0.16f, 0.4f);
    // A drone's hand moves like a hand: a sweep crosses the surface in a
    // few seconds whatever the design's pace for its struck fingers.
    float speed = std::max(d_.travel, 0.12f) * (flick ? r.in(5.f, 9.f) : r.in(1.6f, 3.2f));
    f.w = (r.chance(0.5f) ? 1.f : -1.f) * speed / f.r;
    f.w2 = f.w * (r.chance(0.5f) ? 2.f : 1.5f);
    f.phase = r.in(0.f, kTwoPi);
    f.phase2 = r.in(0.f, kTwoPi);
    float a = r.in(0.f, kTwoPi);
    f.x1 = std::cos(a); f.y1 = std::sin(a);
    // And the figure carries its anchor somewhere else on the surface as
    // it goes, so the hand travels rather than circling one spot.
    f.tx = r.in(0.15f, 0.85f) - f.ax; f.ty = r.in(0.15f, 0.85f) - f.ay;
    if (flick) f.tx = f.ty = 0.f;
  }

  void moveHeld(Finger &f, float dt) {
    f.t += dt * motionScale_;
    float u = f.t - f.figureAt;
    float ox = 0.f, oy = 0.f;
    switch (f.path) {
      case DroneOrbit:
        ox = f.r * (std::cos(f.phase + f.w * u) - std::cos(f.phase));
        oy = f.r * (std::sin(f.phase + f.w * u) - std::sin(f.phase));
        break;
      case DroneFigure:
        ox = f.r * (std::sin(f.w * u + f.phase) - std::sin(f.phase));
        oy = f.r * 0.7f * (std::sin(f.w2 * u + f.phase2) - std::sin(f.phase2));
        break;
      case DroneScribble: {
        float k = std::max(d_.travel, 0.04f) * 5.f;
        ox = (noise1(u * k, f.nseed) - noise1(0.f, f.nseed)) * 0.25f;
        oy = (noise1(u * k, f.nseed + 40.f) - noise1(0.f, f.nseed + 40.f)) * 0.25f;
        break;
      }
      default: {   // a slow slide back and forth
        float a = f.r * 1.5f * std::sin(f.w * u * 0.5f);
        ox = f.x1 * a; oy = f.y1 * a;
        break;
      }
    }
    // Eased in from where the figure began.
    float ease = clampTo(u / 1.f, 0.f, 1.f);
    ease = ease * ease * (3.f - 2.f * ease);
    float carry = clampTo(u / f.figureFor, 0.f, 1.f);
    carry = carry * carry * (3.f - 2.f * carry);
    f.x = clampTo(f.ax + f.tx * carry + ox * ease, 0.03f, 0.97f);
    f.y = clampTo(f.ay + f.ty * carry + oy * ease, 0.03f, 0.97f);
    float fade = f.guest ? clampTo((f.dur - f.t) / 1.f, 0.f, 1.f) : 1.f;
    // Pressing harder and easing off in slow swells.
    f.size = f.pressure * (0.6f + 0.4f * std::sin(f.t * 0.45f + f.nseed)) * clampTo(f.t / 0.5f, 0.f, 1.f) * fade;
    float dx = f.x - f.px, dy = f.y - f.py;
    float sp = std::sqrt(dx * dx + dy * dy) / std::max(dt, 1e-4f);
    f.speed += (clampTo(sp / 0.3f, 0.f, 1.f) - f.speed) * 0.05f;
    f.px = f.x; f.py = f.y;
    if (u >= f.figureFor) { f.x = clampTo(f.x, 0.03f, 0.97f); newFigure(f); }
    if (f.guest && f.t >= f.dur) { f.down = false; f.guest = false; }
  }

  float noise1(float t, float seed) {
    return std::sin(t * 1.3f + seed) * 0.6f + std::sin(t * 2.9f + seed * 1.7f) * 0.3f + std::sin(t * 6.1f + seed * 0.3f) * 0.1f;
  }

  void moveFinger(Finger &f, float dt) {
    f.t += dt * motionScale_;
    float u = clampTo(f.t / f.dur, 0.f, 1.f);
    float ease = u * u * (3.f - 2.f * u);
    switch (f.path) {
      case PathLine: f.x = f.x0 + (f.x1 - f.x0) * ease; f.y = f.y0 + (f.y1 - f.y0) * ease; break;
      case PathOrbit: {
        float a = f.phase + f.w * f.t;
        f.x = clampTo(f.cx + std::cos(a) * f.r, 0.02f, 0.98f);
        f.y = clampTo(f.cy + std::sin(a) * f.r, 0.02f, 0.98f);
        break;
      }
      case PathScribble:
        f.x = clampTo(f.x0 + noise1(f.t * d_.travel * 6.f, f.nseed) * 0.2f, 0.02f, 0.98f);
        f.y = clampTo(f.y0 + noise1(f.t * d_.travel * 6.f, f.nseed + 40.f) * 0.2f, 0.02f, 0.98f);
        break;
      case PathSweep: f.x = f.x0 + (f.x1 - f.x0) * u; f.y = f.y0 + 0.08f * std::sin(kTwoPi * u * 2.f + f.phase); break;
      case PathTap: break;
      default:
        f.x = f.x0 + 0.01f * std::sin(f.t * 3.1f + f.nseed);
        f.y = f.y0 + 0.01f * std::cos(f.t * 2.7f + f.nseed);
        break;
    }
    // Pressure: pressed in, held with a breath in it, eased off at the end.
    float in = clampTo(f.t / 0.08f, 0.f, 1.f), outFade = clampTo((f.dur - f.t) / 0.15f, 0.f, 1.f);
    f.size = f.pressure * in * outFade * (0.85f + 0.15f * std::sin(f.t * 2.2f + f.nseed));
    float dx = f.x - f.px, dy = f.y - f.py;
    float sp = std::sqrt(dx * dx + dy * dy) / std::max(dt, 1e-4f);
    f.speed += (clampTo(sp / 0.6f, 0.f, 1.f) - f.speed) * 0.2f;
    f.px = f.x; f.py = f.y;
    if (f.t >= f.dur) f.down = false;
  }

  void control(float dt) {
    if (autonomous_) {
      // A drone: one finger always down, dragged, never lifted -- and now
      // and then a guest or two, down a while and lifted again, on a clock
      // that will not keep time: each wait drawn from an average that is
      // itself drawn again every time.
      bool anchored = false;
      for (auto &f : fingers_) anchored |= f.held && f.down;
      if (!anchored) {
        for (int i = 0; i < kFingers; ++i)
          if (!fingers_[i].down && voices_[i].env.st == Envelope::Idle) { spawnHeld(i, 0); break; }
      }
      // Now and then the hand moves on: a finger lands somewhere else and
      // the one that was holding lifts a moment later -- one is always down,
      // and the sound shifts at once to where the new one is.
      handover_ -= dt * motionScale_;
      if (handover_ <= 0.f) {
        for (int i = 0; i < kFingers; ++i)
          if (!fingers_[i].down && voices_[i].env.st == Envelope::Idle) {
            for (auto &f : fingers_) if (f.held && f.down) { f.held = false; f.guest = true; f.dur = f.t + 1.2f; }
            spawnHeld(i, 0);
            break;
          }
        handover_ = -18.f * motion_.in(0.4f, 2.f) * std::log(std::max(motion_.unit(), 1e-3f));
      }
      int guests = 0;
      for (auto &f : fingers_) guests += f.guest && f.down;
      int maxGuests = clampTo((int)std::lround(density_ * 1.6f - 0.3f), 0, 3);
      guestClock_ -= dt * motionScale_;
      if (guestClock_ <= 0.f) {
        if (guests < maxGuests) {
          for (int i = 0; i < kFingers; ++i)
            if (!fingers_[i].down && voices_[i].env.st == Envelope::Idle) { spawnHeld(i, 1, true); break; }
        }
        float mean = 10.f / (std::max(density_, 0.2f) * std::max(density_, 0.2f)) * motion_.in(0.35f, 2.5f);
        guestClock_ = -mean * std::log(std::max(motion_.unit(), 1e-4f));
      }
    }
    lfoPhase1_ = wrap(lfoPhase1_ + dt * d_.lfo1 * motionScale_);
    lfoPhase2_ = wrap(lfoPhase2_ + dt * d_.lfo2 * motionScale_);
    float l1 = lfoShape(d_.lfo1Shape, lfoPhase1_, 1);
    float l2 = lfoShape(d_.lfo2Shape, lfoPhase2_, 2);
    float gx = 0.f, gy = 0.f; int count = 0;
    for (auto &f : fingers_) if (f.down) { gx += f.x; gy += f.y; ++count; }
    if (count > 0) { gx /= count; gy /= count; } else { gx = gy = 0.5f; }
    // How many fingers are down: one is none of it, four are all of it,
    // reached in a few milliseconds -- a finger landing is heard landing.
    countSmooth_ += (clampTo((count - 1) / 3.f, 0.f, 1.f) - countSmooth_) * 0.03f;
    const float countNow = countSmooth_;
    float hardest = 0.f, fastest = 0.f;
    for (auto &f : fingers_) if (f.down) { hardest = std::max(hardest, f.size); fastest = std::max(fastest, f.speed); }
    for (int i = 0; i < kFingers; ++i) {
      Finger &f = fingers_[i];
      Voice &v = voices_[i];
      bool was = f.down;
      // A drone's fingers, held or visiting, drag its figures.
      if (f.down) { if (f.held || f.guest) moveHeld(f, dt); else moveFinger(f, dt); }
      if (was && !f.down) v.env.off();
      float c[SourceCount];
      c[Const] = 1.f; c[X] = f.x; c[Y] = 1.f - f.y; c[Size] = f.size; c[Speed] = f.speed;
      c[Angle] = (std::atan2(f.y - 0.5f, f.x - 0.5f) + kPi) / kTwoPi;
      c[Distance] = clampTo(std::sqrt((f.x - 0.5f) * (f.x - 0.5f) + (f.y - 0.5f) * (f.y - 0.5f)) / 0.7071f, 0.f, 1.f);
      c[Age] = f.held ? 0.5f - 0.5f * std::cos(kTwoPi * f.t / 23.f)
                      : clampTo(f.t / std::max(f.dur, 0.01f), 0.f, 1.f);
      c[GroupX] = gx; c[GroupY] = 1.f - gy; c[Count] = countNow;
      c[Lfo1] = l1; c[Lfo2] = l2;
      v.lA = d_.levelA.at(c); v.lB = d_.levelB.at(c); v.lFM = d_.levelFM.at(c);
      v.lMulti = d_.levelMulti.at(c); v.lGran = d_.levelGran.at(c); v.lNoise = d_.levelNoise.at(c);
      v.index = d_.fmIndex.at(c); v.gRate = d_.granRate.at(c); v.gLen = d_.granLen.at(c) * 0.001f;
      v.nCut = d_.noiseCut.at(c); v.cut = d_.cutoff.at(c); v.q = d_.q.at(c);
      v.amRate = d_.amRate.at(c); v.amDepth = d_.amDepth.at(c);
      // A lifted finger's voice keeps the level it had while it rings out.
      if (f.down) v.amp = d_.amp.at(c);
      pub_.x[i] = f.x; pub_.y[i] = f.y; pub_.size[i] = f.down ? f.size : 0.f; pub_.down[i] = f.down ? 1.f : 0.f;
      if (i == 0) { groupC_[0] = c[GroupX]; groupC_[1] = c[GroupY]; groupC_[2] = c[Count]; groupC_[3] = l1; groupC_[4] = l2; }
    }
    float gc[SourceCount];
    // The whole hand: where it is, its hardest press and its quickest finger.
    gc[Const] = 1.f; gc[X] = groupC_[0]; gc[Y] = groupC_[1]; gc[Size] = hardest; gc[Speed] = fastest;
    gc[Angle] = 0.5f; gc[Distance] = 0.5f; gc[Age] = 0.5f;
    gc[GroupX] = groupC_[0]; gc[GroupY] = groupC_[1]; gc[Count] = groupC_[2]; gc[Lfo1] = groupC_[3]; gc[Lfo2] = groupC_[4];
    downRate_ = d_.downRate.at(gc); drive_ = d_.shaperDrive.at(gc);
    phRate_ = d_.phaserRate.at(gc); phDepth_ = d_.phaserDepth.at(gc); phPlace_ = d_.phaserPlace.at(gc);
    gCut_ = d_.groupCut.at(gc); gQ_ = d_.groupQ.at(gc);
    resFb_ = d_.resFeedback.at(gc); resMix_ = d_.resMix.at(gc);
  }

  float lfoShape(int shape, float p, int which) {
    if (shape == 0) return 0.5f + 0.5f * std::sin(kTwoPi * p);
    if (shape == 1) return p < 0.5f ? 2.f * p : 2.f - 2.f * p;
    float &held = which == 1 ? held1_ : held2_;
    float &last = which == 1 ? last1_ : last2_;
    if (p < last) held = motion_.unit();
    last = p;
    float &smooth = which == 1 ? smooth1_ : smooth2_;
    smooth += (held - smooth) * 0.02f;
    return smooth;
  }

  // -- the hands' mix --
  float group(float x) {
    if (d_.down) {
      holdCount_ += downRate_ / sr_;
      if (holdCount_ >= 1.f) { holdCount_ -= 1.f; held_ = x; }
      x = held_;
    }
    if (d_.shaper) {
      float y = x * drive_;
      switch (d_.shaperCurve) {
        case 1: y = std::sin(clampTo(y, -4.f, 4.f) * 1.2f); break;              // folding
        case 2: { float z = clampTo(y, -1.f, 1.f); y = 4.f * z * z * z - 3.f * z; break; }  // third-order
        default: y = std::tanh(y); break;
      }
      x = y / std::max(1.f, drive_ * 0.6f);
    }
    if (d_.phaser) {
      phPhase_ = wrap(phPhase_ + phRate_ / sr_);
      phPlaceSmooth_ += (phPlace_ - phPlaceSmooth_) * (1.f / (sr_ * 0.05f));
      float sweep = d_.phaserByHand ? phPlaceSmooth_ : 0.5f + 0.5f * std::sin(kTwoPi * phPhase_);
      float hz = std::min(300.f * std::exp2(sweep * 3.5f * clampTo(phDepth_, 0.f, 1.f)), sr_ * 0.4f);
      float tanValue=std::tan(kPi*hz/sr_);float a=(1.f-tanValue)/(1.f+tanValue);
      float y = x + phFb_ * 0.5f;
      for (int s = 0; s < 4; ++s) {
        float o = -a * y + ap_[s];
        ap_[s] = y + a * o;
        y = o;
      }
      phFb_ = y;
      x = 0.6f * x + 0.6f * y;
    }
    if (d_.groupFilter > 0) x = gFilter_.filter(d_.groupFilter, x, gCut_, gQ_, sr_);
    if (d_.resonator) {
      float delay = clampTo(sr_ / std::max(base_, 30.f), 2.f, 4000.f);
      float rp = (float)resPos_ - delay;
      while (rp < 0.f) rp += 4096.f;
      int i0 = (int)rp; float fr = rp - (float)i0;
      float d0 = res_[i0 & 4095], d1 = res_[(i0 + 1) & 4095];
      float y = d0 + (d1 - d0) * fr;
      resLp_ += (y - resLp_) * 0.5f;
      res_[resPos_ & 4095] = x + resLp_ * resFb_;
      resPos_ = (resPos_ + 1) & 4095;
      x = x * (1.f - resMix_) + y * resMix_;
    }
    return x;
  }

  Design d_;
  float sr_ = 48000.f, base_ = 220.f;
  Random motion_{1};
  Finger fingers_[kFingers];
  Voice voices_[kFingers];
  Published pub_;
  int controlCount_ = 0;
  bool autonomous_ = true;
  float spawnClock_ = 0.6f, motionScale_ = 1.f, density_ = 1.f, guestClock_ = 6.f;
  float power_ = 0.01f, level_ = 1.f, countSmooth_ = 0.f, handover_ = 20.f;
  float lfoPhase1_ = 0.f, lfoPhase2_ = 0.f, held1_ = 0.5f, held2_ = 0.5f, last1_ = 0.f, last2_ = 0.f;
  float smooth1_ = 0.5f, smooth2_ = 0.5f;
  float groupC_[5] = {0.5f, 0.5f, 0.f, 0.5f, 0.5f};
  float downRate_ = 48000.f, drive_ = 1.f, phRate_ = 0.3f, phDepth_ = 0.5f, gCut_ = 8000.f, gQ_ = 0.7f;
  float resFb_ = 0.5f, resMix_ = 0.f;
  float holdCount_ = 0.f, held_ = 0.f;
  float ap_[4] = {}, phPhase_ = 0.f, phFb_ = 0.f, phPlace_ = 0.5f, phPlaceSmooth_ = 0.5f;
  Svf gFilter_;
  float res_[4096] = {};
  int resPos_ = 0;
  float resLp_ = 0.f;
};

}  // namespace anima
}  // namespace vascular

#endif
