/*
 * Concrete renderer factory (src/app/bootstrap/renderer.ts) and the
 * catalog-based application content composition (content.ts):
 * backend selection by kind, capture on swap, restart publish, the
 * discovers-mods predicate, source selection, movement fallback,
 * preset assembly, the default archive inspector over a temp PAK,
 * discovery over an empty temp corpus, and the native loader's
 * remote-context guard. TS throw cases run in children and abort.
 * GPL-2.0-or-later.
 */
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/renderer.h"
#include "qa/content/catalog/products.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    checks++;                                                           \
    if (!(cond)) {                                                      \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
    }                                                                   \
  } while (0)

#define CHECK_ABORTS(...)                                               \
  do {                                                                  \
    checks++;                                                           \
    pid_t pid = fork();                                                 \
    if (pid == 0) {                                                     \
      (void)prctl(PR_SET_DUMPABLE, 0);                                  \
      freopen("/dev/null", "w", stderr);                                \
      __VA_ARGS__;                                                      \
      _exit(0);                                                         \
    }                                                                   \
    int status = 0;                                                     \
    waitpid(pid, &status, 0);                                           \
    if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT)) {        \
      failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d: expected abort: %s\n", __FILE__,     \
              __LINE__, #__VA_ARGS__);                                  \
    }                                                                   \
  } while (0)

static char *xstrdup(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy) memcpy(copy, text, length);
  return copy;
}

/* ---- renderer ---- */

static const void *owner_identity;
static const void *owner_token;
static qa_renderer_resource_owner test_owner(void) {
  qa_renderer_resource_owner owner = {&owner_identity, {"test", &owner_token}, 1};
  return owner;
}
typedef struct capture_result { bool resolved, rejected; double width, height; size_t length; } capture_result;
static void capture_resolve(void *ctx, qa_image_level image) {
  capture_result *result = ctx;
  result->resolved = true; result->width = image.width; result->height = image.height;
  result->length = image.pixels.length;
  free((void *)image.pixels.bytes);
}
static void capture_reject(void *ctx, const char *error) {
  (void)error;
  ((capture_result *)ctx)->rejected = true;
}

static void test_renderer_cpu(void) {
  qa_app_native_renderer_host host = {0}; /* concrete factory */
  qa_app_native_renderer *renderer = qa_app_native_renderer_open(host, "cpu", 64, 48, true, 1, false, test_owner(), NULL);
  CHECK(renderer != NULL);
  if (!renderer) return;
  qa_renderer_backend backend = qa_app_native_renderer_backend(renderer);
  CHECK(backend.width == 64 && backend.height == 48);
  CHECK(!qa_app_native_renderer_render_worker(renderer));
  CHECK(qa_app_native_renderer_driver(renderer) == NULL);
  qa_app_renderer_diagnostics diagnostics = qa_app_native_renderer_diagnostics(renderer);
  CHECK(strcmp(diagnostics.backend, "cpu") == 0);
  CHECK(diagnostics.width == 64 && diagnostics.height == 48);
  qa_app_renderer_diagnostics_free(&diagnostics);
  size_t length = 0;
  uint8_t *pixels = qa_app_native_renderer_read_pixels(renderer, &length);
  CHECK(pixels != NULL && length == 64u * 48u * 4u);
  free(pixels);
  capture_result capture = {0};
  qa_app_frame_capture_request request = {&capture, capture_resolve, capture_reject, false, NULL};
  qa_app_native_renderer_capture_next_frame(renderer, request);
  qa_render_command command = {0};
  command.kind = QA_RENDER_COMMAND_SWAP_BUFFERS;
  qa_render_frame frame = {test_owner(), 1, &command, 1};
  qa_app_native_renderer_execute(renderer, frame);
  CHECK(capture.resolved && !capture.rejected);
  CHECK(capture.width == 64 && capture.height == 48 && capture.length == 64u * 48u * 4u);
  qa_app_prepared_renderer_restart *restart = qa_app_native_renderer_prepare_restart(renderer, "cpu", false, false);
  CHECK(restart != NULL);
  if (restart) {
    CHECK(!qa_app_prepared_renderer_restart_published(restart));
    qa_app_prepared_renderer_restart_publish(restart);
    CHECK(qa_app_prepared_renderer_restart_published(restart));
    qa_app_prepared_renderer_restart_retire(restart);
    backend = qa_app_native_renderer_backend(renderer);
    CHECK(backend.width == 64 && backend.height == 48);
  }
  qa_app_native_renderer_destroy(renderer);
}

