/* SPDX-License-Identifier: GPL-3.0-only
 * FIBER: the engine -- a two-oscillator wavetable voice, written to ~/Wavetable-research/REPORT.md (the static
 * analysis of an eight-voice wavetable instrument's firmware). The report's evidence classes are kept in the
 * comments: [B] bytes or traced code, [D] documented, [I] interpretation, [C] a candidate written here where the
 * report recovered nothing exact.
 *
 * A voice:
 *   two oscillators, each on a table (fiber_wt.c) at a position 0 .. 63, playing a 128-sample signed wave by the top
 *   seven bits of a 32-bit phase, no interpolation [B 8]; a new position is taken only when the phase wraps [B/I 8]:
 *   the next wave is expanded at the control rate into the oscillator's second buffer and swapped in at the wrap;
 *   the oscillators run on a virtual clock of 250 kHz [B 9]: 0x05ab3f48 virtual samples a host sample (Q24), the
 *   samples of 16 host samples made at once into a 128-entry circular history;
 *   the mix  g1 s1 + g2 s2 + gr s1 s2  [B 10] (gr from DIRT's ring modulation), and DIRT's noise processes on the
 *   multiply-add generator  s = 0x41c64ed1 s + 0x329d  [B 10] (the processes themselves [C]);
 *   a polyphase FIR from the history to 44.1 kHz [B 9]: the accumulator's high byte the history's advance, bits
 *   17 .. 23 one of 128 phases; 12 taps (or 8: the coarser bank); coefficients [C] (gen_fiber.py);
 *   a DAC stage [C]: each voice's own small offset, gain and curvature;
 *   the filter section [D 11]: low-pass or high-pass, 12 or 24 dB, or DRIVE (PNP, TUBE, PICKUP, DIODE, CRUNCH:
 *   the cutoff and its modulations setting the drive), or off. The filter is a candidate [C]: a four-state ladder
 *   of one-pole stages with a tanh at its input and resonance feedback, self-oscillating above 0.8;
 *   three ADSRs (wave, filter, amplifier) and two LFOs (sine, triangle, pulse, saw up, saw down, random, sample
 *   and hold; each running per voice or one for the track) [D 12]; envelope laws [C] (RC segments);
 *   an eight-slot modulation matrix with the recovered accumulation law [B 12]:
 *       amount = 2 stored - 1;  if the destination is curved: amount = amount |amount|;
 *       acc[dst] = clamp(acc[dst] + src amount, -100, 100)   (saturating at each slot, in slot order);
 *   FLAVOUR [D 13]: small per-note variations of the wave positions and of OSC 1's pitch, the pitch's scaled by the
 *   bend range (0: none); the generator [C].
 * Control values once a block of CTL (32) samples; the amplifier interpolated across it. */

#include "fiber_tables.h"
#include "fiber_wtgen.h"
#include "fiber_wt.c"

/* ------------------------------------------------------------ the patch --- */
enum { FT_LP24, FT_LP12, FT_HP24, FT_HP12, FT_DRIVE, FT_OFF, FT_N };
static const char *const FT_NAME[FT_N] = {"LP 24", "LP 12", "HP 24", "HP 12", "DRIVE", "OFF"};
enum { DC_PNP, DC_TUBE, DC_PICKUP, DC_DIODE, DC_CRUNCH, DC_N };
static const char *const DC_NAME[DC_N] = {"PNP", "TUBE", "PICKUP", "DIODE", "CRUNCH"};
enum { DT_OFF, DT_STATIC, DT_CRACKLE, DT_GEIGER, DT_CLICK, DT_BURST1, DT_BURST2, DT_BURST3, DT_RING, DT_N };
static const char *const DT_NAME[DT_N] = {"OFF", "STATIC", "CRACKLE", "GEIGER", "CLICK", "BURST 1", "BURST 2", "BURST 3",
                                          "RING MOD"};
enum { FBL_SINE, FBL_TRI, FBL_PULSE, FBL_SAWUP, FBL_SAWDN, FBL_RANDOM, FBL_SH, FBL_N };
enum { VM_POLY, VM_MONO, VM_LEGATO };
/* each oscillator's kind: the wavetable (the analysis's); VA, after the Lines engine of a dual-engine string and pad
 * synthesizer (Crema-research/REPORT.md 9: a saw / pulse oscillator whose shape runs saw -> square -> narrow pulse, the
 * position as the shape); STRINGS, after its Texture strings (REPORT.md 5: a divide-down bank, saw 8', square 8',
 * square 4', square 16', its registration -- violin, viola, cello, brass, organ, choir -- as the position, with
 * formants after the resampler). Both [C]: the report documents what they do, not how */
enum { OM_WAVE, OM_VA, OM_STR, OM_N };
/* the track's ensemble (Crema REPORT.md 8 [D]: three delay lines, their modulation speeds and phases apart): STRINGS
 * (two LFOs, slow and quick), STR+CH (the same and a chorus), CHORUS (one LFO). layers.c runs it, on the track */
enum { EM_STRINGS, EM_STRCH, EM_CHORUS, EM_N };

/* the matrix's sources [D 12; the manual's table, pp. 56-57]: values Q15, unipolar 0 .. 1 or bipolar */
enum { FBS_NONE, FBS_E1, FBS_E2, FBS_E3, FBS_L1, FBS_L1MW, FBS_L1AT, FBS_L2, FBS_L2MW, FBS_L2AT, FBS_MW, FBS_BEND, FBS_NOTEPB, FBS_AT,
       FBS_POLYAT, FBS_MPEY, FBS_PEDAL, FBS_EXPR, FBS_BREATH, FBS_CC22, FBS_KEY = FBS_CC22 + 10, FBS_VEL, FBS_RVEL, FBS_RUNI, FBS_RBI,
       FBS_CONST, FBS_N };
/* the track's MIDI controllers the matrix reads, Q15 (layers.c sets them): wheel, bend, per-note bend, channel and poly
 * pressure, MPE Y, pedal, expression, breath, CC 22 .. 31 */
enum { FC_MW, FC_BEND, FC_NOTEPB, FC_AT, FC_POLYAT, FC_MPEY, FC_PEDAL, FC_EXPR, FC_BREATH, FC_CC22, FC_N = FC_CC22 + 10 };
/* .. its targets, 1 .. 26 (0: the slot off) [B: the range check against 26; D: the manual's list] */
enum { FBD_NONE, FBD_POS1, FBD_POS2, FBD_PITCH, FBD_PITCH1, FBD_PITCH2, FBD_LVL1, FBD_LVL2, FBD_DIRT, FBD_CUT, FBD_RESO, FBD_VOL,
       FBD_PANV, FBD_L1SPD, FBD_L2SPD, FBD_E1A, FBD_E1D, FBD_E1S, FBD_E1R, FBD_E2A, FBD_E2D, FBD_E2S, FBD_E2R, FBD_E3A, FBD_E3D, FBD_E3S,
       FBD_E3R, FB_NDST };
typedef char fb_26_dst[FB_NDST == 27 ? 1 : -1];
/* the targets taking the curved amount (amount |amount|: fine near zero) [I: a flag of the target's state; which
 * ones have it, not recovered -- here the pitches, the LFO speeds and the envelope times] */
static const uint8_t FB_CURVED[FB_NDST] = {0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1};
#define FB_NSLOT 8u
#define FB_VIB_MAX 133           /* a quarter semitone in the matrix's units (100 % = 48 semitones = 25600) */
typedef struct { uint8_t src, dst; uint16_t amt; } fb_slot_t;   /* amt: stored, 0 .. 32768 (16384: none) */

