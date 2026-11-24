// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
namespace amalthea {
// Pattern generator for the NOTE column. Exactly `count` of the first `length`
// rows (<= 16) receive a note >= 0; other rows are -1 (empty) or -3 (tie).
// Pitched output is always in the given root/scale. With kit=true the rows use
// the PERC kit's note map (60 kick, 62 snare, 64/66 hats, 68 cymbal, 70 perc,
// other notes toms) and are not scale-quantised.
struct MelodyRequest {
  uint32_t seed = 1;
  int length = 16, count = 8;
  int root = 0;
  uint16_t scaleMask = 0xfff;
  bool kit = false;
  // FM DRUM: one drum voice; a rhythm around note 60 (its recipes' design
  // pitch), mostly on one pitch with in-scale accents.
  bool hits = false;
  // EUCLIDEAN mode: the onsets are exactly a Euclidean rhythm of `count` in `length`.
  bool euclid = false;
};
// Rows of a Euclidean rhythm: `count` onsets spread as evenly as possible over
// `length` rows, rotated by `rotation`.
void euclideanRows(int length, int count, int rotation, bool *on);
// Returns the name of the drawn idiom (for the status line).
const char *generateMelody(const MelodyRequest &, int16_t *notes);
} // namespace amalthea
