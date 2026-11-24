/*
 * resurgent's modulators: the two ADSRs and the LFOs with their DAHDSR.
 *
 * To the stage lists, ranges and behaviours the Surge XT manual gives
 * (SPEC-from-manual.md 5.2 and 6); the curves are this instrument's own.
 * Times arrive in the patch's units, log2 seconds, with -8 meaning nothing.
 */
#ifndef VASCULAR_RESURGENT_MOD_H
#define VASCULAR_RESURGENT_MOD_H

#include <cmath>

#include "ResurgentOsc.h"

namespace vascular {
namespace resurgent {

inline float stageSeconds(float raw) { return raw <= -7.99f ? 0.0f : std::pow(2.0f, raw); }

/// A digital stage's curve by the index a patch stores: a bow above the
/// line, the line, a bow below it, and a steeper one still.
inline float curve(float x, int shape) {
  x = clampf(x, 0, 1);
  switch (shape) {
    case 0: return std::sqrt(x);
    case 2: return x * x;
    case 3: return x * x * x;
    default: return x;
  }
}

struct Adsr {
  enum Stage { Idle, Attack, Decay, Sustain, Release };
  Stage stage = Idle;
  float level = 0, from = 0, t = 0;

  void gate(bool on) {
    if (on) { stage = Attack; from = level; t = 0; }
    else if (stage != Idle) { stage = Release; from = level; t = 0; }
  }

  /// `analog` is the patch's mode switch: the analog envelope charges toward
  /// a target as a capacitor does, so its attack bows and its decay and
  /// release fall exponentially, whatever the stage curves say.
  float tick(float dt, float a, float d, float s, float r, int as, int ds, int rs, bool analog,
             bool freezeRelease) {
    s = clampf(s, 0, 1);
    switch (stage) {
      case Idle: level = 0; break;
      case Attack: {
        const float len = stageSeconds(a);
        t += dt;
        if (analog) {
          const float tau = len <= 0 ? 0.0001f : len / 1.6f;
          level += (1.25f - level) * (1.0f - std::exp(-dt / tau));
          if (level >= 1.0f) { level = 1; stage = Decay; t = 0; }
        } else {
          const float x = len <= 0 ? 1.0f : t / len;
          level = from + (1.0f - from) * curve(x, as);
          if (x >= 1.0f) { stage = Decay; t = 0; level = 1; }
        }
        break;
      }
      case Decay: {
        const float len = stageSeconds(d);
        t += dt;
        if (analog) {
          const float tau = len <= 0 ? 0.0001f : len / 4.6f;
          level = s + (level - s) * std::exp(-dt / tau);
          if (std::fabs(level - s) < 0.0005f) { level = s; stage = Sustain; }
        } else {
          const float x = len <= 0 ? 1.0f : t / len;
          level = 1.0f - (1.0f - s) * curve(x, ds);
          if (x >= 1.0f) { stage = Sustain; level = s; }
        }
        break;
      }
      case Sustain: level = s; break;
      case Release: {
        const float len = stageSeconds(r);
        t += dt;
        if (freezeRelease) {
          // Held at the level the key let go at, for the whole release.
          level = from;
          if (t >= len) { stage = Idle; level = 0; }
        } else if (analog) {
          const float tau = len <= 0 ? 0.0001f : len / 4.6f;
          level *= std::exp(-dt / tau);
          if (level < 0.0002f) { level = 0; stage = Idle; }
        } else {
          const float x = len <= 0 ? 1.0f : t / len;
          level = from * (1.0f - curve(x, rs));
          if (x >= 1.0f) { stage = Idle; level = 0; }
        }
        break;
      }
    }
    return clampf(level, 0, 1);
  }
};

/// One LFO's multi-segment shape (ENUMS.md 4.5): segments of a duration,
/// from a start value to an end value through a control point. Edit mode 1
/// plays them as one cycle of an LFO; 0 as an envelope in time, looping by
/// the loop mode: 1 off, 2 on, 3 until the key goes up.
struct Mseg {
  static const int kMaxSegments = 32;
  int count = 0, editMode = 1, loopMode = 1, loopStart = -1, loopEnd = -1;
  // duration, v0, v1, cp position, cp value, type
  float seg[kMaxSegments][6] = {};

