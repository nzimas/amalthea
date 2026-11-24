/* SPDX-License-Identifier: GPL-3.0-only
 * Amalthea's own recipe book for the three engines inherited from Fugitive (VAP, CHIP, PERC).
 * Included by patches.c after Fugitive's gen.c / gen_perc.c, whose setters and kit families it reuses.
 *
 * Fugitive's recipes were written for a fugue of clean, steady monophonic lines: short attacks, releases
 * under 1.5 s, no pitch movement, clean families drawn most often. Amalthea is aimed at dark synthwave,
 * dark ambient, industrial, EBM, electroacoustic and rhythmic noise, so these families are low, heavy,
 * resonant, crushed or slow, and allow long tails and small pitch motion (impacts, tape wow). Formant /
 * vowel / bright-bell recipes are gone. A family is still a seeded range: the same (engine, seed) always
 * cooks the same patch. */

/* ================================================================== VAP */
static int32_t av_sub_pressure(sound_t *s, rnd_t *r)     /* a sub-heavy saw / square under a closed ladder */
{
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(800), GK(1000));
    G_U(GV_MIX, -GK(200), GK(300));
    G_V(GV_TUNE2, -GI(12));
    G_U(GV_DET, GI(2), GI(9));
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    G_E(GV_TYPE, G_CH(60) ? GVT_MG24 : GVT_LPOB24);
    G_U(GV_TRK, GK(100), GK(350));
    gv_cut(s, r, GHZ(90), GHZ(320));
    G_U(GV_RES, GK(150), GK(550));
    G_U(GV_FENV, GK(25), GK(90));
    G_L(GV_FD, GK(120), GK(450));
    G_U(GV_FS, GK(50), GK(250));
    G_V(GV_FR, GK(150));
    G_V(GV_AA, 0);
    G_V(GV_AD, GK(600));
    G_U(GV_AS, GK(800), GK(1000));
    G_L(GV_AR, GK(90), GK(320));
    return 65536;
}
static int32_t av_ebm_bass(sound_t *s, rnd_t *r)         /* the sequenced bass: snap of a resonant 24 dB filter */
{
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(900), GK(1000));
    G_U(GV_PW2, -GK(400), -GK(150));
    G_U(GV_MIX, -GK(400), GK(100));
    G_V(GV_TUNE2, G_CH(45) ? -GI(12) : 0);
    G_U(GV_DET, GI(3), GI(12));
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    G_P(GV_TYPE, GVT_LPOB24, GVT_MG24, GVT_LPOB24);
    G_U(GV_TRK, GK(200), GK(500));
    gv_cut(s, r, GHZ(110), GHZ(380));
    G_U(GV_RES, GK(400), GK(820));
    G_U(GV_FENV, GK(90), GK(220));
    G_L(GV_FD, GK(55), GK(190));
    G_U(GV_FS, 0, GK(120));
    G_V(GV_FR, GK(80));
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(160), GK(480));
    G_U(GV_AS, GK(250), GK(650));
    G_L(GV_AR, GK(50), GK(160));
    return 65536;
}
static int32_t av_acid_wound(sound_t *s, rnd_t *r)       /* a ladder near self-oscillation, a long squelch */
{
    G_U(GV_SH1, GK(500), GK(1000));
    G_V(GV_MIX, -GK(1000));
    G_E(GV_RST1, 1);
    G_E(GV_TYPE, GVT_MG24);
    G_U(GV_TRK, GK(300), GK(700));
    gv_cut(s, r, GHZ(120), GHZ(420));
    G_U(GV_RES, GK(780), GK(980));
    G_U(GV_FENV, GK(120), GK(350));
    G_L(GV_FD, GK(110), GK(420));
    G_V(GV_FS, 0);
    G_V(GV_FR, GK(120));
    G_V(GV_AA, 0);
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(700), GK(1000));
    G_L(GV_AR, GK(60), GK(180));
    return 52000;
}
static int32_t av_iron_stab(sound_t *s, rnd_t *r)        /* detuned saws, a hard filter snap, no sustain */
{
    G_V(GV_SH1, GK(500));
    G_V(GV_SH2, GK(500));
    G_U(GV_DET, GI(9), GI(24));
    G_P(GV_TUNE2, 0, 0, 12);
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    G_P(GV_TYPE, GVT_MG24, GVT_LPOB24, GVT_LPSV24);
    G_U(GV_TRK, GK(500), GK(900));
    gv_cut(s, r, GHZ(180), GHZ(700));
    G_U(GV_RES, GK(450), GK(850));
    G_U(GV_FENV, GK(180), GK(450));
    G_L(GV_FD, GK(40), GK(150));
    G_V(GV_FS, 0);
    G_V(GV_FR, GK(100));
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(140), GK(380));
    G_V(GV_AS, 0);
    G_L(GV_AR, GK(150), GK(600));
    return 65536;
}
static int32_t av_black_brass(sound_t *s, rnd_t *r)      /* a swelling filter on low saws: dark synthwave horns */
{
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(450), GK(600));
    G_U(GV_DET, GI(7), GI(20));
    G_V(GV_TUNE2, G_CH(50) ? -GI(12) : 0);
    G_E(GV_TYPE, GVT_LPOB24);
    G_U(GV_TRK, GK(500), GK(900));
    gv_cut(s, r, GHZ(200), GHZ(600));
    G_U(GV_RES, GK(150), GK(450));
    G_U(GV_FENV, GK(70), GK(180));
    G_L(GV_FA, GK(40), GK(180));
    G_L(GV_FD, GK(300), GK(900));
    G_U(GV_FS, GK(250), GK(550));
    G_V(GV_FR, GK(400));
    G_L(GV_AA, GK(15), GK(70));
    G_V(GV_AD, GK(800));
    G_U(GV_AS, GK(850), GK(1000));
    G_L(GV_AR, GK(250), GK(900));
    return 65536;
}
static int32_t av_saw_stack(sound_t *s, rnd_t *r)        /* wide-detuned saws, an octave or a fifth apart */
{
    G_V(GV_SH1, GK(500));
    G_V(GV_SH2, GK(500));
    G_U(GV_DET, GI(22), GI(55));
    {
        static const int8_t t[] = {-12, -12, 0, 7, -5, 12};
        G_V(GV_TUNE2, GI(t[rnd_below(r, 6)]));
    }
    G_U(GV_MIX, -GK(250), GK(250));
    G_U(GV_NOISE, 0, GK(60));
    G_P(GV_TYPE, GVT_LPSV24, GVT_LPOB24, GVT_LPSV12);
    G_U(GV_TRK, GK(500), GK(1000));
    gv_cut(s, r, GHZ(500), GHZ(2200));
    G_U(GV_RES, GK(100), GK(350));
    G_U(GV_FENV, 0, GK(60));
    G_L(GV_FD, GK(300), GK(1200));
    G_V(GV_FS, GK(300));
    G_L(GV_AA, GK(4), GK(45));
    G_V(GV_AD, GK(900));
    G_U(GV_AS, GK(800), GK(1000));
    G_L(GV_AR, GK(400), GK(1600));
    return 60000;
}
static int32_t av_mourning_pad(sound_t *s, rnd_t *r)     /* a slow, dark pad breathing under a moving low-pass */
{
    G_U(GV_SH1, GK(250), GK(500));
    G_U(GV_SH2, GK(400), GK(600));
    G_U(GV_DET, GI(14), GI(40));
    {
        static const int8_t t[] = {-12, 0, 7, -5, 12, -12};
        G_V(GV_TUNE2, GI(t[rnd_below(r, 6)]));
    }
    G_U(GV_NOISE, GK(20), GK(140));
    G_E(GV_TYPE, GVT_LPSV12);
    G_U(GV_TRK, GK(300), GK(700));
    gv_cut(s, r, GHZ(240), GHZ(900));
    G_U(GV_RES, GK(150), GK(450));
    G_V(GV_FENV, 0);
    gv_slot(s, 0, GVS_LFO1, GVD_CUT, rnd_in(r, GK(15), GK(55)));
    G_L(GV_L1F, GK(40), GK(300));
    G_E(GV_L1W, 0);
    G_E(GV_L1R, 2);
    gv_slot(s, 1, GVS_LFO2, GVD_MIX, rnd_in(r, GK(150), GK(400)));
    G_L(GV_L2F, GK(30), GK(200));
    G_E(GV_L2W, 1);
    G_E(GV_L2R, 2);
    G_L(GV_AA, GK(250), GK(1500));
    G_V(GV_AD, GK(1000));
    G_V(GV_AS, GK(1000));
    G_L(GV_AR, GK(1800), GK(5000));
    return 65536;
}
static int32_t av_abyss(sound_t *s, rnd_t *r)            /* sub drone two octaves deep, a random wander on the band */
{
    G_U(GV_SH1, 0, GK(500));
    G_U(GV_SH2, GK(450), GK(550));
    G_V(GV_TUNE2, G_CH(50) ? -GI(24) : -GI(12));
    G_U(GV_DET, GI(4), GI(20));
    G_U(GV_MIX, -GK(300), GK(200));
    G_U(GV_NOISE, GK(30), GK(200));
    G_P(GV_TYPE, GVT_LPSV12, GVT_BPSV12, GVT_LPSV24);
    G_U(GV_TRK, GK(200), GK(600));
    gv_cut(s, r, GHZ(150), GHZ(600));
    G_U(GV_RES, GK(300), GK(700));
    G_V(GV_FENV, 0);
    gv_slot(s, 0, GVS_LFO1, GVD_CUT, rnd_in(r, GK(10), GK(35)));
    G_L(GV_L1F, GK(60), GK(500));
    G_E(GV_L1W, 5);                              /* S&H */
    G_E(GV_L1R, 2);
    G_L(GV_AA, GK(1200), GK(4000));
    G_V(GV_AD, GK(1000));
    G_V(GV_AS, GK(1000));
    G_L(GV_AR, GK(3000), GK(8000));
    return 65536;
}
static int32_t av_tape_dread(sound_t *s, rnd_t *r)       /* a dark pad on worn tape: a slow wow on one oscillator */
{
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, 0, GK(300));
    G_U(GV_DET, GI(5), GI(18));
    G_V(GV_TUNE2, G_CH(50) ? GI(12) : 0);
    G_U(GV_NOISE, GK(30), GK(120));
    G_E(GV_TYPE, GVT_LPSV24);
    G_U(GV_TRK, GK(400), GK(800));
    gv_cut(s, r, GHZ(400), GHZ(1400));
    G_U(GV_RES, GK(100), GK(300));
    G_V(GV_FENV, 0);
    gv_slot(s, 0, GVS_LFO1, GVD_P2, gsign(r, rnd_in(r, 260, 900)));   /* (~ 0.2 .. 0.6 semitone) */
    G_L(GV_L1F, GK(250), GK(1400));
    G_E(GV_L1W, 0);
    G_E(GV_L1R, 2);
    G_L(GV_AA, GK(150), GK(900));
    G_V(GV_AD, GK(1000));
    G_V(GV_AS, GK(1000));
    G_L(GV_AR, GK(1200), GK(3500));
    return 65536;
}
static int32_t av_rust_sync(sound_t *s, rnd_t *r)        /* hard sync tearing open with the aux envelope */
{
    G_E(GV_SYNC, 1);
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(450), GK(800));
    G_U(GV_MIX, GK(400), GK(900));
    G_U(GV_TUNE2, GI(5), GI(24));
    G_U(GV_NOISE, 0, GK(120));
    G_P(GV_TYPE, GVT_LPOB12, GVT_LPOB24, GVT_BPOB12);
    G_U(GV_TRK, GK(500), GK(900));
    gv_cut(s, r, GHZ(1200), GHZ(4200));
    G_U(GV_RES, GK(150), GK(500));
    G_V(GV_FENV, 0);
    gv_slot(s, 0, GVS_AUX, GVD_SH2, rnd_in(r, GK(300), GK(800)));
    G_V(GV_XA, 0);
    G_L(GV_XD, GK(80), GK(400));
    G_V(GV_XS, 0);
    G_V(GV_XR, GK(100));
    G_V(GV_AA, 0);
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(750), GK(1000));
    G_L(GV_AR, GK(120), GK(500));
    return 56000;
}
static int32_t av_serrated(sound_t *s, rnd_t *r)         /* sync stepped by S&H: a snarling, unstable lead */
{
    G_E(GV_SYNC, 1);
    G_U(GV_SH1, GK(500), GK(1000));
    G_U(GV_SH2, GK(500), GK(1000));
    G_U(GV_MIX, GK(300), GK(800));
    G_U(GV_TUNE2, GI(7), GI(19));
    gv_slot(s, 0, GVS_LFO1, GVD_SH2, rnd_in(r, GK(400), GK(900)));
    G_L(GV_L1F, GHZ(4), GHZ(16));
    G_E(GV_L1W, 5);
    G_E(GV_L1R, 0);
    G_P(GV_TYPE, GVT_LPOB12, GVT_LPOB24, GVT_BPSV24);
    G_U(GV_TRK, GK(600), GK(1000));
    gv_cut(s, r, GHZ(500), GHZ(2500));
    G_U(GV_RES, GK(200), GK(550));
    G_V(GV_FENV, 0);
    G_V(GV_AA, 0);
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(800), GK(1000));
    G_L(GV_AR, GK(100), GK(400));
    return 52000;
}
static int32_t av_ring_corrode(sound_t *s, rnd_t *r)     /* audio-rate AM: clangorous, out-of-scale sidebands */
{
    G_U(GV_SH1, GK(300), GK(700));
    G_U(GV_SH2, GK(500), GK(1000));
    G_U(GV_MIX, -GK(800), GK(200));
    G_V(GV_TUNE2, G_CH(50) ? -GI(12) : GI(7));
    G_U(GV_NOISE, 0, GK(100));
    gv_slot(s, 0, GVS_LFO1, GVD_VOL, -rnd_in(r, GK(1300), GK(2000)));
    G_L(GV_L1F, GHZ(23), GHZ(97));
    G_P(GV_L1W, 1, 4, 0);
    G_E(GV_L1R, 0);
    G_P(GV_TYPE, GVT_LPOB24, GVT_BPOB24, GVT_LPSV12);
    G_U(GV_TRK, GK(500), GK(1000));
    gv_cut(s, r, GHZ(500), GHZ(2500));
    G_U(GV_RES, GK(200), GK(600));
    G_U(GV_FENV, 0, GK(80));
    G_L(GV_FD, GK(150), GK(600));
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(300), GK(1200));
    G_U(GV_AS, GK(400), GK(900));
    G_L(GV_AR, GK(200), GK(1200));
    return 60000;
}
static int32_t av_noise_wall(sound_t *s, rnd_t *r)       /* noise through a resonant band on the note, its level chopped by S&H */
{
    G_U(GV_NOISE, GK(600), GK(1000));
    G_V(GV_SH1, GK(500));
    G_U(GV_MIX, -GK(1000), -GK(600));
    G_P(GV_TYPE, GVT_BPOB24, GVT_BPSV24, GVT_LPOB24);
    G_V(GV_TRK, GK(1000));
    G_V(GV_CUT, G_F4);
    G_U(GV_RES, GK(600), GK(900));
    G_V(GV_FENV, 0);
    gv_slot(s, 0, GVS_LFO1, GVD_VOL, -rnd_in(r, GK(800), GK(1600)));   /* (S&H on the level: a random chop) */
    G_L(GV_L1F, GHZ(3), GHZ(18));
    G_E(GV_L1W, 5);
    G_E(GV_L1R, 0);
    G_V(GV_AA, 0);
    G_V(GV_AD, GK(800));
    G_U(GV_AS, GK(800), GK(1000));
    G_L(GV_AR, GK(150), GK(900));
    return 65536;
}
static int32_t av_static_gate(sound_t *s, rnd_t *r)      /* a tempo-locked chop on saws and noise: machine rhythm */
{
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(700), GK(1000));
    G_U(GV_DET, GI(6), GI(20));
    G_V(GV_TUNE2, G_CH(50) ? -GI(12) : 0);
    G_U(GV_NOISE, GK(100), GK(400));
    gv_slot(s, 0, GVS_LFO1, GVD_VOL, -rnd_in(r, GK(1600), GK(2000)));
    G_E(GV_L1S, 1);
    G_P(GV_L1F, 11, 12, 13, 12);
    G_P(GV_L1W, 2, 4, 3);
    G_E(GV_L1R, 0);
    G_P(GV_TYPE, GVT_LPOB24, GVT_HPOB12, GVT_BPOB12);
    G_U(GV_TRK, GK(600), GK(1000));
    gv_cut(s, r, GHZ(400), GHZ(2200));
    G_U(GV_RES, GK(200), GK(600));
    G_V(GV_FENV, 0);
    G_V(GV_AA, 0);
    G_V(GV_AD, GK(500));
    G_U(GV_AS, GK(900), GK(1000));
    G_L(GV_AR, GK(80), GK(300));
    return 65536;
}
static int32_t av_impact(sound_t *s, rnd_t *r)           /* a falling-pitch hit with a noise strike: industrial tom */
{
    G_U(GV_SH1, 0, GK(500));
    G_U(GV_SH2, GK(500), GK(1000));
    G_V(GV_TUNE2, -GI(12));
    G_U(GV_MIX, -GK(500), GK(300));
    gv_slot(s, 0, GVS_AUX, GVD_P1, rnd_in(r, 9000, 22000));     /* (~ 0.5 .. 1.3 octaves at the strike, falling) */
    gv_slot(s, 1, GVS_AUX, GVD_P2, rnd_in(r, 9000, 22000));
    gv_slot(s, 2, GVS_AUX, GVD_NOISE, rnd_in(r, GK(500), GK(1000)));
    G_V(GV_XA, 0);
    G_L(GV_XD, GK(18), GK(70));
    G_V(GV_XS, 0);
    G_V(GV_XR, GK(40));
    G_E(GV_RST1, 1);
    G_E(GV_RST2, 1);
    G_P(GV_TYPE, GVT_LPOB24, GVT_MG24);
    G_U(GV_TRK, GK(300), GK(700));
    gv_cut(s, r, GHZ(250), GHZ(900));
    G_U(GV_RES, GK(200), GK(600));
    G_U(GV_FENV, GK(150), GK(400));
    G_L(GV_FD, GK(30), GK(110));
    G_V(GV_FS, 0);
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(180), GK(600));
    G_V(GV_AS, 0);
    G_L(GV_AR, GK(150), GK(500));
    return 65536;
}
static int32_t av_cold_pluck(sound_t *s, rnd_t *r)       /* a thin pulse through a resonant high-pass: a cold arp */
{
    G_U(GV_SH1, GK(900), GK(1000));
    G_U(GV_PW1, -GK(450), -GK(250));
    G_U(GV_SH2, GK(400), GK(550));
    G_U(GV_MIX, -GK(500), GK(0));
    G_U(GV_DET, GI(3), GI(10));
    G_E(GV_RST1, 1);
    G_P(GV_TYPE, GVT_LPOB24, GVT_BPOB12, GVT_HPOB12, GVT_LPOB24);
    G_U(GV_TRK, GK(800), GK(1000));
    gv_cut(s, r, GHZ(180), GHZ(700));
    G_U(GV_RES, GK(300), GK(650));
    G_U(GV_FENV, GK(60), GK(180));
    G_L(GV_FD, GK(40), GK(160));
    G_V(GV_FS, 0);
    G_V(GV_AA, 0);
    G_L(GV_AD, GK(120), GK(350));
    G_V(GV_AS, 0);
    G_L(GV_AR, GK(300), GK(1100));
    return 70000;
}
static int32_t av_phase_ghost(sound_t *s, rnd_t *r)      /* a notch sweeping very slowly through a noisy drone */
{
    G_V(GV_SH1, GK(500));
    G_U(GV_SH2, GK(500), GK(1000));
    G_U(GV_DET, GI(8), GI(30));
    G_V(GV_TUNE2, G_CH(50) ? -GI(12) : GI(12));
    G_U(GV_NOISE, GK(80), GK(250));
    G_E(GV_TYPE, G_CH(50) ? GVT_NSV24 : GVT_NSV12);
    G_V(GV_TRK, 0);
    G_L(GV_CUT, GHZ(400), GHZ(2500));
    G_U(GV_RES, GK(300), GK(800));
    G_V(GV_FENV, 0);
    gv_slot(s, 0, GVS_LFO1, GVD_CUT, rnd_in(r, GK(40), GK(140)));
    G_L(GV_L1F, GK(30), GK(250));
    G_E(GV_L1W, 0);
    G_E(GV_L1R, 2);
    G_L(GV_AA, GK(300), GK(1800));
    G_V(GV_AD, GK(1000));
    G_V(GV_AS, GK(1000));
    G_L(GV_AR, GK(1500), GK(4500));
    return 60000;
}
static int32_t av_shiver(sound_t *s, rnd_t *r)           /* a pulse whose width trembles: nervous, cold */
{
    G_V(GV_SH1, GK(1000));
    G_U(GV_SH2, GK(500), GK(1000));
    gv_slot(s, 0, GVS_LFO1, GVD_PW1, rnd_in(r, GK(300), GK(700)));
    G_L(GV_L1F, GHZ(3), GHZ(11));
    G_P(GV_L1W, 0, 5, 4);
    G_E(GV_L1R, 0);
    G_U(GV_DET, GI(4), GI(15));
    G_V(GV_TUNE2, G_CH(50) ? -GI(12) : 0);
    G_P(GV_TYPE, GVT_LPSV24, GVT_LPOB24);
    G_U(GV_TRK, GK(500), GK(1000));
    gv_cut(s, r, GHZ(500), GHZ(1800));
    G_U(GV_RES, GK(150), GK(450));
    G_U(GV_FENV, 0, GK(80));
    G_L(GV_FD, GK(200), GK(700));
    G_L(GV_AA, 0, GK(40));
    G_V(GV_AD, GK(600));
    G_U(GV_AS, GK(700), GK(1000));
    G_L(GV_AR, GK(300), GK(1200));
    return 62000;
}
static const gen_fam_t AM_VAP[] = {
    {"SUB PRESSURE", av_sub_pressure, GR_BASS, 0, 1100},
    {"EBM BASS", av_ebm_bass, GR_BASS, 0, 1300},
    {"ACID WOUND", av_acid_wound, GR_BASS | GR_MID, GFL_EXP, 800},
    {"IRON STAB", av_iron_stab, GR_MID, 0, 1200},
    {"BLACK BRASS", av_black_brass, GR_MID | GR_PAD, 0, 1000},
    {"SAW STACK", av_saw_stack, GR_MID | GR_HIGH, 0, 1100},
    {"MOURNING PAD", av_mourning_pad, GR_PAD, GFL_SLOW, 2000},
    {"ABYSS", av_abyss, GR_PAD | GR_BASS, GFL_SLOW, 1400},
    {"TAPE DREAD", av_tape_dread, GR_PAD, GFL_SLOW, 1000},
    {"RUST SYNC", av_rust_sync, GR_MID | GR_HIGH, 0, 1200},
    {"SERRATED", av_serrated, GR_MID | GR_HIGH, GFL_EXP, 2000},
    {"RING CORRODE", av_ring_corrode, GR_MID, GFL_EXP, 1500},
    {"NOISE WALL", av_noise_wall, GR_MID | GR_PAD, GFL_EXP, 700},
    {"STATIC GATE", av_static_gate, GR_MID, GFL_EXP, 1000},
    {"IMPACT", av_impact, GR_BASS | GR_MID, 0, 1600},
    {"COLD PLUCK", av_cold_pluck, GR_HIGH | GR_MID, 0, 2000},
    {"PHASE GHOST", av_phase_ghost, GR_PAD, GFL_SLOW, 1000},
    {"SHIVER", av_shiver, GR_MID | GR_HIGH, 0, 1000},
};

