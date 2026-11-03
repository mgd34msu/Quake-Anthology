/* Port of src/app/bootstrap/application.ts, region 837-1233: open,
 * openApplication, openSource, prepareGuestBots, createBots. Layout and
 * status: docs/application_layout.md. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/authored_start.h"
#include "qa/app/bootstrap/match_preflight.h"
#include "qa/app/bootstrap/startup_source.h"
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include "qa/app/bootstrap/component_client_save.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/app/bootstrap/simulation/monster_navigation.h"
#include "qa/app/bootstrap/simulation/navigation.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/content/catalog/index.h"
#include "qa/formats/q2_map.h"
#include "qa/content/mounts.h"
#include "qa/content/user_data.h"
#include "qa/foundation/js.h"
#include "qa/input/seat.h"
#include "qa/network/common/js_number.h"
#include "qa/settings/server/profile.h"
#include "qa/settings/server/selection.h"
#include "qa/world/session/mod_commands.h"
#include "qa/world/session/mod_files.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

/* TS openApplication. */
qa_application *qa_application_open_application(qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences) {
  return qa_application_open(options, host, recipe, preferences, NULL);
}
/* TS static open: openSource({ kind: "owned" }, ...). */
qa_application *qa_application_open(qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences,
    const qa_save_image *initial_save) {
  qa_application_ownership owned = { NULL };
  return qa_application_open_source(owned, options, host, recipe, preferences, initial_save);
}
/* TS static openBorrowed(client, options, host, recipe?, preferences?, initialSave?). */
qa_application *qa_application_open_borrowed(qa_app_client_bootstrap *client, qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences, const qa_save_image *initial_save) {
  if (options.dedicated) qa_raise("Error", 0, 0, "Dedicated applications cannot borrow a graphical client");
  qa_resource_scope_assert_open(qa_engine_session_resources(client->session));
  qa_application_ownership borrowed = { client };
  return qa_application_open_source(borrowed, options, host, recipe, preferences, initial_save);
}

/* ================= static openSource(ownership, options, host, recipe?, preferences?, initialSave?) ================= */
/* C-only: the objects openSource creates that the application owns past close (TS GC), freed by destroy. */
struct qa_application_open_record {
  qa_app_configuration_content *configuration;  /* preparation.content (configuration / borrowed kinds) */
  qa_app_prepared_initial *startup;
  qa_app_prepared_profile *profile;
  qa_app_console_script_files *startup_scripts;
  qa_app_console_script_files *profile_scripts;
  qa_app_console_script_files *restored_scripts;
  qa_mod_commands *mod_commands;
  qa_engine_session *session;                    /* owned session (ownership owned) */
  char *identity_name;                           /* the owned identity's name (createIdentityOwner borrows it) */
  qa_app_shared_simulation *simulation;
  qa_app_loaded_content *content;
  qa_app_server_operator_state *operator_state;
  qa_app_image_settings *image_settings;        /* opened by openSource (not the startup's) */
  qa_cvar_registry *borrowed_source;
  qa_app_options options_for_recipe; bool has_options_for_recipe;
  qa_cvar_archive_entry *source_archive; size_t source_archive_count;
  qa_app_sim_saved_settings saved_settings; bool has_saved_settings;
  qa_app_sim_saved_bots *saved_bots;
  qa_app_sim_decoded_bots_checkpoint *decoded_bots; /* decodeApplicationBotsCheckpoint(saved_bots->value) */
  qa_app_sim_application_monster_navigation *monster_navigation;
  qa_authored_start_catalog authored; bool has_authored;
  qa_app_authored_campaign_start authored_start; bool has_authored_start;
  char *q2_next_server;
  void *operator_host_record;
  void *startup_demo_record;
  qa_app_q2_travel_target authored_travel;
  qa_server_profile server_profile; bool has_server_profile;
  qa_save_value *server_profile_value;
  struct open_scripts_ctx *scripts_ctx;
  qa_app_sim_q3_server_authority local_authority; bool has_local_authority;
  qa_app_sim_network_simulation authority_simulation;
  qa_app_sim_network_content authority_content;
  qa_application_local_q3_guest_world *local_guest;
  qa_identity_owner world_sound_owner; bool has_world_sound_owner;
  void *frontend_printer;
};

/* TS guestCommands: () => { if (guestConsole === null) throw ...; return guestConsole; }. */
typedef struct open_state {
  qa_application_ownership ownership;
  qa_app_options options;
  qa_application_host host;
  const qa_executable_recipe *recipe;
  const qa_app_frontend_preference_overrides *preferences;
  const qa_save_image *initial_save;
  qa_application_operator_output *operator_output;
  struct qa_application_open_record *record;
  const qa_content_catalog *catalog;
  qa_config_store input_config; bool has_input_config;
  qa_identity_owner *identity; bool owns_identity;
  qa_engine_session *session;
  qa_client_id *seat_clients; qa_session_seat **seats; size_t seat_count, seat_capacity; /* localSeats */
  qa_client_id *network_clients; qa_application_player_identity *network_identities; size_t network_count;
  qa_application *application;
  qa_application_owner_ref startup_owner;
  qa_command_buffer *guest_console;
  qa_app_client_demo_commands *startup_demos; void *startup_demos_binding; bool has_startup_demos;
  bool has_borrowed_images; qa_app_image_client_settings borrowed_images;
  double *restored_slots; qa_session_client **restored_clients; size_t restored_count;
  qa_session_client **staged_clients_added; size_t staged_clients_added_count;
  qa_session_client **staged_clients_removed; size_t staged_clients_removed_count;
  qa_session_seat **staged_seats_added; size_t staged_seats_added_count;
  qa_session_seat **staged_seats_removed; size_t staged_seats_removed_count;
  const qa_server_setting_definition **definitions; size_t definition_count;
  double default_capacity, borrowed_capacity;
  qa_source_debug_graph *candidate_graph;
  qa_app_application_world world;
  qa_mod_selection *enabled_mods; qa_app_sim_mod_selections enabled;
  const void **weapon_behaviors;
  qa_mod_user_files *mod_files;
  qa_app_name_value *q3_cvars; size_t q3_cvar_count;
  void **kept; size_t kept_count;
  qa_application *result;
} open_state;