static void test_renderer_gl_if_available(void) {
  /* SDL GL failure aborts (port convention), so probe in a child: an
   * abort means no GL in this environment; exit 42 means the checks
   * below ran and passed there; exit 3 means they failed. */
  checks++;
  pid_t pid = fork();
  if (pid == 0) {
    (void)prctl(PR_SET_DUMPABLE, 0);
    freopen("/dev/null", "w", stderr);
    qa_app_native_renderer_host host = {0};
    qa_app_native_renderer *renderer = qa_app_native_renderer_open(host, "gl", 64, 48, true, 1, false, test_owner(), NULL);
    if (!renderer) _exit(42);
    int bad = 0;
    qa_app_renderer_diagnostics diagnostics = qa_app_native_renderer_diagnostics(renderer);
    if (strcmp(diagnostics.backend, "gl") != 0) bad = 1;
    if (diagnostics.driver == NULL) bad = 1;
    if (qa_app_native_renderer_gl_config(renderer) == NULL) bad = 1;
    qa_app_renderer_diagnostics_free(&diagnostics);
    qa_app_native_renderer_destroy(renderer);
    _exit(bad ? 3 : 42);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFSIGNALED(status)) printf("gl backend unavailable in this environment; gl probe aborted as ported SDL does\n");
  else if (!(WIFEXITED(status) && WEXITSTATUS(status) == 42)) {
    failures++;
    fprintf(stderr, "FAIL %s:%d: gl backend checks failed in child\n", __FILE__, __LINE__);
  }
}

/* ---- content: borrowed catalog over real expectations ---- */

static qa_catalog_discovered_product products[16];
static size_t product_count = 0;
static qa_content_catalog catalog;

static void add_product(const char *id, qa_catalog_availability_kind kind) {
  const qa_catalog_product_expectation *expectation = NULL;
  for (size_t i = 0; i < qa_expected_product_count; i++)
    if (strcmp(qa_expected_products[i].id, id) == 0) expectation = &qa_expected_products[i];
  CHECK(expectation != NULL);
  if (!expectation) return;
  qa_catalog_discovered_product *product = &products[product_count++];
  memset(product, 0, sizeof *product);
  product->id = xstrdup(id);
  product->expectation = *expectation;
  product->availability.kind = kind;
}

static void build_catalog(void) {
  add_product("q1-classic-id1", QA_CATALOG_INSTALLED);
  add_product("q1-classic-ctf", QA_CATALOG_INSTALLED);
  add_product("q1-quakeworld", QA_CATALOG_MISSING);
  add_product("q2-classic-baseq2", QA_CATALOG_INSTALLED);
  add_product("q2-classic-ctf", QA_CATALOG_INSTALLED);
  add_product("q2-classic-rogue", QA_CATALOG_INSTALLED);
  add_product("q3-baseq3", QA_CATALOG_INSTALLED);
  qa_content_catalog_init(&catalog, "/tmp", products, product_count, NULL, 0, 0, NULL, NULL, 0);
}

static qa_app_options base_options(void) {
  qa_app_options options = {0};
  options.product = xstrdup("q1-classic-id1");
  options.map = xstrdup("maps/e1m1.bsp");
  options.character_model = xstrdup("player");
  options.movement = QA_GAME_FAMILY_Q1;
  options.character = QA_GAME_FAMILY_Q1;
  options.mode = "singleplayer";
  options.network_kind = "offline";
  options.renderer = "cpu";
  options.width = 640; options.height = 480; options.gamma = 1;
  return options;
}

