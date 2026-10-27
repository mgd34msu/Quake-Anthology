/* Port of src/movement/body-shape.ts. GPL-2.0-or-later. */
#include "qa/movement/body_shape.h"

qa_bounds qa_movement_bounds(qa_bounds previous, qa_bounds requested,
                             qa_movement_bounds_clear_fn clear, void *ctx) {
  const bool expands = requested.min.x < previous.min.x || requested.min.y < previous.min.y ||
                       requested.min.z < previous.min.z || requested.max.x > previous.max.x ||
                       requested.max.y > previous.max.y || requested.max.z > previous.max.z;
  return expands && !clear(ctx, requested) ? previous : requested;
}
