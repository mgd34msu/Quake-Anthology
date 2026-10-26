/*
 * Port of src/foundation/id-index.ts (IdIndex): (string-id,
 * discriminator) hash index over integer values. Storage is reserved
 * before insertion; duplicate keys retain admission order.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_ID_INDEX_H
#define QA_FOUNDATION_ID_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_id_index_cursor {
  int32_t entry;
  uint32_t key;
  uint32_t discriminator;
} qa_id_index_cursor;

typedef bool (*qa_id_index_keep_fn)(uint32_t key, uint32_t discriminator,
                                    int64_t value, void *ctx);

typedef struct qa_id_index {
  uint32_t *keys;
  uint32_t *discriminators;
  int64_t *values;
  int32_t *next_entry;
  int32_t *buckets;
  size_t bucket_count;
  size_t keys_capacity;
  int64_t entry_count;
  int64_t reserved_count;
} qa_id_index;

void qa_id_index_init(qa_id_index *index);
void qa_id_index_destroy(qa_id_index *index);

/* Grows entry storage and rehashes buckets. Cold path: call at load. */
void qa_id_index_reserve(qa_id_index *index, int64_t additional);
/* Inserts one entry. Requires a prior reserve covering the new count. */
void qa_id_index_insert(qa_id_index *index, uint32_t key, uint32_t discriminator,
                        int64_t value);
/* Returns the most recently inserted matching value, or -1. */
int64_t qa_id_index_find(const qa_id_index *index, uint32_t key, uint32_t discriminator);
qa_id_index_cursor qa_id_index_begin(const qa_id_index *index, uint32_t key,
                                     uint32_t discriminator);
/* Advances to the next matching value in reverse admission order, or -1. */
int64_t qa_id_index_next(const qa_id_index *index, qa_id_index_cursor *cursor);
bool qa_id_index_remove(qa_id_index *index, uint32_t key, uint32_t discriminator);
bool qa_id_index_remove_value(qa_id_index *index, uint32_t key,
                              uint32_t discriminator, int64_t value);
/* Keeps selected entries in admission order. Returns removals. */
int64_t qa_id_index_retain(qa_id_index *index, qa_id_index_keep_fn keep, void *ctx);
void qa_id_index_clear(qa_id_index *index);

#endif
