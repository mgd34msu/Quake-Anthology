/* Port of src/app/bootstrap/application.ts, region 1302-1751: dialect,
 * input actions, server_setting, debug graph, application tools, source
 * command buffers, kick, map_restart, Q1 server commands and the game log.
 * Layout and status: docs/application_layout.md. TS async members are
 * synchronous (PORTING.md). GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include "simulation/shared_simulation_internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/commands.h"
#include "qa/app/bootstrap/component_commands.h"
#include "qa/app/bootstrap/console.h"
#include "qa/app/bootstrap/frame_time.h"
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/q1_client_commands.h"
#include "qa/app/bootstrap/q1_source_cvars.h"
#include "qa/app/bootstrap/renderer_diagnostics.h"
#include "qa/console/session.h"
#include "qa/app/bootstrap/save_requests.h"
#include "qa/app/bootstrap/simulation/classic_guest_world.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/content/q3/base/settings.h"
#include "qa/content/q3/team_arena/commands.h"
#include "qa/content/q3/team_arena/server_commands.h"
#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/content/user_data.h"
#include "qa/core/numeric.h"
#include "qa/core/q3_product_policy.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/settings/server/profile.h"
#include "qa/world/session/mod_commands.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *dup_text(const char *text) {
  size_t n = strlen(text) + 1;
  char *out = checked(malloc(n));
  memcpy(out, text, n);
  return out;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static char *lower_text(const char *text) {
  char *out = dup_text(text);
  for (char *p = out; *p != '\0'; p++) *p = (char)tolower((unsigned char)*p);
  return out;
}
static void grow(void **items, size_t *capacity, size_t needed, size_t size) {
  if (needed <= *capacity) return;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  while (next < needed) next *= 2;
  *items = checked(realloc(*items, next * size));
  *capacity = next;
}
/* TS invocation.args: argv without the command name. */
static const char *const *invocation_args(const qa_command_invocation *invocation, size_t *out_count) {
  *out_count = invocation->argc == 0 ? 0 : invocation->argc - 1;
  return invocation->argc == 0 ? NULL : (const char *const *)invocation->argv + 1;
}
static const qa_command_origin *root_origin(const qa_command_context *source) {
  const qa_command_origin *origin = &source->origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  return origin;
}
static bool produced_by_instance(const qa_command_context *source) {
  return source->producer != NULL && source->producer->instance != NULL;
}
static void host_print_source(void *ctx, const char *text, const qa_command_context *source) {
  (void)source; /* TS print: text => this.host.print(text) takes no source */
  qa_application_print(ctx, text);
}
static qa_command_context server_console_context(qa_application *app) {
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = qa_engine_session_id(app->session);
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  return context;
}

/* ---- 1302-1306 ---- */
qa_command_dialect qa_application_source_dialect(qa_application *app, qa_app_loaded_content *content) {
  return qa_application_content_dialect(content == NULL ? app->loaded_content : content);
}
qa_command_dialect qa_application_content_dialect(qa_app_loaded_content *content) {
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);
  const qa_catalog_discovered_product *product =
      qa_content_catalog_product(catalog, qa_app_loaded_content_recipe(content)->engine_behavior.content);
  const qa_catalog_product_expectation *source = &product->expectation;
  if (source->family == QA_GAME_FAMILY_Q1)
    return is(source->edition, "quakeworld") ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD : QA_COMMAND_DIALECT_Q1_NETQUAKE;
  if (source->family == QA_GAME_FAMILY_Q2)
    return is(source->edition, "rerelease") ? QA_COMMAND_DIALECT_Q2_RERELEASE : QA_COMMAND_DIALECT_Q2_CLASSIC;
  return QA_COMMAND_DIALECT_Q3;
}

/* ---- 1308-1352 inputActions ---- */
static double actions_local_player_capacity(void *ctx) {
  qa_application_input_actions *actions = ctx;
  qa_application *app = actions->app;
  double available = 0;
  for (double slot = 0; slot < actions->simulation->options.max_clients; slot++)
    if (qa_engine_session_client_at(app->session, slot) == NULL) available++;
  return (double)app->local_seat_count + available;
}
static void actions_quit(void *ctx) {
  qa_application_input_actions *actions = ctx;
  if (actions->local_guest != NULL && actions->local_guest != actions->app->local_guest)
    qa_raise("Error", 0, 0, "Guest candidate requested quit");
  qa_application_request_quit(actions->app);
}
static qa_application_command_request application_request(const char *name, const char *const *args, size_t count,
    const qa_seat_id *seat, const qa_command_context *source) {
  qa_application_command_request request;
  memset(&request, 0, sizeof request);
  request.target = QA_APPLICATION_TARGET_APPLICATION;
  request.name = dup_text(name);
  request.arguments = count == 0 ? NULL : checked(calloc(count, sizeof *request.arguments));
  for (size_t i = 0; i < count; i++) request.arguments[i] = dup_text(args[i]);
  request.argument_count = count;
  if (seat != NULL) { request.has_seat = true; request.seat = *seat; }
  if (source != NULL) { request.has_source = true; request.source = qa_command_context_copy(source); }
  return request;
}
static void actions_execute(void *ctx, const char *name, const char *const *args, size_t count,
    const qa_seat_id *seat, const qa_command_context *source) {
  qa_application_input_actions *actions = ctx;
  if (actions->candidate_action != NULL) {
    qa_application_command_request request = application_request(name, args, count, seat, source);
    actions->candidate_action(actions->candidate_ctx, &request);
    return;
  }
  if (actions->local_guest == NULL) {
    qa_application_queue_command_source(actions->app, name, args, count, seat, source);
    return;
  }
  qa_application_command_request request = application_request(name, args, count, seat, source);
  qa_application_queue_local_guest_command(actions->app, actions->local_guest, &request);
}
static void actions_print(void *ctx, const char *text) {
  qa_application_input_actions *actions = ctx;
  qa_application_print(actions->app, text);
}
/* [...this.localSeats].find(([, local]) => local.id.equals(seat))?.[0] then the simulation player it owns. */
static bool seat_player(qa_application_input_actions *actions, qa_seat_id seat, qa_actor_id *out) {
  qa_application *app = actions->app;
  for (size_t i = 0; i < app->local_seat_count; i++) {
    if (!qa_same_seat(qa_session_seat_id(app->local_seats[i]), seat)) continue;
    qa_client_id client = app->local_seat_clients[i];
    size_t count = 0;
    const qa_actor_id *players = qa_app_shared_sim_players(actions->simulation, &count);
    for (size_t j = 0; j < count; j++) {
      qa_client_id owner;
      if (qa_app_shared_sim_player_client(actions->simulation, players[j], &owner) && qa_same_client(owner, client)) {
        *out = players[j];
        return true;
      }
    }
    return false;
  }
  return false;
}
static bool actions_arsenal_impulse_provider(void *ctx, qa_seat_id seat, qa_provider_id *out) {
  qa_application_input_actions *actions = ctx;
  qa_actor_id actor;
  if (!seat_player(actions, seat, &actor)) return false;
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(actions->simulation, actor);
  if (player == NULL || (qa_app_shared_sim_q1_source(actions->simulation) == NULL && !is(player->arsenal.state.kind, "q1"))) return false;
  *out = player->arsenal.provider;
  return true;
}
static const qa_weapon_binding_item *actions_binding_items(void *ctx, qa_seat_id seat, size_t *out_count) {
  qa_application_input_actions *actions = ctx;
  *out_count = 0;
  qa_actor_id actor;
  if (!seat_player(actions, seat, &actor)) return NULL;
  qa_app_sim_player_ui ui = qa_app_shared_sim_player_ui(actions->simulation, actor);
  grow((void **)&actions->binding_items, &actions->binding_item_capacity, ui.item_count, sizeof *actions->binding_items);
  for (size_t i = 0; i < ui.item_count; i++) {
    actions->binding_items[i].id = ui.items[i].id;
    actions->binding_items[i].label = ui.items[i].label;
    actions->binding_items[i].kind = is(ui.items[i].kind, "powerup") ? QA_WEAPON_BINDING_POWERUP : QA_WEAPON_BINDING_WEAPON;
  }
  *out_count = ui.item_count;
  return actions->binding_items;
}
static qa_ui_binding_capabilities actions_binding_capabilities(void *ctx) {
  qa_application_input_actions *actions = ctx;
  qa_app_shared_simulation *simulation = actions->simulation;
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  bool q2_native = qa_app_shared_sim_q2_native(simulation, &classic, &rerelease);
  bool q2 = qa_app_shared_sim_q2_source(simulation) != NULL, q3 = qa_app_shared_sim_q3_source(simulation) != NULL,
       guest = qa_app_shared_sim_q3_guest(simulation) != NULL;
  qa_ui_binding_capabilities capabilities;
  capabilities.chat = qa_app_shared_sim_q1_source(simulation) != NULL || q2 || q2_native || q3 || guest;
  capabilities.score_command = q2 || q2_native ? "score" : q3 || guest ? "+scores" : NULL;
  const qa_executable_recipe *recipe = qa_app_shared_sim_recipe(simulation);
  capabilities.offhand_grapple = recipe->equipment.grapple.is_enabled && is(recipe->equipment.grapple.binding, "offhand");
  capabilities.offhand_grenades = recipe->equipment.hand_grenades.is_enabled;
  return capabilities;
}
static qa_command_dialect actions_console_dialect(void *ctx) {
  qa_application_input_actions *actions = ctx;
  return qa_application_source_dialect(actions->app, actions->content);
}
/* [...names, ...frameTimeCvarNames(dialect)] into the actions' scratch. */
static void console_shared_names(qa_application_input_actions *actions, const char *const *names, size_t count,
    qa_command_dialect dialect, qa_app_console_server *out) {
  size_t frame_count = 0;
  const char *const *frame = qa_app_frame_time_cvar_names(dialect, &frame_count);
  grow((void **)&actions->shared_names, &actions->shared_name_capacity, count + frame_count, sizeof *actions->shared_names);
  for (size_t i = 0; i < count; i++) actions->shared_names[i] = names[i];
  for (size_t i = 0; i < frame_count; i++) actions->shared_names[count + i] = frame[i];
  out->shared_names = actions->shared_names;
  out->shared_name_count = count + frame_count;
}
static bool actions_console_server(void *ctx, qa_app_console_server *out) {
  qa_application_input_actions *actions = ctx;
  qa_app_shared_simulation *simulation = actions->simulation;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  if (source != NULL) {
    qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(source)->cvars;
    size_t count = 0;
    const qa_q3_base_cvar_definition *definitions = qa_q3_base_game_settings_definitions(
        qa_app_q3_source_subsystems_owner_settings(qa_app_q3_source_runtime_owner(source)), &count);
    const char **names = count == 0 ? NULL : checked(malloc(count * sizeof *names));
    for (size_t i = 0; i < count; i++) names[i] = definitions[i].name;
    out->cvars = cvars;
    console_shared_names(actions, names, count, qa_cvar_registry_dialect(cvars), out);
    free(names);
    return true;
  }
  if (actions->q2_console != NULL) {
    size_t count = 0;
    const char *const *names = qa_app_q2_console_shared_names(actions->q2_console, &count);
    out->cvars = qa_app_q2_console_cvars(actions->q2_console);
    console_shared_names(actions, names, count, qa_cvar_registry_dialect(out->cvars), out);
    return true;
  }
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  if (guest != NULL) {
    qa_cvar_registry *cvars = qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(guest));
    size_t count = 0;
    const qa_q3_base_cvar_definition *definitions = qa_q3_base_game_cvar_definitions(QA_Q3_BASE_PRODUCT_MISSIONPACK, &count);
    const char **names = count == 0 ? NULL : checked(malloc(count * sizeof *names));
    for (size_t i = 0; i < count; i++) names[i] = definitions[i].name;
    out->cvars = cvars;
    console_shared_names(actions, names, count, qa_cvar_registry_dialect(cvars), out);
    free(names);
    return true;
  }
  const qa_ss_source_runtime *q1 = qa_app_shared_sim_q1_source(simulation);
  qa_app_sim_quakec_source *quakec = qa_app_shared_sim_quakec_source(simulation);
  return qa_app_q1_console_server(q1 == NULL ? NULL : q1->q1_cvars, quakec == NULL ? NULL : qa_app_sim_quakec_source_cvars(quakec), out);
}
static qa_cvar_registry *actions_console_seat(void *ctx, qa_seat_id id) {
  qa_application_input_actions *actions = ctx;
  qa_application_local_q3_guest_world *guest = actions->local_guest;
  if (guest != NULL)
    for (size_t i = 0; i < guest->seat_count; i++)
      if (qa_same_seat(guest->seat_ids[i], id)) return guest->seats[i].cvars;
  if (!actions->explicit_client_cvars) return qa_application_client_cvars_get(actions->app, id);
  for (size_t i = 0; i < actions->client_cvar_count; i++)
    if (qa_same_seat(actions->client_cvar_seats[i], id)) return actions->client_cvars[i];
  return NULL;
}
static qa_application_q3_seat_client *graphical_q3(qa_application *app, qa_seat_id seat) {
  if (app->graphical == NULL) return NULL;
  for (size_t i = 0; i < app->graphical->q3_count; i++)
    if (qa_same_seat(app->graphical->q3_seats[i], seat)) return &app->graphical->q3[i];
  return NULL;
}
static bool actions_client_captures_input(void *ctx, qa_seat_id seat) {
  qa_application_input_actions *actions = ctx;
  if (qa_application_campaign_movie(actions->app) != NULL) return true;
  qa_application_q3_seat_client *source = graphical_q3(actions->app, seat);
  return source != NULL && qa_app_application_q3_client_captures_input(source->client);
}
static bool actions_client_input(void *ctx, const qa_seat_input_event *event) {
  qa_application_input_actions *actions = ctx;
  qa_application *app = actions->app;
  qa_application_movie *movie = qa_application_campaign_movie(app);
  if (movie != NULL) return qa_app_campaign_cinematic_input(movie->playback, event);
  qa_application_q3_seat_client *source = graphical_q3(app, event->seat);
  if (source == NULL || !qa_app_application_q3_client_captures_input(source->client)) return false;
  grow((void **)&app->client_inputs, &app->client_input_capacity, app->client_input_count + 1, sizeof *app->client_inputs);
  app->client_inputs[app->client_input_count++] = *event;
  return true;
}
static bool actions_startup_reader(void *ctx, qa_app_console_script_files *scripts, const qa_app_options *options,
    qa_app_prepared_scoped_read_fn *out_read, void **out_read_ctx) {
  qa_application_input_actions *actions = ctx;
  return qa_application_startup_script_reader(actions->content, options, scripts, out_read, out_read_ctx);
}

