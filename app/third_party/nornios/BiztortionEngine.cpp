// Engine_Biztortion: Gabriel Bizzo's Biztortion (https://github.com/killbizz/Biztortion, GPL 3), a modular distortion,
// as a native engine (docs/adr/0054-biztortion.md).
//
// Upstream keeps each module's DSP and its JUCE editor in one file, so the files cannot be compiled without JUCE's
// GUI. The DSP halves are carried over here statement for statement onto plain buffers (the originals are kept for
// reference in Vendor/biztortion/Source), with what they take from JUCE written from JUCE 6.1's own formulas:
// IIR::Coefficients (peak, high/low pass, first order), FilterDesign's high-order Butterworth methods, IIR::Filter's
// transposed direct form II, Decibels and Random. Upstream's quirks are kept and marked "as upstream".
//
// The chain is upstream's: an input stage (position 0), eight slots (1-8), each empty or holding one module, and an
// output stage (9), processed in order. Every module type exists in every slot from the start, so choosing a module
// allocates nothing; choosing it resets it.
//
//   commands: module(i slot, i type)        type as upstream's ModuleType: 0 none, 2 filter, 3 oscilloscope,
//                                           4 waveshaper, 5 bitcrusher, 6 slew limiter
//             set(i slot, i index, f value) a slot's parameter, by its place in kSlotParams, as a plain value
//             level(i stage, f dB)          0 the input stage's level, 1 the output's
//             scope(i position, f h zoom)   where the screen's oscilloscope listens: after position 0..9
//   polls:    bz_in_l, bz_in_r, bz_out_l, bz_out_r (peaks, falling), bz_scope (data: 128 minima then 128 maxima)
#include "EngineRegistry.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cfenv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace nornios {
namespace {

constexpr int kSlots = 8;
constexpr int kMaxBlock = 256;

enum ModuleType { Uninstantiated = 0, Meter = 1, IIRFilter = 2, Oscilloscope = 3, Waveshaper = 4, Bitcrusher = 5, SlewLimiter = 6 };

// a slot's parameters, in the order the script lists them
enum SlotParam {
    kLowCutFreq = 1, kLowCutSlope, kHighCutFreq, kHighCutSlope, kPeakFreq, kPeakGain, kPeakQuality, kFilterBypassed,
    kWsDrive, kWsMix, kWsSymmetry, kWsBias, kWsTanhAmp, kWsTanhSlope, kWsSineAmp, kWsSineFreq, kWsBypassed,
    kBcDrive, kBcMix, kBcSymmetry, kBcBias, kBcRateRedux, kBcBitRedux, kBcDither, kBcBypassed,
    kSlDrive, kSlMix, kSlSymmetry, kSlBias, kSlRise, kSlFall, kSlDCoffset, kSlBypassed,
    kOscHZoom, kOscVZoom, kOscBypassed,
    kSlotParams = kOscBypassed
};

// upstream's defaults and ranges (each module's addParameters)
struct ParamSpec { float lo, hi, def; };
const ParamSpec kSpecs[kSlotParams + 1] = {
    {0, 0, 0},
    {20, 20000, 20}, {0, 3, 0}, {20, 20000, 20000}, {0, 3, 0}, {20, 20000, 800}, {-24, 24, 0}, {0.1f, 10, 1}, {0, 1, 0},
    {0, 40, 0}, {0, 100, 100}, {-100, 100, 0}, {-0.9f, 0.9f, 0}, {0, 100, 100}, {1, 15, 1}, {0, 100, 0}, {0.5f, 100, 0.5f}, {0, 1, 0},
    {0, 40, 0}, {0, 100, 100}, {-100, 100, 0}, {-0.9f, 0.9f, 0}, {100, 44100, 44100}, {1, 16, 16}, {0, 100, 0}, {0, 1, 0},
    {0, 40, 0}, {0, 100, 100}, {-100, 100, 0}, {-0.9f, 0.9f, 0}, {0, 100, 0}, {0, 100, 0}, {0, 1, 0}, {0, 1, 0},
    {0, 1, 0}, {0, 2, 1}, {0, 1, 0},
};

inline float decibelsToGain(float dB) { return dB > -100.f ? std::pow(10.f, dB * 0.05f) : 0.f; }

// juce::Random
struct JuceRandom {
    int64_t seed{1};
    void setSeed(int64_t s) { seed = s; }
    int nextInt() {
        seed = static_cast<int64_t>((static_cast<uint64_t>(seed) * 0x5deece66dULL + 11) & 0xffffffffffffULL);
        return static_cast<int>(seed >> 16);
    }
    float nextFloat() {
        auto r = static_cast<float>(static_cast<uint32_t>(nextInt())) / (static_cast<float>(UINT32_MAX) + 1.0f);
        return r == 1.0f ? 1.0f - std::numeric_limits<float>::epsilon() : r;
    }
};

// juce::dsp::IIR::Filter<float> with its Coefficients: up to second order, a0 divided out, transposed direct form II
struct Biquad {
    float c[5]{1, 0, 0, 0, 0}; // b0 b1 (b2) a1 (a2)
    int order{2};
    float state[2]{0, 0};

