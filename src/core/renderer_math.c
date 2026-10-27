/* Port of src/core/renderer-math.ts. GPL-2.0-or-later. */
#include "qa/core/renderer_math.h"

#include <math.h>

#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static float sine_table[1024];
static int sine_table_ready;

static void build_sine_table(void) {
  if (sine_table_ready) return;
  for (int index = 0; index < 1024; index++) {
    float degrees = (float)((double)index * 360.0 / 1023.0);
    sine_table[index] = (float)sin((double)degrees * 3.141592653589793 / 180.0);
  }
  sine_table_ready = 1;
}

float qa_renderer_sine(double index) {
  if (index != trunc(index) || fabs(index) > 9007199254740991.0 ||
      index != (double)qa_int32(index)) {
    qa_range_error("renderer sine index must be a signed 32-bit integer");
  }
  build_sine_table();
  return sine_table[qa_int32(index) & 1023];
}

float qa_inverse_sqrt32(double square_input) {
  float square = (float)square_input;
  int32_t source_bits = qa_int32((double)qa_float32_to_bits((double)square));
  /* int32 arithmetic: 0x5f3759df - (sourceBits >> 1), wrapped. */
  uint32_t approx_bits = 0x5f3759dfu - (uint32_t)(source_bits >> 1);
  float inverse = qa_bits_to_float32(approx_bits);
  float half_square = (float)((double)square * 0.5);
  float half_times_inverse = (float)((double)half_square * (double)inverse);
  float correction_product = (float)((double)half_times_inverse * (double)inverse);
  float correction = (float)(1.5 - (double)correction_product);
  return (float)((double)inverse * (double)correction);
}

qa_vec3 qa_normalize_fast3(qa_vec3 value) {
  return qa_scale3(value, (double)qa_inverse_sqrt32(qa_dot3(value, value)));
}
