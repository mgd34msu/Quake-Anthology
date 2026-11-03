/*
 * Port of src/compat/qvm/memory-writes.ts: committed-store publication
 * for one QVM allocation. C accommodations:
 *  - The TS unsubscribe closure becomes a watch id passed to
 *    qa_qvm_memory_writes_unobserve.
 *  - Publish callbacks are fn + ctx; the TS `effect` thunk wrapper is
 *    fn + ctx receiving a perform fn + ctx, which it must run (the TS
 *    default runs it immediately) before returning.
 *  - A publish callback that throws through qa_range_error is caught so
 *    later listeners still drain; one failure rethrows as itself and
 *    several rethrow as the TS AggregateError message.
 *  - QvmWritableView (a DataView subclass) becomes a view struct with
 *    explicit get/set functions; multi-byte accesses take the TS
 *    DataView littleEndian flag.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_MEMORY_WRITES_H
#define QA_COMPAT_QVM_MEMORY_WRITES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TS QvmWriteRange. Offsets/lengths are int64 so TS safe-integer and
 * negativity checks reproduce exactly. */
typedef struct qa_qvm_write_range {
  int64_t byte_offset;
  int64_t byte_length;
} qa_qvm_write_range;

/* TS QvmCommittedBytes: before/after are borrowed byte copies valid
 * only for the duration of the publish callback. */
typedef struct qa_qvm_committed_bytes {
  int64_t byte_offset;
  const uint8_t *before;
  const uint8_t *after;
  size_t length;
} qa_qvm_committed_bytes;

/* TS QvmCommittedWrite. */
typedef struct qa_qvm_committed_write {
  int64_t sequence;
  const qa_qvm_committed_bytes *ranges;
  size_t range_count;
} qa_qvm_committed_write;

typedef void (*qa_qvm_publish_fn)(void *ctx, const qa_qvm_committed_write *event);
typedef void (*qa_qvm_perform_fn)(void *ctx);
typedef void (*qa_qvm_effect_fn)(void *ctx, qa_qvm_perform_fn perform, void *perform_ctx);

typedef struct qa_qvm_memory_writes qa_qvm_memory_writes;

/* TS QvmWriteCapture: opaque, allocated by _before, consumed by _after. */
typedef struct qa_qvm_write_capture qa_qvm_write_capture;

struct qa_qvm_memory_writes {
  uint8_t *bytes; /* borrowed */
  size_t length;
  qa_qvm_effect_fn effect; /* NULL runs perform immediately */
  void *effect_ctx;
  void *watches; /* opaque */
  size_t watch_count;
  size_t watch_capacity;
  size_t next_watch_id;
  bool publishing;
  bool closed;
  int64_t sequence;
};

void qa_qvm_memory_writes_init(qa_qvm_memory_writes *writes, uint8_t *bytes,
                               size_t length, qa_qvm_effect_fn effect, void *effect_ctx);
void qa_qvm_memory_writes_destroy(qa_qvm_memory_writes *writes);
bool qa_qvm_memory_writes_get_intercepts(const qa_qvm_memory_writes *writes);
void qa_qvm_memory_writes_assert_not_publishing(const qa_qvm_memory_writes *writes);
void qa_qvm_memory_writes_assert_live(const qa_qvm_memory_writes *writes);
void qa_qvm_memory_writes_assert_writable(const qa_qvm_memory_writes *writes);
void qa_qvm_memory_writes_range(const qa_qvm_memory_writes *writes, int64_t offset, int64_t length);
/* Returns the watch id for qa_qvm_memory_writes_unobserve. */
size_t qa_qvm_memory_writes_observe(qa_qvm_memory_writes *writes,
                                    const qa_qvm_write_range *ranges, size_t range_count,
                                    qa_qvm_publish_fn publish, void *publish_ctx,
                                    qa_qvm_publish_fn after_publication, void *after_ctx);
void qa_qvm_memory_writes_unobserve(qa_qvm_memory_writes *writes, size_t watch_id);
qa_qvm_write_capture *qa_qvm_memory_writes_before(qa_qvm_memory_writes *writes,
                                                  int64_t offset, int64_t length);
