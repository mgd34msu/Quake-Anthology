/* Port of src/content/q2/rerelease/debug-shapes.ts. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/debug_shapes.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <string.h>

double qa_q2_rerelease_debug_lifetime(double seconds) {
  if (!isfinite(seconds)) qa_range_error("Invalid debug lifetime");
  double staged = (double)(float)((double)(float)seconds * 1000.0);
  int64_t truncated = (int64_t)trunc(staged);
  return (double)(uint32_t)truncated; /* TS >>> 0 */
}
size_t qa_q2_debug_shape(qa_debug_shape shape, qa_vec4 color, double lifetime_seconds,
                         bool depth_test, qa_q2_rerelease_event *out,
                         qa_debug_line *lines, size_t capacity) {
  size_t total = qa_debug_shape_lines(shape, color, depth_test, lines, capacity);
  memset(out, 0, sizeof *out);
  out->kind = "debug-shapes";
  out->lines = lines;
  out->line_count = total < capacity ? total : capacity;
  out->lifetime_milliseconds = qa_q2_rerelease_debug_lifetime(lifetime_seconds);
  return total;
}
