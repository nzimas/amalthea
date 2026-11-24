/*
 * resurgent's oscillators: every algorithm a scene's three slots can run.
 *
 * Written from the Surge XT manual's descriptions of what each algorithm
 * does (Docs/resurgent/SPEC-from-manual.md, section 1) and from the
 * literature named per algorithm below; no Surge source was read. Where the
 * manual describes a behaviour and leaves its form open -- Sine's 28 shapes,
 * String's exciters, Alias's mask -- the form here is this instrument's own.
 *
 *  - band-limited steps: PolyBLEP, Valimaki & Huovilainen, IEEE SPM 24(2), 2007.
 *  - FM as phase modulation: Chowning, JAES 21(7), 1973.
 *  - String: plucked and bowed strings as Karplus & Strong, CMJ 7(2), 1983,
 *    with Jaffe & Smith's extensions (CMJ 7(2), 1983) for decay and
 *    stiffness.
 *  - Twist: Emilie Gillet's Plaits, vendored under its MIT licence (see
 *    vendor/PLAITS_LICENSE) and driven through its own controls.
 */
#ifndef VASCULAR_RESURGENT_OSC_H
#define VASCULAR_RESURGENT_OSC_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>
#include "RtBuffer.h"

#include "plaits/dsp/voice.h"
#include "vascular_stmlib/utils/buffer_allocator.h"

namespace vascular {
namespace resurgent {

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float dbToGain(float db) { return std::pow(10.0f, db / 20.0f); }
inline float noteHz(float semisFromA440) { return 440.0f * std::pow(2.0f, semisFromA440 / 12.0f); }
inline float wrap01(float x) { return x - std::floor(x); }
const float kTwoPi = 6.28318530718f;

/// A cheap, seedable generator; quality is not the point, independence is.
struct Rng {
  uint32_t s = 0x9E3779B9u;
  void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; for (int i = 0; i < 4; ++i) next(); }
  uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
  float unit() { return (next() >> 8) * (1.0f / 16777216.0f); }
  float bipolar() { return unit() * 2.0f - 1.0f; }
};

/// PolyBLEP residual for a step at phase 0.
inline float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

/// One slot's wavetable, frames scaled to +-1, with a mip level per octave.
struct Wavetable {
  int frameSize = 0;
  int frames = 0;
  std::vector<float> data;
  std::vector<std::vector<float>> mips;

  void build() {
    mips.clear();
    if (frameSize < 2 || frames < 1) return;
    std::vector<float> level = data;
    int size = frameSize;
    mips.push_back(level);
    while (size > 16) {
      const int half = size / 2;
      std::vector<float> next((size_t)half * frames);
      for (int f = 0; f < frames; ++f) {
        const float *src = level.data() + (size_t)f * size;
        float *dst = next.data() + (size_t)f * half;
        for (int i = 0; i < half; ++i) {
          auto at = [&](int k) { return src[((k % size) + size) % size]; };
          const int c = 2 * i;
          dst[i] = 0.5f * at(c) + 0.3125f * (at(c - 1) + at(c + 1))
                   - 0.0625f * (at(c - 3) + at(c + 3));
        }
      }
      mips.push_back(next);
      level.swap(next);
      size = half;
    }
  }
};

/// The algorithms, by the number a patch stores. 0-8 are anchored by the
/// format study; String, Twist and Alias follow in the order they joined the
/// instrument (Docs/resurgent/ENUMS.md).
enum OscType {
  O_classic = 0, O_sine = 1, O_wavetable = 2, O_shnoise = 3, O_audioin = 4,
  O_fm3 = 5, O_fm2 = 6, O_window = 7, O_modern = 8, O_string = 9, O_twist = 10,
  O_alias = 11
};

inline bool hasUnison(int type) {
  return type == O_classic || type == O_sine || type == O_wavetable || type == O_shnoise
         || type == O_window || type == O_modern || type == O_alias;
}

/// Sine's shapes: variants of one sine cycle by masking, shifting and
/// doubling its quadrants, as the manual describes them; patches store up to
/// 31 (ENUMS.md 3.5). Which variant has which number is this instrument's
/// own: eight treatments of the cycle, each in four forms -- as it is, the
/// second half inverted, shifted by a quarter, and cubed.
inline float sineShape(int shape, float t) {
  shape = clampi(shape, 0, 31);
  const int form = shape / 8, treatment = shape % 8;
  if (form == 2) t = wrap01(t + 0.25f);
  const float s = std::sin(kTwoPi * t);
  float v;
  switch (treatment) {
    case 1: v = t < 0.5f ? s : 0.0f; break;                                // half
    case 2: v = 2.0f * std::fabs(s) - 1.0f; break;                          // full wave
    case 3: v = t < 0.5f ? std::sin(kTwoPi * 2.0f * t) : 0.0f; break;       // doubled, half
    case 4: v = std::sin(kTwoPi * wrap01(t * 2.0f)); break;                 // doubled
    case 5: v = std::sin(kTwoPi * 0.25f * wrap01(t * 4.0f)); break;         // first quadrant, four times
    case 6: v = s >= 0 ? std::sqrt(s) : -std::sqrt(-s); break;               // squared off
    case 7: v = t < 0.25f || t >= 0.75f ? s : 0.0f; break;                  // outer quadrants
    default: v = s; break;
  }
  if (form == 1 && t >= 0.5f) v = -v;
  if (form == 3) v = v * v * v;
  return v;
}

/// The Plaits voice a Twist slot runs, built only for a slot that needs one.
struct TwistEngine {
  static const int kArena = 16384;
  char arena[kArena];
  plaits::Voice voice;
  plaits::Patch patch;
  plaits::Modulations mods;
  plaits::Voice::Frame block[plaits::kBlockSize];
  int taken = plaits::kBlockSize;