static bool mounted_script_open(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count) {
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes;
  *out_count = opened.byte_length;
  return true;
}
qa_application_input_actions *qa_application_input_actions_create(qa_application *app, const qa_application_input_actions_options *options) {
  qa_application_input_actions *actions = checked(calloc(1, sizeof *actions));
  actions->app = app;
  /* TS defaults: this.simulation, this.content, this.q2Console, this.localGuest, this.clientCvars, this.imageSettings?.cvars. */
  actions->simulation = options != NULL && options->simulation != NULL ? options->simulation : app->world_simulation;
  actions->content = options != NULL && options->content != NULL ? options->content : app->loaded_content;
  actions->q2_console = options != NULL && options->has_q2_console ? options->q2_console : app->q2_console;
  actions->local_guest = options != NULL && options->has_local_guest ? options->local_guest : app->local_guest;
  if (options != NULL && options->explicit_client_cvars) {
    actions->explicit_client_cvars = true;
    actions->client_cvar_seats = options->client_cvar_seats;
    actions->client_cvars = options->client_cvars;
    actions->client_cvar_count = options->client_cvar_count;
  }
  qa_cvar_registry *shared = options != NULL && options->has_shared_cvars ? options->shared_cvars
      : app->image_settings == NULL ? NULL : qa_app_image_settings_cvars(app->image_settings);
  if (options != NULL) { actions->candidate_action = options->candidate_action; actions->candidate_ctx = options->candidate_ctx; }
  actions->console.ctx = actions;
  actions->console.dialect = actions_console_dialect;
  actions->console.server = actions_console_server;
  actions->console.seat = actions_console_seat;
  qa_app_input_commands *commands = &actions->commands;
  commands->ctx = actions;
  commands->local_player_capacity = actions_local_player_capacity;
  /* readScript: path => content.mounts.open(path).then(resource => resource?.bytes) */
  commands->has_read_script = true;
  commands->read_script.ctx = (void *)qa_app_loaded_content_mounts(actions->content);
  commands->read_script.open = mounted_script_open;
  commands->has_read_mounted_script = true;
  commands->read_mounted_script = qa_app_source_script_reader_create(qa_app_loaded_content_catalog(actions->content),
      (qa_content_mounted *)qa_app_loaded_content_mounts(actions->content),
      qa_app_loaded_content_recipe(actions->content)->engine_behavior.content);
  commands->startup_reader = actions_startup_reader;
  if (app->host.llm_requester != NULL) commands->llm = app->host.llm_requester;
  commands->quit = actions_quit;
  commands->execute = actions_execute;
  commands->print = actions_print;
  commands->arsenal_impulse_provider = actions_arsenal_impulse_provider;
  commands->binding_items = actions_binding_items;
  commands->binding_capabilities = actions_binding_capabilities;
  commands->shared_cvars = shared;
  commands->console = &actions->console;
  commands->client_captures_input = actions_client_captures_input;
  commands->client_input = actions_client_input;
  return actions;
}
void qa_application_input_actions_release(void *record) {
  qa_application_input_actions *actions = record;
  if (actions == NULL) return;
  qa_app_source_script_reader_destroy(actions->commands.read_mounted_script);
  free(actions->binding_items);
  free(actions->shared_names);
}
void qa_application_input_actions_destroy(qa_application_input_actions *actions) {
  if (actions == NULL) return;
  qa_application_input_actions_release(actions);
  free(actions);
}

/* ---- 1354-1360 ---- */
void qa_application_dispatch_client_inputs(qa_application *app) {
  qa_seat_input_event *events = app->client_inputs;
  size_t count = app->client_input_count;
  app->client_inputs = NULL; app->client_input_count = 0; app->client_input_capacity = 0;
  for (size_t i = 0; i < count; i++) {
    qa_application_q3_seat_client *source = graphical_q3(app, events[i].seat);
    if (source == NULL) continue;
    qa_app_application_q3_client_input(source->client, &events[i]);
  }
  free(events);
}

/* ---- 1362-1374 bindServerSettingCommand ---- */
typedef struct server_setting_binding {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_command_buffer *commands;
} server_setting_binding;
static void server_setting_handler(void *ctx, qa_command_invocation *invocation) {
  server_setting_binding *binding = ctx;
  size_t count = 0;
  const char *const *args = invocation_args(invocation, &count);
  if (count != 2) qa_raise("Error", 0, 0, "Usage: server_setting <server:setting-id> <value>");
  const char *id = args[0], *value = args[1];
  qa_app_shared_sim_bound_settings settings = qa_app_shared_sim_server_settings(binding->simulation);
  const qa_bound_server_setting *found = NULL;
  for (size_t i = 0; i < settings.count; i++)
    if (is(settings.bindings[i].definition->id, id)) { found = &settings.bindings[i]; break; }
  if (found == NULL) {
    qa_app_shared_sim_bound_settings_free(&settings);
    qa_raise("Error", 0, 0, "No selected server setting %s", id);
  }
  qa_server_setting_status status = qa_write_server_setting(found, value);
  qa_app_shared_sim_bound_settings_free(&settings);
  if (status.pending)
    qa_application_printf(binding->app, "%s = %s (effective %s; %s)\n", id, status.desired, status.effective,
        qa_server_apply_at_name(status.apply_at));
  else qa_application_printf(binding->app, "%s = %s\n", id, status.desired);
  qa_server_setting_status_free(&status);
}
static void server_setting_release(void *ctx) {
  server_setting_binding *binding = ctx;
  (void)qa_command_buffer_unregister(binding->commands, "server_setting", server_setting_handler, binding, true);
  free(binding);
}
qa_application_release qa_application_bind_server_setting_command(qa_application *app, qa_command_buffer *commands,
    qa_app_shared_simulation *simulation) {
  server_setting_binding *binding = checked(malloc(sizeof *binding));
  binding->app = app; binding->simulation = simulation; binding->commands = commands;
  (void)qa_command_buffer_register(commands, "server_setting", server_setting_handler, binding, NULL, QA_COMMAND_WORLD);
  qa_application_release release = { server_setting_release, binding };
  return release;
}

/* ---- 1375-1380 ---- */
static double cvar_or(qa_cvar_registry *cvars, const char *name, double fallback) {
  return cvars == NULL ? fallback : qa_cvar_registry_variable_value(cvars, name);
}
qa_debug_graph_settings qa_application_debug_graph_settings(qa_application *app) {
  qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
  qa_debug_graph_settings settings;
  settings.debuggraph = cvar_or(cvars, "debuggraph", 0);
  settings.timegraph = cvar_or(cvars, "timegraph", 0);
  settings.netgraph = cvar_or(cvars, "netgraph", 0);
  settings.height = cvar_or(cvars, "graphheight", 32);
  settings.scale = cvar_or(cvars, "graphscale", 1);
  settings.shift = cvar_or(cvars, "graphshift", 0);
  return settings;
}

/* ---- 1430-1435 ---- */
void qa_application_mod_command(qa_application *app, qa_command_invocation *command, qa_app_shared_simulation *simulation) {
  (void)app; /* TS modCommand reads only its arguments */
  qa_mod_selection selection;
  char *text = NULL;
  if (!qa_read_mod_command(command->raw, &selection, &text)) qa_range_error("Out of memory");
  qa_mod_commands *mods = (qa_mod_commands *)simulation->options.mod_commands;
  if (mods == NULL) {
    qa_mod_selection_free(&selection); free(text);
    qa_raise("Error", 0, 0, "This world has no component command services");
  }
  (void)qa_mod_commands_execute(mods, selection, text, command->source);
  qa_mod_selection_free(&selection);
  free(text);
}

