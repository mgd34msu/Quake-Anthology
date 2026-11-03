/* Port of src/app/bootstrap/remote-application.ts bindSeat (TS 2242-2690 in
 * the 2026-11-03 tree): the primary seat's input, UI, Q3 guest and world
 * presentation over the retained frontend, staged against candidate cvar
 * owners and published at once. Closures the published owners keep (the
 * candidate routing, the Q3 guest's connection view and command closures)
 * live as long as the application (TS GC: arena). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/q3_client/cvars.h"
#include "qa/app/bootstrap/q3_client/qvm.h"
#include "qa/app/bootstrap/q3_seat_client.h"
#include "qa/app/bootstrap/render_settings.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/input/mouse_settings.h"
#include "qa/input/user_command.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/game_state.h"

static void *xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}

/* ---- the remote as the presentation access (TS passes the remote) ------ */
static const qa_remote_presentation *remote_of(void *ctx) { return qa_remote_application_remote(ctx); }
static const qa_world_text *access_world_text(void *ctx, size_t *out_count) {
  return qa_app_remote_seat_presentation_world_text(remote_of(ctx), out_count);
}
static qa_app_sim_player_view access_player_view(void *ctx, qa_actor_id actor) {
  return qa_app_remote_seat_presentation_player_view(remote_of(ctx), actor);
}
static qa_app_sim_player_ui access_player_ui(void *ctx, qa_actor_id actor) {
  return qa_remote_presentation_player_ui(remote_of(ctx), actor);
}
static const qa_active_mod_client_presentation *access_mod_sources(void *ctx, size_t *out_count) {
  const qa_remote_presentation *remote = remote_of(ctx);
  if (remote->family != QA_APP_REMOTE_FAMILY_UNIFIED) { *out_count = 0; return NULL; }
  return qa_app_unified_remote_mod_client_sources(remote->unified, out_count);
}
static const qa_q3_character_view *access_character_views(void *ctx, size_t *out_count) {
  return qa_app_remote_seat_presentation_character_views(remote_of(ctx), out_count);
}
static const qa_app_simulation_presentation *access_presentations(void *ctx, size_t *out_count) {
  return qa_app_remote_seat_presentation_presentations(remote_of(ctx), out_count);
}
/* TS sourceForActor(actor).playerView(actor) (the input's player view over the local seats). */
static qa_app_sim_player_view source_player_view(void *ctx, qa_actor_id actor) {
  return qa_app_remote_seat_presentation_player_view(qa_remote_application_source_for_actor(ctx, actor), actor);
}
const qa_app_sim_presentation_access *qa_remote_application_remote_access(qa_remote_application *app) {
  qa_app_sim_presentation_access *access = &app->remote_view;
  memset(access, 0, sizeof *access);
  access->ctx = app;
  access->world_text = access_world_text;
  access->player_view = access_player_view;
  access->player_ui = access_player_ui;
  access->character_views = access_character_views;
  access->presentations = access_presentations;
  if (qa_remote_application_remote(app)->family == QA_APP_REMOTE_FAMILY_UNIFIED)
    access->mod_client_presentation_sources = access_mod_sources;
  return access;
}
const qa_app_sim_presentation_access *qa_remote_application_source_access(qa_remote_application *app) {
  qa_app_sim_presentation_access *access = &app->source_view;
  memset(access, 0, sizeof *access);
  access->ctx = app;
  access->player_view = source_player_view;
  return access;
}

