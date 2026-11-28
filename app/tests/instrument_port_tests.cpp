// OBRADOR (Tonitor), RINGS and PLAITS (Mutable Instruments) as tracker instruments.
#include "audio.hpp"
#include "mi_resampler.hpp"
#include "native_engines.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <vector>
extern "C" {
#include "eng.h"
extern const eng_t ENG_OBRADOR, ENG_RINGS, ENG_PLAITS;
void amalthea_patch(int, uint32_t, int32_t *);
void amalthea_patch_variant(int, uint32_t, uint32_t, int32_t *);
int amalthea_obrador_family_count(void);
int amalthea_obrador_family_index(uint32_t);
int amalthea_obrador_family_kind(int);
int amalthea_obrador_family_path(int);
int amalthea_rings_family_count(void);
int amalthea_rings_family_index(unsigned);
int amalthea_rings_family_shape(int, int *, int *, int *, int *);
int amalthea_plaits_family_count(void);
int amalthea_plaits_family_index(unsigned);
int amalthea_plaits_family_model(int);
int obrador_reference(int kind, int path, uint32_t seed, int note, int blocks, int off_block, int32_t *out);
}
static std::atomic<size_t> allocations{0};
void *operator new(size_t n) { allocations++; if (auto p = std::malloc(n)) return p; throw std::bad_alloc(); }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }
using namespace amalthea;
static void require(bool ok, const char *why) { if (!ok) { fprintf(stderr, "FAIL: %s\n", why); exit(1); } }

// The first seed (from 1) whose family is f.
template <class F> static uint32_t seedFor(int f, F familyOf) {
  for (uint32_t s = 1; s < 1000000; s++) if (familyOf(s) == f) return s;
  require(false, "every family reachable from a seed");
  return 0;
}
struct Played { std::vector<float> l, r; bool died = false; size_t allocated = 0; };
// One note held `hold` blocks of 32 frames, then released and rendered until silent (or `tail` blocks).
static Played play(const eng_t &E, const void *patch, int note, int hold, int tail, NativeVoice *native, unsigned char *data) {
  Played out;
  out.l.reserve(size_t(hold + tail) * ENG_BLK), out.r.reserve(size_t(hold + tail) * ENG_BLK);
  const size_t before = allocations;
  if (native) bindNativeVoice(data, native);
  E.reset(data);
  E.on(data, patch, note * 65536, 127);
  for (int b = 0; b < hold + tail; b++) {
    if (b == hold) E.off(data, patch, note * 65536);
    int32_t l[ENG_BLK]{}, r[ENG_BLK]{};
    int alive = E.render(data, patch, l, r);
    if (!E.stereo) std::copy(l, l + ENG_BLK, r);
    for (int i = 0; i < ENG_BLK; i++) out.l.push_back(l[i] / 32768.f), out.r.push_back(r[i] / 32768.f);
    if (!alive && b >= hold) { out.died = true; break; }
  }
  out.allocated = allocations - before;
  return out;
}
static double rms(const std::vector<float> &x, size_t from = 0, size_t to = ~size_t(0)) {
  to = std::min(to, x.size());
  double e = 0;
  for (size_t i = from; i < to; i++) e += double(x[i]) * x[i];
  return to > from ? std::sqrt(e / double(to - from)) : 0;
}
// Normalised autocorrelation of x at lag.
static double autocorr(const std::vector<float> &x, size_t from, size_t n, int lag) {
  double a = 0, b = 0, c = 0;
  for (size_t i = from; i < from + n; i++) { a += double(x[i]) * x[i + lag]; b += double(x[i]) * x[i]; c += double(x[i + lag]) * x[i + lag]; }
  return a / std::sqrt(b * c + 1e-30);
}

