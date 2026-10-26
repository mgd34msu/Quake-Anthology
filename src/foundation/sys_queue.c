/* Port of src/foundation/sys-queue.ts. GPL-2.0-or-later. */
#include "qa/foundation/sys_queue.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

enum { DEFAULT_RECORDS = 1024, DEFAULT_BYTES = 1024 * 1024 };

bool qa_sys_event_queue_init(qa_sys_event_queue *queue, size_t record_capacity,
                             size_t byte_capacity) {
  if (record_capacity < 1 || record_capacity > (size_t)QA_MAX_SAFE_INTEGER ||
      (record_capacity & (record_capacity - 1)) != 0) {
    qa_range_error("System queue record capacity must be a positive power of two");
  }
  memset(queue, 0, sizeof *queue);
  queue->slots = (qa_sys_event_slot *)malloc(record_capacity * sizeof *queue->slots);
  if (queue->slots == NULL) return false;
  queue->slot_count = record_capacity;
  queue->mask = record_capacity - 1;
  for (size_t i = 0; i < record_capacity; i++) {
    queue->slots[i].kind = 0;
    queue->slots[i].time_ns = 0;
    queue->slots[i].a = 0;
    queue->slots[i].b = 0;
    queue->slots[i].c = 0;
    queue->slots[i].d = 0;
    queue->slots[i].payload_offset = -1;
    queue->slots[i].payload_length = 0;
  }
  if (!qa_byte_arena_init(&queue->payload, byte_capacity)) {
    free(queue->slots);
    queue->slots = NULL;
    return false;
  }
  qa_byte_arena_seal(&queue->payload);
  queue->first_id = 1;
  queue->next_id = 1;
  return true;
}

bool qa_sys_event_queue_init_default(qa_sys_event_queue *queue) {
  return qa_sys_event_queue_init(queue, DEFAULT_RECORDS, DEFAULT_BYTES);
}

void qa_sys_event_queue_destroy(qa_sys_event_queue *queue) {
  free(queue->slots);
  free(queue->cursors);
  qa_byte_arena_destroy(&queue->payload);
  memset(queue, 0, sizeof *queue);
}

int64_t qa_sys_event_queue_live(const qa_sys_event_queue *queue) {
  return queue->next_id - queue->first_id;
}

qa_sys_queue_stats qa_sys_event_queue_stats_of(const qa_sys_event_queue *queue) {
  qa_sys_queue_stats stats;
  stats.accepted = queue->accepted_count;
  stats.consumed = queue->consumed_count;
  stats.full_records = queue->full_record_count;
  stats.full_bytes = queue->full_byte_count;
  stats.records = qa_sys_event_queue_live(queue);
  stats.bytes = queue->payload.used;
  stats.peak_records = queue->peak_record_count;
  stats.peak_bytes = queue->peak_byte_count;
  return stats;
}

uint32_t qa_sys_event_queue_oversize(const qa_sys_event_queue *queue,
                                     qa_sys_event_kind kind) {
  if (kind < 0 || kind >= QA_SYS_KIND_COUNT) return 0;
  return queue->oversize_by_kind[kind];
}

uint32_t qa_sys_event_queue_deferred(const qa_sys_event_queue *queue,
                                     qa_sys_event_kind kind) {
  if (kind < 0 || kind >= QA_SYS_KIND_COUNT) return 0;
  return queue->deferred_by_kind[kind];
}

static void bump_kind(uint32_t *by_kind, qa_sys_event_kind kind) {
  if (kind >= 0 && kind < QA_SYS_KIND_COUNT) by_kind[kind]++;
}

qa_sys_admit_result qa_sys_event_queue_admit(qa_sys_event_queue *queue,
                                             qa_sys_event_kind kind,
                                             size_t maximum_payload) {
  if (maximum_payload > queue->payload.capacity) {
    bump_kind(queue->oversize_by_kind, kind);
    return QA_SYS_OVERSIZE;
  }
  if (qa_sys_event_queue_live(queue) >= (int64_t)queue->slot_count) {
    queue->full_record_count++;
    bump_kind(queue->deferred_by_kind, kind);
    return QA_SYS_FULL;
  }
  if (maximum_payload > queue->payload.capacity - queue->payload.used) {
    queue->full_byte_count++;
    bump_kind(queue->deferred_by_kind, kind);
    return QA_SYS_FULL;
  }
  return QA_SYS_ACCEPTED;
}

