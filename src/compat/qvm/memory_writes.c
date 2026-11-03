/* See include/qa/compat/qvm/memory_writes.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/memory_writes.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/foundation/js.h"

typedef struct watch {
  size_t id;
  qa_qvm_write_range *ranges;
  size_t range_count;
  qa_qvm_publish_fn publish;
  void *publish_ctx;
  qa_qvm_publish_fn after_publication;
  void *after_ctx;
  bool active;
} watch;

typedef struct captured_range {
  int64_t byte_offset;
  uint8_t *before;
  size_t length;
} captured_range;

typedef struct capture_entry {
  /* Snapshot the watch identity and callbacks, not a pointer into the
   * watches array: unobserve memmoves it and observe may realloc it
   * between before() and after() (or from a publish callback), while
   * the TS capture holds the Watch object itself. */
  size_t watch_id;
  qa_qvm_publish_fn publish;
  void *publish_ctx;
  qa_qvm_publish_fn after_publication;
  void *after_ctx;
  captured_range *ranges;
  size_t range_count;
} capture_entry;

static bool watch_active(const qa_qvm_memory_writes *writes, size_t watch_id) {
  const watch *watches = writes->watches;
  for (size_t i = 0; i < writes->watch_count; i++)
    if (watches[i].id == watch_id) return watches[i].active;
  return false;
}

struct qa_qvm_write_capture {
  capture_entry *entries;
  size_t entry_count;
};

static void *xmalloc(size_t size) {
  void *result = malloc(size == 0 ? 1 : size);
  if (result == NULL) qa_range_error("QVM memory allocation failed");
  return result;
}

/* Raw views (qa_qvmx_view) carry no owner, so stores into an interpreter's
 * allocation find its one writes owner here. The TS engine is single threaded
 * and so is every QVM call path. */
#define RAW_OWNER_CAPACITY 64
static qa_qvm_memory_writes *raw_owners[RAW_OWNER_CAPACITY];
static size_t raw_owner_count;

void qa_qvm_memory_writes_bind_raw(qa_qvm_memory_writes *writes) {
  if (raw_owner_count == RAW_OWNER_CAPACITY) qa_range_error("Too many live QVM allocations");
  raw_owners[raw_owner_count++] = writes;
}

void qa_qvm_memory_writes_unbind_raw(qa_qvm_memory_writes *writes) {
  for (size_t i = 0; i < raw_owner_count; i++) {
    if (raw_owners[i] == writes) {
      raw_owners[i] = raw_owners[--raw_owner_count];
      return;
    }
  }
}

qa_qvm_memory_writes *qa_qvmx_writes_for(const uint8_t *bytes) {
  for (size_t i = 0; i < raw_owner_count; i++) {
    qa_qvm_memory_writes *owner = raw_owners[i];
    if (bytes >= owner->bytes && bytes < owner->bytes + owner->length) return owner;
  }
  return NULL;
}

void qa_qvm_memory_writes_init(qa_qvm_memory_writes *writes, uint8_t *bytes,
                               size_t length, qa_qvm_effect_fn effect, void *effect_ctx) {
  memset(writes, 0, sizeof *writes);
  writes->bytes = bytes;
  writes->length = length;
  writes->effect = effect;
  writes->effect_ctx = effect_ctx;
}

void qa_qvm_memory_writes_destroy(qa_qvm_memory_writes *writes) {
  watch *watches = writes->watches;
  for (size_t i = 0; i < writes->watch_count; i++) free(watches[i].ranges);
  free(watches);
  memset(writes, 0, sizeof *writes);
}

bool qa_qvm_memory_writes_get_intercepts(const qa_qvm_memory_writes *writes) {
  return writes->watch_count != 0 || writes->publishing || writes->closed;
}

void qa_qvm_memory_writes_assert_not_publishing(const qa_qvm_memory_writes *writes) {
  if (writes->publishing) qa_range_error("QVM store publication permits bookkeeping only");
}

void qa_qvm_memory_writes_assert_live(const qa_qvm_memory_writes *writes) {
  if (writes->closed) qa_range_error("QVM memory has been retired");
}

void qa_qvm_memory_writes_assert_writable(const qa_qvm_memory_writes *writes) {
  qa_qvm_memory_writes_assert_not_publishing(writes);
  qa_qvm_memory_writes_assert_live(writes);
}

