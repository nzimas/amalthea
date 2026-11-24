/* SPDX-License-Identifier: GPL-3.0-only
 * FUGITIVE: CHIP, the Polyend open-source chiptune engine (synth-plugin-sdk/engines/chip.cpp, MIT,
 * ChiptuneSynthVoice), in fixed point for the FM-1. One monophonic voice; mono output.
 *
 * Signal flow, as the original (per sample; docs/engines/CHIP.md has the laws):
 *   env (linear ADSR) -> glide -> chord arp -> PWM triangle LFO -> vibrato sine -> pitch sweep / zap
 *   -> one of 8 models (2A03 pulse, SID saw/tri/ring, LFSR noise, FM bell, super square, zap, arp chord, drum)
 *   -> stepped-triangle sub osc an octave down -> sample-and-hold decimator + bit crusher
 *   -> Chamberlin SVF low-pass (cutoff x 2^(4 env amount env)), clamped states -> x env x volume.
 * Formats: phases uint32 (2^32 a cycle), samples Q15, envelope Q30, SVF states Q28 and coefficients Q30,
 * pitch Q16 semitones (glide Q24), octaves Q24 for 2^x.
 */
#include "eng_chip_tab.h"

enum {
    CHIP_P_MODEL, CHIP_P_TUNE, CHIP_P_FINE, CHIP_P_SHAPE, CHIP_P_PWMRATE, CHIP_P_PWMDEPTH, CHIP_P_SUB,
    CHIP_P_ARP, CHIP_P_ARPSPEED, CHIP_P_SWEEP, CHIP_P_SWEEPTIME, CHIP_P_VIBRATE, CHIP_P_VIBDEPTH,
    CHIP_P_ATT, CHIP_P_DEC, CHIP_P_SUS, CHIP_P_REL, CHIP_P_CUT, CHIP_P_RES, CHIP_P_FENV,
    CHIP_P_CRUSH, CHIP_P_DECIM, CHIP_P_PLAYMODE, CHIP_P_GLIDE, CHIP_P_VOL, CHIP_NPAR
};
enum { CHIP_PULSE, CHIP_SID, CHIP_NOISE, CHIP_FM, CHIP_SUPER, CHIP_ZAP, CHIP_ARPCHORD, CHIP_DRUM };
enum { CHIP_IDLE, CHIP_ATT, CHIP_DEC, CHIP_SUS, CHIP_REL };
enum { CHIP_ALWAYS, CHIP_OVERLAP, CHIP_LEGATO };

#define CHIP_ONE (1 << 30)               /* envelope 1.0, Q30 */
#define CHIP_ENV_THR 107374              /* 0.0001, Q30 */
#define CHIP_GLIDE_THR 16777             /* 0.001 semitone, Q24 */
#define CHIP_NHELD 8                     /* the SDK's kMaxHeldNotes */