/* ---- the candidate (TS locals of bindSeat) ----------------------------- */
typedef struct staged_transfer { qa_cvar_registry *registry; qa_cvar_transfer *transfer; } staged_transfer;
typedef struct bind_candidate {
  qa_remote_application *app;
  qa_q3_client_connection *connection; /* NULL = TS undefined */
  double generation;
  bool published;
  qa_session_seat *seat;
  qa_command_context context;
  qa_app_application_input *previous;
  qa_app_capture *previous_capture;
  qa_app_console_script_files *retired_scripts, *candidate_scripts;
  bool retired;
  qa_app_image_client_settings image;
  bool has_image;
  qa_cvar_registry **live; size_t live_count;
  staged_transfer *transfers; size_t transfer_count;
  qa_cvar_registry **visible; size_t visible_capacity;
  qa_remote_command *actions; size_t action_count, action_capacity;
  bool quit;
  qa_app_application_input *input;
  qa_app_seat_ui *ui;
  qa_app_application_q3_client *q3;
  qa_app_q3_client_cvars *guest_cvars;
  qa_remote_cinematic *candidate_cinematic;
  qa_mouse_settings mouse;
  qa_app_input_command_owner owner_view;
  qa_ui_setting_binding view_binding;
  char *pure;
  qa_app_world_seat_presentation *presentation;
  qa_app_capture *capture;
  /* the Q3 guest's connection view */
  char *text;
  char **tokens; size_t token_count;
  qa_q3_source_game_state_record record; bool has_record;
  qa_shared_scene_queries *q3_queries; /* TS q3Scene.queries, read before the cvar owners are staged */
} bind_candidate;
static void bind_assert(bind_candidate *c) {
  qa_remote_application *app = c->app;
  if ((!c->published && app->closing) || app->closed || c->generation != app->world_load_generation)
    qa_raise("Error", 0, 0, "Remote seat loading was cancelled");
}
static void bind_assert_ctx(void *ctx) { bind_assert(ctx); }
static qa_cvar_registry *staged(bind_candidate *c, qa_cvar_registry *registry) {
  if (registry == qa_app_image_settings_cvars(c->app->image_settings)) return c->image.settings != NULL
      ? qa_app_image_settings_cvars(c->image.settings) : registry;
  for (size_t i = 0; i < c->transfer_count; i++) if (c->transfers[i].registry == registry) return c->transfers[i].transfer->cvars;
  qa_raise("Error", 0, 0, "Remote candidate reached an unstaged cvar owner");
}
static qa_cvar_registry *candidate_owner(void *ctx, const char *name, qa_command_context source) {
  bind_candidate *c = ctx;
  qa_command_cvar_routing routing = c->app->client_command_owner->routing;
  qa_cvar_registry *registry = routing.owner(routing.ctx, name, source);
  return c->published ? registry : staged(c, registry);
}
static qa_cvar_registry *const *candidate_visible(void *ctx, qa_command_context source, size_t *out_count) {
  bind_candidate *c = ctx;
  qa_command_cvar_routing routing = c->app->client_command_owner->routing;
  size_t count = 0;
  qa_cvar_registry *const *visible = routing.visible(routing.ctx, source, &count);
  if (count > c->visible_capacity) {
    qa_cvar_registry **grown = realloc(c->visible, count * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    c->visible = grown;
    c->visible_capacity = count;
  }
  for (size_t i = 0; i < count; i++) c->visible[i] = c->published ? visible[i] : staged(c, visible[i]);
  *out_count = count;
  return c->visible;
}
static void add_live(bind_candidate *c, qa_cvar_registry *registry) {
  for (size_t i = 0; i < c->live_count; i++) if (c->live[i] == registry) return;
  c->live[c->live_count++] = registry;
}
/* TS execute(name, args, id, source) */
static void candidate_execute(bind_candidate *c, const char *name, const char *const *args, size_t arg_count,
    const qa_seat_id *seat, const qa_command_context *source) {
  if (c->published) {
    qa_remote_application_queue_command(c->app, name, args, arg_count, seat, source);
    return;
  }
  if (c->action_count == c->action_capacity) {
    size_t capacity = c->action_capacity == 0 ? 4 : c->action_capacity * 2;
    qa_remote_command *grown = realloc(c->actions, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    c->actions = grown;
    c->action_capacity = capacity;
  }
  qa_remote_command *action = &c->actions[c->action_count++];
  memset(action, 0, sizeof *action);
  action->name = strdup(name);
  action->args = xcalloc(arg_count, sizeof *action->args);
  action->arg_count = arg_count;
  for (size_t i = 0; i < arg_count; i++) action->args[i] = strdup(args[i]);
  if (seat != NULL) { action->has_seat = true; action->seat = *seat; }
  if (source != NULL) { action->has_source = true; action->source = qa_remote_command_context_copy(source); }
}
static void candidate_quit(void *ctx) {
  bind_candidate *c = ctx;
  if (c->published) qa_remote_application_request_quit(c->app);
  else c->quit = true;
}
static void input_execute(void *ctx, const char *name, const char *const *args, size_t arg_count, const qa_seat_id *seat,
    const qa_command_context *source) {
  candidate_execute(ctx, name, args, arg_count, seat, source);
}
static void input_print(void *ctx, const char *text) {
  bind_candidate *c = ctx;
  if (c->app->host.base.print != NULL) c->app->host.base.print(c->app->host.base.ctx, text);
}
static bool input_startup_reader(void *ctx, qa_app_console_script_files *scripts, const qa_app_options *options,
    qa_app_prepared_scoped_read_fn *out_read, void **out_read_ctx) {
  (void)options; /* TS startupReader: selected => this.startupReader(selected) */
  bind_candidate *c = ctx;
  qa_remote_application_startup_reader(c->app, scripts, out_read, out_read_ctx);
  return true;
}
static double input_capacity(void *ctx) {
  bind_candidate *c = ctx;
  return qa_remote_application_source(c->app)->kind == QA_REMOTE_SOURCE_LIVE ? 4 : 1;
}
static bool input_can_remove(void *ctx, qa_seat_id seat) {
  (void)seat; /* TS canRemoveLocalPlayer: () => this.source.kind === "live" */
  bind_candidate *c = ctx;
  return qa_remote_application_source(c->app)->kind == QA_REMOTE_SOURCE_LIVE;
}
/* The Q3 client of seat id: the primary presentation's or an open peer view's. */
static qa_app_application_q3_client *seat_q3_client(qa_remote_application *app, qa_seat_id id) {
  if (qa_same_seat(id, qa_remote_application_seat_id(app)))
    return qa_remote_presentation_q3_client(qa_remote_application_presentation(app));
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (!peer->closed && qa_same_seat(qa_session_seat_id(peer->seat), id) && peer->view != NULL)
      return qa_remote_presentation_q3_client(qa_app_remote_seat_view_presentation(peer->view));
  }
  return NULL;
}
static bool client_owns_seat(qa_app_application_q3_client *client, qa_seat_id id) {
  return qa_same_seat(qa_session_seat_id(qa_app_application_q3_client_options_of(client)->local->player.seat), id);
}
static bool input_client_captures(void *ctx, qa_seat_id id) {
  bind_candidate *c = ctx;
  qa_remote_application *app = c->app;
  if (app->cinematic != NULL) return true;
  qa_app_application_q3_client *client = seat_q3_client(app, id);
  return client != NULL && client_owns_seat(client, id) && qa_app_application_q3_client_captures_input(client);
}
static bool input_client_input(void *ctx, const qa_seat_input_event *event) {
  bind_candidate *c = ctx;
  qa_remote_application *app = c->app;
  if (c->published && app->cinematic != NULL) return qa_app_campaign_cinematic_input(app->cinematic->movie, event);
  qa_app_application_q3_client *client = seat_q3_client(app, event->seat);
  if (!c->published || client == NULL || !client_owns_seat(client, event->seat) || !qa_app_application_q3_client_captures_input(client))
    return false;
  if (app->client_input_count == app->client_input_capacity) {
    size_t capacity = app->client_input_capacity == 0 ? 8 : app->client_input_capacity * 2;
    qa_remote_client_input *grown = realloc(app->client_inputs, capacity * sizeof *grown);
    if (grown == NULL) qa_range_error("Out of memory");
    app->client_inputs = grown;
    app->client_input_capacity = capacity;
  }
  qa_remote_client_input *entry = &app->client_inputs[app->client_input_count++];
  entry->generation = app->world_load_generation;
  entry->client = client;
  entry->event = *event;
  return true;
}
static double input_now(void *ctx) { (void)ctx; return qa_app_remote_access_now_milliseconds(NULL); }

/* ---- the seat UI's closures -------------------------------------------- */
typedef struct window_operation { const qa_ui_window_mutation *mutation; } window_operation;
static void apply_window(void *ctx) { qa_ui_apply_window_mutation(((window_operation *)ctx)->mutation); }
static void ui_mutate_window(void *ctx, const qa_ui_window_mutation *mutation) {
  bind_candidate *c = ctx;
  window_operation operation = {mutation};
  qa_app_native_renderer_mutate_window(c->app->renderer, apply_window, &operation);
}
static void ui_command(void *ctx, const char *name, const char *const *args, size_t arg_count) {
  bind_candidate *c = ctx;
  qa_seat_id seat = qa_session_seat_id(c->seat);
  candidate_execute(c, name, args, arg_count, &seat, NULL);
}

/* ---- the Q3 guest's closures ------------------------------------------- */
static bind_candidate *cv(void *ctx) { return ctx; }
static double cv_generation(void *ctx) { return (double)qa_q3_client_connection_generation(cv(ctx)->connection); }
static double cv_message(void *ctx) { return (double)qa_q3_client_connection_server_message_sequence(cv(ctx)->connection); }
static double cv_executed(void *ctx) { return (double)qa_q3_client_connection_last_executed_server_command(cv(ctx)->connection); }
static double cv_client_number(void *ctx) { return (double)qa_q3_client_connection_client_number(cv(ctx)->connection); }
static bool cv_ping(void *ctx, double number, double *out) {
  int64_t ping = 0;
  if (!qa_q3_client_connection_snapshot_ping(cv(ctx)->connection, (int64_t)number, &ping)) return false;
  *out = (double)ping;
  return true;
}
static void cv_free_tokens(bind_candidate *c) {
  for (size_t i = 0; i < c->token_count; i++) free(c->tokens[i]);
  free(c->tokens);
  c->tokens = NULL;
  c->token_count = 0;
}
static bool cv_server_command(void *ctx, double sequence, const char *const **out_tokens, size_t *out_count) {
  bind_candidate *c = cv(ctx);
  cv_free_tokens(c);
  if (!qa_q3_client_connection_get_server_command(c->connection, (int64_t)sequence, &c->tokens, &c->token_count)) return false;
  *out_tokens = (const char *const *)c->tokens;
  *out_count = c->token_count;
  return true;
}
static const char *cv_get(void *ctx, double index) {
  bind_candidate *c = cv(ctx);
  free(c->text);
  c->text = NULL;
  (void)qa_q3_client_game_state_storage_get(qa_q3_client_connection_game_state(c->connection), (int64_t)index, &c->text);
  return c->text;
}
static void cv_free_record(bind_candidate *c) {
  if (!c->has_record) return;
  free(c->record.string_offsets);
  free(c->record.string_data);
  c->has_record = false;
}
static qa_qvmx_source_game_state_record cv_record(void *ctx) {
  bind_candidate *c = cv(ctx);
  cv_free_record(c);
  if (!qa_q3_client_game_state_storage_copy_source_record(qa_q3_client_connection_game_state(c->connection), &c->record))
    qa_range_error("Out of memory");
  c->has_record = true;
  qa_qvmx_source_game_state_record out;
  out.string_offsets = c->record.string_offsets;
  out.string_offset_count = 1024;
  out.string_data = c->record.string_data;
  out.string_data_length = 16000;
  out.data_count = (double)c->record.data_count;
  return out;
}
static double cv_command_number(void *ctx) {
  return (double)qa_q3_wire_command_history_current_number(qa_q3_client_connection_commands(cv(ctx)->connection));
}
static bool cv_command_read(void *ctx, double number, qa_qvmx_wire_user_command *out) {
  qa_q3_wire_user_command command;
  if (!qa_q3_wire_command_history_read(qa_q3_client_connection_commands(cv(ctx)->connection), (int64_t)number, &command))
    return false;
  out->server_time = command.server_time;
  for (int i = 0; i < 3; i++) out->angles[i] = command.angles[i];
  out->forwardmove = command.forwardmove;
  out->rightmove = command.rightmove;
  out->upmove = command.upmove;
  out->buttons = command.buttons;
  out->weapon = command.weapon;
  return true;
}
static bool q3_save_font_data(void *ctx) {
  bind_candidate *c = ctx;
  qa_cvar_registry *shared = qa_app_application_input_shared_cvars(c->input);
  return (shared != NULL ? qa_cvar_registry_variable_value(shared, "r_saveFontData") : 0) != 0;
}
static qa_app_q3_renderer_settings q3_render_settings(void *ctx) {
  bind_candidate *c = ctx;
  return qa_app_q3_renderer_settings_from(qa_app_application_input_shared_cvars(c->input));
}
static bool q3_split_screen(void *ctx) { return qa_app_application_input_local_count(((bind_candidate *)ctx)->input) > 1; }
static qa_rect q3_viewport(void *ctx) {
  bind_candidate *c = ctx;
  return qa_remote_application_seat_viewport(c->app, qa_session_seat_id(c->seat));
}
static double q3_now(void *ctx) { (void)ctx; return qa_app_remote_access_now_milliseconds(NULL); }
static void q3_dispatch_reliable(void *ctx, const char *text, const qa_command_context *source) {
  (void)source; /* TS command => this.sendCommand(command) */
  qa_remote_application_send_command(((bind_candidate *)ctx)->app, text);
}
static void q3_reliable(void *ctx, const char *text) {
  bind_candidate *c = ctx;
  qa_app_application_input_enqueue_client_reliable(c->input, text, &c->context, q3_dispatch_reliable, c);
}
static void q3_console(void *ctx, const char *text) {
  bind_candidate *c = ctx;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = c->context.session;
  source.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
  source.origin.name = "q3-cgame";
  source.origin.caller = &c->context.origin;
  qa_app_application_input_enqueue_client_command(c->input, text, &source);
}
static void q3_print(void *ctx, const char *text) {
  bind_candidate *c = ctx;
  qa_app_application_input_print(c->input, text, &c->context);
}
static double guest_execute_now(void *ctx, const char *text, const qa_command_context *source) {
  return qa_app_application_input_guest_execute_now(((bind_candidate *)ctx)->input, text, source);
}
static void guest_insert(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_application_input_guest_insert(((bind_candidate *)ctx)->input, text, source);
}
static void guest_append(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_application_input_guest_append(((bind_candidate *)ctx)->input, text, source, -1);
}
static qa_app_qvm_client_state_info q3_client_state(void *ctx) {
  bind_candidate *c = ctx;
  qa_remote_application *app = c->app;
  const char *phase = qa_remote_application_network_phase(app);
  const qa_remote_live_network *network = qa_remote_application_network(app);
  qa_app_qvm_client_state_info info;
  info.phase = strcmp(phase, "active") == 0 ? 8 : strcmp(phase, "loading") == 0 ? 6 : 5;
  info.connect_packet_count = network != NULL && network->family == QA_APP_REMOTE_FAMILY_Q3
      ? qa_app_q3_client_network_connect_packet_count(network->q3) : 0;
  info.client_number = (double)qa_q3_client_connection_client_number(c->connection);
  info.server_name = qa_remote_network_is(&app->launch_options, "q3-client") && app->launch_options.network_remote != NULL
      ? app->launch_options.network_remote : "";
  info.message = "";
  return info;
}
/* The guest's systemCinematicHost owner (TS bindSeat 2392-2420). */
static bool system_current(void *ctx) {
  bind_candidate *c = ctx;
  qa_remote_application *app = c->app;
  return !app->closed && !app->closing && !app->stopping && c->generation == app->world_load_generation;
}
static void system_complete(void *ctx) {
  bind_candidate *c = ctx;
  qa_remote_command_owner *owner = c->app->client_command_owner;
  if (owner == NULL) return;
  char *next = qa_cvar_registry_variable_string(owner->cvars, "nextmap");
  if (next == NULL) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, next);
  qa_remote_cvar_set(owner->cvars, "nextmap", "", true);
  if (next[0] != '\0') {
    size_t length = strlen(next) + 2;
    char *text = xcalloc(length, 1);
    snprintf(text, length, "%s\n", next);
    qa_command_buffer_append(owner->commands, text, &c->context, -1);
    free(text);
  }
  qa_cleanup_pop(&guard);
  free(next);
}
static void system_publish(void *ctx, qa_remote_cinematic *movie) {
  bind_candidate *c = ctx;
  if (c->published) qa_remote_application_activate_cinematic(c->app, movie);
  else {
    if (c->candidate_cinematic != NULL) qa_remote_application_close_cinematic(c->app, c->candidate_cinematic);
    c->candidate_cinematic = movie;
  }
}
static void system_unpublish(void *ctx, qa_remote_cinematic *movie) {
  bind_candidate *c = ctx;
  if (c->candidate_cinematic == movie) c->candidate_cinematic = NULL;
}
static double cull_factor(void *ctx) {
  return qa_cvar_registry_variable_value(qa_app_image_settings_cvars(((bind_candidate *)ctx)->app->image_settings), "gl_debug_distfrac");
}
static double field_of_view(void *ctx) { return qa_app_view_settings_field_of_view(((bind_candidate *)ctx)->app->view_settings); }
static double console_scale(void *ctx) {
  return qa_cvar_registry_variable_value(qa_app_image_settings_cvars(((bind_candidate *)ctx)->app->image_settings), "con_scale");
}
static const char *capture_map(void *ctx) {
  const char *map = qa_remote_application_capture_map(ctx);
  return map != NULL ? map : "menu";
}
static void capture_print(void *ctx, const char *text) { qa_remote_application_print(ctx, text, NULL); }
static void release_seat_ui(void *object) { qa_app_seat_ui_destroy(object); }
static void release_candidate(void *object) {
  bind_candidate *c = object;
  for (size_t i = 0; i < c->action_count; i++) qa_remote_command_free(&c->actions[i]);
  free(c->actions);
  free(c->live);
  free(c->transfers);
  free(c->visible);
  free(c->pure);
  free(c->text);
  cv_free_tokens(c);
  cv_free_record(c);
  if (c->guest_cvars != NULL && c->q3 == NULL) qa_app_q3_client_cvars_destroy(c->guest_cvars);
  if (c->has_image) qa_app_image_client_settings_destroy(&c->image);
  free(c);
}

