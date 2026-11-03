/* Port of src/app/bootstrap/startup-saves.ts: StartupSaves over the installed
 * content catalog. Each listed file is inspected with the persistence readers
 * (readKexQ2SaveArchive, readSaveHeader, readSavedGame, selectQ1SaveProduct,
 * savedSimulationSettings); a raised error becomes the row's unavailable text,
 * as the TS catch. Async -> synchronous. path/namedPath raise Error as TS throws.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_SAVES_H
#define QA_APP_BOOTSTRAP_STARTUP_SAVES_H
#include <stddef.h>
#include "qa/content/catalog/index.h"
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
typedef struct qa_app_startup_saves qa_app_startup_saves;
/* TS new StartupSaves(catalog, directory, externalDirectories = []): the catalog is borrowed. */
qa_app_startup_saves *qa_app_startup_saves_create(const qa_content_catalog *catalog, const char *directory,
                                                  const char *const *external_directories, size_t external_count);
void qa_app_startup_saves_destroy(qa_app_startup_saves *s);
const char *qa_app_startup_saves_directory(const qa_app_startup_saves *s);
const qa_content_catalog *qa_app_startup_saves_catalog(const qa_app_startup_saves *s);
const qa_app_startup_save_list *qa_app_startup_saves_list(const qa_app_startup_saves *s);
const char *qa_app_startup_saves_path(qa_app_startup_saves *s, const char *id); /* borrowed */
char *qa_app_startup_saves_named_path(qa_app_startup_saves *s, const char *name); /* malloc'd */
void qa_app_startup_saves_refresh(qa_app_startup_saves *s);
#endif
