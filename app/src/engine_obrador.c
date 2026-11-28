/* SPDX-License-Identifier: GPL-3.0-only
 * OBRADOR: Nuno Zimas's Oberheim-style polysynth voice from Tonitor (third_party/tonitor), at Tonitor's own 44.1 kHz
 * and 32-frame control block, which are Amalthea's. One Amalthea voice is one Obrador voice (card 1, and card 2 in
 * the dual kinds); the track's voice pool gives the polyphony Tonitor's layer did.
 * Patch generation (ob_design and its level measurement) runs where patches are cooked, never in the audio callback. */
#define HOST 1
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#include <stdint.h>
#include <string.h>
#include "../third_party/tonitor/tonitor_tables.h"
#include "../third_party/tonitor/dsp.h"
#include "../third_party/tonitor/obrador.c"
#include "eng.h"

/* ------------------------------------------------------------------ families
 * Each family is one of Tonitor's ten kinds on one of its three paths: OPEN as drawn, AMBIENT softer, slower and
 * darker, EXP harder (more drive and resonance, cross-modulation now and then). The weights lean on what Amalthea is
 * for: the slow string, PWM and swell pads and drones of dark ambient, and the brass, bass, sync leads and keys of
 * synthwave. */
typedef struct { const char *name; uint8_t kind, path, weight; } ob_family_t;
static const ob_family_t OB_FAMILY[] = {
    {"OB STRINGS", OBK_STRINGS, PATH_OPEN, 2},    {"DARK STRINGS", OBK_STRINGS, PATH_AMBIENT, 3},
    {"OB BRASS", OBK_BRASS, PATH_OPEN, 3},        {"HARD BRASS", OBK_BRASS, PATH_EXP, 1},
    {"PWM PAD", OBK_PWM, PATH_OPEN, 2},           {"NIGHT PWM", OBK_PWM, PATH_AMBIENT, 3},
    {"OB BASS", OBK_BASS, PATH_OPEN, 3},          {"GRIT BASS", OBK_BASS, PATH_EXP, 2},
    {"SYNC LEAD", OBK_SYNC, PATH_OPEN, 2},        {"TEAR LEAD", OBK_SYNC, PATH_EXP, 2},
    {"NOTCH SWEEP", OBK_NOTCH, PATH_OPEN, 1},     {"PHASE FOG", OBK_NOTCH, PATH_AMBIENT, 2},
    {"METAL", OBK_METAL, PATH_OPEN, 1},           {"RUST BELL", OBK_METAL, PATH_EXP, 2},
    {"DRONE", OBK_DRONE, PATH_OPEN, 2},           {"DEEP DRONE", OBK_DRONE, PATH_AMBIENT, 3},
    {"POLY KEYS", OBK_KEYS, PATH_OPEN, 2},        {"DARK KEYS", OBK_KEYS, PATH_AMBIENT, 1},
    {"SWELL", OBK_SWELL, PATH_OPEN, 2},           {"SLOW SWELL", OBK_SWELL, PATH_AMBIENT, 3},
};
#define OB_NFAMILY ((int)NELEM(OB_FAMILY))
static int ob_family_of(uint32_t seed)
{
    uint32_t h = seed * 0x9e3779b1u, total = 0, x;
    int i;
    h ^= h >> 16;
    for (i = 0; i < OB_NFAMILY; i++)
        total += OB_FAMILY[i].weight;
    x = h % total;
    for (i = 0; i < OB_NFAMILY; i++) {
        if (x < OB_FAMILY[i].weight)
            return i;
        x -= OB_FAMILY[i].weight;
    }
    return 0;
}

/* ------------------------------------------------------------- row variations
 * val[1] nonzero: the drawn design's tone moved part of the way toward a value near it -- the cutoff (+-10
 * semitones at most), the SEM morph, the resonance (never above max(drawn, 0.6)), the filter envelope's depth, the
 * six envelope times, the sustains and the LFO's cutoff and morph depths. Never touched: the kind, the waves, the
 * interval, every detune, the sync sweep, the LFO's pitch depth, shape and rate, the vintage offsets and the level;
 * ob_measure_on then brings the level to the target as for any design. */