    void reset() { state[0] = state[1] = 0; }
    void setOrder(int o) { if (o != order) { order = o; reset(); } }
    // IIR::Coefficients::assign with six values { b0, b1, b2, a0, a1, a2 }
    void assign6(const std::array<float, 6> &v) {
        const float inv = v[3] != 0.f ? 1.f / v[3] : 0.f;
        setOrder(2);
        c[0] = v[0] * inv; c[1] = v[1] * inv; c[2] = v[2] * inv; c[3] = v[4] * inv; c[4] = v[5] * inv;
    }
    // ... with four { b0, b1, a0, a1 }
    void assign4(const std::array<float, 4> &v) {
        const float inv = v[2] != 0.f ? 1.f / v[2] : 0.f;
        setOrder(1);
        c[0] = v[0] * inv; c[1] = v[1] * inv; c[2] = v[3] * inv;
    }
    inline float processSample(float x) {
        if (order == 1) {
            const float y = c[0] * x + state[0];
            state[0] = c[1] * x - c[2] * y;
            return y;
        }
        const float y = c[0] * x + state[0];
        state[0] = c[1] * x - c[3] * y + state[1];
        state[1] = c[2] * x - c[4] * y;
        return y;
    }
    void process(float *d, int n) {
        for (int i = 0; i < n; ++i) d[i] = processSample(d[i]);
        for (float &s : state) if (!(s < -1.0e-8f || s > 1.0e-8f)) s = 0.f; // snapToZero
    }
};

// IIR::ArrayCoefficients<float>
std::array<float, 6> makeLowPass(double sr, float f, float Q) {
    const float n = 1 / std::tan(3.14159265358979f * f / static_cast<float>(sr));
    const float n2 = n * n, invQ = 1 / Q, c1 = 1 / (1 + invQ * n + n2);
    return {c1, c1 * 2, c1, 1, c1 * 2 * (1 - n2), c1 * (1 - invQ * n + n2)};
}
std::array<float, 6> makeHighPass(double sr, float f, float Q) {
    const float n = std::tan(3.14159265358979f * f / static_cast<float>(sr));
    const float n2 = n * n, invQ = 1 / Q, c1 = 1 / (1 + invQ * n + n2);
    return {c1, c1 * -2, c1, 1, c1 * 2 * (n2 - 1), c1 * (1 - invQ * n + n2)};
}
std::array<float, 4> makeFirstOrderHighPass(double sr, float f) {
    const float n = std::tan(3.14159265358979f * f / static_cast<float>(sr));
    return {1, -1, n + 1, n - 1};
}
std::array<float, 6> makePeakFilter(double sr, float f, float Q, float gainFactor) {
    const float A = std::max(0.f, std::sqrt(gainFactor));
    const float omega = (2 * 3.14159265358979f * std::max(f, 2.f)) / static_cast<float>(sr);
    const float alpha = std::sin(omega) / (Q * 2), c2 = -2 * std::cos(omega);
    return {1 + alpha * A, c2, 1 - alpha * A, 1 + alpha / A, c2, 1 - alpha / A};
}
// FilterDesign::designIIR{High,Low}passHighOrderButterworthMethod for an even order: the Q of biquad i
inline float butterworthQ(int i, int order) {
    return static_cast<float>(1.0 / (2.0 * std::cos((2.0 * i + 1.0) * 3.141592653589793 / (order * 2.0))));
}

// DSPModule::applyAsymmetry: the wet signal into the dry buffer, whole, or only above or below the bias
void applyAsymmetry(float *dry[2], float *wet[2], float symmetryAmount, float symmetryBias, int numSamples) {
    const float dryGain = std::abs(symmetryAmount);
    const float wetGain = 1 - dryGain;
    for (int channel = 0; channel < 2; ++channel) {
        float *wetData = wet[channel];
        float *bufferData = dry[channel];
        for (int i = 0; i < numSamples; ++i) {
            if (symmetryAmount == 0.f) {
                bufferData[i] = wetData[i];
            } else if (symmetryAmount > 0.f) {
                if (bufferData[i] < -symmetryBias) bufferData[i] = wetData[i];
                else bufferData[i] = bufferData[i] * dryGain + wetData[i] * wetGain;
            } else {
                if (bufferData[i] >= -symmetryBias) bufferData[i] = wetData[i];
                else bufferData[i] = bufferData[i] * dryGain + wetData[i] * wetGain;
            }
        }
    }
}

struct Slot {
    int type{Uninstantiated};
    float p[kSlotParams + 1]{};
    // filter: low cut x4, peak, high cut x4, per channel
    Biquad lowCut[2][4], peak[2], highCut[2][4];
    // slew limiter
    Biquad dcHpf[2];
    float lastOutput{0.f};
    // bitcrusher
    JuceRandom random;

