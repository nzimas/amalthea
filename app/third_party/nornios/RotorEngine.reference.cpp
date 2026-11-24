// Engine_Rotor: blackboxaudio's Rotor (https://github.com/blackboxaudio/rotor, GPL-3), a ring modulator whose
// modulator is a wavetable — sine, triangle, sawtooth, square or semi-sine — as a native engine
// (docs/adr/0076-rotor.md).
//
// Upstream's DSP is its processBlock and the helpers under it, and this is that, statement for statement, in a
// stand-in for the plugin class with the same members (Vendor/rotor/src/RotorProcessor.cpp is the reference). Three
// of its faults crackled and are mended, each marked NorniOS below (docs/adr/0077-rotor-crackle.md): the phase ran
// on from one channel into the next, so both jumped at every block's edge; the triangle's second half was silence;
// and the table was read at the point below the phase, not between points.
//
//   commands: set(i index, f value)   1 waveform (0-4), 2 rate (10-12500 Hz), 3 noise (0-1), 4 pulse width
//                                     (0.01-0.99), 5 inversion (0/1), 6 mix (0-100 %), 7 level (-48..12 dB)
//   polls:    rt_level, rt_scope (data: 128 points each of what came in, the modulator and what went out, over
//             about three cycles of the modulator)
#include "EngineRegistry.hpp"

#include <JuceHeader.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

using namespace juce;

//==============================================================================
// The members of upstream's RotorAudioProcessor that its DSP reads, under their names.
class RotorAudioProcessor {
public:
    RotorAudioProcessor() {
        // upstream's createParameterLayout defaults
        p_waveform = 0.f;
        p_rate = 1000.f;
        p_noise = 0.f;
        p_pulseWidth = 0.5f;
        p_inversion = 0.f;
        p_mix = 50.f;
        p_level = 0.f;

        // upstream's constructor
        modulationShape = &p_waveform;
        modulationRate = &p_rate;
        modulationNoise = &p_noise;
        modulationIsInverted = &p_inversion;
        pulseWidth = &p_pulseWidth;
        level = &p_level;
        mix = &p_mix;

        previousShape = *modulationShape;
        modulationInversionFactor = getModulationInversion((bool)*modulationIsInverted);
        previousPulseWidth = *pulseWidth;
        previousLevel = *level;
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    void processBlock(AudioBuffer<float> &buffer, MidiBuffer &midiMessages);
    float getModulationInversion(bool inverted);
    void setPhaseDelta(double frequency, double sampleRate);
    void setWavetable(int waveformIndex);

    double getSampleRate() const noexcept { return sampleRate_; }

    float readWavetable(double phase) const noexcept {
        const int n = wavetable.size();
        if (n == 0) return 0.f;
        const int i = static_cast<int>(phase) % n;
        const float frac = static_cast<float>(phase - std::floor(phase));
        const float a = wavetable.getUnchecked(i), b = wavetable.getUnchecked((i + 1) % n);
        return a + (b - a) * frac;
    }

    // the parameters' values, as the value tree would hold them
    std::atomic<float> p_waveform, p_rate, p_noise, p_pulseWidth, p_inversion, p_mix, p_level;

    std::atomic<float> *modulationShape;
    std::atomic<float> *modulationRate;
    std::atomic<float> *modulationNoise;
    std::atomic<float> *modulationIsInverted;
    std::atomic<float> *pulseWidth;
    std::atomic<float> *level;
    std::atomic<float> *mix;

    float previousShape;
    float modulationInversionFactor;
    float previousPulseWidth;
    float previousLevel;

    Array<float> wavetable;
    int wavetableSize;
    double currentPhase = 0.0;
    double previousPhase = 0.0;
    double phaseDelta = 0.0;

    double sampleRate_ = 48000.0;
};

//==============================================================================
// upstream's own, from RotorProcessor.cpp, statement for statement

void RotorAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // set frequency variables
    wavetableSize = 1024;
    setPhaseDelta((double)*modulationRate, sampleRate);

    // write the wavetable according to current waveform
    setWavetable((int)*modulationShape);
}

void RotorAudioProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midiMessages)
{
    // write new wavetable if pulse width changes
    if (previousPulseWidth != *pulseWidth) {
        previousPulseWidth = *pulseWidth;
        setWavetable((int)*modulationShape);
    }

    // write new wavetable if waveform selection change
    if (previousShape != *modulationShape) {
        previousShape = *modulationShape;
        setWavetable((int)*modulationShape);
    }
    // update phase delta for wavetable
    setPhaseDelta((double)*modulationRate, this->getSampleRate());

    // update inversion factor
    modulationInversionFactor = getModulationInversion((bool)*modulationIsInverted);

    // NorniOS: every channel starts from where the modulator stood as the block began (docs/adr/0077-rotor-crackle.md).
    // Upstream let the right channel carry on from where the left had finished and the next block's left from where
    // the right had, so each channel's modulator jumped at every block's edge: a click a block, a crackle.
    const double blockPhase = previousPhase;
    double endPhase = previousPhase;
    for (auto channel = 0; channel < buffer.getNumChannels(); channel++) {
        previousPhase = blockPhase;
        for (auto sample = 0; sample < buffer.getNumSamples(); sample++) {
            // set current phase to previous block's last sample value (for continuity)
            currentPhase = previousPhase;

            // get write pointer to buffer samples
            auto* samples = buffer.getWritePointer(channel);

            // read first sample value into two variables (original [o_sampleData] and processed [p_sampleData])
            auto o_sampleData = samples[sample], p_sampleData = samples[sample];

            // multiply wavetable value by original signal and update phase value
            // NorniOS: read between the table's points rather than at the one below, which at an audio rate is a
            // phase that jitters by up to a point, heard as grit
            p_sampleData *= (readWavetable(currentPhase) * modulationInversionFactor);
            currentPhase = fmod(currentPhase + phaseDelta, wavetableSize);

            // read current mix ratio value and scale between 0.0f and 1.0f (** DOES NOT CHECK IF MIX IS > 100.0f **)
            auto currentDryWet = *mix / 100.0f;

            // calculate values of dry and wet signals according to currentDryWet
            p_sampleData *= currentDryWet;
            o_sampleData *= (1.0f - currentDryWet);

            // calculate random noise and udpate p_sampleData accordingly to noise parameter
            auto n_sampleData = p_sampleData * Random::getSystemRandom().nextFloat();
            n_sampleData *= *modulationNoise;
            p_sampleData *= (1.0f - *modulationNoise);
            p_sampleData += n_sampleData;

            // calculate result sample value (r_sampleData)
            auto r_sampleData = p_sampleData + o_sampleData;

            // loop through channels and set samples accordingly
            buffer.setSample(channel, sample, r_sampleData);

            // update previous phase value to avoid discontinuities
            previousPhase = currentPhase;
        }
        endPhase = previousPhase;
    }
    previousPhase = endPhase;

    // get current output gain and update if needed then apply it
    auto currentGain = powf(2, *level / 6);
    if (previousLevel == currentGain) {
        buffer.applyGain(currentGain);
    } else {
        buffer.applyGainRamp(0, buffer.getNumSamples(), previousLevel, currentGain);
        previousLevel = currentGain;
    }
}

// switches modulation inversion factor between values -1.0f and 1.0f
float RotorAudioProcessor::getModulationInversion(bool inverted)
{
    if (inverted)
        return -1.0f;
    return 1.0f;
}

// updates angle delta provided a frequency value from slider
void RotorAudioProcessor::setPhaseDelta(double frequency, double sampleRate)
{
    auto cyclesPerSample = frequency / sampleRate;
    phaseDelta = cyclesPerSample * (double)wavetableSize;
}

// writes wavetable according to currently selected waveform
void RotorAudioProcessor::setWavetable(int waveformIndex)
{
    // clear the wavetable
    wavetable.clear();

    // set pulse width
    int pulseWidthSize = *pulseWidth * wavetableSize;

    // check values and compute accordingly
    switch (waveformIndex) {
    // SINE
    default:
    case 0:
        for (int i = 0; i < wavetableSize; i++) {
            auto waveformValue = sinf(MathConstants<float>::twoPi * (float)i / wavetableSize);
            wavetable.insert(i, waveformValue);
        }
        break;

    // TRIANGLE
    case 1:
        for (int i = 0; i < wavetableSize / 2; i++) {
            float waveformValue = 2.0f * ((float)i / (wavetableSize / 2)) - 1.0f;
            wavetable.insert(i, waveformValue);
        }
        // NorniOS: the second half mirrors the first, as upstream meant; its wavetable[-1 * (i + 1)] reads before
        // the table's start, which is zero, so its triangle fell to silence halfway and jumped every cycle
        for (int i = wavetableSize / 2; i < wavetableSize; i++) {
            wavetable.insert(i, wavetable[wavetableSize - 1 - i]);
        }
        break;

    // SAWTOOTH
    case 2:
        for (int i = 0; i < wavetableSize; i++) {
            float waveformValue = 2.0f * ((float)i / wavetableSize) - 1.0f;
            wavetable.insert(i, waveformValue);
        }
        break;

    // SQUARE
    case 3:
        for (int i = 0; i < pulseWidthSize; i++) {
            wavetable.insert(i, 1.0f);
        }
        for (int i = pulseWidthSize; i < wavetableSize; i++) {
            wavetable.insert(i, -1.0f);
        }
        break;

    // SEMI-SINE
    case 4:
        for (int i = 0; i < wavetableSize; i++) {
            auto waveformValue = sinf(MathConstants<float>::pi * (float)i / wavetableSize);
            wavetable.insert(i, waveformValue);
        }
        break;
    }
}

