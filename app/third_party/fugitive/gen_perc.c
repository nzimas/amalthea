/* SPDX-License-Identifier: GPL-3.0-only
 * FUGITIVE: the kit's sounds (track 6: PERC, docs/SOUNDS.md). PERC's one voice holds six instruments -- kick, snare,
 * hi-hat (closed and open share it), cymbal, the percussion slot and the tom -- each with its own section of
 * parameters and its own level, pan and output in the mixer. Each has its families (TIGHT, BOOM, CLICK .. for the kick;
 * TIGHT, RIM, NOISE .. for the snare ..), leaning to IDM, industrial and breakcore -- short, metallic, crushed, driven --
 * with a few clean ones in each. A recipe sets only its instrument's parameters, from PERC's defaults, each drawn
 * from a seed within a range that keeps it that family; its level x a trim, +-10 %.
 *
 * The kit has one drive, PERC's waveshaper, on its MAIN bus: a family that is driven or crushed routes its
 * instrument there (OUTPUT MAIN) and sets the shaper (type, level); the clean ones go round it (POST OD). Two driven
 * instruments share the shaper the last of them drew; with none on MAIN it is off (it costs nothing then).
 * The 12 aux sends do nothing in Fugitive (the track's own sends stand in) and stay at 0. The cymbals' tails are kept
 * at most ~1.1 s and the open hats' ~0.7 s: the kit's cost is its instruments ringing at once (PERC's SVF stages).
 *
 *   kit_inst_group(inst)        the group (KG_*) of a subtrack's instrument (the hats: one)
 *   kit_nfam(g), kit_fam_name(g, f)
 *   gen_kit(s, fam, seed)       a whole kit: each group's family drawn (fam[KG_N] out)
 *   gen_kit_group(s, fam, g, f, seed)   group g drawn again, family f (-1: drawn): HOME, SHIFT + HOME, SHIFT + PRESETS
 */

enum { KG_KICK, KG_SNARE, KG_HAT, KG_CYM, KG_PERC, KG_TOM, KG_N };
static const uint8_t DI_GROUP[NDI] = {KG_KICK, KG_SNARE, KG_HAT, KG_HAT, KG_CYM, KG_PERC, KG_TOM};
static const char *const DI_NAME[NDI] = {"KICK", "SNARE", "CL HAT", "OP HAT", "CYMBAL", "PERC", "TOM"};
static const char *const KG_NAME[KG_N] = {"KICK", "SNARE", "HAT", "CYMBAL", "PERC", "TOM"};
/* each group's parameters (PERC's sections) and its mixer: level, pan, delay, reverb, output */
static const uint8_t KG_P0[KG_N] = {0, 18, 27, 36, 45, 9}, KG_PN[KG_N] = {9, 9, 9, 9, 4, 9};
static const uint8_t KG_MIX[KG_N] = {49, 59, 64, 69, 74, 54};
#define KP_SHLEV 79
#define KP_SHTYPE 80
enum { KSH_SAT, KSH_OD, KSH_ODP, KSH_BIT, KSH_BITP };
static uint32_t kit_inst_group(uint32_t inst) { return DI_GROUP[inst % NDI]; }

/* (the setters out of line: a recipe a list of calls, small in the flash) */
#define K_NOINL __attribute__((noinline))
static K_NOINL void k_u(sound_t *s, rnd_t *r, uint32_t j, int32_t lo, int32_t hi) { gs(s, j, rnd_in(r, lo, hi)); }
static K_NOINL void k_l(sound_t *s, rnd_t *r, uint32_t j, int32_t lo, int32_t hi) { gs(s, j, glog(r, lo, hi)); }
static K_NOINL void k_i(sound_t *s, rnd_t *r, uint32_t j, int32_t lo, int32_t hi) { gs(s, j, GI(lo + rnd_below(r, hi - lo + 1))); }
static K_NOINL int k_ch(rnd_t *r, int32_t p) { return rnd_chance(r, p); }
#define K_U(j, lo, hi) k_u(s, r, (j), (lo), (hi))              /* even */
#define K_L(j, lo, hi) k_l(s, r, (j), (lo), (hi))              /* in octaves (times, Hz) */
#define K_V(j, v) gs(s, (j), (v))
#define K_E(j, i) gs(s, (j), GI(i))
#define K_I(j, lo, hi) k_i(s, r, (j), (lo), (hi))              /* a whole number lo .. hi */
#define K_CH(p) k_ch(r, (p))
/* a body wave: wave w (0 SAT SIN, 1 TRIANGLE, 2 SQUARE, 3 SQUARE FM, 4 .. 7 FM1 .. FM11, 8 .. 11 TZFM1 .. TZFM11,
 * 12 .. 15 GLITCH 1 .. 4), morphed from the sine by m0 .. m1 tenths */
