/*
 * resurgent's effects section: sixteen units, as Surge XT's manual lays them
 * out -- four inserts on each scene, four sends fed from both scenes and
 * returned, four on the master -- and the effects they can run.
 *
 * Written from the manual and from the patch files read as data
 * (Docs/resurgent/FX-SPEC.md: the slot numbers, the type numbers and each
 * effect's parameter order and units); the algorithms are this instrument's
 * own, from the literature:
 *  - delays, chorus, flanger, ensemble: modulated fractional delay lines
 *    (J. Dattorro, "Effect Design Part 2", JAES 45(10), 1997);
 *  - reverbs: feedback delay networks (J.-M. Jot & A. Chaigne, AES preprint
 *    3030, 1991) and Schroeder allpass diffusion; the spring as a chain of
 *    dispersive allpasses (J. Parker, DAFx 2011);
 *  - phaser: first-order allpass chain; frequency shifter: a Hilbert pair of
 *    allpass chains (Weaver's method as in Dattorro 1997);
 *  - EQ: RBJ peaking and shelving biquads (R. Bristow-Johnson, "Cookbook
 *    formulae for audio EQ biquad filter coefficients");
 *  - granular texture: overlapping windowed grains from a recorded buffer.
 * One voice, mono: a scene's stereo width has nowhere to go and is ignored.
 * Airwindows, the vocoder and the audio input need what a node does not
 * have -- another program's algorithms, a modulator, an input -- and pass
 * their input through.
 */
#ifndef VASCULAR_RESURGENT_FX_H
#define VASCULAR_RESURGENT_FX_H

#include <cmath>
#include "RtBuffer.h"

#include "ResurgentFilter.h"
#include "ResurgentOsc.h"

