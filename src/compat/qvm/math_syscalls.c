/* See include/qa/compat/qvm/math_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/math_syscalls.h"

#include <math.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static uint32_t word_bits(const uint8_t *words, size_t length, size_t offset) {
  if (offset > length || length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  return (uint32_t)words[offset] | ((uint32_t)words[offset + 1] << 8) |
         ((uint32_t)words[offset + 2] << 16) | ((uint32_t)words[offset + 3] << 24);
}

static float word_f32(const uint8_t *words, size_t length, size_t offset) {
  uint32_t bits = word_bits(words, length, offset);
  float value;
  memcpy(&value, &bits, sizeof value);
  return value;
}

bool qa_qvm_math_syscall(const char *role, const uint8_t *words, size_t words_length, int32_t *out) {
  int32_t trap = (int32_t)word_bits(words, words_length, 0);
  bool game = strcmp(role, "game") == 0;
  bool ui = strcmp(role, "ui") == 0;
  switch (trap) {
    case 103: *out = (int32_t)qa_float32_to_bits(sin((double)word_f32(words, words_length, 4))); return true;
    case 104: *out = (int32_t)qa_float32_to_bits(cos((double)word_f32(words, words_length, 4))); return true;
    case 105:
      *out = (int32_t)qa_float32_to_bits(atan2((double)word_f32(words, words_length, 4),
                                               (double)word_f32(words, words_length, 8)));
      return true;
    case 106: *out = (int32_t)qa_float32_to_bits(sqrt((double)word_f32(words, words_length, 4))); return true;
    case 107:
      if (game) return false;
      *out = (int32_t)qa_float32_to_bits(floor((double)word_f32(words, words_length, 4)));
      return true;
    case 108:
      if (game) return false;
      *out = (int32_t)qa_float32_to_bits(ceil((double)word_f32(words, words_length, 4)));
      return true;
    case 110:
      if (!game) return false;
      *out = (int32_t)qa_float32_to_bits(floor((double)word_f32(words, words_length, 4)));
      return true;
    case 111: {
      if (ui) return false;
      if (game) {
        *out = (int32_t)qa_float32_to_bits(ceil((double)word_f32(words, words_length, 4)));
        return true;
      }
      /* Math.fround(acos(x)); source clamps the result, not the input. */
      float angle = (float)acos((double)word_f32(words, words_length, 4));
      const double pi = 3.141592653589793;
      *out = (int32_t)qa_float32_to_bits((double)angle > pi || (double)angle < -pi ? pi : (double)angle);
      return true;
    }
    default: return false;
  }
}