/* ---- the candidate phase ----------------------------------------------- */
static void bind_candidate_body(void *ctx) {
  bind_candidate *c = ctx;
  qa_remote_application *app = c->app;
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  qa_remote_world_frontend *frontend = app->frontend;
  qa_remote_command_owner *owner = app->client_command_owner;
  qa_actor_id player;
  (void)qa_remote_presentation_admitted_player(remote, c->connection != NULL, &player);
  c->candidate_scripts = qa_app_source_console_scripts(qa_remote_application_content(app), &app->launch_options, app->input_config);
  if (c->candidate_scripts == NULL) qa_range_error("Out of memory");
  /* TS GC: the files stay reachable from the seat input and command owner until the application is dropped. */
  qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_console_script_files_destroy, c->candidate_scripts);
  /* TS q3Scene = remote instanceof Q3RemotePresentation ? { remote, queries: remote.scene } : null, before the
   * transfers: creating the scene binds the collision settings, which registers the cm_* cvars on the live owner. */
  if (remote->family == QA_APP_REMOTE_FAMILY_Q3) c->q3_queries = qa_app_remote_seat_presentation_scene(remote);
  c->image = qa_app_image_settings_prepare_client_settings(app->image_settings);
  c->has_image = true;
  size_t visible_count = 0;
  qa_cvar_registry *const *visible = owner->routing.visible(owner->routing.ctx, c->context, &visible_count);
  c->live = xcalloc(visible_count + 6, sizeof *c->live);
  for (size_t i = 0; i < visible_count; i++) add_live(c, visible[i]);
  add_live(c, owner->cvars);
  add_live(c, owner->movement != NULL ? owner->movement : owner->cvars);
  add_live(c, owner->fallback != NULL ? owner->fallback : owner->cvars);
  add_live(c, owner->input_settings->cvars);
  add_live(c, qa_app_image_settings_cvars(app->image_settings));
  c->transfers = xcalloc(c->live_count, sizeof *c->transfers);
  for (size_t i = 0; i < c->live_count; i++) {
    if (c->live[i] == qa_app_image_settings_cvars(app->image_settings)) continue;
    qa_cvar_transfer *transfer = qa_cvar_registry_prepare_candidate(c->live[i], qa_remote_host_print, &app->host.base, true);
    if (transfer == NULL) qa_range_error("Out of memory");
    qa_remote_arena_add(&app->arena, qa_remote_release_transfer, transfer);
    c->transfers[c->transfer_count].registry = c->live[i];
    c->transfers[c->transfer_count].transfer = transfer;
    c->transfer_count++;
  }
  qa_mouse_settings_init(&c->mouse, staged(c, owner->input_settings->cvars));
  c->owner_view = qa_remote_input_owner(owner);
  c->owner_view.scripts = c->candidate_scripts;
  c->owner_view.cvars = staged(c, owner->cvars);
  c->owner_view.movement = staged(c, owner->movement != NULL ? owner->movement : owner->cvars);
  c->owner_view.fallback = staged(c, owner->fallback != NULL ? owner->fallback : owner->cvars);
  c->owner_view.input_settings = &c->mouse;
  c->owner_view.routing = (qa_command_cvar_routing){c, candidate_owner, candidate_visible};
  qa_app_input_open_args args;
  memset(&args, 0, sizeof args);
  args.window = qa_app_input_window_from_sdl(qa_remote_application_window(app));
  qa_app_local_player local_player = {c->seat, player};
  args.players = &local_player;
  args.player_count = 1;
  args.options = &app->launch_options;
  args.dialect = qa_app_movement_dialect(&app->launch_options,
      remote->family == QA_APP_REMOTE_FAMILY_UNIFIED ? qa_app_loaded_content_recipe(qa_remote_application_content(app)) : NULL);
  args.simulation = qa_remote_application_remote_access(app);
  args.actions.ctx = c;
  args.actions.llm = app->host.base.llm_requester;
  args.actions.quit = candidate_quit;
  args.actions.execute = input_execute;
  args.actions.print = input_print;
  args.actions.shared_cvars = qa_app_image_settings_cvars(c->image.settings);
  args.actions.scripts = c->candidate_scripts;
  args.actions.startup_reader = input_startup_reader;
  args.actions.local_player_capacity = input_capacity;
  args.actions.can_remove_local_player = input_can_remove;
  args.actions.client_captures_input = input_client_captures;
  args.actions.client_input = input_client_input;
  args.now = input_now;
  args.settings = app->input_config;
  args.owner = &c->owner_view;
  args.previous = c->previous;
  args.prepared = app->configuration->prepared;
  c->input = app->ownership.borrowed
      ? qa_app_application_input_prepare_for_client(app->ownership.client, c->live, c->live_count, &args)
      : qa_app_application_input_prepare(c->live, c->live_count, &args);
  if (c->input == NULL) qa_range_error("Out of memory");
  bind_assert(c);
  if (qa_app_application_input_local_count(c->input) == 0) qa_raise("Error", 0, 0, "Remote input has no local seat");
  qa_app_local_input *local = qa_app_application_input_local(c->input, 0);
  if (qa_input_command_builder_dialect(local->builder) == QA_COMMAND_DIALECT_Q3 && c->connection != NULL) {
    qa_q3_wire_command_history *history = qa_q3_client_connection_commands(c->connection);
    qa_q3_wire_user_command command;
    qa_vec3 angles = {0, 0, 0};
    if (qa_q3_wire_command_history_read(history, qa_q3_wire_command_history_current_number(history), &command)) {
      /* TS ((angle << 16) >> 16) * (360 / 65536) */
      angles.x = (double)(int16_t)(uint16_t)(uint32_t)command.angles[0] * (360.0 / 65536.0);
      angles.y = (double)(int16_t)(uint16_t)(uint32_t)command.angles[1] * (360.0 / 65536.0);
      angles.z = (double)(int16_t)(uint16_t)(uint32_t)command.angles[2] * (360.0 / 65536.0);
    }
    qa_input_command_builder_set_view_angles(local->builder, angles);
  }
  const qa_app_menu_typography *typography = qa_app_application_assets_load_menu_typography(frontend->assets);
  bind_assert(c);
  qa_app_seat_ui_options ui;
  memset(&ui, 0, sizeof ui);
  ui.local = local;
  ui.art = frontend->art;
  ui.input = c->input;
  ui.mutate_window = ui_mutate_window;
  ui.mutate_ctx = c;
  ui.simulation = qa_remote_application_remote_access(app);
  ui.font = frontend->font;
  ui.audio = frontend->audio;
  ui.quit = candidate_quit;
  ui.quit_ctx = c;
  ui.command = ui_command;
  ui.command_ctx = c;
  ui.typography = typography;
  c->view_binding = qa_app_view_settings_binding(app->view_settings);
  ui.view_setting = &c->view_binding;
  ui.llm = app->host.base.llm_settings;
  c->ui = qa_app_seat_ui_create(&ui);
  if (c->ui == NULL) qa_range_error("Out of memory");
  if (app->has_ui_preferences) qa_ui_seat_preferences_set_values(qa_app_seat_ui_preferences(c->ui), app->ui_preferences);
  if (remote->family == QA_APP_REMOTE_FAMILY_Q3) {
    if (c->connection == NULL) qa_raise("Error", 0, 0, "Q3 guest seat must initialize with its gamestate");
    if (app->q3_browser == NULL) qa_raise("Error", 0, 0, "Q3 guest seat requires its application browser");
    if (app->key_profile == NULL) qa_raise("Error", 0, 0, "Q3 remote guest requires its published key profile");
    qa_seat_id seat_id = qa_session_seat_id(c->seat);
    c->guest_cvars = qa_app_application_input_guest_cvars(c->input, seat_id);
    qa_remote_cinematic_owner *cinematic = qa_remote_cinematic_owner_create(app,
        qa_app_application_audio_engine(frontend->audio), qa_remote_application_mounts(app), seat_id,
        qa_app_application_input_cvars(c->input), c, system_current, system_complete, system_publish, system_unpublish);
    qa_app_application_q3_client_options q3;
    memset(&q3, 0, sizeof q3);
    q3.kind = QA_APP_Q3_CLIENT_QVM;
    q3.ctx = c;
    q3.keys = app->key_profile;
    q3.save_font_data = q3_save_font_data;
    q3.render_settings = q3_render_settings;
    q3.assert_current = bind_assert_ctx;
    q3.source = qa_app_q3_remote_cgame_source(remote->q3);
    q3.connection.ctx = c;
    q3.connection.get_generation = cv_generation;
    q3.connection.get_server_message_sequence = cv_message;
    q3.connection.get_last_executed_server_command = cv_executed;
    q3.connection.get_client_number = cv_client_number;
    q3.connection.snapshot_ping = cv_ping;
    q3.connection.get_server_command = cv_server_command;
    q3.connection.game_state = (qa_q3_client_game_state_view){c, cv_get, cv_record};
    q3.connection.commands = (qa_q3_client_commands_view){c, cv_command_number, cv_command_read};
    q3.command_buffer = (qa_app_qvm_command_target){c, guest_execute_now, guest_insert, guest_append};
    q3.guest_cvars = qa_app_q3_client_cvars_services(c->guest_cvars);
    q3.guest_input = qa_app_application_input_guest_input(c->input, seat_id);
    q3.cvars = qa_app_application_input_cvars(c->input);
    q3.system_cinematics = qa_remote_application_system_cinematic_host(app, cinematic);
    q3.renderer.backend_stencil_bits = qa_app_native_renderer_backend(app->renderer).stencil_bits;
    q3.renderer.driver = qa_app_native_renderer_driver(app->renderer);
    q3.renderer.gl_config = qa_app_native_renderer_gl_config(app->renderer);
    q3.browser = app->q3_browser;
    q3.command_bindings = qa_app_application_input_client_commands(c->input);
    q3.command_registration = qa_app_application_input_client_command_registration(c->input, seat_id, NULL);
    q3.client_state = q3_client_state;
    q3.assets = frontend->assets;
    q3.queries = c->q3_queries;
    q3.local = local;
    q3.audio = frontend->audio;
    q3.viewport = q3_viewport;
    q3.split_screen = q3_split_screen;
    q3.now = q3_now;
    q3.reliable = q3_reliable;
    q3.console = q3_console;
    q3.print = q3_print;
    c->q3 = qa_app_application_q3_client_create(&q3, NULL);
    if (c->q3 == NULL) qa_range_error("Out of memory");
    bind_assert(c);
  }
  if (c->connection != NULL) {
    if (app->q3_content == NULL) qa_raise("Error", 0, 0, "Q3 guest initialization has no content owner");
    char *info = NULL;
    if (!qa_q3_client_game_state_storage_get(qa_q3_client_connection_game_state(c->connection), 1, &info) || info == NULL)
      info = strdup("");
    if (info == NULL) qa_range_error("Out of memory");
    char *server = qa_q3_info_value(info, "sv_serverid");
    free(info);
    if (server == NULL) qa_range_error("Out of memory");
    double id = qa_native_atoi(server);
    free(server);
    c->pure = qa_app_q3_client_content_referenced_pure_command(app->q3_content, id);
  }
  bind_assert(c);
  qa_app_world_seat_presentation_options p;
  memset(&p, 0, sizeof p);
  p.local = local;
  p.assets = frontend->assets;
  p.renderer = app->renderer;
  p.simulation = qa_remote_application_remote_access(app);
  p.seat_count = 1;
  p.font = frontend->font;
  /* TS characters: the unified replica with a Q3 character loads it from the recipe's appearance content. */
  qa_app_loaded_content *content = qa_remote_application_content(app);
  p.characters = remote->family == QA_APP_REMOTE_FAMILY_UNIFIED && app->launch_options.character == QA_GAME_FAMILY_Q3
      ? qa_app_load_q3_character_for_content(content, qa_app_loaded_content_recipe(content)->character.appearance.content,
          app->launch_options.character_model)
      : NULL;
  bind_assert(c);
  p.ui = c->ui;
  p.effects = frontend->effects;
  qa_app_seat_q3_client q3_seat;
  if (c->q3 != NULL) {
    q3_seat = qa_app_application_q3_client_seat(c->q3);
    p.q3_client = &q3_seat;
  }
  p.world_text_cull_factor = cull_factor;
  p.world_text_cull_factor_ctx = c;
  p.field_of_view = field_of_view;
  p.field_of_view_ctx = c;
  p.console_scale = console_scale;
  p.console_scale_ctx = c;
  c->presentation = qa_app_world_seat_presentation_create(&p);
  if (c->presentation == NULL) qa_range_error("Out of memory");
  /* Closed by the presentation from here; freed when the application is destroyed (TS GC), after every presentation
   * built over it (the arena runs last-in first-out and a cleared presentation joins it later). */
  qa_remote_arena_add(&app->arena, release_seat_ui, c->ui);
  c->ui = NULL;
  if (app->ownership.borrowed) c->capture = app->ownership.client->capture;
  else {
    char *root = qa_app_application_capture_root(app->launch_options.user_content_root);
    if (root == NULL) qa_range_error("Out of memory");
    qa_app_application_capture_services services = qa_app_input_capture_services(c->input, root, capture_map, app, capture_print, app);
    free(root);
    c->capture = qa_app_capture_create(&services, app->renderer);
    if (c->capture == NULL) qa_range_error("Out of memory");
  }
  bind_assert(c);
  qa_app_world_seat_presentation_publish_layout(c->presentation, 0, (double)qa_app_application_input_local_count(c->input));
  qa_session_seat_validate_presentation(c->seat, qa_app_world_seat_presentation_seat(c->presentation));
  qa_app_application_input_validate_startup_adoption(c->input);
  qa_app_application_input_validate_candidate_commands(c->input);
  qa_app_image_client_settings_validate_publication(&c->image);
  for (size_t i = 0; i < c->transfer_count; i++) c->transfers[i].transfer->validate_publication(c->transfers[i].transfer->ctx);
}