/* ================================================================== CHIP */
static int32_t ac_crush_bass(sound_t *s, rnd_t *r)       /* a few bits and a heavy sub under a resonant low-pass */
{
    G_P(GC_MODEL, GCM_PULSE, GCM_SID, GCM_PULSE);
    G_U(GC_DUTY, GK(50), GK(450));
    G_U(GC_SUB, GK(500), GK(900));
    G_U(GC_BITS, GI(3), GI(6));
    G_U(GC_DEC, GI(1), GI(3));
    G_V(GC_A, 0);
    G_L(GC_D, GK(120), GK(400));
    G_U(GC_S, GK(350), GK(800));
    G_L(GC_R, GK(50), GK(200));
    G_L(GC_CUT, GHZ(300), GHZ(1500));
    G_U(GC_RES, GK(300), GK(650));
    G_U(GC_ENV, GK(300), GK(650));
    return 65536;
}
static int32_t ac_decay_square(sound_t *s, rnd_t *r)     /* a thin, decayed pulse lead with a slow, sick vibrato */
{
    G_E(GC_MODEL, GCM_PULSE);
    G_U(GC_DUTY, GK(40), GK(200));
    G_U(GC_PWMD, GK(50), GK(250));
    G_L(GC_PWMR, GK(200), GK(1500));
    G_U(GC_BITS, GI(5), GI(8));
    G_U(GC_DEC, GI(1), GI(3));
    G_L(GC_VIBR, GK(3500), GK(5500));
    G_U(GC_VIBD, GK(10), GK(45));
    G_L(GC_A, GK(2), GK(30));
    G_L(GC_D, GK(300), GK(900));
    G_U(GC_S, GK(500), GK(850));
    G_L(GC_R, GK(300), GK(1100));
    G_L(GC_CUT, GHZ(1400), GHZ(4500));
    G_U(GC_RES, GK(150), GK(450));
    G_U(GC_ENV, 0, GK(250));
    return 65536;
}
static int32_t ac_zap_hit(sound_t *s, rnd_t *r)          /* a laser falling into the note: industrial percussion */
{
    G_E(GC_MODEL, GCM_ZAP);
    G_U(GC_DUTY, GK(100), GK(600));
    G_V(GC_SWEEP, GI(24 + rnd_below(r, 13)));
    G_L(GC_SWT, GK(40), GK(220));
    G_U(GC_SUB, GK(200), GK(700));
    G_U(GC_BITS, GI(4), GI(9));
    G_V(GC_A, 0);
    G_L(GC_D, GK(90), GK(350));
    G_V(GC_S, 0);
    G_L(GC_R, GK(60), GK(250));
    G_L(GC_CUT, GHZ(1200), GHZ(6000));
    G_U(GC_RES, GK(150), GK(500));
    G_U(GC_ENV, GK(200), GK(600));
    return 65536;
}
static int32_t ac_lfsr_storm(sound_t *s, rnd_t *r)       /* the metallic LFSR on the note under a resonant filter */
{
    G_E(GC_MODEL, GCM_NOISE);
    G_U(GC_DUTY, GK(600), GK(1000));
    G_V(GC_PWMD, 20480);
    G_V(GC_TUNE, G_CH(50) ? GI(12) : GI(24));
    G_U(GC_DEC, GI(1), GI(4));
    G_U(GC_BITS, GI(4), GI(8));
    G_L(GC_A, 0, GK(60));
    G_L(GC_D, GK(300), GK(1200));
    G_U(GC_S, GK(500), GK(900));
    G_L(GC_R, GK(200), GK(1200));
    G_L(GC_CUT, GHZ(500), GHZ(2500));
    G_U(GC_RES, GK(400), GK(750));
    G_U(GC_ENV, 0, GK(300));
    return 60000;
}
static int32_t ac_noise_grit(sound_t *s, rnd_t *r)       /* long-sequence noise hits: crushed snare / static burst */
{
    G_E(GC_MODEL, GCM_NOISE);
    G_U(GC_DUTY, 0, GK(400));
    G_U(GC_DEC, GI(1), GI(6));
    G_U(GC_BITS, GI(3), GI(7));
    G_V(GC_A, 0);
    G_L(GC_D, GK(60), GK(280));
    G_V(GC_S, 0);
    G_L(GC_R, GK(40), GK(200));
    G_L(GC_CUT, GHZ(1500), GHZ(9000));
    G_U(GC_RES, GK(100), GK(500));
    G_U(GC_ENV, GK(100), GK(500));
    return 55000;
}
static int32_t ac_dead_bell(sound_t *s, rnd_t *r)        /* an inharmonic FM bell, decimated and darkened */
{
    G_E(GC_MODEL, GCM_BELL);
    G_U(GC_DUTY, GK(550), GK(1000));
    G_U(GC_PWMD, GK(450), GK(950));
    G_U(GC_DEC, GI(2), GI(6));
    G_U(GC_BITS, GI(5), GI(8));
    G_V(GC_A, 0);
    G_L(GC_D, GK(500), GK(2000));
    G_U(GC_S, 0, GK(120));
    G_L(GC_R, GK(500), GK(2200));
    G_L(GC_CUT, GHZ(1800), GHZ(6000));
    G_U(GC_RES, GK(100), GK(400));
    G_V(GC_ENV, 0);
    G_U(GC_SUB, GK(100), GK(400));
    return 60000;
}
static int32_t ac_sid_growl(sound_t *s, rnd_t *r)        /* SID ring-mod saw into a resonant, enveloped low-pass */
{
    G_E(GC_MODEL, GCM_SID);
    G_U(GC_DUTY, GK(750), GK(1000));
    G_U(GC_PWMD, 0, GK(300));
    G_U(GC_SUB, GK(300), GK(700));
    G_U(GC_BITS, GI(6), GI(10));
    G_V(GC_A, 0);
    G_L(GC_D, GK(200), GK(600));
    G_U(GC_S, GK(450), GK(850));
    G_L(GC_R, GK(100), GK(400));
    G_L(GC_CUT, GHZ(350), GHZ(1500));
    G_U(GC_RES, GK(450), GK(800));
    G_U(GC_ENV, GK(300), GK(650));
    return 60000;
}
static int32_t ac_super_dread(sound_t *s, rnd_t *r)      /* a slow super-square pad, worn down to a few bits */
{
    G_E(GC_MODEL, GCM_SUPER);
    G_U(GC_DUTY, GK(300), GK(900));
    G_U(GC_PWMD, GK(100), GK(400));
    G_L(GC_PWMR, GK(150), GK(900));
    G_U(GC_BITS, GI(5), GI(9));
    G_U(GC_DEC, GI(1), GI(2));
    G_U(GC_SUB, GK(200), GK(500));
    G_L(GC_A, GK(400), GK(2000));
    G_L(GC_D, GK(800), GK(2000));
    G_U(GC_S, GK(700), GK(1000));
    G_L(GC_R, GK(1200), GK(4000));
    G_L(GC_CUT, GHZ(600), GHZ(2200));
    G_U(GC_RES, GK(150), GK(450));
    G_U(GC_ENV, 0, GK(200));
    return 65536;
}
static int32_t ac_drum_slam(sound_t *s, rnd_t *r)        /* CHIP DRUM's falling body, crushed: a pitched slam */
{
    G_E(GC_MODEL, GCM_DRUM);
    G_U(GC_DUTY, GK(100), GK(800));
    G_U(GC_SUB, GK(300), GK(800));
    G_U(GC_BITS, GI(3), GI(8));
    G_U(GC_DEC, GI(1), GI(4));
    G_V(GC_A, 0);
    G_L(GC_D, GK(120), GK(450));
    G_V(GC_S, 0);
    G_L(GC_R, GK(80), GK(300));
    G_L(GC_CUT, GHZ(900), GHZ(5000));
    G_U(GC_RES, GK(150), GK(450));
    G_U(GC_ENV, GK(200), GK(500));
    return 65536;
}
static int32_t ac_broken_chord(sound_t *s, rnd_t *r)     /* a minor chord arpeggiated at audio-ish speed: a smear */
{
    G_P(GC_MODEL, GCM_PULSE, GCM_SID, GCM_SUPER);
    G_P(GC_ARP, 4, 7, 2, 1, 4);                  /* minor, min7, fifth, octave */
    G_L(GC_ARPS, GHZ(28), GHZ(70));
    G_U(GC_DUTY, GK(150), GK(600));
    G_U(GC_BITS, GI(5), GI(9));
    G_U(GC_DEC, GI(1), GI(3));
    G_L(GC_A, 0, GK(40));
    G_L(GC_D, GK(300), GK(900));
    G_U(GC_S, GK(400), GK(800));
    G_L(GC_R, GK(300), GK(1200));
    G_L(GC_CUT, GHZ(900), GHZ(3500));
    G_U(GC_RES, GK(200), GK(500));
    G_U(GC_ENV, GK(100), GK(400));
    return 60000;
}
static int32_t ac_alias_wound(sound_t *s, rnd_t *r)      /* decimated hard: screaming aliases, out of the scale */
{
    G_P(GC_MODEL, GCM_SID, GCM_PULSE, GCM_SUPER);
    G_U(GC_DUTY, GK(200), GK(900));
    G_U(GC_DEC, GI(8), GI(24));
    G_U(GC_BITS, GI(3), GI(6));
    G_V(GC_A, 0);
    G_L(GC_D, GK(200), GK(700));
    G_U(GC_S, GK(400), GK(900));
    G_L(GC_R, GK(100), GK(500));
    G_L(GC_CUT, GHZ(1200), GHZ(5000));
    G_U(GC_RES, GK(200), GK(550));
    G_U(GC_ENV, 0, GK(300));
    return 50000;
}
static int32_t ac_pulse_drone(sound_t *s, rnd_t *r)      /* a narrow pulse breathing very slowly: a dark drone */
{
    G_E(GC_MODEL, GCM_PULSE);
    G_U(GC_DUTY, GK(60), GK(300));
    G_U(GC_PWMD, GK(300), GK(800));
    G_L(GC_PWMR, GK(100), GK(600));
    G_U(GC_SUB, GK(400), GK(900));
    G_U(GC_BITS, GI(6), GI(12));
    G_L(GC_A, GK(600), GK(2500));
    G_L(GC_D, GK(1000), GK(3000));
    G_V(GC_S, GK(1000));
    G_L(GC_R, GK(2000), GK(5000));
    G_L(GC_CUT, GHZ(300), GHZ(1200));
    G_U(GC_RES, GK(250), GK(550));
    G_V(GC_ENV, 0);
    return 65536;
}
static const gen_fam_t AM_CHIP[] = {
    {"CRUSH BASS", ac_crush_bass, GR_BASS, 0, 1000},
    {"DECAY SQUARE", ac_decay_square, GR_MID | GR_HIGH, 0, 700},
    {"ZAP HIT", ac_zap_hit, GR_MID, 0, 750},
    {"LFSR STORM", ac_lfsr_storm, GR_MID | GR_PAD, GFL_EXP, 600},
    {"NOISE GRIT", ac_noise_grit, GR_HIGH, GFL_EXP, 900},
    {"DEAD BELL", ac_dead_bell, GR_HIGH | GR_MID, 0, 1000},
    {"SID GROWL", ac_sid_growl, GR_BASS | GR_MID, 0, 1300},
    {"SUPER DREAD", ac_super_dread, GR_PAD, GFL_SLOW, 600},
    {"DRUM SLAM", ac_drum_slam, GR_BASS | GR_MID, 0, 1000},
    {"BROKEN CHORD", ac_broken_chord, GR_MID, 0, 1100},
    {"ALIAS WOUND", ac_alias_wound, GR_MID, GFL_EXP, 1100},
    {"PULSE DRONE", ac_pulse_drone, GR_PAD | GR_BASS, GFL_SLOW, 650},
};

