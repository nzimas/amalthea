/* SPDX-License-Identifier: GPL-3.0-only
 * FUGITIVE: VAP, the Polyend "Virtual Analog" factory engine, ported to fixed point (docs/engines/VAP.md).
 *
 * One voice: two PolyBLEP oscillators (triangle -> saw -> pulse morph, hard sync, phase reset), pink noise, one of
 * 15 filters (a Stilson/Smith "MG" ladder, an OB-style zero-delay ladder in 6 taps, a non-linear SVF in 8 taps), an
 * amplifier with a one-pole gain smoother, three ADSR envelopes (amp, filter, aux: Pirkle's analog curves), two
 * LFOs and the original's 10-slot modulation matrix (4 fixed slots, 6 user slots), a note stack with legato and an
 * exponential glide. As the original: control work every 128 frames (4 renders), audio per sample.
 *
 * Numbers: audio inside the voice Q24 (the original's 1.0 = 1 << 24; the filters' states reach ~50), Q15 out.
 * Unit modulation values (sources, PW, mix, sustain, volume, shape, noise, decay seconds) Q24; frequencies (the
 * cutoff Hz Q12; the two pitch destinations and the glide source Hz Q16); phases uint32; coefficients Q30 / Q31.
 */
#include "eng_vap_tab.h"

enum {
    VAP_P_SH1, VAP_P_SH2, VAP_P_PW1, VAP_P_PW2, VAP_P_MIX, VAP_P_TUNE1, VAP_P_TUNE2, VAP_P_DET, VAP_P_FINE,
    VAP_P_RST1, VAP_P_RST2, VAP_P_TYPE, VAP_P_CUT, VAP_P_RES, VAP_P_ENVAMT,
    VAP_P_XA, VAP_P_XD, VAP_P_XS, VAP_P_XR,         /* 15 .. 18: the aux envelope (EDIT: ENVELOPE 2) */
    VAP_P_AA, VAP_P_AD, VAP_P_AS, VAP_P_AR,         /* 19 .. 22: the amp envelope */
    VAP_P_FA, VAP_P_FD, VAP_P_FS, VAP_P_FR,         /* 23 .. 26: the filter envelope (EDIT: ENVELOPE 1) */
    VAP_P_L1F, VAP_P_L1W, VAP_P_L1R, VAP_P_L1S, VAP_P_L2F, VAP_P_L2W, VAP_P_L2R, VAP_P_L2S,
    VAP_P_GMODE, VAP_P_GTIME, VAP_P_TRACK, VAP_P_MIXENV, VAP_P_VOL, VAP_P_SYNC, VAP_P_NOISE,
    VAP_P_SRC1,                                     /* 42 .. 59: source, destination, amount x 6 */
    VAP_NPAR = 60
};

#define VAP_Q16(x) ((int32_t)((x) * 65536))
static const eng_par_t VAP_PAR[VAP_NPAR] = {
    {"SHAPE 1", 0, 65536, 32768, 0, 0, 0, 22},                 /* 0: tri 0 .. saw 0.5 .. pulse 1 */
    {"SHAPE 2", 0, 65536, 32768, 0, 0, 0, 27},
    {"PW 1", -32768, 32768, 0, 0, 0, 0, 21},                   /* 2: -0.5 .. 0.5 */
    {"PW 2", -32768, 32768, 0, 0, 0, 0, 28},
    {"MIX", -65536, 65536, 0, 0, 0, 0, 20},                    /* 4: -1 (osc 1) .. 1 (osc 2) */
    {"TUNE 1", -2359296, 2359296, 0, 0, 1, 0, 25},             /* 5: -36 .. 36 st */
    {"TUNE 2", -2359296, 2359296, 0, 0, 0, 0, 30},
    {"DETUNE", 0, 6553600, 0, 0, 1, 0, 26},                    /* 7: 0 .. 100 cents (osc 1 up, osc 2 down) */
    {"FINETUNE", -6553600, 6553600, 0, 0, 0, 0, 31},           /* 8: -100 .. 100 cents */
    {"RESET 1", 0, 65536, 0, 0, 1, 0, 255},
    {"RESET 2", 0, 65536, 0, 0, 1, 0, 255},
    {"TYPE", 0, 917504, 0, 0, 1, 1, 255},                      /* 11: 15 filters */
    {"CUTOFF", 1310720, 1310720000, 26214400, 2, 0, 1, 74},    /* 12: 20 .. 20000 Hz (20000: off), 400 */
    {"RESONANCE", 0, 98304, 32768, 0, 0, 1, 71},               /* 13: 0 .. 1.5, 0.5 */
    {"ENV AMOUNT", -65536, 65536, 9175, 1, 0, 1, 77},          /* 14: -1 .. 1 (x 20 kHz), 0.14 */
    {"ATTACK", 0, 655360, 0, 1, 0, 4, 46},                     /* 15: aux envelope, 0 .. 10 s */
    {"DECAY", 0, 655360, 32768, 1, 0, 4, 47},
    {"SUSTAIN", 0, 65536, 65536, 0, 0, 4, 48},
    {"RELEASE", 0, 655360, 19661, 1, 0, 4, 49},
    {"ATTACK", 0, 655360, 0, 1, 0, 2, 75},                     /* 19: amp envelope */
    {"DECAY", 0, 655360, 32768, 1, 0, 2, 72},
    {"SUSTAIN", 0, 65536, 65536, 0, 0, 2, 76},
    {"RELEASE", 0, 655360, 32768, 1, 0, 2, 73},
    {"ATTACK", 0, 655360, 0, 1, 0, 3, 80},                     /* 23: filter envelope */
    {"DECAY", 0, 655360, 32768, 1, 0, 3, 81},
    {"SUSTAIN", 0, 65536, 0, 0, 0, 3, 82},
    {"RELEASE", 0, 655360, 19661, 1, 0, 3, 83},
    {"FREQUENCY", 0, 6553600, 655360, 2, 0, 5, 255},           /* 27: LFO 1, 0 .. 100 Hz (sync: a time base) */
    {"WAVEFORM", 0, 327680, 65536, 0, 1, 5, 255},
    {"RETRIGGER", 0, 131072, 131072, 0, 1, 5, 255},
    {"SYNC", 0, 65536, 0, 0, 1, 5, 255},
    {"FREQUENCY", 0, 6553600, 655360, 2, 0, 6, 255},           /* 31: LFO 2 */
    {"WAVEFORM", 0, 327680, 65536, 0, 1, 6, 255},
    {"RETRIGGER", 0, 131072, 131072, 0, 1, 6, 255},
    {"SYNC", 0, 65536, 0, 0, 1, 6, 255},
    {"GLIDE MODE", 0, 196608, 0, 0, 1, 8, 255},                /* 35 */
    {"GLIDE TIME", 0, 196608, 0, 1, 0, 8, 255},                /* 36: 0 .. 3 s */
    {"NOTE TRACK", -131072, 131072, 65536, 0, 0, 1, 255},      /* 37: -2 .. 2, 1 */
    {"MIX ENV AMT", -131072, 131072, 0, 0, 0, 8, 255},         /* 38: not on the original's pages (see notes) */
    {"VOLUME", 0, 131072, 65536, 0, 0, 8, 255},                /* 39: 0 .. 2 */
    {"SYNC", 0, 65536, 0, 0, 1, 0, 255},                       /* 40: osc 2 hard-synced to osc 1 */
    {"NOISE", 0, 65536, 0, 0, 0, 0, 23},
    {"SRC 1", 0, 262144, 0, 0, 1, 7, 255},                     /* 42 */
    {"DEST 1", 0, 851968, 0, 0, 1, 7, 255},
    {"AMT 1", -262144, 262144, 0, 3, 0, 7, 255},
    {"SRC 2", 0, 262144, 262144, 0, 1, 7, 255},
    {"DEST 2", 0, 851968, 65536, 0, 1, 7, 255},
    {"AMT 2", -262144, 262144, 0, 3, 0, 7, 255},
    {"SRC 3", 0, 262144, 196608, 0, 1, 7, 255},
    {"DEST 3", 0, 851968, 524288, 0, 1, 7, 255},
    {"AMT 3", -262144, 262144, 0, 3, 0, 7, 255},
    {"SRC 4", 0, 262144, 196608, 0, 1, 7, 255},
    {"DEST 4", 0, 851968, 589824, 0, 1, 7, 255},
    {"AMT 4", -262144, 262144, 0, 3, 0, 7, 255},
    {"SRC 5", 0, 262144, 196608, 0, 1, 7, 255},
    {"DEST 5", 0, 851968, 196608, 0, 1, 7, 255},
    {"AMT 5", -262144, 262144, 0, 3, 0, 7, 255},
    {"SRC 6", 0, 262144, 262144, 0, 1, 7, 255},
    {"DEST 6", 0, 851968, 655360, 0, 1, 7, 255},
    {"AMT 6", -262144, 262144, 0, 3, 0, 7, 255},
};

static const char *const VAP_SECTION[9] = {"OSCILLATORS", "FILTER", "AMP ENVELOPE", "ENVELOPE 1", "ENVELOPE 2",
                                           "LFO 1", "LFO 2", "MODULATION", "VOICE"};

/* the original's 6 named default macros (its 7th, unnamed: Volume x 0.66, is left out: ENG_NMACRO) */
static const eng_macro_t VAP_MACRO[6] = {
    {"TIMBRE", 0, 2, {{VAP_P_CUT, 6554}, {VAP_P_SH2, 9830}}},
    {"MOTION", 0, 2, {{VAP_P_L1F, 2294}, {50, 983}}},
    {"EXPERIMENT", 0, 4, {{VAP_P_CUT, 9830}, {VAP_P_RES, 6554}, {59, 4588}, {VAP_P_NOISE, 11141}}},
    {"FILTER CUTOFF", 0, 1, {{VAP_P_CUT, 32768}}},
    {"FILTER RESONANCE", 0, 1, {{VAP_P_RES, 32768}}},
    {"FILTER ENV AMOUNT", 0, 1, {{VAP_P_ENVAMT, 32768}}},
};

static const char *const VAP_E_ONOFF[] = {"OFF", "ON", 0};
static const char *const VAP_E_TYPE[] = {"LP MG24", "LP OB24", "LP OB12", "LP SV24", "LP SV12", "HP OB24", "HP OB12",
                                         "HP SV24", "HP SV12", "BP OB24", "BP OB12", "BP SV24", "BP SV12",
                                         "NOTCH SV24", "NOTCH SV12", 0};
