/* Port of src/app/bootstrap/simulation/q3/guest-runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/guest_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/guest_world.h"
#include "qa/compat/qvm/abi.h"
#include "qa/foundation/support.h"
/* checkpoint value helpers are in persistence/value.h */
#include "qa/persistence/save_image.h"

typedef enum lifecycle_kind { LC_CREATED, LC_RESTORING, LC_RESTORED, LC_INITIALIZING, LC_RUNNING, LC_SHUTTING_DOWN, LC_RETIRED } lifecycle_kind;
typedef enum phase_kind { PH_CONNECTING, PH_CONNECTED, PH_ACTIVE, PH_DROPPING } phase_kind;
typedef struct client_entry {
  qa_app_q3_player player;
  bool disconnect_notified;
  phase_kind phase;
  char drop_reason[256];
} client_entry;
typedef struct message_queue { char *items[64]; size_t head, count; } message_queue;
typedef struct pending_command { double slot; char *argv[16]; size_t argc; } pending_command;
typedef struct retirement { double slot; qa_app_q3_mod_client_identity identity; client_entry *entry; int phase; } retirement;
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
  pending_command pending_commands[64]; size_t pending_command_count;
  double pending_admissions[64]; size_t pending_admission_count;
  int64_t bot_message_slots[64]; message_queue bot_messages[64]; size_t bot_message_count;
  lifecycle_kind lifecycle;
  qa_app_q3_guest_output output;
  bool has_output;
  double external_operations;
  client_entry clients[64]; size_t client_count;
  qa_app_q3_guest_map_client reconnecting[64]; size_t reconnecting_count;
  retirement retirements[64]; size_t retirement_count;
  double cursor_offset;
  bool cursor_has_offset;
  qa_client_id (*resolve_client)(void *ctx, qa_app_q3_saved_guest_client_id saved);
  void *resolve_ctx;
  bool in_syscall;
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
  if (game->options.module.is_active(game->options.module.ctx)) qa_range_error("Q3 guest already has an active external operation");
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
  if (game->external_operations != 0 || game->options.module.is_active(game->options.module.ctx)) return;
  for (size_t i = 0; i < game->retirement_count;) {
    retirement *item = &game->retirements[i];
    if (item->phase != 2) { i++; continue; }
    double slot = item->slot;
    if (item->entry && find_client(game, slot)) release_entry(game, find_client(game, slot));
    if (slot_remove(game->bot_clients, &game->bot_client_count, slot) && game->has_bots)
      game->bot_preparation.free_client(game->bot_preparation.ctx, item->identity.client);
    slot = item->slot;
    message_queue *queue = find_messages(game, slot);
    if (queue) { for (size_t j = 0; j < queue->count; j++) free(queue->items[(queue->head + j) % 64]); }
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
  if ((double)client.slot != (double)(long long)client.slot || client.slot < 0 || (double)client.slot >= game->options.max_clients
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
      game->options.module.client_command(game->options.module.ctx, pending.slot, (const char *const *)pending.argv, pending.argc);
    for (size_t i = 0; i < pending.argc; i++) free(pending.argv[i]);
    current(game);
  }
  admit_pending_bots(game);
}

