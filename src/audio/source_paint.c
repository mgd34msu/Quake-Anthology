/* Port of src/audio/source-paint.ts. GPL-2.0-or-later. */
#include "qa/audio/source_paint.h"
#include "qa/foundation/js.h"

#include <math.h>

#include "qa/foundation/support.h"

static int16_t clipped_i32(int32_t value) {
  int32_t shifted = value >> 8;
  if (shifted < -32768) return -32768;
  if (shifted > 32767) return 32767;
  return (int16_t)shifted;
}

static int32_t paint_i32(const int32_t *paint, size_t length, int64_t index) {
  if (index < 0 || (uint64_t)index >= length)
    qa_range_error("Sound paint access outside allocation at %lld",
                   (long long)index);
  return paint[index];
}

static double paint_f64(const double *paint, size_t length, int64_t index) {
  if (index < 0 || (uint64_t)index >= length)
    qa_range_error("Sound paint access outside allocation at %lld",
                   (long long)index);
  return paint[index];
}

void qa_write_linear_blast_stereo16_i32(const int32_t *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count) {
  if (count < 0 || count % 2 != 0)
    qa_range_error("Stereo blast requires an even sample count");
  for (int64_t index = 0; index < count; index += 2) {
    if (index + 1 >= (int64_t)output_length)
      qa_range_error("Stereo output allocation is truncated");
    output[index] = clipped_i32(paint_i32(paint, paint_length, index));
    output[index + 1] = clipped_i32(paint_i32(paint, paint_length, index + 1));
  }
}

/* Math.max(-32768, Math.min(32767, Math.floor(sample / 256))) stored into an
   Int16Array (a NaN sample stores 0). */
static int16_t clipped_f64(double sample) {
  return qa_js_to_int16(qa_js_max(-32768, qa_js_min(32767, floor(sample / 256))));
}

void qa_write_linear_blast_stereo16_f64(const double *paint,
                                        size_t paint_length, int16_t *output,
                                        size_t output_length, int64_t count) {
  if (count < 0 || count % 2 != 0)
    qa_range_error("Stereo blast requires an even sample count");
  for (int64_t index = 0; index < count; index += 2) {
    if (index + 1 >= (int64_t)output_length)
      qa_range_error("Stereo output allocation is truncated");
    output[index] = clipped_f64(paint_f64(paint, paint_length, index));
    output[index + 1] = clipped_f64(paint_f64(paint, paint_length, index + 1));
  }
}
