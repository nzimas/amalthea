/* SPDX-License-Identifier: GPL-3.0-only
 * FIBER: the sounds HOME draws (fb_design). A sound is (family, seed, path, generation): drawn again from them bit for
 * bit, as Tonitor's are. The families lean towards industrial techno, EBM, synthwave and film music; the path
 * (OPEN, AMBIENT, EXPERIMENTAL) weighs the families and pushes the draw softer or harder.
 * The rule of every family: no pitch glides or sweeps. No glide is drawn; a drum's pitch is fixed from its first sample
 * (its punch from the click, the filter and amplifier envelopes and the drive); a vibrato is the only pitch movement,
 * a quarter semitone at most (fiber.c holds every moving pitch route to that). */

#include "fiber_rows.h"
typedef char fk_fits_presets[FK_N <= 255 ? 1 : -1];   /* (a preset keeps the family in 8 bits, a session in 10:
                                                       * sessions.c) */
static const char *const FK_NAME[FK_N] = {FK_HAND(FK_NAME_H) FK_ROWS(FK_NAME_H)};
static const uint8_t FK_W[PATH_N][FK_N] = {
    {FK_HAND(FK_W_O) FK_ROWS(FK_W_O)},           /* OPEN */
    {FK_HAND(FK_W_A) FK_ROWS(FK_W_A)},           /* AMBIENT */
    {FK_HAND(FK_W_E) FK_ROWS(FK_W_E)},           /* EXPERIMENTAL */
};
static const uint8_t FK_MODP[FK_N] = {FK_HAND(FK_MODP) FK_ROWS(FK_MODP)};   /* (mod.c: HOME's modulation odds) */
static const fk_row_t FK_ROW[FK_N - FK_ROW0] = {FK_ROWS(FK_ROW_R)};

/* the tables each family draws from (internal IDs, REPORT.md appendix A) */
static const uint8_t FKT_EBM[] = {6, 5, 15, 56, 13, 7, 3, 66};
static const uint8_t FKT_STAB[] = {23, 13, 68, 15, 3, 57, 20};
static const uint8_t FKT_METAL[] = {4, 2, 8, 24, 83, 86, 30, 21, 25};
static const uint8_t FKT_NOISE[] = {71, 72, 73, 8, 69, 20, 70};
static const uint8_t FKT_SUB[] = {0, 1, 81, 14, 85, 74, 9};
static const uint8_t FKT_GRIND[] = {67, 69, 70, 57, 58, 60, 23, 68};
static const uint8_t FKT_SAW[] = {6, 7, 15, 66, 65, 5};
static const uint8_t FKT_PAD[] = {7, 87, 29, 85, 17, 18, 76, 77, 78, 79};
static const uint8_t FKT_LEAD[] = {6, 15, 56, 57, 58, 59, 62, 28, 13};
static const uint8_t FKT_PLUCK[] = {9, 12, 19, 84, 22, 3, 30, 81};
static const uint8_t FKT_BRASS[] = {6, 7, 5, 15, 14};
static const uint8_t FKT_TENSION[] = {10, 25, 82, 20, 31, 75, 11, 64};
static const uint8_t FKT_CHOIR[] = {16, 26, 27, 80, 11};
static const uint8_t FKT_BELLS[] = {4, 83, 86, 84, 30, 24, 2};
static const uint8_t FKT_SWELL[] = {7, 17, 18, 76, 77, 78, 79, 82};
static const uint8_t FKT_PULSE[] = {5, 29, 65, 59, 3, 61};
static const uint8_t FKT_DRUM[] = {0, 9, 14, 81, 85};           /* (a drum's second wave, when not the triangle) */
static const uint8_t FKT_ANVIL[] = {8, 4, 24, 25, 20, 21, 2};
static const uint8_t FKT_MACHINE[] = {56, 57, 59, 60, 67, 69, 71, 72, 20, 13};
static const uint8_t FKT_VA[] = {6};                             /* (STRINGS and VA: no table played; one named) */
static const struct { const uint8_t *t; uint8_t n; } FK_TABS[FK_N] = {
    {FKT_EBM, sizeof FKT_EBM}, {FKT_STAB, sizeof FKT_STAB}, {FKT_METAL, sizeof FKT_METAL}, {FKT_NOISE, sizeof FKT_NOISE},
    {FKT_SUB, sizeof FKT_SUB}, {FKT_GRIND, sizeof FKT_GRIND}, {FKT_SAW, sizeof FKT_SAW}, {FKT_PAD, sizeof FKT_PAD},
    {FKT_LEAD, sizeof FKT_LEAD}, {FKT_PLUCK, sizeof FKT_PLUCK}, {FKT_BRASS, sizeof FKT_BRASS},
    {FKT_TENSION, sizeof FKT_TENSION}, {FKT_CHOIR, sizeof FKT_CHOIR}, {FKT_BELLS, sizeof FKT_BELLS},
    {FKT_SWELL, sizeof FKT_SWELL}, {FKT_PULSE, sizeof FKT_PULSE},
    {FKT_DRUM, sizeof FKT_DRUM}, {FKT_DRUM, sizeof FKT_DRUM}, {FKT_DRUM, sizeof FKT_DRUM}, {FKT_DRUM, sizeof FKT_DRUM},
    {FKT_DRUM, sizeof FKT_DRUM}, {FKT_DRUM, sizeof FKT_DRUM}, {FKT_DRUM, sizeof FKT_DRUM}, {FKT_DRUM, sizeof FKT_DRUM},
    {FKT_ANVIL, sizeof FKT_ANVIL}, {FKT_MACHINE, sizeof FKT_MACHINE}, {FKT_VA, 1}, {FKT_VA, 1}, {FKT_VA, 1}, {FKT_VA, 1},
    {FKT_VA, 1},
};

static int32_t fb_rms(rnd_t *r, int32_t lo, int32_t hi)   /* a time between lo and hi ms, evenly on a log scale */
{
    int32_t l = log2_q16((uint32_t)lo), h = log2_q16((uint32_t)hi);
    return (int32_t)(exp2_q16(rnd_in(r, l, h)) >> 16);
}
/* the two faces of the one engine, as the screen and PRESETS show them: FIBER (the tonal families) and PULSE (the
 * drums and percussion, and the pitched one-shot hits METAL HIT, NOISE HIT, ANVIL, IMPACT) */
enum { FG_FIBER, FG_PULSE, FG_N };
static const char *const FG_NAME[FG_N] = {"FIBER", "PULSE"};
static uint32_t fk_group(uint32_t k)
{
    if (k >= FK_N)
        return FG_FIBER;
    if ((k >= FK_KICK && k <= FK_CYMBAL) || k == FK_METAL || k == FK_NOISE || k == FK_ANVIL || k == FK_IMPACT)
        return FG_PULSE;
    if (k >= FK_ROW0 && FK_ROW[k - FK_ROW0].fixp[1])
        return FG_PULSE;
    return FG_FIBER;
}
/* the family before (dir < 0) or after k in its group, in the families' order; at an end, k */
static int32_t fk_step(int32_t k, int32_t dir)
{
    uint32_t g = fk_group((uint32_t)k);
    int32_t j = k;
    for (;;) {
        j += dir < 0 ? -1 : 1;
        if (j < 0 || j >= FK_N)
            return k;
        if (fk_group((uint32_t)j) == g)
            return j;
    }
}
/* a family of group g for HOME, by the path's weights, not `not` (drawn from the seed: the same seed, the same one) */
static int32_t fk_draw_group(uint32_t seed, uint32_t path, uint32_t g, int32_t not)
{
    rnd_t r;
    uint32_t k, t = 0, any = 0, x;
    path = path < PATH_N ? path : PATH_OPEN;
    for (k = 0; k < FK_N; k++)
        if (fk_group(k) == g && (int32_t)k != not)
            t += FK_W[path][k], any++;
    rnd_seed(&r, seed ^ 0x6A0F5u);
    x = (uint32_t)rnd_below(&r, (int32_t)(t ? t : any));
    for (k = 0; k < FK_N; k++) {
        uint32_t w;
        if (fk_group(k) != g || (int32_t)k == not)
            continue;
        w = t ? FK_W[path][k] : 1u;
        if (x < w)
            return (int32_t)k;
        x -= w;
    }
    return not >= 0 ? not : 0;
}
static uint32_t fb_tabof(rnd_t *r, uint32_t kind)
{
    if (kind >= FK_ROW0) {                        /* (a row's list, 255 at its end; none: a ROM saw for its name) */
        const uint8_t *t = FK_ROW[kind - FK_ROW0].tab;
        uint32_t n = 0;
        if (!t)
            return 6u;
        while (t[n] != 255u)
            n++;
        return t[rnd_below(r, (int32_t)n)];
    }
    return FK_TABS[kind].t[rnd_below(r, FK_TABS[kind].n)];
}
/* a slot: the next free one; amount -1 .. 1 as Q15 (stored as (amount + 1) / 2) */
static void fb_mod(fb_design_t *d, uint32_t src, uint32_t dst, int32_t amt)
{
    uint32_t s;
    for (s = 0; s < FB_NSLOT && d->slot[s].dst; s++)
        ;
    if (s >= FB_NSLOT)
        return;
    d->slot[s].src = (uint8_t)src;
    d->slot[s].dst = (uint8_t)dst;
    d->slot[s].amt = (uint16_t)((clampi(amt, -Q15, Q15) + Q15) >> 1);
}
/* an amount for a destination in its units (FBD_PITCH*: 1/16 semitone; FBD_CUT: 1/16 semitone; FBD_POS*: positions Q8;
 * the rest: Q15 of full) as the matrix's Q15 amount, for a source at full; curved ones by the square root */
