/*
 * Port of src/foundation/bounded-queue.ts (BoundedQueue<T>): fixed
 * capacity drop-oldest history over one ring. C accommodation: elements
 * are elem_size byte blocks copied in/out; TypeScript stores references,
 * so pointer payloads should store the pointer itself. Unlike the
 * TypeScript, iteration visits stored NULL pointer payloads (the
 * TypeScript skips null values it can never retain via push of T).
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_BOUNDED_QUEUE_H
#define QA_FOUNDATION_BOUNDED_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*qa_bounded_visit_fn)(const void *value, size_t index, void *ctx);
typedef bool (*qa_bounded_pred_fn)(const void *value, void *ctx);

typedef struct qa_bounded_queue {
  uint8_t *slots;
  size_t elem_size;
  size_t capacity;
  bool power_of_two;
  size_t head;
  size_t count;
  int64_t dropped;
} qa_bounded_queue;

/* Returns false only on allocation failure; capacity < 1 aborts. */
bool qa_bounded_queue_init(qa_bounded_queue *queue, size_t capacity, size_t elem_size);
void qa_bounded_queue_destroy(qa_bounded_queue *queue);

/* Appends one value, discarding the oldest when full. */
void qa_bounded_queue_push(qa_bounded_queue *queue, const void *value);
/* Removes the oldest value into out. Returns false when empty. */
bool qa_bounded_queue_shift(qa_bounded_queue *queue, void *out);
const void *qa_bounded_queue_peek_oldest(const qa_bounded_queue *queue);
const void *qa_bounded_queue_peek_newest(const qa_bounded_queue *queue);
void qa_bounded_queue_for_each(const qa_bounded_queue *queue,
                               qa_bounded_visit_fn visit, void *ctx);
const void *qa_bounded_queue_at(const qa_bounded_queue *queue, size_t index);
int64_t qa_bounded_queue_find_index(const qa_bounded_queue *queue,
                                    qa_bounded_pred_fn predicate, void *ctx);
void qa_bounded_queue_set_at(qa_bounded_queue *queue, size_t index, const void *value);
bool qa_bounded_queue_remove_first(qa_bounded_queue *queue,
                                   qa_bounded_pred_fn predicate, void *ctx);
void qa_bounded_queue_retain(qa_bounded_queue *queue,
                             qa_bounded_pred_fn predicate, void *ctx);
/* Malloc'd copy of retained values oldest first; caller frees. */
void *qa_bounded_queue_to_array(const qa_bounded_queue *queue, size_t *out_count);
/* to_array plus clear. */
void *qa_bounded_queue_take_all(qa_bounded_queue *queue, size_t *out_count);
void qa_bounded_queue_clear(qa_bounded_queue *queue);

#endif