int main() {
  setvbuf(stdout, nullptr, _IONBF, 0);
  // --- the resampler: a 1 kHz sine at 48 kHz comes out a 1 kHz sine at 44.1 kHz; 23.5 kHz (which
  // would fold to 20.6 kHz) is gone.
  {
    auto through = [](double hz) {
      MiResampler<12> rs; rs.reset();
      long n = 0;
      std::vector<float> out(44100);
      for (size_t at = 0; at < out.size(); at += 32) {
        float l[32], r[32];
        rs.process(l, r, 32, [&](float *bl, float *br) { for (int i = 0; i < 12; i++, n++) bl[i] = br[i] = float(std::sin(2 * M_PI * hz * n / 48000.0)); });
        for (int i = 0; i < 32; i++) out[at + i] = l[i];
      }
      return out;
    };
    auto pass = through(1000);
    double best = 0; int lag = 0;
    for (int k = 40; k <= 48; k++) { double c = autocorr(pass, 4410, 8820, k); if (c > best) best = c, lag = k; }
    // 44.1 samples a period at 44.1 kHz: lags 44 and 45 straddle it.
    require(lag == 44 || lag == 45, "48 -> 44.1 kHz keeps the pitch");
    require(std::fabs(rms(pass, 4410) * std::sqrt(2.0) - 1) < .01, "pass band flat at 1 kHz");
    auto stop = through(23500);
    require(rms(stop, 4410) * std::sqrt(2.0) < 1e-3, "what would alias below 20.6 kHz is at least 60 dB down");
    printf("resampler: 1 kHz flat within 1%%, 23.5 kHz %.1f dB\n", 20 * std::log10(rms(stop, 4410) * std::sqrt(2.0)));
  }

  // --- OBRADOR: every family is one of Tonitor's kinds on one of its paths, and plays exactly what
  // Tonitor's own calls play.
  {
    const int n = amalthea_obrador_family_count();
    std::set<int> kinds, paths;
    for (int f = 0; f < n; f++) {
      kinds.insert(amalthea_obrador_family_kind(f)), paths.insert(amalthea_obrador_family_path(f));
      const uint32_t seed = seedFor(f, amalthea_obrador_family_index);
      int32_t val[ENG_NPAR]{};
      amalthea_patch(ObradorEngine, seed, val);
      alignas(16) static unsigned char patch[ENG_CK_MAX], voice[ENG_V_MAX];
      ENG_OBRADOR.cook(patch, val);
      const int note = 45 + f % 12, blocks = 2000, off = 1200;
      std::vector<int32_t> ref(blocks * ENG_BLK);
      obrador_reference(amalthea_obrador_family_kind(f), amalthea_obrador_family_path(f), seed, note, blocks, off, ref.data());
      ENG_OBRADOR.reset(voice);
      ENG_OBRADOR.on(voice, patch, note * 65536, 127);
      double energy = 0;
      for (int b = 0; b < blocks; b++) {
        if (b == off) ENG_OBRADOR.off(voice, patch, note * 65536);
        int32_t l[ENG_BLK]{}, r[ENG_BLK]{};
        ENG_OBRADOR.render(voice, patch, l, r);
        for (int i = 0; i < ENG_BLK; i++) { require(l[i] == ref[b * ENG_BLK + i], "OBRADOR sample for sample as Tonitor's own code"); energy += double(l[i]) * l[i]; }
      }
      require(energy > 1, "every OBRADOR family audible");
      // A row variation is a different, still audible, sound.
      int32_t var[ENG_NPAR]{};
      amalthea_patch_variant(ObradorEngine, seed, 12345, var);
      alignas(16) static unsigned char varied[ENG_CK_MAX];
      ENG_OBRADOR.cook(varied, var);
      require(std::memcmp(varied, patch, ENG_OBRADOR.ck_size) != 0, "an OBRADOR variation changes the patch");
    }
    require(kinds.size() == 10 && paths.size() == 3, "OBRADOR families cover Tonitor's ten kinds and three paths");
    printf("OBRADOR: %d families over 10 kinds / 3 paths, identical to Tonitor's code\n", n);
  }

  // --- RINGS and PLAITS: every family, real-time safety, release, determinism.
  struct Engine { const eng_t *E; int id; int count; int (*index)(unsigned); };
  const Engine mi[] = {{&ENG_RINGS, RingsEngine, amalthea_rings_family_count(), amalthea_rings_family_index},
                       {&ENG_PLAITS, PlaitsEngine, amalthea_plaits_family_count(), amalthea_plaits_family_index}};
  for (const auto &m : mi) {
    const eng_t &E = *m.E;
    auto a = makeNativeVoice(), b = makeNativeVoice();
    alignas(16) static unsigned char da[ENG_V_MAX], db[ENG_V_MAX];
    size_t arena = 0;
    for (int f = 0; f < m.count; f++) {
      const uint32_t seed = seedFor(f, m.index);
      int32_t val[ENG_NPAR]{};
      amalthea_patch(m.id, seed, val);
      alignas(16) static unsigned char patch[ENG_CK_MAX];
      E.cook(patch, val);
      auto x = play(E, patch, 48, 1400, 44100 * 120 / 32, a.get(), da);
      auto y = play(E, patch, 48, 1400, 44100 * 120 / 32, b.get(), db);
      require(x.allocated == 0 && y.allocated == 0, "note-on, rendering and release allocate nothing");
      arena = std::max(arena, nativeVoiceArenaUsed(a.get()));
      require(x.l == y.l && x.r == y.r, "two voices of one patch play the same (private state, private random)");
      for (float v : x.l) require(std::isfinite(v) && std::fabs(v) < 4, "output finite and bounded");
      const double level = rms(x.l, 0, 1400 * 32);
      if (level < 1e-4) fprintf(stderr, "%s family %d seed %u rms %g\n", E.name, f, seed, level);
      require(level > 1e-4, "every family audible");
      require(rms(x.l, 0, 1400 * 32) < .5, "level brought to the target");
      if (!x.died) fprintf(stderr, "%s family %d seed %u: tail rms %g after %zu frames\n", E.name, f, seed, rms(x.l, x.l.size() - 44100), x.l.size());
      require(x.died, "a released voice falls silent and ends");
    }
    printf("%s: %d families audible, deterministic, no callback allocation, natural release; arena %zu bytes\n", E.name, m.count, arena);
  }

  // RINGS: all six models, every polyphony, the internal exciter and the bow, the easter egg's six effects.
  {
    std::set<int> models, polyphony, fx, exciters;
    for (int f = 0; f < amalthea_rings_family_count(); f++) {
      int model, poly, egg, exciter;
      require(amalthea_rings_family_shape(f, &model, &poly, &egg, &exciter), "family shape");
      (egg ? fx : models).insert(model), polyphony.insert(poly), exciters.insert(exciter);
    }
    require(models.size() == 6 && fx.size() == 6, "RINGS: six resonator models and six Disastrous Peace effects");
    require(polyphony == std::set<int>({1, 2, 3, 4}), "RINGS: polyphony 1, 2, 3 and 4");
    require(exciters.size() == 3, "RINGS: internal exciter, bow, bow and pluck");
    // Polyphony: a second note strums a second string while the first still rings (DARK HARP, 4 voices).
    int harp = -1;
    for (int f = 0; f < amalthea_rings_family_count(); f++) { int mo, p, e, x; amalthea_rings_family_shape(f, &mo, &p, &e, &x); if (mo == 2 && p == 4 && !e) { harp = f; break; } }
    require(harp >= 0, "a four-voice string family");
    int32_t val[ENG_NPAR]{};
    amalthea_patch(RingsEngine, seedFor(harp, amalthea_rings_family_index), val);
    alignas(16) static unsigned char patch[ENG_CK_MAX], data[ENG_V_MAX];
    ENG_RINGS.cook(patch, val);
    auto voice = makeNativeVoice();
    bindNativeVoice(data, voice.get());
    ENG_RINGS.reset(data);
    ENG_RINGS.on(data, patch, 45 * 65536, 127);
    std::vector<float> left;
    for (int blk = 0; blk < 1400; blk++) {
      if (blk == 400) retriggerNativeVoice(data, 52, 127);   // audio.cpp's path for a running native instrument
      int32_t l[ENG_BLK]{}, r[ENG_BLK]{};
      ENG_RINGS.render(data, patch, l, r);
      for (int i = 0; i < ENG_BLK; i++) left.push_back(l[i] / 32768.f + r[i] / 32768.f);
    }
    require(rms(left, 400 * 32 + 2000, 400 * 32 + 6000) > rms(left, 300 * 32, 400 * 32), "a retriggered RINGS strums again");
    printf("RINGS: 6 models, 6 easter-egg effects, polyphony 1-4, 3 exciters, retrigger strums\n");
  }

  // PLAITS: all 24 models, spread across the draw; the note's pitch survives 48 -> 44.1 kHz.
  {
    const int n = amalthea_plaits_family_count();
    std::vector<int> perModel(24);
    for (int f = 0; f < n; f++) { int mo = amalthea_plaits_family_model(f); require(mo >= 0 && mo < 24, "model in range"); perModel[mo]++; }
    for (int mo = 0; mo < 24; mo++) require(perModel[mo] >= 1 && perModel[mo] <= 3, "every PLAITS model has its own family, none more than three");
    std::vector<int> drawn(24);
    for (uint32_t s = 1; s <= 24000; s++) drawn[amalthea_plaits_family_model(amalthea_plaits_family_index(s))]++;
    for (int mo = 0; mo < 24; mo++) require(drawn[mo] > 24000 * .02 && drawn[mo] < 24000 * .07, "seeds spread over the models (2-7% each)");
    // Note length: every model, pinged, gated or droning, ends with its note. A 100 ms note has
    // fallen silent (and its voice ended) within two seconds.
    {
      auto voice = makeNativeVoice();
      alignas(16) static unsigned char shortPatch[ENG_CK_MAX], shortData[ENG_V_MAX];
      for (int f = 0; f < n; f++) {
        int32_t v[ENG_NPAR]{};
        amalthea_patch(PlaitsEngine, seedFor(f, amalthea_plaits_family_index), v);
        ENG_PLAITS.cook(shortPatch, v);
        auto x = play(ENG_PLAITS, shortPatch, 48, 138, 44100 * 2 / 32, voice.get(), shortData);
        if (!x.died) fprintf(stderr, "PLAITS family %d still sounds 2 s after a 100 ms note\n", f);
        require(x.died, "a short PLAITS note ends with its length");
      }
    }
    // TWIN SAW (virtual analog): C3 is 130.81 Hz, 337.1 samples a period at 44.1 kHz.
    int saw = -1;
    for (int f = 0; f < n; f++) if (amalthea_plaits_family_model(f) == 8) { saw = f; break; }
    int32_t val[ENG_NPAR]{};
    amalthea_patch(PlaitsEngine, seedFor(saw, amalthea_plaits_family_index), val);
    alignas(16) static unsigned char patch[ENG_CK_MAX], data[ENG_V_MAX];
    ENG_PLAITS.cook(patch, val);
    auto voice = makeNativeVoice();
    auto x = play(ENG_PLAITS, patch, 48, 2000, 0, voice.get(), data);
    double best = 0; int lag = 0;
    for (int k = 300; k <= 380; k++) { double c = autocorr(x.l, 44100, 16384, k); if (c > best) best = c, lag = k; }
    if (lag < 336 || lag > 338) fprintf(stderr, "lag %d corr %f\n", lag, best);
    require(lag >= 336 && lag <= 338, "PLAITS plays the tracker's note at 44.1 kHz");
    printf("PLAITS: %d families over all 24 models; C3 period %d samples (337.1 expected)\n", n, lag);
  }

  // Both instruments as tracks: four DSP lanes stay deterministic.
  for (int e : {ObradorEngine, RingsEngine, PlaitsEngine}) {
    auto p = blankProject();
    for (auto &patch : p.patches) patch = {uint8_t(e), 7};
    for (int t = 0; t < Tracks; t++) for (int r = 0; r < 16; r++) { p.tracks[t].steps[r].note = 40 + t + r % 5; p.tracks[t].steps[r].instrument = 0; }
    Audio a(48), b(48);
    a.submit(p, true), b.submit(p, true);
    std::vector<float> x(22050 * 2), y(x.size());
    a.render(x.data(), 22050), b.render(y.data(), 22050);
    require(x == y, "12 tracks deterministic across four DSP lanes");
    require(rms(x) > 1e-4, "12 tracks audible");
  }
  printf("OBRADOR, RINGS, PLAITS: 12-track renders deterministic\n");
}