static int32_t fb_amt(uint32_t dst, int32_t units)
{
    int32_t a, neg = units < 0;
    units = absi(units);
    if (dst >= FBD_PITCH && dst <= FBD_PITCH2)
        a = units * 2048 / 48;                      /* (48 semitones = 768 sixteenths at full: x 32768 / 768) */
    else if (dst == FBD_CUT)
        a = units * 16;                             /* (128 semitones = 2048 at full) */
    else if (dst == FBD_POS1 || dst == FBD_POS2)
        a = units * 2;                              /* (64 positions = 16384 Q8 at full) */
    else
        a = units;
    a = clampi(a, 0, Q15);
    if (FB_CURVED[dst])
        a = (int32_t)isqrt32((uint32_t)a << 15);
    return neg ? -a : a;
}
/* a source on both wave positions (two slots: Wave 1, Wave 2), the second a little less or more */
static void fb_modw(fb_design_t *d, uint32_t src, int32_t q8)
{
    fb_mod(d, src, FBD_POS1, fb_amt(FBD_POS1, q8));
    fb_mod(d, src, FBD_POS2, fb_amt(FBD_POS2, q8 * 3 / 4));
}
static void fb_env3(fb_design_t *d, uint32_t e, int32_t a, int32_t dd, int32_t s, int32_t rr)
{
    d->ea[e] = a, d->ed[e] = dd, d->es[e] = s, d->er[e] = rr;
}

/* a row's family (fiber_rows.h): every range drawn, over the common ground fb_design has laid */
static int32_t fb_pct(int32_t p) { return p * Q15 / 100; }
static void fb_row_lfo(fb_design_t *d, rnd_t *r, uint32_t o, const fk_lfo_t *f)
{
    int32_t a;
    if (f->dst == LD_NONE)
        return;
    d->lshape[o] = f->shape;
    d->lglob[o] = (uint8_t)(f->flags & LF_GLOB ? 1 : 0);
    d->lsync[o] = (uint8_t)(f->flags & LF_SYNC ? 1 : 0);
    d->lhz[o] = rnd_in(r, f->hz[0], f->hz[1]) * 655;   /* (hundredths of a Hz to Q16) */
    a = rnd_in(r, f->amt[0], f->amt[1]);
    switch (f->dst) {
    case LD_POS:
        fb_modw(d, FBS_L1 + o * 3u, a << 8);
        break;
    case LD_POS1:
        fb_mod(d, FBS_L1 + o * 3u, FBD_POS1, fb_amt(FBD_POS1, a << 8));
        break;
    case LD_CUT:
        fb_mod(d, FBS_L1 + o * 3u, FBD_CUT, fb_amt(FBD_CUT, a * 16));
        break;
    case LD_PITCH:                                 /* (a vibrato: fiber.c holds it to a quarter semitone) */
        fb_mod(d, FBS_L1 + o * 3u, FBD_PITCH, fb_amt(FBD_PITCH, clampi(a, -4, 4)));
        break;
    case LD_PITCH2:
        fb_mod(d, FBS_L1 + o * 3u, FBD_PITCH2, fb_amt(FBD_PITCH2, clampi(a, -4, 4)));
        break;
    default: {
        static const uint8_t T[] = {0, 0, 0, 0, FBD_VOL, 0, FBD_LVL2, FBD_DIRT, FBD_RESO, FBD_PANV};
        fb_mod(d, FBS_L1 + o * 3u, T[f->dst < sizeof T ? f->dst : 0], fb_pct(a));
        break;
    }
    }
}
static void fb_row_env(fb_design_t *d, rnd_t *r, uint32_t e, const fk_env_t *v)
{
    if (!v->a[1] && !v->d[1] && !v->r[1])
        return;                                    /* (not given: the common ground's) */
    fb_env3(d, e, fb_rms(r, v->a[0] ? v->a[0] : 1, v->a[1] ? v->a[1] : 1), fb_rms(r, v->d[0] ? v->d[0] : 1, v->d[1] ? v->d[1] : 1),
            fb_pct(rnd_in(r, v->s[0], v->s[1])), fb_rms(r, v->r[0] ? v->r[0] : 1, v->r[1] ? v->r[1] : 1));
}
static void fb_row_design(fb_design_t *d, rnd_t *r, const fk_row_t *w)
{
    d->om[0] = w->om[0], d->om[1] = w->om[1];
    d->pos[0] = (int16_t)(rnd_in(r, w->pos[0], w->pos[1]) << 8);
    d->pos[1] = (int16_t)((w->pos2[1] ? rnd_in(r, w->pos2[0], w->pos2[1]) : rnd_in(r, w->pos[0], w->pos[1])) << 8);
    if (w->pos[1] > 60 || w->pos2[1] > 60)
        d->limit = 0;                              /* (the common waves are meant: no LIMIT keeping them out) */
    d->semi[1] = w->semi2[rnd_below(r, 4)];
    d->det[1] = (int16_t)rnd_in(r, -(int32_t)w->det2, w->det2);
    d->lvl[0] = 200, d->lvl[1] = (int16_t)rnd_in(r, w->lvl2[0], w->lvl2[1]);
    d->ftype = w->ft;
    if (w->ft2pct && rnd_chance(r, w->ft2pct))
        d->ftype = w->ft2;
    d->dcurve = (uint8_t)(w->dc == FT_ANY ? rnd_below(r, DC_N) : w->dc);
    d->cut = 16 * rnd_in(r, w->cut[0], w->cut[1]);
    d->reso = fb_pct(rnd_in(r, w->res[0], w->res[1]));
    d->fenv = 16 * rnd_in(r, w->fenv[0], w->fenv[1]);
    d->keytrk = w->keytrk ? fb_pct(w->keytrk) : 16384;   /* (not given: half -- a low cutoff stays open on high notes) */
    fb_row_env(d, r, 1, &w->fe);
    fb_row_env(d, r, 2, &w->ae);
    if (w->wd[1]) {                                /* the wave envelope: a scan at the note, decaying */
        int32_t q = rnd_in(r, w->wenv[0], w->wenv[1]) << 8;
        fb_env3(d, 0, 1, fb_rms(r, w->wd[0] ? w->wd[0] : 1, w->wd[1]), 0, w->wd[0] ? w->wd[0] : 20);
        d->wenv[0] = (int16_t)q;
        d->wenv[1] = (int16_t)(w->wneg ? -q * 3 / 4 : q * 3 / 4);
    } else
        d->wenv[0] = d->wenv[1] = 0;
    d->dirt = DT_OFF, d->damt = 0;
    if (w->dpct && rnd_chance(r, w->dpct))
        d->dirt = w->dirt, d->damt = (int16_t)rnd_in(r, w->damt[0], w->damt[1]);
    if (w->ring[1] && d->dirt == DT_OFF)          /* (ring modulation is DIRT's: when no other process is drawn) */
        d->dirt = DT_RING, d->damt = (int16_t)rnd_in(r, w->ring[0], w->ring[1]);
    fb_row_lfo(d, r, 0, &w->lfo[0]);
    fb_row_lfo(d, r, 1, &w->lfo[1]);
    d->mode = w->mode;
    if (w->fixp[1]) {                              /* a drum: its pitch fixed, each hit from silence */
        d->fixp = (uint8_t)rnd_in(r, w->fixp[0], w->fixp[1]);
        d->perc = 1, d->mode = VM_MONO, d->keytrk = 0;
    }
    if (w->ens[1])
        d->ens = (int16_t)fb_pct(rnd_in(r, w->ens[0], w->ens[1])), d->emode = w->emode;
    d->flav = (int16_t)(d->perc ? 0 : fb_pct(w->flav));
    d->rs = (uint8_t)(w->rs8 && rnd_chance(r, w->rs8));
    if (w->vvel)
        d->vvel = fb_pct(w->vvel);
    if (w->lvl1)
        d->lvl[0] = w->lvl1 == 255 ? 0 : w->lvl1;
}

