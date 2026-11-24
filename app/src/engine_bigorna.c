/* SPDX-License-Identifier: GPL-3.0-only
 * Native Bigorna HIVE, 4-OP and FM DRUM, including original filter/amp voice chain and recipes.
 * FM DRUM's machine is Amalthea's floating-point re-implementation (fmdrum.c); its recipes are Bigorna's DRUM ones.
 * The firmware UI/HAL is excluded; cooked patches and voices are preallocated. */
#define HOST 1
#define RAMTAB
#define NELEM(a) (sizeof(a)/sizeof((a)[0]))
#include <stdint.h>
#include <string.h>
#include "../third_party/bigorna/fw_tables.h"
#include "../third_party/bigorna/dsp.h"
#include "../third_party/bigorna/sound.h"
#include "../third_party/bigorna/voice.c"
#include "../third_party/bigorna/osc.c"
#include "../third_party/bigorna/fmt.c"
#include "../third_party/bigorna/wav.c"
static void str_cpy(char*d,const char*s,unsigned n){if(n){strncpy(d,s,n-1);d[n-1]=0;}}
static void lfo_off(lfo_t *l)
{
    l->spd = 80;
    l->mult = 2;
    l->fade = 64;
    l->dest = 0;
    l->wave = LW_TRI;
    l->sph = 0;
    l->mode = LM_FREE;
    l->dep = 64;
}
static void lfos_off(sound_t *s)
{
    uint32_t n;
    for (n = 0; n < NLFO; n++)
        lfo_off(&s->lfo[n]);
}
#include "../third_party/bigorna/generator_params.h"
#include "../third_party/bigorna/gen.c"
#include "eng.h"
#include "fmdrum.c"
enum {K_OP4,K_HIVE,K_DRUM};
typedef struct {sound_t sound;vck_t chain;union {ftck_t ft;swck_t sw;fdf_ck_t dr;} syn;uint32_t seed;uint8_t hive,modulated;} patch_t;
#include "../third_party/bigorna/voice_lfo.h"
typedef struct {vchain_t chain;lfo_st_t lfo;patch_t moving;uint32_t random,blocks;union {ftv_t ft;swv_t sw;fdf_v_t dr;} syn;int32_t note;int vel;} voice_t;
static const char *const DISCOVERY_NAMES[]={"IRON CHOIR","FRACTURED","COMB BODY","ARC WELD","SPECTRAL EDGE","RUSTED WIRE","FEEDBACK GLASS","NIGHT SIGNAL"};
static void discovery_sound(sound_t*s,uint32_t seed,int hive){
 if(!seed||(seed&3u)<2u)return;unsigned f=(seed>>2)%8;
 if(f==2||f==5){s->fmach=f==2?FLT_CBP:FLT_CBM;s->flt[F_RESO]=78+(seed%20);s->flt[F_KEYT]=127;s->flt[F_FREQ]=42+(seed%28);}
 else {s->flt[F_RESO]=50+seed%45;s->flt[F_ENV]=78+seed%25;}
 s->fx[X_OD]=30+seed%40;s->amp[A_VOL]=(uint8_t)clampi(s->amp[A_VOL],55,110);
 if(hive){s->syn[SW_DET]=30+seed%70;s->syn[SW_ANIM]=40+seed%75;s->syn[SW_NMOD]=f%2?15+seed%50:0;}
 else{s->syn[FT_FDBK]=40+seed%65;s->syn[FT_HARM]=30+seed%85;}
}
static void syn_cook(patch_t *c,const sound_t *s){if(c->hive==K_DRUM)fdf_cook(&c->syn.dr,s->syn);else if(c->hive==K_HIVE)sw_cook(&c->syn.sw,s);else ft_cook(&c->syn.ft,s);}
static void cook_kind(void *p,const int32_t *v,int kind){patch_t*c=p;sound_t s;gen_sound(&s,kind==K_DRUM?ENG_FMD:kind==K_HIVE?ENG_SWM:ENG_FMT,-1,-1,(uint32_t)v[0]);if(kind!=K_DRUM)discovery_sound(&s,(uint32_t)v[0],kind==K_HIVE);c->sound=s;c->modulated=0;for(unsigned i=0;i<NLFO;i++)c->modulated|=lfo_on(&s.lfo[i]);vck_cook(&c->chain,&s);c->seed=(uint32_t)v[0];c->hive=(uint8_t)kind;syn_cook(c,&s);}
static void cook_hive(void*p,const int32_t*v){cook_kind(p,v,K_HIVE);}
static void cook_op4(void*p,const int32_t*v){cook_kind(p,v,K_OP4);}
static void cook_drum(void*p,const int32_t*v){cook_kind(p,v,K_DRUM);}
static void reset(void*p){memset(p,0,sizeof(voice_t));}
static void on(void*p,const void*cp,int32_t n,int vel){voice_t*v=p;const patch_t*c=cp;v->note=n;v->vel=vel;v->moving=*c;v->random=c->seed^0x2545f491u;vc_on(&v->chain,&c->chain,1);if(c->hive==K_DRUM)fdf_on(&v->syn.dr,&c->syn.dr,c->seed^0x5eedu,1);else if(c->hive==K_HIVE)sw_on(&v->syn.sw,1,c->seed);else ft_on(&v->syn.ft,&c->syn.ft,1);}
static void off(void*p,const void*cp,int32_t n){voice_t*v=p;const patch_t*c=cp;(void)n;vc_off(&v->chain,&c->chain);if(c->hive==K_OP4)ft_off(&v->syn.ft);}
static int render(void*p,const void*cp,int32_t*l,int32_t*r){voice_t*v=p;const patch_t*c=cp;(void)r;
 if(c->modulated){
  if((v->blocks++%LFO_EVERY)==0){int32_t mod[NLFO]={0};sound_t sound=c->sound;port_lfo_step(&sound,&v->lfo,mod,&v->random,v->blocks==1);lfo_apply(&sound,mod);vck_cook(&v->moving.chain,&sound);syn_cook(&v->moving,&sound);}
  c=&v->moving;
 }
 if(c->hive==K_DRUM)fdf_render(&v->syn.dr,&c->syn.dr,v->note,l);else if(c->hive==K_HIVE)sw_render(&v->syn.sw,&c->syn.sw,v->note,l);else ft_render(&v->syn.ft,&c->syn.ft,note_inc(v->note),v->note,l);return vc_block(&v->chain,&c->chain,l,v->note,v->vel);}