qa_sys_admit_result qa_sys_event_queue_push(qa_sys_event_queue *queue,
                                            qa_sys_event_kind kind, double time_ns,
                                            double a, double b, double c, double d,
                                            const uint8_t *payload, size_t payload_length,
                                            const uint8_t *tail, size_t tail_length) {
  size_t total_length = payload_length + tail_length;
  qa_sys_admit_result admission = qa_sys_event_queue_admit(queue, kind, total_length);
  if (admission != QA_SYS_ACCEPTED) return admission;
  int64_t payload_offset = -1;
  if (total_length > 0) {
    payload_offset = qa_byte_arena_alloc(&queue->payload, total_length, 1);
    if (payload_offset < 0) {
      queue->full_byte_count++;
      return QA_SYS_FULL;
    }
    uint8_t *target = qa_byte_arena_view(&queue->payload, payload_offset, total_length);
    if (payload != NULL && payload_length != 0) memcpy(target, payload, payload_length);
    if (tail != NULL && tail_length != 0) memcpy(target + payload_length, tail, tail_length);
  }
  qa_sys_event_slot *slot =
      &queue->slots[(size_t)((queue->next_id - 1) & (int64_t)queue->mask)];
  slot->kind = kind;
  slot->time_ns = time_ns;
  slot->a = a;
  slot->b = b;
  slot->c = c;
  slot->d = d;
  slot->payload_offset = payload_offset;
  slot->payload_length = total_length;
  queue->next_id++;
  queue->accepted_count++;
  if (kind == QA_SYS_QUIT) queue->quit = true;
  int64_t live = qa_sys_event_queue_live(queue);
  if (live > queue->peak_record_count) queue->peak_record_count = live;
  if (queue->payload.used > queue->peak_byte_count) {
    queue->peak_byte_count = queue->payload.used;
  }
  return QA_SYS_ACCEPTED;
}

qa_sys_admit_result qa_sys_event_queue_frame(qa_sys_event_queue *queue, double time_ns) {
  return qa_sys_event_queue_push(queue, QA_SYS_TIME, time_ns, 0, 0, 0, 0, NULL, 0, NULL, 0);
}

bool qa_sys_event_queue_open_cursor(qa_sys_event_queue *queue,
                                    qa_sys_event_cursor *cursor) {
  if (queue->cursor_count == queue->cursor_capacity) {
    size_t grown = queue->cursor_capacity == 0 ? 4 : queue->cursor_capacity * 2;
    qa_sys_event_cursor **cursors =
        (qa_sys_event_cursor **)realloc(queue->cursors, grown * sizeof *cursors);
    if (cursors == NULL) return false;
    queue->cursors = cursors;
    queue->cursor_capacity = grown;
  }
  cursor->next = queue->first_id;
  queue->cursors[queue->cursor_count++] = cursor;
  return true;
}

void qa_sys_event_queue_close_cursor(qa_sys_event_queue *queue,
                                     qa_sys_event_cursor *cursor) {
  for (size_t i = 0; i < queue->cursor_count; i++) {
    if (queue->cursors[i] == cursor) {
      queue->cursors[i] = queue->cursors[--queue->cursor_count];
      return;
    }
  }
}

qa_sys_event_slot *qa_sys_event_queue_read(qa_sys_event_queue *queue,
                                           qa_sys_event_cursor *cursor) {
  bool found = false;
  for (size_t i = 0; i < queue->cursor_count; i++) {
    if (queue->cursors[i] == cursor) found = true;
  }
  if (!found) qa_range_error("System cursor does not belong to this queue");
  if (cursor->next < queue->first_id) {
    qa_range_error("System cursor was overtaken by retirement");
  }
  if (cursor->next >= queue->next_id) return NULL;
  return &queue->slots[(size_t)((cursor->next - 1) & (int64_t)queue->mask)];
}

const uint8_t *qa_sys_event_queue_payload_of(qa_sys_event_queue *queue,
                                             const qa_sys_event_slot *slot,
                                             size_t *out_length) {
  static const uint8_t empty = 0;
  *out_length = slot->payload_length;
  if (slot->payload_offset < 0) return &empty;
  return qa_byte_arena_view(&queue->payload, slot->payload_offset, slot->payload_length);
}

static void retire(qa_sys_event_queue *queue) {
  if (queue->cursor_count == 0) return;
  int64_t minimum = queue->next_id;
  for (size_t i = 0; i < queue->cursor_count; i++) {
    if (queue->cursors[i]->next < minimum) minimum = queue->cursors[i]->next;
  }
  if (minimum > queue->first_id) queue->first_id = minimum;
  if (qa_sys_event_queue_live(queue) == 0) {
    qa_byte_arena_reset(&queue->payload);
    if (queue->first_id == queue->next_id && queue->next_id > 1) {
      queue->first_id = 1;
      queue->next_id = 1;
      for (size_t i = 0; i < queue->cursor_count; i++) queue->cursors[i]->next = 1;
    }
  }
}

void qa_sys_event_queue_consume(qa_sys_event_queue *queue,
                                qa_sys_event_cursor *cursor) {
  if (qa_sys_event_queue_read(queue, cursor) == NULL) return;
  cursor->next++;
  queue->consumed_count++;
  retire(queue);
}

bool qa_sys_event_queue_pending(const qa_sys_event_queue *queue,
                                qa_sys_event_kind kind) {
  for (int64_t id = queue->first_id; id < queue->next_id; id++) {
    if (queue->slots[(size_t)((id - 1) & (int64_t)queue->mask)].kind == kind) return true;
  }
  return false;
}

bool qa_sys_event_queue_quit_requested(const qa_sys_event_queue *queue) {
  return queue->quit;
}

void qa_sys_event_queue_latch_quit(qa_sys_event_queue *queue) { queue->quit = true; }

void qa_sys_event_queue_reset(qa_sys_event_queue *queue) {
  queue->first_id = 1;
  queue->next_id = 1;
  queue->quit = false;
  qa_byte_arena_reset(&queue->payload);
  for (size_t i = 0; i < queue->cursor_count; i++) queue->cursors[i]->next = 1;
}
