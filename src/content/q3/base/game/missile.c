/* Port of src/content/q3/base/game/missile.ts (snap helpers). GPL-2.0-or-later. */
#include "qa/content/q3/base/game/missile.h"
#include "qa/core/numeric.h"
qa_vec3 qa_q3_snap_vector(qa_vec3 v) {
  qa_vec3 r = {(double)qa_qvm_float_to_int(v.x), (double)qa_qvm_float_to_int(v.y), (double)qa_qvm_float_to_int(v.z)}; return r;
}
static double axis_snap(double v, double to) { return qa_int32((double)qa_qvm_float_to_int(v) + (to <= v ? 0 : 1)); }
qa_vec3 qa_q3_snap_vector_towards(qa_vec3 v, qa_vec3 toward) {
  qa_vec3 r = {axis_snap(v.x, toward.x), axis_snap(v.y, toward.y), axis_snap(v.z, toward.z)}; return r;
}
void qa_q3_missile_runtime_init(qa_q3_missile_runtime *runtime, qa_q3_missile_host host) { runtime->host = host; }
