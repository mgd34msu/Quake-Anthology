/* Port of src/foundation/pool.ts. GPL-2.0-or-later. */
#include "qa/foundation/pool.h"

#include <stdlib.h>

#include "qa/foundation/support.h"

enum { SLOT_FREE_END = -1, SLOT_USED = -2 };

bool qa_fixed_pool_init(qa_fixed_pool *pool, int32_t capacity, size_t elem_size,
                        qa_pool_create_fn create, void *create_ctx) {
  if (capacity < 0) qa_range_error("Pool capacity must fit a signed slot index");
  pool->values = NULL;
  pool->next = NULL;
  pool->elem_size = elem_size;
  pool->capacity = capacity;
  pool->active = 0;
  pool->peak = 0;
  pool->overflows = 0;
  if (capacity == 0) {
    pool->head = SLOT_FREE_END;
    return true;
  }
  pool->values = (uint8_t *)calloc((size_t)capacity, elem_size == 0 ? 1 : elem_size);
  pool->next = (int32_t *)malloc((size_t)capacity * sizeof(int32_t));
  if (pool->values == NULL || pool->next == NULL) {
    free(pool->values);
    free(pool->next);
    pool->values = NULL;
    pool->next = NULL;
    pool->head = SLOT_FREE_END;
    return false;
  }
  for (int32_t slot = 0; slot < capacity; slot++) {
    if (create != NULL) create(slot, pool->values + (size_t)slot * elem_size, create_ctx);
    pool->next[slot] = slot + 1 < capacity ? slot + 1 : SLOT_FREE_END;
  }
  pool->head = 0;
  return true;
}

void qa_fixed_pool_destroy(qa_fixed_pool *pool) {
  free(pool->values);
  free(pool->next);
  pool->values = NULL;
  pool->next = NULL;
  pool->capacity = 0;
  pool->head = SLOT_FREE_END;
  pool->active = 0;
}

int32_t qa_fixed_pool_take_slot(qa_fixed_pool *pool) {
  int32_t slot = pool->head;
  if (slot == SLOT_FREE_END) {
    if (pool->overflows < QA_MAX_SAFE_INTEGER) pool->overflows++;
    return -1;
  }
  pool->head = pool->next[slot];
  pool->next[slot] = SLOT_USED;
  pool->active++;
  if (pool->active > pool->peak) pool->peak = pool->active;
  return slot;
}

void *qa_fixed_pool_at(qa_fixed_pool *pool, int32_t slot) {
  if (slot < 0 || slot >= pool->capacity || pool->next[slot] != SLOT_USED) {
    qa_range_error("Pool slot is not leased: %d", slot);
  }
  return pool->values + (size_t)slot * pool->elem_size;
}

bool qa_fixed_pool_is_leased(const qa_fixed_pool *pool, int32_t slot) {
  if (slot < 0 || slot >= pool->capacity) return false;
  return pool->next[slot] == SLOT_USED;
}

void qa_fixed_pool_release(qa_fixed_pool *pool, int32_t slot) {
  if (slot < 0 || slot >= pool->capacity || pool->next[slot] != SLOT_USED) {
    qa_range_error("Pool slot is not leased: %d", slot);
  }
  pool->next[slot] = pool->head;
  pool->head = slot;
  pool->active--;
}

void qa_fixed_pool_reset(qa_fixed_pool *pool) {
  for (int32_t slot = 0; slot < pool->capacity; slot++) {
    pool->next[slot] = slot + 1 < pool->capacity ? slot + 1 : SLOT_FREE_END;
  }
  pool->head = pool->capacity == 0 ? SLOT_FREE_END : 0;
  pool->active = 0;
}
