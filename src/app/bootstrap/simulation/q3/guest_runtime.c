/* Port of src/app/bootstrap/simulation/q3/guest-runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include "qa/network/common/js_number.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/guest_world.h"
#include "qa/app/bootstrap/simulation/q3/qvm_engine.h"
#include "qa/compat/qvm/abi.h"
#include "qa/compat/qvm/server_game_syscalls.h"
#include "qa/core/commands/index.h"
#include "qa/core/commands/text.h"
#include "qa/foundation/support.h"
/* checkpoint value helpers are in persistence/value.h */
#include "qa/persistence/save_image.h"
#include <math.h>

typedef enum lifecycle_kind { LC_CREATED, LC_RESTORING, LC_RESTORED, LC_INITIALIZING, LC_RUNNING, LC_SHUTTING_DOWN, LC_RETIRED } lifecycle_kind;
typedef enum phase_kind { PH_CONNECTING, PH_CONNECTED, PH_ACTIVE, PH_DROPPING } phase_kind;
typedef struct client_entry {
  qa_app_q3_player player;
  bool disconnect_notified;
  phase_kind phase;
  char drop_reason[256];
} client_entry;
typedef struct message_queue { char *items[64]; size_t head, count; } message_queue;
typedef struct pending_command { double slot; char **argv; size_t argc; } pending_command;
/* TS InputRetirement: entry is a copy of the ClientEntry player (has_entry false = null). */
typedef struct retirement { double slot; qa_app_q3_mod_client_identity identity; bool has_entry; qa_app_q3_player entry; int phase; } retirement;
struct qa_app_q3_qvm_server_game {
  qa_app_q3_qvm_server_game_options options;
  qa_app_q3_guest_records *records;
  qa_app_q3_guest_world *guest_world;
  qa_app_q3_guest_spatial *spatial;
  qa_app_q3_guest_bots *bots;
  bool has_bots;
  qa_app_q3_guest_bot_preparation bot_preparation;
  const qa_save_value *saved_bots;
  double bot_clients[64]; size_t bot_client_count;
  pending_command *pending_commands; size_t pending_command_count, pending_command_capacity; /* TS pendingBotCommands */
  double pending_admissions[64]; size_t pending_admission_count;
  int64_t bot_message_slots[64]; message_queue bot_messages[64]; size_t bot_message_count;
  lifecycle_kind lifecycle;
  qa_app_q3_guest_output output;
  bool has_output;
  double external_operations;
  client_entry clients[64]; size_t client_count;
  qa_app_q3_guest_map_client reconnecting[64]; size_t reconnecting_count;
  retirement retirements[64]; size_t retirement_count;
  qa_client_id (*resolve_client)(void *ctx, qa_app_q3_saved_guest_client_id saved);
  void *resolve_ctx;
  /* The game-owned engine side (TS engine, files, cursor, parser, services, common, traps). */
  qa_app_q3_game_engine *engine;
  qa_app_q3_server_traps traps;
  qa_qvm_files *files;
  qa_common_parse_cursor cursor;
  bool has_cursor;
  qa_common_parse_state parser;
  qa_qvm_server_spatial_operations spatial_operations;
  qa_qvm_server_game_services services;
  qa_qvm_common_services common;
  qa_qvm_host_state host_state;
  qa_qvm_input_binding *input_binding;
  qa_qvm_input_definition input_definition;
  uint8_t *host_bytes;
  char *denied;
};

static bool client_eq(qa_client_id a, qa_client_id b) {
  return a.session.token == b.session.token && a.slot == b.slot && a.generation == b.generation;
}
static bool in_slots(const double *slots, size_t count, double slot) {
  for (size_t i = 0; i < count; i++) if (slots[i] == slot) return true;
  return false;
}
static void slot_add(double *slots, size_t *count, double slot) {
  if (!in_slots(slots, *count, slot) && *count < 64) slots[(*count)++] = slot;
}
static bool slot_remove(double *slots, size_t *count, double slot) {
  for (size_t i = 0; i < *count; i++) if (slots[i] == slot) {
    memmove(slots + i, slots + i + 1, (*count - i - 1) * sizeof *slots); (*count)--; return true;
  }
  return false;
}
static bool module_active(qa_app_q3_qvm_server_game *game) { return game->engine->ops->active(game->engine); }
bool qa_app_q3_qvm_server_game_engine_active(qa_app_q3_qvm_server_game *game) { return module_active(game); }
static client_entry *find_client(qa_app_q3_qvm_server_game *game, double slot) {
  for (size_t i = 0; i < game->client_count; i++) if (game->clients[i].player.source_entity == slot) return &game->clients[i];
  return NULL;
}
static void remove_client_entry(qa_app_q3_qvm_server_game *game, client_entry *entry) {
  size_t index = (size_t)(entry - game->clients);
  memmove(game->clients + index, game->clients + index + 1, (game->client_count - index - 1) * sizeof *game->clients);
  game->client_count--;
}
static void current(qa_app_q3_qvm_server_game *game) {
  if (game->lifecycle == LC_RETIRED) qa_range_error("Q3 guest is retired");
  game->options.assert_current(game->options.ctx);
}
static void running(qa_app_q3_qvm_server_game *game) {
  current(game);
  if (game->lifecycle != LC_RUNNING) qa_range_error("Q3 guest is not running");
  if (module_active(game)) qa_range_error("Q3 guest already has an active external operation");
}
static qa_app_q3_guest_output output_of(qa_app_q3_qvm_server_game *game) {
  if (!(game->lifecycle == LC_INITIALIZING || game->lifecycle == LC_RUNNING || game->lifecycle == LC_SHUTTING_DOWN))
    qa_range_error("Q3 guest output is unavailable");
  return game->output;
}
static message_queue *find_messages(qa_app_q3_qvm_server_game *game, double slot) {
  for (size_t i = 0; i < game->bot_message_count; i++) if (game->bot_message_slots[i] == (int64_t)slot) return &game->bot_messages[i];
  return NULL;
}
static message_queue *add_messages(qa_app_q3_qvm_server_game *game, double slot) {
  message_queue *queue = find_messages(game, slot);
  if (!queue && game->bot_message_count < 64) {
    game->bot_message_slots[game->bot_message_count] = (int64_t)slot;
    queue = &game->bot_messages[game->bot_message_count++];
    memset(queue, 0, sizeof *queue);
  }
  return queue;
}
static void queue_push(message_queue *queue, const char *text) {
  if (!queue) return;
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  if (queue->count >= 64) { free(queue->items[queue->head]); queue->items[queue->head] = copy; queue->head = (queue->head + 1) % 64; return; }
  queue->items[(queue->head + queue->count) % 64] = copy;
  queue->count++;
}
static char *dup_str(const char *text) {
  size_t length = strlen(text ? text : "") + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text ? text : "", length);
  return copy;
}
static void notify_disconnect(qa_app_q3_qvm_server_game *game, client_entry *entry) {
  if (entry->disconnect_notified) return;
  entry->disconnect_notified = true;
  if (game->options.before_disconnect) game->options.before_disconnect(game->options.ctx, entry->player.actor);
}
static void release_entry(qa_app_q3_qvm_server_game *game, client_entry *entry);
static void finish_operation(qa_app_q3_qvm_server_game *game) {
  game->external_operations--;
  if (game->external_operations != 0 || module_active(game)) return;
  for (size_t i = 0; i < game->retirement_count;) {
    retirement *item = &game->retirements[i];
    if (item->phase != 2) { i++; continue; }
    double slot = item->slot;
    client_entry *current_entry = find_client(game, slot);
    if (item->has_entry && current_entry && client_eq(current_entry->player.client, item->entry.client)
        && qa_same_actor(current_entry->player.actor, item->entry.actor)) release_entry(game, current_entry);
    if (slot_remove(game->bot_clients, &game->bot_client_count, slot) && game->has_bots)
      game->bot_preparation.free_client(game->bot_preparation.ctx, item->identity.client);
    slot = item->slot;
    message_queue *queue = find_messages(game, slot);
    if (queue) { for (size_t j = 0; j < queue->count; j++) free(queue->items[(queue->head + j) % 64]); queue->count = 0; }
    slot_remove(game->pending_admissions, &game->pending_admission_count, slot);
    qa_app_q3_guest_records_finish_input_retirement(game->records, slot);
    memmove(game->retirements + i, game->retirements + i + 1, (game->retirement_count - i - 1) * sizeof *game->retirements);
    game->retirement_count--;
  }
}
static void validate_client(qa_app_q3_qvm_server_game *game, qa_client_id client) {
  qa_session_id session = client.session;
  size_t observations = 0;
  (void)qa_session_actor_registry_observations(game->options.records.actors, &observations);
  if (observations == (size_t)-1) return;
  /* session equality via registry checkpoint session is enforced by caller ids */
  if (client.slot < 0 || /* client.slot is an int64_t: always integral */ (double)client.slot >= game->options.max_clients
      || session.token == 0)
    qa_range_error("Q3 client slot belongs to another server or is outside its capacity");
}
static client_entry *entry_of(qa_app_q3_qvm_server_game *game, qa_app_q3_player player) {
  client_entry *entry = find_client(game, player.source_entity);
  if (!entry || !client_eq(entry->player.client, player.client) || !qa_same_actor(entry->player.actor, player.actor))
    qa_range_error("Q3 player is no longer admitted");
  return entry;
}
static void release_entry(qa_app_q3_qvm_server_game *game, client_entry *entry) {
  double slot = entry->player.source_entity;
  slot_remove(game->pending_admissions, &game->pending_admission_count, slot);
  remove_client_entry(game, entry);
  qa_app_q3_server_state_clear_client(game->options.state, slot);
  qa_app_q3_guest_records_release_client(game->records, slot);
}
static void admit_pending_bots(qa_app_q3_qvm_server_game *game) {
  while (game->pending_admission_count) {
    double slot = game->pending_admissions[0];
    slot_remove(game->pending_admissions, &game->pending_admission_count, slot);
    client_entry *entry = find_client(game, slot);
    if (entry && entry->phase == PH_ACTIVE && game->options.client_changed)
      game->options.client_changed(game->options.ctx, "admitted", entry->player.actor);
  }
}
static void drain_bot_commands(qa_app_q3_qvm_server_game *game) {
  while (game->pending_command_count) {
    pending_command pending = game->pending_commands[0];
    memmove(game->pending_commands, game->pending_commands + 1, (game->pending_command_count - 1) * sizeof *game->pending_commands);
    game->pending_command_count--;
    if (in_slots(game->bot_clients, game->bot_client_count, pending.slot))
      game->engine->ops->client_command_async(game->engine, pending.slot, (const char *const *)pending.argv, pending.argc);
    for (size_t i = 0; i < pending.argc; i++) free(pending.argv[i]);
    free(pending.argv);
    current(game);
  }
  admit_pending_bots(game);
}