/* ---- RemoteComponentView (the unified replica's component consumers) --------------------- */
typedef struct component_options_record {
  qa_remote_application *app;
  qa_app_qvm_display_renderer display;
  qa_remote_component_cinematics cinematics;
  qa_app_mod_presentations_options options;
  double (*frame_number)(void *ctx); void *frame_ctx;
  qa_app_remote_component_view *view;
} component_options_record;
static void component_print(void *ctx, const char *text) {
  qa_remote_application_print(((component_options_record *)ctx)->app, text, NULL);
}
/* nextFrame: this.host.loading?.nextFrame ?? setImmediate. */
static void component_next_frame(void *ctx) {
  const qa_application_loading *loading = ((component_options_record *)ctx)->app->host.base.loading;
  if (loading != NULL && loading->next_frame != NULL) loading->next_frame(loading->ctx);
  else qa_remote_next_frame(NULL);
}
static const qa_app_system_cinematic_host *component_cinematics(void *ctx, const qa_active_mod_presentation *source,
    const qa_app_viewing_seat *presentation, const qa_app_component_cinematic_scope *scope) {
  component_options_record *record = ctx;
  return record->cinematics.host(record->cinematics.ctx, source, presentation, scope);
}
/* clock.now: () => performance.now() */
static double component_now(void *ctx) { (void)ctx; return qa_app_remote_access_now_milliseconds(NULL); }
static double component_frame(void *ctx) {
  component_options_record *record = ctx;
  return record->frame_number(record->frame_ctx);
}
static void component_record_release(void *object) {
  component_options_record *record = object;
  qa_app_remote_component_view_destroy(record->view);
  free(record);
}
qa_app_remote_component_view *qa_remote_application_create_component_view(qa_remote_application *app,
    qa_app_unified_remote *remote, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_application_input *input, double (*frame_number)(void *ctx), void *frame_ctx) {
  component_options_record *record = xcalloc(1, sizeof *record);
  qa_remote_arena_add(&app->arena, component_record_release, record);
  record->app = app;
  record->display = qa_app_native_renderer_qvm_display(app->renderer);
  record->cinematics = qa_remote_application_component_cinematics(app, audio);
  record->frame_number = frame_number;
  record->frame_ctx = frame_ctx;
  qa_app_mod_presentations_options *o = &record->options;
  o->assets = assets;
  o->audio = audio;
  o->input = input;
  o->queries = (qa_shared_scene_queries *)qa_app_unified_remote_scene(remote);
  o->renderer = &record->display;
  o->ctx = record;
  o->print = component_print;
  o->next_frame = component_next_frame;
  o->system_cinematics = component_cinematics;
  o->clock = (qa_app_q3_service_clock){record, component_now, component_frame};
  o->hosts = qa_app_application_assets_q3_presentation_hosts(assets, audio);
  record->view = qa_app_remote_component_view_create(remote, o);
  if (record->view == NULL) qa_range_error("Out of memory");
  return record->view;
}

