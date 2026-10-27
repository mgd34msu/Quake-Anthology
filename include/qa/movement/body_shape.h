/*
 * Port of src/movement/body-shape.ts: a requested local hull expands
 * only after the selected source collision query accepts it.
 * GPL-2.0-or-later.
 */
#ifndef QA_MOVEMENT_BODY_SHAPE_H
#define QA_MOVEMENT_BODY_SHAPE_H

#include <stdbool.h>

#include "qa/contracts/math.h"

/** TS clear callback: true when the requested bounds are free. */
typedef bool (*qa_movement_bounds_clear_fn)(void *ctx, qa_bounds bounds);

qa_bounds qa_movement_bounds(qa_bounds previous, qa_bounds requested,
                             qa_movement_bounds_clear_fn clear, void *ctx);

/** TS MovementBodyShape. requested is optional (has_requested). */
typedef struct qa_movement_body_shape {
  void *ctx;
  qa_bounds current;
  bool has_requested;
  qa_bounds requested;
  void (*current_actor)(void *ctx);
} qa_movement_body_shape;

#endif
