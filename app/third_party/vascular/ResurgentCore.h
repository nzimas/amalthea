/*
 * resurgent -- a voice of the kind Surge XT plays, built independently.
 *
 * Written from the Surge XT user manual, from Surge patch files read as data,
 * and from the DSP literature. No Surge source was read: Surge is GPL-3.0 and
 * this instrument is not. See Docs/resurgent/PROVENANCE.md for the rules,
 * SPEC-from-manual.md for what the manual does and does not say, and
 * FXP-FORMAT.md and ENUMS.md for what the patch files showed. Where nothing
 * says, the choice is this instrument's own and is marked so.
 *
 * The voice takes its patch in Surge's own stored units -- semitones from
 * A440 for frequencies, log2 seconds for times, log2 hertz for LFO rates,
 * decibels for gains -- so a patch read from a file needs no translating, and
 * modulation depths, which a patch stores in those units, add to them
 * directly.
 *
 * Two scenes, each three oscillators, a mixer, two filters with the
 * waveshaper in one of eight configurations, two envelopes, twelve LFOs and
 * a modulation matrix; then the effects. One voice, mono out: a Vascular node
 * is one voice, and the stereo configurations are summed.
 *
 * The parts are in ResurgentOsc.h, ResurgentFilter.h, ResurgentMod.h and
 * ResurgentFx.h. No Csound in any of them, so the voice can be rendered and
 * measured on the Mac (Scripts/resurgent-harness.sh).
 */
#ifndef VASCULAR_RESURGENT_CORE_H
#define VASCULAR_RESURGENT_CORE_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "ResurgentFilter.h"
#include "ResurgentFx.h"
#include "ResurgentMod.h"
#include "ResurgentOsc.h"

namespace vascular {
namespace resurgent {

// -- the patch -----------------------------------------------------------------

enum Param {
#define RP(id, name, def) P_##id,
#include "ResurgentParams.def"
#undef RP
  kParamCount
};

enum Global {
#define RG(id, name, def) G_##id,
#include "ResurgentGlobals.def"
#undef RG
  kGlobalCount
};

inline const float *paramDefaults() {
  static const float values[kParamCount] = {
#define RP(id, name, def) (float)(def),
#include "ResurgentParams.def"
#undef RP
  };
  return values;
}

inline const float *globalDefaults() {
  static const float values[kGlobalCount] = {
#define RG(id, name, def) (float)(def),
#include "ResurgentGlobals.def"
#undef RG
  };
  return values;
}

/// The table the host writes. Version 3:
///   [0] version, [1] scene parameter count, [2] global parameter count;
///   scene A's parameters, scene B's, the globals;
///   12 LFOs x (16 steps, loop start, loop end) for scene A, then for B;
///   8 macros x (value, bipolar);
///   scene A's routing count and (source, target, depth) x 128, then B's;
///   scene A's MSEGs, 12 x (count, edit mode, loop mode, loop start, loop
///   end, endpoint mode, then 32 x (duration, v0, v1, cp position, cp
///   value, type)), then B's;
///   scene A's additive harmonics for Alias, 3 oscillators x 16, then B's.
static const int kTableVersion = 4;
static const int kSteps = 16;
static const int kLfoCount = 12;
static const int kMacroCount = 8;
static const int kMaxRoutes = 128;
static const int kHeader = 3;
static const int kStepBlock = kLfoCount * (kSteps + 2);
static const int kRouteBlock = 1 + kMaxRoutes * 3;
static const int kMsegBlock = kLfoCount * (6 + Mseg::kMaxSegments * 6);
static const int kAdditiveBlock = 3 * 16;
static const int kTableSize = kHeader + 2 * kParamCount + kGlobalCount + 2 * kStepBlock
                              + kMacroCount * 2 + 2 * kRouteBlock + 2 * kMsegBlock + 2 * kAdditiveBlock;

/// Modulation sources, by the numbers a patch stores them under
/// (FXP-FORMAT.md 4.4 and ENUMS.md); an unknown one is read as nothing.
enum Source {
  S_none = 0, S_velocity = 1, S_keytrack = 2, S_polyAT = 3, S_channelAT = 4,
  S_pitchbend = 5, S_modwheel = 6, S_macro1 = 7, S_aeg = 15, S_feg = 16,
  S_lfo1 = 17, S_slfo1 = 23, kSourceCount = 48
};

struct Route {
  int source;
  int target;
  float depth;
};

struct Scene {
  float p[kParamCount];
  float steps[kLfoCount][kSteps];
  int loopStart[kLfoCount];
  int loopEnd[kLfoCount];
  Route routes[kMaxRoutes];
  int routeCount = 0;
  Mseg mseg[kLfoCount];
  float additive[3][16];