void qa_qvm_memory_writes_range(const qa_qvm_memory_writes *writes, int64_t offset, int64_t length) {
  if (offset < 0 || length < 0 || offset > (int64_t)writes->length - length)
    qa_range_error("QVM raw memory range exceeds allocation");
}

static int compare_range(const void *left, const void *right) {
  int64_t a = ((const qa_qvm_write_range *)left)->byte_offset;
  int64_t b = ((const qa_qvm_write_range *)right)->byte_offset;
  return a < b ? -1 : a > b ? 1 : 0;
}

size_t qa_qvm_memory_writes_observe(qa_qvm_memory_writes *writes,
                                    const qa_qvm_write_range *ranges, size_t range_count,
                                    qa_qvm_publish_fn publish, void *publish_ctx,
                                    qa_qvm_publish_fn after_publication, void *after_ctx) {
  qa_qvm_memory_writes_assert_live(writes);
  qa_qvm_write_range *sorted = xmalloc(range_count * sizeof *sorted);
  size_t kept = 0;
  for (size_t i = 0; i < range_count; i++) {
    qa_qvm_memory_writes_range(writes, ranges[i].byte_offset, ranges[i].byte_length);
    if (ranges[i].byte_length != 0) sorted[kept++] = ranges[i];
  }
  qa_js_sort(sorted, kept, sizeof *sorted, compare_range);
  size_t merged_count = 0;
  for (size_t i = 0; i < kept; i++) {
    if (merged_count != 0) {
      qa_qvm_write_range *previous = &sorted[merged_count - 1];
      if (previous->byte_offset + previous->byte_length >= sorted[i].byte_offset) {
        int64_t end = previous->byte_offset + previous->byte_length;
        int64_t other = sorted[i].byte_offset + sorted[i].byte_length;
        previous->byte_length = (end > other ? end : other) - previous->byte_offset;
        continue;
      }
    }
    sorted[merged_count++] = sorted[i];
  }
  watch *watches = writes->watches;
  if (writes->watch_count == writes->watch_capacity) {
    writes->watch_capacity = writes->watch_capacity == 0 ? 4 : writes->watch_capacity * 2;
    watches = realloc(watches, writes->watch_capacity * sizeof *watches);
    if (watches == NULL) qa_range_error("QVM memory allocation failed");
    writes->watches = watches;
  }
  watch *entry = &watches[writes->watch_count++];
  entry->id = ++writes->next_watch_id;
  entry->ranges = sorted;
  entry->range_count = merged_count;
  entry->publish = publish;
  entry->publish_ctx = publish_ctx;
  entry->after_publication = after_publication;
  entry->after_ctx = after_ctx;
  entry->active = true;
  return entry->id;
}

void qa_qvm_memory_writes_unobserve(qa_qvm_memory_writes *writes, size_t watch_id) {
  watch *watches = writes->watches;
  for (size_t i = 0; i < writes->watch_count; i++) {
    if (watches[i].id == watch_id && watches[i].active) {
      watches[i].active = false;
      free(watches[i].ranges);
      watches[i].ranges = NULL;
      watches[i].range_count = 0;
      memmove(&watches[i], &watches[i + 1], (writes->watch_count - i - 1) * sizeof *watches);
      writes->watch_count--;
      return;
    }
  }
}

