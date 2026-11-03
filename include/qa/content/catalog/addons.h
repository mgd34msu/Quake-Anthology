/*
 * Port of src/content/catalog/addons.ts: Quaddicted catalog
 * parsing/install paths/dependency resolution and Nightdive
 * addons.json titles. TS `unknown` inputs are persistence save
 * values; TS URL is the malloc'd source string (validated as the
 * TS URL checks validate it). Async file reads are synchronous.
 * Plain TS Errors abort printing "Error: ...".
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_ADDONS_H
#define QA_CONTENT_CATALOG_ADDONS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/content.h"
#include "qa/persistence/value.h"

extern const char *const qa_quaddicted_catalog_url;

typedef struct qa_addon_mapping {
  char *from; /* owned */
  char *to;   /* owned, nullable */
} qa_addon_mapping;

/* TS AddonPackage (all strings owned). */
typedef struct qa_addon_package {
  char *digest; /* owned qa_content_digest text */
  char *sha256;
  char *title;
  char *filename;
  char *group;
  int64_t bytes;
  char *url;
  char **tags;
  size_t tag_count;
  char **starts;
  size_t start_count;
  char *game_directory;
  qa_addon_mapping *mappings;
  size_t mapping_count;
  char *unavailable; /* nullable */
} qa_addon_package;

typedef struct qa_addon_package_list {
  qa_addon_package *values;
  size_t count;
} qa_addon_package_list;

void qa_addon_package_free(qa_addon_package *package);
void qa_addon_package_list_free(qa_addon_package_list *list);

/* TS addonTags: malloc'd dup'd values after "name=". */
char **qa_addon_tags(const char *const *tags, size_t tag_count,
                     const char *name, size_t *out_count);
void qa_addon_tags_free(char **values, size_t count);

/* TS parseAddonCatalog. False only on OOM. A malformed row is skipped
 * and its error message passed to `skipped` (NULL ignores it); a value that
 * is not an array raises. */
typedef void (*qa_addon_skip_fn)(void *ctx, const char *reason);
bool qa_parse_addon_catalog_reporting(const qa_save_value *value,
                                      qa_addon_skip_fn skipped, void *skipped_ctx,
                                      qa_addon_package_list *out);
bool qa_parse_addon_catalog(const qa_save_value *value,
                            qa_addon_package_list *out);

/* TS addonInstallPath: malloc'd path or NULL (TS null or OOM use
 * *out_found when non-NULL to tell them apart). */
char *qa_addon_install_path(const qa_addon_package *package,
                            const char *member, bool *out_found);

/* TS resolveAddonPackages: borrowed-pointer result (free array). */
const qa_addon_package **qa_resolve_addon_packages(
    const qa_addon_package *catalog, size_t catalog_count,
    const qa_addon_package *selected, size_t *out_count);

/* TS managedAddonHidden (synchronous). */
bool qa_managed_addon_hidden(const char *root, const char *directory);

/* TS NightdiveAddon. */
typedef struct qa_nightdive_addon {
  char *id;
  char *game_directory;
  char *title;
} qa_nightdive_addon;
typedef struct qa_nightdive_addon_list {
  qa_nightdive_addon *values;
  size_t count;
} qa_nightdive_addon_list;
void qa_nightdive_addon_list_free(qa_nightdive_addon_list *list);

/* TS parseNightdiveAddons. False only on OOM. */
bool qa_parse_nightdive_addons(const qa_save_value *value,
                               qa_nightdive_addon_list *out);
/* TS readNightdiveAddons (synchronous; any failure = empty). */
bool qa_read_nightdive_addons(const char *root, qa_nightdive_addon_list *out);
/* TS managedAddonTitle: malloc'd title or NULL (also NULL when the file
 * cannot be read or is not JSON). */
char *qa_managed_addon_title(const char *root, const char *content_directory);

#endif