/* ---- the game-owned engine side (guest-runtime.ts constructor and host) ---- */
static void state_print(void *ctx, const char *text) { qa_app_q3_server_state_print(((qa_app_q3_qvm_server_game *)ctx)->options.state, text); }
static void files_assert_current(void *ctx) { current(ctx); }
static const char *const *common_arguments(void *ctx, size_t *out_count) { (void)ctx; *out_count = 0; return NULL; }
static double common_milliseconds(void *ctx) {
  const qa_qvm_common_services *common = ((qa_app_q3_qvm_server_game *)ctx)->options.common;
  return common->milliseconds(common->ctx);
}
static double common_real_time(void *ctx, bool has_output, qa_qvm_calendar *out) {
  const qa_qvm_common_services *common = ((qa_app_q3_qvm_server_game *)ctx)->options.common;
  return common->real_time(common->ctx, has_output, out);
}
static const char *services_configstring_get(void *ctx, int32_t index) {
  const char *value = qa_app_q3_server_state_configstring_get(((qa_app_q3_qvm_server_game *)ctx)->options.state, index);
  return value ? value : "";
}
static void services_configstring_set(void *ctx, int32_t index, const char *value) {
  qa_app_q3_qvm_server_game_set_configstring(ctx, index, value);
}
static const char *services_get_userinfo(void *ctx, double slot) {
  const char *value = qa_app_q3_server_state_get_userinfo(((qa_app_q3_qvm_server_game *)ctx)->options.state, slot);
  return value ? value : "";
}
static void services_set_userinfo(void *ctx, double slot, const char *value) {
  qa_app_q3_server_state_set_userinfo(((qa_app_q3_qvm_server_game *)ctx)->options.state, slot, value);
}
static qa_qvmx_wire_user_command services_user_command(void *ctx, double slot) {
  qa_app_q3_wire_user_command command;
  qa_qvmx_wire_user_command out; memset(&out, 0, sizeof out);
  if (!qa_app_q3_server_state_get_user_command(((qa_app_q3_qvm_server_game *)ctx)->options.state, slot, &command)) return out;
  out.server_time = command.server_time;
  out.angles[0] = command.angles.x; out.angles[1] = command.angles.y; out.angles[2] = command.angles.z;
  out.buttons = command.buttons; out.weapon = command.weapon;
  out.forwardmove = command.forwardmove; out.rightmove = command.rightmove; out.upmove = command.upmove;
  return out;
}
static void services_drop_client(void *ctx, double slot, const char *reason) { qa_app_q3_qvm_server_game_drop(ctx, slot, reason); }
static void services_send_server_command(void *ctx, double slot, const char *text) {
  qa_app_q3_qvm_server_game *game = ctx;
  for (size_t i = 0; i < game->bot_message_count; i++)
    if (slot == -1 || (double)game->bot_message_slots[i] == slot) queue_push(&game->bot_messages[i], text);
  if (in_slots(game->bot_clients, game->bot_client_count, slot)) return;
  output_of(game).send_server_command(game->output.ctx, slot, text);
}
static qa_qvm_entity_token_result services_entity_token(void *ctx) {
  qa_app_q3_qvm_server_game *game = ctx;
  qa_qvm_entity_token_result out;
  out.token = qa_common_parse_state_parse(&game->parser, &game->cursor, true);
  out.ended = !game->cursor.has_offset;
  return out;
}
static void validate_current(void *ctx) { current(ctx); }
/* The shared trap table's dispatch (TS dispatch closure): the table is bound once the services exist. */
static qa_qvm_host_result dispatch(void *ctx, const qa_qvm_host_call *call) {
  return qa_app_q3_server_traps_dispatch(&((qa_app_q3_qvm_server_game *)ctx)->traps, call);
}
/* A native module's traps: the server game is current before and after the shared table. */
static qa_qvm_host_result native_traps(void *ctx, const qa_qvm_host_call *call) {
  qa_app_q3_qvm_server_game *game = ctx;
  current(game);
  qa_qvm_host_result result = dispatch(game, call);
  current(game);
  return result;
}
static bool traps_bots(void *ctx, const qa_qvm_host_call *call, qa_qvm_host_result *out) {
  qa_app_q3_qvm_server_game *game = ctx;
  return game->has_bots && qa_app_q3_guest_bots_syscall(game->bots, call, out);
}
typedef struct engine_build { qa_app_q3_qvm_server_game *game; const char *semantics; } engine_build;
static void build_engine(void *arg) {
  engine_build *b = arg;
  qa_app_q3_qvm_server_game *game = b->game;
  const qa_app_q3_guest_module *module = &game->options.module;
  if (strcmp(module->kind, "qvm") == 0) {
    qa_app_q3_qvm_engine_options qvm;
    memset(&qvm, 0, sizeof qvm);
    qvm.artifact = module->artifact; qvm.semantics = b->semantics; qvm.host_state = &game->host_state;
    qvm.ctx = game; qvm.traps = dispatch; qvm.assert_current = validate_current;
    game->engine = qa_app_q3_qvm_engine_create(qvm);
  } else {
    qa_app_q3_native_guest_options native;
    memset(&native, 0, sizeof native);
    native.prepared = module->image; native.traps_ctx = game; native.traps = native_traps; native.host_state = &game->host_state;
    native.has_instruction_budget = module->has_instruction_budget; native.instruction_budget = module->instruction_budget;
    qa_app_q3_native_guest_source *source = qa_app_q3_native_guest_source_create(&native);
    game->engine = source != NULL ? qa_app_q3_native_guest_source_engine(source) : NULL;
  }
  if (game->engine == NULL) qa_range_error("Out of memory");
}
typedef struct records_build { qa_app_q3_qvm_server_game *game; } records_build;
static void build_records(void *arg) {
  qa_app_q3_qvm_server_game *game = ((records_build *)arg)->game;
  qa_qvm_game_data_set_client_count(game->engine->data, game->options.max_clients);
  game->records = qa_app_q3_guest_records_create(game->engine->data, game->options.records);
  game->guest_world = qa_app_q3_guest_world_create(game->options.records.scene);
  qa_app_q3_guest_spatial_dependencies spatial; memset(&spatial, 0, sizeof spatial);
  spatial.scene = game->options.records.scene; spatial.records = game->records; spatial.world = game->guest_world;
  spatial.ctx = game->options.records.ctx; spatial.collision = game->options.records.collision;
  game->spatial = game->records ? qa_app_q3_guest_spatial_create(spatial) : NULL;
  if (!game->records || !game->spatial) qa_range_error("Out of memory");
}
static qa_qvm_host_checkpoint host_checkpoint(void *ctx);
static void host_restore(void *ctx, const qa_qvm_host_checkpoint *checkpoint);

