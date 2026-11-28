#include <stdio.h>
/* SPDX-License-Identifier: GPL-3.0-only
 * Adapter for Fugitive's VAP / CHIP / PERC engines; recipes from amalthea_recipes.c. No RT calls. */
#define HOST 1
#define RAMTAB
#include <stdint.h>
#include <string.h>
#include "fw_tables.h"
#include "dsp.h"
#include "eng.h"
#define NENG 7u
#define NMENG 6u
#define NDI 7u
enum { E_ACD, E_FAT, E_VAP, E_WTFM, E_DIRT, E_CHIP, E_PERC };
extern const eng_t ENG_VAP,ENG_CHIP,ENG_PERC;
const char *amalthea_native_family(int,uint32_t);
const char *amalthea_fiber_family(uint32_t);
const char *amalthea_bigorna_family(int,uint32_t);
const char *amalthea_bigorna_drum_family(uint32_t);
static const eng_t *ENGS[] = {&ENG_VAP,&ENG_VAP,&ENG_VAP,&ENG_VAP,&ENG_VAP,&ENG_CHIP,&ENG_PERC};
static const eng_t *eng_of(uint32_t e) { return ENGS[e < NENG ? e : 0]; }
typedef struct { uint8_t eng, family; int8_t mac[ENG_NMACRO]; uint32_t seed; int32_t val[ENG_NPAR]; } sound_t;
static int32_t par_clamp(const eng_par_t *p, int32_t v)
{
    v = clampi(v, p->lo, p->hi);
    if (p->is_enum)
        v = (int32_t)((uint32_t)(v - p->lo + 0x8000) & 0xFFFF0000u) + p->lo;
    return v;
}
/* the engine's defaults, the macros at 0 */
static void snd_default(sound_t *s, uint32_t e)
{
    const eng_t *E = eng_of(e);
    uint32_t j;
    memset(s, 0, sizeof *s);
    s->eng = (uint8_t)(e < NENG ? e : 0u);
    for (j = 0; j < E->npar && j < ENG_NPAR; j++)
        s->val[j] = E->par[j].def;
}

#include "gen.c"
#include "gen_perc.c"
#include "amalthea_recipes.c"
void amalthea_patch(int engine, uint32_t seed, int32_t *out) {
  memset(out,0,ENG_NPAR*sizeof(*out));
  if(engine>=3){out[0]=(int32_t)seed;return;}
  static const int old[]={E_VAP,E_PERC,E_CHIP};
  sound_t s;int e=old[engine];
  if(seed) am_sound(&s,(uint32_t)e,seed);else snd_default(&s,(uint32_t)e);
  memcpy(out,s.val,sizeof s.val);
}

/* ---------------------------------------------------------------- per-row variation of a patch
 * amalthea_patch_variant(): the patch amalthea_patch() builds, then each eligible parameter moved 30 .. 60 % of the
 * way toward a random value near it (a window around the value, not the whole range, so the family's character
 * stays). Seeded only by (seed, variation, parameter); variation 0 is the plain patch.
 * Never touched: anything that sets or moves pitch (tune, detune, fine, glide, arps, sweeps, vibrato, pitch mod
 * depths and the envelopes / LFOs that drive a pitch destination, a filter set on the note, FM ratios, drum TUNE),
 * the output levels and mixer, and every enum (models, waves, filter types, modes, routing). A value sitting at its
 * minimum or maximum (an attack of 0, a sustain of 0 or 1, a filter off at 20 kHz, no noise, no resonance,
 * 16 bits) stays there: those are switches as much as values, and some gate CPU work. */
enum { PV_NO, PV_LIN, PV_LOG, PV_SLIN, PV_SLOG, PV_RES };  /* domain: none, linear, octaves, sign-kept lin / oct */
#define PV_LOGW 81920              /* the window in octaves: +-1.25 (Q16) -> a move of at most ~0.75 octave */
#define PV_LINW 19661              /* the window in the range: +-30 % (Q16) -> a move of at most ~18 % */
#define PV_RESW 9830               /* resonance: +-15 % */
#define PV_RESCAP 52429            /* resonance is never raised above max(its value, 0.8) */
static uint32_t pv_hash(uint32_t seed, uint32_t variation, uint32_t j)
{
    uint32_t h = seed * 0x9E3779B1u ^ variation * 0x85EBCA77u ^ (j + 1u) * 0xC2B2AE3Du;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}
