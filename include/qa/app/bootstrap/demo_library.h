/* Port of src/app/bootstrap/demo-library.ts. Async becomes
 * synchronous; errors are returned strings. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_DEMO_LIBRARY_H
#define QA_APP_BOOTSTRAP_DEMO_LIBRARY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/demo_playback.h"
/* Owned strings; label aliases id text (separate allocation). */
typedef struct qa_app_demo_library_entry { char *id, *label; qa_app_demo_family family; } qa_app_demo_library_entry;
/* TS DemoLibraryMounts. list returns malloc'd names; read false = TS undefined. */
typedef struct qa_app_demo_library_mounts {
  void *ctx;
  char **(*list_files)(void *ctx, const char *directory, const char *extension, size_t *out_count);
  bool (*read)(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count);
} qa_app_demo_library_mounts;
typedef struct qa_app_demo_library qa_app_demo_library;
qa_app_demo_library *qa_app_demo_library_create(const char *root, qa_app_demo_library_mounts mounts);
void qa_app_demo_library_destroy(qa_app_demo_library *library);
const char *qa_app_demo_library_root(const qa_app_demo_library *library);
/* *out_error set on scan failure (non-missing readdir errors). */
qa_app_demo_library_entry *qa_app_demo_library_list(qa_app_demo_library *library, size_t *out_count, const char **out_error);
void qa_app_demo_library_entries_free(qa_app_demo_library_entry *entries, size_t count);
/* False = TS undefined. Local file wins over mounts. */
bool qa_app_demo_library_read(qa_app_demo_library *library, const char *path, uint8_t **out_bytes, size_t *out_count, const char **out_error);
#endif