/* ---- 1681-1695 kickTargets ---- */
static void strip_colors_lower(const char *text, char *out, size_t capacity) {
  /* name.replace(/\^[0-9]/g, "").toLowerCase() */
  size_t n = 0;
  for (size_t i = 0; text[i] != '\0' && n + 1 < capacity; i++) {
    if (text[i] == '^' && text[i + 1] >= '0' && text[i + 1] <= '9') { i++; continue; }
    out[n++] = (char)tolower((unsigned char)text[i]);
  }
  out[n] = '\0';
}
size_t qa_application_kick_targets(qa_application *app, const char *const *args, size_t count, qa_actor_id **out) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  if (count == 0) qa_raise("Error", 0, 0, "Usage: kick <player name|slot|all|allbots>");
  const char *target = args[0];
  char *lowered = lower_text(target);
  size_t player_count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(simulation, &player_count);
  qa_actor_id *selected = player_count == 0 ? NULL : checked(malloc(player_count * sizeof *selected));
  size_t selected_count = 0;
  qa_q2_product_runtime *q2 = qa_app_shared_sim_q2_source(simulation);
  for (size_t i = 0; i < player_count; i++) {
    qa_actor_id actor = players[i];
    qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(simulation, actor);
    if (player == NULL || qa_application_local_seat(app, player->client) != NULL) continue;
    bool take;
    if (strcmp(lowered, "all") == 0) take = true;
    else if (strcmp(lowered, "allbots") == 0) {
      qa_actor_id bot;
      take = app->bots != NULL && app->bots->actor(app->bots->ctx, player->client, &bot);
    } else {
      const char *name = "";
      char scratch[2048];
      if (source != NULL) {
        qa_q3_game_client *client = qa_q3_entity_pool_client_at(qa_app_q3_source_runtime_pool(source), (double)player->client.slot);
        name = client->pers.netname == NULL ? "" : client->pers.netname;
      } else if (q2 != NULL) {
        qa_q2_base_player_state *state = qa_q2_players_get_state(q2->players, actor);
        if (state != NULL) name = qa_app_q2_operator_player_name(state->userinfo, state->name, scratch, sizeof scratch);
      }
      char slot[32], stripped[2048];
      (void)snprintf(slot, sizeof slot, "%u", (unsigned)player->client.slot);
      strip_colors_lower(name, stripped, sizeof stripped);
      take = strcmp(slot, target) == 0 || strcmp(stripped, lowered) == 0;
    }
    if (take) selected[selected_count++] = actor;
  }
  free(lowered);
  if (selected_count == 0) {
    free(selected);
    qa_raise("Error", 0, 0, "Player %s is not on the server", target);
  }
  *out = selected;
  return selected_count;
}

/* ---- 1697-1708 kickClients ---- */
static bool network_disconnect_client(qa_application *app, qa_client_id client, const char *reason) {
  qa_application_native_server *network = app->network;
  if (network == NULL) return false;
  switch (network->kind) {
    case QA_APPLICATION_NETWORK_Q1: return qa_app_q1_server_network_disconnect_client(network->q1, client, reason);
    case QA_APPLICATION_NETWORK_QW: return qa_app_qw_server_network_disconnect_client(network->qw, client, reason);
    case QA_APPLICATION_NETWORK_Q2: return qa_app_q2_server_network_disconnect_client(network->q2, client, reason);
    case QA_APPLICATION_NETWORK_Q3: return qa_app_q3_server_network_disconnect_client(network->q3, client, reason);
    case QA_APPLICATION_NETWORK_UNIFIED: return qa_app_unified_server_network_disconnect_client(network->unified, client, reason);
  }
  return false;
}
void qa_application_kick_clients(qa_application *app, const char *const *args, size_t count) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(app->world_simulation);
  qa_actor_id *targets = NULL;
  size_t target_count = qa_application_kick_targets(app, args, count, &targets);
  for (size_t i = 0; i < target_count; i++) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(app->world_simulation, targets[i]);
    if (player == NULL) continue;
    qa_client_id client = player->client;
    if (source != NULL) {
      qa_app_q3_source_engine *engine = &qa_app_q3_source_runtime_host(source)->engine;
      engine->drop_client(engine->ctx, (double)client.slot, "was kicked");
    } else if (!(app->bots != NULL && app->bots->disconnect(app->bots->ctx, client.slot))
        && !network_disconnect_client(app, client, "was kicked")) {
      qa_app_shared_sim_disconnect_player(app->world_simulation, targets[i]);
      qa_engine_session_close_client(app->session, client);
    }
  }
  free(targets);
}

/* ---- 1710-1723 requestRestart ---- */
void qa_application_request_restart(qa_application *app, const char *const *args, size_t count) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(app->world_simulation);
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(app->world_simulation);
  if (source == NULL && guest == NULL) qa_raise("Error", 0, 0, "map_restart requires the Quake III game provider");
  if (app->last_restart_frame == app->frames || app->has_pending_restart) return;
  double delay = count == 0 ? 5 : qa_native_atoi(args[0]);
  qa_app_q3_source_host *host = source == NULL ? NULL : qa_app_q3_source_runtime_host(source);
  qa_cvar_registry *cvars = host != NULL ? host->cvars : qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(guest));
  double now = host != NULL ? host->now(host->ctx)
      : guest != NULL ? qa_app_q3_qvm_server_game_time_milliseconds(guest) : app->elapsed;
  bool scheduled = delay != 0 && qa_cvar_registry_variable_value(cvars, "g_doWarmup") == 0;
  app->pending_restart = scheduled ? (double)qa_js_to_int32(now + (double)(int32_t)((uint32_t)qa_js_to_int32(delay) * 1000u)) : now;
  app->has_pending_restart = true;
  char text[40];
  qa_net_js_number(app->pending_restart, text, sizeof text);
  if (scheduled && source != NULL) host->configstring_set(host->configstrings_ctx, 5, text);
  else if (scheduled && guest != NULL) qa_app_q3_qvm_server_game_set_configstring(guest, 5, text);
}

/* ---- 1725-1731 appendQ1Commands ---- */
void qa_application_append_q1_commands(qa_application *app, const qa_app_sim_presentation_event *const *events, size_t count) {
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_presentation_event *source = events[i];
    if (!is(source->kind, "q1") || !is(source->q1->kind, "server-command")) continue;
    if (app->source_commands == NULL) qa_raise("Error", 0, 0, "Q1 source console has no command owner");
    qa_command_buffer_append(app->source_commands, source->q1->text, NULL, -1);
  }
}

/* ---- 1734-1751 writeGameLog / closeGameLog ---- */
static void game_log_print(const char *text, void *ctx) { qa_application_print(ctx, text); }
void qa_application_write_game_log(qa_application *app, qa_app_shared_simulation *simulation, const char *text) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  if (source == NULL) return;
  qa_q3_game_level *level = qa_app_q3_source_subsystems_owner_level(qa_app_q3_source_runtime_owner(source));
  double elapsed = trunc((level->base.time - level->base.start_time) / 1000), seconds = fmod(elapsed, 60);
  char minutes[48];
  qa_net_js_number(trunc(elapsed / 60), minutes, sizeof minutes);
  char tens[48], ones[48];
  qa_net_js_number(trunc(seconds / 10), tens, sizeof tens);
  qa_net_js_number(fmod(seconds, 10), ones, sizeof ones);
  size_t n = strlen(text) + strlen(minutes) + strlen(tens) + strlen(ones) + 8;
  char *line = checked(malloc(n));
  (void)snprintf(line, n, "%3s:%s%s %s", minutes, tens, ones, text);
  if (app->launch_options.dedicated) qa_application_print(app, text);
  qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(source)->cvars;
  char *name = qa_application_cvar_string(cvars, "g_log");
  if (name[0] == '\0') { free(name); free(line); return; }
  bool sync = qa_cvar_registry_variable_value(cvars, "g_logSync") != 0;
  if (app->game_log == NULL || strcmp(app->game_log->name, name) != 0 || app->game_log->sync != sync) {
    qa_application_close_game_log(app);
    qa_config_store config = qa_application_source_config(&app->launch_options, app->loaded_content);
    qa_user_file_store store;
    if (!qa_user_file_store_init(&store, config.root)) qa_range_error("Out of memory");
    qa_config_store_destroy(&config);
    qa_writable_binary_file *file = checked(calloc(1, sizeof *file));
    bool opened = qa_user_file_store_open(&store, name, sync ? QA_WRITABLE_APPEND_SYNC : QA_WRITABLE_APPEND,
        game_log_print, app, file);
    qa_user_file_store_destroy(&store);
    if (!opened) {
      free(file);
      qa_application_printf(app, "WARNING: Couldn't open logfile: %s\n", name);
      free(name); free(line);
      return;
    }
    app->game_log = checked(malloc(sizeof *app->game_log));
    app->game_log->name = name; name = NULL;
    app->game_log->sync = sync;
    app->game_log->file = file;
  }
  free(name);
  (void)qa_writable_binary_file_write(app->game_log->file, (const uint8_t *)line, strlen(line));
  free(line);
}
void qa_application_close_game_log(qa_application *app) {
  if (app->game_log != NULL) {
    qa_writable_binary_file_close(app->game_log->file);
    free(app->game_log->file);
    free(app->game_log->name);
    free(app->game_log);
  }
  app->game_log = NULL;
}

/* TextDecoder("utf-8").decode(bytes): malformed sequences become U+FFFD
 * (WHATWG maximal-subpart replacement). */
static char *utf8_decode_text(const uint8_t *bytes, size_t count) {
  char *out = checked(malloc(count * 3 + 1));
  size_t n = 0, i = 0;
  while (i < count) {
    uint8_t b = bytes[i];
    size_t need = 0; uint8_t lower = 0x80, upper = 0xBF;
    if (b < 0x80) { out[n++] = (char)b; i++; continue; }
    if (b >= 0xC2 && b <= 0xDF) need = 1;
    else if (b >= 0xE0 && b <= 0xEF) { need = 2; if (b == 0xE0) lower = 0xA0; if (b == 0xED) upper = 0x9F; }
    else if (b >= 0xF0 && b <= 0xF4) { need = 3; if (b == 0xF0) lower = 0x90; if (b == 0xF4) upper = 0x8F; }
    else { out[n++] = (char)0xEF; out[n++] = (char)0xBF; out[n++] = (char)0xBD; i++; continue; }
    size_t j = 1;
    for (; j <= need && i + j < count; j++) {
      uint8_t c = bytes[i + j];
      if (c < lower || c > upper) break;
      lower = 0x80; upper = 0xBF;
    }
    if (j == need + 1) { memcpy(out + n, bytes + i, need + 1); n += need + 1; i += need + 1; }
    else { out[n++] = (char)0xEF; out[n++] = (char)0xBF; out[n++] = (char)0xBD; i += j; }
  }
  out[n] = '\0';
  return out;
}

