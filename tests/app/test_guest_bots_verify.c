/* Guest bots over the retail baseq3 qagame.qvm on q3dm1, compared with
 * tests/app/oracles/guest_bots_verify.ts (bun): the engine botlib attached the
 * way Application.prepareGuestBots attaches it (application bot assets,
 * ApplicationBotNavigation, Q3QvmServerGame attach_bots over Q3GuestBots),
 * bot_enable 1, one human player, "addbot sarge 1", then a deterministic run.
 * snapshotEntities is the empty list on both sides; clients come from the
 * identity owner. Every callback checks its sentinel ctx.
 *   build/.../test-app/test_guest_bots_verify [frames...]   prints the dump
 *   build/.../test-app/test_guest_bots_verify               compares with
 *     tests/app/oracles/guest_bots_verify.txt when it exists
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/simulation/bot_assets.h"
#include "qa/app/bootstrap/simulation/bots.h"
#include "qa/app/bootstrap/simulation/navigation.h"
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/bots/behavior/assets.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/platform/files/writable.h"
#include "qa/world/actors/body.h"
#include "../../src/app/bootstrap/simulation/shared_simulation_internal.h"

typedef struct lines { char **items; size_t count, capacity; } lines;
static void lines_push(lines *list, const char *text) {
  if (list->count == list->capacity) {
    list->capacity = list->capacity ? list->capacity * 2 : 256;
    list->items = realloc(list->items, list->capacity * sizeof *list->items);
    if (!list->items) abort();
  }
  list->items[list->count] = strdup(text);
  if (!list->items[list->count]) abort();
  list->count++;
}
static void lines_free(lines *list) {
  for (size_t i = 0; i < list->count; i++) free(list->items[i]);
  free(list->items);
  memset(list, 0, sizeof *list);
}
static lines out, console_lines;
static void emit(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void emit(const char *format, ...) {
  char line[16384];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof line, format, args);
  va_end(args);
  lines_push(&out, line);
}
#define N(value) QA_JS_NUMBER(value)
static char *vec(qa_vec3 v, char *buffer, size_t size) {
  snprintf(buffer, size, "%s %s %s", N(v.x), N(v.y), N(v.z));
  return buffer;
}
#define V(value) vec((value), (char[128]){0}, 128)

/* ---- sentinels: each callback checks the context its author passed ---- */
typedef struct guest_state { double clock_milliseconds; } guest_state;
static guest_state guest;
static int bot_sentinel, files_sentinel;
#define REQUIRE(ctx, expected) do { if ((ctx) != (void *)(expected)) { fprintf(stderr, "wrong ctx in %s\n", __func__); abort(); } } while (0)

static void guest_print(void *ctx, const char *text) { REQUIRE(ctx, &guest); lines_push(&console_lines, text); }
static double guest_milliseconds(void *ctx) { REQUIRE(ctx, &guest); return guest.clock_milliseconds; }
static double guest_real_time(void *ctx, bool has_output, qa_qvm_calendar *out_calendar) {
  REQUIRE(ctx, &guest);
  if (has_output) *out_calendar = (qa_qvm_calendar){0, 0, 0, 1, 0, 124, 1, 0, 0};
  return 1704067200;
}
static void command_line(const char *prefix, const char *text) {
  char line[4096];
  snprintf(line, sizeof line, "[%s] %s", prefix, text != NULL ? text : "null");
  lines_push(&console_lines, line);
}
static void guest_execute_now(void *ctx, const char *text) { REQUIRE(ctx, &guest); command_line("now", text); }
static void guest_append(void *ctx, const char *text) { REQUIRE(ctx, &guest); command_line("append", text); }
static void guest_insert(void *ctx, const char *text) { REQUIRE(ctx, &guest); command_line("insert", text); }
static void guest_configstring(void *ctx, double index, const char *value) {
  REQUIRE(ctx, &guest);
  char line[8192];
  snprintf(line, sizeof line, "[configstring] %s %s", N(index), value);
  lines_push(&console_lines, line);
}
static void guest_server_command(void *ctx, double slot, const char *text) {
  REQUIRE(ctx, &guest);
  char line[8192];
  snprintf(line, sizeof line, "[server-command] %s %s", N(slot), text);
  lines_push(&console_lines, line);
}
static void guest_drop(void *ctx, double slot, const char *reason) {
  REQUIRE(ctx, &guest);
  char line[4096];
  snprintf(line, sizeof line, "[drop] %s %s", N(slot), reason);
  lines_push(&console_lines, line);
}
static void next_frame(void *ctx) { REQUIRE(ctx, &guest); }
static bool player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  (void)client;
  REQUIRE(ctx, &out);
  *out_seat = 0;
  *out_social_id = "";
  return true;
}
static bool prompt_supported(void *ctx, qa_client_id client) {
  (void)client;
  REQUIRE(ctx, &console_lines);
  return false;
}

