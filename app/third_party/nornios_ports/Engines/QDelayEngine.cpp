// Engine_QDelay: tiagolr's QDelay (https://github.com/tiagolr/qdelay, GPL 3) as a native engine.
//
// The DSP is upstream's own, unmodified (Vendor/qdelay/src/dsp). This file is upstream's PluginProcessor.cpp —
// the parameter layout, prepareToPlay, onSlider, clearAll and processBlock — carried over statement for statement
// onto plain buffers, with JUCE's plugin framework replaced by Vendor/qdelay/shim (docs/adr/0053-qdelay.md).
//
//   commands: set(i index, f value)      a parameter, by its 1-based place in the layout, as a plain value
//             tempo(f bpm)               the host's tempo
//             transport(i playing, f s)  the host's transport and its time in seconds
//             clear(i)                   empties every line, as the plugin does when the host starts playing
//   polls:    qd_in, qd_out_l, qd_out_r (peaks, falling), qd_duck (the ducker's gain reduction, 0..1),
//             qd_params (how many parameters the layout has, for the script's list to be checked against)
#include "EngineRegistry.hpp"
#include <cstdio>

#include "../Vendor/qdelay/src/PluginProcessor.h"

// ---------------------------------------------------------------------------------------------------------------
// upstream: PluginProcessor.cpp

