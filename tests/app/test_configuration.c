/* Port of key cases from tests/app/configuration-content.test.ts for
 * the comp-configuration slice: configurationDialect /
 * configurationMovementDialect / configurationStore /
 * legacyConfigurationOptions / openInitialConfigurationContent over a
 * fake catalog, seat archive owners, request seat unwinding, and
 * client-bootstrap localClientAccount + lifetime forwarding.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "qa/app/bootstrap/client_bootstrap.h"
#include "qa/app/bootstrap/configuration.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
static int checks, failures;
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
static bool text_eq(const char *a, const char *b) {
  return a != NULL && b != NULL && strcmp(a, b) == 0;
}
/* Fake catalog over borrowed products (qa_content_catalog_init). */
static qa_catalog_directory_content q2_user = {"/tmp/qa-cfg-q2-user", NULL, 0};
static qa_catalog_discovered_product products[] = {
  {"q1-classic-id1", {"q1-classic-id1", QA_GAME_FAMILY_Q1, "classic", "id1", "Q1", "id1", NULL, NULL, 0, NULL, 0, NULL, NULL},
   {QA_CATALOG_INSTALLED, NULL, 0, NULL}, NULL, 0, "/corpus/id1", NULL, NULL, NULL, 0, NULL, 0},
  {"q1-quakeworld", {"q1-quakeworld", QA_GAME_FAMILY_Q1, "quakeworld", "qw", "QW", "qw", NULL, NULL, 0, NULL, 0, NULL, NULL},
   {QA_CATALOG_INSTALLED, NULL, 0, NULL}, NULL, 0, NULL, NULL, NULL, NULL, 0, NULL, 0},
  {"q2-classic-baseq2", {"q2-classic-baseq2", QA_GAME_FAMILY_Q2, "classic", "baseq2", "Q2", "baseq2", NULL, NULL, 0, NULL, 0, NULL, NULL},
   {QA_CATALOG_INSTALLED, NULL, 0, NULL}, NULL, 0, "/corpus/baseq2", &q2_user, NULL, NULL, 0, NULL, 0},
  {"q3-baseq3", {"q3-baseq3", QA_GAME_FAMILY_Q3, "retail", "baseq3", "Q3", "q3a/baseq3", NULL, NULL, 0, NULL, 0, NULL, NULL},
   {QA_CATALOG_INSTALLED, NULL, 0, NULL}, NULL, 0, "/corpus/q3a/baseq3", NULL, NULL, NULL, 0, NULL, 0},
  {"q3-testmod", {"q3-testmod", QA_GAME_FAMILY_Q3, "retail", "testmod", "Q3M", "q3a/testmod", "q3-baseq3", NULL, 0, NULL, 0, NULL, NULL},
   {QA_CATALOG_INSTALLED, NULL, 0, NULL}, NULL, 0, NULL, NULL, NULL, NULL, 0, NULL, 0},
};
static qa_content_catalog catalog;
static void test_dialects(void) {
  CHECK(qa_app_configuration_dialect_for(QA_GAME_FAMILY_Q1, "classic") == QA_COMMAND_DIALECT_Q1_NETQUAKE);
  CHECK(qa_app_configuration_dialect_for(QA_GAME_FAMILY_Q1, "quakeworld") == QA_COMMAND_DIALECT_Q1_QUAKEWORLD);
  CHECK(qa_app_configuration_dialect_for(QA_GAME_FAMILY_Q2, "classic") == QA_COMMAND_DIALECT_Q2_CLASSIC);
  CHECK(qa_app_configuration_dialect_for(QA_GAME_FAMILY_Q2, "rerelease") == QA_COMMAND_DIALECT_Q2_RERELEASE);
  CHECK(qa_app_configuration_dialect_for(QA_GAME_FAMILY_Q3, "retail") == QA_COMMAND_DIALECT_Q3);
  CHECK(text_eq(qa_app_configuration_dialect_name(QA_COMMAND_DIALECT_Q1_NETQUAKE), "q1-netquake"));
  CHECK(text_eq(qa_app_configuration_dialect_name(QA_COMMAND_DIALECT_Q3), "q3"));
  CHECK(qa_app_configuration_dialect_for_product(&products[1]) == QA_COMMAND_DIALECT_Q1_QUAKEWORLD);
  /* Movement dialect follows the timing clock, not the product. */
  qa_provider_timing timing[2] = {0};
  timing[0].provider = "q2:official"; timing[0].clock.kind = QA_CLOCK_Q2_CLASSIC;
  timing[1].provider = "q1:official"; timing[1].clock.kind = QA_CLOCK_Q1_NETQUAKE;
  CHECK(qa_app_configuration_movement_dialect_for(timing, 2, "q1:official") == QA_COMMAND_DIALECT_Q1_NETQUAKE);
  CHECK(qa_app_configuration_movement_dialect_for(timing, 2, "q2:official") == QA_COMMAND_DIALECT_Q2_CLASSIC);
  CHECK_ABORTS((void)qa_app_configuration_movement_dialect_for(timing, 2, "q3:official"));
  qa_app_application_configuration_selection selection = {0};
  selection.movement.provider = "q1:official";
  selection.timing = timing[1];
  CHECK(qa_app_configuration_movement_dialect(&selection) == QA_COMMAND_DIALECT_Q1_NETQUAKE);
}
static void test_store_and_legacy(void) {
  qa_app_options options = {0};
  options.user_content_root = "/tmp/qa-cfg-user";
  qa_config_store store;
  /* Product with userContent root wins (TS configurationStore). */
  CHECK(qa_app_configuration_store_for_catalog(&catalog, &options, "q2-classic-baseq2", &store));
  CHECK(text_eq(store.root, "/tmp/qa-cfg-q2-user"));
  qa_config_store_destroy(&store);
  /* Otherwise userProductDirectory(userContentRoot, contentDirectory). */
  char *expected = qa_user_product_directory("/tmp/qa-cfg-user", "id1");
  CHECK(qa_app_configuration_store_for_catalog(&catalog, &options, "q1-classic-id1", &store));
  CHECK(text_eq(store.root, expected));
  free(expected);
  qa_config_store_destroy(&store);
  /* Legacy: q1 non-quakeworld only; selected first, deduped. */
  qa_app_legacy_console_config_sources legacy;
  const char *roots[8] = {0};
  char *owned[9] = {0};
  CHECK(qa_app_legacy_configuration_options_for_catalog(&catalog, &options, "q1-classic-id1", &legacy, roots, 8, owned));
  CHECK(text_eq(legacy.shared_root, "/tmp/qa-cfg-user"));
  CHECK(legacy.game_root_count == 1);
  for (size_t i = 0; i < 9; i++) free(owned[i]);
  CHECK(!qa_app_legacy_configuration_options_for_catalog(&catalog, &options, "q1-quakeworld", &legacy, roots, 8, owned));
  CHECK(!qa_app_legacy_configuration_options_for_catalog(&catalog, &options, "q2-classic-baseq2", &legacy, roots, 8, owned));
  /* Script roots + hasMod from baseProduct. */
  qa_app_application_configuration_selection selection = {0};
  selection.engine_behavior.content = "q3-testmod";
  qa_app_configuration_script_roots script = qa_app_configuration_script_roots_for(&catalog, &selection);
  CHECK(script.has_mod);
  CHECK(script.base_root_count == 1 && text_eq(script.base_roots[0], "/corpus/q3a/baseq3"));
  CHECK(script.game_root_count == 0);
  selection.engine_behavior.content = "q2-classic-baseq2";
  script = qa_app_configuration_script_roots_for(&catalog, &selection);
  CHECK(!script.has_mod);
  CHECK(script.game_root_count == 2 && text_eq(script.game_roots[0], "/tmp/qa-cfg-q2-user"));
}
static void test_archives_and_profile(void) {
  char root_template[] = "/tmp/qa-cfg-store-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  qa_config_store store;
  CHECK(qa_config_store_init(&store, root));
  qa_cvar_archive_entry entries[] = {{"sensitivity", "9"}};
  qa_cvar_registry_options registry_options = {0};
  registry_options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  registry_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_cvar_registry *registry = qa_cvar_registry_create(&registry_options);
  CHECK(registry != NULL);
  const char *client_owner[] = {"client", "q1-classic-id1", "q1:official", "0"};
  qa_app_save_cvar_archive(&store, client_owner, 4, registry, entries, 1);
  const char *input_owner[] = {"input", "q1-netquake", "0"};
  qa_app_save_cvar_archive(&store, input_owner, 3, registry, entries, 1);
  qa_cvar_registry_destroy(registry);
  size_t count = 99;
  qa_cvar_archive_entry *loaded = qa_app_configuration_load_client_archive(
      &store, "q1-classic-id1", "q1:official", 0, QA_COMMAND_DIALECT_Q1_NETQUAKE, &count);
  CHECK(count == 1 && loaded && text_eq(loaded[0].name, "sensitivity") && text_eq(loaded[0].value, "9"));
  qa_app_cvar_archive_entries_free(loaded, count);
  loaded = qa_app_configuration_load_input_archive(&store, 0, QA_COMMAND_DIALECT_Q1_NETQUAKE, &count);
  CHECK(count == 1 && loaded && text_eq(loaded[0].value, "9"));
  qa_app_cvar_archive_entries_free(loaded, count);
  /* Wrong seat index / movement owner misses. */
  loaded = qa_app_configuration_load_client_archive(&store, "q1-classic-id1", "q1:official", 1,
                                                    QA_COMMAND_DIALECT_Q1_NETQUAKE, &count);
  CHECK(count == 0);
  qa_app_cvar_archive_entries_free(loaded, count);
  loaded = qa_app_configuration_load_movement_archive(&store, QA_COMMAND_DIALECT_Q3, &count);
  CHECK(count == 0);
  qa_app_cvar_archive_entries_free(loaded, count);
  loaded = qa_app_configuration_load_fallback_archive(&store, QA_COMMAND_DIALECT_Q3, &count);
  CHECK(count == 0);
  qa_app_cvar_archive_entries_free(loaded, count);
  char *profile = qa_app_configuration_seat_profile_name(0);
  CHECK(text_eq(profile, "input/seat-1.json"));
  free(profile);
  profile = qa_app_configuration_seat_profile_name(3);
  CHECK(text_eq(profile, "input/seat-4.json"));
  free(profile);
  qa_config_store_destroy(&store);
}
static void test_make_request(void) {
  qa_command_context source = {0};
  source.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  const char *args[] = {"e1m1"};
  qa_app_configuration_command_request request = qa_app_configuration_make_request("map", args, 1, source);
  CHECK(text_eq(request.target, "application") && text_eq(request.name, "map"));
  CHECK(!request.has_seat && request.argument_count == 1);
  /* Script origin unwinds to the local-seat caller. */
  qa_command_origin seat_origin = {0};
  seat_origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  seat_origin.seat.index = 2;
  qa_command_origin script_origin = {0};
  script_origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  script_origin.name = "autoexec.cfg";
  script_origin.caller = &seat_origin;
  source.origin = script_origin;
  request = qa_app_configuration_make_request("connect", NULL, 0, source);
  CHECK(request.has_seat && request.seat.index == 2);
  size_t routed = 0;
  const char *const *names = qa_app_configuration_routed_commands(&routed);
  CHECK(routed > 20 && text_eq(names[0], "save"));
}
/* Fake content host for openInitialConfigurationContent. */
static int preset_calls, open_calls;
static bool preset_was_configuration;
static bool fake_product(void *ctx, const char *id, qa_game_family *family, const char **edition,
                         const char **campaign, bool *installed, bool *builtin) {
  (void)ctx; (void)id;
  *family = QA_GAME_FAMILY_Q1; *edition = "classic"; *campaign = "id1";
  *installed = true; *builtin = true;
  return true;
}
static bool fake_require(void *ctx, const char *id) { (void)ctx; (void)id; return true; }
static const char *fake_open_configuration(void *ctx, qa_app_configuration_content *content) {
  (void)ctx;
  open_calls++;
  qa_app_application_configuration_selection selection = {0};
  selection.engine_behavior.content = "q2-classic-baseq2";
  qa_app_configuration_content_set_selection(content, selection);
  return NULL;
}
static const char *fake_preset(void *ctx, const qa_app_options *options, bool configuration, qa_launch_preset *out) {
  (void)ctx; (void)options;
  preset_calls++;
  preset_was_configuration = configuration;
  memset(out, 0, sizeof *out);
  out->id = "preset:test";
  return NULL;
}
static void test_open_initial(void) {
  qa_app_content_host host = {0};
  host.product = fake_product;
  host.require_product = fake_require;
  host.open_configuration = fake_open_configuration;
  host.preset = fake_preset;
  qa_app_options options = {0};
  options.product = "q1-classic-id1";
  preset_calls = 0; open_calls = 0;
  qa_app_configuration_content *content = qa_app_open_initial_configuration_content_for_catalog(
      host, &catalog, &options, NULL, false, NULL);
  CHECK(content != NULL);
  CHECK(preset_calls == 1 && preset_was_configuration && open_calls == 1);
  CHECK(qa_app_configuration_content_catalog(content) == (const void *)&catalog);
  /* Dialect of the opened content follows its engine behavior. */
  CHECK(qa_app_configuration_dialect_in_catalog(&catalog, content) == QA_COMMAND_DIALECT_Q2_CLASSIC);
  qa_app_configuration_content_destroy(content);
  /* Recipe path skips the preset and still records the catalog. */
  qa_executable_recipe recipe = {0};
  recipe.map.entities.content = "q1-classic-id1";
  preset_calls = 0; open_calls = 0;
  content = qa_app_open_initial_configuration_content_for_catalog(host, &catalog, &options, &recipe, true, NULL);
  CHECK(content != NULL && preset_calls == 0 && open_calls == 1);
  qa_app_configuration_content_destroy(content);
  /* Q3 product already on options is carried onto the content. */
  qa_app_options q3_options = {0};
  q3_options.product = "q3-baseq3";
  q3_options.has_q3_product = true;
  content = qa_app_open_initial_configuration_content_for_catalog(host, &catalog, &q3_options, NULL, false, NULL);
  const void *q3_product = NULL;
  CHECK(content != NULL && qa_app_configuration_content_q3_product(content, &q3_product));
  CHECK(q3_product == (const void *)&q3_options.q3_product);
  qa_app_configuration_content_destroy(content);
}
static void fixed_random(void *ctx, unsigned char out[16]) {
  (void)ctx;
  for (int i = 0; i < 16; i++) out[i] = (unsigned char)i;
}
static int lifetime_order[8], lifetime_len;
static const char *lt_prepare_retirement(void *ctx) { (void)ctx; lifetime_order[lifetime_len++] = 1; return NULL; }
static void lt_release(void *ctx) { (void)ctx; lifetime_order[lifetime_len++] = 2; }
static const char *lt_retire(void *ctx) { (void)ctx; lifetime_order[lifetime_len++] = 3; return "retire-error"; }
static void test_client_bootstrap(void) {
  CHECK(text_eq(qa_app_client_source_publication_error, "Client source failed after publication began"));
  qa_app_local_accounts *accounts = qa_app_local_accounts_create(fixed_random, NULL);
  CHECK(accounts != NULL);
  qa_identity_owner identity = {0};
  CHECK(qa_identity_owner_create(&identity, "cfg-test"));
  const qa_app_local_account *account = qa_app_local_client_account(accounts, &identity);
  CHECK(text_eq(account->id, "account:000102030405060708090a0b0c0d0e0f"));
  CHECK(text_eq(account->name, "cfg-test"));
  /* Stable per identity (TS WeakMap hit). */
  CHECK(qa_app_local_client_account(accounts, &identity) == account);
  qa_app_local_accounts_destroy(accounts);
  qa_identity_owner_destroy(&identity);
  /* OS-random registry produces a well-formed id. */
  accounts = qa_app_local_accounts_create_os_random();
  CHECK(qa_identity_owner_create(&identity, "os-rand"));
  account = qa_app_local_client_account(accounts, &identity);
  CHECK(account && strncmp(account->id, "account:", 8) == 0 && strlen(account->id) == 40);
  qa_app_local_accounts_destroy(accounts);
  qa_identity_owner_destroy(&identity);
  /* Lifetime forwarding: NULL lifetime / NULL fns are no-ops. */
  qa_command_context context = {0};
  qa_app_client_recording_feed feed = {0};
  const void *prepared = (const void *)1;
  CHECK(qa_app_client_source_prepare_recording(NULL, context, &feed) == NULL);
  CHECK(qa_app_client_source_prepare_video_restart(NULL, &prepared) == NULL && prepared == NULL);
  CHECK(qa_app_client_source_prepare_retirement(NULL) == NULL);
  qa_app_client_source_release_settings(NULL);
  CHECK(qa_app_client_source_retire(NULL) == NULL);
  CHECK(text_eq(qa_app_client_source_capture_map(NULL), "menu"));
  qa_app_client_source_lifetime lifetime = {0};
  lifetime.prepare_retirement = lt_prepare_retirement;
  lifetime.release_settings = lt_release;
  lifetime.retire = lt_retire;
  lifetime.capture_map = "e1m1";
  lifetime_len = 0;
  CHECK(qa_app_client_source_prepare_retirement(&lifetime) == NULL);
  qa_app_client_source_release_settings(&lifetime);
  CHECK(text_eq(qa_app_client_source_retire(&lifetime), "retire-error"));
  CHECK(lifetime_len == 3 && lifetime_order[0] == 1 && lifetime_order[1] == 2 && lifetime_order[2] == 3);
  CHECK(text_eq(qa_app_client_source_capture_map(&lifetime), "e1m1"));
}
int main(void) {
  qa_content_catalog_init(&catalog, "/corpus", products, sizeof products / sizeof products[0],
                          NULL, 0, 1, "/tmp/qa-cfg-user", NULL, 0);
  test_dialects();
  test_store_and_legacy();
  test_archives_and_profile();
  test_make_request();
  test_open_initial();
  test_client_bootstrap();
  qa_content_catalog_destroy(&catalog);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