static K_NOINL void k_wave(sound_t *s, rnd_t *r, uint32_t j, uint32_t w, int32_t m0, int32_t m1)
{
    gs(s, j, GI((int32_t)w * 11 + m0 + rnd_below(r, m1 - m0 + 1)));
}
static void k_clean(sound_t *s, uint32_t g) { gs(s, KG_MIX[g] + 4u, GI(1)); }   /* (POST OD: round the shaper) */
static void k_drive(sound_t *s, rnd_t *r, uint32_t g, uint32_t type, int32_t lo, int32_t hi)   /* (MAIN, the shaper) */
{
    gs(s, KG_MIX[g] + 4u, 0);
    gs(s, KP_SHTYPE, GI(type));
    gs(s, KP_SHLEV, rnd_in(r, GK(lo), GK(hi)));
}
static void k_pan(sound_t *s, rnd_t *r, uint32_t g, int32_t lo, int32_t hi)   /* (either side, thousandths) */
{
    int32_t p = rnd_in(r, GK(lo), GK(hi));
    gs(s, KG_MIX[g] + 1u, rnd_chance(r, 50) ? p : -p);
}

/* ------------------------------------------------------------------------- KICK: 0 .. 8 */
enum { KK_WAVE, KK_PITCH, KK_ATK, KK_DEC, KK_TTIM, KK_TLEV, KK_MDEC, KK_MAMT, KK_MODEL };
/* (build 11) every kick a BODY first: CLASSIC9, its pitch falling from several times the note over the mod envelope
 * (Polyend's own default kick), a sine around 45 .. 60 Hz, a long enough decay, a soft low-passed click -- build 10's
 * were CLASSIC8 (no pitch fall: a 20 ms blip) with a loud click, clicks with no body. Each family's character on top */
static K_NOINL void kk_body(sound_t *s, rnd_t *r, int32_t p0, int32_t p1, int32_t d0, int32_t d1, int32_t a0, int32_t a1,
                            int32_t m0, int32_t m1)
{
    K_E(KK_MODEL, 2);
    k_wave(s, r, KK_WAVE, 0, 0, 1);
    K_I(KK_PITCH, p0, p1);                       /* (MIDI note: 28 E1 41 Hz .. 36 C2 65 Hz) */
    K_U(KK_ATK, 0, GK(200));
    K_L(KK_DEC, GK(d0), GK(d1));                 /* (the body, ms) */
    K_U(KK_MAMT, GK(a0), GK(a1));                /* (the pitch fall: x (1 + 20 a)) */
    K_L(KK_MDEC, GK(m0), GK(m1));                /* (its time, ms) */
    K_U(KK_TLEV, GK(150), GK(300));              /* (the click: under the body) */
    K_U(KK_TTIM, GK(200), GK(450));              /* (low-passed, no noise) */
}
static int32_t kk_tight(sound_t *s, rnd_t *r)
{
    kk_body(s, r, 31, 35, 220, 380, 100, 180, 25, 45);
    k_clean(s, KG_KICK);
    return Q16;
}
static int32_t kk_boom(sound_t *s, rnd_t *r)      /* long, deep */
{
    kk_body(s, r, 28, 32, 550, 900, 150, 280, 50, 90);
    k_clean(s, KG_KICK);
    return Q16;
}
static int32_t kk_click(sound_t *s, rnd_t *r)     /* (PUNCH: a fast fall, the click a little forward) */
{
    kk_body(s, r, 31, 35, 280, 450, 200, 320, 18, 32);
    K_U(KK_TLEV, GK(300), GK(450));
    k_clean(s, KG_KICK);
    return Q16;
}
static int32_t kk_driven(sound_t *s, rnd_t *r)
{
    kk_body(s, r, 30, 34, 300, 500, 120, 220, 30, 60);
    k_wave(s, r, KK_WAVE, 0, 1, 4);
    k_drive(s, r, KG_KICK, KSH_OD, 250, 450);
    return Q16 * 2 / 5;
}
static int32_t kk_fm(sound_t *s, rnd_t *r)         /* an FM wave on the falling body */
{
    kk_body(s, r, 30, 34, 300, 550, 120, 220, 30, 70);
    k_wave(s, r, KK_WAVE, 4 + rnd_below(r, 2), 1, 3);
    k_clean(s, KG_KICK);
    return Q16 * 4 / 5;
}
static int32_t kk_distorted(sound_t *s, rnd_t *r)
{
    kk_body(s, r, 29, 33, 350, 600, 140, 260, 35, 70);
    if (K_CH(60))
        k_drive(s, r, KG_KICK, KSH_ODP, 450, 750);
    else
        k_drive(s, r, KG_KICK, KSH_SAT, 600, 900);
    return Q16 / 3;
}
static int32_t kk_crushed(sound_t *s, rnd_t *r)
{
    kk_body(s, r, 30, 34, 250, 450, 120, 220, 25, 55);
    k_drive(s, r, KG_KICK, rnd_chance(r, 50) ? KSH_BIT : KSH_BITP, 450, 750);
    return Q16 * 3 / 5;
}
static int32_t kk_glitch(sound_t *s, rnd_t *r)     /* a glitch wave a little into the morph, on the body */
{
    kk_body(s, r, 30, 35, 200, 400, 100, 250, 20, 60);
    k_wave(s, r, KK_WAVE, 12 + rnd_below(r, 4), 1, 3);
    k_clean(s, KG_KICK);
    return Q16 * 4 / 5;
}

