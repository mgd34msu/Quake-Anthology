/*
 * Port of src/content/catalog/products.ts: the ProductExpectation
 * shape and the expectedProducts inventory. The name is
 * qa_catalog_product_expectation (not qa_product_expectation)
 * because include/qa/persistence/q1_selection.h already mirrors
 * this type under qa_product_expectation; reconciling that mirror
 * is outside this slice. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_PRODUCTS_H
#define QA_CONTENT_CATALOG_PRODUCTS_H

#include <stddef.h>

#include "qa/contracts/content.h"

typedef struct qa_catalog_product_expectation {
  const char *id;
  qa_game_family family;
  const char *edition;
  const char *campaign;
  const char *title;
  const char *content_directory;
  const char *base_product; /* nullable */
  const char *const *required_content_archives;
  size_t required_content_archive_count;
  const char *const *required_programs;
  size_t required_program_count;
  const char *map_witness;       /* nullable */
  const char *unresolved_reason; /* nullable */
} qa_catalog_product_expectation;

/* TS expectedProducts. */
extern const qa_catalog_product_expectation qa_expected_products[];
extern const size_t qa_expected_product_count;

#endif