typedef struct {
    uint32_t seed;               /* what made it: fb_design(kind, seed, path, gen) + fb_measure give it again */
    uint8_t kind, path, gen, mode;   /* mode: VM_* */
    uint8_t tab[2];              /* each oscillator's table, 0 .. 87 */
    uint8_t ftype, dcurve, dirt, limit;   /* the filter section; DIRT; LIMIT WAVES (61 .. 63 kept out of scans) */
    uint8_t rs;                  /* the resampler: 0 the 12-tap bank, 1 the 8-tap one */
    uint8_t lshape[2], lglob[2], lsync[2];   /* the LFOs: shape; one for the track; restarted at each note */
    uint8_t bend;                /* the bend range, semitones (FLAVOUR's pitch scaled by it) */
    uint8_t om[2];               /* each oscillator's kind: OM_* */
    uint8_t perc;                /* a drum: each note from silence -- phases, envelopes and filter at their start */
    uint8_t fixp;                /* a fixed pitch (a MIDI note) whatever key plays it; 0: the key's */
    uint8_t emode;               /* the ensemble's mode, EM_* */
    int16_t ens;                 /* the ensemble's amount on the track, Q15 (0: none) */
    fb_slot_t slot[FB_NSLOT];
    int16_t pos[2];              /* the positions, Q8 (0 .. 63 << 8) */
    int16_t wenv[2];             /* the wave envelope on each, Q8 positions at full */
    int16_t wvel;                /* the velocity's share of the wave envelope's depth, Q15 */
    int16_t semi[2];             /* each oscillator's pitch, 1/16 semitone */
    int16_t det[2];              /* .. detune, 1/256 semitone */
    int16_t lvl[2];              /* the levels, Q8 (256 = full) */
    int16_t damt;                /* DIRT's amount, Q8 */
    int16_t flav;                /* FLAVOUR, Q15 */
    int32_t cut;                 /* the cutoff, 1/16 semitone (MIDI note x 16) */
    int32_t reso;                /* Q15: 0.8 and above self-oscillates [D 11] */
    int32_t fenv, fvel;          /* the filter envelope's depth; the velocity's, 1/16 semitone */
    int32_t keytrk;              /* Q15 around MIDI note 60 [D 11]; negative: the other way */
    int32_t ea[3], ed[3], es[3], er[3];   /* the envelopes (wave, filter, amplifier): ms, sustain Q15 */
    int32_t lhz[2];              /* the LFOs' rates, Hz Q16 */
    int32_t trans;               /* a kick's transient (build 12, where the never-used glide was): 0 none, else bits
                                  * 0-7 its FB_TRANS bank + 1, bits 8-23 its level (Q15) -- added after the filter */
    int32_t vol, vvel;           /* the voice's level, Q15; the velocity's share of it, Q15 */
    int32_t pan;                 /* Q15, 0 left .. 32768 right (the track's own; the layer mixes by it) */
    int32_t out;                 /* Q12: the measured level brought to the target */
} fb_design_t;

/* the macros (FIBER build 25: EDIT's eight, none touching the pitch), each -MAC_MAX .. MAC_MAX, 0 as drawn:
 * BRIGHTNESS the cutoff (24 semitones either way; on DRIVE, the drive), BITE the resonance, TIMBRE both oscillators'
 * wave positions (32 either way), MOTION every matrix amount and the wave envelope's depth (x 0 .. 2), and up,
 * LFO 1 on the positions (12 at the most), DIRT DIRT's amount (with none: the
 * oscillators' ring modulation, up), ATTACK the envelopes' attacks (4 octaves of time either way), LENGTH their
 * decays and releases (the same), BALANCE the oscillators' levels (left OSC 1 alone, right OSC 2 alone) */
enum { MC_BRIGHT, MC_BITE, MC_TIMBRE, MC_MOTION, MC_DIRT, MC_ATTACK, MC_LENGTH, MC_BALANCE };
typedef char mc_fits[NMAC == 8u ? 1 : -1];
/* ------------------------------------------------------------ the voice --- */
typedef struct {
    uint8_t live, gate, hold, legato;
    uint8_t st[3];               /* the envelopes' stages: 0 idle, 1 attack, 2 decay / sustain, 3 release */
    uint8_t ws[2];               /* each oscillator's playing buffer (0 / 1) */
    uint8_t act[2], nxt[2], pend[2];   /* the position playing; the one prepared; a swap waiting for the wrap */
    uint8_t hw, rp;              /* the history's write and read places (mod 128) */
    uint8_t burst, click;        /* DIRT: a burst's groups left; the click's virtual samples left (/ 4) */
    uint16_t tp;                 /* the transient's next sample (FB_TRL: over) */
    int32_t tg;                  /* .. its gain (Q15: the level, the velocity's share, the voice's level) */
    int8_t mac[NMAC];
    rnd_t r;
    uint32_t lcg;                /* DIRT's generator [B 10] */
    uint32_t gacc, racc;         /* the virtual clock: generation and read accumulators, Q24 fraction */
    uint32_t ph[2], inc[2];      /* the oscillators */
    int32_t g1, g2, gr, dn;      /* this block's mix (Q8) and DIRT's noise level (Q8) */
    int32_t n16, n16t;           /* the pitch (1/16 semitone, Q8) and where it glides to */
    int32_t vel, rvel;           /* Q15 */
    int32_t env[3];              /* Q24 */
    int32_t z[4], y4;            /* the ladder's states; its last output */
    int32_t G, k, gc;            /* this block's one-pole gain (Q24), feedback (Q12), makeup (Q12) */
    int32_t drv, dmk;            /* DRIVE: this block's gain; its makeup for the DRIVE macro (Q12 both) */
    uint32_t lph[2];             /* the voices' own LFO phases */
    int32_t la[2], lb[2];        /* the random shapes' last and next values */
    int32_t runi, rbi;           /* this note's random sources */
    int32_t fpos, fpit, drift;   /* FLAVOUR: the positions' offset (Q8), OSC 1's (1/256 semitone), a slow walk */
    int32_t dac_off, dac_g, dac_c;   /* the DAC [C]: offset (Q15), gain (Q15), curvature (Q15) */
    int32_t amp0, amp1;          /* Q15 */
    int32_t dc;                  /* the DC blocker's state, Q15 << 10 (the drive curves' and the waves' own offsets) */
    uint32_t blk;
    /* VA and STRINGS: this block's shape (VA: saw -> pulse blend Q7, the pulse's width as a phase) or registration
     * (STRINGS: the four footages' gains Q7), the 16' square's flip-flop; the formants (two band-passes) */
    int32_t sg[2][4];
    uint32_t pw[2];
    uint8_t sub[2], fmon, rsv0;
    int32_t fk[2];               /* the formants' gains, Q12 */
    svf_t fs[2];
    svf_c fc[2];
    int8_t wb[2][2][128];        /* each oscillator's two playback waves */
    int16_t hist[128];           /* the virtual-clock history */
} fb_voice_t;

/* the track's controllers (MIDI), Q15; the block clock for the track-wide LFOs (layers.c sets them) */
static volatile int16_t fb_cc[6][FC_N];
static volatile uint32_t fb_clock;