/* the drums: a fixed pitch, MONO (each hit from silence, as a drum machine's channel), no pitch movement at all --
 * the punch from DIRT's CLICK, the filter envelope, the amplifier's, the wave envelope and the drive */
static void fb_drum(fb_design_t *d, rnd_t *r, uint32_t kind)
{
    d->perc = 1, d->mode = VM_MONO, d->flav = 0, d->keytrk = 0, d->limit = 0, d->fvel = 16 * rnd_in(r, 4, 16);
    d->lvl[0] = 220, d->lvl[1] = 0, d->semi[1] = 0, d->det[1] = 0;
    d->pos[0] = d->pos[1] = 61 << 8;              /* (the common triangle: the drums' body) */
    d->vvel = rnd_in(r, 16384, 29491);
    fb_env3(d, 0, 1, 40, 0, 40);
    switch (kind) {
    case FK_KICK:                                  /* a low triangle (and its octave below), a click, a closing filter */
        d->fixp = (uint8_t)rnd_in(r, 31, 40);
        d->lvl[1] = (int16_t)rnd_in(r, 0, 140), d->semi[1] = -12;
        d->ftype = FT_LP24, d->cut = 16 * rnd_in(r, 36, 50), d->fenv = 16 * rnd_in(r, 36, 60), d->reso = rnd_in(r, 0, 9830);
        fb_env3(d, 1, 1, fb_rms(r, 15, 60), 0, 30);
        fb_env3(d, 2, 1, fb_rms(r, 180, 700), 0, fb_rms(r, 40, 160));
        d->dirt = DT_CLICK, d->damt = (int16_t)rnd_in(r, 60, 200);
        if (rnd_chance(r, 35)) {                   /* (a harder, industrial kick: a wave scan at the attack) */
            d->tab[0] = (uint8_t)FKT_DRUM[rnd_below(r, sizeof FKT_DRUM)];
            d->pos[0] = (int16_t)(rnd_in(r, 0, 20) << 8), d->wenv[0] = (int16_t)(rnd_in(r, 10, 30) << 8);
            fb_env3(d, 0, 1, fb_rms(r, 10, 40), 0, 20);
        }
        if (rnd_chance(r, 35))
            d->ftype = FT_DRIVE, d->dcurve = (uint8_t)(rnd_chance(r, 50) ? DC_PNP : DC_TUBE), d->cut = 16 * rnd_in(r, 54, 70);
        break;
    case FK_SNARE:                                 /* two triangles a few semitones apart, short, under longer noise */
    case FK_TOM:
        d->fixp = (uint8_t)(kind == FK_SNARE ? rnd_in(r, 47, 56) : rnd_in(r, 40, 55));
        d->semi[1] = (int16_t)rnd_in(r, 4, 8);
        if (kind == FK_SNARE) {                    /* (the body's own, quicker envelope: the wave envelope, over a
                                                    * little of it held, so MOD DEPTH at 0 leaves a body) */
            d->lvl[0] = 48, d->lvl[1] = 32;
            fb_env3(d, 0, 1, fb_rms(r, 40, 120), 0, 40);
            fb_mod(d, FBS_E1, FBD_LVL1, Q15 * 2 / 3);
            fb_mod(d, FBS_E1, FBD_LVL2, Q15 / 2);
        } else
            d->lvl[0] = 220, d->lvl[1] = (int16_t)rnd_in(r, 40, 110);
        if (kind == FK_SNARE) {
            d->dirt = DT_STATIC, d->damt = (int16_t)rnd_in(r, 140, 256);
            d->ftype = FT_HP12, d->cut = 16 * rnd_in(r, 44, 60), d->reso = rnd_in(r, 0, 6554);
            fb_env3(d, 2, 1, fb_rms(r, 140, 360), 0, fb_rms(r, 60, 160));
        } else {
            d->dirt = DT_CLICK, d->damt = (int16_t)rnd_in(r, 40, 140);
            d->ftype = FT_LP24, d->cut = 16 * rnd_in(r, 50, 64), d->fenv = 16 * rnd_in(r, 24, 40);
            fb_env3(d, 1, 1, fb_rms(r, 40, 120), 0, 40);
            fb_env3(d, 2, 1, fb_rms(r, 250, 700), 0, fb_rms(r, 80, 250));
        }
        if (rnd_chance(r, 30))
            d->ftype = FT_DRIVE, d->dcurve = (uint8_t)(rnd_chance(r, 50) ? DC_DIODE : DC_PNP), d->cut = 16 * rnd_in(r, 56, 72);
        break;
    case FK_CLAP:                                  /* noise through a band, fluttered by a quick pulse LFO from the hit */
        d->fixp = 60;
        d->lvl[0] = 0;
        d->dirt = DT_STATIC, d->damt = (int16_t)rnd_in(r, 200, 256);
        d->ftype = FT_HP12, d->cut = 16 * rnd_in(r, 60, 74), d->reso = rnd_in(r, 6554, 16384);
        d->lshape[0] = FBL_PULSE, d->lhz[0] = rnd_in(r, 60 * Q16, 110 * Q16), d->lsync[0] = 1;
        fb_mod(d, FBS_L1, FBD_VOL, -rnd_in(r, 13107, 22937));
        fb_env3(d, 2, 1, fb_rms(r, 120, 380), 0, fb_rms(r, 60, 160));
        break;
    case FK_CHAT:                                  /* hats and cymbals: two high pulses ring-modulated at an odd ratio, */
    case FK_OHAT:                                  /* or noise, high-passed */
    case FK_CYMBAL:
        d->fixp = (uint8_t)rnd_in(r, 76, 90);
        d->om[0] = d->om[1] = OM_VA;
        d->pos[0] = (int16_t)(rnd_in(r, 32, 52) << 8), d->pos[1] = (int16_t)(rnd_in(r, 32, 52) << 8);
        d->semi[1] = (int16_t)rnd_in(r, 4, 9), d->det[1] = (int16_t)rnd_in(r, -90, 90);
        d->lvl[0] = (int16_t)rnd_in(r, 60, 140), d->lvl[1] = (int16_t)rnd_in(r, 60, 140);
        if (rnd_chance(r, kind == FK_CYMBAL ? 70 : 55))
            d->dirt = DT_RING, d->damt = (int16_t)rnd_in(r, 160, 256);
        else
            d->dirt = DT_STATIC, d->damt = (int16_t)rnd_in(r, 160, 256), d->lvl[0] = d->lvl[1] = (int16_t)rnd_in(r, 0, 60);
        d->ftype = FT_HP24, d->cut = 16 * rnd_in(r, kind == FK_CYMBAL ? 90 : 96, 112), d->reso = rnd_in(r, 1638, 9830);
        fb_env3(d, 2, 1, kind == FK_CHAT ? fb_rms(r, 25, 90) : kind == FK_OHAT ? fb_rms(r, 250, 700) : fb_rms(r, 900, 2500),
                0, kind == FK_CHAT ? fb_rms(r, 20, 60) : fb_rms(r, 150, 500));
        break;
    default:                                       /* RIM: a short, high click of a body */
        d->fixp = (uint8_t)rnd_in(r, 70, 84);
        if (rnd_chance(r, 50))
            d->om[0] = OM_VA, d->pos[0] = 32 << 8;
        d->dirt = DT_CLICK, d->damt = (int16_t)rnd_in(r, 60, 160);
        d->ftype = FT_HP12, d->cut = 16 * rnd_in(r, 62, 76), d->reso = rnd_in(r, 3277, 13107);
        fb_env3(d, 2, 1, fb_rms(r, 15, 50), 0, 20);
        break;
    }
}