static void *open_grow(void *items, size_t count, size_t size) {
  void *grown = realloc(items, (count + 1) * size);
  if (grown == NULL) qa_range_error("Out of memory");
  return grown;
}
static void open_print(void *ctx, const char *text) {
  open_state *state = ctx;
  state->host.print(state->host.ctx, text);
}
static qa_session_seat *open_local_seat(open_state *state, qa_client_id client) {
  for (size_t i = 0; i < state->seat_count; i++) if (qa_same_client(state->seat_clients[i], client)) return state->seats[i];
  return NULL;
}
static void open_local_seat_set(open_state *state, qa_client_id client, qa_session_seat *seat) {
  for (size_t i = 0; i < state->seat_count; i++)
    if (qa_same_client(state->seat_clients[i], client)) { state->seats[i] = seat; return; }
  state->seat_clients = open_grow(state->seat_clients, state->seat_count, sizeof *state->seat_clients);
  state->seats = open_grow(state->seats, state->seat_count, sizeof *state->seats);
  state->seat_clients[state->seat_count] = client;
  state->seats[state->seat_count++] = seat;
}
static bool saved_bot_slot(const open_state *state, double slot) {
  if (state->record->saved_bots == NULL) return false;
  for (size_t i = 0; i < state->record->saved_bots->connection_count; i++)
    if (state->record->saved_bots->connections[i].client_slot == slot) return true;
  return false;
}
static qa_session_client *open_restored_client(open_state *state, double slot) {
  for (size_t i = 0; i < state->restored_count; i++) if (state->restored_slots[i] == slot) return state->restored_clients[i];
  return NULL;
}
static void open_restored_set(open_state *state, double slot, qa_session_client *client) {
  for (size_t i = 0; i < state->restored_count; i++)
    if (state->restored_slots[i] == slot) { state->restored_clients[i] = client; return; }
  state->restored_slots = open_grow(state->restored_slots, state->restored_count, sizeof *state->restored_slots);
  state->restored_clients = open_grow(state->restored_clients, state->restored_count, sizeof *state->restored_clients);
  state->restored_slots[state->restored_count] = slot;
  state->restored_clients[state->restored_count++] = client;
}
#define OPEN_PUSH(state, list, value) do { \
    (state)->list = open_grow((state)->list, (state)->list##_count, sizeof *(state)->list); \
    (state)->list[(state)->list##_count++] = (value); } while (0)
static bool open_contains_client(qa_session_client *const *items, size_t count, const qa_session_client *client) {
  for (size_t i = 0; i < count; i++) if (items[i] == client) return true;
  return false;
}

/* TS guestCommands and nativeCommand. */
static qa_command_buffer *open_guest_commands(void *ctx) {
  open_state *state = ctx;
  if (state->guest_console == NULL) qa_raise("Error", 0, 0, "Q3 guest console is unavailable");
  return state->guest_console;
}
static qa_command_buffer *open_mod_buffer(void *ctx) { return ((open_state *)ctx)->guest_console; }
static void open_native_command(void *ctx, const char *text) {
  open_state *state = ctx;
  qa_command_context context = qa_application_native_command_context(state->record->content, qa_engine_session_id(state->session));
  qa_mod_commands_append(state->record->mod_commands, text, context, qa_application_content_dialect(state->record->content));
}

/* The ConsoleScriptFiles prepareInitialConfiguration builds over the configuration content (TS configuration.ts):
 * the C configuration borrows one built here; closing it closes the content (TS () => content.close()). */
typedef struct open_scripts_ctx {
  qa_app_configuration_content *content;
  qa_content_mounted *mounts;
  const char **game_roots; char **owned_roots; size_t owned_count;
  char *console_root;
  qa_app_source_script_reader reader;
} open_scripts_ctx;
static void open_scripts_release(void *object) {
  open_scripts_ctx *ctx = object;
  for (size_t i = 0; i < ctx->owned_count; i++) free(ctx->owned_roots[i]);
  free(ctx->owned_roots); free(ctx->game_roots); free(ctx->console_root);
  qa_app_source_script_reader_destroy(ctx->reader);
  free(ctx);
}
static bool open_scripts_mounted(void *raw, const char *name, uint8_t **out_bytes, size_t *out_count) {
  open_scripts_ctx *ctx = raw;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(ctx->mounts, name, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; *out_count = opened.byte_length;
  return true;
}
static char **open_scripts_files(void *raw, const char *directory, const char *extension, size_t *out_count) {
  open_scripts_ctx *ctx = raw;
  char **names = qa_content_mounted_list_files(ctx->mounts, directory, extension, out_count);
  if (names == NULL && *out_count > 0) qa_range_error("Out of memory");
  return names;
}
static const char *open_scripts_retire(void *raw) {
  open_scripts_ctx *ctx = raw;
  qa_app_configuration_content_close(ctx->content);
  return NULL;
}
qa_app_console_script_files *qa_app_open_configuration_scripts(const qa_app_options *options, qa_app_configuration_content *content,
    const qa_config_store *settings) {
  const qa_content_catalog *catalog = qa_app_configuration_content_catalog(content);
  qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(content);
  open_scripts_ctx *ctx = checked(calloc(1, sizeof *ctx));
  ctx->content = content;
  ctx->mounts = (qa_content_mounted *)qa_app_configuration_content_mounts(content);
  ctx->console_root = checked(qa_app_console_config_root(options->user_content_root));
  qa_app_console_script_files_options files;
  memset(&files, 0, sizeof files);
  size_t capacity = catalog->product_count + 1;
  ctx->game_roots = checked(calloc(capacity, sizeof *ctx->game_roots));
  ctx->owned_roots = checked(calloc(capacity + 1, sizeof *ctx->owned_roots));
  ctx->owned_count = capacity + 1;
  files.has_legacy_config = qa_app_legacy_configuration_options_for_catalog(catalog, options, selection.engine_behavior.content,
      &files.legacy_config, ctx->game_roots, capacity, ctx->owned_roots);
  files.console_root = ctx->console_root;
  files.settings = settings;
  ctx->reader = qa_app_source_script_reader_create(catalog, ctx->mounts, selection.engine_behavior.content);
  files.mounted_script = ctx->reader; files.has_mounted_script = true;
  files.mounted_ctx = ctx;
  files.mounted_resource = open_scripts_mounted;
  files.mounted_files = open_scripts_files;
  files.mounted.ctx = ctx; files.mounted.open = open_scripts_mounted; files.has_mounted = true;
  files.retire_mounted = open_scripts_retire;
  files.release = open_scripts_release; files.release_ctx = ctx;
  return checked(qa_app_console_script_files_create(files));
}

/* MountPreparationScope.open for catalog.authoredStartsFor. */
typedef struct open_reader_state { qa_content_mounted *mounted; uint8_t **buffers; size_t count; } open_reader_state;
static bool open_reader_open(void *ctx, const char *path, qa_catalog_opened_resource *out) {
  open_reader_state *state = ctx;
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(state->mounted, path, NULL, NULL, &opened)) return false;
  state->buffers = open_grow(state->buffers, state->count, sizeof *state->buffers);
  state->buffers[state->count++] = opened.bytes;
  out->reference = *opened.reference; out->bytes = opened.bytes; out->byte_length = opened.byte_length;
  return true;
}
static void open_reader_close(void *ctx) {
  open_reader_state *state = ctx;
  qa_content_mounted_destroy(state->mounted);
  for (size_t i = 0; i < state->count; i++) free(state->buffers[i]);
  free(state->buffers);
  free(state);
}
static bool open_plan(void *ctx, const qa_resolved_mount_plan *plan, qa_catalog_mounted_reader *out) {
  (void)ctx; /* the TS opener closure captures nothing */
  qa_open_mount_options options;
  memset(&options, 0, sizeof options);
  qa_content_mounted *mounted = checked(qa_open_mount_plan(plan, &options));
  open_reader_state *state = checked(calloc(1, sizeof *state));
  state->mounted = mounted;
  out->ctx = state; out->open = open_reader_open; out->close = open_reader_close;
  return true;
}

/* prepareRereleaseNavigation: simulation => createApplicationBotNavigation({ content, simulation }). */
static const void *open_rerelease_navigation(void *ctx, qa_app_sim_shared_simulation *opaque) {
  open_state *state = ctx;
  qa_app_shared_simulation *simulation = (qa_app_shared_simulation *)(void *)opaque;
  qa_app_sim_application_bot_navigation *navigation = qa_app_sim_create_application_bot_navigation(
      qa_application_navigation_content(state->record->content),
      qa_app_shared_sim_navigation_simulation(simulation, qa_app_loaded_content_recipe(state->record->content)));
  return checked(navigation);
}
/* promptSupported: client => !options.dedicated && localSeats.has(client). */
static bool open_prompt_supported(void *ctx, qa_client_id client) {
  open_state *state = ctx;
  if (state->application != NULL) return !state->options.dedicated && qa_application_local_seat(state->application, client) != NULL;
  return !state->options.dedicated && open_local_seat(state, client) != NULL;
}
/* playerIdentity: client => networkPlayerIdentities.get(client) ?? { seat: localSeats.get(client)?.id.index ?? 0, socialId: "" }. */
static bool open_player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  open_state *state = ctx;
  qa_application *app = state->application;
  if (app != NULL) {
    for (size_t i = 0; i < app->network_player_identity_count; i++)
      if (qa_same_client(app->network_player_clients[i], client)) {
        *out_seat = app->network_player_identities[i].seat;
        *out_social_id = app->network_player_identities[i].social_id;
        return true;
      }
  }
  qa_session_seat *seat = app != NULL ? qa_application_local_seat(app, client) : open_local_seat(state, client);
  *out_seat = seat == NULL ? 0 : (double)qa_session_seat_id(seat).index;
  *out_social_id = "";
  return true;
}
static double open_clamp_color(bool has, double value) {
  double color = has ? trunc(value) : 0;
  return qa_js_min(13, qa_js_max(0, color));
}
/* TS loadSimulation's nextFrame: host.loading?.nextFrame (serviceLoading pumps a plain loop). */
static void open_next_frame(void *ctx) {
  open_state *state = ctx;
  if (state->host.loading != NULL && state->host.loading->next_frame != NULL) state->host.loading->next_frame(state->host.loading->ctx);
}
static void open_stage(open_state *state, const char *message) {
  if (state->host.loading != NULL && state->host.loading->stage != NULL) state->host.loading->stage(state->host.loading->ctx, message);
}
/* prepareInitialConfiguration's onPrepared: startupDemos = { service, release: service.attach(prepared.commands) }. */
static void open_on_prepared(void *ctx, qa_app_prepared_startup *prepared) {
  open_state *state = ctx;
  state->startup_demos = qa_application_startup_demo_service(state->options.dedicated, prepared, &state->host, &state->startup_owner,
      &state->record->startup_demo_record);
  state->startup_demos_binding = qa_app_client_demo_commands_attach(state->startup_demos, qa_app_prepared_startup_commands(prepared));
  state->has_startup_demos = true;
}
static void open_release_startup_demos(void *ctx) {
  open_state *state = ctx;
  qa_app_client_demo_commands_detach(state->startup_demos, state->startup_demos_binding);
}
/* resolveClient for the restored bot service: a saved connection with this client slot and
 * generation maps to the restored client in that slot. */
static qa_session_client *open_resolve_bot(void *ctx, double slot, double generation) {
  open_state *state = ctx;
  const qa_app_sim_saved_bots *saved = state->record->saved_bots;
  bool known = false;
  for (size_t i = 0; saved != NULL && i < saved->connection_count; i++)
    if (saved->connections[i].client_slot == slot && saved->connections[i].client_generation == generation) known = true;
  return known ? open_restored_client(state, slot) : NULL;
}


static qa_content_mounted *open_content_for(void *ctx, qa_content_id id) {
  return (qa_content_mounted *)qa_app_loaded_content_for_content(ctx, id);
}
static qa_decoded_world open_decoded_world(qa_app_loaded_content *content) {
  qa_app_application_world world = qa_app_loaded_content_world(content);
  qa_decoded_world out;
  memset(&out, 0, sizeof out);
  if (strcmp(world.kind, "q1-bsp") == 0) { out.kind = QA_DECODED_WORLD_Q1; out.as.q1 = &((const qa_q1_map *)world.value)->world; }
  else if (strcmp(world.kind, "q2-bsp") == 0) { out.kind = QA_DECODED_WORLD_Q2; out.as.q2 = &((const qa_q2_decoded_map *)world.value)->world; }
  else { out.kind = QA_DECODED_WORLD_Q3; out.as.q3 = world.value; }
  return out;
}
static qa_app_preflight_content open_preflight_content(qa_app_loaded_content *content) {
  qa_app_preflight_content out;
  memset(&out, 0, sizeof out);
  out.catalog = qa_app_loaded_content_catalog(content);
  out.recipe = qa_app_loaded_content_recipe(content);
  qa_app_application_world world = qa_app_loaded_content_world(content);
  if (strcmp(world.kind, "q1-bsp") == 0) {
    out.world_kind = QA_Q3_TA_WORLD_Q1_BSP;
    out.world_entities = ((const qa_q1_map *)world.value)->world.entities;
  } else if (strcmp(world.kind, "q2-bsp") == 0) {
    out.world_kind = QA_Q3_TA_WORLD_Q2_BSP;
    out.world_entities = ((const qa_q2_decoded_map *)world.value)->world.entities;
  } else {
    out.world_kind = QA_Q3_TA_WORLD_Q3_BSP;
    out.world_entities = ((const qa_q3_world_geometry *)world.value)->entities;
  }
  if (out.world_entities == NULL) out.world_entities = "";
  return out;
}
/* The records openSource keeps alive with the application (restored client ids). */
static void qa_application_keep_record_open(open_state *s, void *record) {
  s->kept = open_grow(s->kept, s->kept_count, sizeof *s->kept);
  s->kept[s->kept_count++] = record;
}
static void open_app_push_client(qa_session_client ***items, size_t *count, size_t *capacity, qa_session_client *client) {
  if (*count == *capacity) { *capacity = *capacity == 0 ? 4 : *capacity * 2; *items = checked(realloc(*items, *capacity * sizeof **items)); }
  (*items)[(*count)++] = client;
}
static void open_app_push_seat(qa_session_seat ***items, size_t *count, size_t *capacity, qa_session_seat *seat) {
  if (*count == *capacity) { *capacity = *capacity == 0 ? 4 : *capacity * 2; *items = checked(realloc(*items, *capacity * sizeof **items)); }
  (*items)[(*count)++] = seat;
}
/* sourceConnections.set(client, connection). */
static void open_app_set_connection(qa_application *app, qa_session_client *client, qa_session_connection *connection) {
  for (size_t i = 0; i < app->source_connection_count; i++)
    if (app->source_connection_clients[i] == client) { app->source_connections[i] = connection; return; }
  if (app->source_connection_count == app->source_connection_capacity) {
    app->source_connection_capacity = app->source_connection_capacity == 0 ? 4 : app->source_connection_capacity * 2;
    app->source_connection_clients = checked(realloc(app->source_connection_clients, app->source_connection_capacity * sizeof *app->source_connection_clients));
    app->source_connections = checked(realloc(app->source_connections, app->source_connection_capacity * sizeof *app->source_connections));
  }
  app->source_connection_clients[app->source_connection_count] = client;
  app->source_connections[app->source_connection_count++] = connection;
}
/* requestedCommands.admitAll(requests): ConfigurationCommandRequest rows become owned application requests. */
static void open_admit_request(qa_application *app, const qa_app_configuration_command_request *source) {
  qa_application_command_request request;
  memset(&request, 0, sizeof request);
  request.target = QA_APPLICATION_TARGET_APPLICATION;
  request.name = checked(strdup(source->name));
  request.arguments = source->argument_count == 0 ? NULL : checked(calloc(source->argument_count, sizeof *request.arguments));
  for (size_t i = 0; i < source->argument_count; i++) request.arguments[i] = checked(strdup(source->arguments[i]));
  request.argument_count = source->argument_count;
  request.has_seat = source->has_seat; request.seat = source->seat;
  request.has_source = true; request.source = qa_command_context_copy(&source->source);
  (void)qa_event_ring_admit(&app->requested_commands, &request);
}
static void open_release_view_binding(void *ctx) { qa_app_image_view_binding_release(ctx); }
static void open_after_dispatch(void *ctx) { qa_application_commands(ctx); }
/* initialized.sourceCommands?.executeAsync(() => initialized.commands()). */
static void open_execute_commands(qa_application *app) {
  (void)qa_command_buffer_execute_async(app->source_commands, open_after_dispatch, app, NULL, NULL);
}
static void open_host_print(void *ctx, const char *text) { open_print(ctx, text); }
/* application.localGuest = { worldSound, authority, seats, pendingCommands } and its preparation. */
static void open_local_guest(open_state *s, qa_application *application, qa_app_loaded_content *content, qa_app_shared_simulation *simulation,
    bool restored) {
  struct qa_application_open_record *r = application->open_record;
  r->authority_simulation = qa_app_shared_sim_network_simulation(simulation);
  r->authority_content = qa_app_shared_sim_network_content(simulation, content);
  qa_app_sim_q3_server_binding_options options;
  memset(&options, 0, sizeof options);
  options.session = s->session;
  options.simulation = &r->authority_simulation;
  options.content = &r->authority_content;
  options.mode = restored ? "restore" : NULL;
  options.print_ctx = s; options.print = open_host_print;
  qa_app_sim_q3_server_authority authority = qa_app_sim_create_q3_application_server_host(options);
  r->local_authority = authority; r->has_local_authority = true;
  qa_application_local_q3_guest_world *world = checked(calloc(1, sizeof *world));
  r->local_guest = world;
  if (!qa_identity_owner_create(&r->world_sound_owner, "local-qvm-world-audio")) qa_range_error("Out of memory");
  r->has_world_sound_owner = true;
  world->world_sound = qa_identity_owner_actor(&r->world_sound_owner, 1022, 0);
  world->authority = authority;
  if (!qa_event_ring_init(&world->pending_commands, QA_APPLICATION_REQUEST_CAPACITY, sizeof(qa_application_command_request)))
    qa_range_error("Out of memory");
  application->local_guest = world;
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  double server_id = restored ? qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(guest)), "sv_serverid") : 1;
  authority.host.prepare(authority.host.ctx, 0, server_id, NULL, NULL);
  if (!restored) {
    qa_app_q3_qvm_server_game_initialize(guest, qa_application_guest_output(application, NULL, true), "new", NULL, 0);
    if (application->source_commands != NULL) open_execute_commands(application);
  }
}

