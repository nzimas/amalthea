/* SPDX-License-Identifier: GPL-3.0-only
 * FUGITIVE: the subject and the playheads.
 *
 * One melody, the SUBJECT, is shared by the 6 tracks. Track 1 states it; every track reads it with its own PLAYHEAD:
 * speed (augmentation, diminution), play order (forward, reverse and 17 others), transposition in steps of the scale,
 * mirror (inversion), entry delay, note length, chance, ornaments, humanize and pitch variation. After Fugue
 * Machine's playheads and Vascular's Fugitive (weighted fugal answers: at the 5th, the 4th, the octave).
 *
 * Subject: up to FG_MAXLEN steps of a 16th (at 1x). A step is a note's start (its degree), a tie (the note before
 * goes on) or a rest. Degrees are steps of the session's scale from its root: 0 the tonic, 7 the tonic an octave up
 * in a 7-note scale, negative below.
 *
 * This file is pure logic (no hardware, no globals of the firmware): fg_gen draws a subject, fg_pos says which step
 * a playhead reads at its i-th move. tracks.c runs the playheads in the audio interrupt (fg_lane_*).
 */

#define FG_MAXLEN 64u
#define FG_REST (-128)
#define FG_TIE 1u                /* fl: the note before goes on through this step */
#define FG_ACC_SH 1u             /* fl bits 1-2: accent 0 .. 3 */

typedef struct {
    uint8_t len;                 /* steps, 4 .. FG_MAXLEN */
    uint8_t style;               /* the generator's style that drew it (FG_ST_*) */
    uint8_t lo, hi;              /* (unused: 0) */
    uint32_t seed;               /* what drew it */
    int8_t deg[FG_MAXLEN];       /* a start's degree; a tie's: the note's (for drawing); FG_REST */
    uint8_t fl[FG_MAXLEN];       /* FG_TIE, accent */
} fg_subject_t;

/* ---- the playheads' parameters (a pattern keeps them, one per track) ---- */
enum {
    FG_DIR_FWD, FG_DIR_REV, FG_DIR_PING, FG_DIR_PEND, FG_DIR_ODEV, FG_DIR_ROCK, FG_DIR_CASC, FG_DIR_PAIR,
    FG_DIR_BLK4, FG_DIR_BRET, FG_DIR_CONV, FG_DIR_DIVG, FG_DIR_DBL, FG_DIR_STRD, FG_DIR_DRNK, FG_DIR_MEAN,
    FG_DIR_SHUF, FG_DIR_RND, FG_DIR_FRAG, FG_NDIR
};
static const char *const FG_DIR_NAME[FG_NDIR] = {
    "FORWARD", "REVERSE", "PING-PONG", "PENDULUM", "ODD-EVEN", "ROCKING", "CASCADE", "PAIRS",
    "BLOCKS REV", "BLOCKS RETRO", "CONVERGE", "DIVERGE", "DOUBLE", "STRIDE", "DRUNK", "MEANDER",
    "SHUFFLE", "RANDOM", "FRAGMENTS"
};
/* what each does, for the manual and the screen's second line */
static const char *const FG_DIR_HINT[FG_NDIR] = {
    "1 2 3 4", "4 3 2 1", "1 2 3 4 3 2", "1 2 3 4 4 3 2 1", "1 3 5 .. 2 4 6", "1 2 1 3 2 4 3", "123 234 345",
    "2 1 4 3 6 5", "4 3 2 1 8 7 6 5", "LAST 4 FIRST", "1 8 2 7 3 6", "MIDDLE OUT", "1 1 2 2 3 3", "EVERY 3RD",
    "RANDOM WALK", "FORWARD LOOKS BACK", "EACH ONCE A CYCLE", "ANY STEP", "REPEATS A PIECE"
};

/* speeds: num / den of the subject's steps a 16th */
#define FG_NSPD 11u
#define FG_SPD_1X 5u
static const uint8_t FG_SPD_NUM[FG_NSPD] = {1, 1, 1, 2, 3, 1, 4, 3, 2, 3, 4};
static const uint8_t FG_SPD_DEN[FG_NSPD] = {4, 3, 2, 3, 4, 1, 3, 2, 1, 1, 1};
static const char *const FG_SPD_NAME[FG_NSPD] = {"1/4", "1/3", "1/2", "2/3", "3/4", "1X", "4/3", "3/2", "2X", "3X", "4X"};

enum { FG_MIR_OFF, FG_MIR_INV, FG_MIR_TONIC, FG_NMIR };
static const char *const FG_MIR_NAME[FG_NMIR] = {"OFF", "INVERTED", "INV. TONIC"};

typedef struct {
    int8_t trn;                  /* transposition, steps of the scale: -FG_TRN_MAX .. FG_TRN_MAX */
    uint8_t dir;                 /* FG_DIR_* */
    uint8_t gate;                /* note length, % of the note's own: 5 .. 200 */
    uint8_t hum;                 /* humanize, 0 .. 100: timing and velocity */
    uint8_t spd;                 /* FG_SPD index */
    uint8_t mir;                 /* FG_MIR_* */
    uint8_t ent;                 /* entry: 16ths of silence after PLAY before it starts, 0 .. FG_ENT_MAX */
    uint8_t chance;              /* % of the notes it plays */
    uint8_t orn;                 /* ornaments, 0 .. 100: grace notes, turns, repeats */
    uint8_t pvar;                /* pitch variation, 0 .. 100: detune drift, now and then a neighbour note */
    uint8_t on;                  /* 1: reads the subject (0: the track is silent in the fugue) */
    uint8_t pad;
} fg_head_t;
#define FG_TRN_MAX 14
#define FG_ENT_MAX 64u

