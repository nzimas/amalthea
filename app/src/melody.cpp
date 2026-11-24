// SPDX-License-Identifier: GPL-3.0-only
// NOTE-column pattern generator. Replaces the Fugitive fugue-subject adapter,
// whose tonal cadences, fixed 16-step template and copied opening cell made
// every draw sound alike. Each draw picks an idiom (riff, pedal point, drone,
// lament, stab, ...), an independent rhythm model, a register, a harmonic
// motion and a set of mutations, so successive draws differ in kind and not
// only in detail. Everything is expressed in scale degrees and resolved to the
// project's root/scale at the end, so the output never leaves the scale.
#include "melody.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>

namespace amalthea {
namespace {
constexpr int MaxLen = 16;

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 0x6d2b79f5u) {
    for (int i = 0; i < 3; i++) next();
  }
  uint32_t next() {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    uint32_t z = s * 0x9e3779bbu;
    return z ^ (z >> 15);
  }
  int below(int n) { return n <= 1 ? 0 : int((uint64_t(next()) * uint32_t(n)) >> 32); }
  bool chance(int pct) { return below(100) < pct; }
  int range(int lo, int hi) { return lo + below(hi - lo + 1); }
  template <class T, size_t N> T pick(const T (&a)[N]) { return a[below(int(N))]; }
  int weighted(const int *w, int n) {
    int t = 0;
    for (int i = 0; i < n; i++) t += std::max(0, w[i]);
    int x = below(t);
    for (int i = 0; i < n; i++) { if (x < std::max(0, w[i])) return i; x -= std::max(0, w[i]); }
    return n - 1;
  }
};

struct Scale {
  int pcs[12]{}, n = 0;
  explicit Scale(uint16_t mask) {
    for (int i = 0; i < 12; i++) if (mask & (1 << i)) pcs[n++] = i;
    if (!n) pcs[n++] = 0;
  }
  int semis(int d) const {
    int o = d >= 0 ? d / n : -((-d + n - 1) / n);
    return o * 12 + pcs[d - o * n];
  }
  // degree (0 .. n-1) whose pitch class is closest to pc
  int near(int pc) const {
    int best = 0, bd = 99;
    for (int i = 0; i < n; i++) {
      int d = std::abs(pcs[i] - pc); d = std::min(d, 12 - d);
      if (d < bd) { bd = d; best = i; }
    }
    return best;
  }
  // the degree above d closest to `interval` semitones above it
  int above(int d, int interval) const {
    int base = semis(d), best = d + 1, bd = 99;
    for (int k = d + 1; k <= d + n + 1; k++) {
      int x = std::abs(semis(k) - base - interval);
      if (x < bd) { bd = x; best = k; }
    }
    return best;
  }
  bool has(int pc) const { for (int i = 0; i < n; i++) if (pcs[i] == pc) return true; return false; }
};

// A progression as pitch classes; mapped to the nearest scale degree, so the
// same shape works in every scale. Kept minor/modal and tense on purpose.
const int8_t Progressions[][4] = {
  {0, 0, 0, 0}, {0, 8, 10, 7}, {0, 10, 8, 7}, {0, 1, 0, 1}, {0, 5, 8, 7},
  {0, 0, 8, 10}, {0, 3, 10, 8}, {0, 6, 0, 6}, {0, 7, 5, 8}, {0, 8, 0, 5},
  {0, 0, 1, 0}, {0, 10, 0, 8},
};

enum Rhythm { Euclid, Metric, Offbeat, Cluster, Scatter, Anchor };

