// NorniOS: the 2x oversampler QDelay asks JUCE for — juce::dsp::Oversampling<float>(2, 1,
// filterHalfBandPolyphaseIIR, /*maximum quality*/ false, /*integer latency*/ true) — written from JUCE's design so
// the saturation and the crusher hear the same filters (docs/adr/0053-qdelay.md):
//   * half-band polyphase allpass pairs, designed as FilterDesign::designIIRLowpassHalfBandPolyphaseAllpassMethod
//     does, with the first stage's figures: transition 0.06 / -70 dB up, 0.075 / -60 dB down;
//   * the first-order Thiran allpass that rounds the latency up to whole samples.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

namespace qdelay_shim {

class Oversampling2x {
public:
    Oversampling2x() {
        double latency = design(0.12 * 0.5, -70.0, up_, directUp_);
        latency += design(0.15 * 0.5, -60.0, down_, directDown_);
        latency /= 2.0;
        uncompensated_ = static_cast<float>(latency);
        fractional_ = 1.0f - (uncompensated_ - std::floor(uncompensated_));
        if (std::fabs(fractional_ - 1.0f) < 1e-6f) fractional_ = 0.f;
        else if (fractional_ < 0.618f) fractional_ += 1.0f;
        alpha_ = (1.f - fractional_) / (1.f + fractional_);
        for (int c = 0; c < 2; ++c) { v1Up_[c].assign(up_.size(), 0.f); v1Down_[c].assign(down_.size(), 0.f); }
    }

    size_t getOversamplingFactor() const noexcept { return 2; }
    float getLatencyInSamples() const noexcept { return uncompensated_ + fractional_; }

    void initProcessing(int maxBlock) {
        for (auto &b : buffer_) b.assign(static_cast<size_t>(maxBlock) * 2, 0.f);
        reset();
    }
    void reset() noexcept {
        for (int c = 0; c < 2; ++c) {
            std::fill(v1Up_[c].begin(), v1Up_[c].end(), 0.f);
            std::fill(v1Down_[c].begin(), v1Down_[c].end(), 0.f);
            std::fill(buffer_[c].begin(), buffer_[c].end(), 0.f);
            delayDown_[c] = 0.f; thiranX_[c] = 0.f; thiranY_[c] = 0.f;
        }
    }

    float *channel(int c) noexcept { return buffer_[c].data(); }

    // n samples of each channel in, 2n samples in channel(c)
    void processSamplesUp(const float *const in[2], int n) noexcept {
        const int stages = static_cast<int>(up_.size());
        for (int c = 0; c < 2; ++c) {
            float *out = buffer_[c].data();
            float *lv1 = v1Up_[c].data();
            for (int i = 0; i < n; ++i) {
                float input = in[c][i];
                for (int s = 0; s < directUp_; ++s) {
                    const float a = up_[static_cast<size_t>(s)], o = a * input + lv1[s];
                    lv1[s] = input - a * o; input = o;
                }
                out[i << 1] = input;
                input = in[c][i];
                for (int s = directUp_; s < stages; ++s) {
                    const float a = up_[static_cast<size_t>(s)], o = a * input + lv1[s];
                    lv1[s] = input - a * o; input = o;
                }
                out[(i << 1) + 1] = input;
            }
            snap(v1Up_[c]);
        }
    }

    // the 2n samples in channel(c) down to n samples of each channel
    void processSamplesDown(float *const out[2], int n) noexcept {
        const int stages = static_cast<int>(down_.size());
        for (int c = 0; c < 2; ++c) {
            const float *buf = buffer_[c].data();
            float *lv1 = v1Down_[c].data();
            float delay = delayDown_[c];
            for (int i = 0; i < n; ++i) {
                float input = buf[i << 1];
                for (int s = 0; s < directDown_; ++s) {
                    const float a = down_[static_cast<size_t>(s)], o = a * input + lv1[s];
                    lv1[s] = input - a * o; input = o;
                }
                const float direct = input;
                input = buf[(i << 1) + 1];
                for (int s = directDown_; s < stages; ++s) {
                    const float a = down_[static_cast<size_t>(s)], o = a * input + lv1[s];
                    lv1[s] = input - a * o; input = o;
                }
                out[c][i] = (delay + direct) * 0.5f;
                delay = input;
            }
            delayDown_[c] = delay;
            snap(v1Down_[c]);
            if (fractional_ > 0.f) {
                // DelayLine<float, Thiran>, whole part 0: y[n] = x[n-1] + alpha (x[n] - y[n-1])
                float x1 = thiranX_[c], y1 = thiranY_[c];
                for (int i = 0; i < n; ++i) {
                    const float x = out[c][i];
                    const float y = x1 + alpha_ * (x - y1);
                    x1 = x; y1 = y; out[c][i] = y;
                }
                thiranX_[c] = x1; thiranY_[c] = y1;
            }
        }
    }

private:
    static void snap(std::vector<float> &v) noexcept {
        for (auto &x : v) if (!(x < -1.0e-8f || x > 1.0e-8f)) x = 0.f;
    }

