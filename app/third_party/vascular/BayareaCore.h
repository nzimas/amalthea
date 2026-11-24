/*
 * bayarea -- the second macrosynth: one West Coast voice.
 *
 * A sine, triangle, square and saw morphing oscillator with two linear FM
 * modulators, a wavefolder, a 12 kHz smoothing filter, then either a
 * low-pass gate (filter and amplifier struck together, one shot) or a
 * sustaining envelope over two resonant low-passes, a saturating output
 * stage, and a reverb: a shelved, saturated, predelayed comb-and-allpass
 * reverb with a limited bank of three resonators beside it. One LFO moves
 * eight destinations.
 *
 * Written from Docs/bayarea/SPEC.md -- see Docs/bayarea/PROVENANCE.md. The
 * section numbers below are that document's.
 *
 * Everything that is control rate there is control rate here: the LFO, the
 * smoothers, the partial attenuation and the filter coefficients update every
 * 64 samples, and control values reaching audio-rate arithmetic are
 * interpolated across the block.
 *
 * Header-only, so the Mac harness (Scripts/bayarea-harness.cpp) runs the same
 * code the opcode does.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include "RtBuffer.h"

namespace vascular {
namespace bayarea {

static const int kBlock = 64;
static const double kTwoPi = 6.283185307179586;

inline double clampd(double x, double a, double b) { return x < a ? a : (x > b ? b : x); }
inline double linlin(double x, double a, double b, double c, double d) {
  return clampd(c + (x - a) / (b - a) * (d - c), c < d ? c : d, c < d ? d : c);
}
inline double midiratio(double s) { return std::pow(2.0, s / 12.0); }

/// A small, fast generator of uniform numbers, seeded per voice.
struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  void seed(uint64_t v) {
    s = v * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
    if (!s) s = 1;
    for (int i = 0; i < 4; ++i) next();
  }
  uint64_t next() {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
  }
  double unit() { return (next() >> 11) * (1.0 / 9007199254740992.0); }  // [0, 1)
  double bipolar() { return unit() * 2.0 - 1.0; }
};

/// 3.4: a one-pole smoother reaching -60 dB of a step in T seconds,
/// evaluated once per control block.
struct Lag {
  double y = 0;
  bool primed = false;
  double tick(double x, double T, double kr) {
    if (!primed) { y = x; primed = true; return y; }
    if (T <= 0) { y = x; return y; }
    const double b = std::exp(std::log(0.001) / (T * kr));
    y += (1 - b) * (x - y);
    return y;
  }
};

/// 4.1: a random value joined by smooth parabolic segments -- a uniform
/// quadratic B-spline through random control points, a new one per period.
struct SmoothNoise {
  double p0 = 0, p1 = 0, p2 = 0, u = 0;
  void prime(Rng &r) { p0 = r.bipolar(); p1 = r.bipolar(); p2 = r.bipolar(); u = 0; }
  double tick(double hz, double kr, Rng &r) {
    const double y = 0.5 * (1 - u) * (1 - u) * p0 + (-u * u + u + 0.5) * p1 + 0.5 * u * u * p2;
    u += hz / kr;
    while (u >= 1) { u -= 1; p0 = p1; p1 = p2; p2 = r.bipolar(); }
    return y;
  }
};

/// 8.2: a random value joined by straight lines, a new one per period.
struct LinearNoise {
  double from = 0, to = 0, u = 0;
  bool primed = false;
  double tick(double hz, double kr, Rng &r) {
    if (!primed) { from = r.bipolar(); to = r.bipolar(); primed = true; }
    const double y = from + (to - from) * u;
    u += hz / kr;
    while (u >= 1) { u -= 1; from = to; to = r.bipolar(); }
    return y;
  }
};

/// 5: an envelope segment in the curve law of a numeric curve c,
/// y(t) = y0 + (y1 - y0)(1 - e^(c t/T)) / (1 - e^c).
struct Segment {
  double from = 0, to = 0, curve = 0;
  double samples = 1, at = 0;
  bool running = false;
  void start(double y0, double y1, double seconds, double c, double sr) {
    from = y0; to = y1; curve = c;
    samples = seconds * sr < 1 ? 1 : seconds * sr;
    at = 0;
    running = true;
  }
  /// The value one sample further in.
  double tick() {
    at += 1;
    if (at >= samples) { running = false; return to; }
    const double t = at / samples;
    if (std::fabs(curve) < 1e-6) return from + (to - from) * t;
    return from + (to - from) * (1 - std::exp(curve * t)) / (1 - std::exp(curve));
  }
};

/// 5.1: a one-shot attack-decay, struck again from where it is.
struct StrikeEnv {
  Segment seg;
  int stage = 0;  // 0 idle, 1 attack, 2 decay
  double level = 0;
  double attack, attackCurve, decayCurve;
  StrikeEnv(double a, double ac, double dc) : attack(a), attackCurve(ac), decayCurve(dc) {}
  void strike(double sr) { seg.start(level, 1, attack, attackCurve, sr); stage = 1; }
  double tick(double decay, double sr) {
    if (stage == 0) return level;
    level = seg.tick();
    if (!seg.running) {
      if (stage == 1) { stage = 2; seg.start(1, 0, decay, decayCurve, sr); }
      else { stage = 0; level = 0; }
    }
    return level;
  }
};

/// 5.2: attack to one while the gate is held, release on its fall; both
/// from the level it is at, curve -4.
struct GateEnv {
  Segment seg;
  int stage = 0;  // 0 idle, 1 attack, 2 held, 3 release
  double level = 0;
  void gate(bool on, double attack, double release, double sr) {
    if (on) { seg.start(level, 1, attack, -4, sr); stage = 1; }
    else if (stage != 0) { seg.start(level, 0, release, -4, sr); stage = 3; }
  }
  double tick() {
    if (stage == 1 || stage == 3) {
      level = seg.tick();
      if (!seg.running) stage = stage == 1 ? 2 : 0;
    }
    return level;
  }
};

/// 5.3: the two-pole resonant low-pass, its coefficients from a cutoff
/// sampled once a block and interpolated across it.
struct ResonantLowpass {
  double a0 = 0, b1 = 0, b2 = 0, pa0 = 0, pb1 = 0, pb2 = 0;
  double v1 = 0, v2 = 0;
  bool primed = false;
  void set(double fc, double rq, double sr) {
    fc = clampd(fc, 1, sr * 0.49);
    const double w = kTwoPi * fc / sr;
    const double D = std::tan(w * rq * 0.5);
    const double C = (1 - D) / (1 + D);
    pa0 = a0; pb1 = b1; pb2 = b2;
    b1 = (1 + C) * std::cos(w);
    b2 = -C;
    a0 = (1 + C - b1) * 0.25;
    if (!primed) { pa0 = a0; pb1 = b1; pb2 = b2; primed = true; }
  }
  double tick(double x, double t) {
    const double A0 = pa0 + (a0 - pa0) * t, B1 = pb1 + (b1 - pb1) * t, B2 = pb2 + (b2 - pb2) * t;
    const double v = A0 * x + B1 * v1 + B2 * v2;
    const double y = v + 2 * v1 + v2;
    v2 = v1; v1 = v;
    return y;
  }
};

/// 4.5 and 7.1: a fixed biquad.
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  double tick(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
  }
  void butterworthLowpass(double fc, double sr) {
    const double C = 1 / std::tan(kTwoPi * 0.5 * fc / sr);
    const double s2 = std::sqrt(2.0);
    b0 = 1 / (1 + s2 * C + C * C);
    b1 = 2 * b0;
    b2 = b0;
    a1 = 2 * (1 - C * C) * b0;
    a2 = (1 - s2 * C + C * C) * b0;
  }
  void highShelf(double fc, double gainDb, double S, double sr) {
    const double A = std::pow(10.0, gainDb / 40);
    const double w0 = kTwoPi * fc / sr;
    const double cw = std::cos(w0);
    const double alpha = std::sin(w0) / 2 * std::sqrt((A + 1 / A) * (1 / S - 1) + 2);
    const double sa = 2 * std::sqrt(A) * alpha;
    const double nb0 = A * ((A + 1) + (A - 1) * cw + sa);
    const double nb1 = -2 * A * ((A - 1) + (A + 1) * cw);
    const double nb2 = A * ((A + 1) + (A - 1) * cw - sa);
    const double na0 = (A + 1) - (A - 1) * cw + sa;
    const double na1 = 2 * ((A - 1) - (A + 1) * cw);
    const double na2 = (A + 1) - (A - 1) * cw - sa;
    b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
  }
};

/// 4.5: pink noise, a sum of first-order low-passes of white noise with a
/// -3 dB/octave slope, scaled to an RMS of 0.183.
struct Pink {
  double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
  double tick(Rng &r) {
    const double w = r.bipolar();
    b0 = 0.99886 * b0 + w * 0.0555179;
    b1 = 0.99332 * b1 + w * 0.0750759;
    b2 = 0.96900 * b2 + w * 0.1538520;
    b3 = 0.86650 * b3 + w * 0.3104856;
    b4 = 0.55000 * b4 + w * 0.5329522;
    b5 = -0.7616 * b5 - w * 0.0168980;
    const double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
    b6 = w * 0.115926;
    return p * 0.0855;
  }
};

/// 7.2: a comb with a damped feedback path whose stored value uses the
/// filter state from the sample before -- a loop of length + 1.
struct Comb {
  amalthea_rt::Buffer<double> buf;
  int at = 0;
  double s = 0;
  void prepare(int length) { buf.assign(length, 0.0); at = 0; s = 0; }
  double tick(double in, double fb, double damp) {
    const double o = buf[at];
    buf[at] = in + fb * s;
    s = o * (1 - damp) + s * damp;
    if (++at >= (int)buf.size()) at = 0;
    return o;
  }
};

struct Allpass {
  amalthea_rt::Buffer<double> buf;
  int at = 0;
  double prev = 0;
  void prepare(int length) { buf.assign(length, 0.0); at = 0; prev = 0; }
  double tick(double in) {
    const double bo = buf[at];
    const double out = -in + bo;
    buf[at] = in + 0.5 * prev;
    prev = bo;
    if (++at >= (int)buf.size()) at = 0;
    return out;
  }
};

/// 7.3: a two-pole resonator ringing for T seconds to -60 dB.
struct Resonator {
  double c1 = 0, c2 = 0, amp = 0, v1 = 0, v2 = 0;
  void prepare(double hz, double a, double T, double sr) {
    const double R = std::pow(0.001, 1 / (T * sr));
    c1 = 2 * R * std::cos(kTwoPi * hz / sr);
    c2 = -R * R;
    amp = a;
  }
  double tick(double x) {
    const double v = x + c1 * v1 + c2 * v2;
    const double y = amp * 0.25 * (v - v2);
    v2 = v1; v1 = v;
    return y;
  }
};

/// 7.4: a look-ahead brick-wall limiter at 1.0: the output two windows of
/// ten milliseconds late, its gain ramped across each window towards what
/// the loudest of it and the window after it allows.
struct Limiter {
  amalthea_rt::Buffer<double> delay;
  int window = 480, at = 0, inWindow = 0;
  double peakNow = 0, peakLast = 0, gainFrom = 1, gainTo = 1;
  void prepare(double sr) {
    window = (int)(0.01 * sr);
    delay.assign(window * 2, 0.0);
    at = inWindow = 0;
    peakNow = peakLast = 0;
    gainFrom = gainTo = 1;
  }
  double tick(double x) {
    const double out = delay[at];
    delay[at] = x;
    if (++at >= (int)delay.size()) at = 0;
    const double ax = std::fabs(x);
    if (ax > peakNow) peakNow = ax;
    const double g = gainFrom + (gainTo - gainFrom) * (double)inWindow / window;
    if (++inWindow >= window) {
      inWindow = 0;
      // The window now leaving is peakLast's; the one after it is peakNow's.
      const double peak = peakLast > peakNow ? peakLast : peakNow;
      gainFrom = gainTo;
      gainTo = peak > 1 ? 1 / peak : 1;
      peakLast = peakNow;
      peakNow = 0;
    }
    return out * g;
  }
};

/// Every control of the voice, in engine units (2).
struct Params {
  double glide = 0, shape = 0, folds = 0, fm1Ratio = 0.66, fm2Ratio = 3.3, fm1 = 0, fm2 = 0;
  bool asr = false;
  double attack = 0.04, peak = 10000, decay = 1, amp = 1, reverb = 0;
  int lfoShape = 0;
  double lfoFreq = 0.5;
  // 8.3: LFO amounts on frequency, wave shape, folds, FM low, FM high,
  // attack, peak, decay, reverb mix.
  double toFreq = 0, toShape = 0, toFolds = 0, toFm1 = 0, toFm2 = 0, toAttack = 0, toPeak = 0, toDecay = 0,
         toReverb = 0;
  double drift = 0, timbre = 0, pressure = 0, bend = 1;

  /// What a node's sliders do not reach, drawn from its seed as the dice
  /// draws them (10) -- less what would move the pitch: no glide, no LFO on
  /// the frequency, no drift.
  void drawHidden(uint64_t seed) {
    Rng r;
    r.seed(seed ^ 0xBA7A4EAull);
    auto u = [&] { return r.unit(); };
    asr = u() < 0.5;
    { const double x = u(); attack = 0.003 + (8 - 0.003) * x * x * x * x; }
    lfoShape = (int)(u() * 4) % 4;
    lfoFreq = 0.001 + 9.999 * u();
    auto maybe = [&](double p) { const bool on = u() < p; const double x = u(); return on ? x : 0.0; };
    toShape = maybe(0.35);
    toFolds = maybe(0.35);
    toFm1 = maybe(0.25);
    toFm2 = maybe(0.25);
    toAttack = maybe(0.35);
    toPeak = maybe(0.35);
    toDecay = maybe(0.35);
    toReverb = maybe(0.35);
  }

  /// The eight sliders, nought to one: wave shape, folds, FM low, FM high,
  /// peak, decay, reverb, drive.
  static double peakOf(double v) { return 100 * std::pow(100.0, clampd(v, 0, 1)); }
  static double decayOf(double v) { return 0.01 * std::pow(800.0, clampd(v, 0, 1)); }
  void setSliders(const double *v) {
    shape = clampd(v[0], 0, 1);
    folds = 3 * clampd(v[1], 0, 1);
    fm1 = clampd(v[2], 0, 1);
    fm2 = clampd(v[3], 0, 1);
    peak = peakOf(v[4]);
    decay = decayOf(v[5]);
    reverb = clampd(v[6], 0, 1);
    amp = 11 * clampd(v[7], 0, 1);
  }
};

class Voice {
 public:
  Params params;
  /// Forces the sustaining envelope: a drone has nothing to strike it again.
  bool drone = false;
  /// Forces the low-pass gate: a voice a controller plays is struck by each
  /// note and never let go.
  bool struck = false;

  void prepare(double sr, uint64_t seed) {
    sr_ = sr;
    kr_ = sr / kBlock;
    rng_.seed(seed);
    n1_.prime(rng_);
    n2_.prime(rng_);
    lfoRandom_ = rng_.bipolar();
    smooth_.butterworthLowpass(12000, sr);
    shelf_.highShelf(1000, -6, 1, sr);
    predelay_.assign((size_t)(0.015 * sr), 0.0);
    static const int combs[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    static const int alls[4] = {556, 441, 341, 225};
    for (int i = 0; i < 8; ++i) { combL_[i].prepare(combs[i]); combR_[i].prepare(combs[i] + 23); }
    for (int i = 0; i < 4; ++i) { allL_[i].prepare(alls[i]); allR_[i].prepare(alls[i] + 23); }
    res_[0].prepare(508, 0.15, 1.0, sr);
    res_[1].prepare(270, 0.25, 1.2, sr);
    res_[2].prepare(1153, 0.10, 1.4, sr);
    limiter_.prepare(sr);
  }

  /// A key down: the gate struck, or the sustain opened.
  void noteOn(double hz, double velocity) {
    hz_ = hz;
    velocity_ = clampd(velocity, 0, 1);
    pendingOn_ = true;
  }
  void noteOff() { pendingOff_ = true; }
  /// A fall-back to a key still held: the pitch only (3.1).
  void setPitch(double hz) { hz_ = hz; }

  /// Renders n samples, a multiple of nothing in particular: the control
  /// blocks run on across calls.
  void render(float *outL, float *outR, int n) {
    for (int i = 0; i < n; ++i) {
      if (inBlock_ == 0) controlBlock();
      const double t = (double)(inBlock_ + 1) / kBlock;
      float l, r;
      sample(t, l, r);
      outL[i] = l;
      if (outR) outR[i] = r;
      if (++inBlock_ >= kBlock) inBlock_ = 0;
    }
  }

 private:
  double sr_ = 48000, kr_ = 750;
  Rng rng_;
  double hz_ = 220, velocity_ = 0.8;
  bool pendingOn_ = false, pendingOff_ = false, gateHeld_ = false;
  int inBlock_ = 0;

  // The LFO and its smoothers.
  double lfoPhase_ = 0, lfoRandom_ = 0, lfoRandomPhase_ = 0;
  Lag lagF_, lagR1_, lagR2_, lagA1_, lagA2_, lagVel_, lagShape_, lagFolds_, lagAttack_, lagPeak_, lagDecay_, lagMix_;
  SmoothNoise n1_, n2_;
  LinearNoise drift_[8];

  // Control values: last block's and this block's, for interpolation.
  struct K {
    double f = 0, m1f = 0, amp1 = 0, m2f = 0, amp2 = 0, xfade = 0, fold = 1, velGain = 1, mix = 0, even = 0;
  } prev_, cur_;
  bool primed_ = false;
  bool primedEven_ = false;
  double attack_ = 0.04, decay_ = 1, top_ = 10000, low_ = 300;
  double att_[45] = {};

  // Audio state.
  double ph1_ = 0, ph2_ = 0, phSine_ = 0, phTri_ = 0, phSaw_ = 0;
  Biquad smooth_, shelf_;
  Pink pink_;
  StrikeEnv cutEnv_{0.003, 4, -20}, ampEnv_{0.002, 4, -10};
  GateEnv asrEnv_;
  ResonantLowpass lpg_, asr1_, asr2_;

  // The reverb.
  amalthea_rt::Buffer<double> predelay_;
  int predelayAt_ = 0;
  Comb combL_[8], combR_[8];
  Allpass allL_[4], allR_[4];
  Resonator res_[3];
  Limiter limiter_;

  bool sustaining() const { return !struck && (drone || params.asr); }

  void controlBlock() {
    const Params &h = params;
    // 8.1: the LFO, free-running. The square reads its phase before the
    // block's step, the triangle and the ramp after it -- measured.
    const double squarePhase = lfoPhase_;
    lfoPhase_ += h.lfoFreq / kr_;
    lfoPhase_ -= std::floor(lfoPhase_);
    double L = 0;
    switch (h.lfoShape) {
      case 0: L = lfoPhase_ < 0.25 ? 4 * lfoPhase_ : (lfoPhase_ < 0.75 ? 2 - 4 * lfoPhase_ : 4 * lfoPhase_ - 4); break;
      case 1: { double p = lfoPhase_ + 0.5; p -= std::floor(p); L = 2 * p - 1; break; }
      case 2: L = squarePhase < 0.5 ? 1 : 0; break;
      default: L = lfoRandom_; break;
    }
    lfoRandomPhase_ += 2 * h.lfoFreq / kr_;
    if (lfoRandomPhase_ >= 1) { lfoRandomPhase_ -= std::floor(lfoRandomPhase_); lfoRandom_ = rng_.bipolar(); }

    // 8.3 and 3.4: the destinations, clipped, then smoothed.
    const Params &s = params;
    const double nyq = sr_ / 2;
    double dn[8] = {};
    for (int i = 0; i < 8; ++i) dn[i] = h.drift > 0 ? drift_[i].tick(0.15, kr_, rng_) * h.drift : 0;
    const double f = lagF_.tick(clampd(hz_ * midiratio(L * h.toFreq * 18), 0, nyq) * h.bend,
                                0.007 + h.glide, kr_);
    const double r1 = lagR1_.tick(h.fm1Ratio, 0.005, kr_);
    const double r2 = lagR2_.tick(h.fm2Ratio, 0.005, kr_);
    const double a1c = clampd(s.fm1 + (L * h.toFm1 + dn[2]) * 0.5, 0, 1);
    const double a2c = clampd(s.fm2 + (L * h.toFm2 + dn[3]) * 0.5, 0, 1);
    const double A1 = lagA1_.tick(a1c * a1c, 0.005, kr_);
    const double A2 = lagA2_.tick(a2c * a2c, 0.005, kr_);
    const double vel = lagVel_.tick(velocity_, 0.005, kr_);
    const double shape = lagShape_.tick(clampd(s.shape + L * h.toShape + dn[0], 0, 1), 0.005, kr_);
    const double folds = lagFolds_.tick(clampd(s.folds + (L * h.toFolds + dn[1]) * 2, 0, 3), 0.005, kr_);
    attack_ = lagAttack_.tick(clampd(h.attack + (L * h.toAttack + dn[4]) * 2.2, 0.003, 8), 0.005, kr_);
    const double P = lagPeak_.tick(clampd(s.peak * midiratio((L * h.toPeak + dn[5]) * 24), 100, 10000), 0.005, kr_);
    decay_ = lagDecay_.tick(clampd(s.decay + (L * h.toDecay + dn[6]) * 2.2, 0.01, 8), 0.005, kr_);
    const double mix = lagMix_.tick(clampd(s.reverb + L * h.toReverb + dn[7], 0, 1), 0.01, kr_);

    // 3.1: the key, its segment times read as they start.
    if (pendingOn_) {
      pendingOn_ = false;
      if (sustaining()) { if (!gateHeld_) asrEnv_.gate(true, attack_, decay_, sr_); }
      else { cutEnv_.strike(sr_); ampEnv_.strike(sr_); }
      gateHeld_ = true;
    }
    if (pendingOff_) {
      pendingOff_ = false;
      if (gateHeld_ && sustaining()) asrEnv_.gate(false, attack_, decay_, sr_);
      gateHeld_ = false;
    }

    // 4.1: the modulators' frequencies and depths, in hertz.
    const double N1 = n1_.tick(0.1, kr_, rng_), N2 = n2_.tick(0.1, kr_, rng_);
    prev_ = cur_;
    cur_.f = f;
    cur_.m1f = f * r1 * (1 + 0.001 * N1);
    cur_.m2f = f * r2 * (1 + 0.005 * N2);
    cur_.amp1 = 22 * A1 * cur_.m1f;
    cur_.amp2 = 12 * A2 * cur_.m2f;
    cur_.xfade = shape * 3;
    cur_.fold = 1 + 0.5 * h.timbre + 2 * folds;
    cur_.velGain = linlin(vel, 0, 1, 0.2, 1);
    cur_.mix = mix;
    if (!primed_) { prev_ = cur_; primed_ = true; }

    // 4.2: the additive partials' attenuation and the even partials' weight.
    att_[1] = 1;
    for (int k = 2; k <= 44; ++k) {
      const double H = f * k;
      const double x = nyq - 2.25 * H;
      const double hi = 0.5 * H;
      if (x <= 1e-6) att_[k] = 1e-6;
      else if (x >= hi) att_[k] = 1;
      else att_[k] = 1e-6 * std::pow(1e6, std::log(x / 1e-6) / std::log(hi / 1e-6));
    }
    cur_.even = linlin(shape, 0.666666, 1, 0, 1);
    if (!primedEven_) { prev_.even = cur_.even; primedEven_ = true; }

    // 5: the filters' sweep, its cutoff read at the block's start.
    const double ve = linlin(vel, 0, 1, 0.5, 1);
    top_ = P * ve;
    low_ = top_ < 300 ? top_ : 300;
    if (sustaining()) {
      const double fc = linlin(asrEnv_.level, 0, 1, low_, top_);
      asr1_.set(fc, 0.95, sr_);
      asr2_.set(fc, 0.95, sr_);
    } else {
      lpg_.set(linlin(cutEnv_.level, 0, 1, low_, top_), 0.9, sr_);
    }
  }

  void sample(double t, float &outL, float &outR) {
    auto lerp = [t](double a, double b) { return a + (b - a) * t; };
    const double f = lerp(prev_.f, cur_.f);
    const double m1f = lerp(prev_.m1f, cur_.m1f), m2f = lerp(prev_.m2f, cur_.m2f);
    const double mod1 = std::sin(kTwoPi * ph1_) * lerp(prev_.amp1, cur_.amp1);
    const double mod2 = std::sin(kTwoPi * ph2_) * lerp(prev_.amp2, cur_.amp2);
    ph1_ += m1f / sr_; ph1_ -= std::floor(ph1_);
    ph2_ += m2f / sr_; ph2_ -= std::floor(ph2_);
    const double fc = f + mod1 + mod2;
    const double inc = fc / sr_;

    // 4.2: the three carriers.
    const double sine = 0.5 * std::sin(kTwoPi * phSine_);
    phSine_ += inc; phSine_ -= std::floor(phSine_);
    // Naive, and wrapped only upwards: a negative frequency runs it away.
    const double raw = phTri_ < 0.5 ? 4 * phTri_ - 1 : 4 * (1 - phTri_) - 1;
    const double tri = 0.5 * raw;
    phTri_ += inc;
    if (phTri_ >= 1) phTri_ -= 1;

    const double s = lerp(prev_.xfade, cur_.xfade);
    double add = 0;
    phSaw_ += inc; phSaw_ -= std::floor(phSaw_);
    if (s > 1) {
      const double th = kTwoPi * phSaw_;
      const double even = lerp(prev_.even, cur_.even);
      const double c2 = 2 * std::cos(th);
      double sPrev = 0, sNow = std::sin(th);
      for (int k = 1; k <= 44; ++k) {
        const double w = (k & 1) ? 1 : even;
        if (w > 0) add += sNow / k * att_[k] * w;
        const double sNext = c2 * sNow - sPrev;
        sPrev = sNow; sNow = sNext;
      }
    }
    double x;
    if (s <= 1) x = (1 - s) * sine + s * tri;
    else if (s <= 2) x = (2 - s) * tri + (s - 1) * add;
    else x = add;

    // 4.4: the folder, reflecting into +-0.5.
    double u = x * lerp(prev_.fold, cur_.fold) + 0.5;
    u -= 2 * std::floor(u * 0.5);
    x = (u < 1 ? u : 2 - u) - 0.5;

    // 4.5
    x = smooth_.tick(x) + 0.003 * pink_.tick(rng_);

    // 5: the gate, or the sustain.
    if (sustaining()) {
      const double env = asrEnv_.tick();
      x = asr2_.tick(asr1_.tick(x, t), t) * env;
    } else {
      cutEnv_.tick(decay_, sr_);
      const double env = ampEnv_.tick(decay_, sr_);
      x = lpg_.tick(x, t) * env;
    }

    // 6
    const double v = std::tanh(x * lerp(prev_.velGain, cur_.velGain) * linlin(params.pressure, 0, 1, 1.5, 3) * params.amp);
    const double d = std::fabs(v) <= 0.5 ? v : (std::fabs(v) - 0.25) / v;

    // 7: the reverb.
    double p = std::tanh(1.5 * shelf_.tick(d));
    const double delayed = predelay_[predelayAt_];
    predelay_[predelayAt_] = p;
    if (++predelayAt_ >= (int)predelay_.size()) predelayAt_ = 0;
    p = delayed;
    const double in = 0.0165 * p;
    const double fb = 0.7 * 0.28 + 0.7, damp = 0.35 * 0.4;
    double wl = 0, wr = 0;
    for (int i = 0; i < 8; ++i) { wl += combL_[i].tick(in, fb, damp); wr += combR_[i].tick(in, fb, damp); }
    for (int i = 0; i < 4; ++i) { wl = allL_[i].tick(wl); wr = allR_[i].tick(wr); }
    double bank = 0;
    for (auto &r : res_) bank += r.tick(p);
    const double spring = limiter_.tick(bank);
    const double wetL = 0.935 * std::tanh(1.8 * wl) + 0.065 * spring;
    const double wetR = 0.935 * std::tanh(1.8 * wr) + 0.065 * spring;
    const double mix = lerp(prev_.mix, cur_.mix);
    outL = (float)((1 - mix) * d + mix * wetL);
    outR = (float)((1 - mix) * d + mix * wetR);
  }
};

}  // namespace bayarea
}  // namespace vascular
