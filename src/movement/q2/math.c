/* Port of src/movement/q2/math.ts. GPL-2.0-or-later. */
#include "qa/movement/q2/math.h"

#include <math.h>

/* JS Math.PI (the nearest double). */
static const double q2_pi = 3.141592653589793;

void qa_q2_movement_math_init(qa_q2_movement_math *math, const qa_numeric_operations *operations,
                              qa_q2_movement_math_edition edition) {
  math->n = operations;
  math->edition = edition;
}

double qa_q2_math_scalar(const qa_q2_movement_math *math, double value) {
  return math->n->profile.arithmetic.kind == QA_ARITHMETIC_DONOR_BINARY64
             ? value
             : qa_q2_n_store(math->n, value);
}

void qa_q2_math_vec3(const qa_q2_movement_math *math, double x, double y, double z,
                     qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  out[0] = qa_q2_n_store(n, x);
  out[1] = qa_q2_n_store(n, y);
  out[2] = qa_q2_n_store(n, z);
}

double qa_q2_math_dot_product(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                              const qa_q2_vec3 b) {
  const qa_numeric_operations *n = math->n;
  return qa_q2_n_add(n,
                     qa_q2_n_add(n, qa_q2_n_multiply(n, a[0], b[0]),
                                 qa_q2_n_multiply(n, a[1], b[1])),
                     qa_q2_n_multiply(n, a[2], b[2]));
}

void qa_q2_math_vector_copy(const qa_q2_movement_math *math, const qa_q2_vec3 a, qa_q2_vec3 b) {
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    b[i] = qa_q2_n_store(math->n, a[i]);
  }
}

void qa_q2_math_vector_clear(const qa_q2_movement_math *math, qa_q2_vec3 a) {
  (void)math;
  a[0] = a[1] = a[2] = 0;
}

void qa_q2_math_vector_ma(const qa_q2_movement_math *math, const qa_q2_vec3 a, double scale,
                          const qa_q2_vec3 b, qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    out[i] = qa_q2_n_store(n, qa_q2_n_add(n, a[i], qa_q2_n_multiply(n, scale, b[i])));
  }
}

void qa_q2_math_vector_scale(const qa_q2_movement_math *math, const qa_q2_vec3 a, double scale,
                             qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    out[i] = qa_q2_n_store(n, qa_q2_n_multiply(n, a[i], scale));
  }
}

double qa_q2_math_vector_length(const qa_q2_movement_math *math, const qa_q2_vec3 a) {
  return qa_q2_n_square_root(math->n, qa_q2_math_dot_product(math, a, a));
}

double qa_q2_math_vector_normalize(const qa_q2_movement_math *math, qa_q2_vec3 a) {
  const double length = qa_q2_math_vector_length(math, a);
  if (qa_q2_js_truthy(length))
    qa_q2_math_vector_scale(math, a, qa_q2_n_divide(math->n, 1, length), a);
  return length;
}

void qa_q2_math_cross_product(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                              const qa_q2_vec3 b, qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  qa_q2_vec3 result;
  qa_q2_math_vec3(
      math,
      qa_q2_n_subtract(n, qa_q2_n_multiply(n, a[1], b[2]), qa_q2_n_multiply(n, a[2], b[1])),
      qa_q2_n_subtract(n, qa_q2_n_multiply(n, a[2], b[0]), qa_q2_n_multiply(n, a[0], b[2])),
      qa_q2_n_subtract(n, qa_q2_n_multiply(n, a[0], b[1]), qa_q2_n_multiply(n, a[1], b[0])),
      result);
  qa_q2_math_vector_copy(math, result, out);
}

void qa_q2_math_angle_vectors(const qa_q2_movement_math *math, const qa_q2_vec3 angles,
                              qa_q2_vec3 forward, qa_q2_vec3 right, qa_q2_vec3 up) {
  const qa_numeric_operations *n = math->n;
  const double radians = math->edition == QA_Q2_MATH_CLASSIC
                             ? q2_pi * 2 / 360
                             : qa_q2_n_divide(n, qa_q2_n_multiply(n, qa_q2_n_store(n, q2_pi), 2), 360);
  const double yaw = qa_q2_math_scalar(math, angles[1] * radians);
  const double pitch = qa_q2_math_scalar(math, angles[0] * radians);
  const double roll = qa_q2_math_scalar(math, angles[2] * radians);
  const double sy = qa_q2_math_scalar(math, sin(yaw));
  const double cy = qa_q2_math_scalar(math, cos(yaw));
  const double sp = qa_q2_math_scalar(math, sin(pitch));
  const double cp = qa_q2_math_scalar(math, cos(pitch));
  const double sr = qa_q2_math_scalar(math, sin(roll));
  const double cr = qa_q2_math_scalar(math, cos(roll));
  forward[0] = qa_q2_n_store(n, qa_q2_n_multiply(n, cp, cy));
  forward[1] = qa_q2_n_store(n, qa_q2_n_multiply(n, cp, sy));
  forward[2] = qa_q2_n_store(n, -sp);
  right[0] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_n_multiply(n, -sr, sp), cy),
                                          qa_q2_n_multiply(n, cr, sy)));
  right[1] = qa_q2_n_store(
      n, qa_q2_n_subtract(n, qa_q2_n_multiply(n, qa_q2_n_multiply(n, -sr, sp), sy),
                          qa_q2_n_multiply(n, cr, cy)));
  right[2] = qa_q2_n_store(n, qa_q2_n_multiply(n, -sr, cp));
  up[0] = qa_q2_n_store(n, qa_q2_n_add(n, qa_q2_n_multiply(n, qa_q2_n_multiply(n, cr, sp), cy),
                                       qa_q2_n_multiply(n, sr, sy)));
  up[1] = qa_q2_n_store(
      n, qa_q2_n_subtract(n, qa_q2_n_multiply(n, qa_q2_n_multiply(n, cr, sp), sy),
                          qa_q2_n_multiply(n, sr, cy)));
  up[2] = qa_q2_n_store(n, qa_q2_n_multiply(n, cr, cp));
}