/* ================================================================== PERC: kit characters */
/* A kit is drawn as a whole, in one character, from the darker and driven families of each piece; then the
 * kit's shaper and EQ set the character's weight. (Family indices: gen_perc.c KF_* tables.) */
typedef struct {
    const char *name;
    uint8_t fam[KG_N][4];        /* allowed families per group, 255 ends a list */
    uint8_t shaper, shlo, shhi;  /* type, level range (percent) */
    int8_t eq[3][2];             /* low / mid / high, dB ranges */
    uint8_t drive_all;           /* every piece through the shaper */
} kit_char_t;
#define KX 255
static const kit_char_t AM_KITS[] = {
    {"EBM KIT", {{0, 3, 2, KX}, {1, 6, 3, KX}, {4, 2, 3, KX}, {2, 4, KX, KX}, {1, 2, KX, KX}, {1, 5, KX, KX}},
     KSH_OD, 50, 70, {{3, 6}, {-2, 1}, {-3, 0}}, 0},
    {"INDUSTRIAL", {{3, 5, 4, KX}, {6, 5, 4, KX}, {3, 5, 6, KX}, {2, 4, 5, KX}, {6, 7, 1, KX}, {2, 5, 3, KX}},
     KSH_ODP, 60, 85, {{4, 8}, {2, 5}, {-2, 2}}, 1},
    {"RHYTHMIC NOISE", {{5, 6, 7, KX}, {5, 6, 7, 3}, {5, 6, KX, KX}, {5, 2, KX, KX}, {7, 6, KX, KX}, {5, 6, KX, KX}},
     KSH_ODP, 75, 95, {{6, 9}, {3, 6}, {-1, 3}}, 1},
    {"RITUAL", {{1, 4, KX, KX}, {3, 1, KX, KX}, {4, 3, KX, KX}, {4, 2, KX, KX}, {6, 1, KX, KX}, {1, 2, 3, KX}},
     KSH_SAT, 50, 70, {{4, 7}, {-1, 2}, {-6, -2}}, 0},
    {"VOID", {{1, 6, KX, KX}, {7, 4, KX, KX}, {6, 4, 2, KX}, {5, 4, KX, KX}, {7, 2, KX, KX}, {6, 4, KX, KX}},
     KSH_BIT, 60, 90, {{2, 6}, {-3, 1}, {-9, -3}}, 0},
};
#define AM_NKIT (sizeof AM_KITS / sizeof AM_KITS[0])
static void am_kit(sound_t *s, uint32_t seed)
{
    rnd_t rr, *r = &rr;
    uint8_t fam[KG_N];
    uint32_t g, k;
    rnd_seed(r, seed ? seed : 5u);
    k = (uint32_t)rnd_below(r, (int32_t)AM_NKIT);
    const kit_char_t *C = &AM_KITS[k];
    snd_default(s, E_PERC);
    for (g = 0; g < KG_N; g++) {
        uint32_t n = 0;
        while (n < 4 && C->fam[g][n] != KX) n++;
        gen_kit_group_w(s, fam, g, C->fam[g][rnd_below(r, (int32_t)n)], rnd_next(r), 3u);
        if (C->drive_all) gs(s, KG_MIX[g] + 4u, 0);
    }
    gs(s, KP_SHTYPE, GI(C->shaper));
    gs(s, KP_SHLEV, rnd_in(r, GK(C->shlo * 10), GK(C->shhi * 10)));
    for (g = 0; g < 3; g++) gs(s, 81u + g, GI(rnd_in(r, C->eq[g][0], C->eq[g][1])));
    s->seed = seed;
    s->family = (uint8_t)k;
}