qa_app_q3_qvm_server_game *qa_app_q3_qvm_server_game_create(qa_app_q3_qvm_server_game_options options) {
  if (!(isfinite(options.max_clients) && options.max_clients == trunc(options.max_clients)) || options.max_clients < 1 || options.max_clients > 64)
    qa_range_error("Q3 guest requires 1..64 clients");
  qa_app_q3_qvm_server_game *game = calloc(1, sizeof *game);
  if (!game) return NULL;
  game->options = options;
  qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(options.state);
  qa_cvar_snapshot result;
  if (qa_cvar_registry_register(cvars, "bot_enable", "1", 0, &result)) qa_cvar_read_free(&result);
  char buffer[32];
  snprintf(buffer, sizeof buffer, "%s", QA_JS_NUMBER(options.max_clients));
  if (qa_cvar_registry_set(cvars, "sv_maxclients", buffer, true, &result)) qa_cvar_read_free(&result);
  if (qa_cvar_registry_set(cvars, "dedicated", options.has_dedicated && !options.dedicated ? "0" : "1", true, &result)) qa_cvar_read_free(&result);
  qa_qvm_file_services files; memset(&files, 0, sizeof files);
  files.mounts = options.mounts; files.writable = options.writable; files.ctx = game; files.print = state_print;
  files.assert_current = files_assert_current;
  game->files = qa_qvm_files_create(files);
  if (!game->files) { free(game); return NULL; }
  /* qvmSemanticsFromCvar(state.cvars, "qagame"): vm_game 1 selects the interpreted VM. */
  game->host_state.ctx = game; game->host_state.checkpoint = host_checkpoint; game->host_state.restore = host_restore;
  engine_build build = {game, strcmp(options.module.kind, "qvm") == 0 ? qa_qvm_semantics_from_cvar(cvars, "qagame") : NULL};
  qa_caught_error error;
  if (!qa_try(build_engine, &build, &error)) {
    qa_qvm_files_close_all(game->files);
    qa_app_q3_qvm_server_game_destroy(game);
    qa_rethrow(&error);
  }
  records_build records = {game};
  if (!qa_try(build_records, &records, &error)) {
    qa_qvm_files_close_all(game->files);
    game->engine->ops->retire(game->engine);
    qa_app_q3_qvm_server_game_destroy(game);
    qa_rethrow(&error);
  }
  memset(&game->common, 0, sizeof game->common);
  game->common.role = "qagame"; game->common.cvars = cvars; game->common.ctx = game; game->common.print = state_print;
  game->common.arguments = common_arguments; game->common.milliseconds = common_milliseconds;
  game->common.real_time = common_real_time; game->common.commands = options.common->commands;
  const char *text = options.entity_text ? options.entity_text : "";
  if (!qa_common_parse_cursor_init(&game->cursor, text, strlen(text), false)) qa_range_error("Out of memory");
  game->has_cursor = true;
  qa_common_parse_state_init(&game->parser);
  game->spatial_operations = qa_app_q3_guest_spatial_operations(game->spatial);
  memset(&game->services, 0, sizeof game->services);
  game->services.data = game->engine->data; game->services.cvars = cvars; game->services.max_clients = options.max_clients;
  game->services.spatial = &game->spatial_operations;
  game->services.configstrings_ctx = game; game->services.configstring_get = services_configstring_get;
  game->services.configstring_set = services_configstring_set;
  game->services.ctx = game; game->services.get_userinfo = services_get_userinfo; game->services.set_userinfo = services_set_userinfo;
  game->services.get_user_command = services_user_command; game->services.drop_client = services_drop_client;
  game->services.send_server_command = services_send_server_command;
  game->services.entity_tokens.ctx = game; game->services.entity_tokens.entity_token = services_entity_token;
  qa_app_q3_server_traps_services traps = {&game->common, game->files, &game->services, game, traps_bots, state_print};
  qa_app_q3_server_traps_init(&game->traps, traps);
  game->lifecycle = LC_CREATED;
  return game;
}
void qa_app_q3_qvm_server_game_destroy(qa_app_q3_qvm_server_game *game) {
  if (!game) return;
  if (game->input_binding) qa_qvm_input_binding_destroy(game->input_binding);
  qa_app_q3_guest_spatial_destroy(game->spatial);
  qa_app_q3_guest_records_destroy(game->records);
  qa_app_q3_guest_world_destroy(game->guest_world);
  qa_app_q3_guest_bots_destroy(game->bots);
  for (size_t i = 0; i < game->pending_command_count; i++) {
    for (size_t j = 0; j < game->pending_commands[i].argc; j++) free(game->pending_commands[i].argv[j]);
    free(game->pending_commands[i].argv);
  }
  free(game->pending_commands);
  for (size_t i = 0; i < game->bot_message_count; i++)
    for (size_t j = 0; j < game->bot_messages[i].count; j++) free(game->bot_messages[i].items[(game->bot_messages[i].head + j) % 64]);
  if (game->has_cursor) qa_common_parse_cursor_destroy(&game->cursor);
  if (game->files) qa_qvm_files_destroy(game->files);
  if (game->engine) game->engine->ops->destroy(game->engine);
  free(game->host_bytes); free(game->denied);
  free(game);
}
qa_qvm_game *qa_app_q3_qvm_server_game_game(qa_app_q3_qvm_server_game *game) {
  qa_qvm_game *qvm = qa_app_q3_qvm_engine_game(game->engine);
  if (qvm == NULL) qa_raise("Error", 0, 0, "This Q3 guest runs a native module; interpreted-module bindings do not apply");
  return qvm;
}
qa_app_q3_game_engine *qa_app_q3_qvm_server_game_engine(qa_app_q3_qvm_server_game *game) { return game->engine; }
qa_app_q3_server_state *qa_app_q3_qvm_server_game_state(qa_app_q3_qvm_server_game *game) { return game->options.state; }
qa_app_q3_guest_records *qa_app_q3_qvm_server_game_records(qa_app_q3_qvm_server_game *game) { return game->records; }
qa_app_q3_guest_spatial *qa_app_q3_qvm_server_game_spatial(qa_app_q3_qvm_server_game *game) { return game->spatial; }
bool qa_app_q3_qvm_server_game_player_arsenal(qa_app_q3_qvm_server_game *game, qa_actor_id actor, qa_arsenal_state *out) {
  current(game);
  return game->options.arsenal && game->options.arsenal(game->options.ctx, actor, out);
}
/* retireInput (guest-runtime.ts 132-143). */
static void retire_input(qa_app_q3_qvm_server_game *game, double slot, qa_mod_client_identity identity) {
  for (size_t i = 0; i < game->retirement_count; i++) {
    retirement *existing = &game->retirements[i];
    if (existing->slot != slot) continue;
    if (!qa_same_actor(existing->identity.actor, identity.actor) || !client_eq(existing->identity.client, identity.client))
      qa_range_error("QVM input retirement changed client generation");
    return;
  }
  client_entry *entry = find_client(game, slot);
  if (entry && (!qa_same_actor(entry->player.actor, identity.actor) || !client_eq(entry->player.client, identity.client)))
    qa_range_error("QVM input retirement does not own its source client");
  qa_app_q3_guest_records_retire_input_client(game->records, slot);
  if (game->retirement_count >= 64) qa_range_error("QVM input retirement capacity exceeded");
  retirement *item = &game->retirements[game->retirement_count++];
  memset(item, 0, sizeof *item);
  item->slot = slot; item->identity.actor = identity.actor; item->identity.client = identity.client;
  item->has_entry = entry != NULL; if (entry) item->entry = entry->player;
  item->phase = entry ? 0 : 2; /* pending | complete */
}
static void input_retiring(void *ctx, double slot, qa_mod_client_identity identity) { retire_input(ctx, slot, identity); }
static bool input_retired(void *ctx, double slot) {
  return qa_app_q3_guest_records_is_input_retired(((qa_app_q3_qvm_server_game *)ctx)->records, slot);
}
/* disconnectInput (guest-runtime.ts 144-156). */
static int32_t input_disconnect(void *ctx, double slot, qa_mod_client_identity identity, qa_qvm_module_function_call *call) {
  qa_app_q3_qvm_server_game *game = ctx;
  retire_input(game, slot, identity);
  retirement *item = NULL;
  for (size_t i = 0; i < game->retirement_count; i++) if (game->retirements[i].slot == slot) item = &game->retirements[i];
  if (!item) qa_range_error("QVM input retirement was lost");
  if (item->phase != 0) return 0;
  item->phase = 1; /* disconnecting */
  if (item->has_entry) {
    client_entry *entry = find_client(game, slot);
    if (entry && client_eq(entry->player.client, item->entry.client)) {
      notify_disconnect(game, entry); entry->phase = PH_DROPPING;
      snprintf(entry->drop_reason, sizeof entry->drop_reason, "%s", "Client input actor was removed.");
    }
  }
  int32_t args[10] = {QA_QVM_GAME_EXPORT_GAME_CLIENT_DISCONNECT, (int32_t)slot, 0, 0, 0, 0, 0, 0, 0, 0};
  int32_t result = strcmp(call->execution, "synchronous") == 0
      ? call->invoke(call->call_ctx, args, 10, 0, NULL)
      : call->invoke_async(call->call_ctx, args, 10, 0, validate_current, game);
  for (size_t i = 0; i < game->retirement_count; i++) if (game->retirements[i].slot == slot) game->retirements[i].phase = 2;
  return result;
}
typedef struct input_motion { qa_qvm_input_run_fn run; void *run_ctx; int32_t result; } input_motion;
static void input_motion_run(void *ctx) { input_motion *motion = ctx; motion->result = motion->run(motion->run_ctx); }
static int32_t input_movement(void *ctx, double slot, qa_qvm_input_run_fn run, void *run_ctx) {
  input_motion motion = {run, run_ctx, 0};
  qa_app_q3_guest_records_with_input_motion(((qa_app_q3_qvm_server_game *)ctx)->records, slot, input_motion_run, &motion);
  return motion.result;
}
void qa_app_q3_qvm_server_game_bind_input(qa_app_q3_qvm_server_game *game, const qa_qvm_input_definition *definition,
    qa_qvm_input_services services) {
  if (game->input_binding) { qa_qvm_input_binding_close(game->input_binding); qa_qvm_input_binding_destroy(game->input_binding); game->input_binding = NULL; }
  game->input_definition = *definition;
  qa_qvm_input_source source; memset(&source, 0, sizeof source);
  source.game = qa_app_q3_qvm_server_game_game(game); source.definition = game->input_definition;
  source.ctx = game; source.retiring = input_retiring; source.retired = input_retired;
  source.disconnect = input_disconnect; source.movement = input_movement;
  game->input_binding = qa_qvm_input_binding_create(source, services);
  if (!game->input_binding) qa_range_error("Out of memory");
}
static void close_input(qa_app_q3_qvm_server_game *game) {
  if (game->input_binding) { qa_qvm_input_binding_close(game->input_binding); qa_qvm_input_binding_destroy(game->input_binding); game->input_binding = NULL; }
}
static double bot_allocate(void *v);
static void bot_free(void *v, double slot);
static double bot_snapshot(void *v, double slot, double sequence);
static char *bot_message(void *v, double slot);
static void bot_user_command(void *v, double slot, qa_qvmx_wire_user_command command);
static double bot_allocate(void *v);
static void bot_client_command(void *v, int64_t client, const char *text) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  if (!in_slots(game->bot_clients, game->bot_client_count, (double)client))
    qa_raise("Error", 0, 0, "Bot command has no owned client %s", QA_JS_NUMBER((double)client));
  if (game->pending_command_count == game->pending_command_capacity) {
    size_t next = game->pending_command_capacity == 0 ? 8 : game->pending_command_capacity * 2;
    pending_command *grown = realloc(game->pending_commands, next * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    game->pending_commands = grown;
    game->pending_command_capacity = next;
  }
  /* tokenizeCommand(text, "q3").argv */
  qa_command_tokens tokens;
  if (!qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, &tokens)) qa_range_error("Out of memory");
  pending_command pending;
  memset(&pending, 0, sizeof pending);
  pending.slot = (double)client;
  pending.argv = tokens.argc == 0 ? NULL : calloc(tokens.argc, sizeof *pending.argv);
  if (tokens.argc != 0 && !pending.argv) qa_range_error("Out of memory");
  for (size_t i = 0; i < tokens.argc; i++) pending.argv[pending.argc++] = dup_str(tokens.argv[i]);
  qa_command_tokens_destroy(&tokens);
  game->pending_commands[game->pending_command_count++] = pending;
}
void qa_app_q3_qvm_server_game_attach_bots(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_bot_preparation preparation) {
  current(game);
  if (game->has_bots || (game->lifecycle != LC_CREATED && game->lifecycle != LC_RESTORED))
    qa_raise("Error", 0, 0, "Attach guest bot services before game initialization");
  game->bot_preparation = preparation;
  qa_app_q3_guest_bot_options options;
  memset(&options, 0, sizeof options);
  options.library = preparation.library;
  options.command_ctx = game;
  options.client_command = bot_client_command;
  options.selected = preparation.selected;
  options.records = game->records;
  options.spatial = game->spatial;
  options.entities = game->options.entity_text;
  char *map_name = qa_cvar_registry_variable_string(qa_app_q3_server_state_cvars(game->options.state), "mapname");
  options.map_name = map_name;
  options.clients.ctx = game;
  options.clients.allocate_client = bot_allocate;
  options.clients.free_client = bot_free;
  options.clients.snapshot_entity = bot_snapshot;
  options.clients.console_message = bot_message;
  options.clients.user_command = bot_user_command;
  game->bots = qa_app_q3_guest_bots_create(&options);
  free(map_name);
  if (game->bots == NULL) qa_range_error("Out of memory");
  game->has_bots = true;
  if (game->saved_bots) { qa_app_q3_guest_bots_restore(game->bots, game->saved_bots); game->saved_bots = NULL; }
}
static double bot_allocate(void *v) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  for (double slot = 0; slot < game->options.max_clients; slot++) {
    if (find_client(game, slot)) continue;
    bool reconnecting = false, retiring = false;
    for (size_t i = 0; i < game->reconnecting_count; i++) if ((double)game->reconnecting[i].client.slot == slot) reconnecting = true;
    for (size_t i = 0; i < game->retirement_count; i++) if (game->retirements[i].slot == slot) retiring = true;
    if (reconnecting || retiring) continue;
    qa_client_id client;
    if (!game->bot_preparation.create_client(game->bot_preparation.ctx, slot, &client)) continue;
    qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(game->records, slot);
    qa_qvm_entity_state number_state = qa_qvm_borrow_entity_state(entity->s.view, entity->profile);
    qa_qvm_entity_state_set_number(&number_state, slot);
    qa_owned_actor actor = qa_app_q3_guest_records_actor(game->records, slot);
    qa_app_q3_player player = {client, actor.id, slot};
    validate_client(game, client);
    client_entry *entry = &game->clients[game->client_count++];
    memset(entry, 0, sizeof *entry); entry->player = player; entry->phase = PH_ACTIVE;
    slot_add(game->bot_clients, &game->bot_client_count, slot);
    slot_add(game->pending_admissions, &game->pending_admission_count, slot);
    add_messages(game, slot);
    return slot;
  }
  return -1;
}
static void bot_free(void *v, double slot) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  if (!slot_remove(game->bot_clients, &game->bot_client_count, slot)) return;
  qa_qvm_shared_entity *entity = qa_app_q3_guest_records_entity(game->records, slot);
  qa_qvm_shared_entity_set_sv_flags(entity, qa_qvm_shared_entity_get_sv_flags(entity) & ~8);
  client_entry *entry = find_client(game, slot);
  if (entry) {
    notify_disconnect(game, entry);
    qa_app_q3_player player = entry->player;
    release_entry(game, entry);
    game->bot_preparation.free_client(game->bot_preparation.ctx, player.client);
  }
  slot_remove(game->pending_admissions, &game->pending_admission_count, slot);
  message_queue *queue = find_messages(game, slot);
  if (queue) { for (size_t i = 0; i < queue->count; i++) free(queue->items[(queue->head + i) % 64]); queue->count = 0; }
}
static double bot_snapshot(void *v, double slot, double sequence) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  client_entry *entry = find_client(game, slot);
  return !entry ? -1 : game->bot_preparation.snapshot_entity(game->bot_preparation.ctx, entry->player, sequence);
}
static char *bot_message(void *v, double slot) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  message_queue *queue = find_messages(game, slot);
  if (!queue || queue->count == 0) return NULL;
  char *message = queue->items[queue->head];
  queue->items[queue->head] = NULL;
  queue->head = (queue->head + 1) % 64; queue->count--;
  return message; /* ownership moves to the bot syscall */
}
static void bot_user_command(void *v, double slot, qa_qvmx_wire_user_command wire) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  qa_q3_base_user_command command;
  memset(&command, 0, sizeof command);
  command.server_time = wire.server_time;
  command.angles.x = wire.angles[0]; command.angles.y = wire.angles[1]; command.angles.z = wire.angles[2];
  command.buttons = wire.buttons; command.weapon = wire.weapon;
  command.forwardmove = wire.forwardmove; command.rightmove = wire.rightmove; command.upmove = wire.upmove;
  if (!in_slots(game->bot_clients, game->bot_client_count, slot))
    qa_raise("Error", 0, 0, "Bot input has no owned client %s", QA_JS_NUMBER(slot));
  client_entry *entry = find_client(game, slot);
  if (!entry) qa_raise("Error", 0, 0, "Bot input lost its source client");
  admit_pending_bots(game);
  if (game->options.bot_command) game->options.bot_command(game->options.ctx, entry->player.actor, command);
  qa_app_q3_server_state_set_user_command(game->options.state, slot, command);
  game->engine->ops->client_think(game->engine, slot);
}
bool qa_app_q3_qvm_server_game_is_retired(const qa_app_q3_qvm_server_game *game) { return game->lifecycle == LC_RETIRED; }
const qa_guest_checkpoint *qa_app_q3_qvm_server_game_checkpoint(qa_app_q3_qvm_server_game *game) {
  return game->engine->ops->checkpoint(game->engine);
}
/* captureHost (guest-runtime.ts 251-262). */
static qa_save_value *capture_host_value(qa_app_q3_qvm_server_game *game) {
  running(game);
  bool busy = game->external_operations != 0 || module_active(game) || game->reconnecting_count != 0 || game->retirement_count != 0;
  for (size_t i = 0; i < game->client_count; i++)
    if (game->clients[i].phase != PH_CONNECTED && game->clients[i].phase != PH_ACTIVE) busy = true;
  if (busy) qa_range_error("Q3 guest checkpoint requires completed client operations");
  qa_save_value *root = qa_save_value_object();
  qa_qvm_game_data_state data = qa_qvm_game_data_checkpoint(game->engine->data);
  qa_save_value *data_value = qa_save_value_object();
  qa_save_value *server = qa_app_q3_server_state_capture_save_state(game->options.state);
  qa_save_value *parser = qa_common_parse_state_capture(&game->parser);
  qa_save_value *bots = game->has_bots ? qa_app_q3_guest_bots_checkpoint(game->bots) : qa_save_value_null();
  qa_save_value *bot_clients = qa_save_value_array(), *messages = qa_save_value_array(), *clients = qa_save_value_array();
  qa_save_value *records = qa_app_q3_guest_records_capture_checkpoint(game->records);
  qa_save_value *portals = qa_app_q3_guest_world_capture_portal_checkpoint(game->guest_world);
  qa_save_value *files = qa_qvm_files_checkpoint_value(game->files);
  bool ok = root && data_value && server && records && portals && files && bots
      && qa_save_value_set(data_value, "entitiesWord", qa_save_value_number(data.entities_word))
      && qa_save_value_set(data_value, "numEntities", qa_save_value_number(data.num_entities))
      && qa_save_value_set(data_value, "entityStride", qa_save_value_number(data.entity_stride))
      && qa_save_value_set(data_value, "clientsWord", qa_save_value_number(data.clients_word))
      && qa_save_value_set(data_value, "clientStride", qa_save_value_number(data.client_stride))
      && qa_save_value_set(root, "version", qa_save_value_number(1))
      && qa_save_value_set(root, "maxClients", qa_save_value_number(qa_qvm_game_data_get_num_clients(game->engine->data)))
      && qa_save_value_set(root, "data", data_value)
      && qa_save_value_set(root, "server", server)
      && qa_save_value_set(root, "entityText", qa_save_value_string(game->cursor.source))
      && qa_save_value_set(root, "cursor", game->cursor.has_offset ? qa_save_value_number((double)game->cursor.offset) : qa_save_value_null())
      && qa_save_value_set(root, "parser", parser)
      && qa_save_value_set(root, "bots", bots);
  for (size_t i = 0; ok && i < game->bot_client_count; i++) ok = qa_save_value_push(bot_clients, qa_save_value_number(game->bot_clients[i]));
  for (size_t i = 0; ok && i < game->bot_message_count; i++) {
    qa_save_value *entry = qa_save_value_object(), *texts = qa_save_value_array();
    message_queue *queue = &game->bot_messages[i];
    for (size_t j = 0; ok && j < queue->count; j++) ok = qa_save_value_push(texts, qa_save_value_string(queue->items[(queue->head + j) % 64]));
    ok = ok && entry && qa_save_value_set(entry, "slot", qa_save_value_number((double)game->bot_message_slots[i]))
        && qa_save_value_set(entry, "messages", texts) && qa_save_value_push(messages, entry);
    if (!ok) qa_save_value_free(entry);
  }
  for (size_t i = 0; ok && i < game->client_count; i++) {
    client_entry *entry = &game->clients[i];
    qa_save_value *saved = qa_save_value_object(), *client = qa_save_value_object(), *actor = qa_save_value_object();
    qa_saved_actor_id saved_id = qa_saved_actor_id_of(entry->player.actor);
    ok = saved && client && actor
        && qa_save_value_set(client, "slot", qa_save_value_number(entry->player.client.slot))
        && qa_save_value_set(client, "generation", qa_save_value_number((double)entry->player.client.generation))
        && qa_save_value_set(actor, "slot", qa_save_value_number(saved_id.slot))
        && qa_save_value_set(actor, "generation", qa_save_value_number(saved_id.generation))
        && qa_save_value_set(saved, "sourceEntity", qa_save_value_number(entry->player.source_entity))
        && qa_save_value_set(saved, "client", client)
        && qa_save_value_set(saved, "actor", actor)
        && qa_save_value_set(saved, "phase", qa_save_value_string(entry->phase == PH_ACTIVE ? "active" : "connected"))
        && qa_save_value_push(clients, saved);
    if (!ok) qa_save_value_free(saved);
  }
  ok = ok && qa_save_value_set(root, "botClients", bot_clients)
      && qa_save_value_set(root, "botMessages", messages)
      && qa_save_value_set(root, "records", records)
      && qa_save_value_set(root, "portals", portals)
      && qa_save_value_set(root, "files", files)
      && qa_save_value_set(root, "clients", clients);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}