/* ------------------------------------------------------------- helpers --- */
/* an RC step a block for a time of ms (as OBRADOR's): Q24 */
AINL int32_t fb_k(int32_t ms, int32_t tcs8)
{
    int32_t n = clampi(ms * 1378 / 1000, 1, 1 << 20);
    uint32_t k = (16777216u / (uint32_t)n) * (uint32_t)tcs8 >> 3;
    return (int32_t)(k > 16777216u ? 16777216u : k);
}
/* a cutoff (1/16 semitone, MIDI x 16) in Hz, 16 Hz .. 0.45 FS */
AINL int32_t fb_hz(int32_t c16)
{
    c16 = clampi(c16, 0, 2047 + 16 * 14);
    return (int32_t)(((uint64_t)exp2_q16((c16 - 69 * 16) * 1024 / 3) * 440u) >> 16);
}
AINL int32_t fb_ladg(int32_t hz)   /* the ladder's one-pole gain, Q24 */
{
    uint32_t x, i, f8;
    hz = clampi(hz, 16, TANG_FMAX);
    x = (uint32_t)hz * 512u;
    i = x / TANG_FMAX;
    if (i >= 512u)
        return (int32_t)FB_LADG[512];
    f8 = (x - i * TANG_FMAX) * 256u / TANG_FMAX;
    return (int32_t)(FB_LADG[i] + (((FB_LADG[i + 1u] - FB_LADG[i]) * f8) >> 8));
}
/* the phase step at 250 kHz of a pitch in 1/256 semitone */
AINL uint32_t fb_inc(int32_t p256)
{
    int32_t p = clampi(p256, 0, 2047 * 16), i = p >> 4, f = p & 15;
    uint32_t a = pitch_inc(i), b = pitch_inc(i + 1);
    uint32_t h = a + (uint32_t)(((uint64_t)(b - a) * (uint32_t)f) >> 4);
    return (uint32_t)(((uint64_t)h * FB_VINC_K) >> 32);
}
/* a drive curve at x (Q15, any range: -4 .. 4 used), Q15 */
AINL int32_t fb_curve(const int16_t *c, int32_t x)
{
    int32_t u = clampi(x + 4 * Q15, 0, 8 * Q15 - 1), i = u >> 8, f = u & 255;
    return c[i] + (((c[i + 1] - c[i]) * f) >> 8);
}
AINL uint32_t fb_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

/* an LFO's shape at phase ph; a, b: the random shapes' last and next values (Q15) */
AINL int32_t fb_shape(uint32_t sh, uint32_t ph, int32_t a, int32_t b)
{
    switch (sh) {
    case FBL_SINE:
        return sin_ph(ph);
    case FBL_TRI: {
        int32_t p = (int32_t)((ph + 0x40000000u) >> 15);
        return p < Q16 ? p - Q15 : 98303 - p;
    }
    case FBL_PULSE:
        return ph < 0x80000000u ? 32767 : -32767;
    case FBL_SAWUP:
        return (int32_t)(ph >> 16) - Q15;
    case FBL_SAWDN:
        return Q15 - 1 - (int32_t)(ph >> 16);
    case FBL_SH:
        return b;
    default:                                       /* RANDOM: a line from the last value to the next */
        return a + (int32_t)(((int64_t)(b - a) * (ph >> 16)) >> 16);
    }
}

/* ------------------------------------------------------------ the voice --- */
static void fb_init(fb_voice_t *v, uint32_t seed)
{
    memset(v, 0, sizeof *v);
    rnd_seed(&v->r, seed);
    v->lcg = seed * 2654435761u + 1u;
}
AINL int32_t fb_maxpos(const fb_design_t *d) { return (d->limit ? 60 : 63) << 8; }

/* a drum's note (build 12): the scale's root (pitch class root, 0 .. 11) near its own pitch (fixp), so it sits in
 * the key of the tracks around it and in its own register, moved by the track's octave; any key plays it */
AINL int32_t fb_drum_note(int32_t fixp, int32_t root, int32_t oct)
{
    int32_t p = fixp - (((fixp - root) % 12 + 12) % 12);   /* (the root at or below fixp ..) */
    if (fixp - p > 4)
        p += 12;                                   /* (.. or above it: fixp - 4 .. fixp + 7, leaning up -- a kick is
                                                    * not pushed down into the sub) */
    return clampi(p + 12 * oct, 12, 120);
}
/* dnote: a drum's note as the track has it (fb_drum_note; 0: its own pitch, as the probes play it) */
static void fb_note_on(fb_voice_t *v, const fb_design_t *d, int32_t note, int32_t vel127, int fresh, int legato,
                       int32_t dnote)
{
    uint32_t o;
    int32_t bend = d->bend > 24 ? 24 : d->bend;
    if (d->fixp)                                   /* (a drum: one note, any key) */
        note = dnote ? dnote : d->fixp;
    if (d->perc && !legato)                        /* (a drum: from silence each time, the same attack each hit) */
        fresh = 1;
    if (fresh || !v->live) {
        rnd_t r = v->r;
        uint32_t lcg = v->lcg;
        memset(v, 0, __builtin_offsetof(fb_voice_t, wb));
        v->r = r;
        v->lcg = lcg;
        for (o = 0; o < 2u; o++) {                 /* the waves at the start positions, at once */
            int32_t p = clampi(d->pos[o], 0, fb_maxpos(d)) >> 8;
            uint8_t tmp[FB_HW];
            fb_expand(v->wb[o][0], fb_frame(d->tab[o], (uint32_t)p, tmp));
            v->act[o] = v->nxt[o] = (uint8_t)p;
            v->ph[o] = d->perc ? 0u : rnd_next(&v->r);   /* (free-running: anywhere [C]; a drum's from 0) */
        }
        memset(v->hist, 0, sizeof v->hist);
        v->dac_off = rnd_bi(&v->r) >> 8;           /* (+- 0.4 %) */
        v->dac_g = Q15 + (rnd_bi(&v->r) >> 5);     /* (+- 3 %, +- 0.3 dB) */
        v->dac_c = rnd_bi(&v->r) >> 4;             /* (+- 6 % second-order) */
        v->drift = 0;
        v->n16 = note * 16 * 256;
        for (o = 0; o < 2u; o++) {
            v->la[o] = rnd_bi(&v->r);
            v->lb[o] = rnd_bi(&v->r);
            v->lph[o] = rnd_next(&v->r);
        }
    }
    if (!legato || !v->live) {                     /* the envelopes from where they are (a voice taken again glides on) */
        v->st[0] = v->st[1] = v->st[2] = 1;
        for (o = 0; o < 2u; o++)
            if (d->lsync[o])
                v->lph[o] = 0;
        v->runi = rnd_q15(&v->r);
        v->rbi = rnd_bi(&v->r);
        /* FLAVOUR [D 13, C]: the positions a little apart, OSC 1's pitch by the bend range's share */
        v->fpos = mulq15(rnd_bi(&v->r), d->flav) >> 6;              /* (+- 2 positions at full, Q8) */
        v->fpit = (mulq15(rnd_bi(&v->r), d->flav) * bend) >> 9;      /* (+- 1.5 cents a semitone of range) */
        v->click = d->dirt == DT_CLICK ? 64u : 0u;
        v->vel = clampi(vel127, 1, 127) * 258;
        v->tp = FB_TRL;
        if (d->trans & 255) {                      /* (a kick's transient, from its start, at the velocity) */
            int32_t vg = Q15 - d->vvel + mulq15(d->vvel, v->vel);
            v->tp = 0;
            v->tg = mulq15(mulq15(vg, d->vol), (int32_t)((uint32_t)d->trans >> 8 & 0xFFFFu));
        }
    }
    v->n16t = note * 16 * 256;
    v->n16 = v->n16t;                              /* (no glide, ever: the pitch goes to the note at once) */
    v->live = v->gate = 1;
    v->legato = (uint8_t)legato;
}
static void fb_note_off(fb_voice_t *v, int32_t rvel127)
{
    uint32_t e;
    v->gate = 0;
    v->rvel = clampi(rvel127, 0, 127) * 258;
    for (e = 0; e < 3u; e++)
        if (v->st[e] && !(e == 2u && v->hold))
            v->st[e] = 3;
}
/* an envelope a block on (Q24): attack aiming at 1.3, decay to the sustain, release to nothing [C] */
AINL void fb_env(int32_t *e, uint8_t *st, int32_t ka, int32_t kd, int32_t s24, int32_t kr)
{
    switch (*st) {
    case 1:
        *e += mul24(16777216 * 13 / 10 - *e, ka);
        if (*e >= 16777216) {
            *e = 16777216;
            *st = 2;
        }
        break;
    case 2:
        *e += mul24(s24 - *e, kd);
        break;
    case 3:
        *e += mul24(-*e, kr) - 16;
        if (*e <= 0) {
            *e = 0;
            *st = 0;
        }
        break;
    default:
        *e = 0;
    }
}
/* a time of ms moved by oct (Q16 octaves), 1 ms .. 30 s */
AINL int32_t fb_ms(int32_t ms, int32_t oct)
{
    if (!oct)
        return ms;
    return clampi((int32_t)(((int64_t)ms * exp2_q16(clampi(oct, -(16 << 16), 14 << 16))) >> 16), 1, 30000);
}

