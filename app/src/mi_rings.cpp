// SPDX-License-Identifier: GPL-3.0-only
// RINGS: Mutable Instruments Rings (third_party/rings, unchanged) as a tracker instrument.
//
// The adapter does what rings.cc and cv_scaler.cc do with the module's jacks and knobs, with
// V/OCT and STRUM patched (the tracker plays the notes) and IN either unpatched -- the internal
// exciter, struck on every strum -- or fed with a bow: noise shaped by the note's gate, as a
// cable into IN would bring it, with rings.cc's own noise gate in front. All six resonator models,
// polyphony 1, 2, 3 and 4, the chord quantization of the sympathetic-quantized model (STRUCTURE
// picks one of the eleven chords, with cv_scaler's rounding), the FREQUENCY knob (the tonic the
// sympathetic strings are tuned from), the FM input (driven here by a slow sine at a recipe's
// depth), and the easter-egg mode (Disastrous Peace: StringSynthPart with its six effects, picked
// as the module picks them, by the model). ODD goes left, EVEN right.
//
// Rings runs at its own 48 kHz in its own 24-sample blocks; mi_resampler.hpp brings it to
// 44.1 kHz. One running Rings per track / slot / patch: notes strum it (audio.cpp retriggers
// native instruments), so its own polyphony decides how many strings ring at once, as on the
// module.
#include "eng.h"
#include "mi_resampler.hpp"
#include "native_engines.hpp"
#include "rings/dsp/part.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/strummer.h"
#include "stmlib/utils/random.h"
#include <cstdint>
#include <memory>
#include <new>
namespace {
using namespace amalthea;
enum Exciter : uint8_t { Internal, Bow, BowPluck };
struct RingsCooked {
  uint32_t seed = 0;
  int family = 0;
  uint8_t model = 0, polyphony = 1, easterEgg = 0, fx = 0, exciter = Internal;
  float structure = .4f, brightness = .5f, damping = .6f, position = .3f;   // the four knobs
  float tonic = 36;            // the FREQUENCY knob: 12 + 0..60 semitones
  float transpose = 0;         // the notes' octave (semitones)
  float fmDepth = 0, fmHz = .2f;   // FM input: a sine of this depth (semitones) and rate
  float bowLevel = 0, bowAttack = .05f, bowRelease = .3f;   // the bow into IN (seconds)
  float width = 1;             // 1: ODD left / EVEN right; 0: both summed in the middle
  float gain = 1;              // the measured level brought to the target
};
struct RingsState {
  uint16_t reverb[32768];
  rings::Part part;
  rings::StringSynthPart synth;
  rings::Strummer strummer;
  MiResampler<int(rings::kMaxBlockSize)> resampler;
  rings::Patch patch;
  rings::PerformanceState ps;
  float note = 48, bowEnv = 0, inLevel = 0, lfo = 0;
  uint32_t random = 1;
  bool strum = false, gate = false, released = false;
  int quiet = 0;
  float bowNoise() { return stmlib::Random::GetFloat() * 2 - 1; }
};

// ------------------------------------------------------------------ recipes
// Each family is a model, a polyphony, an exciter and ranges for the knobs, written for dark
// ambient, drones and synthwave: bowls, bells and gongs, bowed metal and strings, sympathetic
// drones, dark harps and plucks, FM tines and growls, quantized minor chords, long reverberant
// strings, and the six Disastrous Peace string machines.
struct Range { float lo, hi; };
struct RingsFamily {
  const char *name;
  uint8_t model, polyphony, egg, exciter, weight;
  Range structure, brightness, damping, position, tonic, bow;
  int8_t transpose;
};
const RingsFamily families[] = {
  // name           model poly egg exciter w  structure     brightness    damping       position      tonic       bow       transpose
  {"GLASS BOWL",     0, 1, 0, Internal, 3, {.22f,.42f}, {.55f,.80f}, {.78f,.95f}, {.30f,.70f}, {24,48}, {0,0}, 0},
  {"IRON BELL",      0, 3, 0, Internal, 2, {.55f,.80f}, {.30f,.55f}, {.70f,.90f}, {.15f,.45f}, {24,48}, {0,0}, 0},
  {"DARK MALLET",    0, 4, 0, Internal, 2, {.10f,.30f}, {.12f,.32f}, {.40f,.65f}, {.20f,.60f}, {24,48}, {0,0}, -12},
  {"TEMPLE GONG",    0, 2, 0, Internal, 2, {.78f,.96f}, {.30f,.55f}, {.86f,.96f}, {.10f,.35f}, {24,36}, {0,0}, -12},
  {"BOWED METAL",    0, 1, 0, Bow,      2, {.40f,.75f}, {.25f,.55f}, {.80f,.95f}, {.25f,.60f}, {24,48}, {.10f,.25f}, 0},
  {"SITAR HAZE",     1, 1, 0, Internal, 2, {.20f,.50f}, {.40f,.70f}, {.70f,.90f}, {.20f,.60f}, {24,40}, {0,0}, 0},
  {"SYMPATHY",       1, 1, 0, Bow,      3, {.10f,.60f}, {.25f,.50f}, {.85f,.97f}, {.30f,.70f}, {24,36}, {.08f,.20f}, -12},
  {"DARK HARP",      2, 4, 0, Internal, 3, {.05f,.30f}, {.20f,.45f}, {.50f,.75f}, {.15f,.50f}, {24,48}, {0,0}, 0},
  {"NEON PLUCK",     2, 2, 0, Internal, 3, {.15f,.45f}, {.50f,.75f}, {.40f,.65f}, {.10f,.40f}, {24,48}, {0,0}, 0},
  {"BOWED STRING",   2, 1, 0, Bow,      2, {.20f,.50f}, {.30f,.60f}, {.80f,.95f}, {.20f,.60f}, {24,48}, {.10f,.22f}, -12},
  {"WIRE BASS",      2, 1, 0, Internal, 2, {.30f,.65f}, {.35f,.65f}, {.35f,.60f}, {.10f,.30f}, {12,24}, {0,0}, -24},
  {"FM TINE",        3, 2, 0, Internal, 2, {.25f,.45f}, {.35f,.60f}, {.55f,.80f}, {.20f,.60f}, {24,48}, {0,0}, 0},
  {"FM GROWL",       3, 1, 0, Internal, 2, {.65f,.92f}, {.55f,.85f}, {.30f,.60f}, {.40f,.80f}, {24,40}, {0,0}, -12},
  {"FM CHIME",       3, 4, 0, Internal, 1, {.45f,.70f}, {.40f,.70f}, {.65f,.85f}, {.20f,.50f}, {24,48}, {0,0}, 0},
  {"RING CHORDS",    4, 2, 0, Internal, 3, {.00f,.99f}, {.30f,.55f}, {.70f,.90f}, {.20f,.60f}, {24,48}, {0,0}, 0},
  {"CHORD RAIN",     4, 4, 0, Internal, 2, {.00f,.99f}, {.40f,.65f}, {.55f,.80f}, {.30f,.70f}, {24,48}, {0,0}, 0},
  {"CHORD DRONE",    4, 1, 0, Bow,      2, {.00f,.99f}, {.25f,.50f}, {.85f,.97f}, {.30f,.70f}, {24,40}, {.08f,.18f}, -12},
  {"CATHEDRAL",      5, 1, 0, Internal, 3, {.20f,.50f}, {.30f,.55f}, {.78f,.90f}, {.20f,.80f}, {24,48}, {0,0}, 0},
  {"SPACE PLUCK",    5, 2, 0, Internal, 2, {.10f,.40f}, {.45f,.70f}, {.60f,.85f}, {.30f,.80f}, {24,48}, {0,0}, 0},
  {"GHOST STRING",   5, 1, 0, BowPluck, 2, {.20f,.55f}, {.20f,.45f}, {.78f,.90f}, {.25f,.75f}, {24,40}, {.06f,.15f}, -12},
  {"PEACE FORMANT",  0, 2, 1, Internal, 1, {.00f,.99f}, {.20f,.60f}, {.60f,.90f}, {.20f,.80f}, {24,36}, {0,0}, 0},
  {"PEACE CHORUS",   1, 2, 1, Internal, 2, {.00f,.99f}, {.30f,.65f}, {.70f,.95f}, {.30f,.70f}, {24,36}, {0,0}, 0},
  {"PEACE REVERB",   2, 1, 1, Internal, 2, {.00f,.99f}, {.25f,.55f}, {.80f,.96f}, {.30f,.80f}, {24,36}, {0,0}, 0},
  {"PEACE VOWEL",    3, 2, 1, Internal, 1, {.00f,.99f}, {.30f,.60f}, {.60f,.90f}, {.20f,.80f}, {24,36}, {0,0}, 0},
  {"PEACE ENSEMB",   4, 4, 1, Internal, 2, {.00f,.99f}, {.30f,.65f}, {.65f,.92f}, {.30f,.70f}, {24,36}, {0,0}, 0},
  {"PEACE SPACE",    5, 1, 1, Internal, 2, {.00f,.99f}, {.25f,.50f}, {.85f,.96f}, {.30f,.80f}, {24,36}, {0,0}, -12},
};
constexpr int FamilyCount = int(sizeof families / sizeof families[0]);
int familyOf(uint32_t seed) {
  int total = 0;
  for (const auto &f : families) total += f.weight;
  uint32_t h = seed * 0x9e3779b1u; h ^= h >> 16;
  int x = int(h % uint32_t(total));
  for (int i = 0; i < FamilyCount; i++) { if (x < families[i].weight) return i; x -= families[i].weight; }
  return 0;
}
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed) : s(seed * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull) {}
  uint32_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return uint32_t(s >> 11); }
  float unit() { return float(next() & 0xffffff) / 16777216.f; }
  float in(Range r) { return r.lo + (r.hi - r.lo) * unit(); }
  float in(float a, float b) { return a + (b - a) * unit(); }
};
void draw(RingsCooked &c, const RingsFamily &f, Rng &r) {
  c.model = f.model, c.polyphony = f.polyphony, c.easterEgg = f.egg, c.fx = f.model, c.exciter = f.exciter;
  c.structure = r.in(f.structure), c.brightness = r.in(f.brightness), c.damping = r.in(f.damping), c.position = r.in(f.position);
  c.tonic = 12 + std::round(r.in(f.tonic));
  c.transpose = f.transpose;
  c.bowLevel = r.in(f.bow);
  c.bowAttack = r.in(.02f, .9f), c.bowRelease = r.in(.2f, 1.6f);
  // A slow FM wander on a third of the patches: under a fifth of a semitone.
  c.fmDepth = r.unit() < .35f ? r.in(.03f, .18f) : 0, c.fmHz = r.in(.05f, .6f);
  c.width = r.in(.6f, 1.f);
}
// Varies the tone, never the pitch: STRUCTURE always stays (it tunes the sympathetic strings,
// picks the chord or the FM ratio, and sets the modes' and strings' inharmonicity, which moves
// their partials), as do the model, the polyphony, FREQUENCY and the FM depth.
void vary(RingsCooked &c, const RingsFamily &f, uint32_t var) {
  Rng r(uint64_t(var) << 8 ^ 0x5113c0ull ^ c.seed), t(uint64_t(var) * 31 + 7);
  RingsCooked target = c;
  draw(target, f, t);
  float a = r.in(.4f, .75f);
  auto toward = [&](float &v, float w) { v += (w - v) * a; };
  toward(c.brightness, target.brightness), toward(c.damping, target.damping), toward(c.position, target.position);
  toward(c.bowLevel, target.bowLevel), toward(c.bowAttack, target.bowAttack), toward(c.bowRelease, target.bowRelease);
  toward(c.width, target.width);
}

