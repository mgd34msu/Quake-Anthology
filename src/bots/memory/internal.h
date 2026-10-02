#ifndef QA_BOT_MEMORY_INTERNAL_H
#define QA_BOT_MEMORY_INTERNAL_H
#include "qa/bots_allocator.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

typedef struct bot_memory_record {
    uint8_t *backing;
    uint32_t size;
    qa_bot_memory_kind kind;
    bool live,has_provenance;
    char *file,*label;
    uint32_t line;
    void *host_allocation;
    uint64_t generation;
    uint32_t previous,next,free_next;
} bot_memory_record;
struct qa_bot_memory {
    qa_bot_memory_options options;
    qa_script_memory scripts;
    qa_bot_memory_allocation *script_restored;
    size_t script_restored_count;
    bot_memory_record *records;
    uint32_t first,last,free_head,slots,capacity;
    uint64_t owner,next_generation;
    uint64_t allocated_bytes;
    size_t references;
    size_t live_count;
    bool busy,disposed;
};
bool bot_memory_fail(qa_error *,qa_status,const char *);
bool bot_memory_mutable(qa_bot_memory *,qa_error *);
bool bot_memory_owned(const qa_bot_memory *,qa_bot_memory_allocation);
bot_memory_record *bot_memory_record_get(const qa_bot_memory *,qa_bot_memory_allocation);
qa_bot_memory_allocation bot_memory_handle(const qa_bot_memory *,uint32_t);
bool bot_memory_allocate(qa_bot_memory *,uint32_t,qa_bot_memory_kind,bool,
    const qa_bot_memory_provenance *,qa_bot_memory_allocation *,qa_error *);
bool bot_memory_release(qa_bot_memory *,qa_bot_memory_allocation,bool,qa_error *);
void bot_memory_clear(qa_bot_memory *);
#endif
