/* SPDX-License-Identifier: GPL-3.0-only
 * FM DRUM: Amalthea's drum and percussion machine, re-implemented in floating point after the Digitone II's
 * FM DRUM as DigiRust documents it (mvave-digirust docs/dt2-curves.md, appendix C: formulas and numbers read
 * from the owner's own copy of the OS; no Elektron code or data). Its parameters are the DT2's (0 .. 127,
 * bipolar on 64), in the order of mvave-digirust's sound.h (FD_*), so Bigorna's DRUM recipes drive it as is.
 *
 * What the FM-1's fixed-point DRUM (mvave-digirust fmd.c) had to give up, and this one keeps:
 *   - every envelope per sample, at the DT2's law (e^-6 in N = 8 fs (0.1 p + 0.9 p^4) + 32 samples), not per
 *     32-sample block; the pitch sweep per sample too (the DT2 steps it a block)
 *   - the body (three operators, feedback, the folder) 2x oversampled, decimated by a half-band filter: high
 *     ratios and a sweep from 17 x the note fold back much less
 *   - the 33-wave set as the DT2's (sine; saw build-up to 8 partials; low partials removed; 1-3-8; square
 *     build-up to 17; 1-5-7-9-11, 1-5, 1-5-9, 1-5-9-11; every third partial from 4), crossfaded on 32 p
 *   - 32 transients crossfaded on 31 p, as the DT2 does with its sampled ones (here synthesised: clicks, ticks,
 *     snaps, pops, zaps, knocks, metal, glass, crackle, grit ..), 50 .. 150 ms
 *   - float precision for the ratios (0.001 .. 32), the folder and the noise's base-width band
 * Kept from DigiRust: ABN (A's and B's waveforms mixed toward a held noise, a new value each of their cycles).
 *
 * Body:      C, A, B in one of 7 algorithms; f = f0 (1 + 16 SDEP^2 sweep), sweep e^-6 in 5 s (0.1 p + 0.9 p^4);
 *            A / B at RATIO x f, their levels MOD^2 cycles x their envelopes (DEC to END); FDBK^2 x 2/pi cycles;
 *            FOLD: u = clip((p^2 + 0.07) x), 1.12 u - 0.6716 tanh 4u, mixed min(16 p, 1); body env HOLD (1000
 *            p^2 ms; 127 for ever) then DEC (127: none); LEV^2. In algorithms 5 .. 7 B sounds as well.
 * Noise:     white, or grains (odds a sample 0.0001 + (0.992 - p)^2); NRM ring-modulated by C; one-pole high-pass
 *            at BASE and low-pass at BASE + WDTH (5 x 8000^(v/127) Hz; >= 127 open); NHLD / NDEC; 2 NLEV^3.
 * Transient: TRAN's position, TLEV^2. */
#include <math.h>

#define FDF_FS 44100.f
#define FDF_WN 2048                       /* a wave's points (+1 guard) */
#define FDF_NWAVE 33
#define FDF_NTRAN 32
#define FDF_TMAX 6615                     /* 150 ms */
#define FDF_HB 23                         /* half-band taps */

static float FDF_WAVE[FDF_NWAVE][FDF_WN + 1];
static float FDF_POS[128][FDF_WN + 1];          /* the waves crossfaded at each OP.C / OP.AB value (32 p) */
static float FDF_TRAN[FDF_NTRAN][FDF_TMAX];
static uint16_t FDF_TLEN[FDF_NTRAN];
static float FDF_HBK[FDF_HB];

