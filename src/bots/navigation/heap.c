/* Port of src/bots/navigation/heap.ts. GPL-2.0-or-later. */
#include "qa/bots/navigation/heap.h"
#include <stdlib.h>
#include <string.h>

void qa_min_heap_init(qa_min_heap *heap, size_t element_size, double (*key)(const void *value)) {
  memset(heap, 0, sizeof *heap);
  heap->element_size = element_size;
  heap->key = key;
}
void qa_min_heap_destroy(qa_min_heap *heap) {
  free(heap->values);
  heap->values = NULL;
  heap->count = heap->capacity = 0;
}
static unsigned char *slot(qa_min_heap *heap, size_t index) { return heap->values + index * heap->element_size; }
bool qa_min_heap_push(qa_min_heap *heap, const void *value) {
  if (heap->count == heap->capacity) {
    size_t capacity = heap->capacity ? heap->capacity * 2 : 16;
    unsigned char *grown = realloc(heap->values, capacity * heap->element_size);
    if (grown == NULL) return false;
    heap->values = grown;
    heap->capacity = capacity;
  }
  double cost = heap->key(value);
  size_t index = heap->count++;
  while (index > 0) {
    size_t parent_index = (index - 1) / 2;
    if (heap->key(slot(heap, parent_index)) <= cost) break;
    memcpy(slot(heap, index), slot(heap, parent_index), heap->element_size);
    index = parent_index;
  }
  memcpy(slot(heap, index), value, heap->element_size);
  return true;
}
bool qa_min_heap_pop(qa_min_heap *heap, void *out) {
  if (heap->count == 0) return false;
  memcpy(out, slot(heap, 0), heap->element_size);
  heap->count--;
  if (heap->count == 0) return true;
  /* The tail element now sits just past the shrunken range; the sift below writes only slots inside it. */
  const unsigned char *tail = slot(heap, heap->count);
  double tail_cost = heap->key(tail);
  size_t index = 0;
  while (index * 2 + 1 < heap->count) {
    size_t child_index = index * 2 + 1;
    if (child_index + 1 < heap->count && heap->key(slot(heap, child_index + 1)) < heap->key(slot(heap, child_index)))
      child_index++;
    if (tail_cost <= heap->key(slot(heap, child_index))) break;
    memcpy(slot(heap, index), slot(heap, child_index), heap->element_size);
    index = child_index;
  }
  memcpy(slot(heap, index), tail, heap->element_size);
  return true;
}
