/*
 * anima's designs, and its eight sliders.
 *
 * A design chooses the objects (which oscillators, which noise, which
 * filter, which effects), their waveforms, and -- for every parameter -- the
 * controller of the hands that moves it, over what range and along what
 * slope; and how the hands move. Eight kinds: haze, voyage, glass, lilt,
 * tool, ostinato, bells and golden (FM and a stack on the golden ratio).
 *
 * A touch's note is the node's, or its octave or fifth, taken when the
 * finger comes down: nothing here moves pitch while a finger is down.
 */
#ifndef VASCULAR_ANIMA_REFERENCE_DESIGN_H
#define VASCULAR_ANIMA_REFERENCE_DESIGN_H

#include <initializer_list>

#include "AnimaReferenceCore.h"

namespace vascular_reference {
namespace anima {

enum Kind : int { Haze, Voyage, Glass, Lilt, Tool, Ostinato, Bells, Golden, KindCount };

inline Map M(Source s, float lo, float hi, float slope = 0.f) {
  Map m; m.source = s; m.lo = lo; m.hi = hi; m.slope = slope; return m;
}
inline Map C(float v) { return M(Const, v, v); }

inline void intervals(Design &d, std::initializer_list<int> list) {
  int i = 0;
  for (int v : list) { if (i < 6) d.intervals[i++] = v; }
  d.intervalCount = i;
}

/** A controller for a parameter, drawn: the gestures that read best as
 * movement, and the two LFOs. */
inline Source anySource(Random &r) {
  const Source pool[10] = {X, Y, Size, Speed, Distance, Angle, Age, Lfo1, Lfo2, GroupX};
  return pool[r.below(10)];
}

/** One parameter's mapping varied: sometimes another controller, its range
 * stretched or narrowed about its middle, its slope drawn. A range in hertz
 * is varied in octaves. */
inline void vary(Map &m, Random &r, bool hertz = false, float sourceChance = 0.45f) {
  if (m.source != Const && r.chance(sourceChance)) m.source = anySource(r);
  if (hertz) {
    float k = std::exp2(r.in(-0.8f, 0.8f));
    m.lo *= k; m.hi *= k * std::exp2(r.in(-0.5f, 0.5f));
  } else if (m.lo != m.hi) {
    float mid = 0.5f * (m.lo + m.hi), half = 0.5f * (m.hi - m.lo) * r.in(0.6f, 1.4f);
    m.lo = mid - half; m.hi = mid + half;
  }
  m.slope = clampTo(m.slope + r.in(-0.6f, 0.6f), -1.f, 1.f);
}

/** The variation every design is given on top of its kind's character:
 * the waveforms from a pool that suits it, objects in and out, the
 * intervals and ratios, every mapping's controller, range and slope, the
 * envelope's times. The kind decides what a sound is; this decides which
 * one of them it is. */
inline void varyDesign(Design &d, Random &r, int kind) {
  const int warm[8] = {0, 1, 3, 5, 16, 17, 20, 13};
  const int bright[8] = {2, 4, 6, 15, 18, 21, 23, 12};
  const int dense[8] = {7, 8, 9, 10, 11, 14, 19, 22};
  auto pick = [&](int base) {
    double x = r.unit();
    if (x < 0.4) return base;
    const int *pool = x < 0.6 ? warm : x < 0.8 ? bright : dense;
    return pool[r.below(8)];
  };
  d.tableA = pick(d.tableA); d.tableB = pick(d.tableB); d.tableMulti = pick(d.tableMulti);
  d.tableGran = pick(d.tableGran); d.tableAM = r.below(3) == 0 ? 2 : d.tableAM;
  if (kind != Bells && kind != Golden) d.tableFM = r.chance(0.7f) ? 0 : pick(d.tableFM);
  // Objects in and out, the kind's own kept more often than not.
  const bool held = kind == Haze || kind == Voyage || kind == Golden;
  if (!d.oscB && r.chance(0.3f)) { d.oscB = true; d.levelB = M(anySource(r), 0.1f, 0.45f); }
  if (!d.fm && r.chance(0.25f)) { d.fm = true; d.levelFM = M(anySource(r), 0.1f, 0.5f); d.fmIndex = M(anySource(r), 0.2f, 2.f); }
  if (!d.multi && r.chance(held ? 0.35f : 0.15f)) { d.multi = true; d.multiCount = 2 + r.below(3); d.levelMulti = M(anySource(r), 0.15f, 0.45f); }
  if (!d.gran && r.chance(held ? 0.35f : 0.15f)) { d.gran = true; d.levelGran = M(anySource(r), 0.05f, 0.4f); d.granRate = M(anySource(r), 5.f, 40.f); d.granLen = M(anySource(r), 20.f, 150.f); }
  if (d.noise == 0 && r.chance(0.3f)) { d.noise = 1 + r.below(4); d.levelNoise = M(anySource(r), 0.f, 0.15f); d.noiseCut = M(anySource(r), 500.f, 5000.f); }
  if (!d.ampMod && r.chance(0.25f)) { d.ampMod = true; d.amRate = M(anySource(r), 0.5f, 7.f); d.amDepth = M(anySource(r), 0.f, 0.35f); }
  // A band-pass only where the kind's range sits low enough to keep the
  // fundamental's neighbourhood; never a high-pass, which takes the note.
  if (d.filter == 1 && d.cutoff.hi < 6000.f && r.chance(0.2f)) d.filter = 2;
  if (d.filter == 0 && r.chance(0.3f)) d.filter = 1;
  d.down = d.down || r.chance(0.12f);
  if (d.down && d.downRate.lo >= 47000.f) d.downRate = M(anySource(r), 6000.f, 30000.f);
  d.shaper = d.shaper || r.chance(0.25f);
  if (d.shaper) { d.shaperCurve = r.below(3); if (d.shaperDrive.lo == d.shaperDrive.hi) d.shaperDrive = M(anySource(r), 1.f, r.in(1.5f, 3.f)); }
  // A phaser on its own clock is a regular sweep that the hands' movement
  // drowns under: kept slow and shallow, and less often.
  d.phaser = r.chance(d.phaser ? 0.55f : 0.12f);
  if (d.phaser) { d.phaserRate = M(anySource(r), 0.03f, r.in(0.08f, 0.2f)); d.phaserDepth = M(anySource(r), 0.15f, r.in(0.3f, 0.5f)); }
  d.resonator = r.chance(d.resonator ? 0.8f : 0.25f);
  if (d.resonator && d.resMix.lo == d.resMix.hi && d.resMix.lo == 0.f) { d.resMix = M(anySource(r), 0.05f, 0.35f); d.resFeedback = C(r.in(0.4f, 0.75f)); }
  if (d.groupFilter == 0 && r.chance(0.2f)) { d.groupFilter = 1; d.groupCut = M(anySource(r), 1500.f, 9000.f); d.groupQ = C(r.in(0.7f, 2.5f)); }
  // Intervals and ratios, on whole steps that sit with any key.
  const int intervalsB[6] = {0, 7, 12, 12, 19, -12};
  if (d.oscB && r.chance(0.6f)) d.intervalB = intervalsB[r.below(6)];
  if (!d.golden) {
    const float ratios[9] = {1.f, 1.5f, 2.f, 3.f, 3.5f, 4.f, 1.4f, 2.76f, 5.f};
    if (d.fm && r.chance(0.6f)) d.fmRatio = ratios[r.below(9)];
  }
  const float steps[4] = {12.f, 7.f, 5.f, 24.f};
  if (d.multi && !d.golden && r.chance(0.6f)) d.multiStep = steps[r.below(4)];
  // Every mapping varied.
  vary(d.levelA, r); vary(d.levelB, r); vary(d.levelFM, r); vary(d.levelMulti, r);
  vary(d.levelGran, r); vary(d.levelNoise, r);
  vary(d.fmIndex, r); vary(d.granRate, r); vary(d.granLen, r);
  vary(d.noiseCut, r, true); vary(d.cutoff, r, true); vary(d.q, r);
  vary(d.amRate, r); vary(d.amDepth, r); vary(d.amp, r, false, 0.15f);
  vary(d.shaperDrive, r); vary(d.phaserRate, r); vary(d.phaserDepth, r);
  d.phaserRate.lo = clampTo(d.phaserRate.lo, 0.01f, 0.25f); d.phaserRate.hi = clampTo(d.phaserRate.hi, 0.01f, 0.25f);
  d.phaserDepth.lo = clampTo(d.phaserDepth.lo, 0.f, 0.55f); d.phaserDepth.hi = clampTo(d.phaserDepth.hi, 0.f, 0.55f);
  vary(d.groupCut, r, true); vary(d.groupQ, r); vary(d.resMix, r);
  d.q.lo = std::max(d.q.lo, 0.5f); d.q.hi = clampTo(d.q.hi, 0.5f, 12.f);
  d.amp.lo = clampTo(d.amp.lo, 0.15f, 1.f); d.amp.hi = clampTo(d.amp.hi, 0.3f, 1.f);
  // The envelope's times, stretched or shortened together.
  float t = std::exp2(r.in(-0.8f, 0.8f));
  d.att *= t; d.dec *= std::exp2(r.in(-0.6f, 0.6f)); d.rel *= std::exp2(r.in(-0.6f, 0.6f));
  // And the hands' pace.
  d.travel *= std::exp2(r.in(-0.7f, 0.7f));
}

inline Design design(int kind, uint64_t seed) {
  Random r(seed ^ ((uint64_t)kind << 52));
  Design d;
  d.levelA = d.levelB = d.levelFM = d.levelMulti = d.levelGran = d.levelNoise = C(0.f);
  d.fmIndex = C(0.f); d.granRate = C(12.f); d.granLen = C(60.f); d.noiseCut = C(2000.f);
  d.cutoff = C(6000.f); d.q = C(0.8f); d.amRate = C(3.f); d.amDepth = C(0.f); d.amp = C(0.8f);
  d.downRate = C(48000.f); d.shaperDrive = C(1.f); d.phaserRate = C(0.2f); d.phaserDepth = C(0.5f);
  d.groupCut = C(9000.f); d.groupQ = C(0.7f); d.resFeedback = C(0.5f); d.resMix = C(0.f);

  switch (kind) {
    case Haze:   // a breathed, ghostly pad
      d.oscA = true; d.tableA = r.chance(0.5f) ? 22 : 20;
      d.oscB = true; d.tableB = r.chance(0.5f) ? 0 : 17; d.intervalB = 12; d.detuneB = r.in(4.f, 9.f);
      d.noise = 3; d.levelNoise = M(Speed, 0.02f, 0.25f); d.noiseCut = M(X, 600.f, 3000.f, -0.5f);
      d.levelA = C(0.6f); d.levelB = M(Distance, 0.15f, 0.55f);
      d.filter = 1; d.cutoff = M(Y, 400.f, r.in(2500.f, 4000.f), -0.6f); d.q = M(Size, 0.8f, 2.2f);
      d.amp = M(Size, 0.35f, 0.9f, 0.3f);
      d.att = r.in(900.f, 1800.f); d.dec = 1500.f; d.sus = 0.8f; d.rel = r.in(2500.f, 4000.f);
      d.phaser = r.chance(0.7f); d.phaserRate = M(Lfo2, 0.05f, 0.3f); d.phaserDepth = C(0.6f);
      d.motion = r.chance(0.5f) ? Holds : Drags; d.fingers = 3; d.spawn = 0.25f;
      d.durMin = 5.f; d.durMax = 12.f; d.travel = 0.05f;
      intervals(d, {0, 0, 12, 7});
      d.out = 0.5f;
      break;
    case Voyage:   // a slowly travelling, grainy stack
      d.multi = true; d.tableMulti = 8 + r.below(3); d.multiCount = 3 + r.below(2); d.multiStep = r.chance(0.5f) ? 12.f : 7.f;
      d.levelMulti = M(Distance, 0.6f, 0.25f);
      d.gran = true; d.tableGran = r.chance(0.5f) ? 11 : 18; d.levelGran = M(Speed, 0.1f, 0.6f);
      d.granRate = M(X, 4.f, 30.f, -0.5f); d.granLen = M(Y, 30.f, 200.f);
      d.filter = 1; d.cutoff = M(Age, 700.f, 5000.f, -0.4f); d.q = C(1.2f);
      d.ampMod = r.chance(0.5f); d.tableAM = 1; d.amRate = M(Lfo1, 0.3f, 2.f); d.amDepth = C(0.25f);
      d.amp = M(Size, 0.4f, 0.85f);
      d.att = r.in(1200.f, 2500.f); d.dec = 2000.f; d.sus = 0.75f; d.rel = r.in(3000.f, 5000.f);
      d.shaper = r.chance(0.4f); d.shaperDrive = M(GroupX, 1.f, 2.2f);
      d.phaser = true; d.phaserRate = M(Lfo2, 0.05f, 0.25f);
      d.resonator = r.chance(0.5f); d.resMix = M(Lfo2, 0.f, 0.3f); d.resFeedback = C(0.6f);
      d.motion = r.chance(0.5f) ? Sweeps : Orbits; d.fingers = 3; d.spawn = 0.2f;
      d.durMin = 6.f; d.durMax = 14.f; d.travel = 0.08f;
      intervals(d, {0, 12, 7, 0});
      d.out = 0.5f;
      break;
    case Glass:   // bright, crystalline taps
      d.oscA = true; d.tableA = r.chance(0.5f) ? 6 : 15; d.levelA = C(0.5f);
      d.fm = true; d.fmRatio = r.chance(0.5f) ? 3.f : 3.5f; d.levelFM = C(0.6f); d.fmIndex = M(Size, 0.2f, 2.5f, 0.3f);
      d.filter = 1; d.cutoff = M(X, 2500.f, 9000.f, -0.3f); d.q = M(Y, 1.f, 4.f);
      d.amp = M(Size, 0.4f, 0.9f);
      d.att = r.in(3.f, 20.f); d.dec = r.in(600.f, 1500.f); d.sus = 0.15f; d.rel = 1500.f;
      d.resonator = true; d.resMix = C(r.in(0.12f, 0.3f)); d.resFeedback = C(0.6f);
      d.motion = Taps; d.fingers = 4; d.spawn = 1.2f; d.travel = 0.1f;
      intervals(d, {0, 12, 7, 19});
      d.out = 0.45f;
      break;
    case Lilt:   // a gentle, swaying melody voice
      d.oscA = true; d.tableA = r.chance(0.5f) ? 17 : 16; d.levelA = C(0.7f);
      d.oscB = true; d.tableB = 1; d.intervalB = 12; d.levelB = M(Y, 0.1f, 0.4f);
      d.filter = 1; d.cutoff = M(Size, 800.f, 4000.f, -0.4f); d.q = C(1.f);
      d.ampMod = true; d.tableAM = 0; d.amRate = C(r.in(4.f, 6.f)); d.amDepth = M(Y, 0.f, 0.3f);
      d.amp = M(Size, 0.45f, 0.9f);
      d.att = 6.f; d.dec = r.in(300.f, 700.f); d.sus = 0.3f; d.rel = 700.f;
      d.phaser = r.chance(0.3f);
      d.motion = r.chance(0.5f) ? Taps : Drags; d.fingers = 3; d.spawn = 0.9f;
      d.durMin = 0.4f; d.durMax = 1.4f; d.travel = 0.2f;
      intervals(d, {0, 7, 12, 0});
      d.out = 0.45f;
      break;
    case Tool:   // a buzzy, hands-on synth
      d.oscA = true; d.tableA = 2; d.levelA = C(0.6f);
      d.oscB = true; d.tableB = 4; d.intervalB = 0; d.detuneB = r.in(6.f, 12.f); d.levelB = C(0.5f);
      d.filter = 1; d.cutoff = M(X, 300.f, 6000.f, -0.5f); d.q = M(Y, 0.7f, 6.f);
      d.amp = M(Size, 0.4f, 0.85f);
      d.att = 10.f; d.dec = 500.f; d.sus = 0.6f; d.rel = 400.f;
      d.shaper = true; d.shaperCurve = r.below(2); d.shaperDrive = M(GroupX, 1.f, 2.6f);
      d.down = r.chance(0.3f); d.downRate = M(Lfo1, 12000.f, 48000.f);
      d.motion = Drags; d.fingers = 2; d.spawn = 0.5f; d.durMin = 1.5f; d.durMax = 4.f; d.travel = 0.25f;
      intervals(d, {0, 12, 0});
      d.out = 0.35f;
      break;
    case Ostinato:   // a dark, insistent figure
      d.oscA = true; d.tableA = r.chance(0.5f) ? 13 : 21; d.levelA = C(0.6f);
      d.fm = true; d.fmRatio = 2.f; d.levelFM = C(0.4f); d.fmIndex = M(Lfo1, 0.5f, 2.f);
      d.filter = 1; d.cutoff = M(Age, 1500.f, 200.f, 0.4f); d.q = C(r.in(2.f, 5.f));
      d.amp = M(Size, 0.5f, 0.9f);
      d.att = 3.f; d.dec = r.in(200.f, 400.f); d.sus = 0.f; d.rel = 300.f;
      d.resonator = true; d.resMix = C(0.2f); d.resFeedback = C(0.7f);
      d.shaper = true; d.shaperDrive = C(1.6f);
      d.groupFilter = 1; d.groupCut = M(Lfo2, 800.f, 3500.f); d.groupQ = C(1.5f);
      d.motion = Taps; d.fingers = 3; d.spawn = 2.5f; d.travel = 0.1f;
      intervals(d, {0, 0, 7, -12});
      d.octave = -1;
      d.out = 0.5f;
      break;
    case Bells:   // struck FM bells
      d.fm = true; d.tableFM = 0; d.levelFM = C(0.8f);
      { const float ratios[4] = {1.4f, 2.76f, 3.5f, 1.5f}; d.fmRatio = ratios[r.below(4)]; }
      d.fmIndex = M(Age, 4.f, 0.4f, 0.5f);
      d.oscA = true; d.tableA = 12; d.levelA = C(0.25f);
      d.filter = 0;
      d.amp = M(Size, 0.4f, 0.9f);
      d.att = 2.f; d.dec = r.in(2000.f, 4000.f); d.sus = 0.f; d.rel = 3000.f;
      d.phaser = r.chance(0.3f);
      d.motion = Taps; d.fingers = 5; d.spawn = 0.8f; d.travel = 0.05f;
      intervals(d, {0, 7, 12, 19});
      d.out = 0.4f;
      break;
    case Golden:   // inharmonic FM and a stack, both on the golden ratio
    default:
      d.golden = true;
      d.fm = true; d.tableFM = 0; d.levelFM = C(0.7f); d.fmIndex = M(Size, 0.5f, 3.f, -0.3f);
      d.multi = true; d.tableMulti = 0; d.multiCount = 3 + r.below(3); d.levelMulti = M(Distance, 0.2f, 0.5f);
      d.filter = 1; d.cutoff = M(Y, 2000.f, 9000.f); d.q = C(0.7f);
      d.amp = M(Size, 0.4f, 0.85f);
      d.att = r.in(1500.f, 4000.f); d.dec = 3000.f; d.sus = 0.6f; d.rel = r.in(4000.f, 7000.f);
      d.phaser = r.chance(0.3f);
      d.motion = Holds; d.fingers = 4; d.spawn = 0.15f; d.durMin = 8.f; d.durMax = 16.f; d.travel = 0.03f;
      intervals(d, {0, 0, 12});
      d.out = 0.4f;
      break;
  }
  varyDesign(d, r, kind);
  // Each kind brought to about 0.1 RMS, measured over ten seeds of each,
  // droning, every slider at its middle.
  static const float level[KindCount] = {0.95f, 2.2f, 1.2f, 2.2f, 0.7f, 1.5f, 1.5f, 1.f};
  d.out *= level[clampTo(kind, 0, (int)KindCount - 1)];
  d.lfo1 = r.in(0.1f, 0.6f);
  d.lfo2 = r.in(0.03f, 0.12f);
  d.lfo1Shape = r.below(3);
  d.lfo2Shape = 2;
  return d;
}

inline void scale(Map &m, float k) { m.lo *= k; m.hi *= k; }

/** The node's eight sliders on a design: tone, resonance, fm, grain,
 * texture, space, motion and density. Each slider's middle is the design.
 * Motion and density go to the hands rather than the design. */
/** The mod matrix: what the hands play. Three to six routes, each from a
 * gesture to a part of the sound, over a range wide enough to be heard
 * travelling -- dealt from the hands' own seed, so the mod die changes
 * which gestures move what, the sound die what they move it from. A part
 * no route reaches keeps the design's own mapping. */
inline void route(Design &d, uint64_t seed) {
  Random r(seed * 0x9E3779B97F4A7C15ull + 0x5EED);
  // A finger's own gestures, for what each voice does: where it is, how
  // hard, how fast; and, for what the whole hand does, where it is, how
  // many fingers are down, its hardest press, its quickest finger.
  const Source voice[10] = {X, Y, Size, Speed, Distance, Angle, X, Y, Count, Lfo1};
  const Source hand[7] = {GroupX, GroupY, Count, Size, Speed, GroupX, Lfo2};
  auto hz = [&](Map &m, float centre, float octaves, Source src) {
    centre = clampTo(centre, 250.f, 4000.f);
    m = M(src, std::max(centre * std::exp2(-octaves), 80.f), std::min(centre * std::exp2(octaves), 17000.f), r.in(-0.4f, 0.4f));
    if (r.chance(0.3f)) std::swap(m.lo, m.hi);
  };
  auto span = [&](Map &m, float lo, float hi, Source src) {
    m = M(src, lo, hi, r.in(-0.5f, 0.5f));
    if (r.chance(0.3f)) std::swap(m.lo, m.hi);
  };
  enum { Cutoff, Peak, Fm, Grains, Noise, Second, Tremolo, Stack, Drive, Ring, Phase, Colour, Crush, Targets };
  bool taken[Targets] = {};
  const int routes = 4 + r.below(4);
  // The first route is the filter, from where a finger is: sliding is heard
  // as the sound opening and closing. The second is from pressing and from
  // fingers landing: something in the sound that jumps when a finger does.
  const Source where[4] = {X, Y, X, GroupY};
  const Source landing[3] = {Size, Count, Count};
  const int sudden[6] = {Fm, Drive, Ring, Second, Grains, Noise};
  for (int n = 0, tries = 0; n < routes && tries < 40; ++tries) {
    int t = n == 0 ? Cutoff : n == 1 ? sudden[r.below(6)] : r.below(Targets);
    if (t == Crush && r.chance(0.6f)) t = Drive;     // crushing, rarely
    if (taken[t]) continue;
    taken[t] = true;
    Source v = voice[r.below(10)], h = hand[r.below(7)];
    if (n == 0) v = h = where[r.below(4)];
    if (n == 1) v = h = landing[r.below(3)];
    ++n;
    switch (t) {
      case Cutoff:
        // A low-pass the hand opens and closes: a band-pass swept across
        // the surface loses the note whenever it strays from it.
        d.filter = 1;
        hz(d.cutoff, std::sqrt(std::max(d.cutoff.lo, 60.f) * std::max(d.cutoff.hi, 60.f)), r.in(2.5f, 3.5f), v);
        d.cutoff.lo = std::max(d.cutoff.lo, 150.f); d.cutoff.hi = std::max(d.cutoff.hi, 150.f);
        break;
      case Peak: span(d.q, 0.7f, r.in(5.f, 12.f), v); break;
      case Fm:
        if (!d.fm) { d.fm = true; d.tableFM = 0; d.levelFM = C(0.45f); }
        span(d.fmIndex, 0.f, r.in(3.f, 6.f), v);
        break;
      case Grains:
        if (!d.gran) { d.gran = true; d.granLen = M(Y, 30.f, 120.f); }
        span(d.levelGran, 0.f, r.in(0.4f, 0.7f), v);
        span(d.granRate, 4.f, r.in(25.f, 60.f), voice[r.below(10)]);
        break;
      case Noise:
        if (d.noise == 0) { d.noise = 3; d.noiseCut = M(X, 1200.f, 6000.f); }
        span(d.levelNoise, 0.f, r.in(0.12f, 0.25f), v);
        break;
      case Second:
        if (!d.oscB) { d.oscB = true; d.intervalB = r.chance(0.5f) ? 12 : 7; }
        span(d.levelB, 0.f, r.in(0.4f, 0.7f), v);
        break;
      case Tremolo:
        d.ampMod = true;
        span(d.amDepth, 0.f, r.in(0.5f, 0.85f), v);
        span(d.amRate, r.in(1.f, 3.f), r.in(6.f, 12.f), voice[r.below(10)]);
        break;
      case Stack:
        if (!d.multi) { d.multi = true; d.multiCount = 3; }
        span(d.levelMulti, 0.f, r.in(0.35f, 0.6f), v);
        break;
      case Drive:
        d.shaper = true;
        span(d.shaperDrive, 1.f, r.in(3.f, 6.f), h);
        break;
      case Ring:
        if (!d.resonator) { d.resonator = true; d.resFeedback = C(r.in(0.55f, 0.8f)); }
        span(d.resMix, 0.f, r.in(0.35f, 0.55f), h);
        break;
      case Phase:
        // The sweep placed by the hand: a finger sliding is the notch moving.
        d.phaser = true; d.phaserByHand = true;
        d.phaserDepth = C(r.in(0.8f, 1.f));
        span(d.phaserPlace, 0.f, 1.f, h);
        break;
      case Colour:
        d.groupFilter = 1; d.groupQ = C(r.in(0.8f, 2.5f));
        hz(d.groupCut, r.in(1500.f, 5000.f), r.in(1.2f, 2.f), h);
        break;
      case Crush:
        d.down = true;
        span(d.downRate, 4000.f, r.in(20000.f, 40000.f), h);
        break;
    }
  }
}

inline Design applySliders(const Design &base, const float s[8]) {
  Design d = base;
  // Every slider acts on every design: in its middle the design as drawn;
  // towards either end the design pushed, and where the design has no
  // object for a slider to move, the slider brings one in above its middle.
  auto up = [](float v) { return clampTo((v - 0.5f) * 2.f, 0.f, 1.f); };
  // tone: the filters, two octaves either way. A design without a voice
  // filter is given an open low-pass, so darkening it is heard.
  if (d.filter == 0) { d.filter = 1; d.cutoff = M(Const, 16000.f, 16000.f); d.q = C(0.7f); }
  float tone = std::exp2((s[0] - 0.5f) * 4.f);
  scale(d.cutoff, tone); scale(d.groupCut, tone); scale(d.noiseCut, tone);
  // resonance: the filters' Q, and above the middle a floor that rises to a
  // ringing peak; the resonator rings longer with it.
  float res = std::exp2((s[1] - 0.5f) * 3.f), floorQ = 0.5f + std::pow(up(s[1]), 1.5f) * 14.f;
  scale(d.q, res); scale(d.groupQ, res);
  d.q.lo = std::max(d.q.lo, floorQ); d.q.hi = std::max(d.q.hi, floorQ);
  d.resFeedback.lo = clampTo(d.resFeedback.lo + (s[1] - 0.5f) * 0.4f, 0.f, 0.92f);
  d.resFeedback.hi = clampTo(d.resFeedback.hi + (s[1] - 0.5f) * 0.4f, 0.f, 0.92f);
  // fm: the index, from none to twice the design's; one brought in above
  // the middle where the design has none.
  if (!d.fm && up(s[2]) > 0.f) {
    d.fm = true; d.tableFM = 0;
    d.levelFM = C(0.6f * up(s[2])); d.fmIndex = M(Size, 0.4f, 1.6f);
    d.levelA.lo *= 1.f - 0.5f * up(s[2]); d.levelA.hi *= 1.f - 0.5f * up(s[2]);
  }
  scale(d.fmIndex, s[2] * 2.f);
  // grain: the clouds and the stacked copies; grains brought in where the
  // design has none, thicker and quicker towards the top.
  if (!d.gran && up(s[3]) > 0.f) {
    d.gran = true; d.levelGran = C(0.5f * up(s[3]));
    d.granRate = M(Speed, 8.f, 30.f); d.granLen = M(Y, 30.f, 120.f);
  }
  scale(d.levelGran, s[3] * 2.f); scale(d.levelMulti, 0.5f + s[3]);
  d.granSpread = clampTo(d.granSpread + up(s[3]) * 0.75f, 0.f, 1.f);
  // and the clouds take over from the steady tone as they thicken.
  float steady = 1.f - 0.55f * up(s[3]);
  scale(d.levelA, steady); scale(d.levelB, steady);
  scale(d.granRate, std::exp2((s[3] - 0.5f) * 1.6f));
  // texture: noise and the shaper's drive, both brought in above the middle.
  if (d.noise == 0 && up(s[4]) > 0.f) { d.noise = 3; d.levelNoise = M(Size, 0.f, 0.12f * up(s[4])); d.noiseCut = M(X, 1500.f, 6000.f); }
  else scale(d.levelNoise, s[4] * 2.f);
  if (!d.shaper && up(s[4]) > 0.f) { d.shaper = true; d.shaperCurve = 0; d.shaperDrive = C(1.f); }
  float texture = s[4] * 2.f;
  d.shaperDrive.lo = 1.f + (d.shaperDrive.lo - 1.f) * texture + up(s[4]) * 3.f;
  d.shaperDrive.hi = 1.f + (d.shaperDrive.hi - 1.f) * texture + up(s[4]) * 3.f;
  // space: the resonator and the phaser, both brought in above the middle.
  if (!d.resonator && up(s[5]) > 0.f) { d.resonator = true; d.resMix = C(0.f); d.resFeedback = C(0.6f); }
  float space = s[5] * 2.f;
  d.resMix.lo = clampTo(d.resMix.lo * space + up(s[5]) * 0.3f, 0.f, 0.6f);
  d.resMix.hi = clampTo(d.resMix.hi * space + up(s[5]) * 0.3f, 0.f, 0.6f);
  if (!d.phaser && s[5] > 0.65f) { d.phaser = true; d.phaserRate = M(Lfo2, 0.04f, 0.15f); d.phaserDepth = C(0.f); }
  scale(d.phaserDepth, space);
  d.phaserDepth.lo += up(s[5]) * 0.5f; d.phaserDepth.hi += up(s[5]) * 0.5f;
  return d;
}

}  // namespace anima
}  // namespace vascular_reference

#endif