/* the slot sources' parameters (VAP: amp, filter, aux envelope, LFO 1, LFO 2) */
static const uint8_t PV_VAP_SRC_PAR[5] = {19, 23, 15, 27, 31};
static uint8_t pv_vap_class(const int32_t *v, uint32_t j, uint32_t frozen)
{
    if (j < 32 && ((frozen >> j) & 1u)) return PV_NO;
    switch (j) {
    case 0: case 1: case 2: case 3: case 41: return PV_LIN;             /* shapes, widths, noise */
    case 4: return v[5] == v[6] ? PV_LIN : PV_NO;                       /* the mix: not between two tunings */
    case 12: return PV_LOG;                                             /* cutoff (on-the-note: frozen) */
    case 13: return PV_RES;
    case 14: return PV_SLOG;                                            /* filter env amount, its sign kept */
    case 15: case 16: case 18: case 19: case 20: case 22: case 23: case 24: case 26: return PV_LOG;
    case 17: case 21: case 25: return PV_LIN;                           /* sustains */
    case 27: case 31: return PV_LOG;                                    /* LFO rates (conditions: frozen) */
    default: break;
    }
    if (j >= 42 && j < 60 && (j - 42) % 3 == 2) {                       /* a slot's amount */
        int32_t d = (v[j - 1] + 32768) >> 16;
        if (d == GVD_VOL || d == GVD_AMPS || d == GVD_P1 || d == GVD_P2) return PV_NO;
        return PV_SLOG;
    }
    return PV_NO;   /* tunes, detune, fine, resets, type, LFO enums, glide, note track, mix env, volume, sync,
                       slot sources / destinations */
}
/* VAP's frozen parameters (< 32): what drives a pitch destination, a sync'd or audio-rate LFO, a filter on the note */
static uint32_t pv_vap_frozen(const int32_t *v)
{
    uint32_t f = 0, k, j;
    for (k = 0; k < 6; k++) {
        int32_t src = (v[42 + 3 * k] + 32768) >> 16, dst = (v[43 + 3 * k] + 32768) >> 16;
        if ((dst == GVD_P1 || dst == GVD_P2) && v[44 + 3 * k] != 0 && src >= 0 && src < 5) {
            j = PV_VAP_SRC_PAR[src];
            f |= src >= GVS_LFO1 ? 1u << j : 15u << j;              /* the LFO's rate, or the envelope's ADSR */
        }
    }
    if (v[30] >= 32768 || v[27] >= GHZ(20)) f |= 1u << 27;          /* LFO 1 synced (a time base) or audio rate */
    if (v[34] >= 32768 || v[31] >= GHZ(20)) f |= 1u << 31;
    if (v[37] != 0 && (v[12] == G_F4 || v[12] == G_F4X2)) f |= 1u << 12;   /* the band set on the note: a pitch */
    return f;
}
static uint8_t pv_chip_class(const int32_t *v, uint32_t j)
{
    int32_t m = (v[0] + 32768) >> 16;
    switch (j) {
    case 3: return (m == 0 || m == 1 || m == 6) ? PV_LIN : PV_NO;     /* duty: pulse / SID / arp chord only
                                                                           (FM ratio, LFSR, spread, zap, drum sweep) */
    case 4: return PV_LOG;                                              /* PWM rate */
    case 5: return m == 2 ? PV_NO : PV_LIN;                             /* PWM depth (LFSR: its clock) */
    case 13: case 14: case 16: case 17: case 21: return PV_LOG;         /* A, D, R, cutoff, decimation */
    case 15: case 20: return PV_LIN;                                    /* sustain, bits */
    case 18: return PV_RES;
    case 19: return PV_SLIN;                                            /* env amount, its sign kept */
    default: return PV_NO;   /* model, tune, fine, sub (an octave down), arp, arp speed, sweep, sweep time,
                                vibrato, glide, play mode, volume */
    }
}
static uint8_t pv_perc_class(const int32_t *v, uint32_t j)
{
    switch (j) {
    case 2: case 3: case 10: case 11: case 22: case 23: case 24: case 28: case 29:
    case 36: case 37: case 38: case 39: case 48: return PV_LOG;        /* attacks, decays, filters */
    case 30: case 31: return PV_LOG;                                    /* hihat HP / LP: moved together */
    case 4: return PV_SLIN;                                             /* kick click timbre (sign: noise / tone) */
    case 5: case 12: case 13: case 25: case 26: case 27: case 33: case 41: case 42: case 46: return PV_LIN;
    case 47: return ((v[45] + 32768) >> 16) >= 4 ? PV_NO : PV_LIN;    /* perc timbre (cowbell: its pitch) */
    default: return PV_NO;   /* body waves (FM ratios), pitches, kick / tom pitch mod and its decay, wave mod,
                                snare tone (two pitched oscillators), tone pitches, metal timbres, models,
                                the mixer (levels, pans, sends, outputs), shaper, EQ */
    }
}
static int32_t pv_move(const eng_par_t *p, int32_t x, uint8_t cls, uint32_t h, int32_t logd)
{
    rnd_t r;
    int32_t u, f, sg = 1, lo = p->lo, hi = p->hi;
    int64_t y;
    if (x <= lo || x >= hi) return x;                                   /* at an end: a switch, left */
    if ((cls == PV_SLIN || cls == PV_SLOG) && x == 0) return x;
    r.s = h ? h : 1u;
    rnd_next(&r);
    u = rnd_bi(&r);                                                     /* the target: -1 .. 1 of the window */
    f = 19661 + (int32_t)(((int64_t)rnd_q15(&r) * 19661) >> 15);       /* 0.3 .. 0.6, Q16 */
    if (cls == PV_SLIN || cls == PV_SLOG) {
        if (x < 0) { sg = -1; x = -x; lo = 0; hi = -p->lo; }
        else { lo = 0; }
    }
    if (cls == PV_LOG || cls == PV_SLOG) {
        int32_t d = logd ? logd : (int32_t)(((((int64_t)PV_LOGW * u) >> 15) * f) >> 16);
        y = ((int64_t)x * exp2_q16(d)) >> 16;
        if (y < 1) y = 1;
    } else {
        int64_t w = ((int64_t)(hi - lo) * (cls == PV_RES ? PV_RESW : PV_LINW)) >> 16;   /* (sign kept: of |range|) */
        y = x + ((((w * u) >> 15) * f) >> 16);
    }
    if (y < lo) y = lo;
    if (y > hi) y = hi;
    if (cls == PV_RES && y > x && y > PV_RESCAP) y = x > PV_RESCAP ? x : PV_RESCAP;
    if ((cls == PV_SLIN || cls == PV_SLOG) && y == 0) y = 1;            /* the sign kept, never switched off */
    return par_clamp(p, (int32_t)(sg * y));
}
void amalthea_patch_variant(int engine, uint32_t seed, uint32_t variation, int32_t *out)
{
    static const int old[] = {E_VAP, E_PERC, E_CHIP};
    const eng_t *E;
    int32_t v[ENG_NPAR];
    uint32_t j, frozen = 0;
    amalthea_patch(engine, seed, out);
    if (!variation) return;
    if (engine >= 3) { out[1] = (int32_t)variation; return; }
    if (engine < 0) return;
    E = eng_of((uint32_t)old[engine]);
    memcpy(v, out, sizeof v);
    if (old[engine] == E_VAP) frozen = pv_vap_frozen(v);
    for (j = 0; j < E->npar && j < ENG_NPAR; j++) {
        uint8_t cls;
        int32_t logd = 0;
        if (E->par[j].is_enum) continue;
        if (old[engine] == E_VAP) cls = pv_vap_class(v, j, frozen);
        else if (old[engine] == E_CHIP) cls = pv_chip_class(v, j);
        else cls = pv_perc_class(v, j);
        if (cls == PV_NO) continue;
        if (old[engine] == E_PERC && j == 31) {                         /* the hihat's band keeps its width */
            if (out[30] != v[30]) {
                int32_t a = log2_q16((uint32_t)out[30]), b = log2_q16((uint32_t)v[30]);
                logd = a - b;
                if (!logd) continue;
            } else continue;
        }
        out[j] = pv_move(&E->par[j], v[j], cls, pv_hash(seed, variation, j), logd);
    }
}
/* ---------------------------------------------------------------- PERFORMANCE: live moves (Y)
 * The parameters a variation may move (VAP, PERC, CHIP; never pitch, levels or enums) move live under an LFO:
 * amount -1 .. 1 (Q15) of the same window a variation uses, deterministically. Recipe engines (3+): none. */