/* The inner try of openSource: everything after the configuration content opened. */
static void open_body(void *ctx) {
  open_state *s = ctx;
  struct qa_application_open_record *r = s->record;
  bool borrowed = s->ownership.borrowed_client != NULL;
  qa_app_client_bootstrap *client = s->ownership.borrowed_client;
  const qa_save_image *initial_save = s->initial_save;
  bool restored = initial_save != NULL;
  qa_app_application_configuration_selection selection;
  memset(&selection, 0, sizeof selection);
  if (!restored) selection = qa_app_configuration_content_selection(r->configuration);
  /* options = recipe === undefined ? resolveApplicationMovement(catalog, options) : applicationOptionsForRecipe(options, { catalog, recipe }) */
  if (s->recipe == NULL) s->options = qa_app_resolve_application_movement(s->catalog, &s->options);
  else {
    r->options_for_recipe = qa_app_application_options_for_recipe(&s->options, s->catalog, s->recipe);
    r->has_options_for_recipe = true;
    s->options = r->options_for_recipe;
  }
  s->definitions = restored ? qa_server_definitions_for_recipe(qa_app_loaded_content_recipe(r->content), &s->definition_count)
      : qa_server_definitions_for_selection(selection.source, selection.match, selection.combat, &s->definition_count);
  if (s->options.server_profile_path != NULL) {
    FILE *file = fopen(s->options.server_profile_path, "rb");
    if (file == NULL) qa_raise("Error", 0, 0, "ENOENT: no such file or directory, open '%s'", s->options.server_profile_path);
    uint8_t *bytes = NULL; size_t length = 0, capacity = 0;
    for (;;) {
      if (length == capacity) { capacity = capacity == 0 ? 4096 : capacity * 2; bytes = checked(realloc(bytes, capacity)); }
      size_t read = fread(bytes + length, 1, capacity - length, file);
      if (read == 0) break;
      length += read;
    }
    fclose(file);
    r->server_profile_value = qa_parse_json_value(bytes, length, false);
    free(bytes);
    qa_parse_server_profile(r->server_profile_value, s->definitions, s->definition_count, &r->server_profile);
    r->has_server_profile = true;
    s->options.server_profile = &r->server_profile;
  } else if (s->options.server_profile != NULL) {
    /* parseServerProfile(options.serverProfile, definitions): the C launch options carry the parsed profile already. */
    r->server_profile = *(const qa_server_profile *)s->options.server_profile;
    s->options.server_profile = &r->server_profile;
  }
  const qa_catalog_discovered_product *product = qa_content_catalog_require(s->catalog, s->options.product);
  {
    char *root;
    if (product->user_content != NULL) root = checked(strdup(product->user_content->root));
    else {
      char *base = s->options.user_content_root != NULL ? checked(strdup(s->options.user_content_root)) : checked(qa_default_user_content_root());
      root = checked(qa_user_product_directory(base, product->expectation.content_directory));
      free(base);
    }
    bool stored = qa_config_store_init(&s->input_config, root);
    free(root);
    if (!stored) qa_range_error("Out of memory");
    s->has_input_config = true;
  }
  if (borrowed) s->identity = client->identity;
  else {
    /* `quake:${product}:${map}`: the identity owner and every SessionId keep the name, so it lives with the record. */
    size_t length = strlen(s->options.product) + strlen(s->options.map) + sizeof "quake::";
    r->identity_name = checked(malloc(length));
    snprintf(r->identity_name, length, "quake:%s:%s", s->options.product, s->options.map);
    s->identity = checked(calloc(1, sizeof *s->identity));
    if (!qa_identity_owner_create(s->identity, r->identity_name)) qa_range_error("Out of memory");
    s->owns_identity = true;
  }
  if (borrowed) s->session = client->session;
  else {
    qa_session_mode mode = {s->options.dedicated ? QA_SESSION_MODE_HEADLESS : QA_SESSION_MODE_LOCAL};
    s->session = checked(qa_engine_session_create(s->identity, mode));
    r->session = s->session;
  }
  qa_command_context mod_context;
  memset(&mod_context, 0, sizeof mod_context);
  mod_context.session = qa_engine_session_id(s->session);
  mod_context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  r->mod_commands = checked(qa_mod_commands_create(mod_context, open_mod_buffer, s));
  if (borrowed) {
    s->borrowed_images = qa_app_image_settings_prepare_client_settings(client->image_settings);
    s->has_borrowed_images = true;
    /* slots: the retained local clients, or the saved human slots */
    size_t slot_count = 0;
    double *slots = checked(calloc((size_t)s->options.seats + client->local_count + (r->has_saved_settings ? r->saved_settings.client_slot_count : 0) + 1, sizeof *slots));
    if (!r->has_saved_settings) {
      for (size_t i = 0; i < client->local_count && (double)i < s->options.seats; i++)
        slots[slot_count++] = (double)qa_session_client_id(client->locals[i].client).slot;
    } else {
      for (size_t i = 0; i < r->saved_settings.client_slot_count; i++)
        if (!saved_bot_slot(s, r->saved_settings.client_slots[i])) slots[slot_count++] = r->saved_settings.client_slots[i];
    }
    while ((double)slot_count < s->options.seats) {
      double slot = 0;
      for (bool taken = true; taken;) {
        taken = false;
        for (size_t i = 0; i < slot_count; i++) if (slots[i] == slot) { taken = true; slot++; break; }
      }
      slots[slot_count++] = slot;
    }
    for (size_t index = 0; index < slot_count; index++) {
      double slot = slots[index];
      const qa_app_client_bootstrap_seat *retained = NULL;
      for (size_t i = 0; i < client->local_count && retained == NULL; i++)
        if ((double)qa_session_client_id(client->locals[i].client).slot == slot) retained = &client->locals[i];
      qa_session_client *session_client = retained != NULL ? retained->client : NULL;
      if (session_client == NULL) {
        qa_session_client *previous = qa_engine_session_client_at(s->session, (int64_t)slot);
        session_client = checked(qa_engine_session_prepare_client(s->session, (int64_t)slot, previous));
        OPEN_PUSH(s, staged_clients_added, session_client);
        if (previous != NULL) OPEN_PUSH(s, staged_clients_removed, previous);
      }
      qa_session_seat *incumbent = NULL;
      for (size_t i = 0; i < client->local_count && incumbent == NULL; i++)
        if ((size_t)qa_session_seat_id(client->locals[i].seat).index == index) incumbent = client->locals[i].seat;
      qa_session_seat *seat = retained != NULL && (size_t)qa_session_seat_id(retained->seat).index == index ? retained->seat
          : checked(qa_engine_session_prepare_seat(s->session, (int64_t)index, session_client, incumbent));
      if (retained == NULL || seat != retained->seat) OPEN_PUSH(s, staged_seats_added, seat);
      open_local_seat_set(s, qa_session_client_id(session_client), seat);
    }
    free(slots);
    for (size_t i = 0; i < client->local_count; i++) {
      bool kept = false;
      for (size_t j = 0; j < s->seat_count; j++) if (s->seats[j] == client->locals[i].seat) kept = true;
      if (!kept) OPEN_PUSH(s, staged_seats_removed, client->locals[i].seat);
      if (open_local_seat(s, qa_session_client_id(client->locals[i].client)) == NULL) OPEN_PUSH(s, staged_clients_removed, client->locals[i].client);
    }
  }
  /* sourceArchive */
  if (!restored && !s->options.dedicated) {
    qa_config_store store;
    if (!qa_app_configuration_store_for_catalog(s->catalog, &s->options, selection.source.content, &store)) qa_range_error("Out of memory");
    const char *owner[3] = {"source", selection.source.content, selection.source.provider};
    r->source_archive = qa_app_load_cvar_archive(&store, owner, 3,
        qa_app_configuration_dialect_in_catalog(s->catalog, r->configuration), &r->source_archive_count);
    qa_config_store_destroy(&store);
  }
  if (r->has_saved_settings) {
    for (size_t i = 0; i < r->saved_settings.client_slot_count; i++) {
      double slot = r->saved_settings.client_slots[i];
      if (!borrowed) open_restored_set(s, slot, checked(qa_engine_session_create_client(s->session, (int64_t)slot)));
      else {
        qa_session_client *retained = NULL;
        for (size_t j = 0; j < s->seat_count && retained == NULL; j++)
          if ((double)qa_session_client_id(qa_session_seat_client(s->seats[j])).slot == slot) retained = qa_session_seat_client(s->seats[j]);
        if (retained != NULL) open_restored_set(s, slot, retained);
        else {
          qa_session_client *previous = qa_engine_session_client_at(s->session, (int64_t)slot);
          qa_session_client *prepared = checked(qa_engine_session_prepare_client(s->session, (int64_t)slot, previous));
          open_restored_set(s, slot, prepared);
          OPEN_PUSH(s, staged_clients_added, prepared);
          if (previous != NULL && !open_contains_client(s->staged_clients_removed, s->staged_clients_removed_count, previous))
            OPEN_PUSH(s, staged_clients_removed, previous);
        }
      }
    }
    if (!s->options.dedicated && !borrowed)
      for (size_t i = 0; i < s->restored_count; i++) {
        qa_session_client *restored_client = s->restored_clients[i];
        if (saved_bot_slot(s, (double)qa_session_client_id(restored_client).slot)) continue;
        (void)qa_session_client_connect(restored_client, "loopback");
        open_local_seat_set(s, qa_session_client_id(restored_client),
            checked(qa_engine_session_create_seat(s->session, (int64_t)s->seat_count, restored_client)));
      }
  }
  /* engine = preparation.content.recipe.engineBehavior / selection.engineBehavior */
  qa_provider_reference engine = restored ? qa_app_loaded_content_recipe(r->content)->engine_behavior : selection.engine_behavior;
  s->default_capacity = r->has_saved_settings ? r->saved_settings.max_clients
      : strcmp(s->options.product, "q1-quakeworld") == 0 ? 8
      : strcmp(s->options.mode, "singleplayer") == 0
          ? (qa_content_catalog_product(s->catalog, engine.content)->expectation.family == QA_GAME_FAMILY_Q3 ? 8 : 1) : 16;
  s->borrowed_capacity = s->default_capacity;
  if (borrowed && !restored) {
    const qa_provider_timing *movement_timing = NULL;
    size_t timing_count = 0;
    const qa_provider_timing *timings = qa_app_configuration_content_timing(r->configuration, &timing_count);
    for (size_t i = 0; i < timing_count && movement_timing == NULL; i++)
      if (strcmp(timings[i].provider, selection.movement.provider) == 0) movement_timing = &timings[i];
    if (movement_timing == NULL || !s->has_borrowed_images) qa_raise("Error", 0, 0, "Borrowed configuration has no movement or image owner");
    qa_command_dialect movement = qa_app_configuration_movement_dialect_for(timings, timing_count, selection.movement.provider);
    qa_app_prepared_startup *prepared = client->prepared;
    qa_command_dialect dialect = qa_app_configuration_dialect_in_catalog(s->catalog, r->configuration);
    if (client->has_source_profile && strcmp(client->source_profile.content, selection.source.content) == 0
        && strcmp(client->source_profile.provider, selection.source.provider) == 0
        && qa_command_buffer_dialect(qa_app_prepared_startup_commands(prepared)) == dialect
        && qa_cvar_registry_dialect(qa_app_prepared_startup_movement(prepared)) == movement) {
      qa_cvar_registry *source = qa_app_prepared_startup_source(prepared);
      qa_cvar_registry_options registry;
      memset(&registry, 0, sizeof registry);
      registry.dialect = qa_cvar_registry_dialect(source);
      registry.context = qa_cvar_registry_context(source);
      registry.print = (qa_command_print_fn)open_print; registry.print_ctx = s;
      r->borrowed_source = checked(qa_cvar_registry_create(&registry));
      qa_save_value *transfer = checked(qa_cvar_registry_capture_world_transfer_state(source));
      qa_cvar_registry_restore_save_state(r->borrowed_source, transfer);
      qa_save_value_free(transfer);
      if (s->options.q3_map_launch != NULL) qa_app_apply_q3_map_launch(r->borrowed_source, s->options.q3_map_launch, NULL);
      const qa_app_q3_map_launch *launch = s->options.q3_map_launch;
      qa_app_startup_rules rules = qa_app_resolve_startup_rules(s->options, r->borrowed_source,
          launch != NULL && launch->has_max_clients ? launch->max_clients : s->default_capacity,
          s->definitions, s->definition_count, launch == NULL);
      s->options = rules.options;
      s->borrowed_capacity = rules.max_clients;
    } else {
      size_t prepared_count = 0;
      const qa_app_prepared_seat *prepared_seats = qa_app_prepared_startup_seats(prepared, &prepared_count);
      qa_app_profile_seat *seats = checked(calloc(s->seat_count + 1, sizeof *seats));
      for (size_t i = 0; i < s->seat_count; i++) {
        qa_session_seat *seat = s->seats[i];
        qa_seat_id id = qa_session_seat_id(seat);
        qa_client_id seat_client = qa_session_client_id(qa_session_seat_client(seat));
        qa_seat_input *input = NULL;
        for (size_t j = 0; j < prepared_count && input == NULL; j++)
          if (qa_same_seat(prepared_seats[j].id, id) && prepared_seats[j].context.origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
              && qa_same_client(prepared_seats[j].context.origin.client, seat_client)) input = prepared_seats[j].input;
        if (input == NULL) {
          qa_seat_input_options input_options;
          memset(&input_options, 0, sizeof input_options);
          input_options.seat = id; input_options.dialect = movement;
          input_options.context.session = qa_engine_session_id(s->session);
          input_options.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
          input_options.context.origin.seat = id; input_options.context.origin.client = seat_client;
          input_options.commands = qa_app_prepared_startup_commands(prepared);
          input = checked(qa_seat_input_create(&input_options));
        }
        seats[i].seat = seat; seats[i].input = input;
      }
      r->profile_scripts = qa_app_open_configuration_scripts(&s->options, r->configuration, &s->input_config);
      qa_app_prepare_profile_args args;
      memset(&args, 0, sizeof args);
      args.prepared = prepared;
      args.seats = seats; args.seat_count = s->seat_count;
      args.content = r->configuration; args.catalog = s->catalog;
      args.settings = &s->input_config;
      args.shared = qa_app_image_settings_cvars(s->borrowed_images.settings);
      args.scripts = r->profile_scripts;
      args.print = (qa_command_print_fn)open_print; args.print_ctx = s;
      args.source_archive = r->source_archive; args.source_archive_count = r->source_archive_count;
      args.default_capacity = s->default_capacity;
      args.next_frame = open_next_frame; args.next_frame_ctx = s;
      r->profile = checked(qa_app_prepare_profile_configuration(&s->options, &args));
      free(seats);
      s->options = *qa_app_prepared_profile_options(r->profile);
      s->borrowed_capacity = qa_app_prepared_profile_max_clients(r->profile);
      r->borrowed_source = qa_app_prepared_profile_source(r->profile);
    }
  }
  if (!restored && !borrowed) {
    r->startup_scripts = qa_app_open_configuration_scripts(&s->options, r->configuration, &s->input_config);
    qa_app_prepare_initial_args args;
    memset(&args, 0, sizeof args);
    args.catalog = s->catalog; args.content = r->configuration; args.session = s->session; args.identity = s->identity;
    args.settings = &s->input_config; args.scripts = r->startup_scripts;
    args.print = (qa_command_print_fn)open_print; args.print_ctx = s;
    args.source_archive = r->source_archive; args.source_archive_count = r->source_archive_count;
    args.default_capacity = s->default_capacity;
    args.next_frame = open_next_frame; args.next_frame_ctx = s;
    args.on_prepared = open_on_prepared; args.on_prepared_ctx = s;
    r->startup = checked(qa_app_prepare_initial_configuration(&s->options, &args));
    /* prepareInitialConfiguration fills localSeats with the seats it creates. */
    size_t prepared_count = 0;
    const qa_app_prepared_seat *prepared_seats = qa_app_prepared_startup_seats(qa_app_prepared_initial_startup(r->startup), &prepared_count);
    for (size_t i = 0; i < prepared_count; i++) {
      if (prepared_seats[i].context.origin.kind != QA_COMMAND_ORIGIN_LOCAL_SEAT) continue;
      qa_session_seat *seat = qa_app_prepared_initial_local_seat(r->startup, prepared_seats[i].context.origin.client);
      if (seat != NULL) open_local_seat_set(s, prepared_seats[i].context.origin.client, seat);
    }
  }
  if (r->startup != NULL) s->options = *qa_app_prepared_initial_options(r->startup);
  if (!restored) {
    qa_cvar_registry *configured = r->borrowed_source != NULL ? r->borrowed_source
        : r->startup != NULL ? qa_app_prepared_startup_source(qa_app_prepared_initial_startup(r->startup)) : NULL;
    if (configured != NULL) s->options = qa_app_configured_weapon_behavior_options(s->options, configured);
    /* prepareConfiguredApplicationRecipe(catalog, options, recipe): the native content loader applies the configured
     * weapon behavior to a given recipe (content.c configured_backing), so the recipe passes through unchanged here. */
  }
  open_stage(s, "Loading map...");
  if (!restored) {
    r->content = qa_app_load_application_content_native((qa_app_content_host){0}, qa_application_options_copy(&s->options), s->recipe,
        s->recipe != NULL, NULL, (qa_content_catalog *)s->catalog, NULL);
    if (r->content == NULL) qa_range_error("Out of memory");
  }
  qa_app_loaded_content *content = r->content;
  if (borrowed && r->profile == NULL) qa_app_configuration_content_close(r->configuration);
  qa_q3_application_product q3_product;
  if (qa_app_loaded_content_q3_product(content, &q3_product)) { s->options.has_q3_product = true; s->options.q3_product = q3_product; }
  if (!restored) {
    qa_cvar_registry *source = r->borrowed_source != NULL ? r->borrowed_source
        : r->startup != NULL ? qa_app_prepared_startup_source(qa_app_prepared_initial_startup(r->startup)) : NULL;
    size_t count = 0;
    qa_app_name_value *values;
    if (source != NULL) {
      qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(source, 0, &count);
      values = checked(calloc(count + 1, sizeof *values));
      for (size_t i = 0; i < count; i++) {
        values[i].name = checked(strdup(snapshots[i].name));
        values[i].value = checked(strdup(snapshots[i].latched_value != NULL ? snapshots[i].latched_value : snapshots[i].value));
        qa_cvar_read_free(&snapshots[i]);
      }
      free(snapshots);
    } else {
      count = r->source_archive_count;
      values = checked(calloc(count + 1, sizeof *values));
      for (size_t i = 0; i < count; i++) {
        values[i].name = checked(strdup(r->source_archive[i].name));
        values[i].value = checked(strdup(r->source_archive[i].value));
      }
    }
    size_t team_count = 0;
    qa_app_name_value *team = s->options.team_arena_skirmish == NULL ? NULL
        : qa_app_team_arena_source_cvars(s->options.team_arena_skirmish, values, count, &team_count);
    const qa_app_q3_map_launch *launch = s->options.q3_map_launch;
    size_t launch_count = launch == NULL ? 0 : launch->cvar_count;
    qa_app_source_value *all = checked(calloc(count + team_count + launch_count + 1, sizeof *all));
    size_t total = 0;
    for (size_t i = 0; i < count; i++) all[total++] = (qa_app_source_value){values[i].name, values[i].value};
    for (size_t i = 0; i < team_count; i++) all[total++] = (qa_app_source_value){team[i].name, team[i].value};
    for (size_t i = 0; i < launch_count; i++) all[total++] = (qa_app_source_value){launch->cvars[i].name, launch->cvars[i].value};
    qa_app_preflight_application_match(open_preflight_content(content), s->options.mode, all, total);
    free(all);
    qa_app_name_values_free(team, team_count);
    qa_app_name_values_free(values, count);
  }
  open_stage(s, "Preparing world...");
  qa_app_sim_monster_navigation_content navigation;
  memset(&navigation, 0, sizeof navigation);
  navigation.ctx = content;
  navigation.recipe = qa_app_loaded_content_recipe(content);
  navigation.world = open_decoded_world(content);
  navigation.mounts = (qa_content_mounted *)qa_app_loaded_content_mounts(content);
  navigation.for_content = open_content_for;
  r->monster_navigation = qa_app_sim_preload_application_monster_navigation(navigation);
  if (!restored && s->options.authored_campaign_start) {
    qa_catalog_mount_plan_opener opener = {NULL, open_plan};
    bool found = false;
    if (!qa_content_catalog_authored_starts_for((qa_content_catalog *)qa_app_loaded_content_catalog(content), opener,
            qa_app_loaded_content_recipe(content)->map.entities.content, &r->authored, &found)) qa_range_error("Out of memory");
    r->has_authored = true;
    r->has_authored_start = qa_app_authored_campaign_start_for(found ? &r->authored : NULL, s->options.map, &r->authored_start);
  }
  double max_clients = r->startup != NULL ? qa_app_prepared_initial_max_clients(r->startup)
      : r->has_saved_settings ? r->saved_settings.max_clients
      : s->options.team_arena_skirmish != NULL ? ((const qa_app_team_arena_skirmish *)s->options.team_arena_skirmish)->max_clients
      : s->borrowed_capacity;
  for (size_t i = 0; i < s->seat_count; i++) max_clients = qa_js_max(max_clients, (double)s->seat_clients[i].slot + 1);
  if (r->borrowed_source != NULL) {
    char text[40];
    qa_net_js_number(max_clients, text, sizeof text);
    qa_application_cvar_set(r->borrowed_source, qa_cvar_registry_dialect(r->borrowed_source) == QA_COMMAND_DIALECT_Q3 ? "sv_maxclients" : "maxclients", text, true);
  }
  s->candidate_graph = checked(malloc(sizeof *s->candidate_graph));
  qa_source_debug_graph_init(s->candidate_graph);
  /* loadSimulation({...}) */
  qa_app_simulation_options request;
  memset(&request, 0, sizeof request);
  request.dedicated = s->options.dedicated;
  request.source_archive = r->source_archive; request.source_archive_count = r->source_archive_count;
  if (r->has_authored_start) {
    request.start_items = r->authored_start.start_items;
    if (strcmp(r->authored_start.target->kind, "map") == 0) {
      request.initial_spawn_point = r->authored_start.target->spawn_point;
      r->q2_next_server = qa_app_q2_next_server_command(r->authored_start.target);
      request.q2_next_server = r->q2_next_server;
    }
  }
  if (r->borrowed_source != NULL) request.source_registry = r->borrowed_source;
  else if (r->startup != NULL) request.source_registry = qa_app_prepared_startup_source(qa_app_prepared_initial_startup(r->startup));
  const qa_app_team_arena_skirmish *skirmish = s->options.team_arena_skirmish;
  if (borrowed && !restored && skirmish != NULL) {
    bool every = true;
    for (size_t i = 0; i < skirmish->cvar_count && every; i++) {
      char *value = r->borrowed_source == NULL ? NULL : qa_application_cvar_string(r->borrowed_source, skirmish->cvars[i].name);
      every = value != NULL && strcmp(value, skirmish->cvars[i].value) == 0;
      free(value);
    }
    if (!every) {
      size_t baseline_count = 0;
      qa_app_name_value *baseline = NULL;
      if (r->borrowed_source != NULL) {
        qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(r->borrowed_source, 0, &baseline_count);
        baseline = checked(calloc(baseline_count + 1, sizeof *baseline));
        for (size_t i = 0; i < baseline_count; i++) {
          baseline[i].name = checked(strdup(snapshots[i].name)); baseline[i].value = checked(strdup(snapshots[i].value));
          qa_cvar_read_free(&snapshots[i]);
        }
        free(snapshots);
      } else {
        baseline_count = r->source_archive_count;
        baseline = checked(calloc(baseline_count + 1, sizeof *baseline));
        for (size_t i = 0; i < baseline_count; i++) {
          baseline[i].name = checked(strdup(r->source_archive[i].name)); baseline[i].value = checked(strdup(r->source_archive[i].value));
        }
      }
      s->q3_cvars = qa_app_team_arena_source_cvars(skirmish, baseline, baseline_count, &s->q3_cvar_count);
      qa_app_name_values_free(baseline, baseline_count);
      request.q3_cvars = (const qa_app_sim_cvar_name_value *)s->q3_cvars; request.q3_cvar_count = s->q3_cvar_count;
    }
  }
  qa_application_guest_options(content, &s->options, &s->host, open_guest_commands, s, s->candidate_graph, open_native_command, s, &request);
  request.nav_ctx = s; request.prepare_rerelease_navigation = open_rerelease_navigation;
  request.prepared_quakec = qa_app_loaded_content_prepared_quakec(content);
  request.monster_navigation = r->monster_navigation;
  request.identity = *s->identity;
  size_t behavior_count = 0;
  const qa_app_prepared_weapon_behavior *behaviors = qa_app_loaded_content_prepared_weapon_behaviors(content, &behavior_count);
  s->weapon_behaviors = checked(calloc(behavior_count + 1, sizeof *s->weapon_behaviors));
  for (size_t i = 0; i < behavior_count; i++) s->weapon_behaviors[i] = &behaviors[i];
  request.weapon_behaviors = s->weapon_behaviors; request.weapon_behavior_count = behavior_count;
  const qa_executable_recipe *content_recipe = qa_app_loaded_content_recipe(content);
  request.recipe = *content_recipe;
  s->world = qa_app_loaded_content_world(content);
  request.world = &s->world;
  request.mounts = qa_app_loaded_content_mounts(content);
  /* content.preparedMods (owned by the content). */
  request.prepared_mods = qa_app_loaded_content_prepared_mods(content, &request.prepared_mod_count);
  s->enabled_mods = checked(calloc(content_recipe->mod_count + 1, sizeof *s->enabled_mods));
  for (size_t i = 0; i < content_recipe->mod_count; i++) s->enabled_mods[i] = content_recipe->mods[i]->selection;
  s->enabled.items = s->enabled_mods; s->enabled.count = content_recipe->mod_count;
  request.enabled_mods = &s->enabled;
  request.mod_commands = r->mod_commands;
  {
    char *default_root = s->options.user_content_root == NULL ? checked(qa_default_user_content_root()) : NULL;
    s->mod_files = checked(qa_mod_user_files_create(s->options.user_content_root != NULL ? s->options.user_content_root : default_root));
    free(default_root);
  }
  request.mod_files = s->mod_files;
  request.skill = s->options.skill; request.mode = s->options.mode; request.seed = s->options.seed;
  if (s->options.server_profile != NULL) request.server_profile = s->options.server_profile;
  if (restored) {
    request.restore = initial_save;
    qa_client_id *clients = checked(calloc(s->restored_count + 1, sizeof *clients));
    for (size_t i = 0; i < s->restored_count; i++) clients[i] = qa_session_client_id(s->restored_clients[i]);
    request.restored_clients = clients; request.restored_client_count = s->restored_count;
    qa_application_keep_record_open(s, clients);
  }
  request.max_clients = max_clients;
  request.has_player_colors = true;
  request.player_color_top = open_clamp_color(s->options.has_character_top_color, s->options.character_top_color);
  request.player_color_bottom = open_clamp_color(s->options.has_character_bottom_color, s->options.character_bottom_color);
  request.prompt_ctx = s; request.prompt_supported = open_prompt_supported;
  request.identity_ctx = s; request.player_identity = open_player_identity;
  r->simulation = qa_app_shared_simulation_load(request, open_next_frame, s);
  if (r->simulation == NULL) qa_range_error("Out of memory");
  qa_app_shared_simulation *simulation = r->simulation;
  if (s->options.has_bot_skill) {
    qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
    if (source == NULL) qa_raise("Error", 0, 0, "--bot-skill requires the Quake III game provider");
    char text[40];
    qa_net_js_number(s->options.bot_skill, text, sizeof text);
    qa_application_cvar_set(qa_app_q3_source_runtime_host(source)->cvars, "g_spSkill", text, true);
  }
  /* const { authoredCampaignStart, q3MapLaunch, ...retainedOptions } = options */
  qa_app_options retained = s->options;
  retained.authored_campaign_start = false;
  retained.q3_map_launch = NULL;
  qa_application *application = qa_application_construct(retained, content, s->session, simulation, s->host, s->identity,
      s->seat_clients, s->seats, s->seat_count, s->input_config, s->ownership);
  s->has_input_config = false; /* the application owns inputConfig */
  s->application = application;
  application->owns_identity = s->owns_identity;
  s->owns_identity = false;
  application->open_record = r;
  s->record = NULL;
  application->simulation_view.simulation = simulation;
  if (!borrowed) (void)qa_engine_session_attach_world(s->session, qa_application_simulation_contract(&application->simulation_view));
  s->startup_owner.application = application;
  if (s->has_startup_demos) {
    application->has_owned_demos = true;
    application->owned_demos_service = s->startup_demos;
    application->owned_demos_release.fn = open_release_startup_demos; application->owned_demos_release.ctx = s;
  }
  free(application->operator_output);
  application->operator_output = s->operator_output;
  s->operator_output = NULL;
  {
    qa_config_store store = qa_application_source_config(&s->options, content);
    r->operator_state = checked(qa_app_server_operator_state_open(&store, "settings/server-operator.json"));
    qa_config_store_destroy(&store);
    application->operator_state = r->operator_state;
  }
  free(application->debug_graph);
  application->debug_graph = s->candidate_graph;
  s->candidate_graph = NULL;
  for (size_t i = 0; i < s->staged_clients_added_count; i++) open_app_push_client(&application->source_clients_added, &application->source_clients_added_count, &application->source_clients_added_capacity, s->staged_clients_added[i]);
  for (size_t i = 0; i < s->staged_clients_removed_count; i++) open_app_push_client(&application->source_clients_removed, &application->source_clients_removed_count, &application->source_clients_removed_capacity, s->staged_clients_removed[i]);
  for (size_t i = 0; i < s->staged_seats_added_count; i++) open_app_push_seat(&application->source_seats_added, &application->source_seats_added_count, &application->source_seats_added_capacity, s->staged_seats_added[i]);
  for (size_t i = 0; i < s->staged_seats_removed_count; i++) open_app_push_seat(&application->source_seats_removed, &application->source_seats_removed_count, &application->source_seats_removed_capacity, s->staged_seats_removed[i]);
  if (!borrowed)
    for (size_t i = 0; i < s->seat_count; i++) {
      qa_session_client *seat_client = qa_session_seat_client(s->seats[i]);
      open_app_set_connection(application, seat_client, qa_session_client_connection(seat_client));
    }
  application->prepared_startup = borrowed ? client->prepared : r->startup != NULL ? qa_app_prepared_initial_startup(r->startup) : NULL;
  if (r->profile != NULL) application->configuration_scripts = qa_app_prepared_profile_scripts(r->profile);
  else if (r->startup != NULL) application->configuration_scripts = qa_app_prepared_initial_scripts(r->startup);
  else if (restored) {
    r->restored_scripts = checked(qa_app_source_console_scripts(content, &s->options, &application->input_config));
    application->configuration_scripts = r->restored_scripts;
  }
  application->profile_configuration = r->profile;
  qa_application_adopt_image_settings(application, s->has_borrowed_images ? s->borrowed_images.settings
      : r->startup != NULL ? qa_app_prepared_initial_image(r->startup) : NULL);
  if (restored) {
    qa_app_campaign_unit_restore(application->campaign_unit, initial_save);
    qa_app_override_seat *override_seats = checked(calloc(s->seat_count + 1, sizeof *override_seats));
    for (size_t i = 0; i < s->seat_count; i++) {
      override_seats[i].seat = (double)qa_session_seat_id(s->seats[i]).index;
      override_seats[i].client = (double)qa_session_client_id(qa_session_seat_client(s->seats[i])).slot;
    }
    qa_app_team_arena_overrides overrides;
    memset(&overrides, 0, sizeof overrides);
    bool has_overrides = qa_app_read_team_arena_overrides(initial_save, override_seats, s->seat_count, &overrides);
    free(override_seats);
    if (has_overrides) {
      application->team_arena_overrides = checked(malloc(sizeof *application->team_arena_overrides));
      *application->team_arena_overrides = overrides;
    }
    if (qa_application_is_team_arena_skirmish(application, NULL) && application->team_arena_overrides == NULL)
      qa_application_print(application, "Legacy Team Arena save has no per-seat timer baseline; current timer preferences will be preserved.\n");
  }
  if (r->startup != NULL) qa_application_forward_startup_commands(qa_app_prepared_initial_startup(r->startup), application);
  if (r->startup != NULL) {
    size_t count = 0;
    const qa_app_configuration_command_request *requests = qa_app_prepared_initial_requests(r->startup, &count);
    for (size_t i = 0; i < count; i++) open_admit_request(application, &requests[i]);
  }
  if (r->profile != NULL) {
    size_t count = 0;
    const qa_app_configuration_command_request *requests = qa_app_prepared_profile_requests(r->profile, &count);
    for (size_t i = 0; i < count; i++) open_admit_request(application, &requests[i]);
  }
  if (s->preferences != NULL) application->frontend_overrides = *s->preferences;
  else memset(&application->frontend_overrides, 0, sizeof application->frontend_overrides);
  application->elapsed = r->has_saved_settings ? r->saved_settings.host_milliseconds : 0;
  if (restored && qa_app_shared_sim_q3_source(simulation) != NULL && r->saved_bots == NULL)
    qa_raise("Error", 0, 0, "Q3 application restoration requires saved bot service state");
  qa_application_bind_source_commands(application, restored);
  s->guest_console = application->source_commands;
  qa_mod_commands_flush(r->mod_commands);
  qa_application_prepare_guest_bots(application, content, simulation);
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  if (!s->options.dedicated && guest != NULL) {
    open_local_guest(s, application, content, simulation, restored);
  }
  if (restored && guest != NULL) qa_app_q3_qvm_server_game_complete_restore(guest, qa_application_guest_output(application, NULL, true));
  if (s->options.dedicated) qa_application_open_dedicated_console(application);
  else {
    if (r->startup == NULL && !borrowed) qa_app_view_settings_load(application->view_settings, &application->input_config);
    if (application->image_settings == NULL) {
      qa_app_audio_preferences audio;
      memset(&audio, 0, sizeof audio);
      bool has_audio = qa_app_load_audio_settings(&application->input_config, &audio);
      qa_app_image_settings_options image;
      memset(&image, 0, sizeof image);
      image.has_audio_format = true;
      image.audio_output_format = has_audio ? audio.output_format : qa_default_audio_output_format;
      if (has_audio) { free(audio.menu_track); free(audio.device_name); }
      image.defer_persistence = true;
      image.context.session = qa_engine_session_id(s->session);
      image.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
      image.dialect = qa_application_source_dialect(application, NULL);
      image.has_gamma = true; image.gamma = s->options.gamma;
      if (s->options.has_render_worker) { image.has_render_worker = true; image.render_worker = s->options.render_worker; }
      image.has_display_width = s->options.has_display_width; image.display_width = s->options.display_width;
      image.has_display_height = s->options.has_display_height; image.display_height = s->options.display_height;
      image.has_display_gamma = s->options.has_display_gamma; image.display_gamma = s->options.display_gamma;
      image.user_content_root = s->options.user_content_root;
      qa_application_printer printer = qa_application_frontend_print(&s->host, &s->startup_owner);
      image.print = printer.print; image.print_ctx = printer.ctx;
      application->open_record->frontend_printer = printer.record;
      application->image_settings = checked(qa_app_image_settings_open(image));
      application->open_record->image_settings = application->image_settings;
    }
    qa_app_image_view_binding *binding = qa_app_image_settings_bind_view_settings(application->image_settings, application->view_settings);
    application->release_view_cvars.fn = open_release_view_binding; application->release_view_cvars.ctx = binding;
    qa_application_open_graphical(application, initial_save);
  }
  if (borrowed) {
    if (!s->has_borrowed_images) qa_raise("Error", 0, 0, "Borrowed source has no image candidate");
    qa_application_open_network(application);
    qa_application_publish_borrowed(application, client, &s->borrowed_images);
  } else {
    application->source_published = true;
    qa_application_publish_keys(application);
  }
  open_stage(s, "Starting game...");
  if (!restored) application->bots = qa_application_create_bots(application, content, simulation, NULL);
  else if (r->saved_bots != NULL) {
    qa_application_create_bots_setup setup;
    memset(&setup, 0, sizeof setup);
    setup.restore = r->decoded_bots;
    setup.resolve_client = open_resolve_bot; setup.resolve_ctx = s;
    application->bots = qa_application_create_bots(application, content, simulation, &setup);
  }
  if (!borrowed) qa_application_open_network(application);
  if (guest != NULL && s->options.dedicated && !restored) {
    qa_app_q3_qvm_server_game_initialize(guest, qa_application_guest_output(application, NULL, true), "new", NULL, 0);
    if (application->source_commands != NULL) open_execute_commands(application);
  }
  qa_application_ranking_frame(application);
  if (!restored && !borrowed) qa_application_commands(application);
  qa_application_printf(application, "Loaded %s with %s and %s.\n", content_recipe->map.geometry.requested_path,
      content_recipe->movement.provider, content_recipe->character.appearance.provider);
  qa_application_enable_startup_persistence(application);
  if (!restored) {
    if (skirmish != NULL) qa_application_start_team_arena_skirmish(application, skirmish);
    qa_application_autosave_level(application);
    struct qa_application_open_record *kept = application->open_record;
    if (kept->has_authored_start && strcmp(kept->authored_start.target->kind, "map") != 0) {
      qa_app_simulation_travel carry = qa_app_shared_sim_capture_travel(simulation, NULL);
      qa_application_advance_q2_travel(application, kept->authored_start.target, &carry);
    }
  } else qa_application_request_restored_scores(application);
  s->result = application;
}