void chooseRows(Rng &r, Rhythm rhythm, int len, int count, int period, bool *on) {
  std::array<int, MaxLen> score{};
  switch (rhythm) {
  case Euclid: {
    int k = std::clamp(count, 1, len), rot = r.below(len);
    for (int i = 0; i < len; i++) score[(i + rot) % len] = ((i * k) % len < k ? 1000 : 0) + r.below(100);
    break;
  }
  case Metric: case Offbeat:
    for (int i = 0; i < len; i++) {
      int level = i % 8 == 0 ? 4 : i % 4 == 0 ? 3 : i % 2 == 0 ? 2 : 1;
      if (rhythm == Offbeat) level = i == 0 && r.chance(40) ? 4 : 5 - level;
      score[i] = level * 200 + r.below(380);
    }
    break;
  case Cluster: {
    int centres[3], nc = r.range(1, 3);
    for (int c = 0; c < nc; c++) centres[c] = r.below(len);
    for (int i = 0; i < len; i++) {
      int d = 99;
      for (int c = 0; c < nc; c++) { int x = std::abs(i - centres[c]); d = std::min({d, x, len - x}); }
      score[i] = 1000 - 170 * d + r.below(140);
    }
    break;
  }
  case Scatter:
    for (int i = 0; i < len; i++) score[i] = r.below(1000);
    break;
  case Anchor:
    for (int i = 0; i < len; i++) score[i] = r.below(700);
    score[0] = 5000;
    if (len >= 8 && r.chance(50)) score[len / 2] += 900;
    break;
  }
  // A repeating rhythm cell (riffs), including odd periods against the bar.
  if (period > 1 && period < len)
    for (int i = period; i < len; i++) score[i] = score[i % period] + r.below(60) - 30;
  std::array<int, MaxLen> order{};
  for (int i = 0; i < len; i++) order[i] = i;
  std::stable_sort(order.begin(), order.begin() + len, [&](int a, int b) { return score[a] > score[b]; });
  for (int i = 0; i < len; i++) on[i] = false;
  for (int i = 0; i < count && i < len; i++) on[order[i]] = true;
}

enum Idiom { Ostinato, Pedal, Drone, Walk, Lament, Stab, Answer, Arp, Spiral, IdiomCount };
const char *const IdiomNames[IdiomCount] = {"OSTINATO", "PEDAL POINT", "DRONE", "WALK", "LAMENT",
                                             "STAB", "CALL/ANSWER", "ARP", "SPIRAL"};

struct Context {
  Rng &r;
  const Scale &sc;
  int len, m;            // rows, onsets
  const int *rows;       // onset rows
  int F, T, O;           // fifth, third, octave (degrees)
  int dark[12], nd;      // tension degrees (b2, tritone, b6 ... as available)
  int darkDeg() { return dark[r.below(nd)]; }
  int progShift(const int8_t *prog, int segs, int i, bool low) {
    int seg = std::min(segs - 1, rows[i] * segs / len);
    int d = sc.near(prog[seg] % 12);
    if (low && sc.pcs[d] > 7) d -= O;    // VI / VII under the root: heavier
    return d;
  }
};

int walkStep(Context &c, int last) {
  static const int w[] = {8, 26, 26, 13, 13, 5, 5, 3, 3, 2};
  static const int s[] = {0, 1, -1, 2, -2, 3, -3, 0, 0, 0};
  int k = c.r.weighted(w, 10);
  int step = k == 7 ? c.F : k == 8 ? -c.F : k == 9 ? (c.r.chance(50) ? c.O : -c.O) : s[k];
  if (std::abs(last) >= 3 && step * last > 0) step = last > 0 ? -c.r.range(1, 2) : c.r.range(1, 2);
  return step;
}

void walkLine(Context &c, int *deg, int from, int to, int start, int target) {
  int cur = start, last = 0, lo = -c.F - 1, hi = c.O + c.F;
  for (int i = from; i < to; i++) {
    if (i == to - 1 && to - from > 2 && c.r.chance(65)) {
      int t = target;
      while (t - cur > c.O / 2 + 1) t -= c.O;
      while (cur - t > c.O / 2 + 1) t += c.O;
      cur = t;
    }
    deg[i] = cur;
    last = walkStep(c, last);
    cur += last;
    if (cur > hi) cur -= 2 * std::abs(last) + 1;
    if (cur < lo) cur += 2 * std::abs(last) + 1;
    cur = std::clamp(cur, lo, hi);
  }
}

