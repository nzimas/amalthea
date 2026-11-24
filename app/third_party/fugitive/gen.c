/* SPDX-License-Identifier: GPL-3.0-only
 * FUGITIVE: sounds drawn (HOME, SHIFT + HOME, SHIFT + PRESETS, NEW FUGUE).
 *
 * Each engine has a list of families (ACID BASS, METAL FM, FOLD BASS ...; docs/SOUNDS.md). A family is a recipe: from
 * the engine's defaults it sets the parameters that make that kind of sound, each drawn from a seed within a range
 * that keeps it that family and playable, so a sound is drawn again bit for bit from (engine, family, seed). Values
 * are Q16 of the original's units (eng.h): seconds, Hz, 0 .. 1, an enum's index << 16.
 *
 * The music is a fugue of monophonic lines, often 16ths at 120 .. 160 BPM, in several registers, and it lives on clear
 * pitches: no recipe moves a note's pitch. No glide, no pitch LFO or vibrato, no pitch envelope or sweep, no
 * arpeggio, no modulation slot on a pitch; gen_steady() makes sure of it after every recipe, and the host checks it
 * (tests/host/host.c, `host families`). A carrier or an oscillator is at the note or an octave of it (a synced
 * oscillator's tuning is its timbre). What moves is the timbre: cutoff, wavetable shape, FM index, feedback, fold,
 * width, noise, the mix. Attacks are short (pads at most ~0.12 s), releases below ~1.5 s. The filters that track the
 * note do it in Hz (cutoff + track x (note Hz - 349)): a bass keeps its tracking low so C2 still sounds; a cutoff of
 * 349.228 Hz at track 1 (698.456 at 2) is the note itself (2 x the note): a resonance there stays in tune. A resonant
 * filter that moves (an envelope, an LFO, S&H) stays below self-oscillation, or it would sing a pitch of its own.
 * Some families are rougher (S&H, feedback, crushing, AM: flagged GFL_EXP), drawn a little less often.
 *
 *   gen_nfam(e)            how many families engine e has
 *   family_name(s)         the family of a sound, <= 12 characters
 *   gen_sound(s, e, f, seed)   a sound of engine e, family f (-1: one drawn), from seed
 *   gen_role_sound(snd, t, h, oct, seed)   NEW FUGUE: track t's engine and family, fitting its playhead h (speed,
 *                          transposition) and octave, unlike the tracks before it (snd[0 .. t - 1]) */

/* parameter values: thousandths (seconds, 0 .. 1 ...), Hz, semitones, enum indices -- Q16 */
#define GK(x) ((int32_t)(x) * 65536 / 1000)
#define GHZ(x) ((int32_t)(x) * 65536)
#define GI(x) ((int32_t)(x) * 65536)
#define G_OFF GHZ(20000)          /* a cutoff of 20 kHz: the filter off (skipped: cheaper) */
#define G_F4 22887022             /* 349.228 Hz: + track 1 x (note Hz - 349.228), the note's own frequency */
#define G_F4X2 45774044           /* 698.456 Hz: at track 2, twice the note's */

/* roles (NEW FUGUE) and flags */
#define GR_BASS 1u
#define GR_PAD 2u
#define GR_MID 4u                 /* keys, strings, leads in the middle */
#define GR_HIGH 8u                /* plucks, leads, bells: fast or high lines */
#define GFL_SLOW 1u                /* a pad: its attack may take up to ~0.12 s */
#define GFL_EXP 2u                 /* rougher, experimental */
#define GEN_FMAX 24u               /* the most families an engine may have */

typedef struct {
    const char *name;             /* <= 12 characters: A .. Z, 0 .. 9, space, - and / (FONT_XS) */
    int32_t (*fn)(sound_t *s, rnd_t *r);   /* the recipe; returns a level trim, Q16 */
    uint8_t roles, flags;
    uint16_t vol;                 /* the family's VOLUME, thousandths (x the trim, +-10 % drawn) */
} gen_fam_t;

/* the setters: parameter j of the sound's engine, clamped to its range (enums whole) */
static void gs(sound_t *s, uint32_t j, int32_t v)
{
    const eng_t *E = eng_of(s->eng);
    if (j < E->npar && j < ENG_NPAR)
        s->val[j] = par_clamp(&E->par[j], v);
}
/* lo .. hi, spread evenly in octaves (lo > 0) */
static int32_t glog(rnd_t *r, int32_t lo, int32_t hi)
{
    int32_t a, b;
    int64_t v;
    if (lo <= 0 || hi <= lo)
        return rnd_in(r, lo, hi);
    a = log2_q16((uint32_t)lo);
    b = log2_q16((uint32_t)hi);
    v = ((int64_t)lo * exp2_q16(rnd_in(r, 0, b - a))) >> 16;
    return v > hi ? hi : (int32_t)v;
}
static int32_t gpick(rnd_t *r, const uint8_t *o, uint32_t n) { return o[rnd_below(r, (int32_t)n)]; }
static int32_t gsign(rnd_t *r, int32_t v) { return rnd_chance(r, 50) ? v : -v; }
#define G_U(j, lo, hi) gs(s, (j), rnd_in(r, (lo), (hi)))       /* even */
#define G_L(j, lo, hi) gs(s, (j), glog(r, (lo), (hi)))         /* in octaves (times, Hz, rates) */
#define G_V(j, v) gs(s, (j), (v))
#define G_E(j, i) gs(s, (j), GI(i))                            /* an enum's index */
#define G_P(j, ...) do { static const uint8_t o_[] = {__VA_ARGS__}; gs(s, (j), GI(gpick(r, o_, sizeof o_))); } while (0)
#define G_CH(p) rnd_chance(r, (p))

/* ================================================================== ACD: one oscillator + sub, SV / RD3 filter */
enum { GA_SAW, GA_SQR, GA_SUB, GA_NOISE, GA_PW, GA_SUBM, GA_PWLFO, GA_PWENV, GA_PLFO, GA_CUT, GA_RES, GA_TYPE,
       GA_TRK, GA_FLFO, GA_FENV, GA_FA, GA_FD, GA_FS, GA_FR, GA_AA, GA_AD, GA_AS, GA_AR, GA_LF, GA_LW, GA_LR,
       GA_GM, GA_GT, GA_VOL };
/* the filter's cutoff when it tracks the note: base + 300 Hz x track, so C2 (65 Hz, 284 below F4) is not cut */
static int32_t ga_cut(sound_t *s, rnd_t *r, uint32_t j, uint32_t jtrk, int32_t lo, int32_t hi)
{
    int32_t c = glog(r, lo, hi) + s->val[jtrk] * 300;
    gs(s, j, c);
    return c;
}
static void ga_osc(sound_t *s, rnd_t *r, int sq_pct)
{
    if (G_CH(sq_pct)) {
        G_V(GA_SAW, 0);
        G_V(GA_SQR, GK(1000));
        G_U(GA_PW, GK(350), GK(500));
    } else {
        G_V(GA_SAW, GK(1000));
        G_V(GA_SQR, G_CH(25) ? rnd_in(r, GK(200), GK(600)) : 0);
    }
}
static int32_t ga_acid(sound_t *s, rnd_t *r)
{
    int32_t t = 65536;
    ga_osc(s, r, 30);
    G_U(GA_TRK, 0, GK(300));
    if (G_CH(60)) {
        G_E(GA_TYPE, 1);                         /* SV24: squelch well below its self-oscillation (r 1.2) */
        G_U(GA_RES, GK(800), GK(1000));
        t = 32768;                               /* (x 0.5: its resonant peaks) */
    } else {
        G_E(GA_TYPE, 2);                         /* RD3 */
        G_U(GA_RES, GK(300), GK(650));
    }
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(120), GHZ(420));
    G_U(GA_FENV, GK(60), GK(220));
    G_V(GA_FA, 0);
    G_L(GA_FD, GK(80), GK(400));
    G_V(GA_FS, 0);
    G_V(GA_FR, GK(100));
    G_V(GA_AA, 0);
    G_L(GA_AD, GK(200), GK(1200));
    G_U(GA_AS, GK(500), GK(1000));
    G_L(GA_AR, GK(40), GK(120));
    if (G_CH(30)) {
        G_U(GA_SUB, GK(200), GK(500));
        G_E(GA_SUBM, 0);
    }
    return t;
}
static int32_t ga_squelch(sound_t *s, rnd_t *r)
{
    int32_t t = 65536;
    G_V(GA_SAW, GK(1000));
    G_U(GA_SQR, 0, GK(600));
    G_U(GA_PW, GK(300), GK(500));
    G_U(GA_TRK, GK(500), GK(1000));
    if (G_CH(50)) {
        G_E(GA_TYPE, 0);                         /* SV12 (self-oscillates above r 0.77) */
        G_U(GA_RES, GK(450), GK(720));
    } else {
        G_E(GA_TYPE, 1);
        G_U(GA_RES, GK(700), GK(950));
        t = 45875;                               /* (x 0.7) */
    }
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(500), GHZ(1800));
    G_U(GA_FENV, GK(80), GK(300));
    G_L(GA_FD, GK(150), GK(600));
    G_U(GA_FS, GK(100), GK(400));
    G_V(GA_FR, GK(200));
    G_L(GA_AA, 0, GK(8));
    G_L(GA_AD, GK(300), GK(800));
    G_U(GA_AS, GK(750), GK(1000));
    G_L(GA_AR, GK(80), GK(250));
    return t;
}
static int32_t ga_sub(sound_t *s, rnd_t *r)
{
    G_U(GA_SAW, 0, GK(300));
    G_U(GA_SQR, 0, GK(500));
    G_V(GA_PW, GK(500));
    G_U(GA_SUB, GK(700), GK(1000));
    G_P(GA_SUBM, 0, 1, 2);                       /* -1 octave square, pulse, triangle */
    G_P(GA_TYPE, 0, 1);
    G_U(GA_RES, 0, GK(300));
    G_U(GA_TRK, 0, GK(300));
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(180), GHZ(500));
    G_U(GA_FENV, GK(10), GK(50));
    G_L(GA_FD, GK(100), GK(300));
    G_V(GA_FS, 0);
    G_L(GA_AA, 0, GK(5));
    G_L(GA_AD, GK(300), GK(800));
    G_U(GA_AS, GK(700), GK(1000));
    G_L(GA_AR, GK(50), GK(120));
    return 65536;
}
static int32_t ga_pluck(sound_t *s, rnd_t *r)
{
    ga_osc(s, r, 35);
    G_U(GA_TRK, GK(500), GK(1000));
    if (G_CH(65)) {
        G_E(GA_TYPE, 1);
        G_U(GA_RES, GK(200), GK(750));
    } else {
        G_E(GA_TYPE, 2);
        G_U(GA_RES, GK(150), GK(500));
    }
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(250), GHZ(800));
    G_U(GA_FENV, GK(120), GK(350));
    G_V(GA_FA, 0);
    G_L(GA_FD, GK(60), GK(250));
    G_V(GA_FS, 0);
    G_V(GA_AA, 0);
    G_L(GA_AD, GK(120), GK(400));
    G_U(GA_AS, 0, GK(80));
    G_L(GA_AR, GK(80), GK(200));
    G_V(GA_FR, GK(150));
    return 65536;
}
static int32_t ga_stab(sound_t *s, rnd_t *r)
{
    int32_t t = 65536;
    ga_osc(s, r, 50);
    G_U(GA_TRK, GK(300), GK(800));
    if (G_CH(60)) {                              /* (resonant, but not so much that the fall sings: no zap) */
        G_E(GA_TYPE, 1);
        G_U(GA_RES, GK(800), GK(1000));
        t = 45875;
    } else {
        G_E(GA_TYPE, 0);
        G_U(GA_RES, GK(550), GK(700));
    }
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(250), GHZ(700));
    G_U(GA_FENV, GK(60), GK(180));
    G_L(GA_FD, GK(60), GK(180));
    G_V(GA_FS, 0);
    G_U(GA_NOISE, -GK(150), GK(150));
    G_V(GA_AA, 0);
    G_L(GA_AD, GK(200), GK(500));
    G_U(GA_AS, GK(250), GK(450));
    G_L(GA_AR, GK(60), GK(150));
    G_V(GA_FR, GK(100));
    return t;
}
static int32_t ga_square(sound_t *s, rnd_t *r)
{
    G_V(GA_SQR, GK(1000));
    G_U(GA_SAW, 0, GK(300));
    G_U(GA_PW, GK(250), GK(500));
    G_U(GA_PWLFO, GK(50), GK(250));
    G_L(GA_LF, GK(500), GK(6000));
    G_P(GA_LW, 0, 1);
    G_E(GA_TYPE, 0);
    G_U(GA_RES, GK(100), GK(450));
    G_U(GA_TRK, GK(500), GK(1000));
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(1200), GHZ(5000));
    G_U(GA_FENV, GK(20), GK(80));
    G_V(GA_FD, GK(200));
    G_L(GA_AA, 0, GK(10));
    G_V(GA_AD, GK(500));
    G_U(GA_AS, GK(800), GK(1000));
    G_L(GA_AR, GK(80), GK(250));
    return 65536;
}
static int32_t ga_snh(sound_t *s, rnd_t *r)        /* a random step on the cutoff, a few a second */
{
    int32_t c, d;
    ga_osc(s, r, 40);
    G_E(GA_TYPE, 1);
    G_U(GA_RES, GK(600), GK(950));               /* (well below SV24's self-oscillation: no random pitches) */
    G_U(GA_TRK, GK(300), GK(800));
    c = ga_cut(s, r, GA_CUT, GA_TRK, GHZ(400), GHZ(1100));
    d = rnd_in(r, c / 3, c);                     /* (the steps' reach in Hz: up to the cutoff, down to near 0) */
    G_V(GA_FLFO, d / 20000);                     /* (x 20 kHz) */
    G_E(GA_LW, 5);
    G_E(GA_LR, G_CH(50) ? 0 : 2);
    G_L(GA_LF, GHZ(3), GHZ(14));
    G_U(GA_FENV, GK(50), GK(150));
    G_V(GA_FD, GK(150));
    G_V(GA_AA, 0);
    G_V(GA_AD, GK(500));
    G_U(GA_AS, GK(800), GK(1000));
    G_L(GA_AR, GK(80), GK(200));
    return 45875;
}
static int32_t ga_whistle(sound_t *s, rnd_t *r)    /* SV12 self-oscillating on the note (or its octave), still */
{
    int oct = G_CH(35);
    G_U(GA_SAW, GK(150), GK(450));
    G_V(GA_SQR, G_CH(40) ? rnd_in(r, GK(100), GK(300)) : 0);
    G_U(GA_PW, GK(300), GK(500));
    G_E(GA_TYPE, 0);
    G_U(GA_RES, GK(820), GK(950));
    G_V(GA_TRK, oct ? GK(2000) : GK(1000));
    G_V(GA_CUT, oct ? G_F4X2 : G_F4);
    G_V(GA_FENV, 0);                             /* (a resonance that moves would sing a glide) */
    G_V(GA_FLFO, 0);
    G_L(GA_AA, 0, GK(6));
    G_L(GA_AD, GK(300), GK(900));
    G_U(GA_AS, GK(550), GK(900));
    G_L(GA_AR, GK(80), GK(250));
    return 65536;
}
static int32_t ga_growl(sound_t *s, rnd_t *r)      /* RD3 with a wah (LFO on the cutoff) over a sub */
{
    int32_t c;
    G_V(GA_SAW, GK(1000));
    G_U(GA_SQR, 0, GK(400));
    G_U(GA_PW, GK(300), GK(500));
    G_U(GA_SUB, GK(300), GK(600));
    G_P(GA_SUBM, 0, 2);
    if (G_CH(50)) {                              /* (RD3 with a moving cutoff is ACD's dearest: half SV24) */
        G_E(GA_TYPE, 2);
        G_U(GA_RES, GK(300), GK(550));
    } else {
        G_E(GA_TYPE, 1);
        G_U(GA_RES, GK(500), GK(850));
    }
    G_U(GA_TRK, 0, GK(300));
    c = ga_cut(s, r, GA_CUT, GA_TRK, GHZ(200), GHZ(500));
    G_V(GA_FLFO, rnd_in(r, c * 2 / 5, c * 4 / 5) / 20000);
    G_P(GA_LW, 0, 1);
    G_E(GA_LR, 0);
    G_L(GA_LF, GK(1500), GK(6000));
    G_U(GA_FENV, GK(20), GK(80));
    G_L(GA_FD, GK(150), GK(400));
    G_V(GA_FS, 0);
    G_V(GA_AA, 0);
    G_L(GA_AD, GK(300), GK(1000));
    G_U(GA_AS, GK(700), GK(1000));
    G_L(GA_AR, GK(50), GK(120));
    return 65536;
}
static int32_t ga_breath(sound_t *s, rnd_t *r)     /* noise and tone through a resonant SV12, its timbre swelling */
{
    ga_osc(s, r, 50);
    G_V(GA_NOISE, gsign(r, rnd_in(r, GK(250), GK(600))));
    G_E(GA_TYPE, 0);
    G_U(GA_RES, GK(500), GK(720));
    G_V(GA_TRK, GK(1000));
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(300), GHZ(900));
    G_U(GA_FENV, GK(30), GK(100));
    G_L(GA_FA, GK(20), GK(60));
    G_L(GA_FD, GK(200), GK(500));
    G_U(GA_FS, GK(300), GK(600));
    G_V(GA_FR, GK(200));
    G_L(GA_AA, GK(2), GK(9));
    G_V(GA_AD, GK(500));
    G_U(GA_AS, GK(800), GK(1000));
    G_L(GA_AR, GK(100), GK(300));
    return 65536;
}
static int32_t ga_pwpluck(sound_t *s, rnd_t *r)    /* a narrow pulse opened to a square by the envelope, closing */
{
    G_V(GA_SQR, GK(1000));
    G_U(GA_SAW, 0, GK(200));
    G_U(GA_PW, GK(60), GK(180));
    G_U(GA_PWENV, GK(250), GK(400));
    G_E(GA_TYPE, 1);
    G_U(GA_RES, GK(200), GK(500));
    G_U(GA_TRK, GK(700), GK(1000));
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(500), GHZ(1500));
    G_U(GA_FENV, GK(50), GK(150));
    G_V(GA_FA, 0);
    G_L(GA_FD, GK(100), GK(300));
    G_V(GA_FS, 0);
    G_V(GA_FR, GK(150));
    G_V(GA_AA, 0);
    G_L(GA_AD, GK(150), GK(450));
    G_U(GA_AS, 0, GK(100));
    G_L(GA_AR, GK(80), GK(200));
    return 65536;
}
static int32_t ga_knock(sound_t *s, rnd_t *r)      /* a very short filter envelope on a low RD3: a knock, then body */
{
    ga_osc(s, r, 40);
    G_U(GA_SUB, GK(200), GK(500));
    G_P(GA_SUBM, 0, 2);
    G_E(GA_TYPE, 2);
    G_U(GA_RES, GK(150), GK(300));
    G_U(GA_TRK, 0, GK(300));
    ga_cut(s, r, GA_CUT, GA_TRK, GHZ(150), GHZ(400));
    G_U(GA_FENV, GK(80), GK(150));
    G_V(GA_FA, 0);
    G_L(GA_FD, GK(20), GK(60));
    G_V(GA_FS, 0);
    G_V(GA_FR, GK(50));
    G_V(GA_AA, 0);
    G_L(GA_AD, GK(250), GK(500));
    G_U(GA_AS, GK(350), GK(600));
    G_L(GA_AR, GK(50), GK(120));
    return 65536;
}
static const gen_fam_t GEN_ACD[] = {
    {"ACID BASS", ga_acid, GR_BASS | GR_MID, 0, 1000},
    {"SQUELCH LEAD", ga_squelch, GR_MID | GR_HIGH, 0, 1000},
    {"SUB BASS", ga_sub, GR_BASS, 0, 1000},
    {"PLUCK", ga_pluck, GR_MID | GR_HIGH, 0, 1600},
    {"RESO STAB", ga_stab, GR_MID | GR_HIGH, 0, 1100},
    {"SQUARE LEAD", ga_square, GR_MID | GR_HIGH, 0, 1000},
    {"S/H FILTER", ga_snh, GR_MID | GR_BASS, GFL_EXP, 1000},
    {"WHISTLE", ga_whistle, GR_HIGH | GR_MID, 0, 1000},
    {"GROWL BASS", ga_growl, GR_BASS, 0, 1000},
    {"BREATH", ga_breath, GR_MID, 0, 1000},
    {"PW PLUCK", ga_pwpluck, GR_HIGH | GR_MID, 0, 1400},
    {"KNOCK", ga_knock, GR_BASS | GR_MID, 0, 1600},
};