/* ---- 1382-1393 createDebugGraphOverlay ---- */
typedef struct graph_overlay_draw { const qa_draw2d *draw; } graph_overlay_draw;
static void graph_fill(void *ctx, qa_rect rect, qa_vec4 color, const qa_picture_asset *white) {
  graph_overlay_draw *draw = ctx;
  qa_draw2d_fill_rect(draw->draw, rect, color, white);
}
static qa_vec4 graph_palette_color(void *ctx, double index) {
  const qa_palette *palette = ctx;
  return qa_debug_graph_color(*palette, index);
}
static void graph_overlay_run(void *ctx, const qa_draw2d *draw, qa_rect view, qa_app_component_drawings *components) {
  qa_application_graph_overlay *overlay = ctx;
  qa_application *app = overlay->app;
  qa_debug_graph_settings settings = qa_application_debug_graph_settings(app);
  qa_picture_asset white;
  memset(&white, 0, sizeof white);
  white.kind = QA_PICTURE_IMAGE;
  white.image.name = "white";
  white.image.image = qa_app_application_assets_world(overlay->assets)->shaders->textures->white->image;
  double occupied = qa_app_component_drawings_draw_graphs(components, draw, view, settings, &white);
  if (overlay->provider.family == QA_GAME_FAMILY_Q2 && occupied < view.height) {
    const qa_palette *palette = overlay->provider.palette;
    if (palette == NULL) qa_raise("Error", 0, 0, "Quake II debug graph requires its mounted palette");
    qa_rect rect = view;
    rect.height = view.height - occupied;
    graph_overlay_draw fill = { draw };
    qa_source_debug_graph_draw(app->debug_graph, graph_fill, &fill, rect, settings, graph_palette_color, (void *)palette, &white);
  }
}
qa_application_graph_overlay *qa_application_create_debug_graph_overlay(qa_application *app, qa_app_application_assets *assets) {
  qa_application_graph_overlay *overlay = checked(calloc(1, sizeof *overlay));
  overlay->provider = qa_app_application_assets_provider(assets,
      qa_app_loaded_content_recipe(qa_app_application_assets_content(assets))->presentation.assets);
  overlay->app = app;
  overlay->assets = assets;
  overlay->draw = graph_overlay_run;
  overlay->ctx = overlay;
  return overlay;
}

/* ---- 1438-1455 createSourceCommandBuffer: callbacks follow the published binding ---- */
static const qa_command_buffer_options *current_binding(qa_application *app) {
  return app->has_source_command_binding ? app->source_command_binding : NULL;
}
static qa_cvar_registry *published_owner(void *ctx, const char *name, qa_command_context source) {
  qa_application *app = ctx;
  const qa_command_buffer_options *current = current_binding(app);
  qa_cvar_registry *cvars = NULL;
  if (current != NULL && current->cvar_routing.owner != NULL) cvars = current->cvar_routing.owner(current->cvar_routing.ctx, name, source);
  if (cvars == NULL && current != NULL) cvars = current->cvars;
  if (cvars == NULL) qa_raise("Error", 0, 0, "Published authority requires a source registry");
  return cvars;
}
static qa_cvar_registry *const *published_visible(void *ctx, qa_command_context source, size_t *out_count) {
  qa_application *app = ctx;
  const qa_command_buffer_options *current = current_binding(app);
  if (current != NULL && current->cvar_routing.visible != NULL) return current->cvar_routing.visible(current->cvar_routing.ctx, source, out_count);
  if (current == NULL || current->cvars == NULL) { *out_count = 0; return NULL; }
  *out_count = 1;
  return &((qa_command_buffer_options *)current)->cvars;
}
static void published_print(void *ctx, const char *text, const qa_command_context *source) {
  (void)source; /* TS print: text => this.host.print(text) */
  qa_application_print(ctx, text);
}
static qa_script_read_result published_read_script(void *ctx, const char *name, qa_command_context source) {
  qa_application *app = ctx;
  const qa_command_buffer_options *current = current_binding(app);
  if (current != NULL && current->read_script != NULL) return current->read_script(current->read_script_ctx, name, source);
  qa_script_read_result missing;
  memset(&missing, 0, sizeof missing);
  missing.kind = QA_SCRIPT_READ_READY;
  return missing;
}
static int published_source_command(void *ctx, qa_command_invocation *command, bool registered) {
  const qa_command_buffer_options *current = current_binding(ctx);
  int result = current != NULL && current->source_command != NULL ? current->source_command(current->source_command_ctx, command, registered) : -1;
  return result < 0 ? 0 : result;
}
static int published_client_game(void *ctx, qa_command_invocation *command) {
  const qa_command_buffer_options *current = current_binding(ctx);
  int result = current != NULL && current->client_game != NULL ? current->client_game(current->client_game_ctx, command) : -1;
  return result < 0 ? 0 : result;
}
static int published_server_game(void *ctx, qa_command_invocation *command) {
  const qa_command_buffer_options *current = current_binding(ctx);
  int result = current != NULL && current->server_game != NULL ? current->server_game(current->server_game_ctx, command) : -1;
  return result < 0 ? 0 : result;
}
static void published_forward(void *ctx, qa_command_invocation *command) {
  const qa_command_buffer_options *current = current_binding(ctx);
  if (current != NULL && current->forward_to_server != NULL) current->forward_to_server(current->forward_to_server_ctx, command);
}
qa_command_buffer *qa_application_create_source_command_buffer(qa_application *app, const qa_command_buffer_options *options) {
  qa_command_buffer_options next;
  memset(&next, 0, sizeof next);
  next.dialect = options->dialect;
  next.context = options->context;
  next.cvar_routing.ctx = app;
  next.cvar_routing.owner = published_owner;
  next.cvar_routing.visible = published_visible;
  next.print = published_print; next.print_ctx = app;
  next.read_script = published_read_script; next.read_script_ctx = app;
  next.source_command = published_source_command; next.source_command_ctx = app;
  next.client_game = published_client_game; next.client_game_ctx = app;
  next.server_game = published_server_game; next.server_game_ctx = app;
  next.forward_to_server = published_forward; next.forward_to_server_ctx = app;
  return checked(qa_command_buffer_create(&next));
}

/* ---- 1457-1470 publishSourceCommands ---- */
typedef struct source_release {
  qa_application *app;
  qa_application_release source;
  qa_app_client_demo_commands *demos_service; /* TS releaseDemos captures the service it attached to */
  void *demos;
  qa_command_buffer *commands;
} source_release;
static void release_published_source(void *ctx) {
  source_release *release = ctx;
  /* TS releaseDemos?.(): runs even after closeOwned dropped ownedDemos. */
  if (release->demos != NULL) qa_app_client_demo_commands_detach(release->demos_service, release->demos);
  if (release->source.fn != NULL) release->source.fn(release->source.ctx);
  free(release);
}
void qa_application_publish_source_commands(qa_application *app, qa_application_prepared_source_commands *prepared) {
  const qa_command_buffer_options *options = prepared->options;
  if (options == NULL) qa_raise("Error", 0, 0, "Published world requires source command bindings");
  if (app->release_source_commands.fn != NULL) app->release_source_commands.fn(app->release_source_commands.ctx);
  app->release_source_commands.fn = NULL; app->release_source_commands.ctx = NULL;
  app->has_source_command_binding = true;
  app->source_command_binding = options;
  /* C ownership: the previously published preparation is unreferenced now. */
  if (app->published_source_commands != NULL && app->published_source_commands != prepared)
    app->published_source_commands->destroy(app->published_source_commands->ctx);
  app->published_source_commands = prepared;
  if (app->source_commands == NULL) app->source_commands = qa_application_create_source_command_buffer(app, options);
  else qa_command_buffer_set_profile(app->source_commands, options->dialect, NULL);
  qa_application_release release_source = prepared->activate(prepared->ctx, app->source_commands);
  source_release *release = checked(calloc(1, sizeof *release));
  release->app = app;
  release->source = release_source;
  release->commands = app->source_commands;
  release->demos_service = app->has_owned_demos ? app->owned_demos_service : NULL;
  release->demos = app->has_owned_demos ? qa_app_client_demo_commands_attach(app->owned_demos_service, app->source_commands) : NULL;
  app->release_source_commands.fn = release_published_source;
  app->release_source_commands.ctx = release;
  app->q2_console = prepared->q2_console;
  qa_application_refresh_application_tools(app);
}

/* ---- 1425-1428 bindSourceCommands ---- */
static void admit_request(void *ctx, const qa_application_command_request *request) {
  qa_application *app = ctx;
  (void)qa_event_ring_admit(&app->requested_commands, request);
}
void qa_application_bind_source_commands(qa_application *app, bool restoring) {
  qa_application_prepared_source_commands *prepared = qa_application_prepare_source_commands(app, app->world_simulation,
      app->loaded_content, restoring, admit_request, app);
  qa_application_publish_source_commands(app, prepared);
}

/* ---- 1472-1679 prepareSourceCommands ---- */
typedef enum handler_kind {
  H_QUEUE,          /* queue(name, args, null, source) */
  H_OPERATOR,       /* sourceAdministrationCommandNames: setmaster marks public, then queue */
  H_LIMITED_RCON,   /* addlrconcmd / dellrconcmd / listlrconcmds */
  H_AUDIO,          /* applicationAudioCommands: queue with the invoking local seat */
  H_MODCMD,         /* modCommand(command, simulation) */
  H_NATIVE_SV,      /* native Q2 sv */
  H_SOURCE_ACTION,  /* sourceAction(action, args, source) */
  H_GUEST_EXEC      /* Q3 guest exec */
} handler_kind;
typedef struct source_prep source_prep;
typedef struct source_handler {
  source_prep *prep;
  const char *name;    /* static text */
  const char *action;  /* H_SOURCE_ACTION name */
  handler_kind kind;
} source_handler;
typedef enum binding_kind { B_REGISTER, B_SERVER_SETTING, B_Q2_CONSOLE, B_Q1_CLIENT_COMMANDS, B_UNREGISTER_EXEC } binding_kind;
typedef struct prep_binding { binding_kind kind; source_handler *handler; } prep_binding;
struct source_prep {
  qa_application_prepared_source_commands prepared;
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_loaded_content *content;
  void (*action)(void *ctx, const qa_application_command_request *request); void *action_ctx;
  qa_command_dialect content_dialect;
  qa_mod_commands *mods;
  qa_app_source_script_reader read_source_script;
  qa_command_buffer_options selected;
  qa_command_buffer_options options;
  bool has_options;
  bool dynamic_q3_cvars;                      /* TS `get cvars() { return game().host.cvars; }` */
  qa_cvar_registry *visible[2];
  qa_cvar_registry **routed; size_t routed_capacity; /* [shared, ...base] scratch */
  qa_command_buffer *own_commands;            /* new CommandBuffer(options) */
  qa_prepared_program *program;
  qa_app_q2_console *q2_console;
  qa_app_sim_classic_guest_world *native_classic;
  qa_app_sim_rerelease_guest_world *native_rerelease;
  qa_app_q3_qvm_server_game *guest;
  source_handler **handlers; size_t handler_count, handler_capacity;
  prep_binding *bindings; size_t binding_count, binding_capacity;
  struct prep_disposer *initial; size_t initial_count, initial_capacity; /* bind()'s first installs */
  qa_app_q2_console_player *console_players; size_t console_player_capacity;
};