/* STRINGS' registrations (Crema REPORT.md 5 [D]: violin, viola, cello, brass, organ, choir), each the gains of the
 * saw 8', square 8', square 4' and square 16' (Q7, at most 128 together) and two formants (Hz) with their gain (Q12)
 * -- all [C]: the report names the registrations, not their spectra */
static const int16_t FB_REG[6][7] = {
    {96, 0, 32, 0, 600, 2900, 6144},       /* violin: bright, the 4' over the saw */
    {80, 16, 16, 16, 500, 2300, 6144},     /* viola */
    {72, 16, 0, 40, 320, 1400, 6144},      /* cello: the 16' under, lower formants */
    {100, 28, 0, 0, 900, 2500, 4915},      /* brass: the saw and the square, a high first formant */
    {0, 56, 40, 32, 1000, 3000, 0},        /* organ: squares only, no formants */
    {24, 48, 0, 56, 700, 1150, 7373},      /* choir: soft, a vowel's two formants */
};
/* oscillator o's shape for this block at position p (Q8, 0 .. 63): VA's blend and width, STRINGS' footages (and with
 * OSC 1, the formants) */
AINL void fb_shape_osc(fb_voice_t *v, const fb_design_t *d, uint32_t o, int32_t p)
{
    if (d->om[o] == OM_VA) {                       /* 0 .. 31: saw to square; 32 .. 63: the square narrowing to 6 % */
        int32_t half = 32 << 8;
        v->sg[o][0] = p < half ? p * 128 / half : 128;
        v->pw[o] = p < half ? 0x80000000u : 0x80000000u - (uint32_t)(p - half) * (0x70000000u / (31u << 8));
        return;
    }
    {
        int32_t r = p * 5 / 63, i = r >> 8, f = r & 255, k;   /* (the registration: 0 .. 5, Q8) */
        const int16_t *a = FB_REG[i > 4 ? 4 : i], *b = FB_REG[i > 4 ? 5 : i + 1];
        if (i > 4)
            f = 256;
        for (k = 0; k < 4; k++)
            v->sg[o][k] = a[k] + (((b[k] - a[k]) * f) >> 8);
        if (o == 0u) {                             /* (the formants follow OSC 1's registration) */
            for (k = 0; k < 2; k++) {
                int32_t hz = a[4 + k] + (((b[4 + k] - a[4 + k]) * f) >> 8);
                svf_coef(&v->fc[k], hz, 4 * 4096);
            }
            v->fk[0] = a[6] + (((b[6] - a[6]) * f) >> 8);
            v->fk[1] = v->fk[0] * 3 / 4;
        }
    }
}

/* (the wave frame made from the table: the flash's, rarely -- a position newly reached -- through a pointer) */
static __typeof__(&fb_frame) volatile fb_frame_fp = fb_frame;
/* a control block: the matrix, the envelopes, the LFOs, the positions, the pitches, the filter, the amplifier (run
 * from RAM, build 11: its helpers inlined) */
