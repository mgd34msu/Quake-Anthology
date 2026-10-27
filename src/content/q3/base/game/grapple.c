/* Port of src/content/q3/base/game/grapple.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/grapple.h"
#include "qa/core/math.h"
qa_vec3 qa_q3_grapple_velocity(qa_vec3 origin, qa_vec3 point, qa_vec3 forward) {
  qa_vec3 pull = qa_sub3(qa_add3(point, qa_scale3(forward, -16)), origin);
  double distance = (float)qa_length3(pull);
  return qa_scale3(qa_normalize3(pull), distance <= 100 ? (float)(10 * distance) : QA_Q3_GRAPPLE_SPEED);
}
qa_vec3 qa_q3_grapple_target(qa_vec3 origin, qa_bounds bounds) {
  return qa_add3(origin, qa_scale3(qa_add3(bounds.min, bounds.max), 0.5));
}
bool qa_q3_get_grapple_cable(qa_vec3 origin, qa_vec3 up, qa_vec3 point, double view_height, qa_q3_grapple_cable *out) {
  qa_vec3 start = qa_add3(qa_add3(origin, qa_math_vec3(0, 0, view_height)), qa_scale3(up, -6));
  if (qa_length3(qa_sub3(start, point)) < 64) return false;
  out->start = start; out->end = point; return true;
}