static void fg_head_plain(fg_head_t *h)
{
    h->trn = 0;
    h->dir = FG_DIR_FWD;
    h->gate = 90;
    h->hum = 0;
    h->spd = FG_SPD_1X;
    h->mir = FG_MIR_OFF;
    h->ent = 0;
    h->chance = 100;
    h->orn = 0;
    h->pvar = 0;
    h->on = 1;
    h->pad = 0;
}

/* ---- which step a playhead reads ---- */
/* a playhead's moving state (the ISR's) */
typedef struct {
    uint32_t i;                  /* moves made since it started */
    int16_t pos, last;           /* the step it reads now, the one before (-1: none) */
    uint8_t perm[FG_MAXLEN];     /* SHUFFLE: this cycle's order */
    uint8_t frag_left, frag_from, frag_len, frag_at;   /* FRAGMENTS: a piece repeating */
    rnd_t rnd;
} fg_walk_t;

static void fg_shuffle(fg_walk_t *w, uint32_t len)
{
    uint32_t k;
    for (k = 0; k < len; k++)
        w->perm[k] = (uint8_t)k;
    for (k = len; k > 1u; k--) {
        uint32_t j = (uint32_t)rnd_below(&w->rnd, (int32_t)k), t = w->perm[k - 1u];
        w->perm[k - 1u] = w->perm[j];
        w->perm[j] = (uint8_t)t;
    }
}
static void fg_walk_start(fg_walk_t *w, uint32_t seed)
{
    w->i = 0;
    w->pos = -1;
    w->last = -1;
    w->frag_left = 0;
    rnd_seed(&w->rnd, seed);
}
static uint32_t fg_stride(uint32_t len)          /* a step coprime with len: 3, else 5, else 7 */
{
    static const uint8_t S[4] = {3, 5, 7, 1};
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        uint32_t a = len, b = S[k];
        while (b) {
            uint32_t t = a % b;
            a = b;
            b = t;
        }
        if (a == 1u && S[k] < len)
            return S[k];
    }
    return 1;
}
/* the step read at move i (w->i), len steps; advances w */
static uint32_t fg_next(fg_walk_t *w, uint32_t dir, uint32_t len)
{
    uint32_t i = w->i++, p, L = len ? len : 1u;
    int32_t q;
    switch (dir) {
    default:
    case FG_DIR_FWD:
        p = i % L;
        break;
    case FG_DIR_REV:
        p = L - 1u - i % L;
        break;
    case FG_DIR_PING:
        if (L < 2u)
            p = 0;
        else {
            uint32_t per = 2u * L - 2u, k = i % per;
            p = k < L ? k : per - k;
        }
        break;
    case FG_DIR_PEND: {
        uint32_t k = i % (2u * L);
        p = k < L ? k : 2u * L - 1u - k;
        break;
    }
    case FG_DIR_ODEV: {          /* the even indices (1st, 3rd ..), then the odd */
        uint32_t k = i % L, ne = (L + 1u) / 2u;
        p = k < ne ? 2u * k : 2u * (k - ne) + 1u;
        break;
    }
    case FG_DIR_ROCK:            /* on, and back to the one before: 0 1 0 2 1 3 2 4 3 .. */
        p = i == 0u ? 0u : (i & 1u) ? ((i + 1u) / 2u) % L : (i / 2u - 1u) % L;
        break;
    case FG_DIR_CASC: {          /* 0 1 2, 1 2 3, 2 3 4 .. */
        uint32_t c = i / 3u, k = i % 3u;
        p = (c + k) % L;
        break;
    }
    case FG_DIR_PAIR: {          /* 1 0 3 2 5 4 .. */
        uint32_t k = i % L;
        p = k ^ 1u;
        if (p >= L)
            p = k;
        break;
    }
    case FG_DIR_BLK4: {          /* each 4 in place, reversed */
        uint32_t k = i % L, b = k & ~3u, e = b + 3u < L ? b + 3u : L - 1u;
        p = e - (k - b);
        break;
    }
    case FG_DIR_BRET: {          /* the blocks of 4 from the last to the first, each forward */
        uint32_t k = i % L, st = ((L - 1u) / 4u) * 4u;
        for (;;) {
            uint32_t sz = L - st < 4u ? L - st : 4u;
            if (k < sz || st == 0u) {
                p = st + (k < sz ? k : sz - 1u);
                break;
            }
            k -= sz;
            st -= 4u;
        }
        break;
    }
    case FG_DIR_CONV: {          /* 0, L-1, 1, L-2 .. */
        uint32_t k = i % L;
        p = (k & 1u) ? L - 1u - k / 2u : k / 2u;
        break;
    }
    case FG_DIR_DIVG: {          /* from the middle outwards: m, m-1, m+1, m-2 .. */
        uint32_t k = i % L, m = (L - 1u) / 2u;
        q = (k & 1u) ? (int32_t)m + (int32_t)(k + 1u) / 2 : (int32_t)m - (int32_t)k / 2;
        p = (uint32_t)clampi(q, 0, (int32_t)L - 1);
        break;
    }
    case FG_DIR_DBL:
        p = (i / 2u) % L;
        break;
    case FG_DIR_STRD:
        p = (i * fg_stride(L)) % L;
        break;
    case FG_DIR_DRNK: {          /* a random walk: +-1 mostly, now and then +-2, wrapping */
        int32_t r = rnd_below(&w->rnd, 16), d = r < 6 ? 1 : r < 12 ? -1 : r < 14 ? 2 : -2;
        q = w->pos < 0 ? 0 : (int32_t)w->pos + d;
        p = (uint32_t)((q % (int32_t)L + (int32_t)L) % (int32_t)L);
        break;
    }
    case FG_DIR_MEAN: {          /* forward, a step back now and then, a step held now and then */
        int32_t r = rnd_below(&w->rnd, 20), d = r < 15 ? 1 : r < 18 ? -1 : 0;
        q = w->pos < 0 ? 0 : (int32_t)w->pos + d;
        p = (uint32_t)((q % (int32_t)L + (int32_t)L) % (int32_t)L);
        break;
    }
    case FG_DIR_SHUF: {
        uint32_t k = i % L;
        if (k == 0u)
            fg_shuffle(w, L);
        p = w->perm[k] < L ? w->perm[k] : k;
        break;
    }
    case FG_DIR_RND:
        p = (uint32_t)rnd_below(&w->rnd, (int32_t)L);
        break;
    case FG_DIR_FRAG: {          /* forward, and now and then a piece of 2 .. 4 steps just played, again 1 .. 3 times */
        if (w->frag_left) {
            p = (w->frag_from + w->frag_at) % L;
            if (++w->frag_at >= w->frag_len) {
                w->frag_at = 0;
                w->frag_left--;
            }
            w->i--;              /* (the walk forward waits for the piece) */
            break;
        }
        q = w->pos < 0 ? 0 : (int32_t)w->last + 1;
        p = (uint32_t)q % L;
        if (i > 4u && rnd_below(&w->rnd, 8) == 0) {
            w->frag_len = (uint8_t)(2 + rnd_below(&w->rnd, 3));
            w->frag_from = (uint8_t)((p + L - w->frag_len + 1u) % L);
            w->frag_at = 0;
            w->frag_left = (uint8_t)(1 + rnd_below(&w->rnd, 3));
        }
        w->last = (int16_t)p;    /* (FRAGMENTS: where the walk forward is) */
        w->pos = (int16_t)p;
        return p;
    }
    }
    if (dir != FG_DIR_FRAG)
        w->last = w->pos;
    w->pos = (int16_t)p;
    return p;
}

