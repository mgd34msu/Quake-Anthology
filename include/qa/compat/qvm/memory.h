/*
 * Port of src/compat/qvm/memory.ts: QvmMemory over a borrowed
 * power-of-two allocation. C accommodations:
 *  - The TS Uint8Array may be a subarray; the C pointer is the
 *    allocation base, so byteOffset differences are plain offsets.
 *  - pointer()/span() return offset + length spans; a null pointer is
 *    reported by qa_qvm_memory_pointer returning false.
 *  - dataView()/view() fill a qa_qvm_writable_view out parameter.
 *  - readString returns a malloc'd NUL-terminated byte copy;
 *    caller frees.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MEMORY_H
#define QA_COMPAT_QVM_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/memory_writes.h"

/* Re-exported by memory.ts from memory-writes.ts. */
typedef qa_qvm_write_range qa_qvm_memory_write_range;

typedef struct qa_qvm_memory {
  uint8_t *bytes; /* borrowed allocation base */
  size_t length;
  qa_qvm_memory_writes writes;
} qa_qvm_memory;

/* A resolved span inside the allocation. */
typedef struct qa_qvm_memory_span {
  int64_t byte_offset;
  int64_t byte_length;
} qa_qvm_memory_span;

void qa_qvm_memory_init(qa_qvm_memory *memory, uint8_t *bytes, size_t length,
                        qa_qvm_effect_fn effect, void *effect_ctx);
void qa_qvm_memory_destroy(qa_qvm_memory *memory);
size_t qa_qvm_memory_observe_writes(qa_qvm_memory *memory,
                                    const qa_qvm_write_range *ranges, size_t range_count,
                                    qa_qvm_publish_fn publish, void *publish_ctx,
                                    qa_qvm_publish_fn after_publication, void *after_ctx);
bool qa_qvm_memory_get_observes_writes(const qa_qvm_memory *memory);
void qa_qvm_memory_assert_live(qa_qvm_memory *memory);
void qa_qvm_memory_assert_not_publishing(qa_qvm_memory *memory);
void qa_qvm_memory_clear_write_observers(qa_qvm_memory *memory);
void qa_qvm_memory_close(qa_qvm_memory *memory);
void qa_qvm_memory_data_view(qa_qvm_memory *memory, int64_t byte_offset, int64_t byte_length,
                             qa_qvm_writable_view *out);
void qa_qvm_memory_write_bytes(qa_qvm_memory *memory, int64_t byte_offset,
                               const uint8_t *bytes, size_t length);
void qa_qvm_memory_fill_bytes(qa_qvm_memory *memory, int64_t byte_offset, int64_t byte_length, int32_t value);
void qa_qvm_memory_copy_bytes(qa_qvm_memory *memory, int64_t destination_offset,
                              int64_t source_offset, int64_t byte_length);
/* False when word == 0 (TS null). Span runs to the allocation end. */
bool qa_qvm_memory_pointer(const qa_qvm_memory *memory, int64_t word, qa_qvm_memory_span *out);
qa_qvm_memory_span qa_qvm_memory_get_span(const qa_qvm_memory *memory, int64_t word, int64_t length, int64_t relative_offset);
void qa_qvm_memory_view(qa_qvm_memory *memory, int64_t word, int64_t length, int64_t relative_offset,
                        qa_qvm_writable_view *out);
/* Malloc'd NUL-terminated copy; caller frees. */
char *qa_qvm_memory_read_string(const qa_qvm_memory *memory, int64_t word);
void qa_qvm_memory_write_string(qa_qvm_memory *memory, int64_t word, const char *text, int64_t capacity);
void qa_qvm_memory_write_bounded_string(qa_qvm_memory *memory, int64_t word, const char *text, int64_t capacity);

#endif