static qa_cvar_registry *selected_cvars(source_prep *prep);
/* queue(name, args, seat, source): components may only queue their declared engine commands. */
static void prep_queue(source_prep *prep, const char *name, const char *const *args, size_t count,
    const qa_seat_id *seat, const qa_command_context *source) {
  if (source != NULL && produced_by_instance(source)) {
    char *lowered = lower_text(name);
    bool engine = qa_app_component_engine_command(prep->content_dialect, prep->content, lowered);
    free(lowered);
    if (!engine)
      qa_raise("Error", 0, 0, "Command %s belongs to the primary world; component %s has no declared command with that name",
          name, source->producer->module.id);
  }
  qa_application_command_request request = application_request(name, args, count, seat, source);
  prep->action(prep->action_ctx, &request);
}
static void prep_source_action(source_prep *prep, const char *name, const char *const *args, size_t count,
    const qa_command_context *source) {
  qa_application_command_request request = application_request(name, args, count, NULL, source);
  request.target = QA_APPLICATION_TARGET_SOURCE;
  prep->action(prep->action_ctx, &request);
}
typedef struct native_sv_call { source_prep *prep; qa_command_invocation *invocation; } native_sv_call;
static void native_sv_run(void *ctx) {
  native_sv_call *call = ctx;
  const qa_command_invocation *invocation = call->invocation;
  if (call->prep->native_classic != NULL)
    qa_app_sim_classic_guest_world_server_command(call->prep->native_classic, (const char *const *)invocation->argv, invocation->argc, invocation->args_text);
  else qa_app_sim_rerelease_guest_world_server_command(call->prep->native_rerelease, (const char *const *)invocation->argv, invocation->argc, invocation->args_text);
}
static void prep_print(void *ctx, const char *text) { qa_application_print(ctx, text); }
static void source_handler_run(void *ctx, qa_command_invocation *invocation) {
  source_handler *handler = ctx;
  source_prep *prep = handler->prep;
  qa_application *app = prep->app;
  size_t count = 0;
  const char *const *args = invocation_args(invocation, &count);
  switch (handler->kind) {
    case H_QUEUE:
      prep_queue(prep, handler->name, args, count, NULL, &invocation->source);
      return;
    case H_OPERATOR: {
      if (strcmp(handler->name, "setmaster") == 0 && app->launch_options.dedicated
          && (prep->selected.dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || prep->selected.dialect == QA_COMMAND_DIALECT_Q2_RERELEASE)
          && selected_cvars(prep) != NULL)
        qa_application_cvar_set(selected_cvars(prep), "public", "1", false);
      prep_queue(prep, handler->name, args, count, NULL, &invocation->source);
      return;
    }
    case H_LIMITED_RCON:
      if (app->operator_state == NULL) qa_raise("Error", 0, 0, "Server operator is unavailable");
      (void)qa_app_server_operator_limited_rcon_command(app->operator_state, handler->name, invocation->args_text, prep_print, app);
      return;
    case H_AUDIO: {
      const qa_command_origin *origin = root_origin(&invocation->source);
      qa_seat_id seat = origin->seat;
      prep_queue(prep, handler->name, args, count, origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT ? &seat : NULL, &invocation->source);
      return;
    }
    case H_MODCMD:
      qa_application_mod_command(app, invocation, prep->simulation);
      return;
    case H_NATIVE_SV: {
      const char *first = count == 0 ? "" : args[0];
      if (strcmp(first, "addip") == 0 || strcmp(first, "removeip") == 0 || strcmp(first, "listip") == 0 || strcmp(first, "writeip") == 0) {
        prep_queue(prep, "sv", args, count, NULL, &invocation->source);
        return;
      }
      native_sv_call call = { prep, invocation };
      qa_app_q2_game_callback(native_sv_run, &call);
      return;
    }
    case H_SOURCE_ACTION:
      prep_source_action(prep, handler->action, args, count, &invocation->source);
      return;
    case H_GUEST_EXEC: {
      if (produced_by_instance(&invocation->source)) { qa_command_invocation_execute_script(invocation); return; }
      if (qa_app_q3_qvm_server_game_engine_active(prep->guest))
        qa_raise("Error", 0, 0, "Q3 guest immediate exec is unsupported; append the script command");
      qa_command_context source;
      memset(&source, 0, sizeof source);
      source.session = invocation->source.session;
      source.origin = invocation->source.origin;
      prep_queue(prep, "exec", args, count, NULL, &source);
      return;
    }
  }
}
static source_handler *prep_handler(source_prep *prep, handler_kind kind, const char *name, const char *action) {
  source_handler *handler = checked(calloc(1, sizeof *handler));
  handler->prep = prep; handler->kind = kind; handler->name = name; handler->action = action;
  grow((void **)&prep->handlers, &prep->handler_capacity, prep->handler_count + 1, sizeof *prep->handlers);
  prep->handlers[prep->handler_count++] = handler;
  return handler;
}
static void prep_push_binding(source_prep *prep, binding_kind kind, source_handler *handler) {
  grow((void **)&prep->bindings, &prep->binding_capacity, prep->binding_count + 1, sizeof *prep->bindings);
  prep->bindings[prep->binding_count].kind = kind;
  prep->bindings[prep->binding_count].handler = handler;
  prep->binding_count++;
}
/* register(commands, name, handler): registers now and on every activation. */
static void prep_register(source_prep *prep, qa_command_buffer *commands, const char *name, handler_kind kind, const char *action) {
  source_handler *handler = prep_handler(prep, kind, name, action);
  (void)qa_command_buffer_register(commands, name, source_handler_run, handler, qa_app_save_command_documentation(name), QA_COMMAND_WORLD);
  prep_push_binding(prep, B_REGISTER, handler);
}

/* One installed binding's disposer. */
typedef struct prep_disposer {
  binding_kind kind;
  qa_command_buffer *owner;
  source_handler *handler;
  bool installed;
  qa_application_release release;
  qa_app_q2_console_binding *q2_console;
  qa_app_q1_client_commands *q1_commands;
} prep_disposer;
static void q1_execute(void *ctx, const char *name, const char *const *args, size_t count, const qa_seat_id *seat, qa_command_context source) {
  prep_queue(ctx, name, args, count, seat, &source);
}
static prep_disposer prep_install(source_prep *prep, const prep_binding *binding, qa_command_buffer *owner) {
  prep_disposer disposer;
  memset(&disposer, 0, sizeof disposer);
  disposer.kind = binding->kind;
  disposer.owner = owner;
  switch (binding->kind) {
    case B_REGISTER:
      disposer.handler = binding->handler;
      disposer.installed = qa_command_buffer_register(owner, binding->handler->name, source_handler_run, binding->handler,
          qa_app_save_command_documentation(binding->handler->name), QA_COMMAND_WORLD);
      break;
    case B_SERVER_SETTING:
      disposer.release = qa_application_bind_server_setting_command(prep->app, owner, prep->simulation);
      break;
    case B_Q2_CONSOLE:
      disposer.q2_console = qa_app_q2_console_bind(prep->q2_console, owner);
      break;
    case B_Q1_CLIENT_COMMANDS: {
      qa_cvar_registry *cvars = selected_cvars(prep);
      disposer.q1_commands = qa_app_register_q1_client_commands(owner, qa_cvar_registry_dialect(cvars), q1_execute, prep);
      break;
    }
    case B_UNREGISTER_EXEC:
      (void)qa_command_buffer_unregister(owner, "exec", NULL, NULL, false);
      break;
  }
  return disposer;
}
static void prep_dispose(prep_disposer *disposer) {
  switch (disposer->kind) {
    case B_REGISTER:
      if (disposer->installed) (void)qa_command_buffer_unregister(disposer->owner, disposer->handler->name, source_handler_run, disposer->handler, true);
      break;
    case B_SERVER_SETTING:
      disposer->release.fn(disposer->release.ctx);
      break;
    case B_Q2_CONSOLE:
      qa_app_q2_console_binding_release(disposer->q2_console);
      break;
    case B_Q1_CLIENT_COMMANDS:
      qa_app_q1_client_commands_release(disposer->q1_commands);
      break;
    case B_UNREGISTER_EXEC:
      break;
  }
}
typedef struct prep_activation { prep_disposer *disposers; size_t count; } prep_activation;
static void prep_deactivate(void *ctx) {
  prep_activation *activation = ctx;
  for (size_t i = activation->count; i-- > 0;) prep_dispose(&activation->disposers[i]);
  free(activation->disposers);
  free(activation);
}
static qa_application_release prep_activate(void *ctx, qa_command_buffer *commands) {
  source_prep *prep = ctx;
  prep_activation *activation = checked(calloc(1, sizeof *activation));
  activation->disposers = prep->binding_count == 0 ? NULL : checked(calloc(prep->binding_count, sizeof *activation->disposers));
  for (size_t i = 0; i < prep->binding_count; i++) {
    activation->disposers[i] = prep_install(prep, &prep->bindings[i], commands);
    activation->count = i + 1;
  }
  qa_application_release release = { prep_deactivate, activation };
  return release;
}
static void prep_destroy(void *ctx) {
  source_prep *prep = ctx;
  for (size_t i = prep->initial_count; i-- > 0;) prep_dispose(&prep->initial[i]);
  free(prep->initial);
  if (prep->program != NULL) prep->program->destroy(prep->program->ctx);
  if (prep->own_commands != NULL) qa_command_buffer_destroy(prep->own_commands);
  if (prep->q2_console != NULL) qa_app_q2_console_destroy(prep->q2_console);
  qa_app_source_script_reader_destroy(prep->read_source_script);
  for (size_t i = 0; i < prep->handler_count; i++) free(prep->handlers[i]);
  free(prep->handlers);
  free(prep->bindings);
  free(prep->console_players);
  free(prep->routed);
  free(prep);
}

/* The Q3 source game (TS game(): "Source console no longer owns a Quake III game"). */
static qa_app_q3_source_runtime *prep_game(source_prep *prep) {
  qa_app_q3_source_runtime *current = qa_app_shared_sim_q3_source(prep->simulation);
  if (current == NULL) qa_raise("Error", 0, 0, "Source console no longer owns a Quake III game");
  return current;
}
static qa_cvar_registry *selected_cvars(source_prep *prep) {
  if (prep->dynamic_q3_cvars) return qa_app_q3_source_runtime_host(prep_game(prep))->cvars;
  return prep->selected.cvars;
}