/* the note step p belongs to: its first step (-1: a rest) */
static int32_t fg_onset(const fg_subject_t *s, uint32_t p)
{
    if (p >= s->len || s->deg[p] == FG_REST)
        return -1;
    while (p > 0u && (s->fl[p] & FG_TIE))
        p--;
    return (int32_t)p;
}
/* steps from p to the end of its note (1: p is the note's last step) */
static uint32_t fg_rest_of(const fg_subject_t *s, uint32_t p)
{
    uint32_t n = 1;
    while (p + n < s->len && (s->fl[p + n] & FG_TIE) && s->deg[p + n] != FG_REST)
        n++;
    return n;
}
static uint32_t fg_accent(const fg_subject_t *s, uint32_t p) { return (s->fl[p] >> FG_ACC_SH) & 3u; }

/* a degree as the playhead plays it: mirrored, then transposed. Mirror INVERTED: about the subject's first note;
 * INV. TONIC: about the tonic */
static int32_t fg_first_deg(const fg_subject_t *s)
{
    uint32_t p;
    for (p = 0; p < s->len; p++)
        if (s->deg[p] != FG_REST)
            return s->deg[p];
    return 0;
}
static int32_t fg_degree(const fg_subject_t *s, const fg_head_t *h, int32_t d)
{
    if (h->mir == FG_MIR_INV)
        d = 2 * fg_first_deg(s) - d;
    else if (h->mir == FG_MIR_TONIC)
        d = -d;
    return d + h->trn;
}
/* a degree to a MIDI note: scale of n notes, steps st[], root (0 .. 11), base the tonic's note */
static int32_t fg_note(int32_t deg, const uint8_t *st, uint32_t n, int32_t base)
{
    int32_t q = deg >= 0 ? deg / (int32_t)n : -(((int32_t)n - 1 - deg) / (int32_t)n);
    return base + 12 * q + st[deg - q * (int32_t)n];
}

/* ======================================================================================================================
 * the subject's generator
 *
 * A subject is drawn in a style, from a seed, for the session's scale. Every style follows the same plan:
 *   1. a FORM: the subject's bars as a motif and its repetitions (sequence a step lower or higher, inversion,
 *      a varied rhythm), a contrasting idea, and a cadence;
 *   2. a RHYTHM for each beat, from a library of one-beat and two-beat cells weighted by style (16ths and 8ths for
 *      the baroque subject, long notes for the lyrical line, syncopation and rests for the groove ...);
 *   3. a HARMONY for each bar (I-V, I-IV-V-I, I-vi-IV-V ..): strong beats lean to the chord's notes;
 *   4. PITCHES, one note at a time, from a weighted draw over the candidates: steps over leaps (by style), a leap
 *      followed by a step back, a CONTOUR the line follows (arch, rising, falling, wave, valley), no third repeat,
 *      the register kept, and the cadence: the last note the tonic (or the 5th), approached by step or from the 5th;
 *   5. ACCENTS from the metre, leaps and long notes.
 * The draw keeps room for licence: candidates are weighted, not ranked, and each style has its own temperament.
 * ==================================================================================================================== */
enum { FG_ST_BAROQUE, FG_ST_LYRIC, FG_ST_GROOVE, FG_ST_ANGULAR, FG_ST_MINIMAL, FG_ST_CHANT, FG_NST };
static const char *const FG_ST_NAME[FG_NST] = {"BAROQUE", "LYRICAL", "GROOVE", "ANGULAR", "MINIMAL", "CHANT"};