/* ---- outer openSource ---- */
static void open_cleanup_body(void *ctx) {
  open_state *s = ctx;
  struct qa_application_open_record *r = s->record;
  if (s->has_startup_demos) open_release_startup_demos(s);
  if (s->ownership.borrowed_client == NULL && s->session != NULL) qa_engine_session_close(s->session);
  for (size_t i = 0; i < s->staged_seats_added_count; i++) qa_session_seat_close(s->staged_seats_added[i]);
  for (size_t i = 0; i < s->staged_clients_added_count; i++) qa_session_client_close(s->staged_clients_added[i]);
  if (r != NULL && r->profile != NULL) qa_app_console_routing_close(qa_app_prepared_profile_routing(r->profile));
  if (r != NULL && r->profile_scripts != NULL) qa_app_console_script_files_close(r->profile_scripts);
  if (r != NULL && r->startup != NULL && qa_app_prepared_initial_image(r->startup) != NULL)
    qa_app_image_settings_close(qa_app_prepared_initial_image(r->startup));
  if (r != NULL && r->startup_scripts != NULL) qa_app_console_script_files_close(r->startup_scripts);
  if (r != NULL && r->content != NULL) qa_app_loaded_content_close(r->content);
}
typedef struct open_close_application { qa_application *app; const qa_save_image *initial_save; } open_close_application;
static void open_close_application_body(void *ctx) {
  open_close_application *c = ctx;
  if (c->initial_save != NULL) {
    qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(c->app->world_simulation);
    if (guest != NULL) qa_app_q3_qvm_server_game_discard(guest);
  }
  qa_application_close(c->app);
}
static void open_close_configuration_body(void *ctx) { qa_app_configuration_content_close(ctx); }
static void open_close_content_body(void *ctx) { qa_app_loaded_content_close(ctx); }

