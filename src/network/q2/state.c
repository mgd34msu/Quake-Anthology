/* Port of src/network/q2/state.ts. GPL-2.0-or-later. */
#include "qa/network/q2/state.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_q2_wire_pmove_init(qa_q2_wire_pmove *state) { memset(state, 0, sizeof *state); }
void qa_q2_wire_usercmd_init(qa_q2_wire_usercmd *cmd) { memset(cmd, 0, sizeof *cmd); }
void qa_q2_wire_entity_init(qa_q2_wire_entity *state) { memset(state, 0, sizeof *state); }
void qa_q2_wire_player_init(qa_q2_wire_player *state) { memset(state, 0, sizeof *state); }

int32_t qa_q2_angle2short(double x) {
  /* TS: Math.trunc((x * 65536) / 360) & 65535 (ToInt32, then mask). */
  double t = trunc((x * 65536.0) / 360.0);
  int64_t v = (int64_t)t;
  return (int32_t)(v & 65535);
}

double qa_q2_short2angle(double x) { return x * (360.0 / 65536.0); }

void qa_q2_vec3_make(qa_q2_wire_vec3 out, double x, double y, double z) {
  out[0] = (float)x;
  out[1] = (float)y;
  out[2] = (float)z;
}

void qa_q2_vector_copy(const qa_q2_wire_vec3 a, qa_q2_wire_vec3 b) {
  b[0] = a[0];
  b[1] = a[1];
  b[2] = a[2];
}

double qa_q2_dot_product(const qa_q2_wire_vec3 a, const qa_q2_wire_vec3 b) {
  return (double)a[0] * (double)b[0] + (double)a[1] * (double)b[1] +
         (double)a[2] * (double)b[2];
}

static void check_index(size_t length, int64_t index) {
  if (index < 0 || (uint64_t)index >= (uint64_t)length) {
    qa_range_error("Q2 field index outside storage: %lld", (long long)index);
  }
}

float qa_q2_read_element_f32(const float *array, size_t length, int64_t index) {
  check_index(length, index);
  return array[index];
}
int16_t qa_q2_read_element_i16(const int16_t *array, size_t length, int64_t index) {
  check_index(length, index);
  return array[index];
}
int32_t qa_q2_read_element_i32(const int32_t *array, size_t length, int64_t index) {
  check_index(length, index);
  return array[index];
}
uint8_t qa_q2_read_element_u8(const uint8_t *array, size_t length, int64_t index) {
  check_index(length, index);
  return array[index];
}
double qa_q2_read_element_f64(const double *array, size_t length, int64_t index) {
  check_index(length, index);
  return array[index];
}
