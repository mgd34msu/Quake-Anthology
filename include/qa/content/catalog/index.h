/*
 * Port of src/content/catalog/index.ts: installed-content
 * discovery (corpus/user/Nightdive-Steam layers, anchor game
 * roots, mod discovery), archive ordering, maps/mounts, mount
 * plans and authored starts. Re-exports products/launch/equipment
 * through the umbrella (qa/content/catalog.h).
 * C accommodations:
 *  - InstalledCatalog is qa_content_catalog (qa_installed_catalog
 *    is already a mirror in persistence/q1_selection.h). Products
 *    own their strings/archives/maps; init borrows the caller's
 *    arrays, discovery allocates and destroy frees them.
 *  - TS async filesystem work is synchronous POSIX. digestFile
 *    (content/mounts/index.ts), game roots (content/roots.ts),
 *    findContentPath/normalizeResourcePath (content/mounts/
 *    paths.ts) and userProductDirectory (content/user-data.ts)
 *    delegate to their landed ports. openArchive
 *    (content/archive/index.ts) and openMountPlan
 *    (content/mounts/index.ts) stay injected services (fn + ctx)
 *    in this header's public options; MountedContent is the
 *    minimal open() service shape (qa_catalog_mounted_opener).
 *  - TS plain Errors abort printing "Error: ..."; RangeErrors use
 *    qa_range_error, matching earlier ports.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_INDEX_H
#define QA_CONTENT_CATALOG_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/catalog/products.h"
#include "qa/content/catalog/start_maps.h"
#include "qa/contracts/content.h"

typedef struct qa_catalog_archive_entry {
  char *path; /* owned */
  int64_t ordinal;
  int64_t byte_length;
} qa_catalog_archive_entry;

/* TS CatalogArchive. */
typedef struct qa_catalog_archive {
  char *path; /* owned */
  qa_archive_format format;
  qa_catalog_archive_entry *entries; /* owned */
  size_t entry_count;
} qa_catalog_archive;

void qa_catalog_archive_free(qa_catalog_archive *archive);

/* TS ContentMap (name differs from the q1_selection mirror). */
typedef struct qa_catalog_content_map {
  char *path;   /* owned */
  char *source; /* owned */
  bool has_member_index; /* memberIndex is number | null */
  int64_t member_index;
} qa_catalog_content_map;

/* TS ProductAvailability discriminated union. */
typedef enum qa_catalog_availability_kind {
  QA_CATALOG_INSTALLED,
  QA_CATALOG_MISSING,
  QA_CATALOG_UNRESOLVED
} qa_catalog_availability_kind;
typedef struct qa_catalog_availability {
  qa_catalog_availability_kind kind;
  char **requirements; /* owned; missing */
  size_t requirement_count;
  char *reason; /* owned; unresolved */
} qa_catalog_availability;

/* TS userContent / addonContent {root, archives}: archives are
 * borrowed pointers into the product's archive array. */
typedef struct qa_catalog_directory_content {
  char *root; /* owned */
  qa_catalog_archive **archives; /* borrowed elements */
  size_t archive_count;
} qa_catalog_directory_content;

/* TS CatalogProduct. expectation strings are owned in discovered
 * catalogs (its archive/program lists are owned char* lists cast
 * to the const view). */
typedef struct qa_catalog_discovered_product {
  char *id; /* owned qa_content_id text */
  qa_catalog_product_expectation expectation;
  qa_catalog_availability availability;
  qa_catalog_archive *archives; /* owned */
  size_t archive_count;
  char *loose_root; /* owned, nullable */
  qa_catalog_directory_content *user_content;  /* nullable */
  qa_catalog_directory_content *addon_content; /* nullable */
  qa_catalog_content_map *maps; /* owned */
  size_t map_count;
  char **diagnostics; /* owned */
  size_t diagnostic_count;
} qa_catalog_discovered_product;

void qa_catalog_discovered_product_free(qa_catalog_discovered_product *product);

/* TS RemoteContentBase (exact strings) + RemoteContentSelection. */
typedef const char *qa_remote_content_base;
typedef struct qa_remote_content_selection {
  qa_remote_content_base base;
  char *directory; /* owned */
} qa_remote_content_selection;

/* TS remoteContentSelection. False only on OOM; unsafe directory
 * names abort with the exact TS Error. */
bool qa_remote_content_selection_make(qa_remote_content_base base,
                                      const char *game_directory,
                                      qa_remote_content_selection *out);