qa_application *qa_application_open_source(qa_application_ownership ownership, qa_app_options options, qa_application_host host,
    const qa_executable_recipe *recipe, const qa_app_frontend_preference_overrides *preferences, const qa_save_image *initial_save) {
  open_state *s = checked(calloc(1, sizeof *s));
  s->ownership = ownership; s->options = options; s->recipe = recipe; s->preferences = preferences; s->initial_save = initial_save;
  s->operator_output = checked(calloc(1, sizeof *s->operator_output));
  struct qa_application_open_record *r = checked(calloc(1, sizeof *r));
  s->record = r;
  s->host = qa_application_operator_host_wrap(host, s->operator_output, &r->operator_host_record);
  const char *network = options.network_kind;
  if (strcmp(network, "qw-client") == 0 || strcmp(network, "q1-client") == 0 || strcmp(network, "q2-client") == 0
      || strcmp(network, "q3-client") == 0 || strcmp(network, "unified-client") == 0)
    qa_raise("Error", 0, 0, "Remote clients require RemoteApplication without a local simulation");
  if (initial_save != NULL && options.dedicated) qa_app_require_component_client_presentation(initial_save);
  if (initial_save != NULL) {
    r->saved_settings = qa_app_sim_saved_simulation_settings(*initial_save);
    r->has_saved_settings = true;
    r->saved_bots = qa_app_sim_saved_bot_checkpoint(*initial_save);
    if (r->saved_bots != NULL) {
      r->decoded_bots = checked(calloc(1, sizeof *r->decoded_bots));
      qa_app_sim_decode_application_bots_checkpoint(r->saved_bots->value, r->decoded_bots);
    }
  }
  if (initial_save != NULL && r->has_saved_settings) {
    if (strcmp(options.network_kind, "offline") != 0) qa_raise("Error", 0, 0, "Save/load unavailable while hosting a network game.");
    size_t humans = 0;
    for (size_t i = 0; i < r->saved_settings.client_slot_count; i++) if (!saved_bot_slot(s, r->saved_settings.client_slots[i])) humans++;
    if (!options.dedicated && (humans < 1 || humans > 4)) qa_raise("Error", 0, 0, "Saved local player count must be between one and four");
    /* const { botSkill, teamArenaSkirmish, serverProfile, serverProfilePath, ...frontendOptions } = options */
    s->options.has_bot_skill = false; s->options.team_arena_skirmish = NULL;
    s->options.server_profile = NULL; s->options.server_profile_path = NULL;
    s->options.map = (char *)initial_save->recipe.map.geometry.requested_path;
    s->options.skill = r->saved_settings.skill;
    s->options.mode = r->saved_settings.mode;
    s->options.seed = r->saved_settings.seed;
    s->options.seats = options.dedicated ? options.seats : (double)humans;
    s->recipe = &initial_save->recipe;
  }
  open_stage(s, initial_save == NULL ? "Preparing configuration..." : "Loading saved world...");
  if (initial_save == NULL) {
    r->configuration = checked(qa_app_open_initial_configuration_content_native(&s->options, s->recipe, NULL));
    s->catalog = qa_app_configuration_content_catalog(r->configuration);
  } else {
    r->content = qa_app_load_application_content_native((qa_app_content_host){0}, qa_application_options_copy(&s->options), s->recipe,
        true, NULL, NULL, NULL);
    if (r->content == NULL) qa_range_error("Out of memory");
    s->catalog = qa_app_loaded_content_catalog(r->content);
  }
  qa_caught_error error;
  if (!qa_try(open_body, s, &error)) {
    qa_caught_error cleanup;
    size_t failures = 1;
    bool published = false;
    if (s->application != NULL) {
      open_close_application close = {s->application, initial_save};
      if (!qa_try(open_close_application_body, &close, &cleanup)) failures++;
      published = s->ownership.borrowed_client != NULL && s->application->source_published;
    } else if (!qa_try(open_cleanup_body, s, &cleanup)) failures++;
    qa_caught_error preparation;
    bool preparation_failed = r->configuration != NULL ? !qa_try(open_close_configuration_body, r->configuration, &preparation)
        : r->content != NULL && s->application == NULL && !qa_try(open_close_content_body, r->content, &preparation);
    if (published) qa_raise("ClientSourcePublicationError", 0, (long long)failures, "%s", qa_app_client_source_publication_error);
    if (failures > 1) qa_raise("AggregateError", 0, (long long)failures, "Application initialization and cleanup failed");
    if (preparation_failed) qa_raise("AggregateError", 0, 2, "Application preparation and cleanup failed");
    qa_rethrow(&error);
  }
  qa_application *application = s->result;
  application->open_state = s;
  return application;
}

