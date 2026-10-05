#include "qa/network_unified_frame_pool.h"
#include "qa/arena.h"

#include <stdlib.h>
#include <string.h>

struct qa_unified_frame_pool {
    qa_unified_frame_lease *available;
    size_t references, block_size;
    bool retired;
};
struct qa_unified_frame_lease {
    qa_unified_frame_pool *pool;
    qa_unified_frame_lease *next;
    qa_arena arena;
    size_t references, used;
};

qa_unified_frame_pool *qa_unified_frame_pool_create(size_t block_size, qa_error *error)
{
    qa_unified_frame_pool *pool=malloc(sizeof(*pool));
    if (!pool) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Cannot allocate Unified frame pool");
        return NULL;
    }
    *pool=(qa_unified_frame_pool){.references=1,.block_size=block_size};
    return pool;
}

void qa_unified_frame_pool_destroy(qa_unified_frame_pool **owner)
{
    if (!owner || !*owner) return;
    qa_unified_frame_pool *pool=*owner;
    *owner=NULL; pool->retired=true;
    while (pool->available) {
        qa_unified_frame_lease *lease=pool->available;
        pool->available=lease->next;
        qa_arena_destroy(&lease->arena); free(lease);
    }
    if (!--pool->references) free(pool);
}

qa_unified_frame_lease *qa_unified_frame_lease_acquire(qa_unified_frame_pool *pool,
    qa_error *error)
{
    if (!pool || pool->retired) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unified frame lease requires a live pool owner");
        return NULL;
    }
    if (pool->references==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Unified frame pool reference capacity exhausted");
        return NULL;
    }
    qa_unified_frame_lease *lease=pool->available;
    if (lease) pool->available=lease->next;
    else {
        lease=malloc(sizeof(*lease));
        if (!lease) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Cannot allocate Unified frame lease");
            return NULL;
        }
        qa_arena_init(&lease->arena,pool->block_size);
    }
    lease->pool=pool; lease->next=NULL; lease->references=1; lease->used=0;
    ++pool->references;
    return lease;
}

bool qa_unified_frame_lease_retain(qa_unified_frame_lease *lease, qa_error *error)
{
    if (!lease || !lease->references) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unified frame retain requires a live lease");
        return false;
    }
    if (lease->references==SIZE_MAX) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Unified frame lease reference capacity exhausted");
        return false;
    }
    ++lease->references;
    return true;
}

void qa_unified_frame_lease_release(qa_unified_frame_lease *lease)
{
    if (!lease || --lease->references) return;
    qa_unified_frame_pool *pool=lease->pool;
    if (pool->retired) { qa_arena_destroy(&lease->arena); free(lease); }
    else {
        qa_arena_reset(&lease->arena);
        lease->used=0;
        lease->next=pool->available; pool->available=lease;
    }
    if (!--pool->references) free(pool);
}

size_t qa_unified_frame_lease_used(const qa_unified_frame_lease *lease)
{ return lease?lease->used:0; }

void *qa_unified_frame_lease_alloc(qa_unified_frame_lease *lease, size_t count,
    size_t stride, size_t alignment, qa_error *error)
{
    if (!lease || !lease->references || !stride || !alignment ||
        (alignment&(alignment-1))) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unified frame allocation requires its live aligned lease");
        return NULL;
    }
    if (count>SIZE_MAX/stride) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Unified frame allocation size overflow");
        return NULL;
    }
    if (!count) return NULL;
    size_t size=count*stride;
    if (size>SIZE_MAX-lease->used) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Unified frame lease byte count overflow");
        return NULL;
    }
    void *out=qa_arena_alloc(&lease->arena,size,alignment,error);
    if (out) { memset(out,0,size); lease->used+=size; }
    return out;
}