    Slot() { for (int i = 1; i <= kSlotParams; ++i) p[i] = kSpecs[i].def; }

    void reset() {
        for (int ch = 0; ch < 2; ++ch) {
            for (auto &f : lowCut[ch]) f.reset();
            for (auto &f : highCut[ch]) f.reset();
            peak[ch].reset();
            dcHpf[ch].reset();
        }
        lastOutput = 0.f;
    }
};

} // namespace

class BiztortionEngine : public Engine {
public:
    const char *name() const override { return "Biztortion"; }
    const std::vector<CommandDef> &commands() const override {
        static std::vector<CommandDef> c = {{"module", "ii"}, {"set", "iif"}, {"level", "if"}, {"scope", "if"}};
        return c;
    }
    const std::vector<PollDef> &polls() const override {
        static std::vector<PollDef> p = {{"bz_in_l", true}, {"bz_in_r", true}, {"bz_out_l", true}, {"bz_out_r", true}, {"bz_scope", true}};
        return p;
    }

    void prepare(double sampleRate, int) override {
        sr_ = sampleRate;
        elapsed_ = 0; // Amalthea: clear/rebuild starts from a fresh deterministic noise clock.
        for (auto &s : slots_) {
            s.reset();
            for (int ch = 0; ch < 2; ++ch) s.dcHpf[ch].assign4(makeFirstOrderHighPass(sr_, 5.f));
        }
    }

    void command(const EngineCommand &c) override {
        const int a = static_cast<int>(c.args[0]);
        switch (c.id) {
        case 1: {
            if (a < 1 || a > kSlots) break;
            const int t = static_cast<int>(c.args[1]);
            Slot &s = slots_[a - 1];
            const int type = (t == IIRFilter || t == Oscilloscope || t == Waveshaper || t == Bitcrusher || t == SlewLimiter) ? t : Uninstantiated;
            if (type != s.type) { s.type = type; s.reset(); }
            break;
        }
        case 2: {
            const int k = static_cast<int>(c.args[1]);
            if (a < 1 || a > kSlots || k < 1 || k > kSlotParams || !std::isfinite(c.args[2])) break;
            slots_[a - 1].p[k] = std::clamp(c.args[2], kSpecs[k].lo, kSpecs[k].hi);
            break;
        }
        case 3:
            if (std::isfinite(c.args[1])) (a == 0 ? inputDb_ : outputDb_) = std::clamp(c.args[1], -60.f, 10.f);
            break;
        case 4:
            scopeAt_ = std::clamp(a, 0, 9);
            if (std::isfinite(c.args[1])) {
                // 1 .. 512 samples a column: 2.7 ms .. 1.4 s across the screen at 48 kHz
                const int per = static_cast<int>(std::lround(std::pow(2.0, std::clamp(c.args[1], 0.f, 1.f) * 9.0)));
                if (per != scopePer_) { scopePer_ = per; scopeCol_ = -1; scopeWait_ = 0; }
            }
            break;
        default: break;
        }
    }

