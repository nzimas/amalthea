// caligula: an eight-oscillator organismic drone voice.
//
// Eight oscillators in four pairs on two sides, each pair frequency-modulated
// by the audible output of another; a hyper-LFO of two linked phasors; two
// modulated delay lines that sing on their own; a distortion with a spike on
// its peaks; and the whole output able to modulate the oscillators again.
// Written from Docs/caligula/SPEC.md: every control in its 0..127 units.
#ifndef VASCULAR_CALIGULA_CORE_H
#define VASCULAR_CALIGULA_CORE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include "RtBuffer.h"

namespace vascular {
namespace caligula {

constexpr double kTwoPi = 6.283185307179586;

inline double clampd(double x, double lo, double hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline double mtof(double m) { return m <= -1500 ? 0 : 440.0 * std::pow(2.0, (m - 69.0) / 12.0); }
inline double ftom(double f) { return f <= 0 ? -1500 : 69.0 + 12.0 * std::log2(f / 440.0); }
/// The soft clipper everywhere in the voice: exactly 1 at |x| = 3.
inline double tanhP(double x) {
  const double c = clampd(x, -3, 3);
  return c * (27 + c * c) / (27 + 9 * c * c);
}

/// Every control: its 0..127 values (switches 0/1, selectors 0..2).
struct Params {
  double tune[8] = {64, 64, 64, 64, 64, 64, 64, 64};
  double fast[4] = {1, 1, 1, 1};
  double sharp[4] = {64, 64, 64, 64};
  double mod[4] = {0, 0, 0, 0};
  int source[4] = {1, 1, 1, 1};          // 0 partner, 1 off, 2 LFO-CV
  double pitch[2] = {64, 64};
  double hold[2] = {0, 0};
  int switchRing = 0, totalFB = 0, vibrato = 0, quantize = 0;
  double fa = 64, fb = 64;
  int andor = 0, link = 0;
  double time1 = 64, time2 = 64, mod1 = 0, mod2 = 0, feedback = 0, delMix = 0;
  int delMod = 1, lfoWav = 0;            // SELF / OFF / LFO; TRI / SQR
  double drv = 0, dstMix = 0, vol = 127;
  /// The node's note against 110 Hz: every voice moved by it together.
  double transpose = 1;
};

/// A control's 23.22 ms linear glide to each new target.
struct Ramp {
  double value = 0, target = 0, step = 0;
  int left = 0;
  void set(double t, int samples, bool now) {
    if (now) { value = target = t; left = 0; return; }
    if (t == target && left == 0) return;
    target = t;
    left = std::max(1, samples);
    step = (target - value) / left;
  }
  double tick() {
    if (left > 0) { value += step; if (--left == 0) value = target; }
    return value;
  }
};

/// One-pole lowpass, the simple form.
struct OnePoleLP {
  double y = 0, k = 1;
  void set(double fc, double sr) { k = clampd(kTwoPi * fc / sr, 0, 1); }
  double run(double x) { y = k * x + (1 - k) * y; return y; }
};

/// One-pole high-pass.
struct OnePoleHP {
  double w = 0, a = 1;
  void set(double fc, double sr) { a = clampd(1 - kTwoPi * fc / sr, 0, 1); }
  double run(double x) {
    const double wn = x + a * w;
    const double y = 0.5 * (1 + a) * (wn - w);
    w = wn;
    return y;
  }
};

/// A fixed delay of `n` samples, for the modulation buses.
struct Late {
  amalthea_rt::Buffer<double> buf;
  int at = 0;
  void prepare(int n) { buf.assign(std::max(1, n), 0.0); at = 0; }
  double push(double x) {
    const double y = buf[at];
    buf[at] = x;
    at = (at + 1) % (int)buf.size();
    return y;
  }
};

struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  void seed(uint64_t v) { s = v * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull; if (!s) s = 1; }
  uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
  double unit() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
};

/// One delay line: its buffer, its loop filters, its compressor and expander.
struct Line {
  amalthea_rt::Buffer<float> buf;
  int write = 0;
  OnePoleHP hp1;
  OnePoleLP lp4000, lp689;
  double env = 0, c = 0.96;
  // The expander's detector: 512 samples Hann-windowed, every 256.
  amalthea_rt::Buffer<float> hist;
  int histAt = 0, sinceLevel = 0;
  double gain = 1, gainStep = 0;
  int gainLeft = 0;
  double y = 0;