/* The composed options create() builds over the selected ones. */
static qa_cvar_registry *prep_shared(source_prep *prep) {
  return prep->app->image_settings == NULL ? NULL : qa_app_image_settings_cvars(prep->app->image_settings);
}
static qa_cvar_registry *prep_owner(void *ctx, const char *name, qa_command_context source) {
  source_prep *prep = ctx;
  qa_cvar_registry *shared = prep_shared(prep);
  if (shared != NULL) {
    qa_cvar_read read;
    if (qa_cvar_registry_find(shared, name, &read)) { qa_cvar_read_free(&read); return shared; }
  }
  qa_cvar_registry *registry = prep->mods == NULL ? NULL : qa_mod_commands_cvars(prep->mods, source);
  if (registry == NULL && prep->selected.cvar_routing.owner != NULL)
    registry = prep->selected.cvar_routing.owner(prep->selected.cvar_routing.ctx, name, source);
  if (registry == NULL) registry = selected_cvars(prep);
  if (registry == NULL) qa_raise("Error", 0, 0, "Source command has no cvar registry");
  return registry;
}
static qa_cvar_registry *const *prep_visible(void *ctx, qa_command_context source, size_t *out_count) {
  source_prep *prep = ctx;
  qa_cvar_registry *registry = prep->mods == NULL ? NULL : qa_mod_commands_cvars(prep->mods, source);
  qa_cvar_registry *shared = prep_shared(prep);
  qa_cvar_registry *const *base;
  size_t base_count;
  if (registry == NULL) {
    if (prep->selected.cvar_routing.visible != NULL) base = prep->selected.cvar_routing.visible(prep->selected.cvar_routing.ctx, source, &base_count);
    else {
      qa_cvar_registry *cvars = selected_cvars(prep);
      prep->visible[1] = cvars;
      base = cvars == NULL ? NULL : &prep->visible[1];
      base_count = cvars == NULL ? 0 : 1;
    }
  } else { prep->visible[1] = registry; base = &prep->visible[1]; base_count = 1; }
  bool includes = false;
  for (size_t i = 0; i < base_count; i++) if (base[i] == shared) includes = true;
  if (shared == NULL || includes) { *out_count = base_count; return base; }
  /* [shared, ...base]: base here is at most the one registry in visible[1]. */
  grow((void **)&prep->routed, &prep->routed_capacity, base_count + 1, sizeof *prep->routed);
  qa_cvar_registry *first = base_count == 0 ? NULL : base[0];
  for (size_t i = base_count; i > 1; i--) prep->routed[i] = base[i - 1];
  if (base_count > 0) prep->routed[1] = first;
  prep->routed[0] = shared;
  *out_count = base_count + 1;
  return prep->routed;
}
static qa_script_read_result script_text(char *text) {
  qa_script_read_result result;
  memset(&result, 0, sizeof result);
  result.kind = QA_SCRIPT_READ_READY;
  result.has_text = text != NULL;
  result.text = text;
  return result;
}
static qa_script_read_result prep_read_script(void *ctx, const char *name, qa_command_context source) {
  source_prep *prep = ctx;
  if (produced_by_instance(&source)) {
    const char *text = prep->mods == NULL ? NULL : qa_mod_commands_read_script(prep->mods, name, source);
    return script_text(text == NULL ? NULL : dup_text(text));
  }
  if (prep->selected.read_script != NULL) {
    qa_script_read_result selected = prep->selected.read_script(prep->selected.read_script_ctx, name, source);
    if (selected.kind != QA_SCRIPT_READ_READY || selected.has_text) return selected;
  }
  uint8_t *bytes = NULL;
  size_t count = 0;
  if (!prep->read_source_script.open(prep->read_source_script.ctx, name, &bytes, &count)) return script_text(NULL);
  char *text = utf8_decode_text(bytes, count);
  free(bytes);
  return script_text(text);
}
static int prep_source_command(void *ctx, qa_command_invocation *command, bool registered) {
  source_prep *prep = ctx;
  char *name = lower_text(command->argc == 0 ? "" : command->argv[0]);
  if (!produced_by_instance(&command->source) || qa_app_component_engine_command(prep->content_dialect, prep->content, name)) {
    free(name);
    return 0;
  }
  qa_cvar_registry *component = prep->mods == NULL ? NULL : qa_mod_commands_cvars(prep->mods, command->source);
  qa_command_dialect component_dialect = component == NULL ? QA_COMMAND_DIALECT_Q3 : qa_cvar_registry_dialect(component);
  if ((prep->mods != NULL && qa_mod_commands_handles(prep->mods, name, command->source))
      || (strcmp(name, "sv") == 0 && component != NULL
          && (component_dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || component_dialect == QA_COMMAND_DIALECT_Q2_RERELEASE))) {
    free(name);
    if (prep->mods == NULL || !qa_mod_commands_invoke(prep->mods, command))
      qa_raise("Error", 0, 0, "Component command was not handled: %s", command->raw);
    return 1;
  }
  if (registered) {
    qa_caught_error error;
    (void)snprintf(error.message, sizeof error.message, "Command %s belongs to the primary world; component %s has no declared command with that name",
        name, command->source.producer->module.id);
    free(name);
    qa_raise("Error", 0, 0, "%s", error.message);
  }
  free(name);
  return 0;
}
static int prep_client_game(void *ctx, qa_command_invocation *command) {
  source_prep *prep = ctx;
  if (produced_by_instance(&command->source)) return 0;
  return prep->selected.client_game == NULL ? -1 : prep->selected.client_game(prep->selected.client_game_ctx, command);
}
static int prep_server_game(void *ctx, qa_command_invocation *command) {
  source_prep *prep = ctx;
  if (!produced_by_instance(&command->source))
    return prep->selected.server_game == NULL ? -1 : prep->selected.server_game(prep->selected.server_game_ctx, command);
  return prep->mods == NULL ? 0 : qa_mod_commands_invoke(prep->mods, command) ? 1 : 0;
}
static void prep_forward(void *ctx, qa_command_invocation *command) {
  source_prep *prep = ctx;
  if (!produced_by_instance(&command->source)) {
    if (prep->selected.forward_to_server != NULL) prep->selected.forward_to_server(prep->selected.forward_to_server_ctx, command);
    return;
  }
  if (command->dialect == QA_COMMAND_DIALECT_Q3 || prep->mods == NULL || !qa_mod_commands_invoke(prep->mods, command))
    qa_application_printf(prep->app, "Unknown component command: %s\n", command->raw);
}

/* create(selected): the composed options, the buffer or program, and the shared registrations. */
static qa_command_buffer *prep_create(source_prep *prep, const qa_command_buffer_options *selected) {
  qa_application *app = prep->app;
  prep->selected = *selected;
  prep->options = *selected;
  prep->options.cvar_routing.ctx = prep;
  prep->options.cvar_routing.owner = prep_owner;
  prep->options.cvar_routing.visible = prep_visible;
  prep->options.read_script = prep_read_script; prep->options.read_script_ctx = prep;
  prep->options.source_command = prep_source_command; prep->options.source_command_ctx = prep;
  prep->options.client_game = prep_client_game; prep->options.client_game_ctx = prep;
  prep->options.server_game = prep_server_game; prep->options.server_game_ctx = prep;
  prep->options.forward_to_server = prep_forward; prep->options.forward_to_server_ctx = prep;
  prep->has_options = true;
  qa_command_buffer *commands;
  if (app->source_commands == NULL) commands = prep->own_commands = checked(qa_command_buffer_create(&prep->options));
  else { prep->program = checked(qa_command_buffer_prepare_program(app->source_commands, &prep->options)); commands = prep->program->commands; }
  static const char *const world_commands[] = { "changelevel", "gamemap", "save", "load" };
  size_t operator_count = 0;
  const char *const *operator_names = qa_app_source_administration_command_names(selected->dialect, &operator_count);
  for (size_t i = 0; i < operator_count; i++) {
    const char *name = operator_names[i];
    bool world = false;
    for (size_t j = 0; j < 4; j++) if (strcmp(name, world_commands[j]) == 0) world = true;
    if (world || strcmp(name, "sv") == 0 || strcmp(name, "addlrconcmd") == 0 || strcmp(name, "dellrconcmd") == 0
        || strcmp(name, "listlrconcmds") == 0 || (selected->dialect == QA_COMMAND_DIALECT_Q3 && strcmp(name, "heartbeat") != 0)) continue;
    prep_register(prep, commands, name, H_OPERATOR, NULL);
  }
  if (selected->dialect == QA_COMMAND_DIALECT_Q2_CLASSIC || selected->dialect == QA_COMMAND_DIALECT_Q2_RERELEASE) {
    prep_register(prep, commands, "addlrconcmd", H_LIMITED_RCON, NULL);
    prep_register(prep, commands, "dellrconcmd", H_LIMITED_RCON, NULL);
    prep_register(prep, commands, "listlrconcmds", H_LIMITED_RCON, NULL);
  }
  for (size_t i = 0; i < qa_app_application_audio_command_count; i++)
    prep_register(prep, commands, qa_app_application_audio_commands[i], H_AUDIO, NULL);
  prep_register(prep, commands, "modcmd", H_MODCMD, NULL);
  for (size_t i = 0; i < 4; i++) prep_register(prep, commands, world_commands[i], H_QUEUE, NULL);
  return commands;
}
/* bind(commands, install): installs now and on every activation. */
static void prep_bind(source_prep *prep, qa_command_buffer *commands, binding_kind kind) {
  prep_binding binding = { kind, NULL };
  /* TS discards this install's disposer; C keeps it to release its storage with the preparation's buffer. */
  prep_disposer disposer = prep_install(prep, &binding, commands);
  grow((void **)&prep->initial, &prep->initial_capacity, prep->initial_count + 1, sizeof *prep->initial);
  prep->initial[prep->initial_count++] = disposer;
  prep_push_binding(prep, kind, NULL);
}
static void time_cvar_register(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  bool q1 = dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
  qa_cvar_read read;
  bool found = qa_cvar_registry_find(cvars, name, &read);
  if (found) qa_cvar_read_free(&read);
  if (!q1 || !found || qa_cvar_registry_is_console_created(cvars, name)) {
    qa_cvar_snapshot snapshot;
    (void)qa_cvar_registry_register(cvars, name, value, flags, &snapshot);
    qa_cvar_read_free(&snapshot);
  } else qa_cvar_registry_add_flags(cvars, name, flags);
}