void QDelayAudioProcessor::createParameters()
{
    using qdelay_shim::Range;
    auto choice = [this](const String& id, int count, int def) { params.add(id, Range{ 0.f, (float)(count - 1), 1.f, 1.f, false }, (float)def); };
    auto toggle = [this](const String& id, bool def) { params.add(id, Range{ 0.f, 1.f, 1.f, 1.f, false }, def ? 1.f : 0.f); };
    auto linear = [this](const String& id, float lo, float hi, float def) { params.add(id, Range{ lo, hi, 0.f, 1.f, false }, def); };
    auto ranged = [this](const String& id, Range r, float def) { params.add(id, r, def); };

    choice("mode", 4, 0); // Normal, Ping-Pong, Tap, Classic Ping-Pong
    toggle("reverse", false);
    toggle("disable_clipping", false);
    toggle("link", true);
    choice("sync_l", 4, 1); // RateHz, Straight, Triplet, Dotted
    choice("sync_r", 4, 1);
    ranged("rate_l", Range{ 0.0f, 5.f, 0.001f, 0.3f, false }, .5f);
    ranged("rate_r", Range{ 0.0f, 5.f, 0.001f, 0.3f, false }, .5f);
    choice("rate_sync_l", 7, 3); // 1/64 .. 1/1
    choice("rate_sync_r", 7, 3);
    linear("mix", 0.f, 1.f, 0.5f);
    linear("out_gain", -18.f, 18.f, 0.0f);
    linear("feedback", -1.f, 1.f, 0.5f);
    linear("pipo_width", -1.f, 1.f, 1.f);
    ranged("haas_width", Range{ -1.f, 1.f, 0.0001f, 0.3f, true }, 0.f);

    linear("pan_dry", 0.f, 1.f, 0.5f);
    toggle("pan_dry_sum", false);

    linear("pan_wet", 0.f, 1.f, 0.5f);
    toggle("pan_wet_sum", false);
    linear("stereo", 0.f, 2.f, 1.0f);

    linear("swing", -1.f, 1.f, 0.0f);
    linear("feel", -1.f, 1.f, 0.0f);
    linear("accent", -1.f, 1.f, 0.0f);

    toggle("diff_legacy", false);
    linear("diff_amt", 0.f, 1.f, 0.0f);
    linear("diff_size", 0.f, 1.f, 0.0f);
    choice("diff_path", 2, 0); // Pre, Post

    linear("mod_depth", 0.f, 1.f, 0.0f);
    ranged("mod_rate", Range{ 0.01f, 10.f, 0.0001f, 0.5f, false }, 0.01f);

    linear("dist_pre", 0.f, 1.f, 0.0f);
    choice("dist_pre_path", 2, 0); // Pre, Feedback
    linear("dist_post", 0.f, 1.f, 0.0f);
    choice("dist_mode", 2, 0); // Tape, Tube
    linear("dist_drive", 0.f, 1.f, .5f);
    linear("dist_trim", -24.f, 24.f, -6.0f);
    linear("dist_color", 0.f, 1.f, 0.0f);
    linear("dist_bias", 0.f, 1.f, 0.0f);
    linear("dist_dyn", 0.f, 1.f, 0.0f);

    choice("crush_path", 3, 0); // Pre, Post, Feedback
    toggle("crush_upsample", true);
    linear("crush_srate", 0.f, 1.f, 0.0f);
    linear("crush_bits", 0.f, 1.f, 0.0f);

    linear("tape_amt", 0.f, 1.f, 0.0f);
    ranged("flutter_rate", Range{ 0.01f, 50.f, 0.0001f, 0.3f, false }, 10.f);
    linear("flutter_depth", 0.0f, 1.f, .1f);
    ranged("wow_rate", Range{ 0.01f, 6.f, 0.0001f, 0.5f, false }, .5f);
    linear("wow_depth", 0.0f, 1.f, 0.5f);
    linear("wow_drift", 0.0f, 1.f, 0.25f);
    linear("wow_var", 0.0f, 1.f, 0.25f);

    choice("shifter_mode", 2, 0); // Pitch, Frequency
    ranged("freq_shift", Range{ -1000.f, 1000.f, 0.1f, 0.35f, true }, 0.f);
    ranged("pitch_shift", Range{ -12.f, 12.f, 0.01f, 1.f, false }, 0.0f);
    choice("pitch_mode", 3, 1); // Drums, General, Smooth
    choice("pitch_path", 2, 1); // Feedback, Post
    linear("pitch_mix", 0.f, 1.f, 1.0f);

    ranged("duck_thres", Range{ 0.0f, 1.f - 0.001f, 0.001f, 3.f, false }, 1.f - 0.001f);
    ranged("duck_amt", Range{ 0.f, 1.f - 0.001f, 0.001f, 3.f, false }, 0.0f);
    ranged("duck_atk", Range{ 0.01f, 200.0f, 0.01f, 0.75f, false }, 5.f);
    linear("duck_hld", 0.0f, 1000.0f, 0.f);
    ranged("duck_rel", Range{ 10.f, 5000.0f, 1.f, 0.5f, false }, 500.f);

    choice("phaser_path", 2, 1); // Feedback, Post
    ranged("phaser_center", Range{ 20.f, 20000.f, 1.f, 0.3f, false }, 2000.0f);
    linear("phaser_depth", 0.f, 48.f, 12.0f);
    choice("phaser_sync", 4, 1); // RateHz, Straight, Triplet, Dotted
    ranged("phaser_rate", Range{ 0.01f, 10.f, 0.0001f, 0.5f, false }, 1.f);
    choice("phaser_rate_sync", 12, 3); // 1/64 .. 32/1
    linear("phaser_res", 0.f, 1.f, 0.0f);
    linear("phaser_morph", 0.f, 1.f, 0.0f);
    linear("phaser_stereo", 0.f, 1.f, 0.0f);
    linear("phaser_mix", 0.f, 1.f, 0.0f);

    choice("eq_path", 2, 0); // Input, Output
    auto getEQBandFreq = [](int band)
        {
            return 20.f * 2.f * std::pow(10000.f / 20.f / 2.f, band / (float)(EQ_BANDS - 1));
        };

    for (const char* prefix : { "inputeq_band", "decayeq_band" }) {
        for (int i = 0; i < EQ_BANDS; ++i) {
            auto paramPrefix = String(prefix) + std::to_string(i + 1);
            choice(paramPrefix + "_mode", 3, 1); // Filter, EQ, Filter2
            ranged(paramPrefix + "_freq", Range{ 20.f, 20000.f, 1.f, 0.3f, false }, getEQBandFreq(i));
            linear(paramPrefix + "_q", 0.2f, 8.f, 0.707f);
            linear(paramPrefix + "_gain", -EQ_MAX_GAIN, EQ_MAX_GAIN, 0.f);
            toggle(paramPrefix + "_bypass", false);
        }
    }
}

QDelayAudioProcessor::QDelayAudioProcessor()
{
    createParameters();

    delay = std::make_unique<Delay>(*this);
    distPre = std::make_unique<Distortion>(*this);
    distPost = std::make_unique<Distortion>(*this);
    crushPre = std::make_unique<Crusher>(*this);
    crushPost = std::make_unique<Crusher>(*this);
    pitcher = std::make_unique<Pitcher>();
    shifter = std::make_unique<Shifter>(*this);
    flutter = std::make_unique<Flutter>(*this);
    phaser = std::make_unique<Phaser>(*this);
    wow = std::make_unique<Wow>(*this);
    wowflut_l = std::make_unique<DelayLine>();
    wowflut_r = std::make_unique<DelayLine>();
    diffusorModern = std::make_unique<Diffusor>();
    diffusorLegacy = std::make_unique<DiffusorLegacy>();
    diffusor = diffusorModern.get();
    distPreOversampler = std::make_unique<qdelay_shim::Oversampling2x>();
    distPostOversampler = std::make_unique<qdelay_shim::Oversampling2x>();
}

