/* Port of src/foundation/bounded-queue.ts. GPL-2.0-or-later. */
#include "qa/foundation/bounded_queue.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static size_t wrap(const qa_bounded_queue *queue, size_t index) {
  return queue->power_of_two ? index & (queue->capacity - 1)
                              : index % queue->capacity;
}

static void *slot_at(qa_bounded_queue *queue, size_t ring_index) {
  return queue->slots + ring_index * queue->elem_size;
}

static const void *slot_at_const(const qa_bounded_queue *queue, size_t ring_index) {
  return queue->slots + ring_index * queue->elem_size;
}

bool qa_bounded_queue_init(qa_bounded_queue *queue, size_t capacity, size_t elem_size) {
  if (capacity < 1 || capacity > (size_t)QA_MAX_SAFE_INTEGER) {
    qa_range_error("Bounded queue capacity must be a positive safe integer");
  }
  queue->slots = (uint8_t *)calloc(capacity, elem_size == 0 ? 1 : elem_size);
  if (queue->slots == NULL) {
    queue->capacity = 0;
    queue->elem_size = elem_size;
    queue->head = 0;
    queue->count = 0;
    queue->dropped = 0;
    queue->power_of_two = false;
    return false;
  }
  queue->elem_size = elem_size;
  queue->capacity = capacity;
  queue->power_of_two = (capacity & (capacity - 1)) == 0;
  queue->head = 0;
  queue->count = 0;
  queue->dropped = 0;
  return true;
}

void qa_bounded_queue_destroy(qa_bounded_queue *queue) {
  free(queue->slots);
  queue->slots = NULL;
  queue->capacity = 0;
  queue->count = 0;
  queue->head = 0;
}

void qa_bounded_queue_push(qa_bounded_queue *queue, const void *value) {
  if (queue->count >= queue->capacity) {
    memcpy(slot_at(queue, queue->head), value, queue->elem_size);
    queue->head = wrap(queue, queue->head + 1);
    queue->dropped++;
    return;
  }
  memcpy(slot_at(queue, wrap(queue, queue->head + queue->count)), value,
         queue->elem_size);
  queue->count++;
}

bool qa_bounded_queue_shift(qa_bounded_queue *queue, void *out) {
  if (queue->count == 0) return false;
  if (out != NULL) memcpy(out, slot_at(queue, queue->head), queue->elem_size);
  memset(slot_at(queue, queue->head), 0, queue->elem_size);
  queue->head = wrap(queue, queue->head + 1);
  queue->count--;
  return true;
}

const void *qa_bounded_queue_peek_oldest(const qa_bounded_queue *queue) {
  return queue->count == 0 ? NULL : slot_at_const(queue, queue->head);
}

const void *qa_bounded_queue_peek_newest(const qa_bounded_queue *queue) {
  return queue->count == 0 ? NULL
                            : slot_at_const(queue, wrap(queue, queue->head + queue->count - 1));
}

void qa_bounded_queue_for_each(const qa_bounded_queue *queue,
                               qa_bounded_visit_fn visit, void *ctx) {
  for (size_t index = 0; index < queue->count; index++) {
    visit(slot_at_const(queue, wrap(queue, queue->head + index)), index, ctx);
  }
}

const void *qa_bounded_queue_at(const qa_bounded_queue *queue, size_t index) {
  if (index >= queue->count) return NULL;
  return slot_at_const(queue, wrap(queue, queue->head + index));
}

int64_t qa_bounded_queue_find_index(const qa_bounded_queue *queue,
                                    qa_bounded_pred_fn predicate, void *ctx) {
  for (size_t index = 0; index < queue->count; index++) {
    if (predicate(slot_at_const(queue, wrap(queue, queue->head + index)), ctx)) {
      return (int64_t)index;
    }
  }
  return -1;
}

void qa_bounded_queue_set_at(qa_bounded_queue *queue, size_t index, const void *value) {
  if (index == queue->count) {
    qa_bounded_queue_push(queue, value);
    return;
  }
  if (index > queue->count) {
    qa_range_error("Bounded queue index is outside retained values");
  }
  memcpy(slot_at(queue, wrap(queue, queue->head + index)), value, queue->elem_size);
}

bool qa_bounded_queue_remove_first(qa_bounded_queue *queue,
                                   qa_bounded_pred_fn predicate, void *ctx) {
  for (size_t index = 0; index < queue->count; index++) {
    if (!predicate(slot_at_const(queue, wrap(queue, queue->head + index)), ctx)) {
      continue;
    }
    for (size_t move = index; move + 1 < queue->count; move++) {
      memcpy(slot_at(queue, wrap(queue, queue->head + move)),
             slot_at(queue, wrap(queue, queue->head + move + 1)), queue->elem_size);
    }
    memset(slot_at(queue, wrap(queue, queue->head + queue->count - 1)), 0,
           queue->elem_size);
    queue->count--;
    return true;
  }
  return false;
}

void qa_bounded_queue_retain(qa_bounded_queue *queue,
                             qa_bounded_pred_fn predicate, void *ctx) {
  size_t write = 0;
  for (size_t index = 0; index < queue->count; index++) {
    const void *value = slot_at_const(queue, wrap(queue, queue->head + index));
    if (!predicate(value, ctx)) continue;
    if (write != index) {
      memcpy(slot_at(queue, wrap(queue, queue->head + write)), value, queue->elem_size);
    }
    write++;
  }
  for (size_t index = write; index < queue->count; index++) {
    memset(slot_at(queue, wrap(queue, queue->head + index)), 0, queue->elem_size);
  }
  queue->count = write;
}

void *qa_bounded_queue_to_array(const qa_bounded_queue *queue, size_t *out_count) {
  *out_count = queue->count;
  if (queue->count == 0) return NULL;
  uint8_t *out = (uint8_t *)malloc(queue->count * queue->elem_size);
  if (out == NULL) {
    *out_count = 0;
    return NULL;
  }
  for (size_t index = 0; index < queue->count; index++) {
    memcpy(out + index * queue->elem_size,
           slot_at_const(queue, wrap(queue, queue->head + index)), queue->elem_size);
  }
  return out;
}

void *qa_bounded_queue_take_all(qa_bounded_queue *queue, size_t *out_count) {
  void *out = qa_bounded_queue_to_array(queue, out_count);
  qa_bounded_queue_clear(queue);
  return out;
}

void qa_bounded_queue_clear(qa_bounded_queue *queue) {
  if (queue->slots != NULL) memset(queue->slots, 0, queue->capacity * queue->elem_size);
  queue->head = 0;
  queue->count = 0;
}
