/*
 * Port of src/foundation/pool.ts (FixedPool<T>): fixed slot pool. Every
 * value is created once at construction, take/release are O(1) free-list
 * operations, and exhaustion is counted rather than extended. Slot
 * numbers are the lease; payloads survive release. C accommodation:
 * payloads are elem_size byte blocks, optionally initialized by callback.
 * GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_POOL_H
#define QA_FOUNDATION_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*qa_pool_create_fn)(int32_t slot, void *value, void *ctx);

typedef struct qa_fixed_pool {
  uint8_t *values;
  size_t elem_size;
  int32_t *next;
  int32_t capacity;
  int32_t head;
  int32_t active;
  int32_t peak;
  int64_t overflows;
} qa_fixed_pool;

/* Returns false only on allocation failure; invalid capacity aborts. */
bool qa_fixed_pool_init(qa_fixed_pool *pool, int32_t capacity, size_t elem_size,
                        qa_pool_create_fn create, void *create_ctx);
void qa_fixed_pool_destroy(qa_fixed_pool *pool);

/* O(1), no allocation. Returns the leased slot, or -1 when exhausted. */
int32_t qa_fixed_pool_take_slot(qa_fixed_pool *pool);
void *qa_fixed_pool_at(qa_fixed_pool *pool, int32_t slot);
bool qa_fixed_pool_is_leased(const qa_fixed_pool *pool, int32_t slot);
void qa_fixed_pool_release(qa_fixed_pool *pool, int32_t slot);
/* Invalidates all leases and retains payloads. */
void qa_fixed_pool_reset(qa_fixed_pool *pool);

#endif
