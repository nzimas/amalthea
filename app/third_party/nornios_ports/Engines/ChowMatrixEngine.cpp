// Engine_ChowMatrix: Jatin Chowdhury's ChowMatrix (https://github.com/Chowdhury-DSP/ChowMatrix, BSD-3; chowdsp_utils
// GPL-3), a delay effect whose delays grow as trees from the two input channels: each node a delay with feedback,
// filters, distortion, pitch shift, diffusion, reversing and modulation, feeding its children in turn; as a native
// engine (docs/adr/0087-chowmatrix.md).
//
// The delay processing is upstream's, compiled unmodified (Vendor/chowmatrix/src/dsp: DelayProc, VariableDelay with
// all nine delay types, Diffusion, Distortion with its lookup tables, PitchShiftWrapper, Reverser), on the chowdsp_utils
// it was written against (Vendor/chowmatrix/chowdsp_dsp, unmodified). The nodes, the tree and the plugin are carried
// over statement for statement (Vendor/chowmatrix/reference): DelayNode's cookParameters, process, panner and output,
// BaseNode's processing of its children, ChowMatrix's dry/wet processing with its wet gain compensation, and the
// Insanity control, which wanders the nodes' delays and pans. Galleries, presets, A/B states, the host parameter
// mappings, solo and the editor are the plugin's shell and are left out.
//
// NorniOS: the nodes are a pool of eight made before the audio starts, and the tree over them is rewired by command,
// without allocating; each node's delay lines are 2^17 samples rather than 2^19 (DelayStore.h here).
//
//   commands: np(i node, i param, f value)   a node's parameter, 1-13 in upstream's order (delay ms, pan, feedback,
//                                            gain dB, LPF Hz, HPF Hz, pitch st, diffusion, distortion, reverse ms,
//                                            mod freq Hz, delay mod, pan mod)
//             node(i node, i parent)         -1 off, 0 the left input, 1 the right, 2+k a child of node k (k < node)
//             set(i index, f value)          1 dry dB, 2 wet dB, 3 wet gain compensation, 4 insanity, 5 delay type
//                                            (0-8), 6 tempo sync, 7 tempo bpm, 8 NorniOS output level dB,
//                                            9 a new matrix begins, 10 it ends (ADR-0088)
//             flush(i)                       empties every delay
//   polls:    cm_level, cm_state (data: per node its parent code, delay 0-1 and pan with modulation, and how loud;
//             then how loud each input is)
#include "EngineRegistry.hpp"

#include <pch.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// upstream's DSP and what it stands on, unmodified
#include "../Vendor/chowmatrix/chowdsp_dsp/Other/chowdsp_Panner.cpp"
#include "../Vendor/chowmatrix/polylogarithm/Li2.cpp"
#include "../Vendor/chowmatrix/src/dsp/Distortion/LookupTables.cpp"
#include "../Vendor/chowmatrix/src/dsp/Distortion/Distortion.cpp"
#include "../Vendor/chowmatrix/src/dsp/Delay/VariableDelay.cpp"
#include "../Vendor/chowmatrix/src/dsp/Delay/Diffusion.cpp"
#include "../Vendor/chowmatrix/src/dsp/Delay/PitchShiftWrapper.cpp"
#include "../Vendor/chowmatrix/src/dsp/Delay/Reverser.cpp"
#include "../Vendor/chowmatrix/src/dsp/Delay/DelayProc.cpp"

// upstream's ParamHelpers::setParameterValue sets an AudioParameterFloat; here, the value the node reads
namespace ParamHelpers
{
void setParameterValue (Parameter* param, float newVal) { param->value.store (newVal); }
} // namespace ParamHelpers

using namespace TempoSyncUtils;

//==============================================================================
// BaseNode<DelayNode>, upstream's processing of a node's children; the children are the pool's nodes the tree gives
// it, in order
class DelayNode;
class DBaseNode
{
public:
    virtual ~DBaseNode() = default;

    void prepareBase (double newSampleRate, int newSamplesPerBlock)
    {
        sampleRate = newSampleRate;
        samplesPerBlock = newSamplesPerBlock;
        childBuffer.setSize (1, newSamplesPerBlock);
        children.reserve (16);
    }

