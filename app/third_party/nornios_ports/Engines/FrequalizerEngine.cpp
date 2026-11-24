// Engine_Frequalizer: Daniel Walz's Frequalizer (https://github.com/ffAudio/Frequalizer, BSD 3-clause), a six-band
// parametric equalizer, as a native engine (docs/adr/0062-frequalizer.md).
//
// Upstream's DSP is JUCE's own: six IIR biquads in a chain, their coefficients from dsp::IIR::Coefficients, and an
// output gain. So there is no third-party DSP to run here — what makes it Frequalizer is the arrangement, and that
// is carried over from Vendor/frequalizer/Source/FrequalizerProcessor.cpp statement for statement: the six bands
// with their names, types and defaults, the twelve filter types, the solo, and the plot of what the bands add up to.
// The filters themselves are Vendor/juce-shim's, written from JUCE's formulas.
//
// An equalizer asks nothing of the level it is given — a filter is a filter at any amplitude — so, unlike the
// plugins whose distortion or threshold is measured against full scale, this one needs no trim (ADR-0059).
//
//   commands: set(i index, f value)   1 output gain (linear); then five per band, band b at 2 + (b-1) * 5:
//                                     type, frequency (Hz), quality, gain (linear), active
//             solo(i band)            0 for none, 1..6 to hear that band alone
//   polls:    fq_in, fq_out (peaks, falling), fq_curve (data: what the bands add up to, 128 points in dB across
//             20 Hz to 20 kHz, as upstream's plot draws it), fq_spectrum (data: 128 points in dB of what leaves,
//             NorniOS's own, where upstream's editor runs its analyser)
#include "EngineRegistry.hpp"

#include <JuceHeader.h>

namespace nornios {

class FrequalizerEngine : public Engine {
public:
    // upstream: FrequalizerAudioProcessor::FilterType
    enum FilterType { NoFilter = 0, HighPass, HighPass1st, LowShelf, BandPass, AllPass, AllPass1st, Notch, Peak,
                      HighShelf, LowPass1st, LowPass, LastFilterID };
    static constexpr int kBands = 6;
    static constexpr int kPoints = 128;

    const char *name() const override { return "Frequalizer"; }
    const std::vector<CommandDef> &commands() const override {
        static std::vector<CommandDef> c = {{"set", "if"}, {"solo", "i"}};
        return c;
    }
    const std::vector<PollDef> &polls() const override {
        static std::vector<PollDef> p = {{"fq_in", true}, {"fq_out", true}, {"fq_curve", false}, {"fq_spectrum", true}};
        return p;
    }

    FrequalizerEngine() {
        // upstream: createDefaultBands
        bands[0] = {HighPass, 20.f, 0.707f, 1.f, true};
        bands[1] = {LowShelf, 250.f, 0.707f, 1.f, true};
        bands[2] = {Peak, 500.f, 0.707f, 1.f, true};
        bands[3] = {Peak, 1000.f, 0.707f, 1.f, true};
        bands[4] = {HighShelf, 5000.f, 0.707f, 1.f, true};
        bands[5] = {LowPass, 12000.f, 0.707f, 1.f, true};
        for (int i = 0; i < kPoints; ++i)
            plotHz[static_cast<size_t>(i)] = 20.0 * std::pow(1000.0, static_cast<double>(i) / (kPoints - 1));
    }

    void prepare(double sr, int) override {
        sampleRate = sr;
        for (int b = 0; b < kBands; ++b) {
            updateBand(b);
            for (int ch = 0; ch < 2; ++ch) filters[b][ch].reset();
        }
        /* no analyser display in the tracker audio path */;
    }

