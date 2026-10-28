/* Port of src/app/bootstrap/q3-product.ts. Mount-plan verification
 * and restricted-catalog construction are the host below; the
 * startup-variable extraction mirrors startup-commands.ts (other
 * slice) on the landed command tokenizer for the three policy
 * variable names this file reads. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_PRODUCT_H
#define QA_APP_BOOTSTRAP_Q3_PRODUCT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/core/q3_product_policy.h"
typedef struct qa_app_q3_product_host {
  void *ctx;
  /* TS resolveQ3MountRestriction's read(): borrowed productid.txt
   * bytes for the selected product, NULL when absent. */
  const uint8_t *(*product_id_bytes)(void *ctx, const qa_catalog_discovered_product *selected, size_t *out_length);
  /* Builds the restricted catalog + verifies the mount plan.
   * On success *out_catalog is a host-owned catalog (borrowed). */
  const char *(*restrict_catalog)(void *ctx, const qa_content_catalog *catalog,
      const qa_catalog_discovered_product *selected, const qa_content_catalog **out_catalog);
} qa_app_q3_product_host;
typedef struct qa_app_prepared_q3_product {
  const qa_content_catalog *catalog; /* borrowed */
  bool has_q3_product;
  qa_q3_application_product q3_product;
} qa_app_prepared_q3_product;
/* Picks startupCommands + q3Product from options (NULL = none). */
qa_app_prepared_q3_product qa_app_prepare_q3_application_product(
    const qa_content_catalog *catalog, const char *content, const qa_app_options *options,
    const qa_app_q3_product_host *host);
#endif