static qa_save_reader c_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
typedef struct client_row { double source_entity; qa_app_q3_saved_guest_client_id client; qa_saved_actor_id actor; const char *phase; } client_row;
static client_row *read_clients(const qa_save_reader *reader, size_t *out_count) {
  size_t count = qa_save_reader_list_count(reader);
  client_row *rows = calloc(count ? count : 1, sizeof *rows);
  if (!rows) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(reader, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader r = c_field(&entry, "sourceEntity"); rows[i].source_entity = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    qa_save_reader client = c_field(&entry, "client");
    r = c_field(&client, "slot"); rows[i].client.slot = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    r = c_field(&client, "generation"); rows[i].client.generation = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
    qa_save_reader_destroy(&client);
    r = c_field(&entry, "actor"); rows[i].actor = qa_read_saved_actor(&r); qa_save_reader_destroy(&r);
    r = c_field(&entry, "phase");
    const char *choices[2] = {"connected", "active"};
    rows[i].phase = qa_save_reader_choice_string(&r, choices, 2);
    qa_save_reader_destroy(&r);
    qa_save_reader_destroy(&entry);
  }
  *out_count = count;
  return rows;
}
qa_app_q3_saved_guest_client *qa_app_saved_q3_guest_clients(const uint8_t *host_bytes, size_t host_length,
    const char *host_format, size_t *out_count) {
  if (!host_format || strcmp(host_format, "q3:qagame-host") != 0)
    qa_range_error("Unsupported Q3 guest host checkpoint format");
  qa_save_value *decoded = qa_decode_checkpoint_value(host_bytes, host_length);
  if (!decoded) qa_range_error("Unsupported Q3 guest host checkpoint format");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "q3.guest.host")) qa_range_error("Out of memory");
  qa_save_reader version = c_field(&reader, "version");
  qa_save_reader_literal_number(&version, 1); qa_save_reader_destroy(&version);
  qa_save_reader maximum_reader = c_field(&reader, "maxClients");
  double maximum = qa_save_reader_integer_min(&maximum_reader, 1);
  qa_save_reader_destroy(&maximum_reader);
  if (maximum > 64) qa_save_reader_fail(&reader, "invalid guest client capacity");
  qa_save_reader clients_reader = c_field(&reader, "clients");
  size_t count = 0;
  client_row *rows = read_clients(&clients_reader, &count);
  qa_save_reader_destroy(&clients_reader);
  double slots[64]; size_t slot_count = 0;
  for (size_t i = 0; i < count; i++) {
    if (rows[i].source_entity >= maximum || rows[i].source_entity != rows[i].client.slot || in_slots(slots, slot_count, rows[i].source_entity))
      qa_save_reader_fail(&reader, "invalid or duplicate source client slot");
    slots[slot_count++] = rows[i].source_entity;
  }
  qa_save_reader_destroy(&reader);
  qa_app_q3_saved_guest_client *out = calloc(count ? count : 1, sizeof *out);
  if (!out) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) {
    out[i].source_entity = rows[i].source_entity; out[i].client = rows[i].client;
    out[i].actor = rows[i].actor; out[i].phase = rows[i].phase;
  }
  free(rows);
  *out_count = count;
  (void)decoded;
  return out;
}
static qa_qvm_host_checkpoint host_checkpoint(void *ctx) {
  qa_app_q3_qvm_server_game *game = ctx;
  qa_save_value *value = capture_host_value(game);
  if (!value) qa_range_error("Out of memory");
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(value, &length);
  qa_save_value_free(value);
  if (!bytes) qa_range_error("Out of memory");
  free(game->host_bytes); game->host_bytes = bytes;
  qa_qvm_host_checkpoint out; memset(&out, 0, sizeof out);
  out.state.module = game->engine->module; out.state.format = "q3:qagame-host";
  out.state.bytes.bytes = bytes; out.state.bytes.length = length;
  return out;
}
typedef struct restore_step { qa_app_q3_qvm_server_game *game; const qa_guest_checkpoint *checkpoint; } restore_step;
static void restore_engine(void *arg) {
  restore_step *step = arg;
  step->game->engine->ops->restore(step->game->engine, step->checkpoint);
  step->game->lifecycle = LC_RESTORED;
}
static void discard_step(void *arg) { qa_app_q3_qvm_server_game_discard(arg); }
void qa_app_q3_qvm_server_game_restore_checkpoint(qa_app_q3_qvm_server_game *game, const qa_guest_checkpoint *checkpoint,
    qa_client_id (*resolve_client)(void *ctx, qa_app_q3_saved_guest_client_id saved), void *resolve_ctx) {
  current(game);
  if (game->lifecycle != LC_CREATED) qa_range_error("Q3 guest restore requires an inert candidate");
  game->lifecycle = LC_RESTORING; game->resolve_client = resolve_client; game->resolve_ctx = resolve_ctx;
  restore_step step = {game, checkpoint};
  qa_caught_error error;
  if (!qa_try(restore_engine, &step, &error)) {
    game->resolve_client = NULL;
    qa_caught_error cleanup;
    if (!qa_try(discard_step, game, &cleanup))
      qa_raise("AggregateError", 0, 0, "Q3 guest restore and discard failed: %s; %s", error.message, cleanup.message);
    qa_rethrow(&error);
  }
  game->resolve_client = NULL;
}
static void restore_host_bytes(qa_app_q3_qvm_server_game *game, const uint8_t *bytes, size_t length,
    const char *format, size_t callback_count, size_t random_count);