  TwistEngine() {
    vascular_stmlib::BufferAllocator allocator(arena, kArena);
    voice.Init(&allocator);
    std::memset(&patch, 0, sizeof(patch));
    std::memset(&mods, 0, sizeof(mods));
    patch.decay = 0.5f;
    patch.lpg_colour = 0.5f;
  }
};

/// One of the three slots.
struct Oscillator {
  static const int kMaxUnison = 16;
  float phase[kMaxUnison] = {};
  float subPhase[kMaxUnison] = {};
  float syncPhase[kMaxUnison] = {};
  float held[kMaxUnison] = {};
  float fb1 = 0, fb2 = 0;
  float m1Phase = 0, m2Phase = 0, m3Phase = 0;
  float lowState = 0, highState = 0;
  Rng rng;
  const Wavetable *table = nullptr;
  float last = 0;
  // String: two delay lines, their loop filters, and the burst still to play.
  amalthea_rt::Buffer<float> line1, line2;
  int at1 = 0, at2 = 0;
  float lp1 = 0, lp2 = 0;
  int burst = 0;
  float exciterPhase = 0, pink = 0;
  TwistEngine *twist = nullptr;
  const float *additive = nullptr;  // Alias's sixteen harmonics
  bool twistLpg = false;  // set per block from the slot's LPG switch
  float sampleRate = 48000;

  ~Oscillator() = default;

  void prepare(float sr) {
    sampleRate = sr;
    if (line1.empty()) { line1.assign(4096, 0.0f); line2.assign(4096, 0.0f); }
  }

  void needTwist() { if (!twist) twist = new (amalthea_rt::current->take(sizeof(TwistEngine),alignof(TwistEngine))) TwistEngine(); }

  void reset(uint32_t seed, bool retrigger) {
    rng.seed(seed);
    for (int u = 0; u < kMaxUnison; ++u) {
      phase[u] = retrigger ? 0.0f : rng.unit();
      subPhase[u] = phase[u] * 0.5f;
      syncPhase[u] = phase[u];
      held[u] = rng.bipolar();
    }
    m1Phase = m2Phase = m3Phase = 0;
    fb1 = fb2 = last = 0;
  }

  /// A key down: what a string or a Twist does at the start of a note.
  void strike(int type) {
    if (type == O_string) burst = 1 << 30;  // set to one period on the next sample
    if (type == O_twist && twist) twist->mods.trigger = 1.0f;
  }