  void prepare(double sr) {
    buf.assign((size_t)(sr * 6.0) + 8, 0.0f);
    write = 0;
    hp1.set(1, sr); lp4000.set(4000, sr); lp689.set(689, sr);
    c = std::pow(0.01, 1.0 / (0.0025 * sr));
    hist.assign(512, 0.0f);
    histAt = 0; sinceLevel = 0; gain = 1; gainLeft = 0; y = 0; env = 0;
  }
  double read(double delay) const {
    const int n = (int)buf.size();
    double pos = write - delay;
    while (pos < 0) pos += n;
    const int i1 = (int)pos;
    const double fr = pos - i1;
    const int i0 = (i1 - 1 + n) % n, i2 = (i1 + 1) % n, i3 = (i1 + 2) % n;
    const double y0 = buf[i0], y1 = buf[i1 % n], y2 = buf[i2], y3 = buf[i3];
    // 4-point cubic.
    const double c0 = y1, c1 = 0.5 * (y2 - y0), c2 = y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3,
                 c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
    return ((c3 * fr + c2) * fr + c1) * fr + c0;
  }
  double compressor(double in) {
    env = (1 - c) * std::fabs(in) + c * env;
    const double th = 0.2512;
    const double g = env > 0 ? clampd((th + (env - th) / 5) / env, 0, 1) : 0;
    return in * tanhP(g);
  }
  double expander(double in, double sr) {
    hist[histAt] = (float)in;
    histAt = (histAt + 1) & 511;
    if (++sinceLevel >= 256) {
      sinceLevel = 0;
      double sum = 0, wsum = 0;
      for (int k = 0; k < 512; ++k) {
        const double w = 0.5 - 0.5 * std::cos(kTwoPi * (k + 0.5) / 512);
        const double x = hist[(histAt + k) & 511];
        sum += w * x * x; wsum += w;
      }
      const double rms = std::sqrt(sum / wsum);
      const double level = std::max(-100.0, 20 * std::log10(rms + 1e-12));
      const double gdB = std::min(0.0, (level + 60) * (1 - 1.0 / 5));
      const double want = std::pow(10.0, gdB / 20);
      gainLeft = std::max(1, (int)(0.011 * sr));
      gainStep = (want - gain) / gainLeft;
    }
    if (gainLeft > 0) { gain += gainStep; --gainLeft; }
    return in * gain;
  }
};

class Engine {
 public:
  Params p;
  bool sensor[8] = {};

  void prepare(double sr, uint64_t seed) {
    sr_ = sr;
    rng_.seed(seed);
    ramp_ = (int)std::lround(0.02322 * sr);
    for (int i = 0; i < 4; ++i) {
      vibRate_[i] = 0.5 + 3 * (std::floor(rng_.unit() * 1001) / 1000.0);
      vibPhase_[i] = 0;
      busHP_[i].set(3, sr);
      bus_[i].prepare(64);
    }
    lfoBus_.prepare(64);
    fbBus_.prepare(64);
    hp20_.set(20, sr); hp10_.set(10, sr); hp3_.set(3, sr);
    for (auto &l : line_) l.prepare(sr);
    self_[0].prepare(65); self_[1].prepare(65);
    for (int k = 0; k < 8; ++k) { phase_[k] = rng_.unit(); pw_[k] = 0.53125; lp_[k] = 0; r_[k] = 0; rStep_[k] = 0; rLeft_[k] = 0; gateWas_[k] = false; }
    applyParams(true);
    for (int i = 0; i < 4; ++i) fastWas_[i] = p.fast[i] > 0.5;
    // Initial parameter application can invert gateWas_ when setting fast.
    // Start from closed gates so the first struck sensor always opens its ramp.
    for (int k = 0; k < 8; ++k) gateWas_[k] = false;
  }