qa_app_q3_qvm_server_game *qa_app_q3_qvm_server_game_create(qa_app_q3_qvm_server_game_options options) {
  if (options.max_clients != (double)(long long)options.max_clients || options.max_clients < 1 || options.max_clients > 64)
    qa_range_error("Q3 guest requires 1..64 clients");
  qa_app_q3_qvm_server_game *game = calloc(1, sizeof *game);
  if (!game) return NULL;
  game->options = options;
  qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(options.state);
  qa_cvar_registry_register(cvars, "bot_enable", "1", 0, NULL);
  char buffer[32];
  snprintf(buffer, sizeof buffer, "%lld", (long long)options.max_clients);
  qa_cvar_snapshot set_result; qa_cvar_registry_set(cvars, "sv_maxclients", buffer, true, &set_result);
  qa_cvar_registry_set(cvars, "dedicated", options.has_dedicated && !options.dedicated ? "0" : "1", true, &set_result);
  qa_qvm_game_data_set_client_count(options.data, options.max_clients);
  game->records = qa_app_q3_guest_records_create(options.data, options.records);
  game->guest_world = qa_app_q3_guest_world_create(options.records.scene);
  qa_app_q3_guest_spatial_dependencies spatial; memset(&spatial, 0, sizeof spatial);
  spatial.scene = options.records.scene; spatial.records = game->records; spatial.world = game->guest_world;
  spatial.ctx = options.records.ctx; spatial.collision = options.records.collision;
  game->spatial = qa_app_q3_guest_spatial_create(spatial);
  if (!game->records || !game->spatial) {
    if (options.files_close_all) options.files_close_all(options.files_ctx);
    options.module.retire(options.module.ctx);
    free(game);
    return NULL;
  }
  game->lifecycle = LC_CREATED;
  return game;
}
void qa_app_q3_qvm_server_game_destroy(qa_app_q3_qvm_server_game *game) {
  if (!game) return;
  qa_app_q3_guest_spatial_destroy(game->spatial);
  qa_app_q3_guest_records_destroy(game->records);
  qa_app_q3_guest_world_destroy(game->guest_world);
  qa_app_q3_guest_bots_destroy(game->bots);
  free(game);
}
qa_app_q3_server_state *qa_app_q3_qvm_server_game_state(qa_app_q3_qvm_server_game *game) { return game->options.state; }
qa_app_q3_guest_records *qa_app_q3_qvm_server_game_records(qa_app_q3_qvm_server_game *game) { return game->records; }
qa_app_q3_guest_spatial *qa_app_q3_qvm_server_game_spatial(qa_app_q3_qvm_server_game *game) { return game->spatial; }
bool qa_app_q3_qvm_server_game_player_arsenal(qa_app_q3_qvm_server_game *game, qa_actor_id actor, qa_arsenal_state *out) {
  current(game);
  return game->options.arsenal && game->options.arsenal(game->options.ctx, actor, out);
}
void qa_app_q3_qvm_server_game_bind_input(qa_app_q3_qvm_server_game *game) {
  /* TS replaces the input binding; retire/disconnect seams are the
   * explicit functions below in this port. */
  if (game->options.input_close) game->options.input_close(game->options.input_ctx);
}
static double bot_allocate(void *v);
static void bot_free(void *v, double slot);
static double bot_snapshot(void *v, double slot, double sequence);
static const char *bot_message(void *v, double slot);
static void bot_user_command(void *v, double slot, qa_q3_base_user_command command);
static const char *bot_client_command(void *v, double client, const char *text) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  if (!in_slots(game->bot_clients, game->bot_client_count, client))
    qa_range_error("Bot command has no owned client %g", client);
  if (game->pending_command_count < 64) {
    pending_command *pending = &game->pending_commands[game->pending_command_count++];
    memset(pending, 0, sizeof *pending);
    pending->slot = client;
    pending->argv[0] = dup_str(text); pending->argc = 1;
  }
  return NULL;
}
void qa_app_q3_qvm_server_game_attach_bots(qa_app_q3_qvm_server_game *game, qa_app_q3_guest_bot_preparation preparation) {
  current(game);
  if (game->has_bots || (game->lifecycle != LC_CREATED && game->lifecycle != LC_RESTORED))
    qa_range_error("Attach guest bot services before game initialization");
  game->bot_preparation = preparation;
  qa_app_q3_guest_bot_options options = preparation.bots;
  options.library.client_command = bot_client_command;
  /* library ctx for clientCommand routes through game via host_ctx */
  options.host_ctx = game;
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
  game->bots = qa_app_q3_guest_bots_create(options);
  free(map_name);
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
    (void)entity; /* s.number written through entity record by wiring */
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
static const char *bot_message(void *v, double slot) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  message_queue *queue = find_messages(game, slot);
  if (!queue || queue->count == 0) return NULL;
  char *message = queue->items[queue->head];
  queue->head = (queue->head + 1) % 64; queue->count--;
  /* leaked to caller lifetime like TS string (bounded by frame strings in tests) */
  return message;
}
static void bot_user_command(void *v, double slot, qa_q3_base_user_command command) {
  qa_app_q3_qvm_server_game *game = (qa_app_q3_qvm_server_game *)v;
  if (!in_slots(game->bot_clients, game->bot_client_count, slot))
    qa_range_error("Bot input has no owned client %g", slot);
  client_entry *entry = find_client(game, slot);
  if (!entry) qa_range_error("Bot input lost its source client");
  admit_pending_bots(game);
  if (game->options.bot_command) game->options.bot_command(game->options.ctx, entry->player.actor, command);
  qa_app_q3_server_state_set_user_command(game->options.state, slot, command);
  game->options.module.client_think(game->options.module.ctx, slot);
}
bool qa_app_q3_qvm_server_game_is_retired(const qa_app_q3_qvm_server_game *game) { return game->lifecycle == LC_RETIRED; }
void *qa_app_q3_qvm_server_game_checkpoint(qa_app_q3_qvm_server_game *game) {
  return game->options.module.checkpoint(game->options.module.ctx);
}
qa_save_value *qa_app_q3_qvm_server_game_capture_host(qa_app_q3_qvm_server_game *game) {
  running(game);
  bool busy = game->external_operations != 0 || game->reconnecting_count != 0 || game->retirement_count != 0;
  for (size_t i = 0; i < game->client_count; i++)
    if (game->clients[i].phase != PH_CONNECTED && game->clients[i].phase != PH_ACTIVE) busy = true;
  if (busy) qa_range_error("Q3 guest checkpoint requires completed client operations");
  qa_save_value *root = qa_save_value_object();
  qa_qvm_game_data_state data = qa_qvm_game_data_checkpoint(game->options.data);
  qa_save_value *data_value = qa_save_value_object();
  qa_save_value *server = qa_app_q3_server_state_capture_save_state(game->options.state);
  qa_save_value *parser = game->options.parser_capture ? game->options.parser_capture(game->options.files_ctx) : qa_save_value_null();
  qa_save_value *bots = game->has_bots ? qa_app_q3_guest_bots_checkpoint(game->bots) : qa_save_value_null();
  qa_save_value *bot_clients = qa_save_value_array(), *messages = qa_save_value_array(), *clients = qa_save_value_array();
  qa_save_value *records = qa_app_q3_guest_records_capture_checkpoint(game->records);
  qa_save_value *portals = qa_app_q3_guest_world_capture_portal_checkpoint(game->guest_world);
  qa_save_value *files = game->options.files_capture ? game->options.files_capture(game->options.files_ctx) : qa_save_value_null();
  bool ok = root && data_value && server && records && portals && files && bots
      && qa_save_value_set(data_value, "entitiesWord", qa_save_value_number(data.entities_word))
      && qa_save_value_set(data_value, "numEntities", qa_save_value_number(data.num_entities))
      && qa_save_value_set(data_value, "entityStride", qa_save_value_number(data.entity_stride))
      && qa_save_value_set(data_value, "clientsWord", qa_save_value_number(data.clients_word))
      && qa_save_value_set(data_value, "clientStride", qa_save_value_number(data.client_stride))
      && qa_save_value_set(root, "version", qa_save_value_number(1))
      && qa_save_value_set(root, "maxClients", qa_save_value_number(qa_qvm_game_data_get_num_clients(game->options.data)))
      && qa_save_value_set(root, "data", data_value)
      && qa_save_value_set(root, "server", server)
      && qa_save_value_set(root, "entityText", qa_save_value_string(game->options.entity_text))
      && qa_save_value_set(root, "cursor", game->cursor_has_offset ? qa_save_value_number(game->cursor_offset) : qa_save_value_null())
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
void qa_app_q3_qvm_server_game_restore_checkpoint(qa_app_q3_qvm_server_game *game, void *checkpoint,
    qa_client_id (*resolve_client)(void *ctx, qa_app_q3_saved_guest_client_id saved), void *resolve_ctx) {
  current(game);
  if (game->lifecycle != LC_CREATED) qa_range_error("Q3 guest restore requires an inert candidate");
  game->lifecycle = LC_RESTORING; game->resolve_client = resolve_client; game->resolve_ctx = resolve_ctx;
  game->options.module.restore(game->options.module.ctx, checkpoint);
  game->lifecycle = LC_RESTORED; game->resolve_client = NULL;
}
void qa_app_q3_qvm_server_game_restore_host(qa_app_q3_qvm_server_game *game, const uint8_t *bytes, size_t length,
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
  r = c_field(&reader, "entityText"); qa_save_reader_literal_string(&r, game->options.entity_text); qa_save_reader_destroy(&r);
  qa_save_reader data_reader = c_field(&reader, "data");
  qa_qvm_game_data_state data;
  r = c_field(&data_reader, "entitiesWord"); data.entities_word = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "numEntities"); data.num_entities = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "entityStride"); data.entity_stride = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "clientsWord"); data.clients_word = qa_save_reader_integer(&r); qa_save_reader_destroy(&r);
  r = c_field(&data_reader, "clientStride"); data.client_stride = qa_save_reader_integer_min(&r, 0); qa_save_reader_destroy(&r);
  qa_save_reader_destroy(&data_reader);
  qa_qvm_game_data_set_client_count(game->options.data, game->options.max_clients);
  qa_qvm_game_data_restore(game->options.data, data);
  r = c_field(&reader, "server"); qa_app_q3_server_state_restore_save_state(game->options.state, r.value); qa_save_reader_destroy(&r);
  current(game);
  r = c_field(&reader, "cursor");
  game->cursor_has_offset = !qa_save_reader_is_null(&r);
  game->cursor_offset = game->cursor_has_offset ? qa_save_reader_integer_min(&r, 0) : 0;
  qa_save_reader_destroy(&r);
  r = c_field(&reader, "parser"); if (game->options.parser_restore && !qa_save_reader_is_null(&r)) game->options.parser_restore(game->options.files_ctx, r.value); qa_save_reader_destroy(&r);
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
  r = c_field(&reader, "files"); if (game->options.files_restore && !qa_save_reader_is_null(&r)) game->options.files_restore(game->options.files_ctx, r.value); qa_save_reader_destroy(&r);
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
  if (game->options.files_close_all) game->options.files_close_all(game->options.files_ctx);
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
  game->options.module.retire(game->options.module.ctx);
  game->client_count = 0; game->reconnecting_count = 0; game->lifecycle = LC_RETIRED;
}
void qa_app_q3_qvm_server_game_discard(qa_app_q3_qvm_server_game *game) {
  if (game->lifecycle == LC_RETIRED) return;
  if (game->external_operations != 0 || game->options.module.is_active(game->options.module.ctx))
    qa_range_error("Q3 guest discard must await the active operation");
  if (game->options.input_close) game->options.input_close(game->options.input_ctx);
  game->retirement_count = 0;
  if (game->has_bots) qa_app_q3_guest_bots_close(game->bots);
  if (game->options.files_close_all) game->options.files_close_all(game->options.files_ctx);
  if (game->options.before_retire) game->options.before_retire(game->options.ctx);
  qa_app_q3_guest_records_close(game->records);
  game->options.module.retire(game->options.module.ctx);
  game->client_count = 0; game->reconnecting_count = 0; game->lifecycle = LC_RETIRED;
}
double qa_app_q3_qvm_server_game_time_milliseconds(qa_app_q3_qvm_server_game *game) {
  current(game); return game->options.now(game->options.ctx);
}
void qa_app_q3_qvm_server_game_set_configstring(qa_app_q3_qvm_server_game *game, double index, const char *value) {
  current(game);
  if (strcmp(qa_app_q3_server_state_configstring_get(game->options.state, index), value) == 0
      && qa_app_q3_server_state_has_configstring(game->options.state, index)) return;
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
    /* variableValue('bot_enable') check via registry string value */
    char *value = qa_cvar_registry_variable_string(qa_app_q3_server_state_cvars(game->options.state), "bot_enable");
    double enabled = value ? atof(value) : 0;
    free(value);
    if (enabled != 0) qa_range_error("Q3 guest bot services must be attached before initialization");
  }
  game->output = output; game->has_output = true; game->lifecycle = LC_INITIALIZING;
  game->options.module.initialize(game->options.module.ctx, game->options.now(game->options.ctx), game->options.seed,
      start_kind && strcmp(start_kind, "map-restart") == 0);
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
  const char *denied = game->options.module.client_connect(game->options.module.ctx, slot, first_time, is_bot);
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
  game->options.module.client_begin(game->options.module.ctx, player.source_entity);
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
  game->options.module.client_think(game->options.module.ctx, player.source_entity);
  current(game);
  finish_operation(game);
}
void qa_app_q3_qvm_server_game_userinfo(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *value) {
  game->external_operations++;
  running(game); (void)entry_of(game, player);
  qa_app_q3_server_state_set_userinfo(game->options.state, player.source_entity, value);
  game->options.module.client_userinfo_changed(game->options.module.ctx, player.source_entity);
  current(game);
  if (game->options.client_changed) game->options.client_changed(game->options.ctx, "userinfo", player.actor);
  finish_operation(game);
}
void qa_app_q3_qvm_server_game_command_immediate(qa_app_q3_qvm_server_game *game, qa_app_q3_player player, const char *const *argv, size_t argc) {
  game->external_operations++;
  running(game); (void)entry_of(game, player);
  game->options.module.client_command(game->options.module.ctx, player.source_entity, argv, argc);
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
  game->options.module.client_disconnect(game->options.module.ctx, player.source_entity);
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
  bool handled = game->options.module.console_command(game->options.module.ctx, argv, argc);
  current(game); drain_bot_commands(game);
  finish_operation(game);
  return handled;
}
void qa_app_q3_qvm_server_game_run_frame(qa_app_q3_qvm_server_game *game, double time_milliseconds) {
  game->external_operations++;
  running(game);
  if (game->has_bots) {
    char *value = qa_cvar_registry_variable_string(qa_app_q3_server_state_cvars(game->options.state), "bot_enable");
    bool enabled = value && atof(value) != 0;
    free(value);
    if (enabled) game->options.module.bot_start_frame(game->options.module.ctx, time_milliseconds);
  }
  game->options.module.run_frame(game->options.module.ctx, time_milliseconds);
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
  if (game->external_operations != 0 || game->options.module.is_active(game->options.module.ctx))
    qa_range_error("Q3 guest shutdown must await the active operation");
  if (game->options.input_close) game->options.input_close(game->options.input_ctx);
  game->retirement_count = 0;
  if (game->lifecycle == LC_INITIALIZING || game->lifecycle == LC_RUNNING || game->lifecycle == LC_SHUTTING_DOWN) {
    qa_app_q3_guest_output output = game->output;
    (void)output; game->lifecycle = LC_SHUTTING_DOWN;
    game->options.module.shutdown(game->options.module.ctx, false);
  }
  release_source(game, false);
}
qa_app_q3_guest_map_transition qa_app_q3_qvm_server_game_shutdown_for_map_change(qa_app_q3_qvm_server_game *game, bool restart) {
  running(game);
  bool busy = game->external_operations != 0 || game->options.module.is_active(game->options.module.ctx) || game->reconnecting_count != 0;
  for (size_t i = 0; i < game->client_count; i++)
    if (game->clients[i].phase != PH_CONNECTED && game->clients[i].phase != PH_ACTIVE) busy = true;
  if (busy) qa_range_error("Q3 map transition requires completed client operations");
  if (game->options.input_close) game->options.input_close(game->options.input_ctx);
  game->retirement_count = 0;
  game->lifecycle = LC_SHUTTING_DOWN;
  game->options.module.shutdown(game->options.module.ctx, restart);
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
