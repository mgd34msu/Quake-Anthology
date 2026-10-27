/*
 * Port of src/core/math.ts: staged-binary32 and double vector/angle/
 * matrix routines. Every Math.fround computes in double and casts to
 * float at the same point; scalar results the TS holds as binary32
 * numbers are returned as double holding that binary32 value.
 * C accommodations:
 *  - Vec3/Vec4/etc. are the contracts structs (double fields); the *Into
 *    helpers write through qa_vec3 * scratch and snapshot inputs first,
 *    exactly as the TS locals do.
 *  - MutableVec3 in createMutableVectorMath may be a guest-backed
 *    Float32VectorView whose overlapping writes are observable, so
 *    those functions take qa_vec3_access (get/set + ctx) and read/write
 *    in the TS component order without snapshotting.
 *  - planeFromPoints' null becomes a false return; colorBytes3's
 *    DataView becomes a caller byte buffer + length.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_MATH_H
#define QA_CORE_MATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"

qa_vec2 qa_math_vec2(double x, double y);
qa_vec3 qa_math_vec3(double x, double y, double z);
qa_vec4 qa_math_vec4(double x, double y, double z, double w);

qa_vec3 qa_add3(qa_vec3 a, qa_vec3 b);
void qa_add3_into(qa_vec3 *out, qa_vec3 a, qa_vec3 b);
qa_vec3 qa_sub3(qa_vec3 a, qa_vec3 b);
void qa_sub3_into(qa_vec3 *out, qa_vec3 a, qa_vec3 b);
qa_vec3 qa_scale3(qa_vec3 value, double scale);
void qa_scale3_into(qa_vec3 *out, qa_vec3 value, double scale);
double qa_dot3(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_cross3(qa_vec3 a, qa_vec3 b);
void qa_cross3_into(qa_vec3 *out, qa_vec3 a, qa_vec3 b);
double qa_length3(qa_vec3 value);
qa_vec3 qa_normalize3(qa_vec3 value);
qa_vec3 qa_normalize3_or_zero(qa_vec3 value);
double qa_normalize3_into(qa_vec3 *out, qa_vec3 value);
double qa_normalize3_or_zero_into(qa_vec3 *out, qa_vec3 value);

/* Arguments must be signed 32-bit integers (TS RangeError). */
int32_t qa_clamp_short(double value);
int32_t qa_q_log2(double value);

/* Returns the retained maximum; writes normalized color to *output. */
double qa_normalize_color(qa_vec3 input, qa_vec3 *output);
/* Writes 3 bytes into storage (length >= 4, TS RangeError otherwise)
 * and returns the little-endian 32-bit word including storage[3]. */
uint32_t qa_color_bytes3(double red, double green, double blue,
                         uint8_t *storage, size_t storage_length);

qa_vec3 qa_lerp3(qa_vec3 from, qa_vec3 to, double fraction);
void qa_lerp3_into(qa_vec3 *out, qa_vec3 from, qa_vec3 to, double fraction);