/* one-beat cells: 4 characters, x a start, - a tie, . a rest; weights by style (BAR LYR GRV ANG MIN CHA) */
typedef struct { char c[9]; uint8_t w[FG_NST]; } fg_cell_t;
static const fg_cell_t FG_CELL1[] = {
    {"x---", {2, 6, 1, 1, 2, 4}},
    {"x-x-", {6, 5, 3, 2, 4, 6}},
    {"xxxx", {7, 1, 3, 3, 5, 1}},
    {"x-xx", {5, 2, 3, 2, 3, 2}},
    {"xxx-", {4, 1, 3, 2, 2, 1}},
    {"x--x", {3, 4, 3, 2, 1, 2}},
    {"xx-x", {1, 0, 4, 4, 2, 0}},
    {".xxx", {2, 0, 2, 3, 1, 0}},
    {"..x-", {0, 1, 3, 3, 2, 0}},
    {"x-.x", {1, 0, 4, 3, 2, 0}},
    {"x...", {0, 0, 3, 3, 3, 0}},
    {"x.x.", {0, 0, 4, 3, 4, 0}},
    {".x.x", {0, 0, 3, 3, 3, 0}},
    {"x.xx", {1, 0, 3, 3, 3, 0}},
    {"-x-x", {0, 0, 2, 3, 1, 0}},    /* (a tie into the beat: syncopation across the bar line) */
};
#define FG_NCELL1 (sizeof FG_CELL1 / sizeof FG_CELL1[0])
static const fg_cell_t FG_CELL2[] = {   /* two beats */
    {"x-------", {1, 5, 0, 1, 1, 4}},
    {"x-----x-", {2, 5, 1, 1, 1, 3}},
    {"x-x---x-", {1, 2, 4, 2, 2, 1}},
    {"x--x--x-", {1, 2, 5, 3, 5, 1}},    /* 3 + 3 + 2 */
    {"x---x-xx", {3, 2, 2, 2, 2, 2}},
    {"x-xxx-x-", {3, 1, 2, 2, 2, 1}},
};
#define FG_NCELL2 (sizeof FG_CELL2 / sizeof FG_CELL2[0])

/* per style: interval weights for 0 (repeat) .. 7 degrees; the register; the leap recovery; the contour's pull;
 * the harmony's pull (x10); the chance of a 2-beat cell (%); bars (0: 2 or 4) */
typedef struct {
    uint8_t iv[8];
    int8_t lo, hi;
    uint8_t recover, contour, harm, cell2, bars, rests;
} fg_style_t;
static const fg_style_t FG_STYLE[FG_NST] = {
    /*          0   1   2   3   4  5  6  7    lo  hi  rec ctr hrm c2 bars rests */
    [FG_ST_BAROQUE] = {{3, 40, 18, 9, 8, 3, 1, 4}, -4, 9, 8, 5, 14, 15, 0, 0},
    [FG_ST_LYRIC]   = {{4, 40, 16, 6, 5, 4, 1, 2}, -3, 10, 9, 8, 12, 45, 4, 0},
    [FG_ST_GROOVE]  = {{14, 24, 14, 8, 8, 3, 1, 6}, -3, 7, 4, 3, 10, 30, 2, 1},
    [FG_ST_ANGULAR] = {{3, 12, 10, 10, 10, 9, 7, 9}, -7, 12, 3, 3, 6, 20, 0, 1},
    [FG_ST_MINIMAL] = {{10, 22, 22, 8, 6, 2, 0, 4}, -2, 7, 4, 2, 18, 35, 2, 1},
    [FG_ST_CHANT]   = {{18, 44, 12, 4, 2, 1, 0, 1}, -3, 6, 9, 6, 8, 40, 4, 0},
};
/* contours: the target height (0 .. 64) at 0, 1/4, 1/2, 3/4, 1 of the subject */
enum { FG_CT_ARCH, FG_CT_RISE, FG_CT_FALL, FG_CT_WAVE, FG_CT_VALLEY, FG_CT_PLATEAU, FG_NCT };
static const uint8_t FG_CT[FG_NCT][5] = {
    {16, 40, 60, 40, 8}, {6, 20, 34, 52, 40}, {56, 46, 30, 18, 6}, {20, 50, 20, 48, 10}, {48, 24, 8, 26, 30},
    {14, 44, 46, 44, 10},
};
/* harmony: chord roots (degrees) bar by bar, for 1, 2 and 4 bars */
static const int8_t FG_PROG[6][4] = {
    {0, 4, 0, 0}, {0, 3, 4, 0}, {0, 5, 3, 4}, {0, 4, 5, 3}, {0, 1, 4, 0}, {0, 3, 0, 4},
};

typedef struct {
    rnd_t r;
    const fg_style_t *S;
    uint32_t st, n;              /* style, notes of the scale */
    int32_t five;                /* the degree nearest a 5th above the tonic */
    const uint8_t *steps;        /* the scale's semitones */
    int32_t lo_seen, hi_seen;    /* the lowest and highest degree so far */
} fg_gctx_t;

