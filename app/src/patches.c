/* SPDX-License-Identifier: GPL-3.0-only
 * Adapter for Fugitive's VAP / CHIP / PERC engines; recipes from amalthea_recipes.c. No RT calls. */
#define HOST 1
#define RAMTAB
#include <stdint.h>
#include <string.h>
#include "fw_tables.h"
#include "dsp.h"
#include "eng.h"
#define NENG 7u
#define NMENG 6u
#define NDI 7u
enum { E_ACD, E_FAT, E_VAP, E_WTFM, E_DIRT, E_CHIP, E_PERC };
extern const eng_t ENG_VAP,ENG_CHIP,ENG_PERC;
const char *amalthea_native_family(int,uint32_t);
const char *amalthea_fiber_family(uint32_t);
const char *amalthea_bigorna_family(int,uint32_t);
const char *amalthea_bigorna_drum_family(uint32_t);
static const eng_t *ENGS[] = {&ENG_VAP,&ENG_VAP,&ENG_VAP,&ENG_VAP,&ENG_VAP,&ENG_CHIP,&ENG_PERC};
static const eng_t *eng_of(uint32_t e) { return ENGS[e < NENG ? e : 0]; }
typedef struct { uint8_t eng, family; int8_t mac[ENG_NMACRO]; uint32_t seed; int32_t val[ENG_NPAR]; } sound_t;
static int32_t par_clamp(const eng_par_t *p, int32_t v)
{
    v = clampi(v, p->lo, p->hi);
    if (p->is_enum)
        v = (int32_t)((uint32_t)(v - p->lo + 0x8000) & 0xFFFF0000u) + p->lo;
    return v;
}
/* the engine's defaults, the macros at 0 */
static void snd_default(sound_t *s, uint32_t e)
{
    const eng_t *E = eng_of(e);
    uint32_t j;
    memset(s, 0, sizeof *s);
    s->eng = (uint8_t)(e < NENG ? e : 0u);
    for (j = 0; j < E->npar && j < ENG_NPAR; j++)
        s->val[j] = E->par[j].def;
}

#include "gen.c"
#include "gen_perc.c"
#include "amalthea_recipes.c"
void amalthea_patch(int engine, uint32_t seed, int32_t *out) {
  memset(out,0,ENG_NPAR*sizeof(*out));
  if(engine>=3){out[0]=(int32_t)seed;return;}
  static const int old[]={E_VAP,E_PERC,E_CHIP};
  sound_t s;int e=old[engine];
  if(seed) am_sound(&s,(uint32_t)e,seed);else snd_default(&s,(uint32_t)e);
  memcpy(out,s.val,sizeof s.val);
}
const char *amalthea_family(int engine,uint32_t seed) {
  if(engine==11)return seed?amalthea_bigorna_drum_family(seed):"DEFAULT";
  if(engine>=6)return amalthea_native_family(engine,seed);
  if(engine==3)return amalthea_fiber_family(seed);
  if(engine>=4)return amalthea_bigorna_family(engine==4,seed);
  if(!seed)return "DEFAULT";
  static const int old[]={E_VAP,E_PERC,E_CHIP};return am_family((uint32_t)old[engine],seed);
}