qa_vec3 qa_add3_double(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_sub3_double(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_scale3_double(qa_vec3 value, double scale);
qa_vec3 qa_ma3_double(qa_vec3 base, double scale, qa_vec3 direction);
double qa_dot3_double(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_cross3_double(qa_vec3 a, qa_vec3 b);
double qa_length3_double(qa_vec3 value);
qa_vec3 qa_lerp3_double(qa_vec3 from, qa_vec3 to, double fraction);
qa_vec3 qa_cross3_stored(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_normalize3_stored(qa_vec3 value);
qa_vec3 qa_normalize3_stored_or_zero(qa_vec3 value);
double qa_hypot3(qa_vec3 value);
double qa_distance3_double(qa_vec3 a, qa_vec3 b);
qa_vec3 qa_normalize3_double_hypot(qa_vec3 value);
qa_vec3 qa_normalize3_double_or_zero(qa_vec3 value);

/* TS AngleMathRule. */
typedef enum qa_angle_math_rule {
  QA_ANGLE_RULE_FLOAT,  /* "float" */
  QA_ANGLE_RULE_VM,     /* "vm" */
  QA_ANGLE_RULE_DONOR,  /* "donor" */
  QA_ANGLE_RULE_Q1,     /* "q1" */
  QA_ANGLE_RULE_NATIVE, /* "native" */
  QA_ANGLE_RULE_BOT     /* "bot" */
} qa_angle_math_rule;

void qa_angle_vectors_rule_into(qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up,
                                qa_vec3 angles, qa_angle_math_rule rule);
qa_angle_vectors qa_math_angle_vectors_rule(qa_vec3 angles, qa_angle_math_rule rule);
void qa_angle_vectors_into(qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up, qa_vec3 angles);
qa_angle_vectors qa_math_angle_vectors(qa_vec3 angles);
qa_axis qa_angles_to_axis(qa_vec3 angles);
void qa_angles_to_axis_into(qa_vec3 *forward, qa_vec3 *left, qa_vec3 *up, qa_vec3 angles);

/* TS VectorToAnglesRule. */
typedef enum qa_vector_to_angles_rule {
  QA_VECTOR_TO_ANGLES_FLOAT,     /* "float" */
  QA_VECTOR_TO_ANGLES_NATIVE,    /* "native" */
  QA_VECTOR_TO_ANGLES_NATIVE_AI, /* "native-ai" */
  QA_VECTOR_TO_ANGLES_BOT,       /* "bot" */
  QA_VECTOR_TO_ANGLES_BOT_Q3     /* "bot-q3" */
} qa_vector_to_angles_rule;

qa_vec3 qa_vector_to_angles_with_rule(qa_vec3 value, qa_vector_to_angles_rule rule);
qa_vec3 qa_vector_to_angles(qa_vec3 value);

double qa_angle_mod(double angle);
double qa_angle_mod_scaled(double angle);
double qa_qvm_angle_mod(double angle);
qa_angle_vectors qa_qvm_angle_vectors(qa_vec3 angles);
qa_axis qa_qvm_angles_to_axis(qa_vec3 angles);
double qa_angle_normalize360(double angle);
double qa_angle_normalize180(double angle);
double qa_angle_delta(double angle1, double angle2);

void qa_project_point_on_plane_into(qa_vec3 *out, qa_vec3 point, qa_vec3 normal);
qa_vec3 qa_project_point_on_plane(qa_vec3 point, qa_vec3 normal);
void qa_perpendicular_vector_into(qa_vec3 *out, qa_vec3 source);
qa_vec3 qa_perpendicular_vector(qa_vec3 source);

/* TS rotate rule: "float" | "vm" (reuses the angle-rule spelling). */
typedef enum qa_rotate_rule { QA_ROTATE_FLOAT, QA_ROTATE_VM } qa_rotate_rule;
void qa_rotate_point_around_vector_rule_into(qa_vec3 *out, qa_vec3 direction,
                                             qa_vec3 point, double degrees,
                                             qa_rotate_rule rule);
void qa_rotate_point_around_vector_into(qa_vec3 *out, qa_vec3 direction,
                                        qa_vec3 point, double degrees);
qa_vec3 qa_qvm_rotate_point_around_vector(qa_vec3 direction, qa_vec3 point, double degrees);
qa_vec3 qa_rotate_point_around_vector(qa_vec3 direction, qa_vec3 point, double degrees);

bool qa_math_bounds_overlap(qa_bounds a, qa_bounds b);
qa_bounds qa_empty_bounds(void);
qa_bounds qa_add_point_to_bounds(qa_bounds bounds, qa_vec3 point);
double qa_radius_from_bounds(qa_bounds bounds);
/* False is the TS null (degenerate triangle). */
bool qa_plane_from_points(qa_plane *out, qa_vec3 a, qa_vec3 b, qa_vec3 c);
double qa_distance_to_plane(qa_plane plane, qa_vec3 point);
int qa_box_on_plane_side(qa_bounds bounds, qa_plane plane);

qa_mat4 qa_identity_mat4(void);
qa_mat4 qa_multiply_mat4(qa_mat4 a, qa_mat4 b);
qa_mat4 qa_perspective_mat4(double vertical_fov_degrees, double aspect,
                            double near_plane, double far_plane);
qa_mat4 qa_look_at_mat4(qa_vec3 eye, qa_vec3 target, qa_vec3 up);
qa_vec4 qa_transform_vec4(qa_mat4 matrix, qa_vec4 value);

/** TS Float32VectorView over caller guest bytes (little-endian). */
typedef struct qa_float32_vector_view {
  uint8_t *bytes; /* 12 bytes at the view's base */
} qa_float32_vector_view;

/* byte_offset must leave 12 bytes within the buffer (TS RangeError). */
void qa_float32_vector_view_init(qa_float32_vector_view *view, uint8_t *data,
                                 size_t data_length, int64_t byte_offset);
double qa_float32_vector_view_get(const qa_float32_vector_view *view, int component);
void qa_float32_vector_view_set(qa_float32_vector_view *view, int component, double value);
qa_vec3 qa_float32_vector_view_value(const qa_float32_vector_view *view);

/** TS mutableVec3(): retained 12-byte binary32 storage + its view. */
typedef struct qa_mutable_vec3_storage {
  uint8_t bytes[12];
  qa_float32_vector_view view;
} qa_mutable_vec3_storage;

void qa_mutable_vec3_storage_init(qa_mutable_vec3_storage *storage,
                                  double x, double y, double z);

/**
 * Vec3/MutableVec3 access for createMutableVectorMath: component is
 * 0=x, 1=y, 2=z. Plain access reads/writes a qa_vec3's double fields;
 * view access goes through guest binary32 storage, preserving the
 * overlapping-write behavior the TS macros expose.
 */
typedef struct qa_vec3_access {
  void *ctx;
  double (*get)(void *ctx, int component);
  void (*set)(void *ctx, int component, double value);
} qa_vec3_access;

qa_vec3_access qa_vec3_access_plain(qa_vec3 *value);
qa_vec3_access qa_vec3_access_view(qa_float32_vector_view *view);

/* TS createMutableVectorMath zeroLengthOutput. */
typedef enum qa_zero_length_output {
  QA_ZERO_LENGTH_PRESERVE, /* "preserve" */
  QA_ZERO_LENGTH_CLEAR     /* "clear" */
} qa_zero_length_output;

typedef struct qa_mutable_vector_math {
  const qa_numeric_operations *operations;
  bool donor_arithmetic;
  qa_zero_length_output zero_length_output;
} qa_mutable_vector_math;

void qa_create_mutable_vector_math(qa_mutable_vector_math *math,
                                   const qa_numeric_operations *operations,
                                   qa_zero_length_output zero_length_output);
/* The TS q1DonorMath / q2DonorMath retained instances. */
const qa_mutable_vector_math *qa_q1_donor_math(void);
const qa_mutable_vector_math *qa_q2_donor_math(void);

double qa_mvm_dot_product(const qa_mutable_vector_math *m, qa_vec3_access a, qa_vec3_access b);
void qa_mvm_vector_set(const qa_mutable_vector_math *m, qa_vec3_access out,
                       double x, double y, double z);
void qa_mvm_vector_copy(const qa_mutable_vector_math *m, qa_vec3_access in, qa_vec3_access out);
void qa_mvm_vector_add(const qa_mutable_vector_math *m, qa_vec3_access a,
                       qa_vec3_access b, qa_vec3_access out);
void qa_mvm_vector_subtract(const qa_mutable_vector_math *m, qa_vec3_access a,
                            qa_vec3_access b, qa_vec3_access out);
void qa_mvm_vector_scale(const qa_mutable_vector_math *m, qa_vec3_access in,
                         double scale, qa_vec3_access out);
void qa_mvm_vector_ma(const qa_mutable_vector_math *m, qa_vec3_access a, double scale,
                      qa_vec3_access b, qa_vec3_access out);
void qa_mvm_cross_product(const qa_mutable_vector_math *m, qa_vec3_access a,
                          qa_vec3_access b, qa_vec3_access out);
double qa_mvm_vector_length(const qa_mutable_vector_math *m, qa_vec3_access in);
double qa_mvm_vector_normalize(const qa_mutable_vector_math *m, qa_vec3_access in);
double qa_mvm_vector_normalize2(const qa_mutable_vector_math *m, qa_vec3_access in,
                                qa_vec3_access out);
void qa_mvm_vector_clear(const qa_mutable_vector_math *m, qa_vec3_access out);
void qa_mvm_vector_negate(const qa_mutable_vector_math *m, qa_vec3_access in, qa_vec3_access out);
void qa_mvm_vector_inverse(const qa_mutable_vector_math *m, qa_vec3_access in);
int qa_mvm_vector_compare(const qa_mutable_vector_math *m, qa_vec3_access a, qa_vec3_access b);
void qa_mvm_project_point_on_plane(const qa_mutable_vector_math *m, qa_vec3_access out,
                                   qa_vec3_access point, qa_vec3_access normal);
void qa_mvm_perpendicular_vector(const qa_mutable_vector_math *m, qa_vec3_access out,
                                 qa_vec3_access source);
void qa_mvm_clear_bounds(const qa_mutable_vector_math *m, qa_vec3_access min, qa_vec3_access max);
void qa_mvm_add_point_to_bounds(const qa_mutable_vector_math *m, qa_vec3_access point,
                                qa_vec3_access min, qa_vec3_access max);

/* Donor entry points (also fields of q1DonorMath/q2DonorMath in TS).
 * Any of forward/right/up may be NULL (shared scratch, as in the TS). */
void qa_donor_angle_vectors(qa_vec3 angles, qa_vec3 *forward, qa_vec3 *right, qa_vec3 *up);
void qa_donor_concat_rotations(const qa_vec3 first[3], const qa_vec3 second[3], qa_vec3 output[3]);
void qa_donor_rotate_point_around_vector(qa_vec3 *out, qa_vec3 direction,
                                         qa_vec3 point, double degrees);

/* TS LerpAngleRule. */
typedef enum qa_lerp_angle_rule {
  QA_LERP_ANGLE_NATIVE, /* "native" */
  QA_LERP_ANGLE_DELTA,  /* "delta" */
  QA_LERP_ANGLE_FLOAT   /* "float" */
} qa_lerp_angle_rule;

double qa_lerp_angle_with_rule(double from, double to, double fraction, qa_lerp_angle_rule rule);
double qa_lerp_angle(double from, double to, double fraction);
qa_vec3 qa_lerp_angles_wrapped(qa_vec3 a, qa_vec3 b, double fraction);

typedef struct qa_floor_div_mod_result {
  double quotient;
  double remainder;
} qa_floor_div_mod_result;

/* Denominator <= 0 is the TS RangeError. */
qa_floor_div_mod_result qa_floor_div_mod(double numerator, double denominator);
double qa_greatest_common_divisor(double first, double second);

#endif
