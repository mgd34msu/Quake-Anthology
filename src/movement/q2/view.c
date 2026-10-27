/* Port of src/movement/q2/view.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/view.h"

#include <stdint.h>

#include "qa/core/numeric.h"
#include "qa/movement/q2/math.h"

static void clamp_pitch(qa_q2_vec3 view, const qa_numeric_operations *numeric) {
  if (view[0] > 89 && view[0] < 180) view[0] = qa_q2_n_store(numeric, 89);
  else if (view[0] < 271 && view[0] >= 180) view[0] = qa_q2_n_store(numeric, 271);
}

/* TS (word << 16) >> 16 over the ToInt32 of the numeric sum. */
static double short_word(double value) {
  return (double)(int16_t)(uint16_t)(uint32_t)qa_int32(value);
}

void qa_q2_classic_view_angles(qa_q2_vec3 view, const qa_q2_vec3 angles, const qa_q2_vec3 delta,
                               double flags, const qa_numeric_operations *numeric) {
  const qa_numeric_operations *n = numeric;
  if (qa_int32(flags) & QA_Q2_PMF_TIME_TELEPORT) {
    view[1] = qa_q2_n_store(
        n, qa_q2_n_multiply(n, qa_q2_n_add(n, angles[1], delta[1]), 360.0 / 65536.0));
    view[0] = qa_q2_n_store(n, 0);
    view[2] = qa_q2_n_store(n, 0);
  } else {
    for (int k = 0; k < 3; k++) {
      const int axis = qa_q2_axes[k];
      const double word = short_word(qa_q2_n_add(
          n, qa_q2_element(angles, 3, axis), qa_q2_element(delta, 3, axis)));
      view[axis] = qa_q2_n_store(n, qa_q2_n_multiply(n, word, 360.0 / 65536.0));
    }
    clamp_pitch(view, numeric);
  }
}

void qa_q2_rerelease_view_angles(qa_q2_vec3 view, const qa_q2_vec3 angles, const qa_q2_vec3 delta,
                                 double flags, const qa_numeric_operations *numeric) {
  const qa_numeric_operations *n = numeric;
  if (qa_int32(flags) & QA_Q2_PMF_TIME_TELEPORT) {
    view[1] = qa_q2_n_store(n, qa_q2_n_add(n, angles[1], delta[1]));
    view[0] = qa_q2_n_store(n, 0);
    view[2] = qa_q2_n_store(n, 0);
  } else {
    for (int k = 0; k < 3; k++) {
      const int axis = qa_q2_axes[k];
      view[axis] = qa_q2_n_store(
          n, qa_q2_n_add(n, qa_q2_element(angles, 3, axis), qa_q2_element(delta, 3, axis)));
    }
    clamp_pitch(view, numeric);
  }
}