    // Fills `coeffs` with the direct path's allpass coefficients followed by the delayed path's, and returns the
    // filter's delay at (almost) DC, in samples of the doubled rate.
    static double design(double transitionWidth, double stopbandDb, std::vector<float> &coeffs, int &directStages) {
        const double pi = 3.141592653589793238;
        const double wt = 2.0 * pi * transitionWidth;
        const double ds = std::pow(10.0, stopbandDb * 0.05);
        const double k = std::pow(std::tan((pi - wt) / 4), 2.0);
        const double kp = std::sqrt(1.0 - k * k);
        const double e = (1 - std::sqrt(kp)) / (1 + std::sqrt(kp)) * 0.5;
        const double q = e + 2 * std::pow(e, 5.0) + 15 * std::pow(e, 9.0) + 150 * std::pow(e, 13.0);
        double k1 = ds * ds / (1 - ds * ds);
        int n = static_cast<int>(std::lround(std::ceil(std::log(k1 * k1 / 16) / std::log(q))));
        if (n % 2 == 0) ++n;
        if (n == 1) n = 3;
        const int N = (n - 1) / 2;
        std::vector<double> ai;
        for (int i = 1; i <= N; ++i) {
            double num = 0.0, delta = 1.0;
            int m = 0;
            while (std::abs(delta) > 1e-100) {
                delta = std::pow(-1, m) * std::pow(q, m * (m + 1)) * std::sin((2 * m + 1) * pi * i / static_cast<double>(n));
                num += delta; m++;
            }
            num *= 2 * std::pow(q, 0.25);
            double den = 0.0;
            delta = 1.0; m = 1;
            while (std::abs(delta) > 1e-100) {
                delta = std::pow(-1, m) * std::pow(q, m * m) * std::cos(m * 2.0 * pi * i / static_cast<double>(n));
                den += delta; ++m;
            }
            den = 1 + 2 * den;
            const double wi = num / den;
            const double api = std::sqrt((1 - wi * wi * k) * (1 - wi * wi / k)) / (1 + wi * wi);
            ai.push_back((1 - api) / (1 + api));
        }
        std::vector<float> direct, delayed;
        for (int i = 0; i < N; i += 2) direct.push_back(static_cast<float>(ai[static_cast<size_t>(i)]));
        for (int i = 1; i < N; i += 2) delayed.push_back(static_cast<float>(ai[static_cast<size_t>(i)]));
        coeffs = direct;
        coeffs.insert(coeffs.end(), delayed.begin(), delayed.end());
        directStages = static_cast<int>(direct.size());

        // as Oversampling2TimesPolyphaseIIR: the phase of direct(z) + z^-1 delayed(z) at 0.0001 of the rate
        const double w = 2.0 * pi * 0.0001;
        const std::complex<double> zi = std::polar(1.0, -w);
        auto allpass = [&](double a) { return (a + zi) / (1.0 + a * zi); };
        std::complex<double> h1{1.0, 0.0}, h2 = zi;
        for (float a : direct) h1 *= allpass(a);
        for (float a : delayed) h2 *= allpass(a);
        return -std::arg(h1 + h2) / w;
    }

    std::vector<float> up_, down_;
    int directUp_{0}, directDown_{0};
    std::vector<float> v1Up_[2], v1Down_[2], buffer_[2];
    float delayDown_[2]{0.f, 0.f};
    float uncompensated_{0.f}, fractional_{0.f}, alpha_{0.f};
    float thiranX_[2]{0.f, 0.f}, thiranY_[2]{0.f, 0.f};
};

} // namespace qdelay_shim