  /// The controls moved: every glide set off towards its new value.
  void applyParams(bool now = false) {
    for (int k = 0; k < 8; ++k) {
      const int side = k < 4 ? 0 : 1;
      // Each voice's tuning range, in MIDI notes.
      static const double kLo[8] = {-16, -16, 7, 9, 20, 20, 33, 33};
      static const double kHi[8] = {93, 93, 109, 107, 116.54, 116.54, 126.24, 131.22};
      const double note = kLo[k] + (kHi[k] - kLo[k]) * clampd(p.tune[k], 0, 127) / 127.0;
      const double pitch = 0.01 + 1.99 * clampd(p.pitch[side], 0, 127) / 127.0;
      double f = mtof(note) * pitch * p.transpose;
      if (p.quantize) f = mtof(std::floor(ftom(f) + 0.5));
      freq_[k].set(std::min(f, sr_ * 0.49), ramp_, now);
    }
    for (int i = 0; i < 4; ++i) {
      const double s = clampd(p.sharp[i], 0, 127) / 127.0;
      sharp_[i].set(s * s, ramp_, now);
      const double m = clampd(p.mod[i], 0, 127) / 127.0;
      depth_[i].set(2 * m * m * m * m, ramp_, now);
      const bool fast = p.fast[i] > 0.5;
      if (fast != fastWas_[i]) {
        fastWas_[i] = fast;
        gateWas_[2 * i] = !sensor[2 * i];        // re-issue the gates with the new time
        gateWas_[2 * i + 1] = !sensor[2 * i + 1];
      }
    }
    for (int s = 0; s < 2; ++s) {
      const double h = clampd(p.hold[s], 0, 127) / 127.0;
      hold_[s].set(h * h, ramp_, now);
    }
    vib_.set(p.vibrato ? 1 : 0, ramp_, now);
    auto rate = [](double v) { const double u = clampd(v, 0, 127) / 127.0; return mtof(127 * u * u - 75); };
    rateA_.set(rate(p.fa), ramp_, now);
    rateB_.set(rate(p.fb), ramp_, now);
    auto time = [](double v) { return 1.45125 * std::pow(2.0, 12 * clampd(v, 0, 127) / 127.0); };
    time_[0].set(time(p.time1), ramp_, now);
    time_[1].set(time(p.time2), ramp_, now);
    auto depthMs = [](double v) { const double u = clampd(v, 0, 127) / 127.0; return 10 * u * u; };
    dmod_[0].set(depthMs(p.mod1), ramp_, now);
    dmod_[1].set(depthMs(p.mod2), ramp_, now);
    const double fu = clampd(p.feedback, 0, 127) / 127.0;
    fbk_.set(fu * std::pow(4.0, fu), ramp_, now);
    delMix_.set(clampd(p.delMix, 0, 127) / 127.0, ramp_, now);
    const double du = clampd(p.drv, 0, 127) / 127.0;
    drive_.set(std::pow(10.0, (std::pow(3.0, 1 + 2 * du) + 3) / 20), ramp_, now);
    dstMix_.set(clampd(p.dstMix, 0, 127) / 127.0, ramp_, now);
    const double vu = clampd(p.vol, 0, 127) / 127.0;
    vol_.set(vu * vu, ramp_, now);
  }

  double sample() {
    const double sr = sr_;
    // -- the gates --
    for (int k = 0; k < 8; ++k) {
      if (sensor[k] != gateWas_[k]) {
        gateWas_[k] = sensor[k];
        const bool fast = p.fast[k / 2] > 0.5;
        const double secs = sensor[k] ? (fast ? 0.1 : 0.2) : (fast ? 0.1 : 8.0);
        rLeft_[k] = std::max(1, (int)(secs * sr));
        rStep_[k] = ((sensor[k] ? 1.0 : 0.0) - r_[k]) / rLeft_[k];
      }
      if (rLeft_[k] > 0) { r_[k] += rStep_[k]; if (--rLeft_[k] == 0) r_[k] = sensor[k] ? 1 : 0; }
    }
    // -- the LFO --
    const double ra = rateA_.tick(), rb = rateB_.tick();
    const double sqA = clampd(1000 * std::cos(kTwoPi * phA_), -1, 1);
    phA_ += ra / sr; phA_ -= std::floor(phA_);
    const double sqB = clampd(1000 * std::cos(kTwoPi * phB_), -1, 1);
    phB_ += rb * (1 + (p.link ? sqA / 2 : 0)) / sr; phB_ -= std::floor(phB_);
    const double triA = 4 * std::fabs(phA_ - 0.5) - 1, triB = 4 * std::fabs(phB_ - 0.5) - 1;
    const double lfoSq = p.andor ? (sqA + sqB) / 2 : sqA * sqB;
    // -- the buses, a block late --
    const double lfoLate = lateLfo_;
    const double fbLate = lateFb_;
    auto g = [](bool b) { return 0.001 + 0.999 * (b ? 1.0 : 0.0); };
    const bool w = p.switchRing != 0, T = p.totalFB != 0;
    const double busOf[4] = {late_[0], late_[1], late_[2], late_[3]};
    // first partner, second partner (SWITCH off, on)
    static const int first[4] = {1, 0, 3, 2}, second[4] = {3, 0, 1, 2};
    const double vib = vib_.tick();
    double pairOut[4];
    for (int i = 0; i < 4; ++i) {
      const double busP = g(!w) * busOf[first[i]] + g(w) * busOf[second[i]];
      const double lfoP = g(T) * fbLate + g(!T) * lfoLate;
      const double m = g(p.source[i] == 0) * busP + g(p.source[i] == 2) * lfoP;
      const double depth = depth_[i].tick();
      const double sharp = sharp_[i].tick();
      vibPhase_[i] += vibRate_[i] / sr; vibPhase_[i] -= std::floor(vibPhase_[i]);
      const double v = vib * std::cos(kTwoPi * vibPhase_[i]);
      const int side = i < 2 ? 0 : 1;
      double out = 0;
      for (int j = 0; j < 2; ++j) {
        const int k = 2 * i + j;
        const double f = freq_[k].tick() * (1 + 0.005 * v) * (1 + depth * m);
        out += voice(k, std::fabs(f), 0.53125 + 0.025 * v, sharp, side);
      }
      pairOut[i] = out;
    }
    hold_[0].tick(); hold_[1].tick();
    for (int i = 0; i < 4; ++i) late_[i] = bus_[i].push(busHP_[i].run(pairOut[i]));
    const double x = (pairOut[0] + pairOut[1] + pairOut[2] + pairOut[3]) / 6;

    // -- the delay --
    const double noise = (rng_.unit() * 2 - 1) * 0.001;
    const double fb = fbk_.tick();
    double wetSum = 0;
    for (int i = 0; i < 2; ++i) {
      Line &L = line_[i];
      const double write = x + noise + fb * L.y;
      const double selfSig = self_[i].push(write);
      double mod = 0;
      if (p.delMod == 0) mod = L.lp689.run(selfSig / 2);
      else if (p.delMod == 2) mod = p.lfoWav ? (sqA + sqB) / 2 : (triA + triB) / 2;
      const double ms = time_[i].tick() + dmod_[i].tick() * mod;
      const double delay = std::max(65.0, ms * sr / 1000.0);
      const double tap = L.read(std::min(delay, (double)L.buf.size() - 4));
      L.y = L.expander(L.compressor(L.lp4000.run(L.hp1.run(tap))), sr);
      L.buf[L.write] = (float)write;
      L.write = (L.write + 1) % (int)L.buf.size();
      wetSum += L.y;
    }
    const double wet = tanhP(wetSum / 2 / std::max(fb, 1.5));
    const double dm = delMix_.tick();
    const double z = (1 - dm) * x + dm * wet;

    // -- the distortion --
    const double gD = drive_.tick();
    const double d1 = tanhP(hp20_.run(gD * z));
    const double d2 = d1 + std::pow(d1, 31) / 4;
    const double d3 = hp10_.run(d2) / clampd(gD, 1, 4) + 0.1 * z;
    const double mixD = dstMix_.tick();
    const double o = tanhP(mixD * d3 + (1 - mixD) * z);
    lateFb_ = fbBus_.push(hp3_.run(o));
    lateLfo_ = lfoBus_.push(lfoSq);
    return o * vol_.tick();
  }

