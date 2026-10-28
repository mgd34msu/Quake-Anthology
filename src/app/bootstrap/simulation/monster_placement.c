/* Port of src/app/bootstrap/simulation/monster-placement.ts:
 * offsetsWithin grid; placement traces ride landed trace queries at
 * the monster-runtime call site. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include <math.h>
#include <stdlib.h>
#include "qa/foundation/support.h"
bool qa_app_sim_monster_same_vector(qa_vec3 left, qa_vec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}
static int cmp_offset(const void *a, const void *b) {
  const qa_app_sim_placement_offset *x = a, *y = b;
  double dx = (x->x * x->x + x->y * x->y) - (y->x * y->x + y->y * y->y);
  if (dx != 0) return dx < 0 ? -1 : 1;
  if (x->y != y->y) return x->y < y->y ? -1 : 1;
  if (x->x != y->x) return x->x < y->x ? -1 : 1;
  return 0;
}
qa_app_sim_placement_offset *qa_app_sim_placement_offsets_within(double radius, size_t *out_count) {
  size_t cap = 16, n = 0; qa_app_sim_placement_offset *out = malloc(cap * sizeof *out);
  if (out == NULL) qa_range_error("Out of memory");
  long lim = (long)ceil(radius / 4);
  for (long x = -lim; x <= lim; x++) for (long y = -lim; y <= lim; y++) {
    if ((double)(x * x + y * y) * 16 <= radius * radius) {
      if (n == cap) { cap *= 2; out = realloc(out, cap * sizeof *out); if (out == NULL) qa_range_error("Out of memory"); }
      out[n].x = (double)(x * 4); out[n].y = (double)(y * 4); n++;
    }
  }
  qsort(out, n, sizeof *out, cmp_offset);
  *out_count = n; return out;
}