  float tick(int type, float hz, float pm, const float *p, int voices, float detuneCents,
             int deform, float sr, float lowCutHz, float highCutHz) {
    voices = hasUnison(type) ? clampi(voices, 1, kMaxUnison) : 1;
    float sum = 0.0f;
    for (int u = 0; u < voices; ++u) {
      const float spread = voices > 1 ? (2.0f * u / (voices - 1) - 1.0f) : 0.0f;
      const float f = hz * std::pow(2.0f, spread * detuneCents / 1200.0f);
      sum += voice(type, u, f, pm, p, deform, sr);
    }
    float out = sum / std::sqrt((float)voices);
    // The built-in cuts, where the slot has them switched on.
    if (lowCutHz > 0) {
      const float g = 1.0f - std::exp(-kTwoPi * lowCutHz / sr);
      lowState += g * (out - lowState);
      out -= lowState;
    }
    if (highCutHz > 0) {
      const float g = 1.0f - std::exp(-kTwoPi * clampf(highCutHz, 20, sr * 0.45f) / sr);
      highState += g * (out - highState);
      out = highState;
    }
    last = out;
    return out;
  }

  float voice(int type, int u, float hz, float pm, const float *p, int deform, float sr) {
    const float dt = clampf(hz / sr, 0.0f, 0.49f);
    switch (type) {
      case O_classic: return classic(u, dt, pm, p);
      case O_sine: return sine(u, dt, pm, p, deform);
      case O_wavetable: return wavetable(u, hz, dt, pm, p, sr, false);
      case O_shnoise: return shNoise(u, dt, p);
      case O_fm3: return fm3(u, hz, dt, pm, p, sr);
      case O_fm2: return fm2(u, hz, dt, pm, p, sr);
      case O_window: return wavetable(u, hz, dt, pm, p, sr, true);
      case O_modern: return modern(u, dt, pm, p, deform);
      case O_string: return string(hz, p, sr);
      case O_twist: return twistTick(hz, p);
      case O_alias: return alias(u, dt, pm, p);
      case O_audioin: return 0.0f;  // a node has no input of its own
      default: return sine(u, dt, pm, p, 0);
    }
  }

  float advance(int u, float dt) {
    float ph = phase[u] + dt;
    if (ph >= 1.0f) ph -= 1.0f;
    phase[u] = ph;
    return ph;
  }

  /// Pulse through saw to dual saw, the sub an octave down, and hard sync
  /// against the slot's own base pitch.
  float classic(int u, float dt, float pm, const float *p) {
    const float shape = clampf(p[0], -1, 1);
    const float width = clampf(p[1], 0.02f, 0.98f);
    const float width2 = clampf(p[2], 0.02f, 0.98f);
    const float sub = clampf(p[3], 0, 1);
    const float syncSemis = clampf(p[4], 0, 60);
    float base = syncPhase[u] + dt;
    bool wrapped = false;
    if (base >= 1.0f) { base -= 1.0f; wrapped = true; }
    syncPhase[u] = base;
    const float rdt = clampf(dt * std::pow(2.0f, syncSemis / 12.0f), 0.0f, 0.49f);
    float ph = phase[u] + rdt;
    if (ph >= 1.0f) ph -= 1.0f;
    if (wrapped && syncSemis > 0.01f) ph = base * (rdt / dt);
    phase[u] = ph;
    // Width 2 squeezes the second half of the cycle toward the first.
    float t = wrap01(ph + pm);
    if (width2 != 0.5f) {
      t = t < width2 ? 0.5f * t / width2 : 0.5f + 0.5f * (t - width2) / (1.0f - width2);
    }
    const float saw = 2.0f * t - 1.0f - blep(t, rdt);
    const float pulse = (t < width ? 1.0f : -1.0f) + blep(t, rdt) - blep(wrap01(t - width + 1.0f), rdt);
    const float t2 = wrap01(t + width);
    const float saw2 = 2.0f * t2 - 1.0f - blep(t2, rdt);
    const float mainOut = shape < 0 ? saw * (1.0f + shape) + pulse * (-shape)
                                    : saw * (1.0f - shape) + 0.5f * (saw + saw2) * shape;
    float sp = subPhase[u] + dt * 0.5f;
    if (sp >= 1.0f) sp -= 1.0f;
    subPhase[u] = sp;
    const float sdt = dt * 0.5f;
    const float subOut = (sp < width ? 1.0f : -1.0f) + blep(sp, sdt) - blep(wrap01(sp - width + 1.0f), sdt);
    return mainOut * (1.0f - sub) + subOut * sub;
  }

