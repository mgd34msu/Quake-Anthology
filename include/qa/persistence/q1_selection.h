/*
 * Port of src/persistence/q1-selection.ts: original v5 does not
 * identify progs.dat or the game directory, so selection never
 * guesses between mods. qa_select_q1_save_product_in_catalog
 * selects over the content catalog (TS InstalledCatalog); the
 * mirrored CatalogProduct fields below are the older form its unit
 * tests build, and both share one selection. Catalog fields the selector never
 * reads (archives, loose roots, diagnostics) are omitted until
 * then. Plain TS Errors abort printing "Error: ..." (the
 * provider_ownership precedent). Case folding is ASCII byte-wise;
 * TS toLowerCase is Unicode (paths/ids here are ASCII in
 * practice). GPL-2.0-or-later.
 */
#ifndef QA_PERSISTENCE_Q1_SELECTION_H
#define QA_PERSISTENCE_Q1_SELECTION_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/persistence/q1.h"

/* Mirrored TS ProductExpectation (src/content/catalog/products.ts). */
typedef struct qa_product_expectation {
  const char *id;
  const char *family; /* TS GameFamily */
  const char *edition;
  const char *campaign;
  const char *title;
  const char *content_directory;
  const char *base_product; /* nullable */
  const char *const *required_content_archives;
  size_t required_content_archive_count;
  const char *const *required_programs;
  size_t required_program_count;
  const char *map_witness;        /* nullable */
  const char *unresolved_reason;  /* nullable */
} qa_product_expectation;

/* Mirrored TS ContentMap (src/content/catalog/index.ts). */
typedef struct qa_content_map {
  const char *path;
  const char *source;
  bool has_member_index; /* memberIndex is number | null */
  double member_index;
} qa_content_map;

/* Mirrored TS ProductAvailability: kind is "installed" | "missing"
 * (requirements) | "unresolved" (reason). */
typedef struct qa_product_availability {
  const char *kind;
  const char *const *requirements;
  size_t requirement_count;
  const char *reason;
} qa_product_availability;

/* Mirrored TS CatalogProduct (fields read by q1-selection.ts). */
typedef struct qa_catalog_product {
  qa_content_id id;
  qa_product_expectation expectation;
  qa_product_availability availability;
  const qa_content_map *maps;
  size_t map_count;
} qa_catalog_product;

/* Mirrored TS InstalledCatalog: only `products` is read here. */
typedef struct qa_installed_catalog {
  const qa_catalog_product *products;
  size_t product_count;
} qa_installed_catalog;

/* `selected` NULL spells the TS undefined argument. The result
 * borrows from the catalog. */
const qa_catalog_product *qa_select_q1_save_product(
    const qa_installed_catalog *catalog, const qa_q1_save_data *save,
    const char *path, const char *selected);
/* The same selection over the content catalog (TS InstalledCatalog). */
const qa_catalog_discovered_product *qa_select_q1_save_product_in_catalog(
    const qa_content_catalog *catalog, const qa_q1_save_data *save,
    const char *path, const char *selected);

#endif