// ------------------------------------------------------------------ the voice
void setup(RingsState &s, const RingsCooked &c) {
  // The module's state lives in zeroed static memory, and Init leaves parts of it (the easter egg's
  // voice groups) to that; here it may be reused arena or scratch memory, so it is zeroed first.
  std::memset(static_cast<void *>(&s), 0, sizeof s);
  new (&s.part) rings::Part();
  new (&s.synth) rings::StringSynthPart();
  new (&s.strummer) rings::Strummer();
  s.strummer.Init(0.01f, rings::kSampleRate / rings::kMaxBlockSize);
  s.part.Init(s.reverb);
  s.synth.Init(s.reverb);
  s.part.set_polyphony(c.polyphony);
  s.part.set_model(rings::ResonatorModel(c.model));
  s.synth.set_polyphony(c.polyphony);
  s.synth.set_fx(rings::FxType(c.fx));
  s.resampler.reset();
  std::memset(&s.ps, 0, sizeof s.ps);
  s.bowEnv = 0, s.inLevel = 0, s.lfo = 0, s.quiet = 0;
  s.strum = s.gate = s.released = false;
  s.random = c.seed ? c.seed : 1;
}
void strike(RingsState &s, int note) { s.note = float(note); s.strum = true; s.gate = true; s.released = false; s.quiet = 0; }
// One Rings block (24 samples at 48 kHz): rings.cc's FillBuffer.
void block(RingsState &s, const RingsCooked &c, float *l, float *r) {
  constexpr size_t n = rings::kMaxBlockSize;
  float in[n], out[n], aux[n];
  s.patch.structure = std::clamp(c.structure, 0.f, .9995f);   // (cv_scaler's ranges)
  s.patch.brightness = std::clamp(c.brightness, 0.f, 1.f);
  s.patch.damping = std::clamp(c.damping, 0.f, 1.f);
  s.patch.position = std::clamp(c.position, 0.f, 1.f);
  s.lfo += c.fmHz * n / rings::kSampleRate;
  if (s.lfo >= 1) s.lfo -= 1;
  s.ps.fm = std::clamp(c.fmDepth * std::sin(6.2831853f * s.lfo), -48.f, 48.f);
  s.ps.tonic = c.tonic;
  s.ps.note = s.note + c.transpose - c.tonic;   // V/OCT: the tracker's note, heard as tonic + note
  s.ps.chord = std::clamp(int(c.structure * (rings::kNumChords - 1) + .5f), 0, rings::kNumChords - 1);
  s.ps.internal_note = false;
  s.ps.internal_strum = false;
  s.ps.strum = s.strum;
  s.strum = false;
  const bool bow = !c.easterEgg && c.exciter != Internal;
  s.ps.internal_exciter = !bow;
  // The bow: noise, its level following the gate, through rings.cc's noise gate.
  const float att = 1 - std::exp(-float(n) / (rings::kSampleRate * std::max(.005f, c.bowAttack)));
  const float rel = 1 - std::exp(-float(n) / (rings::kSampleRate * std::max(.01f, c.bowRelease)));
  for (size_t i = 0; i < n; i++) {
    float x = 0;
    if (bow) {
      s.bowEnv += ((s.gate ? 1.f : 0.f) - s.bowEnv) * (s.gate ? att : rel) / float(n);
      x = c.bowLevel * s.bowEnv * s.bowNoise();
      if (c.exciter == BowPluck && s.ps.strum && i < 48) x += .5f * s.bowNoise();
      const float kThreshold = 0.00003f, e = x * x - s.inLevel;
      s.inLevel += e * (e > 0 ? .1f : .0001f);
      x *= s.inLevel <= kThreshold ? (1.f / kThreshold) * s.inLevel : 1.f;
    }
    in[i] = x;
  }
  if (c.easterEgg) {
    s.strummer.Process(nullptr, n, &s.ps);
    s.synth.Process(s.ps, s.patch, in, out, aux, n);
  } else {
    s.strummer.Process(in, n, &s.ps);
    s.part.Process(s.ps, s.patch, in, out, aux, n);
  }
  const float mid = .5f * (1 - c.width);
  for (size_t i = 0; i < n; i++) {
    const float o = out[i] * c.gain, a = aux[i] * c.gain;
    l[i] = o + (a - o) * mid;
    r[i] = a + (o - a) * mid;
  }
}
struct RandomScope {
  uint32_t &slot, saved;
  explicit RandomScope(uint32_t &s) : slot(s), saved(stmlib::Random::state()) { stmlib::Random::Seed(slot); }
  ~RandomScope() { slot = stmlib::Random::state(); stmlib::Random::Seed(saved); }
};
// The level: C3 struck (and held 1.5 s for the bowed ones) over 2 s; the RMS brought to 0.12,
// no more than keeps the peak under 0.7. Run where the patch is cooked, never in the callback.
void measure(RingsCooked &c) {
  thread_local std::unique_ptr<RingsState> scratch;
  if (!scratch) scratch.reset(static_cast<RingsState *>(::operator new(sizeof(RingsState), std::align_val_t(64))));
  RingsState &s = *scratch;
  c.gain = 1;
  setup(s, c);
  RandomScope rs(s.random);
  strike(s, 48);
  double energy = 0;
  float peak = 1e-6f;
  float l[rings::kMaxBlockSize], r[rings::kMaxBlockSize];
  const int blocks = int(2 * rings::kSampleRate / rings::kMaxBlockSize);
  for (int b = 0; b < blocks; b++) {
    if (b == blocks * 3 / 4) s.gate = false;
    block(s, c, l, r);
    for (size_t i = 0; i < rings::kMaxBlockSize; i++) {
      energy += .5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
      peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
  }
  const float rms = float(std::sqrt(energy / (blocks * rings::kMaxBlockSize)));
  c.gain = std::clamp(std::min(rms > 1e-7f ? .12f / rms : 4.f, .7f / peak), .1f, 4.f);
}

void cook(void *out, const int32_t *val) {
  auto &c = *new (out) RingsCooked();
  c.seed = uint32_t(val[0]);
  c.family = familyOf(c.seed);
  Rng r(c.seed ^ 0x41494e47ull);
  draw(c, families[c.family], r);
  if (val[1]) vary(c, families[c.family], uint32_t(val[1]));
  measure(c);
}
void retrigger(void *state, int note, int) { strike(*static_cast<RingsState *>(state), note); }
void reset(void *) {}
void on(void *data, const void *patch, int32_t note16, int) {
  const auto &c = *static_cast<const RingsCooked *>(patch);
  auto &s = *static_cast<RingsState *>(nativeVoiceClaim(data, sizeof(RingsState), retrigger));
  setup(s, c);
  strike(s, note16 >> 16);
}
void off(void *data, const void *, int32_t) {
  auto *s = static_cast<RingsState *>(nativeVoiceState(data));
  if (s) s->gate = false, s->released = true;
}
int render(void *data, const void *patch, int32_t *left, int32_t *right) {
  auto *sp = static_cast<RingsState *>(nativeVoiceState(data));
  if (!sp) return 0;
  auto &s = *sp;
  const auto &c = *static_cast<const RingsCooked *>(patch);
  RandomScope rs(s.random);
  float l[ENG_BLK], r[ENG_BLK];
  s.resampler.process(l, r, ENG_BLK, [&](float *bl, float *br) { block(s, c, bl, br); });
  float peak = 0;
  for (int i = 0; i < ENG_BLK; i++) {
    const float a = std::isfinite(l[i]) ? std::clamp(l[i], -4.f, 4.f) : 0.f, b = std::isfinite(r[i]) ? std::clamp(r[i], -4.f, 4.f) : 0.f;
    left[i] = int32_t(a * 32768), right[i] = int32_t(b * 32768);
    peak = std::max(peak, std::max(std::fabs(a), std::fabs(b)));
  }
  if (s.released && peak < 1e-4f) s.quiet++; else s.quiet = 0;   // (released and under -80 dBFS for 186 ms: ended)
  return !s.released || s.quiet < 256;
}
static_assert(sizeof(RingsCooked) <= ENG_CK_MAX, "RINGS patch storage");
}
extern "C" const eng_t ENG_RINGS = {"RINGS", "MI RESONATOR", 0, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, sizeof(void *), sizeof(RingsCooked), nullptr, cook, reset, on, off, render};
extern "C" const char *amalthea_rings_family(unsigned seed) { return families[familyOf(seed)].name; }
extern "C" int amalthea_rings_family_count(void) { return FamilyCount; }
extern "C" int amalthea_rings_family_index(unsigned seed) { return familyOf(seed); }
// (tests: what a family is) model 0..5, polyphony 1..4, easter egg, exciter
extern "C" int amalthea_rings_family_shape(int f, int *model, int *polyphony, int *egg, int *exciter) {
  if (f < 0 || f >= FamilyCount) return 0;
  *model = families[f].model, *polyphony = families[f].polyphony, *egg = families[f].egg, *exciter = families[f].exciter;
  return 1;
}
namespace amalthea {
size_t ringsStateBytes() { return sizeof(RingsState); }
}