    void render(const float *inL, const float *inR, float *outL, float *outR, int frames) override {
        frames = std::min(frames, kMaxBlock);
        std::copy(inL, inL + frames, outL);
        std::copy(inR, inR + frames, outR);
        float *buffer[2] = {outL, outR};

#ifdef FE_DFL_DISABLE_DENORMS_ENV
        std::fenv_t env; // juce::ScopedNoDenormals
        std::fegetenv(&env);
        std::fesetenv(FE_DFL_DISABLE_DENORMS_ENV);
#endif
        // position 0: the input stage
        stage(buffer, frames, inputDb_, inMeter_);
        if (scopeAt_ == 0) scope(buffer, frames);
        for (int i = 0; i < kSlots; ++i) {
            Slot &s = slots_[i];
            switch (s.type) {
            case IIRFilter: filter(s, buffer, frames); break;
            case Waveshaper: waveshaper(s, buffer, frames); break;
            case Bitcrusher: bitcrusher(s, buffer, frames); break;
            case SlewLimiter: slewLimiter(s, buffer, frames); break;
            default: break; // an oscilloscope only looks
            }
            if (scopeAt_ == i + 1) scope(buffer, frames);
        }
        // position 9: the output stage
        stage(buffer, frames, outputDb_, outMeter_);
        if (scopeAt_ == 9) scope(buffer, frames);
        elapsed_ += frames;
#ifdef FE_DFL_DISABLE_DENORMS_ENV
        std::fesetenv(&env);
#endif
    }

    bool pollValue(int index, float &out) override {
        switch (index) {
        case 0: out = inMeter_[0].load(); return true;
        case 1: out = inMeter_[1].load(); return true;
        case 2: out = outMeter_[0].load(); return true;
        case 3: out = outMeter_[1].load(); return true;
        default: return false;
        }
    }

    int pollData(int index, float *out, int max) override {
        if (index != 4 || max < kScopeColumns * 2) return 0;
        for (int i = 0; i < kScopeColumns * 2; ++i) out[i] = scopeFront_[static_cast<size_t>(i)].load(std::memory_order_relaxed);
        return kScopeColumns * 2;
    }

private:
    static constexpr int kScopeColumns = 128;
    static constexpr float kMeterFall = 0.93f;

    // MeterModuleDSP: a level (juce::dsp::Gain, no ramp), then the meter
    void stage(float *buffer[2], int n, float dB, std::atomic<float> meter[2]) {
        const float g = decibelsToGain(dB);
        for (int ch = 0; ch < 2; ++ch) {
            float peak = 0;
            for (int i = 0; i < n; ++i) { buffer[ch][i] *= g; peak = std::max(peak, std::fabs(buffer[ch][i])); }
            meter[ch].store(std::max(peak, meter[ch].load() * kMeterFall));
        }
    }

    // FilterModuleDSP: low cut (Butterworth, 12-48 dB/oct), peak, high cut; coefficients renewed every block
    void filter(Slot &s, float *buffer[2], int n) {
        const bool bypassed = s.p[kFilterBypassed] > 0.5f;
        if (bypassed) return; // every link of the chain is bypassed
        const int lowStages = static_cast<int>(s.p[kLowCutSlope]) + 1, highStages = static_cast<int>(s.p[kHighCutSlope]) + 1;
        const float nyq = static_cast<float>(sr_ * 0.5) * 0.999f;
        const float lowF = std::min(s.p[kLowCutFreq], nyq), highF = std::min(s.p[kHighCutFreq], nyq), peakF = std::min(s.p[kPeakFreq], nyq);
        const auto peakC = makePeakFilter(sr_, peakF, s.p[kPeakQuality], decibelsToGain(s.p[kPeakGain]));
        for (int ch = 0; ch < 2; ++ch) {
            for (int i = 0; i < lowStages; ++i) {
                s.lowCut[ch][i].assign6(makeHighPass(sr_, lowF, butterworthQ(i, 2 * lowStages)));
                s.lowCut[ch][i].process(buffer[ch], n);
            }
            s.peak[ch].assign6(peakC);
            s.peak[ch].process(buffer[ch], n);
            for (int i = 0; i < highStages; ++i) {
                s.highCut[ch][i].assign6(makeLowPass(sr_, highF, butterworthQ(i, 2 * highStages)));
                s.highCut[ch][i].process(buffer[ch], n);
            }
        }
    }