_Static_assert(sizeof(patch_t)<=ENG_CK_MAX,"Bigorna patch storage");
_Static_assert(sizeof(voice_t)<=ENG_V_MAX,"Bigorna voice storage");
#define ENGINE(n,dsc,cookfn) {.name=n,.desc=dsc,.v_size=sizeof(voice_t),.ck_size=sizeof(patch_t),.cook=cookfn,.reset=reset,.on=on,.off=off,.render=render}
const eng_t ENG_HIVE=ENGINE("HIVE","SEVEN OSC SWARM",cook_hive);
const eng_t ENG_OP4=ENGINE("OP4","FOUR OPERATOR FM",cook_op4);
static void init_drum(void){fdf_init();}
const eng_t ENG_FMDRUM={.name="FM DRUM",.desc="FM DRUM MACHINE",.v_size=sizeof(voice_t),.ck_size=sizeof(patch_t),.init=init_drum,.cook=cook_drum,.reset=reset,.on=on,.off=off,.render=render};
const char *amalthea_bigorna_family(int hive,uint32_t seed){if(seed&&(seed&3u)>=2u)return DISCOVERY_NAMES[(seed>>2)%8];sound_t s;gen_sound(&s,hive?ENG_SWM:ENG_FMT,-1,-1,seed);return family_name(&s);}
const char *amalthea_bigorna_drum_family(uint32_t seed){sound_t s;gen_sound(&s,ENG_FMD,-1,-1,seed?seed:1u);return family_name(&s);}