static uint32_t fg_pick(rnd_t *r, const uint32_t *w, uint32_t n)
{
    uint32_t k, sum = 0, x;
    for (k = 0; k < n; k++)
        sum += w[k];
    if (!sum)
        return (uint32_t)rnd_below(r, (int32_t)n);
    x = (uint32_t)rnd_below(r, (int32_t)sum);
    for (k = 0; k < n; k++) {
        if (x < w[k])
            return k;
        x -= w[k];
    }
    return n - 1u;
}
/* the rhythm: starts (1), ties (2), rests (0) for len steps; the motif's beats copied by the form */
static void fg_rhythm_cells(fg_gctx_t *g, uint8_t *rh, uint32_t from, uint32_t beats)
{
    uint32_t b = 0, k, w[16];
    while (b < beats) {
        const char *c;
        uint32_t nb;
        if (b + 2u <= beats && (uint32_t)rnd_below(&g->r, 100) < g->S->cell2) {
            for (k = 0; k < FG_NCELL2; k++)
                w[k] = FG_CELL2[k].w[g->st];
            c = FG_CELL2[fg_pick(&g->r, w, FG_NCELL2)].c;
            nb = 2;
        } else {
            for (k = 0; k < FG_NCELL1; k++)
                w[k] = FG_CELL1[k].w[g->st];
            c = FG_CELL1[fg_pick(&g->r, w, FG_NCELL1)].c;
            nb = 1;
        }
        for (k = 0; k < 4u * nb; k++)
            rh[from + 4u * b + k] = c[k] == 'x' ? 1u : c[k] == '-' ? 2u : 0u;
        b += nb;
    }
}
/* metric strength of step p (16ths): 4 the bar's first, 3 the half bar, 2 a beat, 1 an 8th, 0 a 16th */
static uint32_t fg_metre(uint32_t p)
{
    return (p & 15u) == 0u ? 4u : (p & 7u) == 0u ? 3u : (p & 3u) == 0u ? 2u : (p & 1u) == 0u ? 1u : 0u;
}
static int fg_chord_tone(const fg_gctx_t *g, int32_t d, int32_t root)
{
    int32_t n = (int32_t)g->n, r = ((d - root) % n + n) % n;
    if (n >= 7)
        return r == 0 || r == 2 || r == 4;
    return r == 0 || r == 2 || (n >= 5 && r == 3);   /* (pentatonic and the like: root, 3rd step, 4th step) */
}
/* the next pitch: weighted draw (see above) */
static int32_t fg_draw_pitch(fg_gctx_t *g, int32_t prev, int32_t prev2, int32_t target, int32_t root,
                             uint32_t strength, int reps, int last_leap)
{
    const fg_style_t *S = g->S;
    uint32_t w[32], k, nc = 0;
    int32_t cand[32], d;
    for (d = S->lo; d <= S->hi && nc < 32u; d++) {
        int32_t iv = d - prev, a = iv < 0 ? -iv : iv;
        int32_t x;                /* (32-bit: a 64-bit divide needs a runtime the FM-1 has not) */
        if (a > 7) {             /* (an octave or more: only the octave, sometimes) */
            if (a != (int32_t)g->n)
                continue;
            a = 7;
        }
        x = S->iv[a] * 100;
        if (a == 7 && iv != (int32_t)g->n && iv != -(int32_t)g->n)
            x = x / 4;
        /* after a leap: back by step */
        if (last_leap >= 3 || last_leap <= -3) {
            int opp = (last_leap > 0) != (iv > 0);
            if (opp && a <= 2)
                x = x * (2 + S->recover) / 2;
            else if (!opp && a >= 2)
                x = x / (1 + S->recover);
        }
        /* after a leap, another leap (either way): rarely; two the same way: more rarely */
        if (prev2 > -100 && a >= 3 && (prev - prev2 > 2 || prev - prev2 < -2))
            x = ((prev - prev2 > 0) == (iv > 0)) ? x / 5 : x / 2;
        /* a zig-zag of 3rds and wider (B D B D): once is a figure, twice a habit */
        if (prev2 > -100 && a >= 2 && (prev - prev2 >= 2 || prev - prev2 <= -2) && (prev - prev2 > 0) != (iv > 0)) {
            x = x * 2 / 3;
            if (d == prev2)
                x /= 3;
        }
        /* the subject's span: past a 10th, each degree more halves the weight */
        {
            int32_t nlo = d < g->lo_seen ? d : g->lo_seen, nhi = d > g->hi_seen ? d : g->hi_seen, over = nhi - nlo - 9;
            while (over-- > 0)
                x /= 2;
        }
        /* the contour: the nearer the target, the better */
        {
            int32_t dist = d - target, ad = dist < 0 ? -dist : dist, span = S->hi - S->lo + 1;
            int32_t f = 64 - ad * 64 * (int32_t)S->contour / (4 * span);
            x = x * (f < 4 ? 4 : f) / 64;
        }
        /* the harmony on strong beats */
        if (strength >= 2u) {
            if (fg_chord_tone(g, d, root))
                x = x * (10 + S->harm * (int32_t)strength / 2) / 10;
            else
                x = x * 10 / (10 + S->harm * (int32_t)strength / 4);
        }
        /* repeats */
        if (iv == 0) {
            if (reps >= 2)
                x /= 12;
            else if (reps == 1)
                x /= 3;
        }
        /* the register's edges */
        if (d == S->lo || d == S->hi)
            x /= 2;
        cand[nc] = d;
        w[nc++] = (uint32_t)(x > 0 ? x : 1);
    }
    if (!nc)
        return prev;
    k = fg_pick(&g->r, w, nc);
    return cand[k];
}

/* variations a form applies to a copy of the motif */
enum { FG_V_SAME, FG_V_SEQ_DN, FG_V_SEQ_UP, FG_V_INV, FG_V_RHY, FG_V_NEW, FG_V_TAIL };

static int32_t fg_target(uint32_t ct, uint32_t p, uint32_t len, const fg_style_t *S)
{
    uint32_t x = p * 64u / (len ? len : 1u), k = x / 16u, f = x % 16u;
    int32_t h;
    if (k >= 4u)
        h = FG_CT[ct][4];
    else
        h = FG_CT[ct][k] + ((int32_t)FG_CT[ct][k + 1u] - FG_CT[ct][k]) * (int32_t)f / 16;
    return S->lo + h * (S->hi - S->lo) / 64;
}

