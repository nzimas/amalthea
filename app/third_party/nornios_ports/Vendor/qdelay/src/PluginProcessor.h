// NorniOS: QDelay's processor without JUCE's plugin framework, so that the DSP in ./dsp (unmodified) finds what it
// reads: the parameter tree, the tempo and transport, and the pitch shifter's state. The parameters, their ranges
// and the signal path are upstream's (PluginProcessor.cpp at the commit in ../UPSTREAM); the implementation is in
// Engines/QDelayEngine.cpp (JuceHeader.h is Vendor/juce-shim's). The editor, the preset manager and the spectrum display's buffer are left out
// (docs/adr/0053-qdelay.md).
#pragma once

#include <JuceHeader.h>
#include <Oversampling2x.h>

#include <array>
#include <unordered_map>

#include "Globals.h"
#include "dsp/Crusher.h"
#include "dsp/Delay.h"
#include "dsp/DelayLine.h"
#include "dsp/Diffusor.h"
#include "dsp/Distortion.h"
#include "dsp/Flutter.h"
#include "dsp/Follower.h"
#include "dsp/Phaser.h"
#include "dsp/Pitcher.h"
#include "dsp/SVF.h"
#include "dsp/Shifter.h"
#include "dsp/Wow.h"

using namespace globals;

namespace qdelay_shim {

// juce::NormalisableRange<float>
struct Range {
    float start{0.f}, end{1.f}, interval{0.f}, skew{1.f};
    bool symmetric{false};

    float convertTo0to1(float v) const noexcept {
        float p = std::clamp((v - start) / (end - start), 0.f, 1.f);
        if (skew == 1.f) return p;
        if (!symmetric) return std::pow(p, skew);
        const float d = 2.f * p - 1.f;
        return (1.f + std::pow(std::fabs(d), skew) * (d < 0 ? -1.f : 1.f)) / 2.f;
    }
    float convertFrom0to1(float p) const noexcept {
        p = std::clamp(p, 0.f, 1.f);
        if (!symmetric) {
            if (skew != 1.f && p > 0.f) p = std::exp(std::log(p) / skew);
            return start + (end - start) * p;
        }
        float d = 2.f * p - 1.f;
        if (skew != 1.f && d != 0.f) d = std::exp(std::log(std::fabs(d)) / skew) * (d < 0 ? -1.f : 1.f);
        return start + (end - start) / 2.f * (1.f + d);
    }
    float snapToLegalValue(float v) const noexcept {
        if (interval > 0.f) v = start + interval * std::floor((v - start) / interval + 0.5f);
        return std::clamp(v, start, end);
    }
};

class Params;

// juce::RangedAudioParameter, as far as Delay's link logic uses it
class Parameter {
public:
    String id;
    Range range;
    float defaultValue{0.f};
    std::atomic<float> value{0.f};

    float getValue() const noexcept { return range.convertTo0to1(value.load()); }
    float convertTo0to1(float v) const noexcept { return range.convertTo0to1(v); }
    float convertFrom0to1(float p) const noexcept { return range.convertFrom0to1(p); }
    void setValueNotifyingHost(float normalised);
    // a denormalised value, as a host's automation or a preset would set it
    void setRaw(float v);

private:
    friend class Params;
    Params *owner{nullptr};
    std::vector<AudioProcessorValueTreeState::Listener *> listeners;
};

// juce::AudioProcessorValueTreeState, as far as the DSP uses it
class Params {
public:
    Parameter &add(const String &id, Range range, float def) {
        auto p = std::make_unique<Parameter>();
        p->id = id; p->range = range; p->defaultValue = def; p->value.store(def); p->owner = this;
        size_t h=hash(id.c_str())&255;while(fixed_[h])h=(h+1)&255;fixed_[h]=p.get();
        index_[id] = list_.size();
        list_.push_back(std::move(p));
        return *list_.back();
    }
    static size_t hash(const char*s){size_t h=2166136261u;while(*s){h^=static_cast<unsigned char>(*s++);h*=16777619u;}return h;}
    Parameter *getParameter(const char *id)const{size_t h=hash(id)&255;while(fixed_[h]){if(fixed_[h]->id==id)return fixed_[h];h=(h+1)&255;}return nullptr;}
    std::atomic<float>*getRawParameterValue(const char*id)const{auto*p=getParameter(id);return p?&p->value:nullptr;}
    std::atomic<float> *getRawParameterValue(const String &id) const {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : &list_[it->second]->value;
    }
    Parameter *getParameter(const String &id) const {
        auto it = index_.find(id);
        return it == index_.end() ? nullptr : list_[it->second].get();
    }
    Parameter *at(size_t i) const { return i < list_.size() ? list_[i].get() : nullptr; }
    size_t size() const { return list_.size(); }
    void addParameterListener(const String &id, AudioProcessorValueTreeState::Listener *l) {
        if (auto *p = getParameter(id)) p->listeners.push_back(l);
    }
    void removeParameterListener(const String &id, AudioProcessorValueTreeState::Listener *l) {
        if (auto *p = getParameter(id)) p->listeners.erase(std::remove(p->listeners.begin(), p->listeners.end(), l), p->listeners.end());
    }
    // set when any parameter changes; the processor picks it up at the next block (upstream's paramChanged)
    std::atomic<bool> changed{false};

private:
    std::vector<std::unique_ptr<Parameter>> list_;
    std::array<Parameter*,256> fixed_{};
    std::unordered_map<String, size_t> index_;
};

inline void Parameter::setRaw(float v) {
    v = range.snapToLegalValue(v);
    if (value.load() == v) return;
    value.store(v);
    if (owner) owner->changed.store(true);
    for (auto *l : listeners) l->parameterChanged(id, v);
}
inline void Parameter::setValueNotifyingHost(float normalised) { setRaw(range.convertFrom0to1(normalised)); }

} // namespace qdelay_shim