/* C-only GC of what openSource created (the application is closed). */
void qa_application_open_free(qa_application *app) {
  struct qa_application_open_record *r = app->open_record;
  open_state *s = app->open_state;
  if (r != NULL) {
    if (r->local_guest != NULL) {
      if (app->local_guest == r->local_guest) app->local_guest = NULL;
      free(r->local_guest->seat_ids);
      free(r->local_guest->seats);
      qa_event_ring_destroy(&r->local_guest->pending_commands);
      free(r->local_guest);
    }
    if (r->has_local_authority) qa_app_sim_q3_application_server_authority_destroy(r->local_authority);
    if (r->has_world_sound_owner) qa_identity_owner_destroy(&r->world_sound_owner);
    if (r->image_settings != NULL) {
      if (app->image_settings == r->image_settings) app->image_settings = NULL;
      qa_app_image_settings_destroy(r->image_settings);
    }
    free(r->frontend_printer);
    if (r->operator_state != NULL) {
      if (app->operator_state == r->operator_state) app->operator_state = NULL;
      qa_app_server_operator_state_destroy(r->operator_state);
    }
    if (r->simulation != NULL) {
      if (app->world_simulation == r->simulation) app->world_simulation = NULL;
      qa_app_shared_simulation_destroy(r->simulation);
    }
    if (r->monster_navigation != NULL) qa_app_sim_application_monster_navigation_destroy(r->monster_navigation);
    if (r->mod_commands != NULL) qa_mod_commands_destroy(r->mod_commands);
    if (r->startup != NULL) {
      if (app->prepared_startup == qa_app_prepared_initial_startup(r->startup)) app->prepared_startup = NULL;
      qa_app_prepared_initial_destroy(r->startup);
    }
    free(r->startup_demo_record);
    if (r->startup_scripts != NULL) qa_app_console_script_files_destroy(r->startup_scripts);
    if (r->profile_scripts != NULL) qa_app_console_script_files_destroy(r->profile_scripts);
    if (r->restored_scripts != NULL) qa_app_console_script_files_destroy(r->restored_scripts);
    if (app->configuration_scripts == r->startup_scripts || app->configuration_scripts == r->restored_scripts) app->configuration_scripts = NULL;
    if (r->content != NULL) {
      if (app->loaded_content == r->content) app->loaded_content = NULL;
      qa_app_loaded_content_destroy(r->content);
    }
    if (r->configuration != NULL) qa_app_configuration_content_destroy(r->configuration);
    if (r->session != NULL) {
      if (app->session == r->session) app->session = NULL;
      qa_engine_session_destroy(r->session);
    }
    if (r->has_saved_settings) { qa_save_value_free(r->saved_settings.value); free(r->saved_settings.client_slots); }
    if (r->decoded_bots != NULL) { qa_app_sim_decoded_bots_checkpoint_free(r->decoded_bots); free(r->decoded_bots); }
    if (r->saved_bots != NULL) { qa_save_value_free(r->saved_bots->value); free(r->saved_bots->connections); free(r->saved_bots); }
    qa_app_cvar_archive_entries_free(r->source_archive, r->source_archive_count);
    qa_save_value_free(r->server_profile_value);
    if (r->has_options_for_recipe) qa_app_options_for_recipe_free(&r->options_for_recipe);
    if (r->has_authored) qa_authored_start_catalog_free(&r->authored);
    free(r->q2_next_server);
    free(r->operator_host_record);
    /* The application's identity (destroyed after this) frees only its token; the name is no longer read. */
    free(r->identity_name);
    free(r);
    app->open_record = NULL;
  }
  if (s != NULL) {
    free(s->seat_clients); free(s->seats);
    free(s->restored_slots); free(s->restored_clients);
    free(s->staged_clients_added); free(s->staged_clients_removed);
    free(s->staged_seats_added); free(s->staged_seats_removed);
    for (size_t i = 0; i < s->kept_count; i++) free(s->kept[i]);
    free(s->kept);
    free(s->enabled_mods);
    free(s->weapon_behaviors);
    free((void *)s->definitions); /* serverDefinitionsForRecipe/Selection's array (the rows are static) */
    if (s->mod_files != NULL) qa_mod_user_files_destroy(s->mod_files);
    qa_app_name_values_free(s->q3_cvars, s->q3_cvar_count);
    if (s->has_borrowed_images) qa_app_image_client_settings_destroy(&s->borrowed_images);
    /* TS GC of the startup ClientDemoCommands; closeOwned released its startup and source bindings. */
    if (s->has_startup_demos) qa_app_client_demo_commands_destroy(s->startup_demos);
    free(s);
    app->open_state = NULL;
  }
}
