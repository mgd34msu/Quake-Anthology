#ifndef QA_ARENA_PROGRESS_CATALOG_H
#define QA_ARENA_PROGRESS_CATALOG_H
#include "qa/arena_progress.h"
#include "qa/vfs.h"

typedef struct qa_base_arena_catalog qa_base_arena_catalog;
typedef struct qa_base_arena {
    int32_t number, selection;
    double frag_limit, time_limit;
    const char *map, *title, *special;
    const char *const *bots;
    size_t bot_count;
} qa_base_arena;
typedef struct qa_base_arena_catalog_refs {
    void *context;
    bool (*resource_encode)(void *, const qa_resource *, const char *requested_path,
                            uint64_t *, qa_error *);
    bool (*resource_decode)(void *, uint64_t, const char *requested_path,
                            qa_resource **borrowed, qa_error *);
} qa_base_arena_catalog_refs;

/* Reads scripts/arenas.txt followed by the actual scoped .arena listing.
 * Rows use UI_LoadArenas campaign numbering, independently of GAME's order.
 * Retained immutable authored resources keep every borrowed row alive. */
bool qa_base_arena_catalog_create(qa_vfs *, qa_base_arena_catalog **, qa_error *);
void qa_base_arena_catalog_destroy(qa_base_arena_catalog *);
bool qa_base_arena_catalog_ready(const qa_base_arena_catalog *, qa_error *);
qa_arena_catalog qa_base_arena_catalog_levels(const qa_base_arena_catalog *);
size_t qa_base_arena_catalog_count(const qa_base_arena_catalog *);
const qa_base_arena *qa_base_arena_catalog_at(const qa_base_arena_catalog *, size_t);
const qa_base_arena *qa_base_arena_catalog_find(const qa_base_arena_catalog *, const char *map);
size_t qa_base_arena_catalog_resource_count(const qa_base_arena_catalog *);
const qa_resource *qa_base_arena_catalog_resource_at(const qa_base_arena_catalog *, size_t,
                                                    const char **requested_path);
/* Reference resolvers qualify the exact authored resource under its genuine
 * source content owner. Restore parses only those retained immutable bytes;
 * it does not enumerate mounts or reopen a file. */
bool qa_base_arena_catalog_checkpoint(const qa_base_arena_catalog *,
    const qa_base_arena_catalog_refs *, qa_buffer *, qa_error *);
bool qa_base_arena_catalog_restore(qa_bytes, const qa_base_arena_catalog_refs *,
    qa_base_arena_catalog **, qa_error *);
#endif