/* draw a subject: style st (FG_NST: any), len steps (0: the style's choice), scale of n notes with steps st[] */
static void fg_gen(fg_subject_t *s, uint32_t style, uint32_t len, const uint8_t *steps, uint32_t n, uint32_t seed)
{
    fg_gctx_t g;
    uint8_t rh[FG_MAXLEN];
    int8_t form[4];
    uint32_t bars, b, p, ct, prog, k;
    int32_t prev = 0, prev2 = -128, last_leap = 0;
    int reps = 0;
    memset(s, 0, sizeof *s);
    rnd_seed(&g.r, seed ? seed : 1u);
    if (style >= FG_NST) {
        static const uint32_t W[FG_NST] = {7, 5, 5, 3, 3, 2};
        style = fg_pick(&g.r, W, FG_NST);
    }
    g.st = style;
    g.S = &FG_STYLE[style];
    g.n = n ? n : 7u;
    g.steps = steps;
    g.five = 0;
    for (k = 0; k < g.n; k++)
        if (steps[k] <= 7u)
            g.five = (int32_t)k;
    if (len >= 4u && len <= FG_MAXLEN)
        bars = (len + 15u) / 16u;
    else {
        bars = g.S->bars ? g.S->bars : (rnd_below(&g.r, 3) ? 2u : 4u);
        if (style == FG_ST_GROOVE || style == FG_ST_MINIMAL)
            bars = rnd_below(&g.r, 3) ? bars : 1u;
        len = bars * 16u;
    }
    if (bars > 4u)
        bars = 4u;
    s->len = (uint8_t)len;
    s->style = (uint8_t)style;
    s->seed = seed;
    {
        static const uint32_t WC[FG_NST][FG_NCT] = {
            {5, 2, 3, 3, 1, 2}, {6, 2, 2, 2, 1, 3}, {2, 2, 2, 5, 2, 3}, {2, 2, 2, 4, 3, 1}, {2, 1, 1, 5, 2, 3},
            {4, 1, 2, 2, 1, 5},
        };
        ct = fg_pick(&g.r, WC[style], FG_NCT);
    }
    prog = (uint32_t)rnd_below(&g.r, 6);
    /* the form: what each bar is (the motif a bar long) */
    form[0] = FG_V_SAME;
    {
        static const int8_t F2[4][2] = {{FG_V_SAME, FG_V_TAIL}, {FG_V_SAME, FG_V_SEQ_DN}, {FG_V_SAME, FG_V_RHY},
                                        {FG_V_SAME, FG_V_NEW}};
        static const int8_t F4[8][4] = {
            {FG_V_SAME, FG_V_SAME, FG_V_NEW, FG_V_TAIL},       /* A A B c */
            {FG_V_SAME, FG_V_SEQ_DN, FG_V_SEQ_DN, FG_V_TAIL},  /* A A-1 A-2 c: a sequence, the baroque way */
            {FG_V_SAME, FG_V_NEW, FG_V_SAME, FG_V_TAIL},       /* A B A c */
            {FG_V_SAME, FG_V_SEQ_UP, FG_V_NEW, FG_V_TAIL},
            {FG_V_SAME, FG_V_RHY, FG_V_INV, FG_V_TAIL},
            {FG_V_SAME, FG_V_NEW, FG_V_NEW, FG_V_TAIL},        /* through-composed */
            {FG_V_SAME, FG_V_SAME, FG_V_SAME, FG_V_RHY},       /* (an ostinato) */
            {FG_V_SAME, FG_V_INV, FG_V_SEQ_DN, FG_V_TAIL},
        };
        if (bars == 2u) {
            k = (uint32_t)rnd_below(&g.r, 4);
            form[1] = F2[k][1];
        } else if (bars >= 3u) {
            static const uint32_t W4[FG_NST][8] = {
                {3, 6, 2, 2, 2, 1, 0, 2}, {4, 2, 5, 3, 2, 3, 0, 1}, {4, 1, 3, 1, 3, 1, 5, 1}, {2, 2, 2, 2, 3, 4, 0, 3},
                {3, 2, 2, 1, 2, 0, 6, 1}, {4, 2, 4, 2, 1, 2, 1, 0},
            };
            k = fg_pick(&g.r, W4[style], 8);
            for (b = 1; b < 4u; b++)
                form[b] = F4[k][b];
        }
    }
    /* rhythm: the motif, then each bar from it */
    fg_rhythm_cells(&g, rh, 0, 4);
    if (rh[0] != 1u && style != FG_ST_GROOVE && style != FG_ST_ANGULAR && rnd_below(&g.r, 4))
        rh[0] = 1u;              /* (most subjects start on the beat) */
    if (rh[0] == 2u)
        rh[0] = 1u;
    for (b = 1; b < bars; b++) {
        uint32_t o = b * 16u;
        if (form[b] == FG_V_NEW || form[b] == FG_V_RHY || form[b] == FG_V_TAIL) {
            fg_rhythm_cells(&g, rh, o, 4);
            if (form[b] == FG_V_RHY)     /* (half the motif kept) */
                memcpy(rh + o, rh, rnd_below(&g.r, 2) ? 8u : 4u);
        } else
            memcpy(rh + o, rh, 16);
    }
    /* the cadence: the last note long enough (a beat or more), on a beat */
    if (style != FG_ST_GROOVE || rnd_below(&g.r, 2)) {
        uint32_t e = len, cad = len - (style == FG_ST_LYRIC || style == FG_ST_CHANT ? 8u : 4u);
        rh[cad] = 1u;
        for (p = cad + 1u; p < e; p++)
            rh[p] = (style == FG_ST_BAROQUE && p >= e - 1u && rnd_below(&g.r, 3) == 0) ? 0u : 2u;
    }
    /* rests at the start for some styles (a subject entering after an 8th rest) */
    if (style == FG_ST_BAROQUE && rnd_below(&g.r, 5) == 0) {
        rh[0] = 0;
        rh[1] = 0;
        if (rh[2] != 1u)
            rh[2] = 1u;
    }
    for (p = 0; p < len; p++)    /* (a tie after a rest is a start) */
        if (rh[p] == 2u && (p == 0u || rh[p - 1u] == 0u))
            rh[p] = 1u;
    /* pitches */
    {
        int32_t motif_deg[16], motif_n = 0, first = 0, mi = 0;
        int32_t shift = 0;
        uint32_t fb = 0;
        /* the first note: the tonic, the 5th, or the 3rd (by style) */
        k = (uint32_t)rnd_below(&g.r, 10);
        first = k < 6u ? 0 : k < 9u ? g.five : 2;
        if (style == FG_ST_ANGULAR && rnd_below(&g.r, 3) == 0)
            first = (int32_t)g.n;
        prev = first;
        g.lo_seen = g.hi_seen = first;
        for (p = 0; p < len; p++) {
            uint32_t bar = p / 16u, v = bar < 4u ? (uint32_t)form[bar] : FG_V_NEW;
            int32_t root = FG_PROG[prog][bars == 1u ? (p / 8u) % 2u * 3u : bars == 2u ? bar * 2u : bar & 3u];
            int32_t d;
            if (bar != fb) {     /* a bar starts: where its copy of the motif sits */
                fb = bar;
                mi = 0;
                shift += v == FG_V_SEQ_DN ? -1 : v == FG_V_SEQ_UP ? 1 : 0;
                if (v == FG_V_SAME || v == FG_V_RHY)
                    shift = shift / 2;   /* (back towards the motif's own place) */
            }
            if (rh[p] == 0u) {
                s->deg[p] = FG_REST;
                continue;
            }
            if (rh[p] == 2u) {
                s->deg[p] = s->deg[p - 1u];
                s->fl[p] = FG_TIE;
                continue;
            }
            /* a start */
            if (p == 0u || (bar == 0u && motif_n == 0 && p < 4u))
                d = first;
            else if (bar > 0u && (v == FG_V_SAME || v == FG_V_SEQ_DN || v == FG_V_SEQ_UP || v == FG_V_INV) &&
                     mi < motif_n) {
                d = motif_deg[mi];
                if (v == FG_V_INV)
                    d = 2 * first - d;
                d += shift;
                mi++;
            } else {
                int32_t tgt = fg_target(ct, p, len, g.S);
                d = fg_draw_pitch(&g, prev, prev2, tgt, root, fg_metre(p), reps, last_leap);
                if (v == FG_V_RHY && mi < motif_n && rnd_below(&g.r, 2))   /* (half the time, the motif's pitch) */
                    d = motif_deg[mi] + shift;
                mi++;
            }
            /* the cadence: the last start is the tonic (or the 5th, or the octave); the one before steps into it */
            {
                uint32_t q, lastp = 0, before = 0;
                for (q = 0; q < len; q++)
                    if (rh[q] == 1u)
                        before = lastp, lastp = q;
                if (p == lastp) {
                    k = (uint32_t)rnd_below(&g.r, 10);
                    d = k < 6u ? 0 : k < 8u ? g.five : (prev > (int32_t)g.n / 2 ? (int32_t)g.n : 0);
                    if (style == FG_ST_GROOVE && k >= 8u)
                        d = prev;
                } else if (p == before && style != FG_ST_ANGULAR) {
                    k = (uint32_t)rnd_below(&g.r, 10);
                    d = k < 4u ? 1 : k < 7u ? -1 : k < 9u ? g.five : 2;
                    if (d == -1 && g.n < 7u)
                        d = 1;
                }
            }
            d = clampi(d, g.S->lo - 2, g.S->hi + 2);
            if (bar == 0u && motif_n < 16)
                motif_deg[motif_n++] = d;
            g.lo_seen = d < g.lo_seen ? d : g.lo_seen;
            g.hi_seen = d > g.hi_seen ? d : g.hi_seen;
            reps = d == prev ? reps + 1 : 0;
            last_leap = d - prev;
            prev2 = prev;
            prev = d;
            s->deg[p] = (int8_t)d;
        }
    }
    /* accents: the metre, a leap into the note, a long note */
    {
        int32_t pd = -128;
        for (p = 0; p < len; p++) {
            uint32_t a, m;
            if (s->deg[p] == FG_REST || (s->fl[p] & FG_TIE))
                continue;
            m = fg_metre(p);
            a = m >= 4u ? 2u : m >= 2u ? 1u : 0u;
            if (pd > -100 && (s->deg[p] - pd > 2 || pd - s->deg[p] > 2))
                a++;
            if (fg_rest_of(s, p) >= 4u)
                a++;
            if (style == FG_ST_GROOVE && m == 0u && rnd_below(&g.r, 4) == 0)
                a = 3;   /* (an off-beat push) */
            s->fl[p] |= (uint8_t)((a > 3u ? 3u : a) << FG_ACC_SH);
            pd = s->deg[p];
        }
    }
}