  void reset() {
    for (int o = 0; o < 3; ++o)
      for (int h = 0; h < 16; ++h) additive[o][h] = 1.0f / (h + 1);
    std::memcpy(p, paramDefaults(), sizeof(p));
    std::memset(steps, 0, sizeof(steps));
    for (int i = 0; i < kLfoCount; ++i) { loopStart[i] = 0; loopEnd[i] = kSteps - 1; mseg[i] = Mseg(); }
    routeCount = 0;
  }
};

struct Patch {
  Scene scene[2];
  float g[kGlobalCount];
  float macro[kMacroCount];
  bool macroBipolar[kMacroCount];

  Patch() { reset(); }

  void reset() {
    scene[0].reset();
    scene[1].reset();
    std::memcpy(g, globalDefaults(), sizeof(g));
    for (int i = 0; i < kMacroCount; ++i) { macro[i] = 0; macroBipolar[i] = false; }
  }

  /// From the table the host writes. Anything malformed leaves the defaults.
  void read(const double *t, int size) {
    reset();
    if (!t || size < kTableSize || (int)t[0] != kTableVersion) return;
    const int count = clampi((int)t[1], 0, kParamCount);
    const int globals = clampi((int)t[2], 0, kGlobalCount);
    int at = kHeader;
    for (int s = 0; s < 2; ++s) {
      for (int i = 0; i < count; ++i) scene[s].p[i] = (float)t[at + i];
      at += (int)t[1];
    }
    for (int i = 0; i < globals; ++i) g[i] = (float)t[at + i];
    at += (int)t[2];
    for (int s = 0; s < 2; ++s) {
      for (int l = 0; l < kLfoCount; ++l) {
        const double *r = t + at + l * (kSteps + 2);
        for (int k = 0; k < kSteps; ++k) scene[s].steps[l][k] = (float)r[k];
        scene[s].loopStart[l] = clampi((int)r[kSteps], 0, kSteps - 1);
        scene[s].loopEnd[l] = clampi((int)r[kSteps + 1], scene[s].loopStart[l], kSteps - 1);
      }
      at += kStepBlock;
    }
    for (int m = 0; m < kMacroCount; ++m) {
      macro[m] = (float)t[at + m * 2];
      macroBipolar[m] = t[at + m * 2 + 1] > 0.5;
    }
    at += kMacroCount * 2;
    for (int s = 0; s < 2; ++s) {
      const int n = clampi((int)t[at], 0, kMaxRoutes);
      scene[s].routeCount = 0;
      for (int r = 0; r < n; ++r) {
        const double *e = t + at + 1 + r * 3;
        const int src = (int)e[0], tgt = (int)e[1];
        if (src <= 0 || src >= kSourceCount || tgt < 0 || tgt >= kParamCount) continue;
        scene[s].routes[scene[s].routeCount++] = {src, tgt, (float)e[2]};
      }
      at += kRouteBlock;
    }
    for (int s = 0; s < 2; ++s) {
      for (int l = 0; l < kLfoCount; ++l) {
        const double *h = t + at + l * (6 + Mseg::kMaxSegments * 6);
        Mseg &m = scene[s].mseg[l];
        m.count = clampi((int)h[0], 0, Mseg::kMaxSegments);
        m.editMode = (int)h[1];
        m.loopMode = (int)h[2];
        m.loopStart = (int)h[3];
        m.loopEnd = (int)h[4];
        for (int k = 0; k < m.count; ++k)
          for (int f = 0; f < 6; ++f) m.seg[k][f] = (float)h[6 + k * 6 + f];
      }
      at += kMsegBlock;
    }
    for (int s = 0; s < 2; ++s) {
      for (int o = 0; o < 3; ++o)
        for (int h = 0; h < 16; ++h) scene[s].additive[o][h] = (float)t[at + o * 16 + h];
      at += kAdditiveBlock;
    }
  }
};

inline LfoFields lfoFields() {
  const int b = P_lfo0_shape;
  return {P_lfo0_shape - b, P_lfo0_rate - b, P_lfo0_rate__deactivated - b, P_lfo0_rate__temposync - b,
          P_lfo0_phase - b, P_lfo0_magnitude - b, P_lfo0_deform - b, P_lfo0_deform__deform_type - b,
          P_lfo0_trigmode - b, P_lfo0_unipolar - b, P_lfo0_delay - b, P_lfo0_delay__deactivated - b,
          P_lfo0_attack - b, P_lfo0_hold - b, P_lfo0_decay - b, P_lfo0_sustain - b, P_lfo0_release - b};
}
static const int kLfoStride = P_lfo1_shape - P_lfo0_shape;
static const int kOscStride = P_osc2_type - P_osc1_type;

// -- one scene -------------------------------------------------------------------

class SceneVoice {
 public:
  void prepare(float sr, uint32_t seed) {
    sr_ = sr;
    seed_ = seed;
    for (auto &f : filter_) f.reset();
    for (int i = 0; i < 3; ++i) { osc_[i].prepare(sr); osc_[i].reset(seed + 101u * i, false); }
    rng_.seed(seed ^ 0xA5A5A5A5u);
  }