  /// Saw, pulse and a third wave mixed by signed amounts, with hard sync.
  float modern(int u, float dt, float pm, const float *p, int deform) {
    const float a = clampf(p[0], -1, 1), b = clampf(p[1], -1, 1), c = clampf(p[2], -1, 1);
    const float width = clampf(p[3], 0.02f, 0.98f);
    const float syncSemis = clampf(p[4], 0, 60);
    float base = syncPhase[u] + dt;
    bool wrapped = false;
    if (base >= 1.0f) { base -= 1.0f; wrapped = true; }
    syncPhase[u] = base;
    const float rdt = clampf(dt * std::pow(2.0f, syncSemis / 12.0f), 0.0f, 0.49f);
    float ph = phase[u] + rdt;
    if (ph >= 1.0f) ph -= 1.0f;
    if (wrapped && syncSemis > 0.01f) ph = base * (rdt / dt);
    phase[u] = ph;
    const float t = wrap01(ph + pm);
    const float saw = 2.0f * t - 1.0f - blep(t, rdt);
    const float pulse = (t < width ? 1.0f : -1.0f) + blep(t, rdt) - blep(wrap01(t - width + 1.0f), rdt);
    // The third wave by the low bits of its deform field: 0 triangle,
    // 1 square, 2 sine (ENUMS.md 5); bit 1024 plays it as a sub-oscillator,
    // at half the pitch.
    float tt = t, tdt = rdt;
    if (deform & 1024) {
      float sp = subPhase[u] + dt * 0.5f;
      if (sp >= 1.0f) sp -= 1.0f;
      subPhase[u] = sp;
      tt = sp; tdt = dt * 0.5f;
    }
    float third;
    switch (deform & 3) {
      case 2: third = std::sin(kTwoPi * tt); break;
      case 1: third = (tt < 0.5f ? 1.0f : -1.0f) + blep(tt, tdt) - blep(wrap01(tt + 0.5f), tdt); break;
      default: third = tt < 0.5f ? 4.0f * tt - 1.0f : 3.0f - 4.0f * tt; break;
    }
    return clampf(a * saw + b * pulse + c * third, -2.0f, 2.0f);
  }

  /// A sine in one of its 28 shapes, with feedback; the vintage option, a
  /// bit of the deform field, averages the last two outputs.
  float sine(int u, float dt, float pm, const float *p, int deform) {
    const int shape = (int)std::round(p[0]);
    const float fb = clampf(p[1], -4, 4);
    const float t = advance(u, dt);
    const float feedback = (deform & 1) ? 0.5f * (fb1 + fb2) : fb1;
    const float out = sineShape(shape, wrap01(t + pm + fb * feedback / kTwoPi));
    fb2 = fb1; fb1 = out;
    return out;
  }

  /// Frames read and crossfaded by Morph from the mip level whose top
  /// harmonic stays under Nyquist. Skew, saturation and formant as the
  /// manual describes them. Window restarts the frame inside a window.
  float wavetable(int u, float hz, float dt, float pm, const float *p, float sr, bool window) {
    const float t = wrap01(advance(u, dt) + pm);
    if (!table || table->mips.empty()) return std::sin(kTwoPi * t);
    const float morph = clampf(p[0], 0, 1);
    float read;
    if (window) {
      read = wrap01(t * std::pow(2.0f, clampf(p[1], -60, 60) / 12.0f));
    } else {
      // Horizontal skew bends the read position; formant compresses the
      // cycle and leaves the rest silent.
      const float skewH = clampf(p[4], -1, 1);
      float x = t;
      if (skewH != 0) x = std::pow(x, std::pow(2.0f, -skewH * 2.0f));
      x *= std::pow(2.0f, clampf(p[3], 0, 60) / 12.0f);
      if (x >= 1.0f) return 0.0f;
      read = x;
    }
    int level = 0, size = table->frameSize;
    while (size > 16 && hz * (size / 2) > sr * 0.5f && level + 1 < (int)table->mips.size()) {
      size /= 2; ++level;
    }
    const std::vector<float> &m = table->mips[level];
    const float pos = morph * (table->frames - 1);
    const int f0 = (int)pos;
    const int f1 = f0 + 1 < table->frames ? f0 + 1 : f0;
    const float ff = window ? 0.0f : pos - f0;
    const float x = read * size;
    const int i0 = (int)x % size, i1 = (i0 + 1) % size;
    const float fx = x - std::floor(x);
    auto at = [&](int frame, int i) { return m[(size_t)frame * size + i]; };
    const float a = at(f0, i0) + (at(f0, i1) - at(f0, i0)) * fx;
    const float b = at(f1, i0) + (at(f1, i1) - at(f1, i0)) * fx;
    float out = a + (b - a) * ff;
    if (window) {
      const int w = clampi((int)std::round(p[2]), 0, 8);
      out *= windowShape(w, t);
    } else {
      const float skewV = clampf(p[1], -1, 1);
      if (skewV != 0) out = out + skewV * (1.0f - out * out) * 0.5f;
      const float sat = clampf(p[2], 0, 1);
      if (sat > 0) out = std::tanh(out * (1.0f + sat * 4.0f)) / std::tanh(1.0f + sat * 4.0f);
    }
    return out;
  }