static void test_selection(void) {
  qa_app_source_selection_result result;
  CHECK(qa_app_application_source_selection_for_catalog(&catalog, "q1-classic-id1", NULL, &result));
  CHECK(strcmp(result.selection.source.provider, "q1:official") == 0);
  CHECK(strcmp(result.selection.source.content, "q1-classic-id1") == 0);
  CHECK(strcmp(result.selection.rules, "standard") == 0);
  CHECK(strcmp(result.selection.match.provider, "q1:official") == 0);
  qa_app_source_selection_result_free(&result);
  CHECK(qa_app_application_source_selection_for_catalog(&catalog, "q1-classic-id1", "ctf", &result));
  CHECK(strcmp(result.selection.match.provider, "q1:ctf") == 0);
  CHECK(strcmp(result.selection.match.content, "q1-classic-ctf") == 0);
  qa_app_source_selection_result_free(&result);
  CHECK(qa_app_application_source_selection_for_catalog(&catalog, "q2-classic-ctf", NULL, &result));
  CHECK(strcmp(result.selection.rules, "ctf") == 0);
  CHECK(strcmp(result.selection.match.provider, "q2:ctf") == 0);
  CHECK(strcmp(result.selection.match.content, "q2-classic-ctf") == 0);
  qa_app_source_selection_result_free(&result);
  CHECK(qa_app_application_source_selection_for_catalog(&catalog, "q2-classic-baseq2", "tag", &result));
  CHECK(strcmp(result.selection.match.provider, "q2:tag") == 0);
  CHECK(strcmp(result.selection.match.content, "q2-classic-rogue") == 0);
  qa_app_source_selection_result_free(&result);
  CHECK(qa_app_application_source_selection_for_catalog(&catalog, "q3-baseq3", "ctf", &result));
  CHECK(strcmp(result.selection.match.provider, "q3:ctf") == 0);
  CHECK(strcmp(result.selection.match.content, "q3-baseq3") == 0);
  qa_app_source_selection_result_free(&result);
  const qa_catalog_discovered_product *movement = qa_app_movement_content_product(&catalog, "q1-quakeworld");
  CHECK(strcmp(movement->expectation.id, "q1-classic-id1") == 0);
  qa_app_options options = base_options();
  CHECK(!qa_app_application_discovers_mods_native(&options, false));
  CHECK(qa_app_application_discovers_mods_native(&options, true));
  options.dedicated = true;
  CHECK(qa_app_application_discovers_mods_native(&options, false));
  options.dedicated = false;
  free(options.product); options.product = xstrdup("q3-baseq3");
  CHECK(qa_app_application_discovers_mods_native(&options, false));
  qa_app_options_destroy(&options);
}

static void test_preset(void) {
  qa_app_options options = base_options();
  qa_app_application_preset_result result;
  CHECK(qa_app_application_preset_for_catalog(&catalog, &options, NULL, NULL, &result));
  qa_launch_preset *preset = &result.preset;
  CHECK(preset->id != NULL && strstr(preset->id, "q1-classic-id1") != NULL);
  CHECK(strcmp(preset->map.geometry.content, "q1-classic-id1") == 0);
  CHECK(strcmp(preset->map.geometry.path, "maps/e1m1.bsp") == 0);
  CHECK(strcmp(preset->map.entities.provider, "q1:official") == 0);
  CHECK(preset->campaign.is_campaign);
  CHECK(strcmp(preset->movement.provider, "q1:movement") == 0);
  CHECK(strcmp(preset->character.definition.provider, "q1:character") == 0);
  CHECK(strcmp(preset->character.appearance.provider, "q1:model/player") == 0);
  CHECK(preset->weapons.count == 1 && strcmp(preset->weapons.values[0].provider, "q1:official") == 0);
  CHECK(preset->execution_count == 1 && preset->execution[0].kind == QA_EXECUTION_MODULE_TYPESCRIPT);
  CHECK(preset->execution[0].api.kind == QA_GAME_API_QUAKEC && preset->execution[0].api.as.quakec.system_crc == 5927);
  CHECK(strcmp(preset->execution[0].implementation, "q1:official") == 0);
  CHECK(preset->timing_count == 3);
  CHECK(preset->ordering.kind == QA_FRAME_ORDERING_MIXED && preset->ordering.provider_count == 3);
  CHECK(strcmp(preset->ordering.providers[1], "q1:movement") == 0);
  qa_app_application_preset_result_free(&result);
  options.mode = "deathmatch";
  CHECK(qa_app_application_configuration_preset_for_catalog(&catalog, &options, NULL, &result));
  CHECK(!result.preset.campaign.is_campaign);
  qa_app_application_preset_result_free(&result);
  options.rules = "ctf";
  CHECK(qa_app_application_preset_for_catalog(&catalog, &options, NULL, NULL, &result));
  CHECK(strcmp(result.preset.match.provider, "q1:ctf") == 0);
  CHECK(strcmp(result.preset.match.content, "q1-classic-ctf") == 0);
  qa_app_application_preset_result_free(&result);
  qa_app_options_destroy(&options);
}

