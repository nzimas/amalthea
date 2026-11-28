/* SPDX-License-Identifier: GPL-3.0-only
 * The vendored Tonitor OBRADOR called the way Tonitor calls it (ob_design, ob_measure_on, ob_note_on, ob_render), for
 * instrument_port_tests to compare the Amalthea adapter against, sample for sample. */
#define HOST 1
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#include <stdint.h>
#include <string.h>
#include "../third_party/tonitor/tonitor_tables.h"
#include "../third_party/tonitor/dsp.h"
#include "../third_party/tonitor/obrador.c"
int obrador_reference(int kind, int path, uint32_t seed, int note, int blocks, int off_block, int32_t *out)
{
    ob_design_t d;
    ob_voice_t v, probe;
    int b, live = 1;
    ob_design(&d, kind, seed, (uint32_t)path, 0);
    ob_measure_on(&d, &probe);
    ob_init(&v, d.seed ^ 0x77u ^ (uint32_t)(note << 16));
    ob_note_on(&v, &d, note, 127, 1);
    for (b = 0; b < blocks; b++) {
        if (b == off_block)
            ob_note_off(&v);
        memset(out + b * CTL, 0, CTL * sizeof *out);
        live = ob_render(&v, &d, out + b * CTL, CTL);
    }
    return live;
}
const char *obrador_reference_kind(int kind) { return OBK_NAME[kind]; }