static const char *const VAP_E_WAVE[] = {"TRIANGLE", "SIN", "RAMP UP", "RAMP DOWN", "SQUARE", "RND S&H", 0};
static const char *const VAP_E_RETRIG[] = {"NOTE", "ONE SHOT", "FREE RUNNING", 0};
static const char *const VAP_E_GLIDE[] = {"ALWAYS", "OVERLAP", "LEGATO", "LEGATO O.", 0};
static const char *const VAP_E_SRC[] = {"ENV AMP", "ENV FILTER", "ENV AUX", "LFO 1", "LFO 2", 0};
static const char *const VAP_E_DEST[] = {"OSC1 PW", "OSC2 PW", "OSC MIX", "FILTER CUTOFF", "ENV AMP S",
                                         "ENV FILTER S", "ENV AUX S", "VOLUME", "OSC1 PITCH", "OSC2 PITCH",
                                         "ENV FILTER D", "NOISE", "OSC1 SHAPE", "OSC2 SHAPE", 0};
static const char *const *const VAP_ENUMS[VAP_NPAR] = {
    [VAP_P_RST1] = VAP_E_ONOFF, [VAP_P_RST2] = VAP_E_ONOFF, [VAP_P_TYPE] = VAP_E_TYPE,
    [VAP_P_L1W] = VAP_E_WAVE, [VAP_P_L1R] = VAP_E_RETRIG, [VAP_P_L1S] = VAP_E_ONOFF,
    [VAP_P_L2W] = VAP_E_WAVE, [VAP_P_L2R] = VAP_E_RETRIG, [VAP_P_L2S] = VAP_E_ONOFF,
    [VAP_P_GMODE] = VAP_E_GLIDE, [VAP_P_SYNC] = VAP_E_ONOFF,
    [42] = VAP_E_SRC, [43] = VAP_E_DEST, [45] = VAP_E_SRC, [46] = VAP_E_DEST, [48] = VAP_E_SRC, [49] = VAP_E_DEST,
    [51] = VAP_E_SRC, [52] = VAP_E_DEST, [54] = VAP_E_SRC, [55] = VAP_E_DEST, [57] = VAP_E_SRC, [58] = VAP_E_DEST,
};

/* ------------------------------------------------------------------------------------------- numbers --- */
#define VAP_ONE24 (1 << 24)
#define VAP_ONE30 (1 << 30)
#define VAP_N 128                        /* the original's block */
/* the envelopes' curves (PolyDSP EnvelopeADSR(-1.5, -5)): e^-1.5, ln((1 + tco) / tco), e^-5, ..; float32 */
#define VAP_ATCO30 239584192             /* 0.22313017 */
#define VAP_ALOG28 456719648u            /* 1.7014133 */
#define VAP_DTCO30 7234816               /* 0.0067379470 */
#define VAP_DLOG28 1343979904u           /* 5.0067153 */
#define VAP_GLOG28 1879292737u           /* the glide's 7.000911 */
/* the modulation destinations, the sources (the original's ids) */
enum { VAP_D_PW1, VAP_D_PW2, VAP_D_MIX, VAP_D_CUT, VAP_D_SA, VAP_D_SF, VAP_D_SX, VAP_D_VOL, VAP_D_P1, VAP_D_P2,
       VAP_D_DF, VAP_D_NOISE, VAP_D_SH1, VAP_D_SH2, VAP_ND };
enum { VAP_S_EA, VAP_S_EF, VAP_S_EX, VAP_S_L1, VAP_S_L2, VAP_S_GLIDE, VAP_NS };
#define VAP_HZ_DEST ((1u << VAP_D_CUT) | (1u << VAP_D_P1) | (1u << VAP_D_P2))   /* Hz: cutoff Q12, pitches Q16 */
/* the user slots' destination ranges (hi - lo), x 0.5 = the multiplier of an amount 1 */
static const uint8_t VAP_DRANGE2[VAP_ND] = {1, 1, 2, 0, 1, 1, 1, 1, 4, 4, 1, 1, 1, 1};   /* (cutoff: 20000) */

/* the tables read every sample, in RAM (POOL) on the target */
static POOL uint32_t vap_padet[1025];
static POOL uint16_t vap_padeq[385], vap_padem[961];
#define VAP_TAB_T vap_padet
static POOL int32_t vap_obg[513], vap_svg[513], vap_svk[513];
static POOL uint32_t vap_rcp[514];

/* (FUGITIVE build check: +-VAP_GEPS32 selected as a constant reads as a ROM address to tools/build.py's check;
 * loaded from RAM, set here) */
static int64_t vap_geps;
static int64_t vap_opq(int64_t x) { volatile int64_t q = x; return q; }   /* (a value the compiler cannot know) */
static void vap_init(void)
{
    uint32_t i;
    vap_geps = vap_opq(VAP_GEPS32);
    for (i = 0; i < 1025; i++)
        vap_padet[i] = VAP_PADE_T[i];
    for (i = 0; i < 385; i++)
        vap_padeq[i] = VAP_PADE_Q[i];
    for (i = 0; i < 961; i++)
        vap_padem[i] = VAP_PADE_M[i];
    for (i = 0; i < 514; i++)
        vap_rcp[i] = VAP_RCP[i];
    for (i = 0; i < 513; i++)
        vap_obg[i] = VAP_OBG[i], vap_svg[i] = VAP_SVG[i], vap_svk[i] = VAP_SVK[i];
}

/* r ~ 2^62 / m, m normalised (2^31 .. 2^32 - 1): the table (~20 bits), and a Newton step (~31 bits) */
AINL uint32_t vap_rcpf(uint32_t m)
{
    uint32_t k = (m >> 22) - 512u, f = (m >> 6) & 0xFFFFu, a = vap_rcp[k], b = vap_rcp[k + 1u];
    return a - (uint32_t)(((a - b) * (uint64_t)f) >> 16);
}
AINL uint32_t vap_rcpm(uint32_t m)
{
    uint32_t r = vap_rcpf(m);
    int64_t e = (int64_t)((1ull << 62) - (uint64_t)m * r);
    return r + (uint32_t)(int32_t)(((e >> 31) * (int64_t)r) >> 31);
}
/* 1 / y as Q30, y >= 1.0 given in Q(qy) */
AINL int32_t vap_inv30(uint32_t y, int qy)
{
    int z = __builtin_clz(y);
    return (int32_t)(vap_rcpm(y << z) >> (32 - qy - z));
}
/* the original's saturator u (u^2 + 27) / (9 u^2 + 27): u - u^3 t(u) below 2, u (1/9 + q(u)) above (tables:
 * no division per sample). Near 0, the correction u^3 t(u), Q30 (32-bit products) */
AINL int32_t vap_pade_c30(int32_t u30)
{
    uint32_t a = (uint32_t)(u30 < 0 ? -u30 : u30), k = a >> 21, f = (a >> 9) & 0xFFFu;
    uint32_t t = VAP_TAB_T[k] - (((VAP_TAB_T[k] - VAP_TAB_T[k + 1u]) * f) >> 12);   /* Q31 */
    uint32_t w = (uint32_t)(((uint64_t)a * a) >> 30);                               /* u^2, Q30 */
    int32_t ut = (int32_t)(((int64_t)u30 * (int32_t)t) >> 31);                      /* u t, Q30 */
    return (int32_t)(((int64_t)ut * w + (1 << 29)) >> 30);                          /* (two products deep) */
}
/* u Q24 (|u| < 128) */
AINL int32_t vap_pade(int32_t u)
{
    uint32_t a = (uint32_t)(u < 0 ? -u : u), k, f, q;
    if (a < (2u << 24))
        return u - ((vap_pade_c30(u << 6) + 32) >> 6);
    if (a < (8u << 24)) {
        a -= 2u << 24;
        k = a >> 18;                                 /* 1/64 steps */
        f = (a >> 2) & 0xFFFFu;
        q = (vap_padeq[k] - (uint32_t)(((int32_t)(vap_padeq[k] - vap_padeq[k + 1u]) * (int32_t)f) >> 16)) << 14;
    } else {
        a -= 8u << 24;
        k = a >> 21;                                 /* 1/8 steps */
        f = (a >> 5) & 0xFFFFu;
        q = (vap_padem[k] - (uint32_t)(((int32_t)(vap_padem[k] - vap_padem[k + 1u]) * (int32_t)f) >> 16)) << 11;
    }
    return (int32_t)(((int64_t)u * (238609294u + q) + (1u << 30)) >> 31);   /* 1/9 + q, Q31 */
}

AINL int32_t vap_mul30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 30); }
AINL int32_t vap_mulr30(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 29)) >> 30); }
AINL int32_t vap_mul24(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 24); }

/* e^-y as (coefficient Q31, 1 - coefficient Q31), y = l / (44100 t): l Q28, t seconds Q16; 0 for t <= 0 and
 * y >= 87.33 as the original's fast expf */
static void vap_coef(uint32_t l28, int32_t t16, uint32_t *coef, uint32_t *om)
{
    uint32_t den, y, n;
    uint64_t num, term, s;
    int k;
    *coef = 0;
    *om = 1u << 31;
    if (t16 <= 0)
        return;
    den = (uint32_t)(((uint64_t)(uint32_t)t16 * 44100u) >> 4);    /* 44100 t, Q12 */
    if (!den)
        return;
    num = (uint64_t)l28 << 16;
    if ((uint32_t)(num >> 32) < den) {               /* y < 1: y Q32, the series of 1 - e^-y */
        y = udiv64_32((uint32_t)(num >> 32), (uint32_t)num, den);
        term = y;
        s = 0;
        for (k = 1; k < 14 && term; k++) {
            s = (k & 1) ? s + term : s - term;
            term = (uint32_t)((term * y) >> 32) / (uint32_t)(k + 1);   /* (< 2^32: a 32-bit division) */
        }
        *om = (uint32_t)((s + 1u) >> 1);
        *coef = (1u << 31) - *om;
        return;
    }
    y = l28 / den;                                   /* y Q16 */
    n = y >> 16;
    if (n >= 22u)
        return;
    term = (uint64_t)(y & 0xFFFFu) << 16;            /* e^-frac, Q32 */
    s = 1ull << 32;
    {
        uint64_t t = 1ull << 32;
        for (k = 1; k < 12 && t; k++) {
            t = (uint32_t)((t * term) >> 32) / (uint32_t)k;
            s = (k & 1) ? s - t : s + t;
        }
    }
    *coef = (uint32_t)((s * VAP_EXPN[n]) >> 32);
    *om = (1u << 31) - *coef;
}