/* ------------------------------------------------------------------------- SNARE: 18 .. 26 */
enum { KS_WAVE = 18, KS_PITCH, KS_TONE, KS_MODEL, KS_BDEC, KS_SATK, KS_SDEC, KS_MIX, KS_STIM };
enum { KSM_C8, KSM_C9, KSM_C9P, KSM_MOD, KSM_MODP };
static int32_t ks_tight(sound_t *s, rnd_t *r)
{
    K_E(KS_MODEL, KSM_C9);
    K_I(KS_PITCH, 52, 60);
    K_U(KS_TONE, GK(300), GK(600));
    K_L(KS_BDEC, GK(60), GK(120));
    K_L(KS_SDEC, GK(80), GK(160));
    K_U(KS_MIX, -GK(100), GK(300));
    K_U(KS_STIM, GK(200), GK(500));
    k_clean(s, KG_SNARE);
    return Q16 * 3 / 4;
}
static int32_t ks_fat(sound_t *s, rnd_t *r)
{
    K_E(KS_MODEL, KSM_C9P);
    K_I(KS_PITCH, 45, 52);
    K_U(KS_TONE, GK(200), GK(500));
    K_L(KS_BDEC, GK(150), GK(300));
    K_L(KS_SDEC, GK(150), GK(300));
    K_U(KS_MIX, -GK(300), GK(100));
    K_U(KS_STIM, GK(100), GK(300));
    k_clean(s, KG_SNARE);
    return Q16 * 3 / 5;
}
static int32_t ks_rim(sound_t *s, rnd_t *r)       /* a knock: very short, high, the body mostly */
{
    K_E(KS_MODEL, KSM_C8);
    k_wave(s, r, KS_WAVE, 2, 3, 6);
    K_I(KS_PITCH, 68, 80);
    K_U(KS_TONE, GK(700), GK(1000));
    K_L(KS_BDEC, GK(30), GK(60));
    K_L(KS_SDEC, GK(30), GK(60));
    K_U(KS_MIX, -GK(700), -GK(400));
    k_clean(s, KG_SNARE);
    return Q16 * 8 / 5;
}
static int32_t ks_noise(sound_t *s, rnd_t *r)     /* the snap mostly: a burst of filtered noise */
{
    K_E(KS_MODEL, KSM_MODP);
    K_U(KS_MIX, GK(500), GK(900));
    K_L(KS_SDEC, GK(150), GK(350));
    K_U(KS_STIM, GK(400), GK(800));
    K_L(KS_BDEC, GK(50), GK(100));
    K_I(KS_PITCH, 50, 62);
    k_clean(s, KG_SNARE);
    return Q16 * 3 / 5;
}
static int32_t ks_fm(sound_t *s, rnd_t *r)        /* an FM body: metallic */
{
    static const uint8_t W[4] = {5, 6, 9, 10};
    K_E(KS_MODEL, KSM_C9);
    k_wave(s, r, KS_WAVE, W[rnd_below(r, 4)], 4, 9);
    K_I(KS_PITCH, 50, 66);
    K_L(KS_BDEC, GK(80), GK(200));
    K_L(KS_SDEC, GK(60), GK(140));
    K_U(KS_MIX, -GK(500), 0);
    K_U(KS_STIM, GK(200), GK(600));
    k_clean(s, KG_SNARE);
    return Q16 * 4 / 5;
}
static int32_t ks_crushed(sound_t *s, rnd_t *r)
{
    K_E(KS_MODEL, KSM_MOD);
    K_I(KS_PITCH, 50, 64);
    K_U(KS_MIX, GK(200), GK(600));
    K_L(KS_BDEC, GK(60), GK(150));
    K_L(KS_SDEC, GK(80), GK(220));
    K_U(KS_STIM, GK(300), GK(700));
    k_drive(s, r, KG_SNARE, rnd_chance(r, 50) ? KSH_BIT : KSH_BITP, 400, 750);
    return Q16;
}
static int32_t ks_distorted(sound_t *s, rnd_t *r)
{
    K_E(KS_MODEL, KSM_C9P);
    K_I(KS_PITCH, 46, 56);
    K_L(KS_BDEC, GK(100), GK(220));
    K_L(KS_SDEC, GK(100), GK(250));
    K_U(KS_MIX, -GK(200), GK(300));
    k_drive(s, r, KG_SNARE, KSH_ODP, 400, 700);
    return Q16 / 6;
}
static int32_t ks_glitch(sound_t *s, rnd_t *r)
{
    K_E(KS_MODEL, KSM_MODP);
    k_wave(s, r, KS_WAVE, 12 + rnd_below(r, 4), 5, 10);
    K_I(KS_PITCH, 48, 72);
    K_L(KS_BDEC, GK(30), GK(80));
    K_L(KS_SDEC, GK(40), GK(100));
    K_U(KS_MIX, -GK(400), GK(400));
    K_U(KS_STIM, GK(300), GK(900));
    k_clean(s, KG_SNARE);
    return Q16 * 3 / 5;
}

