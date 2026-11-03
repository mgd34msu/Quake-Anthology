/* Port of src/app/bootstrap/application.ts, region 643-653: autosaveLevel.
 * Separate from application_core.c because it calls saveGame (P5), so the
 * constructor and storage link without it. Layout and status:
 * docs/application_layout.md. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
/* node:fs/promises mkdir(directory, { recursive: true }); a failure raises
 * the Node error text ("<CODE>: <description>, mkdir '<path>'"). */
static void node_errno_text(int error, const char **code, const char **description) {
  switch (error) {
    case EACCES: *code = "EACCES"; *description = "permission denied"; return;
    case EPERM: *code = "EPERM"; *description = "operation not permitted"; return;
    case ENOTDIR: *code = "ENOTDIR"; *description = "not a directory"; return;
    case EEXIST: *code = "EEXIST"; *description = "file already exists"; return;
    case EROFS: *code = "EROFS"; *description = "read-only file system"; return;
    case ENOSPC: *code = "ENOSPC"; *description = "no space left on device"; return;
    case ENAMETOOLONG: *code = "ENAMETOOLONG"; *description = "name too long"; return;
    case ELOOP: *code = "ELOOP"; *description = "too many symbolic links encountered"; return;
    default: *code = "EIO"; *description = strerror(error); return;
  }
}
static void mkdir_recursive(const char *directory) {
  char *copy = checked(strdup(directory));
  size_t n = strlen(copy);
  for (size_t i = 1; i <= n; i++) {
    if (copy[i] != '/' && copy[i] != '\0') continue;
    char saved = copy[i];
    copy[i] = '\0';
    struct stat info;
    if (stat(copy, &info) != 0) {
      if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
        int error = errno;
        const char *code, *description;
        node_errno_text(error, &code, &description);
        qa_caught_error message;
        (void)snprintf(message.message, sizeof message.message, "%s: %s, mkdir '%s'", code, description, directory);
        free(copy);
        qa_raise("Error", 0, 0, "%s", message.message);
      }
    } else if (!S_ISDIR(info.st_mode)) {
      free(copy);
      qa_raise("Error", 0, 0, "%s: %s, mkdir '%s'", saved == '\0' ? "EEXIST" : "ENOTDIR",
          saved == '\0' ? "file already exists" : "not a directory", directory);
    }
    copy[i] = saved;
  }
  free(copy);
}

typedef struct autosave_attempt { qa_application *app; const char *directory; } autosave_attempt;
static void autosave_run(void *ctx) {
  autosave_attempt *attempt = ctx;
  mkdir_recursive(attempt->directory);
  size_t n = strlen(attempt->directory) + sizeof "/autosave.sav";
  char *joined = checked(malloc(n));
  (void)snprintf(joined, n, "%s/autosave.sav", attempt->directory);
  char *path = qa_application_path_normalize(joined);
  free(joined);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  qa_application_save_game(attempt->app, path, NULL);
  qa_application_printf(attempt->app, "Autosaved %s.\n", path);
  qa_cleanup_pop(&cleanup);
  free(path);
}
/* TS autosaveLevel(). */
void qa_application_autosave_level(qa_application *app) {
  if (!qa_application_level_recovery_available(app, app->world_simulation, &app->launch_options)
      || qa_application_save_unavailable(app, QA_SAVE_PURPOSE_AUTOSAVE) != NULL) return;
  qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
  if (cvars != NULL && qa_cvar_registry_variable_value(cvars, "sv_autosave") == 0) return;
  const qa_catalog_discovered_product *product = qa_content_catalog_product(qa_app_loaded_content_catalog(app->loaded_content),
      qa_app_loaded_content_recipe(app->loaded_content)->map.entities.content);
  const char *root = qa_application_save_directory(app);
  size_t n = strlen(root) + 1 + strlen(product->expectation.id) + 1;
  char *joined = checked(malloc(n));
  (void)snprintf(joined, n, "%s/%s", root, product->expectation.id);
  char *directory = qa_application_path_normalize(joined);
  free(joined);
  autosave_attempt attempt = { app, directory };
  qa_caught_error caught;
  if (!qa_try(autosave_run, &attempt, &caught)) qa_application_printf(app, "Autosave failed: %s\n", caught.message);
  free(directory);
}