  /// The nine windows the manual names, in its order: triangle, cosine,
  /// three blends of the two, sawtooth, sine, square and rectangle.
  static float windowShape(int w, float t) {
    const float tri = 1.0f - std::fabs(2.0f * t - 1.0f);
    const float cosine = 0.5f - 0.5f * std::cos(kTwoPi * t);
    switch (w) {
      case 0: return tri;
      case 1: return cosine;
      case 2: return 0.75f * tri + 0.25f * cosine;
      case 3: return 0.5f * tri + 0.5f * cosine;
      case 4: return 0.25f * tri + 0.75f * cosine;
      case 5: return 1.0f - t;
      case 6: return std::sin(3.14159265f * t);
      case 7: return t < 0.5f ? 1.0f : 0.0f;
      default: return (t > 0.05f && t < 0.95f) ? 1.0f : 0.0f;
    }
  }

  /// A pulse whose two levels are drawn: correlation +1 alternates the
  /// extremes, -1 holds, nought draws freely. Synced like the others.
  float shNoise(int u, float dt, const float *p) {
    const float corr = clampf(p[0], -1, 1);
    const float width = clampf(p[1], 0.02f, 0.98f);
    const float syncSemis = clampf(p[4], 0, 60);
    const float rdt = clampf(dt * std::pow(2.0f, syncSemis / 12.0f), 0.0f, 0.49f);
    const float before = phase[u];
    float t = before + rdt;
    if (t >= 1.0f) t -= 1.0f;
    phase[u] = t;
    if ((before < width && t >= width) || t < before) {
      held[u] = clampf(-corr * held[u] + (1.0f - std::fabs(corr)) * rng.bipolar(), -1, 1);
    }
    return held[u];
  }

  /// A sine carrier and two sine modulators at whole ratios, in parallel.
  float fm2(int u, float hz, float dt, float pm, const float *p, float sr) {
    const float a1 = clampf(p[0], 0, 1), a2 = clampf(p[2], 0, 1);
    const float r1 = clampf(std::round(p[1]), 1, 32), r2 = clampf(std::round(p[3]), 1, 32);
    const float offsetHz = p[4];
    const float phase0 = clampf(p[5], 0, 1);
    const float fb = clampf(p[6], -4, 4);
    const float t = advance(u, dt);
    if (u == 0) {
      m1Phase = wrap01(m1Phase + (hz * r1 + offsetHz) / sr);
      m2Phase = wrap01(m2Phase + (hz * r2 - offsetHz) / sr);
    }
    const float m1 = std::sin(kTwoPi * (m1Phase + phase0));
    const float m2 = std::sin(kTwoPi * (m2Phase + phase0));
    const float i1 = 12.566f * a1 * a1, i2 = 12.566f * a2 * a2;
    const float out = std::sin(kTwoPi * (t + pm) + i1 * m1 + i2 * m2 + fb * 0.5f * (fb1 + fb2));
    fb2 = fb1; fb1 = out;
    return out;
  }

  /// FM2 with free ratios and a third modulator at a fixed frequency.
  float fm3(int u, float hz, float dt, float pm, const float *p, float sr) {
    const float a1 = clampf(p[0], 0, 1), a2 = clampf(p[2], 0, 1), a3 = clampf(p[4], 0, 1);
    const float r1 = clampf(std::fabs(p[1]), 0.0f, 32.0f), r2 = clampf(std::fabs(p[3]), 0.0f, 32.0f);
    const float f3 = noteHz(clampf(p[5], -72, 72));
    const float fb = clampf(p[6], -4, 4);
    const float t = advance(u, dt);
    if (u == 0) {
      m1Phase = wrap01(m1Phase + hz * r1 / sr);
      m2Phase = wrap01(m2Phase + hz * r2 / sr);
      m3Phase = wrap01(m3Phase + f3 / sr);
    }
    const float i1 = 12.566f * a1 * a1, i2 = 12.566f * a2 * a2, i3 = 12.566f * a3 * a3;
    const float out = std::sin(kTwoPi * (t + pm) + i1 * std::sin(kTwoPi * m1Phase)
                               + i2 * std::sin(kTwoPi * m2Phase)
                               + i3 * std::sin(kTwoPi * m3Phase) + fb * 0.5f * (fb1 + fb2));
    fb2 = fb1; fb1 = out;
    return out;
  }