/* ------------------------------------------------------------------------- HAT: 27 .. 35 (closed and open) */
enum { KH_SHAPE = 27, KH_CDEC, KH_ODEC, KH_HPF, KH_LPF, KH_PITCH, KH_MIX, KH_TIMB, KH_MODEL };
static int32_t kh_clean(sound_t *s, rnd_t *r)
{
    K_E(KH_MODEL, 0);
    K_I(KH_TIMB, 0, 5);
    K_U(KH_MIX, -GK(400), GK(200));
    K_L(KH_HPF, GHZ(5000), GHZ(8000));
    K_L(KH_LPF, GHZ(11000), GHZ(18000));
    K_L(KH_CDEC, GK(40), GK(80));
    K_L(KH_ODEC, GK(350), GK(600));
    K_U(KH_SHAPE, GK(50), GK(200));
    k_clean(s, KG_HAT);
    return Q16;
}
static int32_t kh_pulse(sound_t *s, rnd_t *r)     /* the six pulses mostly: the classic metal */
{
    K_E(KH_MODEL, 0);
    K_I(KH_TIMB, 0, 3);
    K_U(KH_MIX, GK(500), GK(900));
    K_L(KH_PITCH, GHZ(280), GHZ(480));
    K_L(KH_HPF, GHZ(6000), GHZ(9000));
    K_L(KH_LPF, GHZ(12000), GHZ(20000));
    K_L(KH_CDEC, GK(50), GK(90));
    K_L(KH_ODEC, GK(300), GK(500));
    k_clean(s, KG_HAT);
    return Q16;
}
static int32_t kh_tick(sound_t *s, rnd_t *r)      /* the shortest: a tick of noise */
{
    K_E(KH_MODEL, 0);
    K_U(KH_MIX, -GK(800), -GK(400));
    K_L(KH_HPF, GHZ(7000), GHZ(10000));
    K_L(KH_LPF, GHZ(16000), GHZ(20000));
    K_L(KH_CDEC, GK(25), GK(50));
    K_L(KH_ODEC, GK(120), GK(250));
    K_U(KH_SHAPE, GK(300), GK(600));
    k_clean(s, KG_HAT);
    return Q16 * 2;
}
static int32_t kh_metal(sound_t *s, rnd_t *r)     /* FM: three sines, inharmonic */
{
    K_E(KH_MODEL, 1);
    K_I(KH_TIMB, 3, 16);
    K_U(KH_MIX, GK(300), GK(800));
    K_L(KH_PITCH, GHZ(200), GHZ(600));
    K_L(KH_HPF, GHZ(3000), GHZ(6000));
    K_L(KH_LPF, GHZ(9000), GHZ(16000));
    K_L(KH_CDEC, GK(50), GK(100));
    K_L(KH_ODEC, GK(300), GK(600));
    k_clean(s, KG_HAT);
    return Q16;
}
static int32_t kh_dark(sound_t *s, rnd_t *r)
{
    K_E(KH_MODEL, 0);
    K_L(KH_HPF, GHZ(1500), GHZ(3000));
    K_L(KH_LPF, GHZ(4000), GHZ(7000));
    K_L(KH_PITCH, GHZ(120), GHZ(220));
    K_U(KH_MIX, -GK(200), GK(400));
    K_L(KH_CDEC, GK(60), GK(120));
    K_L(KH_ODEC, GK(400), GK(700));
    k_clean(s, KG_HAT);
    return Q16 * 4 / 5;
}
static int32_t kh_chaos(sound_t *s, rnd_t *r)     /* FM with feedback: chaotic */
{
    K_E(KH_MODEL, 1);
    K_I(KH_TIMB, 17, 24);
    K_U(KH_MIX, GK(400), GK(900));
    K_L(KH_HPF, GHZ(4000), GHZ(8000));
    K_L(KH_CDEC, GK(30), GK(70));
    K_L(KH_ODEC, GK(200), GK(400));
    k_clean(s, KG_HAT);
    return Q16;
}
static int32_t kh_crushed(sound_t *s, rnd_t *r)
{
    K_E(KH_MODEL, rnd_below(r, 2));
    K_I(KH_TIMB, 0, 12);
    K_U(KH_MIX, -GK(300), GK(600));
    K_L(KH_HPF, GHZ(3000), GHZ(7000));
    K_L(KH_CDEC, GK(40), GK(90));
    K_L(KH_ODEC, GK(250), GK(500));
    k_drive(s, r, KG_HAT, KSH_BIT, 300, 550);   /* (rounded, not truncated: a hat's small signal kept) */
    return Q16 * 3 / 2;
}