QDelayAudioProcessor::~QDelayAudioProcessor()
{
}

std::array<SVF::EQBand, EQ_BANDS> QDelayAudioProcessor::getEqualizer(SVF::EQType type) const
{
    std::array<SVF::EQBand, EQ_BANDS> bands;

    const char* pre = type == 0 ? "inputeq_" : "decayeq_";
    for (int i = 0; i < EQ_BANDS; i++)
    {
        SVF::EQBand band{};
        band.mode = SVF::PK;
        char n[64];auto key=[&](const char* suffix){std::snprintf(n,sizeof(n),"%sband%d_%s",pre,i+1,suffix);return n;};

        auto filterOrShelf = (int)params.getRawParameterValue(key("mode"))->load();
        if (i == 0 && filterOrShelf == 0) band.mode = SVF::HP;
        else if (i == 0 && filterOrShelf == 1) band.mode = SVF::LS;
        else if (i == 0 && filterOrShelf == 2) band.mode = SVF::HP6;
        else if (i == EQ_BANDS - 1 && filterOrShelf == 0) band.mode = SVF::LP;
        else if (i == EQ_BANDS - 1 && filterOrShelf == 1) band.mode = SVF::HS;
        else if (i == EQ_BANDS - 1 && filterOrShelf == 2) band.mode = SVF::LP6;
        else if (filterOrShelf == 0) band.mode = SVF::BP;

        band.freq = params.getRawParameterValue(key("freq"))->load();
        float gain = params.getRawParameterValue(key("gain"))->load();
        band.gain = std::exp(gain * DB2LOG);
        band.q = params.getRawParameterValue(key("q"))->load();
        bool bypass = params.getRawParameterValue(key("bypass"))->load();

        if (bypass || (filterOrShelf == 1 && std::fabs(gain) < 1e-4f))
            band.mode = SVF::Off;

        bands[i]=band;
    }

    return bands;
}

void QDelayAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (auto& b : dryBuffer) b.assign((size_t)samplesPerBlock, 0.f);
    for (auto& b : wetBuffer) b.assign((size_t)samplesPerBlock, 0.f);
    srate = sampleRate;
    distPreOversampler->initProcessing(samplesPerBlock);
    distPreOversampler->reset();
    distPostOversampler->initProcessing(samplesPerBlock);
    distPostOversampler->reset();
    distPrePath = (int)params.getRawParameterValue("dist_pre_path")->load();
    delay->prepare((float)srate*2); // prepare the largest routing rate once
    delay->prepare((float)srate * (distPrePath == 1 ? distPreOversampler->getOversamplingFactor() : 1));
    distPre->prepare((float)srate * (float)distPreOversampler->getOversamplingFactor());
    distPost->prepare((float)srate * (float)distPostOversampler->getOversamplingFactor());
    crushPre->prepare((float)srate * (float)distPreOversampler->getOversamplingFactor());
    crushPost->prepare((float)srate * (float)distPostOversampler->getOversamplingFactor());
    diffusorModern->prepare((float)srate);
    diffusorLegacy->prepare((float)srate);
    for (auto& b : distPreBuffer) b.assign((size_t)samplesPerBlock * distPreOversampler->getOversamplingFactor(), 0.f);
    for (auto& b : distPostBuffer) b.assign((size_t)samplesPerBlock * distPostOversampler->getOversamplingFactor(), 0.f);
    pitcher->init((Pitcher::WindowMode)params.getRawParameterValue("pitch_mode")->load());
    shifter->prepare((float)sampleRate);
    flutter->prepare((float)sampleRate);
    wow->prepare((float)sampleRate);
    wowflut_l->resize((int)std::ceil(srate / 10));
    wowflut_r->resize((int)std::ceil(srate / 10));
    wowflut_l->clear();
    wowflut_r->clear();
    phaser->prepare((float)srate);
    onSlider();
}

