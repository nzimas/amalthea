/* SPDX-License-Identifier: GPL-3.0-only
 * FIBER: the families, as lists the enum, the names, the path weights and HOME's modulation odds are made from.
 *   FK_HAND: the families drawn by code of their own (fiber_gen.c fb_design's cases): X(ID, NAME, OPEN, AMBIENT,
 *            EXPERIMENTAL, MODP) -- the path weights and the odds (%) of a Tonitor modulation drawn with the sound;
 *   FK_ROWS: the families drawn from a row of ranges (fb_row_design): Y(ID, NAME, OPEN, AMBIENT, EXPERIMENTAL, MODP,
 *            { the row }).
 * The order is the families' numbers: sessions and presets keep them, so a family is only ever added at the end.
 * Every row keeps FIBER's rule: no pitch moves but a vibrato (LD_PITCH, a quarter semitone at most). */
#pragma once

/* a row's LFO targets */
enum { LD_NONE, LD_POS, LD_POS1, LD_CUT, LD_VOL, LD_PITCH, LD_LVL2, LD_DIRT, LD_RESO, LD_PAN, LD_PITCH2 };
#define LF_GLOB 1u               /* one LFO for the track */
#define LF_SYNC 2u               /* restarted at each note */
#define FT_ANY 255u              /* (a drive curve drawn) */
typedef struct {
    uint8_t shape, dst, flags;
    int8_t amt[2];               /* positions (POS), semitones (CUT), sixteenths of a semitone (PITCH: 4 at most), % */
    uint16_t hz[2];              /* hundredths of a Hz */
} fk_lfo_t;
typedef struct {
    uint16_t a[2], d[2], r[2];   /* ms, low .. high (drawn on a log scale) */
    uint8_t s[2];                /* sustain, % */
} fk_env_t;
typedef struct {
    uint8_t om[2];               /* the oscillators' kinds (OM_*) */
    const uint8_t *tab;          /* the tables drawn from (wavetable oscillators), 255 ends the list; 0: the family's
                                  * positions are the common waves' or VA's / STRINGS' */
    uint8_t pos[2], pos2[2];     /* positions, OSC 1 and OSC 2 (low, high; 0, 0 for OSC 2: as OSC 1's) */
    int8_t semi2[4];             /* OSC 2's interval, one drawn of four (semitones; repeat one for fewer) */
    uint8_t det2;                /* OSC 2's detune, up to (1/256 semitone), either way */
    uint8_t lvl2[2];             /* OSC 2's level (Q8: 256 full; OSC 1's is 200) */
    uint8_t ft, ft2, ft2pct;     /* the filter section; another, and how often (%) */
    uint8_t dc;                  /* the drive curve (DC_*, FT_ANY: drawn) */
    uint8_t cut[2], res[2];      /* the cutoff (MIDI note), the resonance (%) */
    int8_t fenv[2];              /* the filter envelope's depth (semitones) */
    uint8_t keytrk;              /* % */
    fk_env_t fe, ae;             /* the filter's and the amplifier's envelopes */
    uint16_t wd[2];              /* the wave envelope's decay (ms; attack 1 ms, sustain 0) */
    int8_t wenv[2];              /* .. its depth on the positions (OSC 2: three quarters of it) */
    uint8_t dirt, dpct;          /* DIRT, how often (%) */
    uint8_t damt[2];             /* its amount (Q8) */
    fk_lfo_t lfo[2];
    uint8_t mode;                /* VM_* */
    uint8_t fixp[2];             /* a drum's pitch (MIDI notes); 0: a tonal sound */
    uint8_t emode, ens[2];       /* the ensemble: its mode and amount (%) */
    uint8_t flav;                /* FLAVOUR, % */
    uint8_t rs8;                 /* % of draws on the coarser 8-tap resampler */
    uint8_t ring[2];             /* RING MOD's amount (Q8): DIRT is RING when no other process is drawn */
    uint8_t wneg;                /* the wave envelope moves OSC 2 the other way */
    uint8_t vvel;                /* the velocity's share of the level, % (0: the common ground's) */
    uint8_t lvl1;                /* OSC 1's level (Q8; 0: 200, 255: none -- a noise sound) */
} fk_row_t;
#define TABS(...) ((const uint8_t[]){__VA_ARGS__, 255})
#define ENV(a0, a1, d0, d1, s0, s1, r0, r1) {{a0, a1}, {d0, d1}, {r0, r1}, {s0, s1}}
#define LFO(sh, dst, fl, a0, a1, h0, h1) {sh, dst, fl, {a0, a1}, {h0, h1}}
#define NOLFO {0, LD_NONE, 0, {0, 0}, {0, 0}}

#define FK_HAND(X)                                                                                             \
    X(EBMBASS, "EBM BASS", 3, 0, 4, 20) X(STAB, "BODY STAB", 3, 1, 4, 15) X(METAL, "METAL HIT", 3, 1, 5, 15)     \
    X(NOISE, "NOISE HIT", 2, 0, 5, 20) X(SUB, "SUB DRONE", 2, 4, 3, 70) X(GRIND, "GRIND", 3, 0, 5, 40)          \
    X(SAWBASS, "SAW BASS", 3, 1, 1, 15) X(PAD, "POLY PAD", 3, 5, 0, 70) X(LEAD, "LEAD", 3, 1, 2, 30)            \
    X(PLUCK, "PLUCK", 3, 2, 1, 20) X(BRASS, "BRASS", 2, 1, 0, 30) X(TENSION, "TENSION", 2, 5, 3, 80)            \
    X(CHOIR, "CHOIR", 2, 5, 0, 70) X(BELLS, "BELLS", 2, 4, 1, 40) X(SWELL, "SWELL", 2, 5, 0, 70)                \
    X(PULSE, "OSTINATO", 2, 2, 2, 20) X(KICK, "KICK", 3, 0, 4, 0) X(SNARE, "SNARE", 3, 0, 4, 0)                 \
    X(CLAP, "CLAP", 2, 0, 3, 0) X(CHAT, "CLOSED HAT", 3, 0, 3, 0) X(OHAT, "OPEN HAT", 2, 0, 2, 0)              \
    X(TOM, "TOM", 2, 1, 2, 0) X(RIM, "RIM", 2, 0, 3, 0) X(CYMBAL, "CYMBAL", 1, 1, 1, 0)                         \
    X(ANVIL, "ANVIL", 3, 0, 5, 10) X(MACHINE, "MACHINE", 3, 1, 5, 30) X(STRINGS, "STRINGS", 3, 5, 0, 60)        \
    X(LOSTR, "LOW STRINGS", 2, 5, 1, 70) X(STRHIT, "STRING HIT", 2, 0, 3, 10) X(PULSEBASS, "PULSE BASS", 3, 0, 3, 20) \
    X(POLYSYN, "POLY SYNTH", 3, 2, 0, 40)

/* ---------------------------------------------------------------- rows --- */
/* (build 4) Tonitor's families brought back as FIBER sounds -- OBRADOR's, HIVE's, FM's, DRUID's, BAYAREA's, ANIMA's
 * kinds by what they were, not how their engines made them -- and further families from synthesizer practice */
