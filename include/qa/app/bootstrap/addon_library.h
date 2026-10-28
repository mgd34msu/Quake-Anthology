/* Port of src/app/bootstrap/addon-library.ts. Network/archive IO
 * (Quaddicted fetch, downloads, zip staging) is host-wired; menu
 * scope/status/entries, product naming and activation routing are
 * kept here. Installed scanning reads .quaddicted.json directly.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_ADDON_LIBRARY_H
#define QA_APP_BOOTSTRAP_ADDON_LIBRARY_H
#include <stdbool.h>
#include <stddef.h>
/* TS AddonPackage fields read here. */
typedef struct qa_app_addon_package {
  const char *sha256, *title, *group, *unavailable;
  double bytes;
} qa_app_addon_package;
/* TS LibraryEntry rows (owned strings). */
typedef struct qa_app_library_entry { char *id, *label, *detail, *unavailable; } qa_app_library_entry;
typedef struct qa_app_addon_library qa_app_addon_library;
typedef struct qa_app_addon_library_host {
  void *ctx;
  const char *(*changed)(void *ctx);
  void (*launch)(void *ctx, const char *product, const char *map);
  /* Catalog refresh/install/import/remove IO; error or NULL. */
  const char *(*refresh_catalog)(void *ctx, qa_app_addon_package **out_packages, size_t *out_count, bool *used_cache);
  const char *(*install)(void *ctx, const qa_app_addon_package *package);
  const char *(*import_local)(void *ctx, const char *path);
  const char *(*remove_installed)(void *ctx, const char *directory);
  void (*cancel)(void *ctx);
  /* managedAddonHidden check for "<parent>/<directory>". */
  bool (*hidden)(void *ctx, const char *relative_directory);
} qa_app_addon_library_host;
typedef struct qa_app_addon_library_options {
  const char *root;
  const char *edition; /* NULL classic; "rerelease" selects q1/rerelease */
  qa_app_addon_library_host host;
} qa_app_addon_library_options;
qa_app_addon_library *qa_app_addon_library_create(qa_app_addon_library_options options);
void qa_app_addon_library_destroy(qa_app_addon_library *library);
/* Malloc'd scope string; caller frees. */
char *qa_app_addon_library_scope(const qa_app_addon_library *library);
const char *qa_app_addon_library_status(const qa_app_addon_library *library);
qa_app_library_entry *qa_app_addon_library_entries(qa_app_addon_library *library, size_t *out_count);
void qa_app_library_entries_free(qa_app_library_entry *entries, size_t count);
extern const char qa_app_addon_library_stop_label[]; /* "Cancel download" */
extern const char qa_app_addon_library_create_label[]; /* "Import Quake ZIP" */
void qa_app_addon_library_stop(qa_app_addon_library *library);
void qa_app_addon_library_create_submit(qa_app_addon_library *library, const char *path);
void qa_app_addon_library_refresh(qa_app_addon_library *library);
void qa_app_addon_library_activate(qa_app_addon_library *library, const char *id);
void qa_app_addon_library_settle(qa_app_addon_library *library);
void qa_app_addon_library_suspend(qa_app_addon_library *library);
void qa_app_addon_library_close(qa_app_addon_library *library);
/* TS contentParent/product helpers. product is malloc'd. */
const char *qa_app_addon_content_parent(const char *edition);
char *qa_app_addon_product(const char *edition, const char *directory);
#endif