static const eng_par_t CHIP_PAR[CHIP_NPAR] = {
    {"MODEL", 0, 458752, 0, 0, 1, 0, 20},                  /* 0: 0 .. 7 */
    {"TUNE", -1572864, 1572864, 0, 0, 0, 0, 21},           /* 1: -24 .. 24 semitones */
    {"FINE", -6553600, 6553600, 0, 0, 0, 0, 22},           /* 2: -100 .. 100 cents */
    {"DUTY / SHAPE", 0, 65536, 22938, 0, 0, 0, 23},        /* 3: 0 .. 1, 0.35 */
    {"PWM RATE", 6554, 2621440, 163840, 1, 0, 0, 24},      /* 4: 0.1 .. 40 Hz, 2.5 */
    {"PWM DEPTH", 0, 65536, 13107, 0, 0, 0, 25},           /* 5: 0 .. 1, 0.2 */
    {"SUB OSC", 0, 65536, 16384, 0, 0, 0, 26},             /* 6: 0 .. 1, 0.25 */
    {"CHORD ARP", 0, 458752, 0, 0, 1, 1, 27},              /* 7: 0 .. 7 */
    {"ARP SPEED", 327680, 4915200, 3276800, 0, 0, 1, 28},  /* 8: 5 .. 75 Hz, 50 */
    {"PITCH SWEEP", -2359296, 2359296, 0, 0, 0, 1, 29},    /* 9: -36 .. 36 semitones */
    {"SWEEP TIME", 1311, 131072, 16384, 1, 0, 1, 30},      /* 10: 0.02 .. 2 s, 0.25 */
    {"VIBRATO RATE", 32768, 1638400, 393216, 1, 0, 1, 54}, /* 11: 0.5 .. 25 Hz, 6 */
    {"VIB DEPTH", 0, 65536, 0, 0, 0, 1, 31},               /* 12: 0 .. 1 ("Vibrato Depth") */
    {"ATTACK", 0, 327680, 0, 1, 0, 2, 75},                 /* 13: 0 .. 5 s */
    {"DECAY", 655, 327680, 22938, 1, 0, 2, 72},            /* 14: 0.01 .. 5 s, 0.35 */
    {"SUSTAIN", 0, 65536, 26214, 0, 0, 2, 76},             /* 15: 0 .. 1, 0.4 */
    {"RELEASE", 655, 327680, 9830, 1, 0, 2, 73},           /* 16: 0.01 .. 5 s, 0.15 */
    {"CUTOFF", 5242880, 1179648000, 786432000, 2, 0, 3, 74}, /* 17: 80 .. 18000 Hz, 12000 */
    {"RESONANCE", 0, 65536, 9830, 0, 0, 3, 71},            /* 18: 0 .. 1, 0.15 */
    {"ENV AMT", -65536, 65536, 13107, 0, 0, 3, 77},        /* 19: -1 .. 1, 0.2 */
    {"BITCRUSH", 131072, 1048576, 524288, 0, 0, 3, 255},   /* 20: 2 .. 16 bits, 8 */
    {"DECIMATE", 65536, 2097152, 65536, 0, 0, 3, 255},     /* 21: 1 .. 32 x, 1 */
    {"GLIDE MODE", 0, 131072, 0, 0, 1, 4, 255},            /* 22: 0 .. 2 */
    {"GLIDE TIME", 0, 131072, 0, 1, 0, 4, 255},            /* 23: 0 .. 2 s */
    {"VOLUME", 0, 131072, 65536, 0, 0, 4, 255},            /* 24: 0 .. 2, 1 (the original's macro op: multiply) */
};

static const char *const CHIP_SECTION[5] = {"OSCILLATOR", "MODULATION", "AMPLIFIER", "FILTER", "VOICE"};

static const eng_macro_t CHIP_MACRO[6] = {
    {"TIMBRE / PWM", 1, 2, {{CHIP_P_SHAPE, 22938}, {CHIP_P_PWMDEPTH, 16384}}},
    {"ARP / CHORD", 0, 1, {{CHIP_P_ARP, 32768}}},
    {"SWEEP / VIBRATO", 1, 2, {{CHIP_P_VIBDEPTH, 19661}, {CHIP_P_SWEEP, 16384}}},
    {"ENV DECAY / REL", 1, 2, {{CHIP_P_DEC, 26214}, {CHIP_P_REL, 19661}}},
    {"CRUSH / LO-FI", 0, 2, {{CHIP_P_CRUSH, -19661}, {CHIP_P_DECIM, 13107}}},
    {"FILTER CUTOFF", 1, 2, {{CHIP_P_CUT, 32768}, {CHIP_P_RES, 11469}}},
};

static const char *const CHIP_E_MODEL[] = {"2A03 PULSE", "SID SAW/TRI", "NOISE / LFSR", "FM BELL",
                                           "SUPER SQUARE", "ZAP / LASER", "ARP CHORD", "CHIP DRUM", 0};
static const char *const CHIP_E_ARP[] = {"OFF", "OCTAVE", "FIFTH", "MAJOR", "MINOR", "SUS4", "MAJ7", "MIN7", 0};
static const char *const CHIP_E_MODE[] = {"ALWAYS", "OVERLAP", "LEGATO", 0};
static const char *const *const CHIP_ENUMS[CHIP_NPAR] = {
    [CHIP_P_MODEL] = CHIP_E_MODEL, [CHIP_P_ARP] = CHIP_E_ARP, [CHIP_P_PLAYMODE] = CHIP_E_MODE,
};

static const int8_t CHIP_ARPS[8][4] = {
    {0, 0, 0, 0}, {0, 12, 0, 12}, {0, 7, 12, 7}, {0, 4, 7, 12},
    {0, 3, 7, 12}, {0, 5, 7, 12}, {0, 4, 7, 11}, {0, 3, 7, 10},
};
static const uint8_t CHIP_FM_RATIO2[8] = {2, 4, 6, 7, 8, 10, 14, 18};   /* 2 x {1, 2, 3, 3.5, 4, 5, 7, 9} */

/* the tables read every sample, in RAM (POOL) on the target */
static POOL uint32_t chip_exp2[258];
static POOL int32_t chip_svf[CHIP_SVF_N];