static FAST void fb_control(fb_voice_t *v, const fb_design_t *d, uint32_t l)
{
    int32_t src[FBS_N], acc[FB_NDST], lfo[2], pmov[3] = {0, 0, 0};
    uint32_t s, o;
    int32_t m0 = v->mac[MC_TIMBRE], m1 = v->mac[MC_BRIGHT], m2 = v->mac[MC_BITE], m3 = v->mac[MC_MOTION];
    int32_t m4 = v->mac[MC_DIRT], m5 = v->mac[MC_ATTACK] * 4096, m6 = v->mac[MC_LENGTH] * 4096, m7 = v->mac[MC_BALANCE];
    int32_t e1, e2, e3, x;
    uint32_t probe = l >= 6u, clk = probe ? v->blk : fb_clock;   /* (the measure's probe: its own clock, no MIDI --
                                                                  * a sound measured again gives the same level) */
    static const int16_t NOCC[FC_N];
    const volatile int16_t *cc = probe ? NOCC : fb_cc[l];
    l = l < 6u ? l : 0u;
    /* the LFOs: the voice's own, or the track's (its phase the block clock's: the same in every voice) */
    for (o = 0; o < 2u; o++) {
        uint32_t inc = (uint32_t)(((uint64_t)(uint32_t)d->lhz[o] * 3116558u) >> 16);   /* Hz Q16 x 2^32 / 1378.125 a block */
        if (d->lglob[o]) {
            uint64_t t = (uint64_t)inc * clk;
            uint32_t cyc = (uint32_t)(t >> 32), ph = (uint32_t)t;
            int32_t a = (int32_t)(fb_hash(cyc * 2u + o + l * 8u) >> 16) - Q15;
            int32_t b = (int32_t)(fb_hash((cyc + 1u) * 2u + o + l * 8u) >> 16) - Q15;
            lfo[o] = fb_shape(d->lshape[o], ph, a, b);
        } else
            lfo[o] = fb_shape(d->lshape[o], v->lph[o], v->la[o], v->lb[o]);
    }
    e1 = v->env[0] >> 9;
    e2 = v->env[1] >> 9;
    e3 = v->env[2] >> 9;
    /* the sources */
    src[FBS_NONE] = 0;
    src[FBS_E1] = e1, src[FBS_E2] = e2, src[FBS_E3] = e3;
    src[FBS_L1] = lfo[0], src[FBS_L2] = lfo[1];
    for (o = 0; o < FC_N; o++)                     /* (FBS_MW .. FBS_CC22 + 9 in the order of FC_*) */
        src[FBS_MW + o] = cc[o];
    src[FBS_L1MW] = mulq15(lfo[0], src[FBS_MW]), src[FBS_L2MW] = mulq15(lfo[1], src[FBS_MW]);
    src[FBS_L1AT] = mulq15(lfo[0], src[FBS_AT]), src[FBS_L2AT] = mulq15(lfo[1], src[FBS_AT]);
    src[FBS_KEY] = clampi(((v->n16t >> 8) - 60 * 16) * 32, -Q15, Q15);   /* (+- 64 semitones from C4 = +- 1) */
    src[FBS_VEL] = v->vel, src[FBS_RVEL] = v->rvel;
    src[FBS_RUNI] = v->runi, src[FBS_RBI] = v->rbi, src[FBS_CONST] = Q15;
    /* the matrix [B 12]: the accumulators cleared for this block, each slot in turn, saturating as it adds */
    for (o = 0; o < FB_NDST; o++)
        acc[o] = 0;
    for (s = 0; s < FB_NSLOT; s++) {
        const fb_slot_t *sl = &d->slot[s];
        int32_t a;
        if (!sl->dst || sl->dst >= FB_NDST || sl->src >= FBS_N)
            continue;
        a = 2 * (int32_t)sl->amt - Q15;           /* amount = 2 stored - 1, Q15 */
        a = clampi(a, -Q15, Q15);
        if (m3)                                    /* (MOD DEPTH: the amounts x 0 .. 2) */
            a = clampi(a + a * m3 / MAC_MAX, -Q15, Q15);
        if (FB_CURVED[sl->dst])
            a = (int32_t)(((int64_t)a * absi(a)) >> 15);
        /* source x amount, in hundredths Q8 (100 = 25600) */
        {
            int32_t c = (int32_t)(((int64_t)src[sl->src] * a * 25600) >> 30);
            if (sl->dst >= FBD_PITCH && sl->dst <= FBD_PITCH2 && sl->src != FBS_KEY && sl->src != FBS_VEL &&
                sl->src != FBS_RUNI && sl->src != FBS_RBI && sl->src != FBS_CONST) {
                /* FIBER's rule: no pitch glides, sweeps or sirens. A source that moves during a note (an envelope,
                 * an LFO, a controller) moves a pitch by a quarter semitone at most, all such slots together: a
                 * vibrato. A note's fixed offsets (key, velocity, the random draws, constant) are free */
                pmov[sl->dst - FBD_PITCH] = clampi(pmov[sl->dst - FBD_PITCH] + c, -FB_VIB_MAX, FB_VIB_MAX);
                continue;
            }
            acc[sl->dst] = clampi(acc[sl->dst] + c, -25600, 25600);
        }
    }
    for (o = 0; o < 3u; o++)
        acc[FBD_PITCH + o] = clampi(acc[FBD_PITCH + o] + pmov[o], -25600, 25600);
    /* the envelopes, their times as the matrix moves them */
    for (o = 0; o < 3u; o++) {
        uint32_t b = FBD_E1A + 4u * o;
        int32_t ka = 0, kd = 0, kr = 0, s24;
        if (v->st[o] == 1)
            ka = fb_k(fb_ms(d->ea[o], acc[b] * 512 / 25 + m5), 12);   /* (+- 100: +- 8 octaves; ATTACK +- 4) */
        else if (v->st[o] == 2)
            kd = fb_k(fb_ms(d->ed[o], acc[b + 1u] * 512 / 25 + m6), 24);   /* (LENGTH +- 4) */
        else if (v->st[o] == 3)
            kr = fb_k(fb_ms(d->er[o], acc[b + 3u] * 512 / 25 + m6), 24);
        s24 = clampi(d->es[o] + acc[b + 2u] * 32 / 25, 0, Q15) * 512;
        if (o == 2u && v->hold) {                  /* (the track's own envelope shapes the voice: this one held full) */
            v->env[2] = 16777216;
            continue;
        }
        fb_env(&v->env[o], &v->st[o], ka, kd, s24, kr);
    }
    /* the LFOs on: their speeds as the matrix moves them (+- 100: +- 6 octaves) */
    for (o = 0; o < 2u; o++) {
        int32_t hz = d->lhz[o];
        uint32_t old = v->lph[o];
        if (acc[FBD_L1SPD + o])
            hz = (int32_t)(((int64_t)hz * exp2_q16(acc[FBD_L1SPD + o] * 15)) >> 16);
        v->lph[o] += (uint32_t)(((uint64_t)(uint32_t)clampi(hz, 0, 200 << 16) * 3116558u) >> 16);
        if (v->lph[o] < old) {
            v->la[o] = v->lb[o];
            v->lb[o] = rnd_bi(&v->r);
        }
    }
    /* FLAVOUR's slow walk */
    if (d->flav)
        v->drift += (mulq15(rnd_bi(&v->r), d->flav) >> 6) - (v->drift >> 7);
    /* the positions: the start, the wave envelope (its depth partly the velocity's), the matrix, FLAVOUR, POSITION */
    {
        int32_t wd = Q15 - d->wvel + mulq15(d->wvel, v->vel);
        for (o = 0; o < 2u; o++) {
            int32_t p = d->pos[o] + mulq15(mulq15(e1, d->wenv[o]), wd) * (MAC_MAX + m3) / MAC_MAX +
                        acc[FBD_POS1 + o] * 64 / 100 + v->fpos * (o ? -1 : 1) + (v->drift >> 4) + m0 * 128;
            if (m3 > 0)                            /* (MOTION up: LFO 1 sweeps the positions too, 12 at the most) */
                p += mulq15(lfo[0], m3 * 48) * (o ? -1 : 1);
            if (d->om[o] != OM_WAVE) {             /* VA, STRINGS: the position is the shape, or the registration */
                fb_shape_osc(v, d, o, clampi(p, 0, 63 << 8));
                continue;
            }
            p = clampi(p, 0, fb_maxpos(d)) >> 8;
            if (p != v->act[o] || v->pend[o]) {    /* (a new one: into the buffer not playing; swapped at the wrap) */
                if (!v->pend[o] || p != v->nxt[o]) {
                    if (p == v->act[o])
                        v->pend[o] = 0;            /* (back where it plays: nothing to swap) */
                    else {
                        uint8_t tmp[FB_HW];
                        fb_expand(v->wb[o][v->ws[o] ^ 1u], fb_frame_fp(d->tab[o], (uint32_t)p, tmp));
                        v->nxt[o] = (uint8_t)p;
                        v->pend[o] = 1;
                    }
                }
            }
        }
    }
    /* the pitches */
    {
        int32_t base = (v->n16 >> 4) + (acc[FBD_PITCH] * 48 * 256 / 25600) + (cc[FC_BEND] * (int32_t)d->bend >> 7);
        int32_t p1 = base + d->semi[0] * 16 + d->det[0] + acc[FBD_PITCH1] * 48 * 256 / 25600 + v->fpit;
        int32_t p2 = base + d->semi[1] * 16 + d->det[1] + acc[FBD_PITCH2] * 48 * 256 / 25600;
        v->inc[0] = fb_inc(p1);
        v->inc[1] = fb_inc(p2);
    }
    /* the mix, DIRT */
    v->g1 = clampi(d->lvl[0] + acc[FBD_LVL1] / 100, 0, 256);
    v->g2 = clampi(d->lvl[1] + acc[FBD_LVL2] / 100, 0, 256);
    if (m7 > 0)                                    /* (BALANCE: towards OSC 2 alone) */
        v->g1 = v->g1 * (MAC_MAX - m7) / MAC_MAX;
    else if (m7 < 0)                               /* (.. towards OSC 1 alone) */
        v->g2 = v->g2 * (MAC_MAX + m7) / MAC_MAX;
    x = clampi(d->damt + acc[FBD_DIRT] / 100 + (d->dirt ? m4 * 4 : 0), 0, 256);
    v->gr = d->dirt == DT_RING ? x : !d->dirt && m4 > 0 ? m4 * 2 : 0;   /* (DIRT with no process: ring
                                                                       * modulation, up to half) */
    v->dn = d->dirt != DT_RING && d->dirt ? x : 0;
    /* the filter section: the cutoff (its envelope, the key from C4, the velocity, the matrix), then CUTOFF: 24
     * semitones either way, never down past C2 (65 Hz) unless the sound is there already */
    {
        int32_t cut0 = d->cut + mulq15(e2, d->fenv) + mulq15((v->n16 >> 8) - 60 * 16, d->keytrk) +
                       mulq15(v->vel, d->fvel) + acc[FBD_CUT] * 2 / 25;
        int32_t cut = cut0 + m1 * 6, lo = cut0 < 36 * 16 ? cut0 : 36 * 16;
        int32_t reso = clampi(d->reso + acc[FBD_RESO] * 32 / 25 + m2 * 256, 0, Q15);
        cut = cut < lo ? lo : cut;
        if (d->ftype == FT_DRIVE) {                /* the cutoff sets the drive: x 0.25 at C0 .. x 16 at C8 [D 11, C] */
            int32_t d0 = (int32_t)(exp2_q16(clampi((cut0 - 48 * 16) * 256, -(2 << 16), 4 << 16)) >> 4);
            v->drv = (int32_t)(exp2_q16(clampi((cut - 48 * 16) * 256, -(2 << 16), 4 << 16)) >> 4);
            /* (DRIVE turned up: the level brought back by the square root of the drive added, so it is heard as
             * more drive, not as more level) */
            v->dmk = 4096;
            if (v->drv > d0)
                v->dmk = (int32_t)isqrt32(((uint32_t)d0 << 12) / (uint32_t)v->drv << 12);
        } else if (d->ftype != FT_OFF) {
            v->G = fb_ladg(fb_hz(cut));
            v->k = reso * 5 >> 3;                  /* Q12: 0 .. 5, 4 (self-oscillation) at 0.8 */
            v->gc = 4096 + (v->k >> 2);            /* (some of the level lost to the resonance made up) */
        }
    }
    /* the amplifier: its envelope, the velocity's share, the level, the matrix */
    v->amp0 = v->amp1;
    {
        int32_t vg = Q15 - d->vvel + mulq15(d->vvel, v->vel);
        int32_t a = mulq15(mulq15(e3 > Q15 ? Q15 : e3, vg), clampi(d->vol + acc[FBD_VOL] * 32 / 25, 0, Q15));
        v->amp1 = a;
    }
    v->blk++;
}