typedef struct { uint8_t order[3], mod[3], fb, bout; } fdf_alg_t;
#define FB_(o) (1u << (o))
enum { FDF_C, FDF_A, FDF_B };
static const fdf_alg_t FDF_ALG[7] = {                  /* the DT2's seven, as its screen draws them */
    {{FDF_A, FDF_B, FDF_C}, {FB_(FDF_A) | FB_(FDF_B), 0, 0}, FB_(FDF_B), 0},             /* A, B -> C; B feeds back */
    {{FDF_A, FDF_B, FDF_C}, {FB_(FDF_A) | FB_(FDF_B), 0, 0}, FB_(FDF_A) | FB_(FDF_B), 0},
    {{FDF_B, FDF_A, FDF_C}, {FB_(FDF_A) | FB_(FDF_B), FB_(FDF_B), 0}, FB_(FDF_B), 0},    /* B -> A, A and B -> C */
    {{FDF_B, FDF_A, FDF_C}, {FB_(FDF_A), FB_(FDF_B), 0}, FB_(FDF_B), 0},                 /* B -> A -> C */
    {{FDF_A, FDF_B, FDF_C}, {FB_(FDF_A), 0, 0}, FB_(FDF_B), 1},                          /* A -> C; B heard */
    {{FDF_A, FDF_B, FDF_C}, {FB_(FDF_A), 0, 0}, FB_(FDF_A), 1},
    {{FDF_A, FDF_B, FDF_C}, {FB_(FDF_A), 0, 0}, FB_(FDF_A) | FB_(FDF_B), 1},
};
#undef FB_

typedef struct {
    uint8_t alg, nrst, nrm, bhold_inf, nhold_inf, has_noise, has_tran;
    float tune;                   /* semitones */
    float sdep, sfall;            /* 16 p^2; the sweep's factor a (2x) sample */
    uint8_t opc, opab;            /* OP.C, OP.AB: rows of FDF_POS */
    float fb, abn, fold, fwet;
    float ra, rb, moda, modb, enda, endb, deca, decb;   /* (decays: factors a 2x sample) */
    uint32_t bhold, nhold;        /* 2x samples / samples */
    float bdec, ndec;             /* body: a 2x sample; noise: a sample (1: none) */
    float blev, tlev, nlev;
    int32_t phc;                  /* C's start phase in degrees, -1: not reset */
    float gran, gran_g;
    float hp, lp;                 /* the band's one-pole coefficients (lp 1: open) */
    int tran0;                    /* the transients crossfaded: tran0, tran0 + 1 by tfrac */
    float tfrac;
} fdf_ck_t;

typedef struct {
    float ph[3], o1[3], o2[3], sh[3];
    float sw, ea, eb, be, ne;
    uint32_t bh, nh, tn;
    uint32_t rn, sr;              /* the noise's and ABN's generators */
    float hpz, hpx, lpz;
    float hb[2 * FDF_HB];         /* the decimator's history (2x samples), written twice: a contiguous window */
    int hbi;
    float lev;                    /* LEV, ramped a block */
} fdf_v_t;