  void setScene(const Scene *s) {
    scene_ = s;
    for (int i = 0; i < 3; ++i)
      if ((int)s->p[P_osc1_type + i * kOscStride] == O_twist) osc_[i].needTwist();
  }
  void setTable(int slot, const Wavetable *t) { if (slot >= 0 && slot < 3) osc_[slot].table = t; }

  void noteOn(float velocity, uint32_t noteIndex) {
    velocity_ = clampf(velocity, 0, 1);
    aeg_.gate(true); feg_.gate(true);
    const float *p = scene_->p;
    for (int i = 0; i < 3; ++i) {
      const int b = i * kOscStride;
      if (p[P_osc1_retrigger + b] > 0.5f) osc_[i].reset(seed_ + 101u * i, true);
      osc_[i].strike((int)p[P_osc1_type + b]);
    }
    const LfoFields F = lfoFields();
    for (int l = 0; l < kLfoCount; ++l) {
      const float *q = p + P_lfo0_shape + l * kLfoStride;
      lfo_[l].trigger((int)q[F.trig], q[F.phase], seed_ + 977u * l + noteIndex);
    }
  }

  void noteOff() {
    aeg_.gate(false); feg_.gate(false);
    for (auto &l : lfo_) l.release();
  }

  bool silent() const { return aeg_.stage == Adsr::Idle; }

