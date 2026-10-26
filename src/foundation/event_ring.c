/* Port of src/foundation/event-ring.ts. GPL-2.0-or-later. */
#include "qa/foundation/event_ring.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static void *slot_of(qa_event_ring *ring, int64_t id) {
  return ring->slots + (size_t)((id - 1) & (int64_t)ring->mask) * ring->elem_size;
}

static const void *slot_of_const(const qa_event_ring *ring, int64_t id) {
  return ring->slots + (size_t)((id - 1) & (int64_t)ring->mask) * ring->elem_size;
}

bool qa_event_ring_init(qa_event_ring *ring, size_t capacity, size_t elem_size) {
  if (capacity < 1 || capacity > (size_t)QA_MAX_SAFE_INTEGER ||
      (capacity & (capacity - 1)) != 0) {
    qa_range_error("Event ring capacity must be a positive power of two");
  }
  memset(ring, 0, sizeof *ring);
  ring->slots = (uint8_t *)calloc(capacity, elem_size == 0 ? 1 : elem_size);
  if (ring->slots == NULL) return false;
  ring->elem_size = elem_size;
  ring->capacity = capacity;
  ring->mask = capacity - 1;
  ring->first_id = 1;
  ring->next_id = 1;
  return true;
}

void qa_event_ring_destroy(qa_event_ring *ring) {
  free(ring->slots);
  free(ring->cursors);
  for (size_t i = 0; i < ring->lease_count; i++) free(ring->leases[i].record);
  free(ring->leases);
  memset(ring, 0, sizeof *ring);
}

int64_t qa_event_ring_live(const qa_event_ring *ring) {
  return ring->next_id - ring->first_id;
}

qa_event_ring_stats qa_event_ring_stats_of(const qa_event_ring *ring) {
  qa_event_ring_stats stats;
  stats.capacity = ring->capacity;
  stats.live = qa_event_ring_live(ring);
  stats.leased = ring->lease_count;
  stats.accepted = ring->accepted_count;
  stats.blocked = ring->blocked_count;
  stats.retired = ring->retired_count;
  stats.first = ring->first_id;
  stats.next = ring->next_id;
  return stats;
}

int64_t qa_event_ring_append(qa_event_ring *ring, const void *record) {
  if (qa_event_ring_live(ring) >= (int64_t)ring->capacity ||
      ring->next_id >= QA_MAX_SAFE_INTEGER) {
    ring->blocked_count++;
    return 0;
  }
  int64_t id = ring->next_id++;
  memcpy(slot_of(ring, id), record, ring->elem_size);
  ring->accepted_count++;
  return id;
}

int64_t qa_event_ring_admit(qa_event_ring *ring, const void *record) {
  int64_t id = qa_event_ring_append(ring, record);
  if (id == 0) {
    qa_range_error("Event ring capacity exhausted (%zu undrained records); "
                   "drain before admitting more", ring->capacity);
  }
  return id;
}

int64_t qa_event_ring_append_all(qa_event_ring *ring, const void *records, size_t count) {
  if (count == 0) return ring->next_id;
  if ((int64_t)count > (int64_t)ring->capacity - qa_event_ring_live(ring) ||
      ring->next_id + (int64_t)count > QA_MAX_SAFE_INTEGER) {
    ring->blocked_count++;
    return 0;
  }
  int64_t first = ring->next_id;
  const uint8_t *bytes = (const uint8_t *)records;
  for (size_t i = 0; i < count; i++) {
    qa_event_ring_append(ring, bytes + i * ring->elem_size);
  }
  return first;
}

int64_t qa_event_ring_admit_all(qa_event_ring *ring, const void *records, size_t count) {
  int64_t first = qa_event_ring_append_all(ring, records, count);
  if (first == 0) {
    qa_range_error("Event ring capacity exhausted (%zu undrained records, "
                   "batch of %zu); drain before admitting more",
                   ring->capacity, count);
  }
  return first;
}

const void *qa_event_ring_at(const qa_event_ring *ring, int64_t id) {
  if (id < ring->first_id || id >= ring->next_id) return NULL;
  return slot_of_const(ring, id);
}