void QDelayAudioProcessor::onSlider()
{
    float thresh = 1.f - params.getRawParameterValue("duck_thres")->load();
    float attack = params.getRawParameterValue("duck_atk")->load();
    float hold = params.getRawParameterValue("duck_hld")->load();
    float release = params.getRawParameterValue("duck_rel")->load();
    follower.prepare((float)srate, thresh, false, attack, hold, release, true);

    // prepare input EQ
    eqPath = (int)params.getRawParameterValue("eq_path")->load();
    eqBands = getEqualizer(SVF::EQType::ParamEQ);
    for (int i = 0; i < EQ_BANDS; ++i)
    {
        if (eqL.size() < EQ_BANDS)
        {
            eqL.push_back({});
            eqR.push_back({});
            eqL[i].mode = SVF::Off;
        }

        // only bands that change mode need to be reset
        // otherwise the frequency,gain and q are interpolated during SVF::processBlock
        auto rate = (float)srate;
        bool rateChanged = eqL[i].srate != rate;
        auto& band = eqBands[i];
        if (band.mode != eqL[i].mode || rateChanged)
        {
            auto mode = band.mode;
            if (mode == SVF::LP) eqL[i].lp(rate, band.freq, band.q);
            else if (mode == SVF::LP6) eqL[i].lp6(rate, band.freq);
            else if (mode == SVF::LS) eqL[i].ls(rate, band.freq, band.q, band.gain);
            else if (mode == SVF::HP) eqL[i].hp(rate, band.freq, band.q);
            else if (mode == SVF::HP6) eqL[i].hp6(rate, band.freq);
            else if (mode == SVF::HS) eqL[i].hs(rate, band.freq, band.q, band.gain);
            else if (mode == SVF::PK) eqL[i].pk(rate, band.freq, band.q, band.gain);
            else if (mode == SVF::BP) eqL[i].bp(rate, band.freq, band.q);
            else eqL[i].mode = SVF::Off;
        }

        eqR[i].copyFrom(eqL[i]);
    }

    // prepare feedback EQ
    delay->setEqualizer(getEqualizer(SVF::EQType::DecayEQ));
    delay->onSlider();

    // update distortion
    int _distPrePath = (int)params.getRawParameterValue("dist_pre_path")->load();
    if (_distPrePath != distPrePath)
    {
        delay->prepare((float)srate * (_distPrePath == 1 ? distPreOversampler->getOversamplingFactor() : 1));
        distPreOversampler->reset();
        distPrePath = _distPrePath;
    }
    distPre->onSlider();
    distPost->onSlider();
    crushPre->onSlider();
    crushPost->onSlider();
    float dpre = params.getRawParameterValue("dist_pre")->load();
    if (dpre == 0.f && dist_pre > 0.f && distPrePath == 0)
        distPreOversampler->reset();
    dist_pre = dpre;
    float dpost = params.getRawParameterValue("dist_post")->load();
    if (dpost == 0.f && dist_post > 0.f)
        distPostOversampler->reset();
    dist_post = dpost;

    // diffusor
    float diffsize = params.getRawParameterValue("diff_size")->load();
    diffPath = (int)params.getRawParameterValue("diff_path")->load();
    diffusor->setSize(diffsize);
    bool legacy = (bool)params.getRawParameterValue("diff_legacy")->load();
    if (legacy != useLegacyDiffusor) {
        useLegacyDiffusor = legacy;
        diffusorChanged = true;
    }

    // pitch shifter
    shifterMode = (int)params.getRawParameterValue("shifter_mode")->load();
    Pitcher::WindowMode pitchMode = (Pitcher::WindowMode)params.getRawParameterValue("pitch_mode")->load();
    if (pitcher->mode != pitchMode) {
        pitcher->init(pitchMode);
        delay->pitcher->init(pitchMode);
        delay->pitcherSwing->init(pitchMode);
    }
    pitcherPath = (int)params.getRawParameterValue("pitch_path")->load();
    float pitchSemis = params.getRawParameterValue("pitch_shift")->load();
    pitcherSpeed = pitcher->getSpeedFromSemis(pitchSemis);
    shifter->onSlider();

    // phaser
    bool isPhaserOn = phaser->isOn;
    phaserPath = (int)params.getRawParameterValue("phaser_path")->load();
    phaser->onSlider();
    if (!isPhaserOn && phaser->isOn)
        phaser->clear();

    // wow and flutter
    float tape = params.getRawParameterValue("tape_amt")->load();
    if (tape > 0.f && tapeAmt == 0.f) { // turning tape ON
        tapeFadeIn = true;
        tapeFadeSize = (int)std::ceil(100 * 0.001 * srate);
        tapeFadeSamps = tapeFadeSize;
    }
    else if (tape == 0.f && tapeAmt > 0.f) { // turning tape OFF
        tapeFadeIn = false;
        tapeFadeSize = (int)std::ceil(100 * 0.001 * srate);
        tapeFadeSamps = tapeFadeSize;
    }
    tapeAmt = tape;
}