 private:

  double voice(int k, double f, double pw, double sharp, int side) {
    const double dt = f / sr_;
    double &ph = phase_[k];
    // The pulse: two polyBLEP saws a width apart.
    auto blep = [dt](double t) {
      if (dt <= 0) return 0.0;
      if (t < dt) { const double x = t / dt; return 2 * x - x * x - 1; }
      if (t > 1 - dt) { const double x = (t - 1) / dt; return x * x + 2 * x + 1; }
      return 0.0;
    };
    const double w = clampd(pw_[k], 0, 1);
    auto saw = [&](double t) { return (2 * t - 1) - blep(t); };
    double t2 = ph + w; t2 -= std::floor(t2);
    const double pulse = saw(ph) - saw(t2) + 2 * w - 1;
    const double sq = 0.59 * pulse;
    // The "triangle": the pulse through a leaky integrator, its corner a
    // quarter of the pitch and never below 100 Hz.
    const double fc = clampd(std::max(f / 4, 100.0), 0, sr_ / 2);
    const double a = std::exp(-kTwoPi * fc / sr_);
    lp_[k] = (1 - a) * pulse + a * lp_[k];
    const double tri = 2.65 * lp_[k] - 0.145;
    ph += dt;
    if (ph >= 1) { ph -= std::floor(ph); pw_[k] = pw; }
    const double amp = std::min(r_[k] * r_[k] + hold_[side].value, 1.0);
    const double bump = 0.5 * std::sin(kTwoPi * r_[k]);
    return (sharp * sq + (1 - sharp) * tri + bump) * amp;
  }

  double sr_ = 48000;
  int ramp_ = 1115;
  Rng rng_;
  Ramp freq_[8], sharp_[4], depth_[4], hold_[2], vib_, rateA_, rateB_, time_[2], dmod_[2], fbk_, delMix_, drive_, dstMix_, vol_;
  double phase_[8] = {}, pw_[8] = {}, lp_[8] = {}, r_[8] = {}, rStep_[8] = {};
  int rLeft_[8] = {};
  bool gateWas_[8] = {}, fastWas_[4] = {};
  double vibRate_[4] = {}, vibPhase_[4] = {};
  double phA_ = 0, phB_ = 0;
  OnePoleHP busHP_[4], hp20_, hp10_, hp3_;
  Late bus_[4], lfoBus_, fbBus_, self_[2];
  double late_[4] = {}, lateLfo_ = 0, lateFb_ = 0;
  Line line_[2];
};

}  // namespace caligula
}  // namespace vascular

#endif
