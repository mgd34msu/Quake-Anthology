/* Port of src/compat/q2/rerelease/spatial.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/spatial.h"
#include <math.h>
#include <stdint.h>

static float fr(double v) { return (float)v; }
qa_bounds qa_compat_q2_rerelease_link_bounds(qa_body_state state, double solid) {
  qa_vec3 min = state.bounds.min, max = state.bounds.max;
  if (solid == 3 && (state.angles.x != 0 || state.angles.y != 0 || state.angles.z != 0)) {
    double extent = fabs(min.x);
    const double vals[] = {min.y, min.z, max.x, max.y, max.z};
    for (int i = 0; i < 5; i++) extent = fmax(extent, fabs(vals[i]));
    min = (qa_vec3){-extent, -extent, -extent};
    max = (qa_vec3){extent, extent, extent};
  }
  qa_bounds b;
  b.min = (qa_vec3){fr(fr(state.origin.x + min.x) - 1), fr(fr(state.origin.y + min.y) - 1), fr(fr(state.origin.z + min.z) - 1)};
  b.max = (qa_vec3){fr(fr(state.origin.x + max.x) + 1), fr(fr(state.origin.y + max.y) + 1), fr(fr(state.origin.z + max.z) + 1)};
  return b;
}
static double clamp255(double value, double minimum) { double t = trunc(value); return fmax(minimum, fmin(255, t)); }
double qa_compat_q2_rerelease_network_solid(qa_bounds bounds, double solid, double server_flags) {
  if (solid == 3) return 31;
  if (solid != 2 || ((int64_t)server_flags & 2) != 0 ||
      (bounds.min.x == bounds.max.x && bounds.min.y == bounds.max.y && bounds.min.z == bounds.max.z)) return 0;
  uint32_t packed = ((uint32_t)clamp255(fr(bounds.max.z + 32), 0) << 24) | ((uint32_t)clamp255(-bounds.min.z, 0) << 16) |
                    ((uint32_t)clamp255(bounds.max.y, 1) << 8) | (uint32_t)clamp255(bounds.max.x, 1);
  return packed == 31 ? 0 : packed;
}