static uint32_t ov_next(uint32_t *r) { uint32_t x = *r; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *r = x; }
static int32_t ov_in(uint32_t *r, int32_t a, int32_t b) { return a + (int32_t)(ov_next(r) % (uint32_t)(b - a + 1)); }
static int32_t ov_scale(uint32_t *r, int32_t v, int32_t lo, int32_t hi) { return (int32_t)((int64_t)v * ov_in(r, lo, hi) / 100); }
static void ob_vary(ob_design_t *d, uint32_t var)
{
    uint32_t rs = (var * 0x85ebca77u) ^ (d->seed * 0x9e3779b1u) ^ 0x0b5ad0u, *r = &rs;
    int32_t top = d->reso > 19661 ? d->reso : 19661;
    if (!rs)
        rs = 1;
    d->cut = clampi(d->cut + ov_in(r, -160, 160), 16 * 24, 16 * 120);
    d->morph = clampi(d->morph + ov_in(r, -4915, 4915), 0, Q15);
    d->reso = clampi(d->reso + ov_in(r, -4915, 4915), 0, top);
    d->fenv = ov_scale(r, d->fenv, 70, 130);
    d->fa = maxi(1, ov_scale(r, d->fa, 70, 140)), d->fd = maxi(1, ov_scale(r, d->fd, 70, 140)), d->fr = maxi(1, ov_scale(r, d->fr, 70, 135));
    d->aa = maxi(1, ov_scale(r, d->aa, 70, 140)), d->ad = maxi(1, ov_scale(r, d->ad, 70, 140)), d->ar = maxi(1, ov_scale(r, d->ar, 70, 135));
    if (d->fs > 0 && d->fs < Q15)
        d->fs = clampi(d->fs + ov_in(r, -4915, 4915), 1, Q15 - 1);
    if (d->as > 0 && d->as < Q15)
        d->as = clampi(d->as + ov_in(r, -3277, 3277), 1, Q15 - 1);
    d->lfo_cut = ov_scale(r, d->lfo_cut, 60, 140);
    d->lfo_morph = clampi(ov_scale(r, d->lfo_morph, 60, 140), 0, Q15);
}

/* ------------------------------------------------------------------ the engine */
static void ob_design_seed(ob_design_t *d, uint32_t seed)
{
    const ob_family_t *f = &OB_FAMILY[ob_family_of(seed)];
    ob_design(d, f->kind, seed, f->path, 0);
}
static void cook(void *p, const int32_t *v)
{
    ob_design_t *d = p;
    ob_voice_t probe;
    ob_design_seed(d, (uint32_t)v[0]);
    if (v[1])
        ob_vary(d, (uint32_t)v[1]);
    ob_measure_on(d, &probe);
}
static void reset(void *v) { ob_init(v, 1); }
static void on(void *v, const void *p, int32_t n, int vel)
{
    const ob_design_t *d = p;
    ob_voice_t *o = v;
    int32_t note = clampi(n >> 16, 0, 127);
    ob_init(o, d->seed ^ 0x77u ^ (uint32_t)n);     /* (free-running oscillators and drift: a new start each note) */
    ob_note_on(o, d, note, vel, 1);
    o->note16 = clampi(n >> 12, 0, 127 * 16);      /* (the fine pitch: 1/16 semitone) */
}
static void off(void *v, const void *p, int32_t n) { (void)p; (void)n; ob_note_off(v); }
static int render(void *v, const void *p, int32_t *l, int32_t *r) { (void)r; return ob_render(v, p, l, ENG_BLK); }
_Static_assert(ENG_BLK == CTL, "Obrador's control block is Amalthea's render block");
_Static_assert(FS == 44100, "Obrador's tables are for 44.1 kHz");
_Static_assert(sizeof(ob_design_t) <= ENG_CK_MAX, "Obrador patch storage");
_Static_assert(sizeof(ob_voice_t) <= ENG_V_MAX, "Obrador voice storage");
const eng_t ENG_OBRADOR = {.name = "OBRADOR", .desc = "OBERHEIM-STYLE POLY", .v_size = sizeof(ob_voice_t), .ck_size = sizeof(ob_design_t),
                           .cook = cook, .reset = reset, .on = on, .off = off, .render = render};
const char *amalthea_obrador_family(uint32_t seed) { return OB_FAMILY[ob_family_of(seed)].name; }
int amalthea_obrador_family_count(void) { return OB_NFAMILY; }
int amalthea_obrador_family_index(uint32_t seed) { return ob_family_of(seed); }
int amalthea_obrador_family_kind(int f) { return f >= 0 && f < OB_NFAMILY ? OB_FAMILY[f].kind : -1; }
int amalthea_obrador_family_path(int f) { return f >= 0 && f < OB_NFAMILY ? OB_FAMILY[f].path : -1; }
