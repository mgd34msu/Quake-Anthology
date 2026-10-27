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

static void reject_overlap(qa_qvm_memory_span destination, qa_qvm_memory_span source) {
  if (destination.byte_length != 0 && source.byte_length != 0 &&
      destination.byte_offset < source.byte_offset + source.byte_length &&
      source.byte_offset < destination.byte_offset + destination.byte_length)
    qa_range_error("Overlapping QVM memcpy/strncpy ranges are unsupported");
}

bool qa_qvm_memory_syscall(const char *role, const uint8_t *words, size_t words_length,
                           qa_qvm_memory *memory, int32_t *out) {
  (void)role;
  int32_t trap = word_i32(words, words_length, 0);
  if (trap != 100 && trap != 101 && trap != 102) return false;
  int32_t destination_word = word_i32(words, words_length, 4);
  int32_t source_word = word_i32(words, words_length, 8);
  int32_t count = word_i32(words, words_length, 12);
  qa_qvm_memory_span destination = qa_qvm_memory_get_span(memory, destination_word, count, 0);
  int64_t offset = destination.byte_offset;
  if (trap == 100) {
    qa_qvm_memory_fill_bytes(memory, offset, count, source_word & 255);
    *out = 0;
    return true;
  }
  if (trap == 101) {
    qa_qvm_memory_span source = qa_qvm_memory_get_span(memory, source_word, count, 0);
    reject_overlap(destination, source);
    /* Copy through a snapshot: writeBytes of a subarray of the same
     * allocation in TS reads the current bytes; ranges are disjoint. */
    qa_qvm_memory_write_bytes(memory, offset, memory->bytes + source.byte_offset, (size_t)count);
    *out = 0;
    return true;
  }
  qa_qvm_memory_span source;
  if (!qa_qvm_memory_pointer(memory, source_word, &source))
    qa_range_error("QVM strncpy requires a nonnull source pointer");
  const uint8_t *source_bytes = memory->bytes + source.byte_offset;
  int64_t scan = count < source.byte_length ? count : source.byte_length;
  const void *found = scan > 0 ? memchr(source_bytes, 0, (size_t)scan) : NULL;
  int64_t terminator = found == NULL ? -1 : (int64_t)((const uint8_t *)found - source_bytes);
  int64_t copied_length = terminator < 0 ? count : terminator;
  qa_qvm_memory_span consumed = qa_qvm_memory_get_span(memory, source_word, terminator < 0 ? count : terminator + 1, 0);
  reject_overlap(destination, consumed);
  qa_qvm_memory_write_bytes(memory, offset, source_bytes, (size_t)copied_length);
  qa_qvm_memory_fill_bytes(memory, offset + copied_length, count - copied_length, 0);
  *out = destination_word;
  return true;
}