    virtual void process (AudioBuffer<float>& inBuffer, AudioBuffer<float>& outBuffer);
    virtual float getNodeLevel (float inputLevel) const;

    std::vector<DelayNode*> children;

protected:
    AudioBuffer<float> childBuffer;
    double sampleRate = 44100.0;
    int samplesPerBlock = 256;
};

//==============================================================================
// DelayNode, upstream's: its parameters as the node reads them, and its processing
class DelayNode : public DBaseNode
{
public:
    DelayNode()
        : delayRange (makeSkewed (0.0f, ParamHelpers::maxDelay, ParamHelpers::centreDelay)),
          lpfRange (makeSkewed (ParamHelpers::minLPF, ParamHelpers::maxLPF, std::sqrt (ParamHelpers::minLPF * ParamHelpers::maxLPF))),
          hpfRange (makeSkewed (ParamHelpers::minHPF, ParamHelpers::maxHPF, std::sqrt (ParamHelpers::minHPF * ParamHelpers::maxHPF))),
          modRange (makeSkewed (ParamHelpers::minModFreq, ParamHelpers::maxModFreq, 2.0f))
    {
        // upstream's createParameterLayout, in its order, at its defaults
        auto init = [this] (int idx, NormalisableRange<float> range, float def) {
            params[(size_t) idx].range = range;
            params[(size_t) idx].value.store (def);
        };
        init (0, delayRange, ParamHelpers::centreDelay);
        init (1, { -1.0f, 1.0f }, 0.0f);
        init (2, { 0.0f, ParamHelpers::maxFeedback }, 0.0f);
        init (3, { -ParamHelpers::maxGain, ParamHelpers::maxGain }, 0.0f);
        init (4, lpfRange, ParamHelpers::maxLPF);
        init (5, hpfRange, ParamHelpers::minHPF);
        init (6, { -ParamHelpers::maxPitch, ParamHelpers::maxPitch }, 0.0f);
        init (7, { 0.0f, 1.0f }, 0.0f);
        init (8, { 0.0f, 1.0f }, 0.0f);
        init (9, { 0.0f, 1000.0f }, 0.0f);
        init (10, modRange, 0.0f);
        init (11, { 0.0f, 1.0f }, 0.0f);
        init (12, { -1.0f, 1.0f }, 0.0f);

        processors.get<gainIdx>().setRampDurationSeconds (0.05);
        panner.setRule (chowdsp::Panner<float>::Rule::squareRoot3dB);

        delaySmoother.reset();
        panSmoother.reset();
    }

    // Get/Set delay param [0,1]
    float getDelay() const noexcept { return delayMs->convertTo0to1 (delayMs->get()); }
    float getDelayWithMod() const noexcept
    {
        return delayMs->convertTo0to1 (delayMs->get() + processors.get<delayIdx>().getModDepth());
    }
    void setDelay (float newDelay) { ParamHelpers::setParameterValue (delayMs, delayMs->convertFrom0to1 (newDelay)); }

    void setDelaySync (bool shouldBeSynced) { syncDelay = shouldBeSynced; }
    void setTempo (double newTempoBPM) { tempoBPM = newTempoBPM; }

    // Get/Set pan param [-1,1]
    float getPan() const noexcept { return pan->get(); }
    float getPanWithMod() const noexcept { return pan->get() + panModValue; }
    void setPan (float newPan) { ParamHelpers::setParameterValue (pan, newPan); }

    void setDelayType (VariableDelay::DelayType type) { processors.get<delayIdx>().setDelayType (type); }
    void flushDelays() { processors.get<delayIdx>().flushDelay(); }
    void setCopyOnSwitch (bool copy) { processors.get<delayIdx>().setCopyOnSwitch (copy); }   // NorniOS (ADR-0090)

    // a parameter by its place in upstream's order, clamped to its range
    void setParam (int idx, float value)
    {
        if (idx < 0 || idx >= ParamHelpers::numParams) return;
        auto& p = params[(size_t) idx];
        p.value.store (jlimit (p.range.start, p.range.end, value));
    }

