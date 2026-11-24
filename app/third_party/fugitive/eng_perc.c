/* SPDX-License-Identifier: GPL-3.0-only
 * FUGITIVE: PERC, the Polyend "Drum Machine" factory engine (PERC.psyn), in fixed point for the FM-1.
 * One voice holds the whole kit, as the original's single voice: kick, tom, snare, hi-hat, cymbal and a
 * percussion slot (clap / clap+ / maracas / shaker / cowbell), each with its own envelopes and state, so
 * they ring over each other. A MIDI note picks the instrument (docs/engines/PERC.md):
 *   60, 61 kick   62, 63 snare   64, 65 closed hat   66, 67 open hat   68, 69 cymbal   70, 71 perc
 *   anything else: the tom, at that note's pitch (notes above 71 an octave down: 72 plays as 60 ...)
 * Note off is ignored (one-shots). Stereo: each instrument has a level, a linear pan and an output point
 * (main / after the waveshaper / after the EQ); then the waveshaper, the 3-band EQ (two peaking biquads a
 * band, one per channel) and x2, as the original. Its two aux sends (delay, reverb) are not rendered: the
 * track's own sends stand in for them (docs/engines/PERC.md).
 *
 * Formats: samples Q15 (int32, headroom above), envelopes Q30, phases uint32 (2^32 a cycle), biquad and SVF
 * coefficients Q30 / Q27, filter states Q23 / Q24. The original renders 128-frame blocks; its once-a-block
 * work (gain ramps, the kick's click, filter coefficients) is kept on 128-frame spans counted from the event.
 * The original's parameter handlers are re-run on the voice when a value changes (perc_sync): the cooked data
 * carries the values and the coefficients worked out from them, the voice applies what changed.
 */
#include "dsp.h"
#include "eng.h"
#include "eng_perc_tab.h"

#define PERC_NPAR 84
#define PERC_FS 44100
#define PERC_ONE (1 << 30)               /* 1.0, Q30 */
#define PERC_THR 107374                  /* 0.0001 Q30: the original's "envelope still sounding" threshold */
#define PERC_THR_RETRIG 107374182        /* 0.1 Q30: a kick / tom note-on below it restarts without a fade */
#define PERC_LOG2E 1549082005            /* log2(e), Q30 */
#define PERC_LN2 744261118               /* ln(2), Q30 */
#define PERC_INC_HZ16 6382652533ull      /* 2^32 / FS, Q16: phase increment = hz16 x this >> 32 */
#define PERC_INC_20K 1947826030u         /* the oscillators' 20 kHz ceiling, as a phase increment */
#define PERC_UNSET ((int32_t)0x80000000)

enum { PERC_KICK, PERC_TOM, PERC_SNARE, PERC_HIHAT, PERC_CYMBAL, PERC_PERC, PERC_NINST };

/* ------------------------------------------------------------------------------------------------ math --- */

/* 2^f, f in [0, 1) Q30 -> Q30 (1 .. 2): e^(f ln2), Taylor to the 10th power (error < 1e-9) */
static uint32_t perc_pow2frac(uint32_t f)
{
    uint32_t t = (uint32_t)(((uint64_t)f * PERC_LN2) >> 30), r = PERC_ONE, k;
    for (k = 10; k >= 1; k--)
        r = PERC_ONE + (uint32_t)(((uint64_t)r * t) >> 30) / k;
    return r;
}

/* 2^-t, t >= 0 Q30 (64-bit: any size) -> Q30 */
static int32_t perc_pow2neg(uint64_t t)
{
    uint32_t n = (uint32_t)(t >> 30), f = (uint32_t)t & (PERC_ONE - 1);
    if (n >= 31)
        return 0;
    if (!f)
        return (int32_t)(PERC_ONE >> n);
    return (int32_t)((perc_pow2frac(PERC_ONE - f) + (1u << n)) >> (n + 1));    /* 2^-(n+1) 2^(1-f) */
}

/* 2^x, x Q24 (-24 .. 7.9) -> Q24 */
static uint32_t perc_pow2(int32_t x)
{
    int32_t n = x >> 24, s;
    uint32_t m = perc_pow2frac(((uint32_t)x & 0xFFFFFFu) << 6);
    s = 6 - n;
    if (s >= 32)
        return 0;
    if (s < 0)
        return s == -1 && m < 0x80000000u ? m << 1 : 0xFFFFFFFFu;
    return (m + (s ? 1u << (s - 1) : 0)) >> s;
}

/* e^x, x Q24 -> Q24 */
static uint32_t perc_exp(int32_t x)
{
    return perc_pow2((int32_t)(((int64_t)x * PERC_LOG2E) >> 30));
}

/* ln(x), x Q16 > 0 -> Q24 */
static int32_t perc_ln(uint32_t x16)
{
    return (int32_t)(((int64_t)(log2_q16(x16) - (16 << 16)) * PERC_LN2) >> 22);
}

/* e^(-a / (t fs)): a > 0 Q24, t seconds Q24 -> Q30; 0 when t fs is under a sample (the original's no-time) */
static int32_t perc_expdec(int32_t a24, int32_t t24)
{
    uint64_t n8 = ((uint64_t)(uint32_t)t24 * PERC_FS) >> 16, num;
    uint32_t y32;
    if (t24 <= 0 || n8 < 256)
        return 0;
    if (n8 > 0xFFFFFFFFu)
        n8 = 0xFFFFFFFFu;
    num = (uint64_t)(uint32_t)a24 << 16;                  /* y = a / n, Q32 */
    if ((uint32_t)(num >> 32) >= (uint32_t)n8) {          /* y >= 1: Q24 */
        uint32_t y24 = udiv64_32((uint32_t)((uint64_t)(uint32_t)a24 >> 24), (uint32_t)a24 << 8, (uint32_t)n8);
        if (y24 > 87u << 24)
            return 0;
        return perc_pow2neg((((uint64_t)y24 * PERC_LOG2E) >> 32) << 8);   /* (constant shifts: no variable 64-bit shift) */
    }
    y32 = udiv64_32((uint32_t)(num >> 32), (uint32_t)num, (uint32_t)n8);
    return perc_pow2neg(((uint64_t)y32 * PERC_LOG2E) >> 32);
}

/* sin and cos of w (Q28 radians, 0 .. pi), Q30; omc = 1 - cos, Q30 (precise for small w) */
static void perc_sincos(int32_t w28, int32_t *s, int32_t *c, int32_t *omc)
{
    const int32_t HALFPI = 421657428;                     /* pi / 2, Q28 */
    int flip = 0;
    int64_t w, w2, t, sn, hs, h2;
    uint32_t k;
    if (w28 > HALFPI) {
        w28 = 2 * HALFPI - w28;                           /* sin(pi - w) = sin w, cos(pi - w) = -cos w */
        flip = 1;
    }
    w = (int64_t)w28 << 2;                                /* Q30 */
    w2 = (w * w) >> 30;
    t = w;
    sn = w;
    for (k = 2; k <= 14; k += 2) {
        {
            int64_t p = (t * w2) >> 30;                     /* |p| < 2^32: a 32-bit division */
            uint32_t q = (uint32_t)(p < 0 ? -p : p) / (k * (k + 1));
            t = p < 0 ? (int64_t)q : -(int64_t)q;
        }
        sn += t;
    }
    /* 1 - cos w = 2 sin^2(w / 2) */
    h2 = w2 >> 2;
    t = w >> 1;
    hs = t;
    for (k = 2; k <= 12; k += 2) {
        {
            int64_t p = (t * h2) >> 30;                     /* |p| < 2^32: a 32-bit division */
            uint32_t q = (uint32_t)(p < 0 ? -p : p) / (k * (k + 1));
            t = p < 0 ? (int64_t)q : -(int64_t)q;
        }
        hs += t;
    }
    t = (hs * hs) >> 29;
    *s = (int32_t)sn;
    if (flip) {
        *c = (int32_t)(t - PERC_ONE);
        *omc = (int32_t)(2 * (int64_t)PERC_ONE - t);
    } else {
        *c = (int32_t)(PERC_ONE - t);
        *omc = (int32_t)t;
    }
}

/* 1 / x for x Q30 > 0 (0.0005 .. 2^1) -> Q30 */
static int32_t perc_recip30(int32_t x)
{
    if (x <= 0)
        return 0x7FFFFFFF;
    return (int32_t)udiv64_32(1u << 28, 0, (uint32_t)x);    /* 2^60 / x */
}

/* a x b / c, all Q30, c > 0 */
static int32_t perc_muldiv30(int32_t a, int32_t b, int32_t c)
{
    int64_t p = (int64_t)a * b;                           /* Q60 */
    int neg = p < 0;
    uint64_t u = neg ? (uint64_t)-p : (uint64_t)p;
    uint32_t q = udiv64_32((uint32_t)(u >> 32), (uint32_t)u, (uint32_t)c);
    if (q > 0x7FFFFFFFu)
        q = 0x7FFFFFFFu;
    return neg ? -(int32_t)q : (int32_t)q;
}

/* ------------------------------------------------------------------------------------------ envelopes --- */
/* The original's drum envelope (0x1000a4ac): delay, an exponential attack towards 1 + o (stops at 1), a hold,
 * an exponential decay towards -floor (stops at 0); a retrigger while sounding fades out over 2 ms first. */
typedef struct {
    int32_t x;                           /* level, Q30 */
    int32_t ac, aa, dc, da;              /* x = a + c x, attack and decay, Q30 */
    int32_t cnt;                         /* delay / hold / fade countdown */
    int32_t fstep;                       /* fade step a sample, Q30 */
    int32_t hold_n, del_n;               /* (int)(hold fs), (int)(delay fs); del_n < 0: no delay */
    uint8_t st, nxt;                     /* 0 idle, 1 delay, 2 attack, 3 hold, 4 decay, 5 fade */
} perc_env_t;

/* a curve's constants (0x1000a308, 0x1000a1f8): attack overshoot o = e^ca, decay floor e^cd */
typedef struct {
    int32_t a_log;                       /* ln((1 + o) / o), Q24 */
    int32_t a_top;                       /* 1 + o, Q30 */
    int32_t d_log;                       /* ln((1 + f) / f), Q24 */
    int32_t d_floor;                     /* f, Q30 */
    int32_t d_scale;                     /* the decay time's scale, 1 - 0.09375 (cd + 5), Q16 */
} perc_curve_t;

/* the curves of the original's constructors */
static const perc_curve_t PERC_CURVE_A15_D7 = {28544978, 1313326009, 117455804, 979126, 77824};
static const perc_curve_t PERC_CURVE_A15_D37 = {28544978, 1313326009, 620756992, 0, 262144};
static const perc_curve_t PERC_CURVE_A15_D5 = {28544978, 1313326009, 83998745, 7234816, 65536};
static const perc_curve_t PERC_CURVE_A0_D77 = {11629080, 0x7FFFFFFF, 1291845632, 0, 507904};

/* attack time (seconds Q24) -> the envelope's attack coefficients */
static void perc_att_coef(const perc_curve_t *k, int32_t t24, int32_t *ac, int32_t *aa)
{
    int32_t c = perc_expdec(k->a_log, t24);
    *ac = c;
    *aa = (int32_t)(((int64_t)k->a_top * (PERC_ONE - c)) >> 30);
}

/* decay time (seconds Q24, before the curve's scale) -> the decay coefficients */
static void perc_dec_coef(const perc_curve_t *k, int32_t t24, int32_t *dc, int32_t *da)
{
    int32_t t = (int32_t)(((int64_t)t24 * k->d_scale) >> 16);
    int32_t c = perc_expdec(k->d_log, t);
    *dc = c;
    *da = -(int32_t)(((int64_t)k->d_floor * (PERC_ONE - c)) >> 30);
}

/* seconds Q24 -> (int)(t fs) */
static int32_t perc_samples(int32_t t24)
{
    return t24 <= 0 ? 0 : (int32_t)(((int64_t)t24 * PERC_FS) >> 24);
}

/* a parameter's seconds (Q16) -> Q24; x 0.0001 (the attacks' 0 .. 100 -> 0 .. 10 ms) -> Q24 */
#define PERC_S24(v) ((v) << 8)
#define PERC_ATT24(v) ((int32_t)(((int64_t)(v) * 109951163) >> 32))

/* 0x1000a87c: a note-on */
static void perc_env_trig(perc_env_t *e, int fade)
{
    if (e->del_n >= 0) {
        e->x = 0;
        e->st = 1;
        e->cnt = e->del_n;
        return;
    }
    if (e->st && fade) {
        e->st = 5;
        e->nxt = 2;
        e->fstep = -(int32_t)(((int64_t)e->x * 12173944) >> 30);   /* x x -500 / fs */
        e->cnt = 88;                                             /* (int)(fs x 0.002) */
        return;
    }
    e->st = 2;
}

/* 0x1000a4ac, n samples (the original's control flow, sample for sample) */
static void perc_env_run(perc_env_t *e, int32_t *o, int n)
{
    int32_t x = e->x, c;
    int st = e->st, i = 0, k;
    if (!st) {
        e->x = 0;
        for (k = 0; k < n; k++)
            o[k] = 0;
        return;
    }
    if ((st == 1 || st == 3) && e->cnt > n) {
        e->cnt -= n;
        for (k = 0; k < n; k++)
            o[k] = x;
        return;
    }
    for (;;) {
        switch (st) {
        case 0:
            for (; i < n; i++)
                o[i] = 0;
            x = 0;
            goto done;
        case 1:
            c = e->cnt - 1;
            e->cnt = c;
            while (c > 0) {
                o[i++] = x;
                if (i == n)
                    goto done;
                e->cnt = --c;
            }
            st = 2;
            do {
                o[i++] = x;
                if (i == n)
                    goto done;
        case 2:
                x = e->aa + (int32_t)(((int64_t)e->ac * x) >> 30);
            } while (x < PERC_ONE);
            x = PERC_ONE;
            if (e->hold_n > 0) {
                st = 3;
                e->cnt = e->hold_n;
                o[i++] = x;
                if (i == n)
                    goto done;
                goto hold;
            }
            st = 4;
            o[i] = x;
            goto decay_next;
        case 3:
        hold:
            c = e->cnt;
            for (;;) {
                e->cnt = --c;
                if (c < 1)
                    break;
                o[i++] = x;
                if (i == n)
                    goto done;
            }
            st = 4;
            o[i] = x;
        decay_next:
            for (;;) {
                i++;
                if (i == n)
                    goto done;
        case 4:
                x = e->da + (int32_t)(((int64_t)e->dc * x) >> 30);
                if (x <= 0)
                    break;
                o[i] = x;
            }
            st = 0;
            x = 0;
            for (; i < n; i++)
                o[i] = 0;
            goto done;
        case 5:
            c = e->cnt;
            for (;;) {
                x += e->fstep;
                e->cnt = c - 1;
                if (c < 1)
                    break;
                o[i++] = x;
                c--;
                if (i == n)
                    goto done;
            }
            st = e->nxt;
            x = 0;
            o[i++] = x;
            if (i == n)
                goto done;
            break;
        }
    }
done:
    e->x = x;
    e->st = (uint8_t)st;
}

/* the original's "still sounding": not idle, nor decayed below 0.0001 */
static int perc_env_live(const perc_env_t *e)
{
    return e->st && (e->st != 4 || e->x >= PERC_THR);
}

/* ------------------------------------------------------------------------------------------ oscillators --- */
/* the waves, in RAM on the target (read every sample) */
static POOL int16_t perc_wt[PERC_NWT * 257];

/* a wave at phase ph (2^32 a cycle), linear between its 256 points, Q15 */
#define PERC_WAVE(t, ph) ((t)[(ph) >> 24] + ((((t)[((ph) >> 24) + 1] - (t)[(ph) >> 24]) * (int32_t)(((ph) >> 8) & 0xFFFF)) >> 16))

/* A to B by m (Q15): m <= 0 A, m >= 1 B (0x10011208's three cases) */
AINL int32_t perc_morph(const int16_t *a, const int16_t *b, uint32_t ph, int32_t m)
{
    int32_t ya, yb;
    if (m <= 0)
        return PERC_WAVE(a, ph);
    yb = PERC_WAVE(b, ph);
    if (m >= Q15)
        return yb;
    ya = PERC_WAVE(a, ph);
    return ya + (((yb - ya) * m) >> 15);
}