/* ---- the LoadedApplicationContent views (application_bots.c's, over this content) ---- */
static qa_content_mounted *content_for(qa_app_loaded_content *content, qa_content_id id) {
  return (qa_content_mounted *)qa_app_loaded_content_for_content(content, id);
}
static bool asset_mounted(void *ctx, qa_content_id id, qa_bot_asset_files *files) {
  qa_app_loaded_content *content = ctx;
  return qa_load_mounted_bot_asset_files(files, content_for(content, id), qa_app_loaded_content_catalog(content), NULL, 0);
}
static qa_content_id asset_installed(void *ctx, const char *expectation_id) {
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(ctx);
  for (size_t i = 0; i < catalog->product_count; i++)
    if (strcmp(catalog->products[i].expectation.id, expectation_id) == 0 && catalog->products[i].availability.kind == QA_CATALOG_INSTALLED)
      return catalog->products[i].id;
  return NULL;
}
static qa_content_id asset_product(void *ctx, const char *id) { return qa_content_catalog_product(qa_app_loaded_content_catalog(ctx), id)->id; }
static uint8_t *asset_open(void *ctx, qa_content_id id, const char *path, size_t *out_length) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(content_for(ctx, id), path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  return opened.bytes;
}
static qa_content_mounted *navigation_for_content(void *ctx, qa_content_id id) { return content_for(ctx, id); }

/* ---- Q3GuestBotPreparation ---- */
typedef struct bot_context {
  qa_app_shared_simulation *sim;
  qa_identity_owner *identity;
  qa_app_sim_application_bot_navigation *navigation;
} bot_context;
static bot_context bots;
static int64_t bot_random(void *ctx) { REQUIRE(ctx, &bots); return (int64_t)qa_app_sim_source_random_next_integer(bots.sim->random); }
static double bot_milliseconds(void *ctx) { REQUIRE(ctx, &bot_sentinel); return trunc(qa_app_shared_sim_time_seconds(bots.sim) * 1000); }
static void bot_print(void *ctx, int severity, const char *text) { (void)severity; REQUIRE(ctx, &bot_sentinel); lines_push(&console_lines, text); }
static qa_bot_log_open_result bot_open_log(void *ctx, const char *filename) {
  REQUIRE(ctx, &bot_sentinel);
  return qa_app_sim_open_application_bot_log(NULL, filename);
}
static qa_bot_log_open_result bot_resume_log(void *ctx, const char *filename, int64_t position) {
  REQUIRE(ctx, &bot_sentinel);
  return qa_app_sim_resume_application_bot_log(NULL, filename, (double)position);
}
static bool bot_create_client(void *ctx, double slot, qa_client_id *out_client) {
  REQUIRE(ctx, &bots);
  *out_client = qa_identity_owner_client(bots.identity, (int64_t)slot, 0);
  return true;
}
static void bot_free_client(void *ctx, qa_client_id client) { (void)client; REQUIRE(ctx, &bots); }
static double bot_snapshot_entity(void *ctx, qa_app_q3_player player, double sequence) {
  (void)player;
  (void)sequence;
  REQUIRE(ctx, &bots);
  return -1;
}
static qa_save_value *navigation_checkpoint(void *ctx) {
  REQUIRE(ctx, bots.navigation);
  qa_app_sim_bot_navigation_checkpoint checkpoint;
  if (!qa_app_sim_application_bot_navigation_checkpoint(ctx, &checkpoint)) abort();
  qa_save_value *value = qa_app_sim_bot_navigation_checkpoint_value(&checkpoint);
  qa_app_sim_bot_navigation_checkpoint_destroy(&checkpoint);
  return value;
}
static void navigation_restore(void *ctx, const qa_save_value *value) {
  REQUIRE(ctx, bots.navigation);
  qa_app_sim_application_bot_navigation_restore_checkpoint(ctx, value, NULL, NULL);
}
/* The files view keeps its own context; provenance and reads go through it. */
static const uint8_t *files_read(void *ctx, const char *path, size_t *out_length);
static const char *const *files_list(void *ctx, const char *directory, const char *extension, size_t *out_count);
static const char *files_provenance(void *ctx);
static qa_source_bot_files inner_files;
static const uint8_t *files_read(void *ctx, const char *path, size_t *out_length) {
  REQUIRE(ctx, &files_sentinel);
  return inner_files.files.read(inner_files.files.ctx, path, out_length);
}
static const char *const *files_list(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  REQUIRE(ctx, &files_sentinel);
  return inner_files.files.list(inner_files.files.ctx, directory, extension, out_count);
}
static const char *files_provenance(void *ctx) {
  REQUIRE(ctx, &files_sentinel);
  return inner_files.provenance == NULL ? NULL : inner_files.provenance(inner_files.files.ctx);
}