void qa_remote_content_selection_free(qa_remote_content_selection *selection);

/* TS remoteContentProduct. Malloc'd product id; NULL only on OOM.
 * products NULL/count 0 means expectedProducts. */
char *qa_remote_content_product(const qa_remote_content_selection *selection,
                                const qa_catalog_product_expectation *products,
                                size_t product_count);

/* NightdiveAddonRoot input shape (TS owner: src/content/roots.ts;
 * qa_nightdive_addon_root in qa/content/roots.h owns its root). */
typedef struct qa_catalog_nightdive_addon_root {
  qa_game_family family;
  const char *root;
} qa_catalog_nightdive_addon_root;

/* Archive inspection service (TS openArchive in
 * src/content/archive/index.ts; injected here). inspect fills *out
 * (owned by caller on success); on failure returns false and may
 * set *error_out to a malloc'd message (caller frees). */
typedef struct qa_catalog_archive_inspector {
  void *ctx;
  bool (*inspect)(void *ctx, const char *path, qa_archive_format format,
                  qa_catalog_archive *out, char **error_out);
} qa_catalog_archive_inspector;

/* TS DiscoverContentOptions. user_content_root NULL = undefined;
 * products NULL = expectedProducts; user_content_root_present
 * distinguishes an explicitly empty root from undefined. */
typedef struct qa_discover_content_options {
  const char *corpus_root;
  const char *user_content_root; /* nullable */
  const qa_catalog_nightdive_addon_root *addon_content_roots;
  size_t addon_content_root_count;
  const qa_catalog_product_expectation *products;
  size_t product_count;
  bool has_generation;
  double generation; /* TS number; safe-integer checked */
  bool discover_mods; /* TS discoverMods !== false */
  const qa_remote_content_selection *remote_content; /* nullable */
  qa_catalog_archive_inspector inspector; /* inspect NULL: no archives */
} qa_discover_content_options;

/* TS MountPlanSelection. rules NULL = undefined. */
typedef struct qa_catalog_mount_plan_selection {
  qa_mount_plan_id id;
  qa_content_id assets;
  qa_content_id geometry;
  qa_content_id rules; /* nullable */
  bool explicit_presentation;
  const qa_content_id *additional;
  size_t additional_count;
} qa_catalog_mount_plan_selection;

/* Minimal MountedContent open() shape (TS owner:
 * src/content/mounts/index.ts; injected here), as in
 * text/mounted.h. */
typedef struct qa_catalog_opened_resource {
  qa_resolved_resource_reference reference;
  const uint8_t *bytes; /* borrowed from the service */
  size_t byte_length;
} qa_catalog_opened_resource;
typedef struct qa_catalog_mounted_reader {
  void *ctx;
  bool (*open)(void *ctx, const char *path, qa_catalog_opened_resource *out);
  void (*close)(void *ctx); /* nullable */
} qa_catalog_mounted_reader;
/* TS MountPlanOpener. */
typedef struct qa_catalog_mount_plan_opener {
  void *ctx;
  bool (*open_plan)(void *ctx, const qa_resolved_mount_plan *plan,
                    qa_catalog_mounted_reader *out);
} qa_catalog_mount_plan_opener;

typedef struct qa_catalog_digest_cache {
  char *path;
  char *digest;
} qa_catalog_digest_cache;

/* TS InstalledCatalog. Owns products/root archives only when
 * built by qa_discover_installed_content (owned flag). */
typedef struct qa_content_catalog {
  char *corpus_root; /* owned when owned, else borrowed */
  qa_catalog_discovered_product *products;
  size_t product_count;
  qa_catalog_archive *root_archives;
  size_t root_archive_count;
  int64_t generation;
  char *user_content_root; /* nullable */
  qa_catalog_nightdive_addon_root *addon_content_roots;
  size_t addon_content_root_count;
  char **addon_content_root_strings; /* owned backing, discovery */
  bool owned;
  qa_catalog_digest_cache *digests;
  size_t digest_count;
  size_t digest_capacity;
} qa_content_catalog;

/* TS InstalledCatalog constructor over borrowed arrays. Duplicate
 * identities abort with the TS RangeError. */
