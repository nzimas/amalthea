/* SPDX-License-Identifier: GPL-3.0-only
 * Adapter to Fugitive's playhead walks (directions). NOTE generation lives in melody.cpp. */
#define HOST 1
#define RAMTAB
#include <stdint.h>
#include <string.h>
#include "fw_tables.h"
#include "dsp.h"
#include "fugue.c"
#include "walk.h"
typedef char walk_storage_check[sizeof(fg_walk_t)<=sizeof(AmaltheaWalk)?1:-1];
void amalthea_walk_start(AmaltheaWalk *state, uint32_t seed) {
  fg_walk_t w; memset(&w,0,sizeof w); fg_walk_start(&w,seed?seed:1);
  memcpy(state,&w,sizeof w);
}
int amalthea_walk_next(AmaltheaWalk *state, int direction, int length) {
  fg_walk_t w; memcpy(&w,state,sizeof w);
  int row=fg_next(&w,direction,length); memcpy(state,&w,sizeof w); return row;
}
int amalthea_walk_period(int direction,int length) {
  if(direction==FG_DIR_PING)return length>1?2*length-2:1;
  if(direction==FG_DIR_PEND || direction==FG_DIR_DBL)return 2*length;
  if(direction==FG_DIR_ROCK)return 2*length;
  if(direction==FG_DIR_CASC)return 3*length;
  return length;
}
const char *amalthea_direction_name(int d) {return FG_DIR_NAME[d];}
const char *amalthea_direction_hint(int d) {return FG_DIR_HINT[d];}
