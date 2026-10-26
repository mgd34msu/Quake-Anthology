/*
 * Port of src/foundation/vec.ts: flat binary32 vector storage. Stored
 * writes round to binary32; dot/normalize retain the exact staged
 * rounding of src/core/math.ts. C accommodation: stores carry an
 * explicit float count (TypeScript Float32Arrays carry theirs);
 * out-of-range reads yield 0 and writes drop, as typed arrays do.
 * Intermediates compute in double with float casts at the TypeScript
 * Math.fround points, so results match bit for bit.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_VEC_H
#define QA_FOUNDATION_VEC_H

#include <stdbool.h>
#include <stddef.h>

/** Plain retained scratch for out-parameter math over engine Vec3 values. */
typedef struct qa_vec3_scratch {
  double x;
  double y;
  double z;
} qa_vec3_scratch;

qa_vec3_scratch qa_vec3_scratch_new(void);

/* Malloc'd zeroed stores; caller frees. NULL on OOM. */
float *qa_create_vec3_store(size_t capacity);
float *qa_create_bounds_store(size_t capacity);

void qa_vec3_set(float *store, size_t store_length, size_t index,
                 double x, double y, double z);
void qa_vec3_copy(float *store, size_t store_length, size_t out, size_t a);
void qa_vec3_add(float *store, size_t store_length, size_t out, size_t a, size_t b);
void qa_vec3_sub(float *store, size_t store_length, size_t out, size_t a, size_t b);
void qa_vec3_scale(float *store, size_t store_length, size_t out, size_t a, double scale);
/* out = a + scale * b, with the object path's staged binary32 rounding. */
void qa_vec3_ma(float *store, size_t store_length, size_t out, size_t a,
                double scale, size_t b);
/* Matches dot3 exactly, including each intermediate binary32 rounding. */
float qa_vec3_dot(const float *store, size_t store_length, size_t a, size_t b);
void qa_vec3_cross(float *store, size_t store_length, size_t out, size_t a, size_t b);
float qa_vec3_length(const float *store, size_t store_length, size_t a);
/* Matches normalize3: a zero vector is retained, including signed zero. */
float qa_vec3_normalize(float *store, size_t store_length, size_t out, size_t a);
/* Matches angleVectors exactly; angles are pitch/yaw/roll degrees. */
void qa_vec3_angle_vectors(float *store, size_t store_length, size_t forward,
                           size_t right, size_t up, size_t angles);
bool qa_vec3_equal(const float *store, size_t store_length, size_t a, size_t b);

void qa_bounds_set(float *store, size_t store_length, size_t index,
                   double min_x, double min_y, double min_z,
                   double max_x, double max_y, double max_z);
bool qa_bounds_overlap(const float *store, size_t store_length, size_t a, size_t b);
bool qa_bounds_contains_point(const float *store, size_t store_length, size_t index,
                              double x, double y, double z);

#endif
