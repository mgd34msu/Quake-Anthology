/*
 * Port of src/contracts/flare.ts (types only). GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_FLARE_H
#define QA_CONTRACTS_FLARE_H

#include <stdbool.h>

#include "qa/contracts/math.h"

typedef struct qa_scene_flare {
  const char *image;
  double fade_start, fade_end, scale;
  qa_vec3 color;
  const qa_vec3 *rim_color; /* nullable */
  bool lock_angle;
} qa_scene_flare;

#endif
