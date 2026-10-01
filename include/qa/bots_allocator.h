#ifndef QA_BOTS_ALLOCATOR_H
#define QA_BOTS_ALLOCATOR_H
#include "qa/script.h"

/* Botlib heap/hunk memory is separate from the GAME's fixed source pool. */
typedef struct qa_bot_memory qa_bot_memory;
/* Source allocation identity is independent of its reusable native slot. */
typedef struct qa_bot_memory_allocation {uint64_t owner,generation;uint32_t slot;} qa_bot_memory_allocation;
typedef enum qa_bot_memory_kind {QA_BOT_MEMORY_HEAP,QA_BOT_MEMORY_HUNK} qa_bot_memory_kind;
typedef enum qa_bot_memory_profile {
    QA_BOT_MEMORY_RELEASE,QA_BOT_MEMORY_MANAGER,QA_BOT_MEMORY_DEBUG
} qa_bot_memory_profile;
typedef struct qa_bot_memory_span {uint8_t *data;uint32_t size;} qa_bot_memory_span;
typedef struct qa_bot_memory_backing {uint8_t *bytes;uint32_t size;void *token;} qa_bot_memory_backing;
typedef struct qa_bot_memory_provenance {const char *file;uint32_t line;const char *label;} qa_bot_memory_provenance;
typedef struct qa_bot_memory_host {
    void *context;
    bool (*allocate)(void *,uint32_t,qa_bot_memory_kind,bool,qa_bot_memory_backing *,qa_error *);
    void (*release)(void *,const qa_bot_memory_backing *,qa_bot_memory_kind);
    bool (*available)(void *,uint64_t *,qa_error *);
} qa_bot_memory_host;
typedef struct qa_bot_memory_options {
    qa_bot_memory_host host;
    qa_bot_memory_profile profile;
    void *context;
    bool (*print)(void *,qa_script_severity,const char *,qa_error *);
    bool (*log)(void *,const char *,qa_error *);
} qa_bot_memory_options;
/* The default source owner has zeroed native backing and no invented arena
 * capacity. A host supplies all three operations and returns size+4 bytes. */
bool qa_bot_memory_create(const qa_bot_memory_options *,qa_bot_memory **,qa_error *);
bool qa_bot_memory_retain(qa_bot_memory *,qa_error *);
bool qa_bot_memory_release(qa_bot_memory *,qa_error *);
bool qa_bot_memory_destroy(qa_bot_memory *,qa_error *);
bool qa_bot_memory_dispose(qa_bot_memory *,qa_error *);
bool qa_bot_memory_idle(const qa_bot_memory *);
bool qa_bot_memory_disposed(const qa_bot_memory *);
bool qa_bot_memory_allocate(qa_bot_memory *,uint32_t,qa_bot_memory_kind,bool,
    const qa_bot_memory_provenance *,qa_bot_memory_allocation *,qa_error *);
/* Span is borrowed until release/disposal. It excludes the four-byte prefix. */
bool qa_bot_memory_bytes(const qa_bot_memory *,qa_bot_memory_allocation,qa_bot_memory_span *,qa_error *);
bool qa_bot_memory_free(qa_bot_memory *,qa_bot_memory_allocation,qa_error *);
bool qa_bot_memory_reset_hunk(qa_bot_memory *,qa_error *);
size_t qa_bot_memory_live_allocations(const qa_bot_memory *);
uint64_t qa_bot_memory_allocated_bytes(const qa_bot_memory *);
bool qa_bot_memory_available(qa_bot_memory *,uint64_t *,qa_error *);
bool qa_bot_memory_byte_size(const qa_bot_memory *,qa_bot_memory_allocation,uint32_t *,qa_error *);
bool qa_bot_memory_print_used(qa_bot_memory *,qa_error *);
bool qa_bot_memory_print_labels(qa_bot_memory *,qa_error *);
bool qa_bot_memory_dump(qa_bot_memory *,qa_error *);

struct qa_bot_library;
struct qa_bot_runtime;
/* Borrowed actual owner; retaining it does not keep allocations live after the
 * source library's explicit shutdown/dispose stage. */
qa_bot_memory *qa_bot_library_memory(const struct qa_bot_library *);
qa_bot_memory *qa_bot_runtime_memory(const struct qa_bot_runtime *);
#endif
