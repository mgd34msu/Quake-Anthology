#include "qa/network_unified_frame_pool.h"
#include "qa/arena.h"

#include <stdlib.h>
#include <string.h>

#include "qa/pool.h"

#define FRAME_PAGE_BYTES (64u*1024u)
#define FRAME_POOL_BYTES (96u*1024u*1024u)

struct qa_unified_frame_pool {
    qa_arena storage;
    qa_pool leases, pages;
    size_t references;
    bool retired;
};
struct qa_unified_frame_lease {
    qa_unified_frame_pool *pool;
    qa_arena arena;
    size_t references, used, slot;
};
static void pool_release(qa_unified_frame_pool *pool)
{
    if(--pool->references) return;
    qa_arena_destroy(&pool->storage);
    free(pool);
}

qa_unified_frame_pool *qa_unified_frame_pool_create(size_t bytes,size_t leases,qa_error *error)
{
    qa_unified_frame_pool *pool=calloc(1,sizeof(*pool));
    if(!pool) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Cannot allocate Unified frame pool");
        return NULL;
    }
    pool->references=1;
    if(!bytes) bytes=FRAME_POOL_BYTES;
    if(!leases) leases=128;
    size_t pages=bytes/FRAME_PAGE_BYTES+(bytes%FRAME_PAGE_BYTES!=0);
    if(!qa_pool_prepare(&pool->leases,&pool->storage,leases,sizeof(qa_unified_frame_lease),
        _Alignof(qa_unified_frame_lease),error) ||
        !qa_pool_prepare(&pool->pages,&pool->storage,pages,FRAME_PAGE_BYTES,
        _Alignof(max_align_t),error)) {
        pool_release(pool);return NULL;
    }
    qa_arena_seal(&pool->storage);
    return pool;
}

void qa_unified_frame_pool_destroy(qa_unified_frame_pool **owner)
{
    if(!owner || !*owner) return;
    qa_unified_frame_pool *pool=*owner;
    *owner=NULL;pool->retired=true;
    pool_release(pool);
}

qa_unified_frame_lease *qa_unified_frame_lease_acquire(qa_unified_frame_pool *pool,
    qa_error *error)
{
    if(!pool || pool->retired) {
        qa_error_set(error,QA_ERROR_ARGUMENT,0,"Unified frame lease requires a live pool owner");
        return NULL;
    }
    size_t slot;
    qa_unified_frame_lease *lease=qa_pool_take(&pool->leases,&slot);
    if(!lease) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Unified frame lease capacity exhausted");
        return NULL;
    }
    *lease=(qa_unified_frame_lease){.pool=pool,.references=1,.slot=slot};
    qa_arena_init_pool(&lease->arena,&pool->pages);
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
    qa_arena_destroy(&lease->arena);
    qa_pool_release(&pool->leases,lease->slot);
    pool_release(pool);
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