    void prepare (double newSampleRate, int newSamplesPerBlock)
    {
        prepareBase (newSampleRate, newSamplesPerBlock);

        processors.prepare ({ newSampleRate, (uint32) newSamplesPerBlock, 1 });
        panner.prepare ({ newSampleRate, (uint32) newSamplesPerBlock, 2 });
        modSine.prepare ({ newSampleRate, (uint32) newSamplesPerBlock, 1 });
        cookParameters (true);

        panBuffer.setSize (2, newSamplesPerBlock);
    }

    void process (AudioBuffer<float>& inBuffer, AudioBuffer<float>& outBuffer) override
    {
        // process through node delay processors
        dsp::AudioBlock<float> inBlock { inBuffer };
        cookParameters();
        processors.process<dsp::ProcessContextReplacing<float>> ({ inBlock });

        DBaseNode::process (inBuffer, outBuffer); // process through children
        processPanner (inBlock); // apply pan
        addToOutput (outBuffer);
    }

    float getNodeLevel (float inputLevel) const override
    {
        float outputLevel = inputLevel * Decibels::decibelsToGain (gainDB->get());
        outputLevel *= 1.0f + *feedback;

        return outputLevel + DBaseNode::getNodeLevel (outputLevel);
    }

    // Filters to smooth random param changes from Insanity
    dsp::IIR::Filter<float> delaySmoother;
    dsp::IIR::Filter<float> panSmoother;

    float lastPeak = 0.0f;   // NorniOS: how loud the node was this block, for the screen

private:
    static NormalisableRange<float> makeSkewed (float start, float end, float centre)
    {
        NormalisableRange<float> r { start, end };
        r.setSkewForCentre (centre);
        return r;
    }

    void cookParameters (bool force = false)
    {
        float delayLenMs = *delayMs;
        if (syncDelay)
        {
            auto& rhythm = getRhythmForParam (delayMs->convertTo0to1 (delayLenMs));
            delayLenMs = 1000.0f * (float) getTimeForRythm (tempoBPM, rhythm);
        }

        processors.get<gainIdx>().setGainDecibels (*gainDB);
        processors.get<delayIdx>().setParameters ({ delayLenMs,
                                                    *feedback,
                                                    *lpfHz,
                                                    *hpfHz,
                                                    *distortion,
                                                    *pitchSt,
                                                    *diffAmt,
                                                    *revTimeMs,
                                                    modFreq,
                                                    *delayMod,
                                                    (float) tempoBPM,
                                                    tempoSyncedLFO,
                                                    getPlayHead() },
                                                  force);

        if (tempoSyncedLFO)
            modSine.setFreqSynced (modFreq, (float) tempoBPM);
        else
            modSine.setFrequency (*modFreq);
        panner.setPan (*pan);
    }

    void processPanner (dsp::AudioBlock<float>& inputBlock)
    {
        panBuffer.setSize (2, (int) inputBlock.getNumSamples(), false, false, true);
        dsp::AudioBlock<float> panBlock { panBuffer };

        if (*panMod == 0.0f || *modFreq == 0.0f) // no modulation needed
        {
            modSine.reset();
            panModValue = 0.0f;
            panner.process<dsp::ProcessContextNonReplacing<float>> ({ inputBlock, panBlock });
        }
        else // process with modulation
        {
            const auto panParamValue = (float) *pan;
            const auto panModParamValue = (float) *panMod;
            modSine.setPlayHead (getPlayHead());

            constexpr size_t subBlockSize = 256;
            auto processSubBlock = [=] (size_t i, size_t numSamples) {
                panModValue = 0.33f * panModParamValue * modSine.processSample(); // max pan mod = 33%
                panner.setPan (jlimit (-1.0f, 1.0f, panParamValue + panModValue));

                auto panSubBlock = panBlock.getSubBlock (i, numSamples);
                const auto inSubBlock = inputBlock.getSubBlock (i, numSamples);
                panner.process<dsp::ProcessContextNonReplacing<float>> ({ inSubBlock, panSubBlock });

                // iterate modSine forward
                for (size_t j = 1; j < numSamples; ++j)
                    modSine.processSample();
            };

            size_t i = 0;
            for (; i + subBlockSize <= inputBlock.getNumSamples(); i += subBlockSize)
                processSubBlock (i, subBlockSize);

            if (i < inputBlock.getNumSamples())
                processSubBlock (i, inputBlock.getNumSamples() - i);
        }
    }