typedef struct {
    uint8_t model, arp, lfsr_short, drum_metal, has_sub, has_vib, has_fenv, crush_on, ratio2, decim;
    uint8_t res_on, f_on0, glide_on, mode, has_sweep, pad;
    int32_t base16;                  /* tune + fine, Q16 semitones */
    int32_t duty30, pwm30;           /* 0.125 + 0.75 shape, 0.38 depth: Q30 */
    uint32_t lfsr16;                 /* 2 + 14 (1 - depth), Q16 */
    int32_t fm16;                    /* FM index scale, Q16 */
    uint32_t m2, m3, r2, r3;         /* super square 1 +- spread and their reciprocals, Q30 */
    int32_t drum16, drum_mix;        /* drum sweep scale Q16, sustained noise mix Q15 */
    int32_t sub_main, sub_gain;      /* Q15 */
    uint32_t arp_period;
    uint32_t sweep_inv;              /* 2^37 / sweep samples */
    int32_t sweep16, zap16;          /* Q16 semitones */
    uint32_t pwm_inc, vib_inc;
    int32_t vib16;                   /* 1.5 depth, Q16 semitones */
    int32_t att, dec, rel, sus;      /* Q30 */
    int32_t glide24;                 /* Q24 semitones a sample */
    int32_t crush16;                 /* 2^(bits - 1), Q16 */
    uint32_t crush_inv;              /* 1 / that, Q30 */
    int32_t q30;                     /* damping */
    int32_t l0;                      /* log2 cutoff, Q16 */
    int32_t fenv16;                  /* 4 x env amount, Q16 octaves */
    int32_t f0;                      /* coefficient without the envelope, Q30 */
    int32_t vol16;
} chip_ck_t;

typedef struct {
    int32_t held[CHIP_NHELD];
    uint8_t nheld, stage, played, sub_odd, blkq, blk_live;
    uint16_t lfsr;
    int32_t env;
    int32_t gcur, gtgt;              /* glide, Q24 semitones */
    uint32_t ph, ph2, ph3, sub, pwm_ph, vib_ph, lfsr_ph;
    uint32_t age, arp_cnt, arp_step;
    int32_t dcnt, hold;
    int32_t lo, bd;                  /* SVF, Q28 */
    int32_t note16;                  /* the pitch inc / rdt were made for */
    uint32_t inc, rdt;               /* phase increment; 2^48 / inc */
} chip_v_t;

static void chip_init(void)
{
    int i;
    for (i = 0; i < 258; i++)
        chip_exp2[i] = CHIP_EXP2[i];
    for (i = 0; i < CHIP_SVF_N; i++)
        chip_svf[i] = CHIP_SVF_F[i];
}

/* 2^(x / 2^24) for x in 0 .. 2^24 (inclusive), Q30 */
AINL uint32_t chip_exp2_30(uint32_t x)
{
    uint32_t k = x >> 16, a = chip_exp2[k];
    return a + (uint32_t)(((uint64_t)(chip_exp2[k + 1] - a) * (x & 0xFFFFu)) >> 16);
}

/* the original's clamp(8.1757989 * 2^(note / 12), 16, 18000) / FS as a phase increment, and 2^48 / it */
static void chip_pitch(chip_v_t *v, int32_t note16)
{
    int32_t oct = (int32_t)(((int64_t)note16 * 91625968981LL) >> 32);   /* note / 12, Q24 octaves */
    uint32_t f, t;
    int i;
    if (oct < (int32_t)CHIP_OCT_MIN)
        oct = (int32_t)CHIP_OCT_MIN;
    if (oct > (int32_t)CHIP_OCT_MAX)
        oct = (int32_t)CHIP_OCT_MAX;
    i = oct >> 24;
    f = (uint32_t)oct & 0xFFFFFFu;
    t = (uint32_t)(((uint64_t)chip_exp2_30(f) * CHIP_KINC) >> 32);
    v->inc = i <= 10 ? t >> (10 - i) : t << (i - 10);
    t = (uint32_t)(((uint64_t)chip_exp2_30(0x1000000u - f) * CHIP_KRDT) >> 32);
    v->rdt = t >> (i + 1);
    v->note16 = note16;
}