/* a VA or STRINGS oscillator's sample at phase p, as a wave sample (-128 .. 127) at the 250 kHz clock (no band-limiting:
 * the resampler's filter is the only one, as for the waves) */
AINL int32_t fb_vosc(uint32_t om, uint32_t p, const int32_t *q, uint32_t pw, uint32_t sub)
{
    int32_t sa = (int32_t)(p >> 24) - 128;
    if (om == OM_VA) {
        int32_t pu = p < pw ? 127 : -128;
        return (sa * (128 - q[0]) + pu * q[0]) >> 7;
    }
    return (q[0] * sa + q[1] * ((p >> 31) ? 127 : -128) + q[2] * ((p >> 30 & 1u) ? 127 : -128) +
            q[3] * (sub ? 127 : -128)) >> 7;
}

/* the filter section's state, held in registers through the output's loop (build 11: copied from the voice and back) */
typedef struct { int32_t z0, z1, z2, z3, y4, G, k, gc, drv, dmk; } fb_lad_t;
/* a sample of the filter section (Q15 in and out) */
AINL int32_t fb_filter(fb_lad_t *f, uint32_t ft, const int16_t *crv, int32_t x)
{
    int32_t u, y1, y2, y3, y4, t;
    if (ft == FT_DRIVE)
        return (mulq15(fb_curve(crv, (int32_t)(((int64_t)x * f->drv) >> 12)), 22938) * f->dmk) >> 12;   /* (x 0.7 after) */
    if (ft == FT_OFF)
        return x;
    u = tanh_q15(x - ((f->y4 * f->k) >> 12));
    t = mulr24(f->G, u - f->z0);
    y1 = t + f->z0;
    f->z0 = y1 + t;
    t = mulr24(f->G, y1 - f->z1);
    y2 = t + f->z1;
    f->z1 = y2 + t;
    t = mulr24(f->G, y2 - f->z2);
    y3 = t + f->z2;
    f->z2 = y3 + t;
    t = mulr24(f->G, y3 - f->z3);
    y4 = t + f->z3;
    f->z3 = y4 + t;
    f->y4 = y4;
    switch (ft) {                                  /* the taps [C]; 0.75 and 1.2 as the report's mode gains [B 11] */
    case FT_LP24:
        return (y4 * f->gc) >> 12;
    case FT_LP12:
        return mulq15((y2 * f->gc) >> 12, 39322);
    case FT_HP24:
        return mulq15(u - 4 * y1 + 6 * y2 - 4 * y3 + y4, 24576);
    default:
        return u - 2 * y1 + y2;
    }
}

#if TELEMETRY
/* (a diagnostic build: where a voice's time goes -- its control, the virtual clock's samples, the output's -- ticks,
 * summed over the voices; main.c's benchmark reads and clears them. Two cores add to them: near enough) */
static volatile uint32_t fb_tk[3];
#define FB_TK(i, t0) (fb_tk[i] += fm1_ticks() - (t0))
#define FB_T0() fm1_ticks()
#else
#define FB_TK(i, t0) ((void)(t0))
#define FB_T0() 0u
#endif
/* The render, built for speed on the FM-1 (build 11) -- the same arithmetic in the same order as before, sample for
 * sample (tests: fiber_render's hash), its cases taken apart so each runs a loop of its own:
 *   1. each oscillator's samples at the virtual clock, a loop for its mode (wave, VA, STRINGS);
 *   2. the two mixed (plain, ring, or DIRT -- each kind its own loop) into the history;
 *   3. the output: the FIR (unrolled while the history does not wrap), the DAC, the formants, the filter -- a loop
 *      for each filter type. */

#define FB_VMAX ((0xFFFFFFu + FB_VGRP) >> 24)   /* (the most virtual samples a group can need: 91) */
/* how many steps a phase takes before its next wrap (build 11: the samples between run with no wrap to test -- the
 * same samples, the wrap's work done on the step it happens, as before) */
AINL uint32_t fb_free(uint32_t p, uint32_t inc) { return inc ? ~p / inc : 0xFFFFFFFFu; }
/* oscillator o's wrap: the 16' flips and the wave waiting comes in */
#define FB_WRAP(o, p, inc, sb, w)                                                                                   \
    do {                                                                                                            \
        p += inc;                                                                                                   \
        if (p < inc) {                                                                                              \
            sb ^= 1u;                                                                                               \
            if (v->pend[o]) {                                                                                       \
                v->ws[o] ^= 1u;                                                                                     \
                v->act[o] = v->nxt[o];                                                                              \
                v->pend[o] = 0;                                                                                     \
                w = v->wb[o][v->ws[o]];                                                                             \
            }                                                                                                       \
        }                                                                                                           \
    } while (0)
/* one oscillator's nv samples: runs of SAMPLE with no wrap, then the sample where it wraps */
#define FB_RUN1(SAMPLE)                                                                                             \
    for (j = 0; j < nv;) {                                                                                          \
        uint32_t e = fb_free(p, inc);                                                                               \
        e = e < nv - j ? j + e : nv;                                                                                \
        for (; j < e; j++) {                                                                                        \
            SAMPLE;                                                                                                 \
            p += inc;                                                                                               \
        }                                                                                                           \
        if (j < nv) {                                                                                               \
            SAMPLE;                                                                                                 \
            FB_WRAP(o, p, inc, sb, w);                                                                              \
            j++;                                                                                                    \
        }                                                                                                           \
    }
