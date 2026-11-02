/* Composition-root helpers from src/main.c (TS src/main.ts):
 * option->phase selection, run-loop guard, catalog product
 * selection, first-error capture and named missing-service
 * phases. main.c is included with its entry point renamed so
 * the static helpers under test are the shipped ones.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#define main qa_main_composition_entry_unused
#include "../../src/main.c"
#undef main

static int checks, failures;
#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

int main(void) {
  /* TS main.ts:75 dedicated ? openApplication : StartupApplication. */
  CHECK(composition_kind_for(true) == COMPOSITION_DEDICATED);
  CHECK(composition_kind_for(false) == COMPOSITION_STARTUP);
  CHECK(strcmp(composition_entry_for("run"), "run") == 0);
  CHECK(strcmp(composition_entry_for("menu"), "menu") == 0);
  CHECK(strcmp(composition_entry_for(NULL), "menu") == 0);

  /* TS main.ts:64 LLM settings skipped only for dedicated non-menu. */
  CHECK(!composition_llm_enabled("run", true));
  CHECK(composition_llm_enabled("menu", true));
  CHECK(composition_llm_enabled("run", false));
  CHECK(composition_llm_enabled("menu", false));

  /* TS run(): quit/finished stop; frameLimit is an exclusive bound. */
  CHECK(composition_should_continue(false, false, false, 0, 12345));
  CHECK(!composition_should_continue(true, false, false, 0, 0));
  CHECK(!composition_should_continue(false, true, false, 0, 0));
  CHECK(composition_should_continue(false, false, true, 10, 9));
  CHECK(!composition_should_continue(false, false, true, 10, 10));
  CHECK(!composition_should_continue(false, false, true, 10, 11));

  /* First phase error wins; later phases must not replace it. */
  composition comp = {0};
  composition_set_error(&comp, "first %d", 7);
  composition_set_error(&comp, "second");
  CHECK(comp.error != NULL && strcmp(comp.error, "first 7") == 0);
  free(comp.error);
  comp.error = NULL;

  /* Named missing-service phases: menu vs run vs lobby. */
  (void)comp_phase_menu_or_game(&comp, false);
  CHECK(comp.error != NULL && strstr(comp.error, "configuration.ts") != NULL &&
        strstr(comp.error, "startup-menu.ts") != NULL);
  (void)comp_phase_lobby(&comp); /* skipped: first error kept */
  CHECK(strstr(comp.error, "local-lobby.ts") == NULL);
  free(comp.error);
  comp.error = NULL;
  (void)comp_phase_menu_or_game(&comp, true);
  CHECK(comp.error != NULL && strstr(comp.error, "application.ts") != NULL &&
        strstr(comp.error, "loadApplicationContent") != NULL);
  free(comp.error);
  comp.error = NULL;
  (void)comp_phase_lobby(&comp);
  CHECK(comp.error != NULL && strstr(comp.error, "online.ts") != NULL);
  free(comp.error);
  comp.error = NULL;

  /* Catalog selection over a borrowed two-product catalog. */
  qa_catalog_discovered_product products[2] = {0};
  products[0].id = (char *)"q1:classic:id1:1";
  products[0].expectation.id = "q1-classic-id1";
  products[0].availability.kind = QA_CATALOG_MISSING;
  products[1].id = (char *)"q2:classic:baseq2:1";
  products[1].expectation.id = "q2-classic-baseq2";
  products[1].availability.kind = QA_CATALOG_INSTALLED;
  qa_content_catalog catalog = {0};
  qa_content_catalog_init(&catalog, "/corpus", products, 2, NULL, 0, 0,
                          NULL, NULL, 0);
  CHECK(composition_find_product(&catalog, "q1-classic-id1") == &products[0]);
  CHECK(composition_find_product(&catalog, "q2:classic:baseq2:1") == &products[1]);
  CHECK(composition_find_product(&catalog, "nope") == NULL);
  CHECK(composition_find_product(&catalog, NULL) == NULL);
  CHECK(composition_first_installed(&catalog) == &products[1]);

  /* composition_select_product: menu fallback vs dedicated require. */
  qa_app_options options = {0};
  options.product = (char *)"q1-classic-id1";
  options.corpus_root = (char *)"/corpus";
  composition menu_comp = {0};
  menu_comp.options = &options;
  menu_comp.catalog = catalog;
  menu_comp.catalog_open = false; /* borrowed; close must not destroy */
  composition_select_product(&menu_comp, false);
  CHECK(menu_comp.error == NULL);
  CHECK(menu_comp.selected == &products[0]);
  CHECK(menu_comp.installed == &products[1]); /* charset fallback */

  composition dedicated_comp = {0};
  dedicated_comp.options = &options;
  dedicated_comp.catalog = catalog;
  composition_select_product(&dedicated_comp, true);
  CHECK(dedicated_comp.error != NULL &&
        strstr(dedicated_comp.error, "requires:") != NULL);
  free(dedicated_comp.error);

  options.product = (char *)"unknown-product";
  composition unknown_comp = {0};
  unknown_comp.options = &options;
  unknown_comp.catalog = catalog;
  composition_select_product(&unknown_comp, false);
  CHECK(unknown_comp.error != NULL &&
        strcmp(unknown_comp.error,
               "Unknown requested content or mod: unknown-product") == 0);
  free(unknown_comp.error);
  qa_content_catalog_destroy(&catalog); /* borrowed: frees nothing owned */

  /* TS Buffer.from(product.id).toString("hex") for the mount plan. */
  char *hex = hex_encode("q1");
  CHECK(hex != NULL && strcmp(hex, "7131") == 0);
  free(hex);
  hex = hex_encode("");
  CHECK(hex != NULL && strcmp(hex, "") == 0);
  free(hex);

  /* Dedicated host readPixels uses the exact TS message. */
  uint8_t *bytes = NULL;
  size_t count = 99;
  CHECK(strcmp(dedicated_read_pixels(NULL, &bytes, &count),
               "Dedicated applications have no framebuffer") == 0);
  CHECK(bytes == NULL && count == 0);

  printf("test_main_composition: %d checks, %d failures\n", checks, failures);
  return failures != 0;
}
