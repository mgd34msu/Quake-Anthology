/* Port of src/foundation/id-index.ts. GPL-2.0-or-later. */
#include "qa/foundation/id_index.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

enum { NO_ENTRY = -1 };

static size_t hash_of(uint32_t key, uint32_t discriminator, size_t mask) {
  /* Math.imul wraps at 32 bits; masking keeps the low bits, identical here. */
  uint32_t mixed = (key * 0x9e3779b1u) ^ (discriminator * 0x85ebca6bu);
  return (size_t)mixed & mask;
}

void qa_id_index_init(qa_id_index *index) {
  index->keys = NULL;
  index->discriminators = NULL;
  index->values = NULL;
  index->next_entry = NULL;
  index->buckets = NULL;
  index->bucket_count = 0;
  index->keys_capacity = 0;
  index->entry_count = 0;
  index->reserved_count = 0;
}

void qa_id_index_destroy(qa_id_index *index) {
  free(index->keys);
  free(index->discriminators);
  free(index->values);
  free(index->next_entry);
  free(index->buckets);
  qa_id_index_init(index);
}

void qa_id_index_reserve(qa_id_index *index, int64_t additional) {
  if (additional < 0 || additional > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Id index reservation must be a nonnegative safe integer");
  }
  int64_t needed = index->entry_count + additional;
  if (needed > index->reserved_count) index->reserved_count = needed;
  if (needed <= (int64_t)index->keys_capacity && index->bucket_count != 0) return;
  size_t capacity = index->keys_capacity == 0 ? 64 : index->keys_capacity;
  while (capacity < (size_t)needed) capacity *= 2;
  uint32_t *keys = (uint32_t *)calloc(capacity, sizeof(uint32_t));
  uint32_t *discriminators = (uint32_t *)calloc(capacity, sizeof(uint32_t));
  int32_t *next_entry = (int32_t *)malloc(capacity * sizeof(int32_t));
  int64_t *values = (int64_t *)malloc(capacity * sizeof(int64_t));
  if (keys == NULL || discriminators == NULL || next_entry == NULL || values == NULL) {
    free(keys);
    free(discriminators);
    free(next_entry);
    free(values);
    qa_range_error("Id index reservation must be a nonnegative safe integer");
  }
  if (index->entry_count > 0) {
    memcpy(keys, index->keys, (size_t)index->entry_count * sizeof(uint32_t));
    memcpy(discriminators, index->discriminators,
           (size_t)index->entry_count * sizeof(uint32_t));
    memcpy(values, index->values, (size_t)index->entry_count * sizeof(int64_t));
  }
  for (size_t i = 0; i < capacity; i++) next_entry[i] = NO_ENTRY;
  size_t bucket_count = 128;
  while (bucket_count < capacity * 2) bucket_count *= 2;
  int32_t *buckets = (int32_t *)malloc(bucket_count * sizeof(int32_t));
  if (buckets == NULL) {
    free(keys);
    free(discriminators);
    free(next_entry);
    free(values);
    qa_range_error("Id index reservation must be a nonnegative safe integer");
  }
  for (size_t i = 0; i < bucket_count; i++) buckets[i] = NO_ENTRY;
  size_t mask = bucket_count - 1;
  for (int64_t entry = 0; entry < index->entry_count; entry++) {
    size_t bucket = hash_of(keys[entry], discriminators[entry], mask);
    next_entry[entry] = buckets[bucket];
    buckets[bucket] = (int32_t)entry;
  }
  free(index->keys);
  free(index->discriminators);
  free(index->values);
  free(index->next_entry);
  free(index->buckets);
  index->keys = keys;
  index->discriminators = discriminators;
  index->values = values;
  index->next_entry = next_entry;
  index->buckets = buckets;
  index->bucket_count = bucket_count;
  index->keys_capacity = capacity;
}

