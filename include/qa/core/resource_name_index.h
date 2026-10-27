/*
 * Port of src/core/resource-name-index.ts (ResourceNameIndex): mirrors
 * source lookup order — a matching name must occur before the first
 * free slot. C accommodations: names are owned copies; the TS
 * `number | null` find result uses -1 for null (valid results are
 * >= 0). Slot indices are int64_t like the TS safe-integer checks.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_RESOURCE_NAME_INDEX_H
#define QA_CORE_RESOURCE_NAME_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_resource_name_slots {
  char *name;
  int64_t *slots;
  size_t slot_count;
  size_t slot_capacity;
  int64_t first;
} qa_resource_name_slots;

typedef struct qa_resource_name_index {
  char **values;
  size_t capacity;
  int64_t *reserved;
  size_t reserved_count;
  qa_resource_name_slots *names;
  size_t name_count;
  size_t name_capacity;
  int64_t free_slot;
} qa_resource_name_index;

/* Returns false only on allocation failure; invalid capacity aborts. */
bool qa_resource_name_index_init(qa_resource_name_index *index,
                                 int64_t capacity, const int64_t *reserved,
                                 size_t reserved_count);
void qa_resource_name_index_destroy(qa_resource_name_index *index);
void qa_resource_name_index_clear(qa_resource_name_index *index);
/* Out-of-range and reserved slots are silently ignored, as in the TS. */
void qa_resource_name_index_set(qa_resource_name_index *index, int64_t slot,
                                const char *value);
/* First matching slot before the first hole, else the hole, else -1. */
int64_t qa_resource_name_index_find(const qa_resource_name_index *index,
                                    const char *name);

#endif