/* 2^(c / 1200), c in cents, Q30 (the oscillators' frequency multipliers; -1512 st and below: 0, as the original) */
static uint32_t vap_cents(int32_t c)
{
    int32_t o = 0;
    uint32_t m;
    if (c < -151200)
        return 0;
    while (c < 0)
        c += 1200, o--;
    while (c >= 1200)
        c -= 1200, o++;
    m = (uint32_t)(((uint64_t)VAP_SEMI[c / 100] * VAP_CENT[c % 100]) >> 30);
    if (o >= 0)
        return o > 1 ? 0xFFFFFFFFu : m << o;          /* (+-38 st at most: < 8 x) */
    return o < -31 ? 0 : m >> -o;
}
/* the original's round-half-away of a Q16 value, to an integer */
static int32_t vap_round(int32_t v) { return v < 0 ? -((-v + 32768) >> 16) : (v + 32768) >> 16; }

/* frac(p + inc) as the original's float32 phase accumulator rounds it (to nearest even, 24 bits): the rounding
 * of each step is a constant bias within a binade, a frequency offset of up to ~1e-5 that a long note shows */
AINL uint32_t vap_padd(uint32_t p, uint32_t inc)
{
    uint32_t s = p + inc, r, e;
    if (s < p)                                       /* 1 <= p + inc < 2: 2^-23 */
        e = 9;
    else {
        int z = __builtin_clz(s | 1u);
        if (z >= 8)
            return s;
        e = 8u - (uint32_t)z;
    }
    r = s & ((1u << e) - 1u);
    s -= r;
    if (r > (1u << (e - 1u)) || (r == (1u << (e - 1u)) && ((s >> e) & 1u)))
        s += 1u << e;
    return s;
}

/* a phase increment (Q32) rounded as a float32 (24 bits, to nearest even) */
AINL uint32_t vap_r24(uint32_t inc)
{
    uint32_t r, e;
    int z = __builtin_clz(inc | 1u);
    if (z >= 8)
        return inc;
    e = 8u - (uint32_t)z;
    r = inc & ((1u << e) - 1u);
    inc -= r;
    if (r > (1u << (e - 1u)) || (r == (1u << (e - 1u)) && ((inc >> e) & 1u)))
        inc += 1u << e;
    return inc;
}
/* the phase increment of a frequency (Hz Q16): the original's float32 f / fs */
AINL uint32_t vap_finc(uint32_t f16) { return vap_r24((uint32_t)(((uint64_t)f16 * VAP_KLFO) >> 31)); }

/* ------------------------------------------------------------------------------------------- cooked --- */
typedef struct {
    uint32_t acoef, aoff, dcoef, dom, rcoef, roff;   /* Q31, Q30 (aoff), Q31, Q31, Q31, -Q30 (roff) */
    uint8_t apos, dpos, rpos;
} vap_envc_t;

typedef struct {
    uint8_t src, dst, op;                            /* op 0 add, 1 bipolar multiply (volume), 2 ratio (pitch) */
    int32_t mult;                                    /* in the destination's format (add), Q24 (1, 2) */
} vap_mod_t;

typedef struct {
    int32_t base[VAP_ND];                            /* the destinations' base values */
    vap_mod_t mod[10];
    int32_t m7_38, p38, p53;                         /* slot 4's multiplier from Mix Env Amt; the two values */
    vap_envc_t env[3];                               /* amp, filter, aux (the sources' order) */
    uint32_t m1, m2;                                 /* the oscillators' frequency multipliers, Q30 */
    uint8_t sync, rst1, rst2, ftype, gmode;
    uint8_t lwave[2], lmode[2], lsync[2], lpulse[2];
    uint32_t linc[2];                                /* the LFOs' phase increments (unsynced) */
    uint32_t gcoef;                                  /* glide, Q31 (float32-exact) */
    int32_t gtime;                                   /* glide time, Q16 */
    int32_t track;                                   /* note tracking, Q16 */
    int32_t mg_k4r;                                  /* MG: 4 res, Q24 */
    int32_t mg_gain;                                 /* MG: 10^(res 14 / 20), Q24 */
    int32_t ob_k;                                    /* OB: 6 res, Q24 */
    int32_t sv_rd, sv_a, sv_hi;                      /* SV: 1 / (r^3 c1 + 0.54) Q30, r^3 c1 c2 - 1 Q30, r^3 > thr */
    uint8_t bypass;                                  /* cutoff 20000: no filter */
} vap_ck_t;

/* a time parameter (Q16 seconds) at least m */
static int32_t vap_tmax(int32_t t, int32_t m) { return t > m ? t : m; }

static void vap_envc(vap_envc_t *e, int32_t a, int32_t d, int32_t r)
{
    uint32_t om;
    vap_coef(VAP_ALOG28, a, &e->acoef, &om);
    e->aoff = (uint32_t)(((uint64_t)(VAP_ONE30 + VAP_ATCO30) * om) >> 31);
    vap_coef(VAP_DLOG28, d, &e->dcoef, &e->dom);
    vap_coef(VAP_DLOG28, r, &e->rcoef, &om);
    /* (the original's release offset is (decay coefficient - 1) tco: the decay's, not the release's) */
    e->roff = (uint32_t)(((uint64_t)VAP_DTCO30 * e->dom) >> 31);
    e->apos = a > 0, e->dpos = d > 0, e->rpos = r > 0;
}

/* user slot multiplier: (hi - lo) amount / 2 in the destination's format; op by destination */
static int32_t vap_slot_mult(int d, int32_t amt16)
{
    if (d == VAP_D_CUT)
        return amt16 * 625;                          /* 10000 Hz an amount 1, Hz Q12 */
    return amt16 * 128 * VAP_DRANGE2[d];             /* range / 2, Q24 */
}
static uint8_t vap_slot_op(int d) { return d == VAP_D_VOL ? 1 : (d == VAP_D_P1 || d == VAP_D_P2) ? 2 : 0; }

static void vap_cook(void *ckp, const int32_t *v)
{
    vap_ck_t *c = ckp;
    int i, l;
    int32_t t;
    uint32_t om;
    /* destinations: PW, mix, cutoff, sustains, decay, noise, shapes */
    c->base[VAP_D_PW1] = v[VAP_P_PW1] << 8;
    c->base[VAP_D_PW2] = v[VAP_P_PW2] << 8;
    c->base[VAP_D_MIX] = v[VAP_P_MIX] << 8;
    c->base[VAP_D_CUT] = v[VAP_P_CUT] >> 4;          /* Hz Q12 */
    c->base[VAP_D_SA] = v[VAP_P_AS] << 8;
    c->base[VAP_D_SF] = v[VAP_P_FS] << 8;
    c->base[VAP_D_SX] = v[VAP_P_XS] << 8;
    c->base[VAP_D_VOL] = 0;
    c->base[VAP_D_P1] = c->base[VAP_D_P2] = 0;
    c->base[VAP_D_DF] = v[VAP_P_FD] << 8;
    c->base[VAP_D_NOISE] = v[VAP_P_NOISE] << 8;
    c->base[VAP_D_SH1] = v[VAP_P_SH1] << 8;
    c->base[VAP_D_SH2] = v[VAP_P_SH2] << 8;
    c->bypass = v[VAP_P_CUT] >= VAP_Q16(20000);
    /* the fixed slots: amp env -> volume (x Volume), filter env -> cutoff (x 20000 Env Amount), glide -> pitches */
    c->mod[0] = (vap_mod_t){VAP_S_EA, VAP_D_VOL, 0, v[VAP_P_VOL] << 8};
    c->mod[1] = (vap_mod_t){VAP_S_EF, VAP_D_CUT, 0, v[VAP_P_ENVAMT] * 1250};   /* Hz Q12 */
    c->mod[2] = (vap_mod_t){VAP_S_GLIDE, VAP_D_P1, 0, 1 << 24};
    c->mod[3] = (vap_mod_t){VAP_S_GLIDE, VAP_D_P2, 0, 1 << 24};
    for (i = 0; i < 6; i++) {
        const int32_t *s = &v[VAP_P_SRC1 + 3 * i];
        int d = clampi(s[1] >> 16, 0, VAP_ND - 1);
        vap_mod_t *m = &c->mod[4 + i];
        m->src = (uint8_t)clampi(s[0] >> 16, 0, 4);
        m->dst = (uint8_t)d;
        m->op = vap_slot_op(d);
        m->mult = vap_slot_mult(d, s[2]);
    }
    /* Mix Env Amt writes slot 4's multiplier (the original's: modulation 7, Amt 4's): the later one counts */
    c->p38 = v[VAP_P_MIXENV];
    c->p53 = v[53];
    c->m7_38 = c->mod[7].dst == VAP_D_CUT ? v[VAP_P_MIXENV] >> 4 : v[VAP_P_MIXENV] << 8;
    /* envelopes: amp, filter (attack and release at least 2 ms; its decay is a destination), aux */
    vap_envc(&c->env[0], v[VAP_P_AA], v[VAP_P_AD], v[VAP_P_AR]);
    vap_envc(&c->env[1], vap_tmax(v[VAP_P_FA], 131), v[VAP_P_FD], vap_tmax(v[VAP_P_FR], 131));
    vap_envc(&c->env[2], v[VAP_P_XA], v[VAP_P_XD], v[VAP_P_XR]);
    /* oscillators: 2^((tune + (+-detune + fine) / 100) / 12), cents rounded half away */
    {
        int32_t det = vap_round(v[VAP_P_DET]), fine = vap_round(v[VAP_P_FINE]);
        c->m1 = vap_cents(vap_round(v[VAP_P_TUNE1]) * 100 + det + fine);
        c->m2 = vap_cents(vap_round(v[VAP_P_TUNE2]) * 100 - det + fine);
    }
    c->sync = v[VAP_P_SYNC] >= 65536 || v[VAP_P_SYNC] <= -65536;
    c->rst1 = v[VAP_P_RST1] != 0;
    c->rst2 = v[VAP_P_RST2] != 0;
    /* LFOs */
    for (l = 0; l < 2; l++) {
        const int32_t *s = &v[VAP_P_L1F + 4 * l];
        int32_t ix = s[0] >> 16;
        c->lwave[l] = (uint8_t)clampi(s[1] >> 16, 0, 5);
        c->lmode[l] = (uint8_t)clampi(s[2] >> 16, 0, 2);
        c->lsync[l] = s[3] > 0;
        c->lpulse[l] = (uint8_t)(ix < 0 ? 0 : ix > 14 ? 15 : ix);
        t = s[0] > 0 ? s[0] : 0;
        c->linc[l] = vap_finc((uint32_t)t);
    }
    /* glide, note track */
    c->gmode = (uint8_t)clampi(v[VAP_P_GMODE] >> 16, 0, 3);
    c->gtime = v[VAP_P_GTIME];
    vap_coef(VAP_GLOG28, c->gtime, &c->gcoef, &om);
    {
        /* as a float32 (24 bits) */
        uint32_t g = c->gcoef, k, r;
        int msb = 31 - __builtin_clz(g | 1u);
        if (msb >= 24) {
            k = (uint32_t)(msb - 23);
            r = g & ((1u << k) - 1u);
            g -= r;
            if (r > (1u << (k - 1u)) || (r == (1u << (k - 1u)) && ((g >> k) & 1u)))
                g += 1u << k;
        }
        c->gcoef = g;
    }
    c->track = v[VAP_P_TRACK];
    /* filter */
    c->ftype = (uint8_t)clampi(v[VAP_P_TYPE] >> 16, 0, 14);
    c->mg_k4r = v[VAP_P_RES] << 10;                  /* 4 res, Q24 */
    c->ob_k = v[VAP_P_RES] * 6 * 256;                /* 6 res, Q24 */
    {
        /* 10^(res 14 / 20) = 2^(res 2.3253497) (the original's fast exp of res x 14 dB) */
        int32_t x = (int32_t)(((int64_t)v[VAP_P_RES] * 152394) >> 16);
        c->mg_gain = (int32_t)(exp2_q16(x) << 8);
    }
    {
        /* SV: r^3 = clamp(res, 0, 1.5)^3; 12 dB: c1 7.247407, c2 0.00020441537, thr 3.361202; 24 dB: 1.9140741,
         * 0.00077399379, 3.3227553 (float32). Rd = 1 / (r^3 c1 + 0.54), a = r^3 c1 c2 - 1 */
        int is24 = c->ftype == 3 || c->ftype == 7 || c->ftype == 11 || c->ftype == 13;
        uint32_t r = (uint32_t)clampi(v[VAP_P_RES], 0, 98304);
        uint32_t r3 = (uint32_t)((((uint64_t)r * r) * r) >> 20);              /* Q28 */
        uint32_t rc = (uint32_t)(((uint64_t)r3 * (is24 ? 513805344u : 1945460992u)) >> 30);   /* r^3 c1, Q26 */
        c->sv_rd = (int32_t)udiv64_32(1u << 24, 0, rc + 36238788u);          /* 0.54 Q26; Rd Q30 */
        c->sv_a = (int32_t)(((uint64_t)rc * (is24 ? 851015168u : 224757072u)) >> 36) - VAP_ONE30;   /* c2 Q40 */
        c->sv_hi = r3 > (is24 ? 891945344u : 902265792u);
    }
}