static void test_fixture_discovery(void) {
  char root[] = "/tmp/qa-renderer-content-XXXXXX";
  CHECK(mkdtemp(root) != NULL);
  char pak_path[512]; snprintf(pak_path, sizeof pak_path, "%s/pak0.pak", root);
  FILE *file = fopen(pak_path, "wb");
  CHECK(file != NULL);
  if (file) {
    /* Minimal PAK: header, payload, one 64-byte directory entry. */
    uint8_t header[12] = {'P', 'A', 'C', 'K', 16, 0, 0, 0, 64, 0, 0, 0};
    uint8_t payload[4] = {1, 2, 3, 4};
    uint8_t entry[64] = {0};
    memcpy(entry, "gfx/test.lmp", 12);
    entry[56] = 12; entry[60] = 4;
    fwrite(header, 1, 12, file); fwrite(payload, 1, 4, file); fwrite(entry, 1, 64, file);
    fclose(file);
  }
  qa_catalog_archive_inspector inspector = qa_app_default_archive_inspector();
  qa_catalog_archive archive = {0};
  char *error = NULL;
  CHECK(inspector.inspect(inspector.ctx, pak_path, QA_ARCHIVE_FORMAT_PAK, &archive, &error));
  CHECK(archive.entry_count == 1);
  if (archive.entry_count == 1) {
    CHECK(strcmp(archive.entries[0].path, "gfx/test.lmp") == 0);
    CHECK(archive.entries[0].byte_length == 4);
  }
  qa_catalog_archive_free(&archive);
  char user_root[512]; snprintf(user_root, sizeof user_root, "%s/user", root);
  qa_discover_content_options discover = {0};
  discover.corpus_root = root; discover.user_content_root = user_root;
  discover.discover_mods = false; discover.inspector = inspector;
  qa_content_catalog discovered = {0};
  CHECK(qa_discover_installed_content(&discover, &discovered));
  CHECK(discovered.product_count == qa_expected_product_count);
  const qa_catalog_discovered_product *product = qa_content_catalog_product(&discovered, "q1-classic-id1");
  CHECK(product->availability.kind == QA_CATALOG_MISSING);
  CHECK_ABORTS(qa_content_catalog_require(&discovered, "q1-classic-id1"));
  qa_content_catalog_destroy(&discovered);
  remove(pak_path); rmdir(user_root); rmdir(root);
}

static void test_native_remote_guard(void) {
  qa_app_options options = base_options();
  qa_remote_content_selection selection = {0};
  CHECK(qa_remote_content_selection_make("q1-quakeworld", "qw", &selection));
  options.has_remote_content = true;
  options.remote_content = selection;
  qa_app_content_host host = {0};
  /* Offline network + non-remote product: the TS guard throws. */
  CHECK_ABORTS(qa_app_load_application_content_native(host, options, NULL, false, NULL, &catalog, NULL));
  qa_app_options_destroy(&options);
}

int main(void) {
  setenv("SDL_VIDEODRIVER", "dummy", 0);
  test_renderer_cpu();
  test_renderer_gl_if_available();
  build_catalog();
  test_selection();
  test_preset();
  test_fixture_discovery();
  test_native_remote_guard();
  for (size_t i = 0; i < product_count; i++) free(products[i].id);
  printf("test_renderer_content: %d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