  /// Once per block, then `sample` for each sample of it.
  void control(float hz, int block, const float *macros, const bool *bipolar, float tempo) {
    const float *p = scene_->p;
    const float dt = block / sr_;
    noteMidi_ = 69.0f + 12.0f * std::log2(clampf(hz, 1.0f, 20000.0f) / 440.0f);

    float src[kSourceCount] = {};
    src[S_velocity] = velocity_;
    src[S_keytrack] = (noteMidi_ - p[P_ktrkroot]) / 12.0f;
    for (int i = 0; i < kMacroCount; ++i) {
      const float v = clampf(macros[i], 0, 1);
      src[S_macro1 + i] = bipolar[i] ? 2 * v - 1 : v;
    }
    src[S_aeg] = aeg_.tick(dt, p[P_env1_attack], p[P_env1_decay], p[P_env1_sustain], p[P_env1_release],
                           (int)p[P_env1_attack_shape], (int)p[P_env1_decay_shape],
                           (int)p[P_env1_release_shape], p[P_env1_mode] > 0.5f, false);
    src[S_feg] = feg_.tick(dt, p[P_env2_attack], p[P_env2_decay], p[P_env2_sustain], p[P_env2_release],
                           (int)p[P_env2_attack_shape], (int)p[P_env2_decay_shape],
                           (int)p[P_env2_release_shape], p[P_env2_mode] > 0.5f, false);
    // The LFOs read last block's modulated values for their own controls:
    // one modulating another's rate is a block behind, which is unheard.
    const LfoFields F = lfoFields();
    for (int l = 0; l < kLfoCount; ++l) {
      const int base = P_lfo0_shape + l * kLfoStride;
      const float v = lfo_[l].tick(dt, p + base, F, mod_[base + F.rate], clampf(mod_[base + F.magnitude], -1, 1),
                                   clampf(mod_[base + F.deform], -1, 1), scene_->steps[l],
                                   scene_->loopStart[l], scene_->loopEnd[l], tempo, &scene_->mseg[l]);
      src[S_lfo1 + l] = v;
    }
    std::memcpy(mod_, p, sizeof(mod_));
    for (int r = 0; r < scene_->routeCount; ++r) {
      const Route &e = scene_->routes[r];
      mod_[e.target] += e.depth * src[e.source];
    }
    aegLevel_ = src[S_aeg];
    fegLevel_ = src[S_feg];

    const float scenePitch = m(P_octave) * 12.0f + m(P_pitch);
    for (int i = 0; i < 3; ++i) {
      const int b = i * kOscStride;
      const bool keytracked = p[P_osc1_keytrack + b] > 0.5f;
      const float note = (keytracked ? noteMidi_ : p[P_ktrkroot]) + scenePitch
                         + m(P_osc1_octave + b) * 12.0f + m(P_osc1_pitch + b);
      oscHz_[i] = 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
      oscType_[i] = (int)p[P_osc1_type + b];
      for (int k = 0; k < 7; ++k) oscP_[i][k] = m(P_osc1_param0 + b + k);
      oscVoices_[i] = (int)clampf(std::round(p[P_osc1_param6 + b]), 1, 16);
      const float ext = p[P_osc1_param5__extend_range + b] > 0.5f ? 12.0f : 1.0f;
      oscDetune_[i] = clampf(m(P_osc1_param5 + b), 0, 1) * 100.0f * ext;
      oscDeform_[i] = (int)(oscType_[i] == O_sine ? p[P_osc1_param1__deform_type + b]
                                                  : p[P_osc1_param2__deform_type + b]);
      // The built-in cuts of the algorithms that have them (Sine, S&H Noise,
      // Window): parameters 3 and 4, each switched on in the patch.
      const bool cuts = oscType_[i] == O_sine || oscType_[i] == O_window || oscType_[i] == O_shnoise;
      const int lowAt = oscType_[i] == O_shnoise ? 2 : 3;
      oscLow_[i] = cuts && p[P_osc1_param3__deactivated + i * 2] < 0.5f ? noteHz(m(P_osc1_param0 + b + lowAt)) : 0.0f;
      oscHigh_[i] = cuts && p[P_osc1_param4__deactivated + i * 2] < 0.5f ? noteHz(m(P_osc1_param0 + b + lowAt + 1)) : 0.0f;
      osc_[i].additive = scene_->additive[i];
      if (oscType_[i] == O_twist) {
        osc_[i].needTwist();
        osc_[i].twistLpg = p[P_osc1_param5__deactivated + i] < 0.5f;
      }
    }
    bool anySolo = false;
    for (int c = 0; c < 6; ++c) anySolo = anySolo || p[P_solo_o1 + c] > 0.5f;
    for (int c = 0; c < 6; ++c) {
      const bool on = anySolo ? p[P_solo_o1 + c] > 0.5f : p[P_mute_o1 + c] < 0.5f;
      // The mixer's faders, like every level a patch stores, are the cube
      // root of the amplitude (FX-SPEC.md 3: the "-inf .. 0 dB" taper).
      const float level = clampf(m(P_level_o1 + c), 0, 1);
      chanGain_[c] = on ? level * level * level : 0.0f;
      chanRoute_[c] = (int)p[P_route_o1 + c];
    }
    ringMode_[0] = (int)p[P_level_ring12__deform_type];
    ringMode_[1] = (int)p[P_level_ring23__deform_type];
    noiseColour_ = clampf(m(P_noisecol), -1, 1);
    preGain_ = dbToGain(clampf(m(P_level_pfg), -48, 48));
    fmMode_ = (int)p[P_fm_switch];
    // FM depth runs -48..+24, read as decibels of an index (ENUMS.md 4.7).
    fmIndex_ = 25.13f * dbToGain(clampf(m(P_fm_depth), -96, 24)) / kTwoPi;

    const float key = noteMidi_ + scenePitch - p[P_ktrkroot];
    float c1 = m(P_filter1_cutoff) + m(P_filter1_keytrack) * key + m(P_filter1_envmod) * fegLevel_;
    float c2 = m(P_filter2_cutoff) + m(P_filter2_keytrack) * key + m(P_filter2_envmod) * fegLevel_;
    if (p[P_f2_cf_is_offset] > 0.5f) c2 += c1;
    cut_[0] = noteHz(clampf(c1, -72, 72));
    cut_[1] = noteHz(clampf(c2, -72, 72));
    res_[0] = clampf(m(P_filter1_resonance), 0, 1);
    res_[1] = clampf(m(P_filter2_resonance) + (p[P_f2_link_resonance] > 0.5f ? res_[0] : 0.0f), 0, 1);
    kind_[0] = p[P_filter1_type__deactivated] > 0.5f ? F_off : filterKind((int)p[P_filter1_type]);
    kind_[1] = p[P_filter2_type__deactivated] > 0.5f ? F_off : filterKind((int)p[P_filter2_type]);
    sub_[0] = (int)p[P_filter1_subtype];
    sub_[1] = (int)p[P_filter2_subtype];
    config_ = (int)p[P_fb_config];
    balance_ = clampf(m(P_f_balance), -1, 1);
    feedback_ = clampf(m(P_feedback), -1, 1);
    shaper_ = p[P_ws_type__deactivated] > 0.5f ? W_off : shaperKind((int)p[P_ws_type]);
    wsDrive_ = dbToGain(clampf(m(P_ws_drive), -24, 48));
    const float velDb = clampf(m(P_vca_velsense), -60, 0) * (1.0f - velocity_);
    const float ampTarget = aegLevel_ * dbToGain(clampf(m(P_vca_level), -48, 48) + velDb);
    ampStep_ = (ampTarget - amp_) / block;
    const float vol = clampf(m(P_volume), 0, 1.5f);
    volume_ = vol * vol * vol;
    hpOn_ = p[P_lowcut__deactivated] < 0.5f;
    hpG_ = std::tan(3.14159265f * clampf(noteHz(m(P_lowcut)), 5.0f, sr_ * 0.45f) / sr_);
    hpStages_ = 1 + clampi((int)p[P_lowcut__deform_type], 0, 3);
    for (int s = 0; s < 4; ++s) send_[s] = clampf(m(P_send_fx_1 + s), 0, 1);
  }