/* polyBlep(t, dt), Q15: t phase, inc = dt, rdt = 2^48 / inc */
AINL int32_t chip_blep(uint32_t t, uint32_t inc, uint32_t rdt)
{
    uint32_t x, u;
    if (t < inc) {
        x = (uint32_t)(((uint64_t)t * rdt) >> 32);              /* t / dt, Q16 */
        u = x >= 65536u ? 0 : 65536u - x;
        return -(int32_t)(((uint64_t)u * u) >> 17);             /* 2x - x^2 - 1 = -(1 - x)^2 */
    }
    t = 0u - t;
    if (t < inc) {
        x = (uint32_t)(((uint64_t)t * rdt) >> 32);              /* (1 - t) / dt */
        u = x >= 65536u ? 0 : 65536u - x;
        return (int32_t)(((uint64_t)u * u) >> 17);              /* x^2 + 2x + 1, x = (t - 1) / dt */
    }
    return 0;
}

/* nesSteppedTriangle: round(tri x 7.5) / 7.5, half away from zero, Q15 */
AINL int32_t chip_tri_step(uint32_t p)
{
    uint64_t w;
    int n;
    if (p > 0x80000000u)
        p = 0u - p;                                             /* 3 - 4p = 4 (1 - p) - 1 */
    w = (uint64_t)p * 60u;                                      /* 60 p, Q32: tri x 7.5 = (w - 15) / 2 */
    if (w >= (15ull << 32))
        n = (int)((w - (14ull << 32)) >> 33);
    else
        n = -(int)(((16ull << 32) - w) >> 33);
    return CHIP_TRI_LV[n + 8];
}

AINL uint16_t chip_lfsr(uint16_t s, int short_mode)
{
    uint16_t tap = short_mode ? ((s >> 6) & 1u) : ((s >> 1) & 1u);
    uint16_t bit = (uint16_t)((s ^ tap) & 1u);
    return (uint16_t)((s >> 1) | (bit << 14));
}

/* the SVF coefficient for log2(cutoff) Q16 (clamped to 40 .. 18000 Hz), Q30 */
AINL int32_t chip_svf_f(int32_t l)
{
    int32_t x = l - (CHIP_SVF_L0 << 16), i = x >> 10, a = chip_svf[i];
    return a + (int32_t)(((int64_t)(chip_svf[i + 1] - a) * (x & 1023)) >> 10);
}

AINL int32_t chip_round16(int32_t v)   /* std::round of a Q16 value, an integer */
{
    return v >= 0 ? (v + 32768) >> 16 : -((-v + 32768) >> 16);
}

