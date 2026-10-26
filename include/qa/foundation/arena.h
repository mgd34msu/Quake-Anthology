/*
 * Port of src/foundation/arena.ts (ByteArena): load-sized bump arena over
 * one retained byte buffer. Capacity is reserved at construction,
 * allocations are pointer bumps at fixed offsets until reset, and
 * exhaustion is counted explicitly. GPL-2.0-or-later.
 */
#ifndef QA_FOUNDATION_ARENA_H
#define QA_FOUNDATION_ARENA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct qa_byte_arena {
  uint8_t *storage;
  size_t capacity;
  size_t used;
  int64_t overflows;
  bool sealed;
} qa_byte_arena;

/* Returns false only on allocation failure; invalid capacity aborts. */
bool qa_byte_arena_init(qa_byte_arena *arena, size_t capacity_bytes);
void qa_byte_arena_destroy(qa_byte_arena *arena);

/* Records that no further reservation may occur; layout is already fixed.
 * Like the TypeScript, this only records the flag. */
void qa_byte_arena_seal(qa_byte_arena *arena);

/* Returns the allocation offset, or -1 when capacity is exhausted. */
int64_t qa_byte_arena_alloc(qa_byte_arena *arena, size_t size, size_t alignment);

/* Borrowed view of a previously allocated range. Valid until reset. */
uint8_t *qa_byte_arena_view(qa_byte_arena *arena, int64_t offset, size_t size);

/* Copies bytes into the arena; returns the offset or -1 when full. */
int64_t qa_byte_arena_alloc_copy(qa_byte_arena *arena, const uint8_t *bytes,
                                 size_t length, size_t alignment);

/* Invalidates every allocation but keeps the buffer and overflow count. */
void qa_byte_arena_reset(qa_byte_arena *arena);

#endif
