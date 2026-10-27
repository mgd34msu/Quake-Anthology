/* See include/qa/compat/qvm/vector_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/vector_syscalls.h"

#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"

static int32_t word_i32(const uint8_t *words, size_t length, size_t offset) {
  if (offset > length || length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint32_t bits = (uint32_t)words[offset] | ((uint32_t)words[offset + 1] << 8) |
                  ((uint32_t)words[offset + 2] << 16) | ((uint32_t)words[offset + 3] << 24);
  return (int32_t)bits;
}

static qa_vec3 read_vector(const qa_qvm_writable_view *view) {
  return qa_math_vec3((double)qa_qvm_writable_view_get_float32(view, 0, true),
                      (double)qa_qvm_writable_view_get_float32(view, 4, true),
                      (double)qa_qvm_writable_view_get_float32(view, 8, true));
}

static void write_vector(qa_qvm_writable_view *view, qa_vec3 value) {
  qa_qvm_writable_view_set_float32(view, 0, value.x, true);
  qa_qvm_writable_view_set_float32(view, 4, value.y, true);
  qa_qvm_writable_view_set_float32(view, 8, value.z, true);
}

bool qa_qvm_vector_syscall(const char *role, const uint8_t *words, size_t words_length,
                           qa_qvm_memory *memory, int32_t *out) {
  if (strcmp(role, "game") != 0) return false;
  switch (word_i32(words, words_length, 0)) {
    case 107: {
      qa_qvm_writable_view left, right, output;
      qa_qvm_memory_view(memory, word_i32(words, words_length, 4), 36, 0, &left);
      qa_qvm_memory_view(memory, word_i32(words, words_length, 8), 36, 0, &right);
      qa_qvm_memory_view(memory, word_i32(words, words_length, 12), 36, 0, &output);
      for (int row = 0; row < 3; row++) {
        for (int column = 0; column < 3; column++) {
          qa_vec3 a = qa_math_vec3(
              (double)qa_qvm_writable_view_get_float32(&left, row * 12, true),
              (double)qa_qvm_writable_view_get_float32(&left, row * 12 + 4, true),
              (double)qa_qvm_writable_view_get_float32(&left, row * 12 + 8, true));
          qa_vec3 b = qa_math_vec3(
              (double)qa_qvm_writable_view_get_float32(&right, column * 4, true),
              (double)qa_qvm_writable_view_get_float32(&right, column * 4 + 12, true),
              (double)qa_qvm_writable_view_get_float32(&right, column * 4 + 24, true));
          qa_qvm_writable_view_set_float32(&output, row * 12 + column * 4, qa_dot3(a, b), true);
        }
      }
      *out = 0;
      return true;
    }
    case 108: {
      qa_qvm_writable_view angles;
      qa_qvm_memory_view(memory, word_i32(words, words_length, 4), 12, 0, &angles);
      int32_t forward_word = word_i32(words, words_length, 8);
      int32_t right_word = word_i32(words, words_length, 12);
      int32_t up_word = word_i32(words, words_length, 16);
      qa_qvm_writable_view forward, right, up;
      /* TS resolves every optional span before reading the angles. */
      if (forward_word != 0) qa_qvm_memory_view(memory, forward_word, 12, 0, &forward);
      if (right_word != 0) qa_qvm_memory_view(memory, right_word, 12, 0, &right);
      if (up_word != 0) qa_qvm_memory_view(memory, up_word, 12, 0, &up);
      qa_angle_vectors result = qa_math_angle_vectors(read_vector(&angles));
      if (forward_word != 0) write_vector(&forward, result.forward);
      if (right_word != 0) write_vector(&right, result.right);
      if (up_word != 0) write_vector(&up, result.up);
      *out = 0;
      return true;
    }
    case 109: {
      qa_qvm_writable_view destination, source_view;
      qa_qvm_memory_view(memory, word_i32(words, words_length, 4), 12, 0, &destination);
      qa_qvm_memory_view(memory, word_i32(words, words_length, 8), 12, 0, &source_view);
      qa_vec3 source = read_vector(&source_view);
      if (qa_dot3(source, source) == 0.0)
        qa_range_error("QVM PerpendicularVector has a zero projection denominator");
      write_vector(&destination, qa_perpendicular_vector(source));
      *out = 0;
      return true;
    }
    default: return false;
  }
}