    void command(const EngineCommand &c) override {
        if(c.id==4){for(auto& band:filters)for(auto& f:band)f.reset();return;}
        const int which = static_cast<int>(c.args[0]);
        if (c.id == 2) { soloed = (which >= 1 && which <= kBands) ? which - 1 : -1; /* no analyser display in the tracker audio path */; return; }
        if (c.id != 1 || !std::isfinite(c.args[1])) return;
        const float v = c.args[1];
        if (which == 1) { outputGain = juce::jlimit(0.f, 2.f, v); /* no analyser display in the tracker audio path */; return; } // as upstream: the plot has the gain in it
        const int b = (which - 2) / 5, field = (which - 2) % 5;
        if (b < 0 || b >= kBands) return;
        Band &band = bands[b];
        switch (field) {
        case 0: band.type = juce::jlimit(0, static_cast<int>(LastFilterID) - 1, static_cast<int>(v)); break;
        case 1: band.frequency = juce::jlimit(20.f, 20000.f, v); break;
        case 2: band.quality = juce::jlimit(0.1f, 10.f, v); break;
        case 3: band.gain = juce::jlimit(0.0631f, 15.85f, v); break; // upstream's range: -24 to +24 dB
        case 4: band.active = v > 0.5f; break;
        default: return;
        }
        updateBand(b);
        /* no analyser display in the tracker audio path */;
    }