void qa_content_catalog_init(qa_content_catalog *catalog, const char *corpus_root,
                             qa_catalog_discovered_product *products,
                             size_t product_count,
                             qa_catalog_archive *root_archives,
                             size_t root_archive_count, int64_t generation,
                             const char *user_content_root,
                             const qa_catalog_nightdive_addon_root *addon_roots,
                             size_t addon_root_count);
/* Frees owned (discovery) storage; safe on borrowed catalogs. */
void qa_content_catalog_destroy(qa_content_catalog *catalog);

/* TS product()/require(): abort with exact TS messages. */
const qa_catalog_discovered_product *qa_content_catalog_product(
    const qa_content_catalog *catalog, const char *id);
const qa_catalog_discovered_product *qa_content_catalog_require(
    const qa_content_catalog *catalog, const char *id);

/* TS orderGameArchives: malloc'd borrowed-pointer list (caller
 * frees the array only). NULL only on OOM. */
qa_catalog_archive **qa_order_game_archives(
    const qa_catalog_product_expectation *product,
    qa_catalog_archive **archives, size_t archive_count, size_t *out_count);

/* TS mapsFor: malloc'd borrowed-pointer winner list. */
const qa_catalog_content_map **qa_content_catalog_maps_for(
    const qa_content_catalog *catalog, const char *id, size_t *out_count);

/* TS mountsFor: owned mount list; free with
 * qa_content_catalog_mounts_free. False only on OOM. */
typedef struct qa_catalog_mount_list {
  qa_content_mount *mounts;
  size_t count;
  char **strings; /* owned backing for ids/paths/digests */
  size_t string_count;
} qa_catalog_mount_list;
bool qa_content_catalog_mounts_for(qa_content_catalog *catalog,
                                   qa_content_id id,
                                   qa_catalog_mount_list *out);
void qa_content_catalog_mount_list_free(qa_catalog_mount_list *list);

/* TS createMountPlan: plan mounts/orders own their backing in the
 * returned mount list + order arrays; free with
 * qa_catalog_mount_plan_free. */
typedef struct qa_catalog_mount_plan_result {
  qa_resolved_mount_plan plan;
  qa_catalog_mount_list mounts;
  qa_mount_id *default_order;
  qa_prefix_mount_order *prefix_orders;
  qa_mount_id *prefix_mounts;
} qa_catalog_mount_plan_result;
bool qa_content_catalog_create_mount_plan(
    qa_content_catalog *catalog,
    const qa_catalog_mount_plan_selection *selection,
    qa_catalog_mount_plan_result *out);
void qa_catalog_mount_plan_result_free(qa_catalog_mount_plan_result *result);

/* TS read(): opens via the opener service and copies the bytes.
 * Malloc'd bytes; NULL only on OOM (missing resources abort as
 * the TS read throws). */
uint8_t *qa_content_catalog_read(qa_content_catalog *catalog,
                                 qa_catalog_mount_plan_opener opener,
                                 qa_content_id content, const char *path,
                                 size_t *out_length);

/* TS authoredStartsFor. *out_found false is the TS null return;
 * the catalog out owns strings (resource is by value). */
bool qa_content_catalog_authored_starts_for(
    qa_content_catalog *catalog, qa_catalog_mount_plan_opener opener,
    const char *id, qa_authored_start_catalog *out, bool *out_found);
void qa_authored_start_catalog_free(qa_authored_start_catalog *catalog);

/* TS AddonDirectory + discoverAddonDirectories result (list in
 * first-wins order, keyed by lower-cased contentDirectory). */
typedef struct qa_catalog_addon_directory {
  qa_game_family family;
  char *name;
  char *content_directory;
  char *physical;
  char *title; /* nullable */
} qa_catalog_addon_directory;
typedef struct qa_catalog_addon_directory_list {
  qa_catalog_addon_directory *values;
  size_t count;
} qa_catalog_addon_directory_list;
bool qa_discover_addon_directories(
    const qa_catalog_nightdive_addon_root *roots, size_t root_count,
    qa_catalog_addon_directory_list *out);
void qa_catalog_addon_directory_list_free(
    qa_catalog_addon_directory_list *list);
/* Lookup by canonical content directory (case-insensitive). */
const qa_catalog_addon_directory *qa_catalog_addon_directory_find(
    const qa_catalog_addon_directory_list *list, const char *content_directory);

/* TS discoverInstalledContent (synchronous). False only on OOM. */
bool qa_discover_installed_content(const qa_discover_content_options *options,
                                   qa_content_catalog *out);

#endif
