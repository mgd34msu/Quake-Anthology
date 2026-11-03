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

  /* Graphical run/menu: TS main opens StartupApplication (openGraphics) and reports its
   * failure. With no game data the first failure is openInitialConfigurationContent's
   * catalog.require of the movement product; the expected text is what bun reports for
   * StartupApplication.open with the same options (empty --content-root, --menu --hidden). */
  {
    char corpus_template[] = "/tmp/qa-main-corpus-XXXXXX";
    char user_template[] = "/tmp/qa-main-user-XXXXXX";
    char *corpus = mkdtemp(corpus_template), *user = mkdtemp(user_template);
    CHECK(corpus != NULL && user != NULL);
    const char *argv[] = {"--menu", "--hidden", "--content-root", corpus, "--user-content-root", user};
    qa_content_root_search search = {0};
    qa_app_application_command command = qa_app_parse_application_command(argv, 6, search, NULL);
    CHECK(strcmp(command.kind, "menu") == 0);
    composition startup_comp = {0};
    qa_app_splash_screen *splash = NULL;
    startup_main run;
    memset(&run, 0, sizeof run);
    run.options = &command.options;
    run.entry = composition_entry_for(command.kind);
    run.splash = &splash;
    composition_run_startup(&run, &startup_comp);
    CHECK(startup_comp.error != NULL &&
          strcmp(startup_comp.error, "Content q1-classic-id1 requires: q1/id1/pak0.pak, q1/id1/pak1.pak, q1/id1") == 0);
    if (startup_comp.error != NULL) printf("startup error: %s\n", startup_comp.error);
    CHECK(run.application == NULL);
    free(startup_comp.error);
    composition_close(&startup_comp);
    qa_app_application_command_destroy(&command);
    char cleanup[160];
    snprintf(cleanup, sizeof cleanup, "rm -rf '%s' '%s'", corpus, user);
    CHECK(system(cleanup) == 0);
  }

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

  /* Dedicated/game Application host: owned-path bundles present,
   * session-driven simulation shim, legacy step only as the
   * error-surfacing fallback (never a fake advance). */
  composition host_comp = {0};
  host_comp.options = &options;
  qa_app_application_host app_host = composition_dedicated_host(&host_comp);
  CHECK(app_host.has_content);
  CHECK(app_host.content.product == composition_content_product);
  CHECK(app_host.content.require_product == composition_content_require);
  CHECK(app_host.content.open_loaded == composition_open_loaded);
  CHECK(app_host.content.prepare_mods == composition_prepare_mods);
  CHECK(app_host.content.prepare_grapple == composition_prepare_grapple);
  CHECK(app_host.content.prepare_weapon_behaviors ==
        composition_prepare_weapon_behaviors);
  CHECK(app_host.has_simulation);
  CHECK(app_host.simulation.step == composition_sim_step);
  CHECK(app_host.simulation.checkpoint == composition_sim_checkpoint);
  CHECK(app_host.simulation.close == composition_sim_close);
  CHECK(app_host.open == dedicated_open);
  CHECK(app_host.close == dedicated_close);
  CHECK(!host_comp.concrete_built);
  const void *step_output = (const void *)1;
  CHECK(dedicated_step(&host_comp, NULL, 16, &step_output) ==
        composition_missing_game);
  CHECK(step_output == NULL);
  composition_set_error(&host_comp, "kept");
  CHECK(dedicated_step(&host_comp, NULL, 16, &step_output) == host_comp.error);
  free(host_comp.error);
  host_comp.error = NULL;
  /* Nested-game host must not close composition services. */
  qa_app_application_host game_host =
      composition_application_host(&host_comp, false);
  CHECK(game_host.close == composition_game_close);
  CHECK(game_host.close(game_host.ctx, NULL) == NULL);

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