static void fb_design(fb_design_t *d, int32_t kind, uint32_t seed, uint32_t path, uint32_t gen)
{
    rnd_t rr, *r = &rr;
    path = path < PATH_N ? path : PATH_OPEN;
    rnd_seed(r, seed ^ 0xF1BE5u);
    {   /* (the family drawn always, given or not: the seed and the family then make the sound again) */
        int32_t drawn = (int32_t)rnd_weighted(r, FK_W[path], FK_N);
        if (kind < 0 || kind >= FK_N)
            kind = drawn;
    }
    memset(d, 0, sizeof *d);
    d->seed = seed;
    d->kind = (uint8_t)kind;
    d->path = (uint8_t)path;
    d->gen = (uint8_t)gen;
    /* the common ground */
    d->tab[0] = (uint8_t)fb_tabof(r, (uint32_t)kind);
    d->tab[1] = (uint8_t)(rnd_chance(r, 55) ? d->tab[0] : fb_tabof(r, (uint32_t)kind));
    d->pos[0] = (int16_t)(rnd_in(r, 0, 40) << 8);
    d->pos[1] = (int16_t)(rnd_in(r, 0, 40) << 8);
    d->lvl[0] = 200, d->lvl[1] = (int16_t)rnd_in(r, 120, 200);
    d->det[1] = (int16_t)rnd_in(r, -6, 6);
    d->limit = (uint8_t)rnd_chance(r, 70);
    d->ftype = FT_LP24;
    d->cut = 16 * rnd_in(r, 60, 90);
    d->reso = rnd_in(r, 0, 9830);
    d->keytrk = rnd_in(r, 8192, 24576);
    d->fvel = 16 * rnd_in(r, 0, 12);
    d->fenv = 16 * rnd_in(r, 0, 24);
    d->wvel = (int16_t)rnd_in(r, 0, 16384);
    d->vol = 26214, d->vvel = rnd_in(r, 6554, 19661);
    d->bend = 2;
    d->flav = (int16_t)rnd_in(r, 3277, 13107);
    d->lshape[0] = (uint8_t)rnd_below(r, FBL_N), d->lshape[1] = (uint8_t)rnd_below(r, FBL_N);
    d->lhz[0] = rnd_in(r, Q16 / 8, 3 * Q16), d->lhz[1] = rnd_in(r, Q16 / 4, 6 * Q16);
    d->pan = Q15 / 2;
    fb_env3(d, 0, fb_rms(r, 2, 40), fb_rms(r, 200, 2000), rnd_in(r, 0, 16384), fb_rms(r, 100, 1500));
    fb_env3(d, 1, fb_rms(r, 2, 30), fb_rms(r, 150, 1500), rnd_in(r, 0, 16384), fb_rms(r, 100, 1200));
    fb_env3(d, 2, fb_rms(r, 1, 10), fb_rms(r, 200, 1500), rnd_in(r, 16384, Q15), fb_rms(r, 80, 900));
    if (kind >= FK_ROW0)
        fb_row_design(d, r, &FK_ROW[kind - FK_ROW0]);
    else switch (kind) {
    case FK_EBMBASS:                               /* a sequenced bass: tight envelopes into the 24 dB low-pass */
        d->mode = (uint8_t)(rnd_chance(r, 60) ? VM_MONO : VM_POLY);
        d->semi[1] = (int16_t)(rnd_chance(r, 50) ? -12 : 0);
        d->det[1] = (int16_t)rnd_in(r, 3, 12);
        d->cut = 16 * rnd_in(r, 34, 54), d->fenv = 16 * rnd_in(r, 24, 50), d->reso = rnd_in(r, 6554, 16384);
        d->keytrk = rnd_in(r, 16384, 32767);
        fb_env3(d, 1, fb_rms(r, 1, 4), fb_rms(r, 60, 260), rnd_in(r, 0, 4096), fb_rms(r, 30, 120));
        fb_env3(d, 2, fb_rms(r, 1, 4), fb_rms(r, 150, 500), rnd_in(r, 9830, 22937), fb_rms(r, 30, 140));
        fb_env3(d, 0, 1, fb_rms(r, 40, 300), 0, 60);
        d->wenv[0] = d->wenv[1] = (int16_t)(rnd_in(r, 4, 16) << 8);
        if (rnd_chance(r, 30))
            d->ftype = FT_DRIVE, d->dcurve = (uint8_t)rnd_below(r, DC_N), d->cut = 16 * rnd_in(r, 54, 72);
        fb_mod(d, FBS_VEL, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, 6, 18)));
        break;
    case FK_STAB:                                  /* EBM and industrial chord stabs: a snap of the filter, driven */
        d->cut = 16 * rnd_in(r, 50, 70), d->fenv = 16 * rnd_in(r, 30, 54), d->reso = rnd_in(r, 8192, 19661);
        fb_env3(d, 1, 1, fb_rms(r, 80, 300), rnd_in(r, 0, 3277), fb_rms(r, 60, 200));
        fb_env3(d, 2, 1, fb_rms(r, 120, 450), 0, fb_rms(r, 80, 300));
        d->semi[1] = (int16_t)(rnd_chance(r, 40) ? 12 : rnd_chance(r, 30) ? 7 : 0);
        if (rnd_chance(r, 45))
            d->dirt = DT_CLICK, d->damt = (int16_t)rnd_in(r, 60, 160);
        if (rnd_chance(r, 35))
            d->ftype = FT_DRIVE, d->dcurve = (uint8_t)rnd_below(r, DC_N), d->cut = 16 * rnd_in(r, 60, 80);
        fb_modw(d, FBS_E2, rnd_in(r, 4, 20) << 8);
        break;
    case FK_METAL:                                 /* clangs: the two oscillators ring-modulated at an odd interval */
        d->dirt = DT_RING, d->damt = (int16_t)rnd_in(r, 120, 240);
        d->semi[1] = (int16_t)rnd_in(r, 5, 31);
        d->det[1] = (int16_t)rnd_in(r, -80, 80);
        d->lvl[0] = (int16_t)rnd_in(r, 40, 140), d->lvl[1] = (int16_t)rnd_in(r, 40, 140);
        d->ftype = (uint8_t)(rnd_chance(r, 50) ? FT_HP12 : FT_LP12);
        d->cut = d->ftype == FT_HP12 ? 16 * rnd_in(r, 40, 70) : 16 * rnd_in(r, 90, 115);
        d->reso = rnd_in(r, 6554, 19661), d->fenv = 0;
        fb_env3(d, 2, 1, fb_rms(r, 80, 900), 0, fb_rms(r, 80, 700));
        fb_env3(d, 0, 1, fb_rms(r, 30, 400), 0, 100);
        d->wenv[0] = (int16_t)(rnd_in(r, 8, 40) << 8), d->wenv[1] = (int16_t)(rnd_in(r, -30, 30) << 8);
        d->limit = 1;
        fb_mod(d, FBS_RBI, FBD_PITCH2, fb_amt(FBD_PITCH2, rnd_in(r, 4, 40)));
        break;
    case FK_NOISE:                                 /* noise hits: the shift-register tables, bursts, a high-pass */
        d->dirt = (uint8_t)(DT_STATIC + rnd_below(r, 7));
        if (d->dirt == DT_CLICK)
            d->dirt = DT_BURST3;
        d->damt = (int16_t)rnd_in(r, 120, 256);
        d->ftype = (uint8_t)(rnd_chance(r, 50) ? FT_HP24 : rnd_chance(r, 50) ? FT_DRIVE : FT_LP12);
        d->dcurve = (uint8_t)rnd_below(r, DC_N);
        d->cut = 16 * rnd_in(r, 50, 100), d->reso = rnd_in(r, 3277, 22937);
        fb_env3(d, 2, 1, fb_rms(r, 40, 500), 0, fb_rms(r, 40, 400));
        fb_env3(d, 0, 1, fb_rms(r, 20, 300), 0, 60);
        d->wenv[0] = d->wenv[1] = (int16_t)(rnd_in(r, -40, 40) << 8);
        d->semi[0] = (int16_t)rnd_in(r, -12, 24), d->semi[1] = (int16_t)rnd_in(r, -24, 24);
        d->rs = (uint8_t)rnd_chance(r, 60);
        d->limit = 0;
        fb_mod(d, FBS_E2, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, -36, 36)));
        break;
    case FK_SUB:                                   /* an industrial rumble: low, slow movement of the wave and filter */
        d->semi[0] = (int16_t)(rnd_chance(r, 50) ? -12 : 0), d->semi[1] = (int16_t)(d->semi[0] - (rnd_chance(r, 50) ? 12 : 0));
        d->det[1] = (int16_t)rnd_in(r, 4, 20);
        d->cut = 16 * rnd_in(r, 30, 52), d->reso = rnd_in(r, 6554, 22937), d->fenv = 16 * rnd_in(r, 0, 12);
        fb_env3(d, 2, fb_rms(r, 200, 2000), 2000, Q15, fb_rms(r, 1500, 5000));
        d->lshape[0] = (uint8_t)(rnd_chance(r, 50) ? FBL_RANDOM : FBL_TRI), d->lhz[0] = rnd_in(r, Q16 / 20, Q16 / 3);
        d->lshape[1] = FBL_SINE, d->lhz[1] = rnd_in(r, Q16 / 16, Q16 / 2), d->lglob[1] = 1;
        fb_modw(d, FBS_L1, rnd_in(r, 6, 24) << 8);
        fb_mod(d, FBS_L2, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, 4, 18)));
        if (rnd_chance(r, 50))
            d->ftype = FT_DRIVE, d->dcurve = (uint8_t)(rnd_chance(r, 50) ? DC_TUBE : DC_PNP), d->cut = 16 * rnd_in(r, 50, 70);
        if (rnd_chance(r, 30))
            d->dirt = DT_CRACKLE, d->damt = (int16_t)rnd_in(r, 30, 120);
        break;
    case FK_GRIND:                                 /* driven waves scanned by an LFO: distorted basses and leads */
        d->ftype = FT_DRIVE, d->dcurve = (uint8_t)rnd_below(r, DC_N);
        d->cut = 16 * rnd_in(r, 56, 84);
        d->semi[1] = (int16_t)(rnd_chance(r, 40) ? -12 : rnd_chance(r, 40) ? 7 : 0), d->det[1] = (int16_t)rnd_in(r, -20, 20);
        d->lshape[0] = (uint8_t)rnd_below(r, FBL_N), d->lhz[0] = rnd_in(r, Q16 / 4, 8 * Q16);
        fb_modw(d, FBS_L1, rnd_in(r, 8, 30) << 8);
        fb_mod(d, FBS_E2, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, 6, 24)));
        fb_env3(d, 1, 1, fb_rms(r, 100, 600), rnd_in(r, 8192, 26214), 200);
        if (rnd_chance(r, 50))
            d->dirt = rnd_chance(r, 50) ? DT_CRACKLE : DT_BURST1, d->damt = (int16_t)rnd_in(r, 40, 140);
        d->rs = (uint8_t)rnd_chance(r, 40);
        d->limit = (uint8_t)rnd_chance(r, 40);
        break;
    case FK_SAWBASS:                               /* the synthwave bass: two waves an octave apart, a plucked filter */
        d->semi[1] = -12, d->det[1] = (int16_t)rnd_in(r, 2, 10);
        d->cut = 16 * rnd_in(r, 40, 60), d->fenv = 16 * rnd_in(r, 20, 40), d->reso = rnd_in(r, 1638, 9830);
        fb_env3(d, 1, 1, fb_rms(r, 120, 500), rnd_in(r, 0, 9830), fb_rms(r, 60, 200));
        fb_env3(d, 2, 1, fb_rms(r, 250, 900), rnd_in(r, 13107, 26214), fb_rms(r, 60, 200));
        d->mode = (uint8_t)(rnd_chance(r, 40) ? VM_MONO : VM_POLY);
        d->wenv[0] = (int16_t)(rnd_in(r, 0, 10) << 8);
        break;
    case FK_PAD:                                   /* a wide poly pad: detuned, slow, the waves drifting */
        d->det[0] = (int16_t)rnd_in(r, -8, -3), d->det[1] = (int16_t)rnd_in(r, 3, 8);
        d->semi[1] = (int16_t)(rnd_chance(r, 25) ? 12 : rnd_chance(r, 20) ? 7 : 0);
        d->cut = 16 * rnd_in(r, 70, 96), d->ftype = (uint8_t)(rnd_chance(r, 60) ? FT_LP12 : FT_LP24);
        fb_env3(d, 2, fb_rms(r, 300, 1800), fb_rms(r, 800, 3000), rnd_in(r, 22937, Q15), fb_rms(r, 900, 3500));
        fb_env3(d, 1, fb_rms(r, 400, 2500), fb_rms(r, 1000, 4000), rnd_in(r, 13107, Q15), fb_rms(r, 900, 3000));
        d->fenv = 16 * rnd_in(r, 0, 18);
        d->lshape[0] = FBL_TRI, d->lhz[0] = rnd_in(r, Q16 / 10, Q16 / 2);
        d->lshape[1] = FBL_SINE, d->lhz[1] = rnd_in(r, Q16 * 3 / 10, Q16), d->lglob[1] = 1;
        fb_modw(d, FBS_L1, rnd_in(r, 2, 10) << 8);
        fb_mod(d, FBS_L2, FBD_PITCH2, fb_amt(FBD_PITCH2, rnd_in(r, 1, 2)));
        if (rnd_chance(r, 50))
            d->ens = (int16_t)rnd_in(r, 13107, 26214), d->emode = EM_CHORUS;
        d->flav = (int16_t)rnd_in(r, 9830, 22937);
        break;
    case FK_LEAD:                                  /* a lead: a wave scanned at the attack, vibrato with the wheel */
        d->cut = 16 * rnd_in(r, 74, 100), d->fenv = 16 * rnd_in(r, 6, 24), d->reso = rnd_in(r, 3277, 13107);
        d->semi[1] = (int16_t)(rnd_chance(r, 40) ? 12 : rnd_chance(r, 30) ? 7 : 0), d->det[1] = (int16_t)rnd_in(r, 2, 12);
        fb_env3(d, 0, fb_rms(r, 10, 150), fb_rms(r, 200, 900), rnd_in(r, 4096, 16384), 300);
        d->wenv[0] = (int16_t)(rnd_in(r, 8, 30) << 8), d->wenv[1] = (int16_t)(rnd_in(r, 4, 20) << 8);
        fb_env3(d, 2, fb_rms(r, 3, 30), 800, rnd_in(r, 22937, Q15), fb_rms(r, 150, 600));
        d->lshape[0] = FBL_SINE, d->lhz[0] = rnd_in(r, 4 * Q16, 6 * Q16);
        fb_mod(d, FBS_L1MW, FBD_PITCH, fb_amt(FBD_PITCH, 3));          /* (vibrato: under a quarter semitone) */
        fb_mod(d, FBS_L1, FBD_PITCH, fb_amt(FBD_PITCH, rnd_in(r, 0, 1)));
        d->mode = (uint8_t)(rnd_chance(r, 50) ? VM_LEGATO : VM_POLY);
        break;
    case FK_PLUCK:                                 /* arpeggio plucks: the wave and filter falling fast */
        d->cut = 16 * rnd_in(r, 56, 80), d->fenv = 16 * rnd_in(r, 20, 44), d->reso = rnd_in(r, 3277, 14746);
        d->ftype = (uint8_t)(rnd_chance(r, 50) ? FT_LP12 : FT_LP24);
        fb_env3(d, 0, 1, fb_rms(r, 60, 400), 0, 100);
        d->wenv[0] = (int16_t)(rnd_in(r, 10, 40) << 8), d->wenv[1] = (int16_t)(rnd_in(r, 0, 20) << 8);
        d->pos[0] = d->pos[1] = 0;
        fb_env3(d, 1, 1, fb_rms(r, 80, 400), 0, fb_rms(r, 60, 300));
        fb_env3(d, 2, 1, fb_rms(r, 200, 900), 0, fb_rms(r, 150, 500));
        d->semi[1] = (int16_t)(rnd_chance(r, 40) ? 12 : 0);
        break;
    case FK_BRASS:                                 /* synth brass: the filter envelope's slow attack */
        d->cut = 16 * rnd_in(r, 46, 62), d->fenv = 16 * rnd_in(r, 30, 48), d->reso = rnd_in(r, 0, 6554);
        fb_env3(d, 1, fb_rms(r, 40, 180), fb_rms(r, 250, 900), rnd_in(r, 9830, 19661), fb_rms(r, 200, 600));
        fb_env3(d, 2, fb_rms(r, 15, 80), fb_rms(r, 300, 900), rnd_in(r, 24576, 31130), fb_rms(r, 200, 700));
        d->det[1] = (int16_t)rnd_in(r, 4, 14);
        d->lshape[0] = FBL_SINE, d->lhz[0] = rnd_in(r, 4 * Q16, 6 * Q16);
        fb_mod(d, FBS_L1MW, FBD_PITCH, fb_amt(FBD_PITCH, 3));
        break;
    case FK_TENSION:                               /* film tension: a slow scan through resonant, hollow tables */
        d->ftype = (uint8_t)(rnd_chance(r, 50) ? FT_HP12 : FT_LP12);
        d->cut = d->ftype == FT_HP12 ? 16 * rnd_in(r, 36, 60) : 16 * rnd_in(r, 64, 90);
        d->reso = rnd_in(r, 9830, 24576);
        fb_env3(d, 0, fb_rms(r, 1500, 6000), fb_rms(r, 2000, 6000), rnd_in(r, 16384, Q15), fb_rms(r, 2000, 6000));
        d->wenv[0] = (int16_t)(rnd_in(r, 15, 50) << 8), d->wenv[1] = (int16_t)(rnd_in(r, -40, 40) << 8);
        d->pos[0] = d->pos[1] = 0;
        fb_env3(d, 2, fb_rms(r, 600, 3000), 3000, Q15, fb_rms(r, 1500, 6000));
        d->lshape[0] = FBL_RANDOM, d->lhz[0] = rnd_in(r, Q16 / 10, Q16 / 2);
        fb_mod(d, FBS_L1, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, 4, 16)));
        d->semi[1] = (int16_t)(rnd_chance(r, 50) ? 1 : rnd_chance(r, 50) ? 6 : 11);
        if (rnd_chance(r, 40))
            d->dirt = DT_STATIC, d->damt = (int16_t)rnd_in(r, 10, 50);
        d->limit = 1;
        break;
    case FK_CHOIR:                                 /* vowels: the formant tables, the position moving slowly */
        d->det[1] = (int16_t)rnd_in(r, 4, 12);
        d->cut = 16 * rnd_in(r, 76, 100), d->ftype = FT_LP12;
        fb_env3(d, 2, fb_rms(r, 250, 1500), 1500, rnd_in(r, 26214, Q15), fb_rms(r, 700, 2500));
        d->lshape[0] = (uint8_t)(rnd_chance(r, 50) ? FBL_TRI : FBL_RANDOM), d->lhz[0] = rnd_in(r, Q16 / 6, Q16);
        d->lshape[1] = FBL_SINE, d->lhz[1] = rnd_in(r, 4 * Q16, 6 * Q16);
        fb_modw(d, FBS_L1, rnd_in(r, 8, 24) << 8);
        fb_mod(d, FBS_L2, FBD_PITCH, fb_amt(FBD_PITCH, rnd_in(r, 0, 2)));
        if (rnd_chance(r, 50))
            d->ens = (int16_t)rnd_in(r, 13107, 22937), d->emode = EM_STRCH;
        d->flav = (int16_t)rnd_in(r, 13107, 26214);
        break;
    case FK_BELLS:                                 /* bells and mallets: an interval apart, long decays, no sustain */
        d->semi[1] = (int16_t)(rnd_chance(r, 30) ? 12 : rnd_chance(r, 30) ? 19 : rnd_chance(r, 50) ? 24 : 7);
        d->det[1] = (int16_t)rnd_in(r, -12, 12);
        d->cut = 16 * rnd_in(r, 84, 110), d->ftype = FT_LP12, d->reso = rnd_in(r, 0, 6554);
        fb_env3(d, 2, 1, fb_rms(r, 900, 4000), 0, fb_rms(r, 700, 3000));
        fb_env3(d, 0, 1, fb_rms(r, 300, 2000), 0, 500);
        d->wenv[0] = (int16_t)(rnd_in(r, 6, 24) << 8), d->wenv[1] = (int16_t)(rnd_in(r, 0, 16) << 8);
        if (rnd_chance(r, 35))
            d->dirt = DT_RING, d->damt = (int16_t)rnd_in(r, 40, 140);
        break;
    case FK_SWELL:                                 /* film swells: the filter opening slowly, resonant */
        d->cut = 16 * rnd_in(r, 34, 52), d->fenv = 16 * rnd_in(r, 30, 56), d->reso = rnd_in(r, 13107, 22937);
        fb_env3(d, 1, fb_rms(r, 1200, 5000), fb_rms(r, 1500, 5000), rnd_in(r, 16384, Q15), fb_rms(r, 1200, 4000));
        fb_env3(d, 2, fb_rms(r, 500, 2500), 2000, Q15, fb_rms(r, 1200, 4000));
        d->det[0] = (int16_t)rnd_in(r, -6, -2), d->det[1] = (int16_t)rnd_in(r, 2, 6);
        fb_modw(d, FBS_E2, rnd_in(r, 6, 24) << 8);
        break;
    case FK_KICK: case FK_SNARE: case FK_CLAP: case FK_CHAT: case FK_OHAT: case FK_TOM: case FK_RIM: case FK_CYMBAL:
        fb_drum(d, r, (uint32_t)kind);
        break;
    case FK_ANVIL:                                 /* an industrial metal stab: odd intervals ring-modulated, driven */
        d->perc = 1;
        d->semi[1] = (int16_t)(rnd_chance(r, 40) ? 6 : rnd_chance(r, 50) ? 13 : 11), d->det[1] = (int16_t)rnd_in(r, -40, 40);
        d->dirt = DT_RING, d->damt = (int16_t)rnd_in(r, 110, 220);
        d->lvl[0] = (int16_t)rnd_in(r, 120, 200), d->lvl[1] = (int16_t)rnd_in(r, 60, 160);
        d->ftype = (uint8_t)(rnd_chance(r, 60) ? FT_DRIVE : FT_HP12), d->dcurve = (uint8_t)rnd_below(r, DC_N);
        d->cut = d->ftype == FT_DRIVE ? 16 * rnd_in(r, 60, 84) : 16 * rnd_in(r, 44, 64), d->reso = rnd_in(r, 6554, 19661);
        fb_env3(d, 2, 1, fb_rms(r, 120, 500), 0, fb_rms(r, 80, 300));
        fb_env3(d, 1, 1, fb_rms(r, 40, 200), 0, 80);
        fb_env3(d, 0, 1, fb_rms(r, 30, 200), 0, 60);
        d->fenv = 16 * rnd_in(r, 6, 24);
        d->wenv[0] = (int16_t)(rnd_in(r, 6, 30) << 8);
        d->dirt = rnd_chance(r, 25) ? DT_CLICK : d->dirt;
        d->limit = 0, d->flav = 0;
        break;
    case FK_MACHINE:                               /* a running machine: the track's LFOs gating and stepping it */
        d->om[0] = (uint8_t)(rnd_chance(r, 40) ? OM_VA : OM_WAVE);
        d->semi[1] = (int16_t)(rnd_chance(r, 50) ? -12 : 7);
        /* (the gate: open at the note, closing to a quarter at most -- never silent) */
        d->lshape[0] = (uint8_t)(rnd_chance(r, 60) ? FBL_PULSE : FBL_SH), d->lhz[0] = rnd_in(r, 3 * Q16, 12 * Q16), d->lsync[0] = 1;
        d->lshape[1] = FBL_SH, d->lhz[1] = rnd_in(r, Q16, 6 * Q16), d->lglob[1] = 1;
        fb_mod(d, FBS_L1, FBD_VOL, rnd_in(r, 9830, 18022));
        fb_mod(d, FBS_L2, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, 6, 24)));
        fb_modw(d, FBS_L2, rnd_in(r, 4, 20) << 8);
        d->ftype = (uint8_t)(rnd_chance(r, 50) ? FT_DRIVE : FT_LP24), d->dcurve = (uint8_t)rnd_below(r, DC_N);
        d->cut = 16 * rnd_in(r, 50, 76), d->reso = rnd_in(r, 6554, 22937);
        d->dirt = (uint8_t)(rnd_chance(r, 50) ? DT_CRACKLE : DT_BURST1), d->damt = (int16_t)rnd_in(r, 40, 140);
        fb_env3(d, 2, fb_rms(r, 5, 60), 1000, Q15, fb_rms(r, 100, 600));
        d->rs = (uint8_t)rnd_chance(r, 50);
        break;
    case FK_STRINGS:                               /* a string section: the STRINGS bank, violin to cello, the ensemble */
    case FK_LOSTR:                                 /* film's low strings: the cello registration, slower, darker */
        d->om[0] = d->om[1] = OM_STR;
        d->pos[0] = (int16_t)(kind == FK_LOSTR ? rnd_in(r, 22, 34) << 8 : rnd_in(r, 0, 26) << 8);
        d->pos[1] = (int16_t)clampi(d->pos[0] + (rnd_in(r, -4, 4) << 8), 0, 63 << 8);
        d->det[0] = (int16_t)rnd_in(r, -10, -3), d->det[1] = (int16_t)rnd_in(r, 3, 10);
        d->semi[1] = (int16_t)(rnd_chance(r, 30) ? 12 : 0);
        d->ftype = (uint8_t)(kind == FK_LOSTR ? FT_LP24 : FT_LP12);
        d->cut = kind == FK_LOSTR ? 16 * rnd_in(r, 70, 90) : 16 * rnd_in(r, 86, 108), d->reso = rnd_in(r, 0, 4915);
        d->fenv = 16 * rnd_in(r, 0, 12);
        fb_env3(d, 2, kind == FK_LOSTR ? fb_rms(r, 400, 1600) : fb_rms(r, 150, 900), 1500, rnd_in(r, 26214, Q15),
                fb_rms(r, 700, 2500));
        fb_env3(d, 1, fb_rms(r, 200, 1500), 2000, rnd_in(r, 16384, Q15), 1500);
        d->lshape[0] = FBL_TRI, d->lhz[0] = rnd_in(r, Q16 / 8, Q16 / 2);   /* (Animate: the registration moving) */
        fb_modw(d, FBS_L1, rnd_in(r, 2, 8) << 8);
        d->lshape[1] = FBL_SINE, d->lhz[1] = rnd_in(r, 4 * Q16, 6 * Q16);  /* (vibrato, small) */
        fb_mod(d, FBS_L2, FBD_PITCH, fb_amt(FBD_PITCH, rnd_in(r, 1, 2)));
        d->ens = (int16_t)rnd_in(r, 19661, 29491), d->emode = (uint8_t)(rnd_chance(r, 60) ? EM_STRINGS : EM_STRCH);
        d->flav = (int16_t)rnd_in(r, 6554, 16384);
        break;
    case FK_STRHIT:                                /* an orchestral hit: the strings struck, short, often driven */
        d->om[0] = d->om[1] = OM_STR;
        d->pos[0] = (int16_t)(rnd_in(r, 0, 44) << 8), d->pos[1] = (int16_t)(rnd_in(r, 0, 44) << 8);
        d->semi[1] = (int16_t)(rnd_chance(r, 50) ? 12 : 7), d->det[1] = (int16_t)rnd_in(r, 4, 12);
        d->cut = 16 * rnd_in(r, 60, 84), d->fenv = 16 * rnd_in(r, 18, 40), d->reso = rnd_in(r, 3277, 13107);
        fb_env3(d, 1, 1, fb_rms(r, 100, 400), rnd_in(r, 0, 6554), 150);
        fb_env3(d, 2, 1, fb_rms(r, 200, 700), 0, fb_rms(r, 150, 500));
        if (rnd_chance(r, 45))
            d->ftype = FT_DRIVE, d->dcurve = (uint8_t)rnd_below(r, DC_N), d->cut = 16 * rnd_in(r, 56, 76);
        d->ens = (int16_t)rnd_in(r, 9830, 19661), d->emode = EM_STRCH;
        d->perc = (uint8_t)rnd_chance(r, 50);
        break;
    case FK_PULSEBASS:                             /* the VA pulse bass: the width moving, the 24 dB low-pass, MONO */
        d->om[0] = d->om[1] = OM_VA;
        d->pos[0] = (int16_t)(rnd_in(r, 34, 54) << 8), d->pos[1] = (int16_t)(rnd_in(r, 20, 50) << 8);
        d->semi[1] = -12, d->det[1] = (int16_t)rnd_in(r, 2, 8);
        d->lshape[0] = FBL_TRI, d->lhz[0] = rnd_in(r, Q16 / 4, 2 * Q16);   /* (PWM) */
        fb_mod(d, FBS_L1, FBD_POS1, fb_amt(FBD_POS1, rnd_in(r, 6, 18) << 8));
        d->cut = 16 * rnd_in(r, 36, 56), d->fenv = 16 * rnd_in(r, 18, 40), d->reso = rnd_in(r, 3277, 13107);
        d->keytrk = rnd_in(r, 16384, 32767);
        fb_env3(d, 1, 1, fb_rms(r, 80, 350), rnd_in(r, 0, 6554), 80);
        fb_env3(d, 2, 1, fb_rms(r, 200, 600), rnd_in(r, 9830, 22937), fb_rms(r, 40, 150));
        d->mode = (uint8_t)(rnd_chance(r, 60) ? VM_MONO : VM_POLY);
        break;
    case FK_POLYSYN:                               /* synthwave chords: two VA saws detuned, a filter swell, chorus */
        d->om[0] = d->om[1] = OM_VA;
        d->pos[0] = (int16_t)(rnd_in(r, 0, 12) << 8), d->pos[1] = (int16_t)(rnd_in(r, 0, 20) << 8);
        d->det[0] = (int16_t)rnd_in(r, -12, -4), d->det[1] = (int16_t)rnd_in(r, 4, 12);
        d->semi[1] = (int16_t)(rnd_chance(r, 25) ? 12 : 0);
        d->cut = 16 * rnd_in(r, 60, 84), d->fenv = 16 * rnd_in(r, 12, 30), d->reso = rnd_in(r, 1638, 9830);
        fb_env3(d, 1, fb_rms(r, 20, 250), fb_rms(r, 400, 1500), rnd_in(r, 9830, 22937), fb_rms(r, 300, 900));
        fb_env3(d, 2, fb_rms(r, 5, 80), 1200, rnd_in(r, 22937, Q15), fb_rms(r, 300, 1200));
        d->ens = (int16_t)rnd_in(r, 16384, 26214), d->emode = EM_CHORUS;
        break;
    default:                                       /* OSTINATO: a short, tight pulse for repeated figures */
        d->cut = 16 * rnd_in(r, 50, 72), d->fenv = 16 * rnd_in(r, 18, 36), d->reso = rnd_in(r, 6554, 16384);
        d->ftype = FT_LP12;
        fb_env3(d, 1, 1, fb_rms(r, 50, 180), 0, 60);
        fb_env3(d, 2, 1, fb_rms(r, 80, 250), 0, fb_rms(r, 40, 120));
        d->lshape[0] = FBL_SH, d->lhz[0] = rnd_in(r, Q16, 6 * Q16), d->lglob[0] = 1;
        fb_mod(d, FBS_L1, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, 2, 12)));
        break;
    }
    /* a slot or two at random: the matrix's variety (not on drums: the same hit each time) */
    if (!d->perc && rnd_chance(r, 50))
        fb_mod(d, FBS_RBI, FBD_POS1, fb_amt(FBD_POS1, rnd_in(r, 1, 6) << 8));
    if (!d->perc && rnd_chance(r, 30))
        fb_mod(d, FBS_AT, FBD_CUT, fb_amt(FBD_CUT, 16 * rnd_in(r, 6, 24)));
    if (d->perc && d->fixp) {                      /* (a drum: no path's push on its timing or its DIRT) */
        if (path == PATH_EXP && d->fixp < 70 && rnd_chance(r, 40) && d->ftype != FT_DRIVE)
            d->ftype = FT_DRIVE, d->dcurve = (uint8_t)rnd_below(r, DC_N), d->cut = 16 * rnd_in(r, 56, 80);
    } else if (path == PATH_AMBIENT) {             /* softer, slower, darker */
        d->cut -= 16 * rnd_in(r, 0, 10);
        d->ea[2] = d->ea[2] < 200 ? d->ea[2] + 200 : d->ea[2];
        d->er[2] = d->er[2] * 3 / 2;
        if (d->dirt != DT_RING)
            d->damt >>= 1;
        d->lhz[0] = d->lhz[0] < Q16 ? d->lhz[0] : d->lhz[0] / 3;
    } else if (path == PATH_EXP) {                 /* harder: resonance, dirt, the coarser resampler, no limit */
        d->reso = clampi(d->reso + rnd_in(r, 0, 9830), 0, 29491);
        if (!d->dirt && rnd_chance(r, 40))
            d->dirt = (uint8_t)(DT_STATIC + rnd_below(r, DT_N - 1)), d->damt = (int16_t)rnd_in(r, 40, 160);
        if (rnd_chance(r, 35))
            d->rs = 1;
        d->limit = 0;
    }
    d->cut = clampi(d->cut, 16 * 12, 16 * 132);
    d->out = 4096;
    {   /* a kick's punch (build 12), drawn from a generator of its own, so the rest is drawn as before (no pitch
         * moves: the punch is in the level). Each kick family its own: which transients of the bank it draws from
         * (added after the filter, so the closing filter does not take the click), how loud; and its body's front --
         * the level at full on the hit, falling to the base over the front's time (the wave envelope on the level),
         * then the amplifier's own decay. A 909 a short pulse and high noise, a boom a soft felt push and hardly a
         * knock, a distorted kick a hard crack and a deep knock, a rumble a low push */
        static const struct {
            uint8_t kind, n, tr[6];
            uint16_t lv[2], base[2], ms[2];      /* the transient's level, the base level (Q15); the front's time */
        } KP[5] = {
            {FK_KICK, 6, {0, 1, 2, 4, 5, 6}, {9830, 26214}, {14746, 26214}, {25, 80}},   /* (the drawn kick: wide) */
            {FK_KICK909, 3, {8, 9, 10}, {16384, 24576}, {21299, 26214}, {30, 50}},
            {FK_KICKBOOM, 2, {11, 12}, {4915, 11469}, {27853, 31130}, {60, 120}},
            {FK_KICKDIST, 4, {3, 7, 14, 15}, {24576, 32767}, {11469, 16384}, {20, 45}},
            {FK_KICKRUMBL, 3, {12, 13, 2}, {8192, 14746}, {22938, 27853}, {50, 100}},
        };
        uint32_t j;
        for (j = 0; j < 5u && KP[j].kind != kind; j++)
            ;
        if (j < 5u) {
            rnd_t t;
            int32_t base;
            rnd_seed(&t, seed ^ 0x6B1C4E5Du);
            d->trans = (int32_t)(KP[j].tr[rnd_below(&t, KP[j].n)] + 1u) | rnd_in(&t, KP[j].lv[0], KP[j].lv[1]) << 8;
            if (!d->wenv[0] && !d->wenv[1])       /* (the wave envelope free: no scan on it -- else its own time) */
                fb_env3(d, 0, 1, fb_rms(&t, KP[j].ms[0], KP[j].ms[1]), 0, 20);
            base = rnd_in(&t, KP[j].base[0], KP[j].base[1]);
            fb_mod(d, FBS_E1, FBD_VOL, Q15 - base);
            d->vol = mulq15(d->vol, base);
        }
    }
}