/* ------------------------------------------------------------------------------------------- voice --- */
typedef struct {
    int32_t out;                                     /* Q30 */
    int32_t doff;                                    /* decay offset (sustain - tco)(1 - coef), Q30 */
    uint8_t st;                                      /* 0 off, 1 attack, 2 decay, 3 sustain, 4 release */
} vap_env_t;

typedef struct {
    uint32_t ph, x1, x2;
    int32_t rsh, last;                               /* Q24 */
    uint8_t done;
} vap_lfo_t;

typedef struct {
    /* the note stack */
    uint8_t notes[8], nn;
    uint8_t has_f;
    int32_t lastf;                                   /* the last note's frequency, Hz Q16 */
    int32_t notef;                                   /* the filter's note frequency, Hz Q16 (0: none yet) */
    /* glide: the original's float32 ramp, emulated exactly (Hz Q32) */
    uint8_t gl_on;
    int64_t gl_x, gl_to, gl_off, lastf32;
    int32_t gl_dir;
    uint32_t gl_coef;                                /* Q31, float32-exact */
    /* oscillators, noise */
    uint32_t p1, p2;
    uint32_t nx1, nx2;
    int32_t pk0, pk1, pk2;                           /* pink filter, Q24 */
    vap_env_t env[3];
    vap_lfo_t lfo[2];
    /* filter */
    uint8_t ftype;
    int64_t mg1, mg2, mg3, mgx, mg4;                 /* MG: y1 Q56, y2 Q52, y3 y4 Q48 (after the saturator), x0 Q56 */
    int32_t s1, s2, s3, s4, f1, f2, f3, f4;          /* OB: integrators, feedback taps; SV: s1 .. s4 */
    /* the modulation matrix's base values: previous, latched, ramping */
    int32_t prev[VAP_ND], from[VAP_ND], to[VAP_ND];
    uint16_t ramp;                                   /* destinations ramping this block */
    uint8_t skip, q;                                 /* no smoothing next block; the render in the block */
    uint8_t w38;                                     /* slot 4's multiplier is Mix Env Amt's */
    int32_t l38, l53;
    int32_t amp;                                     /* the amplifier's smoothed gain, Q28 */
    /* this render's buffers */
    int32_t sb[VAP_NS][ENG_BLK];                     /* sources */
    int32_t db[VAP_ND][ENG_BLK];                     /* destinations */
    int32_t ab[ENG_BLK];                             /* audio, Q24 */
} vap_v_t;

static void vap_reset(void *vp)
{
    vap_v_t *v = vp;
    memset(v, 0, sizeof *v);
    v->nx1 = v->lfo[0].x1 = v->lfo[1].x1 = 0x12345678u;
    v->nx2 = v->lfo[0].x2 = v->lfo[1].x2 = 0xdeadbeedu;
    v->skip = 1;
    v->ftype = 0xFF;
}

/* the LFOs' random value (PolyDSP WhiteNoise): x1 ^= x2, out x2, x2 += x1 */
AINL int32_t vap_white(uint32_t *x1, uint32_t *x2)
{
    uint32_t o = *x2;
    *x1 ^= *x2;
    *x2 += *x1;
    return (int32_t)o;
}

/* a positive Hz Q32 value (< 2^22 Hz) rounded to float32 (24 bits, to nearest even): 32-bit shifts only */
static int64_t vap_f32(int64_t y)
{
    uint32_t hi = (uint32_t)((uint64_t)y >> 32), lo = (uint32_t)y, k, r, h;
    int msb = hi ? 63 - __builtin_clz(hi) : 31 - __builtin_clz(lo | 1u);
    if (msb < 24)
        return y;
    k = (uint32_t)(msb - 23);                        /* 1 .. 30 */
    r = lo & ((1u << k) - 1u);
    lo -= r;
    h = 1u << (k - 1u);
    if (r > h || (r == h && ((lo >> k) & 1u))) {
        uint32_t o = lo;
        lo += 1u << k;
        hi += lo < o;
    }
    return (int64_t)(((uint64_t)hi << 32) | lo);
}
/* a Hz Q32 value times a Q31 coefficient (0 .. 1), Q32 */
AINL int64_t vap_mq31(int64_t x, uint32_t c)
{
    return (int64_t)((((uint64_t)x >> 32) * c) << 1) + (int64_t)(((uint64_t)(uint32_t)x * c) >> 31);
}

/* the glide (SynthBase's Ramp, float32): x = fma(x, coef, off), off = (to +- 0.000911882) (1 - coef) */
static void vap_glide_start(vap_v_t *v, const vap_ck_t *c, int64_t to)
{
    int64_t from = v->lastf32, te = vap_f32(to > from ? to + vap_geps : to - vap_geps);
    v->gl_x = from;
    v->gl_to = to;
    v->gl_dir = to > from ? 1 : to < from ? -1 : 0;
    v->gl_coef = c->gcoef;
    v->gl_off = vap_f32(vap_mq31(te, (1u << 31) - c->gcoef));
    v->gl_on = 1;
}

/* a note starts or comes back: envelopes from where they are, LFOs (Note / One Shot: from 0), phases */
static void vap_retrigger(vap_v_t *v, const vap_ck_t *c)
{
    int i;
    if (c->rst1)
        v->p1 = 0;
    if (c->rst2)
        v->p2 = 0;
    for (i = 0; i < 3; i++)
        v->env[i].st = 1;
    for (i = 0; i < 2; i++) {
        vap_lfo_t *l = &v->lfo[i];
        if (c->lmode[i] != 2)
            l->ph = 0;
        l->done = 0;
        if (c->lwave[i] == 5)
            l->rsh = vap_white(&l->x1, &l->x2) >> 7;
    }
    v->skip = 1;
}

static void vap_on(void *vp, const void *ckp, int32_t note16, int vel)
{
    vap_v_t *v = vp;
    const vap_ck_t *c = ckp;
    int n = clampi(note16 >> 16, 0, 127), i;
    int32_t f = VAP_NOTE[n];
    (void)vel;                                       /* (the original's voice ignores it) */
    if ((c->gmode & 1) && v->nn == 0)
        v->gl_on = 0;
    else if (c->gtime <= 0 || !v->has_f)
        v->gl_on = 0;
    else
        vap_glide_start(v, c, (int64_t)VAP_NOTE32[n]);
    if (v->nn == 8) {                                /* (full: the oldest goes) */
        for (i = 0; i < 7; i++)
            v->notes[i] = v->notes[i + 1];
        v->nn = 7;
    }
    v->notes[v->nn++] = (uint8_t)n;
    v->lastf = f;
    v->lastf32 = (int64_t)VAP_NOTE32[n];
    v->has_f = 1;
    v->notef = f;
    if (c->gmode >= 2 && v->nn >= 2)
        return;                                      /* legato */
    vap_retrigger(v, c);
}