void qa_q2_math_vec3_add(const qa_q2_movement_math *math, const qa_q2_vec3 a, const qa_q2_vec3 b,
                         qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  qa_q2_math_vec3(math, qa_q2_n_add(n, a[0], b[0]), qa_q2_n_add(n, a[1], b[1]),
                  qa_q2_n_add(n, a[2], b[2]), out);
}

void qa_q2_math_vec3_sub(const qa_q2_movement_math *math, const qa_q2_vec3 a, const qa_q2_vec3 b,
                         qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  qa_q2_math_vec3(math, qa_q2_n_subtract(n, a[0], b[0]), qa_q2_n_subtract(n, a[1], b[1]),
                  qa_q2_n_subtract(n, a[2], b[2]), out);
}

void qa_q2_math_vec3_muls(const qa_q2_movement_math *math, const qa_q2_vec3 a, double b,
                          qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  qa_q2_math_vec3(math, qa_q2_n_multiply(n, a[0], b), qa_q2_n_multiply(n, a[1], b),
                  qa_q2_n_multiply(n, a[2], b), out);
}

void qa_q2_math_vec3_mul_eqs(const qa_q2_movement_math *math, qa_q2_vec3 a, double scale) {
  qa_q2_math_vector_scale(math, a, scale, a);
}

void qa_q2_math_vec3_add_eq(const qa_q2_movement_math *math, qa_q2_vec3 a, const qa_q2_vec3 b) {
  const qa_numeric_operations *n = math->n;
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    a[i] = qa_q2_n_store(n, qa_q2_n_add(n, a[i], b[i]));
  }
}

double qa_q2_math_vec3_dot(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                           const qa_q2_vec3 b) {
  return qa_q2_math_dot_product(math, a, b);
}

void qa_q2_math_vec3_cross(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                           const qa_q2_vec3 b, qa_q2_vec3 out) {
  qa_q2_math_vec3(math, 0, 0, 0, out);
  qa_q2_math_cross_product(math, a, b, out);
}

double qa_q2_math_vec3_normalize(const qa_q2_movement_math *math, qa_q2_vec3 a) {
  return qa_q2_math_vector_normalize(math, a);
}

double qa_q2_math_vec3_length(const qa_q2_movement_math *math, const qa_q2_vec3 a) {
  return qa_q2_math_vector_length(math, a);
}

double qa_q2_math_vec3_length_squared(const qa_q2_movement_math *math, const qa_q2_vec3 a) {
  return qa_q2_math_dot_product(math, a, a);
}

void qa_q2_math_slide_clip_velocity(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                                    const qa_q2_vec3 normal, double overbounce, qa_q2_vec3 out) {
  const qa_numeric_operations *n = math->n;
  const double backoff =
      qa_q2_n_multiply(n, qa_q2_math_dot_product(math, a, normal),
                       qa_q2_math_scalar(math, overbounce));
  qa_q2_math_vec3(math, 0, 0, 0, out);
  for (int k = 0; k < 3; k++) {
    const int i = qa_q2_axes[k];
    out[i] = qa_q2_n_subtract(n, a[i], qa_q2_n_multiply(n, normal[i], backoff));
    if (out[i] > -qa_q2_math_scalar(math, 0.1) && out[i] < qa_q2_math_scalar(math, 0.1))
      out[i] = 0;
  }
}

void qa_q2_math_g_add_blend(const qa_q2_movement_math *math, double r, double g, double b,
                            double a, qa_q2_vec4 blend) {
  const qa_numeric_operations *n = math->n;
  if (a <= 0) return;
  const double alpha = qa_q2_n_add(n, blend[3], qa_q2_n_multiply(n, qa_q2_n_subtract(n, 1, blend[3]), a));
  const double fraction = qa_q2_n_divide(n, blend[3], alpha);
  blend[0] = qa_q2_n_add(n, qa_q2_n_multiply(n, blend[0], fraction),
                         qa_q2_n_multiply(n, r, qa_q2_n_subtract(n, 1, fraction)));
  blend[1] = qa_q2_n_add(n, qa_q2_n_multiply(n, blend[1], fraction),
                         qa_q2_n_multiply(n, g, qa_q2_n_subtract(n, 1, fraction)));
  blend[2] = qa_q2_n_add(n, qa_q2_n_multiply(n, blend[2], fraction),
                         qa_q2_n_multiply(n, b, qa_q2_n_subtract(n, 1, fraction)));
  blend[3] = alpha;
}

double qa_q2_math_short2angle(const qa_q2_movement_math *math, double value) {
  return qa_q2_n_multiply(math->n, value, 360.0 / 65536.0);
}

double qa_q2_math_clamp(const qa_q2_movement_math *math, double value, double min, double max) {
  (void)math;
  return qa_q2_js_min(qa_q2_js_max(value, min), max);
}