qa_qvm_write_capture *qa_qvm_memory_writes_before(qa_qvm_memory_writes *writes,
                                                  int64_t offset, int64_t length) {
  qa_qvm_memory_writes_assert_writable(writes);
  qa_qvm_memory_writes_range(writes, offset, length);
  watch *watches = writes->watches;
  qa_qvm_write_capture *captures = NULL;
  for (size_t w = 0; w < writes->watch_count; w++) {
    captured_range *ranges = NULL;
    size_t count = 0;
    for (size_t r = 0; r < watches[w].range_count; r++) {
      int64_t range_start = watches[w].ranges[r].byte_offset;
      int64_t start = offset > range_start ? offset : range_start;
      int64_t range_end = range_start + watches[w].ranges[r].byte_length;
      int64_t write_end = offset + length;
      int64_t end = write_end < range_end ? write_end : range_end;
      if (start < end) {
        ranges = realloc(ranges, (count + 1) * sizeof *ranges);
        if (ranges == NULL) qa_range_error("QVM memory allocation failed");
        ranges[count].byte_offset = start;
        ranges[count].length = (size_t)(end - start);
        ranges[count].before = xmalloc(ranges[count].length);
        memcpy(ranges[count].before, writes->bytes + start, ranges[count].length);
        count++;
      }
    }
    if (ranges != NULL) {
      if (captures == NULL) {
        captures = xmalloc(sizeof *captures);
        captures->entries = NULL;
        captures->entry_count = 0;
      }
      captures->entries = realloc(captures->entries, (captures->entry_count + 1) * sizeof *captures->entries);
      if (captures->entries == NULL) qa_range_error("QVM memory allocation failed");
      captures->entries[captures->entry_count].watch_id = watches[w].id;
      captures->entries[captures->entry_count].publish = watches[w].publish;
      captures->entries[captures->entry_count].publish_ctx = watches[w].publish_ctx;
      captures->entries[captures->entry_count].after_publication = watches[w].after_publication;
      captures->entries[captures->entry_count].after_ctx = watches[w].after_ctx;
      captures->entries[captures->entry_count].ranges = ranges;
      captures->entries[captures->entry_count].range_count = count;
      captures->entry_count++;
    }
  }
  return captures;
}

typedef struct after_job {
  qa_qvm_publish_fn publish;
  void *ctx;
  const qa_qvm_committed_write *event;
} after_job;

static void run_after(void *ctx) {
  after_job *job = ctx;
  job->publish(job->ctx, job->event);
}

typedef struct delivery_job {
  qa_qvm_publish_fn publish;
  void *ctx;
  const qa_qvm_committed_write *event;
} delivery_job;

static void run_delivery(void *ctx) {
  delivery_job *job = ctx;
  job->publish(job->ctx, job->event);
}

typedef struct after_publication_job {
  qa_qvm_memory_writes *writes;
  qa_qvm_write_capture *captures;
  const qa_qvm_committed_write *events;
} after_publication_job;

static void run_after_publications(void *ctx) {
  after_publication_job *job = ctx;
  qa_qvm_memory_writes *writes = job->writes;
  for (size_t e = 0; e < job->captures->entry_count; e++) {
    capture_entry *entry = &job->captures->entries[e];
    if (watch_active(writes, entry->watch_id) && !writes->closed && entry->after_publication != NULL) {
      after_job after = {entry->after_publication, entry->after_ctx, &job->events[e]};
      if (writes->effect != NULL) writes->effect(writes->effect_ctx, run_after, &after);
      else run_after(&after);
    }
  }
}

void qa_qvm_memory_writes_after(qa_qvm_memory_writes *writes, qa_qvm_write_capture *captures) {
  if (captures == NULL) return;
  int64_t sequence = ++writes->sequence;
  /* Build every delivery before publishing any, as in the TS. */
  qa_qvm_committed_write *events = xmalloc(captures->entry_count * sizeof *events);
  qa_qvm_committed_bytes **all_ranges = xmalloc(captures->entry_count * sizeof *all_ranges);
  uint8_t ***all_after = xmalloc(captures->entry_count * sizeof *all_after);
  for (size_t e = 0; e < captures->entry_count; e++) {
    capture_entry *entry = &captures->entries[e];
    qa_qvm_committed_bytes *ranges = xmalloc(entry->range_count * sizeof *ranges);
    uint8_t **after = xmalloc(entry->range_count * sizeof *after);
    for (size_t r = 0; r < entry->range_count; r++) {
      after[r] = xmalloc(entry->ranges[r].length);
      memcpy(after[r], writes->bytes + entry->ranges[r].byte_offset, entry->ranges[r].length);
      ranges[r].byte_offset = entry->ranges[r].byte_offset;
      ranges[r].before = entry->ranges[r].before;
      ranges[r].after = after[r];
      ranges[r].length = entry->ranges[r].length;
    }
    all_ranges[e] = ranges;
    all_after[e] = after;
    events[e].sequence = sequence;
    events[e].ranges = ranges;
    events[e].range_count = entry->range_count;
  }
  /* Every live listener drains even when an earlier one throws; one failure
   * rethrows as itself and several become the TS AggregateError message. */
  char failure[512];
  size_t failure_count = 0;
  writes->publishing = true;
  for (size_t e = 0; e < captures->entry_count; e++) {
    capture_entry *entry = &captures->entries[e];
    if (watch_active(writes, entry->watch_id) && entry->publish != NULL) {
      delivery_job job = {entry->publish, entry->publish_ctx, &events[e]};
      char message[512];
      if (!qa_range_error_catch(run_delivery, &job, message, sizeof message)) {
        if (failure_count == 0) memcpy(failure, message, sizeof failure);
        failure_count++;
      }
    }
  }
  writes->publishing = false;
  bool after_failed = false;
  if (failure_count == 0) {
    after_publication_job job = {writes, captures, events};
    after_failed = !qa_range_error_catch(run_after_publications, &job, failure, sizeof failure);
  }
  for (size_t e = 0; e < captures->entry_count; e++) {
    for (size_t r = 0; r < captures->entries[e].range_count; r++) {
      free(captures->entries[e].ranges[r].before);
      free(all_after[e][r]);
    }
    free(captures->entries[e].ranges);
    free(all_ranges[e]);
    free(all_after[e]);
  }
  free(all_ranges);
  free(all_after);
  free(events);
  free(captures->entries);
  free(captures);
  if (failure_count == 1 || after_failed) qa_range_error("%s", failure);
  if (failure_count > 1) qa_range_error("QVM committed store publication failed");
}