    void addToOutput (AudioBuffer<float>& outBuffer)
    {
        const int numSamples = outBuffer.getNumSamples();

        // NorniOS: solo and mute are the editor's; every node is heard (upstream's "normal state")
        float peak = 0.0f;
        for (int ch = 0; ch < outBuffer.getNumChannels(); ++ch)
        {
            outBuffer.addFrom (ch, 0, panBuffer, ch, 0, numSamples);
            const float* p = panBuffer.getReadPointer (ch);
            for (int i = 0; i < numSamples; ++i)
                peak = std::max (peak, std::abs (p[i]));
        }
        lastPeak = peak;
    }

    AudioPlayHead* getPlayHead() const noexcept { return nullptr; }   // the session's tempo arrives by setTempo

    NormalisableRange<float> delayRange, lpfRange, hpfRange, modRange;
    std::array<Parameter, ParamHelpers::numParams> params;

    bool syncDelay = false;
    double tempoBPM = 120.0;

    // parameter handles
    Parameter* delayMs = &params[0];
    Parameter* pan = &params[1];
    Parameter* feedback = &params[2];
    Parameter* gainDB = &params[3];
    Parameter* lpfHz = &params[4];
    Parameter* hpfHz = &params[5];
    Parameter* pitchSt = &params[6];
    Parameter* diffAmt = &params[7];
    Parameter* distortion = &params[8];
    Parameter* revTimeMs = &params[9];
    Parameter* modFreq = &params[10];
    Parameter* delayMod = &params[11];
    Parameter* panMod = &params[12];

    enum
    {
        gainIdx,
        delayIdx,
    };

    MyProcessorChain<dsp::Gain<float>, DelayProc> processors;
    AudioBuffer<float> panBuffer;
    chowdsp::Panner<float> panner;

    TempoSyncUtils::SyncedLFO modSine;
    float panModValue = 0.0f;
    bool tempoSyncedLFO = false;
};

void DBaseNode::process (AudioBuffer<float>& inBuffer, AudioBuffer<float>& outBuffer)
{
    for (auto* child : children)
    {
        childBuffer.makeCopyOf (inBuffer, true);
        child->process (childBuffer, outBuffer);
    }
}

float DBaseNode::getNodeLevel (float inputLevel) const
{
    float outputLevel = 0.0f;
    for (auto* child : children)
        outputLevel += child->getNodeLevel (inputLevel);

    return outputLevel;
}

class InputNode : public DBaseNode
{
};

//==============================================================================
namespace nornios {

class ChowMatrixEngine : public Engine {
public:
    static constexpr int kNodes = 8;
    static constexpr float negInfDB = -60.0f;

    ChowMatrixEngine() {
        for (auto& n : nodes_) n = std::make_unique<DelayNode>();
        // upstream's new plugin: one node from each input
        parent_.fill (-1);
        parent_[0] = 0;
        parent_[1] = 1;
    }

    const char *name() const override { return "ChowMatrix"; }
    const std::vector<CommandDef> &commands() const override {
        static std::vector<CommandDef> c = {{"np", "iif"}, {"node", "ii"}, {"set", "if"}, {"flush", "i"}};
        return c;
    }
    const std::vector<PollDef> &polls() const override {
        static std::vector<PollDef> p = {{"cm_level", true}, {"cm_state", true}};
        return p;
    }

