/*
 * Port of src/movement/q2/math.ts: Quake II q_shared.c and rerelease
 * q_vec3.h staged arithmetic. GPL-2.0-or-later.
 * C accommodations: the TS createMovementMath factory object becomes
 * qa_q2_movement_math plus qa_q2_math_* functions; tuple-returning
 * helpers write through an out array. Every numeric operation goes
 * through the supplied NumericOperations at exactly the TS points.
 */
#ifndef QA_MOVEMENT_Q2_MATH_H
#define QA_MOVEMENT_Q2_MATH_H

#include <math.h>
#include <stdbool.h>

#include "qa/contracts/numeric.h"
#include "qa/movement/q2/types.h"

/** TS createMovementMath edition argument. */
typedef enum qa_q2_movement_math_edition {
  QA_Q2_MATH_CLASSIC,   /* "classic" (default) */
  QA_Q2_MATH_RERELEASE  /* "rerelease" */
} qa_q2_movement_math_edition;

/** TS createMovementMath result (TS field `n` is math.n here). */
typedef struct qa_q2_movement_math {
  const qa_numeric_operations *n;
  qa_q2_movement_math_edition edition;
} qa_q2_movement_math;

void qa_q2_movement_math_init(qa_q2_movement_math *math, const qa_numeric_operations *operations,
                              qa_q2_movement_math_edition edition);

static inline double qa_q2_n_store(const qa_numeric_operations *n, double v) {
  return n->store(n->ctx, v);
}
static inline double qa_q2_n_add(const qa_numeric_operations *n, double a, double b) {
  return n->add(n->ctx, a, b);
}
static inline double qa_q2_n_subtract(const qa_numeric_operations *n, double a, double b) {
  return n->subtract(n->ctx, a, b);
}
static inline double qa_q2_n_multiply(const qa_numeric_operations *n, double a, double b) {
  return n->multiply(n->ctx, a, b);
}
static inline double qa_q2_n_divide(const qa_numeric_operations *n, double a, double b) {
  return n->divide(n->ctx, a, b);
}
static inline double qa_q2_n_square_root(const qa_numeric_operations *n, double v) {
  return n->square_root(n->ctx, v);
}

/* JS Math.max/Math.min (NaN-propagating) and number truthiness. */
static inline double qa_q2_js_max(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return a == 0.0 && signbit(a) && !signbit(b) ? b : a;
  return a > b ? a : b;
}
static inline double qa_q2_js_min(double a, double b) {
  if (isnan(a) || isnan(b)) return NAN;
  if (a == b) return a == 0.0 && !signbit(a) && signbit(b) ? b : a;
  return a < b ? a : b;
}
static inline bool qa_q2_js_truthy(double value) { return value != 0.0 && value == value; }

/** TS scalar(): donor-binary64 passes through, else n.store. */
double qa_q2_math_scalar(const qa_q2_movement_math *math, double value);

void qa_q2_math_vec3(const qa_q2_movement_math *math, double x, double y, double z,
                     qa_q2_vec3 out);
double qa_q2_math_dot_product(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                              const qa_q2_vec3 b);
void qa_q2_math_vector_copy(const qa_q2_movement_math *math, const qa_q2_vec3 a, qa_q2_vec3 b);
void qa_q2_math_vector_clear(const qa_q2_movement_math *math, qa_q2_vec3 a);
void qa_q2_math_vector_ma(const qa_q2_movement_math *math, const qa_q2_vec3 a, double scale,
                          const qa_q2_vec3 b, qa_q2_vec3 out);
void qa_q2_math_vector_scale(const qa_q2_movement_math *math, const qa_q2_vec3 a, double scale,
                             qa_q2_vec3 out);
double qa_q2_math_vector_normalize(const qa_q2_movement_math *math, qa_q2_vec3 a);
double qa_q2_math_vector_length(const qa_q2_movement_math *math, const qa_q2_vec3 a);
void qa_q2_math_cross_product(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                              const qa_q2_vec3 b, qa_q2_vec3 out);
void qa_q2_math_angle_vectors(const qa_q2_movement_math *math, const qa_q2_vec3 angles,
                              qa_q2_vec3 forward, qa_q2_vec3 right, qa_q2_vec3 up);
void qa_q2_math_vec3_add(const qa_q2_movement_math *math, const qa_q2_vec3 a, const qa_q2_vec3 b,
                         qa_q2_vec3 out);
void qa_q2_math_vec3_sub(const qa_q2_movement_math *math, const qa_q2_vec3 a, const qa_q2_vec3 b,
                         qa_q2_vec3 out);
void qa_q2_math_vec3_muls(const qa_q2_movement_math *math, const qa_q2_vec3 a, double b,
                          qa_q2_vec3 out);
void qa_q2_math_vec3_mul_eqs(const qa_q2_movement_math *math, qa_q2_vec3 a, double scale);
void qa_q2_math_vec3_add_eq(const qa_q2_movement_math *math, qa_q2_vec3 a, const qa_q2_vec3 b);
/* TS vec3_dot is DotProduct; vec3_normalize/length alias the Vector* forms. */
double qa_q2_math_vec3_dot(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                           const qa_q2_vec3 b);
void qa_q2_math_vec3_cross(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                           const qa_q2_vec3 b, qa_q2_vec3 out);
double qa_q2_math_vec3_normalize(const qa_q2_movement_math *math, qa_q2_vec3 a);
double qa_q2_math_vec3_length(const qa_q2_movement_math *math, const qa_q2_vec3 a);
double qa_q2_math_vec3_length_squared(const qa_q2_movement_math *math, const qa_q2_vec3 a);
void qa_q2_math_slide_clip_velocity(const qa_q2_movement_math *math, const qa_q2_vec3 a,
                                    const qa_q2_vec3 normal, double overbounce, qa_q2_vec3 out);
void qa_q2_math_g_add_blend(const qa_q2_movement_math *math, double r, double g, double b,
                            double a, qa_q2_vec4 blend);
double qa_q2_math_short2angle(const qa_q2_movement_math *math, double value);
double qa_q2_math_clamp(const qa_q2_movement_math *math, double value, double min, double max);

#endif