static void chip_cook(void *ckp, const int32_t *val)
{
    chip_ck_t *c = (chip_ck_t *)ckp;
    int32_t shape = val[CHIP_P_SHAPE], depth = val[CHIP_P_PWMDEPTH], sub = val[CHIP_P_SUB], t, x;
    uint32_t sp, s8;
    int i;

    c->model = (uint8_t)clampi((val[CHIP_P_MODEL] + 32768) >> 16, 0, 7);
    c->base16 = clampi(chip_round16(val[CHIP_P_TUNE]), -32, 32) * 65536;
    x = val[CHIP_P_FINE];
    c->base16 += x >= 0 ? (x + 50) / 100 : -((-x + 50) / 100);
    c->duty30 = 134217728 + shape * 12288;
    c->pwm30 = (int32_t)(((int64_t)depth * 408021893) >> 16);
    c->lfsr16 = (uint32_t)(2 * 65536 + 14 * (65536 - depth));
    c->lfsr_short = shape >= 32768;
    i = clampi(shape, 0, 65536) * 799 / 100 >> 16;      /* (int)(shape x 7.99) */
    c->ratio2 = CHIP_FM_RATIO2[clampi(i, 0, 7)];
    c->fm16 = 106496 + (int32_t)(((int64_t)depth * 149094) >> 16);
    t = 2147484 + (int32_t)(((int64_t)shape * 19327353) >> 16);   /* spread, Q30 */
    c->m2 = (uint32_t)(CHIP_ONE + t);
    c->m3 = (uint32_t)(CHIP_ONE - t);
    c->r2 = udiv64_32(1u << 28, 0, c->m2);
    c->r3 = udiv64_32(1u << 28, 0, c->m3);
    c->drum16 = 229376 - (int32_t)(((int64_t)shape * 160563) >> 16);
    c->drum_metal = shape > 49152;
    c->drum_mix = clampi((shape * 5) >> 3, 0, 32768);
    c->has_sub = sub > 65;
    c->sub_main = 32768 - (int32_t)(((int64_t)sub * 11469) >> 16);
    c->sub_gain = (int32_t)(((int64_t)sub * 3) >> 3);

    i = clampi((val[CHIP_P_ARP] + 32768) >> 16, 0, 7);
    c->arp = (uint8_t)((c->model == CHIP_ARPCHORD && i == 0) ? 3 : i);
    sp = (uint32_t)maxi(val[CHIP_P_ARPSPEED], 5 << 16);
    c->arp_period = 2890137600u / sp;                   /* (uint32_t)(44100 / speed) */
    if (c->arp_period < 64u)
        c->arp_period = 64u;

    c->sweep16 = val[CHIP_P_SWEEP];
    s8 = (uint32_t)(((uint64_t)(uint32_t)maxi(val[CHIP_P_SWEEPTIME], 0) * 44100u) >> 8);   /* samples, Q8 */
    if (s8 < 64u * 256u)
        s8 = 64u * 256u;
    c->sweep_inv = udiv64_32(1u << 13, 0, s8);         /* 2^37 / samples */
    c->zap16 = 786432 + 24 * shape;
    c->has_sweep = c->sweep16 != 0 || c->model == CHIP_ZAP;

    c->pwm_inc = (uint32_t)(((uint64_t)(uint32_t)maxi(val[CHIP_P_PWMRATE], 0) * CHIP_KHZ) >> 31);
    c->vib_inc = (uint32_t)(((uint64_t)(uint32_t)maxi(val[CHIP_P_VIBRATE], 0) * CHIP_KHZ) >> 31);
    c->vib16 = val[CHIP_P_VIBDEPTH] + val[CHIP_P_VIBDEPTH] / 2;
    c->has_vib = val[CHIP_P_VIBDEPTH] > 0;

    t = val[CHIP_P_ATT];
    c->att = t >= 66 ? (int32_t)((CHIP_KSEC + (uint32_t)t / 2u) / (uint32_t)t) : CHIP_ONE;
    x = clampi(val[CHIP_P_SUS], 0, 65536);
    t = val[CHIP_P_DEC];
    c->dec = t >= 132 ? (int32_t)(((int64_t)((CHIP_KSEC + (uint32_t)t / 2u) / (uint32_t)t) * (65536 - x)) >> 16)
                      : CHIP_ONE;
    t = val[CHIP_P_REL];
    c->rel = t >= 132 ? (int32_t)((CHIP_KSEC + (uint32_t)t / 2u) / (uint32_t)t) : CHIP_ONE;
    c->sus = x << 14;

    t = val[CHIP_P_GLIDE];
    c->glide_on = t >= 66;
    c->glide24 = c->glide_on ? (int32_t)(CHIP_KGLIDE / (uint32_t)t) : 24 << 24;
    c->mode = (uint8_t)clampi((val[CHIP_P_PLAYMODE] + 32768) >> 16, 0, 2);

    t = val[CHIP_P_CRUSH];
    c->crush_on = t < 1015808;                          /* bits < 15.5 */
    {
        uint32_t o = (uint32_t)clampi(t - 65536, 0, 14 * 65536 + 65535) << 8;   /* bits - 1, Q24 octaves */
        c->crush16 = (int32_t)(chip_exp2_30(o & 0xFFFFFFu) >> (14 - (o >> 24)));
        c->crush_inv = udiv64_32(1u << 14, 0, (uint32_t)c->crush16);
    }
    c->decim = (uint8_t)clampi((val[CHIP_P_DECIM] + 32768) >> 16, 1, 32);

    t = val[CHIP_P_RES];
    c->q30 = clampi(CHIP_ONE - (int32_t)(((int64_t)t * 912680550) >> 16), 128849019, CHIP_ONE);
    c->res_on = t > 1310;
    c->l0 = log2_q16((uint32_t)maxi(val[CHIP_P_CUT], 65536)) - (16 << 16);
    t = clampi(c->l0, CHIP_L_40, CHIP_L_18000);
    c->f_on0 = t < (int32_t)CHIP_L_17500 || c->res_on;
    c->f0 = chip_svf_f(t);
    c->fenv16 = val[CHIP_P_FENV] * 4;
    c->has_fenv = absi(val[CHIP_P_FENV]) > 6;
    c->vol16 = val[CHIP_P_VOL];
}

static void chip_reset(void *vp)
{
    chip_v_t *v = (chip_v_t *)vp;
    static const chip_v_t zero;
    *v = zero;
    v->lfsr = 0x7FFF;
    v->gcur = v->gtgt = 60 << 24;
    v->note16 = -0x7FFFFFFF - 1;   /* (no pitch yet) */
}

/* the original's onNoteOn(note, isLegatoStep) */
static void chip_start(chip_v_t *v, const chip_ck_t *c, int32_t note16, int legato)
{
    int32_t tgt = note16 << 8;
    if (!v->played || !c->glide_on || (c->mode == CHIP_LEGATO && !legato))
        v->gcur = tgt;
    v->gtgt = tgt;
    v->played = 1;
    if (!legato || v->stage == CHIP_IDLE || v->stage == CHIP_REL) {
        v->stage = CHIP_ATT;
        v->age = 0;
        v->arp_step = v->arp_cnt = 0;
        v->lfsr = 0x7FFF;
    }
}