void ostinato(Context &c, int *deg) {
  int L = std::min(c.m, c.r.range(2, 5));
  int pool[] = {0, 0, 0, c.F, c.O, c.T, -1, 1, c.F - c.O, c.darkDeg(), c.O + c.F};
  int cell[5];
  for (int j = 0; j < L; j++) cell[j] = c.r.pick(pool);
  if (c.r.chance(70)) cell[0] = 0;
  const int8_t *prog = Progressions[c.r.below(int(sizeof Progressions / sizeof *Progressions))];
  int segs = c.len >= 8 ? c.r.pick({2, 4, 4}) : 1, octave = c.r.pick({0, 0, 12, 25, 40});
  for (int i = 0; i < c.m; i++)
    deg[i] = cell[i % L] + c.progShift(prog, segs, i, true) + (c.r.chance(octave) ? c.O : 0);
}

void pedal(Context &c, int *deg) {
  int p = c.r.chance(70) ? 0 : c.F - (c.r.chance(50) ? c.O : 0);
  int mode = c.r.below(3), up = c.O + c.r.pick({0, 0, c.T, c.F, -1});
  for (int i = 0; i < c.m; i++) {
    bool pedalHit = mode == 0 ? c.rows[i] % 4 == 0 : mode == 1 ? i % 2 == 0 : (i % 2 == 0) != c.r.chance(20);
    if (pedalHit) { deg[i] = p; continue; }
    deg[i] = up;
    up += c.r.pick({-2, -1, -1, 1, 1, 2, 0, 3});
    up = std::clamp(up, p + 2, p + c.O + c.F);
  }
}

void drone(Context &c, int *deg) {
  int pool[] = {0, 0, c.F, -c.O, c.O, c.darkDeg(), 1, c.F - c.O, -1, c.darkDeg() - c.O};
  deg[0] = c.r.chance(75) ? 0 : c.F;
  for (int i = 1; i < c.m; i++) deg[i] = c.r.chance(35) ? deg[i - 1] : c.r.pick(pool);
}

void walk(Context &c, int *deg) {
  int start = c.r.pick({0, 0, c.T, c.F, c.O, -1});
  int target = c.r.pick({0, 0, c.F, c.T, -1, c.darkDeg()});
  walkLine(c, deg, 0, c.m, start, target);
}

void lament(Context &c, int *deg) {
  int dir = c.r.chance(70) ? -1 : 1;
  int start = dir < 0 ? c.r.pick({c.O, c.F, c.O + c.T}) : c.r.pick({0, -1, c.F - c.O});
  int span = c.r.range(4, 9), cur = start;
  bool bounce = c.r.chance(40);
  for (int i = 0; i < c.m; i++) {
    deg[i] = cur;
    if (c.r.chance(20)) continue;
    cur += dir * (c.r.chance(12) ? 2 : 1);
    if (std::abs(cur - start) > span) {
      if (bounce) dir = -dir; else cur = start;
    }
  }
}

void stab(Context &c, int *deg) {
  int pool[3] = {0, c.r.pick({c.darkDeg(), c.O, -c.O, c.F, 1, c.darkDeg()}), c.darkDeg() + (c.r.chance(50) ? c.O : 0)};
  int n = c.r.range(2, 3), disp = c.r.range(10, 40);
  for (int i = 0; i < c.m; i++) {
    int k = c.r.chance(50) ? 0 : c.r.below(n);
    deg[i] = pool[k] + (c.r.chance(disp) ? (c.r.chance(50) ? c.O : -c.O) : 0);
  }
}

void answer(Context &c, int *deg) {
  int half = c.len / 2, a = 0;
  while (a < c.m && c.rows[a] < half) a++;
  if (a == 0 || a == c.m) { walk(c, deg); return; }
  walkLine(c, deg, 0, a, c.r.pick({0, c.T, c.F, c.O}), c.r.pick({c.F, c.T, -1, c.darkDeg()}));
  int mode = c.r.below(4), shift = c.r.pick({-2, -1, 1, 2, c.F, -c.F + 1});
  for (int i = a; i < c.m; i++) {
    int j = (i - a) % a, x = deg[j];
    if (mode == 1) x += shift;
    else if (mode == 2) x = 2 * deg[0] - x;
    else if (mode == 3) x = deg[a - 1 - j];
    deg[i] = x;
  }
  if (mode == 0 && c.r.chance(70)) deg[c.m - 1] = c.r.pick({0, 0, -1, c.F - c.O});
}