const void *qa_event_ring_head(const qa_event_ring *ring) {
  return ring->next_id <= ring->first_id ? NULL : qa_event_ring_at(ring, ring->first_id);
}

const void *qa_event_ring_at_offset(const qa_event_ring *ring, int64_t index) {
  if (index < 0 || index >= qa_event_ring_live(ring)) return NULL;
  return qa_event_ring_at(ring, ring->first_id + index);
}

void qa_event_ring_replace_at_offset(qa_event_ring *ring, int64_t index, const void *record) {
  if (index < 0 || index >= qa_event_ring_live(ring)) {
    qa_range_error("Event ring offset is outside live records");
  }
  memcpy(slot_of(ring, ring->first_id + index), record, ring->elem_size);
}

void *qa_event_ring_snapshot(const qa_event_ring *ring, size_t *out_count) {
  int64_t live = qa_event_ring_live(ring);
  *out_count = (size_t)live;
  if (live == 0) return NULL;
  uint8_t *out = (uint8_t *)malloc((size_t)live * ring->elem_size);
  if (out == NULL) {
    *out_count = 0;
    return NULL;
  }
  for (int64_t i = 0; i < live; i++) {
    memcpy(out + (size_t)i * ring->elem_size,
           slot_of_const(ring, ring->first_id + i), ring->elem_size);
  }
  return out;
}

void qa_event_ring_remove_at_offset(qa_event_ring *ring, int64_t index) {
  if (index < 0 || index >= qa_event_ring_live(ring)) {
    qa_range_error("Event ring offset is outside live records");
  }
  size_t count = 0;
  uint8_t *retained = (uint8_t *)qa_event_ring_snapshot(ring, &count);
  qa_event_ring_reset(ring);
  for (size_t i = 0; i < count; i++) {
    if ((int64_t)i == index) continue;
    qa_event_ring_admit(ring, retained + i * ring->elem_size);
  }
  free(retained);
}

const void *qa_event_ring_newest(const qa_event_ring *ring) {
  return ring->next_id <= ring->first_id ? NULL
                                          : qa_event_ring_at(ring, ring->next_id - 1);
}

bool qa_event_ring_open_cursor(qa_event_ring *ring, qa_event_cursor *cursor, bool tail) {
  if (ring->cursor_count == ring->cursor_capacity) {
    size_t grown = ring->cursor_capacity == 0 ? 4 : ring->cursor_capacity * 2;
    qa_event_cursor **cursors =
        (qa_event_cursor **)realloc(ring->cursors, grown * sizeof *cursors);
    if (cursors == NULL) return false;
    ring->cursors = cursors;
    ring->cursor_capacity = grown;
  }
  cursor->next = tail ? ring->next_id : ring->first_id;
  ring->cursors[ring->cursor_count++] = cursor;
  return true;
}

void qa_event_ring_close_cursor(qa_event_ring *ring, qa_event_cursor *cursor) {
  for (size_t i = 0; i < ring->cursor_count; i++) {
    if (ring->cursors[i] == cursor) {
      ring->cursors[i] = ring->cursors[--ring->cursor_count];
      return;
    }
  }
}

static void assert_cursor(const qa_event_ring *ring, const qa_event_cursor *cursor) {
  bool found = false;
  for (size_t i = 0; i < ring->cursor_count; i++) {
    if (ring->cursors[i] == cursor) found = true;
  }
  if (!found) qa_range_error("Event cursor does not belong to this ring");
  if (cursor->next < ring->first_id) {
    qa_range_error("Event cursor was overtaken by retirement; its records are gone");
  }
}

int64_t qa_event_ring_pending(const qa_event_ring *ring, const qa_event_cursor *cursor) {
  assert_cursor(ring, cursor);
  return ring->next_id - cursor->next;
}

const void *qa_event_ring_peek(const qa_event_ring *ring, const qa_event_cursor *cursor) {
  assert_cursor(ring, cursor);
  return cursor->next >= ring->next_id ? NULL : qa_event_ring_at(ring, cursor->next);
}

const void *qa_event_ring_read(qa_event_ring *ring, qa_event_cursor *cursor) {
  const void *record = qa_event_ring_peek(ring, cursor);
  if (record != NULL) cursor->next++;
  return record;
}