    // upstream's prepareToPlay
    void prepare(double sampleRate, int samplesPerBlock) override {
        sampleRate_ = sampleRate;
        for (size_t ch = 0; ch < 2; ++ch)
        {
            inputNodes[ch].prepareBase (sampleRate, samplesPerBlock);
            chBuffers[ch].setSize (1, samplesPerBlock);
        }
        for (auto& n : nodes_) {
            n->prepare (sampleRate, samplesPerBlock);
            n->setDelayType (delayType_);
            n->children.reserve (kNodes);
        }
        dryBuffer.setSize (2, samplesPerBlock);
        dryGain.setRampDurationSeconds (gainFadeTime);
        wetGain.setRampDurationSeconds (gainFadeTime);
        dryGain.prepare ({ sampleRate, (uint32) samplesPerBlock, 2 });
        wetGain.prepare ({ sampleRate, (uint32) samplesPerBlock, 2 });
        insanityParameterChanged (insanity_);
        rebuildTree();
        maxBlock_ = samplesPerBlock;
    }

    void command(const EngineCommand &c) override {
        // a new matrix (docs/adr/0088-chowmatrix-clicks.md): set 9 begins it, and what follows waits until the wet
        // signal has faded out; set 10 ends it
        if (c.id == 3 && static_cast<int>(c.args[0]) == 9) { morphing_ = true; morphEnded_ = false; morphAge_ = 0; morphFlushNext_ = -1; return; }
        if (c.id == 3 && static_cast<int>(c.args[0]) == 10) { morphEnded_ = true; return; }
        if (morphing_ && morphFlushNext_ < 0) {   // while the delays are emptied, the new matrix is in place already
            if (morphQueue_.size() < morphQueue_.capacity()) morphQueue_.push_back(c);
            return;
        }
        apply(c);
    }

    void apply(const EngineCommand &c) {
        switch (c.id) {
        case 1: {
            const int n = static_cast<int>(c.args[0]), p = static_cast<int>(c.args[1]);
            if (n >= 0 && n < kNodes && std::isfinite(c.args[2])) nodes_[(size_t) n]->setParam (p - 1, c.args[2]);
            break;
        }
        case 2: {
            const int n = static_cast<int>(c.args[0]), p = static_cast<int>(c.args[1]);
            if (n < 0 || n >= kNodes) break;
            // a node hangs from an input or from an earlier node, so the tree has no loops
            const int parent = (p >= 2 && p - 2 < n) || p == 0 || p == 1 ? p : -1;
            if (parent_[(size_t) n] == -1 && parent != -1) nodes_[(size_t) n]->flushDelays();   // a new node starts empty
            parent_[(size_t) n] = parent;
            rebuildTree();
            break;
        }
        case 3: if (std::isfinite(c.args[1])) set(static_cast<int>(c.args[0]), c.args[1]); break;
        case 4: for (auto& n : nodes_) n->flushDelays(); break;
        default: break;
        }
    }

    void render(const float *inL, const float *inR, float *outL, float *outR, int frames) override {
        std::copy(inL, inL + frames, outL);
        std::copy(inR, inR + frames, outR);
        float *channels[2] = {outL, outR};
        AudioBuffer<float> buffer(channels, 2, frames);

        insanityTick(frames);
        morphTick(frames);
        processAudioBlock (buffer);

        // NorniOS's level, moved across the block
        const float target = Decibels::decibelsToGain(levelDb_);
        const float from = gain_ < 0.f ? target : gain_;
        const float step = (target - from) / static_cast<float>(frames);
        float peak = 0.f;
        for (int i = 0; i < frames; ++i) {
            const float g = from + step * static_cast<float>(i + 1);
            outL[i] = std::isfinite(outL[i]) ? soften(outL[i] * g) : 0.f;
            outR[i] = std::isfinite(outR[i]) ? soften(outR[i] * g) : 0.f;
            peak = std::max(peak, std::max(std::fabs(outL[i]), std::fabs(outR[i])));
        }
        gain_ = target;
        level_.store(std::max(peak, level_.load() * 0.93f));
        publish(inL, inR, frames);
    }

    bool pollValue(int index, float &out) override {
        if (index != 0) return false;
        out = level_.load();
        return true;
    }

    int pollData(int index, float *out, int max) override {
        if (index != 1) return 0;
        const int n = std::min(max, kState);
        for (int i = 0; i < n; ++i) out[i] = state_[static_cast<size_t>(i)].load();
        return n;
    }

private:
    static constexpr double gainFadeTime = 0.05;

