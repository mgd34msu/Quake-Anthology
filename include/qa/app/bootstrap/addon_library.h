/* Port of src/app/bootstrap/addon-library.ts: the Quaddicted add-on library
 * (LibraryMenuService). Managed packages install into isolated product
 * directories under <root>/q1 (or q1/rerelease); the original game files are
 * never changed.
 * C accommodations:
 *  - Each TS run(operation) promise runs synchronously inside the call that
 *    starts it; `pending` is set for its duration, so activate()/run() during
 *    an operation (from changed/launch) are ignored as in the TS. A stop()
 *    issued before an operation starts cannot reach it (the TS cancels an
 *    import between its first await and its install check).
 *  - The catalog fetch is a libcurl GET of qa_quaddicted_catalog_url with the
 *    TS 20 s timeout; Cancel/close abort it. Network installs go through the
 *    HttpDownloadQueue (http_downloads.h), pumped to idle per package.
 *  - Strings returned by scope()/status()/entries() are borrowed until the
 *    next call that changes the library; the root is copied.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_ADDON_LIBRARY_H
#define QA_APP_BOOTSTRAP_ADDON_LIBRARY_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/ui/library/menu.h"
/* TS AddonLibraryOptions. changed() returns NULL on success or the message of
 * its rejection; ctx is shared by both callbacks. */
typedef struct qa_app_addon_library_options {
  const char *root;
  const char *edition; /* NULL = undefined (classic); "classic" | "rerelease" */
  void *ctx;
  const char *(*changed)(void *ctx);
  void (*launch)(void *ctx, const char *product, const char *map); /* map NULL = TS null */
} qa_app_addon_library_options;
typedef struct qa_app_addon_library qa_app_addon_library;
/* NULL only on OOM. */
qa_app_addon_library *qa_app_addon_library_create(qa_app_addon_library_options options);
void qa_app_addon_library_destroy(qa_app_addon_library *library);
/* The library as a LibraryMenuService (ctx is the library). */
const qa_ui_library_menu_service *qa_app_addon_library_service(qa_app_addon_library *library);
const char *qa_app_addon_library_scope(qa_app_addon_library *library);
const char *qa_app_addon_library_status(const qa_app_addon_library *library);
const qa_ui_library_entry *qa_app_addon_library_entries(qa_app_addon_library *library, size_t *out_count);
extern const char qa_app_addon_library_stop_label[];   /* "Cancel download" */
extern const char qa_app_addon_library_create_label[]; /* "Import Quake ZIP" */
/* TS stop.activate(). */
void qa_app_addon_library_stop(qa_app_addon_library *library);
/* TS create.submit(path). */
void qa_app_addon_library_create_submit(qa_app_addon_library *library, const char *path);
void qa_app_addon_library_refresh(qa_app_addon_library *library);
void qa_app_addon_library_activate(qa_app_addon_library *library, const char *id);
void qa_app_addon_library_settle(qa_app_addon_library *library);
void qa_app_addon_library_suspend(qa_app_addon_library *library);
void qa_app_addon_library_close(qa_app_addon_library *library);
/* TS readInstalled() (private; reached by tests as the TS test reaches it). */
void qa_app_addon_library_read_installed(qa_app_addon_library *library);
/* TS contentParent/product helpers. product is malloc'd. */
const char *qa_app_addon_content_parent(const char *edition);
char *qa_app_addon_product(const char *edition, const char *directory);
#endif
