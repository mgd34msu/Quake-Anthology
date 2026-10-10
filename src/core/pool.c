#include "qa/pool.h"
#include "qa/allocation_gate.h"

bool qa_pool_prepare(qa_pool *pool, qa_arena *arena, size_t count, size_t stride,
    size_t alignment, qa_error *error)
{
    if (!pool || !arena || !stride || !alignment || (alignment & (alignment - 1))) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
            "pool preparation requires positive stride and power-of-two alignment");
        return false;
    }
    if (stride > SIZE_MAX - (alignment - 1)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "pool stride size overflow");
        return false;
    }
    stride = (stride + alignment - 1) & ~(alignment - 1);
    qa_pool prepared = {.stride = stride, .capacity = count, .head = SIZE_MAX};
    if (!count) {
        *pool = prepared;
        return true;
    }
    if (count > SIZE_MAX / stride || count > SIZE_MAX / sizeof(size_t)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "pool storage size overflow");
        return false;
    }
    size_t values_size = count * stride;
    size_t next_size = count * sizeof(size_t);
    size_t next_alignment = _Alignof(size_t);
    size_t padding = (size_t)(-values_size) & (next_alignment - 1);
    if (values_size > SIZE_MAX - padding || values_size + padding > SIZE_MAX - next_size) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "pool storage size overflow");
        return false;
    }
    size_t storage_alignment = alignment > next_alignment ? alignment : next_alignment;
    prepared.values = qa_arena_alloc(arena, values_size + padding + next_size,
        storage_alignment, error);
    if (!prepared.values) return false;
    prepared.next = (size_t *)(prepared.values + values_size + padding);
    prepared.head = 0;
    for (size_t i = 0; i < count; ++i) prepared.next[i] = i + 1 < count ? i + 1 : SIZE_MAX;
    *pool = prepared;
    return true;
}

void *qa_pool_take(qa_pool *pool, size_t *slot)
{
    *slot = pool->head;
    if (*slot == SIZE_MAX) {
        if (pool->overflow != SIZE_MAX) ++pool->overflow;
        qa_allocation_gate_capacity_exhausted();
        return NULL;
    }
    pool->head = pool->next[*slot];
    pool->next[*slot] = pool->capacity;
    ++pool->active;
    if (pool->active > pool->peak) pool->peak = pool->active;
    return qa_pool_at(pool, *slot);
}

void qa_pool_release(qa_pool *pool, size_t slot)
{
    pool->next[slot] = pool->head;
    pool->head = slot;
    --pool->active;
}

void *qa_pool_at(const qa_pool *pool, size_t slot)
{ return pool->values + slot * pool->stride; }

void *qa_pool_take_run(qa_pool *pool, size_t count, size_t *slot)
{
    if(count==1) return qa_pool_take(pool,slot);
    size_t run=0;
    for(size_t i=0;count && i<pool->capacity;++i) {
        run=pool->next[i]==pool->capacity?0:run+1;
        if(run!=count) continue;
        *slot=i+1-count;
        size_t *link=&pool->head;
        while(*link!=SIZE_MAX) {
            size_t index=*link;
            if(index>=*slot && index<=i) {
                *link=pool->next[index];
                pool->next[index]=pool->capacity;
            } else link=pool->next+index;
        }
        pool->active+=count;
        if(pool->active>pool->peak) pool->peak=pool->active;
        return qa_pool_at(pool,*slot);
    }
    *slot=SIZE_MAX;
    if(pool->overflow!=SIZE_MAX) ++pool->overflow;
    qa_allocation_gate_capacity_exhausted();
    return NULL;
}

void qa_pool_release_run(qa_pool *pool, size_t slot, size_t count)
{
    while(count) qa_pool_release(pool,slot+--count);
}