int64_t qa_event_ring_drain(qa_event_ring *ring, qa_event_cursor *cursor,
                            qa_event_visit_fn visit, void *ctx) {
  assert_cursor(ring, cursor);
  int64_t end = ring->next_id;
  int64_t id = cursor->next;
  int64_t count = 0;
  while (id < end) {
    const void *record = slot_of_const(ring, id);
    id++;
    visit(record, id - 1, ctx);
    count++;
  }
  cursor->next = id;
  return count;
}

void qa_event_ring_for_each_live(const qa_event_ring *ring,
                                 qa_event_visit_fn visit, void *ctx) {
  for (int64_t id = ring->first_id; id < ring->next_id; id++) {
    visit(slot_of_const(ring, id), id, ctx);
  }
}

typedef struct take_ctx {
  uint8_t *out;
  size_t elem_size;
  size_t count;
} take_ctx;

static void take_visit(const void *record, int64_t id, void *raw) {
  (void)id;
  take_ctx *ctx = (take_ctx *)raw;
  memcpy(ctx->out + ctx->count * ctx->elem_size, record, ctx->elem_size);
  ctx->count++;
}

void *qa_event_ring_take(qa_event_ring *ring, qa_event_cursor *cursor, size_t *out_count) {
  int64_t pending = qa_event_ring_pending(ring, cursor);
  if (pending <= 0) {
    /* Still run drain semantics (visitor deferral) for the empty case. */
    *out_count = 0;
    qa_event_ring_drain(ring, cursor, take_visit, &(take_ctx){NULL, ring->elem_size, 0});
    return NULL;
  }
  uint8_t *out = (uint8_t *)malloc((size_t)pending * ring->elem_size);
  if (out == NULL) {
    *out_count = 0;
    return NULL;
  }
  take_ctx ctx = {out, ring->elem_size, 0};
  qa_event_ring_drain(ring, cursor, take_visit, &ctx);
  *out_count = ctx.count;
  return out;
}

void qa_event_ring_retire_before(qa_event_ring *ring, int64_t id) {
  int64_t target = id < ring->next_id ? id : ring->next_id;
  while (ring->first_id < target) {
    memset(slot_of(ring, ring->first_id), 0, ring->elem_size);
    ring->first_id++;
    ring->retired_count++;
  }
}

void qa_event_ring_retire(qa_event_ring *ring) {
  if (ring->cursor_count == 0) return;
  int64_t minimum = ring->next_id;
  for (size_t i = 0; i < ring->cursor_count; i++) {
    if (ring->cursors[i]->next < minimum) minimum = ring->cursors[i]->next;
  }
  qa_event_ring_retire_before(ring, minimum);
}

static qa_event_lease *find_lease(qa_event_ring *ring, int64_t id) {
  for (size_t i = 0; i < ring->lease_count; i++) {
    if (ring->leases[i].id == id) return &ring->leases[i];
  }
  return NULL;
}

bool qa_event_ring_retain(qa_event_ring *ring, int64_t id) {
  qa_event_lease *existing = find_lease(ring, id);
  if (existing != NULL) {
    existing->refs++;
    return true;
  }
  const void *record = qa_event_ring_at(ring, id);
  if (record == NULL) return false;
  if (ring->lease_count == ring->lease_capacity) {
    size_t grown = ring->lease_capacity == 0 ? 4 : ring->lease_capacity * 2;
    qa_event_lease *leases =
        (qa_event_lease *)realloc(ring->leases, grown * sizeof *leases);
    if (leases == NULL) return false;
    ring->leases = leases;
    ring->lease_capacity = grown;
  }
  uint8_t *copy = (uint8_t *)malloc(ring->elem_size == 0 ? 1 : ring->elem_size);
  if (copy == NULL) return false;
  memcpy(copy, record, ring->elem_size);
  ring->leases[ring->lease_count].id = id;
  ring->leases[ring->lease_count].refs = 1;
  ring->leases[ring->lease_count].record = copy;
  ring->lease_count++;
  return true;
}

const void *qa_event_ring_leased_record(const qa_event_ring *ring, int64_t id) {
  for (size_t i = 0; i < ring->lease_count; i++) {
    if (ring->leases[i].id == id) return ring->leases[i].record;
  }
  return NULL;
}

