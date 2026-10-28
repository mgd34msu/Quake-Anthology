/* Port of src/app/bootstrap/startup-saves.ts. Directory scanning,
 * naming, caching and sorting are real POSIX here; per-file save
 * parsing (persistence + catalog product lookup) is a host fn that
 * returns an error message instead of throwing, preserving the TS
 * per-row unavailable behavior. Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_SAVES_H
#define QA_APP_BOOTSTRAP_STARTUP_SAVES_H
#include <stddef.h>
/* Malloc'd path or NULL. */
char *qa_app_resolve_external_save_path(const char *const *directories, size_t directory_count, const char *name);
typedef struct qa_app_startup_save_row {
  char *id; char *label; char *map; char *game;
  double saved_at_milliseconds;
  char *unavailable; /* NULL = available */
} qa_app_startup_save_row;
typedef struct qa_app_startup_save_list {
  qa_app_startup_save_row *rows; size_t row_count;
  char *error; /* NULL = TS null */
} qa_app_startup_save_list;
typedef struct qa_app_startup_save_detail {
  char *label; char *map; char *game;
  double saved_at_milliseconds;
  char *unavailable;
} qa_app_startup_save_detail;
typedef struct qa_app_startup_saves_host {
  void *ctx;
  /* Fills detail for one .pak.gz (kex) or .sav file. Default label/map/
   * game/unavailable seeds are passed in *detail (TS locals). */
  void (*inspect_pak)(void *ctx, const char *path, qa_app_startup_save_detail *detail);
  void (*inspect_sav)(void *ctx, const char *path, qa_app_startup_save_detail *detail);
} qa_app_startup_saves_host;
typedef struct qa_app_startup_saves qa_app_startup_saves;
qa_app_startup_saves *qa_app_startup_saves_create(qa_app_startup_saves_host host, const char *directory,
                                                  const char *const *external_directories, size_t external_count);
void qa_app_startup_saves_destroy(qa_app_startup_saves *s);
const char *qa_app_startup_saves_directory(const qa_app_startup_saves *s);
const qa_app_startup_save_list *qa_app_startup_saves_list(const qa_app_startup_saves *s);
const char *qa_app_startup_saves_path(qa_app_startup_saves *s, const char *id); /* borrowed; aborts as TS throws */
char *qa_app_startup_saves_named_path(qa_app_startup_saves *s, const char *name); /* malloc'd */
void qa_app_startup_saves_refresh(qa_app_startup_saves *s);
#endif