    // the head every distortion module shares: wet = input * drive, temp = wet
    void driveInto(const Slot &s, int driveParam, float *buffer[2], int n) {
        const float driveGain = decibelsToGain(s.p[driveParam]);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) { wet_[ch][i] = buffer[ch][i] * driveGain; temp_[ch][i] = wet_[ch][i]; }
    }
    // ... and the tail: buffer = buffer * dry + temp * wet
    void mixBack(float mixPercent, float *buffer[2], int n) {
        const float mix = mixPercent * 0.01f, dryGain = 1.f - mix, wetGain = mix;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) buffer[ch][i] = buffer[ch][i] * dryGain + temp_[ch][i] * wetGain;
    }

    // WaveshaperModuleDSP: tanhAmp * tanh(x * tanhSlope) + sineAmp * sin(x * sineFreq)
    void waveshaper(Slot &s, float *buffer[2], int n) {
        if (s.p[kWsBypassed] > 0.5f) return;
        driveInto(s, kWsDrive, buffer, n);
        const float tanhAmp = s.p[kWsTanhAmp] * 0.01f, tanhSlope = s.p[kWsTanhSlope];
        const float sineAmp = s.p[kWsSineAmp] * 0.01f, sineFreq = s.p[kWsSineFreq];
        for (int ch = 0; ch < 2; ++ch) {
            float *d = wet_[ch];
            for (int i = 0; i < n; ++i) d[i] = tanhAmp * std::tanh(d[i] * tanhSlope) + sineAmp * std::sin(d[i] * sineFreq);
        }
        float *wet[2] = {wet_[0], wet_[1]}, *temp[2] = {temp_[0], temp_[1]};
        applyAsymmetry(temp, wet, s.p[kWsSymmetry] * 0.01f, s.p[kWsBias], n);
        mixBack(s.p[kWsMix], buffer, n);
        // Hard clipper for limiting
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i) buffer[ch][i] = std::clamp(buffer[ch][i], -1.f, 1.f);
    }

    // BitcrusherModuleDSP
    void bitcrusher(Slot &s, float *buffer[2], int n) {
        if (s.p[kBcBypassed] > 0.5f) return;
        driveInto(s, kBcDrive, buffer, n);
        // getWhiteNoise: Box-Muller, the generator seeded with the clock's milliseconds each block (as upstream; the
        // clock here is the audio's own), the same noise for both channels, multiplied by the signal
        s.random.setSeed(static_cast<int64_t>(elapsed_ * 1000 / std::max(1.0, sr_)) % 1000);
        {
            float z1 = 0;
            bool generate = false;
            const float epsilon = std::numeric_limits<float>::min();
            const float dither = s.p[kBcDither] * 0.01f;
            for (int i = 0; i < n; ++i) {
                float output;
                generate = !generate;
                if (!generate) output = z1;
                else {
                    float u1, u2;
                    do { u1 = s.random.nextFloat(); u2 = s.random.nextFloat(); } while (u1 <= epsilon);
                    const float m = std::sqrt(-2.0f * std::log(u1));
                    output = m * std::cos(2 * 3.14159265358979f * u2);
                    z1 = m * std::sin(2 * 3.14159265358979f * u2);
                }
                noise_[0][i] = output * wet_[0][i] * dither;
                noise_[1][i] = output * wet_[1][i] * dither;
            }
        }
        const float totalQLevels = std::pow(2.f, s.p[kBcBitRedux]);
        const int rateReductionRatio = static_cast<int>(sr_ / s.p[kBcRateRedux]);
        for (int ch = 0; ch < 2; ++ch) {
            float *data = wet_[ch];
            for (int i = 0; i < n; ++i) {
                // REDUCE BIT DEPTH
                const float val = data[i];
                const float remainder = std::fmod(val, 1.f / totalQLevels);
                data[i] = val - remainder;
                // Rate reduction (within the block, as upstream)
                if (rateReductionRatio > 1 && i % rateReductionRatio != 0) data[i] = data[i - i % rateReductionRatio];
            }
            for (int i = 0; i < n; ++i) data[i] += noise_[ch][i];
        }
        float *wet[2] = {wet_[0], wet_[1]}, *temp[2] = {temp_[0], temp_[1]};
        applyAsymmetry(temp, wet, s.p[kBcSymmetry] * 0.01f, s.p[kBcBias], n);
        mixBack(s.p[kBcMix], buffer, n);
    }

    // SlewLimiterModuleDSP
    void slewLimiter(Slot &s, float *buffer[2], int n) {
        if (s.p[kSlBypassed] > 0.5f) return;
        driveInto(s, kSlDrive, buffer, n);
        const float slewMin = 0.1f, slewMax = 10000.f;
        const float Ts = 1.f / static_cast<float>(sr_);
        const float slewRise = slewMax * Ts * std::pow(slewMin / slewMax, s.p[kSlRise] * 0.01f);
        const float slewFall = slewMax * Ts * std::pow(slewMin / slewMax, s.p[kSlFall] * 0.01f);
        // as upstream: both channels start from the one remembered output (the right's), and a rising input is held
        // by "fall", a falling one by "rise"
        float last = s.lastOutput;
        for (int ch = 0; ch < 2; ++ch) {
            float *d = wet_[ch];
            float output = s.lastOutput;
            for (int i = 0; i < n; ++i) {
                const float input = d[i];
                if (input > output) output = std::min(input, output + slewFall);
                else output = std::max(input, output - slewRise);
                d[i] = output;
            }
            last = output;
        }
        s.lastOutput = last;
        float *wet[2] = {wet_[0], wet_[1]}, *temp[2] = {temp_[0], temp_[1]};
        applyAsymmetry(temp, wet, s.p[kSlSymmetry] * 0.01f, s.p[kSlBias], n);
        if (s.p[kSlDCoffset] > 0.5f)
            for (int ch = 0; ch < 2; ++ch) s.dcHpf[ch].process(temp_[ch], n);
        mixBack(s.p[kSlMix], buffer, n);
    }

    // The screen's oscilloscope: sweeps of 128 columns (each the least and the most of its samples, left and right
    // averaged), each sweep starting at a rising zero crossing when one comes within a sweep's length.
    void scope(float *buffer[2], int n) {
        for (int i = 0; i < n; ++i) {
            const float x = (buffer[0][i] + buffer[1][i]) * 0.5f;
            if (scopeCol_ < 0) { // waiting for the trigger
                ++scopeWait_;
                if ((scopePrev_ <= 0.f && x > 0.f) || scopeWait_ > scopePer_ * kScopeColumns) {
                    scopeCol_ = 0; scopeN_ = 0; scopeMin_ = x; scopeMax_ = x;
                }
                scopePrev_ = x;
                if (scopeCol_ < 0) continue;
            }
            scopeMin_ = std::min(scopeMin_, x); scopeMax_ = std::max(scopeMax_, x);
            if (++scopeN_ >= scopePer_) {
                scopeBack_[static_cast<size_t>(scopeCol_)] = scopeMin_;
                scopeBack_[static_cast<size_t>(kScopeColumns + scopeCol_)] = scopeMax_;
                scopeN_ = 0; scopeMin_ = x; scopeMax_ = x;
                if (++scopeCol_ >= kScopeColumns) {
                    for (int k = 0; k < kScopeColumns * 2; ++k)
                        scopeFront_[static_cast<size_t>(k)].store(scopeBack_[static_cast<size_t>(k)], std::memory_order_relaxed);
                    scopeCol_ = -1; scopeWait_ = 0; scopePrev_ = x;
                }
            }
        }
    }

    double sr_{48000};
    int64_t elapsed_{0};
    Slot slots_[kSlots];
    float inputDb_{0.f}, outputDb_{0.f};
    float wet_[2][kMaxBlock]{}, temp_[2][kMaxBlock]{}, noise_[2][kMaxBlock]{};
    std::atomic<float> inMeter_[2]{}, outMeter_[2]{};
    int scopeAt_{9}, scopePer_{4}, scopeCol_{-1}, scopeN_{0}, scopeWait_{0};
    float scopeMin_{0}, scopeMax_{0}, scopePrev_{0};
    std::array<float, kScopeColumns * 2> scopeBack_{};
    std::array<std::atomic<float>, kScopeColumns * 2> scopeFront_{};
};

std::unique_ptr<Engine> makeBiztortionEngine() { return std::make_unique<BiztortionEngine>(); }

} // namespace nornios