static const char *fb_mac_name(const fb_design_t *d, uint32_t j)
{
    static const char *const N[NMAC] = {"BRIGHTNESS", "BITE", "TIMBRE", "MOTION", "DIRT", "ATTACK", "LENGTH", "BALANCE"};
    return j == MC_BRIGHT && d && d->ftype == FT_DRIVE ? "DRIVE" : N[j % NMAC];
}
/* the design's own envelope as A D S R (ms, sustain %): the amplifier's (the ENVELOPE page, as drawn) */
static void fb_env_adsr(const fb_design_t *d, int32_t *a)
{
    a[0] = d->ea[2];
    a[1] = d->ed[2];
    a[2] = d->es[2] * 100 / Q15;
    a[3] = d->er[2];
}
/* the design's level: C3 held 1 s (longer for slow attacks: up to 3 s), let go; the 75th percentile of 12 ms windows brought to 0.08 RMS (0.1 before build 21: 2 dB more room for the drums), no more than
 * keeps its loudest moment under half of full scale (as OBRADOR's) */
/* (build 21) a drum's hit level, its loudest 12 ms: hundredths of full scale, RMS -- a kick the most (its weight is
 * low, where the ear is least keen), snares, claps, toms, hand drums and hits next, hats and cymbals least (high and
 * noisy: their peaks far over their RMS, and the ear keenest there) */