static qa_q3_base_user_command wire(double frame, double clock) {
  qa_q3_base_user_command command;
  memset(&command, 0, sizeof command);
  double forward = fmod(frame, 40) < 30 ? 127 : -127, side = fmod(frame, 16) < 8 ? 64 : -64, yaw = fmod(frame * 7, 360);
  command.server_time = clock;
  command.angles.y = (double)((int64_t)trunc(yaw * 65536 / 360) & 65535);
  command.forwardmove = forward;
  command.rightmove = side;
  command.upmove = fmod(frame, 33) == 32 ? 127 : 0;
  command.buttons = fmod(frame, 25) == 24 ? 1 : 0;
  command.weapon = 2;
  return command;
}
static const char *corpus_root(void) {
  const char *root = getenv("QTS_TEST_CORPUS");
  if (root != NULL && root[0] != '\0') return root;
  static char home[512];
  snprintf(home, sizeof home, "%s/qfiles", getenv("HOME") ? getenv("HOME") : "");
  return home;
}

static void player_state(qa_app_q3_qvm_server_game *qvm, double slot, qa_q3_player_state *state) {
  double stats[16], persistent[16], powerups[16], ammo[16];
  qa_app_q3_guest_records_player(qa_app_q3_qvm_server_game_records(qvm), slot, state, stats, persistent, powerups, ammo);
}

