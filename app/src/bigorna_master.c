/* SPDX-License-Identifier: GPL-3.0-only
 * Bigorna's send effects, master compressor and output ceiling for LIFEBLOOD (bigorna_master.h).
 * fx.c is Bigorna's file unchanged; the two helpers it takes from Bigorna's osc.c (sinr) and
 * voice.c (bw_oct), and tracks.c's ceiling, are copied below verbatim. bgm_block follows
 * Bigorna's mix_block order: fx_render on the last block's sends, the returns ducked into the mix,
 * the sends kept for the next block, cp_block, then ceiling. */
#define HOST 1
#define RAMTAB
#include <stdint.h>
#include <string.h>
#include "../third_party/bigorna/fw_tables.h"
#include "../third_party/bigorna/dsp.h"
#include "bigorna_master.h"

static int16_t sin_ram[1025];
AINL int32_t sinr(uint32_t ph)                   /* the sine from RAM, Q15 (osc.c) */
{
    const int16_t *s = &sin_ram[ph >> 22];
    return s[0] + (((s[1] - s[0]) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 15);
}
static int32_t bw_oct(int32_t v) { return 152170 + clampi(v, 0, 127) * 6691; }   /* (voice.c) */

#include "../third_party/bigorna/fx.c"

static inline int32_t ceiling(int32_t x)         /* (tracks.c: the FM-1's output, soft above half scale) */
{
    int32_t a = absi(x);
    if (a <= Q15 / 2)
        return x;
    a = Q15 / 2 + (tanh_q15((a - Q15 / 2) * 2) >> 1);
    return x < 0 ? -a : a;
}

static struct {
    int32_t ch[CTL], dl[CTL], rv[CTL];           /* (the sends, last block's) */
    int32_t wl[3][CTL], wr[3][CTL];              /* (each return, from them) */
    int32_t rg[3];                               /* (each return's gain as the last block ended, Q16) */
    int fresh;                                   /* (reset: the next parameters taken without a glide) */
} bm;

_Static_assert(CTL == BigornaMasterBlock, "Bigorna's block");
_Static_assert(NCH == BigornaChorusParams && NDL == BigornaDelayParams && NRV == BigornaReverbParams && NCP == BigornaCompParams, "fx.c's parameters");

void bgm_reset(void)
{
    uint32_t i;
    for (i = 0; i < 1025u; i++)
        sin_ram[i] = SINE[i];
    fx_init();
    memset(&bm, 0, sizeof bm);
    bm.rg[0] = bm.rg[1] = bm.rg[2] = Q16;
    bm.fresh = 1;
}
void bgm_set(const uint8_t *chorus, const uint8_t *delay, const uint8_t *reverb, const uint8_t *comp, int bpm10)
{
    memcpy(fx_ch, chorus, NCH);
    memcpy(fx_dl, delay, NDL);
    memcpy(fx_rv, reverb, NRV);
    memcpy(fx_cp, comp, NCP);
    fx_bpm10 = clampi(bpm10, 300, 3000);
    fx_cook_all();
    if (bm.fresh) {                              /* (as fx_init: no glide from the defaults) */
        rv.scale = rvc.scale_t;
        dl.t = dlc.tw;
        bm.fresh = 0;
    }
}
void bgm_block(int32_t *l, int32_t *r, const int32_t *chorus, const int32_t *delay, const int32_t *reverb, const int32_t *duck)
{
    uint32_t i, f;
    for (f = 0; f < 3u; f++)                     /* (tracks.c's fx_render) */
        for (i = 0; i < CTL; i++)
            bm.wl[f][i] = bm.wr[f][i] = 0;
    ch_block(bm.ch, bm.wl[0], bm.wr[0], bm.dl, bm.rv);
    dl_block(bm.dl, bm.wl[1], bm.wr[1], bm.rv);
    rv_block(bm.rv, bm.wl[2], bm.wr[2]);
    for (f = 0; f < 3u; f++) {                   /* (mix_block's returns, ducked) */
        int32_t g1 = duck ? duck[f] : Q16, g = bm.rg[f], dg;
        if (g == Q16 && g1 == Q16)
            for (i = 0; i < CTL; i++) {
                l[i] += bm.wl[f][i];
                r[i] += bm.wr[f][i];
            }
        else {
            dg = (g1 - g) / (int32_t)CTL;
            for (i = 0; i < CTL; i++) {
                g += dg;
                l[i] += (int32_t)(((int64_t)bm.wl[f][i] * g) >> 16);
                r[i] += (int32_t)(((int64_t)bm.wr[f][i] * g) >> 16);
            }
        }
        bm.rg[f] = g1;
    }
    for (i = 0; i < CTL; i++) {
        bm.ch[i] = chorus[i];
        bm.dl[i] = delay[i];
        bm.rv[i] = reverb[i];
    }
    cp_block(l, r);
    for (i = 0; i < CTL; i++) {
        l[i] = ceiling(l[i]);
        r[i] = ceiling(r[i]);
    }
}
