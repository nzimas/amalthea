/* SPDX-License-Identifier: GPL-3.0-only
 * FIBER: the wavetables (REPORT.md 7). A table is 64 half-waves of 64 unsigned bytes (128 = zero): positions 0 .. 60
 * its own, 61 .. 63 the three common waves (triangle, pulse, saw). An oscillator plays a 128-sample signed wave
 * expanded from a half-wave with the one's-complement symmetry of the original (fb_expand):
 *     S[n] = U[n] - 128,  S[127 - n] = 127 - U[n]    (n < 64; so S[n] + S[127 - n] = -1)
 * A wave is fetched when an oscillator is to take it (fb_frame, at the control rate), so no table is held in RAM:
 *   - the 40 ROM tables (IDs 0 .. 27, 76 .. 87): their sparse descriptor rows, the gaps filled at the midpoints with
 *     integer averages (REPORT.md 7.2) [B / I]. The fill depends only on each gap's two ends, so a position's
 *     half-wave is found by halving its gap down to it, averaging as the fill does (fb_frame), the same bytes as
 *     filling the whole table first, the first gap first (fb_fill, kept for the tests);
 *   - the 24 algorithmic tables (IDs 28 .. 31, 56 .. 75): generators written here [C], run once at build time on the
 *     computer (tools/gen_fiber_gen.c) into flash (FB_GEN). The report found the generator code (0x4202bb70 onward)
 *     but did not translate it; these draw what each table's name says, with the two recovered helpers -- the 16-bit
 *     shift register with polynomial 0x8125 and the (p + 6c + n + 4) >> 3 smoothing -- where they apply;
 *   - IDs 32 .. 55 are the original's user tables: not offered here (fb_tab_ok). */

#define FB_FRAMES 64u            /* half-waves a table: 61 + the 3 common ones */
#define FB_HW 64u                /* bytes a half-wave */
typedef uint8_t fb_table_t[FB_FRAMES][FB_HW];

static int fb_tab_ok(uint32_t id) { return id < FB_NTAB && !(id >= 32u && id <= 55u); }

/* the common half-waves (REPORT.md 7.4) [B]: triangle 130 + 3 steps up to 223, the peak twice, down to 130; pulse
 * 192; saw 192, 191, .. 129 */
static void fb_common(uint8_t (*f)[FB_HW])
{
    uint32_t n;
    for (n = 0; n < 32u; n++) {
        f[0][n] = (uint8_t)(130u + 3u * n);
        f[0][63u - n] = (uint8_t)(130u + 3u * n);
        f[1][n] = f[1][63u - n] = 192u;
        f[2][n] = (uint8_t)(192u - n);
        f[2][63u - n] = (uint8_t)(129u + n);
    }
}

/* the gaps between defined half-waves filled at their midpoints (REPORT.md 7.2): the first gap from the left, its
 * midpoint the floor of the two defined positions' mean, each byte the floor of the two bytes' mean, again until
 * none is left */
static void fb_fill(fb_table_t t, uint8_t *ok)
{
    for (;;) {
        uint32_t g, lo, hi, mid, k;
        for (g = 0; g < 61u && ok[g]; g++)
            ;
        if (g >= 61u)
            return;
        lo = g - 1u;
        for (hi = g + 1u; !ok[hi]; hi++)
            ;
        mid = (lo + hi) >> 1;
        for (k = 0; k < FB_HW; k++)
            t[mid][k] = (uint8_t)(((uint32_t)t[lo][k] + t[hi][k]) >> 1);
        ok[mid] = 1;
    }
}

/* ------------------------------------------------------- generators [C] --- */
/* a half-wave from values -32767 .. 32767 at the 64 points of the cycle's first half (amplitude ~ the ROM's) */
static void fb_put(uint8_t *h, const int32_t *v)
{
    uint32_t n;
    for (n = 0; n < FB_HW; n++)
        h[n] = (uint8_t)clampi(128 + ((v[n] * 100) >> 15), 1, 255);
}
/* the recovered smoothing (0x4202b884): (previous + 6 current + next + 4) >> 3 over a half-wave, ends held */
static void fb_smooth(uint8_t *h)
{
    uint8_t o[FB_HW];
    uint32_t n;
    for (n = 0; n < FB_HW; n++) {
        uint32_t p = h[n ? n - 1u : 0u], c = h[n], x = h[n < FB_HW - 1u ? n + 1u : n];
        o[n] = (uint8_t)((p + 6u * c + x + 4u) >> 3);
    }
    memcpy(h, o, FB_HW);
}
/* the cycle's phase of point n of the first half: n / 128 of a cycle */
#define FB_T(n) ((uint32_t)(n) << 25)
static int32_t fb_saw(uint32_t ph) { return 32767 - (int32_t)(ph >> 16); }   /* falling, as the common saw */
static int32_t fb_sq(uint32_t ph, uint32_t w) { return ph < w ? 32767 : -32767; }
static int32_t fb_norm(int32_t *v, int32_t peak)   /* to full scale */
{
    uint32_t n;
    int32_t m = 1;
    for (n = 0; n < FB_HW; n++)
        m = absi(v[n]) > m ? absi(v[n]) : m;
    {
        int32_t sc = (int32_t)((uint32_t)peak * 32768u / (uint32_t)m);   /* (a 32-bit divide: no 64-bit one here) */
        for (n = 0; n < FB_HW; n++)
            v[n] = (int32_t)(((int64_t)v[n] * sc) >> 15);
    }
    return m;
}