void qa_qvm_memory_writes_clear(qa_qvm_memory_writes *writes) {
  qa_qvm_memory_writes_assert_not_publishing(writes);
  watch *watches = writes->watches;
  for (size_t i = 0; i < writes->watch_count; i++) {
    watches[i].active = false;
    free(watches[i].ranges);
  }
  writes->watch_count = 0;
}

void qa_qvm_memory_writes_close(qa_qvm_memory_writes *writes) {
  qa_qvm_memory_writes_clear(writes);
  writes->closed = true;
}

/* View access: TS DataView index() truncates toward zero (NaN -> 0);
 * C callers pass int64 offsets, so only the bounds check remains. */
static size_t view_offset(const qa_qvm_writable_view *view, int64_t byte_offset, int64_t size) {
  if (byte_offset < 0 || byte_offset > view->length - size)
    qa_range_error("Offset is outside the bounds of the DataView");
  return (size_t)(view->start + byte_offset);
}

void qa_qvm_memory_writes_store(qa_qvm_memory_writes *writes, int64_t offset, const void *bytes, size_t length) {
  qa_qvm_write_capture *capture = qa_qvm_memory_writes_get_intercepts(writes)
      ? qa_qvm_memory_writes_before(writes, offset, (int64_t)length) : NULL;
  memcpy(writes->bytes + offset, bytes, length);
  qa_qvm_memory_writes_after(writes, capture);
}

static uint64_t load_bits(const qa_qvm_writable_view *view, int64_t byte_offset, int size, bool little_endian) {
  size_t at = view_offset(view, byte_offset, size);
  uint64_t result = 0;
  for (int i = 0; i < size; i++) {
    int shift = little_endian ? i * 8 : (size - 1 - i) * 8;
    result |= (uint64_t)view->bytes[at + (size_t)i] << shift;
  }
  return result;
}

static void store_bits(qa_qvm_writable_view *view, int64_t byte_offset, uint64_t value, int size, bool little_endian) {
  size_t at;
  qa_qvm_write_capture *capture = NULL;
  if (view->read_only) qa_raise("RangeError", 0, 0, "Native module memory is read-only");
  if (view->writes == NULL) {
    at = view_offset(view, byte_offset, size);
    for (int i = 0; i < size; i++) {
      int shift = little_endian ? i * 8 : (size - 1 - i) * 8;
      view->bytes[at + (size_t)i] = (uint8_t)(value >> shift);
    }
    return;
  }
  if (!qa_qvm_memory_writes_get_intercepts(view->writes)) {
    at = view_offset(view, byte_offset, size);
  } else {
    size_t checked = view_offset(view, byte_offset, size);
    capture = qa_qvm_memory_writes_before(view->writes, (int64_t)checked, size);
    at = checked;
  }
  for (int i = 0; i < size; i++) {
    int shift = little_endian ? i * 8 : (size - 1 - i) * 8;
    view->bytes[at + (size_t)i] = (uint8_t)(value >> shift);
  }
  if (capture != NULL || qa_qvm_memory_writes_get_intercepts(view->writes))
    qa_qvm_memory_writes_after(view->writes, capture);
}