  float sample() {
    const float pm = fmIndex_;
    const float o3 = osc_[2].tick(oscType_[2], oscHz_[2], 0, oscP_[2], oscVoices_[2], oscDetune_[2],
                                  oscDeform_[2], sr_, oscLow_[2], oscHigh_[2]);
    const float pm2 = fmMode_ == 2 ? pm * o3 : 0.0f;
    const float o2 = osc_[1].tick(oscType_[1], oscHz_[1], pm2, oscP_[1], oscVoices_[1], oscDetune_[1],
                                  oscDeform_[1], sr_, oscLow_[1], oscHigh_[1]);
    float pm1 = 0.0f;
    if (fmMode_ == 1 || fmMode_ == 2) pm1 = pm * o2;
    else if (fmMode_ == 3) pm1 = pm * (o2 + o3);
    const float o1 = osc_[0].tick(oscType_[0], oscHz_[0], pm1, oscP_[0], oscVoices_[0], oscDetune_[0],
                                  oscDeform_[0], sr_, oscLow_[0], oscHigh_[0]);
    const float chan[6] = {o1, o2, o3, ring(o1, o2, ringMode_[0]), ring(o2, o3, ringMode_[1]), noise()};
    // A channel set to both reaches both filters in the parallel
    // configurations; in the serial ones it reaches filter 1 and goes on to
    // filter 2 through it, and only what is routed to filter 2 alone joins
    // it there.
    float in1 = 0, in2 = 0, f2only = 0;
    for (int c = 0; c < 6; ++c) {
      const float v = chan[c] * chanGain_[c];
      if (chanRoute_[c] != 2) in1 += v;
      if (chanRoute_[c] != 0) in2 += v;
      if (chanRoute_[c] == 2) f2only += v;
    }
    in1 *= preGain_; in2 *= preGain_; f2only *= preGain_;
    amp_ += ampStep_;
    const float fb = std::tanh(lastOut_ * feedback_);
    float y;
    switch (config_) {
      case 0: {  // Serial 1: F1, shaper, F2, amp; no feedback
        const float a = ws(filter_[0].tick(in1, kind_[0], sub_[0], cut_[0], res_[0], sr_));
        const float b = filter_[1].tick(a + f2only, kind_[1], sub_[1], cut_[1], res_[1], sr_);
        y = serialBlend(a, b) * amp_;
        break;
      }
      case 2: {  // Serial 3: filter 2 heard only through the loop
        const float a = ws(filter_[0].tick(in1 + loop_, kind_[0], sub_[0], cut_[0], res_[0], sr_));
        y = a * amp_;
        loop_ = std::tanh(filter_[1].tick(y + f2only, kind_[1], sub_[1], cut_[1], res_[1], sr_) * feedback_);
        break;
      }
      case 3: case 4: case 5: case 6: {  // Dual 1, Dual 2, Stereo, Ring
        const float a = filter_[0].tick(in1 + fb, kind_[0], sub_[0], cut_[0], res_[0], sr_);
        const float b = filter_[1].tick(in2 + fb, kind_[1], sub_[1], cut_[1], res_[1], sr_);
        const float ga = clampf(1.0f - balance_, 0, 1), gb = clampf(1.0f + balance_, 0, 1);
        float mixed;
        if (config_ == 6) mixed = ws(a * b * 4.0f);
        else if (config_ == 4) mixed = ws(a) * ga + b * gb;
        else mixed = ws(a * ga + b * gb);
        y = mixed * amp_;
        break;
      }
      default: {  // Serial 2 and Wide: Serial 1 with the amp fed back to F1
        const float a = ws(filter_[0].tick(in1 + fb, kind_[0], sub_[0], cut_[0], res_[0], sr_));
        const float b = filter_[1].tick(a + f2only, kind_[1], sub_[1], cut_[1], res_[1], sr_);
        y = serialBlend(a, b) * amp_;
        break;
      }
    }
    if (!std::isfinite(y)) { y = 0; for (auto &f : filter_) f.reset(); loop_ = 0; }
    lastOut_ = y;
    if (hpOn_) {
      const float G = hpG_ / (1.0f + hpG_);
      for (int s = 0; s < hpStages_ * 2 && s < 8; ++s) {
        const float v = (y - hp_[s]) * G;
        const float lp = v + hp_[s];
        hp_[s] = lp + v;
        y -= lp;
      }
    }
    return y * volume_;
  }