  float total() const {
    float t = 0;
    for (int i = 0; i < count; ++i) t += seg[i][0];
    return t;
  }

  /// The value a distance `x` into the segments. Linear is the one type the
  /// data settles (type 1); type 3 is drawn as a curve through its control
  /// point; 9 holds its start value as a stair; the rest are linear.
  float at(float x) const {
    if (count <= 0) return 0.0f;
    for (int i = 0; i < count; ++i) {
      const float d = seg[i][0];
      if (x <= d || i == count - 1) {
        const float f = d > 0 ? clampf(x / d, 0, 1) : 1.0f;
        const float v0 = seg[i][1], v1 = seg[i][2];
        const int type = (int)seg[i][5];
        if (type == 9) return v0;
        if (type == 3) {
          const float cp = seg[i][4];
          const float a = (1 - f) * (1 - f), b = 2 * (1 - f) * f, c = f * f;
          return a * v0 + b * cp + c * v1;
        }
        return v0 + (v1 - v0) * f;
      }
      x -= d;
    }
    return seg[count - 1][2];
  }

  float start(int i) const {
    float t = 0;
    for (int k = 0; k < i && k < count; ++k) t += seg[k][0];
    return t;
  }
};

/// The fields of one LFO, by their offset from its shape parameter.
struct LfoFields {
  int shape, rate, rateOff, sync, phase, magnitude, deform, deformType, trig, unipolar,
      delay, egOff, attack, hold, decay, sustain, release;
};

struct Lfo {
  float phase = 0, value = 0, target = 0, held = 0, raw = 0, egOut = 0;
  int step = 0;
  Rng rng;
  int egStage = 0;   // 0 delay, 1 attack, 2 hold, 3 decay, 4 sustain, 5 release, 6 done
  float egLevel = 0, egFrom = 0, egT = 0;
  float tempoPhase = 0;
  float msegTime = 0;
  bool released = false;

  void trigger(int mode, float startPhase, uint32_t seed) {
    rng.seed(seed);
    if (mode == 1) phase = wrap01(startPhase);
    else if (mode == 2) phase = rng.unit();
    step = 0;
    held = rng.bipolar(); target = rng.bipolar(); value = held;
    egStage = 0; egLevel = 0; egT = 0;
    msegTime = 0;
    released = false;
  }

  void release() {
    released = true;
    if (egStage < 5) { egStage = 5; egFrom = egLevel; egT = 0; }
  }

  /// An MSEG in envelope mode advances in time and loops by its mode.
  float msegEnvelope(const Mseg &m, float dt, float hz) {
    const float total = m.total();
    msegTime += dt * hz;
    const bool loops = m.loopMode == 2 || (m.loopMode == 3 && !released);
    if (loops && m.count > 0) {
      const int ls = m.loopStart < 0 ? 0 : clampi(m.loopStart, 0, m.count - 1);
      const int le = m.loopEnd < 0 ? m.count - 1 : clampi(m.loopEnd, ls, m.count - 1);
      const float from = m.start(ls), to = m.start(le) + m.seg[le][0];
      if (to > from && msegTime > to) msegTime = from + std::fmod(msegTime - from, to - from);
    }
    return m.at(clampf(msegTime, 0, total));
  }

  float eg(float dt, const float *f) {
    const float times[6] = {stageSeconds(f[0]), stageSeconds(f[1]), stageSeconds(f[2]),
                            stageSeconds(f[3]), 0, stageSeconds(f[5])};
    const float sus = clampf(f[4], 0, 1);
    egT += dt;
    switch (egStage) {
      case 0: egLevel = 0; if (egT >= times[0]) { egStage = 1; egT = 0; } break;
      case 1: egLevel = times[1] <= 0 ? 1 : clampf(egT / times[1], 0, 1);
              if (egLevel >= 1) { egStage = 2; egT = 0; } break;
      case 2: egLevel = 1; if (egT >= times[2]) { egStage = 3; egT = 0; } break;
      case 3: { const float x = times[3] <= 0 ? 1 : clampf(egT / times[3], 0, 1);
                egLevel = 1 - (1 - sus) * x; if (x >= 1) egStage = 4; break; }
      case 4: egLevel = sus; break;
      case 5: { const float x = times[5] <= 0 ? 1 : clampf(egT / times[5], 0, 1);
                egLevel = egFrom * (1 - x); if (x >= 1) egStage = 6; break; }
      default: egLevel = 0; break;
    }
    return egLevel;
  }

