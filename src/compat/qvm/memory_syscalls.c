/* See include/qa/compat/qvm/memory_syscalls.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/memory_syscalls.h"

#include <string.h>

#include "qa/foundation/support.h"

static int32_t word_i32(const uint8_t *words, size_t length, size_t offset) {
  if (offset > length || length - offset < 4)
    qa_range_error("Offset is outside the bounds of the DataView");
  uint32_t bits = (uint32_t)words[offset] | ((uint32_t)words[offset + 1] << 8) |
                  ((uint32_t)words[offset + 2] << 16) | ((uint32_t)words[offset + 3] << 24);
  return (int32_t)bits;
}

bool qa_qvm_memory_syscall(const char *role, const uint8_t *words, size_t words_length,
                           const qa_qvm_game_memory *memory, int32_t *out) {
  (void)role;
  int32_t trap = word_i32(words, words_length, 0);
  if (trap != 100 && trap != 101 && trap != 102) return false;
  int32_t destination_word = word_i32(words, words_length, 4);
  int32_t source_word = word_i32(words, words_length, 8);
  int32_t count = word_i32(words, words_length, 12);
  const qa_qvm_game_region destination = qa_qvm_game_memory_span(memory, destination_word, count, 0);
  if (trap == 100) {
    qa_qvm_game_memory_region_fill(memory, &destination, 0, count, source_word & 255);
    *out = 0;
    return true;
  }
  if (trap == 101) {
    const qa_qvm_game_region source = qa_qvm_game_memory_span(memory, source_word, count, 0);
    /* Overlapping copies behave as memmove; a self-copy writes nothing. */
    if (destination.bytes != source.bytes) qa_qvm_game_memory_region_write(memory, &destination, source.bytes, source.length);
    *out = 0;
    return true;
  }
  qa_qvm_game_region source;
  if (!qa_qvm_game_memory_pointer(memory, source_word, &source))
    qa_range_error("QVM strncpy requires a nonnull source pointer");
  /* source.subarray(0, count).indexOf(0) */
  int64_t scan = count < 0 ? 0 : (int64_t)count < (int64_t)source.length ? count : (int64_t)source.length;
  const void *found = scan > 0 ? memchr(source.bytes, 0, (size_t)scan) : NULL;
  int64_t terminator = found == NULL ? -1 : (int64_t)((const uint8_t *)found - source.bytes);
  int64_t copied_length = terminator < 0 ? count : terminator;
  (void)qa_qvm_game_memory_span(memory, source_word, terminator < 0 ? count : terminator + 1, 0);
  qa_qvm_game_memory_region_write(memory, &destination, source.bytes, (size_t)copied_length);
  qa_qvm_game_memory_region_fill(memory, &destination, copied_length, count - copied_length, 0);
  *out = destination_word;
  return true;
}