void qa_id_index_insert(qa_id_index *index, uint32_t key, uint32_t discriminator,
                        int64_t value) {
  if (index->entry_count >= index->reserved_count ||
      index->entry_count >= (int64_t)index->keys_capacity) {
    qa_range_error("Id index insertion exceeds its reservation; reserve at load");
  }
  if (value < 0 || value > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Id index values are nonnegative caller indices");
  }
  int64_t entry = index->entry_count++;
  index->keys[entry] = key;
  index->discriminators[entry] = discriminator;
  index->values[entry] = value;
  size_t bucket = hash_of(key, discriminator, index->bucket_count - 1);
  index->next_entry[entry] = index->buckets[bucket];
  index->buckets[bucket] = (int32_t)entry;
}

int64_t qa_id_index_find(const qa_id_index *index, uint32_t key, uint32_t discriminator) {
  if (index->bucket_count == 0) return -1;
  int32_t entry = index->buckets[hash_of(key, discriminator, index->bucket_count - 1)];
  while (entry != NO_ENTRY) {
    if (index->keys[entry] == key && index->discriminators[entry] == discriminator) {
      return index->values[entry];
    }
    entry = index->next_entry[entry];
  }
  return -1;
}

qa_id_index_cursor qa_id_index_begin(const qa_id_index *index, uint32_t key,
                                     uint32_t discriminator) {
  qa_id_index_cursor cursor;
  cursor.key = key;
  cursor.discriminator = discriminator;
  cursor.entry = index->bucket_count == 0
                     ? NO_ENTRY
                     : index->buckets[hash_of(key, discriminator,
                                              index->bucket_count - 1)];
  return cursor;
}

int64_t qa_id_index_next(const qa_id_index *index, qa_id_index_cursor *cursor) {
  int32_t entry = cursor->entry;
  while (entry != NO_ENTRY) {
    int32_t following = index->next_entry[entry];
    if (index->keys[entry] == cursor->key &&
        index->discriminators[entry] == cursor->discriminator) {
      cursor->entry = following;
      return index->values[entry];
    }
    entry = following;
  }
  cursor->entry = NO_ENTRY;
  return -1;
}

typedef struct remove_ctx {
  uint32_t key;
  uint32_t discriminator;
  int64_t value;
  int by_value;
} remove_ctx;

static bool keep_unless_match(uint32_t key, uint32_t discriminator, int64_t value,
                              void *raw) {
  const remove_ctx *ctx = (const remove_ctx *)raw;
  if (key != ctx->key || discriminator != ctx->discriminator) return true;
  return ctx->by_value && value != ctx->value;
}

bool qa_id_index_remove(qa_id_index *index, uint32_t key, uint32_t discriminator) {
  remove_ctx ctx = {key, discriminator, 0, 0};
  return qa_id_index_retain(index, keep_unless_match, &ctx) > 0;
}

bool qa_id_index_remove_value(qa_id_index *index, uint32_t key,
                              uint32_t discriminator, int64_t value) {
  remove_ctx ctx = {key, discriminator, value, 1};
  return qa_id_index_retain(index, keep_unless_match, &ctx) > 0;
}

int64_t qa_id_index_retain(qa_id_index *index, qa_id_index_keep_fn keep, void *ctx) {
  int64_t write = 0;
  for (int64_t entry = 0; entry < index->entry_count; entry++) {
    uint32_t key = index->keys[entry];
    uint32_t discriminator = index->discriminators[entry];
    int64_t value = index->values[entry];
    if (!keep(key, discriminator, value, ctx)) continue;
    if (write != entry) {
      index->keys[write] = key;
      index->discriminators[write] = discriminator;
      index->values[write] = value;
    }
    write++;
  }
  int64_t removed = index->entry_count - write;
  index->entry_count = write;
  for (size_t i = 0; i < index->bucket_count; i++) index->buckets[i] = NO_ENTRY;
  if (index->bucket_count != 0) {
    size_t mask = index->bucket_count - 1;
    /* Loop direction matches the TypeScript retain() exactly. */
    for (int64_t entry = index->entry_count - 1; entry >= 0; entry--) {
      size_t bucket = hash_of(index->keys[entry], index->discriminators[entry], mask);
      index->next_entry[entry] = index->buckets[bucket];
      index->buckets[bucket] = (int32_t)entry;
    }
  }
  return removed;
}

void qa_id_index_clear(qa_id_index *index) {
  index->entry_count = 0;
  for (size_t i = 0; i < index->bucket_count; i++) index->buckets[i] = NO_ENTRY;
}
