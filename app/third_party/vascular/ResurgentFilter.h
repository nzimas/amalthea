/*
 * resurgent's filters and waveshapers.
 *
 * Every filter the Surge XT manual describes (SPEC-from-manual.md 3.3), built
 * from the literature rather than from Surge:
 *  - the TPT state-variable filter and the TPT one-pole, ladder, Korg 35 and
 *    diode-ladder topologies: V. Zavalishin, "The Art of VA Filter Design",
 *    rev. 2.1.2, 2020, chapters 3-5 and 5.10;
 *  - the nonlinear ladder: A. Huovilainen, "Non-linear digital implementation
 *    of the Moog ladder filter", DAFx 2004;
 *  - cutoff and resonance warping: J. Chowdhury, "Stable structures for
 *    nonlinear biquad filters", DAFx 2020, sections 3 and 4 -- the manual
 *    names this paper for these two types;
 *  - the three-pole filter with a resonance stage in its loop, after Ian
 *    Fritz's published Threeler description, which the manual names.
 * The manual gives the types and their subtypes but not the numbers a patch
 * stores them under; those are mapped in `filterKind`, from
 * Docs/resurgent/ENUMS.md.
 */
#ifndef VASCULAR_RESURGENT_FILTER_H
#define VASCULAR_RESURGENT_FILTER_H

#include <algorithm>
#include <cmath>
#include "RtBuffer.h"

#include "ResurgentOsc.h"

namespace vascular {
namespace resurgent {

enum FilterKind {
  F_off,
  F_lp12, F_lp24, F_hp12, F_hp24, F_bp12, F_bp24, F_notch12, F_notch24,
  F_legacyLadder, F_vintageLadder,
  F_k35lp, F_k35hp,
  F_diode,
  F_obxd12lp, F_obxd12hp, F_obxd12bp, F_obxd12notch, F_obxd24,
  F_cutoffWarpLp, F_cutoffWarpHp, F_cutoffWarpBp, F_cutoffWarpNotch, F_cutoffWarpAp,
  F_resWarpLp, F_resWarpHp, F_resWarpBp, F_resWarpNotch, F_resWarpAp,
  F_triPole, F_allpass,
  F_combPlus, F_combMinus, F_sampleHold,
};

/// What a patch's filter type number means (Docs/resurgent/ENUMS.md 1.2,
/// from 3,561 patches: subtype counts per number against the manual's
/// subtype lists, and patch names). Strong for 0-10, 15, 16, 21, 26 and 33;
/// the OB-Xd, BP 24 and Notch 24 places are the least certain. An unknown
/// number is a 12 dB low pass.
inline FilterKind filterKind(int type) {
  static const FilterKind table[] = {
    F_off,             // 0
    F_lp12,            // 1
    F_lp24,            // 2
    F_legacyLadder,    // 3
    F_hp12,            // 4
    F_hp24,            // 5
    F_bp12,            // 6
    F_notch12,         // 7
    F_combPlus,        // 8
    F_sampleHold,      // 9
    F_vintageLadder,   // 10
    F_obxd12lp,        // 11
    F_obxd24,          // 12
    F_k35lp,           // 13
    F_k35hp,           // 14
    F_diode,           // 15
    F_cutoffWarpLp,    // 16
    F_cutoffWarpHp,    // 17
    F_cutoffWarpNotch, // 18
    F_cutoffWarpBp,    // 19
    F_obxd12hp,        // 20
    F_combMinus,       // 21
    F_obxd12bp,        // 22
    F_bp24,            // 23
    F_obxd12notch,     // 24
    F_notch24,         // 25
    F_allpass,         // 26
    F_cutoffWarpAp,    // 27
    F_resWarpLp,       // 28
    F_resWarpHp,       // 29
    F_resWarpNotch,    // 30
    F_resWarpBp,       // 31
    F_resWarpAp,       // 32
    F_triPole,         // 33
  };
  if (type < 0 || type >= (int)(sizeof(table) / sizeof(table[0]))) return F_lp12;
  return table[type];
}

/// The TPT state-variable filter: low, band and high outputs at once.
struct Svf {
  float s1 = 0, s2 = 0;
  void reset() { s1 = s2 = 0; }
  void tick(float x, float g, float k, float &lp, float &bp, float &hp) {
    const float h = 1.0f / (1.0f + g * (g + k));
    hp = (x - (g + k) * s1 - s2) * h;
    bp = g * hp + s1;
    s1 = g * hp + bp;
    lp = g * bp + s2;
    s2 = g * bp + lp;
  }
};

/// The TPT one-pole, a ladder's stage.
struct OnePole {
  float s = 0;
  void reset() { s = 0; }
  float lp(float x, float G) { const float v = (x - s) * G; const float y = v + s; s = y + v; return y; }
};

struct Filter {
  Svf a, b;
  OnePole stage[4];
  float y4 = 0, held = 0, holdPhase = 0, k35 = 0;
  amalthea_rt::Buffer<float> comb;
  int combAt = 0;
  float ap1 = 0, ap2 = 0;