//==============================================================================
namespace nornios {

class RotorEngine : public Engine {
public:
    static constexpr int kPoints = 128;
    static constexpr int kHistory = 8192;

    RotorEngine() { proc_.wavetable.ensureStorageAllocated(1024); }

    const char *name() const override { return "Rotor"; }
    const std::vector<CommandDef> &commands() const override {
        static std::vector<CommandDef> c = {{"set", "if"}};
        return c;
    }
    const std::vector<PollDef> &polls() const override {
        static std::vector<PollDef> p = {{"rt_level", true}, {"rt_scope", true}};
        return p;
    }

    void prepare(double sampleRate, int maxFrames) override {
        proc_.sampleRate_ = sampleRate;
        proc_.prepareToPlay(sampleRate, maxFrames);
    }

    void command(const EngineCommand &c) override {
        if (c.id != 1 || !std::isfinite(c.args[1])) return;
        const float v = c.args[1];
        switch (static_cast<int>(c.args[0])) {
        case 1: proc_.p_waveform.store(static_cast<float>(jlimit(0, 4, static_cast<int>(std::lround(v))))); break;
        case 2: proc_.p_rate.store(jlimit(10.f, 12500.f, v)); break;
        case 3: proc_.p_noise.store(jlimit(0.f, 1.f, v)); break;
        case 4: proc_.p_pulseWidth.store(jlimit(0.01f, 0.99f, v)); break;
        case 5: proc_.p_inversion.store(v > 0.5f ? 1.f : 0.f); break;
        case 6: proc_.p_mix.store(jlimit(0.f, 100.f, v)); break;
        case 7: proc_.p_level.store(jlimit(-48.f, 12.f, v)); break;
        default: break;
        }
    }

    void render(const float *inL, const float *inR, float *outL, float *outR, int frames) override {
        std::copy(inL, inL + frames, outL);
        std::copy(inR, inR + frames, outR);
        const double phase0 = proc_.previousPhase;   // where the left channel's modulator starts this block

        float *channels[2] = {outL, outR};
        AudioBuffer<float> buffer(channels, 2, frames);
        MidiBuffer midi;
        proc_.processBlock(buffer, midi);

        // what the screen draws: the left channel in and out, and the modulator it was multiplied by, which is the
        // table read from where the phase stood as the block began
        const double size = proc_.wavetableSize, delta = proc_.phaseDelta;
        double ph = phase0;
        float peak = 0.f;
        for (int i = 0; i < frames; ++i) {
            if (!std::isfinite(outL[i])) outL[i] = 0.f;
            if (!std::isfinite(outR[i])) outR[i] = 0.f;
            peak = std::max(peak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
            const size_t at = static_cast<size_t>(written_ % kHistory);
            in_[at] = inL[i];
            mod_[at] = proc_.readWavetable(ph) * proc_.modulationInversionFactor;
            out_[at] = outL[i];
            ++written_;
            ph = std::fmod(ph + delta, size);
        }
        level_.store(std::max(peak, level_.load() * 0.93f));

        // about three cycles of the modulator across the screen, from as far back as the history reaches
        const double cycle = delta > 0.0 ? size / delta : kHistory;
        const int stride = std::max(1, std::min(kHistory / kPoints, static_cast<int>(std::lround(3.0 * cycle / kPoints))));
        const long long start = written_ - static_cast<long long>(stride) * kPoints;
        if (start < 0) return;
        for (int p = 0; p < kPoints; ++p) {
            const size_t at = static_cast<size_t>((start + static_cast<long long>(p) * stride) % kHistory);
            scope_[static_cast<size_t>(p)].store(in_[at]);
            scope_[static_cast<size_t>(kPoints + p)].store(mod_[at]);
            scope_[static_cast<size_t>(2 * kPoints + p)].store(out_[at]);
        }
    }

    bool pollValue(int index, float &out) override {
        if (index != 0) return false;
        out = level_.load();
        return true;
    }

    int pollData(int index, float *out, int max) override {
        if (index != 1) return 0;
        const int n = std::min(max, kPoints * 3);
        for (int i = 0; i < n; ++i) out[i] = scope_[static_cast<size_t>(i)].load();
        return n;
    }

private:
    RotorAudioProcessor proc_;
    std::array<float, kHistory> in_{}, mod_{}, out_{};
    long long written_{0};
    std::atomic<float> level_{0.f};
    std::array<std::atomic<float>, kPoints * 3> scope_{};
};

std::unique_ptr<Engine> makeRotorEngine() { return std::make_unique<RotorEngine>(); }

} // namespace nornios