static void vap_off(void *vp, const void *ckp, int32_t note16)
{
    vap_v_t *v = vp;
    const vap_ck_t *c = ckp;
    int n = note16 >> 16, i;
    if (v->nn > 1 && v->notes[v->nn - 1] == n && v->notes[v->nn - 2] != n) {
        int32_t f;
        v->nn--;
        f = VAP_NOTE[v->notes[v->nn - 1]];
        if (c->gtime <= 0 || !v->has_f)
            v->gl_on = 0;
        else
            vap_glide_start(v, c, (int64_t)VAP_NOTE32[v->notes[v->nn - 1]]);
        v->lastf = f;
        v->lastf32 = (int64_t)VAP_NOTE32[v->notes[v->nn - 1]];
        v->has_f = 1;
        v->notef = f;
        if (c->gmode < 2)
            vap_retrigger(v, c);
        return;
    }
    for (i = 0; i < v->nn; i++)
        if (v->notes[i] == n) {
            for (v->nn--; i < v->nn; i++)
                v->notes[i] = v->notes[i + 1];
            break;
        }
    if (v->nn == 0) {
        for (i = 0; i < 3; i++)
            v->env[i].st = v->env[i].out > 0 ? 4 : 0;
        return;
    }
    v->notef = VAP_NOTE[v->notes[v->nn - 1]];
}

/* ------------------------------------------------------------------------------------------- render --- */
/* one envelope over the render: sus the sustain (Q24, a buffer when svar, else sus[0]); out Q24 into o; 0 when it is
 * constant (o[0]: off, or sustaining a constant sustain) */
static int vap_env_run(vap_env_t *e, const vap_envc_t *c, const int32_t *sus, int svar, int32_t *o)
{
    int32_t x = e->out;
    int i = 0;
    if (e->st == 0 || (e->st == 3 && !svar)) {
        x = e->st ? sus[0] << 6 : 0;                 /* (off: the original's start level, 0) */
        e->out = x;
        o[0] = x >> 6;
        return 0;
    }
    while (i < ENG_BLK) {
        switch (e->st) {
        case 1:
            for (; i < ENG_BLK; i++) {
                x = (int32_t)c->aoff + (int32_t)(((int64_t)c->acoef * x) >> 31);
                if (x >= VAP_ONE30 || !c->apos) {
                    x = VAP_ONE30, e->st = 2, o[i++] = x >> 6;
                    break;
                }
                o[i] = x >> 6;
            }
            break;
        case 2:
            for (; i < ENG_BLK; i++) {
                int32_t s = sus[svar ? i : 0] << 6;
                x = e->doff + (int32_t)(((int64_t)c->dcoef * x) >> 31);
                if (x <= s || !c->dpos) {
                    x = s, e->st = 3, o[i++] = x >> 6;
                    break;
                }
                o[i] = x >> 6;
            }
            break;
        case 3:
            for (; i < ENG_BLK; i++)
                o[i] = sus[svar ? i : 0];
            x = sus[svar ? ENG_BLK - 1 : 0] << 6;
            break;
        case 4:
            for (; i < ENG_BLK; i++) {
                x = (int32_t)(((int64_t)c->rcoef * x) >> 31) - (int32_t)c->roff;
                if (x <= 0 || !c->rpos) {
                    x = 0, e->st = 0, o[i++] = 0;
                    break;
                }
                o[i] = x >> 6;
            }
            break;
        default:
            for (; i < ENG_BLK; i++)
                o[i] = 0;
            x = 0;
            break;
        }
    }
    e->out = x;
    return 1;
}

/* the LFO's wave at phase p, Q24 */
static int32_t vap_lfo_wave(const vap_lfo_t *l, int w)
{
    uint32_t p = l->ph;
    int32_t x, y, a;
    switch (w) {
    case 0:
        x = (int32_t)(p >> 7) - VAP_ONE24;
        return VAP_ONE24 - 2 * (x < 0 ? -x : x);
    case 1:                                          /* the original's parabolic sine of pi - 2 pi p, Q28 */
        x = 843314857 - (int32_t)(((uint64_t)p * 1686629713u) >> 32);
        a = x < 0 ? -x : x;
        y = (int32_t)(((int64_t)x * (341782626 - (int32_t)(((int64_t)108792783 * a) >> 28))) >> 28);
        a = y < 0 ? -y : y;
        y += (int32_t)(((((int64_t)y * (a - (1 << 28))) >> 28) * 60397978) >> 28);
        return y >> 4;
    case 2:
        return (int32_t)(p >> 7) - VAP_ONE24;
    case 3:
        return VAP_ONE24 - (int32_t)(p >> 7);
    case 4:
        return p > 0x80000000u ? -VAP_ONE24 : VAP_ONE24;
    default:
        return l->rsh;
    }
}

/* one LFO over the render (o NULL: not a used source, the phase only: as the original, 128 steps in one float
 * add a block) */
static void vap_lfo_run(vap_lfo_t *l, int w, int mode, uint32_t inc, int32_t *o, int q)
{
    int i;
    if (!o && mode != 1 && w != 5) {
        if (!l->done && q == 0)
            l->ph = vap_padd(l->ph, vap_r24(inc * VAP_N));
        return;
    }
    for (i = 0; i < ENG_BLK; i++) {
        uint32_t p0;
        if (l->done) {
            if (o)
                o[i] = l->last;
            continue;
        }
        l->last = vap_lfo_wave(l, w);
        if (o)
            o[i] = l->last;
        p0 = l->ph;
        l->ph = vap_padd(p0, inc);
        if (l->ph < p0 || (inc && l->ph == p0)) {    /* a cycle ends */
            if (mode == 1)
                l->done = 1;
            if (w == 5)
                l->rsh = vap_white(&l->x1, &l->x2) >> 7;
        }
    }
}

/* the PolyBLEP correction at phase p (Q32) of an oscillator of increment inc, Q30; 1 / dt only where it is needed
 * (two samples a cycle): x / inc = (x << z) (2^62 / (inc << z)) / 2^62 */
AINL int32_t vap_blep(uint32_t p, uint32_t inc)
{
    uint32_t x, q;
    int z;
    if (p > 0u - inc)                                /* (p - 1) / dt > -1: -(t + 1)^2 */
        x = p + inc;
    else if (p < inc)                                /* p / dt < 1: (t - 1)^2 */
        x = inc - p;
    else
        return 0;
    z = __builtin_clz(inc);
    q = (uint32_t)(((uint64_t)(x << z) * vap_rcpm(inc << z)) >> 32);
    q = (uint32_t)(((uint64_t)q * q) >> 30);
    return p > 0u - inc ? -(int32_t)q : (int32_t)q;
}

/* one oscillator's sample, Q30: shape Q24 (0 tri .. 0.5 saw .. 1 pulse), pulse width as a phase offset and the
 * pulse's scale 0.5 / max(pw, 1 - pw) (Q30) */
AINL int32_t vap_osc(uint32_t p, uint32_t inc, int32_t sh, uint32_t pw, int32_t psc)
{
    int32_t tri, saw, c1, pul;
    if (sh <= 0)
        return p < 0x80000000u ? (int32_t)p - VAP_ONE30 : (int32_t)(0xC0000000u - p);
    c1 = inc ? vap_blep(p, inc) : 0;
    saw = (int32_t)(p >> 1) - VAP_ONE30 + c1;
    if (sh < (VAP_ONE24 >> 1)) {
        tri = p < 0x80000000u ? (int32_t)p - VAP_ONE30 : (int32_t)(0xC0000000u - p);
        return tri + vap_mul30(saw - tri, sh << 7);
    }
    if (sh == (VAP_ONE24 >> 1))
        return saw;
    {
        uint32_t p2 = p + pw;
        int32_t c2 = inc ? vap_blep(p2, inc) : 0;
        pul = vap_mul30((int32_t)(p >> 1) + c1 - (int32_t)(p2 >> 1) - c2, psc);
    }
    if (sh >= VAP_ONE24)
        return pul;
    return saw + vap_mul30(pul - saw, (sh << 7) - VAP_ONE30);   /* k = 2 shape - 1 */
}

/* the phase increment of a pitch (Hz Q16) times a multiplier (Q30), at most 20 kHz */
AINL uint32_t vap_inc(int32_t hz16, uint32_t m30)
{
    int64_t f = ((int64_t)hz16 * m30) >> 30;
    if (f <= 0)
        return 0;
    return vap_finc((uint32_t)(f > 20000 << 16 ? 20000 << 16 : f));
}

/* the pulse's PW destination value (Q24) as a phase offset, and its scale 0.5 / max(pw, 1 - pw) (Q30) */
AINL uint32_t vap_pw(int32_t d, int32_t *psc)
{
    int32_t w = clampi(clampi(d, -VAP_ONE24, VAP_ONE24) + (VAP_ONE24 >> 1), 0, VAP_ONE24), m;
    m = w > (VAP_ONE24 >> 1) ? w : VAP_ONE24 - w;    /* 0.5 .. 1, Q24 */
    *psc = vap_inv30((uint32_t)m << 7, 30);          /* 1 / (2 m): 2 m in Q30 >= 1.0 */
    return (uint32_t)w << 8;
}

/* a destination buffer of its constant value */
AINL void vap_fill(int32_t *b, int32_t x)
{
    int i;
    for (i = 0; i < ENG_BLK; i++)
        b[i] = x;
}

/* ---- the filters ---- */
/* the table position (Q16 of the index, x = fc / 1024 fs steps) of a cutoff (Hz Q12) */
AINL uint32_t vap_fpos(int32_t c12) { return (uint32_t)(((uint64_t)(uint32_t)c12 * VAP_KIDX) >> 32); }
/* x = fc / fs of a cutoff (Hz Q12, 0 .. 0.5 fs), Q32 */
AINL uint32_t vap_fx(int32_t c12) { return (uint32_t)(((uint64_t)(uint32_t)c12 * VAP_KINC) >> 24); }
AINL int32_t vap_tab(const int32_t *t, uint32_t pos)
{
    uint32_t k = pos >> 16, f = pos & 0xFFFFu;
    return t[k] + (int32_t)(((int64_t)(t[k + 1u] - t[k]) * (int32_t)f) >> 16);
}

/* a Q48 (.. Q56) value times a Q30 coefficient (0 .. 1), the same Q */
AINL int64_t vap_m48(int64_t a, int32_t c) { return (a >> 30) * c + (((a & 0x3FFFFFFF) * c + (1 << 29)) >> 30); }

