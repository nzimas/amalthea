/*
 * Surge XT Flanger, ported to plain C++ for Amalthea.
 *
 * Derived from sst-effects include/sst/effects/Flanger.h (Flanger.h.orig here), with
 * sst-basic-blocks lipol / SurgeLag and the effects-shared WidthProvider. Copyright
 * 2018-2023 the Surge Synth Team authors. Released under the GNU General Public
 * Licence v3 or later; see LICENSE.md.
 *
 * Adaptations: no FXConfig/templates or SIMD; denormal-sized values in the delay
 * lines and damping state are flushed to zero; the block size is the length of each
 * call (up to MaxBlock), with every per-block rate and interpolator scaled to it, so
 * the sound matches Surge's 32-sample blocks; parameters arrive as plain values in
 * Surge's own units; random numbers come from a local xorshift. The comb, LFO, voice,
 * feedback, damping, gain heuristics, soft clipping and width stages are unchanged.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace surge_flanger {

struct Params {
    int mode = 0;          // 0 dry+combs, 1 combs only, 2 dry+arp combs, 3 arp combs only
    int wave = 0;          // 0 sine, 1 triangle, 2 saw, 3 noise (glide), 4 sample & hold, 5 square
    float rate = -2.f;     // log2 Hz, -7..9
    float depth = 1.f;     // 0..1 (Surge allows up to 2)
    float voices = 4.f;    // 1..4
    float basePitch = 60;  // MIDI note of the first comb
    float spacing = 0;     // semitones between combs, 0..12
    float feedback = 0;    // 0..1
    float damping = .1f;   // 0..1
    float widthDb = 0;     // -24..24 dB
    float mix = .8f;       // comb amount, -1..1
};

class Flanger {
  public:
    static constexpr int MaxBlock = 32, Combs = 4;
    static constexpr double Midi0 = 8.17579891564371;

    explicit Flanger(double sampleRate = 44100) : sr(sampleRate) { initialize(); }

    void initialize()
    {
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < Combs; ++i)
            {
                lfophase[c][i] = 1.f * (i + 0.5f * c) / Combs;
                lfosandhtarget[c][i] = 0;
                lfoval[c][i] = {};
                delaybase[c][i] = {};
            }
        longphase[0] = 0;
        longphase[1] = 0.5f;
        idels[0].reset();
        idels[1].reset();
        lpaL = lpaR = 0;
        depth = mix = voices = feedback = damp = {};
        widthS = Smoothed{};
        haveProcessed = false;
        for (int i = 0; i < LfoTable; ++i)
            sinTable[i] = std::sin(2.0 * M_PI * i / LfoTable);
    }

    void process(float *L, float *R, int frames, const Params &p)
    {
        for (int at = 0; at < frames; at += MaxBlock)
            block(L + at, R + at, std::min(MaxBlock, frames - at), p);
    }

  private:
    struct Lipol {
        float v = 0, target = 0, dv = 0;
        bool first = true;
        void newValue(float f, int n)
        {
            v = target;
            target = f;
            if (first) { v = f; first = false; }
            dv = (target - v) / n;
        }
        void process() { v += dv; }
    };
    struct Smoothed {   // lipol_sse::set_target_smoothed
        float current = 0, target = 0;
        bool first = true;
        void set(float f)
        {
            if (first) { current = target = f; first = false; return; }
            current = target;
            target = .25f * f + .75f * target;
        }
    };
    struct Delay {
        static constexpr int Size = 32768, Mask = Size - 1;
        float line[Size];
        int k = 0;
        void reset() { std::memset(line, 0, sizeof line); k = 0; }
        float value(float by) const
        {
            int itap = int(std::min(by, float(Size - 2)));
            float frac = by - itap;
            int k0 = (k + Size - itap - 1) & Mask, k1 = (k + Size - itap) & Mask;
            return line[k0] * frac + line[k1] * (1.f - frac);
        }
        void push(float v) { k = (k + 1) & Mask; line[k] = std::fabs(v) < 1e-20f ? 0.f : v; }   // no denormals
    };
    static constexpr int LfoTable = 8192, LfoMask = LfoTable - 1;

    float rand01()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (rng >> 8) * (1.f / 16777216.f);
    }

    void block(float *dataL, float *dataR, int n, const Params &p)
    {
        if (!haveProcessed)
        {
            if (p.basePitch > 0) haveProcessed = true;
            zeroPitch = zeroPitchTarget = p.basePitch;
        }
        // envelope_rate_linear: cycles per block of n samples.
        float rate = float(n) / float(sr) * std::pow(2.f, std::clamp(p.rate, -8.f, 10.f));
        for (int c = 0; c < 2; ++c)
        {
            longphase[c] += rate;
            if (longphase[c] >= Combs) longphase[c] -= Combs;
        }
        const int mode = p.mode, mwave = p.wave;
        const float depthVal = std::clamp(p.depth, 0.f, 2.f);
        zeroPitchTarget = p.basePitch;
        lag();
        float v0 = zeroPitch, averageDelayBase = 0;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < Combs; ++i)
            {
                bool reset = false;
                lfophase[c][i] += rate;
                if (lfophase[c][i] > 1) { reset = true; lfophase[c][i] -= 1; }
                float out = lfoval[c][i].v, ph = lfophase[c][i];
                if (mode == 2 || mode == 3) ph = longphase[c] - int(longphase[c]);
                switch (mwave)
                {
                case 0: {
                    float ps = ph * LfoTable;
                    int psi = int(ps) & LfoMask;
                    float psf = ps - int(ps);
                    out = sinTable[psi] * (1.f - psf) + psf * sinTable[(psi + 1) & LfoMask];
                    break;
                }
                case 1: out = 2.f * std::fabs(2.f * ph - 1.f) - 1.f; break;
                case 2: {
                    const float cutAt = .98f;
                    out = ph < cutAt ? ph / cutAt * 2.f - 1.f : (1.f - (ph - cutAt) / (1.f - cutAt)) * 2.f - 1.f;
                    break;
                }
                case 5: {
                    const float cut = .02f, m = 2.f / cut, c2 = cut / 2.f;
                    if (ph < .5f - c2) out = 1.f;
                    else if (ph >= .5f + c2 && ph <= 1.f - cut) out = -1.f;
                    else if (ph > .5f - c2 && ph < .5f + c2) out = -m * ph + m / 2;
                    else out = m * ph - 2 * m + m + 1;
                    break;
                }
                default:   // 3 noise (glides towards each new value), 4 sample & hold
                    if (reset) lfosandhtarget[c][i] = rand01() - 1.f;
                    if (mwave == 3) out = lfoval[c][i].v + (lfosandhtarget[c][i] - lfoval[c][i].v) * rate * 2;
                    else out = lfosandhtarget[c][i];
                    break;
                }
                lfoval[c][i].newValue(out, n);

                float pitch = v0 + p.spacing * i;
                float nv = float(sr / Midi0) / std::pow(2.f, pitch / 12.f);
                float maxtap = nv * (1.f + depthVal) + 1;
                if (maxtap >= Delay::Size) nv = nv * .999f * Delay::Size / maxtap;
                delaybase[c][i].newValue(nv, n);
                averageDelayBase += delaybase[c][i].target;
            }
        averageDelayBase /= 2 * Combs;
        lag();

        float dApprox = rate * float(sr) / n * averageDelayBase * depthVal;
        depth.newValue(depthVal, n);
        mix.newValue(p.mix, n);
        voices.newValue(std::clamp(p.voices, 1.f, 4.f), n);
        float feedbackScale = .4f * std::sqrt((std::clamp(dApprox, 2.f, 60.f) + 30) / 100.f);
        // Surge's feedback adjustment by mode, including its fall-through for the arp modes.
        if (mode == 0 || mode == 1) feedbackScale += (3.f - (voices.v - 1)) * .45f / 3.f;
        if (mode == 3) feedbackScale += .2f;
        if (mode == 3 || mode == 2) feedbackScale += .3f;
        float fbv = p.feedback;
        if (mwave == 2 || mwave == 4) feedbackScale *= .7f;
        if (fbv > 0 && fbv < 1) fbv = std::sqrt(fbv);
        feedback.newValue(feedbackScale * fbv, n);
        damp.newValue(.4f * p.damping, n);

        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < Combs; ++i) weights[c][i] = 0;
            if (mode == 2 || mode == 3)
            {
                int ilp = int(longphase[c]);
                float flp = longphase[c] - ilp;
                if (ilp == Combs) ilp = 0;
                if (flp > .9f)
                {
                    float dt = (flp - .9f) * 10;
                    weights[c][ilp] = std::sqrt(1.f - dt);
                    weights[c][ilp == Combs - 1 ? 0 : ilp + 1] = std::sqrt(dt);
                }
                else weights[c][ilp] = 1.f;
            }
            else
            {
                float vc = std::clamp(p.voices, 1.f, float(Combs));
                weights[c][0] = 1;
                for (int i = 0; i < vc && i < 4; ++i) weights[c][i] = 1;
                int li = int(vc);
                if (li < 4) weights[c][li] = vc - li;
            }
        }

        float combs[2][MaxBlock];
        for (int b = 0; b < n; ++b)
        {
            for (int c = 0; c < 2; ++c)
            {
                combs[c][b] = 0;
                for (int i = 0; i < Combs; ++i)
                {
                    if (weights[c][i] > 0)
                        combs[c][b] += weights[c][i] * idels[c].value(delaybase[c][i].v * (1.f + lfoval[c][i].v * depth.v) + 1);
                    lfoval[c][i].process();
                    delaybase[c][i].process();
                }
            }
            float fbl = 0, fbr = 0;
            if (feedback.v > 0)
            {
                fbl = std::clamp(feedback.v * combs[0][b], -1.f, 1.f);
                fbr = std::clamp(feedback.v * combs[1][b], -1.f, 1.f);
                fbl = 1.5f * fbl - .5f * fbl * fbl * fbl;
                fbr = 1.5f * fbr - .5f * fbr * fbr * fbr;
                float df = std::clamp(damp.v, .01f, .99f);
                lpaL = lpaL * (1.f - df) + fbl * df; fbl -= lpaL;
                lpaR = lpaR * (1.f - df) + fbr * df; fbr -= lpaR;
                if (std::fabs(lpaL) < 1e-20f) lpaL = 0;
                if (std::fabs(lpaR) < 1e-20f) lpaR = 0;
            }
            idels[0].push(dataL[b] - fbl);
            idels[1].push(dataR[b] - fbr);
            float origw = (mode == 1 || mode == 3) ? 0.f : 1.f;
            float outl = origw * dataL[b] + mix.v * combs[0][b];
            float outr = origw * dataR[b] + mix.v * combs[1][b];
            float gainadj = mode == 0 ? -1 / std::sqrt(7 - voices.v) : mode == 1 ? -1 / std::sqrt(8 - voices.v)
                          : mode == 2 ? -1 / std::sqrt(6.f) : -1 / std::sqrt(7.f);
            gainadj -= .07f * mix.v;
            outl = std::clamp((1.f + gainadj) * outl, -1.f, 1.f);
            outr = std::clamp((1.f + gainadj) * outr, -1.f, 1.f);
            dataL[b] = 1.5f * outl - .5f * outl * outl * outl;
            dataR[b] = 1.5f * outr - .5f * outr * outr * outr;
            depth.process(); mix.process(); feedback.process(); damp.process(); voices.process();
        }
        // Width: side scaled by dB-to-linear(width) / 3, ramped across the block.
        widthS.set(std::pow(10.f, p.widthDb / 20.f) / 3.f);
        for (int b = 0; b < n; ++b)
        {
            float w = widthS.current + (widthS.target - widthS.current) * (b + 1) / n;
            float m = .5f * (dataL[b] + dataR[b]), s = .5f * (dataL[b] - dataR[b]) * w;
            dataL[b] = m + s; dataR[b] = m - s;
        }
    }
    void lag() { zeroPitch = zeroPitch * (1 - .004f) + zeroPitchTarget * .004f; }

    double sr;
    Delay idels[2];
    float lfophase[2][Combs], longphase[2], lfosandhtarget[2][Combs], weights[2][Combs];
    float lpaL = 0, lpaR = 0, zeroPitch = 60, zeroPitchTarget = 60;
    Lipol lfoval[2][Combs], delaybase[2][Combs], depth, mix, voices, feedback, damp;
    Smoothed widthS;
    bool haveProcessed = false;
    uint32_t rng = 0x2545f491u;
    float sinTable[LfoTable];
};

} // namespace surge_flanger
