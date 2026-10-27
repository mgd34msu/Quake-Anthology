/* Port of src/movement/q2/dimensions.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/dimensions.h"

const qa_bounds qa_q2_player_bounds = {{-16, -16, -24}, {16, 16, 32}};

double qa_q2_character_height(qa_bounds bounds, double source_height,
                              const qa_numeric_operations *numeric) {
  const qa_numeric_operations *n = numeric;
  return n->add(n->ctx, bounds.min.z,
                n->multiply(n->ctx, n->add(n->ctx, source_height, 24) / 56,
                            n->subtract(n->ctx, bounds.max.z, bounds.min.z)));
}
