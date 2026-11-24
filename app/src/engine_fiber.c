/* SPDX-License-Identifier: GPL-3.0-only
 * Native 44.1kHz/32-frame adapter for Nuno Zimas's Fiber build 28.
 * Patch generation is producer-thread only; voice state is private and preallocated. */
#define HOST 1
#define RAMTAB
#define TABLE_FAST
#define NELEM(a) (sizeof(a)/sizeof((a)[0]))
#include <stdint.h>
#include <string.h>
#include "../third_party/fiber/fw_tables.h"
#include "../third_party/fiber/dsp.h"
#include "../third_party/fiber/fiber.c"
#include "eng.h"
/* Half the draws experimental, a quarter ambient, a quarter open (keys, organ, poly synth live only there). */
static int fiber_path(uint32_t seed){static const int p[4]={PATH_AMBIENT,PATH_EXP,PATH_EXP,PATH_OPEN};return p[seed&3u];}
static void cook(void *p,const int32_t *v){fb_design(p,-1,(uint32_t)v[0],fiber_path((uint32_t)v[0]),GEN_NOW);fb_voice_t probe;fb_measure_on(p,&probe);}
static void reset(void *v){fb_init(v,1);}
static void on(void *v,const void *p,int32_t n,int vel){const fb_design_t*d=p;fb_init(v,d->seed^0x77u);fb_note_on(v,p,n/65536,vel,1,0,0);}
static void off(void *v,const void *p,int32_t n){(void)p;(void)n;fb_note_off(v,64);}
static int render(void *v,const void *p,int32_t*l,int32_t*r){(void)r;return fb_render(v,p,l,ENG_BLK,6);}
_Static_assert(sizeof(fb_design_t)<=ENG_CK_MAX,"Fiber patch storage");
_Static_assert(sizeof(fb_voice_t)<=ENG_V_MAX,"Fiber voice storage");
const eng_t ENG_FIBER={.name="FIBER",.desc="DUAL WAVETABLE",.v_size=sizeof(fb_voice_t),.ck_size=sizeof(fb_design_t),.cook=cook,.reset=reset,.on=on,.off=off,.render=render};
const char *amalthea_fiber_family(uint32_t seed){fb_design_t d;fb_design(&d,-1,seed,fiber_path(seed),GEN_NOW);return FK_NAME[d.kind];}