void qa_qvm_memory_writes_after(qa_qvm_memory_writes *writes, qa_qvm_write_capture *captures);
/* One committed raw store: before(), copy `length` bytes, after(). The caller
 * has already bounds-checked `offset`. */
void qa_qvm_memory_writes_store(qa_qvm_memory_writes *writes, int64_t offset, const void *bytes, size_t length);
/* Make raw qa_qvmx_view / qa_qvmx_memory stores inside this allocation publish
 * through `writes` (the TS retained-view rule). Interpreters bind their one
 * allocation for their lifetime. qa_qvmx_writes_for returns NULL for bytes in no
 * bound allocation. */
void qa_qvm_memory_writes_bind_raw(qa_qvm_memory_writes *writes);
void qa_qvm_memory_writes_unbind_raw(qa_qvm_memory_writes *writes);
qa_qvm_memory_writes *qa_qvmx_writes_for(const uint8_t *bytes);
void qa_qvm_memory_writes_clear(qa_qvm_memory_writes *writes);
void qa_qvm_memory_writes_close(qa_qvm_memory_writes *writes);

/* TS QvmWritableView. */
typedef struct qa_qvm_writable_view {
  uint8_t *bytes; /* allocation base, borrowed */
  size_t allocation_length;
  int64_t start; /* view start within the allocation */
  int64_t length;
  qa_qvm_memory_writes *writes; /* borrowed; NULL: a plain DataView (no observers) */
  bool read_only; /* a store raises "Native module memory is read-only" (compat/q3/native/memory.ts) */
} qa_qvm_writable_view;

void qa_qvm_writable_view_init(qa_qvm_writable_view *view, uint8_t *bytes,
                               size_t allocation_length, int64_t start, int64_t length,
                               qa_qvm_memory_writes *writes);
int8_t qa_qvm_writable_view_get_int8(const qa_qvm_writable_view *view, int64_t byte_offset);
uint8_t qa_qvm_writable_view_get_uint8(const qa_qvm_writable_view *view, int64_t byte_offset);
int16_t qa_qvm_writable_view_get_int16(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
uint16_t qa_qvm_writable_view_get_uint16(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
int32_t qa_qvm_writable_view_get_int32(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
uint32_t qa_qvm_writable_view_get_uint32(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
float qa_qvm_writable_view_get_float32(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
double qa_qvm_writable_view_get_float64(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
int64_t qa_qvm_writable_view_get_big_int64(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
uint64_t qa_qvm_writable_view_get_big_uint64(const qa_qvm_writable_view *view, int64_t byte_offset, bool little_endian);
void qa_qvm_writable_view_set_int8(qa_qvm_writable_view *view, int64_t byte_offset, int32_t value);
void qa_qvm_writable_view_set_uint8(qa_qvm_writable_view *view, int64_t byte_offset, uint32_t value);
void qa_qvm_writable_view_set_int16(qa_qvm_writable_view *view, int64_t byte_offset, int32_t value, bool little_endian);
void qa_qvm_writable_view_set_uint16(qa_qvm_writable_view *view, int64_t byte_offset, uint32_t value, bool little_endian);
void qa_qvm_writable_view_set_int32(qa_qvm_writable_view *view, int64_t byte_offset, int32_t value, bool little_endian);
void qa_qvm_writable_view_set_uint32(qa_qvm_writable_view *view, int64_t byte_offset, uint32_t value, bool little_endian);
void qa_qvm_writable_view_set_float32(qa_qvm_writable_view *view, int64_t byte_offset, double value, bool little_endian);
void qa_qvm_writable_view_set_float64(qa_qvm_writable_view *view, int64_t byte_offset, double value, bool little_endian);
void qa_qvm_writable_view_set_big_int64(qa_qvm_writable_view *view, int64_t byte_offset, int64_t value, bool little_endian);
void qa_qvm_writable_view_set_big_uint64(qa_qvm_writable_view *view, int64_t byte_offset, uint64_t value, bool little_endian);

#endif
