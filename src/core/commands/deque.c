/* Port of src/core/commands/deque.ts. GPL-2.0-or-later. */
#include "qa/core/commands/deque.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static uint8_t *slot(const qa_deque *deque, size_t index) {
  return deque->items + index * deque->elem_size;
}
static void reserve(qa_deque *deque, size_t capacity) {
  if (capacity <= deque->capacity) return;
  size_t grown = deque->capacity == 0 ? 16 : deque->capacity;
  while (grown < capacity) grown *= 2;
  uint8_t *items = realloc(deque->items, grown * deque->elem_size);
  if (items == NULL) qa_range_error("Out of memory");
  deque->items = items;
  deque->capacity = grown;
}
/* Moves the live records to the front of the storage (TS items.slice(start)). */
static void compact(qa_deque *deque) {
  if (deque->start == 0) return;
  memmove(deque->items, slot(deque, deque->start), (deque->length - deque->start) * deque->elem_size);
  deque->length -= deque->start;
  deque->start = 0;
}

void qa_deque_init(qa_deque *deque, size_t elem_size) {
  memset(deque, 0, sizeof *deque);
  deque->elem_size = elem_size == 0 ? 1 : elem_size;
}
void qa_deque_destroy(qa_deque *deque) {
  free(deque->items);
  memset(deque, 0, sizeof *deque);
}
size_t qa_deque_live(const qa_deque *deque) { return deque->length - deque->start; }
void qa_deque_admit(qa_deque *deque, const void *record) {
  reserve(deque, deque->length + 1);
  memcpy(slot(deque, deque->length), record, deque->elem_size);
  deque->length++;
}
const void *qa_deque_head(const qa_deque *deque) { return qa_deque_at(deque, 0); }
const void *qa_deque_at(const qa_deque *deque, int64_t index) {
  if (index < 0 || (uint64_t)index >= qa_deque_live(deque)) return NULL;
  return slot(deque, deque->start + (size_t)index);
}
void qa_deque_replace_at_offset(qa_deque *deque, int64_t index, const void *record) {
  if (index < 0 || (uint64_t)index >= qa_deque_live(deque)) qa_range_error("Deque offset outside live records");
  memcpy(slot(deque, deque->start + (size_t)index), record, deque->elem_size);
}
void qa_deque_remove_at_offset(qa_deque *deque, int64_t index) {
  if (index < 0 || (uint64_t)index >= qa_deque_live(deque)) qa_range_error("Deque offset outside live records");
  if (index == 0) {
    uint8_t *scratch = malloc(deque->elem_size);
    if (scratch == NULL) qa_range_error("Out of memory");
    (void)qa_deque_shift(deque, scratch);
    free(scratch);
    return;
  }
  const size_t at = deque->start + (size_t)index;
  memmove(slot(deque, at), slot(deque, at + 1), (deque->length - at - 1) * deque->elem_size);
  deque->length--;
}
bool qa_deque_shift(qa_deque *deque, void *out) {
  if (qa_deque_live(deque) == 0) return false;
  if (out != NULL) memcpy(out, slot(deque, deque->start), deque->elem_size);
  deque->start++;
  if (deque->start == deque->length) {
    deque->start = 0;
    deque->length = 0;
  } else if (deque->start >= 64 && deque->start * 2 >= deque->length) {
    compact(deque);
  }
  return true;
}
void qa_deque_prepend(qa_deque *deque, const void *records, size_t count) {
  if (count == 0) return;
  if (deque->start >= count) {
    deque->start -= count;
    memcpy(slot(deque, deque->start), records, count * deque->elem_size);
    return;
  }
  const size_t live = qa_deque_live(deque);
  compact(deque);
  reserve(deque, live + count);
  memmove(slot(deque, count), slot(deque, 0), live * deque->elem_size);
  memcpy(slot(deque, 0), records, count * deque->elem_size);
  deque->length = live + count;
}
void *qa_deque_snapshot(const qa_deque *deque, size_t *out_count) {
  const size_t live = qa_deque_live(deque);
  *out_count = live;
  if (live == 0) return NULL;
  void *copy = malloc(live * deque->elem_size);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, slot(deque, deque->start), live * deque->elem_size);
  return copy;
}
void qa_deque_clear(qa_deque *deque) {
  deque->start = 0;
  deque->length = 0;
}
