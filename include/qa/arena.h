#ifndef QA_ARENA_H
#define QA_ARENA_H

#include "qa/common.h"

typedef struct qa_arena_block qa_arena_block;

/* Zero initialization is valid. Arena ownership cannot be copied. */
typedef struct qa_arena {
    qa_arena_block *first;
    qa_arena_block *current;
    size_t block_size;
    size_t overflow_count;
    bool sealed;
} qa_arena;

/* Initialize an unused arena. A zero block size selects the default. */
void qa_arena_init(qa_arena *arena, size_t block_size);
/* Reserve a reusable block during load, then forbid later heap growth. Reset
 * preserves the reservation, seal and overflow count. */
bool qa_arena_reserve(qa_arena *arena, size_t capacity, qa_error *error);
void qa_arena_seal(qa_arena *arena);
/* Allocations stay at fixed addresses until reset/destroy. Size must be positive
 * and alignment a power of two. Memory is uninitialized. Failures leave existing
 * allocations valid. Arenas are owned by one thread at a time. */
void *qa_arena_alloc(qa_arena *arena, size_t size, size_t alignment, qa_error *error);
/* Invalidates every allocation but keeps blocks for reuse. */
void qa_arena_reset(qa_arena *arena);
/* Invalidates allocations, releases blocks and returns to zero-initialized state. */
void qa_arena_destroy(qa_arena *arena);

#endif
