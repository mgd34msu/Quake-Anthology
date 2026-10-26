/* Port of src/foundation/vec.ts. GPL-2.0-or-later. */
#include "qa/foundation/vec.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#include "qa/foundation/support.h"

/* Math.fround: round a binary64 to the nearest binary32 value. */
static float fr(double value) { return (float)value; }

static double read_at(const float *store, size_t store_length, size_t offset) {
  return offset < store_length ? (double)store[offset] : 0.0;
}

static void write_at(float *store, size_t store_length, size_t offset, double value) {
  if (offset < store_length) store[offset] = (float)value;
}

qa_vec3_scratch qa_vec3_scratch_new(void) {
  qa_vec3_scratch scratch = {0, 0, 0};
  return scratch;
}

float *qa_create_vec3_store(size_t capacity) {
  if (capacity > (size_t)QA_MAX_SAFE_INTEGER) {
    qa_range_error("Vector store capacity must be a nonnegative safe integer");
  }
  if (capacity != 0 && capacity > SIZE_MAX / 3) return NULL;
  return (float *)calloc(capacity == 0 ? 1 : capacity * 3, sizeof(float));
}

float *qa_create_bounds_store(size_t capacity) {
  if (capacity > (size_t)QA_MAX_SAFE_INTEGER) {
    qa_range_error("Bounds store capacity must be a nonnegative safe integer");
  }
  if (capacity != 0 && capacity > SIZE_MAX / 6) return NULL;
  return (float *)calloc(capacity == 0 ? 1 : capacity * 6, sizeof(float));
}

void qa_vec3_set(float *store, size_t store_length, size_t index,
                 double x, double y, double z) {
  size_t offset = index * 3;
  write_at(store, store_length, offset, x);
  write_at(store, store_length, offset + 1, y);
  write_at(store, store_length, offset + 2, z);
}

void qa_vec3_copy(float *store, size_t store_length, size_t out, size_t a) {
  size_t from = a * 3;
  double x = read_at(store, store_length, from);
  double y = read_at(store, store_length, from + 1);
  double z = read_at(store, store_length, from + 2);
  qa_vec3_set(store, store_length, out, x, y, z);
}

void qa_vec3_add(float *store, size_t store_length, size_t out, size_t a, size_t b) {
  size_t ao = a * 3, bo = b * 3;
  double x = read_at(store, store_length, ao) + read_at(store, store_length, bo);
  double y = read_at(store, store_length, ao + 1) + read_at(store, store_length, bo + 1);
  double z = read_at(store, store_length, ao + 2) + read_at(store, store_length, bo + 2);
  qa_vec3_set(store, store_length, out, x, y, z);
}

void qa_vec3_sub(float *store, size_t store_length, size_t out, size_t a, size_t b) {
  size_t ao = a * 3, bo = b * 3;
  double x = read_at(store, store_length, ao) - read_at(store, store_length, bo);
  double y = read_at(store, store_length, ao + 1) - read_at(store, store_length, bo + 1);
  double z = read_at(store, store_length, ao + 2) - read_at(store, store_length, bo + 2);
  qa_vec3_set(store, store_length, out, x, y, z);
}

void qa_vec3_scale(float *store, size_t store_length, size_t out, size_t a, double scale) {
  size_t ao = a * 3;
  double x = read_at(store, store_length, ao) * scale;
  double y = read_at(store, store_length, ao + 1) * scale;
  double z = read_at(store, store_length, ao + 2) * scale;
  qa_vec3_set(store, store_length, out, x, y, z);
}

void qa_vec3_ma(float *store, size_t store_length, size_t out, size_t a,
                double scale, size_t b) {
  size_t ao = a * 3, bo = b * 3;
  double s = (double)fr(scale);
  double x = read_at(store, store_length, ao) + s * read_at(store, store_length, bo);
  double y = read_at(store, store_length, ao + 1) + s * read_at(store, store_length, bo + 1);
  double z = read_at(store, store_length, ao + 2) + s * read_at(store, store_length, bo + 2);
  qa_vec3_set(store, store_length, out, x, y, z);
}

float qa_vec3_dot(const float *store, size_t store_length, size_t a, size_t b) {
  size_t ao = a * 3, bo = b * 3;
  return fr((double)fr((double)fr(read_at(store, store_length, ao) *
                             read_at(store, store_length, bo)) +
                        (double)fr(read_at(store, store_length, ao + 1) *
                                   read_at(store, store_length, bo + 1))) +
            (double)fr(read_at(store, store_length, ao + 2) *
                       read_at(store, store_length, bo + 2)));
}

void qa_vec3_cross(float *store, size_t store_length, size_t out, size_t a, size_t b) {
  size_t ao = a * 3, bo = b * 3;
  double ax = read_at(store, store_length, ao);
  double ay = read_at(store, store_length, ao + 1);
  double az = read_at(store, store_length, ao + 2);
  double bx = read_at(store, store_length, bo);
  double by = read_at(store, store_length, bo + 1);
  double bz = read_at(store, store_length, bo + 2);
  qa_vec3_set(store, store_length, out,
              (double)fr(ay * bz) - (double)fr(az * by),
              (double)fr(az * bx) - (double)fr(ax * bz),
              (double)fr(ax * by) - (double)fr(ay * bx));
}