/* the saturator on a Q48 value: exact below 2 (u - u^3 t(u)), the Q24 one to 128, u / 9 + 8 / (3 u) above */
AINL int64_t vap_pade48(int64_t u)
{
    int64_t a = u < 0 ? -u : u;
    if (a < (2ll << 48))
        return u - ((int64_t)vap_pade_c30((int32_t)(u >> 18)) << 18);
    if (a < (127ll << 48))
        return (int64_t)vap_pade((int32_t)(u >> 24)) << 24;
    {
        uint32_t a16 = a >= (32767ll << 48) ? 0x7FFFFFFFu : (uint32_t)(a >> 32);
        int64_t y = ((u >> 32) * 477218588) + (int64_t)((((uint64_t)u & 0xFFFFFFFFu) * 477218588u) >> 32);   /* u / 9 */
        int64_t h = (int64_t)(((uint64_t)vap_inv30(a16, 16) * 2863311531u) >> 31) << 19;   /* 8 / (3 |u|), Q48 */
        return u < 0 ? y - h : y + h;
    }
}

/* MG ladder (Stilson/Smith, musicdsp "Moog VCF variation 1"), int64: the stages spread as f^-3 .. f^-1 (x0 is
 * 0.35 f^4 u: 1e-13 at the lowest cutoff) and a cutoff falling fast blows the later stages up to ~1000 (as the
 * original's): x0, y1 Q56 (+-128), y2 Q52 (+-2048), y3, y4 Q48 (+-32768; y4 after the saturator) */
static void vap_mg(vap_v_t *v, const vap_ck_t *c, const int32_t *cut, int32_t trk)
{
    int64_t y1 = v->mg1, y2 = v->mg2, y3 = v->mg3, x0p = v->mgx, y4 = v->mg4;
    int32_t k4 = c->mg_k4r, gain = c->mg_gain, cp = cut[0] + 1, d = 0, y4s;
    uint64_t g4 = 0, gk = 0;
    int i;
    for (i = 0; i < ENG_BLK; i++) {
        int64_t x0, n1, n2, n3, s4, fb, xa;
        int32_t in = v->ab[i];
        if (cut[i] != cp) {
            /* f = clamp(c 4.9995 / fs + 0.00025, 0.001, 1), d = 1 - f, r = 1 - 0.15 f^2, g4 = 0.35013 f^4 (Q62),
             * g4 4 res r (Q61) */
            int64_t w = (((int64_t)(trk + cut[i]) * VAP_KMG) >> 24) + 268435;   /* Q30 */
            uint32_t f = (uint32_t)(w < 1073742 ? 1073742 : w > VAP_ONE30 ? VAP_ONE30 : w), kr;
            uint64_t f2 = (uint64_t)f * f, f4;      /* Q60 */
            uint32_t f2h = f >= (uint32_t)VAP_ONE30 ? 0xFFFFFFFFu : (uint32_t)(f2 >> 28);   /* Q32 */
            int32_t r = VAP_ONE30 - (int32_t)(((f2 >> 30) * 161061274u) >> 30);
            cp = cut[i];
            d = VAP_ONE30 - (int32_t)f;
            f4 = (uint64_t)f2h * f2h;                /* Q64 */
            g4 = (f4 >> 32) * VAP_K35 + ((((f4 & 0xFFFFFFFFu) * VAP_K35)) >> 32);
            kr = (uint32_t)vap_mul30(k4, r);         /* 4 res r, Q24 */
            gk = (((g4 >> 32) * kr) << 7) + (((g4 & 0xFFFFFFFFu) * kr) >> 25);
        }
        /* x0 = g4 (in - 4 res r y4) = g4 in - (g4 4 res r) y4 (the second product the only one on the feedback
         * path), Q56 */
        xa = (((int64_t)(g4 >> 32) * in) << 2) + (((int64_t)(g4 & 0xFFFFFFFFu) * in + (1 << 29)) >> 30);
        y4s = (int32_t)(y4 > (127ll << 48) ? 127 << 24 : y4 < -(127ll << 48) ? -(127 << 24) : y4 >> 24);
        fb = (int64_t)(gk >> 32) * y4s;              /* (<< 3 below: within int64 but at 1e18, f ~ 1 and y4 ~ 100) */
        fb = (fb > (1ll << 59) ? (1ll << 62) : fb < -(1ll << 59) ? -(1ll << 62) : fb << 3) +
             (((int64_t)(gk & 0xFFFFFFFFu) * y4s + (1 << 28)) >> 29);
        x0 = xa - fb;
        n1 = vap_m48(y1, d) + vap_m48(x0p, 322122547) + x0;                          /* 0.3; Q56 */
        n2 = vap_m48(y2, d) + ((vap_m48(y1, 322122547) + n1 + 8) >> 4);              /* Q52 */
        n3 = vap_m48(y3, d) + ((vap_m48(y2, 322122547) + n2 + 8) >> 4);              /* Q48 */
        s4 = vap_m48(y4, d) + vap_m48(y3, 322122547) + n3;
        x0p = x0, y1 = n1, y2 = n2, y3 = n3;
        y4 = vap_pade48(s4);
        y4s = (int32_t)(y4 > (127ll << 48) ? 127 << 24 : y4 < -(127ll << 48) ? -(127 << 24) : (y4 + (1 << 23)) >> 24);
        v->ab[i] = clampi(vap_mul24(gain, y4s), -36909875, 36909875);   /* +-2.2 */
    }
    v->mg1 = y1, v->mg2 = y2, v->mg3 = y3, v->mgx = x0p, v->mg4 = y4;
}

/* OB ladder (4 zero-delay one-poles, tanh-like input, k = 6 res): output taps by type */
static void vap_ob(vap_v_t *v, const vap_ck_t *c, const int32_t *cut, int32_t trk, int cvar)
{
    int32_t s1 = v->s1, s2 = v->s2, s3 = v->s3, s4 = v->s4, f1 = v->f1, f2 = v->f2, f3 = v->f3, f4 = v->f4;
    int32_t k = c->ob_k, G = 0, c1 = 0, c2 = 0, c3 = 0, c4 = VAP_ONE30, kn = 0, k1n = 0;
    int type = v->ftype, i;
    for (i = 0; i < ENG_BLK; i++) {
        int32_t t, y1, y2, y3, y4, vv, o;
        if (i == 0 || cvar) {
            /* G = g / (1 + g); the taps G^3, G^2, G, 1 over 1 + g; k and k + 1 over 1 + k G^4 (Q27) */
            int32_t cc = trk + cut[cvar ? i : 0], nrm = VAP_ONE30;
            if (cc <= 0)
                G = 0, c1 = c2 = c3 = 0, c4 = VAP_ONE30;
            else {
                int32_t G2;
                if (cc > (int32_t)VAP_C_OB)
                    cc = (int32_t)VAP_C_OB;
                G = (int32_t)(((int64_t)vap_tab(vap_obg, vap_fpos(cc)) * vap_fx(cc)) >> 31);   /* x G/x, Q30 */
                c4 = VAP_ONE30 - G;                  /* 1 / (1 + g) */
                G2 = vap_mul30(G, G);
                c3 = vap_mul30(G, c4);
                c2 = vap_mul30(G2, c4);
                c1 = vap_mul30(G2, c3);
                /* 1 / (1 + k G^4), Q30: 1 + k G^4 in Q28 (<= 10) */
                nrm = vap_inv30((uint32_t)((1 << 28) + (int32_t)(((int64_t)vap_mul30(G2, G2) * k) >> 26)), 28);
            }
            kn = (int32_t)(((int64_t)k * nrm) >> 27);
            k1n = (int32_t)(((int64_t)(k + VAP_ONE24) * nrm) >> 27);
        }
        /* t = pade(((k + 1) in - k S) / (1 + k G^4)), S the taps of the previous sample (as the original) */
        t = vap_pade((int32_t)(((int64_t)k1n * v->ab[i] - (int64_t)kn * (f1 + f2 + f3 + f4)) >> 27));
        vv = vap_mulr30(t - s1, G); y1 = s1 + vv; s1 = y1 + vv;
        vv = vap_mulr30(y1 - s2, G); y2 = s2 + vv; s2 = y2 + vv;
        vv = vap_mulr30(y2 - s3, G); y3 = s3 + vv; s3 = y3 + vv;
        vv = vap_mulr30(y3 - s4, G); y4 = s4 + vv; s4 = y4 + vv;   /* (12 dB: s4 only feeds back, the same) */
        f1 = vap_mul30(s1, c1), f2 = vap_mul30(s2, c2), f3 = vap_mul30(s3, c3), f4 = vap_mul30(s4, c4);
        switch (type) {
        case 1: o = y4; break;
        case 2: o = y2; break;
        case 5: o = t - 4 * y1 + 6 * y2 - 4 * y3 + y4; break;
        case 6: o = t - 2 * y1 + y2; break;
        case 9: o = 4 * y2 - 8 * y3 + 4 * y4; break;
        default: o = 2 * y1 - 2 * y2; break;
        }
        v->ab[i] = o;
    }
    v->s1 = s1, v->s2 = s2, v->s3 = s3, v->s4 = s4, v->f1 = f1, v->f2 = f2, v->f3 = f3, v->f4 = f4;
}

/* SV (a non-linear state-variable filter): one section, or two sharing coefficients (24 dB). g Q28, the damping
 * (Rd + g or g) Q27, K, h = 1 / D, G1 = g / D, a, 1 + a Q30 */
typedef struct { int32_t g, rs, K, h, G1, na, ap1; } vap_svc_t;