/* ================================================================== the draw */
static const gen_fam_t *am_table(uint32_t e, uint32_t *n)
{
    if (e == E_CHIP) { *n = sizeof AM_CHIP / sizeof AM_CHIP[0]; return AM_CHIP; }
    *n = sizeof AM_VAP / sizeof AM_VAP[0];
    return AM_VAP;
}
static void am_sound(sound_t *s, uint32_t e, uint32_t seed)
{
    rnd_t r;
    uint32_t n, f;
    uint8_t w[GEN_FMAX];
    const gen_fam_t *T, *F;
    int32_t t;
    if (e == E_PERC) { am_kit(s, seed); return; }
    T = am_table(e, &n);
    rnd_seed(&r, seed ? seed : 1u);
    snd_default(s, e);
    for (f = 0; f < n; f++) w[f] = (T[f].flags & GFL_EXP) ? 3u : 4u;
    s->family = (uint8_t)rnd_weighted(&r, w, n);
    s->seed = seed;
    F = &T[s->family];
    t = F->fn(s, &r);
    gs(s, GEN_VOLP[e], (int32_t)(((((int64_t)GK(F->vol) * t) >> 16) * rnd_in(&r, 58982, 72090)) >> 16));
}
static const char *am_family(uint32_t e, uint32_t seed)
{
    sound_t s;
    uint32_t n;
    am_sound(&s, e, seed);
    if (e == E_PERC) return AM_KITS[s.family % AM_NKIT].name;
    return am_table(e, &n)[s.family % n].name;
}