/* ApplicationQ2Console over the prepared world (TS { simulation: () => simulation, content: () => content, print, execute }). */
static bool console_has_q2_source(void *ctx) { return qa_app_shared_sim_q2_source(((source_prep *)ctx)->simulation) != NULL; }
static qa_cvar_registry *console_q2_server_cvars(void *ctx) { return qa_app_shared_sim_q2_server_cvars(((source_prep *)ctx)->simulation); }
static const char *console_map_name(void *ctx) {
  return qa_app_shared_sim_q2_source(((source_prep *)ctx)->simulation)->services->options.map_name;
}
static size_t console_players(void *ctx, const qa_app_q2_console_player **out) {
  source_prep *prep = ctx;
  qa_q2_product_runtime *q2 = qa_app_shared_sim_q2_source(prep->simulation);
  size_t count = q2 == NULL ? 0 : qa_q2_players_state_count(q2->players);
  grow((void **)&prep->console_players, &prep->console_player_capacity, count, sizeof *prep->console_players);
  for (size_t i = 0; i < count; i++) {
    qa_actor_id actor;
    qa_q2_base_player_state *state = NULL;
    (void)qa_q2_players_state_at(q2->players, i, &actor, &state);
    qa_app_q2_console_player *row = &prep->console_players[i];
    row->connected = state->connected;
    row->slot = state->slot; row->score = state->score; row->ping = state->ping;
    row->userinfo = state->userinfo; row->name = state->name;
  }
  *out = prep->console_players;
  return count;
}
static bool console_is_lmctf(void *ctx) {
  qa_q2_product_runtime *q2 = qa_app_shared_sim_q2_source(((source_prep *)ctx)->simulation);
  return q2 != NULL && q2->match.lmctf != NULL;
}
static qa_lmctf_rules *console_lmctf_rules(void *ctx) {
  return qa_q2_lmctf_get_rules(qa_app_shared_sim_q2_source(((source_prep *)ctx)->simulation)->match.lmctf);
}
static uint8_t *console_open_match_file(void *ctx, const char *path, size_t *out_length) {
  source_prep *prep = ctx;
  qa_content_mounted *mounts = (qa_content_mounted *)qa_app_loaded_content_for_content(prep->content,
      qa_app_loaded_content_recipe(prep->content)->match.content);
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(mounts, path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  return opened.bytes;
}
static void console_print(void *ctx, const char *text) { qa_application_print(((source_prep *)ctx)->app, text); }
static void free_selection_consumption(void *consumption) { qa_app_shared_sim_selection_consumption_free(consumption); }
/* TS options.simulation().selectionConsumption() for the Q2 console status line. */
static void console_selection_consumption(void *ctx,
    void (*each)(void *each_ctx, const char *axis, const char *outcome, const char *requested, const char *detail),
    void *each_ctx) {
  qa_app_shared_sim_consumption consumption = qa_app_shared_sim_selection_consumption(((source_prep *)ctx)->simulation);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free_selection_consumption, &consumption);
  for (size_t i = 0; i < consumption.count; i++)
    each(each_ctx, consumption.entries[i].axis, consumption.entries[i].outcome, consumption.entries[i].requested,
        consumption.entries[i].detail);
  qa_cleanup_pop(&guard);
  qa_app_shared_sim_selection_consumption_free(&consumption);
}
static void console_execute(void *ctx, const char *name, const char *const *args, size_t count, qa_command_context source) {
  prep_queue(ctx, name, args, count, NULL, &source);
}
static void console_bind_lmctf_rules(void *ctx, qa_cvar_registry *cvars, qa_lmctf_rules *rules) {
  (void)ctx; /* TS bindLmctfConsoleRules(cvars, rules) */
  qa_bind_lmctf_console_rules(cvars, rules);
}

/* selected.serverGame of the Q3 guest console. */
static int guest_server_game(void *ctx, qa_command_invocation *invocation) {
  source_prep *prep = ctx;
  if (qa_app_q3_qvm_server_game_engine_active(prep->guest)) qa_raise("Error", 0, 0, "Nested Q3 guest console exports are unsupported");
  if (invocation->argc == 0) return 0;
  size_t count = 0;
  const char *const *args = invocation_args(invocation, &count);
  prep_queue(prep, invocation->argv[0], args, count, NULL, &invocation->source);
  return 1;
}
/* selected.clientGame of the Q3 source console: a local Q3 client that handles the command. */
static int q3_client_game(void *ctx, qa_command_invocation *command) {
  source_prep *prep = ctx;
  qa_application *app = prep->app;
  if (command->argc == 0) return 0;
  const char *name = command->argv[0];
  qa_application_graphical *graphical = app->world_simulation == prep->simulation ? app->graphical : NULL;
  if (graphical == NULL) return 0;
  for (size_t i = 0; i < graphical->presentation_count; i++) {
    qa_application_seat_presentation *presentation = &graphical->presentations[i];
    if (presentation->q3_client == NULL || !qa_app_application_q3_client_handles_command(presentation->q3_client->client, name)) continue;
    size_t count = 0;
    const char *const *args = invocation_args(command, &count);
    qa_seat_id seat = qa_session_seat_id(presentation->local->player.seat);
    prep_queue(prep, name, args, count, &seat, &command->source);
    return 1;
  }
  return 0;
}
static int q3_server_game(void *ctx, qa_command_invocation *command) {
  source_prep *prep = ctx;
  qa_app_q3_source_subsystems_owner *owner = qa_app_q3_source_runtime_owner(prep_game(prep));
  return qa_q3_ta_game_server_command_runtime_console_command(qa_app_q3_source_subsystems_owner_server_commands(owner),
      (const char *const *)command->argv, command->argc) ? 1 : 0;
}
static void q3_forward(void *ctx, qa_command_invocation *command) {
  source_prep *prep = ctx;
  qa_application_printf(prep->app, "Unbound source engine command: %s\n", command->raw);
}
/* content.q3Product?.policy ?? { kind: "retail" } */
static qa_q3_product_policy q3_policy(qa_app_loaded_content *content) {
  qa_q3_application_product product;
  if (qa_app_loaded_content_q3_product(content, &product)) return product.policy;
  return qa_q3_product_policy_make(false, false);
}
static bool base_arena_progress_has(qa_application *app, qa_app_shared_simulation *simulation) {
  for (size_t i = 0; i < app->base_arena_progress_count; i++)
    if (app->base_arena_progress[i].simulation == simulation) return true;
  return false;
}