/* Hz Q16 -> phase increment, with the oscillators' 0 .. 20 kHz clamp */
static uint32_t perc_hz_inc(int32_t hz16)
{
    uint64_t r;
    if (hz16 <= 0)
        return 0;
    r = ((uint64_t)(uint32_t)hz16 * PERC_INC_HZ16) >> 32;
    return r > PERC_INC_20K ? PERC_INC_20K : (uint32_t)r;
}

/* a base increment times m (Q24, any sign), clamped 0 .. 20 kHz */
AINL uint32_t perc_inc_mul(uint32_t inc, int32_t m24)
{
    int64_t r;
    if (m24 <= 0)
        return 0;
    r = ((int64_t)inc * m24) >> 24;
    return r > PERC_INC_20K ? PERC_INC_20K : (uint32_t)r;
}

/* the original's noise (u ^= v; v += u; the old v is the sample), Q15 */
typedef struct { uint32_t u, v; } perc_rng_t;
AINL int32_t perc_rng31(perc_rng_t *r)        /* the sample as a signed 32-bit integer (x 2^-31: -1 .. 1) */
{
    int32_t o = (int32_t)r->v;
    r->u ^= r->v;
    r->v += r->u;
    return o;
}

/* ------------------------------------------------------------------------------------------- biquads --- */
/* y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2 (the original's direct form I); coefficients Q30, x Q15, y Q23 */
typedef struct { int32_t b0, b1, b2, a1, a2; } perc_bq_c;
typedef struct { int32_t x1, x2, y1, y2; } perc_bq_s;

AINL int32_t perc_bq_run(perc_bq_s *s, const perc_bq_c *c, int32_t x)
{
    int64_t acc = (((int64_t)c->b0 * x + (int64_t)c->b1 * s->x1 + (int64_t)c->b2 * s->x2) << 8)
                  - (int64_t)c->a1 * s->y1 - (int64_t)c->a2 * s->y2;
    int32_t y = (int32_t)(acc >> 30);
    s->x2 = s->x1;
    s->x1 = x;
    s->y2 = s->y1;
    s->y1 = y;
    return (y + 128) >> 8;
}

/* the same with Q28 coefficients (the EQ's peaking filters: b0 above 2) */
AINL int32_t perc_bq_run28(perc_bq_s *s, const perc_bq_c *c, int32_t x)
{
    int64_t acc = (((int64_t)c->b0 * x + (int64_t)c->b1 * s->x1 + (int64_t)c->b2 * s->x2) << 8)
                  - (int64_t)c->a1 * s->y1 - (int64_t)c->a2 * s->y2;
    int32_t y = (int32_t)(acc >> 28);
    s->x2 = s->x1;
    s->x1 = x;
    s->y2 = s->y1;
    s->y1 = y;
    return (y + 128) >> 8;
}

/* the RBJ coefficients the original computes: w = 2 pi f / fs (Q28) of Hz Q16 */
static int32_t perc_w28(int32_t hz16)
{
    return (int32_t)(((uint64_t)(uint32_t)hz16 * 2506461789u) >> 32);
}

/* alpha = sin w sinh(ln2 / 2 x w / sin w) (one octave's bandwidth), Q30 */
static int32_t perc_alpha(int32_t w28, int32_t s30)
{
    int32_t x24;
    uint32_t ep, inv;
    if (s30 < 1074)                                       /* |sin w| < 1e-6 */
        return 0;
    x24 = perc_muldiv30((int32_t)(((int64_t)w28 * 372130569) >> 28), PERC_ONE, s30) >> 6;
    ep = perc_exp(x24);                                   /* e^x, Q24 */
    inv = ep ? udiv64_32(1u << 16, 0, ep) : 0;            /* e^-x, Q24 */
    return (int32_t)(((int64_t)s30 * (int32_t)(ep - inv)) >> 25);
}

/* low-pass (RBJ, one octave's bandwidth) at f Hz Q16: the kick's click (0x1000e42c) */
static void perc_bq_lp(perc_bq_c *c, int32_t hz16)
{
    int32_t w = perc_w28(hz16), s, cs, omc, al, a0;
    perc_sincos(w, &s, &cs, &omc);
    al = perc_alpha(w, s);
    a0 = perc_recip30(PERC_ONE + al);                     /* 1 / (1 + alpha) */
    c->b0 = (int32_t)(((int64_t)omc * a0) >> 31);
    c->b1 = (int32_t)(((int64_t)omc * a0) >> 30);
    c->b2 = c->b0;
    c->a1 = (int32_t)((-2 * (int64_t)cs * a0) >> 30);
    c->a2 = (int32_t)(((int64_t)(PERC_ONE - al) * a0) >> 30);
}

/* ----------------------------------------------------------------------------------------- the SVFs --- */
/* Polyend's zero-delay SVF with a soft clip of the band state, b' = -D b + (1 + D) b (b^2 + 27) / (9 b^2 + 27)
 * (0x1000f8e8 low-pass, 0x1000fb1c high-pass, 0x1000ff7c two high-passes, 0x10010238 two band-passes).
 * Coefficients once (cook): Q27; states Q24. */
typedef struct { int32_t g, G, G1, K, E, Dn, D1, A1; } perc_svf_c;   /* A1 = 1 - K G */
typedef struct { int32_t s1, s2; } perc_svf_s;

/* the coefficients: f Hz Q16, res (Q16, the filter's +8 field), kc 7.247407 / 1.9140741 (Q24), thr the
 * cube of res above which K is g alone (Q24) */
static void perc_svf_coef(perc_svf_c *c, int32_t hz16, int32_t res16, int32_t kc24, int32_t thr24)
{
    int32_t x, x2, p, num, gq, r, r3, k27, den, kg;
    int64_t t;
    if (hz16 < 328)                                       /* 0.005 Hz */
        hz16 = 328;
    if (hz16 > 20000 << 16)
        hz16 = 20000 << 16;
    x = (int32_t)udiv64_32((uint32_t)((uint64_t)(uint32_t)hz16 >> 18), (uint32_t)hz16 << 14, PERC_FS);  /* hz16 2^14 / fs: Q30 */
    if (x > 535797184)
        x = 535797184;
    x2 = (int32_t)(((int64_t)x * x) >> 30);
    p = (int32_t)(((int64_t)-32162146 * x2) >> 30) - 54674152;
    p = (int32_t)(((int64_t)p * x2) >> 30) - 144875807;
    p = (int32_t)(((int64_t)p * x2) >> 30) - 598859456;
    p = (int32_t)(((int64_t)p * x2) >> 30) + 843314788;
    num = (int32_t)(((int64_t)p * x) >> 30);              /* Q30 */
    gq = perc_muldiv30(num, 1 << 27, (1 << 28) - x2);    /* tan(pi x) ~ num / (0.25 - x^2): Q27 */
    if (gq < 1)
        gq = 1;
    r = clampi(res16, 0, 98304) << 8;                     /* Q24, 0 .. 1.5 */
    r3 = (int32_t)(((((int64_t)r * r) >> 24) * r) >> 24);
    t = ((int64_t)r3 * kc24 >> 24) + 9059697;             /* r^3 kc + 0.54, Q24 */
    k27 = (int32_t)udiv64_32(1u << 19, 0, (uint32_t)t);   /* 2^51 / t: Q27 */
    kg = k27 + gq;
    den = (1 << 24) + (int32_t)(((int64_t)gq * kg) >> 30);    /* 1 + g (k + g), Q24 (up to ~60) */
    c->g = gq;
    c->G1 = (int32_t)udiv64_32(1u << 19, 0, (uint32_t)den);           /* 2^51 / den: Q27 */
    c->G = (int32_t)(((int64_t)gq * c->G1) >> 27);
    c->K = r3 > thr24 ? gq : kg;
    {
        uint64_t n = (uint64_t)(uint32_t)x2 << 26;        /* E = 4 x^2 / g, Q27 = x2 2^26 / gq */
        c->E = (uint32_t)(n >> 32) >= (uint32_t)gq ? 0x7FFFFFFF : (int32_t)udiv64_32((uint32_t)(n >> 32), (uint32_t)n, (uint32_t)gq);
    }
    {
        int32_t d = (int32_t)(((int64_t)(((int64_t)r3 * 24855) >> 21) - (1 << 27)) * c->G1 >> 27);  /* (r^3 0.0014815 - 1) G1 */
        c->Dn = -d;
        c->D1 = (1 << 27) + d;
    }
    c->A1 = (1 << 27) - (int32_t)(((int64_t)c->K * c->G) >> 27);
}

#define PERC_M27(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 27))
/* the soft clip p(b) = b (b^2 + 27) / (9 b^2 + 27) by |b| in 0 .. 4 (1/256 steps), Q24 */
static POOL int32_t perc_ptab[1026];

/* p(b) for |b| >= 4 (rare), Q24 */
static __attribute__((noinline)) int32_t perc_pade_far(int32_t a)
{
    uint64_t s8 = ((uint64_t)(uint32_t)a * (uint32_t)a) >> 40;   /* b^2, Q8 */
    uint32_t r;
    if (s8 > 0x3FFFFFF)
        s8 = 0x3FFFFFF;
    r = udiv64_32((uint32_t)((s8 + 6912) >> 16), (uint32_t)((s8 + 6912) << 16), (uint32_t)(9 * s8 + 6912)) << 14;
    return (int32_t)(((int64_t)a * r) >> 30);
}

/* b' = -D b + (1 + D) p(b) */
AINL int32_t perc_shape(const perc_svf_c *c, int32_t b)
{
    int32_t a = b < 0 ? -b : b, p;
    if (a < 4 << 24) {                                    /* |b| < 4: by |b|, 1/256 steps */
        uint32_t i = (uint32_t)a >> 16, f = ((uint32_t)a >> 8) & 0xFF;
        p = perc_ptab[i] + (((perc_ptab[i + 1] - perc_ptab[i]) * (int32_t)f) >> 8);
    } else
        p = perc_pade_far(a);
    return PERC_M27(b, c->Dn) + PERC_M27(b < 0 ? -p : p, c->D1);
}

/* The stages share b = s1 + G v (v = x - s2 - K s1) = A1 s1 + G (x - s2): one multiply less on the state's path.
 * low-pass, x and the result Q24 */
AINL int32_t perc_svf_lp(perc_svf_s *s, const perc_svf_c *c, int32_t x)
{
    int32_t s1 = s->s1, b = PERC_M27(s1, c->A1) + PERC_M27(x - s->s2, c->G), sb = perc_shape(c, b);
    int32_t gb = PERC_M27(sb, c->g), y = s->s2 + PERC_M27(s1, c->E) + gb;
    s->s2 = y + gb;
    s->s1 = sb + b - s1;
    return y;
}

/* high-pass */
AINL int32_t perc_svf_hp(perc_svf_s *s, const perc_svf_c *c, int32_t x)
{
    int32_t s1 = s->s1, hp = PERC_M27(x - s->s2 - PERC_M27(c->K, s1), c->G1);
    int32_t b = PERC_M27(s1, c->A1) + PERC_M27(x - s->s2, c->G), sb = perc_shape(c, b);
    s->s2 += 2 * PERC_M27(sb, c->g) + PERC_M27(s1, c->E);
    s->s1 = sb + b - s1;
    return hp;
}

/* band-pass stage of the two-stage band-pass: returns the shaped band */
AINL int32_t perc_svf_bp(perc_svf_s *s, const perc_svf_c *c, int32_t x)
{
    int32_t s1 = s->s1, b = PERC_M27(s1, c->A1) + PERC_M27(x - s->s2, c->G), sb = perc_shape(c, b);
    s->s2 += 2 * PERC_M27(sb, c->g) + PERC_M27(s1, c->E);
    s->s1 = sb + b - s1;
    return sb;
}

/* ------------------------------------------------------------------------------------------------ kick --- */
/* 0x1000dfc0: a wave oscillator (sine to Body Wave's wave) with a 20 ms pitch drop and a mod envelope
 * (Classic8 / Synced8: the wave's morph; Classic9: the pitch), the body envelope, a click (8-sample ramp,
 * noise when Transient Timbre < 0) through a low-pass for its first 128 samples, and a gain ramp. */
typedef struct {
    int32_t ac, aa;                      /* body attack */
    int32_t mdc, mda;                    /* mod envelope decay */
    int32_t dec24;                       /* Body Decay, s Q24 (x velocity at the note-on) */
    perc_bq_c click;                     /* the click's low-pass */
    int32_t click_g;                     /* Transient Level (8 - 6 |timbre|), Q16 */
    int32_t morph;                       /* Body Wave's percentage, Q15 */
    int32_t mod;                         /* Mod Amount, Q16 */
    uint32_t inc;                        /* the pitch's phase increment */
    uint8_t wt, model, noise;            /* Body Wave's wave; model; timbre < 0 */
} perc_kick_ck_t;

typedef struct {
    perc_env_t amp, menv;
    perc_rng_t rng;
    perc_bq_s bq;
    uint32_t ph;
    int32_t ramp;                        /* the pitch drop, Q30 (1 -> 0 in 20 ms) */
    uint8_t ramp_on;
    int32_t gain, gstep, gtgt;           /* the output's gain ramp, Q30 */
    int32_t gn;                          /* samples of the ramp left */
    int32_t acc;                         /* the note's accent (velocity above 100), Q16 */
    int32_t tn;                          /* samples of the click left (128 -> 0) */
    int32_t click_n[32];                 /* the click's noise (the first 32 samples) */
} perc_kick_t;

static void perc_cook_kick(perc_kick_ck_t *k, const int32_t *val)
{
    int32_t wv = val[0] >> 16, tim = val[4];
    k->wt = PERC_BODY_WT[clampi(wv / 11, 0, 15)];
    k->morph = ((wv % 11) * Q15 + 5) / 10;
    k->inc = perc_hz_inc(PERC_NOTE_HZ[clampi(val[1] >> 16, 0, 127)]);
    perc_att_coef(&PERC_CURVE_A15_D7, PERC_ATT24(val[2]), &k->ac, &k->aa);
    k->dec24 = PERC_S24(val[3]);
    perc_dec_coef(&PERC_CURVE_A15_D7, PERC_S24(val[6]), &k->mdc, &k->mda);
    {
        int32_t at = tim < 0 ? -tim : tim;               /* the click's low-pass: 30 + 4000 |timbre| Hz */
        perc_bq_lp(&k->click, (30 << 16) + at * 4000);
        k->click_g = (int32_t)(((int64_t)val[5] * ((8 << 16) - 6 * at)) >> 16);
        k->noise = tim < 0;
    }
    k->mod = val[7];
    k->model = (uint8_t)clampi(val[8] >> 16, 0, 2);
}

static void perc_reset_kick(perc_kick_t *k)
{
    k->amp.del_n = k->menv.del_n = -1;
    k->menv.aa = PERC_CURVE_A15_D7.a_top;                 /* the mod envelope: no attack (c = 0) */
    k->rng.u = 0x12345678u;
    k->rng.v = 0xdeadbeedu;
}

/* the handlers that change the voice (the rest is read from the cooked data every render) */
static void perc_apply_kick(perc_kick_t *k, const perc_kick_ck_t *c, int id)
{
    if (id == 2)
        k->amp.ac = c->ac, k->amp.aa = c->aa;
    else if (id == 6)
        k->menv.dc = c->mdc, k->menv.da = c->mda;
    else if (id == 8)
        k->amp.hold_n = c->model == 2 ? 2205 : 0;          /* Classic9: a 50 ms hold */
}

/* the velocity, Q30 (vel / 127), and the accent above 100, Q16 (0 .. 1) */
static int32_t perc_vel30(int vel) { return vel * 8454660; }
static int32_t perc_acc16(int vel) { return vel <= 100 ? 0 : vel >= 127 ? Q16 : (vel - 100) * 2427; }