AINL void vap_sv_coef(vap_svc_t *o, const vap_ck_t *c, int32_t cc)
{
    uint32_t pos, x;
    int32_t g, d, rg;
    cc = cc < (int32_t)VAP_C_SVMIN ? (int32_t)VAP_C_SVMIN : cc > (int32_t)VAP_C_SVMAX ? (int32_t)VAP_C_SVMAX : cc;
    pos = vap_fpos(cc);
    x = vap_fx(cc);
    g = (int32_t)(((int64_t)vap_tab(vap_svg, pos) * x) >> 31);    /* x g/x, Q28 (<= 6.9) */
    o->K = (int32_t)(((int64_t)vap_tab(vap_svk, pos) * x) >> 32); /* x K/x, Q30 */
    rg = (c->sv_rd >> 3) + (g >> 1);                 /* Rd + g, Q27 */
    d = (1 << 26) + (int32_t)(((int64_t)g * rg) >> 29);   /* D = 1 + g (Rd + g), Q26 (<= 64) */
    o->h = vap_inv30((uint32_t)d, 26);               /* 1 / D, Q30 */
    o->g = g;
    o->G1 = (int32_t)(((int64_t)g * o->h) >> 28);    /* g / D, Q30 */
    {
        int32_t a = vap_mul30(c->sv_a, o->h);
        o->na = -a;
        o->ap1 = VAP_ONE30 + a;
    }
    o->rs = c->sv_hi ? g >> 1 : rg;                  /* Q27 */
}
/* the bandpass integrator's non-linearity (1 + a) pade(u) - a u */
AINL int32_t vap_sv_nl(const vap_svc_t *o, int32_t u) { return vap_mulr30(vap_pade(u), o->ap1) + vap_mulr30(u, o->na); }
AINL int32_t vap_m28(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 27)) >> 28); }
AINL int32_t vap_m27(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 26)) >> 27); }
AINL int32_t vap_m27x2(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b + (1 << 26)) >> 27); }   /* 2 g a, g Q28 */

static void vap_sv(vap_v_t *v, const vap_ck_t *c, const int32_t *cut, int32_t trk, int cvar)
{
    int32_t za = v->s1, wa = v->s2, zb = v->s3, wb = v->s4;     /* z1, z2 of the sections */
    vap_svc_t o;
    int type = v->ftype, i;
    vap_sv_coef(&o, c, trk + cut[0]);
    for (i = 0; i < ENG_BLK; i++) {
        int32_t x = v->ab[i], hp, u, bp, lp, out;
        if (cvar && i)
            vap_sv_coef(&o, c, trk + cut[i]);
        switch (type) {
        case 4:                                      /* LP12 */
            hp = x - wa - vap_m27(o.rs, za);
            u = za + vap_mulr30(hp, o.G1);
            bp = vap_sv_nl(&o, u);
            lp = wa + vap_mulr30(o.K, za) + vap_m28(bp, o.g);
            za = bp + vap_mulr30(hp, o.G1);
            wa = lp + vap_m28(bp, o.g);
            out = lp;
            break;
        case 8:                                      /* HP12 */
            hp = vap_mul30(x - wa - vap_m27(o.rs, za), o.h);
            u = za + vap_m28(o.g, hp);
            bp = vap_sv_nl(&o, u);
            wa += vap_mulr30(o.K, za) + vap_m27x2(o.g, bp);
            za = bp + vap_m28(o.g, hp);
            out = hp;
            break;
        case 12:                                     /* BP12 */
            hp = x - wa - vap_m27(o.rs, za);
            u = za + vap_mulr30(hp, o.G1);
            bp = vap_sv_nl(&o, u);
            wa += vap_mulr30(o.K, za) + vap_m27x2(o.g, bp);
            za = bp + vap_mulr30(hp, o.G1);
            out = bp;
            break;
        case 14:                                     /* Notch12 */
            hp = vap_mul30(x - wa - vap_m27(o.rs, za), o.h);
            u = za + vap_m28(o.g, hp);
            bp = vap_sv_nl(&o, u);
            lp = wa + vap_mulr30(o.K, za) + vap_m28(bp, o.g);
            za = bp + vap_m28(o.g, hp);
            out = hp + lp;
            wa = lp + vap_m28(bp, o.g);
            break;
        case 3: {                                    /* LP24 */
            int32_t lpa0 = wa + vap_mulr30(o.K, za), lpb0 = wb + vap_mulr30(o.K, zb), xb = wb + vap_m27(o.rs, zb);
            int32_t hpb, ub, bpb;
            hp = x - wa - vap_m27(o.rs, za);
            u = za + vap_mulr30(hp, o.G1);
            bp = vap_sv_nl(&o, u);
            lp = lpa0 + vap_m28(bp, o.g);
            za = bp + vap_mulr30(hp, o.G1);
            hpb = lp - xb;
            wa = lp + vap_m28(bp, o.g);
            ub = zb + vap_mulr30(hpb, o.G1);
            bpb = vap_sv_nl(&o, ub);
            out = lpb0 + vap_m28(bpb, o.g);
            zb = bpb + vap_mulr30(hpb, o.G1);
            wb = out + vap_m28(bpb, o.g);
            break;
        }
        case 7: {                                    /* HP24 */
            int32_t xb = wb + vap_m27(o.rs, zb), hpb, ub, bpb;
            hp = vap_mul30(x - wa - vap_m27(o.rs, za), o.h);
            u = za + vap_m28(o.g, hp);
            hpb = vap_mul30(hp - xb, o.h);
            ub = zb + vap_m28(o.g, hpb);
            bp = vap_sv_nl(&o, u);
            wa += vap_mulr30(o.K, za) + vap_m27x2(o.g, bp);
            za = bp + vap_m28(o.g, hp);
            bpb = vap_sv_nl(&o, ub);
            wb += vap_mulr30(o.K, zb) + vap_m27x2(o.g, bpb);
            zb = bpb + vap_m28(o.g, hpb);
            out = hpb;
            break;
        }
        case 11: {                                   /* BP24 */
            int32_t xb = wb + vap_m27(o.rs, zb), hpb, ub, bpb;
            hp = x - wa - vap_m27(o.rs, za);
            u = za + vap_mulr30(hp, o.G1);
            bp = vap_sv_nl(&o, u);
            hpb = bp - xb;
            ub = zb + vap_mulr30(hpb, o.G1);
            wa += vap_mulr30(o.K, za) + vap_m27x2(o.g, bp);
            za = bp + vap_mulr30(hp, o.G1);
            bpb = vap_sv_nl(&o, ub);
            wb += vap_mulr30(o.K, zb) + vap_m27x2(o.g, bpb);
            zb = bpb + vap_mulr30(hpb, o.G1);
            out = bpb;
            break;
        }
        default: {                                   /* 13: Notch24 */
            int32_t lpa0 = wa + vap_mulr30(o.K, za), lpb0 = wb + vap_mulr30(o.K, zb), xb = wb + vap_m27(o.rs, zb);
            int32_t nb, hpb, ub, bpb, lpb;
            hp = vap_mul30(x - wa - vap_m27(o.rs, za), o.h);
            u = za + vap_m28(o.g, hp);
            nb = hp - xb;
            bp = vap_sv_nl(&o, u);
            lp = lpa0 + vap_m28(o.g, bp);
            za = bp + vap_m28(o.g, hp);
            nb += lp;
            wa = lp + vap_m28(o.g, bp);
            hpb = vap_mul30(nb, o.h);
            ub = zb + vap_m28(o.g, hpb);
            bpb = vap_sv_nl(&o, ub);
            lpb = lpb0 + vap_m28(o.g, bpb);
            zb = bpb + vap_m28(o.g, hpb);
            out = hpb + lpb;
            wb = lpb + vap_m28(o.g, bpb);
            break;
        }
        }
        v->ab[i] = out;
    }
    v->s1 = za, v->s2 = wa, v->s3 = zb, v->s4 = wb;
}