  void reset() {
    a.reset(); b.reset();
    for (auto &s : stage) s.reset();
    y4 = held = holdPhase = k35 = ap1 = ap2 = 0;
    if (comb.empty()) comb.assign(4096, 0.0f);
    std::fill(comb.begin(), comb.end(), 0.0f);
    combAt = 0;
  }

  /// The SVF with its subtype's character: 0 standard, 1 driven (the input
  /// and the band state saturate), 2 clean (less resonance at the top).
  void svf(Svf &f, float x, float g, float res, int subtype, float &lp, float &bp, float &hp) {
    const float top = subtype == 2 ? 1.9f : 1.995f;
    const float k = 2.0f - top * res;
    if (subtype == 1) x = std::tanh(x * 1.5f) / 1.2f;
    f.tick(x, g, k, lp, bp, hp);
    if (subtype == 1) f.s1 = std::tanh(f.s1);
  }

  /// A ladder of four TPT one-poles with the output fed back through tanh,
  /// one sample late; `tap` picks the stage, 0..3 for 6..24 dB.
  float ladder(float x, float g, float k, int tap, bool compensate, float drive) {
    const float G = g / (1.0f + g);
    const float u = std::tanh(drive * (x - k * y4));
    const float s1 = stage[0].lp(u, G);
    const float s2 = stage[1].lp(s1, G);
    const float s3 = stage[2].lp(s2, G);
    const float s4 = stage[3].lp(s3, G);
    y4 = s4;
    const float taps[4] = {s1, s2, s3, s4};
    const float out = taps[clampi(tap, 0, 3)] / drive;
    return compensate ? out * (1.0f + k * 0.25f) : out;
  }

  /// A biquad-shaped section whose cutoff (or resonance) is pushed by the
  /// signal through a saturator: stages 1-4 and the nonlinearity by subtype.
  float warp(float x, float g, float res, int subtype, bool resonanceWarp, int mode) {
    const int stages = 1 + subtype % 4;
    const int nl = subtype / 4;  // 0 tanh, 1 soft clip, 2 a gentler curve
    auto shape = [&](float v) {
      switch (nl) {
        case 1: return clampf(v - v * v * v / 3.0f, -0.6667f, 0.6667f) * 1.5f;
        case 2: return v / (1.0f + std::fabs(v));
        default: return std::tanh(v);
      }
    };
    float y = x;
    for (int s = 0; s < stages; ++s) {
      Svf &f = s % 2 == 0 ? a : b;
      const float level = std::fabs(shape(y));
      float gg = g, rr = res;
      if (resonanceWarp) rr = res * (1.0f - 0.7f * level);
      else gg = g * (1.0f + 1.5f * level);
      float lp, bp, hp;
      f.tick(y, gg, 2.0f - 1.98f * rr, lp, bp, hp);
      switch (mode) {
        case 1: y = hp; break;
        case 2: y = bp * (2.0f - 1.98f * rr); break;
        case 3: y = y - (2.0f - 1.98f * rr) * bp; break;
        case 4: y = lp - (2.0f - 1.98f * rr) * bp + hp; break;
        default: y = lp; break;
      }
    }
    return y;
  }

