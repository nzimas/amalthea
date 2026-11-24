// Engine_CrushClone: doxiedevops' CrushClone (https://github.com/doxiedevops/CrushClone, MIT), a recreation of
// CamelCrusher — distortion, resonant filter and a one-knob compressor — as a native engine (docs/adr/0061-crushclone.md).
//
// The DSP is upstream's own, unmodified (Vendor/crushclone/Source/DSP), compiled against Vendor/juce-shim. This file
// is upstream's PluginProcessor — the parameters, the chain, the master, the dry/wet and the safety limiter —
// carried over statement for statement, without JUCE's plugin framework.
//
// It also holds what upstream has not: a trim, because the plugin is written for a DAW's full-scale material and a
// module here is tens of dB quieter. Its distortion only saturates near full scale, and its compressor's threshold
// runs from 0 to -30 dBFS, so without the trim neither would do anything at all (docs/adr/0059-chow-trim.md).
//
//   commands: set(i index, f value)   a parameter in upstream's layout order: 1 tube, 2 mech, 3 distortion on,
//                                     4 cutoff (Hz), 5 resonance, 6 filter on, 7 compressor amount, 8 phat,
//                                     9 compressor on, 10 master (dB), 11 mix; and 12, NorniOS's trim (dB)
//   polls:    cc_in, cc_out (peaks, falling), cc_gr (the compressor's gain reduction, in dB, 0 or below)
#include "EngineRegistry.hpp"

#include <JuceHeader.h>

#include "../Vendor/crushclone/Source/DSP/Compressor.h"
#include "../Vendor/crushclone/Source/DSP/Distortion.h"
#include "../Vendor/crushclone/Source/DSP/LadderFilter.h"

namespace nornios {

class CrushCloneEngine : public Engine {
public:
    const char *name() const override { return "CrushClone"; }
    const std::vector<CommandDef> &commands() const override {
        static std::vector<CommandDef> c = {{"set", "if"}};
        return c;
    }
    const std::vector<PollDef> &polls() const override {
        static std::vector<PollDef> p = {{"cc_in", true}, {"cc_out", true}, {"cc_gr", true}};
        return p;
    }

    // upstream: prepareToPlay
    void prepare(double sampleRate, int samplesPerBlock) override {
        distortion.prepare(sampleRate, samplesPerBlock, 2);
        filter.prepare(sampleRate, samplesPerBlock, 2);
        compressor.prepare(sampleRate, samplesPerBlock, 2);
        dryBuffer.setSize(2, samplesPerBlock);
        maxBlock = samplesPerBlock;
    }

    void command(const EngineCommand &c) override {
        if(c.id==4){distortion.reset();filter.reset();compressor.reset();dryBuffer.clear();return;}
        const float v = c.args[1];
        if (c.id != 1 || !std::isfinite(v)) return;
        switch (static_cast<int>(c.args[0])) {
        case 1: tubeAmount = juce::jlimit(0.f, 1.f, v); break;
        case 2: mechAmount = juce::jlimit(0.f, 1.f, v); break;
        case 3: distEnable = v > 0.5f; break;
        case 4: filterCutoff = juce::jlimit(20.f, 20000.f, v); break;
        case 5: filterReso = juce::jlimit(0.f, 1.f, v); break;
        case 6: filterEnable = v > 0.5f; break;
        case 7: compAmount = juce::jlimit(0.f, 1.f, v); break;
        case 8: phatMode = v > 0.5f; break;
        case 9: compEnable = v > 0.5f; break;
        case 10: masterVolume = juce::jlimit(-24.f, 12.f, v); break;
        case 11: dryWet = juce::jlimit(0.f, 1.f, v); break;
        case 12: trimDB = juce::jlimit(0.f, 48.f, v); break;
        default: break;
        }
    }