static bool run(const double *checkpoints, size_t checkpoint_count) {
  const char *tmp = getenv("TMPDIR");
  char user_root[1024];
  const char *fixed_root = getenv("GUEST_BOTS_USER_ROOT");
  if (fixed_root != NULL && fixed_root[0] != '\0') snprintf(user_root, sizeof user_root, "%s", fixed_root);
  else snprintf(user_root, sizeof user_root, "%s/guest-bots-user", tmp != NULL && tmp[0] != '\0' ? tmp : "/tmp");
  char clear[2200];
  snprintf(clear, sizeof clear, "rm -rf '%s' && mkdir -p '%s'", user_root, user_root);
  if (system(clear) != 0) return false;
  const char *argv[] = {"--content-root", corpus_root(), "--user-content-root", user_root, "--game", "q3-baseq3", "--map", "q3dm1",
                        "--movement", "q3", "--character", "q3", "--mode", "deathmatch", "--dedicated"};
  size_t argc = sizeof argv / sizeof argv[0];
  qa_content_root_search search = {corpus_root(), NULL};
  qa_app_application_command command = qa_app_parse_application_command(argv, argc, search, NULL);
  qa_app_content_host host;
  memset(&host, 0, sizeof host);
  qa_app_options launch = command.options;
  memset(&command.options, 0, sizeof command.options);
  qa_app_loaded_content *base_content = qa_app_load_application_content_native(host, launch, NULL, false, NULL, NULL, NULL);
  if (base_content == NULL) return false;
  qa_resolved_resource_reference qagame;
  memset(&qagame, 0, sizeof qagame);
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open((qa_content_mounted *)qa_app_loaded_content_mounts(base_content), "vm/qagame.qvm", NULL, NULL, &opened) ||
      !qa_resolved_resource_copy(opened.reference, &qagame)) {
    fprintf(stderr, "vm/qagame.qvm is not mounted\n");
    return false;
  }
  free(opened.bytes);
  qa_executable_recipe restored = *qa_app_loaded_content_recipe(base_content);
  qa_resolved_execution_module qvm_execution;
  memset(&qvm_execution, 0, sizeof qvm_execution);
  qvm_execution.kind = QA_EXECUTION_MODULE_QVM;
  qvm_execution.owner = restored.map.entities;
  qvm_execution.role = "server-game";
  qvm_execution.artifact = &qagame;
  qvm_execution.api.kind = QA_GAME_API_Q3;
  qvm_execution.api.as.q3 = (qa_q3_api_identity){QA_Q3_API_QAGAME, 8};
  restored.execution = &qvm_execution;
  restored.execution_count = 1;
  qa_app_application_command again = qa_app_parse_application_command(argv, argc, search, NULL);
  qa_app_options relaunch = again.options;
  memset(&again.options, 0, sizeof again.options);
  qa_app_application_command_destroy(&again);
  qa_app_loaded_content *content = qa_app_load_application_content_native(host, relaunch, &restored, true, NULL, NULL, NULL);
  if (content == NULL) return false;

  qa_identity_owner identity;
  if (!qa_identity_owner_create(&identity, "guest-bots-verify")) abort();
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_app_simulation_options options;
  memset(&options, 0, sizeof options);
  options.dedicated = true;
  options.identity = identity;
  options.recipe = *qa_app_loaded_content_recipe(content);
  options.world = &world;
  options.mounts = qa_app_loaded_content_mounts(content);
  options.skill = launch.skill;
  options.mode = launch.mode;
  options.seed = launch.seed;
  options.max_clients = 4;
  options.has_player_colors = true;
  options.prompt_ctx = &console_lines;
  options.prompt_supported = prompt_supported;
  options.identity_ctx = &out;
  options.player_identity = player_identity;
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, options.recipe.map.entities.content);
  char directory[2048];
  snprintf(directory, sizeof directory, "%.1000s/%.1000s", user_root, product->expectation.content_directory);
  qa_user_file_store q3_files;
  if (!qa_user_file_store_init(&q3_files, directory)) abort();
  qa_qvm_common_services q3_common;
  memset(&q3_common, 0, sizeof q3_common);
  q3_common.ctx = &guest;
  q3_common.milliseconds = guest_milliseconds;
  q3_common.real_time = guest_real_time;
  q3_common.commands = (qa_qvm_console_commands){&guest, guest_execute_now, guest_insert, guest_append, NULL, NULL, NULL};
  qa_app_sim_q3_guest_options q3_guest;
  memset(&q3_guest, 0, sizeof q3_guest);
  q3_guest.prepared = qa_app_loaded_content_prepared_q3_game(content);
  q3_guest.game_directory = "baseq3";
  q3_guest.has_game_type = true;
  q3_guest.print_ctx = &guest;
  q3_guest.print = guest_print;
  q3_guest.writable = &q3_files;
  q3_guest.common = &q3_common;
  options.q3_guest = &q3_guest;
  static const void *const no_prepared_mods[1] = {NULL};
  options.prepared_mods = no_prepared_mods;
  qa_app_shared_simulation *sim = qa_app_shared_simulation_load(options, next_frame, &guest);
  qa_app_q3_qvm_server_game *qvm = qa_app_shared_sim_q3_guest(sim);
  if (qvm == NULL) { fprintf(stderr, "expected the Q3 QVM guest\n"); abort(); }

  /* Application.prepareGuestBots. */
  qa_app_sim_bot_simulation bot_simulation = qa_app_shared_sim_bot_simulation(sim);
  qa_app_sim_bot_asset_content asset_content;
  memset(&asset_content, 0, sizeof asset_content);
  asset_content.ctx = content;
  asset_content.recipe = qa_app_loaded_content_recipe(content);
  asset_content.mounted = asset_mounted;
  asset_content.installed = asset_installed;
  asset_content.product = asset_product;
  asset_content.open = asset_open;
  qa_app_sim_application_bot_assets *assets = qa_app_sim_load_application_bot_assets(&asset_content, &bot_simulation);
  if (assets == NULL) abort();
  qa_app_sim_navigation_content navigation_content;
  memset(&navigation_content, 0, sizeof navigation_content);
  navigation_content.ctx = content;
  navigation_content.recipe = qa_app_loaded_content_recipe(content);
  navigation_content.mounts = (qa_content_mounted *)qa_app_loaded_content_mounts(content);
  navigation_content.for_content = navigation_for_content;
  qa_app_sim_navigation_simulation navigation_simulation = qa_app_shared_sim_navigation_simulation(sim, qa_app_loaded_content_recipe(content));
  bots.sim = sim;
  bots.identity = &identity;
  bots.navigation = qa_app_sim_create_application_bot_navigation(navigation_content, navigation_simulation);
  if (bots.navigation == NULL) abort();
  qa_app_sim_bot_navigation selected;
  memset(&selected, 0, sizeof selected);
  selected.selected = *qa_app_sim_application_bot_navigation_selected(bots.navigation);
  selected.ctx = bots.navigation;
  selected.checkpoint = navigation_checkpoint;
  selected.restore_checkpoint = navigation_restore;
  inner_files = assets->files;
  qa_app_q3_guest_bot_preparation preparation;
  memset(&preparation, 0, sizeof preparation);
  preparation.selected = &selected;
  preparation.library.files.files.ctx = &files_sentinel;
  preparation.library.files.files.read = files_read;
  preparation.library.files.files.list = files_list;
  preparation.library.files.provenance = files_provenance;
  preparation.library.random.next_int = bot_random;
  preparation.library.random.ctx = &bots;
  preparation.library.ctx = &bot_sentinel;
  preparation.library.milliseconds = bot_milliseconds;
  preparation.library.print = bot_print;
  preparation.library.open_log = bot_open_log;
  preparation.library.resume_log = bot_resume_log;
  preparation.ctx = &bots;
  preparation.create_client = bot_create_client;
  preparation.free_client = bot_free_client;
  preparation.snapshot_entity = bot_snapshot_entity;
  qa_app_q3_qvm_server_game_attach_bots(qvm, preparation);

  qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(qvm));
  qa_cvar_snapshot snapshot;
  memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_set(cvars, "bot_enable", "1", true, &snapshot)) qa_cvar_read_free(&snapshot);
  qa_app_q3_guest_output output = {&guest, guest_drop, guest_server_command, guest_configstring};
  qa_app_q3_qvm_server_game_initialize(qvm, output, "new", NULL, 0);
  qa_app_q3_admission admission = qa_app_q3_qvm_server_game_connect(qvm, qa_identity_owner_client(&identity, 0, 0),
      "\\name\\Player 1\\model\\sarge/default\\rate\\25000\\snaps\\20\\ip\\localhost");
  if (!admission.accepted) { fprintf(stderr, "rejected: %s\n", admission.reason); abort(); }
  qa_app_q3_player player = admission.player;
  qa_app_q3_qvm_server_game_begin(qvm, player, wire(0, guest.clock_milliseconds));
  emit("admitted %lld:%lld entity %s", (long long)player.actor.slot, (long long)player.actor.generation, N(player.source_entity));
  const char *addbot[] = {"addbot", "sarge", "1"};
  emit("addbot %s", qa_app_q3_qvm_server_game_console_command(qvm, addbot, 3) ? "true" : "false");
  qa_app_q3_player players[8];
  size_t player_count = qa_app_q3_qvm_server_game_players(qvm, players, 8);
  bool has_bot = false;
  qa_app_q3_player bot;
  memset(&bot, 0, sizeof bot);
  for (size_t i = 0; i < player_count; i++) {
    if (!qa_app_q3_qvm_server_game_is_bot(qvm, players[i].client)) continue;
    emit("bot %lld actor %lld:%lld entity %s", (long long)players[i].client.slot, (long long)players[i].actor.slot,
         (long long)players[i].actor.generation, N(players[i].source_entity));
    if (!has_bot) bot = players[i];
    has_bot = true;
  }
  double last = 0;
  for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] > last) last = checkpoints[i];
  int thinks = 0;
  qa_q3_player_state state;
  memset(&state, 0, sizeof state);
  if (has_bot) player_state(qvm, bot.source_entity, &state);
  double last_command_time = has_bot ? state.command_time_milliseconds : 0;
  for (double frame = 1; frame <= last; frame++) {
    guest.clock_milliseconds += 100;
    qa_app_q3_qvm_server_game_think(qvm, player, wire(frame, guest.clock_milliseconds));
    qa_input_batch batch = {100, NULL, 0};
    (void)qa_app_shared_sim_step_async(sim, batch, NULL, NULL);
    if (!has_bot) continue;
    player_state(qvm, bot.source_entity, &state);
    if (state.command_time_milliseconds != last_command_time) thinks++;
    last_command_time = state.command_time_milliseconds;
    bool checkpoint = false;
    for (size_t i = 0; i < checkpoint_count; i++) if (checkpoints[i] == frame) checkpoint = true;
    if (!checkpoint) continue;
    emit("== frame %s", N(frame));
    emit("bot ct %s o %s v %s thinks %d", N(state.command_time_milliseconds), V(state.origin), V(state.velocity), thinks);
    qa_body_state_result body = qa_shared_body_table_read(qa_app_shared_sim_bodies(sim), bot.actor);
    if (!body.has_value) emit("bot body -");
    else emit("bot body o %s a %s", V(body.value.origin), V(body.value.angles));
    qa_q3_player_state human;
    player_state(qvm, player.source_entity, &human);
    emit("player ct %s o %s", N(human.command_time_milliseconds), V(human.origin));
  }
  emit("thinks %d", thinks);
  for (size_t i = 0; i < console_lines.count; i++) {
    /* JSON.stringify of the console text. */
    const char *text = console_lines.items[i];
    size_t length = strlen(text);
    char *quoted = malloc(length * 6 + 3);
    if (quoted == NULL) abort();
    size_t at = 0;
    quoted[at++] = '"';
    for (size_t j = 0; j < length; j++) {
      unsigned char c = (unsigned char)text[j];
      if (c == '"' || c == '\\') { quoted[at++] = '\\'; quoted[at++] = (char)c; }
      else if (c == '\n') { quoted[at++] = '\\'; quoted[at++] = 'n'; }
      else if (c == '\r') { quoted[at++] = '\\'; quoted[at++] = 'r'; }
      else if (c == '\t') { quoted[at++] = '\\'; quoted[at++] = 't'; }
      else if (c == '\b') { quoted[at++] = '\\'; quoted[at++] = 'b'; }
      else if (c == '\f') { quoted[at++] = '\\'; quoted[at++] = 'f'; }
      else if (c < 0x20) at += (size_t)snprintf(quoted + at, 7, "\\u%04x", c);
      else quoted[at++] = (char)c;
    }
    quoted[at++] = '"';
    quoted[at] = 0;
    emit("console %s", quoted);
    free(quoted);
  }
  qa_app_q3_qvm_server_game_shutdown(qvm);
  qa_app_shared_sim_close(sim);
  qa_app_shared_simulation_destroy(sim);
  qa_app_sim_application_bot_navigation_destroy(bots.navigation);
  qa_app_sim_application_bot_assets_free(assets);
  qa_user_file_store_destroy(&q3_files);
  qa_identity_owner_destroy(&identity);
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
  qa_app_loaded_content_close(base_content);
  qa_app_loaded_content_destroy(base_content);
  qa_resolved_resource_release(&qagame);
  qa_app_application_command_destroy(&command);
  lines_free(&console_lines);
  return true;
}