    // over 0.7 the output bends towards full scale along a tanh (as modal's, ADR-0070): a dense matrix fed back can sum
    // past what the output carries, and a hard clip there is a crackle
    static float soften(float s) noexcept {
        constexpr float knee = 0.7f, room = 1.f - knee;
        const float a = std::fabs(s);
        if (a <= knee) return s;
        return std::copysign(knee + room * std::tanh((a - knee) / room), s);
    }

    // A new matrix: the wet signal fades out over 80 ms; then the commands that came with it are applied and every delay
    // is emptied, so nothing of the old matrix plays through the new one; then it fades back in over 150 ms. Should the
    // end never come, it ends by itself after a second.
    //
    // Emptying the delays is spread over blocks, one node a block, while the wet signal is silent: all eight at once,
    // and the delay types switched with their lines copied, took an iPad some 30 ms, six blocks' real time, and the
    // audio dropped out (docs/adr/0090-chowmatrix-dropouts.md). A line switched in is not copied into: it is emptied.
    void morphTick(int frames) {
        if (!morphing_) return;
        morphAge_ += frames;
        if (morphFlushNext_ >= 0) {
            nodes_[(size_t) morphFlushNext_]->flushDelays();
            if (++morphFlushNext_ == kNodes) { morphFlushNext_ = -1; morphing_ = false; }
            return;
        }
        if (morphFade_ > 0.f && morphAge_ < sampleRate_) return;
        for (auto& n : nodes_) n->setCopyOnSwitch (false);
        for (const auto &c : morphQueue_) apply(c);
        for (auto& n : nodes_) n->setCopyOnSwitch (true);
        morphQueue_.clear();
        if (morphEnded_ || morphAge_ >= sampleRate_) morphFlushNext_ = 0;
    }

    void applyMorphFade (AudioBuffer<float>& buffer) {
        const float target = morphing_ ? 0.f : 1.f;
        const float out = 1.f / (0.08f * (float) sampleRate_), in = 1.f / (0.15f * (float) sampleRate_);
        for (int i = 0; i < buffer.getNumSamples(); ++i) {
            if (morphFade_ > target) morphFade_ = std::max(target, morphFade_ - out);
            else if (morphFade_ < target) morphFade_ = std::min(target, morphFade_ + in);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch) buffer.getWritePointer(ch)[i] *= morphFade_;
        }
    }

    bool morphing_ = false, morphEnded_ = false;
    int morphFlushNext_ = -1;
    double morphAge_ = 0;
    float morphFade_ = 1.f;
    std::vector<EngineCommand> morphQueue_ = [] { std::vector<EngineCommand> v; v.reserve(512); return v; }();
    static constexpr int kState = kNodes * 4 + 2;

    void set(int index, float v) {
        switch (index) {
        case 1: dryDB_ = jlimit(negInfDB, 12.f, v); break;
        case 2: wetDB_ = jlimit(negInfDB, 12.f, v); break;
        case 3: wetGainComp_ = v > 0.5f; break;
        case 4: insanity_ = jlimit(0.f, 1.f, v); insanityParameterChanged(insanity_); break;
        case 5:
            delayType_ = static_cast<VariableDelay::DelayType>(jlimit(0, 8, static_cast<int>(std::lround(v))));
            for (auto& n : nodes_) n->setDelayType (delayType_);   // upstream's DelayTypeControl
            break;
        case 6: sync_ = v > 0.5f; for (auto& n : nodes_) n->setDelaySync (sync_); break;   // SyncControl
        case 7:
            if (v > 10.f && v != tempo_) { tempo_ = v; for (auto& n : nodes_) n->setTempo (tempo_); }
            break;
        case 8: levelDb_ = jlimit(-40.f, 24.f, v); break;
        default: break;
        }
    }

    // the tree over the pool: each input's children, and each node's, in the pool's order
    void rebuildTree() {
        for (auto& in : inputNodes) in.children.clear();
        for (auto& n : nodes_) n->children.clear();
        for (int i = 0; i < kNodes; ++i) {
            const int p = parent_[(size_t) i];
            if (p == 0 || p == 1) inputNodes[(size_t) p].children.push_back(nodes_[(size_t) i].get());
            else if (p >= 2) nodes_[(size_t) (p - 2)]->children.push_back(nodes_[(size_t) i].get());
        }
        // a node whose parent is off is not reached, and is off too
        for (int i = 0; i < kNodes; ++i) active_[(size_t) i] = false;
        for (int i = 0; i < kNodes; ++i) {
            const int p = parent_[(size_t) i];
            active_[(size_t) i] = p == 0 || p == 1 || (p >= 2 && active_[(size_t) (p - 2)]);
        }
    }