class QDelayAudioProcessor {
public:
    QDelayAudioProcessor();
    ~QDelayAudioProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock);
    // in place: left and right carry the input in and the output out
    void processBlock(float *left, float *right, int numSamples);
    void clearAll();
    void onSlider();
    std::array<SVF::EQBand, EQ_BANDS> getEqualizer(SVF::EQType type) const;
    double getSampleRate() const { return srate; }

    // the host's playhead, which upstream reads at the top of each block
    void setTempo(double bpm);
    void setPlaying(bool play, double seconds);

    qdelay_shim::Params params;
    bool isLoadingState = false;

    std::unique_ptr<Delay> delay;
    std::unique_ptr<Distortion> distPre;
    std::unique_ptr<Crusher> crushPre;
    std::unique_ptr<Phaser> phaser;
    int distPrePath = 0; // Pre or feedback distortion/saturation
    std::unique_ptr<Distortion> distPost;
    std::unique_ptr<Crusher> crushPost;
    float dist_pre = 0.f;
    float dist_post = 0.f;
    std::unique_ptr<qdelay_shim::Oversampling2x> distPreOversampler;
    std::unique_ptr<qdelay_shim::Oversampling2x> distPostOversampler;
    // upstream makes a new diffusor when the legacy switch changes; here both exist and the switch clears the one
    // it turns to, so nothing is allocated while audio runs
    std::unique_ptr<DiffusorBase> diffusorModern, diffusorLegacy;
    DiffusorBase *diffusor = nullptr;
    int diffPath = 0; // pre or post delay diffusion
    std::unique_ptr<Pitcher> pitcher;
    std::unique_ptr<Shifter> shifter;
    float pitcherSpeed = 0.f;
    int pitcherPath = 0; // feedback or post delay signal
    int shifterMode = 0; // pitch shiter = 0, freq shifter = 1
    int phaserPath = 0; // feedback or post delay signal
    std::vector<float> dryBuffer[2];
    std::vector<float> wetBuffer[2];
    std::vector<float> distPreBuffer[2];
    std::vector<float> distPostBuffer[2];
    Follower follower;
    int eqPath = 0; // Input or output eq
    std::array<SVF::EQBand, EQ_BANDS> eqBands;
    std::vector<SVF> eqL;
    std::vector<SVF> eqR;
    bool useLegacyDiffusor = false;
    bool diffusorChanged = false;

    // wow and flutter
    float tapeAmt = 0.f;
    int tapeFadeSamps = 0;
    int tapeFadeSize = 500;
    bool tapeFadeIn = false;
    std::unique_ptr<Flutter> flutter;
    std::unique_ptr<Wow> wow;
    std::unique_ptr<DelayLine> wowflut_l; // wow + flutter delay
    std::unique_ptr<DelayLine> wowflut_r;

    // PlayHead state
    bool playing = false;
    double ppqPosition = 0.0;
    double beatsPerSample = 0.00005;
    double beatsPerSecond = 1.0;
    int samplesPerBeat = 44100;
    double secondsPerBeat = 0.1;
    double srate = 44100.0;
    double secondsPerBar = 1.0;
    double timeInSeconds = 0.f;

    // meters
    std::atomic<float> rmsLeft{0.f};
    std::atomic<float> rmsRight{0.f};
    std::atomic<float> duckEnv{0.f};

private:
    void createParameters();
};