int main(int argc, char **argv) {
  double frames[32];
  size_t frame_count = 0;
  for (int i = 1; i < argc && frame_count < 32; i++) frames[frame_count++] = strtod(argv[i], NULL);
  bool printing = frame_count != 0;
  char qagame_path[1024];
  snprintf(qagame_path, sizeof qagame_path, "%s/Quake 3 Arena", corpus_root());
  struct stat corpus;
  if (stat(qagame_path, &corpus) != 0) { printf("test_guest_bots_verify: no retail Quake 3 corpus at %s, skipped\n", qagame_path); return 0; }
  lines expected = {0};
  if (!printing) {
    FILE *file = fopen("tests/app/oracles/guest_bots_verify.txt", "rb");
    if (file == NULL) { printf("test_guest_bots_verify: no oracle dump, skipped\n"); return 0; }
    char *line = NULL;
    size_t capacity = 0;
    ssize_t length;
    while ((length = getline(&line, &capacity, file)) >= 0) {
      if (length > 0 && line[length - 1] == '\n') line[length - 1] = '\0';
      lines_push(&expected, line);
      if (strncmp(line, "== frame ", 9) == 0 && frame_count < 32) frames[frame_count++] = strtod(line + 9, NULL);
    }
    free(line);
    fclose(file);
  }
  if (!run(frames, frame_count)) { lines_free(&expected); lines_free(&out); return 1; }
  int status = 0;
  if (printing) for (size_t i = 0; i < out.count; i++) printf("%s\n", out.items[i]);
  else {
    size_t mismatches = 0, total = expected.count > out.count ? expected.count : out.count;
    for (size_t i = 0; i < total; i++) {
      const char *want = i < expected.count ? expected.items[i] : "<end>", *got = i < out.count ? out.items[i] : "<end>";
      if (strcmp(want, got) == 0) continue;
      if (mismatches < 10) fprintf(stderr, "line %zu\n  ts: %s\n  c:  %s\n", i + 1, want, got);
      mismatches++;
    }
    printf("test_guest_bots_verify: %zu lines, %zu mismatches\n", total, mismatches);
    status = mismatches == 0 ? 0 : 1;
  }
  fflush(stdout);
  lines_free(&expected);
  lines_free(&out);
  return status;
}
