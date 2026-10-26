/* Port of src/foundation/arena.ts. GPL-2.0-or-later. */
#include "qa/foundation/arena.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

bool qa_byte_arena_init(qa_byte_arena *arena, size_t capacity_bytes) {
  if (capacity_bytes == 0 || capacity_bytes > (size_t)QA_MAX_SAFE_INTEGER) {
    qa_range_error("Arena capacity must be a positive safe integer");
  }
  arena->storage = (uint8_t *)malloc(capacity_bytes);
  if (arena->storage == NULL) {
    arena->capacity = 0;
    arena->used = 0;
    arena->overflows = 0;
    arena->sealed = false;
    return false;
  }
  arena->capacity = capacity_bytes;
  arena->used = 0;
  arena->overflows = 0;
  arena->sealed = false;
  return true;
}

void qa_byte_arena_destroy(qa_byte_arena *arena) {
  free(arena->storage);
  arena->storage = NULL;
  arena->capacity = 0;
  arena->used = 0;
}

void qa_byte_arena_seal(qa_byte_arena *arena) { arena->sealed = true; }

int64_t qa_byte_arena_alloc(qa_byte_arena *arena, size_t size, size_t alignment) {
  if (size == 0 || size > (size_t)QA_MAX_SAFE_INTEGER || alignment == 0 ||
      alignment > (size_t)QA_MAX_SAFE_INTEGER || (alignment & (alignment - 1)) != 0) {
    qa_range_error("Arena allocation requires a positive size and power-of-two alignment");
  }
  size_t padding = (alignment - (arena->used % alignment)) % alignment;
  if (padding > arena->capacity - arena->used ||
      size > arena->capacity - arena->used - padding) {
    if (arena->overflows < QA_MAX_SAFE_INTEGER) arena->overflows++;
    return -1;
  }
  size_t offset = arena->used + padding;
  arena->used = offset + size;
  return (int64_t)offset;
}

uint8_t *qa_byte_arena_view(qa_byte_arena *arena, int64_t offset, size_t size) {
  if (offset < 0 || size > (size_t)QA_MAX_SAFE_INTEGER ||
      (uint64_t)offset > (uint64_t)arena->used ||
      size > arena->used - (size_t)offset) {
    qa_range_error("Arena view is outside the allocated range");
  }
  return arena->storage + (size_t)offset;
}

int64_t qa_byte_arena_alloc_copy(qa_byte_arena *arena, const uint8_t *bytes,
                                 size_t length, size_t alignment) {
  if (length == 0) return (int64_t)arena->used;
  int64_t offset = qa_byte_arena_alloc(arena, length, alignment);
  if (offset >= 0) memcpy(arena->storage + (size_t)offset, bytes, length);
  return offset;
}

void qa_byte_arena_reset(qa_byte_arena *arena) { arena->used = 0; }
