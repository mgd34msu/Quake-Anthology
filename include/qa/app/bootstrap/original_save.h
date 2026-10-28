/* Port of src/app/bootstrap/original-save.ts. Content loading and
 * simulation (content.ts / simulation, other slices) plus the save
 * readers (persistence) are the host below; this file owns the
 * option rewriting and validation. Async becomes synchronous; host
 * failures are borrowed error strings. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_ORIGINAL_SAVE_H
#define QA_APP_BOOTSTRAP_ORIGINAL_SAVE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/session.h"
/* TS readSavedGame result view. */
typedef struct qa_app_saved_game_view {
  const char *kind; /* "shared"|"original" */
  const qa_save_image *image;    /* shared, borrowed */
  double skill, time;
  const char *map;               /* borrowed */
  const void *data;              /* borrowed opaque save data */
} qa_app_saved_game_view;
/* TS KexQ2SaveArchive info view. */
typedef struct qa_app_kex_save_view {
  bool restorable;
  const char *game_directory; /* borrowed, NULL = absent */
  const char *map_command;    /* borrowed */
  const char *(*cvar)(void *ctx, const char *name); /* borrowed or NULL */
  void *cvar_ctx;
} qa_app_kex_save_view;
typedef struct qa_app_original_save_host {
  void *ctx;
  /* Borrowed views valid until the next host call. */
  const char *(*read_saved_game)(void *ctx, const char *path, qa_app_saved_game_view *out);
  const char *(*read_kex_save)(void *ctx, const char *path, qa_app_kex_save_view *out);
  /* TS selectQ1SaveProduct: borrowed catalog product. */
  const qa_catalog_discovered_product *(*select_q1_save_product)(void *ctx,
      const qa_content_catalog *catalog, const void *data, const char *path, const char *source_product);
  /* TS loadApplicationContent + createSimulation + admit/restore +
   * checkpoint. *out_image receives an owned checkpoint the caller
   * releases with the landed save-image destroy implied by its type
   * (host-owned copy semantics documented by the host provider).
   * original_save NULL selects the KEX fresh-spawn path. */
  const char *(*import_save)(void *ctx, const qa_app_options *restored,
      const qa_content_catalog *catalog, const qa_app_saved_game_view *original_save,
      bool has_original_save, qa_save_image *out_image);
} qa_app_original_save_host;
typedef struct qa_app_prepared_application_save {
  qa_save_image image;
  qa_app_options options;
} qa_app_prepared_application_save;
/* On success *error_out is NULL and out is filled (options owned,
 * destroy its options with qa_app_options_destroy). source_product
 * NULL = TS undefined. */
bool qa_app_prepare_application_save(const qa_app_options *options, const qa_content_catalog *catalog,
    const char *path, const char *source_product, const qa_app_original_save_host *host,
    qa_app_prepared_application_save *out, const char **error_out);
#endif