static uint8_t pv_class_of(int engine, const int32_t *v, uint32_t j)
{
    static const int old[] = {E_VAP, E_PERC, E_CHIP};
    const eng_t *E;
    if (engine < 0 || engine >= 3) return PV_NO;
    E = eng_of((uint32_t)old[engine]);
    if (j >= E->npar || j >= ENG_NPAR || E->par[j].is_enum) return PV_NO;
    if (old[engine] == E_VAP) return pv_vap_class(v, j, pv_vap_frozen(v));
    if (old[engine] == E_CHIP) return pv_chip_class(v, j);
    if (j == 30 || j == 31) return PV_NO;                                /* (the hihat's band moves as a pair) */
    return pv_perc_class(v, j);
}
int amalthea_patch_movable(int engine, const int32_t *v, uint32_t j)
{
    static const int old[] = {E_VAP, E_PERC, E_CHIP};
    uint8_t cls = pv_class_of(engine, v, j);
    const eng_par_t *p;
    if (cls == PV_NO) return 0;
    p = &eng_of((uint32_t)old[engine])->par[j];
    if (v[j] <= p->lo || v[j] >= p->hi) return 0;                        /* at an end: a switch */
    if ((cls == PV_SLIN || cls == PV_SLOG) && v[j] == 0) return 0;
    return 1;
}
/* "SECTION NAME" (several sections have a DECAY), for the screen; UI thread only (a static buffer) */
const char *amalthea_patch_parameter_name(int engine, uint32_t j)
{
    static const int old[] = {E_VAP, E_PERC, E_CHIP};
    static char text[48];
    const eng_t *E;
    if (engine < 0 || engine >= 3) return "";
    E = eng_of((uint32_t)old[engine]);
    if (j >= E->npar) return "";
    if (E->section && E->par[j].section < E->nsection && E->section[E->par[j].section])
        snprintf(text, sizeof text, "%s %s", E->section[E->par[j].section], E->par[j].name);
    else snprintf(text, sizeof text, "%s", E->par[j].name);
    return text;
}
int32_t amalthea_patch_move(int engine, const int32_t *v, uint32_t j, int32_t amount)
{
    static const int old[] = {E_VAP, E_PERC, E_CHIP};
    uint8_t cls;
    const eng_par_t *p;
    int32_t x, sg = 1, lo, hi;
    int64_t y;
    if (!amalthea_patch_movable(engine, v, j)) return v[j];
    cls = pv_class_of(engine, v, j);
    p = &eng_of((uint32_t)old[engine])->par[j];
    x = v[j]; lo = p->lo; hi = p->hi;
    if (amount > 32767) amount = 32767;
    if (amount < -32767) amount = -32767;
    if (cls == PV_SLIN || cls == PV_SLOG) {
        if (x < 0) { sg = -1; x = -x; lo = 0; hi = -p->lo; }
        else lo = 0;
    }
    if (cls == PV_LOG || cls == PV_SLOG) {
        y = ((int64_t)x * exp2_q16((int32_t)(((int64_t)PV_LOGW * amount) >> 15))) >> 16;
        if (y < 1) y = 1;
    } else {
        int64_t w = ((int64_t)(hi - lo) * (cls == PV_RES ? PV_RESW : PV_LINW)) >> 16;
        y = x + ((w * amount) >> 15);
    }
    if (y < lo) y = lo;
    if (y > hi) y = hi;
    if (cls == PV_RES && y > x && y > PV_RESCAP) y = x > PV_RESCAP ? x : PV_RESCAP;
    if ((cls == PV_SLIN || cls == PV_SLOG) && y == 0) y = 1;
    return par_clamp(p, (int32_t)(sg * y));
}
const char *amalthea_obrador_family(uint32_t);
const char *amalthea_rings_family(unsigned);
const char *amalthea_plaits_family(unsigned);
const char *amalthea_family(int engine,uint32_t seed) {
  if(engine==11)return seed?amalthea_bigorna_drum_family(seed):"DEFAULT";
  if(engine==12)return amalthea_obrador_family(seed);
  if(engine==13)return amalthea_rings_family(seed);
  if(engine==14)return amalthea_plaits_family(seed);
  if(engine>=6)return amalthea_native_family(engine,seed);
  if(engine==3)return amalthea_fiber_family(seed);
  if(engine>=4)return amalthea_bigorna_family(engine==4,seed);
  if(!seed)return "DEFAULT";
  static const int old[]={E_VAP,E_PERC,E_CHIP};return am_family((uint32_t)old[engine],seed);
}