bool qa_event_ring_release_lease(qa_event_ring *ring, int64_t id) {
  for (size_t i = 0; i < ring->lease_count; i++) {
    if (ring->leases[i].id != id) continue;
    ring->leases[i].refs--;
    if (ring->leases[i].refs <= 0) {
      free(ring->leases[i].record);
      /* Preserve Map insertion order for visit_retained. */
      memmove(&ring->leases[i], &ring->leases[i + 1],
              (ring->lease_count - i - 1) * sizeof *ring->leases);
      ring->lease_count--;
    }
    return true;
  }
  return false;
}

void qa_event_ring_visit_retained(const qa_event_ring *ring,
                                  qa_event_visit_fn visit, void *ctx) {
  for (size_t i = 0; i < ring->lease_count; i++) {
    visit(ring->leases[i].record, ring->leases[i].id, ctx);
  }
}

bool qa_event_ring_some_live(const qa_event_ring *ring,
                             qa_event_pred_fn predicate, void *ctx) {
  for (int64_t id = ring->first_id; id < ring->next_id; id++) {
    if (predicate(slot_of_const(ring, id), ctx)) return true;
  }
  return false;
}

bool qa_event_ring_shift(qa_event_ring *ring, void *out) {
  if (ring->first_id >= ring->next_id) return false;
  if (out != NULL) memcpy(out, qa_event_ring_at(ring, ring->first_id), ring->elem_size);
  qa_event_ring_retire_before(ring, ring->first_id + 1);
  return true;
}

void *qa_event_ring_drain_all(qa_event_ring *ring, size_t *out_count) {
  void *taken = qa_event_ring_snapshot(ring, out_count);
  qa_event_ring_retire_before(ring, ring->next_id);
  return taken;
}

void qa_event_ring_prepend(qa_event_ring *ring, const void *records, size_t count) {
  if (count == 0) return;
  if ((int64_t)count > (int64_t)ring->capacity - qa_event_ring_live(ring)) {
    qa_range_error("Event ring capacity exhausted (%zu undrained records, "
                   "prepend of %zu); drain before requeuing",
                   ring->capacity, count);
  }
  size_t live_count = 0;
  uint8_t *live = (uint8_t *)qa_event_ring_snapshot(ring, &live_count);
  /* Copy the batch first: it may alias ring storage the reset clears. */
  uint8_t *batch = (uint8_t *)malloc(count * ring->elem_size);
  if (batch == NULL && count != 0) {
    free(live);
    qa_range_error("Event ring capacity exhausted (%zu undrained records, "
                   "prepend of %zu); drain before requeuing",
                   ring->capacity, count);
  }
  memcpy(batch, records, count * ring->elem_size);
  qa_event_ring_reset(ring);
  for (size_t i = 0; i < count; i++) {
    qa_event_ring_admit(ring, batch + i * ring->elem_size);
  }
  for (size_t i = 0; i < live_count; i++) {
    qa_event_ring_admit(ring, live + i * ring->elem_size);
  }
  free(batch);
  free(live);
}

void qa_event_ring_filter_live(qa_event_ring *ring,
                               qa_event_pred_fn predicate, void *ctx) {
  size_t count = 0;
  uint8_t *live = (uint8_t *)qa_event_ring_snapshot(ring, &count);
  qa_event_ring_reset(ring);
  for (size_t i = 0; i < count; i++) {
    const void *record = live + i * ring->elem_size;
    if (predicate(record, ctx)) qa_event_ring_admit(ring, record);
  }
  free(live);
}

void qa_event_ring_reset(qa_event_ring *ring) {
  if (ring->slots != NULL) memset(ring->slots, 0, ring->capacity * ring->elem_size);
  for (size_t i = 0; i < ring->lease_count; i++) free(ring->leases[i].record);
  ring->lease_count = 0;
  ring->first_id = 1;
  ring->next_id = 1;
  ring->accepted_count = 0;
  ring->blocked_count = 0;
  ring->retired_count = 0;
  for (size_t i = 0; i < ring->cursor_count; i++) ring->cursors[i]->next = 1;
}