/* restoreHost (guest-runtime.ts 276-317). */
static void host_restore(void *ctx, const qa_qvm_host_checkpoint *checkpoint) {
  restore_host_bytes(ctx, checkpoint->state.bytes.bytes, checkpoint->state.bytes.length, checkpoint->state.format,
      checkpoint->callback_count, checkpoint->random_count);
}
static void restore_host_bytes(qa_app_q3_qvm_server_game *game, const uint8_t *bytes, size_t length,
    const char *format, size_t callback_count, size_t random_count) {
  if (game->lifecycle != LC_RESTORING || !game->resolve_client)
    qa_range_error("Q3 guest host restore requires the candidate restore operation");
  if (!format || strcmp(format, "q3:qagame-host") != 0 || callback_count != 0 || random_count != 0)
    qa_range_error("Unsupported Q3 guest host checkpoint format, callbacks or external random streams");
  if (game->options.source_restored) game->options.source_restored(game->options.ctx);
  qa_save_value *decoded = qa_decode_checkpoint_value(bytes, length);
  if (!decoded) qa_range_error("Unsupported Q3 guest host checkpoint format, callbacks or external random streams");
  qa_save_reader reader;
  if (!qa_save_reader_init(&reader, decoded, "q3.guest.host")) qa_range_error("Out of memory");
  qa_save_reader r = c_field(&reader, "version"); qa_save_reader_literal_number(&r, 1); qa_save_reader_destroy(&r);
  r = c_field(&reader, "maxClients"); qa_save_reader_literal_number(&r, game->options.max_clients); qa_save_reader_destroy(&r);
  r = c_field(&reader, "entityText"); qa_save_reader_literal_string(&r, game->cursor.source); qa_save_reader_destroy(&r);
  qa_save_reader data_reader = c_field(&reader, "data");
  qa_qvm_game_data_state data;
  r = c_field(&data_reader, "entitiesWord"); data.entities_word = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "numEntities"); data.num_entities = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "entityStride"); data.entity_stride = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "clientsWord"); data.clients_word = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "clientStride"); data.client_stride = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  qa_save_reader_destroy(&data_reader);
  qa_qvm_game_data_set_client_count(game->engine->data, game->options.max_clients);
  qa_qvm_game_data_restore(game->engine->data, data);
  r = c_field(&reader, "server"); qa_app_q3_server_state_restore_save_state(game->options.state, r.value); qa_save_reader_destroy(&r);
  current(game);
  r = c_field(&reader, "cursor");
  if (qa_save_reader_is_null(&r)) qa_common_parse_cursor_set_null(&game->cursor);
  else qa_common_parse_cursor_set_offset(&game->cursor, (size_t)qa_save_reader_integer_min(&r, 0));
  qa_save_reader_destroy(&r);
  r = c_field(&reader, "parser"); qa_common_parse_state_restore(&game->parser, r.value); qa_save_reader_destroy(&r);
  r = c_field(&reader, "portals"); qa_app_q3_guest_world_restore_portal_checkpoint(game->guest_world, r.value); qa_save_reader_destroy(&r);
  r = c_field(&reader, "records"); qa_app_q3_guest_records_restore_checkpoint(game->records, r.value); qa_save_reader_destroy(&r);
  r = c_field(&reader, "clients");
  size_t client_count = 0;
  client_row *clients = read_clients(&r, &client_count);
  qa_save_reader_destroy(&r);
  for (size_t i = 0; i < client_count; i++) {
    qa_owned_actor actor;
    bool resolved = qa_session_actor_registry_resolve_saved(game->options.records.actors, clients[i].actor, &actor);
    qa_client_id client = game->resolve_client(game->resolve_ctx, clients[i].client);
    double slot;
    if (clients[i].source_entity >= game->options.max_clients || clients[i].source_entity != clients[i].client.slot
        || find_client(game, clients[i].source_entity) || (double)client.slot != clients[i].source_entity || !resolved
        || !qa_app_q3_guest_records_slot(game->records, actor.id, &slot) || slot != clients[i].source_entity)
      qa_save_reader_fail(&reader, "invalid restored guest client binding");
    client_entry *entry = &game->clients[game->client_count++];
    memset(entry, 0, sizeof *entry);
    entry->player.client = client; entry->player.actor = actor.id; entry->player.source_entity = clients[i].source_entity;
    entry->phase = strcmp(clients[i].phase, "active") == 0 ? PH_ACTIVE : PH_CONNECTED;
  }
  free(clients);
  r = c_field(&reader, "files"); qa_qvm_files_restore_checkpoint(game->files, r.value); qa_save_reader_destroy(&r);
  r = c_field(&reader, "bots");
  game->saved_bots = qa_save_reader_is_null(&r) ? NULL : r.value;
  qa_save_reader_destroy(&r);
  r = c_field(&reader, "botClients");
  if (!qa_save_reader_is_undefined(&r)) {
    size_t count = qa_save_reader_list_count(&r);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader item;
      if (!qa_save_reader_item(&r, i, &item)) qa_range_error("Out of memory");
      double slot = qa_save_reader_integer_min(&item, 0);
      qa_save_reader_destroy(&item);
      if (!find_client(game, slot) || in_slots(game->bot_clients, game->bot_client_count, slot))
        qa_range_error("Invalid saved guest bot slot");
      slot_add(game->bot_clients, &game->bot_client_count, slot);
    }
  }
  qa_save_reader_destroy(&r);
  r = c_field(&reader, "botMessages");
  if (!qa_save_reader_is_undefined(&r)) {
    size_t count = qa_save_reader_list_count(&r);
    for (size_t i = 0; i < count; i++) {
      qa_save_reader entry;
      if (!qa_save_reader_item(&r, i, &entry)) qa_range_error("Out of memory");
      qa_save_reader slot_reader = c_field(&entry, "slot");
      double slot = qa_save_reader_integer_min(&slot_reader, 0);
      qa_save_reader_destroy(&slot_reader);
      if (!in_slots(game->bot_clients, game->bot_client_count, slot) || find_messages(game, slot))
        qa_range_error("Invalid saved guest bot message owner");
      message_queue *queue = add_messages(game, slot);
      qa_save_reader texts = c_field(&entry, "messages");
      size_t texts_count = qa_save_reader_list_count(&texts);
      for (size_t j = 0; j < texts_count; j++) {
        qa_save_reader text;
        if (!qa_save_reader_item(&texts, j, &text)) qa_range_error("Out of memory");
        queue_push(queue, qa_save_reader_string(&text));
        qa_save_reader_destroy(&text);
      }
      qa_save_reader_destroy(&texts);
      qa_save_reader_destroy(&entry);
    }
  }
  qa_save_reader_destroy(&r);
  qa_save_reader_destroy(&reader);
}
void qa_app_q3_qvm_server_game_complete_restore(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_output output) {
  current(game);
  if (game->lifecycle != LC_RESTORED) qa_range_error("Q3 guest restore is not ready for output attachment");
  if (game->saved_bots) qa_range_error("Saved guest bot services must be restored before output attachment");
  game->output = output; game->has_output = true; game->lifecycle = LC_RUNNING;
}
static void release_source(qa_app_q3_qvm_server_game *game, bool transfer_clients) {
  if (game->has_bots) qa_app_q3_guest_bots_close(game->bots);
  qa_qvm_files_close_all(game->files);
  if (game->options.before_retire) game->options.before_retire(game->options.ctx);
  qa_app_q3_guest_records_close(game->records);
  if (!transfer_clients) {
    for (size_t i = 0; i < game->bot_client_count; i++) {
      client_entry *entry = find_client(game, game->bot_clients[i]);
      if (entry && game->has_bots) game->bot_preparation.free_client(game->bot_preparation.ctx, entry->player.client);
    }
  }
  game->bot_client_count = 0; game->bot_message_count = 0;
  for (size_t i = 0; i < game->client_count; i++) qa_app_q3_server_state_clear_client(game->options.state, game->clients[i].player.source_entity);
  for (size_t i = 0; i < game->reconnecting_count; i++) qa_app_q3_server_state_clear_client(game->options.state, (double)game->reconnecting[i].client.slot);
  game->engine->ops->retire(game->engine);
  game->client_count = 0; game->reconnecting_count = 0; game->lifecycle = LC_RETIRED;
}
void qa_app_q3_qvm_server_game_discard(qa_app_q3_qvm_server_game *game) {
  if (game->lifecycle == LC_RETIRED) return;
  if (game->external_operations != 0 || module_active(game))
    qa_range_error("Q3 guest discard must await the active operation");
  close_input(game);
  game->retirement_count = 0;
  if (game->has_bots) qa_app_q3_guest_bots_close(game->bots);
  qa_qvm_files_close_all(game->files);
  if (game->options.before_retire) game->options.before_retire(game->options.ctx);
  qa_app_q3_guest_records_close(game->records);
  game->engine->ops->retire(game->engine);
  game->client_count = 0; game->reconnecting_count = 0; game->lifecycle = LC_RETIRED;
}
double qa_app_q3_qvm_server_game_time_milliseconds(qa_app_q3_qvm_server_game *game) {
  current(game); return game->options.now(game->options.ctx);
}
void qa_app_q3_qvm_server_game_set_configstring(qa_app_q3_qvm_server_game *game, double index, const char *value) {
  current(game);
  /* this.state.configstrings.get(index) === value (an unset index reads ""). */
  const char *previous = qa_app_q3_server_state_configstring_get(game->options.state, index);
  if (strcmp(previous ? previous : "", value) == 0) return;
  qa_app_q3_server_state_configstring_set(game->options.state, index, value);
  output_of(game).configstring(game->output.ctx, index, value);
}
static void refresh_server_info(qa_app_q3_qvm_server_game *game) {
  current(game);
  const char *value = qa_app_q3_server_state_refresh_server_info(game->options.state);
  if (value) output_of(game).configstring(game->output.ctx, 0, value);
  current(game);
}
void qa_app_q3_qvm_server_game_initialize(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_output output,
    const char *start_kind, const qa_app_q3_guest_map_client *clients, size_t client_count) {
  current(game);
  if (game->lifecycle != LC_CREATED) qa_range_error("Q3 guest has already initialized");
  bool is_new = !start_kind || strcmp(start_kind, "new") == 0;
  if (!is_new) {
    double slots[64]; size_t slot_count = 0;
    for (size_t i = 0; i < client_count; i++) {
      validate_client(game, clients[i].client);
      if (in_slots(slots, slot_count, (double)clients[i].client.slot)) qa_range_error("Q3 map transition repeats a client slot");
      slots[slot_count++] = (double)clients[i].client.slot;
    }
    for (size_t i = 0; i < client_count; i++) {
      game->reconnecting[game->reconnecting_count++] = clients[i];
      qa_app_q3_server_state_set_userinfo(game->options.state, (double)clients[i].client.slot, clients[i].userinfo);
      if (clients[i].bot) {
        slot_add(game->bot_clients, &game->bot_client_count, (double)clients[i].client.slot);
        add_messages(game, (double)clients[i].client.slot);
      }
    }
  }
  if (!game->has_bots) {
    double enabled = qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(game->options.state), "bot_enable");
    if (enabled != 0) qa_range_error("Q3 guest bot services must be attached before initialization");
  }
  game->output = output; game->has_output = true; game->lifecycle = LC_INITIALIZING;
  double level_time = game->options.now(game->options.ctx);
  game->engine->ops->initialize(game->engine, level_time, game->options.seed, start_kind && strcmp(start_kind, "map-restart") == 0);
  refresh_server_info(game);
  current(game); game->lifecycle = LC_RUNNING; drain_bot_commands(game);
}
static qa_app_q3_admission admit(qa_app_q3_qvm_server_game *game, qa_client_id client, const char *userinfo, bool first_time, bool is_bot) {
  game->external_operations++;
  qa_app_q3_admission result; memset(&result, 0, sizeof result);
  running(game);
  qa_app_q3_player existing;
  if (qa_app_q3_qvm_server_game_player(game, client, &existing)) { finish_operation(game); qa_range_error("Q3 client already has a guest slot"); }
  double slot = (double)client.slot;
  validate_client(game, client);
  bool retiring = false;
  for (size_t i = 0; i < game->retirement_count; i++) if (game->retirements[i].slot == slot) retiring = true;
  if (find_client(game, slot) || retiring) {
    result.accepted = false; result.reason = "Client slot is already occupied.";
    finish_operation(game); return result;
  }
  qa_owned_actor actor = qa_app_q3_guest_records_actor(game->records, slot);
  qa_app_q3_player player = {client, actor.id, slot};
  client_entry *entry = &game->clients[game->client_count++];
  memset(entry, 0, sizeof *entry); entry->player = player; entry->phase = PH_CONNECTING;
  qa_app_q3_server_state_set_userinfo(game->options.state, slot, userinfo);
  free(game->denied);
  game->denied = game->engine->ops->client_connect(game->engine, slot, first_time, is_bot);
  const char *denied = game->denied;
  current(game);
  if (find_client(game, slot) != entry || entry->phase == PH_DROPPING) {
    result.accepted = false;
    if (entry->phase == PH_DROPPING) {
      static char reasons[64][256]; size_t cursor = 0;
      strncpy(reasons[cursor % 64], entry->drop_reason, 255);
      result.reason = reasons[cursor++ % 64];
    } else result.reason = "Client disconnected during admission.";
    finish_operation(game); return result;
  }
  if (denied) {
    if (first_time) release_entry(game, entry);
    else qa_app_q3_qvm_server_game_disconnect(game, player);
    result.accepted = false; result.reason = denied;
    finish_operation(game); return result;
  }
  entry->phase = PH_CONNECTED;
  result.accepted = true; result.player = player;
  finish_operation(game);
  return result;
}
qa_app_q3_admission qa_app_q3_qvm_server_game_connect(qa_app_q3_qvm_server_game *game, qa_client_id client, const char *userinfo) {
  for (size_t i = 0; i < game->reconnecting_count; i++)
    if (game->reconnecting[i].client.slot == client.slot) qa_range_error("Q3 carried client requires map reconnection");
  return admit(game, client, userinfo, true, false);
}
qa_app_q3_admission qa_app_q3_qvm_server_game_reconnect(qa_app_q3_qvm_server_game *game, qa_client_id client) {
  running(game);
  size_t index = game->reconnecting_count;
  for (size_t i = 0; i < game->reconnecting_count; i++) if (game->reconnecting[i].client.slot == client.slot) index = i;
  if (index >= game->reconnecting_count || !client_eq(game->reconnecting[index].client, client))
    qa_range_error("Q3 client is not carried by this map transition");
  qa_app_q3_guest_map_client previous = game->reconnecting[index];
  const char *userinfo = qa_app_q3_server_state_get_userinfo(game->options.state, (double)client.slot);
  qa_app_q3_admission result = admit(game, client, userinfo ? userinfo : previous.userinfo, false, previous.bot);
  for (size_t i = 0; i < game->reconnecting_count; i++) {
    if (game->reconnecting[i].client.slot == client.slot) {
      memmove(game->reconnecting + i, game->reconnecting + i + 1, (game->reconnecting_count - i - 1) * sizeof *game->reconnecting);
      game->reconnecting_count--; break;
    }
  }
  return result;
}
void qa_app_q3_qvm_server_game_begin(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, qa_q3_base_user_command command) {
  game->external_operations++;
  running(game); client_entry *entry = entry_of(game, player);
  if (entry->phase != PH_CONNECTED) { finish_operation(game); qa_range_error("Q3 guest client cannot begin in this phase"); }
  qa_app_q3_server_state_set_user_command(game->options.state, player.source_entity, command);
  game->engine->ops->client_begin(game->engine, player.source_entity);
  current(game);
  if (find_client(game, player.source_entity) == entry && entry->phase == PH_CONNECTED) {
    entry->phase = PH_ACTIVE;
    if (game->options.client_changed) game->options.client_changed(game->options.ctx, "admitted", player.actor);
  }
  finish_operation(game);
}
void qa_app_q3_qvm_server_game_think(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, qa_q3_base_user_command command) {
  game->external_operations++;
  running(game);
  if (entry_of(game, player)->phase != PH_ACTIVE) { finish_operation(game); qa_range_error("Q3 guest client is not active"); }
  qa_app_q3_server_state_set_user_command(game->options.state, player.source_entity, command);
  game->engine->ops->client_think(game->engine, player.source_entity);
  current(game);
  finish_operation(game);
}
void qa_app_q3_qvm_server_game_userinfo(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *value) {
  game->external_operations++;
  running(game); (void)entry_of(game, player);
  qa_app_q3_server_state_set_userinfo(game->options.state, player.source_entity, value);
  game->engine->ops->client_userinfo_changed(game->engine, player.source_entity);
  current(game);
  if (game->options.client_changed) game->options.client_changed(game->options.ctx, "userinfo", player.actor);
  finish_operation(game);
}
void qa_app_q3_qvm_server_game_command_immediate(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *const *argv, size_t argc) {
  game->external_operations++;
  running(game); (void)entry_of(game, player);
  game->engine->ops->client_command(game->engine, player.source_entity, argv, argc);
  current(game);
  finish_operation(game);
}
void qa_app_q3_qvm_server_game_command(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *const *argv, size_t argc) {
  qa_app_q3_qvm_server_game_command_immediate(game, player, argv, argc);
}
void qa_app_q3_qvm_server_game_drop(qa_app_q3_qvm_server_game *game, double slot, const char *reason) {
  for (size_t i = 0; i < game->reconnecting_count; i++) {
    if ((double)game->reconnecting[i].client.slot == slot) {
      memmove(game->reconnecting + i, game->reconnecting + i + 1, (game->reconnecting_count - i - 1) * sizeof *game->reconnecting);
      game->reconnecting_count--; break;
    }
  }
  client_entry *entry = find_client(game, slot);
  if (entry) { notify_disconnect(game, entry); entry->phase = PH_DROPPING; strncpy(entry->drop_reason, reason, 255); }
  if (!in_slots(game->bot_clients, game->bot_client_count, slot)) output_of(game).drop_client(game->output.ctx, slot, reason);
  current(game);
  if (entry && find_client(game, slot) == entry) qa_app_q3_qvm_server_game_disconnect(game, entry->player);
}
void qa_app_q3_qvm_server_game_disconnect(qa_app_q3_qvm_server_game *game, qa_app_q3_player player) {
  game->external_operations++;
  current(game);
  if (!(game->lifecycle == LC_INITIALIZING || game->lifecycle == LC_RUNNING || game->lifecycle == LC_SHUTTING_DOWN)) {
    finish_operation(game); qa_range_error("Q3 guest client disconnect requires attached output");
  }
  client_entry *entry = find_client(game, player.source_entity);
  if (!entry || !client_eq(entry->player.client, player.client)) { finish_operation(game); return; }
  notify_disconnect(game, entry);
  if (entry->phase != PH_DROPPING) { entry->phase = PH_DROPPING; strncpy(entry->drop_reason, "Client disconnected.", 255); }
  qa_app_q3_player saved_player = entry->player;
  remove_client_entry(game, entry);
  game->engine->ops->client_disconnect(game->engine, player.source_entity);
  current(game);
  qa_app_q3_server_state_clear_client(game->options.state, player.source_entity);
  qa_app_q3_guest_records_release_client(game->records, player.source_entity);
  if (slot_remove(game->bot_clients, &game->bot_client_count, player.source_entity) && game->has_bots)
    game->bot_preparation.free_client(game->bot_preparation.ctx, saved_player.client);
  message_queue *queue = find_messages(game, player.source_entity);
  if (queue) { for (size_t i = 0; i < queue->count; i++) free(queue->items[(queue->head + i) % 64]); queue->count = 0; }
  finish_operation(game);
}
bool qa_app_q3_qvm_server_game_console_command(qa_app_q3_qvm_server_game *game, const char *const *argv, size_t argc) {
  game->external_operations++;
  running(game);
  bool handled = game->engine->ops->console_command(game->engine, argv, argc);
  current(game); drain_bot_commands(game);
  finish_operation(game);
  return handled;
}
void qa_app_q3_qvm_server_game_run_frame(qa_app_q3_qvm_server_game *game, double time_milliseconds) {
  game->external_operations++;
  running(game);
  if (game->has_bots) {
    bool enabled = qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(game->options.state), "bot_enable") != 0;
    if (enabled) {
      game->engine->ops->bot_frame(game->engine, time_milliseconds);
    }
  }
  game->engine->ops->run_frame(game->engine, time_milliseconds);
  refresh_server_info(game);
  current(game); drain_bot_commands(game);
  finish_operation(game);
}
size_t qa_app_q3_qvm_server_game_players(qa_app_q3_qvm_server_game *game, qa_app_q3_player *out, size_t capacity) {
  size_t count = 0;
  for (size_t i = 0; i < game->client_count; i++) {
    if (game->clients[i].phase == PH_DROPPING) continue;
    bool retiring = false;
    for (size_t j = 0; j < game->retirement_count; j++)
      if (game->retirements[j].slot == game->clients[i].player.source_entity) retiring = true;
    if (retiring) continue;
    if (count < capacity) out[count] = game->clients[i].player;
    count++;
  }
  return count;
}
bool qa_app_q3_qvm_server_game_player(qa_app_q3_qvm_server_game *game, qa_client_id client, qa_app_q3_player *out) {
  qa_app_q3_player players[64];
  size_t count = qa_app_q3_qvm_server_game_players(game, players, 64);
  for (size_t i = 0; i < count; i++) if (client_eq(players[i].client, client)) { *out = players[i]; return true; }
  return false;
}
bool qa_app_q3_qvm_server_game_is_bot(qa_app_q3_qvm_server_game *game, qa_client_id client) {
  qa_app_q3_player player;
  return in_slots(game->bot_clients, game->bot_client_count, (double)client.slot)
      && qa_app_q3_qvm_server_game_player(game, client, &player);
}
void qa_app_q3_qvm_server_game_shutdown(qa_app_q3_qvm_server_game *game) {
  if (game->lifecycle == LC_RETIRED) return;
  if (game->external_operations != 0 || module_active(game))
    qa_range_error("Q3 guest shutdown must await the active operation");
  close_input(game);
  game->retirement_count = 0;
  if (game->lifecycle == LC_INITIALIZING || game->lifecycle == LC_RUNNING || game->lifecycle == LC_SHUTTING_DOWN) {
    qa_app_q3_guest_output output = game->output;
    (void)output; game->lifecycle = LC_SHUTTING_DOWN;
    game->engine->ops->shutdown(game->engine, false);
  }
  release_source(game, false);
}
qa_app_q3_guest_map_transition qa_app_q3_qvm_server_game_shutdown_for_map_change(qa_app_q3_qvm_server_game *game, bool restart) {
  running(game);
  bool busy = game->external_operations != 0 || module_active(game) || game->reconnecting_count != 0;
  for (size_t i = 0; i < game->client_count; i++)
    if (game->clients[i].phase != PH_CONNECTED && game->clients[i].phase != PH_ACTIVE) busy = true;
  if (busy) qa_range_error("Q3 map transition requires completed client operations");
  close_input(game);
  game->retirement_count = 0;
  game->lifecycle = LC_SHUTTING_DOWN;
  game->engine->ops->shutdown(game->engine, restart);
  qa_app_q3_guest_map_transition transition; memset(&transition, 0, sizeof transition);
  transition.cvars = qa_cvar_registry_capture_save_state(qa_app_q3_server_state_cvars(game->options.state));
  transition.clients = calloc(game->client_count ? game->client_count : 1, sizeof *transition.clients);
  if (!transition.clients) qa_range_error("Out of memory");
  for (size_t i = 0; i < game->client_count; i++) {
    if (game->clients[i].phase == PH_DROPPING) continue;
    transition.clients[transition.client_count].client = game->clients[i].player.client;
    const char *userinfo = qa_app_q3_server_state_get_userinfo(game->options.state, game->clients[i].player.source_entity);
    transition.clients[transition.client_count].userinfo = dup_str(userinfo ? userinfo : "");
    transition.clients[transition.client_count].bot = in_slots(game->bot_clients, game->bot_client_count, game->clients[i].player.source_entity);
    transition.client_count++;
  }
  release_source(game, true);
  return transition;
}
void qa_app_q3_guest_map_transition_destroy(qa_app_q3_guest_map_transition *transition) {
  if (!transition) return;
  qa_save_value_free(transition->cvars);
  for (size_t i = 0; i < transition->client_count; i++) free((void *)transition->clients[i].userinfo);
  free(transition->clients);
  memset(transition, 0, sizeof *transition);
}
