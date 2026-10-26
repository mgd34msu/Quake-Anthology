/*
 * Port of src/foundation/event-ring.ts (EventRing<T>): one load-sized
 * admitted-output ring with independent consumer cursors, leases and
 * explicit backpressure. Append never grows or overwrites; retirement
 * frees only the contiguous prefix every cursor has passed.
 * C accommodation: records are elem_size byte blocks copied in/out;
 * leases hold a copy of the record bytes (identical for pointer
 * payloads). Cursors are caller-allocated and registered with the ring.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_EVENT_RING_H
#define QA_FOUNDATION_EVENT_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_event_cursor {
  int64_t next;
} qa_event_cursor;

typedef struct qa_event_ring_stats {
  size_t capacity;
  int64_t live;
  size_t leased;
  int64_t accepted;
  int64_t blocked;
  int64_t retired;
  int64_t first;
  int64_t next;
} qa_event_ring_stats;

typedef void (*qa_event_visit_fn)(const void *record, int64_t id, void *ctx);
typedef bool (*qa_event_pred_fn)(const void *record, void *ctx);

typedef struct qa_event_lease {
  int64_t id;
  int64_t refs;
  uint8_t *record;
} qa_event_lease;

typedef struct qa_event_ring {
  uint8_t *slots;
  size_t elem_size;
  size_t capacity;
  size_t mask;
  qa_event_cursor **cursors;
  size_t cursor_count;
  size_t cursor_capacity;
  qa_event_lease *leases;
  size_t lease_count;
  size_t lease_capacity;
  int64_t first_id;
  int64_t next_id;
  int64_t accepted_count;
  int64_t blocked_count;
  int64_t retired_count;
} qa_event_ring;

/* Capacity must be a positive power of two. False only on OOM. */
bool qa_event_ring_init(qa_event_ring *ring, size_t capacity, size_t elem_size);
void qa_event_ring_destroy(qa_event_ring *ring);
qa_event_ring_stats qa_event_ring_stats_of(const qa_event_ring *ring);
int64_t qa_event_ring_live(const qa_event_ring *ring);

/* Admits one record; returns its id (from 1), or 0 when full. */
int64_t qa_event_ring_append(qa_event_ring *ring, const void *record);
/* append or abort when full (TypeScript admit). */
int64_t qa_event_ring_admit(qa_event_ring *ring, const void *record);
/* All-or-nothing batch over a contiguous record array. First id, or 0. */
int64_t qa_event_ring_append_all(qa_event_ring *ring, const void *records, size_t count);
int64_t qa_event_ring_admit_all(qa_event_ring *ring, const void *records, size_t count);

const void *qa_event_ring_head(const qa_event_ring *ring);
const void *qa_event_ring_at_offset(const qa_event_ring *ring, int64_t index);
void qa_event_ring_replace_at_offset(qa_event_ring *ring, int64_t index, const void *record);
void qa_event_ring_remove_at_offset(qa_event_ring *ring, int64_t index);
const void *qa_event_ring_newest(const qa_event_ring *ring);
const void *qa_event_ring_at(const qa_event_ring *ring, int64_t id);

/* Opens a cursor at the oldest live record, or at the tail. False on OOM. */
bool qa_event_ring_open_cursor(qa_event_ring *ring, qa_event_cursor *cursor, bool tail);
void qa_event_ring_close_cursor(qa_event_ring *ring, qa_event_cursor *cursor);
int64_t qa_event_ring_pending(const qa_event_ring *ring, const qa_event_cursor *cursor);
const void *qa_event_ring_peek(const qa_event_ring *ring, const qa_event_cursor *cursor);
const void *qa_event_ring_read(qa_event_ring *ring, qa_event_cursor *cursor);
int64_t qa_event_ring_drain(qa_event_ring *ring, qa_event_cursor *cursor,
                            qa_event_visit_fn visit, void *ctx);
void qa_event_ring_for_each_live(const qa_event_ring *ring,
                                 qa_event_visit_fn visit, void *ctx);
/* Malloc'd copy of live records in admission order; caller frees. */
void *qa_event_ring_snapshot(const qa_event_ring *ring, size_t *out_count);
/* Array form of drain; caller frees. */
void *qa_event_ring_take(qa_event_ring *ring, qa_event_cursor *cursor, size_t *out_count);

void qa_event_ring_retire(qa_event_ring *ring);
void qa_event_ring_retire_before(qa_event_ring *ring, int64_t id);
bool qa_event_ring_retain(qa_event_ring *ring, int64_t id);
const void *qa_event_ring_leased_record(const qa_event_ring *ring, int64_t id);
bool qa_event_ring_release_lease(qa_event_ring *ring, int64_t id);
void qa_event_ring_visit_retained(const qa_event_ring *ring,
                                  qa_event_visit_fn visit, void *ctx);
bool qa_event_ring_some_live(const qa_event_ring *ring,
                             qa_event_pred_fn predicate, void *ctx);
/* Pops the oldest live record into out (may be NULL). False when empty. */
bool qa_event_ring_shift(qa_event_ring *ring, void *out);
/* Malloc'd snapshot plus full retirement; caller frees. */
void *qa_event_ring_drain_all(qa_event_ring *ring, size_t *out_count);
void qa_event_ring_prepend(qa_event_ring *ring, const void *records, size_t count);
void qa_event_ring_filter_live(qa_event_ring *ring,
                               qa_event_pred_fn predicate, void *ctx);
/* Cold load/restore only: empties the ring and rewinds every cursor. */
void qa_event_ring_reset(qa_event_ring *ring);

#endif