void QDelayAudioProcessor::clearAll()
{
    delay->clear();
    distPre->clear();
    distPost->clear();
    diffusor->clear();
    crushPre->clear();
    crushPost->clear();
    distPostOversampler->reset();
    distPreOversampler->reset();
    phaser->clear();
}

// upstream reads these from the host's playhead at the top of processBlock
void QDelayAudioProcessor::setTempo(double tempo)
{
    if (!(tempo > 0.0)) return;
    beatsPerSecond = tempo / 60.0;
    beatsPerSample = tempo / (60.0 * srate);
    samplesPerBeat = (int)((60.0 / tempo) * srate);
    secondsPerBeat = 60.0 / tempo;
    secondsPerBar = secondsPerBeat * 4;
}

void QDelayAudioProcessor::setPlaying(bool play, double seconds)
{
    if (playing != play) // onplay() | onstop()
    {
        if (play)
            clearAll();
    }
    playing = play;
    timeInSeconds = seconds;
}

void QDelayAudioProcessor::processBlock(float* left, float* right, int numSamples)
{
    juce::ScopedNoDenormals disableDenormals;

    if (diffusorChanged) {
        diffusorChanged = false;
        diffusor = useLegacyDiffusor ? diffusorLegacy.get() : diffusorModern.get();
        float diffsize = params.getRawParameterValue("diff_size")->load();
        diffusor->clear();
        diffusor->setSize(diffsize);
    }

    if (!numSamples)
        return;

    if (params.changed.exchange(false))
    {
        onSlider();
    }

    // prepare wet buffer by copying the dry signal into it
    float* wetl = wetBuffer[0].data();
    float* wetr = wetBuffer[1].data();
    std::copy(left, left + numSamples, wetl);
    std::copy(right, right + numSamples, wetr);
    float* wet[2] = { wetl, wetr };

    // process input EQ
    if (eqPath == 0)
    {
        for (int i = 0; i < EQ_BANDS; ++i)
        {
            auto& svfL = eqL[i];
            auto& svfR = eqR[i];
            auto& band = eqBands[i];
            svfL.processBlock(wetl, numSamples, 0, numSamples, band.freq, band.q, band.gain);
            svfR.processBlock(wetr, numSamples, 1, numSamples, band.freq, band.q, band.gain);
        }
    }

    // process pre distortion and bitcrushing
    int crushPath = (int)params.getRawParameterValue("crush_path")->load();
    float crushSrate = params.getRawParameterValue("crush_srate")->load();
    float crushBits = params.getRawParameterValue("crush_bits")->load();

    if ((dist_pre > 0.f && distPrePath == 0) ||
        (crushPath == 0 && (crushSrate > 0.f || crushBits > 0.f)))
    {
        distPreOversampler->processSamplesUp(wet, numSamples);
        float* osleft = distPreOversampler->channel(0);
        float* osright = distPreOversampler->channel(1);
        int os = (int)distPreOversampler->getOversamplingFactor();

        if (crushPath == 0 && (crushSrate > 0.f || crushBits > 0.f)) {
            crushPre->processBlock(osleft, osright, numSamples * os);
        }
        std::copy(osleft, osleft + numSamples * os, distPreBuffer[0].data());
        std::copy(osright, osright + numSamples * os, distPreBuffer[1].data());

        if (dist_pre > 0.f && distPrePath == 0) {
            distPre->processBlock(osleft, osright, numSamples * os);
        }

        for (int i = 0; i < numSamples * os; ++i) {
            osleft[i] = osleft[i] * dist_pre + distPreBuffer[0][(size_t)i] * (1.f - dist_pre);
            osright[i] = osright[i] * dist_pre + distPreBuffer[1][(size_t)i] * (1.f - dist_pre);
        }
        distPreOversampler->processSamplesDown(wet, numSamples);
    }

    // process pre diffusion
    float diffamt = params.getRawParameterValue("diff_amt")->load();
    if (diffamt > 0.f && diffPath == 0) {
        float diffdry = Utils::cosHalfPi()(diffamt);
        float diffwet = Utils::sinHalfPi()(diffamt);
        diffusor->processBlock(wetl, wetr, numSamples, diffdry, diffwet);
    }

    // process delay
    if (distPrePath == 0)
    {
        delay->processBlock(wetl, wetr, numSamples);
    }
    else
    {
        // Pre distortion is on the feedback path
        // upsample the whole delay block
        distPreOversampler->processSamplesUp(wet, numSamples);
        float* osleft = distPreOversampler->channel(0);
        float* osright = distPreOversampler->channel(1);
        int os = (int)distPreOversampler->getOversamplingFactor();
        delay->processBlock(osleft, osright, numSamples * os);
        distPreOversampler->processSamplesDown(wet, numSamples);
    }

    // process pitch shift
    if (shifterMode == 0 && std::fabs(pitcherSpeed) > 1e-6 && pitcherPath == 1)
    {
        float pitchMix = params.getRawParameterValue("pitch_mix")->load();
        auto pitchDry = Utils::cosHalfPi()(pitchMix);
        auto pitchWet = Utils::sinHalfPi()(pitchMix);
        for (int i = 0; i < numSamples; ++i)
        {
            pitcher->setSpeed(pitcherSpeed);
            pitcher->update(wetl[i], wetr[i]);
            wetl[i] = wetl[i] * pitchDry + pitcher->outL * pitchWet;
            wetr[i] = wetr[i] * pitchDry + pitcher->outR * pitchWet;
        }
    }

    // process frequency shift
    if (shifterMode == 1 && shifter->isOn && pitcherPath == 1)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            shifter->process(wetl[i], wetr[i]);
        }
    }

    // process wow and flutter
    if (tapeAmt > 0.f || tapeFadeSamps > 0)
    {
        bool fading = tapeFadeSamps > 0;
        flutter->prepareBlock();
        wow->prepareBlock();
        float flutOffset = flutter->dcOffset;
        float sr = (float)srate * 0.001f;
        for (int i = 0; i < numSamples; ++i)
        {
            auto flutLFO = flutter->getLFO();
            auto wowLFO = wow->getLFO();
            float wowOffset = wow->curDepth;
            auto offset = (flutLFO + flutOffset + wowLFO + wowOffset) * sr;
            wowflut_l->write(wetl[i]);
            wowflut_r->write(wetr[i]);
            float wetSampleL = wowflut_l->read3(offset);
            float wetSampleR = wowflut_r->read3(offset);

            // compute crossfade
            float fade = !fading ? 1.f : tapeFadeIn
                ? 1.0f - (float)tapeFadeSamps / (float)tapeFadeSize
                : (float)tapeFadeSamps / (float)tapeFadeSize;
            if (tapeFadeSamps > 0) tapeFadeSamps--;

            wetl[i] = fade * wetSampleL + (1.0f - fade) * wetl[i];
            wetr[i] = fade * wetSampleR + (1.0f - fade) * wetr[i];
        }

        flutter->boundPhase();
        wow->boundPhase();
    }

    // process post distortion and post bitcrush
    if ((dist_post > 0.f) ||
        (crushPath == 1 && (crushSrate > 0.f || crushBits > 0.f)))
    {
        distPostOversampler->processSamplesUp(wet, numSamples);
        float* osleft = distPostOversampler->channel(0);
        float* osright = distPostOversampler->channel(1);
        int os = (int)distPostOversampler->getOversamplingFactor();

        if (crushPath == 1 && (crushSrate > 0.f || crushBits > 0.f)) {
            crushPost->processBlock(osleft, osright, numSamples * os);
        }
        std::copy(osleft, osleft + numSamples * os, distPostBuffer[0].data());
        std::copy(osright, osright + numSamples * os, distPostBuffer[1].data());

        if (dist_post > 0.f) {
            distPost->processBlock(osleft, osright, numSamples * os);
        }

        for (int i = 0; i < numSamples * os; ++i) {
            osleft[i] = osleft[i] * dist_post + distPostBuffer[0][(size_t)i] * (1.f - dist_post);
            osright[i] = osright[i] * dist_post + distPostBuffer[1][(size_t)i] * (1.f - dist_post);
        }
        distPostOversampler->processSamplesDown(wet, numSamples);
    }

    // process phaser
    if (phaserPath == 1 && phaser->isOn)
    {
        if (playing)
            phaser->syncToSongTime((float)timeInSeconds);

        for (int i = 0; i < numSamples; ++i)
        {
            phaser->process(wetl[i], wetr[i]);
        }
    }

    // process post diffusion
    if (diffamt > 0.f && diffPath == 1)
    {
        float diffdry = Utils::cosHalfPi()(diffamt);
        float diffwet = Utils::sinHalfPi()(diffamt);
        diffusor->processBlock(wetl, wetr, numSamples, diffdry, diffwet);
    }

    // process output EQ
    if (eqPath == 1)
    {
        for (int i = 0; i < EQ_BANDS; ++i)
        {
            auto& svfL = eqL[i];
            auto& svfR = eqR[i];
            auto& band = eqBands[i];
            svfL.processBlock(wetl, numSamples, 0, numSamples, band.freq, band.q, band.gain);
            svfR.processBlock(wetr, numSamples, 1, numSamples, band.freq, band.q, band.gain);
        }
    }

    // apply ducking
    float duck = params.getRawParameterValue("duck_amt")->load();
    if (duck > 0.f)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float env = follower.process(left[i], right[i]);

            float gain = 1.f - env * duck;
            wetl[i] *= gain;
            wetr[i] *= gain;
        }
    }
    else
    {
        follower.clear();
    }
    duckEnv.store(follower.envelope * duck);

    // mix and pan

    auto mix = params.getRawParameterValue("mix")->load();
    auto drymix = mix <= 0.5f ? 1.f : 1.f - (mix - 0.5f) * 2.f;
    auto wetmix = mix <= 0.5f ? mix * 2.f : 1.f;

    bool panDrySum = (bool)params.getRawParameterValue("pan_dry_sum")->load();
    auto panDry = params.getRawParameterValue("pan_dry")->load();
    if (panDrySum) // crossfeed into one of the channels when panning
    {
        float theta = panDry > 0.5f
            ? (panDry - 0.5f) * 2.0f
            : (0.5f - panDry) * 2.0f;

        float keepGain = Utils::cosHalfPi()(theta);
        float sendGain = Utils::sinHalfPi()(theta);
        if (panDry > 0.5f) // pan to right
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float l = left[i];
                float r = right[i];
                left[i] = l * keepGain * drymix;
                right[i] = r * drymix + l * sendGain * drymix;
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float l = left[i];
                float r = right[i];
                right[i] = r * keepGain * drymix;
                left[i] = l * drymix + r * sendGain * drymix;
            }
        }
    }
    else // collapse one of the channels when panning (default)
    {
        auto panDryL = Utils::cosHalfPi()(panDry) * SQRT2; // +0db at center, +3dB when hard panned
        auto panDryR = Utils::sinHalfPi()(panDry) * SQRT2;
        for (int i = 0; i < numSamples; ++i)
        {
            left[i] *= drymix * panDryL;
            right[i] *= drymix * panDryR;
        }
    }

    bool panWetSum = params.getRawParameterValue("pan_wet_sum")->load();
    float panWet = params.getRawParameterValue("pan_wet")->load();
    if (panWetSum) // crossfeed into one of the channels when panning
    {
        // pan to right
        float theta = panWet > 0.5f
            ? (panWet - 0.5f) * 2.0f
            : (0.5f - panWet) * 2.0f; // 0..1

        float keepGain = Utils::cosHalfPi()(theta);
        float sendGain = Utils::sinHalfPi()(theta);

        if (panWet > 0.5f) // pan to right
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float l = wetl[i];
                float r = wetr[i];
                wetl[i] = l * keepGain * wetmix;
                wetr[i] = r * wetmix + l * sendGain * wetmix;
            }
        }
        else
        {
            for (int i = 0; i < numSamples; ++i)
            {
                float l = wetl[i];
                float r = wetr[i];
                wetr[i] = r * keepGain * wetmix;
                wetl[i] = l * wetmix + r * sendGain * wetmix;
            }
        }
    }
    else // collapse one of the channels when panning (default)
    {
        auto panWetL = Utils::cosHalfPi()(panWet) * SQRT2;
        auto panWetR = Utils::sinHalfPi()(panWet) * SQRT2;
        for (int i = 0; i < numSamples; ++i)
        {
            wetl[i] *= wetmix * panWetL;
            wetr[i] *= wetmix * panWetR;
        }
    }

    // apply stereo width
    auto stereo = params.getRawParameterValue("stereo")->load();
    if (stereo != 1.f) {
        for (int sample = 0; sample < numSamples; ++sample) {
            auto lsamp = wetl[sample];
            auto rsamp = wetr[sample];
            auto mid = (lsamp + rsamp) * 0.5f;
            auto side = (lsamp - rsamp) * 0.5f;
            wetl[sample] = (mid + side * stereo);
            wetr[sample] = (mid - side * stereo);
        }
    }

    // sum wet and dry signals, apply final out_gain
    float outGain = params.getRawParameterValue("out_gain")->load();
    outGain = std::exp(outGain * DB2LOG);
    float magL = 0.f, magR = 0.f;
    for (int i = 0; i < numSamples; ++i)
    {
        left[i] = (left[i] + wetl[i]) * outGain;
        right[i] = (right[i] + wetr[i]) * outGain;
        magL = std::max(magL, std::fabs(left[i]));
        magR = std::max(magR, std::fabs(right[i]));
    }
    rmsLeft.store(magL);
    rmsRight.store(magR);

    // the host's clock runs on between the transport messages
    if (playing)
        timeInSeconds += numSamples / srate;
}