/* ---- the published phase ----------------------------------------------- */
/* seat.attachPresentation(presentation, () => { components?.close(); if (this.initialComponents === components)
 * this.initialComponents = null; presentation.close(); }) */
typedef struct presentation_cleanup_record {
  qa_remote_application *app;
  qa_app_remote_component_view *components;
  qa_app_world_seat_presentation *presentation;
} presentation_cleanup_record;
static void presentation_cleanup(void *ctx) {
  presentation_cleanup_record record = *(presentation_cleanup_record *)ctx;
  free(ctx); /* the seat runs its cleanup once */
  if (record.components != NULL) qa_app_remote_component_view_close(record.components);
  if (record.app->initial_components == record.components) record.app->initial_components = NULL;
  qa_app_world_seat_presentation_close(record.presentation);
}
static double application_frames(void *ctx) { return ((qa_remote_application *)ctx)->frames; }
static void retire_capture(void *ctx) {
  bind_candidate *c = ctx;
  if (!c->app->ownership.borrowed && c->previous_capture != NULL) qa_app_capture_close(c->previous_capture);
}
static void retire_input(void *ctx) {
  bind_candidate *c = ctx;
  if (c->previous != NULL) qa_app_application_input_close(c->previous);
}
static void retire_scripts(void *ctx) {
  bind_candidate *c = ctx;
  if (c->retired_scripts != NULL) qa_app_console_script_files_close(c->retired_scripts);
}
static void retire_previous(bind_candidate *c) {
  if (c->retired) return;
  c->retired = true;
  void (*steps[])(void *) = {retire_capture, retire_input, retire_scripts};
  bool failed = false;
  qa_caught_error caught;
  for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) if (!qa_try(steps[i], c, &caught)) failed = true;
  if (failed) qa_raise("AggregateError", 0, 0, "Previous remote input retirement failed");
}
static qa_audio_output_format target_output_format(void *ctx) { return qa_app_application_audio_output_format(ctx); }
static void target_select_output_format(void *ctx, qa_audio_output_format format) {
  qa_app_application_audio_select_output_format(ctx, format);
}
static void bind_publish_body(void *ctx) {
  bind_candidate *c = ctx;
  qa_remote_application *app = c->app;
  qa_remote_world_frontend *frontend = app->frontend;
  qa_remote_command_owner *owner = app->client_command_owner;
  qa_app_client_bootstrap *borrowed = app->ownership.borrowed ? app->ownership.client : NULL;
  qa_unified_audio_transfer publish_audio;
  bool has_publish_audio = false;
  if (borrowed != NULL && borrowed->output_current != NULL) {
    publish_audio = qa_unified_audio_prepare_output_transfer(borrowed->output_current,
        qa_app_application_audio_engine(frontend->audio));
    has_publish_audio = true;
  }
  if (c->previous != NULL && borrowed == NULL) qa_app_application_input_release_into_candidate(c->input, c->previous, true);
  /* From this point a failure retires the published source instead of discarding a candidate. */
  c->published = true;
  app->controls = c->input;
  qa_remote_application_set_presentation(app, c->presentation);
  app->capture = c->capture;
  app->primary_view_generation = c->generation;
  if (borrowed == NULL) qa_app_application_input_publish_candidate_commands(c->input);
  qa_remote_application_release_settings(app);
  if (c->q3 != NULL) qa_app_application_q3_client_adopt_cvars(c->q3, owner->cvars);
  for (size_t i = 0; i < c->transfer_count; i++) {
    c->transfers[i].transfer->publish(c->transfers[i].transfer->ctx);
    qa_app_application_input_adopt_cvar_owner(c->input, c->transfers[i].transfer->cvars, c->transfers[i].registry);
  }
  qa_app_image_client_settings_publish(&c->image);
  qa_cvar_registry *image_cvars = qa_app_image_settings_cvars(app->image_settings);
  qa_app_application_input_publish_shared_cvars(c->input, image_cvars);
  qa_app_application_input_adopt_mouse_owner(c->input, qa_session_seat_id(c->seat), *owner->input_settings);
  qa_app_view_settings_set_field_of_view(app->view_settings, qa_cvar_registry_variable_value(image_cvars, "fov"));
  qa_app_view_settings_bind_cvars(app->view_settings, image_cvars);
  app->release_view_cvars = true;
  double override_fov = 0;
  if (c->q3 != NULL && qa_app_view_settings_override(app->view_settings, &override_fov)) {
    char text[40];
    qa_net_js_number(qa_app_view_settings_field_of_view(app->view_settings), text, sizeof text);
    qa_remote_cvar_set(qa_app_application_q3_client_cvars(c->q3), "cg_fov", text, false);
  }
  qa_app_application_audio_bind_haptics(frontend->audio, qa_app_application_input_audio_haptics(c->input));
  qa_app_application_audio_bind_volume_cvars(frontend->audio, image_cvars);
  qa_app_application_audio_bind_output_cvars(frontend->audio, image_cvars);
  if (app->initial_components != NULL) qa_app_remote_component_view_close(app->initial_components);
  app->initial_components = NULL;
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  qa_app_remote_component_view *components = remote->family == QA_APP_REMOTE_FAMILY_UNIFIED
      ? qa_remote_application_create_component_view(app, remote->unified, frontend->assets, frontend->audio, c->input,
          application_frames, app)
      : NULL;
  app->initial_components = components;
  presentation_cleanup_record *cleanup = xcalloc(1, sizeof *cleanup);
  cleanup->app = app;
  cleanup->components = components;
  cleanup->presentation = c->presentation;
  (void)qa_session_seat_attach_presentation(c->seat, qa_app_world_seat_presentation_seat(c->presentation), presentation_cleanup,
      cleanup);
  if (borrowed != NULL) {
    qa_app_application_input_publish_client_platform(c->input, borrowed, true);
    if (has_publish_audio) qa_unified_audio_transfer_commit(publish_audio);
    qa_app_client_bootstrap_publish_output(borrowed, qa_app_application_audio_engine(frontend->audio));
  } else if (c->previous == NULL) qa_app_application_input_activate_prepared_platform(c->input);
  else qa_app_application_input_transfer_platform_to(c->previous, c->input);
  for (size_t i = 0; i < app->reported_effect_gap_count; i++) free(app->reported_effect_gaps[i]);
  app->reported_effect_gap_count = 0;
  if (c->candidate_cinematic != NULL) {
    qa_remote_cinematic *movie = c->candidate_cinematic;
    c->candidate_cinematic = NULL;
    qa_remote_application_activate_cinematic(app, movie);
  }
  qa_app_audio_output_target target = {frontend->audio, target_output_format, target_select_output_format};
  qa_app_apply_audio_output_settings(image_cvars, target);
  c->retired_scripts = borrowed == NULL ? owner->scripts : borrowed->configuration_current.scripts;
  owner->scripts = c->candidate_scripts;
  if (borrowed != NULL) {
    borrowed->configuration_current.scripts = c->candidate_scripts;
    borrowed->configuration_current.options = &app->launch_options;
  }
  c->candidate_scripts = NULL;
  const qa_simulation_output *output = qa_app_remote_seat_presentation_output(qa_remote_application_remote(app));
  if (output != NULL) qa_remote_application_publish_remote(app, output);
  qa_remote_application_retire_frontends(app);
  retire_previous(c);
  bind_assert(c);
  if (!app->ownership.borrowed) qa_app_capture_activate(c->capture);
  if (c->connection != NULL && c->pure != NULL) {
    qa_q3_reliable_command added;
    if (!qa_q3_client_reliable_commands_add(qa_q3_client_connection_reliable(c->connection), c->pure, &added))
      qa_range_error("Out of memory");
  }
  for (size_t i = 0; i < c->action_count; i++) {
    qa_remote_command *action = &c->actions[i];
    qa_remote_application_queue_command(app, action->name, (const char *const *)action->args, action->arg_count,
        action->has_seat ? &action->seat : NULL, action->has_source ? &action->source : NULL);
  }
  if (c->quit) qa_remote_application_request_quit(app);
  app->q3_initial_view_pending = c->connection != NULL;
  if (c->q3 == NULL) {
    qa_app_application_audio_start_world_music(frontend->audio);
    bind_assert(c);
  }
}

