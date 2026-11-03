/*
 * Port of src/foundation/stamp.ts (StampSet): epoch visit marks.
 * Beginning a query is one epoch increment instead of clearing storage.
 * The epoch never exceeds 0xffffffff: begin() at that value re-zeroes the
 * marks and restarts at 1, as the TypeScript does. Out-of-range indices
 * follow typed-array semantics (reads miss, writes drop).
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_STAMP_H
#define QA_FOUNDATION_STAMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_stamp_set {
  uint32_t *marks;
  size_t capacity;
  uint64_t epoch;
} qa_stamp_set;

/* Returns false only on allocation failure. Capacity 0 is valid. */
bool qa_stamp_set_init(qa_stamp_set *set, size_t capacity);
void qa_stamp_set_destroy(qa_stamp_set *set);

/* Starts a new query. Epoch wrap re-zeroes the marks once. */
void qa_stamp_set_begin(qa_stamp_set *set);
bool qa_stamp_set_test(const qa_stamp_set *set, size_t index);
/* Returns true the first time an index is marked within this epoch. */
bool qa_stamp_set_mark(qa_stamp_set *set, size_t index);
uint32_t qa_stamp_set_value(const qa_stamp_set *set, size_t index);
void qa_stamp_set_restore_mark(qa_stamp_set *set, size_t index, uint32_t value);

#endif