    // upstream's ChowMatrix::processAudioBlock
    void processAudioBlock (AudioBuffer<float>& buffer)
    {
        auto setGain = [] (auto& gainProc, float gainParamDB) {
            if (gainParamDB <= negInfDB)
                gainProc.setGainLinear (0.0f);
            else
                gainProc.setGainDecibels (gainParamDB);
        };

        // Keep dry signal
        dryBuffer.makeCopyOf (buffer, true);
        dsp::AudioBlock<float> dryBlock (dryBuffer);
        dsp::ProcessContextReplacing<float> dryContext (dryBlock);
        setGain (dryGain, dryDB_);
        dryGain.process (dryContext);

        // copy input channels
        const int numSamples = buffer.getNumSamples();
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            chBuffers[ch].setSize (1, numSamples, false, false, true);
            chBuffers[ch].copyFrom (0, 0, buffer, ch, 0, numSamples);
        }

        // get wet signal
        buffer.clear();
        for (size_t ch = 0; ch < (size_t) buffer.getNumChannels(); ++ch)
            inputNodes[ch].process (chBuffers[ch], buffer);

        // NorniOS: the wet signal fades out while a new matrix is put in place, and back in after it
        applyMorphFade (buffer);

        // wet gain (with gain compensation if needed)
        setGain (wetGain, wetDB_);
        if (wetGainComp_)
        {
            auto wetAddedGain = 0.0f;
            for (size_t ch = 0; ch < (size_t) buffer.getNumChannels(); ++ch)
                wetAddedGain += inputNodes[ch].getNodeLevel (1.0f);

            if (wetAddedGain > 0.0f)
                wetGain.setGainLinear (wetGain.getGainLinear() / wetAddedGain);
        }

        dsp::AudioBlock<float> wetBlock (buffer);
        dsp::ProcessContextReplacing<float> wetContext (wetBlock);
        wetGain.process (wetContext);

