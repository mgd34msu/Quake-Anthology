/*
 * Port of src/content/mods/catalog.ts: discoverGameplayMods.
 * The TS CatalogProduct (owner: src/content/catalog/index.ts) and
 * MountedContent (owner: src/content/mounts/index.ts) modules are
 * not ported yet; the two small structs below mirror only the
 * fields this file reads, with collision-proof names, until those
 * modules land. The async mounted.open becomes a synchronous
 * fn+ctx; a NULL content_filter mirrors calling open without the
 * mount predicate. Per-component TS try/catch becomes an
 * unavailable result for the explicit checks this function makes
 * (missing declaration file, digest mismatch); malformed
 * declaration JSON still aborts through the SaveReader, as in
 * other ports, instead of becoming an unavailable reason.
 * Returns a malloc'd result array (NULL only on OOM, or when no
 * gameplay-mods.json exists, with *out_count 0). Description
 * source/provider strings and unavailable reasons are malloc'd.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_MODS_CATALOG_H
#define QA_CONTENT_MODS_CATALOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/mods.h"

/* Mirror of the read fields of TS CatalogProduct. */
typedef struct qa_mod_catalog_product {
  qa_content_id id;
  struct {
    const char *id;
    const char *title;
    const char *family;  /* "q1" | "q2" | "q3" */
    const char *edition; /* "classic" | "rerelease" */
  } expectation;
} qa_mod_catalog_product;

/* One opened mounted file: borrowed bytes + its content digest. */
typedef struct qa_mod_catalog_file {
  const uint8_t *bytes;
  size_t length;
  qa_content_digest digest;
} qa_mod_catalog_file;

/* Mirror of TS MountedContent.open(path, accept?): false means
 * the file is absent (TS null). */
typedef struct qa_mod_mounted_content {
  void *ctx;
  bool (*open)(void *ctx, const char *path, const char *content_filter,
               qa_mod_catalog_file *out);
} qa_mod_mounted_content;

/* TS DiscoveredGameplayMod. */
typedef struct qa_discovered_gameplay_mod {
  bool is_available;
  qa_mod_description description;          /* unavailable form */
  struct qa_resolved_gameplay_mod mod;     /* available form */
} qa_discovered_gameplay_mod;

qa_discovered_gameplay_mod *qa_discover_gameplay_mods(
    const qa_mod_catalog_product *product,
    const qa_mod_mounted_content *mounted, size_t *out_count);

#endif
