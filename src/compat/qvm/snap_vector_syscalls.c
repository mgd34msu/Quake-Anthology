/* See include/qa/compat/qvm/snap_vector_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/snap_vector_syscalls.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

static int32_t word_i32(const uint8_t *words, size_t length, size_t offset) {
  if (offset > length || length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint32_t bits = (uint32_t)words[offset] | ((uint32_t)words[offset + 1] << 8) |
                  ((uint32_t)words[offset + 2] << 16) | ((uint32_t)words[offset + 3] << 24);
  return (int32_t)bits;
}

/* x87 cw037F nearest-even fistp/fild profile, as in the TS. */
static double snap_component(double value) {
  double lower = floor(value);
  double fraction = value - lower;
  double integer = fraction < 0.5 ? lower
                   : fraction > 0.5 ? lower + 1
                   : fmod(lower, 2.0) == 0.0 ? lower : lower + 1;
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0)
    return -2147483648.0;
  return integer == 0.0 ? 0.0 : integer;
}

bool qa_qvm_snap_vector_syscall(const char *role, const uint8_t *words, size_t words_length,
                                const qa_qvm_game_memory *memory, int32_t *out) {
  bool game = strcmp(role, "game") == 0;
  if (strcmp(role, "ui") == 0) return false;
  if (word_i32(words, words_length, 0) != (game ? 42 : 71)) return false;
  qa_qvm_game_region pointer;
  if (!qa_qvm_game_memory_pointer(memory, word_i32(words, words_length, 4), &pointer))
    qa_range_error("QVM SnapVector requires a nonnull pointer");
  qa_qvm_writable_view vector;
  qa_qvm_game_memory_region_view(memory, &pointer, 0, -1, &vector);
  for (int64_t offset = 0; offset < 12; offset += 4) {
    double value = (double)qa_qvm_writable_view_get_float32(&vector, offset, true);
    qa_qvm_writable_view_set_float32(&vector, offset, snap_component(value), true);
  }
  *out = 0;
  return true;
}
