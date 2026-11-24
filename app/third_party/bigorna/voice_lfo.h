/* SPDX-License-Identifier: GPL-3.0-only
 * Bigorna LFO, adapted to private voice state; original cadence and rate laws. */
#define LFO_K 664862u                            /* (2^32 x CTL / (FS x 307200), x 65536: a block's phase, the rate's
                                                  * BPM x 10 x |SPEED| x MULT apart) */
typedef char lfo_k_ok[CTL == 32 && FS == 44100 ? 1 : -1];
typedef struct {
    uint32_t ph[NLFO], run[NLFO], go[NLFO];      /* the phase; how far a ONE or HALF has run; blocks since a note */
    int32_t live[NLFO], held[NLFO], tgt[NLFO];   /* the wave now; HOLD's value; RND's next */
    uint8_t stop[NLFO];
    uint32_t cooked;                             /* lfo_flips[t] + 1 when its copy was cooked (0: none) */
} lfo_st_t;
static int lfo_on(const lfo_t *l) { return l->dest && l->dest < LFO_ND && l->dep != 64; }
static int32_t lfo_wave(uint32_t w, uint32_t ph)  /* Q15 */
{
    uint32_t x;
    switch (w) {
    case LW_SINE:
        return sin_ph(ph);
    case LW_SQR:
        return ph < 0x80000000u ? 32767 : -32768;
    case LW_SAW:                                 /* (+1 down to -1) */
        return 32767 - (int32_t)(ph >> 16);
    case LW_EXP:                                 /* (1 down to 0, (1 - x)^3) */
        x = 32767u - (ph >> 17);
        return (int32_t)((x * x >> 15) * x >> 15);
    case LW_RAMP:                                /* (0 up to 1) */
        return (int32_t)(ph >> 17);
    default:                                     /* TRI: from 0 up, as SINE */
        x = (ph + 0x40000000u) >> 17;
        return x < 16384u ? (int32_t)(x * 4u) - 32768 : (x == 16384u ? 32767 : 98304 - (int32_t)(x * 4u));
    }
}
#define LFO_EVERY 4u
#define SEQ_16TH (FS * 15u * 10u)
static int port_lfo_step(const sound_t *s,lfo_st_t *f,int32_t *mod,uint32_t *random,int trig){
    int32_t sm[NLFO] = {0, 0, 0}, dm[NLFO] = {0, 0, 0}, n;
    uint32_t any = 0;
    for (n = NLFO - 1; n >= 0; n--) {            /* (LFO 3 first: it may move LFO 1's or 2's SPEED or DEPTH) */
        const lfo_t *l = &s->lfo[n];
        int32_t spd = (int32_t)l->spd - 64 + sm[n], dep = clampi((int32_t)l->dep - 64 + dm[n], -64, 64), o, fq = Q15;
        uint32_t mult = 1u << (l->mult % 12u), bpm10 = l->mult < 12u ? (uint32_t)clampi(eng_bpm10, 100, 3000) : 1200u, inc, p0;
        mod[n] = 0;
        if (!lfo_on(l) && !(sm[n] || dm[n]))
            continue;
        spd = clampi(spd, -64, 64);
        {
            uint64_t i64 = (((uint64_t)bpm10 * (uint32_t)absi(spd) * mult * LFO_K) >> 16) * LFO_EVERY;
            inc = i64 > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)i64;   /* (faster than a cycle in two: as good as) */
        }
        if (trig) {
            f->go[n] = 0;
            if (l->mode == LM_HOLD)
                f->held[n] = f->live[n];
            else if (l->mode != LM_FREE) {
                f->ph[n] = (uint32_t)l->sph << 25;
                f->run[n] = 0;
                f->stop[n] = 0;
            }
        }
        p0 = f->ph[n];
        if (l->mode != LM_ONE && l->mode != LM_HALF)
            f->stop[n] = 0;                      /* (stopped by ONE or HALF, then another MODE: on again) */
        if (!f->stop[n]) {
            f->ph[n] += spd >= 0 ? inc : (uint32_t)-inc;
            if ((l->mode == LM_ONE && f->run[n] + inc < f->run[n]) ||
                (l->mode == LM_HALF && f->run[n] < 0x80000000u && f->run[n] + inc >= 0x80000000u))
                f->stop[n] = 1;                  /* (ONE: a cycle run; HALF: half) */
            f->run[n] += inc;
        }
        if (l->wave == LW_RND) {                 /* (a new value each cycle; START PHASE its slew) */
            if ((spd >= 0 ? f->ph[n] < p0 : f->ph[n] > p0) || trig) {
                *random = *random * 1664525u + 1013904223u;
                f->tgt[n] = (int32_t)(*random >> 16) - 32768;
            }
            f->live[n] += l->sph >= 48u ? (f->tgt[n] - f->live[n]) >> (l->sph / 16u - 1u) : f->tgt[n] - f->live[n];
        } else
            f->live[n] = lfo_wave(l->wave, f->ph[n]);
        o = l->mode == LM_HOLD ? f->held[n] : f->live[n];
        if (l->fade != 64) {                     /* (from the last note, over |FADE| 16ths) */
            uint32_t len = (uint32_t)absi((int32_t)l->fade - 64) * (SEQ_16TH / CTL / LFO_EVERY) / bpm10 + 1u;
            uint32_t g = f->go[n] < len ? f->go[n] : len;
            fq = (int32_t)(g * (uint32_t)Q15 / len);
            if (l->fade > 64)
                fq = Q15 - fq;
            if (f->go[n] < 0xFFFFFFF0u)
                f->go[n]++;
        }
        o = (o * fq >> 15) * dep / 64;           /* (32767 x 32767 < 2^31: no 64 bits) */
        if (l->dest >= LFO_D_LFO) {              /* (LFO 1's or 2's SPEED or DEPTH, for a later LFO) */
            uint32_t m = (l->dest - LFO_D_LFO) / 2u;
            if ((int32_t)m < n) {
                if ((l->dest - LFO_D_LFO) & 1u)
                    dm[m] += o * 64 / 32768;
                else
                    sm[m] += o * 64 / 32768;
            }
        } else if (l->dest && l->dest < LFO_D_LFO) {   /* (on, or its DEPTH moved by a later LFO) */
            mod[n] = o;
            any = 1;
        }
    }
    return any;
}