/* ---- the catch blocks -------------------------------------------------- */
static void discard_cinematic(void *ctx) {
  bind_candidate *c = ctx;
  if (c->candidate_cinematic != NULL) {
    qa_remote_cinematic *movie = c->candidate_cinematic;
    c->candidate_cinematic = NULL;
    qa_remote_application_close_cinematic(c->app, movie);
  }
}
static void discard_retire_previous(void *ctx) { retire_previous(ctx); }
static void discard_close_app(void *ctx) { qa_remote_application_close(((bind_candidate *)ctx)->app); }
static void discard_scripts(void *ctx) {
  bind_candidate *c = ctx;
  if (c->candidate_scripts != NULL) qa_app_console_script_files_close(c->candidate_scripts);
}
static void discard_q3(void *ctx) {
  bind_candidate *c = ctx;
  if (c->q3 != NULL) qa_app_application_q3_client_close(c->q3);
}
static void discard_ui(void *ctx) {
  bind_candidate *c = ctx;
  if (c->presentation != NULL) qa_app_world_seat_presentation_close(c->presentation);
  else if (c->ui != NULL) qa_app_seat_ui_close(c->ui);
}
static void discard_input(void *ctx) {
  bind_candidate *c = ctx;
  if (c->input != NULL) qa_app_application_input_close(c->input);
}
static void discard_capture(void *ctx) {
  bind_candidate *c = ctx;
  if (!c->app->ownership.borrowed && c->capture != NULL) qa_app_capture_close(c->capture);
}