  /// Two plucked or bowed strings. Exciters 0-6 are bursts that fill one
  /// period at the key down; 7-13 are the same seven played continuously
  /// while the key is held (ENUMS.md 3.2). The seven waveforms are this
  /// instrument's reading of the manual's "noise, pink noise, ramp, etc.":
  /// noise, pink noise, sine, ramp, square, pulse, a click.
  float string(float hz, const float *p, float sr) {
    const int mode = clampi((int)std::round(p[0]), 0, 13);
    const float level = clampf(std::fabs(p[1]), 0, 1);
    // Decays are stored 0..1; read on the -1..1 scale the loop gain uses.
    const float decay1 = clampf(p[2], 0, 1) * 2 - 1, decay2 = clampf(p[3], 0, 1) * 2 - 1;
    // String 2's detune, read as the manual's percent of a hundred cents.
    const float detuneCents = clampf(p[4], -12, 12) * 100.0f;
    const float balance = clampf(p[5], -1, 1);
    const float stiff = clampf(p[6], -1, 1);
    const float d1 = clampf(sr / clampf(hz, 12, 8000), 2, 4000);
    const float d2 = clampf(sr / clampf(hz * std::pow(2.0f, detuneCents / 1200.0f), 12, 8000), 2, 4000);
    const bool continuous = mode >= 7;
    if (burst == (1 << 30)) burst = (int)d1;
    float exc = 0;
    if (continuous || burst > 0) {
      if (burst > 0) --burst;
      exciterPhase = wrap01(exciterPhase + hz / sr);
      const float white = rng.bipolar();
      pink = 0.97f * pink + 0.03f * white;
      switch (mode % 7) {
        case 0: exc = white; break;
        case 1: exc = pink * 4.0f; break;
        case 2: exc = std::sin(kTwoPi * exciterPhase); break;
        case 3: exc = 2.0f * exciterPhase - 1.0f; break;
        case 4: exc = exciterPhase < 0.5f ? 1.0f : -1.0f; break;
        case 5: exc = exciterPhase < 0.15f ? 1.0f : -0.18f; break;
        default: exc = exciterPhase < 0.02f ? 1.0f : 0.0f; break;
      }
      exc *= level * (continuous ? 0.15f : 1.0f);
    }
    // Decay: the loop gain, from a short pluck to an endless ring.
    auto loopGain = [](float d) { const float x = d * 0.5f + 0.5f; return 0.9f + 0.0999f * std::sqrt(x); };
    // Stiffness: below nought a low pass in the loop, above it a high pass.
    const float lpCoeff = stiff < 0 ? 0.5f + 0.5f * (1.0f + stiff) : 1.0f;
    auto readLine = [&](amalthea_rt::Buffer<float> &line, int at, float delay) {
      float rp = at - delay;
      if (rp < 0) rp += 4096.0f;
      const int i0 = (int)rp & 4095, i1 = (i0 + 1) & 4095;
      const float fr = rp - std::floor(rp);
      return line[i0] + (line[i1] - line[i0]) * fr;
    };
    float y1 = readLine(line1, at1, d1), y2 = readLine(line2, at2, d2);
    lp1 += lpCoeff * (y1 - lp1);
    lp2 += lpCoeff * (y2 - lp2);
    float v1 = stiff > 0 ? y1 - stiff * 0.5f * lp1 : lp1;
    float v2 = stiff > 0 ? y2 - stiff * 0.5f * lp2 : lp2;
    line1[at1] = clampf(exc + v1 * loopGain(decay1), -4, 4);
    line2[at2] = clampf(exc + v2 * loopGain(decay2), -4, 4);
    at1 = (at1 + 1) & 4095;
    at2 = (at2 + 1) & 4095;
    const float b = 0.5f + 0.5f * balance;
    return y1 * (1.0f - b) + y2 * b;
  }