        // sum with dry signal
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            buffer.addFrom (ch, 0, dryBuffer, ch, 0, numSamples);
    }

    // upstream's InsanityControl: its timer, which runs on the message thread there, is counted in samples here and
    // runs where the nodes are, on the audio thread; its maps by node id are arrays by the node's place in the pool
    template <typename F> void doForNodes (F f) {
        for (int i = 0; i < kNodes; ++i) if (active_[(size_t) i]) f (nodes_[(size_t) i].get(), i);
    }

    void insanityStarting()
    {
        doForNodes ([=] (DelayNode* n, int i) {
            insanityResetMap[(size_t) i] = std::make_pair (n->getDelay(), n->getPan());
        });
    }

    void insanityEnding()
    {
        // upstream's lock helper resets both parameters unless a user has locked them; nothing is locked here
        doForNodes ([=] (DelayNode* n, int i) {
            const auto& [delayVal, panVal] = insanityResetMap[(size_t) i];
            n->setDelay (delayVal);
            n->setPan (panVal);
        });
    }

    void insanityTick (int frames)
    {
        timerSamples_ += frames;
        if (timerSamples_ < sampleRate_ / timerFreq) return;
        timerSamples_ = 0;
        timerCallback();
    }

    void timerCallback()
    {
        if (insanity_ == 0.0f)
        {
            if (lastInsanity != 0.0f) // insanity is turning off
                insanityEnding();

            lastInsanity = 0.0f;
            return;
        }

        if (lastInsanity == 0.0f) // turning on insanity
            insanityStarting();

        // set randomised params
        float scale = 0.5f * std::pow (insanity_, 2.0f);
        doForNodes ([=] (DelayNode* n, int) {
            float delay = n->getDelay();
            float pan = n->getPan();

            delay += n->delaySmoother.processSample (delay_dist (generator) * scale);
            pan += n->panSmoother.processSample (pan_dist (generator) * scale);

            n->setDelay (jlimit (0.0f, 1.0f, delay));
            n->setPan (jlimit (-1.0f, 1.0f, pan));
        });

        lastInsanity = insanity_;
    }

    void insanityParameterChanged (float newValue)
    {
        // timer callback won't do anything, so reset smoothing filters
        if (newValue == 0.0f)
        {
            for (auto& n : nodes_) {
                n->delaySmoother.reset();
                n->panSmoother.reset();
            }
        }

        // calc new timer callback rate
        timerFreq = int (std::pow (10, 1.0f + 0.65f * std::sqrt (newValue)));

        // update smoothing filters
        dsp::IIR::Coefficients<float>::Reuse reuse(insanityReuse_);
        auto smoothCoefs = dsp::IIR::Coefficients<float>::makeFirstOrderLowPass ((double) timerFreq, 2.0f);
        for (auto& n : nodes_) {
            n->delaySmoother.coefficients = smoothCoefs;
            n->panSmoother.coefficients = smoothCoefs;
        }
    }

    // What the screen draws, per node: its parent code (-1 off), where it sits (delay 0-1 and pan, with their
    // modulation), and how loud it is; then each input
    void publish(const float *inL, const float *inR, int frames) {
        for (int i = 0; i < kNodes; ++i) {
            DelayNode& n = *nodes_[(size_t) i];
            const size_t at = static_cast<size_t>(i * 4);
            state_[at].store(active_[(size_t) i] ? static_cast<float>(parent_[(size_t) i]) : -1.f);
            state_[at + 1].store(n.getDelayWithMod());
            state_[at + 2].store(jlimit(-1.f, 1.f, n.getPanWithMod()));
            state_[at + 3].store(active_[(size_t) i] ? std::max(n.lastPeak, state_[at + 3].load() * 0.9f) : 0.f);
        }
        float l = 0, r = 0;
        for (int i = 0; i < frames; ++i) { l = std::max(l, std::fabs(inL[i])); r = std::max(r, std::fabs(inR[i])); }
        state_[kNodes * 4].store(std::max(l, state_[kNodes * 4].load() * 0.9f));
        state_[kNodes * 4 + 1].store(std::max(r, state_[kNodes * 4 + 1].load() * 0.9f));
    }

    // upstream's members
    dsp::IIR::Coefficients<float>::Ptr insanityReuse_=dsp::IIR::Coefficients<float>::fromFour(1,0,1,0);
    std::array<InputNode, 2> inputNodes;
    AudioBuffer<float> chBuffers[2];
    AudioBuffer<float> dryBuffer;
    dsp::Gain<float> dryGain;
    dsp::Gain<float> wetGain;
    float dryDB_ = -12.0f, wetDB_ = -12.0f;   // upstream's defaults
    bool wetGainComp_ = false;
    bool sync_ = false;
    VariableDelay::DelayType delayType_ = VariableDelay::ThirdInterp;   // upstream's default, "Smooth"

    // the Insanity control's
    float insanity_ = 0.0f;
    int timerFreq = 10;
    std::default_random_engine generator;
    std::uniform_real_distribution<float> delay_dist { -0.05f, 0.05f };
    std::uniform_real_distribution<float> pan_dist { -0.1f, 0.1f };
    float lastInsanity = 0.0f;
    std::array<std::pair<float, float>, kNodes> insanityResetMap {};

    std::array<std::unique_ptr<DelayNode>, kNodes> nodes_;
    std::array<int, kNodes> parent_ {};
    std::array<bool, kNodes> active_ {};
    double sampleRate_ = 48000.0, timerSamples_ = 0;
    float tempo_ = 120.f;
    int maxBlock_ = 512;
    float levelDb_ = 0.f, gain_ = -1.f;
    std::atomic<float> level_{0.f};
    std::array<std::atomic<float>, kState> state_{};
};

std::unique_ptr<Engine> makeChowMatrixEngine() { return std::make_unique<ChowMatrixEngine>(); }

} // namespace nornios