#define FK_ROWS(Y)                                                                                             \
    Y(PWMPAD, "PWM PAD", 3, 5, 0, 70, {.om = {OM_VA, OM_VA}, .pos = {34, 50}, .semi2 = {0, 0, 12, 0}, .det2 = 10,  \
        .lvl2 = {150, 200}, .ft = FT_LP12, .cut = {76, 96}, .res = {0, 15}, .fenv = {0, 12}, .keytrk = 50,         \
        .fe = ENV(300, 1500, 1000, 3000, 60, 100, 1000, 3000), .ae = ENV(300, 2000, 1000, 3000, 85, 100, 1200, 4000), \
        .lfo = {LFO(FBL_TRI, LD_POS, 0, 6, 14, 15, 60), LFO(FBL_SINE, LD_PITCH, 0, 1, 2, 400, 550)},                \
        .emode = EM_CHORUS, .ens = {40, 70}, .flav = 40})                                                       \
    Y(KEYS, "KEYS", 3, 3, 1, 20, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(19, 84, 81, 22, 9), .pos = {0, 24},       \
        .semi2 = {0, 12, 0, 0}, .det2 = 6, .lvl2 = {80, 160}, .ft = FT_LP12, .cut = {58, 80}, .res = {0, 20},      \
        .fenv = {18, 40}, .keytrk = 60, .fe = ENV(1, 8, 200, 1200, 0, 20, 150, 600),                             \
        .ae = ENV(1, 8, 600, 2500, 0, 20, 200, 900), .wd = {200, 1200}, .wenv = {4, 16}, .flav = 20})            \
    Y(ORGAN, "ORGAN", 2, 2, 1, 30, {.om = {OM_STR, OM_STR}, .pos = {46, 54}, .semi2 = {12, 0, 12, -12}, .det2 = 3, \
        .lvl2 = {100, 180}, .ft = FT_LP12, .cut = {88, 112}, .res = {0, 10}, .keytrk = 50,                       \
        .fe = ENV(1, 10, 500, 500, 100, 100, 100, 100), .ae = ENV(4, 20, 500, 500, 100, 100, 60, 250),           \
        .lfo = {LFO(FBL_SINE, LD_VOL, LF_GLOB, 4, 10, 550, 700), NOLFO}, .flav = 10})                            \
    Y(SYNCLEAD, "SYNC LEAD", 3, 0, 3, 20, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(56, 57, 58, 59, 60, 28),          \
        .pos = {0, 10}, .semi2 = {0, 12, -12, 0}, .det2 = 8, .lvl2 = {80, 180}, .ft = FT_LP24, .cut = {80, 104},   \
        .res = {5, 25}, .fenv = {0, 18}, .keytrk = 50, .fe = ENV(1, 30, 300, 1200, 30, 70, 200, 600),            \
        .ae = ENV(2, 20, 500, 1500, 70, 100, 150, 600), .wd = {200, 900}, .wenv = {20, 44},                     \
        .lfo = {LFO(FBL_SINE, LD_PITCH, 0, 1, 2, 450, 600), NOLFO}, .mode = VM_LEGATO})                          \
    Y(CLUSTER, "CLUSTER", 2, 4, 2, 60, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(7, 87, 17, 18, 85, 29),             \
        .pos = {0, 40}, .semi2 = {0, 7, 5, 12}, .det2 = 40, .lvl2 = {160, 220}, .ft = FT_LP24, .cut = {66, 90},   \
        .res = {5, 30}, .fenv = {0, 12}, .keytrk = 40, .fe = ENV(500, 2500, 1500, 4000, 50, 100, 1500, 4000),    \
        .ae = ENV(400, 2500, 1500, 4000, 80, 100, 1500, 5000),                                                  \
        .lfo = {LFO(FBL_RANDOM, LD_POS, 0, 4, 16, 10, 50), LFO(FBL_TRI, LD_CUT, LF_GLOB, 3, 10, 5, 30)},          \
        .emode = EM_STRCH, .ens = {60, 90}, .flav = 70})                                                        \
    Y(WASH, "WASH", 1, 5, 1, 80, {.om = {OM_WAVE, OM_STR}, .tab = TABS(11, 16, 26, 75, 74), .pos = {0, 60},        \
        .semi2 = {12, 7, 0, 19}, .det2 = 20, .lvl2 = {60, 140}, .ft = FT_LP12, .cut = {64, 88}, .res = {10, 35},   \
        .keytrk = 30, .fe = ENV(1000, 4000, 2000, 5000, 60, 100, 2000, 5000),                                   \
        .ae = ENV(1500, 5000, 2000, 5000, 90, 100, 2500, 6000), .dirt = DT_STATIC, .dpct = 70, .damt = {20, 70}, \
        .lfo = {LFO(FBL_RANDOM, LD_CUT, LF_GLOB, 6, 18, 5, 25), LFO(FBL_RANDOM, LD_POS, 0, 8, 30, 5, 30)},        \
        .emode = EM_STRCH, .ens = {50, 90}, .flav = 60})                                                        \
    Y(AIR, "AIR", 1, 4, 2, 70, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(11, 16, 26, 27, 80), .pos = {0, 60},          \
        .semi2 = {12, 24, 19, 0}, .det2 = 30, .lvl2 = {40, 120}, .ft = FT_HP12, .cut = {60, 84}, .res = {20, 45},  \
        .fe = ENV(800, 3000, 2000, 4000, 80, 100, 1500, 4000), .ae = ENV(800, 3000, 2000, 4000, 80, 100, 1500, 4500), \
        .dirt = DT_STATIC, .dpct = 100, .damt = {60, 140},                                                     \
        .lfo = {LFO(FBL_RANDOM, LD_DIRT, 0, 20, 50, 10, 60), LFO(FBL_RANDOM, LD_CUT, 0, 4, 12, 8, 40)}})         \
    Y(GROUND, "GROUND", 2, 3, 3, 60, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .pos2 = {0, 20},                  \
        .tab = TABS(0, 81, 14, 85), .semi2 = {-12, 0, -12, 7}, .det2 = 12, .lvl2 = {100, 180}, .ft = FT_LP24,    \
        .ft2 = FT_DRIVE, .ft2pct = 30, .dc = DC_TUBE, .cut = {42, 58}, .res = {10, 40}, .fenv = {0, 10},          \
        .fe = ENV(500, 2000, 2000, 4000, 60, 100, 2000, 4000), .ae = ENV(400, 2000, 2000, 4000, 90, 100, 2000, 5000), \
        .dirt = DT_CRACKLE, .dpct = 40, .damt = {20, 80},                                                       \
        .lfo = {LFO(FBL_RANDOM, LD_CUT, LF_GLOB, 3, 10, 5, 30), NOLFO}})                                        \
    Y(IMPACT, "IMPACT", 3, 0, 5, 0, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(8, 2, 4, 24, 21, 25, 13),             \
        .pos = {0, 40}, .semi2 = {6, 11, 13, 18}, .det2 = 60, .lvl2 = {100, 200}, .ft = FT_DRIVE, .ft2 = FT_LP12,  \
        .ft2pct = 40, .dc = FT_ANY, .cut = {56, 84}, .res = {10, 40}, .fenv = {12, 36},                          \
        .fe = ENV(1, 2, 60, 300, 0, 10, 60, 200), .ae = ENV(1, 2, 200, 900, 0, 0, 150, 600), .wd = {40, 300},     \
        .wenv = {10, 30}, .dirt = DT_CLICK, .dpct = 60, .damt = {80, 200}, .ring = {100, 200}})                  \
    Y(ITERATIF, "ITERATIF", 2, 2, 4, 20, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(4, 2, 83, 86, 30, 24, 8),          \
        .pos = {0, 40}, .semi2 = {12, 7, 19, 0}, .det2 = 30, .lvl2 = {80, 180}, .ft = FT_LP12, .cut = {72, 100},   \
        .res = {5, 30}, .fe = ENV(1, 5, 500, 2000, 20, 60, 500, 1500), .ae = ENV(1, 5, 1000, 3500, 0, 0, 500, 2000), \
        .lfo = {LFO(FBL_PULSE, LD_VOL, LF_SYNC, 40, 70, 500, 1600), LFO(FBL_SH, LD_POS, LF_SYNC, 4, 16, 500, 1600)}})  \
    Y(FRICTION, "FRICTION", 2, 3, 4, 60, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(8, 11, 13, 20, 69),               \
        .pos = {0, 50}, .semi2 = {0, 7, 1, 12}, .det2 = 50, .lvl2 = {80, 180}, .ft = FT_HP12, .ft2 = FT_LP12,      \
        .ft2pct = 50, .cut = {50, 76}, .res = {30, 60}, .fe = ENV(200, 1500, 1000, 3000, 60, 100, 800, 2500),    \
        .ae = ENV(150, 1200, 1000, 3000, 80, 100, 800, 3000), .dirt = DT_CRACKLE, .dpct = 80, .damt = {60, 160}, \
        .lfo = {LFO(FBL_RANDOM, LD_POS, 0, 8, 24, 100, 400), LFO(FBL_RANDOM, LD_DIRT, 0, 20, 50, 50, 300)}})      \
    Y(HOLLOW, "HOLLOW", 2, 4, 2, 60, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(10, 31, 25, 64, 1), .pos = {0, 50},   \
        .semi2 = {0, 12, 5, 7}, .det2 = 10, .lvl2 = {80, 160}, .ft = FT_HP12, .cut = {44, 64}, .res = {40, 70},    \
        .fe = ENV(100, 800, 1000, 3000, 60, 100, 800, 2500), .ae = ENV(100, 1200, 1000, 3000, 70, 100, 900, 3000), \
        .lfo = {LFO(FBL_TRI, LD_POS, 0, 4, 12, 10, 60), NOLFO}, .flav = 40})                                     \
    Y(SHARDS, "SHARDS", 2, 2, 4, 10, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(30, 12, 83, 21, 9), .pos = {10, 60},   \
        .semi2 = {12, 24, 19, 31}, .det2 = 80, .lvl2 = {60, 160}, .ft = FT_HP12, .cut = {64, 90}, .res = {10, 40}, \
        .fe = ENV(1, 2, 100, 500, 0, 0, 100, 400), .ae = ENV(1, 2, 150, 800, 0, 0, 100, 500), .wd = {30, 200},    \
        .wenv = {-30, 30}, .dirt = DT_RING, .dpct = 50, .ring = {60, 160}})                                      \
    Y(SPECTRE, "SPECTRE", 1, 5, 2, 90, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(74, 75, 9, 12, 24), .pos = {0, 30},  \
        .semi2 = {12, 19, 7, 24}, .det2 = 16, .lvl2 = {80, 180}, .ft = FT_LP12, .cut = {80, 104}, .res = {0, 20},  \
        .fe = ENV(1000, 4000, 2000, 5000, 80, 100, 2000, 5000), .ae = ENV(1500, 5000, 2000, 5000, 90, 100, 2500, 6000), \
        .lfo = {LFO(FBL_TRI, LD_POS, 0, 20, 30, 3, 15), LFO(FBL_RANDOM, LD_POS1, 0, 4, 12, 10, 40)},               \
        .emode = EM_CHORUS, .ens = {0, 50}, .flav = 60})                                                        \
    Y(ABYSS, "ABYSS", 1, 4, 3, 70, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(81, 0, 1, 14, 85), .pos = {0, 30},       \
        .semi2 = {-12, -12, 0, 7}, .det2 = 14, .lvl2 = {120, 200}, .ft = FT_LP24, .ft2 = FT_DRIVE, .ft2pct = 40,   \
        .dc = DC_TUBE, .cut = {36, 50}, .res = {25, 55}, .fenv = {0, 8},                                         \
        .fe = ENV(1000, 3000, 3000, 6000, 70, 100, 3000, 6000), .ae = ENV(800, 2500, 3000, 6000, 90, 100, 3000, 7000), \
        .lfo = {LFO(FBL_RANDOM, LD_CUT, LF_GLOB, 4, 12, 3, 15), LFO(FBL_RANDOM, LD_POS, 0, 6, 20, 3, 15)}})        \
    Y(STATICDRN, "STATIC", 1, 3, 4, 70, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(71, 72, 73, 8), .pos = {0, 60},     \
        .semi2 = {0, 7, 12, -12}, .det2 = 40, .lvl2 = {20, 80}, .ft = FT_LP12, .ft2 = FT_HP12, .ft2pct = 40,       \
        .cut = {60, 90}, .res = {20, 50}, .fe = ENV(500, 3000, 2000, 4000, 80, 100, 1500, 4000),                \
        .ae = ENV(500, 3000, 2000, 4000, 90, 100, 1500, 5000), .dirt = DT_STATIC, .dpct = 100, .damt = {120, 230}, \
        .lfo = {LFO(FBL_RANDOM, LD_CUT, 0, 6, 20, 10, 60), LFO(FBL_SH, LD_DIRT, 0, 10, 40, 100, 600)}, .rs8 = 50})  \
    Y(GLITCH, "GLITCH", 2, 0, 5, 20, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(71, 72, 73, 20, 13, 67, 70),          \
        .pos = {0, 60}, .semi2 = {0, 12, 7, -12}, .det2 = 20, .lvl2 = {80, 200}, .ft = FT_LP24, .ft2 = FT_DRIVE,   \
        .ft2pct = 50, .dc = FT_ANY, .cut = {60, 92}, .res = {20, 55}, .fe = ENV(1, 10, 200, 800, 30, 80, 100, 400), \
        .ae = ENV(1, 10, 300, 1200, 60, 100, 100, 400), .dirt = DT_BURST1, .dpct = 60, .damt = {60, 180},        \
        .lfo = {LFO(FBL_SH, LD_POS, 0, 16, 40, 600, 2000), LFO(FBL_SH, LD_CUT, LF_SYNC, 6, 24, 600, 2000)}, .rs8 = 70}) \
    Y(CREEP, "CREEP", 1, 5, 2, 80, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(82, 10, 25, 20, 64, 11), .pos = {0, 10},  \
        .semi2 = {0, 1, 6, 11}, .det2 = 12, .lvl2 = {100, 180}, .ft = FT_LP12, .cut = {56, 80}, .res = {20, 50},   \
        .fe = ENV(2000, 6000, 3000, 6000, 80, 100, 2000, 6000), .ae = ENV(2000, 6000, 3000, 6000, 90, 100, 2500, 7000), \
        .wd = {3000, 8000}, .wenv = {20, 50}, .lfo = {LFO(FBL_RANDOM, LD_POS1, 0, 4, 10, 5, 20), NOLFO}})         \
    Y(PLATE, "PLATE", 2, 3, 3, 30, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(4, 83, 24, 21, 8), .pos = {0, 50},       \
        .semi2 = {6, 11, 17, 23}, .det2 = 90, .lvl2 = {100, 200}, .ft = FT_LP12, .cut = {84, 108}, .res = {0, 20},  \
        .fe = ENV(1, 2, 800, 3000, 0, 20, 800, 3000), .ae = ENV(1, 3, 1500, 5000, 0, 0, 1000, 4000),             \
        .wd = {500, 3000}, .wenv = {6, 20}, .dirt = DT_RING, .dpct = 70, .ring = {80, 200}})                     \
    Y(CHIME, "CHIME", 2, 4, 1, 30, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(30, 83, 86, 84, 4), .pos = {0, 40},       \
        .semi2 = {12, 19, 24, 7}, .det2 = 8, .lvl2 = {60, 160}, .ft = FT_LP12, .cut = {90, 112}, .res = {0, 15},    \
        .fe = ENV(1, 2, 600, 2000, 0, 20, 600, 2000), .ae = ENV(1, 3, 1000, 3000, 0, 0, 800, 2500),              \
        .wd = {300, 1500}, .wenv = {4, 14}, .emode = EM_CHORUS, .ens = {0, 40}})                                 \
    Y(FOLDED, "FOLDED", 2, 1, 4, 30, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .tab = TABS(61), .semi2 = {0, 12, 7, -12}, \
        .det2 = 10, .lvl2 = {60, 160}, .ft = FT_DRIVE, .dc = DC_CRUNCH, .cut = {44, 60}, .fenv = {16, 40},        \
        .fe = ENV(1, 30, 200, 1200, 10, 50, 200, 800), .ae = ENV(1, 20, 400, 1500, 30, 90, 200, 900),            \
        .lfo = {LFO(FBL_TRI, LD_CUT, 0, 2, 8, 20, 200), NOLFO}})                                                 \
    Y(BONGO, "BONGO", 2, 1, 2, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .semi2 = {7, 5, 12, 7}, .lvl2 = {40, 110}, \
        .ft = FT_LP12, .cut = {62, 80}, .res = {10, 35}, .fenv = {12, 30}, .fe = ENV(1, 1, 20, 60, 0, 0, 20, 40), \
        .ae = ENV(1, 1, 80, 250, 0, 0, 40, 120), .dirt = DT_CLICK, .dpct = 80, .damt = {40, 120},                \
        .mode = VM_MONO, .fixp = {57, 72}})                                                                     \
    Y(BUZZPLUCK, "BUZZ PLUCK", 2, 1, 3, 10, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(13, 21, 23, 68, 15), .pos = {0, 40}, \
        .semi2 = {0, 12, 0, 7}, .det2 = 10, .lvl2 = {80, 180}, .ft = FT_DRIVE, .ft2 = FT_LP12, .ft2pct = 40,       \
        .dc = DC_DIODE, .cut = {56, 76}, .res = {10, 30}, .fenv = {18, 36}, .fe = ENV(1, 2, 150, 600, 0, 10, 100, 300), \
        .ae = ENV(1, 2, 300, 1500, 0, 10, 150, 500), .wd = {100, 600}, .wenv = {8, 24}})                         \
    Y(BREATH, "BREATH", 1, 5, 1, 70, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(16, 26, 27, 80), .pos = {0, 50},       \
        .semi2 = {0, 12, 0, 7}, .det2 = 14, .lvl2 = {80, 160}, .ft = FT_LP12, .cut = {70, 92}, .res = {5, 25},     \
        .fe = ENV(300, 1500, 1000, 3000, 70, 100, 800, 2500), .ae = ENV(300, 1500, 1000, 3000, 80, 100, 800, 3000), \
        .dirt = DT_STATIC, .dpct = 100, .damt = {30, 90},                                                       \
        .lfo = {LFO(FBL_RANDOM, LD_POS, 0, 6, 18, 20, 80), LFO(FBL_RANDOM, LD_DIRT, 0, 10, 30, 30, 120)}, .flav = 50})  \
    Y(BITTONE, "BIT TONE", 2, 0, 4, 20, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(71, 72, 73, 13, 20), .pos = {0, 60},  \
        .semi2 = {0, 12, -12, 7}, .det2 = 6, .lvl2 = {80, 180}, .ft = FT_LP24, .ft2 = FT_OFF, .ft2pct = 40,         \
        .cut = {70, 100}, .res = {10, 40}, .fe = ENV(1, 10, 200, 800, 40, 90, 100, 400),                         \
        .ae = ENV(1, 10, 300, 1200, 60, 100, 100, 500), .dirt = DT_CRACKLE, .dpct = 40, .damt = {30, 100},        \
        .lfo = {LFO(FBL_SH, LD_POS, LF_GLOB, 6, 24, 200, 1200), NOLFO}, .rs8 = 100})                             \
    Y(FOG, "FOG", 1, 5, 0, 80, {.om = {OM_WAVE, OM_STR}, .tab = TABS(7, 17, 87, 76, 77), .pos = {0, 40},            \
        .pos2 = {20, 40}, .semi2 = {0, 12, 0, -12}, .det2 = 16, .lvl2 = {80, 160}, .ft = FT_LP12, .cut = {54, 76},  \
        .res = {5, 25}, .fe = ENV(1500, 5000, 2000, 5000, 80, 100, 2000, 5000),                                 \
        .ae = ENV(1500, 5000, 2000, 5000, 90, 100, 3000, 7000), .lfo = {LFO(FBL_RANDOM, LD_CUT, LF_GLOB, 4, 12, 5, 25), \
        LFO(FBL_TRI, LD_POS, 0, 4, 14, 5, 25)}, .emode = EM_STRINGS, .ens = {50, 90}, .flav = 70})               \
    Y(GLASS, "GLASS", 2, 4, 1, 40, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(30, 12, 9, 83), .pos = {20, 60},          \
        .semi2 = {12, 24, 19, 0}, .det2 = 10, .lvl2 = {60, 140}, .ft = FT_HP12, .cut = {58, 78}, .res = {10, 35},   \
        .fe = ENV(100, 800, 1000, 3000, 70, 100, 800, 2500), .ae = ENV(50, 600, 1000, 3000, 60, 100, 800, 2500),  \
        .lfo = {LFO(FBL_TRI, LD_POS, 0, 4, 12, 20, 80), NOLFO}, .emode = EM_CHORUS, .ens = {20, 60}})            \
    Y(DRONE, "DRONE", 1, 5, 2, 80, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(6, 7, 14, 85, 0), .pos = {0, 40},        \
        .semi2 = {7, 12, 7, 19}, .det2 = 10, .lvl2 = {140, 220}, .ft = FT_LP24, .cut = {64, 86}, .res = {15, 45},   \
        .fe = ENV(1500, 5000, 2000, 6000, 80, 100, 2000, 6000), .ae = ENV(1500, 5000, 2000, 6000, 100, 100, 2500, 6000), \
        .lfo = {LFO(FBL_RANDOM, LD_CUT, LF_GLOB, 6, 16, 5, 30), LFO(FBL_TRI, LD_POS, 0, 4, 14, 3, 20)}, .flav = 60}) \
    /* EBM and electro-industrial (build 4, from the research list: [D] documented, [A] adapted, [E] extrapolated) */ \
    Y(OFFBEATBS, "OFFBEAT BS", 3, 0, 4, 10, {.om = {OM_VA, OM_VA}, .pos = {0, 4}, .pos2 = {16, 24}, .semi2 = {-12, -12, -12, 0}, \
        .det2 = 8, .lvl2 = {120, 200}, .ft = FT_LP24, .cut = {38, 54}, .res = {30, 45}, .fenv = {30, 48}, .keytrk = 60, \
        .fe = ENV(1, 1, 90, 160, 0, 0, 60, 100), .ae = ENV(1, 1, 120, 200, 0, 0, 30, 60), .mode = VM_MONO, .vvel = 50}) \
    Y(LAYERBS, "LAYER BS", 3, 0, 3, 10, {.om = {OM_VA, OM_WAVE}, .tab = TABS(84, 19), .pos = {0, 4}, .pos2 = {6, 14}, \
        .semi2 = {0, 0, 12, 0}, .det2 = 6, .lvl2 = {100, 180}, .ft = FT_LP12, .cut = {48, 64}, .res = {15, 30}, \
        .fenv = {18, 36}, .keytrk = 60, .fe = ENV(1, 1, 150, 260, 5, 15, 80, 120), .ae = ENV(1, 1, 200, 320, 50, 70, 50, 80), \
        .ring = {50, 90}, .mode = VM_MONO}) \
    Y(RINGBASS, "RING BASS", 2, 0, 4, 10, {.om = {OM_VA, OM_WAVE}, .tab = TABS(20), .pos = {0, 4}, .pos2 = {14, 26}, \
        .semi2 = {7, 7, 5, 7}, .det2 = 4, .lvl2 = {100, 180}, .ft = FT_DRIVE, .ft2 = FT_LP24, .ft2pct = 40, .dc = DC_CRUNCH, \
        .cut = {40, 56}, .res = {10, 30}, .fenv = {12, 30}, .fe = ENV(1, 1, 140, 220, 20, 50, 50, 80), \
        .ae = ENV(1, 1, 150, 220, 30, 50, 50, 80), .ring = {140, 200}, .mode = VM_MONO, .lvl1 = 180}) \
    Y(RESOBASS, "RESO BASS", 3, 0, 4, 10, {.om = {OM_VA, OM_VA}, .pos = {26, 34}, .semi2 = {0, -12, 0, 0}, .det2 = 6, \
        .lvl2 = {0, 80}, .ft = FT_LP24, .cut = {34, 48}, .res = {68, 78}, .fenv = {36, 54}, .keytrk = 100, \
        .fe = ENV(1, 1, 70, 120, 0, 0, 50, 80), .ae = ENV(1, 2, 200, 400, 40, 70, 40, 80), .dirt = DT_CRACKLE, .dpct = 60, \
        .damt = {20, 40}, .mode = VM_LEGATO}) \
    Y(SYNCBASS, "SYNC BASS", 3, 0, 4, 10, {.om = {OM_WAVE, OM_VA}, .tab = TABS(57, 56, 58), .pos = {2, 8}, .pos2 = {0, 2}, \
        .semi2 = {-12, -12, -12, 0}, .det2 = 6, .lvl2 = {100, 140}, .ft = FT_LP24, .cut = {50, 66}, .res = {10, 25}, \
        .fenv = {6, 18}, .keytrk = 60, .fe = ENV(1, 1, 150, 300, 30, 50, 60, 120), .ae = ENV(1, 1, 200, 400, 50, 80, 60, 120), \
        .wd = {180, 280}, .wenv = {28, 38}, .mode = VM_MONO}) \
    Y(PWMBASS, "PWM BASS", 3, 2, 2, 20, {.om = {OM_VA, OM_VA}, .pos = {28, 34}, .semi2 = {-12, -12, 0, -12}, .det2 = 6, \
        .lvl2 = {120, 200}, .ft = FT_LP24, .cut = {42, 56}, .res = {5, 20}, .fenv = {12, 24}, .keytrk = 60, \
        .fe = ENV(1, 1, 250, 400, 20, 40, 80, 140), .ae = ENV(1, 1, 300, 600, 60, 90, 80, 150), \
        .lfo = {LFO(FBL_TRI, LD_POS1, 0, 16, 26, 30, 50), LFO(FBL_TRI, LD_POS, 0, 8, 16, 45, 65)}, .emode = EM_CHORUS, \
        .ens = {40, 70}}) \
    Y(FUZZRIFF, "FUZZ RIFF", 2, 0, 5, 10, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(69, 70, 67), .pos = {24, 36}, \
        .pos2 = {6, 14}, .semi2 = {12, 12, 0, 12}, .det2 = 8, .lvl2 = {80, 180}, .ft = FT_DRIVE, .dc = DC_TUBE, \
        .cut = {56, 72}, .fenv = {6, 18}, .fe = ENV(1, 1, 100, 200, 40, 70, 40, 80), .ae = ENV(1, 1, 100, 160, 40, 60, 40, 70), \
        .mode = VM_MONO, .vvel = 70}) \
    Y(VOXSTAB, "VOX STAB", 3, 2, 3, 15, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(80, 26, 27), .pos = {6, 14}, .pos2 = {20, 30}, \
        .semi2 = {7, 7, 12, 0}, .det2 = 8, .lvl2 = {120, 200}, .ft = FT_LP12, .cut = {80, 100}, .res = {5, 20}, \
        .fe = ENV(1, 2, 300, 600, 30, 60, 120, 200), .ae = ENV(1, 2, 250, 350, 0, 0, 100, 150), .wd = {200, 320}, \
        .wenv = {12, 24}}) \
    Y(ROBOTLEAD, "ROBOT LEAD", 2, 0, 4, 20, {.om = {OM_WAVE, OM_VA}, .tab = TABS(20), .pos = {10, 30}, .pos2 = {30, 34}, \
        .semi2 = {12, 12, 12, 0}, .det2 = 6, .lvl2 = {60, 90}, .ft = FT_LP12, .cut = {72, 92}, .res = {35, 45}, \
        .fe = ENV(1, 5, 300, 600, 60, 90, 100, 200), .ae = ENV(2, 10, 400, 800, 80, 100, 100, 250), \
        .lfo = {LFO(FBL_SH, LD_POS1, 0, 16, 24, 700, 900), NOLFO}, .mode = VM_MONO}) \
    Y(DIGISTAB, "DIGI STAB", 3, 1, 4, 10, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(13, 23), .pos = {26, 34}, .pos2 = {46, 54}, \
        .semi2 = {5, 5, 7, 12}, .det2 = 8, .lvl2 = {140, 200}, .ft = FT_LP12, .cut = {78, 98}, .res = {5, 20}, \
        .fe = ENV(1, 2, 200, 400, 20, 40, 80, 150), .ae = ENV(1, 2, 180, 250, 0, 0, 60, 100), .wd = {150, 300}, \
        .wenv = {14, 24}, .wneg = 1}) \
    Y(SHRIEK, "SHRIEK", 1, 2, 4, 50, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(12), .pos = {44, 56}, .semi2 = {0, 0, 0, 0}, \
        .lvl2 = {0, 0}, .ft = FT_HP12, .cut = {60, 84}, .res = {82, 90}, .keytrk = 100, \
        .fe = ENV(200, 400, 1000, 1000, 100, 100, 600, 1000), .ae = ENV(200, 400, 1000, 1000, 100, 100, 600, 1000), \
        .dirt = DT_STATIC, .dpct = 100, .damt = {12, 30}, .lfo = {LFO(FBL_RANDOM, LD_CUT, 0, 2, 5, 20, 80), NOLFO}}) \
    Y(GATECHORD, "GATE CHORD", 3, 1, 3, 10, {.om = {OM_VA, OM_VA}, .pos = {0, 4}, .semi2 = {7, 7, 12, 5}, .det2 = 8, \
        .lvl2 = {140, 200}, .ft = FT_LP24, .cut = {62, 80}, .res = {10, 25}, .fe = ENV(1, 5, 500, 500, 100, 100, 50, 80), \
        .ae = ENV(3, 8, 500, 500, 100, 100, 40, 70), .lfo = {LFO(FBL_PULSE, LD_VOL, LF_SYNC, 60, 80, 600, 800), NOLFO}}) \
    Y(FIFTHBASS, "FIFTH BASS", 3, 0, 4, 10, {.om = {OM_VA, OM_VA}, .pos = {0, 4}, .semi2 = {7, 7, 7, 12}, .det2 = 6, \
        .lvl2 = {140, 200}, .ft = FT_DRIVE, .ft2 = FT_LP24, .ft2pct = 50, .dc = DC_PNP, .cut = {40, 56}, .res = {10, 30}, \
        .fenv = {8, 20}, .fe = ENV(1, 1, 300, 500, 50, 80, 60, 100), .ae = ENV(1, 1, 350, 450, 60, 80, 60, 100), \
        .ring = {30, 50}, .mode = VM_MONO}) \
    Y(METALLEAD, "METAL LEAD", 2, 1, 3, 30, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(28, 30), .pos = {16, 24}, .pos2 = {30, 40}, \
        .semi2 = {19, 19, 12, 19}, .det2 = 10, .lvl2 = {100, 180}, .ft = FT_LP24, .cut = {70, 90}, .res = {40, 55}, \
        .fenv = {12, 30}, .fe = ENV(1, 2, 400, 600, 30, 50, 250, 350), .ae = ENV(2, 10, 500, 800, 80, 100, 200, 400), \
        .ring = {50, 80}, .lfo = {LFO(FBL_SINE, LD_PITCH, 0, 1, 2, 480, 520), NOLFO}, .mode = VM_MONO}) \
    /* synthwave and darkwave */ \
    Y(JUNOBASS, "JUNO BASS", 3, 1, 1, 10, {.om = {OM_VA, OM_VA}, .pos = {0, 2}, .pos2 = {32, 32}, .semi2 = {-12, -12, -12, -12}, \
        .lvl2 = {150, 180}, .ft = FT_LP24, .cut = {40, 50}, .res = {5, 20}, .fenv = {18, 30}, .keytrk = 60, \
        .fe = ENV(1, 1, 600, 800, 0, 0, 600, 800), .ae = ENV(1, 1, 500, 500, 100, 100, 250, 350), .mode = VM_MONO, .vvel = 25}) \
    Y(OCTBASS, "OCT BASS", 3, 1, 1, 10, {.om = {OM_VA, OM_VA}, .pos = {0, 2}, .semi2 = {12, 12, 12, 12}, .det2 = 12, \
        .lvl2 = {120, 180}, .ft = FT_LP24, .cut = {50, 62}, .res = {5, 20}, .fenv = {12, 24}, .keytrk = 60, \
        .fe = ENV(1, 1, 130, 200, 15, 25, 60, 100), .ae = ENV(1, 1, 180, 240, 50, 70, 50, 80), .mode = VM_MONO}) \
    Y(ARPPLUCK, "ARP PLUCK", 3, 2, 1, 10, {.om = {OM_VA, OM_VA}, .pos = {0, 2}, .pos2 = {28, 32}, .semi2 = {12, 12, 12, 0}, \
        .det2 = 8, .lvl2 = {100, 160}, .ft = FT_LP24, .cut = {52, 64}, .res = {25, 35}, .fenv = {30, 42}, .keytrk = 60, \
        .fe = ENV(1, 1, 150, 220, 0, 0, 120, 180), .ae = ENV(1, 1, 220, 300, 0, 0, 160, 240), .emode = EM_CHORUS, \
        .ens = {30, 60}}) \
    Y(CHORUSPAD, "CHORUS PAD", 3, 4, 0, 60, {.om = {OM_VA, OM_VA}, .pos = {0, 2}, .semi2 = {0, 0, 12, 0}, .det2 = 20, \
        .lvl2 = {160, 200}, .ft = FT_LP24, .cut = {62, 72}, .res = {5, 15}, .fenv = {4, 10}, \
        .fe = ENV(700, 1100, 1300, 1800, 50, 70, 1600, 2400), .ae = ENV(700, 1100, 500, 500, 100, 100, 2000, 3000), \
        .emode = EM_CHORUS, .ens = {60, 90}, .flav = 40}) \
    Y(CSBRASS, "CS BRASS", 3, 3, 0, 30, {.om = {OM_VA, OM_VA}, .pos = {0, 2}, .pos2 = {12, 18}, .semi2 = {0, 0, 0, 12}, \
        .det2 = 24, .lvl2 = {150, 200}, .ft = FT_LP24, .cut = {50, 60}, .res = {5, 20}, .fenv = {24, 36}, \
        .fe = ENV(100, 150, 500, 700, 50, 70, 400, 600), .ae = ENV(50, 80, 500, 500, 100, 100, 600, 900), \
        .emode = EM_CHORUS, .ens = {40, 70}, .vvel = 40}) \
    Y(VPSTRINGS, "VP STRINGS", 2, 5, 0, 60, {.om = {OM_STR, OM_STR}, .pos = {0, 6}, .pos2 = {10, 16}, .semi2 = {-12, -12, 0, -12}, \
        .det2 = 8, .lvl2 = {120, 180}, .ft = FT_LP12, .cut = {92, 110}, .res = {0, 10}, \
        .fe = ENV(300, 500, 500, 500, 100, 100, 1200, 1800), .ae = ENV(300, 500, 500, 500, 100, 100, 1200, 1800), \
        .emode = EM_STRCH, .ens = {60, 90}, .flav = 40}) \
    Y(HIGHSTRING, "HIGH STRING", 2, 4, 0, 60, {.om = {OM_VA, OM_VA}, .pos = {0, 2}, .semi2 = {0, 0, 0, 0}, .det2 = 14, \
        .lvl2 = {160, 200}, .ft = FT_LP24, .cut = {76, 90}, .res = {0, 10}, \
        .fe = ENV(400, 600, 500, 500, 100, 100, 1500, 2000), .ae = ENV(400, 600, 500, 500, 100, 100, 1500, 2100), \
        .lfo = {LFO(FBL_SINE, LD_PITCH2, 0, 2, 3, 520, 580), NOLFO}, .emode = EM_CHORUS, .ens = {50, 80}}) \
    Y(EPDARK, "EP DARK", 2, 4, 1, 20, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(19, 84), .pos = {0, 4}, .pos2 = {4, 8}, \
        .semi2 = {12, 12, 12, 0}, .det2 = 6, .lvl2 = {50, 80}, .ft = FT_LP12, .cut = {70, 82}, .res = {0, 15}, \
        .fe = ENV(1, 2, 1500, 2000, 0, 20, 700, 900), .ae = ENV(1, 2, 1800, 2400, 0, 0, 700, 900), .wd = {700, 1100}, \
        .wenv = {6, 14}, .emode = EM_CHORUS, .ens = {40, 70}}) \
    Y(GOTHORGAN, "GOTH ORGAN", 2, 3, 1, 30, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(85, 22), .pos = {16, 24}, .pos2 = {0, 4}, \
        .semi2 = {12, 12, 12, 12}, .det2 = 4, .lvl2 = {70, 100}, .ft = FT_OFF, .cut = {100, 100}, \
        .fe = ENV(1, 1, 500, 500, 100, 100, 100, 100), .ae = ENV(4, 8, 500, 500, 100, 100, 120, 180), \
        .lfo = {LFO(FBL_TRI, LD_LVL2, LF_GLOB, 10, 15, 580, 620), NOLFO}, .emode = EM_CHORUS, .ens = {40, 70}}) \
    Y(GLASSKEYS, "GLASS KEYS", 2, 4, 1, 20, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(30, 86), .pos = {6, 14}, .pos2 = {16, 24}, \
        .semi2 = {19, 19, 12, 19}, .det2 = 6, .lvl2 = {40, 60}, .ft = FT_LP12, .cut = {82, 94}, .res = {0, 15}, \
        .fe = ENV(1, 2, 1200, 1600, 0, 20, 1000, 1400), .ae = ENV(1, 2, 1300, 1700, 5, 15, 1000, 1400)}) \
    /* dark ambient and film */ \
    Y(GLASSDRONE, "GLASS DRONE", 1, 5, 1, 90, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(30), .pos = {10, 40}, .semi2 = {12, 12, 12, 7}, \
        .det2 = 8, .lvl2 = {120, 180}, .ft = FT_LP24, .cut = {60, 70}, .res = {5, 20}, \
        .fe = ENV(2500, 3500, 500, 500, 100, 100, 5000, 7000), .ae = ENV(2500, 3500, 500, 500, 100, 100, 5000, 7000), \
        .lfo = {LFO(FBL_RANDOM, LD_POS1, 0, 12, 16, 4, 6), LFO(FBL_RANDOM, LD_POS, 0, 6, 10, 6, 8)}, .emode = EM_CHORUS, \
        .ens = {40, 80}}) \
    Y(RINGDRONE, "RING DRONE", 1, 5, 3, 80, {.om = {OM_VA, OM_WAVE}, .tab = TABS(4), .pos = {0, 2}, .pos2 = {26, 34}, \
        .semi2 = {6, 6, 6, 6}, .det2 = 6, .lvl2 = {120, 180}, .ft = FT_LP24, .cut = {48, 56}, .res = {45, 55}, \
        .fe = ENV(3500, 4500, 500, 500, 100, 100, 4500, 5500), .ae = ENV(3500, 4500, 500, 500, 100, 100, 4500, 5500), \
        .ring = {160, 200}, .lfo = {LFO(FBL_SINE, LD_CUT, LF_GLOB, 4, 8, 8, 12), NOLFO}}) \
    Y(DUSTPAD, "DUST PAD", 1, 5, 1, 80, {.om = {OM_STR, OM_WAVE}, .tab = TABS(87), .pos = {58, 63}, .pos2 = {16, 24}, \
        .semi2 = {-12, -12, -12, 0}, .det2 = 8, .lvl2 = {100, 160}, .ft = FT_LP12, .cut = {56, 66}, .res = {5, 15}, \
        .fe = ENV(2000, 3000, 500, 500, 100, 100, 3500, 4500), .ae = ENV(2000, 3000, 500, 500, 100, 100, 3500, 4500), \
        .dirt = DT_CRACKLE, .dpct = 100, .damt = {40, 60}, .emode = EM_CHORUS, .ens = {50, 80}}) \
    Y(WIND, "WIND", 1, 5, 2, 70, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .lvl2 = {0, 0}, .lvl1 = 255, .ft = FT_LP12, \
        .cut = {56, 80}, .res = {55, 65}, .keytrk = 100, .fe = ENV(1200, 1800, 500, 500, 100, 100, 2500, 3500), \
        .ae = ENV(1200, 1800, 500, 500, 100, 100, 2500, 3500), .dirt = DT_STATIC, .dpct = 100, .damt = {200, 255}, \
        .lfo = {LFO(FBL_RANDOM, LD_CUT, 0, 6, 12, 15, 25), NOLFO}}) \
    Y(CLOCKTICK, "CLOCK TICK", 1, 3, 3, 0, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(18), .pos = {0, 2}, .lvl2 = {0, 0}, .lvl1 = 30, \
        .ft = FT_HP24, .cut = {84, 100}, .res = {10, 30}, .ae = ENV(1, 1, 500, 500, 100, 100, 100, 200), \
        .dirt = DT_GEIGER, .dpct = 100, .damt = {120, 220}, .lfo = {LFO(FBL_PULSE, LD_VOL, LF_GLOB, 60, 80, 180, 220), NOLFO}, \
        .fixp = {72, 84}}) \
    Y(MINORSEC, "MINOR SEC", 1, 5, 2, 70, {.om = {OM_VA, OM_VA}, .pos = {0, 2}, .semi2 = {1, 1, 1, 13}, .det2 = 6, \
        .lvl2 = {150, 200}, .ft = FT_LP24, .cut = {44, 54}, .res = {10, 25}, .fe = ENV(1800, 2200, 500, 500, 100, 100, 2500, 3500), \
        .ae = ENV(1800, 2200, 500, 500, 100, 100, 2500, 3500), .emode = EM_STRCH, .ens = {50, 80}}) \
    Y(HAUNTVOX, "HAUNT VOX", 1, 5, 1, 80, {.om = {OM_WAVE, OM_STR}, .tab = TABS(27, 26), .pos = {10, 40}, .pos2 = {58, 63}, \
        .semi2 = {12, 12, 12, 0}, .det2 = 8, .lvl2 = {60, 90}, .ft = FT_LP12, .cut = {64, 76}, .res = {5, 20}, \
        .fe = ENV(1600, 2000, 500, 500, 100, 100, 3000, 4000), .ae = ENV(1600, 2000, 500, 500, 100, 100, 3000, 4000), \
        .lfo = {LFO(FBL_TRI, LD_POS1, 0, 20, 30, 12, 18), NOLFO}, .flav = 50}) \
    Y(BOWEDLOW, "BOWED LOW", 1, 5, 2, 70, {.om = {OM_STR, OM_WAVE}, .tab = TABS(16), .pos = {24, 30}, .pos2 = {12, 18}, \
        .semi2 = {-12, -12, -12, -12}, .det2 = 8, .lvl2 = {80, 140}, .ft = FT_LP24, .cut = {44, 54}, .res = {10, 25}, \
        .fenv = {12, 24}, .fe = ENV(700, 900, 1800, 2200, 30, 50, 1800, 2200), .ae = ENV(500, 700, 500, 500, 100, 100, 2000, 3000), \
        .dirt = DT_STATIC, .dpct = 100, .damt = {10, 16}, .mode = VM_MONO}) \
    Y(METALDRONE, "METAL DRONE", 1, 4, 4, 80, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(83, 77), .pos = {36, 44}, .pos2 = {16, 24}, \
        .semi2 = {11, 11, 11, 11}, .det2 = 44, .lvl2 = {120, 180}, .ft = FT_HP12, .cut = {44, 54}, .res = {20, 35}, \
        .fe = ENV(2500, 3500, 500, 500, 100, 100, 5000, 7000), .ae = ENV(2500, 3500, 500, 500, 100, 100, 5000, 7000), \
        .ring = {100, 160}}) \
    Y(SCANBED, "SCAN BED", 1, 5, 1, 80, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(5, 3), .pos = {0, 4}, .pos2 = {56, 60}, \
        .semi2 = {0, 12, 0, 7}, .det2 = 10, .lvl2 = {140, 200}, .ft = FT_LP24, .cut = {70, 82}, .res = {5, 20}, \
        .fe = ENV(1800, 2200, 500, 500, 100, 100, 3500, 4500), .ae = ENV(1800, 2200, 500, 500, 100, 100, 3500, 4500), \
        .lfo = {LFO(FBL_SAWUP, LD_POS1, LF_GLOB, 26, 30, 2, 4), NOLFO}}) \
    /* percussion (no pitch envelope: the punch from the click, the filter and wave envelopes, the drive) */ \
    Y(KICK909, "KICK 909", 3, 0, 4, 0, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(18), .pos = {61, 61}, .pos2 = {0, 2}, \
        .semi2 = {24, 24, 24, 24}, .lvl2 = {70, 90}, .ft = FT_LP24, .cut = {40, 48}, .res = {0, 15}, .fenv = {44, 56}, \
        .fe = ENV(1, 1, 30, 50, 0, 0, 20, 30), .ae = ENV(1, 1, 300, 400, 0, 0, 50, 80), .wd = {20, 30}, .wenv = {0, 0}, \
        .dirt = DT_CLICK, .dpct = 100, .damt = {120, 200}, .fixp = {33, 38}, .vvel = 60}) \
    Y(KICKBOOM, "KICK BOOM", 3, 1, 3, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .semi2 = {-12, -12, -12, -12}, \
        .lvl2 = {60, 120}, .ft = FT_LP24, .cut = {30, 40}, .res = {15, 25}, .fenv = {12, 24}, \
        .fe = ENV(1, 1, 40, 80, 0, 0, 30, 60), .ae = ENV(1, 1, 1000, 1400, 0, 0, 150, 250), .dirt = DT_CLICK, .dpct = 100, \
        .damt = {30, 70}, .fixp = {29, 35}, .vvel = 50}) \
    Y(KICKDIST, "KICK DIST", 3, 0, 5, 0, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(18), .pos = {61, 61}, .pos2 = {0, 2}, \
        .semi2 = {24, 24, 12, 24}, .lvl2 = {60, 100}, .ft = FT_DRIVE, .dc = DC_CRUNCH, .cut = {66, 80}, .fenv = {-24, -12}, \
        .fe = ENV(1, 1, 60, 120, 0, 0, 30, 60), .ae = ENV(1, 1, 250, 320, 0, 0, 40, 80), .dirt = DT_CLICK, .dpct = 100, \
        .damt = {160, 240}, .fixp = {33, 38}}) \
    Y(KICKRUMBL, "KICK RUMBL", 2, 1, 5, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .semi2 = {-12, -12, -12, 0}, \
        .lvl2 = {80, 140}, .ft = FT_DRIVE, .dc = DC_TUBE, .cut = {58, 68}, .fenv = {-18, -8}, \
        .fe = ENV(1, 1, 300, 600, 0, 0, 200, 300), .ae = ENV(1, 1, 800, 1000, 0, 0, 150, 250), .dirt = DT_CRACKLE, .dpct = 100, \
        .damt = {30, 50}, .fixp = {29, 35}}) \
    Y(SNARE909, "SNARE 909", 3, 0, 4, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .semi2 = {10, 10, 9, 11}, .lvl2 = {120, 180}, \
        .lvl1 = 180, .ft = FT_HP12, .cut = {50, 58}, .res = {0, 15}, .fenv = {12, 24}, .fe = ENV(1, 1, 60, 100, 0, 0, 40, 60), \
        .ae = ENV(1, 1, 160, 220, 0, 0, 60, 100), .dirt = DT_BURST1, .dpct = 100, .damt = {180, 230}, .fixp = {50, 56}, \
        .vvel = 60}) \
    Y(NOISESNR, "NOISE SNR", 3, 0, 5, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .lvl2 = {0, 0}, .lvl1 = 255, \
        .ft = FT_HP24, .cut = {60, 70}, .res = {25, 35}, .ae = ENV(1, 1, 200, 260, 0, 0, 60, 100), .dirt = DT_STATIC, \
        .dpct = 100, .damt = {220, 255}, .fixp = {60, 60}}) \
    Y(GATEDSNR, "GATED SNR", 3, 0, 3, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .semi2 = {10, 10, 9, 11}, .lvl2 = {100, 160}, \
        .lvl1 = 160, .ft = FT_HP12, .cut = {48, 56}, .res = {0, 15}, .ae = ENV(1, 1, 500, 500, 100, 100, 25, 40), \
        .dirt = DT_STATIC, .dpct = 100, .damt = {220, 255}, .fixp = {50, 56}}) \
    Y(RIMSHOT, "RIMSHOT", 2, 0, 3, 0, {.om = {OM_VA, OM_VA}, .pos = {32, 32}, .semi2 = {22, 22, 22, 22}, .det2 = 128, \
        .lvl2 = {120, 180}, .ft = FT_HP12, .cut = {66, 74}, .res = {35, 45}, .ae = ENV(1, 1, 12, 20, 0, 0, 10, 20), \
        .dirt = DT_CLICK, .dpct = 100, .damt = {80, 140}, .fixp = {69, 71}}) \
    Y(CLAVE, "CLAVE", 2, 1, 2, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .lvl2 = {0, 0}, .ft = FT_HP12, .cut = {84, 92}, \
        .res = {0, 5}, .ae = ENV(1, 1, 20, 30, 0, 0, 15, 25), .fixp = {97, 100}}) \
    Y(COWBELL, "COWBELL", 2, 0, 2, 0, {.om = {OM_VA, OM_VA}, .pos = {32, 32}, .semi2 = {6, 6, 6, 6}, .det2 = 77, \
        .lvl2 = {200, 240}, .lvl1 = 150, .ft = FT_HP12, .cut = {78, 82}, .res = {50, 60}, .fenv = {6, 12}, \
        .fe = ENV(1, 1, 30, 50, 0, 0, 20, 40), .ae = ENV(1, 1, 300, 400, 0, 0, 60, 120), .fixp = {73, 75}}) \
    Y(HAT808C, "HAT 808 C", 3, 0, 3, 0, {.om = {OM_VA, OM_VA}, .pos = {32, 32}, .semi2 = {7, 7, 7, 7}, .det2 = 95, \
        .lvl2 = {150, 200}, .ft = FT_HP24, .cut = {100, 110}, .res = {5, 20}, .ae = ENV(1, 1, 35, 55, 0, 0, 25, 45), \
        .ring = {190, 220}, .fixp = {80, 86}}) \
    Y(HAT808O, "HAT 808 O", 2, 0, 2, 0, {.om = {OM_VA, OM_VA}, .pos = {32, 32}, .semi2 = {7, 7, 7, 7}, .det2 = 95, \
        .lvl2 = {150, 200}, .ft = FT_HP24, .cut = {100, 110}, .res = {5, 20}, .ae = ENV(1, 1, 380, 520, 0, 0, 200, 350), \
        .ring = {190, 220}, .fixp = {80, 86}}) \
    Y(RIDE, "RIDE", 2, 1, 1, 0, {.om = {OM_WAVE, OM_VA}, .tab = TABS(4), .pos = {16, 24}, .pos2 = {32, 32}, .semi2 = {17, 17, 17, 17}, \
        .det2 = 59, .lvl2 = {120, 180}, .ft = FT_HP12, .cut = {80, 90}, .res = {5, 20}, .ae = ENV(1, 1, 1500, 2100, 0, 0, 600, 1000), \
        .ring = {140, 170}, .fixp = {74, 80}, .vvel = 60}) \
    Y(CRASH, "CRASH", 1, 1, 2, 0, {.om = {OM_VA, OM_VA}, .pos = {32, 32}, .semi2 = {17, 17, 11, 17}, .det2 = 59, .lvl2 = {120, 180}, \
        .ft = FT_HP24, .cut = {72, 80}, .res = {5, 20}, .fenv = {12, 24}, .fe = ENV(1, 1, 500, 700, 30, 50, 500, 800), \
        .ae = ENV(1, 1, 2000, 3000, 0, 0, 1000, 1500), .dirt = DT_BURST3, .dpct = 50, .damt = {140, 180}, .ring = {140, 180}, \
        .fixp = {74, 80}}) \
    Y(SHAKER, "SHAKER", 2, 1, 2, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .lvl2 = {0, 0}, .lvl1 = 255, .ft = FT_HP24, \
        .cut = {94, 102}, .res = {5, 15}, .ae = ENV(12, 18, 60, 80, 0, 0, 30, 50), .dirt = DT_BURST2, .dpct = 100, \
        .damt = {220, 255}, .fixp = {60, 60}}) \
    Y(TAMBOURIN, "TAMBOURIN", 2, 0, 2, 0, {.om = {OM_VA, OM_VA}, .pos = {32, 40}, .semi2 = {27, 27, 27, 27}, .det2 = 40, \
        .lvl2 = {100, 160}, .lvl1 = 120, .ft = FT_HP24, .cut = {100, 108}, .res = {5, 20}, .ae = ENV(4, 6, 220, 280, 0, 0, 100, 160), \
        .dirt = DT_BURST3, .dpct = 60, .damt = {140, 200}, .ring = {120, 180}, .fixp = {80, 84}}) \
    Y(CONGA, "CONGA", 2, 1, 2, 0, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(2), .pos = {61, 61}, .pos2 = {0, 2}, .semi2 = {19, 19, 19, 19}, \
        .lvl2 = {30, 50}, .ft = FT_LP12, .cut = {54, 62}, .res = {5, 20}, .fenv = {24, 36}, .fe = ENV(1, 1, 40, 60, 0, 0, 30, 40), \
        .ae = ENV(1, 1, 200, 260, 0, 0, 80, 120), .wd = {25, 35}, .wenv = {0, 0}, .fixp = {50, 62}}) \
    Y(GUIRO, "GUIRO", 1, 1, 2, 0, {.om = {OM_WAVE, OM_WAVE}, .pos = {61, 61}, .lvl2 = {0, 0}, .lvl1 = 255, .ft = FT_HP12, \
        .cut = {74, 84}, .res = {45, 55}, .ae = ENV(1, 1, 250, 320, 0, 0, 80, 120), .dirt = DT_BURST3, .dpct = 100, \
        .damt = {200, 255}, .lfo = {LFO(FBL_SAWUP, LD_VOL, LF_SYNC, 50, 70, 2300, 2700), NOLFO}, .fixp = {60, 60}}) \
    Y(PIPEHIT, "PIPE HIT", 2, 0, 4, 0, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(24, 1), .pos = {26, 34}, .pos2 = {36, 44}, \
        .semi2 = {13, 13, 13, 13}, .det2 = 20, .lvl2 = {120, 180}, .ft = FT_LP24, .cut = {70, 84}, .res = {65, 75}, \
        .ae = ENV(1, 1, 400, 600, 0, 0, 200, 300), .dirt = DT_CLICK, .dpct = 100, .damt = {60, 120}, .ring = {110, 140}, \
        .fixp = {52, 64}}) \
    Y(SHEETMETL, "SHEET METL", 2, 0, 4, 0, {.om = {OM_WAVE, OM_VA}, .tab = TABS(78), .pos = {10, 14}, .pos2 = {32, 36}, \
        .semi2 = {9, 9, 9, 9}, .det2 = 40, .lvl2 = {100, 160}, .ft = FT_DRIVE, .dc = DC_CRUNCH, .cut = {60, 72}, \
        .ae = ENV(1, 1, 600, 800, 0, 0, 300, 400), .wd = {120, 180}, .wenv = {36, 42}, .ring = {110, 150}, .fixp = {50, 62}}) \
    Y(CHAIN, "CHAIN", 2, 0, 4, 0, {.om = {OM_WAVE, OM_WAVE}, .tab = TABS(68), .pos = {16, 24}, .lvl2 = {0, 0}, .lvl1 = 50, \
        .ft = FT_HP12, .cut = {66, 80}, .res = {55, 65}, .ae = ENV(1, 1, 300, 400, 0, 0, 120, 200), .dirt = DT_BURST1, .dpct = 100, \
        .damt = {200, 255}, .fixp = {60, 72}})

#define FK_ENUM_H(id, ...) FK_##id,
#define FK_ENUM_R(id, ...) FK_##id,
#define FK_NAME_H(id, nm, ...) nm,
#define FK_W_O(id, nm, o, a, e, ...) o,
#define FK_W_A(id, nm, o, a, e, ...) a,
#define FK_W_E(id, nm, o, a, e, ...) e,
#define FK_MODP(id, nm, o, a, e, m, ...) m,
#define FK_ROW_R(id, nm, o, a, e, m, ...) __VA_ARGS__,
#define FK_NONE_H(...)
enum { FK_HAND(FK_ENUM_H) FK_ROW0, FK_ROWEND_ = FK_ROW0 - 1, FK_ROWS(FK_ENUM_R) FK_N };
