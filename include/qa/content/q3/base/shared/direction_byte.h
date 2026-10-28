/* Port of src/content/q3/base/shared/direction-byte.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_SHARED_DIRECTION_BYTE_H
#define QA_CONTENT_Q3_BASE_SHARED_DIRECTION_BYTE_H
#include "qa/contracts/math.h"
/* TS directionToByte(value|null): NULL value returns 0. */
double qa_q3_base_direction_to_byte(const qa_vec3 *value);
/* TS byteToDirection(byte): non-safe-integer aborts (RangeError);
 * out-of-range returns the zero direction. */
qa_vec3 qa_q3_base_byte_to_direction(double byte_value);
#endif
