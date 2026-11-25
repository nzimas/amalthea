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
/* A row's variation (val[1], nonzero): the drawn design moved part of the way toward a random value near each of its
 * timbre fields -- wave positions and the wave envelope, the levels' balance, the filter, the envelopes' times, DIRT's
 * amount, the matrix's non-pitch amounts and the rates of LFOs that never reach a pitch. Nothing of the pitch (semi,
 * det, fixp, bend, FLAVOUR, pitch slots, the ensemble), the family, the table, the filter type, DIRT's kind, the
 * resampler or the voice's level is touched; fb_measure_on then brings the level to the target as for any design. */
static uint32_t fv_hash(uint32_t x){x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
static uint32_t fv_next(uint32_t *r){uint32_t x=*r;x^=x<<13;x^=x>>17;x^=x<<5;return *r=x;}
static int32_t fv_in(uint32_t *r,int32_t a,int32_t b){return a+(int32_t)(fv_next(r)%(uint32_t)(b-a+1));}
/* half the way toward a value drawn within spread of v (and within lo .. hi) */
static int32_t fv_nudge(uint32_t *r,int32_t v,int32_t lo,int32_t hi,int32_t sp){int32_t a=v-sp>lo?v-sp:lo,b=v+sp<hi?v+sp:hi;return a>b?v:v+(fv_in(r,a,b)-v)/2;}
static int32_t fv_scale(uint32_t *r,int32_t v,int32_t lo,int32_t hi){return (int32_t)((int64_t)v*fv_in(r,lo,hi)/100);}
static int fv_pitch_dst(uint32_t d){return d==FBD_PITCH||d==FBD_PITCH1||d==FBD_PITCH2;}
static void fv_vary(fb_design_t *d,uint32_t var)
{
    uint32_t rs=fv_hash(var^fv_hash(d->seed^0x9e3779b9u)),*r=&rs,i,o,k;
    int pitchy[2]={0,0};
    if(!rs)rs=1;
    for(k=0;k<3;k++)                              /* (an LFO on a pitch, or on the speed of one that is: left as drawn) */
        for(i=0;i<FB_NSLOT;i++){uint32_t s=d->slot[i].src,t=d->slot[i].dst;
            if(s<FBS_L1||s>FBS_L2AT)continue;o=(s-FBS_L1)/3u;
            if(fv_pitch_dst(t)||(t==FBD_L1SPD&&pitchy[0])||(t==FBD_L2SPD&&pitchy[1]))pitchy[o]=1;}
    for(o=0;o<2;o++){
        if(!d->perc&&d->pos[o]<(61<<8)&&d->om[o]!=OM_STR)
            d->pos[o]=(int16_t)fv_nudge(r,d->pos[o],0,60<<8,6<<8);
        if(d->wenv[o])d->wenv[o]=(int16_t)clampi(fv_scale(r,d->wenv[o],70,130),-63<<8,63<<8);
        if(d->lvl[o]>0)d->lvl[o]=(int16_t)clampi(fv_scale(r,d->lvl[o],80,120),1,256);
        if(!pitchy[o]&&d->lhz[o]>0)d->lhz[o]=maxi(1,fv_scale(r,d->lhz[o],75,133));
    }
    d->cut=clampi(fv_nudge(r,d->cut,16*12,16*132,16*10),16*12,16*132);
    d->reso=fv_nudge(r,d->reso,0,d->reso>24000?d->reso:24000,4915);   /* (never pushed into self-oscillation: a tone) */
    if(d->fenv)d->fenv=fv_scale(r,d->fenv,75,125);
    for(k=0;k<3;k++){
        d->ea[k]=maxi(1,fv_scale(r,d->ea[k],70,140));
        d->ed[k]=maxi(1,fv_scale(r,d->ed[k],70,140));
        d->er[k]=maxi(1,fv_scale(r,d->er[k],70,135));
        if(d->es[k]>0&&d->es[k]<Q15)d->es[k]=fv_nudge(r,d->es[k],1,Q15-1,6554);
    }
    if(d->dirt!=DT_OFF&&d->damt>0)d->damt=(int16_t)fv_nudge(r,d->damt,1,256,48);
    for(i=0;i<FB_NSLOT;i++){uint32_t t=d->slot[i].dst;int32_t a;
        if(!t||fv_pitch_dst(t)||t==FBD_VOL||t==FBD_L1SPD||t==FBD_L2SPD)continue;
        a=(int32_t)d->slot[i].amt-16384;
        if(a)d->slot[i].amt=(uint16_t)clampi(16384+fv_scale(r,a,70,130),0,32768);
    }
}
static void cook(void *p,const int32_t *v){fb_design(p,-1,(uint32_t)v[0],fiber_path((uint32_t)v[0]),GEN_NOW);if(v[1])fv_vary(p,(uint32_t)v[1]);fb_voice_t probe;fb_measure_on(p,&probe);}
static void reset(void *v){fb_init(v,1);}
static void on(void *v,const void *p,int32_t n,int vel){const fb_design_t*d=p;fb_init(v,d->seed^0x77u);fb_note_on(v,p,n/65536,vel,1,0,0);}
static void off(void *v,const void *p,int32_t n){(void)p;(void)n;fb_note_off(v,64);}
static int render(void *v,const void *p,int32_t*l,int32_t*r){(void)r;return fb_render(v,p,l,ENG_BLK,6);}
_Static_assert(sizeof(fb_design_t)<=ENG_CK_MAX,"Fiber patch storage");
_Static_assert(sizeof(fb_voice_t)<=ENG_V_MAX,"Fiber voice storage");
const eng_t ENG_FIBER={.name="FIBER",.desc="DUAL WAVETABLE",.v_size=sizeof(fb_voice_t),.ck_size=sizeof(fb_design_t),.cook=cook,.reset=reset,.on=on,.off=off,.render=render};
const char *amalthea_fiber_family(uint32_t seed){fb_design_t d;fb_design(&d,-1,seed,fiber_path(seed),GEN_NOW);return FK_NAME[d.kind];}