namespace vascular {
namespace resurgent {

/// A transposed direct-form biquad.
struct Biquad {
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  float tick(float x) {
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
  void set(double nb0, double nb1, double nb2, double na0, double na1, double na2) {
    b0 = (float)(nb0 / na0); b1 = (float)(nb1 / na0); b2 = (float)(nb2 / na0);
    a1 = (float)(na1 / na0); a2 = (float)(na2 / na0);
  }
  void peak(float sr, float hz, float db, float octaves) {
    const double A = std::pow(10.0, db / 40.0), w = 2 * M_PI * clampf(hz, 10, sr * 0.45f) / sr;
    const double alpha = std::sin(w) * std::sinh(std::log(2.0) / 2 * std::fmax(octaves, 0.05) * w / std::sin(w));
    set(1 + alpha * A, -2 * std::cos(w), 1 - alpha * A, 1 + alpha / A, -2 * std::cos(w), 1 - alpha / A);
  }
  void shelf(float sr, float hz, float db, bool high) {
    const double A = std::pow(10.0, db / 40.0), w = 2 * M_PI * clampf(hz, 10, sr * 0.45f) / sr;
    const double c = std::cos(w), s = std::sin(w), alpha = s / 2 * std::sqrt(2.0), k = 2 * std::sqrt(A) * alpha;
    if (high) set(A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
                  (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
    else set(A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
             (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k);
  }
  void lowpass(float sr, float hz, float q = 0.707f) {
    const double w = 2 * M_PI * clampf(hz, 10, sr * 0.45f) / sr, alpha = std::sin(w) / (2 * q), c = std::cos(w);
    set((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + alpha, -2 * c, 1 - alpha);
  }
  void highpass(float sr, float hz, float q = 0.707f) {
    const double w = 2 * M_PI * clampf(hz, 10, sr * 0.45f) / sr, alpha = std::sin(w) / (2 * q), c = std::cos(w);
    set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + alpha, -2 * c, 1 - alpha);
  }
  void bandpass(float sr, float hz, float q) {
    const double w = 2 * M_PI * clampf(hz, 10, sr * 0.45f) / sr, alpha = std::sin(w) / (2 * std::fmax(q, 0.1f)), c = std::cos(w);
    set(alpha, 0, -alpha, 1 + alpha, -2 * c, 1 - alpha);
  }
};

/// A power-of-two delay line read with linear interpolation.
struct DelayLine {
  amalthea_rt::Buffer<float> buf;
  int mask = 0, at = 0;
  void size(int samples) {
    int n = 1;
    while (n < samples) n <<= 1;
    if ((int)buf.size() != n) { buf.assign(n, 0.0f); mask = n - 1; at = 0; }
  }
  void clear() { std::fill(buf.begin(), buf.end(), 0.0f); }
  void write(float x) { buf[at] = x; at = (at + 1) & mask; }
  float read(float delay) const {
    delay = clampf(delay, 1.0f, (float)mask - 2);
    float rp = at - delay;
    if (rp < 0) rp += mask + 1;
    const int i0 = (int)rp & mask, i1 = (i0 + 1) & mask;
    const float fr = rp - std::floor(rp);
    return buf[i0] + (buf[i1] - buf[i0]) * fr;
  }
};

inline float cubeAmp(float raw) { return raw * raw * raw; }
inline float hzOf(float semis) { return noteHz(semis); }
inline float seconds(float raw) { return raw <= -7.99f ? 0.0f : std::pow(2.0f, raw); }

/// One effect unit.
struct Effect {
  int type = -1;
  float sr = 48000;
  Rng rng;
  DelayLine d[8];
  Biquad bq[12];
  float lp[8] = {}, ap[24] = {}, phase[4] = {}, held[4] = {};
  float fbState = 0, env = 0, gainState = 1, last = 0;
  int counter = 0;

  void setType(int t, float sampleRate) {
    if (t == type) return;
    type = t;
    sr = sampleRate;
    for (auto &line : d) { line.buf.clear(); line.mask = 0; line.at = 0; }
    for (auto &b : bq) b = Biquad();
    std::fill(std::begin(lp), std::end(lp), 0.0f);
    std::fill(std::begin(ap), std::end(ap), 0.0f);
    std::fill(std::begin(phase), std::end(phase), 0.0f);
    fbState = env = last = 0; gainState = 1; counter = 0;
    rng.seed(0x51F7u + t);
    switch (t) {
      case 1: d[0].size((int)(sr * 2.8f)); d[1].size((int)(sr * 2.8f)); break;
      case 2: case 11: for (int i = 0; i < 8; ++i) d[i].size((int)(sr * 0.25f)); break;
      case 4: d[0].size(4096); break;
      case 7: d[0].size((int)(sr * 2.8f)); break;
      case 9: case 20: d[0].size((int)(sr * 0.3f)); break;
      case 12: for (int i = 0; i < 4; ++i) d[i].size(8192); break;
      case 15: d[0].size(8192); break;
      case 21: for (int i = 0; i < 3; ++i) d[i].size(1 << 17); break;
      case 22: d[0].size(1 << 17); break;
      case 27: for (int i = 0; i < 2; ++i) d[i].size((int)(sr * 0.2f)); break;
      default: break;
    }
  }

  /// One sample through the effect: `p` its twelve parameters, `flags`
  /// their deactivated bits.
  float tick(float x, const float *p, int deactivated, float tempo) {
    auto off = [&](int i) { return (deactivated >> i) & 1; };
    auto mix = [](float dry, float wet, float m) { return dry + (wet - dry) * clampf(m, -1, 1); };
    switch (type) {
      case 1: {  // Delay
        const float syncScale = 1.0f;
        (void)tempo; (void)syncScale;
        const float tl = seconds(p[0]) * sr, tr = off(1) ? tl : seconds(p[1]) * sr;
        phase[0] = wrap01(phase[0] + std::pow(2.0f, clampf(p[6], -7, 9)) / sr);
        const float tri = phase[0] < 0.5f ? 4 * phase[0] - 1 : 3 - 4 * phase[0];
        const float mod = (std::pow(2.0f, clampf(p[7], 0, 12) / 12.0f) - 1.0f) * 0.5f * tri;
        const float yl = d[0].read(tl * (1 + mod)), yr = d[1].read(tr * (1 - mod));
        const float fb = cubeAmp(clampf(p[2], -1, 1)), cross = cubeAmp(clampf(p[3], -1, 1));
        const float ch = clampf(p[8], -1, 1);
        float wl = yl, wr = yr;
        if (!off(4)) { wl = bq[0].tick(wl); wr = bq[1].tick(wr); }
        if (!off(5)) { wl = bq[2].tick(wl); wr = bq[3].tick(wr); }
        if (counter++ % 64 == 0) {
          bq[0].highpass(sr, hzOf(p[4])); bq[1].highpass(sr, hzOf(p[4]));
          bq[2].lowpass(sr, hzOf(p[5])); bq[3].lowpass(sr, hzOf(p[5]));
        }
        d[0].write(std::tanh(x * (1 - ch) * 0.5f * 2 + wl * fb + wr * cross));
        d[1].write(std::tanh(x * (1 + ch) * 0.5f * 2 + wr * fb + wl * cross));
        return mix(x, 0.5f * (yl + yr), p[10]);
      }
      case 2: case 11: {  // Reverb 1 and Reverb 2: an eight-line FDN
        const bool two = type == 11;
        const float pre = seconds(p[0]) * sr;
        const float size = two ? 0.5f + 0.5f * clampf(p[1], -1, 1) : clampf(p[2], 0, 1);
        const float rt60 = std::fmax(seconds(p[two ? 2 : 3]), 0.05f);
        const float damp = clampf(two ? p[7] : p[4], 0, 1);
        const float wetMix = two ? p[9] : p[9];
        static const float lengths[8] = {1117, 1277, 1423, 1559, 1697, 1811, 1949, 2083};
        float in = x;
        if (pre > 1) { d[7].write(x); in = d[7].read(pre); }
        float out[8], sum = 0;
        const float scale = 0.4f + 1.6f * size;
        for (int i = 0; i < 7; ++i) {
          const float len = lengths[i] * scale * (two ? 1.0f + 0.003f * std::sin(phase[0] * kTwoPi * (i + 1)) * p[5] * 10 : 1.0f);
          out[i] = d[i].read(len);
          const float g = std::pow(10.0f, -3.0f * len / sr / rt60);
          lp[i] += (1.0f - 0.85f * damp) * (out[i] - lp[i]);
          out[i] = lp[i] * g;
          sum += out[i];
        }
        phase[0] = wrap01(phase[0] + 0.7f / sr);
        // Householder mixing: each line takes the input and every line less
        // twice the mean.
        const float mean = sum * (2.0f / 7.0f);
        for (int i = 0; i < 7; ++i) d[i].write(in * 0.35f + out[i] - mean);
        float wet = sum * 0.3f;
        if (!two) {
          if (counter++ % 64 == 0) {
            bq[0].highpass(sr, hzOf(p[5])); bq[1].peak(sr, hzOf(p[6]), clampf(p[7], -48, 48), 1);
            bq[2].lowpass(sr, hzOf(p[8]));
          }
          if (!off(5)) wet = bq[0].tick(wet);
          wet = bq[1].tick(wet);
          if (!off(8)) wet = bq[2].tick(wet);
        }
        return mix(x, wet, wetMix);
      }
      case 3: {  // Phaser
        const int stages = clampi((int)p[8] < 2 ? 4 : (int)p[8], 2, 16);
        if (!off(3)) phase[0] = wrap01(phase[0] + std::pow(2.0f, clampf(p[3], -7, 9)) / sr);
        const float lfo = lfoShape((int)p[10], phase[0]);
        const float centre = 1000.0f * std::pow(2.0f, clampf(p[0], -1, 1) * 3.0f + lfo * clampf(p[4], 0, 1) * 2.0f);
        float y = x + fbState * clampf(p[1], -0.95f, 0.95f);
        for (int s = 0; s < stages; ++s) {
          const float f = centre * std::pow(2.0f, (s - stages / 2) * clampf(p[9], 0, 1) * 0.5f);
          const float t = std::tan(3.14159265f * clampf(f, 20, sr * 0.45f) / sr);
          const float c = (t - 1) / (t + 1);
          const float v = c * y + ap[s];
          ap[s] = y - c * v;
          y = v;
        }
        fbState = y;
        return mix(x, y, p[6]);
      }
      case 4: {  // Rotary speaker
        const float horn = std::pow(2.0f, clampf(p[0], -7, 9));
        phase[0] = wrap01(phase[0] + horn / sr);
        phase[1] = wrap01(phase[1] + horn * clampf(p[3], 0, 2) / sr);
        float in = off(4) ? x : shape(shaperKind(1 + clampi((int)p[5], 0, 4)), x * (1 + clampf(p[4], 0, 1) * 8), rng);
        d[0].write(in);
        const float s0 = std::sin(kTwoPi * phase[0]), s1 = std::sin(kTwoPi * phase[1]);
        const float hornOut = d[0].read(30 + clampf(p[1], 0, 1) * 20 * (1 + s0));
        bq[0].lowpass(sr, 800); bq[1].highpass(sr, 800);
        const float low = bq[0].tick(in) * (1 - clampf(p[2], 0, 1) * 0.5f * (1 + s1));
        const float high = bq[1].tick(hornOut) * (1 - clampf(p[2], 0, 1) * 0.5f * (1 + s0));
        return mix(x, low + high, p[7]);
      }
      case 5: {  // Distortion
        if (counter++ % 64 == 0) {
          bq[0].peak(sr, hzOf(p[1]), clampf(p[0], -48, 48), clampf(p[2], 0.05f, 5));
          bq[1].lowpass(sr, hzOf(p[3]));
          bq[2].peak(sr, hzOf(p[7]), clampf(p[6], -48, 48), clampf(p[8], 0.05f, 5));
          bq[3].lowpass(sr, hzOf(p[9]));
        }
        float y = bq[0].tick(x);
        if (!off(3)) y = bq[1].tick(y);
        // The feedback goes in after the drive's gain, so the loop's gain is
        // the feedback amount alone and never runs away.
        const float driveGain = dbToGain(clampf(p[4], -120, 120));
        y = shape(shaperKind(1 + clampi((int)p[11], 0, 4)), y * driveGain + fbState * clampf(p[5], -1, 1), rng);
        // The loop is DC-blocked: a clipper fed back on itself otherwise
        // settles on a constant and stays there, pinned at full scale.
        lp[0] += 0.002f * (y - lp[0]);
        fbState = y - lp[0];
        y = bq[2].tick(y);
        if (!off(9)) y = bq[3].tick(y);
        return y * dbToGain(clampf(p[10], -48, 48));
      }
      case 6: {  // EQ
        if (counter++ % 64 == 0)
          for (int b = 0; b < 3; ++b) bq[b].peak(sr, hzOf(p[b * 3 + 1]), clampf(p[b * 3], -48, 48), clampf(p[b * 3 + 2], 0.05f, 5));
        float y = x;
        for (int b = 0; b < 3; ++b) if (!off(b * 3)) y = bq[b].tick(y);
        return mix(x, y * dbToGain(clampf(p[9], -48, 48)), p[10] < -0.5f || p[10] > 1.5f ? 1.0f : p[10]);
      }
      case 7: {  // Frequency shifter: Hilbert pair, shifted, delayed, fed back
        const float shift = clampf(p[0], -1000, 1000);
        const float in = x + fbState * cubeAmp(clampf(p[3], 0, 1));
        float re = in, im = in;
        static const float ca[4] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f};
        static const float cb[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
        for (int i = 0; i < 4; ++i) {
          const float a2 = ca[i] * ca[i];
          const float v = a2 * (re - ap[i * 2 + 1]) + ap[i * 2];
          ap[i * 2] = re; ap[i * 2 + 1] = v; re = v;
          const float b2 = cb[i] * cb[i];
          const float w = b2 * (im - ap[8 + i * 2 + 1]) + ap[8 + i * 2];
          ap[8 + i * 2] = im; ap[8 + i * 2 + 1] = w; im = w;
        }
        im = last; last = im;  // one sample of delay aligns the pair
        phase[0] = wrap01(phase[0] + shift / sr);
        const float shifted = re * std::cos(kTwoPi * phase[0]) - im * std::sin(kTwoPi * phase[0]);
        const float delay = seconds(p[2]) * sr;
        d[0].write(shifted);
        const float y = delay > 1 ? d[0].read(delay) : shifted;
        fbState = y;
        return mix(x, y, p[4]);
      }
      case 8: {  // Conditioner: shelves, then a limiter with make-up
        if (counter++ % 64 == 0) {
          bq[0].shelf(sr, 200, clampf(p[0], -12, 12), false);
          bq[1].shelf(sr, 3000, clampf(p[1], -12, 12), true);
        }
        float y = x;
        if (!off(0)) y = bq[0].tick(y);
        if (!off(1)) y = bq[1].tick(y);
        const float threshold = dbToGain(clampf(p[4], -48, 0));
        const float level = std::fabs(y);
        const float attack = std::exp(-1.0f / (sr * (0.001f * std::pow(10.0f, clampf(p[5], -1, 1) + 1))));
        const float release = std::exp(-1.0f / (sr * (0.05f * std::pow(10.0f, clampf(p[6], -1, 1)))));
        env = level > env ? attack * env + (1 - attack) * level : release * env + (1 - release) * level;
        const float gain = env > threshold ? threshold / env : 1.0f;
        return y * gain / std::sqrt(threshold) * dbToGain(clampf(p[7], -48, 0));
      }
      case 9: {  // Chorus: four taps on one modulated line
        const float centre = clampf(seconds(p[0]), 0.0005f, 0.125f) * sr;
        phase[0] = wrap01(phase[0] + std::pow(2.0f, clampf(p[1], -7, 9)) / sr);
        d[0].write(x + fbState * cubeAmp(clampf(p[3], 0, 1)));
        float sum = 0;
        for (int t = 0; t < 4; ++t) {
          const float m = std::sin(kTwoPi * (phase[0] + t * 0.25f));
          sum += d[0].read(centre * (1 + 0.9f * clampf(p[2], 0, 1) * m));
        }
        float wet = sum * 0.25f;
        fbState = wet;
        if (counter++ % 64 == 0) { bq[0].highpass(sr, hzOf(p[4])); bq[1].lowpass(sr, hzOf(p[5])); }
        if (!off(4)) wet = bq[0].tick(wet);
        if (!off(5)) wet = bq[1].tick(wet);
        return mix(x, wet, p[6]);
      }
      case 12: {  // Flanger: up to four combs tuned from a base pitch
        const int mode = clampi((int)p[0], 0, 3);
        phase[0] = wrap01(phase[0] + std::pow(2.0f, clampf(p[2], -7, 9)) / sr);
        const float lfo = lfoShape(clampi((int)p[1], 0, 3), phase[0]) * clampf(p[3], 0, 1);
        const float count = clampf(p[4], 1, 4);
        float wet = 0;
        for (int c = 0; c < (int)std::ceil(count); ++c) {
          const float note = clampf(p[5], 0, 127) + c * clampf(p[6], 0, 12) + lfo * 12;
          const float hz = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
          const float y = d[c].read(sr / clampf(hz, 8, 12000));
          lp[c] += (1 - 0.9f * clampf(p[8], 0, 1)) * (y - lp[c]) * 0.5f + 0.0f;
          d[c].write(std::tanh(x + (y - lp[c] * clampf(p[8], 0, 1)) * clampf(p[7], 0, 0.95f)));
          const float weight = c + 1 <= count ? 1.0f : count - c;
          wet += y * weight;
        }
        wet /= count;
        const float out = (mode == 1 || mode == 3) ? wet : 0.5f * (x + wet);
        return mix(x, out, p[10]);
      }
      case 13: {  // Ring modulator against a carrier oscillator
        const float hz = 440.0f * std::pow(2.0f, (clampf(p[1], 0, 127) - 69.0f) / 12.0f);
        phase[0] = wrap01(phase[0] + hz / sr);
        const float carrier = lfoShape(clampi((int)p[0], 0, 3), phase[0]);
        // A diode ring: the forward bias sets where it starts to conduct and
        // the linear region how soon it becomes a plain multiply.
        const float bias = clampf(p[4], 0, 1), linear = clampf(p[5], 0, 1);
        auto diode = [&](float v) { return v <= bias ? 0.0f : (v - bias) * (v - bias) / (linear + 0.01f + v - bias); };
        const float y = diode(x + carrier) - diode(x - carrier) - diode(-x + carrier) + diode(-x - carrier);
        float wet = y;
        if (counter++ % 64 == 0) { bq[0].highpass(sr, hzOf(p[6])); bq[1].lowpass(sr, hzOf(p[7])); }
        if (!off(6)) wet = bq[0].tick(wet);
        if (!off(7)) wet = bq[1].tick(wet);
        return mix(x, wet, p[8]);
      }
      case 15: {  // Neuron: a gated recurrent saturator with a comb in its loop
        const float drive = dbToGain(clampf(p[0], 0, 24));
        const float delay = sr / clampf(hzOf(p[5]), 20, 12000);
        const float z = 1.0f / (1.0f + std::exp(-(clampf(p[1], 0, 1) * 4 * x + clampf(p[2], 0, 1) * 4 * fbState + clampf(p[4], 0, 1) * 2 - 1)));
        const float h = std::tanh(drive * x + clampf(p[3], 0, 1) * 2 * d[0].read(delay));
        const float y = z * fbState + (1 - z) * h;
        fbState = y;
        d[0].write(y);
        return y * dbToGain(clampf(p[11], -24, 24));
      }
      case 16: {  // Graphic EQ: eleven octave bands from 20 Hz
        if (counter++ % 128 == 0)
          for (int b = 0; b < 11 && b < 12; ++b) bq[b].peak(sr, 20.0f * std::pow(2.0f, (float)b), clampf(p[b], -24, 24), 1.0f);
        float y = x;
        for (int b = 0; b < 11; ++b) if (!off(b)) y = bq[b].tick(y);
        return y * dbToGain(clampf(p[11], -48, 24));
      }
      case 17: {  // Resonator: three resonant bands in parallel
        const int mode = clampi((int)p[9], 0, 3);
        if (counter++ % 64 == 0)
          for (int b = 0; b < 3; ++b) {
            const float hz = hzOf(p[b * 3]), q = 0.7f + clampf(p[b * 3 + 1], 0, 1) * 30;
            if (mode == 0) bq[b].lowpass(sr, hz, q);
            else if (mode == 3) bq[b].highpass(sr, hz, q);
            else bq[b].bandpass(sr, hz, q);
          }
        float wet = 0;
        for (int b = 0; b < 3; ++b) {
          float y = bq[b].tick(x);
          if (mode == 2) y = x - y * 0.8f;
          wet += y * cubeAmp(clampf(p[b * 3 + 2], 0, 1));
        }
        return mix(x, wet * dbToGain(clampf(p[10], -48, 48)), p[11]);
      }
      case 18: {  // CHOW: rectification above a threshold
        const float t = dbToGain(clampf(p[0], -96, 0)), ratio = clampf(p[1], 1, 20);
        const float a = std::fabs(x);
        float y = a > t ? (x > 0 ? 1 : -1) * (t + (a - t) / ratio) : x;
        if (p[2] > 0.5f) y = -y;
        return mix(x, y, p[3]);
      }
      case 19: {  // Exciter: the top, driven and added
        if (counter++ % 128 == 0) bq[0].highpass(sr, 1500 + clampf(p[1], 0, 1) * 6000);
        const float hp = bq[0].tick(x);
        return x + std::tanh(hp * (1 + clampf(p[0], 0, 1) * 10)) * clampf(p[4], 0, 1) * 0.5f;
      }
      case 20: {  // Ensemble: two LFOs across three taps of one line
        phase[0] = wrap01(phase[0] + std::pow(2.0f, clampf(p[1], -7, 5)) / sr);
        phase[1] = wrap01(phase[1] + std::pow(2.0f, clampf(p[3], -7, 5)) / sr);
        const float base = (clampi((int)p[5], 0, 6) < 6 ? (128 << clampi((int)p[5], 0, 5)) / (clampf(p[6], 1.5f, 100) * 1000.0f) : 0.01f) * sr;
        d[0].write(std::tanh(x * (1 + clampf(p[7], 0, 1) * 2) + fbState * clampf(p[8], 0, 0.9f)));
        float wet = 0;
        for (int t = 0; t < 3; ++t) {
          const float m = clampf(p[2], 0, 1) * std::sin(kTwoPi * (phase[0] + t / 3.0f)) * 0.5f
                        + clampf(p[4], 0, 1) * std::sin(kTwoPi * (phase[1] + t / 3.0f)) * 0.1f;
          wet += d[0].read(clampf(base * (1 + m), 2, sr * 0.25f));
        }
        wet /= 3;
        fbState = wet;
        return mix(x, wet, p[10]);
      }
      case 21: {  // Combulator: three combs around a centre pitch
        const float centre = hzOf(p[1]);
        const float noise = clampf(p[0], 0, 1) * rng.bipolar() * std::fabs(x);
        float wet = 0;
        for (int c = 0; c < 3; ++c) {
          const float hz = centre * std::pow(2.0f, (c == 0 ? 0 : clampf(p[c + 1], -60, 60)) / 12.0f);
          const float y = d[c].read(sr / clampf(hz, 0.5f, 12000));
          d[c].write(std::tanh(x + noise + y * clampf(p[4], -0.99f, 0.99f)));
          wet += y * cubeAmp(clampf(p[6 + c], 0, 1));
        }
        if (!off(5)) {
          const float tone = clampf(p[5], -1, 1);
          lp[0] += (tone < 0 ? 0.05f + 0.95f * (1 + tone) : 1.0f) * (wet - lp[0]);
          wet = tone < 0 ? lp[0] : wet - tone * 0.8f * lp[0];
        }
        return mix(x, wet, p[11]);
      }
      case 22: {  // Nimbus: grains from a recorded second, pitched and spread
        const bool freeze = p[8] > 0.5f;
        if (!freeze) d[0].write(x + fbState * clampf(p[7], 0, 0.9f));
        const float size = (0.02f + 0.3f * (0.5f + 0.5f * clampf(p[3], -1, 1))) * sr;
        const float ratio = std::pow(2.0f, clampf(p[4], -48, 48) / 12.0f);
        const float density = 0.5f + 0.5f * clampf(p[5], -1, 1);
        float wet = 0;
        for (int g = 0; g < 4; ++g) {
          phase[g] += 1.0f / size;
          if (phase[g] >= 1.0f) {
            phase[g] -= 1.0f;
            held[g] = rng.unit() < 0.2f + 0.8f * density
                ? clampf(p[2], 0, 1) * sr * 0.9f + size * (2 + rng.unit()) : -1.0f;
          }
          if (held[g] < 0) continue;
          const float pos = held[g] - phase[g] * size * (ratio - 1.0f);
          const float tex = 0.5f + 0.5f * clampf(p[6], -1, 1);
          const float win = std::pow(std::sin(3.14159265f * phase[g]), 0.5f + 3 * tex);
          wet += d[0].read(clampf(pos, 2, (float)d[0].mask - 4)) * win;
        }
        wet *= 0.5f;
        fbState = wet;
        return mix(x, wet, p[11]);
      }
      case 23: {  // Tape: saturation with memory, loss by speed, wow
        float y = x;
        if (!off(0)) {
          const float drive = 1 + clampf(p[0], 0, 1) * 6, sat = clampf(p[1], 0, 1);
          const float h = std::tanh(drive * y + clampf(p[2], 0, 1) * 0.3f * fbState);
          fbState = h;
          y = y + (h - y) * (0.3f + 0.7f * sat);
        }
        if (!off(4)) {
          const float speed = clampf(p[4], 1, 50);
          lp[0] += clampf(speed / 50.0f, 0.05f, 1.0f) * (y - lp[0]);
          y = lp[0];
        }
        if (!off(8)) y *= 1.0f - clampf(p[8], 0, 1) * clampf(p[9], 0, 1) * 0.3f * (0.5f + 0.5f * rng.bipolar() * clampf(p[10], 0, 1));
        return mix(x, y, p[11]);
      }
      case 24: {  // Treemonster: the input's pitch, as a ring-modulated sine
        if (counter++ % 64 == 0) { bq[0].highpass(sr, hzOf(p[2])); bq[1].lowpass(sr, hzOf(p[3])); }
        float det = x;
        if (!off(2)) det = bq[0].tick(det);
        if (!off(3)) det = bq[1].tick(det);
        held[1] += 1;
        if (last <= 0 && det > 0) {
          if (std::fabs(det) > dbToGain(clampf(p[0], -96, 0)) * 0.1f)
            held[0] += (sr / std::fmax(held[1], 2.0f) - held[0]) * (0.05f + clampf(p[1], 0, 1) * 0.9f);
          held[1] = 0;
        }
        last = det;
        phase[0] = wrap01(phase[0] + held[0] * std::pow(2.0f, clampf(p[4], -60, 60) / 12.0f) / sr);
        const float sine = std::sin(kTwoPi * phase[0]);
        const float rm = clampf(p[5], 0, 1);
        return mix(x, sine * (1 - rm) * 0.5f + sine * x * rm, p[7]);
      }
      case 25: {  // Waveshaper
        if (counter++ % 64 == 0) {
          bq[0].highpass(sr, hzOf(p[0])); bq[1].lowpass(sr, hzOf(p[1]));
          bq[2].highpass(sr, hzOf(p[5])); bq[3].lowpass(sr, hzOf(p[6]));
        }
        float y = x;
        if (!off(0)) y = bq[0].tick(y);
        if (!off(1)) y = bq[1].tick(y);
        if (!off(2)) y = shape(shaperKind((int)p[2]), (y + clampf(p[3], -1, 1)) * dbToGain(clampf(p[4], -120, 120)), rng)
                         - shape(shaperKind((int)p[2]), clampf(p[3], -1, 1), rng);
        if (!off(5)) y = bq[2].tick(y);
        if (!off(6)) y = bq[3].tick(y);
        return mix(x, y * dbToGain(clampf(p[7], -24, 24)), p[8]);
      }
      case 26: {  // Mid-Side Tool: a mono voice is all mid
        if (counter++ % 64 == 0) {
          bq[0].highpass(sr, hzOf(p[1])); bq[1].peak(sr, hzOf(p[3]), clampf(p[2], -24, 24), 1);
          bq[2].lowpass(sr, hzOf(p[4]));
        }
        float y = x;
        if (!off(1)) y = bq[0].tick(y);
        if (!off(2)) y = bq[1].tick(y);
        if (!off(4)) y = bq[2].tick(y);
        return y * dbToGain(clampf(p[9], -48, 12));
      }
      case 27: {  // Spring reverb: dispersive allpasses in a decaying loop
        const float size = 0.3f + clampf(p[0], 0, 1);
        const float decay = 0.5f + 4.0f * clampf(p[1], 0, 1);
        float y = x + fbState;
        for (int s = 0; s < 12; ++s) {
          const float c = 0.6f + 0.02f * s * clampf(p[4], 0, 1);
          const float v = -c * y + ap[s];
          ap[s] = y + c * v;
          y = v;
        }
        const float len = size * 0.05f * sr * (1 + 0.01f * clampf(p[5], 0, 1) * rng.bipolar());
        d[0].write(y);
        const float back = d[0].read(len);
        lp[0] += (1 - 0.9f * clampf(p[3], 0, 1)) * (back - lp[0]);
        fbState = lp[0] * std::pow(10.0f, -3.0f * len / sr / decay) * (0.6f + 0.4f * clampf(p[2], 0, 1));
        if (p[6] > 0.5f && counter++ % (int)sr == 0) fbState += 0.5f;
        return mix(x, back, p[7]);
      }
      case 28: {  // Bonsai: bass boost, saturation, a little noise, dulled
        float y = x * dbToGain(clampf(p[0], -24, 24));
        if (counter++ % 64 == 0) {
          bq[0].shelf(sr, 120, clampf(p[1], -24, 24), false);
          bq[1].lowpass(sr, 18000 - 14000 * clampf(p[8], 0, 1));
        }
        y = bq[0].tick(y);
        y = y + (std::tanh(y * (1 + clampf(p[2], 0, 1) * 4)) - y) * clampf(p[2], 0, 1);
        static const ShaperKind kinds[4] = {W_softOne, W_soft, W_hard, W_fuzz};
        y = y + (shape(kinds[clampi((int)p[4], 0, 3)], y * 2, rng) * 0.5f - y) * clampf(p[5], 0, 1);
        if (std::fabs(y) > (1 - clampf(p[6], 0, 1))) y += rng.bipolar() * dbToGain(clampf(p[7], -96, 24)) * 0.05f;
        y = bq[1].tick(y) * dbToGain(clampf(p[9], -24, 24));
        return mix(x, y, p[10]);
      }
      default:
        // Off, Airwindows, Vocoder, Audio Input, and anything unknown.
        return x;
    }
  }

  static float lfoShape(int shape, float t) {
    switch (shape) {
      case 1: return t < 0.5f ? 4 * t - 1 : 3 - 4 * t;
      case 2: return 2 * t - 1;
      case 3: return t < 0.5f ? 1.0f : -1.0f;
      default: return std::sin(kTwoPi * t);
    }
  }
};

/// Where a unit's values sit among the patch-wide parameters.
struct FxLayout {
  int typeAt;        // unit 1's type; its deactivated flag follows, then p0..p11
  int stride;        // from one unit's type to the next's
  int offAt;         // unit 1's p0@deactivated; twelve per unit
  int syncAt;        // unit 1's p0@temposync
  int extendAt;      // unit 1's p0@extend_range
  int disable;       // the fx_disable bitmask
  int returnsAt;     // volume_FX1
};

/// The sixteen units and their routing, as FX-SPEC.md section 1 has it.
struct FxChain {
  static const int kUnits = 16;
  Effect unit[kUnits];
  float sr_ = 48000;

  void prepare(float sr) { sr_ = sr; }

  /// Which of an effect's parameters are rates rather than times, for tempo
  /// sync: a synced time is stored as its length at 120 BPM, a synced rate
  /// as its speed at 120 BPM (FX-SPEC.md section 3).
  static bool isRate(int type, int k) {
    return (type == 1 && k == 6) || (type == 3 && k == 3) || (type == 4 && k == 0)
           || (type == 9 && k == 1) || (type == 12 && k == 2);
  }

  float process(float a, float b, const float *sendA, const float *sendB, const float *g,
                const FxLayout &L, float tempo) {
    const int disableMask = (int)g[L.disable];
    auto run = [&](int n, float x) {
      const float *u = g + L.typeAt + (n - 1) * L.stride;
      const int type = (int)u[0];
      if (type <= 0 || ((disableMask >> (n - 1)) & 1) || u[1] > 0.5f) return x;
      float p[12];
      int off = 0;
      for (int k = 0; k < 12; ++k) {
        p[k] = u[2 + k];
        if (g[L.offAt + (n - 1) * 12 + k] > 0.5f) off |= 1 << k;
        if (g[L.syncAt + (n - 1) * 12 + k] > 0.5f && tempo > 1) {
          const float shift = std::log2(120.0f / tempo);
          p[k] += isRate(type, k) ? -shift : shift;
        }
      }
      // The frequency shifter's extended range is a hundred times wider.
      if (type == 7 && g[L.extendAt + (n - 1) * 12] > 0.5f) p[0] *= 100.0f;
      Effect &e = unit[n - 1];
      e.setType(type, sr_);
      return e.tick(x, p, off, tempo);
    };
    static const int insertsA[4] = {1, 2, 9, 10}, insertsB[4] = {3, 4, 11, 12};
    static const int sends[4] = {5, 6, 13, 14}, globals[4] = {7, 8, 15, 16};
    for (int n : insertsA) a = run(n, a);
    for (int n : insertsB) b = run(n, b);
    float mix = a + b;
    for (int k = 0; k < 4; ++k) {
      const float *u = g + L.typeAt + (sends[k] - 1) * L.stride;
      if ((int)u[0] <= 0 || ((disableMask >> (sends[k] - 1)) & 1)) continue;
      const float in = clampf(sendA[k], 0, 1.5f) * a + clampf(sendB[k], 0, 1.5f) * b;
      // The return slider, read on the cube taper the levels use.
      mix += run(sends[k], in) * cubeAmp(clampf(g[L.returnsAt + k], 0, 1.2f));
    }
    for (int n : globals) mix = run(n, mix);
    return std::isfinite(mix) ? mix : 0.0f;
  }
};

}  // namespace resurgent
}  // namespace vascular

#endif