float qa_vec3_length(const float *store, size_t store_length, size_t a) {
  return fr(sqrt((double)qa_vec3_dot(store, store_length, a, a)));
}

float qa_vec3_normalize(float *store, size_t store_length, size_t out, size_t a) {
  float length = qa_vec3_length(store, store_length, a);
  if (length == 0.0f) {
    if (out != a) qa_vec3_copy(store, store_length, out, a);
    return length;
  }
  qa_vec3_scale(store, store_length, out, a, (double)fr(1.0 / (double)length));
  return length;
}

static const double DEGREES_TO_RADIANS = 3.141592653589793 * 2.0 / 360.0;

void qa_vec3_angle_vectors(float *store, size_t store_length, size_t forward,
                           size_t right, size_t up, size_t angles) {
  size_t ao = angles * 3;
  double pitch_degrees = read_at(store, store_length, ao);
  double yaw_degrees = read_at(store, store_length, ao + 1);
  double roll_degrees = read_at(store, store_length, ao + 2);
  float yaw = fr(yaw_degrees * DEGREES_TO_RADIANS);
  float pitch = fr(pitch_degrees * DEGREES_TO_RADIANS);
  float roll = fr(roll_degrees * DEGREES_TO_RADIANS);
  float sy = fr(sin((double)yaw));
  float cy = fr(cos((double)yaw));
  float sp = fr(sin((double)pitch));
  float cp = fr(cos((double)pitch));
  float sr = fr(sin((double)roll));
  float cr = fr(cos((double)roll));
  qa_vec3_set(store, store_length, forward,
              (double)cp * (double)cy, (double)cp * (double)sy, -(double)sp);
  qa_vec3_set(store, store_length, right,
              (double)fr((double)fr(-(double)sr * (double)sp) * (double)cy) +
                  (double)fr(-(double)cr * -(double)sy),
              (double)fr((double)fr(-(double)sr * (double)sp) * (double)sy) +
                  (double)fr(-(double)cr * (double)cy),
              -(double)sr * (double)cp);
  qa_vec3_set(store, store_length, up,
              (double)fr((double)fr((double)cr * (double)sp) * (double)cy) +
                  (double)fr(-(double)sr * -(double)sy),
              (double)fr((double)fr((double)cr * (double)sp) * (double)sy) +
                  (double)fr(-(double)sr * (double)cy),
              (double)cr * (double)cp);
}

bool qa_vec3_equal(const float *store, size_t store_length, size_t a, size_t b) {
  size_t ao = a * 3, bo = b * 3;
  for (size_t i = 0; i < 3; i++) {
    bool a_in = ao + i < store_length;
    bool b_in = bo + i < store_length;
    if (!a_in && !b_in) continue;
    if (!a_in || !b_in) return false;
    /* JavaScript === : -0 equals 0, NaN equals nothing. */
    if (!((double)store[ao + i] == (double)store[bo + i])) return false;
  }
  return true;
}

void qa_bounds_set(float *store, size_t store_length, size_t index,
                   double min_x, double min_y, double min_z,
                   double max_x, double max_y, double max_z) {
  size_t offset = index * 6;
  write_at(store, store_length, offset, min_x);
  write_at(store, store_length, offset + 1, min_y);
  write_at(store, store_length, offset + 2, min_z);
  write_at(store, store_length, offset + 3, max_x);
  write_at(store, store_length, offset + 4, max_y);
  write_at(store, store_length, offset + 5, max_z);
}

bool qa_bounds_overlap(const float *store, size_t store_length, size_t a, size_t b) {
  size_t ao = a * 6, bo = b * 6;
  return read_at(store, store_length, ao) <= read_at(store, store_length, bo + 3) &&
         read_at(store, store_length, ao + 3) >= read_at(store, store_length, bo) &&
         read_at(store, store_length, ao + 1) <= read_at(store, store_length, bo + 4) &&
         read_at(store, store_length, ao + 4) >= read_at(store, store_length, bo + 1) &&
         read_at(store, store_length, ao + 2) <= read_at(store, store_length, bo + 5) &&
         read_at(store, store_length, ao + 5) >= read_at(store, store_length, bo + 2);
}

bool qa_bounds_contains_point(const float *store, size_t store_length, size_t index,
                              double x, double y, double z) {
  size_t offset = index * 6;
  return x >= read_at(store, store_length, offset) &&
         x <= read_at(store, store_length, offset + 3) &&
         y >= read_at(store, store_length, offset + 1) &&
         y <= read_at(store, store_length, offset + 4) &&
         z >= read_at(store, store_length, offset + 2) &&
         z <= read_at(store, store_length, offset + 5);
}