static void fb_gen(fb_table_t t, uint32_t id)
{
    uint32_t p, n;
    uint32_t lf = id == 72u ? 0x1234u : id == 73u ? 0xACE1u : 0x0001u;   /* K+ST: the register's seeds */
    uint8_t bits[FB_HW];
    if (id >= 71u && id <= 73u)                    /* the shift register (0x4202b928): binary values, one frame */
        for (n = 0; n < FB_HW; n++) {
            lf = (lf & 0x8000u) ? ((lf << 1) ^ 0x8125u) & 0xFFFFu : (lf << 1) & 0xFFFFu;
            bits[n] = (uint8_t)((lf & 0x0100u) ? 224u : 32u);
        }
    for (p = 0; p < 61u; p++) {
        int32_t v[FB_HW], x = (int32_t)(p * (uint32_t)Q15 / 60u);   /* the position, 0 .. 1 */
        for (n = 0; n < FB_HW; n++) {
            uint32_t ph = FB_T(n), r;
            int32_t s = 0, k;
            switch (id) {
            case 28:                               /* MW SYNC: a sine synced, its ratio rising 1 .. 8 */
                r = 65536u + (uint32_t)x * 14u;
                s = sin_ph((uint32_t)(((uint64_t)ph * r) >> 16));
                break;
            case 29:                               /* MICROPWM: a square, the width narrowing a little */
                s = fb_sq(ph, 0x80000000u - (uint32_t)x * 26214u);
                break;
            case 30:                               /* GLASSY: the fundamental and a pair of partials climbing */
                k = 3 + (x * 14 >> 15);
                s = (sin_ph(ph) >> 1) + mulq15(sin_ph(ph * (uint32_t)k), 22937) + mulq15(sin_ph(ph * (uint32_t)(k + 2)), 13107);
                break;
            case 31:                               /* SQUAREHP: a square, its lowest harmonics taken away in turn */
                for (k = 1 + 2 * (x * 9 >> 15); k < 40; k += 2)
                    s += sin_ph(ph * (uint32_t)k) / k;
                break;
            case 56: case 57: case 58:             /* SAWSYNC 1 .. 3: a saw synced, the ratio up to 2, 4, 8 */
                r = 65536u + (uint32_t)x * (2u << (id - 56u)) * 2u - (uint32_t)x * 2u;
                s = fb_saw((uint32_t)(((uint64_t)ph * r) >> 16));
                break;
            case 59: case 60: case 61:             /* PULSYNC 1 .. 3 */
                r = 65536u + (uint32_t)x * (2u << (id - 59u)) * 2u - (uint32_t)x * 2u;
                s = fb_sq((uint32_t)(((uint64_t)ph * r) >> 16), 0x80000000u);
                break;
            case 62: case 63: case 64:             /* SINSYNC 1 .. 3 */
                r = 65536u + (uint32_t)x * (2u << (id - 62u)) * 2u - (uint32_t)x * 2u;
                s = sin_ph((uint32_t)(((uint64_t)ph * r) >> 16));
                break;
            case 65:                               /* PWMPULSE: the width 50 % .. 5 % of the half */
                s = fb_sq(ph, 0x80000000u - (uint32_t)x * 58982u);
                break;
            case 66: {                             /* PWM SAW: the ramp squeezed into less and less of the half */
                uint32_t w = 0x80000000u - (uint32_t)x * 58982u;
                s = ph < w ? fb_saw(udiv64_32(ph, 0, w)) : 0;   /* (ph / w of a cycle) */
                break;
            }
            case 67:                               /* FUZZWAVE: a sine driven into tanh, x1 .. x8 */
                s = tanh_q15(mulq15(sin_ph(ph), Q15 + x * 7));
                break;
            case 68:                               /* DISTORTD: a sine and its third, clipped harder */
                s = clampi((sin_ph(ph) + (sin_ph(3u * ph) >> 2)) * (1 + (x * 5 >> 15)), -32767, 32767);
                break;
            case 69:                               /* HEAVYFUZ: a sine folded, x1 .. x5 */
                s = sin_ph((uint32_t)((int64_t)mulq15(sin_ph(ph), Q15 + x * 4) * 32768));
                break;
            case 70:                               /* FUZZSYNC: a synced sine into tanh */
                r = 65536u + (uint32_t)x * 6u;
                s = tanh_q15(mulq15(sin_ph((uint32_t)(((uint64_t)ph * r) >> 16)), Q15 * 2 + x * 4));
                break;
            case 71: case 72: case 73:             /* K+ST 1 .. 3: the register's frame, smoothed below */
                s = 0;
                break;
            case 74: {                             /* FROM1TO5: harmonic 1 to harmonic 5, crossfaded */
                int32_t h = x * 4, hi = h >> 15, fr = h & 32767;
                s = mulq15(sin_ph(ph * (uint32_t)(hi + 1)), Q15 - fr) + mulq15(sin_ph(ph * (uint32_t)(hi + 2 > 5 ? 5 : hi + 2)), fr);
                break;
            }
            default:                               /* 75, 19 / 20: the fundamental under harmonics 19 and 20 */
                s = (sin_ph(ph) >> 1) + mulq15(sin_ph(ph * 19u), (Q15 - x) / 3) + mulq15(sin_ph(ph * 20u), x / 3);
                break;
            }
            v[n] = s;
        }
        if (id >= 71u && id <= 73u) {              /* fewer passes of the smoothing at the higher positions */
            uint32_t pass = 1u + (60u - p) / 3u;
            memcpy(t[p], bits, FB_HW);
            while (pass--)
                fb_smooth(t[p]);
            continue;
        }
        fb_norm(v, 32767);
        fb_put(t[p], v);
        if (id == 29u || id == 65u || id == 59u || id == 60u || id == 61u)
            fb_smooth(t[p]);                       /* (the squares' edges as a byte table would hold them) */
    }
}