  const float *sends() const { return send_; }

 private:
  float m(int param) const { return mod_[param]; }

  /// Ring modulation and its alternatives (ENUMS.md 5): 0 the product, 1 a
  /// continuous XOR (the sign-agreement of the two, scaled by both), 2-10
  /// scale-invariant linear modulations, read as cross-fades of product and
  /// sum.
  static float ring(float a, float b, int mode) {
    if (mode == 0) return a * b;
    if (mode == 1) return (std::fabs(a) - std::fabs(b)) * ((a > 0) == (b > 0) ? 1.0f : -1.0f);
    const float k = clampf((mode - 1) / 9.0f, 0, 1);
    return a * b * (1.0f - k) + 0.5f * (a + b) * k * std::fabs(b);
  }

  float noise() {
    const float w = rng_.bipolar();
    const float c = noiseColour_;
    const float coeff = 0.02f + 0.98f * (1.0f - std::fabs(c));
    noiseLp_ += coeff * (w - noiseLp_);
    return c < 0 ? noiseLp_ * (1.0f + 2.0f * -c) : (c > 0 ? (w - noiseLp_) * (1.0f + c) : w);
  }

  float ws(float x) { return shaper_ == W_off ? x : shape(shaper_, x * wsDrive_, rng_); }

  float serialBlend(float a, float b) const {
    if (kind_[1] == F_off) return a;
    return balance_ < 0 ? b + (a - b) * (-balance_) : b;
  }