    void render(const float *inL, const float *inR, float *outL, float *outR, int frames) override {
        float peakIn = 0.f;
        for (int i = 0; i < frames; ++i) {
            outL[i] = inL[i]; outR[i] = inR[i];
            peakIn = std::max(peakIn, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
        }
        inMeter.store(std::max(peakIn, inMeter.load() * 0.93f));

        float *out[2] = {outL, outR};
        for (int b = 0; b < kBands; ++b) {
            if (bypassed(b)) continue;
            for (int ch = 0; ch < 2; ++ch) {
                float *d = out[ch];
                auto &f = filters[b][ch];
                for (int i = 0; i < frames; ++i) d[i] = f.processSample(d[i]);
                f.snapToZero();
            }
        }

        float peakOut = 0.f;
        for (int i = 0; i < frames; ++i) {
            outL[i] *= outputGain; outR[i] *= outputGain;
            peakOut = std::max(peakOut, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
        }
        outMeter.store(std::max(peakOut, outMeter.load() * 0.93f));
        // Amalthea has no spectral plot; audio processing needs no FFT.
    }

    bool pollValue(int index, float &out) override {
        if (index == 0) { out = inMeter.load(); return true; }
        if (index == 1) { out = outMeter.load(); return true; }
        return false;
    }

    int pollData(int index, float *out, int max) override {
        if (max < kPoints) return 0;
        if (index == 2) { // what the bands add up to, in dB
            for (int i = 0; i < kPoints; ++i) out[i] = curve[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            return kPoints;
        }
        if (index == 3) { // what is leaving, in dB
            for (int i = 0; i < kPoints; ++i) out[i] = spectrum[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            return kPoints;
        }
        return 0;
    }

private:
    struct Band { int type; float frequency, quality, gain; bool active; };

    // upstream: updateBypassedStates
    bool bypassed(int b) const { return soloed >= 0 ? soloed != b : !bands[b].active; }

    // upstream: updateBand — the same twelve, in the same order
    void updateBand(int index) {
        if (!(sampleRate > 0)) return;
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        Coeffs::Reuse reuse(coefficients[index]);
        const Band &band = bands[index];
        const float freq = std::min(band.frequency, static_cast<float>(sampleRate) * 0.49f);
        Coeffs::Ptr c;
        switch (band.type) {
        case NoFilter:    c = Coeffs::fromFour(1, 0, 1, 0); break;
        case LowPass:     c = Coeffs::makeLowPass(sampleRate, freq, band.quality); break;
        case LowPass1st:  c = Coeffs::makeFirstOrderLowPass(sampleRate, freq); break;
        case LowShelf:    c = Coeffs::makeLowShelf(sampleRate, freq, band.quality, band.gain); break;
        case BandPass:    c = Coeffs::makeBandPass(sampleRate, freq, band.quality); break;
        case AllPass:     c = Coeffs::makeAllPass(sampleRate, freq, band.quality); break;
        case AllPass1st:  c = Coeffs::makeFirstOrderAllPass(sampleRate, freq); break;
        case Notch:       c = Coeffs::makeNotch(sampleRate, freq, band.quality); break;
        case Peak:        c = Coeffs::makePeakFilter(sampleRate, freq, band.quality, band.gain); break;
        case HighShelf:   c = Coeffs::makeHighShelf(sampleRate, freq, band.quality, band.gain); break;
        case HighPass1st: c = Coeffs::makeFirstOrderHighPass(sampleRate, freq); break;
        case HighPass:    c = Coeffs::makeHighPass(sampleRate, freq, band.quality); break;
        default: return;
        }
        coefficients[index] = c;
        for (int ch = 0; ch < 2; ++ch) filters[index][ch].coefficients = c;
    }

    // upstream: updatePlots — every band that is heard, multiplied together
    void updatePlot() {
        if (!(sampleRate > 0)) return;
        for (int i = 0; i < kPoints; ++i) {
            double mag = 1.0;
            for (int b = 0; b < kBands; ++b)
                if (!bypassed(b) && coefficients[b])
                    mag *= coefficients[b]->getMagnitudeForFrequency(plotHz[static_cast<size_t>(i)], sampleRate);
            mag *= outputGain;
            curve[static_cast<size_t>(i)].store(static_cast<float>(20.0 * std::log10(std::max(mag, 1e-6))), std::memory_order_relaxed);
        }
    }

    // NorniOS's own, where upstream's editor runs an analyser thread: a window of what leaves, its spectrum gathered
    // into the same 128 points the curve is drawn at, falling slowly so the screen is readable
    void analyse(const float *l, const float *r, int frames) {
        for (int i = 0; i < frames; ++i) {
            window[static_cast<size_t>(fill)] = (l[i] + r[i]) * 0.5f;
            if (++fill < kFFT) continue;
            fill = 0;
            for (int k = 0; k < kFFT; ++k) { // Hann
                const float w = 0.5f * (1.f - std::cos(2.f * 3.14159265f * k / (kFFT - 1)));
                fftData[static_cast<size_t>(k)] = window[static_cast<size_t>(k)] * w;
            }
            fft.performRealOnlyForwardTransform(fftData.data());
            const double binHz = sampleRate / kFFT;
            for (int p = 0; p < kPoints; ++p) {
                const int bin = juce::jlimit(1, kFFT / 2 - 1, static_cast<int>(plotHz[static_cast<size_t>(p)] / binHz + 0.5));
                const float re = fftData[static_cast<size_t>(2 * bin)], im = fftData[static_cast<size_t>(2 * bin + 1)];
                const float db = 20.f * std::log10(std::max(2.f * std::sqrt(re * re + im * im) / kFFT, 1e-6f));
                const float was = spectrum[static_cast<size_t>(p)].load(std::memory_order_relaxed);
                spectrum[static_cast<size_t>(p)].store(std::max(db, was - 3.f), std::memory_order_relaxed);
            }
        }
    }

    static constexpr int kFFT = 1024;

    Band bands[kBands]{};
    juce::dsp::IIR::Coefficients<float>::Ptr coefficients[kBands];
    juce::dsp::IIR::Filter<float> filters[kBands][2];
    float outputGain{1.f};
    int soloed{-1};
    double sampleRate{0};

    std::array<double, kPoints> plotHz{};
    std::array<std::atomic<float>, kPoints> curve{};
    std::array<std::atomic<float>, kPoints> spectrum{};
    std::atomic<float> inMeter{0.f}, outMeter{0.f};

    juce::dsp::FFT fft{10};
    std::array<float, kFFT> window{};
    std::array<float, kFFT * 2> fftData{};
    int fill{0};
};

std::unique_ptr<Engine> makeFrequalizerEngine() { return std::make_unique<FrequalizerEngine>(); }

} // namespace nornios
