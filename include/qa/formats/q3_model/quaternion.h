/* Port of src/formats/q3-model/quaternion.ts. GPL-2.0-or-later. */
#ifndef QA_FORMATS_Q3_MODEL_QUATERNION_H
#define QA_FORMATS_Q3_MODEL_QUATERNION_H

#include "qa/contracts/math.h"

qa_vec4 qa_md5_quaternion(qa_vec3 value);
qa_vec4 qa_conjugate_quaternion(qa_vec4 value);
qa_vec4 qa_multiply_quaternion(qa_vec4 a, qa_vec4 b);
qa_vec4 qa_normalize_quaternion(qa_vec4 value);
qa_vec3 qa_rotate_quaternion(qa_vec4 orientation, qa_vec3 position);
qa_vec3 qa_rotate_quaternion_axis(qa_vec4 q, qa_vec3 v);

typedef struct qa_quaternion_rotation_rows {
  double values[9];
} qa_quaternion_rotation_rows;

qa_quaternion_rotation_rows qa_quaternion_rotation_rows_of(qa_vec4 q);
qa_vec3 qa_rotate_quaternion_rows(qa_quaternion_rotation_rows rows, qa_vec3 v);
qa_vec4 qa_slerp_quaternion(qa_vec4 previous, qa_vec4 current, double back_lerp);

#endif