/* ------------------------------------------------------------------------- CYMBAL: 36 .. 44 */
enum { KC_BFILT = 36, KC_IFILT, KC_BDEC, KC_IDEC, KC_PITCH, KC_MIX, KC_IB, KC_MODEL, KC_TIMB };
static int32_t kc_crash(sound_t *s, rnd_t *r)
{
    K_E(KC_MODEL, 0);
    K_L(KC_BFILT, GHZ(2500), GHZ(4500));
    K_L(KC_IFILT, GHZ(6000), GHZ(9000));
    K_L(KC_BDEC, GK(700), GK(1100));
    K_L(KC_IDEC, GK(200), GK(400));
    K_U(KC_IB, 0, GK(400));
    k_clean(s, KG_CYM);
    return Q16;
}
static int32_t kc_ride(sound_t *s, rnd_t *r)
{
    K_E(KC_MODEL, 0);
    K_L(KC_BFILT, GHZ(5000), GHZ(8000));
    K_L(KC_IFILT, GHZ(7000), GHZ(10000));
    K_L(KC_BDEC, GK(550), GK(900));
    K_L(KC_IDEC, GK(80), GK(150));
    K_U(KC_IB, -GK(400), -GK(100));
    K_L(KC_PITCH, GHZ(300), GHZ(500));
    k_clean(s, KG_CYM);
    return Q16;
}
static int32_t kc_china(sound_t *s, rnd_t *r)     /* FM, low band: trashy */
{
    K_E(KC_MODEL, 1);
    K_I(KC_TIMB, 4, 14);
    K_L(KC_BFILT, GHZ(1500), GHZ(3000));
    K_L(KC_BDEC, GK(450), GK(800));
    K_L(KC_IDEC, GK(100), GK(250));
    K_U(KC_MIX, GK(300), GK(700));
    k_clean(s, KG_CYM);
    return Q16 * 4 / 5;
}
static int32_t kc_splash(sound_t *s, rnd_t *r)
{
    K_E(KC_MODEL, 0);
    K_L(KC_BFILT, GHZ(4000), GHZ(7000));
    K_L(KC_BDEC, GK(250), GK(500));
    K_L(KC_IDEC, GK(80), GK(200));
    k_clean(s, KG_CYM);
    return Q16;
}
static int32_t kc_metal(sound_t *s, rnd_t *r)     /* FM tones mostly, the impact: a struck plate */
{
    K_E(KC_MODEL, 1);
    K_I(KC_TIMB, 2, 16);
    K_L(KC_PITCH, GHZ(400), GHZ(880));
    K_U(KC_MIX, GK(600), GK(1000));
    K_L(KC_BDEC, GK(300), GK(800));
    K_U(KC_IB, GK(300), GK(800));
    k_clean(s, KG_CYM);
    return Q16;
}
static int32_t kc_crushed(sound_t *s, rnd_t *r)
{
    K_E(KC_MODEL, rnd_below(r, 2));
    K_L(KC_BDEC, GK(400), GK(800));
    K_L(KC_BFILT, GHZ(2000), GHZ(5000));
    k_drive(s, r, KG_CYM, rnd_chance(r, 50) ? KSH_BIT : KSH_BITP, 400, 700);
    return Q16;
}

