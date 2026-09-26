#include "qa/arena.h"

#include <stdlib.h>

#define QA_ARENA_DEFAULT_BLOCK_SIZE 16384u

struct qa_arena_block {
    qa_arena_block *next;
    size_t capacity;
    size_t used;
    uint8_t data[];
};

void qa_arena_init(qa_arena *arena, size_t block_size)
{
    if (arena != NULL) {
        *arena = (qa_arena){.block_size = block_size};
    }
}

static void *block_alloc(qa_arena_block *block, size_t size, size_t alignment)
{
    uintptr_t address = (uintptr_t)(block->data + block->used);
    size_t padding = (size_t)((-(uintptr_t)address) & (alignment - 1));
    size_t available = block->capacity - block->used;
    if (padding > available || size > available - padding) {
        return NULL;
    }
    void *result = block->data + block->used + padding;
    block->used += padding + size;
    return result;
}

void *qa_arena_alloc(qa_arena *arena, size_t size, size_t alignment, qa_error *error)
{
    if (arena == NULL || size == 0 || alignment == 0 ||
        (alignment & (alignment - 1)) != 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "arena allocation requires a positive size and power-of-two alignment");
        return NULL;
    }
    if (size > SIZE_MAX - (alignment - 1)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "arena allocation size overflow");
        return NULL;
    }
    qa_arena_block *last = NULL;
    for (qa_arena_block *block = arena->current; block != NULL; block = block->next) {
        void *result = block_alloc(block, size, alignment);
        if (result != NULL) {
            arena->current = block;
            return result;
        }
        last = block;
    }
    size_t capacity = arena->block_size == 0 ? QA_ARENA_DEFAULT_BLOCK_SIZE : arena->block_size;
    size_t required = size + alignment - 1;
    if (capacity < required) {
        capacity = required;
    }
    if (capacity > (size_t)PTRDIFF_MAX - sizeof(qa_arena_block)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "arena block size overflow");
        return NULL;
    }
    qa_arena_block *block = malloc(sizeof(*block) + capacity);
    if (block == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate %zu-byte arena block", capacity);
        return NULL;
    }
    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;
    if (last != NULL) {
        last->next = block;
    } else {
        arena->first = block;
    }
    arena->current = block;
    return block_alloc(block, size, alignment);
}

void qa_arena_reset(qa_arena *arena)
{
    if (arena != NULL) {
        for (qa_arena_block *block = arena->first; block != NULL; block = block->next) {
            block->used = 0;
        }
        arena->current = arena->first;
    }
}

void qa_arena_destroy(qa_arena *arena)
{
    if (arena != NULL) {
        qa_arena_block *block = arena->first;
        while (block != NULL) {
            qa_arena_block *next = block->next;
            free(block);
            block = next;
        }
        *arena = (qa_arena){0};
    }
}
