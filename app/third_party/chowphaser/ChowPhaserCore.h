/*
 * ChowPhaser, ported to plain C++ for Amalthea.
 *
 * Derived from ChowPhaser by Jatin Chowdhury (src/SingleChannelPhaser, PhaseSection,
 * FBSection, TanhIIRFilter; the *.orig files here). Copyright (c) 2020 jatinchowdhury18.
 * BSD 3-Clause licence; see LICENSE.
 *
 * Adaptations: no JUCE. juce::dsp::Oscillator (sine) and the 50 ms linear
 * SmoothedValues are reimplemented; the fast tan/sin approximations are replaced by
 * std::tan/std::sin. A stereo pair runs one parameter set with the right channel's LFO
 * offset by a phase, like ChowPhaserStereo with linked controls. The LDR light/resistance
 * model, the tanh-saturated feedback section, the fractional all-pass stage chain and
 * the modulation blend are unchanged.
 */
#pragma once
#include <algorithm>
#include <cmath>

namespace chowphaser {

struct Params {
    float feedback = 0;   // 0..0.95
    float mod = 0;        // 0..1, blend of the phase section into the feedback section
    float lfoFreq = 0;    // Hz, 0..16
    float lfoDepth = 0;   // 0..0.95
    bool freqMult = false;// LFO x10, depth x0.8
    float skew = 0;       // -3..3 (light curve exponent 2^skew)
    float stages = 8;     // 1..50, fractional
    float d1 = 1, d2 = 1, d3 = 1;   // feedback-section drives, 0.1..5
    float stereo = 0;     // right-channel LFO offset, 0..0.5 cycle
};

class Smooth {
  public:
    void reset(double sr, double seconds) { steps = std::max(1, int(sr * seconds)); }
    void target(float t)
    {
        if (first) { v = goal = t; first = false; return; }
        if (t != goal) { goal = t; left = steps; inc = (goal - v) / steps; }
    }
    float next()
    {
        if (left > 0) { v += inc; if (--left == 0) v = goal; }
        return v;
    }
  private:
    float v = 0, goal = 0, inc = 0;
    int steps = 1, left = 0;
    bool first = true;
};

class PhaseSection {
  public:
    void reset(double sampleRate) { std::fill(z, z + MaxStages, 0.f); fs = float(sampleRate); }
    float processSample(float x, float numStages)
    {
        int whole = int(numStages);
        for (int stage = 0; stage < whole; stage++) x = processStage(x, stage);
        float frac = numStages - whole;
        return frac * processStage(x, whole) + (1.f - frac) * x;
    }
    float processStage(float x, int stage)
    {
        float y = z[stage] + x * b[0];
        z[stage] = x * b[1] - y * a[1];
        return y;
    }
    void calcCoefs(float R)
    {
        constexpr float C = 25e-9f;
        const float RC = R * C, b0s = RC, b1s = -1.f, a0s = b0s, a1s = 1.f, K = 2.f * fs;
        const float a0 = a0s * K + a1s;
        b[0] = (b0s * K + b1s) / a0;
        b[1] = (-b0s * K + b1s) / a0;
        a[1] = (-a0s * K + a1s) / a0;
    }
  private:
    static constexpr int MaxStages = 52;
    float a[2] = {1, 0}, b[2] = {1, 0}, z[MaxStages] = {};
    float fs = 44100;
};

class FBSection {
  public:
    void reset(double sampleRate) { fs = float(sampleRate); z[0] = z[1] = z[2] = 0; }
    void calcCoefs(float R, float fbAmt)
    {
        constexpr float C = 15e-9f;
        const float RC = R * C, b0s = RC * RC, b1s = -2.f * RC, b2s = 1.f;
        const float a0s = b0s * (1.f + fbAmt), a1s = -b1s * (1.f - fbAmt), a2s = 1.f + fbAmt;
        const float wc = poleFreq(a0s, a1s, a2s);
        const float K = wc == 0.f ? 2.f * fs : wc / std::tan(wc / (2.f * fs)), KSq = K * K;
        const float a0 = a0s * KSq + a1s * K + a2s;
        a[1] = 2.f * (a2s - a0s * KSq) / a0;
        a[2] = (a0s * KSq - a1s * K + a2s) / a0;
        b[0] = (b0s * KSq + b1s * K + b2s) / a0;
        b[1] = 2.f * (b2s - b0s * KSq) / a0;
        b[2] = (b0s * KSq - b1s * K + b2s) / a0;
    }
    float processSampleTanh(float x, float d1, float d2, float d3)
    {
        float y = z[1] + x * b[0], yDrive = drive(y, d3);
        z[1] = drive(z[2] + x * b[1] - yDrive * a[1], d1);
        z[2] = drive(x * b[2] - yDrive * a[2], d2);
        return y;
    }
  private:
    static float poleFreq(float a, float b, float c)
    {
        float radicand = b * b - 4.f * a * c;
        return radicand >= 0.f ? 0.f : std::sqrt(-radicand) / (2.f * a);
    }
    static float drive(float x, float d) { return std::tanh(x * d) / d; }
    float a[3] = {1, 0, 0}, b[3] = {1, 0, 0}, z[3] = {};
    float fs = 44100;
};

class SingleChannelPhaser {
  public:
    void prepare(double sampleRate)
    {
        sr = sampleRate;
        fb.reset(sampleRate);
        phase.reset(sampleRate);
        for (auto *s : {&depthS, &fbS, &modS, &skewS, &stagesS, &d1S, &d2S, &d3S}) s->reset(sampleRate, .05);
        lfoPhase = startPhase;
    }
    void setStartPhase(float cycles) { startPhase = float(2 * M_PI) * cycles; }
    void clear() { fb.reset(sr); phase.reset(sr); }
    void setPhaseOffset(float cycles, float reference)
    {
        lfoPhase = reference + float(2 * M_PI) * cycles;
        lfoPhase -= float(2 * M_PI) * std::floor(lfoPhase / float(2 * M_PI));
    }
    float lfoPhaseNow() const { return lfoPhase; }