static uint32_t fdf_rnd(uint32_t *s) { uint32_t x = *s ? *s : 0x9e3779b9u; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static float fdf_bi(uint32_t *s) { return (float)(int32_t)fdf_rnd(s) * (1.f / 2147483648.f); }

/* ------------------------------------------------------------------ tables (once, before any voice sounds) */
static void fdf_partials(float *t, const int *h, int n)
{
    float peak = 0.f;
    for (int i = 0; i < FDF_WN; i++) {
        double x = 6.283185307179586 * i / FDF_WN, y = 0;
        for (int k = 0; k < n; k++) y += sin(h[k] * x) / h[k];
        t[i] = (float)y;
        if (fabsf(t[i]) > peak) peak = fabsf(t[i]);
    }
    for (int i = 0; i < FDF_WN; i++) t[i] *= 0.99f / peak;
    t[FDF_WN] = t[0];
}
static void fdf_waves(void)
{
    int h[24], n, w;
    for (w = 0; w < FDF_NWAVE; w++) {
        n = 0;
        h[n++] = 1;
        if (w >= 1 && w <= 7) for (int k = 2; k <= w + 1; k++) h[n++] = k;            /* saw build-up */
        else if (w >= 8 && w <= 13) for (int k = w - 5; k <= 8; k++) h[n++] = k;      /* low partials removed */
        else if (w == 14) { h[n++] = 3; h[n++] = 8; }
        else if (w >= 15 && w <= 22) for (int k = 3; k <= 2 * (w - 15) + 3; k += 2) h[n++] = k;   /* square */
        else if (w == 23) { h[n++] = 5; h[n++] = 7; h[n++] = 9; h[n++] = 11; }
        else if (w == 24) h[n++] = 5;
        else if (w == 25) { h[n++] = 5; h[n++] = 9; }
        else if (w == 26) { h[n++] = 5; h[n++] = 9; h[n++] = 11; }
        else if (w >= 27) for (int k = 4, c = 0; c < w - 26; k += 3, c++) h[n++] = k;   /* every third from 4 */
        fdf_partials(FDF_WAVE[w], h, n);
    }
}
/* the transients, from the softest click to the harshest grit */
static void fdf_transients(void)
{
    uint32_t r = 0x7a11b0u;
    for (int k = 0; k < FDF_NTRAN; k++) {
        float *t = FDF_TRAN[k], peak = 0.f, ph = 0.f, ph2 = 0.f, lp = 0.f, lp2 = 0.f, hold = 0.f;
        int len = k < 12 ? 2205 : k < 24 ? 4410 : FDF_TMAX;                           /* 50 / 100 / 150 ms */
        FDF_TLEN[k] = (uint16_t)len;
        for (int i = 0; i < len; i++) {
            float s = i / FDF_FS, e = expf(-s * 90.f), x = 0.f, n = fdf_bi(&r);
            switch (k) {
            case 0: x = i == 0 ? 1.f : i == 1 ? -0.8f : 0.f; break;                     /* click */
            case 1: x = (i == 0 || i == 90) ? 1.f : (i == 1 || i == 91) ? -0.7f : 0.f; break;   /* double click */
            case 2: lp += (n - lp) * 0.08f; x = lp * expf(-s * 600.f) * 6.f; break;      /* soft click */
            case 3: ph += 8200.f / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 700.f); break;   /* ticks */
            case 4: ph += 5100.f / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 500.f); break;
            case 5: ph += 2900.f / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 380.f); break;
            case 6: lp += (n - lp) * 0.9f; x = (n - lp) * expf(-s * 260.f); break;     /* snaps: high, band, low */
            case 7: lp += (n - lp) * 0.35f; lp2 += (lp - lp2) * 0.35f; x = (lp - lp2) * 3.f * expf(-s * 180.f); break;
            case 8: lp += (n - lp) * 0.05f; x = lp * 5.f * expf(-s * 140.f); break;
            case 9: lp += (n - lp) * 0.3f; x = lp * expf(-s * 45.f); break;              /* a longer burst */
            case 10: ph += (60.f + 340.f * expf(-s * 120.f)) / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 60.f); break;   /* pops */
            case 11: ph += (200.f + 1000.f * expf(-s * 160.f)) / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 80.f); break;
            case 12: ph += (100.f + 6000.f * expf(-s * 260.f)) / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 70.f); break;  /* zaps */
            case 13: ph += (80.f + 3000.f * expf(-s * 60.f)) / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 30.f); break;
            case 14: lp += (n - lp) * 0.03f; lp2 += (lp - lp2) * 0.03f; x = lp2 * 30.f * expf(-s * 40.f); break;   /* thump */
            case 15: ph += 180.f / FDF_FS; ph2 += 910.f / FDF_FS; x = (sinf(6.2831853f * ph) * expf(-s * 50.f) + 0.6f * sinf(6.2831853f * ph2) * expf(-s * 140.f)); break;   /* knock */
            case 16: ph += 1230.f / FDF_FS; ph2 += 2710.f / FDF_FS; x = (sinf(6.2831853f * ph) + 0.7f * sinf(6.2831853f * ph2)) * expf(-s * 110.f); break;   /* wood */
            case 17: ph += 1710.f / FDF_FS; ph2 += 3930.f / FDF_FS; x = (sinf(6.2831853f * ph) + 0.6f * sinf(6.2831853f * ph2)) * expf(-s * 160.f) + n * 0.4f * expf(-s * 400.f); break;   /* rim */
            case 18: ph += 3891.f / FDF_FS; x = (ph - floorf(ph) < 0.5f ? 0.7f : -0.7f) * expf(-s * 90.f); break;   /* metal: a square */
            case 19: {                                                                   /* metal: six squares */
                static const float f[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.f, 800.f};
                float y = 0.f;
                for (int q = 0; q < 6; q++) { float p = fmodf(s * f[q] * 7.3f, 1.f); y += p < 0.5f ? 1.f : -1.f; }
                x = y / 6.f * expf(-s * 60.f);
                break;
            }
            case 20: x = (sinf(6.2831853f * 4530.f * s) + sinf(6.2831853f * 7130.f * s) * 0.7f + sinf(6.2831853f * 9370.f * s) * 0.5f) * expf(-s * 50.f) * 0.5f; break;   /* glass */
            case 21: ph += 1100.f / FDF_FS; x = sinf(6.2831853f * ph + 3.f * expf(-s * 30.f) * sinf(6.2831853f * ph * 3.51f)) * expf(-s * 35.f); break;   /* struck FM */
            case 22: x = (i % 88) < 2 ? (i % 88 ? -0.7f : 1.f) * expf(-s * 25.f) : 0.f; break;     /* a burst of clicks */
            case 23: x = (fdf_rnd(&r) & 63u) == 0 ? n * 2.f * expf(-s * 20.f) : 0.f; break;       /* crackle */
            case 24: if (i % 22 == 0) hold = n; x = hold * expf(-s * 30.f); break;                  /* crushed noise */
            case 25: x = n * sinf(6.2831853f * 3100.f * s) * expf(-s * 28.f); break;               /* ring-modulated noise */
            case 26: ph += 58.f / FDF_FS; x = sinf(6.2831853f * ph) * expf(-s * 22.f); break;     /* sub thud */
            case 27: lp += (n - lp) * 0.6f; x = (n - lp) * (1.f - expf(-s * 60.f)) * expf(-s * 18.f); break;   /* air */
            case 28: {                                                                   /* scrape: a band sweeping */
                float c = 0.02f + 0.3f * s / 0.15f;
                lp += (n - lp) * c; lp2 += (lp - lp2) * c; x = (lp - lp2) * 4.f * expf(-s * 12.f);
                break;
            }
            case 29: x = (i % (int)(30 + 400 * s * s * 40.f + 1)) == 0 ? 1.f * expf(-s * 15.f) : 0.f; break;   /* spark */
            case 30: x = ((i / 441) % 3 == 0) ? n * expf(-s * 14.f) : 0.f; break;                  /* static gate */
            default: ph += 97.f / FDF_FS; x = tanhf((n * 0.8f + (ph - floorf(ph) < 0.5f ? 0.6f : -0.6f)) * 4.f) * expf(-s * 16.f); break;   /* grit */
            }
            (void)e;
            /* every transient ends at 0, faded over its last 3 ms */
            float tail = (float)(len - i) / 132.f;
            t[i] = x * (tail < 1.f ? tail : 1.f);
            if (fabsf(t[i]) > peak) peak = fabsf(t[i]);
        }
        for (int i = 0; i < len; i++) t[i] *= 0.9f / (peak > 1e-6f ? peak : 1.f);
    }
}
static void fdf_halfband(void)
{
    float sum = 0.f;
    for (int i = 0; i < FDF_HB; i++) {
        int m = i - FDF_HB / 2;
        float sinc = m ? (m % 2 ? sinf(1.5707963f * m) / (3.1415927f * m) : 0.f) : 0.5f;   /* (even offsets exactly 0) */
        float w = 0.42f - 0.5f * cosf(6.2831853f * i / (FDF_HB - 1)) + 0.08f * cosf(12.566371f * i / (FDF_HB - 1));
        FDF_HBK[i] = sinc * w;
        sum += FDF_HBK[i];
    }
    for (int i = 0; i < FDF_HB; i++) FDF_HBK[i] /= sum;
}
static void fdf_blend(void)
{
    for (int v = 0; v < 128; v++) {
        float pos = 32.f * v / 127.f;
        int w = (int)pos;
        float f = pos - (float)w;
        if (w >= FDF_NWAVE - 1) { w = FDF_NWAVE - 1; f = 0.f; }
        for (int i = 0; i <= FDF_WN; i++)
            FDF_POS[v][i] = FDF_WAVE[w][i] + (f > 0.f ? (FDF_WAVE[w + 1][i] - FDF_WAVE[w][i]) * f : 0.f);
    }
}
static void fdf_init(void)
{
    fdf_waves();
    fdf_blend();
    fdf_transients();
    fdf_halfband();
}