static int vap_render(void *vp, const void *ckp, int32_t *outl, int32_t *outr)
{
    vap_v_t *v = vp;
    const vap_ck_t *c = ckp;
    uint32_t act = 0, dvar = 0, svar = 0;
    int i, d, m;
    (void)outr;
    /* the filter's type changed: a fresh filter (resonance and tracking kept; its note frequency 0 until a note) */
    if (v->ftype != c->ftype) {
        if (v->ftype != 0xFF)                        /* (a fresh voice: its type was set before any note) */
            v->notef = 0;
        v->ftype = c->ftype;
        v->mg1 = v->mg2 = v->mg3 = v->mgx = 0;
        v->mg4 = 0;
        v->s1 = v->s2 = v->s3 = v->s4 = v->f1 = v->f2 = v->f3 = v->f4 = 0;
    }
    /* Mix Env Amt / Amt 4: whichever changed last sets slot 4 */
    if (c->p38 != v->l38)
        v->w38 = 1, v->l38 = c->p38;
    if (c->p53 != v->l53)
        v->w38 = 0, v->l53 = c->p53;
    /* the base values (once a block: smoothed over the block when they moved) */
    if (v->q == 0) {
        v->ramp = 0;
        for (d = 0; d < VAP_ND; d++) {
            int32_t b = c->base[d], df = b - v->prev[d];
            int32_t thr = d == VAP_D_CUT ? 1 : ((1u << d) & VAP_HZ_DEST) ? 7 : 1678;    /* 0.0001 */
            if (!v->skip && (df >= thr || df <= -thr) && d != VAP_D_DF)
                v->ramp |= (uint16_t)(1u << d), v->from[d] = v->prev[d];
            v->to[d] = b;
            v->prev[d] = b;
        }
        v->skip = 0;
        /* the envelopes' decay offsets: (sustain - tco)(1 - coef), the block's first sustain */
        for (i = 0; i < 3; i++) {
            static const uint8_t sd[3] = {VAP_D_SA, VAP_D_SF, VAP_D_SX};
            int32_t s = ((v->ramp >> sd[i]) & 1 ? v->from[sd[i]] : v->to[sd[i]]) << 6;
            v->env[i].doff = (int32_t)(((int64_t)(s - VAP_DTCO30) * c->env[i].dom) >> 31);
        }
    }
    for (d = 0; d < VAP_ND; d++) {
        if ((v->ramp >> d) & 1) {
            int32_t a = v->from[d], df = v->to[d] - a, j0 = v->q * ENG_BLK;
            for (i = 0; i < ENG_BLK; i++)
                v->db[d][i] = a + (int32_t)(((int64_t)df * (j0 + i)) >> 7);
            dvar |= 1u << d;
        } else
            v->db[d][0] = v->to[d];
    }
    /* sources: the envelopes (always), the LFOs (when a slot uses them), glide */
    for (m = 0; m < 10; m++) {
        const vap_mod_t *md = &c->mod[m];
        int32_t mult = m == 7 && v->w38 ? c->m7_38 : md->mult;
        if (mult != 0)
            act |= 1u << md->src;
    }
    {
        static const uint8_t sd[3] = {VAP_D_SA, VAP_D_SF, VAP_D_SX};
        for (i = 0; i < 3; i++)
            if (vap_env_run(&v->env[i], &c->env[i], v->db[sd[i]], (dvar >> sd[i]) & 1, v->sb[i]))
                svar |= 1u << i;
    }
    for (i = 0; i < 2; i++) {
        uint32_t inc = c->linc[i];
        if (c->lsync[i]) {
            /* tempo x 0.4 / pulses (Hz) */
            static const uint8_t pulses[16] = {96, 72, 48, 36, 32, 24, 18, 16, 12, 9, 8, 6, 4, 3, 2, 1};
            inc = vap_finc((uint32_t)eng_bpm10 * 262144u / 100u / pulses[c->lpulse[i]]);
        }
        vap_lfo_run(&v->lfo[i], c->lwave[i], c->lmode[i], inc, (act >> (3 + i)) & 1 ? v->sb[3 + i] : 0, v->q);
        svar |= 1u << (3 + i);
    }
    /* glide: the ramp, or the note's frequency (440 Hz before any) */
    if (v->gl_on) {
        int64_t x = v->gl_x;
        for (i = 0; i < ENG_BLK; i++) {
            x = vap_f32(vap_mq31(x, v->gl_coef) + v->gl_off);
            v->sb[VAP_S_GLIDE][i] = (int32_t)((x + (1 << 15)) >> 16);
        }
        v->gl_x = x;
        if (v->gl_dir == 0 || (v->gl_dir > 0 ? x >= v->gl_to : x <= v->gl_to))
            v->gl_on = 0;                            /* (in float32 it stalls short of most targets: on) */
        svar |= 1u << VAP_S_GLIDE;
    } else
        v->sb[VAP_S_GLIDE][0] = v->has_f ? v->lastf : 440 << 16;
    /* the modulations (the envelope destinations, read before them, are left as the original does) */
    for (m = 0; m < 10; m++) {
        const vap_mod_t *md = &c->mod[m];
        int32_t mult = m == 7 && v->w38 ? c->m7_38 : md->mult, *db;
        const int32_t *sb = v->sb[md->src];
        int sv = (svar >> md->src) & 1, op = md->op;
        d = md->dst;
        if (mult == 0 || d == VAP_D_SA || d == VAP_D_SF || d == VAP_D_SX || d == VAP_D_DF)
            continue;
        db = v->db[d];
        if (op == 0) {
            if (!sv) {
                int32_t x = md->src == VAP_S_GLIDE ? sb[0] : vap_mul24(sb[0], mult);
                if (x == 0)
                    continue;
                if ((dvar >> d) & 1)
                    for (i = 0; i < ENG_BLK; i++)
                        db[i] += x;
                else
                    db[0] += x;
                continue;
            }
            if (!((dvar >> d) & 1))
                vap_fill(db, db[0]);
            dvar |= 1u << d;
            if (md->src == VAP_S_GLIDE)
                for (i = 0; i < ENG_BLK; i++)
                    db[i] += sb[i];
            else
                for (i = 0; i < ENG_BLK; i++)
                    db[i] += vap_mul24(sb[i], mult);
            continue;
        }
        if (!((dvar >> d) & 1))
            vap_fill(db, db[0]);
        dvar |= 1u << d;
        for (i = 0; i < ENG_BLK; i++) {
            int32_t s = sb[sv ? i : 0];
            if (op == 1)                             /* x ((s + 1) mult / 2 + 1) */
                db[i] = vap_mul24(db[i], VAP_ONE24 + (int32_t)(((int64_t)(s + VAP_ONE24) * (mult >> 1)) >> 24));
            else {                                   /* x (1 + x) or / (1 - x), within +-65504 */
                int32_t x = vap_mul24(s, mult);
                int64_t y;
                if (x >= 0)
                    y = ((int64_t)db[i] * (VAP_ONE24 + x)) >> 24;
                else
                    y = ((int64_t)db[i] * vap_inv30((uint32_t)(VAP_ONE24 - x), 24)) >> 30;
                db[i] = (int32_t)(y > 0x7FFFFFFF ? 0x7FFFFFFF : y < -0x7FFFFFFF ? -0x7FFFFFFF : y);   /* (32767 Hz) */
            }
        }
    }
    /* oscillators, mix, noise */
    {
        const int32_t *pb1 = v->db[VAP_D_P1], *pb2 = v->db[VAP_D_P2];
        int v1 = (dvar >> VAP_D_P1) & 1, v2 = (dvar >> VAP_D_P2) & 1;
        int vs1 = (dvar >> VAP_D_SH1) & 1, vs2 = (dvar >> VAP_D_SH2) & 1, vw1 = (dvar >> VAP_D_PW1) & 1,
            vw2 = (dvar >> VAP_D_PW2) & 1, vm = (dvar >> VAP_D_MIX) & 1;
        uint32_t i1 = vap_inc(pb1[0], c->m1), i2 = vap_inc(pb2[0], c->m2), w1, w2;
        int32_t sc1, sc2, sh1, sh2, mix;
        uint32_t ph1 = v->p1, ph2 = v->p2;
        w1 = vap_pw(v->db[VAP_D_PW1][0], &sc1);
        w2 = vap_pw(v->db[VAP_D_PW2][0], &sc2);
        sh1 = clampi(v->db[VAP_D_SH1][0], 0, VAP_ONE24);
        sh2 = clampi(v->db[VAP_D_SH2][0], 0, VAP_ONE24);
        mix = (clampi(v->db[VAP_D_MIX][0], -VAP_ONE24, VAP_ONE24) + VAP_ONE24) >> 1;
        for (i = 0; i < ENG_BLK; i++) {
            int32_t o = 0;
            uint32_t q2;
            if (v1 && i)
                i1 = vap_inc(pb1[i], c->m1);
            if (v2 && i)
                i2 = vap_inc(pb2[i], c->m2);
            if (vs1)
                sh1 = clampi(v->db[VAP_D_SH1][i], 0, VAP_ONE24);
            if (vs2)
                sh2 = clampi(v->db[VAP_D_SH2][i], 0, VAP_ONE24);
            if (vw1 && i)
                w1 = vap_pw(v->db[VAP_D_PW1][i], &sc1);
            if (vw2 && i)
                w2 = vap_pw(v->db[VAP_D_PW2][i], &sc2);
            if (vm)
                mix = (clampi(v->db[VAP_D_MIX][i], -VAP_ONE24, VAP_ONE24) + VAP_ONE24) >> 1;
            /* hard sync: osc 1 has just wrapped */
            q2 = c->sync && ph1 < i1 ? 0 : ph2;
            if (mix != VAP_ONE24)
                o = vap_mul30(vap_osc(ph1, i1, sh1, w1, sc1), (VAP_ONE24 - mix) << 6);
            if (mix != 0)
                o += vap_mul30(vap_osc(q2, i2, sh2, w2, sc2), mix << 6);
            v->ab[i] = o >> 6;
            ph2 = q2 ? vap_padd(q2, i2) : i2;
            ph1 = vap_padd(ph1, i1);
        }
        v->p1 = ph1, v->p2 = ph2;
    }
    {
        /* pink noise (PolyDSP PinkNoise) x noise / 4 */
        int nv = (dvar >> VAP_D_NOISE) & 1;
        int32_t n0 = v->db[VAP_D_NOISE][0];
        if (nv || n0 > 0) {
            uint32_t x1 = v->nx1, x2 = v->nx2;
            int32_t b0 = v->pk0, b1 = v->pk1, b2 = v->pk2;
            for (i = 0; i < ENG_BLK; i++) {
                int32_t w = vap_white(&x1, &x2) >> 7, n = nv ? v->db[VAP_D_NOISE][i] : n0, p;
                b0 = (int32_t)(((int64_t)b0 * 1071218531 + (int64_t)w * 106349833) >> 30);
                b1 = (int32_t)(((int64_t)b1 * 1034013377 + (int64_t)w * 318382060) >> 30);
                b2 = (int32_t)(((int64_t)b2 * 612032840 + (int64_t)w * 1130318677) >> 30);
                p = b0 + b1 + b2 + vap_mul30(w, 198427489);
                if (n > 0)
                    v->ab[i] += (int32_t)(((int64_t)p * n) >> 26);
            }
            v->nx1 = x1, v->nx2 = x2, v->pk0 = b0, v->pk1 = b1, v->pk2 = b2;
        }
    }
    /* the filter (cutoff 20 kHz: none) */
    if (!c->bypass) {
        int cvar = (dvar >> VAP_D_CUT) & 1;
        int32_t trk = 0;
        if (c->track)
            trk = (int32_t)(((int64_t)c->track * (v->notef - (int32_t)VAP_REF_F4)) >> 20);   /* Hz Q12 */
        if (!cvar)
            vap_fill(v->db[VAP_D_CUT], v->db[VAP_D_CUT][0]);
        switch (v->ftype) {
        case 0: vap_mg(v, c, v->db[VAP_D_CUT], trk); break;
        case 1: case 2: case 5: case 6: case 9: case 10: vap_ob(v, c, v->db[VAP_D_CUT], trk, cvar); break;
        default: vap_sv(v, c, v->db[VAP_D_CUT], trk, cvar); break;
        }
    }
    /* the amplifier: gain (amp env x volume ..) smoothed by a one-pole (0.97), 0 where it is <= 0 */
    {
        int vv = (dvar >> VAP_D_VOL) & 1;
        int32_t s = v->amp;
        for (i = 0; i < ENG_BLK; i++) {
            int32_t g = v->db[VAP_D_VOL][vv ? i : 0];
            if (g <= 0) {
                outl[i] = 0;
                continue;
            }
            g = g > (1 << 27) - 1 ? 0x7FFFFFFF : g << 4;
            s = g + (int32_t)(((int64_t)(s - g) * 1041529569) >> 30);   /* 0.97 */
            outl[i] = (int32_t)(((int64_t)v->ab[i] * s) >> 37);
        }
        v->amp = s;
    }
    v->q = (uint8_t)((v->q + 1u) & 3u);
    return v->env[0].st != 0;
}

const eng_t ENG_VAP = {
    "VAP", "VIRTUAL ANALOG", VAP_NPAR, 0, 9, 6, VAP_PAR, VAP_SECTION, VAP_MACRO, VAP_ENUMS,
    sizeof(vap_v_t), sizeof(vap_ck_t),
    vap_init, vap_cook, vap_reset, vap_on, vap_off, vap_render,
};