  const Scene *scene_ = nullptr;
  Oscillator osc_[3];
  Filter filter_[2];
  Adsr aeg_, feg_;
  Lfo lfo_[kLfoCount];
  Rng rng_;
  float sr_ = 48000;
  uint32_t seed_ = 1;
  float velocity_ = 0.8f;
  float noteMidi_ = 69;
  float mod_[kParamCount] = {};
  float aegLevel_ = 0, fegLevel_ = 0;
  float oscHz_[3] = {}, oscP_[3][7] = {}, oscDetune_[3] = {}, oscLow_[3] = {}, oscHigh_[3] = {};
  int oscType_[3] = {}, oscVoices_[3] = {1, 1, 1}, oscDeform_[3] = {};
  float chanGain_[6] = {};
  int chanRoute_[6] = {}, ringMode_[2] = {};
  float noiseColour_ = 0, noiseLp_ = 0, preGain_ = 1;
  int fmMode_ = 0;
  float fmIndex_ = 0;
  float cut_[2] = {1000, 1000}, res_[2] = {};
  FilterKind kind_[2] = {F_off, F_off};
  int sub_[2] = {};
  int config_ = 0;
  float balance_ = 0, feedback_ = 0;
  ShaperKind shaper_ = W_off;
  float wsDrive_ = 1;
  float amp_ = 0, ampStep_ = 0, volume_ = 1;
  float lastOut_ = 0, loop_ = 0;
  bool hpOn_ = false;
  float hpG_ = 0;
  int hpStages_ = 1;
  float hp_[8] = {};
  float send_[4] = {};
};

// -- the voice -----------------------------------------------------------------------

class Voice {
 public:
  void prepare(double sampleRate, uint32_t seed) {
    sr_ = (float)sampleRate;
    scene_[0].prepare(sr_, seed);
    scene_[1].prepare(sr_, seed ^ 0x5CE4E0B1u);
    fx_.prepare(sr_);
    bindScenes();
  }

  /// The patch, to be read into; call `patchChanged` after writing it.
  Patch &patch() { return patch_; }
  void patchChanged() { bindScenes(); }