static void perc_on_kick(perc_kick_t *k, const perc_kick_ck_t *c, int vel)
{
    int fade = 1;
    int32_t v = perc_vel30(vel), t;
    if (!k->amp.st || (k->amp.st == 4 && k->amp.x < PERC_THR_RETRIG && k->amp.x > -PERC_THR_RETRIG)) {
        if (c->model)
            k->ph = 0x40000000u;                          /* Synced8, Classic9: the phase restarts (0.25) */
        fade = 0;
        k->gain = v;
    }
    k->acc = perc_acc16(vel);
    t = (int32_t)(((int64_t)c->dec24 * (Q16 - ((k->acc * 19661) >> 16))) >> 16);   /* x (1 - 0.3 accent) */
    perc_dec_coef(&PERC_CURVE_A15_D7, t, &k->amp.dc, &k->amp.da);
    perc_env_trig(&k->amp, fade);
    perc_env_trig(&k->menv, 0);
    k->ramp = PERC_ONE;
    k->ramp_on = 1;
    k->gtgt = v;
    k->gstep = (v - k->gain) >> 7;
    k->gn = 128;
    k->tn = 128;
}

static int perc_render_kick(perc_kick_t *k, const perc_kick_ck_t *c, int32_t *o)
{
    int32_t me[ENG_BLK], ae[ENG_BLK];
    const int16_t *ta = perc_wt + PERC_WT_SINE * 257, *tb = perc_wt + c->wt * 257;
    uint32_t ph = k->ph;
    int32_t ramp = k->ramp, i;
    if (!perc_env_live(&k->amp))
        return 0;
    perc_env_run(&k->menv, me, ENG_BLK);
    for (i = 0; i < ENG_BLK; i++) {
        int32_t r = k->ramp_on ? (ramp = -7741 + (int32_t)(((int64_t)1065252690 * ramp) >> 30)) : 0;
        int32_t m = (1 << 24) + (r >> 4), mo = c->morph;
        if (c->model == 2)
            m += (int32_t)(((int64_t)c->mod * me[i] * 20) >> 22);
        else
            mo += (int32_t)(((int64_t)c->mod * me[i]) >> 31);
        o[i] = perc_morph(ta, tb, ph, mo);
        ph += perc_inc_mul(c->inc, m);
    }
    if (k->ramp_on && ramp <= 0)
        k->ramp_on = 0;
    k->ramp = ramp;
    k->ph = ph;
    perc_env_run(&k->amp, ae, ENG_BLK);
    for (i = 0; i < ENG_BLK; i++)
        o[i] = ae[i] > 0 ? (int32_t)(((int64_t)o[i] * ae[i]) >> 30) : 0;
    if (k->tn > 0 && c->click_g > 0) {                    /* the click: the first 128 samples */
        int32_t g = (int32_t)(((int64_t)c->click_g * (Q16 + ((k->acc * 13107) >> 16))) >> 16);   /* x (1 + 0.2 accent) */
        for (i = 0; i < ENG_BLK && k->tn > 0; i++, k->tn--) {
            int32_t n = 128 - k->tn, x = n < 8 ? -Q15 + n * 4096 : 0;
            if (c->noise && n < 32)
                x = (x + (int32_t)(((int64_t)(32 - n) * perc_rng31(&k->rng)) >> 21)) >> 1;
            o[i] += (int32_t)(((int64_t)perc_bq_run(&k->bq, &c->click, x) * g) >> 16);
        }
    }
    for (i = 0; i < ENG_BLK; i++) {
        o[i] = (int32_t)(((int64_t)o[i] * k->gain) >> 30);
        if (k->gn > 0) {
            k->gain += k->gstep;
            if (--k->gn == 0)
                k->gain = k->gtgt;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------------- tom --- */
/* 0x1000b04c: the tom. Three wave oscillators (Classic: one; Classic+: + noise through a fixed low biquad on a
 * second envelope; Resonant: + two at 0.584 / 0.585 of the pitch, the first sine to square), each with the mod
 * envelope on pitch (Pitch Mod) and morph (Wave Mod); a noise transient through the SVF low-pass on its own
 * envelope; a 128-sample gain ramp. The note sets the pitch (perc_on). */
#define PERC_TOM_SQUARE 1                /* 0x10014bfc: the Resonant model's first extra oscillator's wave B */
static const perc_bq_c PERC_TOM_BQ = {106095, 63719, 25046, -2126930176, 1053383104};   /* the ctor's, at 44.1 kHz */

typedef struct {
    int32_t ac, aa;                      /* body attack (the body and the Classic+/Resonant envelopes) */
    int32_t rdc, rda;                    /* the second envelope's decay (Body Decay / 2) */
    int32_t mdc, mda;                    /* mod envelope decay */
    int32_t dec24;                       /* Body Decay, s Q24 (x velocity at the note-on) */
    int32_t morph, morph2;               /* Body Wave's percentage, x 0.7 for the square oscillator, Q15 */
    int32_t pmod, wmod;                  /* Pitch Mod, Wave Mod, Q16 */
    int32_t tlev;                        /* Transient Level, Q16 */
    perc_svf_c tf;                       /* the transient's low-pass, 100 + 19000 timbre Hz */
    uint8_t wt, model;
} perc_tom_ck_t;

typedef struct {
    perc_env_t amp, menv, renv, tenv;
    perc_rng_t rn1, rn2;
    perc_bq_s bq;
    perc_svf_s sv;
    uint32_t ph[3], inc;
    int32_t gain, gstep, gtgt, gn, acc;
} perc_tom_t;

static void perc_cook_tom(perc_tom_ck_t *k, const int32_t *val)
{
    int32_t wv = val[9] >> 16;
    k->wt = PERC_BODY_WT[clampi(wv / 11, 0, 15)];
    k->morph = ((wv % 11) * Q15 + 5) / 10;
    k->morph2 = (k->morph * 7 + 5) / 10;
    perc_att_coef(&PERC_CURVE_A15_D37, PERC_ATT24(val[10]), &k->ac, &k->aa);
    k->dec24 = PERC_S24(val[11]);
    perc_dec_coef(&PERC_CURVE_A15_D37, PERC_S24(val[11]) >> 1, &k->rdc, &k->rda);
    perc_svf_coef(&k->tf, (100 << 16) + (int32_t)(((int64_t)val[12] * 19000)), 0, 121591313, 56391612);
    k->tlev = val[13];
    perc_dec_coef(&PERC_CURVE_A15_D37, PERC_S24(val[14]), &k->mdc, &k->mda);
    k->pmod = val[15];
    k->wmod = val[16];
    k->model = (uint8_t)clampi(val[17] >> 16, 0, 2);
}

static void perc_reset_tom(perc_tom_t *t)
{
    t->amp.del_n = t->menv.del_n = t->renv.del_n = t->tenv.del_n = -1;
    t->menv.aa = PERC_CURVE_A15_D37.a_top;               /* no attack */
    perc_att_coef(&PERC_CURVE_A15_D37, 1678, &t->tenv.ac, &t->tenv.aa);          /* 0.0001 s */
    perc_dec_coef(&PERC_CURVE_A15_D37, 838861, &t->tenv.dc, &t->tenv.da);              /* 0.05 s */
    t->rn1.u = t->rn2.u = 0x12345678u;
    t->rn1.v = t->rn2.v = 0xdeadbeedu;
    t->inc = perc_hz_inc(110 << 16);
}

static void perc_apply_tom(perc_tom_t *t, const perc_tom_ck_t *c, int id)
{
    if (id == 10) {
        t->amp.ac = t->renv.ac = c->ac;
        t->amp.aa = t->renv.aa = c->aa;
    } else if (id == 11)
        t->renv.dc = c->rdc, t->renv.da = c->rda;
    else if (id == 14)
        t->menv.dc = c->mdc, t->menv.da = c->mda;
}

static void perc_on_tom(perc_tom_t *t, const perc_tom_ck_t *c, int note, int vel)
{
    int fade = 1;
    int32_t v = perc_vel30(vel), d;
    t->inc = perc_hz_inc(PERC_NOTE_HZ[note & 127]);
    if (!t->amp.st || (t->amp.st == 4 && t->amp.x < PERC_THR_RETRIG && t->amp.x > -PERC_THR_RETRIG)) {
        if (c->model)
            t->ph[0] = t->ph[1] = t->ph[2] = 0x40000000u;
        fade = 0;
        t->gain = v;
    }
    t->acc = perc_acc16(vel);
    d = (int32_t)(((int64_t)c->dec24 * (Q16 - ((t->acc * 19661) >> 16))) >> 16);     /* x (1 - 0.3 accent) */
    perc_dec_coef(&PERC_CURVE_A15_D37, d, &t->amp.dc, &t->amp.da);
    perc_env_trig(&t->amp, fade);
    perc_env_trig(&t->renv, 0);
    perc_env_trig(&t->menv, 0);
    perc_env_trig(&t->tenv, 0);
    t->gtgt = v;
    t->gstep = (v - t->gain) >> 7;
    t->gn = 128;
}

/* the SVFs over a block, the state and coefficients in locals (kept in registers) */
static void perc_ts_lp(perc_svf_s *sp, const perc_svf_c *cp, int32_t *x, int sh)
{
    perc_svf_s s = *sp;
    perc_svf_c c = *cp;
    int i;
    for (i = 0; i < ENG_BLK; i++)
        x[i] = perc_svf_lp(&s, &c, x[i]) >> sh;
    *sp = s;
}
static void perc_ts_hp(perc_svf_s *sp, const perc_svf_c *cp, int32_t *x, int sh)
{
    perc_svf_s s = *sp;
    perc_svf_c c = *cp;
    int i;
    for (i = 0; i < ENG_BLK; i++)
        x[i] = perc_svf_hp(&s, &c, x[i]) >> sh;
    *sp = s;
}

/* one wave oscillator with the mod envelope on pitch and morph (0x10010f04); add: += (else =) */
static void perc_tom_osc(const perc_tom_ck_t *c, const int16_t *tb, int32_t morph, uint32_t inc, uint32_t *php,
                         const int32_t *me, const int32_t *env, int32_t *o, int add)
{
    const int16_t *ta = perc_wt + PERC_WT_SINE * 257;
    uint32_t ph = *php;
    int i;
    for (i = 0; i < ENG_BLK; i++) {
        int32_t m = (1 << 24) + (int32_t)(((int64_t)c->pmod * me[i]) >> 22);
        int32_t y = perc_morph(ta, tb, ph, morph + (int32_t)(((int64_t)c->wmod * me[i]) >> 31));
        ph += perc_inc_mul(inc, m);
        if (!add)
            o[i] = y;
        else if (env[i] > 0)
            o[i] += (int32_t)(((int64_t)y * env[i]) >> 30);
    }
    *php = ph;
}

static int perc_render_tom(perc_tom_t *t, const perc_tom_ck_t *c, int32_t *o)
{
    int32_t ae[ENG_BLK], me[ENG_BLK], re[ENG_BLK], x[ENG_BLK], fl, i;
    const int16_t *tb = perc_wt + c->wt * 257;
    if (!perc_env_live(&t->amp))
        return 0;
    perc_env_run(&t->amp, ae, ENG_BLK);
    perc_env_run(&t->menv, me, ENG_BLK);
    perc_tom_osc(c, tb, c->morph, t->inc, &t->ph[0], me, ae, o, 0);
    for (i = 0; i < ENG_BLK; i++)
        o[i] = ae[i] > 0 ? (int32_t)(((int64_t)o[i] * ae[i]) >> 30) : 0;
    fl = 22938;                                           /* 0.7, Q15 */
    if (c->model == 1) {                                  /* Classic+: noise through the ctor's biquad */
        for (i = 0; i < ENG_BLK; i++)
            x[i] = perc_bq_run(&t->bq, &PERC_TOM_BQ, perc_rng31(&t->rn1) >> 17);
        perc_env_run(&t->renv, re, ENG_BLK);
        for (i = 0; i < ENG_BLK; i++)
            if (re[i] > 0)
                o[i] += (int32_t)(((int64_t)x[i] * re[i]) >> 30);
    } else if (c->model == 2) {                           /* Resonant: + 0.584 (sine to square) and 0.585 */
        perc_env_run(&t->renv, re, ENG_BLK);
        perc_tom_osc(c, perc_wt + PERC_TOM_SQUARE * 257, c->morph2, (uint32_t)(((uint64_t)t->inc * 38273) >> 16),
                     &t->ph[1], me, re, o, 1);
        perc_tom_osc(c, tb, c->morph, (uint32_t)(((uint64_t)t->inc * 38339) >> 16), &t->ph[2], me, re, o, 1);
        fl = 11469;                                       /* 0.35 */
    }
    if (c->tlev > 0) {                                    /* the transient: noise, low-pass, its envelope */
        int32_t te[ENG_BLK], g = (int32_t)(((int64_t)c->tlev * (Q16 + ((t->acc * 13107) >> 16))) >> 16);
        for (i = 0; i < ENG_BLK; i++)
            x[i] = perc_rng31(&t->rn2) >> 7;              /* Q24 */
        if (!t->tenv.st)                                  /* (the envelope off: the filter's output unheard) */
            goto ramp;
        perc_env_run(&t->tenv, te, ENG_BLK);
        perc_ts_lp(&t->sv, &c->tf, x, 0);
        for (i = 0; i < ENG_BLK; i++) {
            int32_t f = x[i];
            f = te[i] > 0 ? (int32_t)(((int64_t)f * te[i]) >> 39) : 0;     /* Q15 */
            o[i] += (int32_t)(((int64_t)f * g) >> 16);
        }
    }
ramp:
    for (i = 0; i < ENG_BLK; i++) {
        o[i] = (int32_t)(((((int64_t)o[i] * t->gain) >> 30) * fl) >> 15);
        if (t->gn > 0) {
            t->gain += t->gstep;
            if (--t->gn == 0)
                t->gain = t->gtgt;
        }
    }
    return 1;
}

/* ----------------------------------------------------------------------------------------------- snare --- */
/* 0x1000f19c: the snare. Two wave oscillators (the pitch, the pitch + 150 Hz; Tone crossfades them), each on
 * its own envelope, with a pitch envelope (x2, Classic9+: x4; Classic8: none); noise: Classic8 low-passes it at
 * 7 kHz then high-passes it (Snap Timbre); Classic9, Classic9+ add a low-passed copy (10 / 15 kHz) on its own
 * envelope; Modern, Modern+ only that copy, its cutoff and resonance from Snap Timbre / the model.
 * Body/Snap Mix and the velocity (accent: the snap louder) weigh body and snap. */
typedef struct {
    int32_t bdc, bda, b2dc, b2da;        /* the oscillators' envelopes' decays (Body Decay, / 2) */
    int32_t sac, saa, sdc, sda;          /* the snap envelopes' attack, decay */
    int32_t hold_b, hold_s;              /* holds: 0.01 s / 0.02 s but on Classic8 */
    int32_t g1, g2;                      /* 1 - Tone, Tone, Q16 */
    int32_t morph;
    uint32_t inc1, inc2;
    int32_t mix;                         /* Body/Snap Mix, Q16 */
    perc_svf_c lp, hp;
    uint8_t wt, model;
} perc_snare_ck_t;

typedef struct {
    perc_env_t e1, e2, ns, hs, pe;       /* osc 1, osc 2, the low-passed snap, the high-passed snap, pitch */
    perc_rng_t rng;
    perc_svf_s lp, hp;
    uint32_t ph1, ph2;
    int32_t vel, acc;                    /* Q16 */
} perc_snare_t;

static void perc_cook_snare(perc_snare_ck_t *k, const int32_t *val)
{
    int32_t wv = val[18] >> 16, hz = PERC_NOTE_HZ[clampi(val[19] >> 16, 0, 127)], tim = val[26], cut, res = 0;
    k->wt = PERC_BODY_WT[clampi(wv / 11, 0, 15)];
    k->morph = ((wv % 11) * Q15 + 5) / 10;
    k->inc1 = perc_hz_inc(hz);
    k->inc2 = perc_hz_inc(hz + (150 << 16));
    k->g2 = val[20];
    k->g1 = Q16 - val[20];
    k->model = (uint8_t)clampi(val[21] >> 16, 0, 4);
    k->hold_b = k->model ? 441 : 0;
    k->hold_s = k->model ? 882 : 0;
    perc_dec_coef(&PERC_CURVE_A15_D37, PERC_S24(val[22]), &k->bdc, &k->bda);
    perc_dec_coef(&PERC_CURVE_A15_D37, PERC_S24(val[22]) >> 1, &k->b2dc, &k->b2da);
    perc_att_coef(&PERC_CURVE_A15_D37, PERC_ATT24(val[23]), &k->sac, &k->saa);
    perc_dec_coef(&PERC_CURVE_A15_D37, PERC_S24(val[24]), &k->sdc, &k->sda);
    k->mix = val[25];
    switch (k->model) {
    case 0: cut = 7000 << 16; break;
    case 2: cut = 15000 << 16; break;
    case 3: case 4: cut = (100 << 16) + tim * 10000; res = k->model == 3 ? 19661 : 32768; break;
    default: cut = 10000 << 16; break;
    }
    perc_svf_coef(&k->lp, cut, res, 121591313, 56391612);
    perc_svf_coef(&k->hp, (100 << 16) + tim * 10000, 13107, 121591313, 56391612);
}

static void perc_reset_snare(perc_snare_t *t)
{
    int32_t ac, aa;
    t->e1.del_n = t->e2.del_n = t->ns.del_n = t->hs.del_n = t->pe.del_n = -1;
    perc_att_coef(&PERC_CURVE_A15_D37, 8389, &ac, &aa);   /* 0.0005 s */
    t->e1.ac = t->e2.ac = ac;
    t->e1.aa = t->e2.aa = aa;
    t->pe.aa = PERC_CURVE_A15_D37.a_top;                  /* no attack, 0.02 s */
    perc_dec_coef(&PERC_CURVE_A15_D37, 335544, &t->pe.dc, &t->pe.da);
    t->rng.u = 0x12345678u;
    t->rng.v = 0xdeadbeedu;
}

static void perc_apply_snare(perc_snare_t *t, const perc_snare_ck_t *c, int id)
{
    if (id == 21) {
        t->e1.hold_n = t->e2.hold_n = c->hold_b;
        t->ns.hold_n = t->hs.hold_n = c->hold_s;
    } else if (id == 22) {
        t->e1.dc = c->bdc, t->e1.da = c->bda;
        t->e2.dc = c->b2dc, t->e2.da = c->b2da;
    } else if (id == 23) {
        t->ns.ac = t->hs.ac = c->sac;
        t->ns.aa = t->hs.aa = c->saa;
    } else if (id == 24) {
        t->ns.dc = t->hs.dc = c->sdc;
        t->ns.da = t->hs.da = c->sda;
    }
}

static void perc_on_snare(perc_snare_t *t, const perc_snare_ck_t *c, int vel)
{
    (void)c;
    t->ph1 = t->ph2 = 0x40000000u;
    perc_env_trig(&t->e1, 0);
    perc_env_trig(&t->e2, 0);
    perc_env_trig(&t->ns, 0);
    perc_env_trig(&t->hs, 0);
    perc_env_trig(&t->pe, 0);
    t->vel = perc_vel30(vel) >> 14;
    t->acc = perc_acc16(vel);
}

/* an oscillator, the pitch envelope x depth (Q24 per unit of pe; 0: none), added x g (Q16) into o */
static void perc_snare_osc(const int16_t *tb, int32_t morph, uint32_t inc, uint32_t *php, const int32_t *pe,
                           int32_t depth, int32_t g, int32_t *o)
{
    const int16_t *ta = perc_wt + PERC_WT_SINE * 257;
    uint32_t ph = *php;
    int i;
    for (i = 0; i < ENG_BLK; i++) {
        o[i] += (int32_t)(((int64_t)perc_morph(ta, tb, ph, morph) * g) >> 16);
        ph += depth ? perc_inc_mul(inc, (1 << 24) + depth * (pe[i] >> 6)) : inc;
    }
    *php = ph;
}

AINL void perc_snare_env(int32_t *x, const int32_t *e)
{
    int i;
    for (i = 0; i < ENG_BLK; i++)
        x[i] = e[i] > 0 ? (int32_t)(((int64_t)x[i] * e[i]) >> 30) : 0;
}

static int perc_render_snare(perc_snare_t *t, const perc_snare_ck_t *c, int32_t *o)
{
    int32_t pe[ENG_BLK], e[ENG_BLK], b2[ENG_BLK], b3[ENG_BLK], depth = 0, gn, gb, i;
    const int16_t *tb = perc_wt + c->wt * 257;
    int m = c->model;
    if (!perc_env_live(&t->e1) && !perc_env_live(&t->e2) && !(m && perc_env_live(&t->ns))
        && !(m < 3 && perc_env_live(&t->hs)))
        return 0;
    if (m) {
        perc_env_run(&t->pe, pe, ENG_BLK);
        depth = m == 2 ? 4 : 2;
    }
    for (i = 0; i < ENG_BLK; i++)
        o[i] = b2[i] = 0;
    perc_snare_osc(tb, c->morph, c->inc1, &t->ph1, pe, depth, c->g1, o);
    perc_env_run(&t->e1, e, ENG_BLK);
    perc_snare_env(o, e);
    perc_snare_osc(tb, c->morph, c->inc2, &t->ph2, pe, depth, c->g2, b2);
    perc_env_run(&t->e2, e, ENG_BLK);
    perc_snare_env(b2, e);
    for (i = 0; i < ENG_BLK; i++) {
        o[i] += b2[i];
        b2[i] = b3[i] = perc_rng31(&t->rng) >> 7;         /* the noise, Q24 */
    }
    /* snap: vel 0.5 (mix + 1)(accent + 1); body: vel (1 - (mix + 1) / 2) */
    gn = (int32_t)(((((int64_t)t->vel * (c->mix + Q16)) >> 17) * (t->acc + Q16)) >> 16);
    gb = (int32_t)(((int64_t)t->vel * (Q16 - ((c->mix + Q16) >> 1))) >> 16);
    /* both filters in one loop when both are heard (two independent chains: twice as fast) */
    if (m < 3 && t->hs.st && (m ? t->ns.st : 1)) {
        perc_svf_s ls = t->lp, hs = t->hp;
        perc_svf_c lc = c->lp, hc = c->hp;
        if (!m)
            for (i = 0; i < ENG_BLK; i++)
                b3[i] = perc_svf_hp(&hs, &hc, perc_svf_lp(&ls, &lc, b3[i])) >> 9;
        else
            for (i = 0; i < ENG_BLK; i++) {
                b2[i] = perc_svf_lp(&ls, &lc, b2[i]) >> 9;
                b3[i] = perc_svf_hp(&hs, &hc, b3[i]) >> 9;
            }
        t->lp = ls;
        t->hp = hs;
        if (m) {
            perc_env_run(&t->ns, e, ENG_BLK);
            perc_snare_env(b2, e);
        }
        perc_env_run(&t->hs, e, ENG_BLK);
        perc_snare_env(b3, e);
        goto mix;
    }
    /* (a filter whose envelope is off is unheard: skipped, its state kept) */
    if (!m) {
        if (t->hs.st)
            perc_ts_lp(&t->lp, &c->lp, b3, 0);
    } else if (t->ns.st) {
        perc_ts_lp(&t->lp, &c->lp, b2, 9);
        perc_env_run(&t->ns, e, ENG_BLK);
        perc_snare_env(b2, e);
    } else
        for (i = 0; i < ENG_BLK; i++)
            b2[i] = 0;
    if (m < 3) {
        if (t->hs.st) {
            perc_ts_hp(&t->hp, &c->hp, b3, 9);
            perc_env_run(&t->hs, e, ENG_BLK);
            perc_snare_env(b3, e);
        } else
            for (i = 0; i < ENG_BLK; i++)
                b3[i] = 0;
    mix:
        for (i = 0; i < ENG_BLK; i++)
            o[i] = (int32_t)(((int64_t)(m ? b2[i] + b3[i] : b3[i]) * gn + (int64_t)o[i] * gb) >> 16);
    } else
        for (i = 0; i < ENG_BLK; i++)
            o[i] = (int32_t)(((int64_t)b2[i] * gn + (int64_t)o[i] * gb) >> 16);
    return 1;
}

/* ---------------------------------------------------------------------------------------------- hi-hat --- */
/* The metal source the hi-hat and the cymbal share (0x1000aa30's variant): Classic, six pulses (30 % duty) at
 * Timbre's ratios (0x10014524) x Tone Pitch, five of them detuned at random once a 128-frame block (+-0.5 .. 2.5 %)
 * (0x1000d684); FM, three sines (0x10010cb8): A (Timbre's ratio x pitch, self-feedback) phase-modulates B (the
 * pitch), C (another ratio) amplitude-modulates the result. Then pink noise (three one-poles) by Tone/Noise. */
static const int32_t PERC_METAL_RATIO[25][6] = {          /* 0x10014524: 25 x 6 floats, Q16 */
    {65536, 95734, 117579, 165446, 173478, 257004}, {65536, 97340, 128181, 177011, 192432, 317079},
    {65536, 81920, 128181, 198857, 252828, 291378}, {65536, 86418, 95413, 177011, 246403, 257325},
    {65536, 88988, 115973, 138461, 192432, 291378}, {65536, 92843, 117901, 168016, 206567, 263750},
    {65536, 96698, 131715, 170265, 218453, 273067}, {65536, 99589, 135570, 178296, 224557, 289129},
    {65536, 93485, 138140, 179903, 228091, 295554}, {65536, 87703, 106335, 186328, 234516, 301980},
    {65536, 109227, 150669, 1609487, 231946, 292663}, {65536, 96698, 121756, 163519, 208816, 279170},
    {65536, 83526, 150990, 195965, 244154, 311617}, {65536, 96055, 106335, 167053, 228412, 263108},
    {65536, 122077, 160306, 202391, 250579, 318042}, {65536, 128502, 166731, 208816, 262465, 324467},
    {65536, 131715, 164161, 214920, 274030, 327680}, {65536, 102802, 159985, 217811, 263429, 330893},
    {65536, 106335, 141673, 219096, 266642, 334105}, {65536, 138140, 173799, 196287, 231304, 304871},
    {65536, 138782, 174441, 221345, 267605, 328001}, {65536, 133963, 169623, 215562, 264714, 320934},
    {65536, 140710, 178296, 225521, 278528, 331856}, {65536, 136533, 173478, 216847, 260216, 303586},
    {65536, 142958, 181509, 226485, 273067, 322861},
};
static const int32_t PERC_METAL_FM[25][5] = {             /* 0x100194a8: B's index, C's AM depth, A's feedback,
                                                             A's ratio, C's ratio (25 x 6 floats, the 6th unused), Q16 */
    {13107, 0, 4588, 183501, 91750}, {13107, 65536, 4588, 183501, 91750}, {13107, 65536, 2621, 183501, 91750},
    {26870, 65536, 3277, 183501, 91750}, {26870, 65536, 3932, 183501, 183501}, {19661, 0, 3932, 183501, 183501},
    {11796, 65536, 9830, 196608, 163840}, {11796, 65536, 9830, 196608, 229376}, {11796, 65536, 9830, 163840, 229376},
    {11796, 0, 15073, 163840, 229376}, {36700, 65536, 15073, 209715, 104858}, {28180, 65536, 15073, 229376, 117965},
    {28180, 65536, 15073, 229376, 114688}, {22282, 0, 21627, 229376, 294912}, {22282, 65536, 22938, 229376, 301466},
    {14418, 65536, 22282, 327680, 209715}, {14418, 65536, 22282, 419430, 209715}, {11141, 0, 26214, 419430, 294912},
    {11141, 32768, 26214, 419430, 294912}, {11141, 65536, 32768, 419430, 294912}, {9175, 65536, 35389, 465306, 294912},
    {9175, 65536, 35389, 465306, 301466}, {9175, 65536, 35389, 255590, 327680}, {12452, 18350, 36045, 360448, 242483},
    {7209, 65536, 32768, 393216, 308019},
};
static const int32_t PERC_METAL_PULSE[7] = {-16777216, -11184811, -5592405, 0, 5592405, 11184811, 16777216};

typedef struct {
    uint32_t inc[6];                     /* Classic: the pulses, before the jitter (unclamped) */
    uint32_t ia, ib, ic;                 /* FM: A, B, C (clamped to 20 kHz) */
    int32_t fb, idx, am;                 /* FM: A's feedback, B's index (Q16), C's AM depth (Q15) */
    int32_t mix;                         /* Tone/Noise: the noise's share, Q15 */
    uint8_t fm;
} perc_metal_ck_t;

typedef struct {
    uint32_t ph[6];                      /* Classic: six phases; FM: A, B, C */
    uint32_t jinc[6];                    /* Classic: this block's increments */
    perc_rng_t jr;                       /* Classic: the jitter's generator */
    int32_t ya;                          /* FM: A's last output, Q15 */
    perc_rng_t rng;                      /* the noise */
    int32_t p1, p2, p3;                  /* the pink filter, Q24 */
    uint8_t blk;                         /* 32-frame renders into the original's 128-frame block */
} perc_metal_t;

/* model (0 Classic, 1 FM), Timbre (0 .. 24), Tone Pitch (Hz Q16), Tone/Noise (-1 .. 1, Q16) */
static void perc_metal_cook(perc_metal_ck_t *c, int32_t model, int32_t timbre, int32_t hz16, int32_t mix)
{
    int t = clampi(timbre >> 16, 0, 24), k;
    c->fm = model >= Q16;
    for (k = 0; k < 6; k++) {
        int32_t f = (int32_t)(((int64_t)hz16 * PERC_METAL_RATIO[t][k]) >> 16);
        c->inc[k] = (uint32_t)(((uint64_t)(uint32_t)f * PERC_INC_HZ16) >> 32);
    }
    c->ia = perc_hz_inc((int32_t)(((int64_t)hz16 * PERC_METAL_FM[t][3]) >> 16));
    c->ib = perc_hz_inc(hz16);
    c->ic = perc_hz_inc((int32_t)(((int64_t)hz16 * PERC_METAL_FM[t][4]) >> 16));
    c->idx = PERC_METAL_FM[t][0];
    c->am = PERC_METAL_FM[t][1] >> 1;
    c->fb = PERC_METAL_FM[t][2];
    c->mix = (clampi(mix, -Q16, Q16) + Q16) >> 2;         /* (v + 1) / 2, Q15 */
}

/* the variant rebuilt (a Model change): phases 0, the jitter's generator reseeded */
static void perc_metal_new(perc_metal_t *m)
{
    int k;
    for (k = 0; k < 6; k++)
        m->ph[k] = 0;
    m->ya = 0;
    m->jr.u = 0x12345678u;
    m->jr.v = 0xdeadbeedu;
    m->blk = 0;
}

/* a note-on on a silent instrument: Classic's phases to 0, FM's to 0.25 */
static void perc_metal_restart(perc_metal_t *m, const perc_metal_ck_t *c)
{
    int k;
    for (k = 0; k < 6; k++)
        m->ph[k] = c->fm ? 0x40000000u : 0;
    m->blk = 0;
}

/* the wave at a phase plus a signed offset (Q32 cycles), as the original reads it (0x10010cb8): the index is
 * truncated towards zero, so below 0 it reads the next segment backwards */
AINL int32_t perc_metal_wave(const int16_t *t, uint32_t ph, int64_t off)
{
    int64_t s = (int64_t)ph + off;
    int32_t i, fr, k;
    if (s >= 0)
        return PERC_WAVE(t, (uint32_t)s);
    i = -(int32_t)((uint64_t)(-s) >> 24);
    fr = (int32_t)(s - ((int64_t)i << 24));               /* (-1 .. 0], Q24 */
    k = i & 0xFF;
    return t[k] + (int32_t)(((int64_t)(t[k + 1] - t[k]) * fr) >> 24);
}

/* tone + noise, Q24 */
static void perc_metal_src(perc_metal_t *m, const perc_metal_ck_t *c, int32_t *o)
{
    int i, k;
    if (!c->fm) {
        if (m->blk == 0) {                                /* once a 128-frame block: the jitter */
            m->jinc[0] = c->inc[0] > PERC_INC_20K ? PERC_INC_20K : c->inc[0];
            perc_rng31(&m->jr);
            for (k = 1; k < 6; k++) {
                int32_t j = perc_rng31(&m->jr);           /* x (1 + j 0.01 k / 2^32) */
                m->jinc[k] = perc_inc_mul(c->inc[k], (1 << 24) + (int32_t)(((int64_t)j * k * 167772) >> 32));
            }
        }
        for (i = 0; i < ENG_BLK; i++) {
            int p = 0;
            for (k = 0; k < 6; k++) {
                p += m->ph[k] < 1288490189u;              /* 0.3 */
                m->ph[k] += m->jinc[k];
            }
            o[i] = PERC_METAL_PULSE[p];
        }
    } else {
        const int16_t *t = perc_wt + PERC_WT_SINE * 257;
        uint32_t pa = m->ph[0], pb = m->ph[1], pc = m->ph[2];
        int32_t ya = m->ya;
        for (i = 0; i < ENG_BLK; i++) {
            int32_t yb, yc;
            ya = perc_metal_wave(t, pa, (int64_t)c->fb * ya * 2);
            yb = perc_metal_wave(t, pb, (int64_t)c->idx * ya * 2);
            yc = PERC_WAVE(t, pc);
            o[i] = (int32_t)(((int64_t)yb * ((Q15 - c->am) + ((c->am * yc) >> 15))) >> 6);   /* Q24 */
            pa += c->ia;
            pb += c->ib;
            pc += c->ic;
        }
        m->ph[0] = pa, m->ph[1] = pb, m->ph[2] = pc;
        m->ya = ya;
    }
    m->blk = (uint8_t)((m->blk + 1) & 3);
    for (i = 0; i < ENG_BLK; i++) {                       /* + pink noise (0x1000db7c) */
        int32_t n = perc_rng31(&m->rng) >> 7;             /* Q24 */
        int32_t ns;
        m->p1 = (int32_t)(((int64_t)n * 106349833 + (int64_t)m->p1 * 1071218531) >> 30);
        m->p2 = (int32_t)(((int64_t)n * 318382060 + (int64_t)m->p2 * 1034013377) >> 30);
        m->p3 = (int32_t)(((int64_t)n * 1130318677 + (int64_t)m->p3 * 612032840) >> 30);
        ns = (m->p1 + m->p2 + m->p3 + (int32_t)(((int64_t)n * 198427489) >> 30)) >> 2;
        o[i] = (int32_t)(((int64_t)ns * c->mix + (int64_t)o[i] * (Q15 - c->mix)) >> 15);
    }
}

static void perc_metal_reset(perc_metal_t *m)
{
    perc_metal_new(m);
    m->rng.u = 0x12345678u;
    m->rng.v = 0xdeadbeedu;
}

/* the velocity's level, min(v, 100 / 127), Q15 */
static int32_t perc_metal_vel(int vel) { return vel >= 100 ? 25802 : (vel * 8454660) >> 15; }

/* 0x1000db7c: source x min(velocity, 100/127) -> low-pass (res 0.4) -> envelope -> two high-passes (res 0) */
typedef struct {
    perc_metal_ck_t osc;
    perc_svf_c lp, hp;
    perc_curve_t curve;                  /* Decay Shape: decay curve (v - 1) 37 */
    int32_t dec_open, dec_closed;        /* s Q24 */
    int32_t hold_open;                   /* samples: v < 0.1 ? 0.2 v : 0.02 s */
} perc_hihat_ck_t;

typedef struct {
    perc_metal_t osc;
    perc_svf_s lp, hp1, hp2;
    perc_env_t env;
    int32_t vel;
} perc_hihat_t;

static void perc_cook_hihat(perc_hihat_ck_t *k, const int32_t *val)
{
    int32_t v = clampi(val[27], 0, Q16), cd = (int32_t)(((int64_t)(v - Q16) * 37) << 8);   /* Q24 */
    uint32_t f24 = perc_exp(cd);                          /* e^c, Q24 */
    perc_metal_cook(&k->osc, val[35], val[34], val[32], val[33]);
    perc_svf_coef(&k->lp, val[31], 26214, 121591313, 56391612);
    perc_svf_coef(&k->hp, val[30], 0, 32112835, 55746583);
    k->curve = PERC_CURVE_A15_D7;
    k->curve.d_floor = (int32_t)(f24 << 6);
    k->curve.d_log = -cd + perc_ln((uint32_t)Q16 + (f24 >> 8));   /* ln((1 + f) / f) = -c + ln(1 + e^c) */
    k->curve.d_scale = Q16 - (int32_t)(((int64_t)((cd >> 8) + 5 * Q16) * 6144) >> 16);     /* 1 - 0.09375 (c + 5) */
    k->dec_open = PERC_S24(val[29]);
    k->dec_closed = PERC_S24(val[28]);
    k->hold_open = perc_samples(v < 6554 ? (int32_t)(((int64_t)v * 13107) >> 8) : 335544);   /* 0.2 v, 0.02 s */
}

static void perc_reset_hihat(perc_hihat_t *t)
{
    perc_metal_reset(&t->osc);
    t->env.del_n = -1;
    perc_att_coef(&PERC_CURVE_A15_D7, 1678, &t->env.ac, &t->env.aa);   /* 0.1 ms */
}

static void perc_apply_hihat(perc_hihat_t *t, const perc_hihat_ck_t *c, int id)
{
    (void)c;
    if (id == 35)
        perc_metal_new(&t->osc);                          /* a Model change rebuilds the source */
}

/* 0x1000c4e0, notes 64 .. 67: the closed and the open hat share the one envelope (one chokes the other) */
static void perc_on_hihat(perc_hihat_t *t, const perc_hihat_ck_t *c, int open, int vel)
{
    int32_t acc = perc_acc16(vel), d;
    t->vel = perc_metal_vel(vel);
    if (!t->env.st || (t->env.st == 4 && t->env.x < PERC_THR))
        perc_metal_restart(&t->osc, &c->osc);
    d = open ? c->dec_open : c->dec_closed;
    d = (int32_t)(((int64_t)d * (Q16 + (acc >> 1))) >> 16);   /* x (1 + 0.5 accent) */
    t->env.hold_n = open ? c->hold_open : 0;
    perc_dec_coef(&c->curve, d, &t->env.dc, &t->env.da);
    perc_env_trig(&t->env, 0);
}

static int perc_render_hihat(perc_hihat_t *t, const perc_hihat_ck_t *c, int32_t *o)
{
    int32_t e[ENG_BLK], i;
    if (!perc_env_live(&t->env))
        return 0;
    perc_metal_src(&t->osc, &c->osc, o);
    perc_env_run(&t->env, e, ENG_BLK);
    {
        const perc_svf_c lc = c->lp, hc = c->hp;              /* (in registers: the three stages interleave) */
        perc_svf_s l = t->lp, h1 = t->hp1, h2 = t->hp2;
        int32_t vel = t->vel;
        for (i = 0; i < ENG_BLK; i++) {
            int32_t y = perc_svf_lp(&l, &lc, (int32_t)(((int64_t)o[i] * vel) >> 15));
            y = e[i] > 0 ? (int32_t)(((int64_t)y * e[i]) >> 30) : 0;
            o[i] = perc_svf_hp(&h2, &hc, perc_svf_hp(&h1, &hc, y)) >> 9;
        }
        t->lp = l, t->hp1 = h1, t->hp2 = h2;
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------------- cymbal --- */
/* 0x1000bc0c: the metal source -> (a) two band-passes at Body Filter (res 0.6) x the body envelope,
 * (b) two high-passes at Impact Filter (res 0.6) x the impact envelope x a second, longer impact envelope (the
 * original multiplies the one buffer by both); 3 m (a) + 2 (3 - 3 m) (b), m = Impact/Body (0 .. 1), x min(velocity,
 * 100/127) -> a high-pass biquad at Body Filter + 3 kHz (0x1000bfa0) */
typedef struct {
    perc_metal_ck_t osc;
    perc_svf_c bp, hp;
    perc_bq_c bq;
    int32_t gb, gi;                      /* Q16 */
    int32_t dec_body, dec_imp;           /* s Q24 */
} perc_cymbal_ck_t;

typedef struct {
    perc_metal_t osc;
    perc_svf_s bp1, bp2, hp1, hp2;
    perc_bq_s bq;
    perc_env_t eb, ei1, ei2;
    int32_t vel;
} perc_cymbal_t;

static void perc_cook_cymbal(perc_cymbal_ck_t *k, const int32_t *val)
{
    int32_t m = (clampi(val[42], -Q16, Q16) + Q16) >> 1, w, s, cs, omc, al, a0;
    perc_metal_cook(&k->osc, val[43], val[44], val[40], val[41]);
    perc_svf_coef(&k->bp, val[36], 39322, 32112835, 55746583);
    perc_svf_coef(&k->hp, val[37], 39322, 32112835, 55746583);
    k->gb = 3 * m;
    k->gi = 2 * (3 * Q16 - 3 * m);
    w = perc_w28(val[36] + (3000 << 16));                 /* high-pass, one octave's bandwidth */
    perc_sincos(w, &s, &cs, &omc);
    al = perc_alpha(w, s);
    a0 = perc_recip30(PERC_ONE + al);
    k->bq.b0 = (int32_t)((((int64_t)2 * PERC_ONE - omc) * a0) >> 31);
    k->bq.b1 = -(int32_t)((((int64_t)2 * PERC_ONE - omc) * a0) >> 30);
    k->bq.b2 = k->bq.b0;
    k->bq.a1 = (int32_t)((-2 * (int64_t)cs * a0) >> 30);
    k->bq.a2 = (int32_t)(((int64_t)(PERC_ONE - al) * a0) >> 30);
    k->dec_body = PERC_S24(val[38]);
    k->dec_imp = PERC_S24(val[39]);
}

static void perc_reset_cymbal(perc_cymbal_t *t)
{
    perc_metal_reset(&t->osc);
    t->eb.del_n = t->ei1.del_n = t->ei2.del_n = -1;
    perc_att_coef(&PERC_CURVE_A15_D7, 16777, &t->eb.ac, &t->eb.aa);   /* 1 ms */
    t->ei1.ac = t->ei2.ac = t->eb.ac;
    t->ei1.aa = t->ei2.aa = t->eb.aa;
}

static void perc_apply_cymbal(perc_cymbal_t *t, const perc_cymbal_ck_t *c, int id)
{
    (void)c;
    if (id == 43)
        perc_metal_new(&t->osc);
}

static int perc_cy_quiet(const perc_env_t *e) { return !e->st || (e->st == 4 && e->x < PERC_THR); }

/* 0x1000c4e0, notes 68, 69 */
static void perc_on_cymbal(perc_cymbal_t *t, const perc_cymbal_ck_t *c, int vel)
{
    int32_t f = Q16 + (perc_acc16(vel) >> 1), di = (int32_t)(((int64_t)c->dec_imp * f) >> 16);
    t->vel = perc_metal_vel(vel);
    if (perc_cy_quiet(&t->eb) && perc_cy_quiet(&t->ei1))
        perc_metal_restart(&t->osc, &c->osc);
    perc_dec_coef(&PERC_CURVE_A15_D7, (int32_t)(((int64_t)c->dec_body * f) >> 16), &t->eb.dc, &t->eb.da);
    perc_dec_coef(&PERC_CURVE_A15_D37, (int32_t)(((int64_t)di * 19661) >> 16), &t->ei1.dc, &t->ei1.da);   /* x 0.3 */
    perc_dec_coef(&PERC_CURVE_A15_D37, di, &t->ei2.dc, &t->ei2.da);
    perc_env_trig(&t->eb, 0);
    perc_env_trig(&t->ei1, 0);
    perc_env_trig(&t->ei2, 0);
}

static int perc_render_cymbal(perc_cymbal_t *t, const perc_cymbal_ck_t *c, int32_t *o)
{
    int32_t eb[ENG_BLK], e1[ENG_BLK], e2[ENG_BLK], i;
    if (!perc_env_live(&t->eb) && !perc_env_live(&t->ei2))
        return 0;
    perc_metal_src(&t->osc, &c->osc, o);
    perc_env_run(&t->eb, eb, ENG_BLK);
    perc_env_run(&t->ei1, e1, ENG_BLK);
    perc_env_run(&t->ei2, e2, ENG_BLK);
    {
    const perc_svf_c bc = c->bp, hc = c->hp;
    perc_svf_s b1 = t->bp1, b2 = t->bp2, h1 = t->hp1, h2 = t->hp2;
    for (i = 0; i < ENG_BLK; i++) {
        int32_t a = perc_svf_bp(&b2, &bc, perc_svf_bp(&b1, &bc, o[i]));
        int32_t b = perc_svf_hp(&h2, &hc, perc_svf_hp(&h1, &hc, o[i]));
        int32_t y;
        a = eb[i] > 0 ? (int32_t)(((int64_t)a * eb[i]) >> 30) : 0;
        b = e1[i] > 0 ? (int32_t)(((int64_t)b * e1[i]) >> 30) : 0;
        b = e2[i] > 0 ? (int32_t)(((int64_t)b * e2[i]) >> 30) : 0;
        y = (int32_t)(((int64_t)a * c->gb + (int64_t)b * c->gi) >> 16);
        y = (int32_t)(((int64_t)y * t->vel) >> 15);
        o[i] = perc_bq_run(&t->bq, &c->bq, y >> 9);
    }
    t->bp1 = b1, t->bp2 = b2, t->hp1 = h1, t->hp2 = h2;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------------------ perc --- */
/* The percussion slot (0x158c): a std::variant of five models, rebuilt whenever Model is set (0x1000eafc),
 * Variation / Timbre / Decay re-applied after (the model_h1 table, 0x10019280).
 *   Clap, Clap+ (0x100118d4, 0x10011c1c): noise (Clap white, Clap+ pink, x 0.5) through a band-pass (Clap) or
 *     a high-pass (Clap+) at 100 + 1400 Timbre Hz, gated by four bursts (delays k s, s = 5 + 15 Variation ms,
 *     each jittered +-3 ms by a MINSTD generator at the note-on; decays 1.35 s, the last one Decay), plus pink /
 *     white noise through two band-passes under a slow tail (attack 3 s, decay 1.5 s) at 0.2.
 *   Maracas (0x10012484, 0x1001275c): pink noise high-passed at 100 + 15900 Timbre Hz under four short bursts
 *     (attack 0.1 s, decay 0.1 Decay), plus 0.2 x two band-passes, all under a tail (attack 2 s, decay 6 s).
 *   Shaker (0x1000ec64, 0x1000ed8c): pink noise, two high-passes at 100 + 5900 Timbre Hz (resonance 0.5
 *     Variation + 0.3 accent), one envelope (20 ms attack, Decay).
 *   Cowbell (0x10011f5c, 0x100120bc): three 30 % pulses at f, 1.5185 f, 2 f (f = 80 + 920 Timbre Hz; the upper
 *     two jittered 0.5 % / 1 % each block), 0.8 x a band-pass at 400 + 2000 Variation Hz + 0.2 x dry, under
 *     two envelopes (Decay, 6 Decay).
 * Noise is made 128 samples at a time, as the original's blocks (its generators run in that order). */
#define PERC_PC_LP_KC 121591313                           /* the single-stage SVFs' 7.247407, Q24 */
#define PERC_PC_LP_THR 56391612
#define PERC_PC_X4_KC 32112835                            /* the two-stage SVFs' 1.9140741 */
#define PERC_PC_X4_THR 55746583
#define PERC_PC_VMAX 845466000                            /* velocity clamp 0.7874 (100 / 127), Q30 */
static const perc_curve_t PERC_CURVE_A0_D37 = {11629080, 0x7FFFFFFF, 620756992, 0, 262144};
static const perc_curve_t PERC_CURVE_A0_D0 = {11629080, 0x7FFFFFFF, 11629080, PERC_ONE, 34816};
static const perc_curve_t PERC_CURVE_A15_D50 = {28544978, 1313326009, 838860800, 0, 342016};

enum { PERC_PC_CLAP, PERC_PC_CLAPP, PERC_PC_MARACAS, PERC_PC_SHAKER, PERC_PC_COWBELL };

typedef struct { int32_t ac, aa, dc, da; } perc_pc_ec_t;
typedef struct {
    uint8_t model;
    perc_pc_ec_t e[5];                   /* clap / maracas: bursts 0..3, tail; shaker e[0]; cowbell e[0], e[1] */
    int64_t s48;                         /* the bursts' spacing, s Q48 */
    int32_t cut16;                       /* the filter's cutoff, Hz Q16 */
    int32_t var16;                       /* Variation, Q16 */
    int32_t f16;                         /* cowbell: the base pitch, Hz Q16 */
} perc_perc_ck_t;

typedef struct {
    uint8_t model, sub, gen;
    perc_env_t e[5];
    perc_rng_t rng;
    int32_t pk[3];                       /* pink noise's three poles, Q24 */
    uint32_t minstd;                     /* the bursts' jitter (MINSTD, 48271) */
    perc_svf_s f1, f2a, f2b;
    perc_svf_c c1, c2;
    int32_t vel30;                       /* the note's velocity, Q30 */
    int32_t res1, res2;                  /* the filters' resonance, Q16 */
    uint32_t ph[3], inc[3];              /* cowbell */
    int32_t n1[128], n2[128];            /* this block's noise, Q24 */
} perc_perc_t;

static void perc_cook_perc(perc_perc_ck_t *k, const int32_t *val)
{
    int32_t var = val[46], tim = val[47], dec = val[48], i;
    int64_t s48 = 1407374883553ll + (int64_t)var * 64424509;          /* 0.005 + 0.015 Variation s, Q48 */
    int32_t s24 = (int32_t)(s48 >> 24);
    perc_pc_ec_t *e = k->e;
    k->model = (uint8_t)clampi(val[45] >> 16, 0, 4);
    k->s48 = s48;
    k->var16 = var;
    switch (k->model) {
    case PERC_PC_CLAP:
    case PERC_PC_CLAPP:
        for (i = 0; i < 4; i++) {
            e[i].ac = 0;
            e[i].aa = PERC_CURVE_A15_D7.a_top;
            if (i < 3)
                perc_dec_coef(&PERC_CURVE_A15_D7, (int32_t)(((int64_t)s24 * 88474) >> 16), &e[i].dc, &e[i].da);
        }
        perc_dec_coef(&PERC_CURVE_A15_D37, PERC_S24(dec), &e[3].dc, &e[3].da);
        perc_att_coef(&PERC_CURVE_A15_D37, 3 * s24, &e[4].ac, &e[4].aa);
        perc_dec_coef(&PERC_CURVE_A15_D37, 3 << 23, &e[4].dc, &e[4].da);
        k->cut16 = (100 << 16) + 1400 * tim;
        break;
    case PERC_PC_MARACAS:
        for (i = 0; i < 4; i++) {
            perc_att_coef(&PERC_CURVE_A0_D37, s24 / 10, &e[i].ac, &e[i].aa);
            perc_dec_coef(&PERC_CURVE_A0_D37, PERC_S24(dec) / 10, &e[i].dc, &e[i].da);
        }
        perc_att_coef(&PERC_CURVE_A0_D0, 2 * s24, &e[4].ac, &e[4].aa);
        perc_dec_coef(&PERC_CURVE_A0_D0, 6 * s24, &e[4].dc, &e[4].da);
        k->cut16 = (100 << 16) + 15900 * tim;
        break;
    case PERC_PC_SHAKER:
        perc_att_coef(&PERC_CURVE_A0_D77, 335544, &e[0].ac, &e[0].aa);   /* 20 ms */
        perc_dec_coef(&PERC_CURVE_A0_D77, PERC_S24(dec), &e[0].dc, &e[0].da);
        k->cut16 = (100 << 16) + 5900 * tim;
        break;
    default:
        perc_att_coef(&PERC_CURVE_A15_D50, 16777, &e[0].ac, &e[0].aa);   /* 1 ms */
        perc_dec_coef(&PERC_CURVE_A15_D50, PERC_S24(dec), &e[0].dc, &e[0].da);
        perc_att_coef(&PERC_CURVE_A15_D37, 16777, &e[1].ac, &e[1].aa);
        perc_dec_coef(&PERC_CURVE_A15_D37, 6 * PERC_S24(dec), &e[1].dc, &e[1].da);
        k->cut16 = (400 << 16) + 2000 * var;
        k->f16 = (80 << 16) + 920 * tim;
        break;
    }
}

/* the filters' coefficients (the original works them out every block from cutoff and resonance) */
static void perc_pc_coefs(perc_perc_t *t, const perc_perc_ck_t *c)
{
    switch (t->model) {
    case PERC_PC_CLAP:
    case PERC_PC_MARACAS:
    case PERC_PC_CLAPP:
        perc_svf_coef(&t->c1, c->cut16, t->res1, PERC_PC_LP_KC, PERC_PC_LP_THR);
        perc_svf_coef(&t->c2, c->cut16, t->res2, PERC_PC_X4_KC, PERC_PC_X4_THR);
        break;
    case PERC_PC_SHAKER:
        perc_svf_coef(&t->c1, c->cut16, t->res1, PERC_PC_X4_KC, PERC_PC_X4_THR);
        break;
    default:
        perc_svf_coef(&t->c1, c->cut16, t->res1, PERC_PC_LP_KC, PERC_PC_LP_THR);
        break;
    }
}

static void perc_reset_perc(perc_perc_t *t)
{
    int i;
    uint8_t sub = t->sub, *p = (uint8_t *)t;
    for (i = 0; i < (int)sizeof *t; i++)
        p[i] = 0;
    t->sub = sub;                                         /* (the block grid is the engine's) */
    for (i = 0; i < 5; i++)
        t->e[i].del_n = -1;
    t->rng.u = 0x12345678u;
    t->rng.v = 0xdeadbeedu;
    t->minstd = 1;
}

static void perc_apply_perc(perc_perc_t *t, const perc_perc_ck_t *c, int id)
{
    int i;
    if (id == 45) {                                       /* a new model, from scratch */
        perc_reset_perc(t);
        t->model = c->model;
    }
    for (i = 0; i < 5; i++) {
        t->e[i].ac = c->e[i].ac, t->e[i].aa = c->e[i].aa;
        t->e[i].dc = c->e[i].dc, t->e[i].da = c->e[i].da;
    }
    perc_pc_coefs(t, c);
}

/* the original's (float)(r - 1), rounded to 24 bits */
static uint32_t perc_pc_f24(uint32_t x)
{
    int sh = 0;
    while ((x >> sh) >= (1u << 24))
        sh++;
    if (sh)
        x = ((x + (1u << (sh - 1))) >> sh) << sh;
    return x;
}

/* the bursts' note-on (0x10011ab0, 0x10012608): each delay k s jittered by +-3 ms, the tail too, no fades */
static void perc_pc_bursts(perc_perc_t *t, const perc_perc_ck_t *c)
{
    int k;
    perc_env_trig(&t->e[4], 0);
    for (k = 0; k < 4; k++) {
        uint32_t x = t->minstd, hi = x / 44488u, lo = (x % 44488u) * 48271u, r1;
        int64_t d48 = c->s48 * k;
        int32_t r = (int32_t)(lo - hi * 3399u);
        if (lo < hi * 3399u)
            r += 0x7FFFFFFF;
        t->minstd = (uint32_t)r;
        r1 = perc_pc_f24((uint32_t)(r - 1));
        if (r1 >= 0x80000000u)
            d48 += 844424817542ll;                        /* + 0.0029999996 */
        else
            d48 += (((int64_t)r1 * 51539608) >> 16) - 844424930132ll;   /* - 0.003 + (r - 1) 0.006 / 2^31 */
        t->e[k].del_n = d48 > 0 ? (int32_t)((d48 * PERC_FS) >> 48) : -1;
        perc_env_trig(&t->e[k], 0);
        t->e[k].del_n = -1;
    }
}

static void perc_on_perc(perc_perc_t *t, const perc_perc_ck_t *c, int vel)
{
    int32_t acc = perc_acc16(vel);
    t->vel30 = perc_vel30(vel);
    switch (t->model) {
    case PERC_PC_CLAP:
    case PERC_PC_CLAPP:
        t->res1 = t->res2 = 32768 + ((acc * 19661) >> 16);          /* 0.5 + 0.3 accent */
        perc_pc_coefs(t, c);
        perc_pc_bursts(t, c);
        break;
    case PERC_PC_MARACAS:
        t->res1 = t->res2 = 32768 + ((acc * 13107) >> 16);          /* 0.5 + 0.2 accent */
        perc_pc_coefs(t, c);
        perc_pc_bursts(t, c);
        break;
    case PERC_PC_SHAKER:
        t->res1 = ((acc * 19661) >> 16) + (c->var16 >> 1);          /* 0.3 accent + 0.5 Variation */
        perc_pc_coefs(t, c);
        perc_env_trig(&t->e[0], 0);
        break;
    default:
        if (!perc_env_live(&t->e[0]))
            t->ph[0] = t->ph[1] = t->ph[2] = 0;
        perc_env_trig(&t->e[0], 0);
        perc_env_trig(&t->e[1], 0);
        t->res1 = 13107 + ((acc * 39322) >> 16);                    /* 0.2 + 0.6 accent */
        perc_pc_coefs(t, c);
        break;
    }
}

/* pink noise (three poles, 0x10011738), Q24, x 0.25 */
AINL int32_t perc_pc_pink(perc_perc_t *t)
{
    int32_t w = perc_rng31(&t->rng) >> 7;
    t->pk[0] = (int32_t)(((int64_t)w * 106349833 + (int64_t)t->pk[0] * 1071218531) >> 30);
    t->pk[1] = (int32_t)(((int64_t)w * 318382060 + (int64_t)t->pk[1] * 1034013377) >> 30);
    t->pk[2] = (int32_t)(((int64_t)w * 1130318677 + (int64_t)t->pk[2] * 612032840) >> 30);
    return (t->pk[0] + t->pk[1] + (int32_t)(((int64_t)w * 198427489) >> 30) + t->pk[2]) >> 2;
}

/* the original's float phase: ph + inc rounded to a float's 24 bits, the whole cycle dropped (Q32) */
AINL uint32_t perc_pc_round(uint32_t x, int carry)
{
    int n = 32;
    uint32_t u, h;
    if (!carry) {                                         /* the top bit (24 .. 31): below 2^24 a float holds it all */
        if (x < (1u << 24))
            return x;
        n = x >> 28 ? 28 : 24;
        if (x >> (n + 2))
            n += 2;
        if (x >> (n + 1))
            n++;
    }
    u = 1u << (n - 23);
    h = u >> 1;
    return (x + h - 1 + ((x >> (n - 23)) & 1)) & ~(u - 1);   /* to nearest, ties to even (2^32 wraps to 0) */
}
AINL uint32_t perc_pc_fadd(uint32_t ph, uint32_t inc)
{
    uint32_t s = ph + inc;
    return perc_pc_round(s, s < ph);
}

/* a block's (128 samples') noise, in the original's order */
static void perc_pc_gen(perc_perc_t *t, const perc_perc_ck_t *c)
{
    int i;
    switch (t->model) {
    case PERC_PC_CLAP:
        for (i = 0; i < 128; i++)
            t->n1[i] = perc_rng31(&t->rng) >> 7;
        for (i = 0; i < 128; i++)
            t->n2[i] = perc_rng31(&t->rng) >> 7;
        break;
    case PERC_PC_CLAPP:
    case PERC_PC_MARACAS:
        for (i = 0; i < 128; i++)
            t->n1[i] = perc_pc_pink(t);
        for (i = 0; i < 128; i++)
            t->n2[i] = perc_pc_pink(t);
        break;
    case PERC_PC_SHAKER:
        for (i = 0; i < 128; i++)
            t->n1[i] = perc_pc_pink(t);
        break;
    default: {                                            /* cowbell: this block's increments, the upper two jittered */
        uint32_t u = t->rng.u ^ t->rng.v, v1 = t->rng.v + u, v2;
        u ^= v1;
        v2 = v1 + u;
        u ^= v2;
        t->rng.u = u;
        t->rng.v = v2 + u;
        t->inc[0] = perc_pc_round(perc_hz_inc(c->f16), 0);
        t->inc[1] = perc_inc_mul(perc_hz_inc((int32_t)(((int64_t)c->f16 * 25476514) >> 24)),
                                 (1 << 24) + (int32_t)(((int64_t)(int32_t)v1 * 83886) >> 31));
        t->inc[2] = perc_inc_mul(perc_hz_inc(2 * c->f16), (1 << 24) + (int32_t)(((int64_t)(int32_t)v2 * 167772) >> 31));
        t->inc[1] = perc_pc_round(t->inc[1], 0);
        t->inc[2] = perc_pc_round(t->inc[2], 0);
        break;
    }
    }
}

static int perc_pc_live(const perc_perc_t *t)
{
    switch (t->model) {
    case PERC_PC_CLAP:
    case PERC_PC_CLAPP: return perc_env_live(&t->e[4]) || perc_env_live(&t->e[3]);
    case PERC_PC_MARACAS: return perc_env_live(&t->e[4]);
    case PERC_PC_SHAKER: return perc_env_live(&t->e[0]);
    default: return perc_env_live(&t->e[1]);
    }
}

static int perc_render_perc(perc_perc_t *t, const perc_perc_ck_t *c, int32_t *o)
{
    int32_t ev[5][ENG_BLK], i, k, j0 = t->sub * ENG_BLK;
    int32_t g = t->vel30 > PERC_PC_VMAX ? PERC_PC_VMAX : t->vel30 < 0 ? 0 : t->vel30;
    if (!t->sub)
        t->gen = 0;
    t->sub = (uint8_t)((t->sub + 1) & 3);
    if (!perc_pc_live(t))
        return 0;
    if (!t->gen) {
        perc_pc_gen(t, c);
        t->gen = 1;
    }
    switch (t->model) {
    case PERC_PC_CLAP:
    case PERC_PC_CLAPP:
    case PERC_PC_MARACAS: {
        int clap = t->model != PERC_PC_MARACAS;
        if (t->model == PERC_PC_CLAPP)
            g >>= 1;
        /* the bursts' filter only while a burst sounds (above 0.0001): its output is gated by them */
        int bursts = perc_env_live(&t->e[0]) || perc_env_live(&t->e[1]) || perc_env_live(&t->e[2])
                     || perc_env_live(&t->e[3]);
        for (k = 0; k < 5; k++)
            perc_env_run(&t->e[k], ev[k], ENG_BLK);
        for (i = 0; i < ENG_BLK; i++) {
            int32_t x = 0, y, b;
            if (bursts) {
                x = (int32_t)(((int64_t)t->n1[j0 + i] * g) >> 30);
                x = t->model == PERC_PC_CLAP ? perc_svf_bp(&t->f1, &t->c1, x) : perc_svf_hp(&t->f1, &t->c1, x);
                b = ev[0][i] + ev[1][i] + ev[2][i] + ev[3][i];
                x = b > 0 ? (int32_t)(((int64_t)x * b) >> 30) : 0;
            }
            y = (int32_t)(((int64_t)t->n2[j0 + i] * g) >> 30);
            y = perc_svf_bp(&t->f2b, &t->c2, perc_svf_bp(&t->f2a, &t->c2, y));
            if (clap) {
                int32_t tl = (int32_t)(((int64_t)ev[4][i] * 858993459) >> 32);   /* tail x 0.2 */
                y = tl > 0 ? (int32_t)(((int64_t)y * tl) >> 30) : 0;
                o[i] = (x + y) >> 9;
            } else
                o[i] = (int32_t)(((int64_t)(x + (int32_t)(((int64_t)y * 858993459) >> 32)) * ev[4][i]) >> 39);
        }
        break;
    }
    case PERC_PC_SHAKER:
        perc_env_run(&t->e[0], ev[0], ENG_BLK);
        for (i = 0; i < ENG_BLK; i++) {
            int32_t x = (int32_t)(((int64_t)t->n1[j0 + i] * g) >> 30);
            x = perc_svf_hp(&t->f2a, &t->c1, perc_svf_hp(&t->f1, &t->c1, x));
            o[i] = ev[0][i] > 0 ? (int32_t)(((int64_t)x * ev[0][i]) >> 39) : 0;
        }
        break;
    default:
        perc_env_run(&t->e[0], ev[0], ENG_BLK);
        perc_env_run(&t->e[1], ev[1], ENG_BLK);
        for (i = 0; i < ENG_BLK; i++) {
            int32_t s = (t->ph[0] < 1288490189u ? 2796203 : -2796203) + (t->ph[1] < 1288490189u ? 8388608 : -8388608)
                        + (t->ph[2] < 1288490189u ? 4194304 : -4194304), b, m;
            t->ph[0] = perc_pc_fadd(t->ph[0], t->inc[0]);
            t->ph[1] = perc_pc_fadd(t->ph[1], t->inc[1]);
            t->ph[2] = perc_pc_fadd(t->ph[2], t->inc[2]);
            b = perc_svf_bp(&t->f1, &t->c1, s);
            s = (int32_t)(((int64_t)b * 858993459 + (int64_t)s * 214748365) >> 30);   /* 0.8 bp + 0.2 dry */
            m = (int32_t)(((int64_t)(ev[0][i] + (int32_t)(((int64_t)ev[1][i] * 858993459) >> 32)) * g) >> 31);
            o[i] = m > 0 ? (int32_t)(((int64_t)s * m) >> 39) : 0;
        }
        break;
    }
    return 1;
}

/* ------------------------------------------------------------------------------------- mixer, FX, EQ --- */
typedef struct {
    int32_t gl[PERC_NINST], gr[PERC_NINST];              /* level x pan, Q16 */
    uint8_t out[PERC_NINST];                              /* 0 main, 1 after the waveshaper, 2 after the EQ */
    uint8_t ws_on, ws_type;
    int32_t ws_g;                                         /* saturate / overdrive: input gain, Q24 */
    int32_t ws_s;                                         /* .. the output scale, Q15 */
    int32_t bc_g, bc_inv;                                 /* bitcrush: 2^bits Q16, 2^31 / 2^bits (Q15 out of an integer, Q16) */
    uint8_t eq_on[3];
    perc_bq_c eq[3];
} perc_mix_ck_t;

static void perc_cook_mix(perc_mix_ck_t *m, const int32_t *val)
{
    static const int32_t EQ_HZ[3] = {80 << 16, 2500 << 16, 12000 << 16};
    int i;
    for (i = 0; i < PERC_NINST; i++) {
        const int32_t *p = val + 49 + 5 * i;
        int32_t lv = p[0], pan = clampi(p[1], -Q16, Q16);
        m->gl[i] = pan > 0 ? (int32_t)(((int64_t)lv * (Q16 - pan)) >> 16) : lv;
        m->gr[i] = pan < 0 ? (int32_t)(((int64_t)lv * (Q16 + pan)) >> 16) : lv;
        m->out[i] = (uint8_t)clampi(p[4] >> 16, 0, 2);
    }
    m->ws_on = val[79] > 0;
    m->ws_type = (uint8_t)clampi(val[80] >> 16, 0, 4);
    if (m->ws_type < 3) {
        uint32_t g = perc_pow2((int32_t)(((int64_t)val[79] * 765) + (((int64_t)val[79] * 24) >> 6)));  /* 10^(18 l / 20), Q24 */
        m->ws_g = (int32_t)g;
        if (m->ws_type == 0)
            m->ws_s = Q15 - (int32_t)(g >> 13);           /* 1 - g / 16 */
        else
            m->ws_s = Q15 / 2 - (int32_t)(((int64_t)g * 819) >> 24);   /* 0.5 - 0.025 g */
    } else {
        int32_t bits = (8 << 16) - 7 * val[79];           /* 8 - 7 l */
        uint32_t g16 = perc_pow2(bits << 8) >> 8;         /* 2^bits, Q16 */
        m->bc_g = (int32_t)g16;
        m->bc_inv = (int32_t)udiv64_32(1u << 15, 0, g16); /* 2^47 / g16 */
    }
    for (i = 0; i < 3; i++) {
        int32_t db = val[81 + i];
        m->eq_on[i] = db >= 6554 || db <= -6554;          /* |dB| >= 0.1 */
        if (m->eq_on[i]) {
            perc_bq_c *c = &m->eq[i];
            int32_t w = perc_w28(EQ_HZ[i]), s, cs, omc, al, A, a0;
            perc_sincos(w, &s, &cs, &omc);
            al = perc_alpha(w, s);
            A = (int32_t)perc_pow2((int32_t)(((int64_t)db * 1393321) >> 16));   /* 10^(dB / 40) = 2^(dB 0.0830482), Q24 */
            /* coefficients Q28 (b0 reaches ~2.3 at +18 dB, 12 kHz) */
            {
                int64_t aa = ((int64_t)al * A) >> 24;                  /* alpha A, Q30 */
                int64_t ad = perc_muldiv30(al, 1 << 24, A);            /* alpha / A, Q30 */
                a0 = (int32_t)udiv64_32(1u << 26, 0, (uint32_t)((PERC_ONE + ad) >> 2));   /* 1 / (1 + alpha / A), Q30 */
                c->b0 = (int32_t)(((PERC_ONE + aa) * a0) >> 32);
                c->b1 = (int32_t)((-2 * (int64_t)cs * a0) >> 32);
                c->b2 = (int32_t)(((PERC_ONE - aa) * a0) >> 32);
                c->a1 = c->b1;
                c->a2 = (int32_t)(((PERC_ONE - ad) * a0) >> 32);
            }
        }
    }
}

/* the waveshaper on a Q15 sample (0x100104ec .. 0x100109b8) */
AINL int32_t perc_shaper(const perc_mix_ck_t *m, int32_t x)
{
    int32_t y, s = m->ws_s;
    switch (m->ws_type) {
    case 0:                                               /* Saturate */
        y = (int32_t)(((int64_t)x * m->ws_g) >> 24);
        if (y >= Q15)
            return s;
        if (y <= -Q15)
            return -s;
        return (int32_t)(((int64_t)(((3 << 14) - (int32_t)(((int64_t)y * y) >> 16))) * y >> 15) * s >> 15);
    case 1:                                               /* Overdrive */
    case 2: {                                             /* Overdrive+ */
        int32_t a;
        y = (int32_t)(((int64_t)x * m->ws_g) >> 24);
        a = y < 0 ? -y : y;
        if (a > Q15 / 2)
            return y < 0 ? -s : s;
        if (a > Q15 / 4) {
            int32_t t, r;
            if (m->ws_type == 1) {
                t = 2 * Q15 - 3 * a;                      /* 2 - 3 |y|: s / 3 (3 - t^2) */
                r = (int32_t)(((int64_t)(3 * Q15 - (int32_t)(((int64_t)t * t) >> 15)) * s) >> 15) / 3;
            } else {
                t = 2 * Q15 - 4 * a;                      /* s / 4 (4 - t^3) */
                r = (int32_t)(((int64_t)(4 * Q15 - (int32_t)(((((int64_t)t * t) >> 15) * t) >> 15)) * s) >> 17);
            }
            return y < 0 ? -r : r;
        }
        return (int32_t)(((int64_t)y * s * (m->ws_type == 1 ? 2 : 3)) >> 15);
    }
    default: {                                            /* Bitcrush (round half away), Bitcrush+ (truncate) */
        int64_t v = (int64_t)x * m->bc_g;                 /* x 2^bits: Q31 */
        int neg = v < 0;
        uint64_t u = neg ? (uint64_t)-v : (uint64_t)v;
        int32_t q = (int32_t)((m->ws_type == 3 ? u + (1u << 30) : u) >> 31);
        y = (int32_t)(((int64_t)q * m->bc_inv) >> 16);
        return neg ? -y : y;
    }
    }
}

/* ---------------------------------------------------------------------------------------------- voice --- */
typedef struct {
    uint32_t seq;
    int32_t val[PERC_NPAR];
    perc_kick_ck_t kick;
    perc_tom_ck_t tom;
    perc_snare_ck_t snare;
    perc_hihat_ck_t hihat;
    perc_cymbal_ck_t cymbal;
    perc_perc_ck_t perc;
    perc_mix_ck_t mix;
} perc_ck_t;

typedef struct {
    uint32_t seq;
    int32_t pv[PERC_NPAR];               /* the values the voice has had applied (the original's handlers) */
    perc_kick_t kick;
    perc_tom_t tom;
    perc_snare_t snare;
    perc_hihat_t hihat;
    perc_cymbal_t cymbal;
    perc_perc_t perc;
    perc_bq_s eq[3][2];
    uint8_t eq_on[3];
} perc_v_t;

static void perc_init(void)
{
    uint32_t i;
    for (i = 0; i < PERC_NWT * 257; i++)
        perc_wt[i] = PERC_WT_FLASH[i];
    for (i = 0; i < 1026; i++) {                          /* p(b), b = i / 256: b (b^2 + 27) / (9 b^2 + 27) */
        uint32_t num = i * i + 27 * 65536, den = 9 * i * i + 27 * 65536;
        perc_ptab[i] = (int32_t)(((uint64_t)(i << 16) * udiv64_32(num >> 2, num << 30, den)) >> 30);
    }
}

static void perc_cook(void *ckp, const int32_t *val)
{
    static uint32_t seq;
    perc_ck_t *ck = (perc_ck_t *)ckp;
    int i;
    for (i = 0; i < PERC_NPAR; i++)
        ck->val[i] = val[i];
    perc_cook_kick(&ck->kick, val);
    perc_cook_tom(&ck->tom, val);
    perc_cook_snare(&ck->snare, val);
    perc_cook_hihat(&ck->hihat, val);
    perc_cook_cymbal(&ck->cymbal, val);
    perc_cook_perc(&ck->perc, val);
    perc_cook_mix(&ck->mix, val);
    if (++seq == 0)
        seq = 1;
    ck->seq = seq;
}

static void perc_reset(void *vp)
{
    perc_v_t *v = (perc_v_t *)vp;
    int i;
    uint8_t *p = (uint8_t *)v;
    for (i = 0; i < (int)sizeof *v; i++)
        p[i] = 0;
    for (i = 0; i < PERC_NPAR; i++)
        v->pv[i] = PERC_UNSET;
    perc_reset_kick(&v->kick);
    perc_reset_tom(&v->tom);
    perc_reset_snare(&v->snare);
    perc_reset_hihat(&v->hihat);
    perc_reset_cymbal(&v->cymbal);
    perc_reset_perc(&v->perc);
}

/* the values that changed since the voice saw them: the original's handlers, on the voice */
static void perc_sync(perc_v_t *v, const perc_ck_t *ck)
{
    int i;
    if (v->seq == ck->seq)
        return;
    v->seq = ck->seq;
    for (i = 0; i < PERC_NPAR; i++) {
        if (v->pv[i] == ck->val[i])
            continue;
        v->pv[i] = ck->val[i];
        if (i < 9)
            perc_apply_kick(&v->kick, &ck->kick, i);
        else if (i < 18)
            perc_apply_tom(&v->tom, &ck->tom, i);
        else if (i < 27)
            perc_apply_snare(&v->snare, &ck->snare, i);
        else if (i < 36)
            perc_apply_hihat(&v->hihat, &ck->hihat, i);
        else if (i < 45)
            perc_apply_cymbal(&v->cymbal, &ck->cymbal, i);
        else if (i < 49)
            perc_apply_perc(&v->perc, &ck->perc, i);
        else if (i >= 81) {                               /* an EQ band switched off: its state cleared */
            int b = i - 81;
            if (!ck->mix.eq_on[b] && v->eq_on[b]) {
                perc_bq_s z = {0, 0, 0, 0};
                v->eq[b][0] = v->eq[b][1] = z;
            }
            v->eq_on[b] = ck->mix.eq_on[b];
        }
    }
}

/* 0x1000c4e0: the note picks the instrument */
static void perc_on(void *vp, const void *ckp, int32_t note16, int vel)
{
    perc_v_t *v = (perc_v_t *)vp;
    const perc_ck_t *ck = (const perc_ck_t *)ckp;
    int n = note16 >> 16;
    perc_sync(v, ck);
    if (n < 0 || n > 127 || vel <= 0)
        return;
    switch (n) {
    case 60: case 61: perc_on_kick(&v->kick, &ck->kick, vel); break;
    case 62: case 63: perc_on_snare(&v->snare, &ck->snare, vel); break;
    case 64: case 65: perc_on_hihat(&v->hihat, &ck->hihat, 0, vel); break;
    case 66: case 67: perc_on_hihat(&v->hihat, &ck->hihat, 1, vel); break;
    case 68: case 69: perc_on_cymbal(&v->cymbal, &ck->cymbal, vel); break;
    case 70: case 71: perc_on_perc(&v->perc, &ck->perc, vel); break;
    default: perc_on_tom(&v->tom, &ck->tom, n > 59 ? n - 12 : n, vel); break;
    }
}

static void perc_off(void *vp, const void *ckp, int32_t note16)
{
    (void)vp;
    (void)ckp;
    (void)note16;                                         /* the original ignores note off */
}

static int perc_render(void *vp, const void *ckp, int32_t *l, int32_t *r)
{
    perc_v_t *v = (perc_v_t *)vp;
    const perc_ck_t *ck = (const perc_ck_t *)ckp;
    const perc_mix_ck_t *m = &ck->mix;
    int32_t buf[ENG_BLK], acc[3][2][ENG_BLK];
    int used[3] = {0, 0, 0}, any = 0, i, k, b;
    perc_sync(v, ck);
    for (k = 0; k < PERC_NINST; k++) {
        int on, g;
        switch (k) {
        case PERC_KICK: on = perc_render_kick(&v->kick, &ck->kick, buf); break;
        case PERC_TOM: on = perc_render_tom(&v->tom, &ck->tom, buf); break;
        case PERC_SNARE: on = perc_render_snare(&v->snare, &ck->snare, buf); break;
        case PERC_HIHAT: on = perc_render_hihat(&v->hihat, &ck->hihat, buf); break;
        case PERC_CYMBAL: on = perc_render_cymbal(&v->cymbal, &ck->cymbal, buf); break;
        default: on = perc_render_perc(&v->perc, &ck->perc, buf); break;
        }
        if (!on)
            continue;
        any = 1;
        if (!m->gl[k] && !m->gr[k])
            continue;
        g = m->out[k];
        if (!used[g]) {
            used[g] = 1;
            for (i = 0; i < ENG_BLK; i++)
                acc[g][0][i] = acc[g][1][i] = 0;
        }
        for (i = 0; i < ENG_BLK; i++) {
            acc[g][0][i] += (int32_t)(((int64_t)buf[i] * m->gl[k]) >> 16);
            acc[g][1][i] += (int32_t)(((int64_t)buf[i] * m->gr[k]) >> 16);
        }
    }
    for (b = 0; b < 2; b++) {
        int32_t *o = b ? r : l;
        for (i = 0; i < ENG_BLK; i++)
            o[i] = used[0] ? acc[0][b][i] : 0;
        if (used[0] && m->ws_on)
            for (i = 0; i < ENG_BLK; i++)
                o[i] = perc_shaper(m, o[i]);
        if (used[1])
            for (i = 0; i < ENG_BLK; i++)
                o[i] += acc[1][b][i];
    }
    {
        int eq = 0;
        for (k = 0; k < 3; k++)
            if (v->eq_on[k]) {
                eq = 1;
                for (b = 0; b < 2; b++) {
                    int32_t *o = b ? r : l;
                    perc_bq_s *s = &v->eq[k][b];
                    if (!any && !s->x1 && !s->x2 && !s->y1 && !s->y2)
                        continue;
                    for (i = 0; i < ENG_BLK; i++)
                        o[i] = perc_bq_run28(s, &m->eq[k], o[i]);
                    if (!any && (s->y1 || s->y2 || s->x1 || s->x2))
                        any = 1;
                }
            }
        (void)eq;
    }
    for (b = 0; b < 2; b++) {
        int32_t *o = b ? r : l;
        for (i = 0; i < ENG_BLK; i++)
            o[i] = 2 * (o[i] + (used[2] ? acc[2][b][i] : 0));
    }
    return any;
}

/* ------------------------------------------------------------------------------------------ descriptor --- */
static const eng_par_t PERC_PAR[PERC_NPAR] = {
    {"BODY WAVE", 0, 11468800, 0, 0, 1, 0, 20},               /* 0: Body Wave 0 .. 175, 0 */
    {"PITCH", 786432, 7798784, 2031616, 0, 0, 0, 23},         /* 1: Pitch 12 .. 119, 31 */
    {"BODY ATTACK", 0, 6553600, 117965, 1, 0, 0, 75},         /* 2: Body Attack 0 .. 100, 1.8 */
    {"BODY DECAY", 0, 655360, 17039, 1, 0, 0, 72},            /* 3: Body Decay 0 .. 10, 0.26 */
    {"TRANS TIMBRE", -65536, 65536, 20972, 0, 0, 0, 21},      /* 4: Transient Timbre -1 .. 1, 0.32 */
    {"TRANS LEVEL", 0, 65536, 32768, 0, 0, 0, 22},            /* 5: Transient Level 0 .. 1, 0.5 */
    {"MOD DECAY", 0, 655360, 8520, 1, 0, 0, 81},              /* 6: Mod Decay 0 .. 10, 0.13 */
    {"MOD AMOUNT", -65536, 65536, 13107, 2, 0, 0, 77},        /* 7: Mod Amount -1 .. 1, 0.2 */
    {"MODEL", 0, 131072, 131072, 0, 1, 0, 255},               /* 8: Model 0 .. 2, 2 */
    {"BODY WAVE", 0, 11468800, 0, 0, 1, 1, 20},               /* 9: Body Wave 0 .. 175, 0 */
    {"BODY ATTACK", 0, 6553600, 91750, 1, 0, 1, 75},          /* 10: Body Attack 0 .. 100, 1.4 */
    {"BODY DECAY", 0, 655360, 26870, 1, 0, 1, 72},            /* 11: Body Decay 0 .. 10, 0.41 */
    {"TRANS TIMBRE", 0, 65536, 38011, 0, 0, 1, 21},           /* 12: Transient Timbre 0 .. 1, 0.58 */
    {"TRANS LEVEL", 0, 131072, 65536, 0, 0, 1, 22},           /* 13: Transient Level 0 .. 2, 1 */
    {"MOD DECAY", 0, 655360, 3277, 1, 0, 1, 81},              /* 14: Mod Decay 0 .. 10, 0.05 */
    {"PITCH MOD", -655360, 655360, 108134, 2, 0, 1, 77},      /* 15: Pitch Mod -10 .. 10, 1.65 */
    {"WAVE MOD", -65536, 65536, 26214, 0, 0, 1, 24},          /* 16: Wave Mod -1 .. 1, 0.4 */
    {"MODEL", 0, 131072, 65536, 0, 1, 1, 255},                /* 17: Model 0 .. 2, 1 */
    {"BODY WAVE", 0, 11468800, 0, 0, 1, 2, 20},               /* 18: Body Wave 0 .. 175, 0 */
    {"PITCH", 786432, 7798784, 3276800, 0, 0, 2, 23},         /* 19: Pitch 12 .. 119, 50 */
    {"TONE", 0, 65536, 32768, 0, 0, 2, 23},                   /* 20: Tone 0 .. 1, 0.5 */
    {"MODEL", 0, 262144, 65536, 0, 1, 2, 255},                /* 21: Model 0 .. 4, 1 */
    {"BODY DECAY", 0, 655360, 11141, 1, 0, 2, 72},            /* 22: Body Decay 0 .. 10, 0.17 */
    {"SNAP ATTACK", 0, 6553600, 52429, 1, 0, 2, 80},          /* 23: Snap Attack 0 .. 100, 0.8 */
    {"SNAP DECAY", 0, 655360, 17039, 1, 0, 2, 81},            /* 24: Snap Decay 0 .. 10, 0.26 */
    {"BODY/SNAP", -65536, 65536, -25559, 0, 0, 2, 22},        /* 25: Body/Snap Mix -1 .. 1, -0.39 */
    {"SNAP TIMBRE", 0, 65536, 8520, 0, 0, 2, 24},             /* 26: Snap Timbre 0 .. 1, 0.13 */
    {"DECAY SHAPE", 0, 65536, 7864, 0, 0, 3, 80},             /* 27: Decay Shape 0 .. 1, 0.12 */
    {"CLOSED DECAY", 0, 655360, 10486, 1, 0, 3, 81},          /* 28: Closed Decay 0 .. 10, 0.16 */
    {"OPEN DECAY", 0, 655360, 53084, 1, 0, 3, 255},           /* 29: Open Decay 0 .. 10, 0.81 */
    {"HPF CUTOFF", 655360, 1310720000, 332922880, 1, 0, 3, 24},   /* 30: HPF Cutoff 10 .. 20000, 5080 */
    {"LPF CUTOFF", 655360, 1310720000, 592445440, 1, 0, 3, 24},   /* 31: LPF Cutoff 10 .. 20000, 9040 */
    {"TONE PITCH", 6553600, 57671680, 13697024, 1, 0, 3, 255},    /* 32: Tone Pitch 100 .. 880, 209 */
    {"TONE/NOISE", -65536, 65536, -5898, 0, 0, 3, 255},       /* 33: Tone/Noise Mix -1 .. 1, -0.09 */
    {"TIMBRE", 0, 1572864, 0, 0, 1, 3, 255},                  /* 34: Timbre 0 .. 24, 0 */
    {"MODEL", 0, 65536, 0, 0, 1, 3, 255},                     /* 35: Model 0 .. 1, 0 */
    {"BODY FILTER", 32768000, 655360000, 196608000, 1, 0, 4, 24}, /* 36: Body Filter 500 .. 10000, 3000 */
    {"IMPACT FILT", 32768000, 655360000, 458752000, 1, 0, 4, 24}, /* 37: Impact Filter 500 .. 10000, 7000 */
    {"BODY DECAY", 0, 655360, 98304, 1, 0, 4, 72},            /* 38: Body Decay 0 .. 10, 1.5 */
    {"IMPACT DECAY", 0, 655360, 16384, 1, 0, 4, 72},          /* 39: Impact Decay 0 .. 10, 0.25 */
    {"TONE PITCH", 5242880, 57671680, 13369344, 1, 0, 4, 255},    /* 40: Tone Pitch 80 .. 880, 204 */
    {"TONE/NOISE", -65536, 65536, -25559, 0, 0, 4, 255},      /* 41: Tone/Noise Mix -1 .. 1, -0.39 */
    {"IMPACT/BODY", -65536, 65536, 19005, 0, 0, 4, 255},      /* 42: Impact/Body Mix -1 .. 1, 0.29 */
    {"MODEL", 0, 65536, 0, 0, 1, 4, 255},                     /* 43: Model 0 .. 1, 0 */
    {"TIMBRE", 0, 1572864, 0, 0, 1, 4, 255},                  /* 44: Timbre 0 .. 24, 0 */
    {"MODEL", 0, 262144, 0, 0, 1, 5, 255},                    /* 45: Model 0 .. 4, 0 */
    {"VARIATION", 0, 65536, 4588, 0, 0, 5, 255},              /* 46: Variation 0 .. 1, 0.07 */
    {"TIMBRE", 0, 65536, 40632, 0, 0, 5, 24},                 /* 47: Timbre 0 .. 1, 0.62 */
    {"DECAY", 0, 131072, 9175, 1, 0, 5, 81},                  /* 48: Decay 0 .. 2, 0.14 */
    {"KICK LEVEL", 0, 131072, 65536, 0, 0, 6, 255},           /* 49: Kick Level 0 .. 2, 1 */
    {"KICK PAN", -65536, 65536, 0, 0, 0, 6, 255},             /* 50: Kick Panning -1 .. 1, 0 */
    {"KICK DELAY", 0, 65536, 0, 0, 0, 6, 255},                /* 51: Kick Delay 0 .. 1, 0 */
    {"KICK REVERB", 0, 65536, 0, 0, 0, 6, 255},               /* 52: Kick Reverb 0 .. 1, 0 */
    {"KICK OUTPUT", 0, 131072, 0, 0, 1, 6, 255},              /* 53: Kick Output 0 .. 2, 0 */
    {"TOM LEVEL", 0, 131072, 52429, 0, 0, 6, 255},            /* 54: Tom Level 0 .. 2, 0.8 */
    {"TOM PAN", -65536, 65536, 13107, 0, 0, 6, 255},          /* 55: Tom Panning -1 .. 1, 0.2 */
    {"TOM DELAY", 0, 65536, 0, 0, 0, 6, 255},                 /* 56: Tom Delay 0 .. 1, 0 */
    {"TOM REVERB", 0, 65536, 0, 0, 0, 6, 255},                /* 57: Tom Reverb 0 .. 1, 0 */
    {"TOM OUTPUT", 0, 131072, 0, 0, 1, 6, 255},               /* 58: Tom Output 0 .. 2, 0 */
    {"SNARE LEVEL", 0, 131072, 65536, 0, 0, 6, 255},          /* 59: Snare Level 0 .. 2, 1 */
    {"SNARE PAN", -65536, 65536, -6554, 0, 0, 6, 255},        /* 60: Snare Panning -1 .. 1, -0.1 */
    {"SNARE DELAY", 0, 65536, 32768, 0, 0, 6, 255},           /* 61: Snare Delay 0 .. 1, 0.5 */
    {"SNARE REVERB", 0, 65536, 32768, 0, 0, 6, 255},          /* 62: Snare Reverb 0 .. 1, 0.5 */
    {"SNARE OUTPUT", 0, 131072, 0, 0, 1, 6, 255},             /* 63: Snare Output 0 .. 2, 0 */
    {"HIHAT LEVEL", 0, 131072, 65536, 0, 0, 6, 255},          /* 64: Hihat Level 0 .. 2, 1 */
    {"HIHAT PAN", -65536, 65536, 19661, 0, 0, 6, 255},        /* 65: Hihat Panning -1 .. 1, 0.3 */
    {"HIHAT DELAY", 0, 65536, 32768, 0, 0, 6, 255},           /* 66: Hihat Delay 0 .. 1, 0.5 */
    {"HIHAT REVERB", 0, 65536, 32768, 0, 0, 6, 255},          /* 67: Hihat Reverb 0 .. 1, 0.5 */
    {"HIHAT OUTPUT", 0, 131072, 0, 0, 1, 6, 255},             /* 68: Hihat Output 0 .. 2, 0 */
    {"CYMBAL LEVEL", 0, 131072, 65536, 0, 0, 6, 255},         /* 69: Cymbal Level 0 .. 2, 1 */
    {"CYMBAL PAN", -65536, 65536, -19661, 0, 0, 6, 255},      /* 70: Cymbal Panning -1 .. 1, -0.3 */
    {"CYMBAL DELAY", 0, 65536, 32768, 0, 0, 6, 255},          /* 71: Cymbal Delay 0 .. 1, 0.5 */
    {"CYMBAL VERB", 0, 65536, 32768, 0, 0, 6, 255},           /* 72: Cymbal Reverb 0 .. 1, 0.5 */
    {"CYMBAL OUT", 0, 131072, 0, 0, 1, 6, 255},               /* 73: Cymbal Output 0 .. 2, 0 */
    {"PERC LEVEL", 0, 131072, 65536, 0, 0, 6, 255},           /* 74: Perc Level 0 .. 2, 1 */
    {"PERC PAN", -65536, 65536, 6554, 0, 0, 6, 255},          /* 75: Perc Panning -1 .. 1, 0.1 */
    {"PERC DELAY", 0, 65536, 32768, 0, 0, 6, 255},            /* 76: Perc Delay 0 .. 1, 0.5 */
    {"PERC REVERB", 0, 65536, 32768, 0, 0, 6, 255},           /* 77: Perc Reverb 0 .. 1, 0.5 */
    {"PERC OUTPUT", 0, 131072, 0, 0, 1, 6, 255},              /* 78: Perc Output 0 .. 2, 0 */
    {"SHAPER LEVEL", 0, 65536, 0, 0, 0, 7, 255},              /* 79: Waveshaper Level 0 .. 1, 0 */
    {"SHAPER TYPE", 0, 262144, 0, 0, 1, 7, 255},              /* 80: Waveshaper Type 0 .. 4, 0 */
    {"EQ LOW 80", -1179648, 1179648, 0, 0, 0, 7, 255},        /* 81: EQ Low 80 Hz -18 .. 18 dB, 0 */
    {"EQ MID 2.5K", -1179648, 1179648, 0, 0, 0, 7, 255},      /* 82: EQ Mid 2.5 kHz -18 .. 18 dB, 0 */
    {"EQ HIGH 12K", -1179648, 1179648, 0, 0, 0, 7, 255},      /* 83: EQ High 12 kHz -18 .. 18 dB, 0 */
};

static const char *const PERC_SECTION[8] = {"KICK", "TOM", "SNARE", "HIHAT", "CYMBAL", "PERC", "MIXER", "FX"};

static const eng_macro_t PERC_MACRO[6] = {
    {"PERC TYPE", 0, 1, {{45, 32768}}},
    {"MOVEMENT", 0, 5, {{3, 1311}, {24, 1638}, {11, 1638}, {28, 3277}, {29, 3277}}},
    {"EXPERIMENT", 0, 5, {{0, 13107}, {9, 36045}, {18, 11796}, {37, 6554}, {25, -3277}}},
    {"TIMBRE", 0, 5, {{34, 22938}, {0, 13107}, {12, 6554}, {41, 16384}, {18, 13107}}},
    {"EQ", 0, 3, {{81, -65536}, {82, 1966}, {83, 6554}}},
    {"SHAPE", 0, 1, {{79, 8192}}},
};

#define PERC_W11(n) n " 0%", n " 10%", n " 20%", n " 30%", n " 40%", n " 50%", n " 60%", n " 70%", n " 80%", \
                    n " 90%", n " 100%"
static const char *const PERC_E_WAVE[176] = {
    PERC_W11("SAT SIN"), PERC_W11("TRIANGLE"), PERC_W11("SQUARE"), PERC_W11("SQUARE FM"), PERC_W11("FM1"),
    PERC_W11("FM3"), PERC_W11("FM7"), PERC_W11("FM11"), PERC_W11("TZFM1"), PERC_W11("TZFM3"), PERC_W11("TZFM7"),
    PERC_W11("TZFM11"), PERC_W11("GLITCH 1"), PERC_W11("GLITCH 2"), PERC_W11("GLITCH 3"), PERC_W11("GLITCH 4"),
};
static const char *const PERC_E_KICK[3] = {"CLASSIC8", "SYNCED8", "CLASSIC9"};
static const char *const PERC_E_TOM[3] = {"CLASSIC", "CLASSIC+", "RESONANT"};
static const char *const PERC_E_SNARE[5] = {"CLASSIC8", "CLASSIC9", "CLASSIC9+", "MODERN", "MODERN+"};
static const char *const PERC_E_METAL[2] = {"CLASSIC", "FM"};
static const char *const PERC_E_PERC[5] = {"CLAP", "CLAP+", "MARACAS", "SHAKER", "COWBELL"};
static const char *const PERC_E_OUT[3] = {"MAIN", "POST OD", "POST EQ"};
static const char *const PERC_E_SHAPER[5] = {"SATURATE", "OVERDRIVE", "OVERDRIVE+", "BITCRUSH", "BITCRUSH+"};

static const char *const *const PERC_ENUMS[PERC_NPAR] = {
    [0] = PERC_E_WAVE, [8] = PERC_E_KICK, [9] = PERC_E_WAVE, [17] = PERC_E_TOM, [18] = PERC_E_WAVE,
    [21] = PERC_E_SNARE, [35] = PERC_E_METAL, [43] = PERC_E_METAL, [45] = PERC_E_PERC,
    [53] = PERC_E_OUT, [58] = PERC_E_OUT, [63] = PERC_E_OUT, [68] = PERC_E_OUT, [73] = PERC_E_OUT, [78] = PERC_E_OUT,
    [80] = PERC_E_SHAPER,
};

const eng_t ENG_PERC = {
    "PERC", "DRUM MACHINE", PERC_NPAR, 1, 8, 6,
    PERC_PAR, PERC_SECTION, PERC_MACRO, PERC_ENUMS,
    sizeof(perc_v_t), sizeof(perc_ck_t),
    perc_init, perc_cook, perc_reset, perc_on, perc_off, perc_render,
};