/* oscillator o's nv samples at the virtual clock into s, a loop for its mode */
AINL void fb_osc_run(fb_voice_t *v, uint32_t om, uint32_t o, int32_t *s, uint32_t nv)
{
    uint32_t p = v->ph[o], inc = v->inc[o], sb = v->sub[o], j;
    const int8_t *w = v->wb[o][v->ws[o]];
    if (om == OM_WAVE) {
        FB_RUN1(s[j] = w[p >> 25])
    } else if (om == OM_VA) {
        const int32_t q0 = v->sg[o][0], q1 = 128 - q0;
        const uint32_t pw = v->pw[o];
        FB_RUN1(s[j] = (((int32_t)(p >> 24) - 128) * q1 + (p < pw ? 127 : -128) * q0) >> 7)
    } else {
        const int32_t *q = v->sg[o];
        FB_RUN1(s[j] = fb_vosc(OM_STR, p, q, 0, sb))
    }
    v->ph[o] = p;
    v->sub[o] = (uint8_t)sb;
}
#undef FB_RUN1

/* two waves, mixed as they are made (the commonest case: no buffer between); ring: gr, else 0. Runs with neither
 * wrapping, then the sample where one (or both) does */
#define FB_RUN2(SAMPLE)                                                                                             \
    for (j = 0; j < nv;) {                                                                                          \
        uint32_t e = fb_free(p1, i1), e2 = fb_free(p2, i2);                                                         \
        e = e < e2 ? e : e2;                                                                                        \
        e = e < nv - j ? j + e : nv;                                                                                \
        for (; j < e; j++) {                                                                                        \
            SAMPLE;                                                                                                 \
            p1 += i1;                                                                                               \
            p2 += i2;                                                                                               \
        }                                                                                                           \
        if (j < nv) {                                                                                               \
            SAMPLE;                                                                                                 \
            FB_WRAP(0, p1, i1, b1, w1);                                                                             \
            FB_WRAP(1, p2, i2, b2, w2);                                                                             \
            j++;                                                                                                    \
        }                                                                                                           \
    }
AINL uint32_t fb_ww_run(fb_voice_t *v, uint32_t hw, uint32_t nv, int32_t g1, int32_t g2, int32_t gr)
{
    uint32_t p1 = v->ph[0], i1 = v->inc[0], b1 = v->sub[0], p2 = v->ph[1], i2 = v->inc[1], b2 = v->sub[1], j;
    const int8_t *w1 = v->wb[0][v->ws[0]], *w2 = v->wb[1][v->ws[1]];
    int16_t *h = v->hist;
    if (!gr) {
        FB_RUN2(h[(hw + j) & 127u] = (int16_t)((g1 * w1[p1 >> 25] + g2 * w2[p2 >> 25]) >> 2))   /* (|x| <= 65536) */
    } else {
        FB_RUN2(int32_t a = w1[p1 >> 25]; int32_t b = w2[p2 >> 25];
                h[(hw + j) & 127u] = (int16_t)((g1 * a + g2 * b + ((gr * a * b) >> 7)) >> 2))   /* (|x| <= 98304) */
    }
    v->ph[0] = p1, v->ph[1] = p2;
    v->sub[0] = (uint8_t)b1, v->sub[1] = (uint8_t)b2;
    return hw + nv;
}
#undef FB_RUN2
#undef FB_WRAP

/* the two mixed into the history (plain or ring: never past its range, so not clamped) */
AINL uint32_t fb_mix_plain(int16_t *h, uint32_t hw, const int32_t *a, const int32_t *b, uint32_t nv, int32_t g1, int32_t g2)
{
    uint32_t j;
    for (j = 0; j < nv; j++)
        h[(hw + j) & 127u] = (int16_t)((g1 * a[j] + g2 * b[j]) >> 2);   /* (|x| <= 65536) */
    return hw + nv;
}
AINL uint32_t fb_mix_ring(int16_t *h, uint32_t hw, const int32_t *a, const int32_t *b, uint32_t nv, int32_t g1, int32_t g2,
                          int32_t gr)
{
    uint32_t j;
    for (j = 0; j < nv; j++) {
        int32_t x = g1 * a[j] + g2 * b[j];
        x += (gr * a[j] * b[j]) >> 7;              /* (|x| <= 98304) */
        h[(hw + j) & 127u] = (int16_t)(x >> 2);
    }
    return hw + nv;
}
/* .. with DIRT's processes [C] on the generator [B]: kind k, a loop each (its noise drawn every sample, as always) */
#define FB_DIRT_LOOP(ADD)                                                                                          \
    for (j = 0; j < nv; j++) {                                                                                      \
        int32_t x = g1 * a[j] + g2 * b[j], z;                                                                      \
        s = s * 0x41C64ED1u + 0x329Du;                                                                              \
        z = (int32_t)s >> 24;                      /* (-128 .. 127, as a wave sample) */                            \
        ADD;                                                                                                        \
        h[(hw + j) & 127u] = (int16_t)clampi(x >> 2, -32767, 32767);                                               \
    }
AINL uint32_t fb_mix_dirt(fb_voice_t *v, uint32_t k, int16_t *h, uint32_t hw, const int32_t *a, const int32_t *b,
                          uint32_t nv, int32_t g1, int32_t g2, int32_t dn, uint32_t gate)
{
    uint32_t j, s = v->lcg;
    switch (k) {
    case DT_STATIC:
        FB_DIRT_LOOP(x += dn * z >> 1)
        break;
    case DT_CRACKLE:                               /* sparse single impulses */
        FB_DIRT_LOOP(if ((s >> 22) < 3u) x += dn * z * 3)
        break;
    case DT_GEIGER:                                /* rarer, larger ticks */
        FB_DIRT_LOOP(if ((s >> 25) == 0u && ((s >> 17) & 255u) < 6u) x += dn * (z < 0 ? -255 : 255) * 2)
        break;
    case DT_CLICK:                                 /* a burst of noise at the note's start */
        FB_DIRT_LOOP(if (v->click) {
            x += dn * z * 2;
            if ((j & 3u) == 3u)
                v->click--;
        })
        break;
    default:                                       /* the bursts */
        if (gate) {
            FB_DIRT_LOOP(x += dn * z)
        } else {
            FB_DIRT_LOOP((void)z)
        }
        break;
    }
    v->lcg = s;
    return hw + nv;
}
#undef FB_DIRT_LOOP

/* the output's samples i0 .. i1 of the group starting at g: the polyphase FIR to 44.1 kHz [B 9, coefficients C], the
 * DAC [C], STRINGS' formants, the filter (ft: a constant where it is called, each type its own loop), the DC blocker,
 * the amplifier */