    void render(const float *inL, const float *inR, float *outL, float *outR, int frames) override {
        std::copy(inL, inL + frames, outL);
        std::copy(inR, inR + frames, outR);
        if (frames > maxBlock) return;

        float peakIn = 0.f;
        for (int i = 0; i < frames; ++i) peakIn = std::max(peakIn, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
        inMeter.store(std::max(peakIn, inMeter.load() * 0.93f));

        // NorniOS's trim: up to what the plugin was written for, and off again at the end, so the module's own level
        // is unchanged and the dry side of the mix comes back exactly as it went in
        const float trim = std::pow(10.f, trimDB * 0.05f), untrim = 1.f / trim;
        if (trimDB > 0.f)
            for (int i = 0; i < frames; ++i) { outL[i] *= trim; outR[i] *= trim; }

        float *channels[2] = {outL, outR};
        juce::AudioBuffer<float> buffer(channels, 2, frames);
        processBlock(buffer);

        float peakOut = 0.f;
        for (int i = 0; i < frames; ++i) {
            outL[i] *= untrim;
            outR[i] *= untrim;
            peakOut = std::max(peakOut, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
        }
        outMeter.store(std::max(peakOut, outMeter.load() * 0.93f));
        grDB.store(compressor.getGainReductionDB());
    }

    bool pollValue(int index, float &out) override {
        switch (index) {
        case 0: out = inMeter.load(); return true;
        case 1: out = outMeter.load(); return true;
        case 2: out = grDB.load(); return true;
        default: return false;
        }
    }

private:
    // upstream: processBlock
    void processBlock(juce::AudioBuffer<float> &buffer)
    {
        juce::ScopedNoDenormals noDenormals;

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        // Read all parameters
        distortion.setTubeAmount (tubeAmount);
        distortion.setMechAmount (mechAmount);
        distortion.setEnabled    (distEnable);

        filter.setCutoff    (filterCutoff);
        filter.setResonance (filterReso);
        filter.setEnabled   (filterEnable);

        compressor.setAmount   (compAmount);
        compressor.setPhatMode (phatMode);
        compressor.setEnabled  (compEnable);

        const float masterDB = masterVolume;

        // Dry copy for mix
        if (dryWet < 0.999f)
            dryBuffer.makeCopyOf (buffer);

        // === Signal chain ===
        distortion.process (buffer);
        filter.process (buffer);
        compressor.process (buffer);

        // Master gain
        if (std::abs (masterDB) > 0.05f)
        {
            const float gain = std::pow (10.0f, masterDB / 20.0f);
            buffer.applyGain (gain);
        }

        // Dry/wet
        if (dryWet < 0.999f)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* wet = buffer.getWritePointer (ch);
                const float* dry = dryBuffer.getReadPointer (ch);
                for (int i = 0; i < numSamples; ++i)
                    wet[i] = dry[i] * (1.0f - dryWet) + wet[i] * dryWet;
            }
        }

        // === SAFETY HARD LIMITER — protect speakers no matter what ===
        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
            {
                if (data[i] > 2.0f)        data[i] = 2.0f;
                else if (data[i] < -2.0f)   data[i] = -2.0f;
                if (!std::isfinite (data[i])) data[i] = 0.0f;
            }
        }
    }

    DistortionModule distortion;
    LadderFilter filter;
    CompressorModule compressor;
    juce::AudioBuffer<float> dryBuffer;

    // upstream's defaults
    float tubeAmount{0.f}, mechAmount{0.f}, filterCutoff{20000.f}, filterReso{0.f}, compAmount{0.f};
    float masterVolume{0.f}, dryWet{1.f};
    bool distEnable{true}, filterEnable{false}, phatMode{false}, compEnable{true};
    float trimDB{24.f}; // NorniOS's, not the plugin's

    int maxBlock{0};
    std::atomic<float> inMeter{0.f}, outMeter{0.f}, grDB{0.f};
};

std::unique_ptr<Engine> makeCrushCloneEngine() { return std::make_unique<CrushCloneEngine>(); }

} // namespace nornios
