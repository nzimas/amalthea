// SPDX-License-Identifier: GPL-3.0-only
// 48 kHz -> 44.1 kHz for the Mutable Instruments cores (RINGS, PLAITS), which run at their own
// 48 kHz so that every time constant, filter and table in them is the module's. 44100/48000 is
// 147/160: output frame k is read at input position k*160/147, a 32-tap windowed sinc (Kaiser,
// beta 7, cut at 20 kHz) at the nearest of 147 exact phases. Pass band flat to ~16 kHz,
// everything that would fold below 20 kHz is at least ~65 dB down. Latency 16 input samples.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
namespace amalthea {
struct MiKernel {
  static constexpr int Phases = 147, Taps = 32, Half = 16;
  alignas(16) float h[Phases][Taps];
  MiKernel() {
    auto bessel0 = [](double x) { double s = 1, t = 1; for (int k = 1; k < 30; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
    const double fc = 20000.0 / 48000.0, beta = 7.0, norm = bessel0(beta);
    for (int p = 0; p < Phases; p++) {
      double sum = 0, tmp[Taps];
      for (int j = 0; j < Taps; j++) {
        // tap j reads input i-15+j for an output at i + p/147: distance t = p/147 + 15 - j
        double t = double(p) / Phases + (Half - 1) - j, x = 2 * fc * t;
        double sinc = std::fabs(x) < 1e-9 ? 1 : std::sin(M_PI * x) / (M_PI * x);
        double r = t / Half, w = std::fabs(r) >= 1 ? 0 : bessel0(beta * std::sqrt(1 - r * r)) / norm;
        tmp[j] = 2 * fc * sinc * w;
        sum += tmp[j];
      }
      for (int j = 0; j < Taps; j++) h[p][j] = float(tmp[j] / sum);
    }
  }
  static const MiKernel &get() { static const MiKernel k; return k; }
};
// Pulls 48 kHz stereo blocks from `render(float *l, float *r)` (Block frames a call) and writes
// 44.1 kHz frames. State is plain data: it can live in a voice's arena.
template <int Block> struct MiResampler {
  static constexpr int Hist = MiKernel::Taps, Cap = Hist + 64 + Block;
  float l[Cap], r[Cap];
  int have;       // frames buffered
  int base;       // input index of l[0], relative to the current output's integer position
  int phase;      // the current output's fractional position, in 147ths
  void reset() {
    std::memset(l, 0, sizeof l);
    std::memset(r, 0, sizeof r);
    have = MiKernel::Half;   // (the history before the first sample: silence)
    base = 0, phase = 0;
  }
  template <class Render> void process(float *outL, float *outR, int frames, Render &&render) {
    const auto &k = MiKernel::get();
    for (int n = 0; n < frames; n++) {
      // output at input position base_pos = (Half-1) + base, needs up to index base + Taps - 1
      while (have < base + MiKernel::Taps) {
        if (have + Block > Cap) {   // slide the window down
          int drop = base;
          std::memmove(l, l + drop, sizeof(float) * (have - drop));
          std::memmove(r, r + drop, sizeof(float) * (have - drop));
          have -= drop, base = 0;
        }
        render(l + have, r + have);
        have += Block;
      }
      const float *hk = k.h[phase], *xl = l + base, *xr = r + base;
      float a = 0, b = 0;
      for (int j = 0; j < MiKernel::Taps; j++) { a += hk[j] * xl[j]; b += hk[j] * xr[j]; }
      outL[n] = a, outR[n] = b;
      phase += 160;
      while (phase >= MiKernel::Phases) { phase -= MiKernel::Phases; base++; }
    }
  }
};
}