  float tick(float x, FilterKind kind, int subtype, float cutoffHz, float res, float sr) {
    if (kind == F_off) return x;
    const float fc = clampf(cutoffHz, 10.0f, sr * 0.45f);
    const float g = std::tan(3.14159265f * fc / sr);
    res = clampf(res, 0.0f, 1.0f);
    float lp, bp, hp, lp2, bp2, hp2;
    switch (kind) {
      case F_lp12: svf(a, x, g, res, subtype, lp, bp, hp); return lp;
      case F_hp12: svf(a, x, g, res, subtype, lp, bp, hp); return hp;
      case F_bp12: svf(a, x, g, res, subtype, lp, bp, hp); return bp * (2.0f - 1.98f * res);
      case F_notch12: svf(a, x, g, res, subtype, lp, bp, hp); return lp + hp;
      case F_lp24: case F_hp24: case F_bp24: case F_notch24: {
        a.tick(subtype == 1 ? std::tanh(x * 1.5f) / 1.2f : x, g, 1.414f, lp, bp, hp);
        const float in2 = kind == F_lp24 ? lp : kind == F_hp24 ? hp : kind == F_bp24 ? bp * 1.414f : lp + hp;
        svf(b, in2, g, res, subtype, lp2, bp2, hp2);
        if (kind == F_lp24) return lp2;
        if (kind == F_hp24) return hp2;
        if (kind == F_bp24) return bp2 * (2.0f - 1.98f * res);
        return lp2 + hp2;
      }
      case F_legacyLadder: return ladder(x, g, res * 3.9f, subtype, false, 1.0f);
      case F_vintageLadder:
        // Types 1 and 2, each plain or gain-compensated; the second type
        // drives the stages a little harder.
        return ladder(x, g, res * 4.0f, 3, subtype % 2 == 1, subtype >= 2 ? 1.6f : 1.0f);
      case F_diode: return ladder(x, g, res * 3.0f, subtype, false, 1.2f) * 1.2f;
      case F_obxd24: return ladder(x, g, res * 3.6f, subtype, true, 1.3f);
      case F_k35lp: case F_k35hp: {
        // Two one-poles with the output fed back through a saturator whose
        // strength is the subtype: none, mild, moderate, heavy, extreme.
        const float G = g / (1.0f + g);
        const float sat = 1.0f + subtype * 1.2f;
        const float k = res * 1.98f;
        const float fb = std::tanh(sat * k35) / sat;
        const float y1 = stage[0].lp(x - k * fb, G);
        const float y2 = stage[1].lp(y1, G);
        k35 = y2;
        return kind == F_k35lp ? y2 * (1.0f + k * 0.3f) : (x - y2);
      }
      case F_obxd12lp: case F_obxd12hp: case F_obxd12bp: case F_obxd12notch: {
        // The 2-pole, with its state pushed through tanh: subtype 1 pushes
        // harder and self-oscillates sooner.
        const float k = 2.0f - (subtype == 1 ? 2.0f : 1.95f) * res;
        a.tick(std::tanh(x), g, k, lp, bp, hp);
        a.s1 = std::tanh(a.s1 * (subtype == 1 ? 1.3f : 1.0f));
        if (kind == F_obxd12lp) return lp;
        if (kind == F_obxd12hp) return hp;
        if (kind == F_obxd12bp) return bp * k;
        return lp + hp;
      }
      case F_cutoffWarpLp: return warp(x, g, res, subtype, false, 0);
      case F_cutoffWarpHp: return warp(x, g, res, subtype, false, 1);
      case F_cutoffWarpBp: return warp(x, g, res, subtype, false, 2);
      case F_cutoffWarpNotch: return warp(x, g, res, subtype, false, 3);
      case F_cutoffWarpAp: return warp(x, g, res, subtype, false, 4);
      case F_resWarpLp: return warp(x, g, res, subtype, true, 0);
      case F_resWarpHp: return warp(x, g, res, subtype, true, 1);
      case F_resWarpBp: return warp(x, g, res, subtype, true, 2);
      case F_resWarpNotch: return warp(x, g, res, subtype, true, 3);
      case F_resWarpAp: return warp(x, g, res, subtype, true, 4);
      case F_triPole: {
        // Three stages, each low or high by the subtype's letter pattern
        // (LLL, LHL, HLH, HHH), in a loop whose resonance stage feeds the
        // first; the output from the first, second or third stage.
        static const int pattern[4][3] = {{0, 0, 0}, {0, 1, 0}, {1, 0, 1}, {1, 1, 1}};
        const int which = clampi(subtype % 4, 0, 3), tap = clampi(subtype / 4, 0, 2);
        const float G = g / (1.0f + g);
        float v = std::tanh(x - res * 3.5f * y4);
        float outs[3];
        for (int s = 0; s < 3; ++s) {
          const float l = stage[s].lp(v, G);
          v = pattern[which][s] ? v - l : l;
          outs[s] = v;
        }
        y4 = v;
        return outs[tap] * (1.0f + res);
      }
      case F_allpass: {
        // Two first-order allpasses at the cutoff.
        const float c = (g - 1.0f) / (g + 1.0f);
        const float y1 = c * x + ap1; ap1 = x - c * y1;
        const float y2 = c * y1 + ap2; ap2 = y1 - c * y2;
        return y2;
      }
      case F_combPlus: case F_combMinus: {
        // A delay of one period, fed back by the resonance; minus inverts
        // the loop and sounds an octave down. Subtype 0 is half wet.
        const float delay = clampf(sr / fc, 2.0f, 4000.0f);
        float rp = combAt - delay;
        if (rp < 0) rp += 4096.0f;
        const int i0 = (int)rp & 4095, i1 = (i0 + 1) & 4095;
        const float fr = rp - std::floor(rp);
        const float d = comb[i0] + (comb[i1] - comb[i0]) * fr;
        const float sign = kind == F_combMinus ? -1.0f : 1.0f;
        comb[combAt] = std::tanh(x + sign * res * 0.98f * d);
        combAt = (combAt + 1) & 4095;
        return subtype == 0 ? 0.5f * (x + d) : d;
      }
      case F_sampleHold: {
        holdPhase += fc / sr;
        if (holdPhase >= 1.0f) { holdPhase -= 1.0f; held = x + res * (x - held); }
        return held;
      }
      default: return x;
    }
  }
};

/// The waveshaper's curves. The manual names five (Soft, Hard, Asymmetric,
/// Sine, Digital) and counts forty-three; the rest are this instrument's
/// readings of the families the changelog names -- harmonic extension,
/// rectifiers, wavefolders, trigonometric folders and noise. The number a
/// patch stores is mapped onto these in `shaperKind` (ENUMS.md).
enum ShaperKind {
  W_off, W_soft, W_hard, W_asym, W_sine, W_digital,
  W_cheby2, W_cheby3, W_cheby4, W_cheby5, W_addHarmonics, W_addOdd,
  W_fullRect, W_halfRectPos, W_halfRectNeg, W_softRect,
  W_fold, W_dualFold, W_westFold, W_sineFold, W_triFold, W_sineSquare,
  W_fuzz, W_fuzzSoft, W_fuzzHeavy, W_fuzzEdge, W_noisy,
  W_zamsat, W_ojd, W_softOne, W_cubic, W_tube, W_diodeClip, W_foldback,
  W_sinePlusX, W_tanhCubed, W_atan, W_hardSoft, W_bitFold, W_wrap,
  W_stairs, W_drive2, W_asymFold,
  kShaperCount
};

inline ShaperKind shaperKind(int type) {
  return (type >= 0 && type < kShaperCount) ? (ShaperKind)type : W_soft;
}

inline float shape(ShaperKind kind, float x, Rng &rng) {
  switch (kind) {
    case W_off: return x;
    case W_soft: return std::tanh(x);
    case W_hard: return clampf(x, -1, 1);
    case W_asym: return x >= 0 ? std::tanh(x) : std::tanh(0.5f * x) * 1.3f;
    case W_sine: return std::sin(x);
    case W_digital: return std::round(clampf(x, -1, 1) * 16.0f) / 16.0f;
    case W_cheby2: { const float c = clampf(x, -1, 1); return 2 * c * c - 1 + c; }
    case W_cheby3: { const float c = clampf(x, -1, 1); return 4 * c * c * c - 3 * c; }
    case W_cheby4: { const float c = clampf(x, -1, 1); const float c2 = c * c; return 8 * c2 * c2 - 8 * c2 + 1 + c; }
    case W_cheby5: { const float c = clampf(x, -1, 1); const float c2 = c * c; return 16 * c2 * c2 * c - 20 * c2 * c + 5 * c; }
    case W_addHarmonics: { const float c = clampf(x, -1, 1); return 0.5f * c + 0.25f * (2 * c * c - 1) + 0.25f * (4 * c * c * c - 3 * c); }
    case W_addOdd: { const float c = clampf(x, -1, 1); return 0.6f * c + 0.4f * (4 * c * c * c - 3 * c); }
    case W_fullRect: return std::fabs(std::tanh(x)) * 2.0f - 1.0f;
    case W_halfRectPos: return x > 0 ? std::tanh(x) : 0.0f;
    case W_halfRectNeg: return x < 0 ? std::tanh(x) : 0.0f;
    case W_softRect: return std::tanh(std::sqrt(x * x + 0.05f)) * 2.0f - 1.0f;
    case W_fold: { float v = x; while (v > 1 || v < -1) v = v > 1 ? 2 - v : -2 - v; return v; }
    case W_dualFold: { float v = x * 0.5f; for (int i = 0; i < 2; ++i) { while (v > 1 || v < -1) v = v > 1 ? 2 - v : -2 - v; v *= 1.5f; } return clampf(v, -1, 1); }
    case W_westFold: { float v = x; for (int i = 0; i < 4; ++i) v = std::fabs(v) > 0.5f ? (v > 0 ? 1.0f - v : -1.0f - v) * 2.0f : v; return clampf(v * 1.4f, -1, 1); }
    case W_sineFold: return std::sin(x * 1.5707963f);
    case W_triFold: { const float t = wrap01(x * 0.25f + 0.25f); return t < 0.5f ? 4 * t - 1 : 3 - 4 * t; }
    case W_sineSquare: { const float s = std::sin(x); return s * std::fabs(s); }
    case W_fuzz: return std::tanh(x * 4.0f);
    case W_fuzzSoft: return std::tanh(x * 2.0f + 0.2f) - std::tanh(0.2f);
    case W_fuzzHeavy: return clampf(std::tanh(x * 8.0f) * 1.1f, -1, 1);
    case W_fuzzEdge: return x > 0 ? std::tanh(x * 6.0f) : std::tanh(x * 1.5f);
    case W_noisy: return std::tanh(x) + 0.05f * std::fabs(x) * rng.bipolar();
    case W_zamsat: { const float a = std::fabs(x); return a < 1 ? x * (2 - a) : (x > 0 ? 1.0f : -1.0f); }
    case W_ojd: return x <= -1.7f ? -1.0f : x < -0.3f ? x + (x + 0.3f) * (x + 0.3f) / 2.8f
                     : x <= 0.9f ? x : x < 1.1f ? x - (x - 0.9f) * (x - 0.9f) / 0.8f : 1.0f;
    case W_softOne: return x / (1.0f + std::fabs(x));
    case W_cubic: { const float c = clampf(x, -1.5f, 1.5f); return c - c * c * c * 0.148f; }
    case W_tube: return x >= 0 ? 1.0f - std::exp(-x) : -0.8f * (1.0f - std::exp(x * 1.25f));
    case W_diodeClip: return x >= 0 ? std::tanh(x * 0.7f) : std::tanh(x * 2.0f) * 0.6f;
    case W_foldback: { const float t = 0.7f; return std::fabs(x) > t ? (std::fabs(std::fmod(x - t, t * 4.0f)) - t * 2.0f) * (x > 0 ? 1 : -1) * -1.0f : x; }
    case W_sinePlusX: return 0.5f * x + 0.5f * std::sin(x * 3.0f);
    case W_tanhCubed: { const float t = std::tanh(x); return t * t * t; }
    case W_atan: return std::atan(x) * 0.6366f;
    case W_hardSoft: return std::fabs(x) < 0.5f ? x : (x > 0 ? 0.5f + std::tanh((x - 0.5f) * 2) * 0.5f : -0.5f + std::tanh((x + 0.5f) * 2) * 0.5f);
    case W_bitFold: { const int q = (int)(clampf(x, -4, 4) * 32.0f); return ((q ^ 0x15) % 64) / 32.0f; }
    case W_wrap: return wrap01(x * 0.5f + 0.5f) * 2.0f - 1.0f;
    case W_stairs: return std::round(std::tanh(x) * 4.0f) / 4.0f;
    case W_drive2: return std::tanh(x * x * (x > 0 ? 1 : -1));
    case W_asymFold: { float v = x + 0.3f; while (v > 1 || v < -1) v = v > 1 ? 2 - v : -2 - v; return v - 0.3f; }
    default: return std::tanh(x);
  }
}

}  // namespace resurgent
}  // namespace vascular

#endif
