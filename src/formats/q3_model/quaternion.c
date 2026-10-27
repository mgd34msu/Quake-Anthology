/* Port of src/formats/q3-model/quaternion.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/quaternion.h"

#include <math.h>

#include "qa/core/math.h"

#define FR(value) ((double)(float)(value))

qa_vec4 qa_md5_quaternion(qa_vec3 value) {
  double square = 1 - value.x * value.x - value.y * value.y - value.z * value.z;
  return qa_math_vec4(value.x, value.y, value.z, square < 0 ? 0 : -sqrt(square));
}

qa_vec4 qa_conjugate_quaternion(qa_vec4 value) {
  return qa_math_vec4(-value.x, -value.y, -value.z, value.w);
}

qa_vec4 qa_multiply_quaternion(qa_vec4 a, qa_vec4 b) {
  return qa_math_vec4(a.x * b.w + a.w * b.x + a.y * b.z - a.z * b.y,
                      a.y * b.w + a.w * b.y + a.z * b.x - a.x * b.z,
                      a.z * b.w + a.w * b.z + a.x * b.y - a.y * b.x,
                      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

qa_vec4 qa_normalize_quaternion(qa_vec4 value) {
  double length = sqrt(value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w);
  if (length == 0) return value;
  return qa_math_vec4(value.x / length, value.y / length, value.z / length, value.w / length);
}

qa_vec3 qa_rotate_quaternion(qa_vec4 orientation, qa_vec3 position) {
  qa_vec4 as_quat = {position.x, position.y, position.z, 0};
  qa_vec4 product = qa_multiply_quaternion(qa_multiply_quaternion(orientation, as_quat),
                                           qa_conjugate_quaternion(orientation));
  return qa_math_vec3(product.x, product.y, product.z);
}

qa_vec3 qa_rotate_quaternion_axis(qa_vec4 q, qa_vec3 v) {
  return qa_math_vec3(
      v.x * FR(2 * (q.w * q.w + q.x * q.x) - 1) + v.y * FR(2 * (q.x * q.y - q.w * q.z)) +
          v.z * FR(2 * (q.x * q.z + q.w * q.y)),
      v.x * FR(2 * (q.x * q.y + q.w * q.z)) + v.y * FR(2 * (q.w * q.w + q.y * q.y) - 1) +
          v.z * FR(2 * (q.y * q.z - q.w * q.x)),
      v.x * FR(2 * (q.x * q.z - q.w * q.y)) + v.y * FR(2 * (q.y * q.z + q.w * q.x)) +
          v.z * FR(2 * (q.w * q.w + q.z * q.z) - 1));
}

qa_quaternion_rotation_rows qa_quaternion_rotation_rows_of(qa_vec4 q) {
  qa_quaternion_rotation_rows rows = {{
      FR(2 * (q.w * q.w + q.x * q.x) - 1), FR(2 * (q.x * q.y - q.w * q.z)),
      FR(2 * (q.x * q.z + q.w * q.y)), FR(2 * (q.x * q.y + q.w * q.z)),
      FR(2 * (q.w * q.w + q.y * q.y) - 1), FR(2 * (q.y * q.z - q.w * q.x)),
      FR(2 * (q.x * q.z - q.w * q.y)), FR(2 * (q.y * q.z + q.w * q.x)),
      FR(2 * (q.w * q.w + q.z * q.z) - 1),
  }};
  return rows;
}

qa_vec3 qa_rotate_quaternion_rows(qa_quaternion_rotation_rows rows, qa_vec3 v) {
  return qa_math_vec3(v.x * rows.values[0] + v.y * rows.values[1] + v.z * rows.values[2],
                      v.x * rows.values[3] + v.y * rows.values[4] + v.z * rows.values[5],
                      v.x * rows.values[6] + v.y * rows.values[7] + v.z * rows.values[8]);
}

qa_vec4 qa_slerp_quaternion(qa_vec4 previous, qa_vec4 current, double back_lerp) {
  if (back_lerp <= 0) return current;
  if (back_lerp >= 1) return previous;
  double cosine = previous.x * current.x + previous.y * current.y + previous.z * current.z +
                  previous.w * current.w;
  double sign = cosine < 0 ? -1 : 1;
  cosine *= sign;
  double a = back_lerp;
  double b = 1 - back_lerp;
  if (cosine <= 0.9995) {
    double sine = sqrt(1 - cosine * cosine);
    double omega = atan2(sine, cosine);
    a = sin(back_lerp * omega) / sine;
    b = sin((1 - back_lerp) * omega) / sine;
  }
  b *= sign;
  return qa_math_vec4(a * previous.x + b * current.x, a * previous.y + b * current.y,
                      a * previous.z + b * current.z, a * previous.w + b * current.w);
}