// ---------------------------------------------------------------------------------------------------------------

namespace nornios {

class QDelayEngine : public Engine {
public:
    const char *name() const override { return "QDelay"; }
    const std::vector<CommandDef> &commands() const override {
        static std::vector<CommandDef> c = {{"set", "if"}, {"tempo", "f"}, {"transport", "if"}, {"clear", "i"}};
        return c;
    }
    const std::vector<PollDef> &polls() const override {
        static std::vector<PollDef> p = {{"qd_in", true}, {"qd_out_l", true}, {"qd_out_r", true}, {"qd_duck", true}, {"qd_params", false}};
        return p;
    }

    void prepare(double sampleRate, int maxFrames) override {
        processor_.setTempo(120.0); // until the script sends the session's: the delay starts at a time on the grid
        processor_.prepareToPlay(sampleRate, maxFrames);
    }

    void command(const EngineCommand &c) override {
        switch (c.id) {
        case 1:
            if (auto *p = processor_.params.at(static_cast<size_t>(static_cast<int>(c.args[0]) - 1))) {
                if (std::isfinite(c.args[1])) p->setRaw(c.args[1]);
            }
            break;
        case 2: if (std::isfinite(c.args[0])) processor_.setTempo(c.args[0]); break;
        case 3: processor_.setPlaying(c.args[0] > 0.5f, std::isfinite(c.args[1]) ? c.args[1] : 0.0); break;
        case 4: processor_.clearAll(); break;
        default: break;
        }
    }