/* ---- the playheads' parameters drawn, after Vascular's Fugitive: track 0 plain, the others answers ---- */
/* an answer's playhead (tracks 2 .. 6): weighted as a fugue answers -- at the 5th, the 4th, the octave; 1/2 and 2X */
static void fg_head_answer(fg_head_t *x, rnd_t *r, uint32_t scale_n)
{
    uint32_t k;
    fg_head_plain(x);
    {   /* direction */
        static const uint32_t WD[FG_NDIR] = {10, 7, 5, 3, 2, 3, 2, 2, 2, 2, 2, 1, 2, 2, 2, 3, 2, 1, 2};
        x->dir = (uint8_t)fg_pick(r, WD, FG_NDIR);
    }
    {   /* speed: 1/2 and 2X the most */
        static const uint32_t WS[FG_NSPD] = {2, 1, 6, 1, 1, 5, 1, 2, 6, 1, 2};
        x->spd = (uint8_t)fg_pick(r, WS, FG_NSPD);
    }
    {   /* transposition: the answers -- at the 5th (4 steps) up, the 4th (3) up, a 4th or 5th down, the octave */
        int32_t o = (int32_t)(scale_n ? scale_n : 7u);
        int32_t TR[10];
        static const uint32_t WT[10] = {30, 25, 20, 20, 15, 12, 10, 10, 10, 5};
        TR[0] = 4; TR[1] = 3; TR[2] = -3; TR[3] = -4; TR[4] = 2; TR[5] = 0; TR[6] = o; TR[7] = -o; TR[8] = -2;
        TR[9] = 5;
        if (o != 7)              /* (not 7 notes: the 5th and 4th as near as the scale has them) */
            TR[0] = o * 4 / 7, TR[1] = o * 3 / 7, TR[2] = -TR[1], TR[3] = -TR[0];
        k = fg_pick(r, WT, 10);
        x->trn = (int8_t)clampi(TR[k], -FG_TRN_MAX, FG_TRN_MAX);
    }
    x->mir = rnd_below(r, 6) == 0 ? (uint8_t)FG_MIR_INV : (uint8_t)FG_MIR_OFF;
    x->gate = (uint8_t)(40 + rnd_below(r, 70));
    {   /* entry: at once, or a beat, half a bar, a bar later (the voices of a fugue enter one by one) */
        static const uint32_t WE[5] = {10, 2, 3, 4, 1};
        static const uint8_t E[5] = {0, 4, 8, 16, 32};
        x->ent = E[fg_pick(r, WE, 5)];
    }
    x->chance = rnd_below(r, 4) == 0 ? (uint8_t)(55 + rnd_below(r, 40)) : 100u;
    x->orn = rnd_below(r, 3) == 0 ? (uint8_t)(10 + rnd_below(r, 40)) : 0u;
    x->hum = rnd_below(r, 2) ? (uint8_t)(5 + rnd_below(r, 30)) : 0u;
    x->pvar = rnd_below(r, 4) == 0 ? (uint8_t)(5 + rnd_below(r, 30)) : 0u;
}
/* the subject's own playhead (track 1) drawn again, kept recognisable: forward most of the time, at its pitch or an
 * octave or a 5th away, at 1X (or 1/2, 2X), long notes, little chance taken, few ornaments */
