/* SPDX-License-Identifier: GPL-3.0-only
 * FUGITIVE: the synthesis engines' common interface.
 *
 * Each engine is a port, in fixed point, of one of the Polyend Tracker / Play factory engines (ACD, DIRT, FAT,
 * PERC, VAP, WTFM) or of the open CHIP engine. One engine instance is one monophonic voice; a track owns one.
 * The engine is the whole voice: oscillators, its own filter, envelopes, LFOs, glide and output volume, as the
 * original's voice. The track adds level, pan and the effect sends after it.
 *
 * Numbers (dsp.h): samples int32 Q15 (1.0 = 32768, headroom above), phases uint32, the original's 1.0f = 32768.
 * No float, no double, no 64-bit division anywhere (tests/check_target_ir.sh); 64-bit products are fine.
 *
 * Parameters keep the original engine's ids, order and units. A sound hands the engine each parameter's VALUE,
 * in Q16 of the original's unit (Cutoff 400 Hz = 400 << 16, Resonance 0.5 = 32768, an enum's index i = i << 16),
 * the value the original's voice_on_param_change() would get. Macros, curves and knob positions are worked out
 * before that (macro.c); the engine only sees values.
 *
 * Calling context:
 *   init    once at start-up (tables into RAM ...), main loop. May be NULL.
 *   cook    main loop, whenever a value changes: values -> the coefficients render reads. Never in the ISR.
 *           Cooked data is double-buffered by the caller: render reads one copy while cook writes the other.
 *   reset   voice to silence and its starting state (main loop or ISR, voice not rendering).
 *   on/off  MIDI note on / note off, as the original's voice_on_midi_event (legato and glide as it handles them).
 *           note16: MIDI note in Q16 (60 << 16 = middle C; fractions are fine-pitch). vel 1 .. 127. ISR context.
 *   render  ENG_BLK frames into l (and r when the engine is stereo) -- written, not added. Returns 0 once the
 *           voice is silent and has nothing more to play (the caller may then stop calling it), else 1. ISR context.
 *   tempo   eng_bpm10 (BPM x 10) for tempo-synced LFOs and delays inside an engine.
 *
 * Limits for each engine (checked by static asserts in engines.c):
 *   sizeof voice  <= ENG_V_MAX    sizeof cooked <= ENG_CK_MAX
 *   RAM tables shared by all voices: from the engine's own POOL arrays, <= 16 KiB an engine;
 *   const tables in the flash: <= 40 KiB an engine.
 *   CPU: render of a typical patch <= ~600 ns a block on the build machine (tests/eng, host -O2), never above
 *   ~900 ns; that is ~15 .. 25 % of one FM-1 core (160 MHz, no FPU) for 32 frames.
 */
#pragma once
#include <stdint.h>

#define ENG_BLK 32               /* frames a render (CTL) */
#define ENG_NPAR 88              /* most parameters an engine has (PERC: 84) */
#define ENG_V_MAX 8192           /* bytes of a voice's state */
#define ENG_CK_MAX 60000          /* bytes of an engine's cooked data */
#define ENG_NMACRO 6             /* the original's default macros */
#define ENG_NMOD 12              /* modifiers a macro has, at most */

/* a parameter as the original's metadata declares it */
typedef struct {
    const char *name;            /* the original's name, upper case, <= 12 characters on screen */
    int32_t lo, hi, def;         /* minimum, maximum, default: Q16 of the original's unit */
    uint8_t curve;               /* the original's edit curve: 0 linear, 1 exponential, 2 steep exponential, 3 power */
    uint8_t is_enum;             /* 1: an index, lo .. hi in whole steps */
    uint8_t section;             /* index into the engine's section names (EDIT pages) */
    uint8_t cc;                  /* the original's MIDI CC, 255: none */
} eng_par_t;

/* a macro modifier: amount x the parameter's range, as Patch::Metadata::Macro::ParamModifier (amount Q15, -2 .. 2) */
typedef struct {
    uint8_t par;
    int32_t amount;
} eng_mod_t;

typedef struct {
    const char *name;            /* "TIMBRE" .. */
    uint8_t bipolar, nmod;
    eng_mod_t mod[ENG_NMOD];
} eng_macro_t;

typedef struct {
    const char *name;            /* "ACD" */
    const char *desc;            /* "SINGLE-OSC VA" */
    uint8_t npar, stereo, nsection, nmacro;
    const eng_par_t *par;
    const char *const *section;  /* section names, upper case */
    const eng_macro_t *macro;    /* the original's default macros */
    const char *const *const *enums;   /* per parameter: the value names of an enum (NULL: a number) */
    uint16_t v_size, ck_size;
    void (*init)(void);
    void (*cook)(void *ck, const int32_t *val);
    void (*reset)(void *v);
    void (*on)(void *v, const void *ck, int32_t note16, int vel);
    void (*off)(void *v, const void *ck, int32_t note16);
    int (*render)(void *v, const void *ck, int32_t *l, int32_t *r);
} eng_t;

extern int32_t eng_bpm10;        /* tempo, BPM x 10 */