/* ------------------------------------------------------------------------- PERC: 45 .. 48 */
enum { KP_MODEL = 45, KP_VAR, KP_TIMB, KP_DEC };
enum { KPM_CLAP, KPM_CLAPP, KPM_MARACAS, KPM_SHAKER, KPM_COWBELL };
static int32_t kp_clap(sound_t *s, rnd_t *r)
{
    K_E(KP_MODEL, KPM_CLAP);
    K_U(KP_VAR, GK(250), GK(600));
    K_U(KP_TIMB, GK(300), GK(600));
    K_L(KP_DEC, GK(100), GK(300));
    k_clean(s, KG_PERC);
    return Q16;
}
static int32_t kp_bigclap(sound_t *s, rnd_t *r)
{
    K_E(KP_MODEL, KPM_CLAPP);
    K_U(KP_VAR, GK(300), GK(700));
    K_U(KP_TIMB, GK(200), GK(500));
    K_L(KP_DEC, GK(250), GK(550));
    k_clean(s, KG_PERC);
    return Q16;
}
static int32_t kp_snap(sound_t *s, rnd_t *r)      /* the bursts close together, bright, short */
{
    K_E(KP_MODEL, KPM_CLAP);
    K_U(KP_VAR, 0, GK(100));
    K_U(KP_TIMB, GK(600), GK(900));
    K_L(KP_DEC, GK(40), GK(100));
    k_clean(s, KG_PERC);
    return Q16;
}
static int32_t kp_maracas(sound_t *s, rnd_t *r)
{
    K_E(KP_MODEL, KPM_MARACAS);
    K_U(KP_TIMB, GK(400), GK(900));
    K_L(KP_DEC, GK(50), GK(150));
    k_clean(s, KG_PERC);
    return Q16 * 5 / 2;
}
static int32_t kp_shaker(sound_t *s, rnd_t *r)
{
    K_E(KP_MODEL, KPM_SHAKER);
    K_U(KP_TIMB, GK(400), GK(900));
    K_L(KP_DEC, GK(30), GK(80));
    k_clean(s, KG_PERC);
    return Q16;
}
static int32_t kp_cowbell(sound_t *s, rnd_t *r)
{
    K_E(KP_MODEL, KPM_COWBELL);
    K_U(KP_TIMB, GK(300), GK(500));
    K_U(KP_VAR, GK(200), GK(500));
    K_L(KP_DEC, GK(80), GK(200));
    k_clean(s, KG_PERC);
    return Q16;
}
static int32_t kp_bell(sound_t *s, rnd_t *r)      /* the cowbell high and off its band, driven: a clang */
{
    K_E(KP_MODEL, KPM_COWBELL);
    K_U(KP_TIMB, GK(600), GK(1000));
    K_U(KP_VAR, GK(600), GK(1000));
    K_L(KP_DEC, GK(50), GK(120));
    k_drive(s, r, KG_PERC, KSH_OD, 400, 700);
    return Q16;
}
static int32_t kp_crushed(sound_t *s, rnd_t *r)
{
    K_E(KP_MODEL, KPM_CLAPP);
    K_U(KP_VAR, GK(200), GK(700));
    K_U(KP_TIMB, GK(300), GK(800));
    K_L(KP_DEC, GK(100), GK(300));
    k_drive(s, r, KG_PERC, KSH_BITP, 500, 800);
    return Q16;
}

/* ------------------------------------------------------------------------- TOM: 9 .. 17 (the step's note) */
enum { KT_WAVE = 9, KT_ATK, KT_DEC, KT_TTIM, KT_TLEV, KT_MDEC, KT_PMOD, KT_WMOD, KT_MODEL };
/* (a drum's own fall into its note, short: the pitch modulation dies within 20 .. 50 ms) */
static int32_t kt_classic(sound_t *s, rnd_t *r)
{
    K_E(KT_MODEL, 0);
    k_wave(s, r, KT_WAVE, 0, 0, 2);
    K_L(KT_DEC, GK(200), GK(450));
    K_U(KT_PMOD, GK(500), GK(1600));
    K_L(KT_MDEC, GK(20), GK(50));
    K_U(KT_TLEV, GK(400), GK(900));
    k_clean(s, KG_TOM);
    return Q16;
}
static int32_t kt_floor(sound_t *s, rnd_t *r)
{
    K_E(KT_MODEL, 1);
    k_wave(s, r, KT_WAVE, 0, 0, 3);
    K_L(KT_DEC, GK(400), GK(800));
    K_U(KT_PMOD, GK(300), GK(1000));
    K_L(KT_MDEC, GK(20), GK(50));
    k_clean(s, KG_TOM);
    return Q16;
}
static int32_t kt_resonant(sound_t *s, rnd_t *r)  /* RESONANT: two more oscillators, inharmonic */
{
    K_E(KT_MODEL, 2);
    k_wave(s, r, KT_WAVE, 2, 2, 6);
    K_L(KT_DEC, GK(250), GK(600));
    K_U(KT_PMOD, GK(200), GK(800));
    K_L(KT_MDEC, GK(15), GK(40));
    k_clean(s, KG_TOM);
    return Q16;
}
static int32_t kt_fm(sound_t *s, rnd_t *r)
{
    K_E(KT_MODEL, 0);
    k_wave(s, r, KT_WAVE, 4 + rnd_below(r, 3), 3, 8);
    K_U(KT_WMOD, GK(300), GK(700));
    K_L(KT_DEC, GK(150), GK(350));
    K_U(KT_PMOD, GK(200), GK(900));
    K_L(KT_MDEC, GK(20), GK(50));
    k_clean(s, KG_TOM);
    return Q16;
}
static int32_t kt_blip(sound_t *s, rnd_t *r)      /* short, little transient: a pitched blip */
{
    K_E(KT_MODEL, 0);
    k_wave(s, r, KT_WAVE, rnd_below(r, 3), 0, 4);
    K_L(KT_DEC, GK(40), GK(100));
    K_U(KT_PMOD, GK(200), GK(600));
    K_L(KT_MDEC, GK(10), GK(25));
    K_U(KT_TLEV, GK(100), GK(300));
    k_clean(s, KG_TOM);
    return Q16 * 5 / 4;
}
static int32_t kt_distorted(sound_t *s, rnd_t *r)
{
    K_E(KT_MODEL, 1);
    k_wave(s, r, KT_WAVE, rnd_below(r, 3), 0, 5);
    K_L(KT_DEC, GK(200), GK(400));
    K_U(KT_PMOD, GK(400), GK(1200));
    K_L(KT_MDEC, GK(20), GK(45));
    k_drive(s, r, KG_TOM, KSH_ODP, 500, 800);
    return Q16 * 1 / 6;
}
static int32_t kt_glitch(sound_t *s, rnd_t *r)
{
    K_E(KT_MODEL, 0);
    k_wave(s, r, KT_WAVE, 12 + rnd_below(r, 4), 2, 6);
    K_U(KT_WMOD, -GK(200), GK(200));
    K_L(KT_DEC, GK(60), GK(180));
    K_U(KT_PMOD, GK(200), GK(700));
    K_L(KT_MDEC, GK(10), GK(40));
    k_clean(s, KG_TOM);
    return Q16 * 6 / 5;
}

