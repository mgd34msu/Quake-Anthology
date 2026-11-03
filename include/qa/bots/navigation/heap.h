/* Port of src/bots/navigation/heap.ts. GPL-2.0-or-later.
 * Binary min-heap shared by the shortest-path searches in navigation; ties pop in heap order.
 * TS MinHeap<T>: elements are copied by value (element_size bytes); key(value) is the TS key closure. */
#ifndef QA_BOTS_NAVIGATION_HEAP_H
#define QA_BOTS_NAVIGATION_HEAP_H
#include <stdbool.h>
#include <stddef.h>
typedef struct qa_min_heap {
  unsigned char *values;
  size_t count, capacity, element_size;
  double (*key)(const void *value);
} qa_min_heap;
void qa_min_heap_init(qa_min_heap *heap, size_t element_size, double (*key)(const void *value));
void qa_min_heap_destroy(qa_min_heap *heap);
static inline size_t qa_min_heap_size(const qa_min_heap *heap) { return heap->count; }
/* False only on OOM. */
bool qa_min_heap_push(qa_min_heap *heap, const void *value);
/* False is TS null (empty heap). */
bool qa_min_heap_pop(qa_min_heap *heap, void *out);
#endif