static uint32_t fb_perc_rms(uint32_t kind)
{
    static const char *const HI[] = {"HAT", "CYMBAL", "RIDE", "CRASH", "SHAKER", "TAMBOURIN"};
    const char *n = FK_NAME[kind % FK_N];
    uint32_t i, j;
    if (n[0] == 'K' && n[1] == 'I' && n[2] == 'C' && n[3] == 'K')
        return FB_RMS_KICK;
    for (i = 0; i < NELEM(HI); i++)
        for (j = 0; n[j]; j++) {
            uint32_t k;
            for (k = 0; HI[i][k] && n[j + k] == HI[i][k]; k++)
                ;
            if (!HI[i][k])
                return FB_RMS_HAT;
        }
    return FB_RMS_DRUM;
}
static void fb_measure_on(fb_design_t *d, fb_voice_t *probe)
{
    int32_t win[(3000u * 1378u / 1000u + 689u) / 16u + 1u];
    int32_t out[CTL];
    uint32_t b, k, nwin = 0, hold, nb;
    int64_t acc = 0;
    int32_t peak = 1;
    d->out = 4096;
    hold = (uint32_t)clampi(d->ea[2] + (d->fenv > 0 ? d->ea[1] : 0) + 600, 1000, 3000) * 1378u / 1000u;
    nb = hold + 689u;
    fb_init(probe, d->seed ^ 0x77u);
    fb_note_on(probe, d, 48, 127, 1, 0, 0);
    for (b = 0; b < nb; b++) {
        uint32_t i;
        if (b == hold)
            fb_note_off(probe, 64);
        if (!fb_render_fp(probe, d, out, CTL, 6))   /* (6: the probe -- its own clock, no MIDI) */
            memset(out, 0, sizeof out);
        for (i = 0; i < CTL; i++) {
            acc += ((int64_t)out[i] * out[i]) >> 6;
            if (absi(out[i]) > peak)
                peak = absi(out[i]);
        }
        if ((b & 15u) == 15u) {
            if (nwin < NELEM(win))
                win[nwin++] = (int32_t)((acc >> 9) > 0x7FFFFFFF ? 0x7FFFFFFF : (acc >> 9));
            acc = 0;
        }
#ifndef HOST
        if ((b & 255u) == 255u)
            fm1_wdt_feed();
#endif
    }
    for (b = 1; b < nwin; b++)
        for (k = b; k > 0 && win[k - 1] > win[k]; k--) {
            int32_t t = win[k];
            win[k] = win[k - 1];
            win[k - 1] = t;
        }
    {
        uint32_t root = nwin ? isqrt32((uint32_t)win[nwin * 3u / 4u]) : 0;
        int32_t g = root ? (int32_t)(FB_TONE_T / root) : 4096 * 8;
        int32_t gp = (int32_t)((uint32_t)(Q15 / 2) * 4096u / (uint32_t)peak);
        if (d->fixp && nwin) {                     /* (build 21) a drum: by its hit, not by its tail -- the loudest
                                                    * 12 ms brought to fb_perc_rms's level, its peak let up to full scale
                                                    * (the click above the body: rounded off by fb_render's knee).
                                                    * Before, held to half scale by its click, a kick was some 7 dB
                                                    * under a pad heard with it */
            uint32_t top = isqrt32((uint32_t)win[nwin - 1u]);
            int32_t gb = top ? (int32_t)(fb_perc_rms(d->kind) * 1677722u / 10u / top) : 4096 * 8;
            if (fb_perc_rms(d->kind) == FB_RMS_HAT)   /* (hats, cymbals: no louder held than a pad, their peaks
                                                    * held to half scale, as before) */
                g = gb < g ? gb : g;
            else
                g = gb, gp = (int32_t)((uint32_t)Q15 * 4096u / (uint32_t)peak);
        }
        d->out = clampi(g < gp ? g : gp, 64, 4096 * 12);
    }
}