/* ------------------------------------------------------------------------- the families */
#define KFL_CLEAN 1u                             /* (the plain ones: NEW FUGUE draws them more often) */
#define KFL_EXP 2u                               /* (the roughest: a little less often) */
typedef struct {
    const char *name;            /* <= 12 characters FONT_XS has */
    int32_t (*fn)(sound_t *s, rnd_t *r);
    uint8_t flags;
    uint16_t vol;                /* its LEVEL in the mixer, thousandths (0 .. 2000) */
} kit_fam_t;
static const kit_fam_t KF_KICK[] = {
    {"TIGHT", kk_tight, KFL_CLEAN, 1400}, {"BOOM", kk_boom, KFL_CLEAN, 1400}, {"PUNCH", kk_click, KFL_CLEAN, 1400},
    {"DRIVEN", kk_driven, 0, 1400}, {"FM", kk_fm, 0, 1400}, {"DISTORTED", kk_distorted, 0, 1400},
    {"CRUSHED", kk_crushed, KFL_EXP, 1400}, {"GLITCH", kk_glitch, KFL_EXP, 1400},
};
static const kit_fam_t KF_SNARE[] = {
    {"TIGHT", ks_tight, KFL_CLEAN, 1000}, {"FAT", ks_fat, KFL_CLEAN, 1000}, {"RIM", ks_rim, KFL_CLEAN, 1000},
    {"NOISE", ks_noise, 0, 1000}, {"FM", ks_fm, 0, 1000}, {"CRUSHED", ks_crushed, 0, 1000},
    {"DISTORTED", ks_distorted, 0, 1000}, {"GLITCH", ks_glitch, KFL_EXP, 1000},
};
static const kit_fam_t KF_HAT[] = {
    {"CLEAN", kh_clean, KFL_CLEAN, 2000}, {"PULSE", kh_pulse, KFL_CLEAN, 2000}, {"TICK", kh_tick, 0, 1000},
    {"METAL", kh_metal, 0, 1800}, {"DARK", kh_dark, 0, 2000}, {"CHAOS", kh_chaos, KFL_EXP, 2000},
    {"CRUSHED", kh_crushed, KFL_EXP, 1000},
};
static const kit_fam_t KF_CYM[] = {
    {"CRASH", kc_crash, KFL_CLEAN, 1000}, {"RIDE", kc_ride, KFL_CLEAN, 1000}, {"CHINA", kc_china, 0, 1000},
    {"SPLASH", kc_splash, 0, 1000}, {"METAL", kc_metal, 0, 1000}, {"CRUSHED", kc_crushed, KFL_EXP, 1000},
};
static const kit_fam_t KF_PERC[] = {
    {"CLAP", kp_clap, KFL_CLEAN, 1000}, {"BIG CLAP", kp_bigclap, 0, 1000}, {"SNAP", kp_snap, 0, 1000},
    {"MARACAS", kp_maracas, 0, 1000}, {"SHAKER", kp_shaker, 0, 1000}, {"COWBELL", kp_cowbell, KFL_CLEAN, 1000},
    {"CLANG", kp_bell, KFL_EXP, 1000}, {"CRUSHED", kp_crushed, KFL_EXP, 1000},
};
static const kit_fam_t KF_TOM[] = {
    {"CLASSIC", kt_classic, KFL_CLEAN, 1000}, {"FLOOR", kt_floor, KFL_CLEAN, 1000},
    {"RESONANT", kt_resonant, 0, 1000}, {"FM", kt_fm, 0, 1000}, {"BLIP", kt_blip, 0, 1000},
    {"DISTORTED", kt_distorted, 0, 1000}, {"GLITCH", kt_glitch, KFL_EXP, 1000},
};
#define KF_N(a) (uint8_t)(sizeof(a) / sizeof((a)[0]))
static const kit_fam_t *const KIT_FAM[KG_N] = {KF_KICK, KF_SNARE, KF_HAT, KF_CYM, KF_PERC, KF_TOM};
static const uint8_t KIT_NFAM[KG_N] = {KF_N(KF_KICK), KF_N(KF_SNARE), KF_N(KF_HAT), KF_N(KF_CYM), KF_N(KF_PERC),
                                       KF_N(KF_TOM)};
