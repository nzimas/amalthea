/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef union { uint64_t alignment; unsigned char bytes[128]; } AmaltheaWalk;
void amalthea_walk_start(AmaltheaWalk *, uint32_t);
int amalthea_walk_next(AmaltheaWalk *, int direction, int length);
int amalthea_walk_period(int direction, int length);
const char *amalthea_direction_name(int);
const char *amalthea_direction_hint(int);
#ifdef __cplusplus
}
#endif
