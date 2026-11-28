#pragma once
// LIFEBLOOD's mirrored output stage: Bigorna's own send effects (CHORUS, DELAY, REVERB), master
// compressor and output ceiling, compiled from its fx.c unchanged (third_party/bigorna). Fixed
// point, as on the FM-1: samples are int32 Q15 (1.0 = 32768), blocks of BigornaMasterBlock.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { BigornaMasterBlock = 32, BigornaChorusParams = 7, BigornaDelayParams = 8, BigornaReverbParams = 7, BigornaCompParams = 8 };
// Clear every line and state; the next bgm_set starts from its values (no glide), as fx_init does.
void bgm_reset(void);
// The FM-1's effect parameters (fx.c's order, 0..127) and its tempo x 10, as Bigorna broadcasts them.
void bgm_set(const uint8_t *chorus, const uint8_t *delay, const uint8_t *reverb, const uint8_t *comp, int bpm10);
// One block, in place on l and r (the tracks' mix): the returns of the previous block's sends added,
// each ducked from its last gain to duck[k] (Q16, 65536 none), then the compressor, then the ceiling.
// chorus, delay, reverb: this block's mono sends, rendered by the next call (as on the FM-1).
void bgm_block(int32_t *l, int32_t *r, const int32_t *chorus, const int32_t *delay, const int32_t *reverb, const int32_t *duck);
#ifdef __cplusplus
}
#endif