/* ================================================================== FAT: three detuned oscillators, MG / OB filter */
enum { GF_FAT, GF_BRT, GF_FATLFO, GF_TMB, GF_NOISE, GF_RST, GF_TYPE, GF_CUT, GF_RES, GF_FLFO, GF_FENV, GF_FA, GF_FD,
       GF_FS, GF_FR, GF_AA, GF_AD, GF_AS, GF_AR, GF_LF, GF_LW, GF_LR, GF_GM, GF_GT, GF_VOL };
/* (FAT's filter tracks the note at 1.0, in Hz: a cutoff below ~400 Hz closes on C2. FATNESS LFO moves the
 * oscillators' detune, a pitch: never used) */
static int32_t gf_supersaw(sound_t *s, rnd_t *r)
{
    G_U(GF_FAT, GK(180), GK(450));
    G_U(GF_BRT, GK(250), GK(800));
    G_U(GF_TMB, 0, GK(80));
    G_P(GF_TYPE, 0, 2);
    G_L(GF_CUT, GHZ(1800), GHZ(7000));
    G_U(GF_RES, GK(50), GK(300));
    G_U(GF_FENV, GK(30), GK(120));
    G_L(GF_FD, GK(300), GK(800));
    G_V(GF_FS, GK(300));
    G_L(GF_AA, 0, GK(15));
    G_V(GF_AD, GK(500));
    G_U(GF_AS, GK(800), GK(1000));
    G_L(GF_AR, GK(150), GK(450));
    return 65536;
}
static int32_t gf_brass(sound_t *s, rnd_t *r)
{
    G_U(GF_FAT, GK(50), GK(150));
    G_U(GF_BRT, -GK(200), GK(400));
    G_U(GF_TMB, 0, GK(150));
    G_P(GF_TYPE, 0, 1);
    G_L(GF_CUT, GHZ(400), GHZ(900));
    G_U(GF_RES, GK(100), GK(350));
    G_U(GF_FENV, GK(80), GK(220));
    G_L(GF_FA, GK(10), GK(60));
    G_L(GF_FD, GK(200), GK(600));
    G_U(GF_FS, GK(250), GK(500));
    G_V(GF_FR, GK(200));
    G_L(GF_AA, GK(4), GK(25));
    G_V(GF_AD, GK(400));
    G_U(GF_AS, GK(750), GK(950));
    G_L(GF_AR, GK(120), GK(300));
    return 65536;
}
static int32_t gf_bass(sound_t *s, rnd_t *r)
{
    G_U(GF_FAT, GK(20), GK(120));
    G_U(GF_BRT, -GK(1000), -GK(400));            /* the third oscillator an octave down */
    G_U(GF_TMB, 0, GK(500));
    G_E(GF_RST, 1);
    G_P(GF_TYPE, 0, 1);
    G_L(GF_CUT, GHZ(450), GHZ(1000));
    G_U(GF_RES, GK(150), GK(500));
    G_U(GF_FENV, GK(40), GK(150));
    G_L(GF_FD, GK(80), GK(300));
    G_U(GF_FS, 0, GK(200));
    G_V(GF_FR, GK(100));
    G_V(GF_AA, 0);
    G_L(GF_AD, GK(300), GK(800));
    G_U(GF_AS, GK(600), GK(1000));
    G_L(GF_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gf_pad(sound_t *s, rnd_t *r)
{
    int32_t c = glog(r, GHZ(700), GHZ(2500));
    G_U(GF_FAT, GK(200), GK(500));
    G_V(GF_BRT, G_CH(70) ? rnd_in(r, GK(200), GK(600)) : -GK(300));
    G_U(GF_TMB, 0, GK(300));
    G_E(GF_TYPE, 2);
    G_V(GF_CUT, c);
    G_U(GF_RES, GK(100), GK(300));
    G_V(GF_FLFO, rnd_in(r, c / 6, c / 3) / 20000);   /* a slow drift of the cutoff */
    G_L(GF_LF, GK(150), GK(800));
    G_P(GF_LW, 0, 1);
    G_U(GF_FENV, GK(20), GK(80));
    G_L(GF_FA, GK(150), GK(500));
    G_L(GF_FD, GK(500), GK(1500));
    G_V(GF_FS, GK(500));
    G_V(GF_FR, GK(600));
    G_L(GF_AA, GK(25), GK(110));
    G_V(GF_AD, GK(1000));
    G_V(GF_AS, GK(1000));
    G_L(GF_AR, GK(500), GK(1300));
    return 65536;
}
static int32_t gf_organ(sound_t *s, rnd_t *r)
{
    G_U(GF_TMB, GK(470), GK(530));               /* square */
    G_U(GF_BRT, GK(500), GK(1000));              /* + the octave above */
    G_U(GF_FAT, GK(10), GK(50));
    G_E(GF_RST, 1);
    if (G_CH(50))
        G_V(GF_CUT, G_OFF);
    else {
        G_L(GF_CUT, GHZ(3000), GHZ(9000));
        G_U(GF_RES, 0, GK(100));
    }
    G_V(GF_FENV, 0);
    G_L(GF_AA, GK(2), GK(6));
    G_V(GF_AD, GK(200));
    G_V(GF_AS, GK(1000));
    G_L(GF_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gf_pluck(sound_t *s, rnd_t *r)
{
    G_U(GF_FAT, GK(50), GK(250));
    G_U(GF_BRT, 0, GK(500));
    G_U(GF_TMB, 0, GK(400));
    G_E(GF_RST, 1);
    G_P(GF_TYPE, 0, 1, 2);
    G_L(GF_CUT, GHZ(450), GHZ(1000));
    G_U(GF_RES, GK(200), GK(500));
    G_U(GF_FENV, GK(120), GK(350));
    G_V(GF_FA, 0);
    G_L(GF_FD, GK(60), GK(250));
    G_V(GF_FS, 0);
    G_V(GF_FR, GK(150));
    G_V(GF_AA, 0);
    G_L(GF_AD, GK(150), GK(450));
    G_U(GF_AS, 0, GK(100));
    G_L(GF_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gf_pulse(sound_t *s, rnd_t *r)
{
    G_U(GF_TMB, GK(600), GK(900));               /* narrow pulses */
    G_U(GF_FAT, GK(30), GK(120));
    G_U(GF_BRT, -GK(300), GK(300));
    G_E(GF_TYPE, 2);
    G_L(GF_CUT, GHZ(1500), GHZ(4500));
    G_U(GF_RES, GK(100), GK(400));
    G_U(GF_FENV, GK(40), GK(100));
    G_V(GF_FD, GK(300));
    G_L(GF_AA, 0, GK(10));
    G_V(GF_AD, GK(500));
    G_U(GF_AS, GK(850), GK(1000));
    G_L(GF_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gf_snh(sound_t *s, rnd_t *r)
{
    int32_t c, d;
    G_U(GF_FAT, GK(100), GK(300));
    G_U(GF_TMB, 0, GK(500));
    G_U(GF_NOISE, GK(100), GK(400));
    G_E(GF_TYPE, 1);                             /* OB24 (the MG blows up on fast falls) */
    G_U(GF_RES, GK(400), GK(620));               /* (below its self-oscillation, ~0.67: no random pitches) */
    c = glog(r, GHZ(600), GHZ(1500));
    G_V(GF_CUT, c);
    d = rnd_in(r, c / 3, c * 3 / 4);
    G_V(GF_FLFO, d / 20000);
    G_E(GF_LW, 5);
    G_E(GF_LR, G_CH(50) ? 0 : 2);
    G_L(GF_LF, GHZ(4), GHZ(14));
    G_U(GF_FENV, GK(30), GK(100));
    G_V(GF_AA, 0);
    G_V(GF_AD, GK(500));
    G_U(GF_AS, GK(800), GK(1000));
    G_L(GF_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gf_cluster(sound_t *s, rnd_t *r)    /* the outer two oscillators a third of a semitone off: a smear */
{
    G_U(GF_FAT, GK(600), GK(950));
    G_V(GF_BRT, gsign(r, rnd_in(r, GK(400), GK(800))));   /* (the third, exact, an octave up or down) */
    G_U(GF_TMB, 0, GK(350));
    G_E(GF_RST, 1);
    G_P(GF_TYPE, 1, 2);
    G_L(GF_CUT, GHZ(1500), GHZ(4000));
    G_U(GF_RES, GK(100), GK(350));
    G_U(GF_FENV, GK(30), GK(80));
    G_L(GF_FD, GK(300), GK(800));
    G_V(GF_FS, GK(300));
    G_L(GF_AA, GK(3), GK(10));
    G_V(GF_AD, GK(500));
    G_U(GF_AS, GK(850), GK(1000));
    G_L(GF_AR, GK(200), GK(500));
    return 65536;
}
static int32_t gf_buzz(sound_t *s, rnd_t *r)       /* very narrow pulses into the MG ladder */
{
    G_U(GF_TMB, GK(860), GK(950));
    G_U(GF_FAT, GK(50), GK(200));
    G_U(GF_BRT, -GK(500), GK(500));
    G_U(GF_NOISE, 0, GK(150));
    G_E(GF_TYPE, 0);
    G_L(GF_CUT, GHZ(700), GHZ(2000));
    G_U(GF_RES, GK(300), GK(600));
    G_U(GF_FENV, GK(50), GK(150));
    G_L(GF_FD, GK(100), GK(300));
    G_V(GF_FS, GK(200));
    G_L(GF_AA, 0, GK(5));
    G_V(GF_AD, GK(400));
    G_U(GF_AS, GK(800), GK(1000));
    G_L(GF_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gf_reed(sound_t *s, rnd_t *r)       /* a pulse through a fixed resonant OB12, a breath of noise */
{
    G_U(GF_TMB, GK(600), GK(750));
    G_U(GF_FAT, GK(20), GK(60));
    G_U(GF_BRT, -GK(200), GK(200));
    G_U(GF_NOISE, GK(50), GK(150));
    G_E(GF_RST, 1);
    G_E(GF_TYPE, 2);
    G_L(GF_CUT, GHZ(700), GHZ(1600));
    G_U(GF_RES, GK(350), GK(600));
    G_U(GF_FENV, GK(20), GK(60));
    G_L(GF_FA, GK(20), GK(60));
    G_L(GF_FD, GK(300), GK(600));
    G_V(GF_FS, GK(500));
    G_V(GF_FR, GK(200));
    G_L(GF_AA, GK(5), GK(10));
    G_V(GF_AD, GK(500));
    G_U(GF_AS, GK(850), GK(1000));
    G_L(GF_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gf_air(sound_t *s, rnd_t *r)        /* noise over the saws, a resonant OB12 drifting slowly */
{
    int32_t c = glog(r, GHZ(600), GHZ(1800));
    G_U(GF_FAT, GK(200), GK(500));
    G_U(GF_NOISE, GK(400), GK(800));
    G_U(GF_TMB, 0, GK(300));
    G_U(GF_BRT, GK(300), GK(800));
    G_E(GF_TYPE, 2);
    G_V(GF_CUT, c);
    G_U(GF_RES, GK(400), GK(620));
    G_V(GF_FLFO, rnd_in(r, c * 3 / 10, c * 6 / 10) / 20000);
    G_P(GF_LW, 0, 1);
    G_E(GF_LR, 2);
    G_L(GF_LF, GK(100), GK(500));
    G_U(GF_FENV, GK(20), GK(60));
    G_L(GF_FA, GK(200), GK(600));
    G_L(GF_FD, GK(800), GK(1500));
    G_V(GF_FS, GK(500));
    G_V(GF_FR, GK(600));
    G_L(GF_AA, GK(30), GK(110));
    G_V(GF_AD, GK(1000));
    G_V(GF_AS, GK(1000));
    G_L(GF_AR, GK(500), GK(1300));
    return 65536;
}
static int32_t gf_step(sound_t *s, rnd_t *r)       /* a square LFO on the cutoff, restarting on each note */
{
    int32_t c = glog(r, GHZ(700), GHZ(2000));
    G_U(GF_FAT, GK(50), GK(250));
    G_U(GF_TMB, 0, GK(600));
    G_U(GF_BRT, -GK(500), GK(500));
    G_E(GF_TYPE, 1);
    G_V(GF_CUT, c);
    G_U(GF_RES, GK(300), GK(600));
    G_V(GF_FLFO, rnd_in(r, c * 4 / 10, c * 7 / 10) / 20000);
    G_E(GF_LW, 4);
    G_E(GF_LR, 0);
    G_L(GF_LF, GHZ(4), GHZ(10));
    G_U(GF_FENV, GK(20), GK(50));
    G_V(GF_FD, GK(300));
    G_V(GF_AA, 0);
    G_V(GF_AD, GK(500));
    G_U(GF_AS, GK(800), GK(1000));
    G_L(GF_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gf_resobass(sound_t *s, rnd_t *r)   /* OB24 just under self-oscillation, the third an octave down */
{
    G_U(GF_FAT, GK(20), GK(100));
    G_U(GF_BRT, -GK(1000), -GK(500));
    G_U(GF_TMB, 0, GK(500));
    G_E(GF_RST, 1);
    G_E(GF_TYPE, 1);
    G_L(GF_CUT, GHZ(450), GHZ(900));
    G_U(GF_RES, GK(550), GK(650));
    G_U(GF_FENV, GK(50), GK(120));
    G_L(GF_FD, GK(100), GK(300));
    G_U(GF_FS, GK(100), GK(300));
    G_V(GF_FR, GK(100));
    G_V(GF_AA, 0);
    G_L(GF_AD, GK(300), GK(800));
    G_U(GF_AS, GK(600), GK(1000));
    G_L(GF_AR, GK(40), GK(120));
    return 65536;
}
static const gen_fam_t GEN_FAT[] = {
    {"SUPERSAW", gf_supersaw, GR_MID | GR_HIGH, 0, 1000},
    {"BRASS", gf_brass, GR_MID, 0, 1000},
    {"FAT BASS", gf_bass, GR_BASS, 0, 1000},
    {"PAD", gf_pad, GR_PAD, GFL_SLOW, 1000},
    {"ORGAN", gf_organ, GR_MID | GR_PAD, 0, 700},
    {"PLUCK", gf_pluck, GR_MID | GR_HIGH, 0, 1400},
    {"PULSE LEAD", gf_pulse, GR_MID | GR_HIGH, 0, 1000},
    {"S/H RESO", gf_snh, GR_MID, GFL_EXP, 1000},
    {"CLUSTER", gf_cluster, GR_MID | GR_PAD, 0, 1000},
    {"BUZZ", gf_buzz, GR_MID | GR_HIGH, 0, 1600},
    {"REED", gf_reed, GR_MID, 0, 1000},
    {"AIR PAD", gf_air, GR_PAD, GFL_SLOW, 1000},
    {"STEP FILTER", gf_step, GR_MID | GR_BASS, GFL_EXP, 1000},
    {"RESO BASS", gf_resobass, GR_BASS, 0, 1000},
};

/* ================================================================== VAP: two oscillators, 15 filters, mod matrix */
enum { GV_SH1, GV_SH2, GV_PW1, GV_PW2, GV_MIX, GV_TUNE1, GV_TUNE2, GV_DET, GV_FINE, GV_RST1, GV_RST2, GV_TYPE,
       GV_CUT, GV_RES, GV_FENV, GV_XA, GV_XD, GV_XS, GV_XR, GV_AA, GV_AD, GV_AS, GV_AR, GV_FA, GV_FD, GV_FS, GV_FR,
       GV_L1F, GV_L1W, GV_L1R, GV_L1S, GV_L2F, GV_L2W, GV_L2R, GV_L2S, GV_GM, GV_GT, GV_TRK, GV_MIXENV, GV_VOL,
       GV_SYNC, GV_NOISE, GV_SLOT };
/* VAP's sources / destinations (user slots 1 .. 6; slot 4 is shared with MIX ENV AMT: not used here) */
enum { GVS_AMP, GVS_FILT, GVS_AUX, GVS_LFO1, GVS_LFO2 };
enum { GVD_PW1, GVD_PW2, GVD_MIX, GVD_CUT, GVD_AMPS, GVD_FILTS, GVD_AUXS, GVD_VOL, GVD_P1, GVD_P2, GVD_FILTD,
       GVD_NOISE, GVD_SH1, GVD_SH2 };
/* the filter types */
enum { GVT_MG24, GVT_LPOB24, GVT_LPOB12, GVT_LPSV24, GVT_LPSV12, GVT_HPOB24, GVT_HPOB12, GVT_HPSV24, GVT_HPSV12,
       GVT_BPOB24, GVT_BPOB12, GVT_BPSV24, GVT_BPSV12, GVT_NSV24, GVT_NSV12 };
/* slot k (0 .. 5): src -> dest x amt (multiplier = amt x the destination's range / 2: PW, shape, noise x 0.5, mix
 * x 1, cutoff x 10 kHz; VOLUME multiplies: x ((src + 1) amt / 4 + 1)) */
static void gv_slot(sound_t *s, uint32_t k, uint32_t src, uint32_t dst, int32_t amt)
{
    gs(s, GV_SLOT + 3u * k, GI(src));
    gs(s, GV_SLOT + 3u * k + 1u, GI(dst));
    gs(s, GV_SLOT + 3u * k + 2u, amt);
}
static void gv_cut(sound_t *s, rnd_t *r, int32_t lo, int32_t hi)
{
    gs(s, GV_CUT, glog(r, lo, hi) + s->val[GV_TRK] * 300);
}
static int32_t gv_strings(sound_t *s, rnd_t *r)
{
    G_U(GV_SH1, GK(450), GK(550));
    G_U(GV_SH2, GK(450), GK(550));
    G_U(GV_DET, GI(6), GI(22));
    G_U(GV_MIX, -GK(300), GK(300));
    G_V(GV_TUNE2, G_CH(30) ? GI(12) : 0);
    G_P(GV_TYPE, 4, 3, 4, 2);                    /* (SV mostly: the cheaper filters here) */
    G_U(GV_TRK, GK(500), GK(1000));
    gv_cut(s, r, GHZ(1200), GHZ(4000));
    G_U(GV_RES, GK(50), GK(300));
    G_V(GV_FENV, 0);                             /* (a still cutoff: the filter's coefficients once a render) */
    G_L(GV_AA, GK(15), GK(80));
    G_V(GV_AD, GK(800));
    G_U(GV_AS, GK(800), GK(1000));
    G_L(GV_AR, GK(300), GK(900));
    if (G_CH(50)) {                              /* a slow swell between the two */
        gv_slot(s, 0, GVS_LFO1, GVD_MIX, rnd_in(r, GK(50), GK(200)));
        G_L(GV_L1F, GK(200), GK(1500));
        G_E(GV_L1W, 0);
    }
    return 65536;
}
static int32_t gv_pad(sound_t *s, rnd_t *r)
{
    G_U(GV_SH1, GK(200), GK(500));
    G_U(GV_SH2, GK(500), GK(900));
    G_U(GV_PW2, -GK(300), GK(300));
    G_U(GV_DET, GI(4), GI(15));
    G_P(GV_TUNE2, 0, 12, 12, 19);
    G_U(GV_NOISE, 0, GK(100));
    G_U(GV_TRK, GK(300), GK(800));
    gv_cut(s, r, GHZ(500), GHZ(2000));
    G_U(GV_RES, GK(100), GK(400));
    G_L(GV_AA, GK(40), GK(120));
    G_V(GV_AD, GK(1000));
    G_V(GV_AS, GK(1000));
    G_L(GV_AR, GK(600), GK(1500));
    if (G_CH(50)) {                              /* the cutoff moving with a slow LFO (x 10 kHz: 100 .. 400 Hz) */
        G_E(GV_TYPE, GVT_LPSV12);                /* (a moving cutoff: the cheapest filter) */
        gv_slot(s, 4, GVS_LFO1, GVD_CUT, rnd_in(r, GK(10), GK(40)));
        G_L(GV_L1F, GK(100), GK(600));
        G_E(GV_L1W, 0);
        G_V(GV_FENV, 0);
    } else {                                     /* or a filter swell and the pulse's width moving */
        G_E(GV_TYPE, GVT_LPSV12);
        gv_slot(s, 1, GVS_LFO2, GVD_PW2, rnd_in(r, GK(200), GK(500)));
        G_L(GV_L2F, GK(300), GK(2000));
        G_U(GV_FENV, GK(20), GK(80));
        G_L(GV_FA, GK(200), GK(700));
        G_L(GV_FD, GK(1000), GK(2000));
        G_V(GV_FS, GK(400));
        G_V(GV_FR, GK(800));
    }
    return 65536;
}
static int32_t gv_keys(sound_t *s, rnd_t *r)
{
    G_U(GV_SH1, GK(300), GK(500));
    G_U(GV_SH2, GK(750), GK(950));
    G_U(GV_PW2, -GK(200), GK(200));
    G_U(GV_MIX, -GK(400), GK(400));
    G_V(GV_TUNE2, G_CH(40) ? GI(12) : 0);
    G_U(GV_DET, 0, GI(6));
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    G_P(GV_TYPE, 3, 4, 1);
    G_U(GV_TRK, GK(800), GK(1000));
    gv_cut(s, r, GHZ(600), GHZ(1800));
    G_U(GV_RES, GK(100), GK(350));
    G_U(GV_FENV, GK(50), GK(180));
    G_L(GV_FD, GK(200), GK(700));
    G_U(GV_FS, GK(100), GK(250));
    G_V(GV_FR, GK(300));
    G_L(GV_AA, 0, GK(4));
    G_L(GV_AD, GK(600), GK(1800));
    G_U(GV_AS, GK(150), GK(450));
    G_L(GV_AR, GK(150), GK(400));
    return 65536;
}
static int32_t gv_sync(sound_t *s, rnd_t *r)       /* osc 2 hard-synced at a fixed tuning: a formant, not a sweep */
{
    G_E(GV_SYNC, 1);
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(500), GK(750));
    G_U(GV_MIX, GK(300), GK(800));
    G_U(GV_TUNE2, GI(4), GI(19));                /* the synced oscillator's tuning: the timbre */
    if (G_CH(30))
        G_V(GV_CUT, G_OFF);
    else {
        G_P(GV_TYPE, 2, 4);
        G_U(GV_TRK, GK(500), GK(1000));
        gv_cut(s, r, GHZ(2500), GHZ(8000));
        G_U(GV_RES, GK(50), GK(300));
    }
    G_V(GV_FENV, 0);
    G_L(GV_AA, 0, GK(8));
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(100), GK(250));
    if (G_CH(40)) {                              /* the aux envelope on the synced oscillator's shape */
        gv_slot(s, 0, GVS_AUX, GVD_SH2, rnd_in(r, GK(400), GK(900)));
        G_V(GV_XA, 0);
        G_L(GV_XD, GK(100), GK(500));
        G_V(GV_XS, 0);
        G_V(GV_XR, GK(100));
        G_U(GV_SH2, GK(300), GK(500));
    }
    return 65536;
}
static int32_t gv_bass(sound_t *s, rnd_t *r)
{
    G_U(GV_SH1, GK(500), GK(900));
    G_U(GV_SH2, GK(500), GK(900));
    G_U(GV_MIX, -GK(500), GK(200));
    G_V(GV_TUNE2, G_CH(50) ? -GI(12) : 0);
    G_U(GV_DET, 0, GI(8));
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    G_P(GV_TYPE, 0, 1, 3);
    G_U(GV_TRK, 0, GK(300));
    gv_cut(s, r, GHZ(200), GHZ(600));
    G_U(GV_RES, GK(150), GK(500));
    G_U(GV_FENV, GK(40), GK(140));
    G_L(GV_FD, GK(80), GK(300));
    G_U(GV_FS, 0, GK(200));
    G_V(GV_FR, GK(100));
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(300), GK(800));
    G_U(GV_AS, GK(600), GK(1000));
    G_L(GV_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gv_pwm(sound_t *s, rnd_t *r)
{
    G_V(GV_SH1, GK(1000));
    G_V(GV_SH2, GK(1000));
    gv_slot(s, 0, GVS_LFO1, GVD_PW1, rnd_in(r, GK(300), GK(700)));   /* (one moving width: two cost too much) */
    G_L(GV_L1F, GK(300), GK(3000));
    G_U(GV_PW2, -GK(350), GK(350));
    G_U(GV_DET, GI(2), GI(10));
    G_P(GV_TYPE, 4, 3);
    G_U(GV_TRK, GK(500), GK(1000));
    gv_cut(s, r, GHZ(1500), GHZ(5000));
    G_U(GV_RES, GK(100), GK(350));
    G_V(GV_FENV, 0);
    G_L(GV_AA, 0, GK(10));
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(100), GK(300));
    return 65536;
}
static void gv_two(sound_t *s, rnd_t *r)         /* two saws or pulses, a little apart */
{
    G_U(GV_SH1, GK(450), GK(700));
    G_U(GV_SH2, GK(500), GK(1000));
    G_U(GV_PW2, -GK(300), GK(300));
    G_U(GV_DET, 0, GI(8));
    G_U(GV_MIX, -GK(300), GK(300));
    G_V(GV_TUNE2, G_CH(30) ? GI(12) : 0);
}
static int32_t gv_formant(sound_t *s, rnd_t *r)    /* a fixed resonant band (the note does not move it): a vowel */
{
    gv_two(s, r);
    G_P(GV_TYPE, GVT_BPOB12, GVT_BPOB12, GVT_BPOB24);
    G_U(GV_TRK, 0, GK(150));
    gv_cut(s, r, GHZ(450), GHZ(1400));
    G_U(GV_RES, GK(250), GK(550));               /* (a wide band: one harmonic alone would read as the pitch) */
    G_V(GV_FENV, 0);
    G_L(GV_AA, 0, GK(8));
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(100), GK(300));
    return 65536;
}
static int32_t gv_vowel(sound_t *s, rnd_t *r)      /* the band moving slowly between vowels */
{
    int32_t c;
    gv_two(s, r);
    G_E(GV_TYPE, GVT_BPSV12);                    /* (a moving cutoff: SV, the cheaper) */
    G_U(GV_TRK, 0, GK(150));
    gv_cut(s, r, GHZ(500), GHZ(1200));
    c = s->val[GV_CUT];
    G_U(GV_RES, GK(250), GK(500));
    G_V(GV_FENV, 0);
    gv_slot(s, 4, GVS_LFO1, GVD_CUT, rnd_in(r, c * 3 / 10, c * 6 / 10) / 10000);
    G_L(GV_L1F, GK(300), GK(1500));
    G_P(GV_L1W, 0, 1);
    G_E(GV_L1R, 0);
    G_L(GV_AA, GK(2), GK(10));
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(150), GK(400));
    return 65536;
}
static int32_t gv_notch(sound_t *s, rnd_t *r)      /* a notch drifting through the spectrum: a phaser */
{
    int32_t c;
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(500), GK(1000));
    G_U(GV_DET, GI(5), GI(15));
    G_U(GV_NOISE, 0, GK(100));
    G_E(GV_TYPE, GVT_NSV12);                     /* (the 24 dB notch, its cutoff moving, costs too much) */
    G_V(GV_TRK, GK(1000));
    gv_cut(s, r, GHZ(600), GHZ(2000));
    c = s->val[GV_CUT];
    G_U(GV_RES, GK(200), GK(600));
    G_V(GV_FENV, 0);
    gv_slot(s, 4, GVS_LFO1, GVD_CUT, rnd_in(r, c * 4 / 10, c * 8 / 10) / 10000);
    G_L(GV_L1F, GK(100), GK(800));
    G_E(GV_L1W, 0);
    G_E(GV_L1R, 2);
    G_L(GV_AA, GK(2), GK(10));
    G_V(GV_AD, GK(800));
    G_U(GV_AS, GK(900), GK(1000));
    G_L(GV_AR, GK(300), GK(700));
    return 65536;
}
static int32_t gv_hppluck(sound_t *s, rnd_t *r)    /* a resonant high-pass just above the note: thin, glassy */
{
    G_U(GV_SH1, 0, GK(300));                     /* (triangles: a saw's edge, high-passed, is a spike) */
    G_U(GV_SH2, 0, GK(400));
    G_U(GV_DET, 0, GI(10));
    G_V(GV_TUNE2, G_CH(50) ? GI(12) : 0);
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    G_P(GV_TYPE, GVT_HPOB12, GVT_HPSV12);
    G_V(GV_TRK, GK(1000));
    gv_cut(s, r, GHZ(100), GHZ(500));
    G_U(GV_RES, GK(300), GK(500));
    G_U(GV_FENV, GK(10), GK(30));
    G_V(GV_FA, 0);
    G_L(GV_FD, GK(50), GK(150));
    G_V(GV_FS, 0);
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(300), GK(700));
    G_U(GV_AS, GK(100), GK(300));
    G_L(GV_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gv_pipe(sound_t *s, rnd_t *r)       /* triangles, a chiff of noise at the start (aux env on noise) */
{
    G_U(GV_SH1, 0, GK(100));
    G_U(GV_SH2, 0, GK(200));
    G_V(GV_TUNE2, G_CH(50) ? GI(12) : 0);
    G_U(GV_MIX, -GK(600), 0);
    G_U(GV_DET, 0, GI(4));
    G_U(GV_NOISE, GK(50), GK(150));
    gv_slot(s, 0, GVS_AUX, GVD_NOISE, rnd_in(r, GK(600), GK(1200)));
    G_V(GV_XA, 0);
    G_L(GV_XD, GK(30), GK(90));
    G_V(GV_XS, 0);
    G_V(GV_XR, GK(50));
    G_E(GV_TYPE, GVT_LPSV12);
    G_V(GV_TRK, GK(1000));
    gv_cut(s, r, GHZ(1500), GHZ(4000));
    G_U(GV_RES, GK(100), GK(300));
    G_V(GV_FENV, 0);
    G_L(GV_AA, GK(3), GK(10));
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(80), GK(250));
    return 65536;
}
static int32_t gv_shwave(sound_t *s, rnd_t *r)     /* S&H LFO stepping the oscillators' shapes */
{
    G_U(GV_SH1, GK(200), GK(800));
    G_U(GV_SH2, GK(300), GK(900));
    G_U(GV_PW1, -GK(300), GK(300));
    G_U(GV_PW2, -GK(300), GK(300));
    G_U(GV_DET, GI(3), GI(12));
    G_U(GV_MIX, -GK(400), GK(400));
    gv_slot(s, 0, GVS_LFO2, GVD_SH1, gsign(r, rnd_in(r, GK(600), GK(1400))));
    G_L(GV_L2F, GHZ(5), GHZ(14));
    G_E(GV_L2W, 5);
    G_E(GV_L2R, 0);
    G_E(GV_TYPE, GVT_LPSV12);
    G_U(GV_TRK, GK(500), GK(1000));
    gv_cut(s, r, GHZ(1500), GHZ(4000));
    G_U(GV_RES, GK(100), GK(400));
    G_V(GV_FENV, 0);
    G_L(GV_AA, 0, GK(5));
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gv_pwpluck(sound_t *s, rnd_t *r)    /* wide at the attack, narrowing (aux env on the width) */
{
    G_V(GV_SH1, GK(1000));
    G_V(GV_SH2, GK(1000));
    G_U(GV_PW1, GK(350), GK(450));
    G_U(GV_PW2, -GK(200), GK(200));
    G_V(GV_TUNE2, G_CH(40) ? GI(12) : 0);
    G_U(GV_DET, 0, GI(8));
    G_U(GV_MIX, -GK(500), 0);
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    gv_slot(s, 0, GVS_AUX, GVD_PW1, -rnd_in(r, GK(600), GK(1200)));
    G_V(GV_XA, 0);
    G_L(GV_XD, GK(80), GK(300));
    G_V(GV_XS, 0);
    G_V(GV_XR, GK(100));
    G_E(GV_TYPE, GVT_LPSV24);
    G_V(GV_TRK, GK(1000));
    gv_cut(s, r, GHZ(800), GHZ(2500));
    G_U(GV_RES, GK(100), GK(400));
    G_U(GV_FENV, GK(30), GK(100));
    G_V(GV_FA, 0);
    G_L(GV_FD, GK(80), GK(250));
    G_V(GV_FS, 0);
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(150), GK(450));
    G_U(GV_AS, 0, GK(100));
    G_L(GV_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gv_morph(sound_t *s, rnd_t *r)      /* the mix moving from osc 1 to a brighter osc 2 over the note */
{
    G_V(GV_SH1, GK(500));
    if (G_CH(50)) {
        G_E(GV_SYNC, 1);
        G_U(GV_SH2, GK(500), GK(1000));
        G_U(GV_TUNE2, GI(12), GI(24));
    } else {
        G_U(GV_SH2, GK(800), GK(1000));
        G_U(GV_PW2, -GK(400), GK(400));
        G_P(GV_TUNE2, 12, 24);                   /* (octaves: osc 2 alone keeps the note) */
    }
    G_V(GV_MIX, -GK(800));
    gv_slot(s, 0, GVS_AUX, GVD_MIX, rnd_in(r, GK(800), GK(1200)));   /* (to a blend: osc 1 keeps the note clear) */
    G_L(GV_XA, GK(50), GK(300));
    G_L(GV_XD, GK(300), GK(800));
    G_U(GV_XS, GK(300), GK(700));
    G_V(GV_XR, GK(200));
    G_E(GV_TYPE, GVT_LPSV12);
    G_V(GV_TRK, GK(1000));
    gv_cut(s, r, GHZ(2000), GHZ(6000));
    G_U(GV_RES, GK(100), GK(300));
    G_V(GV_FENV, 0);
    G_L(GV_AA, 0, GK(5));
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(150), GK(400));
    return 65536;
}
static int32_t gv_amring(sound_t *s, rnd_t *r)     /* an audio-rate LFO on the volume: AM sidebands, not in the scale */
{
    G_U(GV_SH1, 0, GK(500));
    G_U(GV_SH2, 0, GK(500));
    G_U(GV_MIX, -GK(1000), -GK(600));
    G_U(GV_DET, 0, GI(5));
    gv_slot(s, 0, GVS_LFO1, GVD_VOL, -rnd_in(r, GK(1200), GK(2000)));
    G_L(GV_L1F, GHZ(30), GHZ(95));
    G_E(GV_L1W, 1);
    G_E(GV_L1R, 0);
    if (G_CH(50))
        G_V(GV_CUT, G_OFF);
    else {
        G_E(GV_TYPE, GVT_LPSV12);
        G_V(GV_TRK, GK(1000));
        gv_cut(s, r, GHZ(2000), GHZ(6000));
        G_U(GV_RES, 0, GK(250));
    }
    G_V(GV_FENV, 0);
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(400), GK(1000));
    G_U(GV_AS, GK(500), GK(900));
    G_L(GV_AR, GK(100), GK(300));
    return 65536;
}
static int32_t gv_gate(sound_t *s, rnd_t *r)       /* a ramp on the volume locked to the tempo: re-struck 16ths .. 32nds */
{
    G_U(GV_SH1, GK(500), GK(1000));
    G_U(GV_SH2, GK(500), GK(1000));
    G_U(GV_DET, GI(4), GI(15));
    G_V(GV_TUNE2, G_CH(40) ? GI(12) : 0);
    gv_slot(s, 0, GVS_LFO1, GVD_VOL, -rnd_in(r, GK(1600), GK(2000)));
    G_E(GV_L1S, 1);
    G_P(GV_L1F, 11, 12, 13);                     /* (sync: 6, 4, 3 clock pulses) */
    G_E(GV_L1W, 2);                              /* ramp up: loud at the start of each step, fading */
    G_E(GV_L1R, 0);
    G_E(GV_TYPE, GVT_LPSV24);
    G_V(GV_TRK, GK(1000));
    gv_cut(s, r, GHZ(1000), GHZ(3500));
    G_U(GV_RES, GK(100), GK(350));
    G_V(GV_FENV, 0);
    G_V(GV_AA, 0);
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(900), GK(1000));
    G_L(GV_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gv_bowed(sound_t *s, rnd_t *r)      /* the aux envelope opening the filter slowly, a little noise */
{
    G_U(GV_SH1, GK(450), GK(550));
    G_U(GV_SH2, GK(450), GK(600));
    G_U(GV_DET, GI(6), GI(18));
    G_U(GV_MIX, -GK(300), GK(300));
    G_U(GV_NOISE, GK(30), GK(100));
    G_E(GV_TYPE, GVT_LPSV12);
    G_V(GV_TRK, GK(1000));
    gv_cut(s, r, GHZ(400), GHZ(900));
    G_U(GV_RES, GK(200), GK(450));
    G_V(GV_FENV, 0);
    gv_slot(s, 0, GVS_AUX, GVD_CUT, rnd_in(r, GK(150), GK(400)));   /* (x 10 kHz: + 1.5 .. 4 kHz) */
    G_L(GV_XA, GK(150), GK(400));
    G_V(GV_XD, GK(1000));
    G_U(GV_XS, GK(600), GK(900));
    G_V(GV_XR, GK(300));
    G_L(GV_AA, GK(40), GK(110));
    G_V(GV_AD, GK(1000));
    G_V(GV_AS, GK(1000));
    G_L(GV_AR, GK(300), GK(800));
    return 65536;
}
static int32_t gv_hollow(sound_t *s, rnd_t *r)     /* a notch on the second harmonic (cutoff = 2 x the note) */
{
    G_U(GV_SH1, 0, GK(300));
    G_U(GV_SH2, GK(750), GK(1000));
    G_U(GV_PW2, -GK(200), GK(200));
    G_U(GV_MIX, -GK(500), GK(300));
    G_V(GV_TUNE2, G_CH(40) ? GI(12) : 0);
    G_E(GV_TYPE, GVT_NSV12);
    G_V(GV_TRK, GK(2000));
    G_V(GV_CUT, G_F4X2);
    G_U(GV_RES, GK(300), GK(700));
    G_V(GV_FENV, 0);
    G_L(GV_AA, GK(2), GK(8));
    G_V(GV_AD, GK(600));
    G_U(GV_AS, GK(800), GK(1000));
    G_L(GV_AR, GK(150), GK(350));
    return 65536;
}
static const gen_fam_t GEN_VAP[] = {
    {"STRINGS", gv_strings, GR_PAD | GR_MID, 0, 1000},
    {"PAD", gv_pad, GR_PAD, GFL_SLOW, 1000},
    {"KEYS", gv_keys, GR_MID | GR_HIGH, 0, 1000},
    {"SYNC LEAD", gv_sync, GR_MID | GR_HIGH, 0, 1000},
    {"BASS", gv_bass, GR_BASS, 0, 1000},
    {"PWM LEAD", gv_pwm, GR_MID | GR_HIGH, 0, 1000},
    {"FORMANT", gv_formant, GR_MID, 0, 1000},
    {"VOWEL", gv_vowel, GR_MID | GR_PAD, 0, 1400},
    {"NOTCH", gv_notch, GR_PAD | GR_MID, 0, 1000},
    {"HP PLUCK", gv_hppluck, GR_HIGH, 0, 2400},
    {"PIPE", gv_pipe, GR_HIGH | GR_MID, 0, 1000},
    {"S/H WAVE", gv_shwave, GR_MID, GFL_EXP, 1000},
    {"PW PLUCK", gv_pwpluck, GR_HIGH | GR_MID, 0, 1000},
    {"MORPH", gv_morph, GR_MID, 0, 1000},
    {"AM RING", gv_amring, GR_MID | GR_HIGH, GFL_EXP, 1000},
    {"GATE", gv_gate, GR_MID | GR_PAD, 0, 1000},
    {"BOWED", gv_bowed, GR_PAD | GR_MID, GFL_SLOW, 1000},
    {"HOLLOW", gv_hollow, GR_MID | GR_HIGH, 0, 1000},
};

/* ================================================================== WTFM: two wavetable operators, 1 -> 2 (FM) */
enum { GW_R1, GW_R2, GW_FT1, GW_FT2, GW_SH1, GW_SH2, GW_U6, GW_U7, GW_OVS, GW_CH1, GW_CH2, GW_FM, GW_FB1, GW_FB2,
       GW_F21, GW_TYPE, GW_CUT, GW_RES, GW_FENV, GW_TRK, GW_XA, GW_XD, GW_XS, GW_XR, GW_AA, GW_AD, GW_AS, GW_AR,
       GW_FA, GW_FD, GW_FS, GW_FR, GW_L1F, GW_L1W, GW_L1R, GW_L1S, GW_L2F, GW_L2W, GW_L2R, GW_L2S, GW_GM, GW_GT,
       GW_VOL, GW_SLOT, GW_RST1 = 61, GW_RST2 };
enum { GWS_AMP, GWS_FILT, GWS_AUX, GWS_LFO1, GWS_LFO2 };
enum { GWD_P1, GWD_P2, GWD_SH1, GWD_SH2, GWD_FB1, GWD_FB2, GWD_F21, GWD_FM, GWD_CUT, GWD_AMPS, GWD_FILTS, GWD_AUXS,
       GWD_VOL, GWD_FILTD };
/* the characters (each three tables, SHAPE crossfading them) */
enum { GWC_SMOOTH, GWC_SHARP, GWC_WILD, GWC_ADD1, GWC_ADD2, GWC_ADD3, GWC_ADD5, GWC_ADD7, GWC_ADD11 };
/* the ratio enum: index i is (i + 1) / 4 (3: 1.00, 7: 2.00, 13: 3.50). Op 2 (the carrier: what is heard) stays at
 * 1 or 2 (the note, or its octave); op 1, the modulator, makes the timbre at any ratio */
/* slot k (0 .. 5): src -> dest x amt (multiplier: amt x range / 2: shape, feedback x 0.5, FM x 5, cutoff x 10 kHz,
 * VOLUME multiplies x ((src + 1) amt / 2 + 1)) */
static void gw_slot(sound_t *s, uint32_t k, uint32_t src, uint32_t dst, int32_t amt)
{
    gs(s, GW_SLOT + 3u * k, GI(src));
    gs(s, GW_SLOT + 3u * k + 1u, GI(dst));
    gs(s, GW_SLOT + 3u * k + 2u, amt);
}
/* the FM index decaying with the aux envelope: + amt x 5 at the note's start (FM is in cycles: 0.1 ~ 0.63 rad) */
static void gw_fmenv(sound_t *s, rnd_t *r, int32_t alo, int32_t ahi, int32_t dlo, int32_t dhi, int32_t sus)
{
    gw_slot(s, 0, GWS_AUX, GWD_FM, rnd_in(r, alo, ahi));
    G_V(GW_XA, 0);
    G_L(GW_XD, dlo, dhi);
    G_V(GW_XS, sus);
    G_V(GW_XR, GK(300));
}
static void gw_plain(sound_t *s)                 /* sines, the filter off, the phases reset on each note */
{
    G_V(GW_CUT, G_OFF);
    G_V(GW_FENV, 0);
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
}
static void gw_lp(sound_t *s, rnd_t *r, int32_t lo, int32_t hi)   /* a still LP SV12 tracking the note */
{
    G_E(GW_TYPE, 4);
    G_V(GW_TRK, GK(1000));
    G_L(GW_CUT, lo, hi);
    G_U(GW_RES, GK(50), GK(300));
    G_V(GW_FENV, 0);
}
static int32_t gw_bell(sound_t *s, rnd_t *r)
{
    gw_plain(s);
    G_P(GW_R1, 13, 10, 16, 6, 20, 27);           /* 3.5, 2.75, 4.25, 1.75, 5.25, 7.0: inharmonic */
    G_P(GW_R2, 3, 3, 7);
    G_U(GW_FT1, -GI(8), GI(8));
    G_U(GW_FM, GK(50), GK(150));
    gw_fmenv(s, r, GK(40), GK(100), GK(400), GK(1200), 0);
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(900), GK(2200));
    G_U(GW_AS, 0, GK(100));
    G_L(GW_AR, GK(400), GK(1000));
    return 65536;
}
static int32_t gw_epiano(sound_t *s, rnd_t *r)
{
    G_P(GW_R1, 3, 3, 7);
    G_V(GW_R2, GI(3));
    G_U(GW_FT1, -GI(4), GI(4));
    G_U(GW_FB1, 0, GK(60));
    G_U(GW_FM, GK(20), GK(80));
    gw_fmenv(s, r, GK(30), GK(80), GK(200), GK(700), 0);
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    if (G_CH(50)) {
        G_V(GW_CUT, G_OFF);
        G_V(GW_FENV, 0);
    } else
        gw_lp(s, r, GHZ(3000), GHZ(8000));
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(1200), GK(2500));
    G_U(GW_AS, GK(150), GK(350));
    G_L(GW_AR, GK(250), GK(500));
    if (G_CH(25)) {                              /* tremolo */
        gw_slot(s, 1, GWS_LFO1, GWD_VOL, rnd_in(r, GK(50), GK(150)));
        G_L(GW_L1F, GHZ(3), GHZ(6));
        G_E(GW_L1W, 1);
    }
    return 65536;
}
static int32_t gw_bass(sound_t *s, rnd_t *r)
{
    G_P(GW_R1, 3, 7, 1);
    G_V(GW_R2, GI(3));
    G_U(GW_FM, GK(80), GK(250));
    G_U(GW_FB1, GK(30), GK(150));
    gw_fmenv(s, r, GK(50), GK(200), GK(60), GK(250), rnd_in(r, 0, GK(300)));
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    if (G_CH(50)) {
        G_E(GW_TYPE, 0);
        G_U(GW_TRK, 0, GK(300));
        G_L(GW_CUT, GHZ(500), GHZ(1500));
        G_U(GW_RES, 0, GK(300));
        G_U(GW_FENV, GK(50), GK(150));
        G_L(GW_FD, GK(100), GK(300));
    } else {
        G_V(GW_CUT, G_OFF);
        G_V(GW_FENV, 0);
    }
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(300), GK(800));
    G_U(GW_AS, GK(600), GK(1000));
    G_L(GW_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gw_metal(sound_t *s, rnd_t *r)
{
    G_P(GW_R1, 12, 22, 30, 36, 44, 17);          /* 3.25, 5.75, 7.75, 9.25, 11.25, 4.5 */
    G_P(GW_R2, 3, 3, 7);
    G_P(GW_CH1, 1, 2);
    G_U(GW_SH1, GK(200), GK(800));
    G_U(GW_FM, GK(200), GK(800));
    G_U(GW_FB1, GK(50), GK(200));
    gw_fmenv(s, r, GK(50), GK(200), GK(100), GK(400), 0);
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    if (G_CH(30)) {
        G_E(GW_TYPE, 8);                         /* HP SV12 */
        G_V(GW_TRK, 0);
        G_L(GW_CUT, GHZ(200), GHZ(600));
        G_V(GW_RES, GK(200));
        G_V(GW_FENV, 0);
    } else {
        G_V(GW_CUT, G_OFF);
        G_V(GW_FENV, 0);
    }
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(150), GK(600));
    G_U(GW_AS, 0, GK(250));
    G_L(GW_AR, GK(100), GK(300));
    return 65536;
}
static int32_t gw_glass(sound_t *s, rnd_t *r)
{
    gw_plain(s);
    G_P(GW_R1, 15, 19, 23, 27, 31, 35);          /* 4 .. 9: high, harmonic */
    G_P(GW_R2, 3, 7);
    G_U(GW_FM, GK(30), GK(120));
    G_U(GW_FB2, 0, GK(50));
    gw_fmenv(s, r, GK(30), GK(80), GK(150), GK(500), 0);
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(800), GK(2000));
    G_U(GW_AS, GK(100), GK(300));
    G_L(GW_AR, GK(400), GK(900));
    return 65536;
}
static int32_t gw_pad(sound_t *s, rnd_t *r)        /* a wavetable's shape moving under a slow LFO */
{
    G_V(GW_R1, GI(3));
    G_U(GW_FM, 0, GK(30));
    G_P(GW_CH2, 2, 5, 6, 7, 4, 3);               /* wilder, add 3, 5, 7, 2, 1 */
    G_U(GW_SH2, GK(200), GK(800));
    gw_slot(s, 0, GWS_LFO1, GWD_SH2, rnd_in(r, GK(200), GK(500)));
    G_L(GW_L1F, GK(100), GK(800));
    G_P(GW_L1W, 0, 1);
    if (G_CH(50))                                /* (the filter off half the time: the moving shape costs) */
        G_V(GW_CUT, G_OFF);
    else {
        G_E(GW_TYPE, 4);                         /* LP SV12 */
        G_V(GW_TRK, GK(500));
        G_L(GW_CUT, GHZ(1500), GHZ(5000));
        G_U(GW_RES, GK(100), GK(300));
    }
    G_V(GW_FENV, 0);
    G_L(GW_AA, GK(30), GK(110));
    G_V(GW_AD, GK(1000));
    G_V(GW_AS, GK(1000));
    G_L(GW_AR, GK(500), GK(1300));
    return 65536;
}
static int32_t gw_pluck(sound_t *s, rnd_t *r)
{
    gw_plain(s);
    G_P(GW_R1, 3, 7, 11);
    G_P(GW_R2, 3, 7);
    G_U(GW_FM, GK(20), GK(100));
    G_U(GW_FB1, 0, GK(100));
    gw_fmenv(s, r, GK(100), GK(300), GK(40), GK(150), 0);
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(120), GK(400));
    G_U(GW_AS, 0, GK(50));
    G_L(GW_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gw_feedback(sound_t *s, rnd_t *r)   /* the operators' feedback past ~0.3: chaotic, noisy */
{
    G_U(GW_FB1, GK(250), GK(600));
    G_U(GW_FB2, 0, GK(300));
    G_U(GW_FM, GK(100), GK(500));
    G_U(GW_R1, 0, GI(31));
    G_P(GW_R2, 3, 7);
    G_U(GW_CH1, 0, GI(8));
    gw_fmenv(s, r, -GK(100), GK(100), GK(100), GK(400), 0);
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    if (G_CH(50)) {
        G_E(GW_TYPE, 12);                        /* BP SV12 (the OB costs too much with this feedback) */
        G_V(GW_TRK, GK(1000));
        G_L(GW_CUT, GHZ(800), GHZ(3000));
        G_U(GW_RES, GK(300), GK(700));
        G_V(GW_FENV, 0);
    } else {
        G_V(GW_CUT, G_OFF);
        G_V(GW_FENV, 0);
    }
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(150), GK(600));
    G_U(GW_AS, 0, GK(400));
    G_L(GW_AR, GK(100), GK(300));
    return 65536;
}
static int32_t gw_gong(sound_t *s, rnd_t *r)       /* a low inharmonic modulator, its index swelling then dying */
{
    gw_plain(s);
    G_P(GW_R1, 6, 10, 12, 4, 2);                 /* 1.75, 2.75, 3.25, 1.25, 0.75 */
    G_V(GW_R2, GI(3));
    G_U(GW_FT1, -GI(15), GI(15));
    G_U(GW_FM, GK(50), GK(150));
    G_U(GW_FB1, 0, GK(100));
    gw_slot(s, 0, GWS_AUX, GWD_FM, rnd_in(r, GK(30), GK(90)));
    G_L(GW_XA, GK(20), GK(120));
    G_L(GW_XD, GK(600), GK(1600));
    G_U(GW_XS, 0, GK(50));
    G_V(GW_XR, GK(500));
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(1500), GK(3000));
    G_U(GW_AS, 0, GK(150));
    G_L(GW_AR, GK(500), GK(1200));
    return 65536;
}
static int32_t gw_ring(sound_t *s, rnd_t *r)       /* a steady index at a ratio off the harmonics: ring-modulator sidebands */
{
    gw_plain(s);
    G_P(GW_R1, 8, 6, 12, 18, 4);                 /* 2.25, 1.75, 3.25, 4.75, 1.25 */
    G_V(GW_R2, GI(3));
    G_U(GW_FT1, -GI(5), GI(5));
    G_U(GW_FM, GK(80), GK(200));                 /* (the carrier stays the loudest partial: the note is heard) */
    if (G_CH(50)) {                              /* the index breathing slowly */
        gw_slot(s, 1, GWS_LFO1, GWD_FM, rnd_in(r, GK(10), GK(30)));
        G_L(GW_L1F, GK(200), GK(1000));
        G_E(GW_L1W, 0);
    }
    G_L(GW_AA, 0, GK(5));
    G_L(GW_AD, GK(500), GK(1200));
    G_U(GW_AS, GK(500), GK(850));
    G_L(GW_AR, GK(150), GK(400));
    return 65536;
}
static int32_t gw_formant(sound_t *s, rnd_t *r)    /* carrier at the octave, a saw-ish modulator at the note: a formant */
{
    G_V(GW_R1, GI(3));
    G_V(GW_R2, GI(7));
    G_U(GW_FB1, GK(50), GK(250));
    G_U(GW_FM, GK(150), GK(350));
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    gw_slot(s, 0, GWS_LFO1, GWD_FM, rnd_in(r, GK(10), GK(40)));   /* (the vowel moving) */
    G_L(GW_L1F, GK(300), GK(1500));
    G_E(GW_L1W, 0);
    G_E(GW_L1R, 0);
    if (G_CH(50)) {                              /* a fixed band: the second formant */
        G_E(GW_TYPE, 12);                        /* (BP SV12: with the moving index, the cheaper band) */
        G_V(GW_TRK, 0);
        G_L(GW_CUT, GHZ(600), GHZ(1400));
        G_U(GW_RES, GK(300), GK(600));
        G_V(GW_FENV, 0);
    } else {
        G_V(GW_CUT, G_OFF);
        G_V(GW_FENV, 0);
    }
    G_L(GW_AA, 0, GK(8));
    G_V(GW_AD, GK(500));
    G_U(GW_AS, GK(850), GK(1000));
    G_L(GW_AR, GK(100), GK(300));
    return 65536;
}
static int32_t gw_tine(sound_t *s, rnd_t *r)       /* a high modulator struck hard and gone in tens of ms */
{
    gw_plain(s);
    G_P(GW_R1, 27, 35, 39, 43, 47);              /* 7, 9, 10, 11, 12 */
    G_V(GW_R2, GI(3));
    G_U(GW_FM, GK(10), GK(40));
    gw_fmenv(s, r, GK(50), GK(150), GK(15), GK(60), 0);
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(800), GK(1800));
    G_U(GW_AS, GK(50), GK(250));
    G_L(GW_AR, GK(200), GK(500));
    return 65536;
}
static int32_t gw_reed(sound_t *s, rnd_t *r)       /* a saw-ish modulator (feedback) at 1 or 2, the index breathing */
{
    G_P(GW_R1, 3, 7);
    G_V(GW_R2, GI(3));
    G_U(GW_FB1, GK(150), GK(350));
    G_U(GW_FM, GK(100), GK(250));
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    gw_slot(s, 0, GWS_LFO2, GWD_FM, rnd_in(r, GK(5), GK(20)));
    G_L(GW_L2F, GK(200), GK(800));
    G_E(GW_L2W, 1);
    gw_lp(s, r, GHZ(1500), GHZ(4000));
    G_L(GW_AA, GK(4), GK(10));
    G_V(GW_AD, GK(500));
    G_U(GW_AS, GK(850), GK(1000));
    G_L(GW_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gw_grain(sound_t *s, rnd_t *r)      /* S&H on the wavetable and the level, 10 .. 35 a second: grains */
{
    G_P(GW_CH2, GWC_WILD, GWC_ADD5, GWC_ADD7, GWC_ADD11, GWC_SHARP);
    G_U(GW_SH2, GK(300), GK(700));
    G_P(GW_R1, 3, 7);
    G_U(GW_FM, 0, GK(50));
    gw_slot(s, 0, GWS_LFO1, GWD_SH2, gsign(r, rnd_in(r, GK(600), GK(1400))));
    G_L(GW_L1F, GHZ(10), GHZ(30));
    G_E(GW_L1W, 5);
    gw_slot(s, 1, GWS_LFO2, GWD_VOL, -rnd_in(r, GK(300), GK(600)));
    G_L(GW_L2F, GHZ(12), GHZ(35));
    G_E(GW_L2W, 5);
    G_V(GW_CUT, G_OFF);
    G_V(GW_FENV, 0);
    G_L(GW_AA, GK(30), GK(110));
    G_V(GW_AD, GK(1000));
    G_V(GW_AS, GK(1000));
    G_L(GW_AR, GK(500), GK(1300));
    return 65536;
}
static int32_t gw_scan(sound_t *s, rnd_t *r)       /* the aux envelope scanning the wavetable during the note */
{
    G_P(GW_CH2, GWC_WILD, GWC_SHARP, GWC_ADD5, GWC_ADD7, GWC_ADD11, GWC_ADD2, GWC_ADD3);
    G_U(GW_SH2, 0, GK(200));
    G_P(GW_R1, 3, 7);
    G_U(GW_FM, 0, GK(80));
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    gw_slot(s, 0, GWS_AUX, GWD_SH2, rnd_in(r, GK(1200), GK(2000)));
    G_L(GW_XA, 0, GK(400));
    G_L(GW_XD, GK(500), GK(1500));
    G_U(GW_XS, GK(200), GK(600));
    G_V(GW_XR, GK(300));
    if (G_CH(50))
        G_V(GW_CUT, G_OFF);
    else
        gw_lp(s, r, GHZ(2000), GHZ(6000));
    G_V(GW_FENV, 0);
    G_L(GW_AA, 0, GK(5));
    G_L(GW_AD, GK(600), GK(1500));
    G_U(GW_AS, GK(600), GK(1000));
    G_L(GW_AR, GK(150), GK(400));
    return 65536;
}
static int32_t gw_bowed(sound_t *s, rnd_t *r)      /* the index swelling in, the modulator's feedback wavering */
{
    G_P(GW_R1, 3, 7);
    G_V(GW_R2, GI(3));
    G_U(GW_FB1, GK(50), GK(200));                /* (+ the LFO: below ~0.3, where feedback turns to noise) */
    G_U(GW_FM, GK(20), GK(60));
    G_E(GW_RST1, 1);
    G_E(GW_RST2, 1);
    gw_slot(s, 0, GWS_AUX, GWD_FM, rnd_in(r, GK(30), GK(80)));
    G_L(GW_XA, GK(150), GK(400));
    G_V(GW_XD, GK(1000));
    G_U(GW_XS, GK(700), GK(1000));
    G_V(GW_XR, GK(300));
    gw_slot(s, 1, GWS_LFO1, GWD_FB1, rnd_in(r, GK(50), GK(150)));
    G_L(GW_L1F, GK(150), GK(600));
    G_E(GW_L1W, 0);
    if (G_CH(50))
        G_V(GW_CUT, G_OFF);
    else
        gw_lp(s, r, GHZ(2000), GHZ(5000));
    G_V(GW_FENV, 0);
    G_L(GW_AA, GK(40), GK(110));
    G_V(GW_AD, GK(1000));
    G_V(GW_AS, GK(1000));
    G_L(GW_AR, GK(400), GK(1000));
    return 65536;
}
static int32_t gw_clang(sound_t *s, rnd_t *r)      /* a sharp inharmonic modulator, struck hard, op 2 fed back */
{
    gw_plain(s);
    G_P(GW_R1, 10, 12, 18, 22, 26, 30);          /* 2.75, 3.25, 4.75, 5.75, 6.75, 7.75 */
    G_P(GW_R2, 3, 3, 7);
    if (G_CH(50)) {
        G_E(GW_CH1, GWC_SHARP);
        G_U(GW_SH1, 0, GK(500));
    }
    G_U(GW_FM, GK(100), GK(300));
    G_U(GW_FB2, GK(50), GK(200));
    gw_fmenv(s, r, GK(100), GK(250), GK(40), GK(150), 0);
    if (G_CH(50)) {
        G_E(GW_TYPE, 8);                         /* HP SV12 */
        G_V(GW_TRK, GK(1000));
        G_L(GW_CUT, GHZ(200), GHZ(500));
        G_U(GW_RES, GK(200), GK(400));
    }
    G_V(GW_AA, 0);
    G_L(GW_AD, GK(150), GK(450));
    G_U(GW_AS, 0, GK(100));
    G_L(GW_AR, GK(100), GK(300));
    return 65536;
}
static int32_t gw_fbpad(sound_t *s, rnd_t *r)      /* the modulator's feedback drifting from clean to noisy, slowly */
{
    G_P(GW_R1, 3, 7);
    G_V(GW_R2, GI(3));
    G_U(GW_FM, GK(50), GK(200));
    G_U(GW_FB1, GK(50), GK(150));
    gw_slot(s, 0, GWS_LFO1, GWD_FB1, rnd_in(r, GK(300), GK(600)));
    G_L(GW_L1F, GK(100), GK(400));
    G_E(GW_L1W, 0);
    if (G_CH(50))
        G_V(GW_CUT, G_OFF);
    else
        gw_lp(s, r, GHZ(1500), GHZ(4000));
    G_V(GW_FENV, 0);
    G_L(GW_AA, GK(30), GK(110));
    G_V(GW_AD, GK(1000));
    G_V(GW_AS, GK(1000));
    G_L(GW_AR, GK(500), GK(1300));
    return 65536;
}
static const gen_fam_t GEN_WTFM[] = {
    {"BELL", gw_bell, GR_HIGH | GR_MID, 0, 1000},
    {"E PIANO", gw_epiano, GR_MID, 0, 1000},
    {"FM BASS", gw_bass, GR_BASS, 0, 1000},
    {"METAL FM", gw_metal, GR_HIGH, GFL_EXP, 1200},
    {"GLASS BELL", gw_glass, GR_HIGH, 0, 1000},
    {"WT PAD", gw_pad, GR_PAD, GFL_SLOW, 1000},
    {"FM PLUCK", gw_pluck, GR_HIGH | GR_MID, 0, 1400},
    {"FEEDBACK", gw_feedback, GR_MID, GFL_EXP, 1300},
    {"GONG", gw_gong, GR_MID | GR_BASS, 0, 1000},
    {"RING TONE", gw_ring, GR_MID | GR_HIGH, 0, 1000},
    {"FORMANT", gw_formant, GR_MID, 0, 1000},
    {"TINE", gw_tine, GR_HIGH | GR_MID, 0, 1000},
    {"REED", gw_reed, GR_MID, 0, 1000},
    {"GRAIN PAD", gw_grain, GR_PAD, GFL_SLOW | GFL_EXP, 1000},
    {"WT SCAN", gw_scan, GR_MID | GR_HIGH, 0, 1000},
    {"BOWED", gw_bowed, GR_PAD | GR_MID, GFL_SLOW, 1000},
    {"CLANG", gw_clang, GR_HIGH, GFL_EXP, 1000},
    {"FB PAD", gw_fbpad, GR_PAD, GFL_SLOW, 1000},
};

/* ================================================================== DIRT: oscillator, shaper, filter */
enum { GD_SHAPE, GD_SUB, GD_NOISE, GD_WARP, GD_SUBM, GD_WLFO, GD_WENV, GD_PLFO, GD_SHP, GD_AMT, GD_ROUTE, GD_MENV,
       GD_PRE, GD_CUT, GD_RES, GD_TYPE, GD_TRK, GD_FLFO, GD_FENV, GD_MA, GD_MD, GD_MS, GD_MR, GD_AA, GD_AD, GD_AS,
       GD_AR, GD_LF, GD_LW, GD_LR, GD_GM, GD_GT, GD_VOL };
enum { GDS_NONE, GDS_SAT, GDS_OVER, GDS_DIODE, GDS_FUZZ, GDS_ASYM, GDS_SIN, GDS_SINP, GDS_RECT, GDS_BIT, GDS_BITP,
       GDS_REDUX, GDS_REDUXP, GDS_BITREDUX };
static void gd_cut(sound_t *s, rnd_t *r, int32_t lo, int32_t hi)
{
    gs(s, GD_CUT, glog(r, lo, hi) + s->val[GD_TRK] * 300);
}
static int32_t gd_fuzzbass(sound_t *s, rnd_t *r)
{
    G_U(GD_SHAPE, GK(300), GK(600));
    G_U(GD_SUB, GK(300), GK(700));
    G_P(GD_SUBM, 0, 1, 2);
    G_P(GD_SHP, GDS_FUZZ, GDS_OVER, GDS_DIODE, GDS_SAT);
    G_U(GD_AMT, GK(300), GK(700));
    G_U(GD_PRE, 0, GK(400));
    G_E(GD_ROUTE, 0);                            /* shaper, then the filter */
    G_P(GD_TYPE, 2, 1);
    G_U(GD_TRK, 0, GK(300));
    gd_cut(s, r, GHZ(300), GHZ(1100));
    G_U(GD_RES, GK(50), GK(350));
    G_U(GD_FENV, GK(20), GK(80));
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(80), GK(300));
    G_U(GD_MS, 0, GK(200));
    G_V(GD_MR, GK(100));
    G_V(GD_AA, 0);
    G_L(GD_AD, GK(400), GK(1000));
    G_U(GD_AS, GK(700), GK(1000));
    G_L(GD_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gd_lead(sound_t *s, rnd_t *r)
{
    G_U(GD_SHAPE, GK(500), GK(850));
    G_U(GD_SUB, 0, GK(300));
    G_P(GD_SHP, GDS_SAT, GDS_OVER, GDS_ASYM, GDS_DIODE);
    G_U(GD_AMT, GK(400), GK(850));
    G_U(GD_PRE, GK(100), GK(500));
    G_P(GD_ROUTE, 0, 1);
    G_P(GD_TYPE, 0, 1);
    G_U(GD_TRK, GK(500), GK(1000));
    gd_cut(s, r, GHZ(1500), GHZ(6000));
    G_U(GD_RES, GK(100), GK(400));
    G_U(GD_FENV, GK(20), GK(60));
    G_V(GD_MD, GK(300));
    G_L(GD_AA, GK(2), GK(10));
    G_V(GD_AD, GK(500));
    G_U(GD_AS, GK(850), GK(1000));
    G_L(GD_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gd_pad(sound_t *s, rnd_t *r)
{
    G_U(GD_SHAPE, GK(200), GK(500));
    G_U(GD_WARP, -GK(300), GK(300));
    G_U(GD_WLFO, GK(100), GK(350));
    G_L(GD_LF, GK(150), GK(1000));
    G_P(GD_LW, 0, 1);
    G_P(GD_SHP, GDS_SIN, GDS_SAT, GDS_DIODE, GDS_ASYM);
    G_U(GD_AMT, GK(150), GK(400));
    G_U(GD_NOISE, -GK(150), -GK(30));
    G_E(GD_TYPE, 1);
    G_V(GD_TRK, GK(500));
    gd_cut(s, r, GHZ(800), GHZ(2500));
    G_U(GD_RES, GK(100), GK(350));
    G_U(GD_FENV, GK(20), GK(60));
    G_L(GD_MA, GK(100), GK(400));
    G_V(GD_MD, GK(1000));
    G_V(GD_MS, GK(500));
    G_V(GD_MR, GK(600));
    G_L(GD_AA, GK(30), GK(110));
    G_V(GD_AD, GK(1000));
    G_V(GD_AS, GK(1000));
    G_L(GD_AR, GK(500), GK(1300));
    return 65536;
}
static int32_t gd_noise(sound_t *s, rnd_t *r)      /* noise through a resonant band on the note, then shaped */
{
    G_V(GD_NOISE, G_CH(60) ? rnd_in(r, GK(600), GK(1000)) : rnd_in(r, -GK(1000), -GK(600)));
    G_U(GD_SHAPE, 0, GK(1000));
    G_P(GD_SHP, GDS_SAT, GDS_FUZZ, GDS_OVER);   /* (after the filter: its peaks held in) */
    G_U(GD_AMT, GK(300), GK(800));
    G_U(GD_PRE, GK(300), GK(800));
    G_E(GD_ROUTE, 1);
    G_E(GD_TYPE, 0);
    G_U(GD_RES, GK(600), GK(740));               /* (narrow, just below self-oscillation: it sings the note) */
    if (G_CH(50)) {                              /* (the band on the note itself, or on its octave) */
        G_V(GD_TRK, GK(1000));
        G_V(GD_CUT, G_F4);
    } else {
        G_V(GD_TRK, GK(2000));
        G_V(GD_CUT, G_F4X2);
    }
    G_V(GD_FENV, 0);
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(50), GK(200));
    G_V(GD_MS, 0);
    G_V(GD_AA, 0);
    G_L(GD_AD, GK(60), GK(300));
    G_U(GD_AS, 0, GK(150));
    G_L(GD_AR, GK(60), GK(200));
    return 65536;
}
static int32_t gd_broken(sound_t *s, rnd_t *r)     /* bit crushing and sample-rate reduction */
{
    G_U(GD_SHAPE, GK(500), GK(1000));
    G_U(GD_WARP, -GK(500), GK(500));
    G_P(GD_SHP, GDS_BITP, GDS_REDUX, GDS_REDUXP, GDS_BITREDUX, GDS_BIT);
    if (s->val[GD_SHP] >= GI(GDS_REDUX)) {       /* (a hold of 4 frames, still: aliases that move, or fall near */
        G_U(GD_AMT, GK(100), GK(280));           /*  the note, would be pitches of their own) */
        G_V(GD_MENV, 0);
    } else {
        G_U(GD_AMT, GK(300), GK(850));
        G_U(GD_MENV, -GK(400), GK(400));
    }
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(100), GK(400));
    G_V(GD_MS, 0);
    G_E(GD_ROUTE, 1);
    if (G_CH(30))
        G_V(GD_CUT, G_OFF);
    else {
        G_P(GD_TYPE, 1, 2);
        G_U(GD_TRK, GK(500), GK(1000));
        gd_cut(s, r, GHZ(1500), GHZ(8000));
        G_U(GD_RES, 0, GK(300));
    }
    G_V(GD_FENV, 0);
    G_V(GD_AA, 0);
    G_L(GD_AD, GK(200), GK(800));
    G_U(GD_AS, GK(300), GK(800));
    G_L(GD_AR, GK(80), GK(250));
    return 65536;
}
static int32_t gd_warp(sound_t *s, rnd_t *r)       /* phase distortion opening and closing with the mod envelope */
{
    G_U(GD_SHAPE, 0, GK(500));
    G_U(GD_WARP, -GK(200), GK(200));
    G_V(GD_WENV, G_CH(50) ? rnd_in(r, GK(400), GK(1000)) : rnd_in(r, -GK(1000), -GK(400)));
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(40), GK(200));
    G_V(GD_MS, 0);
    G_P(GD_SHP, GDS_SAT, GDS_NONE);
    G_U(GD_AMT, GK(100), GK(300));
    if (G_CH(50))
        G_V(GD_CUT, G_OFF);
    else {
        G_E(GD_TYPE, 0);
        G_U(GD_TRK, GK(500), GK(1000));
        gd_cut(s, r, GHZ(2000), GHZ(8000));
        G_U(GD_RES, 0, GK(300));
    }
    G_V(GD_FENV, 0);
    G_V(GD_AA, 0);
    G_L(GD_AD, GK(150), GK(450));
    G_U(GD_AS, 0, GK(80));
    G_L(GD_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gd_fold(sound_t *s, rnd_t *r)       /* a sine folded (SIN shapers), the fold moving with the env */
{
    G_U(GD_SHAPE, 0, GK(300));
    G_U(GD_SUB, 0, GK(300));
    G_P(GD_SHP, GDS_SIN, GDS_SINP);
    G_U(GD_AMT, GK(100), GK(450));
    G_U(GD_MENV, GK(100), GK(400));
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(100), GK(500));
    G_U(GD_MS, GK(200), GK(500));
    G_U(GD_PRE, 0, GK(300));
    G_E(GD_ROUTE, 1);
    if (G_CH(50))
        G_V(GD_CUT, G_OFF);
    else {
        G_E(GD_TYPE, 0);
        G_U(GD_TRK, GK(500), GK(1000));
        gd_cut(s, r, GHZ(2000), GHZ(7000));
        G_U(GD_RES, GK(100), GK(300));
    }
    G_V(GD_FENV, 0);
    G_L(GD_AA, 0, GK(8));
    G_V(GD_AD, GK(500));
    G_U(GD_AS, GK(800), GK(1000));
    G_L(GD_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gd_foldbass(sound_t *s, rnd_t *r)   /* sine + sub folded hard at the attack, settling; then an LP */
{
    G_U(GD_SHAPE, 0, GK(200));
    G_U(GD_SUB, GK(300), GK(600));
    G_P(GD_SUBM, 0, 2);
    G_P(GD_SHP, GDS_SIN, GDS_SINP);
    G_U(GD_AMT, GK(100), GK(300));
    G_U(GD_MENV, GK(200), GK(500));
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(80), GK(300));
    G_U(GD_MS, 0, GK(200));
    G_V(GD_MR, GK(100));
    G_U(GD_PRE, 0, GK(200));
    G_E(GD_ROUTE, 0);
    G_E(GD_TYPE, 1);
    G_U(GD_TRK, 0, GK(300));
    gd_cut(s, r, GHZ(800), GHZ(2500));
    G_U(GD_RES, 0, GK(300));
    G_V(GD_FENV, 0);
    G_V(GD_AA, 0);
    G_L(GD_AD, GK(400), GK(1000));
    G_U(GD_AS, GK(700), GK(1000));
    G_L(GD_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gd_rect(sound_t *s, rnd_t *r)       /* half / full-wave rectified sine or triangle: hollow, octave-ish */
{
    G_U(GD_SHAPE, 0, GK(300));
    G_E(GD_SHP, GDS_RECT);
    G_U(GD_AMT, GK(300), GK(800));               /* (still: a moving amount would trade the note for its octave) */
    G_V(GD_MENV, 0);
    G_E(GD_ROUTE, 0);
    G_E(GD_TYPE, 0);
    G_V(GD_TRK, GK(1000));
    gd_cut(s, r, GHZ(1500), GHZ(5000));
    G_U(GD_RES, GK(100), GK(300));
    G_U(GD_FENV, GK(30), GK(100));               /* (the timbre moves with the filter instead) */
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(100), GK(400));
    G_U(GD_MS, GK(200), GK(500));
    G_V(GD_MR, GK(200));
    G_L(GD_AA, 0, GK(5));
    G_V(GD_AD, GK(500));
    G_U(GD_AS, GK(800), GK(1000));
    G_L(GD_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gd_bitbass(sound_t *s, rnd_t *r)    /* a bass crushed to a few levels after its filter */
{
    G_U(GD_SHAPE, GK(300), GK(700));
    G_U(GD_SUB, GK(300), GK(600));
    G_P(GD_SUBM, 0, 2);
    if (G_CH(50)) {
        G_E(GD_SHP, GDS_BITP);
        G_U(GD_AMT, GK(500), GK(800));
    } else {
        G_E(GD_SHP, GDS_BIT);
        G_U(GD_AMT, GK(600), GK(900));
    }
    G_E(GD_ROUTE, 1);
    G_E(GD_TYPE, 1);
    G_U(GD_TRK, 0, GK(300));
    gd_cut(s, r, GHZ(400), GHZ(1200));
    G_U(GD_RES, GK(100), GK(300));
    G_U(GD_FENV, GK(20), GK(60));
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(100), GK(300));
    G_V(GD_MS, 0);
    G_V(GD_AA, 0);
    G_L(GD_AD, GK(300), GK(800));
    G_U(GD_AS, GK(600), GK(1000));
    G_L(GD_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gd_redux(sound_t *s, rnd_t *r)      /* sample-rate reduction: aliasing, the hold moving with the env */
{
    G_U(GD_SHAPE, GK(400), GK(900));
    G_U(GD_WARP, -GK(300), GK(300));
    G_P(GD_SHP, GDS_REDUX, GDS_REDUXP);
    G_U(GD_AMT, GK(100), GK(380));               /* (a hold of 4 or 8 frames, still: see BROKEN) */
    G_V(GD_MENV, 0);
    G_V(GD_WENV, gsign(r, rnd_in(r, GK(300), GK(700))));   /* (the timbre moves with the warp) */
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(100), GK(400));
    G_U(GD_MS, 0, GK(300));
    G_E(GD_ROUTE, 1);
    if (G_CH(40))
        G_V(GD_CUT, G_OFF);
    else {
        G_E(GD_TYPE, 0);
        G_V(GD_TRK, GK(1000));
        gd_cut(s, r, GHZ(3000), GHZ(8000));
        G_U(GD_RES, 0, GK(250));
    }
    G_V(GD_FENV, 0);
    G_L(GD_AA, 0, GK(5));
    G_V(GD_AD, GK(500));
    G_U(GD_AS, GK(700), GK(1000));
    G_L(GD_AR, GK(100), GK(250));
    return 65536;
}
static int32_t gd_warpbass(sound_t *s, rnd_t *r)   /* phase distortion on a saw / pulse bass, the env on the warp */
{
    G_U(GD_SHAPE, GK(500), GK(850));
    G_U(GD_SUB, GK(200), GK(500));
    G_P(GD_SUBM, 0, 1);
    G_U(GD_WARP, -GK(200), GK(200));
    G_V(GD_WENV, gsign(r, rnd_in(r, GK(500), GK(1000))));
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(60), GK(250));
    G_U(GD_MS, 0, GK(200));
    G_V(GD_MR, GK(100));
    G_E(GD_SHP, GDS_SAT);
    G_U(GD_AMT, GK(200), GK(500));
    G_E(GD_ROUTE, 0);
    G_P(GD_TYPE, 1, 2);
    G_U(GD_TRK, 0, GK(300));
    gd_cut(s, r, GHZ(500), GHZ(1500));
    G_U(GD_RES, GK(100), GK(400));
    G_V(GD_FENV, 0);
    G_V(GD_AA, 0);
    G_L(GD_AD, GK(300), GK(800));
    G_U(GD_AS, GK(600), GK(1000));
    G_L(GD_AR, GK(40), GK(120));
    return 65536;
}
static int32_t gd_foldpad(sound_t *s, rnd_t *r)    /* the fold opening slowly (mod env attack), the warp drifting */
{
    G_U(GD_SHAPE, 0, GK(250));
    G_P(GD_SHP, GDS_SINP, GDS_SIN);
    G_U(GD_AMT, GK(50), GK(200));
    G_U(GD_MENV, GK(200), GK(500));
    G_L(GD_MA, GK(200), GK(600));
    G_V(GD_MD, GK(1000));
    G_U(GD_MS, GK(600), GK(1000));
    G_V(GD_MR, GK(600));
    G_U(GD_WLFO, GK(50), GK(200));
    G_L(GD_LF, GK(100), GK(500));
    G_P(GD_LW, 0, 1);
    G_E(GD_ROUTE, 1);
    G_E(GD_TYPE, 1);
    G_V(GD_TRK, GK(500));
    gd_cut(s, r, GHZ(1500), GHZ(4000));
    G_U(GD_RES, GK(100), GK(300));
    G_V(GD_FENV, 0);
    G_L(GD_AA, GK(30), GK(110));
    G_V(GD_AD, GK(1000));
    G_V(GD_AS, GK(1000));
    G_L(GD_AR, GK(500), GK(1300));
    return 65536;
}
static int32_t gd_resodrive(sound_t *s, rnd_t *r)  /* a resonant RD3, then overdriven */
{
    G_U(GD_SHAPE, GK(400), GK(800));
    G_U(GD_SUB, 0, GK(400));
    G_E(GD_TYPE, 2);
    G_U(GD_RES, GK(450), GK(700));
    G_U(GD_TRK, GK(300), GK(800));
    gd_cut(s, r, GHZ(300), GHZ(900));
    G_U(GD_FENV, GK(50), GK(150));
    G_V(GD_MA, 0);
    G_L(GD_MD, GK(100), GK(400));
    G_U(GD_MS, GK(100), GK(300));
    G_V(GD_MR, GK(150));
    G_E(GD_ROUTE, 1);
    G_P(GD_SHP, GDS_OVER, GDS_FUZZ, GDS_DIODE);
    G_U(GD_AMT, GK(300), GK(700));
    G_U(GD_PRE, GK(200), GK(600));
    G_V(GD_AA, 0);
    G_V(GD_AD, GK(500));
    G_U(GD_AS, GK(700), GK(1000));
    G_L(GD_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gd_buzz(sound_t *s, rnd_t *r)       /* a very narrow pulse into fuzz or diode */
{
    G_U(GD_SHAPE, GK(880), GK(1000));
    G_U(GD_SUB, 0, GK(300));
    G_P(GD_SHP, GDS_FUZZ, GDS_DIODE);
    G_U(GD_AMT, GK(300), GK(700));
    G_U(GD_PRE, 0, GK(400));
    G_E(GD_ROUTE, 0);
    G_E(GD_TYPE, 0);
    G_V(GD_TRK, GK(1000));
    gd_cut(s, r, GHZ(1500), GHZ(5000));
    G_U(GD_RES, GK(100), GK(400));
    G_V(GD_FENV, 0);
    G_L(GD_AA, 0, GK(5));
    G_V(GD_AD, GK(500));
    G_U(GD_AS, GK(800), GK(1000));
    G_L(GD_AR, GK(80), GK(200));
    return 65536;
}
static int32_t gd_shfuzz(sound_t *s, rnd_t *r)     /* S&H on a resonant cutoff, then fuzz */
{
    int32_t c;
    G_U(GD_SHAPE, GK(300), GK(700));
    G_P(GD_SHP, GDS_FUZZ, GDS_OVER);
    G_U(GD_AMT, GK(300), GK(700));
    G_E(GD_ROUTE, 1);
    G_E(GD_TYPE, 1);
    G_U(GD_RES, GK(400), GK(800));
    G_U(GD_TRK, GK(500), GK(1000));
    gd_cut(s, r, GHZ(600), GHZ(2000));
    c = s->val[GD_CUT];
    G_V(GD_FLFO, rnd_in(r, c / 3, c * 3 / 4) / 20000);
    G_E(GD_LW, 5);
    G_E(GD_LR, G_CH(50) ? 0 : 2);
    G_L(GD_LF, GHZ(4), GHZ(14));
    G_V(GD_FENV, 0);
    G_V(GD_AA, 0);
    G_V(GD_AD, GK(500));
    G_U(GD_AS, GK(800), GK(1000));
    G_L(GD_AR, GK(100), GK(250));
    return 65536;
}
static const gen_fam_t GEN_DIRT[] = {
    {"FUZZ BASS", gd_fuzzbass, GR_BASS, 0, 500},
    {"DRIVEN LEAD", gd_lead, GR_MID | GR_HIGH, 0, 500},
    {"GRIT PAD", gd_pad, GR_PAD, GFL_SLOW, 650},
    {"NOISE BAND", gd_noise, GR_HIGH, GFL_EXP, 1400},
    {"BROKEN", gd_broken, GR_MID, GFL_EXP, 750},
    {"WARP PLUCK", gd_warp, GR_HIGH | GR_MID, 0, 1300},
    {"FOLD LEAD", gd_fold, GR_MID | GR_HIGH, 0, 800},
    {"FOLD BASS", gd_foldbass, GR_BASS, 0, 1100},
    {"RECTIFIER", gd_rect, GR_MID, 0, 300},
    {"BIT BASS", gd_bitbass, GR_BASS, 0, 600},
    {"REDUX", gd_redux, GR_MID, GFL_EXP, 700},
    {"WARP BASS", gd_warpbass, GR_BASS, 0, 600},
    {"FOLD PAD", gd_foldpad, GR_PAD, GFL_SLOW, 700},
    {"RESO DRIVE", gd_resodrive, GR_MID | GR_BASS, 0, 500},
    {"BUZZ", gd_buzz, GR_MID | GR_HIGH, 0, 500},
    {"S/H FUZZ", gd_shfuzz, GR_MID, GFL_EXP, 500},
};

/* ================================================================== CHIP: chip models, linear envelope, SVF */
enum { GC_MODEL, GC_TUNE, GC_FINE, GC_DUTY, GC_PWMR, GC_PWMD, GC_SUB, GC_ARP, GC_ARPS, GC_SWEEP, GC_SWT, GC_VIBR,
       GC_VIBD, GC_A, GC_D, GC_S, GC_R, GC_CUT, GC_RES, GC_ENV, GC_BITS, GC_DEC, GC_GM, GC_GT, GC_VOL };
/* (ZAP / LASER falls into each note, ARP CHORD steps through a chord, CHIP DRUM's body falls: none is used) */
enum { GCM_PULSE, GCM_SID, GCM_NOISE, GCM_BELL, GCM_SUPER, GCM_ZAP, GCM_CHORD, GCM_DRUM };
static int32_t gc_square(sound_t *s, rnd_t *r)
{
    G_E(GC_MODEL, GCM_PULSE);
    G_U(GC_DUTY, GK(100), GK(500));
    G_U(GC_PWMD, 0, GK(250));
    G_L(GC_PWMR, GK(1000), GK(6000));
    G_U(GC_SUB, 0, GK(200));
    G_L(GC_A, 0, GK(5));
    G_L(GC_D, GK(150), GK(500));
    G_U(GC_S, GK(600), GK(900));
    G_L(GC_R, GK(60), GK(200));
    G_L(GC_CUT, GHZ(4000), GHZ(14000));
    G_U(GC_RES, 0, GK(200));
    G_U(GC_ENV, 0, GK(200));
    G_U(GC_BITS, GI(6), GI(16));
    return 65536;
}
static int32_t gc_bass(sound_t *s, rnd_t *r)
{
    G_P(GC_MODEL, GCM_PULSE, GCM_PULSE, GCM_SID);
    G_U(GC_DUTY, 0, GK(400));
    G_U(GC_SUB, GK(300), GK(800));
    G_U(GC_PWMD, 0, GK(150));
    G_V(GC_A, 0);
    G_L(GC_D, GK(120), GK(400));
    G_U(GC_S, GK(400), GK(800));
    G_L(GC_R, GK(40), GK(120));
    G_L(GC_CUT, GHZ(800), GHZ(3500));
    G_U(GC_RES, GK(100), GK(400));
    G_U(GC_ENV, GK(200), GK(600));
    G_U(GC_BITS, GI(8), GI(16));
    return 65536;
}
static int32_t gc_bell(sound_t *s, rnd_t *r)
{
    G_E(GC_MODEL, GCM_BELL);
    G_U(GC_DUTY, 0, GK(1000));                   /* the modulator's ratio, 1 .. 9 */
    G_U(GC_PWMD, GK(100), GK(700));              /* the FM depth */
    G_V(GC_A, 0);
    G_L(GC_D, GK(400), GK(1500));
    G_U(GC_S, 0, GK(150));
    G_L(GC_R, GK(300), GK(900));
    G_L(GC_CUT, GHZ(9000), GHZ(16000));
    G_V(GC_ENV, 0);
    G_U(GC_SUB, 0, GK(100));
    return 65536;
}
static int32_t gc_sid(sound_t *s, rnd_t *r)
{
    G_E(GC_MODEL, GCM_SID);
    G_U(GC_DUTY, GK(200), GK(800));
    G_U(GC_PWMD, 0, GK(300));
    G_U(GC_SUB, 0, GK(300));
    G_L(GC_A, 0, GK(10));
    G_L(GC_D, GK(300), GK(800));
    G_U(GC_S, GK(600), GK(900));
    G_L(GC_R, GK(100), GK(250));
    G_L(GC_CUT, GHZ(2500), GHZ(10000));
    G_U(GC_RES, GK(100), GK(500));
    G_U(GC_ENV, 0, GK(300));
    return 65536;
}
static int32_t gc_super(sound_t *s, rnd_t *r)
{
    G_E(GC_MODEL, GCM_SUPER);
    G_U(GC_DUTY, GK(200), GK(800));              /* the spread, and the duty */
    G_U(GC_PWMD, 0, GK(200));
    G_L(GC_A, 0, GK(15));
    G_L(GC_D, GK(300), GK(800));
    G_U(GC_S, GK(600), GK(900));
    G_L(GC_R, GK(100), GK(300));
    G_L(GC_CUT, GHZ(3000), GHZ(12000));
    G_U(GC_RES, 0, GK(300));
    G_U(GC_ENV, 0, GK(300));
    G_U(GC_BITS, GI(8), GI(16));
    return 65536;
}
static int32_t gc_pwmbuzz(sound_t *s, rnd_t *r)    /* the duty swung fast and deep: a buzzing, chorused pulse */
{
    G_E(GC_MODEL, GCM_PULSE);
    G_U(GC_DUTY, GK(100), GK(500));
    G_U(GC_PWMD, GK(400), GK(900));
    G_L(GC_PWMR, GHZ(12), GHZ(40));
    G_U(GC_SUB, 0, GK(300));
    G_V(GC_A, 0);
    G_L(GC_D, GK(150), GK(500));
    G_U(GC_S, GK(500), GK(900));
    G_L(GC_R, GK(60), GK(200));
    G_L(GC_CUT, GHZ(3000), GHZ(12000));
    G_U(GC_RES, 0, GK(300));
    G_U(GC_ENV, 0, GK(200));
    G_U(GC_BITS, GI(8), GI(16));
    return 65536;
}
static int32_t gc_sidring(sound_t *s, rnd_t *r)    /* SID's saw ring-modulated by its sub, resonant filter */
{
    G_E(GC_MODEL, GCM_SID);
    G_U(GC_DUTY, GK(750), GK(1000));
    G_U(GC_PWMD, 0, GK(200));
    G_U(GC_SUB, 0, GK(200));
    G_L(GC_A, 0, GK(5));
    G_L(GC_D, GK(200), GK(600));
    G_U(GC_S, GK(500), GK(900));
    G_L(GC_R, GK(100), GK(250));
    G_L(GC_CUT, GHZ(2000), GHZ(8000));
    G_U(GC_RES, GK(200), GK(500));
    G_U(GC_ENV, GK(100), GK(400));
    return 65536;
}
static int32_t gc_alias(sound_t *s, rnd_t *r)      /* decimated at a fixed rate and crushed: aliasing, not in the scale */
{
    G_P(GC_MODEL, GCM_PULSE, GCM_SID, GCM_SUPER);
    G_U(GC_DUTY, GK(200), GK(600));
    G_U(GC_DEC, GI(3), GI(6));
    G_U(GC_BITS, GI(5), GI(10));
    G_V(GC_A, 0);
    G_L(GC_D, GK(200), GK(500));
    G_U(GC_S, GK(500), GK(900));
    G_L(GC_R, GK(80), GK(200));
    G_L(GC_CUT, GHZ(4000), GHZ(12000));
    G_U(GC_RES, 0, GK(200));
    G_V(GC_ENV, 0);
    return 65536;
}
static int32_t gc_bitbass(sound_t *s, rnd_t *r)    /* 2 .. 4 bits, a sub, a filter envelope */
{
    G_P(GC_MODEL, GCM_PULSE, GCM_SID);
    G_U(GC_DUTY, 0, GK(400));
    G_U(GC_SUB, GK(400), GK(800));
    G_U(GC_BITS, GI(2), GI(4));
    G_V(GC_A, 0);
    G_L(GC_D, GK(100), GK(350));
    G_U(GC_S, GK(400), GK(800));
    G_L(GC_R, GK(40), GK(120));
    G_L(GC_CUT, GHZ(600), GHZ(2500));
    G_U(GC_RES, GK(100), GK(400));
    G_U(GC_ENV, GK(200), GK(500));
    return 65536;
}
static int32_t gc_tine(sound_t *s, rnd_t *r)       /* FM BELL at a ratio of 7 or 9, deep, short */
{
    G_E(GC_MODEL, GCM_BELL);
    G_U(GC_DUTY, GK(760), GK(1000));
    G_U(GC_PWMD, GK(600), GK(1000));
    G_V(GC_A, 0);
    G_L(GC_D, GK(150), GK(500));
    G_V(GC_S, 0);
    G_L(GC_R, GK(100), GK(300));
    G_L(GC_CUT, GHZ(9000), GHZ(16000));
    G_V(GC_ENV, 0);
    return 65536;
}
static int32_t gc_tribass(sound_t *s, rnd_t *r)    /* SID's triangle alone, stepped to 4 .. 8 bits (the NES triangle) */
{
    G_E(GC_MODEL, GCM_SID);
    G_V(GC_DUTY, 0);
    G_U(GC_SUB, GK(200), GK(600));
    G_U(GC_BITS, GI(4), GI(8));
    G_V(GC_A, 0);
    G_L(GC_D, GK(200), GK(500));
    G_U(GC_S, GK(600), GK(1000));
    G_L(GC_R, GK(40), GK(120));
    G_L(GC_CUT, GHZ(6000), GHZ(14000));
    G_V(GC_RES, 0);
    G_V(GC_ENV, 0);
    return 65536;
}
static int32_t gc_pad(sound_t *s, rnd_t *r)        /* super square, slow duty drift, a slow attack */
{
    G_E(GC_MODEL, GCM_SUPER);
    G_U(GC_DUTY, GK(300), GK(800));
    G_U(GC_PWMD, GK(100), GK(300));
    G_L(GC_PWMR, GK(300), GK(1500));
    G_L(GC_A, GK(40), GK(110));
    G_L(GC_D, GK(500), GK(1000));
    G_U(GC_S, GK(700), GK(1000));
    G_L(GC_R, GK(400), GK(1000));
    G_L(GC_CUT, GHZ(1500), GHZ(5000));
    G_U(GC_RES, GK(100), GK(300));
    G_U(GC_ENV, GK(100), GK(300));
    G_U(GC_BITS, GI(8), GI(16));
    return 65536;
}
static int32_t gc_lfsr(sound_t *s, rnd_t *r)       /* the short (metallic) LFSR clocked to repeat at the note's octave */
{
    G_E(GC_MODEL, GCM_NOISE);
    G_U(GC_DUTY, GK(600), GK(1000));             /* (>= 0.5: the short sequence) */
    G_V(GC_PWMD, 20480);                         /* (0.3125: the clock 11.625 x the pitch) */
    G_V(GC_TUNE, GI(24));
    G_V(GC_A, 0);
    G_L(GC_D, GK(100), GK(400));
    G_U(GC_S, GK(200), GK(600));
    G_L(GC_R, GK(50), GK(150));
    G_L(GC_CUT, GHZ(2000), GHZ(9000));
    G_U(GC_RES, 0, GK(300));
    G_V(GC_ENV, 0);
    return 65536;
}
static int32_t gc_bitbell(sound_t *s, rnd_t *r)    /* FM BELL decimated and crushed */
{
    G_E(GC_MODEL, GCM_BELL);
    G_U(GC_DUTY, 0, GK(700));
    G_U(GC_PWMD, GK(200), GK(600));
    G_U(GC_DEC, GI(2), GI(6));
    G_U(GC_BITS, GI(4), GI(7));
    G_V(GC_A, 0);
    G_L(GC_D, GK(300), GK(1000));
    G_U(GC_S, 0, GK(100));
    G_L(GC_R, GK(200), GK(600));
    G_L(GC_CUT, GHZ(6000), GHZ(14000));
    G_V(GC_ENV, 0);
    return 65536;
}
static const gen_fam_t GEN_CHIP[] = {
    {"SQUARE LEAD", gc_square, GR_MID | GR_HIGH, 0, 800},
    {"PULSE BASS", gc_bass, GR_BASS, 0, 1000},
    {"BELL", gc_bell, GR_HIGH | GR_MID, 0, 1000},
    {"SID LEAD", gc_sid, GR_MID | GR_HIGH, 0, 1100},
    {"SUPER SQUARE", gc_super, GR_MID | GR_HIGH, 0, 1000},
    {"PWM BUZZ", gc_pwmbuzz, GR_HIGH | GR_MID, 0, 1000},
    {"SID RING", gc_sidring, GR_MID, 0, 1000},
    {"ALIAS", gc_alias, GR_MID, GFL_EXP, 1000},
    {"BIT BASS", gc_bitbass, GR_BASS, 0, 1000},
    {"FM TINE", gc_tine, GR_HIGH, 0, 1000},
    {"TRI BASS", gc_tribass, GR_BASS, 0, 1000},
    {"CHIP PAD", gc_pad, GR_PAD, GFL_SLOW, 1000},
    {"LFSR TONE", gc_lfsr, GR_HIGH, GFL_EXP, 1000},
    {"BIT BELL", gc_bitbell, GR_HIGH, 0, 1000},
};

/* ================================================================== the families of each engine */
#define G_N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
static const gen_fam_t *const GEN_FAM[NMENG] = {GEN_ACD, GEN_FAT, GEN_VAP, GEN_WTFM, GEN_DIRT, GEN_CHIP};
static const uint8_t GEN_NFAM[NMENG] = {G_N(GEN_ACD), G_N(GEN_FAT), G_N(GEN_VAP), G_N(GEN_WTFM), G_N(GEN_DIRT),
                                       G_N(GEN_CHIP)};
typedef char gen_fmax_ok[G_N(GEN_ACD) <= GEN_FMAX && G_N(GEN_FAT) <= GEN_FMAX && G_N(GEN_VAP) <= GEN_FMAX &&
                         G_N(GEN_WTFM) <= GEN_FMAX && G_N(GEN_DIRT) <= GEN_FMAX && G_N(GEN_CHIP) <= GEN_FMAX ? 1 : -1];
static const uint8_t GEN_VOLP[NMENG] = {GA_VOL, GF_VOL, GV_VOL, GW_VOL, GD_VOL, GC_VOL};   /* each one's VOLUME */

/* (the melodic engines' families; PERC's kit has its own, by instrument: gen_perc.c) */
static uint32_t gen_nfam(uint32_t e) { return e < NMENG ? GEN_NFAM[e] : 0u; }
static const gen_fam_t *gen_fam(uint32_t e, uint32_t f)
{
    e = e < NMENG ? e : 0u;
    return &GEN_FAM[e][f < GEN_NFAM[e] ? f : 0u];
}
static const char *family_name(const sound_t *s) { return s->eng < NMENG ? gen_fam(s->eng, s->family)->name : "KIT"; }
static void gen_kit(sound_t *s, uint8_t *fam, uint32_t seed);   /* (gen_perc.c) */

/* every note holds its pitch: glide off, the pitch LFOs and the detune LFO at 0, no slot on a pitch (a slot left on
 * one is turned to a shape, at 0, so a macro on its amount moves a timbre), CHIP's arpeggio, sweep and vibrato off
 * and its falling models (ZAP, ARP CHORD, DRUM) replaced by the pulse */
/* a parameter whose macro modifier would move the pitch (engines.c snd_final leaves such modifiers out: no knob
 * bends, glides, vibratos or transposes a sound -- DIRT's EXPERIMENT on its pitch LFO, WTFM's on the carrier's ratio,
 * CHIP's on vibrato and sweep): the ones gen_steady holds at neutral, the carrier's ratio and tuning, and a
 * modulation slot's amount while the slot aims at a pitch */
static int gen_pitch_par(const sound_t *s, uint32_t p)
{
    uint32_t k, d;
    switch (s->eng) {
    case E_ACD: return p == GA_PLFO || p == GA_GM || p == GA_GT;
    case E_FAT: return p == GF_FATLFO || p == GF_GM || p == GF_GT;
    case E_VAP:
        if (p == GV_GM || p == GV_GT || p == GV_MIXENV || p == GV_FINE)
            return 1;
        for (k = 0; k < 6u; k++)
            if (p == GV_SLOT + 3u * k + 2u) {
                d = (uint32_t)(s->val[GV_SLOT + 3u * k + 1u] + 32768) >> 16;
                return d == GVD_P1 || d == GVD_P2;
            }
        return 0;
    case E_WTFM:
        if (p == GW_GM || p == GW_GT || p == GW_FT2 || p == GW_R2)
            return 1;
        for (k = 0; k < 6u; k++)
            if (p == GW_SLOT + 3u * k + 2u) {
                d = (uint32_t)(s->val[GW_SLOT + 3u * k + 1u] + 32768) >> 16;
                return d == GWD_P1 || d == GWD_P2;
            }
        return 0;
    case E_DIRT: return p == GD_PLFO || p == GD_GM || p == GD_GT;
    case E_CHIP:
        return p == GC_ARP || p == GC_SWEEP || p == GC_VIBD || p == GC_GM || p == GC_GT || p == GC_FINE ||
               p == GC_TUNE || p == GC_MODEL;
    default: return 0;
    }
}
static void gen_steady(sound_t *s)
{
    uint32_t k, d;
    switch (s->eng) {
    case E_ACD:
        gs(s, GA_PLFO, 0), gs(s, GA_GM, 0), gs(s, GA_GT, 0);
        break;
    case E_FAT:
        gs(s, GF_FATLFO, 0), gs(s, GF_GM, 0), gs(s, GF_GT, 0);
        break;
    case E_VAP:
        gs(s, GV_GM, 0), gs(s, GV_GT, 0), gs(s, GV_MIXENV, 0), gs(s, GV_FINE, 0);
        for (k = 0; k < 6u; k++) {
            d = (uint32_t)(s->val[GV_SLOT + 3u * k + 1u] + 32768) >> 16;
            if (d == GVD_P1 || d == GVD_P2)
                gs(s, GV_SLOT + 3u * k + 1u, GI(GVD_SH1)), gs(s, GV_SLOT + 3u * k + 2u, 0);
        }
        break;
    case E_WTFM:
        gs(s, GW_GM, 0), gs(s, GW_GT, 0), gs(s, GW_FT2, 0);
        for (k = 0; k < 6u; k++) {
            d = (uint32_t)(s->val[GW_SLOT + 3u * k + 1u] + 32768) >> 16;
            if (d == GWD_P1 || d == GWD_P2)
                gs(s, GW_SLOT + 3u * k + 1u, GI(GWD_SH1)), gs(s, GW_SLOT + 3u * k + 2u, 0);
        }
        break;
    case E_DIRT:
        gs(s, GD_PLFO, 0), gs(s, GD_GM, 0), gs(s, GD_GT, 0);
        break;
    case E_CHIP:
        gs(s, GC_ARP, 0), gs(s, GC_SWEEP, 0), gs(s, GC_VIBD, 0), gs(s, GC_GM, 0), gs(s, GC_GT, 0), gs(s, GC_FINE, 0);
        d = (uint32_t)(s->val[GC_MODEL] + 32768) >> 16;
        if (d == GCM_ZAP || d == GCM_CHORD || d == GCM_DRUM)
            gs(s, GC_MODEL, GI(GCM_PULSE));
        break;
    default:
        break;
    }
}

/* HOME's draw of a family: the rougher ones a little less often */
static uint32_t gen_draw_family(uint32_t e, rnd_t *r)
{
    uint8_t w[GEN_FMAX];
    uint32_t f, n = gen_nfam(e);
    for (f = 0; f < n && f < GEN_FMAX; f++)
        w[f] = (gen_fam(e, f)->flags & GFL_EXP) ? 2u : 3u;
    return rnd_weighted(r, w, n < GEN_FMAX ? n : GEN_FMAX);
}

/* a sound of engine e, family fam (-1: one drawn), from seed */
static void gen_sound(sound_t *s, uint32_t e, int32_t fam, uint32_t seed)
{
    rnd_t r;
    const gen_fam_t *F;
    int32_t t;
    if (e == E_PERC) {                           /* (the kit: each instrument's family drawn) */
        uint8_t fam_[8];
        gen_kit(s, fam_, seed);
        return;
    }
    rnd_seed(&r, seed ? seed : 1u);
    e = e < NMENG ? e : 0u;
    snd_default(s, e);
    s->family = (uint8_t)(fam >= 0 && (uint32_t)fam < gen_nfam(e) ? (uint32_t)fam : gen_draw_family(e, &r));
    s->seed = seed;
    F = gen_fam(e, s->family);
    t = F->fn(s, &r);
    gen_steady(s);
    /* the level: the family's VOLUME x the recipe's trim, +-10 % */
    gs(s, GEN_VOLP[e], (int32_t)(((((int64_t)GK(F->vol) * t) >> 16) * rnd_in(&r, 58982, 72090)) >> 16));
}

