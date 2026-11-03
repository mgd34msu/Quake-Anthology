/* Port of src/foundation/stamp.ts. GPL-2.0-or-later. */
#include "qa/foundation/stamp.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

bool qa_stamp_set_init(qa_stamp_set *set, size_t capacity) {
  if (capacity > (size_t)QA_MAX_SAFE_INTEGER) {
    qa_range_error("Stamp capacity must be a nonnegative safe integer");
  }
  set->marks = capacity == 0 ? NULL : (uint32_t *)calloc(capacity, sizeof(uint32_t));
  if (capacity != 0 && set->marks == NULL) {
    set->capacity = 0;
    set->epoch = 1;
    return false;
  }
  set->capacity = capacity;
  set->epoch = 1;
  return true;
}

void qa_stamp_set_destroy(qa_stamp_set *set) {
  free(set->marks);
  set->marks = NULL;
  set->capacity = 0;
}

void qa_stamp_set_begin(qa_stamp_set *set) {
  /* TS: the epoch re-zeroes the marks once it reaches the uint32 ceiling. */
  if (set->epoch >= 0xffffffffu) {
    if (set->capacity != 0) memset(set->marks, 0, set->capacity * sizeof(uint32_t));
    set->epoch = 1;
  } else {
    set->epoch++;
  }
}

bool qa_stamp_set_test(const qa_stamp_set *set, size_t index) {
  if (index >= set->capacity) return false;
  return (uint64_t)set->marks[index] == set->epoch;
}

bool qa_stamp_set_mark(qa_stamp_set *set, size_t index) {
  if (index >= set->capacity) return true;
  if ((uint64_t)set->marks[index] == set->epoch) return false;
  set->marks[index] = (uint32_t)set->epoch;
  return true;
}

uint32_t qa_stamp_set_value(const qa_stamp_set *set, size_t index) {
  if (index >= set->capacity) return 0;
  return set->marks[index];
}

void qa_stamp_set_restore_mark(qa_stamp_set *set, size_t index, uint32_t value) {
  if (index < set->capacity) set->marks[index] = value;
}