  /// One control period. `p` points at this LFO's first parameter; `rate`,
  /// `magnitude` and `deform` already carry their modulation. Tempo-synced
  /// rates are read as log2 of cycles per beat.
  float tick(float dt, const float *p, const LfoFields &F, float rate, float magnitude, float deform,
             const float *steps, int loopStart, int loopEnd, float tempo, const Mseg *mseg) {
    const int shape = (int)p[F.shape];
    const bool frozen = p[F.rateOff] > 0.5f;
    float hz = std::pow(2.0f, clampf(rate, -7, 9));
    if (p[F.sync] > 0.5f) hz = std::pow(2.0f, clampf(rate, -7, 9)) * (tempo / 60.0f) * 0.5f;
    const float before = phase;
    if (!frozen) phase = wrap01(phase + hz * dt);
    const bool wrapped = phase < before;
    float w;
    switch (shape) {
      case 0: w = std::sin(kTwoPi * phase); break;
      case 1: w = phase < 0.5f ? 4 * phase - 1 : 3 - 4 * phase; break;
      case 2: w = phase < clampf(0.5f + 0.5f * deform, 0.02f, 0.98f) ? 1.0f : -1.0f; break;
      case 3: w = 2 * phase - 1; break;
      case 4: {  // smooth noise; deform correlates the draws
        if (wrapped) { held = target; target = clampf(deform * held + (1 - std::fabs(deform)) * rng.bipolar(), -1, 1); }
        const float x = phase;
        w = held + (target - held) * (x * x * (3 - 2 * x));
        break;
      }
      case 5: if (wrapped) held = clampf(-deform * held + (1 - std::fabs(deform)) * rng.bipolar(), -1, 1);
              w = held; break;
      case 6: w = 1.0f; break;
      case 7: {  // step sequence; deform smooths (+) or spikes (-) the steps
        if (wrapped) step = step >= loopEnd ? loopStart : step + 1;
        const float cur = steps[clampi(step, 0, 15)];
        const int nextStep = step >= loopEnd ? loopStart : step + 1;
        const float nxt = steps[clampi(nextStep, 0, 15)];
        if (deform > 0) w = cur + (nxt - cur) * phase * deform;
        else w = cur * (1.0f + deform * phase);
        break;
      }
      case 8: {  // MSEG
        if (!mseg || mseg->count <= 0) { w = 0; break; }
        if (mseg->editMode == 1) {
          const float total = mseg->total();
          w = mseg->at(phase * (total > 0 ? total : 1.0f));
        } else {
          w = msegEnvelope(*mseg, dt, hz);
        }
        break;
      }
      default: w = std::sin(kTwoPi * phase); break;  // a formula, read as a sine
    }
    if ((shape == 0 || shape == 1 || shape == 3) && std::fabs(deform) > 0.001f) {
      const float e = std::pow(2.0f, -deform * 2.0f);
      w = (w < 0 ? -1.0f : 1.0f) * std::pow(std::fabs(w), e);
    }
    if (p[F.unipolar] > 0.5f) w = 0.5f * (w + 1.0f);
    const bool egOn = p[F.egOff] < 0.5f || shape == 6;
    const float stages[6] = {p[F.delay], p[F.attack], p[F.hold], p[F.decay], p[F.sustain], p[F.release]};
    const float env = egOn ? eg(dt, stages) : 1.0f;
    raw = w;
    egOut = env;
    value = w * env * magnitude;
    return value;
  }
};

}  // namespace resurgent
}  // namespace vascular

#endif