#define KIT_FMAX 8u
typedef char kit_fmax_ok[KF_N(KF_KICK) <= KIT_FMAX && KF_N(KF_SNARE) <= KIT_FMAX && KF_N(KF_HAT) <= KIT_FMAX &&
                         KF_N(KF_CYM) <= KIT_FMAX && KF_N(KF_PERC) <= KIT_FMAX && KF_N(KF_TOM) <= KIT_FMAX ? 1 : -1];
static uint32_t kit_nfam(uint32_t g) { return KIT_NFAM[g < KG_N ? g : 0u]; }
static const kit_fam_t *kit_fam(uint32_t g, uint32_t f)
{
    g = g < KG_N ? g : 0u;
    return &KIT_FAM[g][f < KIT_NFAM[g] ? f : 0u];
}
static const char *kit_fam_name(uint32_t g, uint32_t f) { return kit_fam(g, f)->name; }

/* the kit's shaper off when nothing goes through it */
static void kit_shaper_tidy(sound_t *s)          /* (build 11: the shaper kept ready, so a DRIVE lock drives -- it was
                                                  * set to 0 when no instrument went through it, and the locks did nothing) */
{
    if (s->val[KP_SHLEV] < GK(450)) {
        s->val[KP_SHTYPE] = GI(KSH_OD);
        s->val[KP_SHLEV] = GK(600);
    }
}
/* group g of the kit s drawn again: its parameters back to PERC's, then family f's recipe (-1: one drawn, the roughest a
 * little less often; clean_w: the clean ones' weight, NEW FUGUE's more) */
static uint32_t kit_draw_fam(uint32_t g, rnd_t *r, uint32_t clean_w)
{
    uint8_t w[KIT_FMAX];
    uint32_t f, n = kit_nfam(g);
    for (f = 0; f < n; f++) {
        uint32_t fl = kit_fam(g, f)->flags;
        w[f] = (uint8_t)((fl & KFL_CLEAN) ? clean_w : (fl & KFL_EXP) ? 2u : 3u);
    }
    return rnd_weighted(r, w, n);
}
static void gen_kit_group_w(sound_t *s, uint8_t *fam, uint32_t g, int32_t f, uint32_t seed, uint32_t clean_w)
{
    const eng_t *E = eng_of(E_PERC);
    const kit_fam_t *F;
    rnd_t rr, *r = &rr;
    uint32_t j;
    int32_t t;
    g = g < KG_N ? g : 0u;
    rnd_seed(r, seed ? seed : 1u);
    if (s->eng != E_PERC)
        snd_default(s, E_PERC);
    for (j = KG_P0[g]; j < (uint32_t)KG_P0[g] + KG_PN[g]; j++)
        s->val[j] = E->par[j].def;
    for (j = KG_MIX[g]; j < KG_MIX[g] + 5u; j++)
        s->val[j] = E->par[j].def;
    gs(s, KG_MIX[g] + 2u, 0);                    /* (the aux sends: nothing here) */
    gs(s, KG_MIX[g] + 3u, 0);
    fam[g] = (uint8_t)(f >= 0 && (uint32_t)f < kit_nfam(g) ? (uint32_t)f : kit_draw_fam(g, r, clean_w));
    F = kit_fam(g, fam[g]);
    t = F->fn(s, r);
    switch (g) {                                 /* (the pan: the kick and snare near the middle) */
    case KG_KICK: K_V(KG_MIX[g] + 1u, 0); break;
    case KG_SNARE: k_pan(s, r, g, 0, 100); break;
    case KG_HAT: k_pan(s, r, g, 150, 350); break;
    case KG_CYM: k_pan(s, r, g, 200, 400); break;
    case KG_PERC: k_pan(s, r, g, 100, 400); break;
    default: k_pan(s, r, g, 100, 300); break;
    }
    gs(s, KG_MIX[g], (int32_t)(((((int64_t)GK(F->vol) * t) >> 16) * rnd_in(r, 58982, 72090)) >> 16));
    kit_shaper_tidy(s);
}
static void gen_kit_group(sound_t *s, uint8_t *fam, uint32_t g, int32_t f, uint32_t seed)
{
    gen_kit_group_w(s, fam, g, f, seed, 3u);
}
/* a whole kit (NEW FUGUE, the start): each group a family, the clean ones as often as the others */
static void gen_kit(sound_t *s, uint8_t *fam, uint32_t seed)
{
    rnd_t r;
    uint32_t g;
    rnd_seed(&r, seed ? seed : 5u);
    snd_default(s, E_PERC);
    for (g = 0; g < KG_N; g++)
        gen_kit_group_w(s, fam, g, -1, rnd_next(&r), 4u);
    s->seed = seed;
    s->family = 0;
}
#undef K_U
#undef K_L
#undef K_V
#undef K_E
#undef K_I
#undef K_CH
#undef K_NOINL
#undef KF_N
