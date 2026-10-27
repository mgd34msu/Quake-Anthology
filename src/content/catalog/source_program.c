/* Port of src/content/catalog/source-program.ts. GPL-2.0-or-later. */
#include "qa/content/catalog/source_program.h"

#include "catalog_internal.h"

const qa_catalog_discovered_product *qa_source_program_product(
    const qa_content_catalog *catalog, const char *content) {
  const qa_catalog_discovered_product *product =
      qa_content_catalog_require(catalog, content);
  qa_catalog_string_list visited = {0};
  for (;;) {
    bool builtin = false;
    for (size_t i = 0; i < qa_expected_product_count; i++)
      if (strcmp(qa_expected_products[i].id, product->expectation.id) == 0)
        builtin = true;
    if (builtin || product->expectation.required_program_count != 0 ||
        product->expectation.base_product == NULL) break;
    if (qa_catalog_string_contains((const char *const *)visited.values,
                                   visited.count, product->id)) {
      qa_catalog_string_list_free(&visited);
      qa_catalog_error("Cyclic source program dependency");
    }
    (void)qa_catalog_string_push(&visited, qa_catalog_dup(product->id));
    const qa_catalog_discovered_product *base =
        qa_content_catalog_require(catalog, product->expectation.base_product);
    if (base->expectation.family != product->expectation.family ||
        strcmp(base->expectation.edition, product->expectation.edition) != 0) {
      qa_catalog_string_list_free(&visited);
      qa_catalog_error("Source program dependency changes game family or edition");
    }
    product = base;
  }
  qa_catalog_string_list_free(&visited);
  return product;
}

char *qa_source_program_implementation(
    const qa_catalog_product_expectation *product) {
  return qa_catalog_format("%s:source/%s/%s",
                           qa_game_family_name(product->family),
                           product->edition, product->campaign);
}

static char *content_part(const char *content, size_t index) {
  const char *start = content;
  for (size_t i = 0; i < index; i++) {
    const char *colon = strchr(start, ':');
    if (colon == NULL) return NULL;
    start = colon + 1;
  }
  const char *colon = strchr(start, ':');
  return colon != NULL ? qa_catalog_format("%.*s", (int)(colon - start), start)
                       : qa_catalog_dup(start);
}

char *qa_selected_source_program(const qa_execution_selection *execution,
                                 size_t execution_count,
                                 qa_provider_reference entities,
                                 bool *out_found) {
  if (out_found != NULL) *out_found = false;
  const qa_execution_selection *module = NULL;
  for (size_t i = 0; i < execution_count; i++) {
    if (strcmp(execution[i].role, "server-game") == 0 &&
        strcmp(execution[i].owner.provider, entities.provider) == 0 &&
        strcmp(execution[i].owner.content, entities.content) == 0) {
      module = &execution[i];
      break;
    }
  }
  if (module == NULL || module->kind != QA_EXECUTION_MODULE_TYPESCRIPT ||
      strcmp(module->implementation, entities.provider) == 0) {
    char *part = content_part(entities.content, 2);
    if (part != NULL && out_found != NULL) *out_found = true;
    return part;
  }
  char *family = content_part(entities.content, 0);
  char *edition = content_part(entities.content, 1);
  const qa_catalog_product_expectation *product = NULL;
  for (size_t i = 0; i < qa_expected_product_count; i++) {
    if (family == NULL || edition == NULL) break;
    if (strcmp(qa_game_family_name(qa_expected_products[i].family), family) != 0 ||
        strcmp(qa_expected_products[i].edition, edition) != 0) continue;
    char *implementation = qa_source_program_implementation(&qa_expected_products[i]);
    bool same = implementation != NULL &&
                strcmp(implementation, module->implementation) == 0;
    free(implementation);
    if (same) { product = &qa_expected_products[i]; break; }
  }
  free(family); free(edition);
  if (product == NULL)
    qa_catalog_error("Unsupported source implementation %s", module->implementation);
  char *campaign = qa_catalog_dup(product->campaign);
  if (campaign != NULL && out_found != NULL) *out_found = true;
  return campaign;
}