    void process(const float *in, float *out, int n, const Params &p)
    {
        const float freq = p.lfoFreq * (p.freqMult ? 10.f : 1.f);
        depthS.target(p.freqMult ? p.lfoDepth * .8f : p.lfoDepth);
        fbS.target(p.feedback);
        modS.target(std::abs(p.mod));
        skewS.target(std::pow(2.f, p.skew));
        stagesS.target(p.stages);
        d1S.target(p.d1); d2S.target(p.d2); d3S.target(p.d3);
        const float inc = float(2 * M_PI) * freq / float(sr);
        for (int i = 0; i < n; ++i)
        {
            constexpr float maxDepth = 20.f;
            float lfo = std::sin(lfoPhase) * depthS.next();
            lfoPhase += inc;
            if (lfoPhase >= float(2 * M_PI)) lfoPhase -= float(2 * M_PI);
            float shaped = std::pow((lfo + 1.f) / 2.f, skewS.next()) * 2.f - 1.f;
            float light = (maxDepth + .1f) - shaped * maxDepth;
            float r = 100000.f * std::pow(light / .1f, -.75f);
            fb.calcCoefs(r, -fbS.next());
            float noMod = fb.processSampleTanh(in[i], d1S.next(), d2S.next(), d3S.next());
            phase.calcCoefs(r);
            float g = modS.next();
            out[i] = g * phase.processSample(noMod, stagesS.next()) + (1.f - g) * noMod;
        }
    }

  private:
    double sr = 44100;
    FBSection fb;
    PhaseSection phase;
    Smooth depthS, fbS, modS, skewS, stagesS, d1S, d2S, d3S;
    float lfoPhase = 0, startPhase = 0;
};

class StereoPhaser {
  public:
    void prepare(double sr) { ch[0].prepare(sr); ch[1].prepare(sr); offset = -1; }
    void clear() { ch[0].clear(); ch[1].clear(); }
    void process(const float *inL, const float *inR, float *outL, float *outR, int n, const Params &p)
    {
        // The right LFO follows the left at the STEREO offset; changing it re-locks the pair.
        if (p.stereo != offset) { ch[1].setPhaseOffset(p.stereo, ch[0].lfoPhaseNow()); offset = p.stereo; }
        ch[0].process(inL, outL, n, p);
        ch[1].process(inR, outR, n, p);
    }
  private:
    SingleChannelPhaser ch[2];
    float offset = -1;
};

} // namespace chowphaser
