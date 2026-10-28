/* Port of src/app/bootstrap/base-arena-catalog.ts. Async mount reads
 * become synchronous host callbacks. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_BASE_ARENA_CATALOG_H
#define QA_APP_BOOTSTRAP_BASE_ARENA_CATALOG_H
#include <stddef.h>
/* Owned strings; free with qa_app_base_arena_catalog_destroy. */
typedef struct qa_app_base_arena {
  double number, selection, frag_limit, time_limit;
  char *map, *title, *special;
  char **bots; size_t bot_count;
} qa_app_base_arena;
typedef struct qa_app_base_arena_catalog {
  qa_app_base_arena *arenas; size_t arena_count;
  double regular_count, tier_count;
} qa_app_base_arena_catalog;
/* TS Pick<MountedContent,"listFiles"|"open">: list returns malloc'd
 * names (caller frees each + array); open returns malloc'd latin1 text
 * or NULL for TS null. */
typedef struct qa_app_arena_mounts {
  void *ctx;
  char **(*list_files)(void *ctx, const char *directory, const char *extension, size_t *out_count);
  char *(*open_text)(void *ctx, const char *path);
} qa_app_arena_mounts;
qa_app_base_arena_catalog qa_app_parse_base_arena_catalog(const char *const *texts, size_t text_count);
qa_app_base_arena_catalog qa_app_read_base_arena_catalog(qa_app_arena_mounts mounts);
void qa_app_base_arena_catalog_destroy(qa_app_base_arena_catalog *catalog);
/* Aborts with the TS Error when no arena matches. Borrowed. */
const qa_app_base_arena *qa_app_base_arena_for_map(const qa_app_base_arena_catalog *catalog, const char *map);
#endif
