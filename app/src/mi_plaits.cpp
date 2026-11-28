// SPDX-License-Identifier: GPL-3.0-only
// PLAITS: Mutable Instruments Plaits 1.2 (third_party/vascular/plaits: upstream, with Vascular's
// namespace and its 48 kHz sample-rate constant) as a tracker instrument.
//
// The adapter does what plaits.cc and cv_scaler.cc do with the module's jacks and knobs: the
// tracker's note into V/OCT (FREQUENCY sets the octave), its gate into TRIGGER, and, by recipe,
// LEVEL, TIMBRE, MORPH, FM and HARMONICS each either unpatched or fed with a slow sine, as a cable
// from an LFO would bring it. All 24 models of 1.2, HARMONICS, TIMBRE, MORPH, the three
// attenuverters (which route the internal decay envelope while their input is unpatched, and
// scale the input while it is patched, as on the module), DECAY and LPG COLOUR. The LPG works in
// the module's three ways:
//   PING   TRIGGER patched, LEVEL unpatched: the gate pings the LPG, DECAY sets its fall;
//   GATE   TRIGGER and LEVEL patched: the LPG follows an attack / release on the note's gate;
//   DRONE  nothing patched: the LPG is bypassed, the voice sounds continuously (an attack /
//          release here keeps notes apart).
// In every mode a note ends at its length: from its end the output falls to -60 dB in the
// recipe's release time.
// OUT and AUX are mixed by recipe and spread across the stereo field.
//
// Plaits runs at its own 48 kHz in its own 12-sample blocks (its trigger delay and envelopes count
// them); mi_resampler.hpp brings it to 44.1 kHz. One running Plaits per track / slot / patch, as
// the module is one voice: a note retriggers it.
#include "eng.h"
#include "mi_resampler.hpp"
#include "native_engines.hpp"
#include "plaits/dsp/voice.h"
#include "vascular_stmlib/utils/random.h"
#include <cstdint>
#include <memory>
#include <new>
namespace {
using namespace amalthea;
enum Lpg : uint8_t { Ping, Gate, Drone };
enum Cv : uint8_t { CvTimbre = 1, CvMorph = 2, CvFm = 4, CvHarm = 8 };
struct PlaitsCooked {
  uint32_t seed = 0;
  int family = 0;
  uint8_t model = 8, lpg = Ping, cv = 0;
  float harmonics = .5f, timbre = .5f, morph = .5f;          // the three big knobs
  float fmAmount = 0, timbreAmount = 0, morphAmount = 0;      // the attenuverters, -1 .. 1
  float decay = .5f, colour = .5f;                            // DECAY, LPG COLOUR
  float frequency = 0;                                        // FREQUENCY: semitones from the note
  float attack = .01f, release = .3f;                         // seconds: attack to full (GATE / DRONE), release to -60 dB
  float lfoHz = .1f, lfoDepth = 0, lfoHarm = 0;               // the sine into the patched inputs
  float aux = 0, spread = 0;                                  // OUT / AUX balance, their stereo spread
  float gain = 1;                                             // the measured level brought to the target
};
struct PlaitsState {
  alignas(16) char shared[16384];                             // plaits.cc's shared_buffer
  plaits::Voice voice;
  MiResampler<int(plaits::kBlockSize)> resampler;
  float note = 48, env = 0, tail = 0, lfo = 0, dcL = 0, dcR = 0;   // tail: 1 while the note is held, the release after it   // dc: Plaits' +1 LSB output offset, blocked (as the other native engines)
  uint32_t random = 1;
  bool gate = false, low = false, released = false;
  int quiet = 0;
};

// ------------------------------------------------------------------ recipes
// Each family is written for one model and sets that model's own controls on purpose; the 47
// together cover all 24 models (two families on most), weighted so no model dominates the draw.
// Ranges are for Amalthea's music: synthwave basses, brass, leads, stabs and pads; dark ambient
// drones, choirs, swarms, wind and dust; industrial drums and metal.
struct Range { float lo, hi; };
struct PlaitsFamily {
  const char *name;
  uint8_t model, lpg, cv, weight;
  Range harmonics, timbre, morph;
  Range fmAmount, timbreAmount, morphAmount, decay, colour;
  Range attack, release;          // seconds: attack to full level (GATE / DRONE); release from the
                                  // note's end to -60 dB (every mode)
  Range lfoHz, lfoDepth;          // the sine on the inputs in cv
  int8_t frequency;               // semitones
  Range aux;
  uint16_t chords;                // chord / patch choice: bit i -> harmonics on index i (CHORDS,
                                  // STRING MACHINE, CHIPTUNE: 11 chords; SIX-OP: patch i of 16 low)
};
constexpr Range Z{0, 0};
const PlaitsFamily families[] = {
  // --- 0 VA-VCF: a classic oscillator into a resonant filter (HARMONICS resonance / character, TIMBRE cutoff, MORPH wave and sub)
  {"VCF BASS",     0, Ping,  0,        2, {.20f,.45f}, {.18f,.40f}, {.10f,.40f}, Z, {.30f,.60f}, Z, {.32f,.52f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, -12, {0,0}, 0},
  {"VCF SWEEP",    0, Gate,  CvTimbre, 2, {.50f,.80f}, {.20f,.40f}, {.50f,.90f}, Z, {.45f,.75f}, Z, {.60f,.80f}, {.40f,.70f}, {.60f,2.f}, {.42f,1.2f}, {.03f,.12f}, {.30f,.55f}, 0, {0,.3f}, 0},
  // --- 1 PHASE DISTORTION (HARMONICS distortion frequency, TIMBRE amount, MORPH asymmetry)
  {"PD BRASS",     1, Gate,  0,        2, {.25f,.50f}, {.30f,.50f}, {.40f,.60f}, Z, {.30f,.55f}, Z, {.50f,.70f}, {.50f,.80f}, {.03f,.10f}, {.15f,.30f}, Z, Z, 0, {0,.2f}, 0},
  {"PD RESONANT",  1, Ping,  0,        1, {.60f,.90f}, {.50f,.80f}, {.10f,.30f}, Z, {.20f,.50f}, Z, {.45f,.70f}, {.60f,.90f}, Z, {.06f,.30f}, Z, Z, 0, {.3f,.6f}, 0},
  // --- 2-4 SIX-OP FM: HARMONICS picks one of 32 DX patches, TIMBRE the modulators' level, MORPH stretches the envelopes
  {"DX BANK A",    2, Ping,  0,        1, Z, {.40f,.60f}, {.40f,.60f}, Z, Z, Z, {.55f,.80f}, {.40f,.70f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0xffff},
  {"DX SLOW A",    2, Ping,  0,        1, Z, {.30f,.50f}, {.65f,.90f}, Z, Z, Z, {.70f,.90f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0xffff},
  {"DX BANK B",    3, Ping,  0,        1, Z, {.40f,.60f}, {.40f,.60f}, Z, Z, Z, {.55f,.80f}, {.40f,.70f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0xffff},
  {"DX DARK B",    3, Ping,  0,        1, Z, {.20f,.40f}, {.60f,.85f}, Z, Z, Z, {.70f,.90f}, {.20f,.45f}, Z, {.06f,.30f}, Z, Z, -12, {0,.3f}, 0xffff},
  {"DX BANK C",    4, Ping,  0,        1, Z, {.40f,.60f}, {.40f,.60f}, Z, Z, Z, {.55f,.80f}, {.40f,.70f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0xffff},
  {"DX GLASS C",   4, Ping,  0,        1, Z, {.55f,.75f}, {.50f,.75f}, Z, Z, Z, {.65f,.85f}, {.60f,.90f}, Z, {.06f,.30f}, Z, Z, 12, {0,.3f}, 0xffff},
  // --- 5 WAVE TERRAIN (HARMONICS terrain, TIMBRE path radius, MORPH path offset)
  {"TERRAIN DRIFT",5, Drone, CvMorph,  2, {.10f,.90f}, {.30f,.60f}, {.30f,.70f}, Z, Z, {.40f,.70f}, Z, Z, {.80f,2.5f}, {.52f,1.4f}, {.02f,.08f}, {.35f,.60f}, -12, {0,.4f}, 0},
  {"TERRAIN LEAD", 5, Ping,  0,        1, {.10f,.90f}, {.50f,.80f}, {.20f,.60f}, Z, {.20f,.50f}, {-.3f,.3f}, {.50f,.70f}, {.50f,.80f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0},
  // --- 6 STRING MACHINE (HARMONICS chord, TIMBRE chorus / filter, MORPH waveform / filter)
  {"SOLINA DARK",  6, Gate,  0,        2, Z, {.50f,.85f}, {.20f,.50f}, Z, Z, Z, {.60f,.80f}, {.40f,.70f}, {.40f,1.5f}, {.52f,1.2f}, Z, Z, -12, {0,.3f}, 0x7a},
  {"STR SWELL",    6, Gate,  CvMorph,  1, Z, {.40f,.70f}, {.30f,.60f}, Z, Z, {.30f,.60f}, {.60f,.80f}, {.40f,.70f}, {1.f,3.f}, {.70f,1.5f}, {.03f,.10f}, {.25f,.45f}, 0, {0,.3f}, 0x3a},
  // --- 7 CHIPTUNE (HARMONICS chord, TIMBRE arpeggio / inversion, MORPH pulse width; TIMBRE attenuverter: its own envelope,
  //     kept positive: a negative shape leaves AUX unenveloped, a drone after the note)
  {"CHIP ARP",     7, Ping,  0,        2, Z, {.55f,.95f}, {.30f,.60f}, Z, {.15f,.60f}, Z, {.45f,.70f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0x78},
  {"CHIP BASS",    7, Ping,  0,        1, Z, {.00f,.20f}, {.10f,.40f}, Z, {.30f,.80f}, Z, {.35f,.55f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, -24, {0,.2f}, 0x03},
  // --- 8 VIRTUAL ANALOG (HARMONICS detune: .515-.525 is 5-25 cents, .6 a fifth, TIMBRE square / pulse, MORPH saw shape; AUX the synced pair)
  {"TWIN SAW",     8, Gate,  0,        2, {.515f,.525f}, {.00f,.30f}, {.70f,1.f}, Z, {.10f,.30f}, Z, {.55f,.75f}, {.50f,.80f}, {.01f,.20f}, {.15f,.52f}, Z, Z, 0, {0,.2f}, 0},
  {"SYNC LEAD",    8, Ping,  0,        2, {.60f,.90f}, {.30f,.60f}, {.60f,.90f}, Z, {.20f,.50f}, {.10f,.40f}, {.55f,.75f}, {.60f,.90f}, Z, {.06f,.30f}, Z, Z, 0, {.6f,1.f}, 0},
  // --- 9 WAVESHAPING (HARMONICS shaper waveform, TIMBRE fold amount, MORPH asymmetry)
  {"FOLD BASS",    9, Ping,  0,        2, {.20f,.60f}, {.25f,.55f}, {.30f,.70f}, Z, {.35f,.65f}, Z, {.35f,.55f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, -12, {0,.3f}, 0},
  {"FOLD BLOOM",   9, Gate,  CvTimbre, 1, {.40f,.80f}, {.20f,.45f}, {.30f,.70f}, Z, {.35f,.60f}, Z, {.60f,.80f}, {.40f,.70f}, {.50f,2.f}, {.52f,1.2f}, {.04f,.15f}, {.30f,.55f}, 0, {0,.3f}, 0},
  // --- 10 FM, two operators (HARMONICS ratio, TIMBRE index, MORPH feedback)
  {"FM BELL",     10, Ping,  0,        2, {.60f,.90f}, {.30f,.60f}, {.45f,.60f}, Z, {.20f,.50f}, Z, {.60f,.80f}, {.60f,.90f}, Z, {.06f,.30f}, Z, Z, 12, {0,.3f}, 0},
  {"FM BASS",     10, Ping,  0,        1, {.28f,.40f}, {.30f,.50f}, {.40f,.70f}, Z, {.40f,.70f}, Z, {.30f,.50f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, -12, {0,.3f}, 0},
  {"FM DRONE",    10, Drone, CvTimbre, 1, {.25f,.55f}, {.15f,.40f}, {.20f,.50f}, Z, {.30f,.60f}, Z, Z, Z, {1.f,3.f}, {.70f,1.4f}, {.02f,.07f}, {.30f,.60f}, -12, {0,.4f}, 0},
  // --- 11 GRAIN / FORMANT (HARMONICS formant ratio, TIMBRE formant frequency, MORPH width and shape)
  {"GRAIN VOX",   11, Gate,  CvTimbre, 2, {.30f,.60f}, {.30f,.60f}, {.30f,.70f}, Z, {.30f,.55f}, Z, {.60f,.80f}, {.40f,.70f}, {.20f,1.f}, {.35f,1.0f}, {.05f,.20f}, {.25f,.50f}, 0, {0,.4f}, 0},
  {"GRAIN HOWL",  11, Gate,  CvMorph,  1, {.60f,.90f}, {.20f,.45f}, {.50f,.90f}, Z, Z, {.30f,.60f}, {.60f,.80f}, {.30f,.60f}, {.50f,2.f}, {.52f,1.4f}, {.03f,.12f}, {.30f,.55f}, -12, {0,.4f}, 0},
  // --- 12 ADDITIVE (HARMONICS spectral bumps, TIMBRE the prominent harmonic, MORPH bump shape / organ)
  {"DARK ORGAN",  12, Gate,  0,        2, {.30f,.55f}, {.15f,.40f}, {.70f,1.f}, Z, Z, Z, {.60f,.80f}, {.40f,.70f}, {.05f,.40f}, {.15f,.52f}, Z, Z, -12, {0,.5f}, 0},
  {"ADD BELLS",   12, Ping,  0,        1, {.10f,.40f}, {.55f,.90f}, {.10f,.40f}, Z, {-.5f,-.2f}, Z, {.60f,.85f}, {.60f,.90f}, Z, {.06f,.30f}, Z, Z, 12, {0,.3f}, 0},
  // --- 13 WAVETABLE (HARMONICS bank, TIMBRE row, MORPH column)
  {"WT CRYPT",    13, Gate,  CvTimbre | CvMorph, 2, {.00f,.50f}, {.20f,.60f}, {.20f,.60f}, Z, {.30f,.60f}, {.30f,.60f}, {.60f,.80f}, {.40f,.70f}, {.40f,2.f}, {.52f,1.4f}, {.02f,.09f}, {.30f,.55f}, 0, {0,.3f}, 0},
  {"WT PLUCK",    13, Ping,  0,        1, {.50f,1.f}, {.40f,.80f}, {.30f,.70f}, Z, {.30f,.60f}, {.20f,.50f}, {.40f,.60f}, {.50f,.80f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0},
  // --- 14 CHORDS (HARMONICS chord, TIMBRE inversion, MORPH waveform)
  {"DARK CHORDS", 14, Gate,  0,        2, Z, {.30f,.60f}, {.15f,.50f}, Z, Z, Z, {.60f,.80f}, {.40f,.70f}, {.30f,1.2f}, {.42f,1.0f}, Z, Z, -12, {0,.3f}, 0x7a},
  {"CHORD STAB",  14, Ping,  0,        2, Z, {.30f,.60f}, {.40f,.70f}, Z, Z, {-.3f,-.1f}, {.30f,.48f}, {.40f,.70f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0x78},
  // --- 15 SPEECH (HARMONICS formant filter / SAM / LPC words, TIMBRE species, MORPH phoneme or word; FM: prosody, MORPH attenuverter: speed)
  {"VOWEL CHOIR", 15, Gate,  CvMorph,  1, {.00f,.30f}, {.30f,.60f}, {.10f,.90f}, Z, Z, {.20f,.50f}, {.60f,.80f}, {.40f,.70f}, {.30f,1.5f}, {.35f,1.0f}, {.03f,.10f}, {.25f,.50f}, -12, {0,.3f}, 0},
  {"SAM WORDS",   15, Ping,  0,        1, {.36f,.60f}, {.20f,.55f}, {.00f,1.f}, {.00f,.40f}, Z, {.00f,.40f}, {.55f,.80f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, 0, {0,.3f}, 0},
  {"LPC WORDS",   15, Ping,  0,        1, {.70f,1.f}, {.20f,.50f}, {.00f,1.f}, {.00f,.40f}, Z, {.00f,.40f}, {.55f,.80f}, {.30f,.60f}, Z, {.06f,.30f}, Z, Z, -12, {0,.3f}, 0},
  // --- 16 SWARM of eight saws (HARMONICS detune, TIMBRE grain density, MORPH grain overlap)
  {"SWARM DRONE", 16, Drone, CvMorph,  2, {.20f,.50f}, {.40f,.80f}, {.50f,.90f}, Z, Z, {.20f,.40f}, Z, Z, {1.f,3.f}, {.70f,1.5f}, {.02f,.06f}, {.20f,.40f}, -12, {0,.4f}, 0},
  {"SWARM HAZE",  16, Gate,  0,        1, {.05f,.30f}, {.70f,1.f}, {.70f,1.f}, Z, Z, Z, {.65f,.85f}, {.30f,.60f}, {.50f,2.f}, {.52f,1.4f}, Z, Z, 0, {0,.5f}, 0},
  // --- 17 FILTERED NOISE (HARMONICS LP -> BP -> HP, TIMBRE clock, MORPH resonance)
  {"WIND",        17, Drone, CvTimbre, 2, {.35f,.60f}, {.45f,.75f}, {.60f,.90f}, Z, {.30f,.60f}, Z, Z, Z, {.80f,2.5f}, {.52f,1.4f}, {.03f,.12f}, {.40f,.70f}, 0, {0,.5f}, 0},
  {"RESO NOISE",  17, Ping,  0,        1, {.30f,.70f}, {.40f,.80f}, {.85f,1.f}, Z, {.20f,.50f}, Z, {.40f,.65f}, {.40f,.80f}, Z, {.06f,.30f}, Z, Z, 0, {0,.5f}, 0},
  // --- 18 PARTICLES (HARMONICS frequency spread, TIMBRE density, MORPH filter: all-pass network -> band-pass)
  {"DUST RAIN",   18, Gate,  0,        2, {.40f,.80f}, {.30f,.60f}, {.10f,.40f}, Z, Z, Z, {.60f,.80f}, {.40f,.70f}, {.20f,1.f}, {.35f,1.0f}, Z, Z, 0, {0,.5f}, 0},
  {"GLASS DUST",  18, Drone, CvTimbre, 1, {.20f,.60f}, {.40f,.65f}, {.60f,.95f}, Z, {.20f,.50f}, Z, Z, Z, {.50f,2.f}, {.52f,1.4f}, {.04f,.15f}, {.25f,.50f}, 12, {0,.5f}, 0},
  // --- 19 INHARMONIC STRING (HARMONICS inharmonicity, TIMBRE excitation brightness, MORPH decay)
  {"DARK STRING", 19, Ping,  0,        2, {.15f,.45f}, {.25f,.50f}, {.60f,.85f}, Z, Z, Z, {.50f,.70f}, {.40f,.70f}, Z, {.06f,.30f}, Z, Z, 0, {0,.4f}, 0},
  {"STEEL DRONE", 19, Drone, 0,        1, {.40f,.75f}, {.30f,.55f}, {.80f,1.f}, Z, Z, Z, Z, Z, {.50f,2.f}, {.52f,1.4f}, Z, Z, -12, {0,.4f}, 0},
  // --- 20 MODAL RESONATOR (HARMONICS material, TIMBRE brightness, MORPH decay)
  {"MODAL BOWL",  20, Ping,  0,        2, {.40f,.70f}, {.30f,.55f}, {.70f,.92f}, Z, Z, Z, {.50f,.70f}, {.50f,.80f}, Z, {.06f,.30f}, Z, Z, 0, {0,.4f}, 0},
  {"MODAL WOOD",  20, Ping,  0,        1, {.05f,.30f}, {.20f,.45f}, {.30f,.60f}, Z, Z, Z, {.40f,.60f}, {.40f,.70f}, Z, {.06f,.30f}, Z, Z, -12, {0,.3f}, 0},
  // --- 21 ANALOG BASS DRUM (HARMONICS attack / drive, TIMBRE brightness, MORPH decay; FM attenuverter: the pitch sweep)
  {"DEEP KICK",   21, Ping,  0,        2, {.20f,.45f}, {.15f,.40f}, {.50f,.80f}, {.00f,.30f}, Z, Z, {.40f,.60f}, {.40f,.70f}, Z, {.08f,.25f}, Z, Z, -12, {0,.3f}, 0},
  // --- 22 SNARE (HARMONICS tone / noise, TIMBRE modes balance, MORPH decay)
  {"DARK SNARE",  22, Ping,  0,        2, {.40f,.75f}, {.25f,.55f}, {.35f,.65f}, Z, Z, Z, {.40f,.60f}, {.40f,.70f}, Z, {.08f,.25f}, Z, Z, 0, {0,.3f}, 0},
  // --- 23 HI-HAT (HARMONICS metallic -> noise, TIMBRE high-pass, MORPH decay)
  {"METAL HAT",   23, Ping,  0,        2, {.25f,.65f}, {.50f,.80f}, {.15f,.50f}, Z, Z, Z, {.30f,.50f}, {.50f,.80f}, Z, {.08f,.25f}, Z, Z, 0, {0,.4f}, 0},
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
// HARMONICS on one of a discrete set's entries, at its centre (the module's quantizers: chords
// x 1.02 over 11, six-op patches x 1.02 over 32).
float pick(const PlaitsFamily &f, Rng &r) {
  const bool sixOp = f.model >= 2 && f.model <= 4;
  int choices[16], n = 0;
  for (int i = 0; i < 16; i++) if (f.chords >> i & 1) choices[n++] = i;
  int i = choices[r.next() % uint32_t(n)];
  if (sixOp) i += 16 * int(r.next() & 1);   // (the 16 bits name a patch in either half of the bank)
  const float count = sixOp ? 32.f : 11.f;
  return (i + .5f) / count / 1.02f;
}
void draw(PlaitsCooked &c, const PlaitsFamily &f, Rng &r) {
  c.model = f.model, c.lpg = f.lpg, c.cv = f.cv;
  c.harmonics = f.chords ? pick(f, r) : r.in(f.harmonics);
  c.timbre = r.in(f.timbre), c.morph = r.in(f.morph);
  c.fmAmount = r.in(f.fmAmount), c.timbreAmount = r.in(f.timbreAmount), c.morphAmount = r.in(f.morphAmount);
  c.decay = r.in(f.decay), c.colour = r.in(f.colour);
  c.attack = std::max(.003f, r.in(f.attack)), c.release = std::max(.02f, r.in(f.release));
  c.lfoHz = r.in(f.lfoHz), c.lfoDepth = r.in(f.lfoDepth);
  c.lfoHarm = 0;
  c.frequency = f.frequency;
  c.aux = r.in(f.aux), c.spread = r.in(.2f, .7f);
}
// Varies the tone, never the pitch or the model's choice: HARMONICS stays where it picks a chord,
// a patch, a bank, a ratio, a word bank or a detune (chords, string machine, chiptune, six-op,
// wavetable, FM, speech, VA, swarm) or sets the partials' spacing (string, modal); the FM attenuverter (pitch) and FREQUENCY always stay.
void vary(PlaitsCooked &c, const PlaitsFamily &f, uint32_t var) {
  Rng r(uint64_t(var) << 8 ^ 0x9a175ull ^ c.seed), t(uint64_t(var) * 131 + 3);
  PlaitsCooked target = c;
  draw(target, f, t);
  const float a = r.in(.4f, .75f);
  auto toward = [&](float &v, float w) { v += (w - v) * a; };
  static const bool harmonicsIsChoice[24] = {0,0,1,1,1,0,1,1,1,0,1,0,0,1,1,1,1,0,0,1,1,0,0,0};   // (+ STRING, MODAL: inharmonicity, material)
  if (!harmonicsIsChoice[c.model]) toward(c.harmonics, target.harmonics);
  toward(c.timbre, target.timbre);
  // SPEECH: MORPH picks the word; MORPH is left as drawn there.
  if (c.model != 15) toward(c.morph, target.morph);
  toward(c.timbreAmount, target.timbreAmount), toward(c.morphAmount, target.morphAmount);
  toward(c.decay, target.decay), toward(c.colour, target.colour);
  toward(c.attack, target.attack), toward(c.release, target.release);
  toward(c.lfoDepth, target.lfoDepth), toward(c.aux, target.aux), toward(c.spread, target.spread);
}

// ------------------------------------------------------------------ the voice
struct RandomScope {
  uint32_t &slot, saved;
  explicit RandomScope(uint32_t &s) : slot(s), saved(vascular_stmlib::Random::state()) { vascular_stmlib::Random::Seed(slot); }
  ~RandomScope() { slot = vascular_stmlib::Random::state(); vascular_stmlib::Random::Seed(saved); }
};
void setup(PlaitsState &s, const PlaitsCooked &c) {
  // (zeroed first, as the module's static memory is: reused arena or scratch memory otherwise)
  std::memset(static_cast<void *>(&s), 0, sizeof s);
  new (&s.voice) plaits::Voice();
  vascular_stmlib::BufferAllocator allocator(s.shared, sizeof s.shared);
  s.voice.Init(&allocator);
  s.resampler.reset();
  s.env = 0, s.tail = 0, s.lfo = 0, s.quiet = 0, s.dcL = s.dcR = 0;
  s.gate = s.low = s.released = false;
  s.random = c.seed ? c.seed : 1;
}
void strike(PlaitsState &s, int note) {
  s.low = s.gate;   // (a gate already high goes low for one block, so the next note is an edge)
  s.note = float(note), s.gate = true, s.released = false, s.quiet = 0;
}
// One Plaits block (12 samples at 48 kHz): plaits.cc's FillBuffer.
void block(PlaitsState &s, const PlaitsCooked &c, float *l, float *r) {
  constexpr size_t n = plaits::kBlockSize;
  plaits::Patch p{};
  plaits::Modulations m{};
  p.note = 60 + c.frequency;                 // FREQUENCY, the octave the recipe sits in
  p.harmonics = c.harmonics, p.timbre = c.timbre, p.morph = c.morph;
  p.frequency_modulation_amount = c.fmAmount, p.timbre_modulation_amount = c.timbreAmount, p.morph_modulation_amount = c.morphAmount;
  p.engine = c.model;
  p.decay = c.decay, p.lpg_colour = c.colour;
  m.note = s.note - 60;                      // V/OCT
  s.lfo += c.lfoHz * n / plaits::kSampleRate;
  if (s.lfo >= 1) s.lfo -= 1;
  const float sine = std::sin(6.2831853f * s.lfo), cosine = std::cos(6.2831853f * s.lfo);
  m.timbre_patched = c.cv & CvTimbre, m.timbre = c.lfoDepth * sine;
  m.morph_patched = c.cv & CvMorph, m.morph = c.lfoDepth * cosine;
  m.frequency_patched = c.cv & CvFm, m.frequency = c.lfoDepth * sine;
  m.harmonics = (c.cv & CvHarm) ? c.lfoHarm * sine : 0;
  m.trigger_patched = c.lpg != Drone;
  m.trigger = s.gate && !s.low ? 1.f : 0.f;
  s.low = false;
  // GATE and DRONE: the level follows an attack (to full in `attack`) and a release (to -60 dB in
  // `release`) on the note's gate. Every mode then ends with the note: after its end the output
  // falls to -60 dB in `release` (a ping or a drum is cut by its note length as on any tracker
  // instrument; while the note lasts the LPG and DECAY shape it as on the module).
  const float attackRate = 1 - std::exp(-3.f * float(n) / (plaits::kSampleRate * c.attack));
  const float releaseRate = 1 - std::exp(-6.9078f * float(n) / (plaits::kSampleRate * c.release));
  s.env += ((s.gate ? 1.f : 0.f) - s.env) * (s.gate ? attackRate : releaseRate);
  const float tailAttack = 1 - std::exp(-float(n) / (plaits::kSampleRate * .002f));   // (2 ms: no click on a retrigger)
  s.tail += ((s.gate ? 1.f : 0.f) - s.tail) * (s.gate ? tailAttack : releaseRate);
  m.level_patched = c.lpg == Gate;
  m.level = s.env;
  plaits::Voice::Frame frames[n];
  s.voice.Render(p, m, frames, n);
  const float g = c.gain / 32768.f * (c.lpg == Drone ? s.env : s.tail);
  for (size_t i = 0; i < n; i++) {
    const float o = frames[i].out * g, x = frames[i].aux * g;
    const float mid = o + (x - o) * c.aux, side = .5f * c.spread * c.aux * (x - o);
    l[i] = mid - side, r[i] = mid + side;
  }
}
// The level: C3 held 1.2 s, let go, over 2 s; the RMS brought to 0.12, no more than keeps the peak
// under 0.7. Run where the patch is cooked, never in the callback.
void measure(PlaitsCooked &c) {
  thread_local std::unique_ptr<PlaitsState> scratch;
  if (!scratch) scratch.reset(static_cast<PlaitsState *>(::operator new(sizeof(PlaitsState), std::align_val_t(64))));
  PlaitsState &s = *scratch;
  c.gain = 1;
  setup(s, c);
  RandomScope rs(s.random);
  strike(s, 48);
  double energy = 0;
  float peak = 1e-6f, l[plaits::kBlockSize], r[plaits::kBlockSize];
  const int blocks = int(2 * plaits::kSampleRate / plaits::kBlockSize);
  for (int b = 0; b < blocks; b++) {
    if (b == blocks * 3 / 5) s.gate = false;
    block(s, c, l, r);
    for (size_t i = 0; i < plaits::kBlockSize; i++) {
      energy += .5 * (double(l[i]) * l[i] + double(r[i]) * r[i]);
      peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
  }
  const float rms = float(std::sqrt(energy / (blocks * plaits::kBlockSize)));
  c.gain = std::clamp(std::min(rms > 1e-7f ? .12f / rms : 8.f, .7f / peak), .1f, 8.f);
}
void cook(void *out, const int32_t *val) {
  auto &c = *new (out) PlaitsCooked();
  c.seed = uint32_t(val[0]);
  c.family = familyOf(c.seed);
  Rng r(c.seed ^ 0x504c4149ull);
  draw(c, families[c.family], r);
  if (val[1]) vary(c, families[c.family], uint32_t(val[1]));
  measure(c);
}
void retrigger(void *state, int note, int) { strike(*static_cast<PlaitsState *>(state), note); }
void reset(void *) {}
void on(void *data, const void *patch, int32_t note16, int) {
  const auto &c = *static_cast<const PlaitsCooked *>(patch);
  auto &s = *static_cast<PlaitsState *>(nativeVoiceClaim(data, sizeof(PlaitsState), retrigger));
  setup(s, c);
  strike(s, note16 >> 16);
}
void off(void *data, const void *, int32_t) {
  auto *s = static_cast<PlaitsState *>(nativeVoiceState(data));
  if (s) s->gate = false, s->released = true;
}
int render(void *data, const void *patch, int32_t *left, int32_t *right) {
  auto *sp = static_cast<PlaitsState *>(nativeVoiceState(data));
  if (!sp) return 0;
  auto &s = *sp;
  const auto &c = *static_cast<const PlaitsCooked *>(patch);
  RandomScope rs(s.random);
  float l[ENG_BLK], r[ENG_BLK];
  s.resampler.process(l, r, ENG_BLK, [&](float *bl, float *br) { block(s, c, bl, br); });
  float peak = 0;
  for (int i = 0; i < ENG_BLK; i++) {
    float a = std::isfinite(l[i]) ? std::clamp(l[i], -4.f, 4.f) : 0.f, b = std::isfinite(r[i]) ? std::clamp(r[i], -4.f, 4.f) : 0.f;
    s.dcL += (a - s.dcL) * .0005f, s.dcR += (b - s.dcR) * .0005f;
    a -= s.dcL, b -= s.dcR;
    left[i] = int32_t(a * 32768), right[i] = int32_t(b * 32768);
    peak = std::max(peak, std::max(std::fabs(a), std::fabs(b)));
  }
  if (s.released && peak < 1e-4f) s.quiet++; else s.quiet = 0;   // (released and under -80 dBFS for 186 ms: ended)
  return !s.released || s.quiet < 256;
}
static_assert(sizeof(PlaitsCooked) <= ENG_CK_MAX, "PLAITS patch storage");
}
extern "C" const eng_t ENG_PLAITS = {"PLAITS", "MI MACRO OSCILLATOR", 0, 1, 0, 0, nullptr, nullptr, nullptr, nullptr, sizeof(void *), sizeof(PlaitsCooked), nullptr, cook, reset, on, off, render};
extern "C" const char *amalthea_plaits_family(unsigned seed) { return families[familyOf(seed)].name; }
extern "C" int amalthea_plaits_family_count(void) { return FamilyCount; }
extern "C" int amalthea_plaits_family_index(unsigned seed) { return familyOf(seed); }
extern "C" int amalthea_plaits_family_model(int family) { return family >= 0 && family < FamilyCount ? families[family].model : -1; }
namespace amalthea {
size_t plaitsStateBytes() { return sizeof(PlaitsState); }
}