  /// Deliberately aliasing: an eight-bit phase read through a shape, wrapped
  /// instead of clipped, masked bit by bit and crushed. Shapes 0-3 are sine,
  /// ramp, pulse and noise; 4-16 the quadrant shapes Sine has; 17 the
  /// additive one, read here as its default saw spectrum (ENUMS.md 3.4).
  float alias(int u, float dt, float pm, const float *p) {
    const int shape = clampi((int)std::round(p[0]), 0, 17);
    const float wrap = clampf(p[1], -1, 1), mask = clampf(p[2], -1, 1);
    const float threshold = clampf(p[3], -1, 1);
    const float bits = clampf(p[4], 1, 8);
    const float t = wrap01(advance(u, dt) + pm);
    const uint8_t phase8 = (uint8_t)(t * 255.0f);
    const float turn = 0.5f + 0.5f * threshold;
    float v;
    switch (shape) {
      case 0: v = std::sin(kTwoPi * (phase8 / 256.0f)); break;
      case 1: v = phase8 / 256.0f < turn ? (phase8 / 256.0f) / turn * 2.0f - 1.0f
                                         : 1.0f - 2.0f * (phase8 / 256.0f - turn) / (1.0f - turn); break;
      case 2: v = phase8 / 256.0f < turn ? 1.0f : -1.0f; break;
      case 3: v = held[u] = (phase8 == 0 ? rng.bipolar() : held[u]); break;
      case 17: {
        // The patch's own sixteen harmonics (its <extraoscdata>), a saw's
        // spectrum when it has none.
        v = 0;
        for (int h = 1; h <= 16; ++h)
          v += std::sin(kTwoPi * h * (phase8 / 256.0f)) * (additive ? additive[h - 1] : 1.0f / h);
        v *= 0.9f;
        break;
      }
      default: v = sineShape(shape - 4, phase8 / 256.0f); break;
    }
    // Wrap: amplified, then folded back around rather than clipped.
    const float gain = 1.0f + std::fabs(wrap) * 15.0f;
    float w = v * gain * 0.5f + 0.5f;
    w = w - std::floor(w);
    v = w * 2.0f - 1.0f;
    // Mask: the sample as a byte, XORed with a mask drawn from the control.
    const uint8_t byte = (uint8_t)clampi((int)((v * 0.5f + 0.5f) * 255.0f), 0, 255);
    const uint8_t m = (uint8_t)(std::fabs(mask) * 255.0f);
    v = (byte ^ m) / 127.5f - 1.0f;
    const float q = std::pow(2.0f, bits) - 1.0f;
    return std::round(v * q) / q;
  }

  /// Plaits, one of its sixteen original models, played at the note.
  float twistTick(float hz, const float *p) {
    if (!twist) return 0.0f;
    TwistEngine &e = *twist;
    if (e.taken >= (int)plaits::kBlockSize) {
      e.patch.engine = 8 + clampi((int)std::round(p[0]), 0, 15);
      // Harmonics, timbre and morph are stored on +-1 (ENUMS.md 3.3): the
      // middle of the knob is nought.
      e.patch.harmonics = clampf(0.5f + 0.5f * p[1], 0, 1);
      e.patch.timbre = clampf(0.5f + 0.5f * p[2], 0, 1);
      e.patch.morph = clampf(0.5f + 0.5f * p[3], 0, 1);
      e.mods.trigger_patched = twistLpg;
      e.mods.level_patched = false;
      e.patch.note = 69.0f + 12.0f * std::log2(clampf(hz, 2.0f, 20000.0f) / 440.0f);
      e.patch.decay = clampf(p[6], 0, 1);
      e.patch.lpg_colour = clampf(p[5], 0, 1);
      e.voice.Render(e.patch, e.mods, e.block, plaits::kBlockSize);
      e.mods.trigger = 0.0f;
      e.taken = 0;
    }
    const plaits::Voice::Frame &f = e.block[e.taken++];
    // Mix: main toward aux, as the manual's Mix control blends them.
    const float mix = 0.5f + 0.5f * clampf(p[4], -1, 1);
    return ((f.out / 32768.0f) * (1.0f - mix) + (f.aux / 32768.0f) * mix);
  }
};

}  // namespace resurgent
}  // namespace vascular

#endif