static void chip_drop(chip_v_t *v, int32_t note16)
{
    int i;
    for (i = 0; i < v->nheld; i++)
        if (v->held[i] == note16) {
            for (; i + 1 < v->nheld; i++)
                v->held[i] = v->held[i + 1];
            v->nheld--;
            return;
        }
}

/* the SDK's CustomSynthVoice note stack (handleNoteOnInternal / handleNoteOffInternal); velocity unused by CHIP */
static void chip_on(void *vp, const void *ckp, int32_t note16, int vel)
{
    chip_v_t *v = (chip_v_t *)vp;
    const chip_ck_t *c = (const chip_ck_t *)ckp;
    int had = v->nheld > 0;
    (void)vel;
    note16 = clampi(note16, 0, 127 << 16);
    chip_drop(v, note16);
    if (v->nheld == CHIP_NHELD)
        chip_drop(v, v->held[0]);
    v->held[v->nheld++] = note16;
    chip_start(v, c, note16, had && c->mode == CHIP_LEGATO);
}

static void chip_off(void *vp, const void *ckp, int32_t note16)
{
    chip_v_t *v = (chip_v_t *)vp;
    const chip_ck_t *c = (const chip_ck_t *)ckp;
    int top;
    note16 = clampi(note16, 0, 127 << 16);
    top = v->nheld && v->held[v->nheld - 1] == note16;
    chip_drop(v, note16);
    if (v->nheld) {
        if (top)
            chip_start(v, c, v->held[v->nheld - 1], c->mode == CHIP_LEGATO);
    } else if (v->stage != CHIP_IDLE)
        v->stage = CHIP_REL;
}