AINL void fb_out_run(fb_voice_t *v, const fb_design_t *d, int32_t *out, uint32_t g, uint32_t m, uint32_t ft,
                     const int16_t *fir, uint32_t taps, const int16_t *crv, uint32_t fmon, int32_t a0, int32_t da)
{
    uint32_t i, racc = v->racc, rp8 = v->rp;
    const int16_t *h = v->hist;
    const int32_t dac_c = v->dac_c, dac_g = v->dac_g, dac_off = v->dac_off, gout = d->out;
    int32_t dc = v->dc;
    fb_lad_t f = {v->z[0], v->z[1], v->z[2], v->z[3], v->y4, v->G, v->k, v->gc, v->drv, v->dmk};
    for (i = 0; i < m; i++) {
        const int16_t *c;
        int32_t y, x;
        uint32_t t, rp;
        racc += FB_VSTEP;
        rp8 = (rp8 + (racc >> 24)) & 255u;
        racc &= 0xFFFFFFu;
        c = &fir[(racc >> 17) * taps];
        rp = (rp8 - 1u) & 127u;
        if (rp >= 11u) {                           /* (the taps not wrapping round the history: straight on) */
            const int16_t *hh = h + rp;
            y = c[0] * hh[0] + c[1] * hh[-1] + c[2] * hh[-2] + c[3] * hh[-3] + c[4] * hh[-4] + c[5] * hh[-5] +
                c[6] * hh[-6] + c[7] * hh[-7];
            if (taps == 12u)
                y += c[8] * hh[-8] + c[9] * hh[-9] + c[10] * hh[-10] + c[11] * hh[-11];
        } else
            for (y = 0, t = 0; t < taps; t++)
                y += c[t] * h[(rp - t) & 127u];
        x = y >> 13;                               /* (Q15: a wave at full level is +- 1) */
        x = mulq15(x + mulq15(dac_c, mulq15(x, absi(x) > 2 * Q15 ? 2 * Q15 : absi(x))), dac_g) + dac_off;
        if (fmon)                                  /* STRINGS' formants: two band-passes added (the EQ of REPORT.md 5) */
            x += (v->fk[0] * svf_run(&v->fs[0], &v->fc[0], x, 2) + v->fk[1] * svf_run(&v->fs[1], &v->fc[1], x, 2)) >> 12;
        x = fb_filter(&f, ft, crv, clampi(x, -4 * Q15, 4 * Q15));
        dc += x - (dc >> 10);                      /* (a one-pole high-pass at ~7 Hz) */
        x -= dc >> 10;
        x = mulq15(x, a0 + ((da * (int32_t)(g + i + 1u)) >> 5));
        out[g + i] = (int32_t)(((int64_t)x * gout) >> 12);
    }
    v->racc = racc;
    v->rp = (uint8_t)rp8;
    v->dc = dc;
    v->z[0] = f.z0, v->z[1] = f.z1, v->z[2] = f.z2, v->z[3] = f.z3, v->y4 = f.y4;
}

/* (build 21) the drums' level: their loudest 12 ms brought to FB_RMS_* (hundredths of full scale, RMS: fb_perc_rms);
 * a knee from FB_KNEE into FB_KTOP (Q15, 0.4 .. 0.55: x 20 / 3 is 1 / 0.15 -- the curve leaves the line at its own slope) */
#define FB_TONE_T 1332716u      /* (the rest: their 75th percentile to 0.0794 RMS -- 1677722, 0.1, before build 21) */
#define FB_RMS_KICK 60u
#define FB_RMS_DRUM 45u
#define FB_RMS_HAT 22u
#define FB_KNEE 13107
#define FB_KTOP 18022
/* n samples (a block) into out, Q15 */
static FAST int fb_render(fb_voice_t *v, const fb_design_t *d, int32_t *out, uint32_t n, uint32_t l)
{
    uint32_t g;
    int32_t a0, da;
    const int16_t *fir = d->rs ? FB_FIR8 : FB_FIR12, *crv = &FB_DRV[(uint32_t)(d->dcurve < DC_N ? d->dcurve : 0) * 1025u];
    uint32_t taps = d->rs ? 8u : 12u, ft = d->ftype, dirt = d->dirt, fmon = d->om[0] == OM_STR, tk = FB_T0();
    int32_t s1[FB_VMAX], s2[FB_VMAX];
    if (!v->live)
        return 0;
    n = n > CTL ? CTL : n;
    fb_control(v, d, l);
    FB_TK(0, tk);
    a0 = v->amp0;
    da = v->amp1 - v->amp0;
    for (g = 0; g < n; g += 16u) {                 /* a group of 16 host samples */
        uint32_t nv, hw = v->hw, gate = 1, m = n - g < 16u ? n - g : 16u;
        int32_t g1 = v->g1, g2 = v->g2;
        tk = FB_T0();
        v->gacc += FB_VGRP;                        /* the virtual samples these 16 need [B 9] */
        nv = v->gacc >> 24;
        v->gacc &= 0xFFFFFFu;
        if (dirt >= DT_BURST1 && dirt <= DT_BURST3) {   /* a burst: on for some groups, off for others [C] */
            if (!v->burst) {
                uint32_t s = v->lcg * 0x41C64ED1u + 0x329Du;
                v->lcg = s;
                if ((s >> 24) < (dirt == DT_BURST1 ? 24u : dirt == DT_BURST2 ? 10u : 4u))
                    v->burst = (uint8_t)(dirt == DT_BURST1 ? 2u + ((s >> 20) & 3u) : dirt == DT_BURST2 ? 6u + ((s >> 19) & 15u)
                                                                                                     : 20u + ((s >> 18) & 63u));
            }
            gate = v->burst ? (v->burst--, 1u) : 0u;
        }
        if (d->om[0] == OM_WAVE && d->om[1] == OM_WAVE && !v->dn)   /* the virtual clock's samples [B 8] */
            hw = fb_ww_run(v, hw, nv, g1, g2, v->gr);
        else {
            fb_osc_run(v, d->om[0], 0, s1, nv);
            fb_osc_run(v, d->om[1], 1, s2, nv);
            if (v->dn)
                hw = fb_mix_dirt(v, dirt, v->hist, hw, s1, s2, nv, g1, g2, v->dn, gate);
            else if (v->gr)
                hw = fb_mix_ring(v->hist, hw, s1, s2, nv, g1, g2, v->gr);
            else
                hw = fb_mix_plain(v->hist, hw, s1, s2, nv, g1, g2);
        }
        v->hw = (uint8_t)hw;
        FB_TK(1, tk);
        tk = FB_T0();
        switch (ft) {                              /* (the output: a loop for each filter type) */
        case FT_LP24:
            fb_out_run(v, d, out, g, m, FT_LP24, fir, taps, crv, fmon, a0, da);
            break;
        case FT_LP12:
            fb_out_run(v, d, out, g, m, FT_LP12, fir, taps, crv, fmon, a0, da);
            break;
        case FT_HP24:
            fb_out_run(v, d, out, g, m, FT_HP24, fir, taps, crv, fmon, a0, da);
            break;
        case FT_DRIVE:
            fb_out_run(v, d, out, g, m, FT_DRIVE, fir, taps, crv, fmon, a0, da);
            break;
        case FT_OFF:
            fb_out_run(v, d, out, g, m, FT_OFF, fir, taps, crv, fmon, a0, da);
            break;
        default:                                   /* (HP 12) */
            fb_out_run(v, d, out, g, m, FT_HP12, fir, taps, crv, fmon, a0, da);
            break;
        }
        FB_TK(2, tk);
    }
    if (v->tp < FB_TRL) {                          /* a kick's transient, added after the filter: its click and knock
                                                    * are not closed off with the body's tone (build 12) */
        const int16_t *t = &FB_TRANS[((uint32_t)(d->trans & 255) - 1u) * FB_TRL];
        uint32_t i, p = v->tp;
        int32_t k = (int32_t)(((int64_t)v->tg * d->out) >> 12);
        for (i = 0; i < n && p < FB_TRL; i++, p++)
            out[i] += (t[p] * k) >> 15;
        v->tp = (uint16_t)p;
    }
    if (d->fixp && l != 6u) {                      /* (build 21) a drum's knee: linear to 0.4, then rounded into
                                                    * 0.55 -- after the layer's x 1.8, under its limiter's 0.95, so
                                                    * the hit is never pulled down with its body (fb_measure_on) */
        uint32_t i;
        for (i = 0; i < n; i++) {
            int32_t x = out[i], a = absi(x);
            if (a > FB_KNEE) {
                a = FB_KNEE + mulq15(FB_KTOP - FB_KNEE, tanh_q15((a - FB_KNEE) * 20 / 3));
                out[i] = x < 0 ? -a : a;
            }
        }
    }
    if (!v->st[2] && !v->hold && !v->amp0 && !v->amp1)
        v->live = 0;                               /* (its envelope over) */
    return 1;
}
static __typeof__(&fb_render) volatile fb_render_fp = fb_render;   /* (the flash's way into the RAM code) */

#include "fiber_gen.c"