void qa_remote_application_bind_seat(qa_remote_application *app, qa_q3_client_connection *connection) {
  if (app->primary_peer != NULL) {
    qa_remote_application_bind_remote_seat(app, app->primary_peer, connection);
    return;
  }
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  qa_remote_world_frontend *frontend = app->frontend;
  qa_actor_id player;
  bool has_player = qa_remote_presentation_admitted_player(remote, connection != NULL, &player);
  const qa_simulation_output *output = qa_app_remote_seat_presentation_output(remote);
  if (connection == NULL && app->q3_initial_view_pending && has_player && output != NULL && app->controls != NULL) {
    qa_app_local_input *local = NULL;
    size_t count = qa_app_application_input_local_count(app->controls);
    for (size_t i = 0; i < count && local == NULL; i++) {
      qa_app_local_input *candidate = qa_app_application_input_local(app->controls, i);
      if (qa_same_seat(qa_session_seat_id(candidate->player.seat), qa_remote_application_seat_id(app))) local = candidate;
    }
    if (local == NULL) qa_raise("Error", 0, 0, "Q3 first snapshot lost its seat");
    qa_seat_input_release(local->input, qa_app_remote_access_now_milliseconds(NULL), NULL);
    local->player.actor = player;
    qa_input_command_builder_set_view_angles(local->builder, qa_app_remote_seat_presentation_player_view(remote, player).angles);
    app->q3_initial_view_pending = false;
  }
  if (!has_player || (connection == NULL && output == NULL) || frontend == NULL || qa_remote_application_presentation(app) != NULL)
    return;
  qa_remote_command_owner *owner = app->client_command_owner;
  if (owner == NULL || owner->input_settings == NULL) qa_raise("Error", 0, 0, "Remote input has no retained command and mouse owner");
  bind_candidate *c = xcalloc(1, sizeof *c);
  qa_remote_arena_add(&app->arena, release_candidate, c);
  c->app = app;
  c->connection = connection;
  c->generation = app->world_load_generation;
  c->previous = app->controls;
  c->previous_capture = app->capture;
  qa_app_local_input *previous_local = c->previous != NULL && qa_app_application_input_local_count(c->previous) > 0
      ? qa_app_application_input_local(c->previous, 0) : NULL;
  if (c->previous != NULL && previous_local == NULL) qa_raise("Error", 0, 0, "Remote input lost its local seat");
  c->seat = previous_local != NULL ? previous_local->player.seat : app->configuration->seat;
  c->context.session = qa_engine_session_id(app->session);
  c->context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  c->context.origin.seat = qa_session_seat_id(c->seat);
  c->context.origin.client = qa_session_client_id(qa_session_seat_client(c->seat));
  qa_caught_error caught;
  bool failed = !qa_try(bind_candidate_body, c, &caught);
  if (!failed) failed = !qa_try(bind_publish_body, c, &caught);
  if (!failed) return;
  bool aggregate = false;
  qa_caught_error cleanup;
  if (!qa_try(discard_cinematic, c, &cleanup)) aggregate = true;
  if (c->published) {
    if (!qa_try(discard_retire_previous, c, &cleanup)) aggregate = true;
    if (!qa_try(discard_close_app, c, &cleanup)) aggregate = true;
    if (!qa_try(discard_scripts, c, &cleanup)) aggregate = true;
    if (aggregate) qa_raise("AggregateError", 0, 0, "Remote seat publication and shutdown failed");
  } else {
    void (*discards[])(void *) = {discard_q3, discard_ui, discard_input};
    for (size_t i = 0; i < sizeof discards / sizeof discards[0]; i++) if (!qa_try(discards[i], c, &cleanup)) aggregate = true;
    if (!qa_try(discard_scripts, c, &cleanup)) aggregate = true;
    (void)qa_try(discard_capture, c, &cleanup);
    if (c->q3 != NULL) { qa_app_application_q3_client_destroy(c->q3); c->q3 = NULL; }
    if (c->presentation != NULL) { qa_app_world_seat_presentation_destroy(c->presentation); c->presentation = NULL; }
    else if (c->ui != NULL) { qa_app_seat_ui_destroy(c->ui); c->ui = NULL; }
    if (c->input != NULL) { qa_app_application_input_destroy(c->input); c->input = NULL; }
    if (!app->ownership.borrowed && c->capture != NULL) { qa_app_capture_destroy(c->capture); c->capture = NULL; }
    if (c->has_image) { qa_app_image_client_settings_destroy(&c->image); c->has_image = false; }
    if (aggregate) qa_raise("AggregateError", 0, 0, "Remote candidate and cleanup failed");
  }
  qa_rethrow(&caught);
}