static void fg_head_lead(fg_head_t *x, rnd_t *r, uint32_t scale_n)
{
    int32_t o = (int32_t)(scale_n ? scale_n : 7u);
    fg_head_plain(x);
    {
        static const uint32_t WD[5] = {12, 2, 2, 1, 1};
        static const uint8_t D[5] = {FG_DIR_FWD, FG_DIR_PING, FG_DIR_REV, FG_DIR_PEND, FG_DIR_DBL};
        x->dir = D[fg_pick(r, WD, 5)];
    }
    {
        static const uint32_t WS[3] = {8, 2, 2};
        static const uint8_t S[3] = {FG_SPD_1X, 2, 8};
        x->spd = S[fg_pick(r, WS, 3)];
    }
    {
        static const uint32_t WT[5] = {12, 3, 3, 2, 1};
        int32_t TR[5];
        TR[0] = 0; TR[1] = o; TR[2] = -o; TR[3] = o * 4 / 7; TR[4] = -(o * 3 / 7);
        x->trn = (int8_t)clampi(TR[fg_pick(r, WT, 5)], -FG_TRN_MAX, FG_TRN_MAX);
    }
    x->mir = rnd_below(r, 10) == 0 ? (uint8_t)FG_MIR_INV : (uint8_t)FG_MIR_OFF;
    x->gate = (uint8_t)(70 + rnd_below(r, 31));
    x->ent = 0;
    x->chance = rnd_below(r, 6) == 0 ? (uint8_t)(80 + rnd_below(r, 16)) : 100u;
    x->orn = rnd_below(r, 4) == 0 ? (uint8_t)(5 + rnd_below(r, 26)) : 0u;
    x->hum = rnd_below(r, 2) ? (uint8_t)(5 + rnd_below(r, 20)) : 0u;
    x->pvar = rnd_below(r, 6) == 0 ? (uint8_t)(5 + rnd_below(r, 16)) : 0u;
}
static void fg_heads_draw(fg_head_t *h, uint32_t nh, uint32_t seed, uint32_t scale_n)
{
    rnd_t r;
    uint32_t t;
    rnd_seed(&r, seed ? seed : 3u);
    for (t = 0; t < nh; t++) {
        fg_head_t *x = &h[t];
        if (t == 0u) {
            static const uint32_t WD[3] = {8, 1, 1};
            static const uint8_t D[3] = {FG_DIR_FWD, FG_DIR_PING, FG_DIR_REV};
            fg_head_plain(x);
            x->dir = D[fg_pick(&r, WD, 3)];
            x->gate = (uint8_t)(70 + rnd_below(&r, 30));
            continue;
        }
        fg_head_answer(x, &r, scale_n);
    }
}