/* ------------------------------------------------------------------ cooking (producer thread) */
static float fdf_p(int v) { return (float)(v < 0 ? 0 : v > 127 ? 127 : v) / 127.f; }
static float fdf_curve(float p) { return 0.1f * p + 0.9f * p * p * p * p; }
/* a factor per sample (at rate fs) reaching e^-6 in N = 8 fs curve + 32 samples (at 48 kHz: 0.67 ms at 0) */
static float fdf_decay(int v, float fs) { return expf(-6.f / (8.f * fs * fdf_curve(fdf_p(v)) + 32.f * fs / 48000.f)); }
static float fdf_band(int v) { return 5.f * powf(8000.f, (float)(v < 0 ? 0 : v) / 127.f); }
static void fdf_cook(fdf_ck_t *c, const int16_t *s)
{
    const float fs2 = 2.f * FDF_FS;
    float p;
    memset(c, 0, sizeof *c);
    c->alg = (uint8_t)(s[FD_ALGO] < 0 ? 0 : s[FD_ALGO] > 6 ? 6 : s[FD_ALGO]);
    c->tune = (float)((s[FD_TUNE] < 0 ? 0 : s[FD_TUNE] > 127 ? 127 : s[FD_TUNE]) - 64);
    p = fdf_p(s[FD_SDEP]);
    c->sdep = 16.f * p * p;
    c->sfall = expf(-6.f / ((5.f * FDF_FS * fdf_curve(fdf_p(s[FD_STIM])) + 32.f) * 2.f));
    c->opc = (uint8_t)(s[FD_OPC] < 0 ? 0 : s[FD_OPC] > 127 ? 127 : s[FD_OPC]);
    c->opab = (uint8_t)(s[FD_OPAB] < 0 ? 0 : s[FD_OPAB] > 127 ? 127 : s[FD_OPAB]);
    p = fdf_p(s[FD_FDBK]);
    c->fb = p * p * 0.63661977f;
    c->abn = fdf_p(s[FD_ABN]);
    p = fdf_p(s[FD_FOLD]);
    c->fold = p > 0.f ? p * p + 0.07f : 0.f;
    c->fwet = fminf(16.f * p, 1.f);
    c->ra = roundf(1000.f * (0.001f + 31.999f * fdf_p(s[FD_RATA]))) / 1000.f;
    c->rb = roundf(1000.f * (0.001f + 31.999f * fdf_p(s[FD_RATB]))) / 1000.f;
    p = fdf_p(s[FD_MODA]); c->moda = p * p;
    p = fdf_p(s[FD_MODB]); c->modb = p * p;
    c->enda = fdf_p(s[FD_ENDA]);
    c->endb = fdf_p(s[FD_ENDB]);
    c->deca = fdf_decay(s[FD_DECA], fs2);
    c->decb = fdf_decay(s[FD_DECB], fs2);
    p = fdf_p(s[FD_HOLD]);
    c->bhold_inf = s[FD_HOLD] >= 127;
    c->bhold = (uint32_t)(fs2 * p * p);
    p = fdf_p(s[FD_NHLD]);
    c->nhold_inf = s[FD_NHLD] >= 127;
    c->nhold = (uint32_t)(FDF_FS * p * p);
    c->bdec = s[FD_DEC] >= 127 ? 1.f : fdf_decay(s[FD_DEC], fs2);
    c->ndec = s[FD_NDEC] >= 127 ? 1.f : fdf_decay(s[FD_NDEC], FDF_FS);
    p = fdf_p(s[FD_LEV]); c->blev = p * p;
    p = fdf_p(s[FD_TLEV]); c->tlev = p * p;
    p = fdf_p(s[FD_NLEV]); c->nlev = 2.f * p * p * p;
    c->phc = s[FD_PHC] >= 91 ? -1 : (s[FD_PHC] < 0 ? 0 : s[FD_PHC]);
    c->nrst = s[FD_NRST] != 0;
    c->nrm = s[FD_NRM] != 0;
    {
        float d = 0.992f - fdf_p(s[FD_GRAN]);
        c->gran = 0.0001f + (d > 0.f ? d * d : 0.f);
        c->gran_g = fminf(fmaxf(sqrtf(1.f / c->gran), 1.f), 8.f);
    }
    c->hp = expf(-6.2831853f * fdf_band(s[FD_NBAS]) / FDF_FS);
    c->lp = s[FD_NBAS] + s[FD_NWID] >= 127 ? 1.f : 1.f - expf(-6.2831853f * fdf_band(s[FD_NBAS] + s[FD_NWID]) / FDF_FS);
    c->has_noise = c->nlev > 0.f;
    c->has_tran = c->tlev > 0.f;
    p = 31.f * fdf_p(s[FD_TRAN]);
    c->tran0 = (int)p;
    c->tfrac = p - (float)c->tran0;
}