void arp(Context &c, int *deg) {
  const int8_t *prog = Progressions[c.r.below(int(sizeof Progressions / sizeof *Progressions))];
  int segs = c.len >= 8 ? c.r.pick({1, 2, 4}) : 1, pattern = c.r.below(5), octaves = c.r.range(1, 2);
  int extra = c.r.below(4);   // none, 7th, 9th, octave
  for (int i = 0; i < c.m; i++) {
    int root = c.progShift(prog, segs, i, false);
    int tones[10], n = 0;
    for (int o = 0; o < octaves; o++) {
      int b = root + o * c.O;
      tones[n++] = b; tones[n++] = c.sc.above(b, 3); tones[n++] = c.sc.above(b, 7);
      if (extra == 1) tones[n++] = c.sc.above(b, 10);
      if (extra == 2) tones[n++] = c.sc.above(b, 14);
    }
    if (extra == 3) tones[n++] = root + octaves * c.O;
    int cyc = 2 * n - 2, k = i;
    switch (pattern) {
    case 0: k = i % n; break;
    case 1: k = n - 1 - i % n; break;
    case 2: k = cyc > 0 ? (i % cyc < n ? i % cyc : cyc - i % cyc) : 0; break;
    case 3: k = c.r.below(n); break;
    default: k = i % 2 == 0 ? 0 : 1 + (i / 2) % std::max(1, n - 1); break;   // pinned root
    }
    deg[i] = tones[k];
  }
}

void spiral(Context &c, int *deg) {
  int L = c.r.range(3, 4), cell[4] = {0, 0, 0, 0};
  for (int j = 1; j < L; j++) cell[j] = cell[j - 1] + c.r.pick({-2, -1, 1, 1, 2, -1});
  int step = c.r.pick({1, -1, 2, -2, 1, -1}), turns = std::max(1, c.r.range(2, 4));
  for (int i = 0; i < c.m; i++) deg[i] = cell[i % L] + ((i / L) % turns) * step;
}

int kitPattern(Rng &r, const Scale &sc, int root, int len, int count, int16_t *notes, const bool *forced) {
  constexpr int Kick = 60, Snare = 62, Hat = 64, Open = 66, Cymbal = 68, Perc = 70;
  int style = r.below(5);
  std::array<int, MaxLen> piece{}, score{};
  auto tom = [&]() {
    int base = 36 + root;
    int n = base + sc.semis(r.below(sc.n * 3 / 2 + 1));
    return std::min(n, 59);
  };
  int kicks = r.range(3, 6), kickRot = r.below(3);
  for (int i = 0; i < len; i++) {
    int noise = r.below(160);
    switch (style) {
    case 0:   // EBM: driving kick, backbeat, offbeat metal
      if (i % 8 == 4) { piece[i] = r.chance(60) ? Snare : Perc; score[i] = 880; }
      else if (i % 4 == 0) { piece[i] = Kick; score[i] = 900; }
      else if (i % 2 == 1) { piece[i] = Hat; score[i] = 420 + noise; }
      else { piece[i] = r.chance(50) ? Open : Hat; score[i] = 300 + noise; }
      break;
    case 1:   // industrial: euclidean kick, displaced snare, low toms, metal
      if (((i + kickRot) * kicks) % len < kicks) { piece[i] = Kick; score[i] = 900 + noise; }
      else if (i % 8 == 6 || (i == len - 2)) { piece[i] = Snare; score[i] = 800; }
      else { piece[i] = r.pick({Cymbal, Perc, 0, 0, Hat}); if (!piece[i]) piece[i] = tom(); score[i] = 250 + noise * 2; }
      break;
    case 2:   // rhythmic noise: dense kick / snare stacking, perc bursts
      if (i % 2 == 0) { piece[i] = r.chance(70) ? Kick : Snare; score[i] = 700 + noise; }
      else { piece[i] = r.pick({Perc, Perc, Snare, Cymbal, Kick}); score[i] = 350 + noise * 2; }
      break;
    case 3:   // ritual: toms carry it, sparse kick, rare metal
      if (i == 0 || (i == len / 2 && r.chance(60))) { piece[i] = Kick; score[i] = 950; }
      else { piece[i] = r.chance(75) ? tom() : r.pick({Cymbal, Perc, Open}); score[i] = (i % 2 ? 300 : 500) + noise * 2; }
      break;
    default:  // broken: syncopated kick, ghosted snare, ticking hats
      if (i == 0 || i == 3 || i == 10 || (i == 7 && r.chance(40))) { piece[i] = Kick; score[i] = 900; }
      else if (i == 4 || i == 12) { piece[i] = Snare; score[i] = 870; }
      else { piece[i] = r.chance(70) ? Hat : r.pick({Snare, Perc, Open}); score[i] = 200 + noise * 3; }
      break;
    }
  }
  std::array<int, MaxLen> order{};
  for (int i = 0; i < len; i++) order[i] = i;
  std::stable_sort(order.begin(), order.begin() + len, [&](int a, int b) { return score[a] > score[b]; });
  for (int i = 0; i < len; i++) notes[i] = -1;
  if (forced) for (int i = 0; i < len; i++) { if (forced[i]) notes[i] = int16_t(piece[i]); }
  else for (int i = 0; i < count && i < len; i++) notes[order[i]] = int16_t(piece[order[i]]);
  return style;
}
} // namespace

