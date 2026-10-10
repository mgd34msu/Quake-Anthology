#include "qa/arena.h"
#include "qa/allocation_gate.h"
#include "qa/pool.h"

#include <stdlib.h>
#include <string.h>

#define QA_ARENA_DEFAULT_BLOCK_SIZE 16384u

struct qa_arena_block {
    qa_arena_block *next;
    size_t capacity;
    size_t used;
    size_t slot, pages;
    uint8_t data[];
};

void qa_arena_init(qa_arena *arena, size_t block_size)
{
    if (arena != NULL) {
        *arena = (qa_arena){.block_size = block_size};
    }
}

bool qa_arena_init_buffer(qa_arena *arena,void *data,size_t size,qa_error *error)
{
    size_t padding=(size_t)(-(uintptr_t)data)&(_Alignof(qa_arena_block)-1);
    if(!arena || !data || padding>=size || size-padding<=sizeof(qa_arena_block)) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"arena buffer requires writable block storage");
        return false;
    }
    qa_arena_block *block=(qa_arena_block *)((uint8_t *)data+padding);
    *block=(qa_arena_block){.capacity=size-padding-sizeof(*block)};
    *arena=(qa_arena){.first=block,.current=block,.sealed=true,.borrowed=true};
    return true;
}

void qa_arena_init_pool(qa_arena *arena, qa_pool *pages)
{
    *arena=(qa_arena){.sealed=true,.pages=pages,
        .block_size=pages->stride>sizeof(qa_arena_block)?pages->stride-sizeof(qa_arena_block):1};
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

static qa_arena_block *block_create(qa_arena *arena, size_t capacity,
                                    qa_arena_block *last, qa_error *error)
{
    if (arena->sealed && !arena->pages) {
        if (arena->overflow_count != SIZE_MAX) ++arena->overflow_count;
        qa_allocation_gate_capacity_exhausted();
        qa_error_set(error, QA_ERROR_MEMORY, 0, "sealed arena capacity exhausted");
        return NULL;
    }
    if (capacity > (size_t)PTRDIFF_MAX - sizeof(qa_arena_block)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "arena block size overflow");
        return NULL;
    }
    qa_arena_block *block;
    size_t slot=0, pages=0;
    if(arena->pages) {
        size_t bytes=sizeof(*block)+capacity;
        pages=bytes/arena->pages->stride+(bytes%arena->pages->stride!=0);
        block=qa_pool_take_run(arena->pages,pages,&slot);
        if(block) capacity=pages*arena->pages->stride-sizeof(*block);
        else if(arena->overflow_count!=SIZE_MAX) ++arena->overflow_count;
    } else block=malloc(sizeof(*block)+capacity);
    if (block == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot allocate %zu-byte arena block", capacity);
        return NULL;
    }
    block->next = NULL;
    block->capacity = capacity;
    block->used = 0;
    block->slot=slot; block->pages=pages;
    if (last != NULL) last->next = block;
    else arena->first = block;
    if (arena->current == NULL) arena->current = block;
    return block;
}

bool qa_arena_reserve(qa_arena *arena, size_t capacity, qa_error *error)
{
    if (arena == NULL || capacity == 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "arena reservation requires positive capacity");
        return false;
    }
    qa_arena_block *last = NULL;
    for (qa_arena_block *block = arena->first; block != NULL; block = block->next) {
        if (block->capacity >= capacity) return true;
        last = block;
    }
    return block_create(arena, capacity, last, error) != NULL;
}

void qa_arena_seal(qa_arena *arena)
{
    if (arena != NULL) arena->sealed = true;
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
    qa_arena_block *block = block_create(arena, capacity, last, error);
    if (block == NULL) return NULL;
    arena->current = block;
    return block_alloc(block, size, alignment);
}

void *qa_arena_grow(qa_arena *arena,void *data,size_t old_size,size_t new_size,
    size_t alignment,qa_error *error)
{
    if (data && new_size<=old_size) return data;
    void *next=qa_arena_alloc(arena,new_size,alignment,error);
    if (next && old_size) memcpy(next,data,old_size);
    return next;
}

void qa_arena_reset(qa_arena *arena)
{
    if (arena != NULL) {
        if(arena->pages) {
            qa_arena_block *block=arena->first;
            while(block) {
                qa_arena_block *next=block->next;
                qa_pool_release_run(arena->pages,block->slot,block->pages);
                block=next;
            }
            arena->first=arena->current=NULL;
            return;
        }
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
            if(arena->pages) qa_pool_release_run(arena->pages,block->slot,block->pages);
            else if(!arena->borrowed)free(block);
            block = next;
        }
        *arena = (qa_arena){0};
    }
}