  /// A new version of the patch while the voice plays: everything it holds
  /// is replaced, and nothing it is doing -- envelopes, phases, filters -- is
  /// reset, so an edit is heard as a change and not as a new note.
  void update(const Patch &next) { patch_ = next; bindScenes(); }

  /// Wavetables per oscillator slot: 0-2 scene A's, 3-5 scene B's.
  void setTable(int slot, const Wavetable *t) {
    if (slot >= 0 && slot < 6) scene_[slot / 3].setTable(slot % 3, t);
  }
  void setMacro(int i, float v) { if (i >= 0 && i < kMacroCount) macroIn_[i] = v; }
  void setTempo(float bpm) { tempo_ = bpm > 1 ? bpm : 120; }

  void noteOn(float velocity) {
    ++notes_;
    for (auto &s : scene_) s.noteOn(velocity, notes_);
  }

  void noteOff() { for (auto &s : scene_) s.noteOff(); }

  void render(float *out, int n, float hz) {
    // Which scenes sound: single plays the active one, key split by the
    // note against the split key, dual both, channel split scene A here.
    const int mode = (int)patch_.g[G_scenemode];
    const float midi = 69.0f + 12.0f * std::log2(clampf(hz, 1.0f, 20000.0f) / 440.0f);
    bool playA, playB;
    switch (mode) {
      case 1: playA = midi < patch_.g[G_splitkey]; playB = !playA; break;
      case 2: playA = playB = true; break;
      case 3: playA = true; playB = false; break;
      default: playA = patch_.g[G_scene_active] < 0.5f; playB = !playA; break;
    }
    const bool fxOn = patch_.g[G_fx_disable] < 0.5f && patch_.g[G_fx_bypass] < 0.5f;
    // The patch's master volume is stored in decibels (ENUMS.md).
    const float masterGain = dbToGain(clampf(patch_.g[G_volume], -48, 12));
    int done = 0;
    while (done < n) {
      const int block = (n - done) < kBlock ? (n - done) : kBlock;
      if (playA) scene_[0].control(hz, block, macroIn_, patch_.macroBipolar, tempo_);
      if (playB) scene_[1].control(hz, block, macroIn_, patch_.macroBipolar, tempo_);
      for (int i = 0; i < block; ++i) {
        const float a = playA ? scene_[0].sample() : 0.0f;
        const float b = playB ? scene_[1].sample() : 0.0f;
        const float mix = fxOn ? fx_.process(a, b, scene_[0].sends(), scene_[1].sends(), patch_.g,
                                             fxLayout(), tempo_)
                               : a + b;
        // A soft ceiling: a patch that sums hot through the effects is
        // rounded off above unity rather than clipped hard by what follows.
        // DC blocked: an asymmetric shaper or a rectifying effect leaves an
        // offset no speaker should be handed.
        dc_ += 0.0005f * (mix - dc_);
        const float y = (mix - dc_) * masterGain;
        const float ay = std::fabs(y);
        out[done + i] = ay <= 0.9f ? y : (y > 0 ? 1.0f : -1.0f) * (0.9f + 0.1f * std::tanh((ay - 0.9f) * 10.0f));
      }
      done += block;
    }
  }

 private:
  static const int kBlock = 16;

  static FxLayout fxLayout() {
    return {G_fx1_type, G_fx2_type - G_fx1_type, G_fx1_p0__deactivated, G_fx1_p0__temposync,
            G_fx1_p0__extend_range, G_fx_disable, G_volume_FX1};
  }

  void bindScenes() {
    scene_[0].setScene(&patch_.scene[0]);
    scene_[1].setScene(&patch_.scene[1]);
  }

  Patch patch_;
  SceneVoice scene_[2];
  FxChain fx_;
  float sr_ = 48000, tempo_ = 120, dc_ = 0;
  uint32_t notes_ = 0;
  float macroIn_[kMacroCount] = {};
};

}  // namespace resurgent
}  // namespace vascular

#endif
