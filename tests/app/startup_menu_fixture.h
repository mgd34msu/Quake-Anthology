/* Shared StartupMenu fixture for tests/app tests: the menu options as
 * StartupApplication.openGraphics builds them over the retail Q1 classic
 * id1 content (tests/app/oracles/startup_menu_draw.ts builds the same):
 * StartupSelectionModel over the discovered catalog, loadMenuFont and
 * loadMenuTypography over the startup font mounts, loadNativeUiArt with
 * { frames: false } and a CharacterTurntable. Needs QTS_TEST_CORPUS.
 * GPL-2.0-or-later. */
#ifndef QA_TESTS_APP_STARTUP_MENU_FIXTURE_H
#define QA_TESTS_APP_STARTUP_MENU_FIXTURE_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/character_turntable.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/menu_art.h"
#include "qa/app/bootstrap/menu_font.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/startup_menu.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/contracts/identity.h"
#include "qa/render/scene/resources.h"
#include "qa/ui/common/assets.h"

typedef struct startup_menu_fixture {
  qa_identity_owner identity;
  qa_seat_id seat; qa_client_id client;
  qa_app_options options;
  qa_app_startup_selection_model *model;
  const qa_catalog_discovered_product *product;
  qa_catalog_mount_list list; qa_mount_id *order; qa_content_mounted *mounted;
  qa_scene_image_registry images;
  qa_app_menu_font *font;
  qa_app_menu_typography *typography;
  qa_native_ui_art art;
  qa_app_character_turntable *turntable;
  qa_app_startup_save_list saves;
  int bad_ctx;
  /* The art reader's decoded pixels: the queued create operations reference them (TS GC). */
  void *art_pixels[4]; size_t art_pixel_count;
} startup_menu_fixture;

static qa_content_catalog *startup_menu_fixture_catalog(void *ctx) {
  return qa_app_startup_selection_catalog(((startup_menu_fixture *)ctx)->model);
}
static void startup_menu_fixture_print(void *ctx, const char *text) { (void)ctx; (void)text; }
static bool startup_menu_fixture_art_read(void *ctx, const char *path, qa_image_level *out) {
  startup_menu_fixture *f = ctx;
  qa_app_load_menu_art_image(path, out);
  if (f->art_pixel_count < 4) f->art_pixels[f->art_pixel_count++] = (void *)out->pixels.bytes;
  return true;
}
/* Opens the fixture; false when the corpus has no Q1 classic id1. */
static bool startup_menu_fixture_open(startup_menu_fixture *f, const char *corpus_root, const char *user_root, bool turntable) {
  memset(f, 0, sizeof *f);
  const char *argv[] = {"--content-root", corpus_root, "--user-content-root", user_root, "--game", "q1-classic-id1", "--map", "e1m2"};
  qa_content_root_search search = {0};
  qa_app_application_command command = qa_app_parse_application_command(argv, 8, search, NULL);
  if (strcmp(command.kind, "run") != 0) { qa_app_application_command_destroy(&command); return false; }
  f->options = command.options;
  memset(&command.options, 0, sizeof command.options);
  qa_app_application_command_destroy(&command);
  qa_content_catalog *catalog = calloc(1, sizeof *catalog);
  qa_discover_content_options discover;
  memset(&discover, 0, sizeof discover);
  discover.corpus_root = corpus_root;
  discover.discover_mods = false;
  discover.inspector = qa_app_default_archive_inspector();
  if (catalog == NULL || !qa_discover_installed_content(&discover, catalog)) abort();
  f->model = qa_app_startup_selection_model_new(catalog, &f->options);
  qa_app_startup_selection_prepare_maps(f->model);
  catalog = qa_app_startup_selection_catalog(f->model);
  f->product = qa_content_catalog_require(catalog, "q1-classic-id1");
  if (!qa_identity_owner_create(&f->identity, "startup-menu-draw")) abort();
  f->seat = qa_identity_owner_seat(&f->identity, 0);
  f->client = qa_identity_owner_client(&f->identity, 0, 0);
  if (!qa_content_catalog_mounts_for(catalog, f->product->id, &f->list)) abort();
  f->order = calloc(f->list.count + 1, sizeof *f->order);
  for (size_t i = 0; i < f->list.count; i++)
    f->order[i] = f->list.mounts[i].kind == QA_CONTENT_MOUNT_ARCHIVE ? f->list.mounts[i].as.archive.identity.id
                                                                     : f->list.mounts[i].as.loose.identity.id;
  qa_resolved_mount_plan plan;
  memset(&plan, 0, sizeof plan);
  plan.id = "mount-plan:startup:font";
  plan.mounts = f->list.mounts; plan.mount_count = f->list.count;
  plan.default_order = f->order; plan.default_order_count = f->list.count;
  f->mounted = qa_open_mount_plan(&plan, NULL);
  qa_renderer_resource_owner owner = {&f->images, f->identity.session, 0};
  qa_scene_image_registry_init(&f->images, owner, NULL);
  qa_app_menu_font_options font_options;
  memset(&font_options, 0, sizeof font_options);
  font_options.catalog = catalog;
  font_options.mounts = f->mounted;
  font_options.family = QA_GAME_FAMILY_Q1;
  font_options.rerelease = false;
  font_options.images = &f->images;
  f->font = qa_app_load_menu_font(&font_options);
  qa_text_font_selection font = qa_app_menu_font_selection(f->font);
  f->typography = qa_app_load_menu_typography(catalog, &f->images, font.classic, NULL);
  qa_ui_load_native_art_selected(&f->art, font.classic->picture.image.image.source_resource.id, &f->images,
                                 startup_menu_fixture_art_read, f, false, true);
  if (turntable) {
    qa_app_character_turntable_options options;
    memset(&options, 0, sizeof options);
    options.catalog_ctx = f; options.catalog = startup_menu_fixture_catalog;
    options.images = &f->images;
    options.print_ctx = f; options.print = startup_menu_fixture_print;
    f->turntable = qa_app_character_turntable_create(options);
  }
  return true;
}
/* Options with the fixture's model, art, fonts and turntable; callbacks are the caller's. */
static qa_app_startup_menu_options startup_menu_fixture_options(startup_menu_fixture *f, void *ctx) {
  qa_app_startup_menu_options options;
  memset(&options, 0, sizeof options);
  options.ctx = ctx;
  options.seat = f->seat;
  options.model = f->model;
  options.character_turntable = f->turntable;
  options.art = &f->art;
  options.font = qa_app_menu_typography_body(f->typography);
  options.title_font = qa_app_menu_typography_title(f->typography);
  return options;
}
static void startup_menu_fixture_close(startup_menu_fixture *f) {
  if (f->turntable) qa_app_character_turntable_destroy(f->turntable);
  qa_native_ui_art_close(&f->art);
  qa_app_menu_typography_close(f->typography);
  qa_app_menu_font_close(f->font);
  qa_scene_image_registry_close(&f->images);
  qa_scene_image_registry_destroy(&f->images);
  for (size_t i = 0; i < f->art_pixel_count; i++) free(f->art_pixels[i]);
  qa_content_mounted_close(f->mounted);
  qa_content_mounted_destroy(f->mounted);
  free(f->order);
  qa_content_catalog_mount_list_free(&f->list);
  qa_app_startup_selection_model_destroy(f->model);
  qa_app_options_destroy(&f->options);
  qa_identity_owner_destroy(&f->identity);
}
#endif