void euclideanRows(int length, int count, int rotation, bool *on) {
  if (length <= 0) return;
  count = std::clamp(count, 0, length);
  rotation = ((rotation % length) + length) % length;
  for (int i = 0; i < length; i++) on[(i + rotation) % length] = (i * count) % length < count;
}

const char *generateMelody(const MelodyRequest &q, int16_t *notes) {
  const int len = std::clamp(q.length, 1, MaxLen), count = std::clamp(q.count, 0, len);
  Rng r(q.seed);
  Scale sc(q.scaleMask);
  bool euclid[MaxLen];
  if (q.euclid) euclideanRows(len, count, r.below(len), euclid);
  if (q.kit) {
    static const char *const kitNames[] = {"KIT / EBM", "KIT / INDUSTRIAL", "KIT / RHYTHMIC NOISE", "KIT / RITUAL", "KIT / BROKEN"};
    return kitNames[kitPattern(r, sc, q.root, len, count, notes, q.euclid ? euclid : nullptr)];
  }
  for (int i = 0; i < len; i++) notes[i] = -1;
  if (!count) return "EMPTY";
  if (q.hits) {
    static const char *const hitNames[] = {"HITS / EUCLID", "HITS / METRIC", "HITS / OFFBEAT", "HITS / CLUSTER", "HITS / SCATTER", "HITS / PULSE"};
    static const Rhythm hitRhythm[] = {Euclid, Metric, Offbeat, Cluster, Scatter, Anchor};
    int style = r.below(6);
    int period = r.chance(50) ? r.pick({2, 3, 4, 4, 5, 8}) : 0;
    bool on[MaxLen];
    chooseRows(r, hitRhythm[style], len, count, period, on);
    if (q.euclid) std::copy(euclid, euclid + len, on);
    // the scale note nearest 60: the drum stays where its recipe was voiced
    int base = 60;
    for (int d = 0; d < 7; d++) {
      auto inScale = [&](int n) { return (q.scaleMask >> (((n - q.root) % 12 + 12) % 12)) & 1; };
      if (inScale(60 - d)) { base = 60 - d; break; }
      if (inScale(60 + d)) { base = 60 + d; break; }
    }
    int motion = r.below(4);   // 0 one pitch, 1 octave ghosts, 2 a few scale steps (toms), 3 a falling fill
    int k = 0;
    for (int i = 0; i < len; i++) {
      if (!on[i]) continue;
      int n = base;
      if (motion == 1 && r.chance(25)) n += r.chance(50) ? 12 : -12;
      else if (motion == 2 && r.chance(55)) n = base + sc.semis(r.range(-3, 4));
      else if (motion == 3 && k >= count - 3 && count > 3) n = base + sc.semis(-(k - (count - 4)));
      notes[i] = int16_t(std::clamp(n, 24, 100));
      k++;
    }
    return hitNames[style];
  }

  int w[IdiomCount] = {16, 12, 8, 14, 11, 10, 10, 10, 9};
  if (count <= 3) { w[Drone] += 22; w[Stab] += 8; w[Arp] = 3; w[Answer] = 2; w[Spiral] = 3; }
  if (count >= 10) { w[Drone] = 2; w[Ostinato] += 8; w[Arp] += 6; }
  if (len < 8) w[Answer] = 0;
  Idiom idiom = Idiom(r.weighted(w, IdiomCount));

  static const Rhythm rhythmFor[IdiomCount][4] = {
    {Euclid, Metric, Offbeat, Euclid}, {Metric, Euclid, Scatter, Metric}, {Anchor, Scatter, Anchor, Cluster},
    {Metric, Scatter, Euclid, Cluster}, {Metric, Euclid, Anchor, Scatter}, {Offbeat, Cluster, Euclid, Scatter},
    {Metric, Euclid, Scatter, Offbeat}, {Euclid, Metric, Offbeat, Scatter}, {Metric, Euclid, Scatter, Cluster}};
  Rhythm rhythm = r.chance(25) ? Rhythm(r.below(6)) : rhythmFor[idiom][r.below(4)];
  int period = 0;
  if ((idiom == Ostinato || idiom == Arp || idiom == Pedal || idiom == Stab) && r.chance(55))
    period = r.pick({3, 4, 4, 5, 6, 8, 8});
  if (idiom == Answer && len >= 8) period = len / 2;
  bool on[MaxLen];
  chooseRows(r, rhythm, len, count, period, on);
  if (q.euclid) std::copy(euclid, euclid + len, on);
  int rows[MaxLen], m = 0;
  for (int i = 0; i < len; i++) if (on[i]) rows[m++] = i;

  Context c{r, sc, len, m, rows, sc.near(7), sc.has(3) ? sc.near(3) : sc.near(4), sc.n, {}, 0};
  for (int i = 1; i < sc.n; i++) if (sc.pcs[i] == 1 || sc.pcs[i] == 6 || sc.pcs[i] == 8) c.dark[c.nd++] = i;
  if (!c.nd) for (int i = 1; i < sc.n; i++) if (sc.pcs[i] == 3 || sc.pcs[i] == 10) c.dark[c.nd++] = i;
  if (!c.nd) c.dark[c.nd++] = sc.n > 1 ? sc.n - 1 : 0;

  int deg[MaxLen]{};
  switch (idiom) {
  case Ostinato: ostinato(c, deg); break;
  case Pedal: pedal(c, deg); break;
  case Drone: drone(c, deg); break;
  case Walk: walk(c, deg); break;
  case Lament: lament(c, deg); break;
  case Stab: stab(c, deg); break;
  case Answer: answer(c, deg); break;
  case Arp: arp(c, deg); break;
  default: spiral(c, deg); break;
  }
  // Mutations: mirror the contour, run it backwards.
  if (idiom != Pedal && idiom != Drone && r.chance(12)) for (int i = 0; i < m; i++) deg[i] = -deg[i];
  if (idiom != Answer && r.chance(10)) std::reverse(deg, deg + m);

  static const int8_t registers[IdiomCount][4] = {
    {2, 2, 3, 4}, {2, 3, 3, 2}, {2, 3, 4, 5}, {3, 4, 4, 5}, {2, 3, 4, 3},
    {2, 3, 4, 2}, {3, 4, 5, 4}, {3, 3, 4, 4}, {3, 4, 5, 3}};
  int base = q.root + 12 * (registers[idiom][r.below(4)] + 1);
  for (int i = 0; i < m; i++) {
    int n = base + sc.semis(deg[i]);
    while (n < 28) n += 12;
    while (n > 88) n -= 12;
    notes[rows[i]] = int16_t(n);
  }
  // Ties: sustained idioms hold into the following empty rows.
  static const int8_t sustain[IdiomCount] = {12, 10, 92, 30, 40, 4, 30, 0, 15};
  int hold = sustain[idiom];
  if (idiom != Drone && r.chance(20)) hold = std::min(95, hold + 50);   // a legato variant
  for (int i = 0; i < m; i++) {
    if (!r.chance(hold)) continue;
    int end = i + 1 < m ? rows[i + 1] : len;
    for (int row = rows[i] + 1; row < end; row++) {
      if (row > rows[i] + 1 && r.chance(20)) break;
      notes[row] = -3;
    }
  }
  return IdiomNames[idiom];
}
} // namespace amalthea