    void render(const float *inL, const float *inR, float *outL, float *outR, int frames) override {
        float peak = 0.f;
        for (int i = 0; i < frames; ++i) {
            outL[i] = inL[i];
            outR[i] = inR[i];
            peak = std::max(peak, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
        }
        processor_.processBlock(outL, outR, frames);
        // meters that hold a peak and let it fall, so a poll between blocks still sees a transient
        inMeter_.store(std::max(peak, inMeter_.load() * kMeterFall));
        outMeterL_.store(std::max(processor_.rmsLeft.load(), outMeterL_.load() * kMeterFall));
        outMeterR_.store(std::max(processor_.rmsRight.load(), outMeterR_.load() * kMeterFall));
    }

    bool pollValue(int index, float &out) override {
        switch (index) {
        case 0: out = inMeter_.load(); return true;
        case 1: out = outMeterL_.load(); return true;
        case 2: out = outMeterR_.load(); return true;
        case 3: out = processor_.duckEnv.load(); return true;
        case 4: out = static_cast<float>(processor_.params.size()); return true;
        default: return false;
        }
    }

private:
    static constexpr float kMeterFall = 0.93f;
    QDelayAudioProcessor processor_;
    std::atomic<float> inMeter_{0.f}, outMeterL_{0.f}, outMeterR_{0.f};
};

std::unique_ptr<Engine> makeQDelayEngine() { return std::make_unique<QDelayEngine>(); }

} // namespace nornios

#include "../PortsMetadata.hpp"
namespace nornios {
static QDelayAudioProcessor &layout(){static QDelayAudioProcessor p;return p;}
int qdelayParameterCount(){return int(layout().params.size());}
const char *qdelayParameterName(int i){static auto labels=[] {std::array<std::string,QDelayValues>a{};auto&p=layout();for(size_t n=0;n<p.params.size()&&n<a.size();n++){a[n]=p.params.at(n)->id;for(auto &c:a[n])c=c=='_'?' ':char(std::toupper(c));}return a;}();return labels.at(i).c_str();}
int qdelayParameterStep(int i){auto &r=layout().params.at(i)->range;return r.interval==1?std::max(1,int(std::lround(100.f/(r.end-r.start)))):1;}
float qdelayParameterValue(int i,int percentage){auto &r=layout().params.at(i)->range;return r.snapToLegalValue(r.convertFrom0to1(percentage*.01f));}
std::array<unsigned char,QDelayValues>defaultQDelayValues(){std::array<unsigned char,QDelayValues>a{};auto&p=layout();for(size_t i=0;i<p.params.size()&&i<a.size();i++)a[i]=std::clamp(int(std::lround(p.params.at(i)->range.convertTo0to1(p.params.at(i)->defaultValue)*100)),0,100);return a;}
}
