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
/* --- prepared-startup slice cases --- */
static char captured_print[8192];
static void capture_print_source(void *ctx, const char *text, const qa_command_context *source) {
  (void)ctx; (void)source;
  size_t used = strlen(captured_print);
  (void)snprintf(captured_print + used, sizeof captured_print - used, "%s", text);
}
static void capture_print_text(void *ctx, const char *text) { capture_print_source(ctx, text, NULL); }
static char forwarded_name[128], forwarded_arg[128];
static int forwarded_count;
static void capture_forward(void *ctx, const char *name, const char *const *args, size_t arg_count, qa_command_context source) {
  (void)ctx; (void)source;
  forwarded_count++;
  (void)snprintf(forwarded_name, sizeof forwarded_name, "%s", name);
  (void)snprintf(forwarded_arg, sizeof forwarded_arg, "%s", arg_count > 0 ? args[0] : "");
}
static qa_cvar_registry *make_registry(qa_command_dialect dialect, qa_command_context context) {
  qa_cvar_registry_options options = {0};
  options.dialect = dialect;
  options.context = context;
  qa_cvar_registry *registry = qa_cvar_registry_create(&options);
  CHECK(registry != NULL);
  return registry;
}
static char *fake_scoped_read(void *ctx, const char *name, qa_command_context source, qa_app_startup_script_scope scope) {
  (void)ctx; (void)source; (void)scope;
  if (strcmp(name, "quake.rc") == 0) return strdup("exec default.cfg\nexec config.cfg\n");
  if (strcmp(name, "default.cfg") == 0) return strdup("");
  if (strcmp(name, "config.cfg") == 0) return strdup("bind x +attack\n");
  return NULL;
}
static int launch_count, frame_count;
static void count_launch(void *ctx) { (void)ctx; launch_count++; }
static void count_frame(void *ctx) { (void)ctx; frame_count++; }
static qa_cvar_registry *fixed_owner(void *ctx, const char *name, qa_command_context source) {
  (void)name; (void)source;
  return ctx;
}
static void test_prepared_startup(void) {
  qa_identity_owner identity = {0};
  CHECK(qa_identity_owner_create(&identity, "prepared-test"));
  qa_seat_id seat_id = qa_identity_owner_seat(&identity, 0);
  qa_client_id client_id = qa_identity_owner_client(&identity, 0, 0);
  qa_command_context server_context = {0};
  server_context.session = identity.session;
  server_context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  qa_command_context seat_context = {0};
  seat_context.session = identity.session;
  seat_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  seat_context.origin.seat = seat_id;
  seat_context.origin.client = client_id;
  qa_cvar_registry *source = make_registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, server_context);
  qa_cvar_registry *movement = make_registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, server_context);
  qa_cvar_registry *seat_cvars = make_registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, seat_context);
  qa_cvar_registry *mouse_cvars = make_registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, seat_context);
  qa_mouse_settings mouse = {NULL};
  qa_mouse_settings_init(&mouse, mouse_cvars);
  qa_cvar_archive_entry mouse_archive[] = {{(char *)"sensitivity", (char *)"9"}};
  qa_app_prepared_seat_configuration seat_config = {0};
  seat_config.context = seat_context;
  seat_config.id = seat_id;
  seat_config.cvars = seat_cvars;
  seat_config.mouse = mouse;
  seat_config.mouse_archive = mouse_archive;
  seat_config.mouse_archive_count = 1;
  forwarded_count = 0;
  captured_print[0] = '\0';
  qa_app_prepared_startup_options options = {0};
  options.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  options.movement_dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  options.seats = &seat_config;
  options.seat_count = 1;
  options.print = capture_print_source;
  options.forward = capture_forward;
  qa_app_prepared_startup *prepared = qa_app_prepared_startup_create_concrete(source, movement, NULL, &options);
  CHECK(prepared != NULL);
  CHECK(qa_app_prepared_startup_commands(prepared) != NULL);
  /* Same-dialect movement is the fallback (TS constructor). */
  CHECK(qa_app_prepared_startup_fallback(prepared) == movement);
  CHECK(qa_app_prepared_startup_shared_cvars(prepared) == NULL);
  CHECK(qa_app_prepared_startup_console_bindings(prepared) == NULL);
  size_t seat_count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &seat_count);
  CHECK(seat_count == 1 && seats[0].input != NULL && qa_same_seat(seats[0].id, seat_id));
  CHECK(!qa_app_prepared_startup_pending(prepared));
  CHECK(qa_app_prepared_startup_configuration_can_continue(prepared));
  /* Deferred world command forwards through the host closure. */
  qa_command_buffer *commands = qa_app_prepared_startup_commands(prepared);
  qa_command_buffer_append(commands, "map e1m1\n", &seat_context, -1);
  (void)qa_command_buffer_execute(commands);
  CHECK(forwarded_count == 1 && text_eq(forwarded_name, "map") && text_eq(forwarded_arg, "e1m1"));
  /* Execute: quake.rc -> default.cfg (defaults+authored) -> config.cfg
   * (archive + binding collection) over the fake scoped reader. */
  qa_cvar_snapshot registered = {0};
  CHECK(qa_cvar_registry_register(source, "test_source", "0", 0, &registered));
  qa_cvar_read_free(&registered);
  qa_cvar_archive_entry source_archive[] = {{(char *)"test_source", (char *)"5"}};
  launch_count = 0; frame_count = 0;
  qa_app_prepared_execute_options execute = {0};
  execute.read = fake_scoped_read;
  execute.apply_launch_options = count_launch;
  execute.next_frame = count_frame;
  execute.source_archive = source_archive;
  execute.source_archive_count = 1;
  qa_app_prepared_startup_execute(prepared, &execute);
  CHECK(!qa_app_prepared_startup_pending(prepared));
  CHECK(launch_count == 1);
  char *value = qa_cvar_registry_variable_string(source, "test_source");
  CHECK(text_eq(value, "5"));
  free(value);
  value = qa_cvar_registry_variable_string(mouse_cvars, "sensitivity");
  CHECK(text_eq(value, "9"));
  free(value);
  bool all_chosen = false, has_selected = true;
  size_t overridden = 0, authored_count = 0;
  CHECK(qa_app_prepared_startup_seat_choices(prepared, seat_id, &all_chosen, NULL, &overridden,
      &has_selected, NULL, NULL, NULL, &authored_count));
  CHECK(authored_count > 0);
  CHECK(overridden >= 1); /* config.cfg "bind x ..." was collected */
  CHECK(!has_selected);
  /* SeatInput binding publication: preview, bind, reset. */
  qa_physical_input physical = {0};
  CHECK(qa_named_physical_input("b", 0, &physical));
  qa_input_binding extra = {physical, {false, NULL, "say staged"}};
  size_t binding_count = 0;
  qa_input_binding *preview = qa_app_prepared_startup_preview_bindings(prepared, seat_id, &extra, 1, &binding_count);
  CHECK(binding_count > 0);
  free(preview);
  CHECK(qa_seat_input_binding(seats[0].input, &physical) == NULL);
  qa_input_binding *bound = qa_app_prepared_startup_bindings(prepared, seat_id, &extra, 1, &binding_count);
  CHECK(bound != NULL && binding_count > 0);
  free(bound);
  const qa_input_binding_target *target = qa_seat_input_binding(seats[0].input, &physical);
  CHECK(target != NULL && !target->is_action && text_eq(target->text, "say staged"));
  qa_app_prepared_startup_reset_bindings(prepared, seat_id);
  CHECK(qa_app_prepared_startup_seat_choices(prepared, seat_id, &all_chosen, NULL, &overridden,
      NULL, NULL, NULL, NULL, NULL));
  CHECK(all_chosen && overridden == 0);
  /* Retired-seat validation aborts with the TS Error. */
  qa_seat_id retired = qa_identity_owner_seat(&identity, 9);
  CHECK_ABORTS(qa_app_prepared_startup_set_active_seats(prepared, &retired, 1));
  CHECK_ABORTS(qa_app_prepared_startup_reset_bindings(prepared, retired));
  /* PreparedClientCommands staging: edits stay staged until publish. */
  qa_cvar_registry *candidate_cvars = make_registry(QA_COMMAND_DIALECT_Q1_NETQUAKE, server_context);
  qa_command_buffer_options candidate = {0};
  candidate.dialect = QA_COMMAND_DIALECT_Q1_NETQUAKE;
  candidate.context = server_context;
  candidate.cvars = candidate_cvars;
  candidate.print = capture_print_source;
  qa_app_prepared_client_commands *client =
      qa_app_prepared_startup_prepare_client_commands(prepared, &candidate, NULL, 0, false);
  CHECK(client != NULL && qa_app_prepared_client_commands_buffer(client) != NULL);
  qa_binding_command_seat staged = {0};
  CHECK(qa_app_prepared_client_commands_input(client, seat_id, &staged));
  qa_physical_input staged_physical = {0};
  CHECK(qa_named_physical_input("c", 0, &staged_physical));
  qa_input_binding staged_binding = {staged_physical, {false, NULL, "say candidate"}};
  CHECK(staged.bind(staged.ctx, &staged_binding));
  CHECK(qa_seat_input_binding(seats[0].input, &staged_physical) == NULL);
  qa_app_prepared_client_commands_validate_publication(client);
  qa_app_prepared_client_commands_publish(client);
  target = qa_seat_input_binding(seats[0].input, &staged_physical);
  CHECK(target != NULL && text_eq(target->text, "say candidate"));
  qa_app_prepared_client_commands_destroy(client);
  qa_cvar_registry_destroy(candidate_cvars);
  /* A candidate over a live registry is rejected (TS isolation Error). */
  qa_command_buffer_options live_candidate = candidate;
  live_candidate.cvars = movement;
  CHECK_ABORTS((void)qa_app_prepared_startup_prepare_client_commands(prepared, &live_candidate, NULL, 0, false));
  /* allowSeatConfigurationCommand over a fixed-owner routing view. */
  qa_command_cvar_routing routing = {source, fixed_owner, NULL};
  qa_command_origin seat_origin = {0};
  seat_origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  seat_origin.seat = seat_id;
  seat_origin.client = client_id;
  qa_command_origin script_origin = {0};
  script_origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  script_origin.name = "config.cfg";
  script_origin.caller = &seat_origin;
  char *set_argv[] = {(char *)"set", (char *)"sensitivity", (char *)"3"};
  qa_command_invocation invocation = {0};
  invocation.source.session = identity.session;
  invocation.source.origin = script_origin;
  invocation.argv = set_argv;
  invocation.argc = 3;
  qa_app_prepared_seat allow_seats[] = {{seat_context, seat_id, seat_cvars, mouse, seats[0].input, false, false}};
  captured_print[0] = '\0';
  CHECK(!qa_app_allow_seat_configuration_command_for(&invocation, routing, allow_seats, 1, capture_print_source, NULL));
  CHECK(strstr(captured_print, "Ignoring shared cvar sensitivity in saved secondary-seat configuration") != NULL);
  routing.ctx = mouse_cvars;
  CHECK(qa_app_allow_seat_configuration_command_for(&invocation, routing, allow_seats, 1, capture_print_source, NULL));
  char *restart_argv[] = {(char *)"cvar_restart"};
  invocation.argv = restart_argv;
  invocation.argc = 1;
  captured_print[0] = '\0';
  CHECK(!qa_app_allow_seat_configuration_command_for(&invocation, routing, allow_seats, 1, capture_print_source, NULL));
  CHECK(strstr(captured_print, "Ignoring shared cvar restart") != NULL);
  script_origin.name = "autoexec.cfg";
  invocation.source.origin = script_origin;
  invocation.argv = set_argv;
  invocation.argc = 3;
  CHECK(qa_app_allow_seat_configuration_command_for(&invocation, routing, allow_seats, 1, capture_print_source, NULL));
  qa_app_prepared_startup_close(prepared);
  /* Dedicated form: console bindings exist; a created fallback takes
   * the console dialect; readConfiguration without a reader aborts. */
  qa_cvar_registry *dedicated_source = make_registry(QA_COMMAND_DIALECT_Q2_CLASSIC, server_context);
  qa_cvar_registry *q3_movement = make_registry(QA_COMMAND_DIALECT_Q3, server_context);
  qa_app_prepared_startup_options dedicated_options = {0};
  dedicated_options.dialect = QA_COMMAND_DIALECT_Q2_CLASSIC;
  dedicated_options.movement_dialect = QA_COMMAND_DIALECT_Q3;
  dedicated_options.print = capture_print_source;
  dedicated_options.forward = capture_forward;
  qa_app_prepared_startup *dedicated =
      qa_app_prepared_startup_create_concrete(dedicated_source, q3_movement, NULL, &dedicated_options);
  CHECK(qa_app_prepared_startup_console_bindings(dedicated) != NULL);
  CHECK(qa_app_prepared_startup_fallback(dedicated) != q3_movement);
  CHECK(qa_cvar_registry_dialect(qa_app_prepared_startup_fallback(dedicated)) == QA_COMMAND_DIALECT_Q2_CLASSIC);
  CHECK_ABORTS((void)qa_app_prepared_startup_read_configuration(dedicated, "x.cfg", server_context, "user"));
  qa_app_prepared_startup_close(dedicated);
  qa_cvar_registry_destroy(dedicated_source);
  qa_cvar_registry_destroy(q3_movement);
  qa_cvar_registry_destroy(mouse_cvars);
  qa_cvar_registry_destroy(seat_cvars);
  qa_cvar_registry_destroy(movement);
  qa_cvar_registry_destroy(source);
  qa_identity_owner_destroy(&identity);
}
static bool fake_mounted_open(void *ctx, const char *name, uint8_t **out_bytes, size_t *out_count) {
  (void)ctx;
  const char *text = NULL;
  if (strcmp(name, "quake.rc") == 0) text = "exec default.cfg\nexec config.cfg\n";
  else if (strcmp(name, "default.cfg") == 0 || strcmp(name, "config.cfg") == 0) text = "";
  if (text == NULL) return false;
  *out_count = strlen(text);
  *out_bytes = (uint8_t *)strdup(text);
  return *out_bytes != NULL;
}
static void fake_register_shared(void *ctx, qa_cvar_registry *cvars, bool has_format, qa_audio_output_format format) {
  (void)ctx; (void)has_format; (void)format;
  qa_cvar_snapshot volume = {0};
  if (qa_cvar_registry_register(cvars, "volume", "1", 0, &volume)) qa_cvar_read_free(&volume);
}
static bool fake_load_text(void *ctx, const char *name, char **out_text) {
  (void)ctx; (void)name;
  *out_text = NULL;
  return false;
}
static const char *fake_dump_text(void *ctx, const char *name, const char *text) {
  (void)ctx; (void)name; (void)text;
  return NULL;
}
static int on_prepared_count;
static void count_on_prepared(void *ctx, qa_app_prepared_startup *prepared) {
  (void)ctx; (void)prepared;
  on_prepared_count++;
}
static void test_prepare_initial(void) {
  qa_app_content_host host = {0};
  host.product = fake_product;
  host.require_product = fake_require;
  host.open_configuration = fake_open_configuration;
  host.preset = fake_preset;
  qa_app_options open_options = {0};
  open_options.product = "q1-classic-id1";
  qa_app_configuration_content *content = qa_app_open_initial_configuration_content_for_catalog(
      host, &catalog, &open_options, NULL, false, NULL);
  CHECK(content != NULL);
  qa_app_application_configuration_selection selection = {0};
  selection.source.content = "q1-classic-id1";
  selection.source.provider = "q1:official";
  selection.engine_behavior.content = "q1-classic-id1";
  selection.engine_behavior.provider = "q1:official";
  selection.movement.provider = "q1:official";
  selection.timing.provider = "q1:official";
  selection.timing.clock.kind = QA_CLOCK_Q1_NETQUAKE;
  qa_app_configuration_content_set_selection(content, selection);
  qa_identity_owner identity = {0};
  CHECK(qa_identity_owner_create(&identity, "initial-test"));
  qa_session_mode mode = {QA_SESSION_MODE_LOCAL};
  qa_engine_session *session = qa_engine_session_create(&identity, mode);
  CHECK(session != NULL);
  char root_template[] = "/tmp/qa-initial-XXXXXX";
  char *root = mkdtemp(root_template);
  CHECK(root != NULL);
  qa_config_store settings;
  CHECK(qa_config_store_init(&settings, root));
  qa_app_console_script_files_options script_options = {0};
  script_options.console_root = root;
  script_options.settings = &settings;
  script_options.has_mounted_script = true;
  script_options.mounted_script.ctx = NULL;
  script_options.mounted_script.open = fake_mounted_open;
  qa_app_console_script_files *scripts = qa_app_console_script_files_create(script_options);
  CHECK(scripts != NULL);
  qa_app_options options = {0};
  options.product = "q1-classic-id1";
  options.seats = 1;
  options.character = QA_GAME_FAMILY_Q1;
  options.character_model = (char *)"male";
  options.user_content_root = root;
  options.mode = "singleplayer";
  qa_app_prepare_initial_args args = {0};
  args.catalog = &catalog;
  args.content = content;
  args.session = session;
  args.identity = &identity;
  args.settings = &settings;
  args.scripts = scripts;
  args.image_host.register_shared = fake_register_shared;
  args.image_host.load_text = fake_load_text;
  args.image_host.dump_text = fake_dump_text;
  args.print = capture_print_text;
  args.default_capacity = 4;
  args.on_prepared = count_on_prepared;
  on_prepared_count = 0;
  captured_print[0] = '\0';
  qa_app_prepared_initial *initial = qa_app_prepare_initial_configuration(&options, &args);
  CHECK(initial != NULL);
  CHECK(on_prepared_count == 1);
  qa_app_prepared_startup *prepared = qa_app_prepared_initial_startup(initial);
  CHECK(prepared != NULL && !qa_app_prepared_startup_pending(prepared));
  size_t seat_count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(prepared, &seat_count);
  CHECK(seat_count == 1 && seats[0].input != NULL);
  /* The EngineSession seat created for the local client is in the map. */
  qa_client_id seat_client = seats[0].context.origin.client;
  CHECK(qa_app_prepared_initial_local_seat(initial, seat_client) != NULL);
  CHECK(qa_app_prepared_initial_image(initial) != NULL);
  CHECK(qa_app_prepared_startup_shared_cvars(prepared) == qa_app_image_settings_cvars(qa_app_prepared_initial_image(initial)));
  CHECK(qa_app_prepared_initial_max_clients(initial) > 0);
  CHECK(qa_app_prepared_initial_scripts(initial) == scripts);
  /* A forwarded world command becomes an application request row. */
  size_t request_count = 99;
  (void)qa_app_prepared_initial_requests(initial, &request_count);
  CHECK(request_count == 0);
  qa_command_buffer *commands = qa_app_prepared_startup_commands(prepared);
  qa_command_context seat_context = seats[0].context;
  qa_command_buffer_append(commands, "connect example.test\n", &seat_context, -1);
  (void)qa_command_buffer_execute(commands);
  const qa_app_configuration_command_request *requests = qa_app_prepared_initial_requests(initial, &request_count);
  CHECK(request_count == 1 && text_eq(requests[0].name, "connect") && requests[0].has_seat
      && requests[0].argument_count == 1 && text_eq(requests[0].arguments[0], "example.test"));
  qa_app_prepared_initial_destroy(initial);
  qa_engine_session_destroy(session);
  qa_app_console_script_files_destroy(scripts);
  qa_app_configuration_content_destroy(content);
  qa_config_store_destroy(&settings);
  qa_identity_owner_destroy(&identity);
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
  test_prepared_startup();
  test_prepare_initial();
  qa_content_catalog_destroy(&catalog);
  printf("%d checks, %d failures\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