/* an algorithmic table's place in FB_GEN (0 .. 23) */
static uint32_t fb_genix(uint32_t id) { return id <= 31u ? id - 28u : id - 56u + 4u; }

#ifndef FB_GEN_TOOL
/* the half-wave of table id at position p (0 .. 63): a pointer into the flash, or into tmp (64 bytes) when it is
 * made here (a ROM table's filled position: its gap halved down to it, averaging the ends as fb_fill does) */
static const uint8_t *fb_frame(uint32_t id, uint32_t p, uint8_t *tmp)
{
    const uint16_t *row;
    uint32_t lo, hi, k;
    const uint8_t *a, *b;
    uint8_t ea[FB_HW], eb[FB_HW];                 /* (the ends as they move in; on the stack: both cores call it) */
    if (p >= 61u)
        return FB_COMMON[p - 61u];
    if (!fb_tab_ok(id))
        id = 6u;
    if (FB_ROW[id] < 0)
        return &FB_GEN[(fb_genix(id) * 61u + p) * FB_HW];
    row = &FB_DESC[(uint32_t)FB_ROW[id] * 61u];
    if (row[p] != 0xFFFFu)
        return &FB_ROM[(uint32_t)row[p] * FB_HW];
    for (lo = p; row[lo] == 0xFFFFu; lo--)       /* (positions 0 and 60 are always defined [B]) */
        ;
    for (hi = p; row[hi] == 0xFFFFu; hi++)
        ;
    a = &FB_ROM[(uint32_t)row[lo] * FB_HW];
    b = &FB_ROM[(uint32_t)row[hi] * FB_HW];
    for (;;) {
        uint32_t mid = (lo + hi) >> 1;
        for (k = 0; k < FB_HW; k++)
            tmp[k] = (uint8_t)(((uint32_t)a[k] + b[k]) >> 1);
        if (mid == p)
            return tmp;
        if (p < mid) {
            hi = mid;
            memcpy(eb, tmp, FB_HW);
            b = eb;
        } else {
            lo = mid;
            memcpy(ea, tmp, FB_HW);
            a = ea;
        }
    }
}
#endif

/* a whole table into t: id's 61 half-waves and the 3 common ones (the generator tool, the tests) */
static void fb_tab_build(fb_table_t t, uint32_t id)
{
    uint8_t ok[64];
    uint32_t p;
    if (!fb_tab_ok(id))
        id = 6u;
    if (FB_ROW[id] >= 0) {
        const uint16_t *row = &FB_DESC[(uint32_t)FB_ROW[id] * 61u];
        for (p = 0; p < 61u; p++) {
            ok[p] = row[p] != 0xFFFFu;
            if (ok[p])
                memcpy(t[p], &FB_ROM[(uint32_t)row[p] * FB_HW], FB_HW);
        }
        fb_fill(t, ok);
    } else {
#ifdef FB_GEN_TOOL
        fb_gen(t, id);
#else
        memcpy(t, &FB_GEN[fb_genix(id) * 61u * FB_HW], 61u * FB_HW);
#endif
    }
    fb_common(&t[61]);
}

/* a half-wave into a 128-sample signed playback wave (REPORT.md 7.3) */
AINL void fb_expand(int8_t *w, const uint8_t *u)
{
    uint32_t n;
    for (n = 0; n < FB_HW; n++) {
        w[n] = (int8_t)(u[n] - 128);
        w[127u - n] = (int8_t)(127 - u[n]);
    }
}