/* ------------------------------------------------------------------ the voice (audio thread) */
static void fdf_on(fdf_v_t *v, const fdf_ck_t *c, uint32_t seed, int fresh)
{
    if (fresh) memset(v, 0, sizeof *v);
    v->ph[FDF_A] = v->ph[FDF_B] = 0.f;
    if (c->phc >= 0) v->ph[FDF_C] = (float)c->phc / 360.f;
    v->sw = 1.f;
    v->ea = v->eb = 1.f;
    v->be = v->ne = 1.f;
    v->bh = c->bhold;
    v->nh = c->nhold;
    v->tn = 0;
    if (c->nrst || fresh) { v->rn = c->nrst ? 1234567890u : (seed | 1u); v->sr = c->nrst ? 0x5bu : (seed ^ 0xa5a5u) | 1u; }
    if (fresh) v->lev = c->blev;
}
static inline float fdf_wave(const float *t, float ph)
{
    float x = ph * FDF_WN;
    int i = (int)x;
    return t[i] + (t[i + 1] - t[i]) * (x - (float)i);
}
static inline float fdf_wrap(float x) { return x - floorf(x); }

/* ENG_BLK frames, Q15-scaled int32 (the Bigorna voice chain's format) */
static void fdf_render(fdf_v_t *v, const fdf_ck_t *c, int32_t note16, int32_t *out)
{
    const fdf_alg_t *g = &FDF_ALG[c->alg];
    const float fs2 = 2.f * FDF_FS;
    float note = (float)note16 / 65536.f + c->tune;
    if (note < 0.f) note = 0.f;
    if (note > 135.f) note = 135.f;
    const float f0 = 440.f * exp2f((note - 69.f) / 12.f);
    const float lev0 = v->lev, dlev = (c->blev - lev0) / (float)ENG_BLK;
    const float *tc = FDF_POS[c->opc], *tab = FDF_POS[c->opab];
    for (int i = 0; i < ENG_BLK; i++) {
        float y = 0.f, oc_fs = 0.f;
        /* the body gone (its envelope, and B's where B is heard, below -100 dB): the operators rest */
        if (v->be < 1e-5f && !(g->bout && fabsf(v->eb) > 1e-5f) && !c->nrm) {
            for (int z = 0; z < 2; z++) {
                v->hb[v->hbi] = v->hb[v->hbi + FDF_HB] = 0.f;
                if (++v->hbi == FDF_HB) v->hbi = 0;
            }
        } else
        for (int sub = 0; sub < 2; sub++) {
            float f = f0 * (1.f + c->sdep * v->sw), o[3] = {0.f, 0.f, 0.f}, lv[3];
            if (f > 0.499f * FDF_FS) f = 0.499f * FDF_FS;
            float inc[3] = {f / fs2, fminf(f * c->ra, 0.499f * fs2) / fs2, fminf(f * c->rb, 0.499f * fs2) / fs2};
            v->sw *= c->sfall;
            v->ea = c->enda + (v->ea - c->enda) * c->deca;
            v->eb = c->endb + (v->eb - c->endb) * c->decb;
            if (!c->bhold_inf) { if (v->bh) v->bh--; else v->be *= c->bdec; }
            lv[FDF_C] = 0.f;
            lv[FDF_A] = c->moda * v->ea;
            lv[FDF_B] = c->modb * v->eb;
            for (int k = 0; k < 3; k++) {
                int op = g->order[k];
                float ph = v->ph[op];
                for (int m = 0; m < 3; m++) if ((g->mod[op] >> m) & 1u) ph += o[m] * lv[m];
                if (((g->fb >> op) & 1u) && c->fb > 0.f) ph += 0.5f * (v->o1[op] + v->o2[op]) * c->fb;
                o[op] = fdf_wave(op == FDF_C ? tc : tab, fdf_wrap(ph));
                if (op != FDF_C && c->abn > 0.f) o[op] += (v->sh[op] - o[op]) * c->abn;
                v->o2[op] = v->o1[op];
                v->o1[op] = o[op];
                v->ph[op] += inc[op];
                if (v->ph[op] >= 1.f) {
                    v->ph[op] -= floorf(v->ph[op]);
                    if (op != FDF_C && c->abn > 0.f) v->sh[op] = fdf_bi(&v->sr);   /* a new held value each cycle */
                }
            }
            float body = o[FDF_C] * v->be;
            if (c->fold > 0.f) {
                float u = body * c->fold;
                u = u > 1.f ? 1.f : u < -1.f ? -1.f : u;
                float z = 4.f * u, z2 = z * z, th = z * (27.f + z2) / (27.f + 9.f * z2);   /* tanh, Pade */
                th = th > 1.f ? 1.f : th < -1.f ? -1.f : th;
                float fy = 1.12f * u - 0.6716f * th;
                body += (2.f * fy - body) * c->fwet;
            }
            if (g->bout) body = 0.5f * (body + o[FDF_B] * v->eb);
            /* the decimator: push the 2x sample; take the output on the second */
            v->hb[v->hbi] = v->hb[v->hbi + FDF_HB] = body;
            if (++v->hbi == FDF_HB) v->hbi = 0;
            if (sub == 0) oc_fs = o[FDF_C];
        }
        {   /* the half-band's centre tap and odd offsets only (the even ones are exactly 0) */
            const float *w = &v->hb[v->hbi];
            y = w[FDF_HB / 2] * FDF_HBK[FDF_HB / 2];
            for (int k = 0; k < FDF_HB; k += 2) y += w[k] * FDF_HBK[k];   /* (k even: odd offsets from the centre, 11) */
        }
        y *= lev0 + dlev * (float)i;
        if (c->has_noise) {
            float nz;
            if (c->gran >= 0.9999f || (float)(fdf_rnd(&v->rn) >> 8) * (1.f / 16777216.f) < c->gran) nz = fdf_bi(&v->rn);
            else nz = 0.f;
            if (c->gran < 0.9999f) nz *= c->gran_g;
            if (c->nrm) nz *= oc_fs;
            /* the band: one-pole high-pass at BASE, one-pole low-pass at BASE + WDTH */
            float hpo = c->hp * (v->hpz + nz - v->hpx);
            v->hpx = nz;
            v->hpz = hpo;
            v->lpz += (hpo - v->lpz) * c->lp;
            y += v->lpz * v->ne * c->nlev;
            if (!c->nhold_inf) { if (v->nh) v->nh--; else v->ne *= c->ndec; }
        }
        if (c->has_tran && v->tn < FDF_TMAX) {
            int a = c->tran0, b = a + 1 < FDF_NTRAN ? a + 1 : a;
            float ta = v->tn < FDF_TLEN[a] ? FDF_TRAN[a][v->tn] : 0.f, tb = v->tn < FDF_TLEN[b] ? FDF_TRAN[b][v->tn] : 0.f;
            y += (ta + (tb - ta) * c->tfrac) * c->tlev;
        }
        v->tn++;
        if (y > 3.f) y = 3.f;
        if (y < -3.f) y = -3.f;
        out[i] = (int32_t)(y * 32767.f);
    }
    v->lev = c->blev;
}
