#include "qa/pool.h"
#include "qa/allocation_gate.h"

#define POOL_USED (SIZE_MAX - 1u)

struct qa_pool_segment {
    qa_pool_segment *next;
    size_t first;
    qa_pool block;
};

static size_t *next_at(const qa_pool *pool,size_t slot)
{
    if(slot<pool->first_capacity)return pool->next+slot;
    for(qa_pool_segment *segment=pool->segments;segment;segment=segment->next)
        if(slot-segment->first<segment->block.capacity)
            return segment->block.next+(slot-segment->first);
    return NULL;
}

static void segment_bounds(const qa_pool *pool,size_t slot,size_t *first,size_t *end)
{
    if(slot<pool->first_capacity){*first=0;*end=pool->first_capacity;return;}
    for(qa_pool_segment *segment=pool->segments;segment;segment=segment->next)
        if(slot-segment->first<segment->block.capacity){
            *first=segment->first;*end=*first+segment->block.capacity;return;
        }
    *first=*end=pool->capacity;
}

static bool prepare(qa_pool *pool, qa_arena *arena, size_t count, size_t stride,
    size_t alignment, bool reserve, qa_error *error)
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
    qa_pool prepared = {.stride = stride, .capacity = count, .head = SIZE_MAX,
        .first_capacity=count,.alignment=alignment};
    if (!count) {
        *pool = prepared;
        if (reserve) qa_arena_seal(arena);
        return true;
    }
    if (count >= POOL_USED || count > SIZE_MAX / stride || count > SIZE_MAX / sizeof(size_t)) {
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
    size_t storage_size=values_size+padding+next_size;
    if (reserve) {
        if (storage_size>SIZE_MAX-(storage_alignment-1)) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"pool storage size overflow");return false;
        }
        if (!qa_arena_reserve(arena,storage_size+storage_alignment-1,error)) return false;
    }
    prepared.values = qa_arena_alloc(arena, storage_size,
        storage_alignment, error);
    if (!prepared.values) return false;
    prepared.next = (size_t *)(prepared.values + values_size + padding);
    prepared.head = 0;
    for (size_t i = 0; i < count; ++i) prepared.next[i] = i + 1 < count ? i + 1 : SIZE_MAX;
    *pool = prepared;
    if (reserve) qa_arena_seal(arena);
    return true;
}

bool qa_pool_prepare(qa_pool *pool,qa_arena *arena,size_t count,size_t stride,
    size_t alignment,qa_error *error)
{ return prepare(pool,arena,count,stride,alignment,false,error); }

bool qa_pool_reserve_pages(qa_pool *pool,qa_arena *arena,size_t count,size_t stride,
    size_t alignment,qa_error *error)
{ return prepare(pool,arena,count,stride,alignment,true,error); }

bool qa_pool_extend(qa_pool *pool,qa_arena *arena,size_t count,qa_error *error)
{
    if(!count)return true;
    if(count>=POOL_USED-pool->capacity){
        qa_error_set(error,QA_ERROR_MEMORY,0,"pool slot capacity overflow");return false;
    }
    qa_pool_segment *segment=qa_arena_alloc(arena,sizeof(*segment),
        _Alignof(qa_pool_segment),error);
    if(!segment)return false;
    *segment=(qa_pool_segment){.first=pool->capacity};
    if(!prepare(&segment->block,arena,count,pool->stride,pool->alignment,false,error))return false;
    for(size_t i=0;i<count;++i)
        segment->block.next[i]=i+1<count?segment->first+i+1:pool->head;
    if(pool->last_segment)pool->last_segment->next=segment;
    else pool->segments=segment;
    pool->last_segment=segment;
    pool->head=pool->capacity;pool->capacity+=count;
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
    size_t *next=next_at(pool,*slot);
    pool->head = *next;
    *next = POOL_USED;
    ++pool->active;
    if (pool->active > pool->peak) pool->peak = pool->active;
    return qa_pool_at(pool, *slot);
}

void qa_pool_release(qa_pool *pool, size_t slot)
{
    *next_at(pool,slot) = pool->head;
    pool->head = slot;
    --pool->active;
}

void qa_pool_reset(qa_pool *pool)
{
    pool->head = pool->capacity ? 0 : SIZE_MAX;
    pool->active = 0;
    for (size_t i = 0; i < pool->capacity; ++i)
        *next_at(pool,i) = i + 1 < pool->capacity ? i + 1 : SIZE_MAX;
}

void *qa_pool_at(const qa_pool *pool, size_t slot)
{
    if(slot<pool->first_capacity)return pool->values+slot*pool->stride;
    for(qa_pool_segment *segment=pool->segments;segment;segment=segment->next)
        if(slot-segment->first<segment->block.capacity)
            return segment->block.values+(slot-segment->first)*pool->stride;
    return NULL;
}

static void *claim_run(qa_pool *pool,size_t slot,size_t count,size_t acquire)
{
    size_t *link=&pool->head,acquired=0;
    while(*link!=SIZE_MAX && acquired<acquire) {
        size_t index=*link;
        size_t *next=next_at(pool,index);
        if(index>=slot && index-slot<count) {
            *link=*next;*next=POOL_USED;++acquired;
        }else link=next;
    }
    pool->active+=acquired;
    if(pool->active>pool->peak)pool->peak=pool->active;
    return qa_pool_at(pool,slot);
}

void *qa_pool_grow_run(qa_pool *pool,size_t *slot,size_t current,size_t needed)
{
    if(needed<=current)return qa_pool_at(pool,*slot);
    size_t segment_first,segment_end;
    segment_bounds(pool,*slot,&segment_first,&segment_end);
    size_t first=*slot,end=first+current;
    while(end<segment_end && *next_at(pool,end)!=POOL_USED)++end;
    if(needed>end-first){
        while(first>segment_first && *next_at(pool,first-1)!=POOL_USED && needed>end-first)--first;
        if(needed>end-first)return NULL;
    }
    *slot=first;return claim_run(pool,first,needed,needed-current);
}

static size_t find_run(const qa_pool *pool,size_t count)
{
    if(count==1)return pool->head;
    size_t run=0,segment_end=pool->first_capacity;
    for(size_t i=0;count && i<pool->capacity;++i) {
        if(i==segment_end){
            size_t first;segment_bounds(pool,i,&first,&segment_end);run=0;
        }
        run=*next_at(pool,i)==POOL_USED?0:run+1;
        if(run!=count) continue;
        return i+1-count;
    }
    return SIZE_MAX;
}

bool qa_pool_run_available(const qa_pool *pool,size_t count)
{
    return find_run(pool,count)!=SIZE_MAX;
}

void *qa_pool_take_run(qa_pool *pool, size_t count, size_t *slot)
{
    if(count==1)return qa_pool_take(pool,slot);
    *slot=find_run(pool,count);
    if(*slot!=SIZE_MAX)return claim_run(pool,*slot,count,count);
    if(pool->overflow!=SIZE_MAX) ++pool->overflow;
    qa_allocation_gate_capacity_exhausted();
    return NULL;
}

void qa_pool_release_run(qa_pool *pool, size_t slot, size_t count)
{
    while(count) qa_pool_release(pool,slot+--count);
}