void qa_qvm_writable_view_init(qa_qvm_writable_view *view, uint8_t *bytes,
                               size_t allocation_length, int64_t start, int64_t length,
                               qa_qvm_memory_writes *writes) {
  view->bytes = bytes;
  view->allocation_length = allocation_length;
  view->start = start;
  view->length = length;
  view->writes = writes;
  view->read_only = false;
}

int8_t qa_qvm_writable_view_get_int8(const qa_qvm_writable_view *v, int64_t o) { return (int8_t)load_bits(v, o, 1, true); }
uint8_t qa_qvm_writable_view_get_uint8(const qa_qvm_writable_view *v, int64_t o) { return (uint8_t)load_bits(v, o, 1, true); }
int16_t qa_qvm_writable_view_get_int16(const qa_qvm_writable_view *v, int64_t o, bool le) { return (int16_t)load_bits(v, o, 2, le); }
uint16_t qa_qvm_writable_view_get_uint16(const qa_qvm_writable_view *v, int64_t o, bool le) { return (uint16_t)load_bits(v, o, 2, le); }
int32_t qa_qvm_writable_view_get_int32(const qa_qvm_writable_view *v, int64_t o, bool le) { return (int32_t)load_bits(v, o, 4, le); }
uint32_t qa_qvm_writable_view_get_uint32(const qa_qvm_writable_view *v, int64_t o, bool le) { return (uint32_t)load_bits(v, o, 4, le); }
int64_t qa_qvm_writable_view_get_big_int64(const qa_qvm_writable_view *v, int64_t o, bool le) { return (int64_t)load_bits(v, o, 8, le); }
uint64_t qa_qvm_writable_view_get_big_uint64(const qa_qvm_writable_view *v, int64_t o, bool le) { return load_bits(v, o, 8, le); }

float qa_qvm_writable_view_get_float32(const qa_qvm_writable_view *v, int64_t o, bool le) {
  uint32_t bits = (uint32_t)load_bits(v, o, 4, le);
  float value;
  memcpy(&value, &bits, sizeof value);
  /* DataView.getFloat32 yields the canonical quiet NaN for every payload. */
  return isnan(value) ? NAN : value;
}

double qa_qvm_writable_view_get_float64(const qa_qvm_writable_view *v, int64_t o, bool le) {
  uint64_t bits = load_bits(v, o, 8, le);
  double value;
  memcpy(&value, &bits, sizeof value);
  return isnan(value) ? NAN : value;
}

void qa_qvm_writable_view_set_int8(qa_qvm_writable_view *v, int64_t o, int32_t x) { store_bits(v, o, (uint8_t)x, 1, true); }
void qa_qvm_writable_view_set_uint8(qa_qvm_writable_view *v, int64_t o, uint32_t x) { store_bits(v, o, (uint8_t)x, 1, true); }
void qa_qvm_writable_view_set_int16(qa_qvm_writable_view *v, int64_t o, int32_t x, bool le) { store_bits(v, o, (uint16_t)x, 2, le); }
void qa_qvm_writable_view_set_uint16(qa_qvm_writable_view *v, int64_t o, uint32_t x, bool le) { store_bits(v, o, (uint16_t)x, 2, le); }
void qa_qvm_writable_view_set_int32(qa_qvm_writable_view *v, int64_t o, int32_t x, bool le) { store_bits(v, o, (uint32_t)x, 4, le); }
void qa_qvm_writable_view_set_uint32(qa_qvm_writable_view *v, int64_t o, uint32_t x, bool le) { store_bits(v, o, x, 4, le); }
void qa_qvm_writable_view_set_big_int64(qa_qvm_writable_view *v, int64_t o, int64_t x, bool le) { store_bits(v, o, (uint64_t)x, 8, le); }
void qa_qvm_writable_view_set_big_uint64(qa_qvm_writable_view *v, int64_t o, uint64_t x, bool le) { store_bits(v, o, x, 8, le); }

void qa_qvm_writable_view_set_float32(qa_qvm_writable_view *v, int64_t o, double x, bool le) {
  float value = (float)x;
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  store_bits(v, o, bits, 4, le);
}

void qa_qvm_writable_view_set_float64(qa_qvm_writable_view *v, int64_t o, double x, bool le) {
  uint64_t bits;
  memcpy(&bits, &x, sizeof bits);
  store_bits(v, o, bits, 8, le);
}