qa_application_prepared_source_commands *qa_application_prepare_source_commands(qa_application *app,
    qa_app_shared_simulation *simulation, qa_app_loaded_content *content, bool restoring,
    void (*action)(void *ctx, const qa_application_command_request *request), void *action_ctx) {
  qa_app_q2_match_simulation reports = qa_app_shared_sim_q2_match_simulation(simulation);
  qa_app_prepare_q2_match_reports(&reports, restoring);
  source_prep *prep = checked(calloc(1, sizeof *prep));
  prep->app = app;
  prep->simulation = simulation;
  prep->content = content;
  prep->action = action; prep->action_ctx = action_ctx;
  prep->mods = (qa_mod_commands *)simulation->options.mod_commands;
  prep->content_dialect = qa_application_content_dialect(content);
  prep->read_source_script = qa_app_source_script_reader_create(qa_app_loaded_content_catalog(content),
      (qa_content_mounted *)qa_app_loaded_content_mounts(content), qa_app_loaded_content_recipe(content)->engine_behavior.content);
  prep->prepared.ctx = prep;
  prep->prepared.activate = prep_activate;
  prep->prepared.destroy = prep_destroy;
  qa_cvar_registry *time_cvars = qa_application_source_cvars(app, simulation);
  if (time_cvars != NULL) {
    qa_app_register_frame_time_cvars(time_cvars);
    qa_app_register_source_administration_cvars(time_cvars);
    time_cvar_register(time_cvars, "cl_avidemo", "0", 0);
    time_cvar_register(time_cvars, "cl_forceavidemo", "0", 0);
    time_cvar_register(time_cvars, "debuggraph", "0", 0);
    time_cvar_register(time_cvars, "timegraph", "0", 0);
    time_cvar_register(time_cvars, "netgraph", "0", 0);
    time_cvar_register(time_cvars, "graphheight", "32", 0);
    time_cvar_register(time_cvars, "graphscale", "1", 0);
    time_cvar_register(time_cvars, "graphshift", "0", 0);
    if (qa_application_level_recovery_available(app, simulation, &app->launch_options)) {
      time_cvar_register(time_cvars, "sv_autosave", "1", QA_CVAR_FLAG_ARCHIVE);
      time_cvar_register(time_cvars, "sv_autosave_interval", "0", QA_CVAR_FLAG_ARCHIVE);
    }
  }
  qa_command_buffer_options selected;
  memset(&selected, 0, sizeof selected);
  selected.context = server_console_context(app);
  selected.print = host_print_source; selected.print_ctx = app;
  qa_command_buffer *source_commands = NULL;
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  if (qa_app_shared_sim_q2_native(simulation, &classic, &rerelease)) {
    prep->native_classic = classic; prep->native_rerelease = rerelease;
    selected.dialect = classic != NULL ? QA_COMMAND_DIALECT_Q2_CLASSIC : QA_COMMAND_DIALECT_Q2_RERELEASE;
    /* TS nativeQ2.services.options.cvars: the native world's registry, published as q2ServerCvars(). */
    selected.cvars = qa_app_shared_sim_q2_server_cvars(simulation);
    qa_command_buffer *commands = prep_create(prep, &selected);
    prep_register(prep, commands, "quit", H_QUEUE, NULL);
    prep_register(prep, commands, "map", H_QUEUE, NULL);
    prep_register(prep, commands, "sv", H_NATIVE_SV, NULL);
    source_commands = commands;
    prep_bind(prep, commands, B_SERVER_SETTING);
  } else if (qa_app_shared_sim_q2_source(simulation) != NULL) {
    qa_app_q2_console_options console;
    memset(&console, 0, sizeof console);
    console.ctx = prep;
    console.has_q2_source = console_has_q2_source;
    console.q2_server_cvars = console_q2_server_cvars;
    console.map_name = console_map_name;
    console.players = console_players;
    console.is_lmctf = console_is_lmctf;
    console.lmctf_rules = console_lmctf_rules;
    console.open_match_file = console_open_match_file;
    console.print = console_print;
    console.execute = console_execute;
    console.bind_lmctf_rules = console_bind_lmctf_rules;
    console.selection_consumption = console_selection_consumption;
    prep->q2_console = checked(qa_app_q2_console_create(console));
    qa_cvar_registry *cvars = qa_app_q2_console_cvars(prep->q2_console);
    selected.dialect = qa_cvar_registry_dialect(cvars);
    selected.cvars = cvars;
    source_commands = prep_create(prep, &selected);
    prep_bind(prep, source_commands, B_Q2_CONSOLE);
    prep_register(prep, source_commands, "sv", H_QUEUE, NULL);
    if (app->ownership.borrowed_client == NULL) {
      prep_register(prep, source_commands, "mvdrecord", H_QUEUE, NULL);
      prep_register(prep, source_commands, "mvdstop", H_QUEUE, NULL);
      prep_register(prep, source_commands, "serverrecord", H_QUEUE, NULL);
      prep_register(prep, source_commands, "serverstop", H_QUEUE, NULL);
    }
    prep_register(prep, source_commands, "addbot", H_QUEUE, NULL);
    prep_register(prep, source_commands, "removebot", H_QUEUE, NULL);
    prep_register(prep, source_commands, "botlist", H_QUEUE, NULL);
    prep_register(prep, source_commands, "kick", H_QUEUE, NULL);
    if (!restoring) qa_app_q2_console_initialize(prep->q2_console);
    prep_register(prep, source_commands, "fly", H_QUEUE, NULL);
    prep_bind(prep, source_commands, B_SERVER_SETTING);
  } else if (qa_app_shared_sim_q1_source(simulation) != NULL || qa_app_shared_sim_quakec_source(simulation) != NULL) {
    const qa_ss_source_runtime *q1 = qa_app_shared_sim_q1_source(simulation);
    qa_cvar_registry *cvars = q1 != NULL ? q1->q1_cvars : qa_app_sim_quakec_source_cvars(qa_app_shared_sim_quakec_source(simulation));
    selected.dialect = qa_cvar_registry_dialect(cvars);
    selected.cvars = cvars;
    qa_command_buffer *commands = prep_create(prep, &selected);
    prep_register(prep, commands, "quit", H_QUEUE, NULL);
    prep_bind(prep, commands, B_Q1_CLIENT_COMMANDS);
    static const char *const names[] = { "map", "say", "addbot", "removebot", "botlist", "kick" };
    for (size_t i = 0; i < 6; i++) prep_register(prep, commands, names[i], H_QUEUE, NULL);
    if (!restoring) qa_app_register_q1_bot_controls(cvars);
    source_commands = commands;
  } else if (qa_app_shared_sim_q3_guest(simulation) != NULL) {
    qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
    prep->guest = guest;
    selected.dialect = QA_COMMAND_DIALECT_Q3;
    selected.cvars = qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(guest));
    selected.server_game = guest_server_game; selected.server_game_ctx = prep;
    qa_command_buffer *commands = prep_create(prep, &selected);
    prep_register(prep, commands, "quit", H_QUEUE, NULL);
    (void)qa_command_buffer_unregister(commands, "exec", NULL, NULL, false);
    prep_push_binding(prep, B_UNREGISTER_EXEC, NULL);
    prep_register(prep, commands, "exec", H_GUEST_EXEC, NULL);
    qa_q3_product_policy policy = q3_policy(content);
    size_t map_count = 0;
    const char *const *maps = qa_q3_product_map_commands(policy, &map_count);
    for (size_t i = 0; i < map_count; i++) prep_register(prep, commands, maps[i], H_QUEUE, NULL);
    static const char *const names[] = { "map_restart", "addbot", "removebot", "botlist", "kick", "cinematic" };
    for (size_t i = 0; i < 6; i++) prep_register(prep, commands, names[i], H_QUEUE, NULL);
    source_commands = commands;
    prep_bind(prep, commands, B_SERVER_SETTING);
  } else if (qa_app_shared_sim_q3_source(simulation) != NULL) {
    qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
    if (!restoring) qa_application_cvar_set(qa_app_q3_source_runtime_host(source)->cvars, "dedicated", app->launch_options.dedicated ? "1" : "0", true);
    selected.dialect = QA_COMMAND_DIALECT_Q3;
    prep->dynamic_q3_cvars = true;
    selected.cvars = qa_app_q3_source_runtime_host(prep_game(prep))->cvars;
    selected.client_game = q3_client_game; selected.client_game_ctx = prep;
    selected.server_game = q3_server_game; selected.server_game_ctx = prep;
    selected.forward_to_server = q3_forward; selected.forward_to_server_ctx = prep;
    qa_command_buffer *commands = prep_create(prep, &selected);
    prep_register(prep, commands, "fly", H_QUEUE, NULL);
    qa_q3_product_policy policy = q3_policy(content);
    size_t map_count = 0;
    const char *const *maps = qa_q3_product_map_commands(policy, &map_count);
    for (size_t i = 0; i < map_count; i++) prep_register(prep, commands, maps[i], H_QUEUE, NULL);
    prep_register(prep, commands, "map_restart", H_QUEUE, NULL);
    prep_register(prep, commands, "cinematic", H_QUEUE, NULL);
    qa_application_prepare_base_arena_progress(app, simulation, content, restoring);
    bool arena = base_arena_progress_has(app, simulation);
    if (qa_application_is_team_arena_skirmish(app, simulation) || arena)
      prep_register(prep, commands, "postgame", H_SOURCE_ACTION, "postgame");
    if (arena) {
      prep_register(prep, commands, "spPostgame", H_SOURCE_ACTION, "spPostgame");
      prep_register(prep, commands, "iamacheater", H_SOURCE_ACTION, "arena-unlock");
      prep_register(prep, commands, "iamamonkey", H_SOURCE_ACTION, "arena-medals");
      prep_register(prep, commands, "arena-reset", H_SOURCE_ACTION, "arena-reset");
    }
    prep_register(prep, commands, "teamarena-results", H_SOURCE_ACTION, "teamarena-results");
    prep_register(prep, commands, "kick", H_SOURCE_ACTION, "kick");
    prep_register(prep, commands, "removebot", H_QUEUE, NULL);
    prep_register(prep, commands, "centerview", H_SOURCE_ACTION, "centerview");
    prep_register(prep, commands, "quit", H_QUEUE, NULL);
    source_commands = commands;
    prep_bind(prep, commands, B_SERVER_SETTING);
  }
  prep->prepared.commands = source_commands;
  prep->prepared.program = prep->program;
  prep->prepared.q2_console = prep->q2_console;
  prep->prepared.options = prep->has_options ? &prep->options : NULL;
  return &prep->prepared;
}

/* ---- 1395-1423 refreshApplicationTools ---- */
static void input_modcmd(void *ctx, qa_command_invocation *command) {
  qa_application *app = ctx;
  qa_application_mod_command(app, command, app->world_simulation);
}
static const char *const modcmd_examples[] = { "modcmd addon/component echo ready" };
static const qa_command_documentation modcmd_documentation = {
  "Run a command for one enabled component.", "modcmd PRODUCT/COMPONENT_ID <command>", modcmd_examples, 1, NULL, 0 };
typedef struct mod_input_release { qa_command_buffer *commands; bool installed; qa_application *app; } mod_input_release;
static void release_mod_input(void *ctx) {
  mod_input_release *release = ctx;
  if (release->installed) (void)qa_command_buffer_unregister(release->commands, "modcmd", input_modcmd, release->app, true);
  free(release);
}
static qa_content_mounted *tools_mounts(void *ctx) {
  qa_application *app = ctx;
  return (qa_content_mounted *)qa_app_loaded_content_mounts(app->loaded_content);
}
static char *tools_output_root(void *ctx) {
  qa_application *app = ctx;
  return checked(qa_app_application_capture_root(app->launch_options.user_content_root));
}
static double tools_milliseconds(void *ctx) { return ((qa_application *)ctx)->elapsed; }
static qa_cvar_registry *tools_cvars(void *ctx) { return qa_application_source_cvars(ctx, NULL); }
static qa_app_diagnostic_frame tools_frame(void *ctx) {
  qa_application *app = ctx;
  qa_app_diagnostic_frame frame;
  frame.frame = app->frames;
  frame.milliseconds = app->elapsed;
  frame.map = app->launch_options.map;
  frame.renderer = "headless";
  if (app->graphical != NULL) {
    qa_app_renderer_diagnostics diagnostics = qa_app_native_renderer_diagnostics(app->graphical->renderer);
    frame.renderer = diagnostics.backend;
    qa_app_renderer_diagnostics_free(&diagnostics);
  }
  size_t count = 0;
  (void)qa_app_shared_sim_players(app->world_simulation, &count);
  frame.clients = (double)count;
  return frame;
}
static qa_app_renderer_diagnostics tools_renderer(void *ctx) {
  qa_application *app = ctx;
  if (app->graphical == NULL) qa_raise("Error", 0, 0, "Renderer is not active");
  return qa_app_native_renderer_diagnostics(app->graphical->renderer);
}
static const qa_registered_scene_material *tools_shaders(void *ctx, bool sorted, size_t *out_count) {
  qa_application *app = ctx;
  if (app->graphical == NULL) qa_raise("Error", 0, 0, "Renderer is not active");
  return qa_scene_material_registrations_snapshot(qa_app_application_assets_material_registrations(app->graphical->assets), sorted, out_count);
}
static qa_q3_pres_q3_renderer_resources *tools_resources(void *ctx, qa_command_context source) {
  qa_application *app = ctx;
  const qa_command_origin *origin = root_origin(&source);
  qa_seat_id seat;
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) seat = origin->seat;
  else if (qa_application_local_player_count(app) > 0) seat = qa_session_seat_id(qa_application_local_player(app, 0).seat);
  else return NULL;
  qa_application_q3_seat_client *client = graphical_q3(app, seat);
  return client == NULL ? NULL : qa_app_application_q3_client_resource_diagnostics(client->client);
}
static void tools_print(void *ctx, const char *text, const qa_command_context *source) {
  qa_application *app = ctx;
  if (source != NULL && app->graphical != NULL) { qa_app_application_input_print(app->graphical->input, text, source); return; }
  qa_application_print(app, text);
  size_t count = app->graphical == NULL ? 0 : qa_app_application_input_local_count(app->graphical->input);
  for (size_t i = 0; i < count; i++)
    qa_seat_console_print(qa_app_application_input_local(app->graphical->input, i)->console, text);
}
void qa_application_refresh_application_tools(qa_application *app) {
  qa_command_buffer *commands = app->source_commands;
  if (commands == NULL) return;
  qa_command_buffer *input_commands = app->graphical == NULL ? NULL : qa_app_application_input_commands(app->graphical->input);
  if (input_commands != app->mod_input_commands) {
    if (app->release_mod_input_commands.fn != NULL) app->release_mod_input_commands.fn(app->release_mod_input_commands.ctx);
    app->mod_input_commands = input_commands;
    mod_input_release *release = checked(calloc(1, sizeof *release));
    release->commands = input_commands;
    release->app = app;
    release->installed = input_commands != NULL
        && qa_command_buffer_register(input_commands, "modcmd", input_modcmd, app, &modcmd_documentation, QA_COMMAND_WORLD);
    app->release_mod_input_commands.fn = release_mod_input;
    app->release_mod_input_commands.ctx = release;
  }
  if (app->tools == NULL) {
    qa_app_application_tool_services services;
    services.ctx = app;
    services.mounts = tools_mounts;
    services.output_root = tools_output_root;
    services.milliseconds = tools_milliseconds;
    services.cvars = tools_cvars;
    services.frame = tools_frame;
    services.renderer = tools_renderer;
    services.shaders = tools_shaders;
    services.resources = tools_resources;
    services.print = tools_print;
    app->tools = checked(qa_app_application_tools_open(commands, services));
  }
  qa_command_buffer *bound[2] = { commands, NULL };
  size_t count = 1;
  if (app->graphical != NULL && (app->source_published || app->ownership.borrowed_client == NULL))
    bound[count++] = qa_app_application_input_commands(app->graphical->input);
  qa_app_application_tools_bind(app->tools, bound, count);
}