static int chip_render(void *vp, const void *ckp, int32_t *l, int32_t *r)
{
    chip_v_t *v = (chip_v_t *)vp;
    const chip_ck_t *c = (const chip_ck_t *)ckp;
    int i, model = c->model, stage = v->stage;
    int32_t env = v->env, lo = v->lo, bd = v->bd, hold = v->hold, dcnt = v->dcnt;
    int32_t fenv_last = -1, f = c->f0, f_on = c->f_on0, drum = 0;
    uint32_t ph = v->ph, ph2 = v->ph2, ph3 = v->ph3, sub = v->sub, pwm_ph = v->pwm_ph, vib_ph = v->vib_ph;
    uint32_t age = v->age;
    (void)r;

    if (v->blkq == 0)                    /* the original's 128-frame block: an idle block advances the LFOs */
        v->blk_live = stage != CHIP_IDLE;
    v->blkq = (uint8_t)((v->blkq + 1) & 3);
    if (stage == CHIP_IDLE) {
        if (!v->blk_live) {              /* (tickIdle; a block that went idle part-way advances nothing more) */
            v->pwm_ph += c->pwm_inc * ENG_BLK;
            v->vib_ph += c->vib_inc * ENG_BLK;
        }
        for (i = 0; i < ENG_BLK; i++)
            l[i] = 0;
        return 0;
    }
    if (model == CHIP_DRUM) {            /* 2^(-age / (0.08 FS)), Q30 */
        uint32_t o = (uint32_t)(((uint64_t)age * CHIP_KDRUM) >> 16);   /* Q24 octaves */
        uint32_t oi = (o >> 24) + 1u;
        drum = oi >= 31u ? 0 : (int32_t)(chip_exp2_30(0x1000000u - (o & 0xFFFFFFu)) >> oi);
    }

    for (i = 0; i < ENG_BLK; i++) {
        int32_t sig, note16, duty30, lfo30, x;
        uint32_t duty, inc;
        int wrapped;

        switch (stage) {
        case CHIP_ATT:
            if (env >= CHIP_ONE - c->att) {
                env = CHIP_ONE;
                stage = CHIP_DEC;
            } else
                env += c->att;
            break;
        case CHIP_DEC:
            env -= c->dec;
            if (env <= c->sus) {
                env = c->sus;
                stage = c->sus <= CHIP_ENV_THR ? CHIP_IDLE : CHIP_SUS;
            }
            break;
        case CHIP_SUS:
            env = c->sus;
            break;
        case CHIP_REL:
            env -= c->rel;
            if (env <= CHIP_ENV_THR) {
                env = 0;
                stage = CHIP_IDLE;
            }
            break;
        default:
            l[i] = 0;
            continue;
        }

        if (v->gcur != v->gtgt) {        /* glide: 24 semitones over the glide time, linear */
            int32_t d = v->gtgt - v->gcur, a = absi(d);
            if (a > CHIP_GLIDE_THR) {
                int32_t s = a < c->glide24 ? a : c->glide24;
                v->gcur += d > 0 ? s : -s;
            } else
                v->gcur = v->gtgt;
        }
        note16 = (v->gcur >> 8) + c->base16;

        if (c->arp) {
            if (++v->arp_cnt >= c->arp_period) {
                v->arp_cnt = 0;
                v->arp_step = (v->arp_step + 1u) & 3u;
            }
            note16 += CHIP_ARPS[c->arp][v->arp_step] * 65536;
        }

        pwm_ph += c->pwm_inc;           /* triangle, Q30 */
        lfo30 = pwm_ph < 0x80000000u ? (int32_t)pwm_ph - CHIP_ONE : (int32_t)(0xC0000000u - pwm_ph);
        vib_ph += c->vib_inc;
        if (c->has_vib)
            note16 += (int32_t)(((int64_t)sin_ph(vib_ph) * c->vib16) >> 15);

        if (c->has_sweep) {
            uint64_t pr = ((uint64_t)age * c->sweep_inv) >> 7;
            int32_t p30 = pr >= (uint64_t)CHIP_ONE ? CHIP_ONE : (int32_t)pr;
            note16 += (int32_t)(((int64_t)c->sweep16 * p30) >> 30);
            if (model == CHIP_ZAP) {
                int32_t z = CHIP_ONE - p30;
                int32_t z3 = (int32_t)(((int64_t)(int32_t)(((int64_t)z * z) >> 30) * z) >> 30);
                note16 += (int32_t)(((int64_t)c->zap16 * (z3 - CHIP_ONE)) >> 30);
            }
        }

        if (note16 != v->note16)
            chip_pitch(v, note16);
        inc = v->inc;

        duty30 = c->duty30 + (int32_t)(((int64_t)c->pwm30 * lfo30) >> 30);
        duty30 = clampi(duty30, 64424509, 1009317315);   /* 0.06 .. 0.94 */
        duty = (uint32_t)duty30 << 2;

        ph += inc;
        wrapped = ph < inc;

        switch (model) {
        default: {                       /* 2A03 pulse, zap, arp chord */
            int32_t p = ph < duty ? 32768 : -32768;
            p += chip_blep(ph, inc, v->rdt) - chip_blep(ph - duty, inc, v->rdt);
            sig = (p * 3) >> 2;
            break;
        }
        case CHIP_SID: {
            int32_t saw = (int32_t)(ph >> 16) - 32768 - chip_blep(ph, inc, v->rdt);
            int32_t tri = ph < 0x80000000u ? (int32_t)(ph >> 15) - 32768 : 98304 - (int32_t)(ph >> 15);
            int32_t ring = sub < 0x80000000u ? saw : -saw;
            int64_t w = (int64_t)(CHIP_ONE - duty30) * tri +
                        (int64_t)duty30 * (int32_t)(((int64_t)saw * 1395864371 + (int64_t)ring * 751619277) >> 31);
            sig = (int32_t)(((w >> 30) * 858993459) >> 30);
            break;
        }
        case CHIP_NOISE: {
            uint64_t acc = (uint64_t)v->lfsr_ph + (((uint64_t)inc * c->lfsr16) >> 16);
            uint32_t n = (uint32_t)(acc >> 32);
            v->lfsr_ph = (uint32_t)acc;
            while (n--)
                v->lfsr = chip_lfsr(v->lfsr, c->lfsr_short);
            sig = (v->lfsr & 1u) ? 26214 : -26214;
            break;
        }
        case CHIP_FM: {
            int32_t idx16 = (int32_t)(((int64_t)c->fm16 * env) >> 30), ms, raw, a, n;
            ph2 += (uint32_t)(((uint64_t)inc * c->ratio2) >> 1);
            ms = sin_ph(ph2 + (uint32_t)(int32_t)(((int64_t)lfo30 * 2576980378u) >> 32));   /* + 0.15 lfo */
            raw = sin_ph(ph + (uint32_t)((int64_t)idx16 * ms));                             /* + idx ms / 2 */
            a = absi(raw);
            n = (a * 31 + 16384) >> 15;
            sig = CHIP_FM_LV[31 + (raw < 0 ? -n : n)];
            break;
        }
        case CHIP_SUPER: {
            uint32_t inc2 = (uint32_t)(((uint64_t)inc * c->m2) >> 30), inc3 = (uint32_t)(((uint64_t)inc * c->m3) >> 30);
            uint32_t rdt2 = (uint32_t)(((uint64_t)v->rdt * c->r2) >> 30), rdt3 = (uint32_t)(((uint64_t)v->rdt * c->r3) >> 30);
            int32_t p1, p2, p3;
            ph2 += inc2;
            ph3 += inc3;
            p1 = (ph < duty ? 32768 : -32768) + chip_blep(ph, inc, v->rdt) - chip_blep(ph - duty, inc, v->rdt);
            p2 = (ph2 < duty ? 32768 : -32768) + chip_blep(ph2, inc2, rdt2) - chip_blep(ph2 - duty, inc2, rdt2);
            p3 = (ph3 < duty ? 32768 : -32768) + chip_blep(ph3, inc3, rdt3) - chip_blep(ph3 - duty, inc3, rdt3);
            sig = (int32_t)(((int64_t)p1 * 365072220 + (int64_t)(p2 + p3) * 292057776) >> 30);
            break;
        }
        case CHIP_DRUM: {
            int32_t d2 = (int32_t)(((int64_t)drum * drum) >> 30), mix, body, noise;
            uint32_t f28 = (1u << 28) + (uint32_t)(((int64_t)c->drum16 * d2) >> 18);
            drum = (int32_t)(((int64_t)drum * CHIP_DRUM_STEP) >> 30);
            ph2 += (uint32_t)(((uint64_t)inc * f28) >> 28);
            if (wrapped || !(age & 1u))
                v->lfsr = chip_lfsr(v->lfsr, c->drum_metal);
            body = chip_tri_step(ph2);
            noise = (v->lfsr & 1u) ? 27853 : -27853;
            mix = age < 180u ? 22938 : c->drum_mix;
            sig = ((32768 - mix) * body + mix * noise) >> 15;
            break;
        }
        }

        if (wrapped)                     /* sub phase = (cycles / 2) mod 1 */
            v->sub_odd ^= 1u;
        sub = (ph >> 1) | ((uint32_t)v->sub_odd << 31);
        if (c->has_sub)
            sig = (sig * c->sub_main + chip_tri_step(sub) * c->sub_gain) >> 15;

        if (++dcnt >= c->decim) {
            dcnt = 0;
            if (c->crush_on) {
                int64_t q = (int64_t)sig * c->crush16;     /* x scale, Q31 */
                int32_t n = q >= 0 ? (int32_t)((q + (1 << 30)) >> 31) : -(int32_t)((-q + (1 << 30)) >> 31);
                hold = (int32_t)(((int64_t)n * c->crush_inv) >> 15);
            } else
                hold = sig;
        }
        x = hold << 13;                  /* Q28 */

        if (c->has_fenv && env != fenv_last) {
            int32_t lc = c->l0 + (int32_t)(((int64_t)c->fenv16 * env) >> 30);
            fenv_last = env;
            lc = clampi(lc, CHIP_L_40, CHIP_L_18000);
            f_on = lc < (int32_t)CHIP_L_17500 || c->res_on;
            if (f_on)
                f = chip_svf_f(lc);
        }
        if (f_on) {
            int32_t hp;
            lo += (int32_t)(((int64_t)f * bd) >> 30);
            hp = x - lo - (int32_t)(((int64_t)c->q30 * bd) >> 30);
            bd += (int32_t)(((int64_t)f * hp) >> 30);
            bd = clampi(bd, -(1 << 29), 1 << 29);
            lo = clampi(lo, -(1 << 29), 1 << 29);
            x = lo;
        }
        l[i] = (int32_t)(((int64_t)x * (((int64_t)env * c->vol16) >> 17)) >> 42);
        age++;
    }

    v->stage = (uint8_t)stage;
    v->env = env;
    v->lo = lo;
    v->bd = bd;
    v->hold = hold;
    v->dcnt = dcnt;
    v->ph = ph;
    v->ph2 = ph2;
    v->ph3 = ph3;
    v->sub = sub;
    v->pwm_ph = pwm_ph;
    v->vib_ph = vib_ph;
    v->age = age;
    return 1;
}

const eng_t ENG_CHIP = {
    "CHIP", "CHIPTUNE", CHIP_NPAR, 0, 5, 6,
    CHIP_PAR, CHIP_SECTION, CHIP_MACRO, CHIP_ENUMS,
    sizeof(chip_v_t), sizeof(chip_ck_t),
    chip_init, chip_cook, chip_reset, chip_on, chip_off, chip_render,
};
