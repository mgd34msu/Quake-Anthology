/* Port of src/app/bootstrap/remote-application.ts: the constructor (TS
 * 380-701), recordedSource (703-723) and the static openers openDemoBorrowed,
 * openGtvBorrowed, open, openBorrowed and openSource (767-1070).
 *
 * The application is the ctx of every remote presentation it builds: its first
 * member is the qa_app_remote_access the remote_access.h accessors read. Async
 * members are synchronous; a TS throw is qa_raise, a TS catch is qa_try.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/q1_demo.h"
#include "qa/app/bootstrap/network/q2_demo.h"
#include "qa/app/bootstrap/network/q3_demo.h"
#include "qa/app/bootstrap/server_map.h"
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/products.h"
#include "qa/content/mounts.h"
#include "qa/content/roots.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q1/checksum.h"
#include "qa/network/q3/admission.h"
#include "qa/settings/config.h"

static void *xcalloc(size_t count, size_t size) {
  void *memory = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (memory == NULL) qa_range_error("Out of memory");
  return memory;
}
static char *xdup(const char *text) {
  char *copy = strdup(text != NULL ? text : "");
  if (copy == NULL) qa_range_error("Out of memory");
  return copy;
}
static const char *keep_text(qa_remote_application *app, char *text) {
  if (text == NULL) qa_range_error("Out of memory");
  free(app->returned_text);
  app->returned_text = text;
  return text;
}
static bool rerelease_kind(const char *kind) {
  return kind != NULL && (strcmp(kind, "q2-rerelease") == 0 || strcmp(kind, "q2-kex") == 0 || strcmp(kind, "q2-kex-demo") == 0);
}
static bool network_is(const qa_app_options *options, const char *kind) {
  return options->network_kind != NULL && strcmp(options->network_kind, kind) == 0;
}

/* ---- source handlers (TS sourceHandlers / sourceDocumentation) --------- */
typedef struct source_handler_ctx { qa_remote_application *app; char *name; bool with_source; } source_handler_ctx;
static void source_handler(void *ctx, qa_command_invocation *invocation) {
  source_handler_ctx *handler = ctx;
  /* TS invocation => this.queueCommand(name, invocation.args, null[, invocation.source]) */
  qa_remote_application_queue_command(handler->app, handler->name, (const char *const *)invocation->argv + 1,
      invocation->argc > 0 ? invocation->argc - 1 : 0, NULL, handler->with_source ? &invocation->source : NULL);
}
static void release_source_handler(void *object) {
  source_handler_ctx *handler = object;
  free(handler->name);
  free(handler);
}
/* TS sourceHandlers.set(name, handler) (and sourceDocumentation.set when documentation is given). */
void qa_remote_application_set_source_handler(qa_remote_application *app, const char *name, bool with_source,
    const qa_command_documentation *documentation) {
  source_handler_ctx *ctx = xcalloc(1, sizeof *ctx);
  ctx->app = app;
  ctx->name = xdup(name);
  ctx->with_source = with_source;
  qa_remote_arena_add(&app->arena, release_source_handler, ctx);
  qa_remote_source_handler *entry = NULL;
  for (size_t i = 0; i < app->source_handler_count; i++)
    if (strcmp(app->source_handlers[i].name, name) == 0) entry = &app->source_handlers[i];
  if (entry == NULL) {
    if (app->source_handler_count == app->source_handler_capacity) {
      size_t capacity = app->source_handler_capacity == 0 ? 8 : app->source_handler_capacity * 2;
      qa_remote_source_handler *grown = realloc(app->source_handlers, capacity * sizeof *grown);
      if (grown == NULL) qa_range_error("Out of memory");
      app->source_handlers = grown;
      app->source_handler_capacity = capacity;
    }
    entry = &app->source_handlers[app->source_handler_count++];
    memset(entry, 0, sizeof *entry);
    entry->name = xdup(name);
  }
  entry->handler = source_handler;
  entry->handler_ctx = ctx;
  if (documentation != NULL) {
    entry->has_documentation = true;
    entry->documentation = *documentation;
  }
}

/* ---- the constructor's closures ---------------------------------------- */
static qa_cvar_registry *owner_cvars(qa_remote_application *app) {
  return app->client_command_owner != NULL ? app->client_command_owner->cvars : NULL;
}
static double c_presentation_time(void *ctx) {
  qa_remote_application *app = ctx;
  return qa_app_presentation_time_milliseconds(&app->presentation_time);
}
static void c_publish(void *ctx, const qa_simulation_output *output) { qa_remote_application_publish_remote(ctx, output); }
static void c_print(void *ctx, const char *text) { qa_remote_application_print(ctx, text, NULL); }
static void c_send_command(void *ctx, const char *text) { qa_remote_application_send_command(ctx, text); }
/* TS disconnected: reason => { this.print(`${reason}\n`); this.disconnectSource(); } */
static void c_disconnected_print(void *ctx, const char *reason) {
  qa_remote_application *app = ctx;
  size_t length = strlen(reason) + 2;
  char *line = xcalloc(length, 1);
  snprintf(line, length, "%s\n", reason);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, line);
  qa_remote_application_print(app, line, NULL);
  qa_cleanup_pop(&guard);
  free(line);
  qa_remote_application_disconnect_source(app);
}
/* TS disconnected: () => this.disconnectSource() (Q2, Q3). */
static void c_disconnected(void *ctx, const char *reason) {
  (void)reason; /* TS Q2/Q3 disconnected ignores the reason */
  qa_remote_application_disconnect_source(ctx);
}
static const char *c_userinfo(void *ctx) {
  /* TS this.clientCommands?.cvars.infoString(CvarFlag.UserInfo) ?? "" */
  qa_remote_application *app = ctx;
  qa_cvar_registry *cvars = owner_cvars(app);
  return keep_text(app, cvars == NULL ? xdup("") : qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_USER_INFO, -1));
}
static qa_app_loaded_content *remember(qa_remote_application *app, qa_app_loaded_content *content) {
  app->access.loaded = content;
  return content;
}

/* unified */
typedef struct unified_load_state {
  qa_remote_application *app;
  const qa_app_unified_control *offer;
  qa_app_unified_operation_fn assert_current; void *assert_ctx;
  qa_app_loaded_content *content;
} unified_load_state;
static void unified_assert(void *ctx) {
  unified_load_state *state = ctx;
  state->assert_current(state->assert_ctx);
}
static const char *unified_assert_text(void *ctx) {
  unified_load_state *state = ctx;
  state->assert_current(state->assert_ctx);
  return NULL;
}
static void unified_adopt_body(void *ctx) {
  unified_load_state *state = ctx;
  qa_remote_application_adopt_unified_profile(state->app, state->content, state->offer->mode, unified_assert_text, state);
}
static void unified_world_body(void *ctx) {
  unified_load_state *state = ctx;
  const char *map = qa_app_loaded_content_recipe(state->content)->map.geometry.requested_path;
  qa_app_q1_remote_world_info world = {map, NULL, 0, NULL, 0};
  state->content = qa_remote_application_load_server_world(state->app, &world, NULL, 0, state->content, false);
}
static void close_loaded(qa_app_loaded_content *content) {
  qa_app_loaded_content_close(content);
  qa_app_loaded_content_destroy(content);
}
static void *u_load_content(void *ctx, const qa_app_unified_control *offer, qa_app_unified_operation_fn assert_current,
    void *assert_ctx) {
  qa_remote_application *app = ctx;
  unified_load_state state = {app, offer, assert_current, assert_ctx, NULL};
  /* TS loadUnifiedContent({ ...this.options, mode: offer.mode }, offer.composition, this.mountedContent.catalog) */
  state.content = qa_remote_application_load_unified_content(app, offer);
  qa_caught_error caught;
  if (!qa_try(unified_assert, &state, &caught) || !qa_try(unified_adopt_body, &state, &caught)) {
    close_loaded(state.content);
    qa_rethrow(&caught);
  }
  qa_app_loaded_content *content = state.content;
  if (!qa_try(unified_world_body, &state, &caught)) {
    if (app->loaded_content != content) close_loaded(content);
    qa_rethrow(&caught);
  }
  return remember(app, state.content);
}
static qa_decoded_model u_model(void *ctx, const qa_app_unified_resource_key *key, bool has_brush, double brush) {
  qa_remote_application *app = ctx;
  if (has_brush) {
    qa_decoded_model model;
    memset(&model, 0, sizeof model);
    model.kind = QA_DECODED_MODEL_BRUSH_MODEL;
    model.brush_world = qa_app_remote_decoded_world(qa_remote_application_content(app));
    model.brush_model_index = brush;
    return model;
  }
  if (app->frontend == NULL) qa_raise("Error", 0, 0, "Unified model has no published content");
  return qa_app_application_assets_model(app->frontend->assets, key->content, key->path).model;
}
static void u_send_command(void *ctx, const char *name, const char *const *args, size_t count) {
  qa_remote_application *app = ctx;
  const qa_remote_live_network *network = qa_remote_application_network(app);
  if (network != NULL && network->family == QA_APP_REMOTE_FAMILY_UNIFIED)
    qa_app_unified_client_network_player_command(network->unified, name, args, count);
}
static void u_send_component_command(void *ctx, qa_presentation_owner owner, double generation,
    const char *const *args, size_t count) {
  qa_remote_application *app = ctx;
  const qa_remote_live_network *network = qa_remote_application_network(app);
  if (network == NULL || network->family != QA_APP_REMOTE_FAMILY_UNIFIED)
    qa_raise("Error", 0, 0, "Component has no unified channel");
  qa_app_unified_client_network_component_command(network->unified, owner, generation, args, count);
}

/* QW */
static double qw_hightrack(void *ctx) {
  qa_cvar_registry *cvars = owner_cvars(ctx);
  return cvars == NULL ? 0 : qa_cvar_registry_variable_value(cvars, "cl_hightrack");
}
static double qw_chasecam(void *ctx) {
  qa_cvar_registry *cvars = owner_cvars(ctx);
  return cvars == NULL ? 0 : qa_cvar_registry_variable_value(cvars, "cl_chasecam");
}
static bool qw_skin_read(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_length) {
  qa_remote_application *app = ctx;
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(qa_remote_application_mounts(app), path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes;
  *out_length = opened.byte_length;
  return true;
}
static double qw_skin_noskins(void *ctx) {
  qa_cvar_registry *cvars = owner_cvars(ctx);
  return cvars == NULL ? 0 : qa_cvar_registry_variable_value(cvars, "noskins");
}
static const char *qw_skin_baseskin(void *ctx) {
  qa_remote_application *app = ctx;
  qa_cvar_registry *cvars = owner_cvars(app);
  return keep_text(app, cvars == NULL ? xdup("base") : qa_cvar_registry_variable_string(cvars, "baseskin"));
}
static const char *qw_skin_allskins(void *ctx) {
  qa_remote_application *app = ctx;
  return app->qw_all_skins != NULL ? app->qw_all_skins : "";
}
static qa_app_qw_download_receiver *qw_require(qa_remote_application *app) {
  if (app->qw_downloads == NULL) qa_raise("Error", 0, 0, "QW download before serverdata");
  return app->qw_downloads;
}
static qa_app_qw_download_request_result qw_request(void *ctx, const char *path, qa_app_qw_download_category category) {
  return qa_app_qw_download_receiver_request(qw_require(ctx), path, category);
}
static qa_app_qw_download_receive_result qw_receive(void *ctx, const void *result) {
  return qa_app_qw_download_receiver_receive(qw_require(ctx), *(const qa_app_qw_download_result *)result);
}
static void qw_close(void *ctx) {
  /* TS close: () => { this.remoteContentGeneration++; this.qwDownloads?.close(); this.qwDownloads = null; } */
  qa_remote_application *app = ctx;
  app->remote_content_generation++;
  qa_remote_application_close_qw_downloads(app);
}
static void qw_prepare_server_data(void *ctx, const qa_quakeworld_message *data) {
  qa_remote_application_prepare_qw_downloads(ctx, data->game_directory != NULL ? data->game_directory : "");
}
static void *qw_load_content(void *ctx, qa_app_q1_remote_world_info world, qa_app_q1_remote_assert_fn assert_current,
    void *assert_ctx) {
  (void)assert_current; (void)assert_ctx; /* TS loadContent: world => this.loadServerWorld(world, undefined, true) */
  qa_remote_application *app = ctx;
  return remember(app, qa_remote_application_load_server_world(app, &world, NULL, 0, NULL, true));
}
static double qw_map_checksum(void *ctx, qa_app_q1_remote_world_info world, const char *game_directory) {
  (void)game_directory; /* TS mapChecksum: async world => quakeWorldMapChecksum2(await this.content.mounts.read(world.map)) */
  qa_remote_application *app = ctx;
  qa_app_loaded_content *content = qa_remote_application_content(app);
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_path((qa_content_mounted *)qa_app_loaded_content_mounts(content), world.map,
      &length);
  int32_t checksum = qa_quake_world_map_checksum2(bytes, length);
  free(bytes);
  return checksum;
}
static const char *qw_userinfo(void *ctx) {
  /* TS this.clientCommands?.cvars.propagatedInfo("client-userinfo") ?? "" */
  qa_remote_application *app = ctx;
  qa_cvar_registry *cvars = owner_cvars(app);
  return keep_text(app, cvars == NULL ? xdup("") : qa_cvar_registry_propagated_info(cvars, QA_CVAR_INFO_CLIENT_USERINFO));
}

/* Q1 */
static void *q1_load_content(void *ctx, qa_app_q1_remote_world_info world, qa_app_q1_remote_assert_fn assert_current,
    void *assert_ctx) {
  (void)assert_current; (void)assert_ctx; /* TS loadContent: world => this.loadServerWorld(world) */
  qa_remote_application *app = ctx;
  return remember(app, qa_remote_application_load_server_world(app, &world, NULL, 0, NULL, false));
}

/* Q3 */
static double q3_timescale(void *ctx) {
  qa_cvar_registry *cvars = owner_cvars(ctx);
  return cvars == NULL ? 1 : qa_cvar_registry_variable_value(cvars, "timescale");
}
static double q3_time_nudge(void *ctx) {
  qa_cvar_registry *cvars = owner_cvars(ctx);
  if (cvars == NULL) return 0;
  qa_cvar_read read;
  if (!qa_cvar_registry_get(cvars, "cl_timeNudge", &read)) return 0;
  double value = read.integer_value;
  qa_cvar_read_free(&read);
  return value;
}
static void *q3_load_content(void *ctx, const char *map, const char *const *models, size_t model_count,
    const char *const *sounds, size_t sound_count, qa_q3_client_connection *connection) {
  qa_remote_application *app = ctx;
  qa_app_q1_remote_world_info world = {map, models, model_count, sounds, sound_count};
  return remember(app, qa_remote_application_load_q3_server_world(app, &world, connection));
}
static void q3_initialize(void *ctx, qa_q3_client_connection *connection) { qa_remote_application_bind_seat(ctx, connection); }
static bool q3_prepare(void *ctx, qa_q3_client_connection *connection) {
  return qa_remote_application_prepare_q3_downloads(ctx, connection);
}
static qa_app_q3_client_downloads *q3_require(qa_remote_application *app) {
  if (app->q3_downloads == NULL) qa_raise("Error", 0, 0, "Q3 download has no content owner");
  return app->q3_downloads;
}
static double q3_publish_size(void *ctx, double size) {
  return qa_app_q3_client_downloads_publish_size(q3_require(ctx), size);
}
static void q3_receive(void *ctx, const qa_q3_download_block *block) {
  qa_app_q3_client_downloads_receive(q3_require(ctx), block);
}
static void q3_downloads_close(void *ctx) { qa_remote_application_close_q3_downloads(ctx); }
static void q3_shutdown(void *ctx) {
  qa_remote_application *app = ctx;
  qa_app_world_seat_presentation *presentation = qa_remote_application_presentation(app);
  if (presentation == NULL) return;
  /* this.uiPreferences = { ...presentation.ui.preferences.values } */
  app->has_ui_preferences = true;
  app->ui_preferences = qa_ui_seat_preferences_values(qa_app_seat_ui_preferences(qa_app_world_seat_presentation_ui(presentation)));
  qa_app_application_q3_client *client = qa_remote_presentation_q3_client(presentation);
  if (client != NULL) qa_app_application_q3_client_shutdown(client);
  qa_remote_application_clear_source_presentation(app);
  app->client_input_count = 0;
}

/* Q2 */
static void q2_cinematic_start(void *ctx, const char *name, qa_app_q2_cinematic_ended_fn ended, void *ended_ctx) {
  qa_remote_application_start_cinematic(ctx, name, false, false, false, ended, ended_ctx);
}
static void q2_cinematic_stop(void *ctx) { qa_remote_application_stop_cinematic(ctx); }
typedef struct q2_select_state {
  qa_app_q2_remote_assert_fn assert_current; void *assert_ctx;
} q2_select_state;
static const char *q2_assert_text(void *ctx) {
  q2_select_state *state = ctx;
  state->assert_current(state->assert_ctx);
  return NULL;
}
static void selection_release(void *selection) { qa_remote_content_selection_free(selection); }
static const qa_app_q2_remote_mounts *q2_prepare_server_data(void *ctx, const void *data_value,
    qa_app_q2_remote_assert_fn assert_current, void *assert_ctx) {
  qa_remote_application *app = ctx;
  const qa_q2_server_data_result *data = data_value;
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  bool rerelease = rerelease_kind(qa_app_q2_remote_protocol(remote->q2).kind);
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make(rerelease ? "q2-rerelease-baseq2" : "q2-classic-baseq2", data->gamedir, &selection))
    qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, selection_release, &selection);
  q2_select_state state = {assert_current, assert_ctx};
  qa_app_remote_content_mounts *content = qa_remote_application_select_remote_content(app, &selection, q2_assert_text, &state);
  qa_cleanup_pop(&guard);
  qa_remote_content_selection_free(&selection);
  return qa_app_remote_access_q2_mounts(&app->access, content);
}
static void *q2_load_content(void *ctx, const qa_app_q2_game_state *state) {
  qa_remote_application *app = ctx;
  return remember(app, qa_remote_application_load_q2_server_world(app, state));
}
static const qa_app_q2_remote_mounts *q2_refresh_downloads(void *ctx, qa_app_q2_remote_assert_fn assert_current,
    void *assert_ctx) {
  qa_remote_application *app = ctx;
  q2_select_state state = {assert_current, assert_ctx};
  qa_app_remote_content_mounts *content = qa_remote_application_refresh_remote_content(app, q2_assert_text, &state);
  return qa_app_remote_access_q2_mounts(&app->access, content);
}
static const char *q2_userinfo(void *ctx) {
  /* TS userinfo: () => cvars.infoString(CvarFlag.UserInfo), over the constructor's configuration.source */
  qa_remote_application *app = ctx;
  return keep_text(app, qa_cvar_registry_info_string(app->configuration->source, QA_CVAR_FLAG_USER_INFO, -1));
}

static void fill_common(qa_remote_application *app, void **ctx, qa_app_remote_world_host *world_host,
    qa_session_id *session, qa_client_id *client) {
  *ctx = app;
  *world_host = qa_app_remote_access_world_host();
  *session = app->access.session;
  *client = qa_session_client_id(qa_session_seat_client(app->configuration->seat));
}

/* ---- recordedSource (TS 703-723) ---------------------------------------- */
typedef struct recorded_complete { qa_remote_application *app; } recorded_complete;
static void recorded_completed(void *ctx, const char *reason, const qa_app_demo_timing *timing) {
  qa_remote_application *app = ctx;
  const qa_remote_launch *launch = &app->launch_storage;
  if (timing != NULL) {
    char *text = qa_app_demo_timing_text(*timing);
    if (text == NULL) qa_range_error("Out of memory");
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, text);
    qa_remote_application_print(app, text, NULL);
    qa_cleanup_pop(&guard);
    free(text);
  }
  if (strcmp(reason, "truncated") == 0) {
    size_t length = strlen(launch->resource->path) + 32;
    char *line = xcalloc(length, 1);
    snprintf(line, length, "Demo %s is truncated.\n", launch->resource->path);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, line);
    qa_remote_application_print(app, line, NULL);
    qa_cleanup_pop(&guard);
    free(line);
  }
  launch->complete(launch->complete_ctx, reason);
}

qa_remote_source qa_remote_application_recorded_source(qa_remote_application *app, const qa_remote_launch *launch) {
  const qa_remote_presentation *remote = &app->initial_remote;
  if (remote->family == QA_APP_REMOTE_FAMILY_Q2) {
    qa_remote_application_set_source_handler(app, "follow", false, NULL);
    qa_remote_application_set_source_handler(app, "chase", false, NULL);
  }
  qa_remote_source source;
  memset(&source, 0, sizeof source);
  if (launch->kind == QA_REMOTE_SOURCE_GTV) {
    if (remote->family != QA_APP_REMOTE_FAMILY_Q2) qa_raise("Error", 0, 0, "GTV requires the Q2 recorded presentation");
    qa_app_gtv_remote_source_bind(launch->watch, qa_app_q2_remote_as_client_host(remote->q2),
        qa_remote_q2_mvd_presentation(remote->q2));
    source.kind = QA_REMOTE_SOURCE_GTV;
    source.watch = launch->watch;
    source.label = xdup(launch->label);
    return source;
  }
  if (app->client_command_owner == NULL) qa_raise("Error", 0, 0, "Recording requires a command owner");
  qa_remote_recorded_inputs *inputs = xcalloc(1, sizeof *inputs);
  qa_remote_arena_add(&app->arena, qa_remote_recorded_inputs_release, inputs);
  qa_app_recorded_demo_resource resource;
  resource.kind = launch->resource->kind == QA_APP_DEMO_Q1 ? QA_APP_REC_Q1 : launch->resource->kind == QA_APP_DEMO_QW
      ? QA_APP_REC_QW : launch->resource->kind == QA_APP_DEMO_Q2 ? QA_APP_REC_Q2 : QA_APP_REC_Q3;
  resource.bytes = launch->resource->bytes;
  resource.length = launch->resource->byte_count;
  resource.path = launch->resource->path;
  qa_app_recorded_playback playback = qa_remote_application_recorded_playback(app, &resource, inputs);
  source.kind = QA_REMOTE_SOURCE_RECORDED;
  source.playback = qa_app_recorded_source_create(resource, playback.family, playback, launch->timedemo,
      app->client_command_owner->cvars, recorded_completed, app);
  if (source.playback == NULL) qa_range_error("Out of memory");
  return source;
}

/* ---- the constructor (TS 380-701) --------------------------------------- */
typedef struct construct_state {
  qa_remote_application *app;
  const qa_remote_launch *launch;
} construct_state;

static void construct_unified(qa_remote_application *app, const qa_remote_launch *launch) {
  qa_app_unified_remote_options o;
  memset(&o, 0, sizeof o);
  fill_common(app, &o.ctx, &o.world_host, &o.session, &o.client);
  o.seat = qa_remote_application_seat_id(app);
  o.next_generation = qa_app_remote_access_next_generation;
  o.identity_actor = qa_app_remote_access_identity_actor;
  o.identity_client = qa_app_remote_access_identity_client;
  o.owned_actor = qa_app_remote_access_owned_actor;
  o.load_content = u_load_content;
  o.prepared_mods = qa_app_remote_access_prepared_mods;
  o.mod_has_presentation = qa_app_remote_access_mod_has_presentation;
  o.recipe = qa_app_remote_access_content_recipe;
  o.map_entities_provider = qa_app_remote_access_map_entities_provider;
  o.resolve_resource = qa_app_remote_access_resolve_unified_resource;
  o.model = u_model;
  o.world_resource = qa_app_remote_access_world_resource;
  o.world_geometry = qa_app_remote_access_content_world;
  o.publish = c_publish;
  o.send_command = u_send_command;
  o.has_mod_files = true; /* TS modFiles: new ModUserFiles(this.options.userContentRoot ?? defaultUserContentRoot()) */
  o.send_component_command = u_send_component_command;
  o.scene_link = qa_app_remote_access_scene_link;
  o.scene_unlink = qa_app_remote_access_scene_unlink;
  o.scene_actor_is_model = qa_app_remote_access_scene_actor_is_model;
  o.disconnected = c_disconnected_print;
  o.print = c_print;
  app->initial_remote.family = QA_APP_REMOTE_FAMILY_UNIFIED;
  app->initial_remote.unified = qa_app_unified_remote_create(o);
  if (app->initial_remote.unified == NULL) qa_range_error("Out of memory");
  app->initial_source.kind = QA_REMOTE_SOURCE_LIVE;
  app->initial_source.transport = launch->transport;
  qa_app_unified_client_options n;
  memset(&n, 0, sizeof n);
  n.transport = qa_remote_application_datagram(app, launch->transport);
  n.remote = launch->address;
  n.host = app->initial_remote.unified;
  n.userinfo_ctx = app;
  n.userinfo = c_userinfo;
  app->initial_source.network.family = QA_APP_REMOTE_FAMILY_UNIFIED;
  app->initial_source.network.unified = qa_app_unified_client_network_create(n);
  if (app->initial_source.network.unified == NULL) qa_range_error("Out of memory");
}

static void construct_qw(qa_remote_application *app, const qa_remote_launch *launch) {
  qa_app_qw_remote_options o;
  memset(&o, 0, sizeof o);
  fill_common(app, &o.shared.ctx, &o.shared.world_host, &o.shared.session, &o.shared.client);
  o.shared.presentation_time = c_presentation_time;
  o.shared.next_generation = qa_app_remote_access_next_generation;
  o.shared.identity_actor = qa_app_remote_access_identity_actor;
  o.shared.load_content = qw_load_content;
  o.shared.content_recipe = qa_app_remote_access_content_recipe;
  o.shared.content_world = qa_app_remote_access_content_world;
  o.shared.send_command = c_send_command;
  o.shared.print = c_print;
  o.shared.publish = c_publish;
  o.shared.disconnected = c_disconnected_print;
  o.seat = qa_remote_application_seat_id(app);
  o.has_camera_options = true;
  o.camera_options = (qa_app_qw_camera_options){app, qw_hightrack, qw_chasecam};
  o.skin_options = (qa_app_qw_skin_options){app, qw_skin_read, qw_skin_noskins, qw_skin_baseskin, qw_skin_allskins};
  o.has_downloads = true;
  o.downloads = (qa_app_qw_downloads){app, qw_request, qw_receive, qw_close};
  o.camera_trace = qa_app_remote_access_qw_camera_trace;
  o.prepare_server_data = qw_prepare_server_data;
  o.map_checksum = qw_map_checksum;
  o.resolve_sound = qa_app_remote_access_resolve_sound;
  o.owned_actor = qa_app_remote_access_owned_actor;
  o.scene_link = qa_app_remote_access_scene_link;
  o.scene_unlink = qa_app_remote_access_scene_unlink;
  o.scene_model_bounds = qa_app_remote_access_scene_model_bounds;
  o.now_milliseconds = qa_app_remote_access_now_milliseconds;
  app->initial_remote.family = QA_APP_REMOTE_FAMILY_QW;
  app->initial_remote.qw = qa_app_qw_remote_create(o);
  if (app->initial_remote.qw == NULL) qa_range_error("Out of memory");
  if (launch->kind != QA_REMOTE_SOURCE_LIVE) {
    app->initial_source = qa_remote_application_recorded_source(app, launch);
    return;
  }
  qa_net_address address = qa_remote_require_qw_address(launch->address);
  app->initial_source.kind = QA_REMOTE_SOURCE_LIVE;
  app->initial_source.transport = launch->transport;
  qa_app_qw_client_network_options n;
  memset(&n, 0, sizeof n);
  n.transport = &qa_app_application_transport_udp_socket(launch->transport)->transport;
  n.remote = address;
  n.host = qa_app_qw_remote_as_client_host(app->initial_remote.qw);
  app->has_initial_qport = true;
  app->initial_qport = qa_remote_application_allocate_qport(app);
  n.qport = app->initial_qport;
  n.userinfo_ctx = app;
  n.userinfo = qw_userinfo;
  app->initial_source.network.family = QA_APP_REMOTE_FAMILY_QW;
  app->initial_source.network.qw = qa_app_qw_client_network_create(n);
  if (app->initial_source.network.qw == NULL) qa_range_error("Out of memory");
}

static void construct_q1(qa_remote_application *app, const qa_remote_launch *launch) {
  qa_app_q1_remote_options o;
  memset(&o, 0, sizeof o);
  fill_common(app, &o.ctx, &o.world_host, &o.session, &o.client);
  o.presentation_time = c_presentation_time;
  o.next_generation = qa_app_remote_access_next_generation;
  o.identity_actor = qa_app_remote_access_identity_actor;
  o.load_content = q1_load_content;
  o.content_recipe = qa_app_remote_access_content_recipe;
  o.content_world = qa_app_remote_access_content_world;
  o.send_command = c_send_command;
  o.print = c_print;
  o.publish = c_publish;
  o.disconnected = c_disconnected_print;
  app->initial_remote.family = QA_APP_REMOTE_FAMILY_Q1;
  app->initial_remote.q1 = qa_app_q1_remote_create(o);
  if (app->initial_remote.q1 == NULL) qa_range_error("Out of memory");
  if (launch->kind != QA_REMOTE_SOURCE_LIVE) {
    app->initial_source = qa_remote_application_recorded_source(app, launch);
    return;
  }
  /* TS seat: { name: cvars.variableString("name"), color: cvars.get("color")?.integerValue ?? 0, ... } */
  qa_cvar_registry *cvars = app->configuration->source;
  char *name = qa_cvar_registry_variable_string(cvars, "name");
  if (name == NULL) qa_range_error("Out of memory");
  free(app->q1_seat_name);
  app->q1_seat_name = name;
  double color_value = 0;
  qa_cvar_read color;
  if (qa_cvar_registry_get(cvars, "color", &color)) {
    color_value = color.integer_value;
    qa_cvar_read_free(&color);
  }
  app->initial_source.kind = QA_REMOTE_SOURCE_LIVE;
  app->initial_source.transport = launch->transport;
  qa_app_q1_client_network_options n;
  memset(&n, 0, sizeof n);
  n.transport = *qa_remote_application_datagram(app, launch->transport);
  n.remote = launch->address;
  n.host = qa_app_q1_remote_as_client_host(app->initial_remote.q1);
  n.seat = (qa_netquake_seat_identity){app->q1_seat_name, color_value, "", false, 0};
  app->initial_source.network.family = QA_APP_REMOTE_FAMILY_Q1;
  app->initial_source.network.q1 = qa_app_q1_client_network_create(n);
  if (app->initial_source.network.q1 == NULL) qa_range_error("Out of memory");
}

static void construct_q3(qa_remote_application *app, const qa_remote_launch *launch) {
  qa_app_q3_remote_options o;
  memset(&o, 0, sizeof o);
  fill_common(app, &o.ctx, &o.world_host, &o.session, &o.client);
  o.next_generation = qa_app_remote_access_next_generation;
  o.identity_actor = qa_app_remote_access_identity_actor;
  o.owned_actor = qa_app_remote_access_owned_actor;
  o.userinfo = c_userinfo;
  o.time_nudge = q3_time_nudge;
  o.timescale = q3_timescale;
  o.presentation_time = c_presentation_time;
  o.load_content = q3_load_content;
  o.content_recipe = qa_app_remote_access_content_recipe;
  o.content_world = qa_app_remote_access_content_world;
  o.has_downloads = true;
  o.downloads = (qa_app_q3_remote_downloads){app, q3_prepare, q3_publish_size, q3_receive, q3_downloads_close};
  o.shutdown = q3_shutdown;
  o.initialize = q3_initialize;
  o.send_command = c_send_command;
  o.disconnected = c_disconnected;
  o.publish = c_publish;
  o.print = c_print;
  app->initial_remote.family = QA_APP_REMOTE_FAMILY_Q3;
  app->initial_remote.q3 = qa_app_q3_remote_create(o);
  if (app->initial_remote.q3 == NULL) qa_range_error("Out of memory");
  if (app->client_command_owner == NULL) qa_raise("Error", 0, 0, "Q3 remote requires its engine cvar owner");
  qa_collision_map_settings_init(&app->collision, app->client_command_owner->cvars);
  qa_app_q3_remote_bind_collision_settings(app->initial_remote.q3, &app->collision);
  if (launch->kind != QA_REMOTE_SOURCE_LIVE) {
    app->initial_source = qa_remote_application_recorded_source(app, launch);
    return;
  }
  if (launch->address.kind != QA_NET_IPV4 && launch->address.kind != QA_NET_IPX)
    qa_raise("Error", 0, 0, "Native Q3 remote requires IPv4");
  app->initial_source.kind = QA_REMOTE_SOURCE_LIVE;
  app->initial_source.transport = launch->transport;
  qa_app_q3_client_network_options n;
  memset(&n, 0, sizeof n);
  n.transport = *qa_remote_application_datagram(app, launch->transport);
  if (!qa_q3_address_from_net(&launch->address, &n.remote)) qa_raise("Error", 0, 0, "Native Q3 remote requires IPv4");
  n.host = qa_app_q3_remote_as_client_host(app->initial_remote.q3);
  n.cvars = app->client_command_owner->cvars;
  n.has_authorization = true;
  n.authorization = qa_remote_application_q3_authorization(app);
  app->has_initial_qport = true;
  app->initial_qport = qa_remote_application_allocate_qport(app);
  n.qport = app->initial_qport;
  app->initial_source.network.family = QA_APP_REMOTE_FAMILY_Q3;
  app->initial_source.network.q3 = qa_app_q3_client_network_create(n);
  if (app->initial_source.network.q3 == NULL) qa_range_error("Out of memory");
}

typedef struct q2_header_guard { qa_app_q2_playback_header header; bool has; } q2_header_guard;
static void release_q2_header(void *object) {
  q2_header_guard *guard = object;
  if (guard->has) qa_app_q2_playback_header_free(&guard->header);
  free(guard);
}
static void construct_q2(qa_remote_application *app, const qa_remote_launch *launch) {
  if (app->download_permission == NULL) qa_raise("Error", 0, 0, "Q2 remote client has no download policy");
  /* TS recording: the recorded header, the GTV MVD header, or null */
  q2_header_guard *recording = xcalloc(1, sizeof *recording);
  qa_remote_arena_add(&app->arena, release_q2_header, recording);
  qa_q2_protocol_identity protocol;
  bool mvd = false;
  qa_q2_mvd_profile profile;
  memset(&profile, 0, sizeof profile);
  if (launch->kind == QA_REMOTE_SOURCE_RECORDED) {
    qa_app_q2_read_playback_header(launch->resource->bytes, launch->resource->byte_count, &recording->header);
    recording->has = true;
    mvd = recording->header.is_mvd;
    protocol = mvd ? recording->header.protocol : recording->header.dm2.protocol;
    if (mvd) profile = recording->header.profile.profile;
  } else if (launch->kind == QA_REMOTE_SOURCE_GTV) {
    const qa_q2_mvd_header *header = qa_app_gtv_remote_source_header(launch->watch);
    mvd = true;
    protocol = header->profile.protocol;
    profile = header->profile;
  } else {
    const qa_catalog_discovered_product *product = qa_content_catalog_product(
        qa_app_configuration_content_catalog(app->mounted_content), app->launch_options.product);
    bool rerelease = product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0;
    protocol = qa_app_live_q2_protocol(&app->launch_options, rerelease);
  }
  qa_app_q2_remote_options o;
  memset(&o, 0, sizeof o);
  fill_common(app, &o.ctx, &o.world_host, &o.session, &o.client);
  o.has_cinematic = true;
  o.cinematic = (qa_app_q2_remote_cinematic){app, q2_cinematic_start, q2_cinematic_stop};
  o.download_permission = app->download_permission;
  o.seat = qa_remote_application_seat_id(app);
  o.publish = c_publish;
  o.disconnected = c_disconnected;
  o.next_generation = qa_app_remote_access_next_generation;
  o.identity_actor = qa_app_remote_access_identity_actor;
  o.owned_actor = qa_app_remote_access_owned_actor;
  o.protocol = protocol;
  if (mvd) { o.has_recorded_profile = true; o.recorded_profile = profile; }
  o.presentation_time = c_presentation_time;
  o.userinfo = q2_userinfo;
  o.print = c_print;
  o.send_command = c_send_command;
  o.prepare_server_data = q2_prepare_server_data;
  o.load_content = q2_load_content;
  o.refresh_downloads = q2_refresh_downloads;
  o.content_recipe = qa_app_remote_access_content_recipe;
  o.content_world = qa_app_remote_access_content_world;
  o.content_read = qa_app_remote_access_content_read;
  o.rebind_download_content = qa_app_remote_access_rebind_q2_mounts;
  app->initial_remote.family = QA_APP_REMOTE_FAMILY_Q2;
  app->initial_remote.q2 = qa_app_q2_remote_create(o);
  if (app->initial_remote.q2 == NULL) qa_range_error("Out of memory");
  if (launch->kind != QA_REMOTE_SOURCE_LIVE) {
    app->initial_source = qa_remote_application_recorded_source(app, launch);
    return;
  }
  app->initial_source.kind = QA_REMOTE_SOURCE_LIVE;
  app->initial_source.transport = launch->transport;
  qa_app_q2_client_network_options n;
  memset(&n, 0, sizeof n);
  n.transport = qa_remote_application_datagram(app, launch->transport);
  n.remote = &app->launch_storage.address;
  n.host = qa_app_q2_remote_as_client_host(app->initial_remote.q2);
  app->has_initial_qport = true;
  app->initial_qport = qa_remote_application_allocate_qport(app);
  n.qport = app->initial_qport;
  app->initial_source.network.family = QA_APP_REMOTE_FAMILY_Q2;
  app->initial_source.network.q2 = qa_app_q2_client_network_create(n);
  if (app->initial_source.network.q2 == NULL) qa_range_error("Out of memory");
}

static void construct_body(void *ctx) {
  construct_state *state = ctx;
  qa_remote_application *app = state->app;
  const qa_remote_launch *launch = state->launch;
  qa_remote_configuration *configuration = app->configuration;
  /* TS family = this.family === "q3" ? "q3" : "qw" ? "qw" : "q1" ? "nq" : "q2" */
  if (app->family == QA_APP_DEMO_QW) {
    qa_remote_application_set_source_handler(app, "skins", false, NULL);
    qa_remote_application_set_source_handler(app, "allskins", false, NULL);
    qa_remote_application_set_source_handler(app, "color", true, NULL);
  }
  qa_remote_command_owner *owner = &app->client_command_owner_storage;
  memset(owner, 0, sizeof *owner);
  owner->cvars = configuration->source;
  owner->commands = qa_app_prepared_startup_commands(configuration->prepared);
  owner->scripts = qa_app_prepared_profile_scripts(configuration->profile);
  owner->routing = configuration->routing;
  owner->movement = network_is(&app->launch_options, "unified-client")
      ? qa_app_prepared_profile_movement(configuration->profile) : configuration->source;
  owner->fallback = configuration->source;
  app->client_command_owner = owner;
  app->client_config = app->input_config;
  if (launch->kind == QA_REMOTE_SOURCE_LIVE && launch->unified) construct_unified(app, launch);
  else if (app->family == QA_APP_DEMO_QW) construct_qw(app, launch);
  else if (app->family == QA_APP_DEMO_Q1) construct_q1(app, launch);
  else if (app->family == QA_APP_DEMO_Q3) construct_q3(app, launch);
  else construct_q2(app, launch);
  if (app->client_command_owner != NULL) {
    qa_mouse_settings *input_settings = qa_remote_profile_seat_mouse(configuration->profile, qa_remote_application_seat_id(app));
    if (input_settings == NULL) qa_raise("Error", 0, 0, "Remote configuration has no primary input owner");
    app->client_command_owner->input_settings = input_settings;
  }
  if (!app->ownership.borrowed)
    app->source_connection = qa_session_client_connect(qa_session_seat_client(configuration->seat), "remote");
}

/* Frees what a failed constructor built (TS: the object is dropped). */
static void construct_release(qa_remote_application *app) {
  qa_app_remote_seat_network_destroy(&app->initial_source.network);
  if (app->initial_source.kind == QA_REMOTE_SOURCE_RECORDED && app->initial_source.playback != NULL)
    qa_app_recorded_source_destroy(app->initial_source.playback);
  free(app->initial_source.label);
  qa_app_remote_seat_presentation_destroy(&app->initial_remote);
  qa_remote_application_free_storage(app);
}

qa_remote_application *qa_remote_application_construct(qa_app_options launch_options,
    qa_app_configuration_content *mounted_content, qa_engine_session *session, qa_app_native_renderer *renderer,
    const qa_remote_application_host *host, qa_app_image_settings *image_settings, const qa_remote_launch *launch,
    const qa_identity_owner *identity, qa_remote_browser browser, qa_remote_ownership ownership,
    qa_remote_configuration *configuration, qa_app_keys *keys, qa_app_key_profile *key_profile) {
  qa_remote_application *app = xcalloc(1, sizeof *app);
  qa_app_remote_access_init(&app->access, identity);
  app->access.session = qa_engine_session_id(session);
  app->launch_options = launch_options;
  app->mounted_content = mounted_content;
  app->session = session;
  app->renderer = renderer;
  app->host = *host;
  app->image_settings = image_settings;
  app->identity = identity;
  app->browser = browser;
  app->ownership = ownership;
  app->configuration = configuration;
  app->keys = keys;
  app->launch_storage = *launch;
  app->launch_storage.label = launch->label != NULL ? xdup(launch->label) : NULL;
  if (launch->kind == QA_REMOTE_SOURCE_LIVE && !qa_net_address_clone(&launch->address, &app->launch_storage.address))
    qa_range_error("Out of memory");
  /* field initializers (TS 216-312) */
  app->view_settings = qa_app_view_settings_create(qa_remote_application_view_fov_changed, app);
  if (app->view_settings == NULL) qa_range_error("Out of memory");
  app->retired_frontends = xcalloc(1, sizeof *app->retired_frontends);
  if (!qa_event_ring_init(app->retired_frontends, 1024, sizeof(qa_remote_retired_frontend))) qa_range_error("Out of memory");
  qa_app_presentation_time_init(&app->presentation_time);
  app->qw_all_skins = xdup("");
  qa_remote_application_init_lifetime(app);
  /* constructor body */
  app->key_profile = key_profile;
  if (ownership.borrowed) app->music_controls = ownership.client->music_controls;
  else {
    qa_music_controls_init(&app->owned_music_controls);
    app->music_controls = &app->owned_music_controls;
  }
  app->family = launch->kind == QA_REMOTE_SOURCE_LIVE ? launch->family
      : launch->kind == QA_REMOTE_SOURCE_GTV ? QA_APP_DEMO_Q2 : launch->resource->kind;
  app->initial_seat_id = qa_session_seat_id(configuration->seat);
  app->input_config = configuration->settings;
  app->socks_settings = configuration->socks;
  app->download_permission = configuration->download_permission;
  construct_state state = {app, &app->launch_storage};
  qa_caught_error caught;
  if (!qa_try(construct_body, &state, &caught)) {
    construct_release(app);
    qa_rethrow(&caught);
  }
  return app;
}

/* ---- the static openers (TS 767-821) ------------------------------------ */
qa_remote_application *qa_remote_application_open_demo_borrowed(qa_app_client_bootstrap *client,
    const qa_app_options *options, const qa_remote_application_host *host,
    const qa_app_demo_resource *resource, bool timedemo, qa_remote_demo_complete_fn complete, void *complete_ctx) {
  qa_remote_ownership ownership = {true, client};
  qa_remote_launch recording;
  memset(&recording, 0, sizeof recording);
  recording.kind = QA_REMOTE_SOURCE_RECORDED;
  recording.resource = resource;
  recording.timedemo = timedemo;
  recording.complete = complete;
  recording.complete_ctx = complete_ctx;
  return qa_remote_application_open_source(ownership, options, host, &recording);
}

typedef struct gtv_open_state {
  qa_app_client_bootstrap *client;
  const qa_app_options *options;
  const qa_remote_application_host *host;
  qa_app_gtv_source_options request;
  qa_app_gtv_remote_source *watch;
  qa_app_options selected;
  char *label;
  qa_remote_application *result;
} gtv_open_state;
static void throw_if_aborted(const bool *aborted) {
  if (aborted != NULL && *aborted) qa_raise("AbortError", 0, 0, "The operation was aborted.");
}
static void gtv_open_body(void *ctx) {
  gtv_open_state *s = ctx;
  throw_if_aborted(s->request.aborted);
  const qa_q2_mvd_header *header = qa_app_gtv_remote_source_header(s->watch);
  if (strcmp(header->profile.protocol.kind, "q2-kex-demo") == 0)
    qa_raise("Error", 0, 0, "GTV MVD cannot use the KEX single-view demo protocol");
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make(header->profile.rerelease ? "q2-rerelease-baseq2" : "q2-classic-baseq2",
          header->data.gamedir, &selection))
    qa_range_error("Out of memory");
  /* const { movementProduct, quakeCProgram, q1Protocol, botSkill, ...retained } = options */
  qa_remote_options_copy(s->options, &s->selected);
  free(s->selected.movement_product); s->selected.movement_product = NULL;
  free(s->selected.quakec_program); s->selected.quakec_program = NULL;
  s->selected.has_q1_protocol = false; memset(&s->selected.q1_protocol, 0, sizeof s->selected.q1_protocol);
  s->selected.has_bot_skill = false; s->selected.bot_skill = 0;
  s->selected.network_kind = "offline";
  free(s->selected.network_host); s->selected.network_host = NULL;
  free(s->selected.network_remote); s->selected.network_remote = NULL;
  s->selected.network_port = 0;
  s->selected.seats = 1;
  s->selected.dedicated = false;
  s->selected.rules = "standard";
  s->selected.movement = QA_GAME_FAMILY_Q2;
  s->selected.character = QA_GAME_FAMILY_Q2;
  char *model = xdup(s->options->character == QA_GAME_FAMILY_Q2 ? s->options->character_model : "male");
  free(s->selected.character_model); s->selected.character_model = model;
  free(s->selected.product);
  s->selected.product = qa_remote_content_product(&selection, NULL, 0);
  if (s->selected.product == NULL) qa_range_error("Out of memory");
  if (s->selected.has_remote_content) qa_remote_content_selection_free(&s->selected.remote_content);
  s->selected.has_remote_content = true;
  s->selected.remote_content = selection;
  s->selected.has_q2_protocol = true;
  s->selected.q2_protocol = header->profile.protocol;
  /* label: request.host + ":" + request.port */
  char port[40];
  qa_net_js_number(s->request.port, port, sizeof port);
  size_t length = strlen(s->request.host) + strlen(port) + 2;
  s->label = xcalloc(length, 1);
  snprintf(s->label, length, "%s:%s", s->request.host, port);
  qa_remote_launch launch;
  memset(&launch, 0, sizeof launch);
  launch.kind = QA_REMOTE_SOURCE_GTV;
  launch.watch = s->watch;
  launch.label = s->label;
  launch.aborted = s->request.aborted;
  qa_remote_ownership ownership = {true, s->client};
  s->result = qa_remote_application_open_source(ownership, &s->selected, s->host, &launch);
}
qa_remote_application *qa_remote_application_open_gtv_borrowed(qa_app_client_bootstrap *client,
    const qa_app_options *options, const qa_remote_application_host *host, qa_app_gtv_source_options request) {
  gtv_open_state s;
  memset(&s, 0, sizeof s);
  s.client = client;
  s.options = options;
  s.host = host;
  s.request = request;
  s.watch = qa_app_gtv_remote_source_prepare(request);
  if (s.watch == NULL) qa_range_error("Out of memory");
  qa_caught_error caught;
  bool opened = qa_try(gtv_open_body, &s, &caught);
  qa_app_options_destroy(&s.selected);
  free(s.label);
  if (!opened) {
    qa_app_gtv_remote_source_close(s.watch);
    qa_app_gtv_remote_source_destroy(s.watch);
    qa_rethrow(&caught);
  }
  return s.result;
}
qa_remote_application *qa_remote_application_open(const qa_app_options *options, const qa_remote_application_host *host) {
  qa_remote_ownership ownership = {false, NULL};
  return qa_remote_application_open_source(ownership, options, host, NULL);
}
qa_remote_application *qa_remote_application_open_borrowed(qa_app_client_bootstrap *client,
    const qa_app_options *options, const qa_remote_application_host *host) {
  qa_remote_ownership ownership = {true, client};
  return qa_remote_application_open_source(ownership, options, host, NULL);
}

/* ---- openSource (TS 823-1070) ------------------------------------------- */
typedef struct open_state {
  qa_remote_ownership ownership;
  qa_app_options options;              /* owned; handed to the application */
  const qa_remote_application_host *host;
  const qa_remote_launch *recording;   /* NULL = TS undefined */
  bool unified, qw, q1, q3;
  qa_game_family family;
  bool has_address; qa_net_address address;
  qa_app_configuration_content *content;
  qa_identity_owner *identity; bool owns_identity;
  qa_engine_session *session;
  qa_app_native_renderer *renderer;
  qa_app_application_transport *transport;
  qa_remote_application *application;
  qa_app_image_settings *image_settings;
  bool has_browser; qa_remote_browser browser;
  qa_config_store *browser_settings;
  qa_remote_configuration *configuration;
  qa_remote_arena arena;               /* configuration arena until the application adopts it */
  qa_app_keys *keys; bool owns_keys;
  qa_app_key_profile *key_profile;
  qa_config_store audio_settings; bool has_audio_settings;
  char *label;
} open_state;

static void open_print(void *ctx, const char *text) {
  open_state *s = ctx;
  if (s->application == NULL) {
    if (s->host->base.print != NULL) s->host->base.print(s->host->base.ctx, text);
  } else qa_remote_application_print(s->application, text, NULL);
}
static int open_cheats(void *ctx) {
  /* TS () => application?.remote instanceof Q3RemotePresentation && q3InfoValue(sourceRecords[1] ?? "", "sv_cheats") === "1" */
  open_state *s = ctx;
  if (s->application == NULL) return 0;
  const qa_remote_presentation *remote = qa_remote_application_remote(s->application);
  if (remote->family != QA_APP_REMOTE_FAMILY_Q3) return 0;
  size_t count = 0;
  const char *const *records = qa_app_q3_remote_source_records(remote->q3, &count);
  char *value = qa_q3_info_value(count > 1 && records[1] != NULL ? records[1] : "", "sv_cheats");
  if (value == NULL) qa_range_error("Out of memory");
  int cheats = strcmp(value, "1") == 0;
  free(value);
  return cheats;
}
static void key_print(void *ctx, const char *text) {
  const qa_remote_application_host *host = ctx;
  if (host->base.print != NULL) host->base.print(host->base.ctx, text);
}

/* ApplicationVideoRestart host over the owned application (TS 1009-1033). */
static qa_app_capture *video_capture(void *ctx) { return ((qa_remote_application *)ctx)->capture; }
static bool video_render_worker(void *ctx, bool *out) {
  qa_remote_application *app = ctx;
  *out = qa_cvar_registry_variable_value(qa_app_image_settings_cvars(app->image_settings), "r_smp") != 0;
  return true;
}
static bool video_prepare(void *ctx, qa_app_prepared_video_presentation *out) {
  return qa_remote_application_prepare_video_restart(ctx, out);
}
static void video_publish_window(void *ctx, qa_sdl_window *window) {
  qa_remote_application *app = ctx;
  if (app->controls != NULL) qa_app_application_input_publish_window(app->controls, qa_app_input_window_from_sdl(window));
}
static void video_published(void *ctx, const char *kind) {
  /* current.launchOptions = { ...current.launchOptions, renderer } */
  qa_remote_application *app = ctx;
  app->launch_options.renderer = strcmp(kind, "gl") == 0 ? "gl" : "cpu";
}
static void video_settled(void *ctx) { (void)ctx; /* TS settled: () => {} */ }
static void video_print(void *ctx, const char *text, const qa_command_context *source) {
  qa_remote_application_print(ctx, text, source);
}
static void video_close_body(void *ctx) { qa_remote_application_close(ctx); }
static void video_failed(void *ctx, const qa_caught_error *error) {
  qa_caught_error cleanup;
  if (!qa_try(video_close_body, ctx, &cleanup))
    qa_raise("AggregateError", 0, 0, "Remote video restart and shutdown failed");
  qa_rethrow(error);
}

static void open_body(void *ctx) {
  open_state *s = ctx;
  const qa_remote_application_host *host = s->host;
  qa_app_options *options = &s->options;
  const qa_content_catalog *catalog = qa_app_configuration_content_catalog(s->content);
  /* options = resolveApplicationMovement(content.catalog, options) */
  qa_app_options resolved = qa_app_resolve_application_movement(catalog, options);
  if (resolved.movement_product != options->movement_product) {
    char *movement = resolved.movement_product != NULL ? xdup(resolved.movement_product) : NULL;
    free(options->movement_product);
    options->movement_product = movement;
  }
  options->movement = resolved.movement;
  if (options->dedicated || options->seats != (double)(int64_t)options->seats || options->seats < 1 || options->seats > 4
      || (!s->unified && (options->movement != s->family || options->character != s->family))) {
    const char *name = s->family == QA_GAME_FAMILY_Q1 ? "q1" : s->family == QA_GAME_FAMILY_Q3 ? "q3" : "q2";
    qa_raise("Error", 0, 0, "Native %s remote play requires one to four graphical seats with matching movement and character providers",
        name);
  }
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, options->product);
  if (!s->unified) {
    bool mismatch = product->expectation.family != s->family
        || (product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0
            && s->family != QA_GAME_FAMILY_Q2);
    if (!mismatch && s->recording == NULL && s->q1 && strcmp(options->product, "q1-classic-id1") != 0) {
      bool qw_match = false;
      if (s->qw) {
        qa_remote_content_selection fallback;
        const qa_remote_content_selection *selection = &options->remote_content;
        bool made = false;
        if (!options->has_remote_content) {
          if (!qa_remote_content_selection_make("q1-quakeworld", "qw", &fallback)) qa_range_error("Out of memory");
          selection = &fallback;
          made = true;
        }
        char *expected = qa_remote_content_product(selection, NULL, 0);
        if (made) qa_remote_content_selection_free(&fallback);
        if (expected == NULL) qa_range_error("Out of memory");
        qw_match = strcmp(options->product, expected) == 0;
        free(expected);
      }
      if (!qw_match) mismatch = true;
    }
    if (!mismatch && s->q3) {
      qa_remote_content_selection fallback;
      const qa_remote_content_selection *selection = &options->remote_content;
      bool made = false;
      if (!options->has_remote_content) {
        if (!qa_remote_content_selection_make("q3-baseq3", "baseq3", &fallback)) qa_range_error("Out of memory");
        selection = &fallback;
        made = true;
      }
      char *expected = qa_remote_content_product(selection, NULL, 0);
      if (made) qa_remote_content_selection_free(&fallback);
      if (expected == NULL) qa_range_error("Out of memory");
      bool match = strcmp(options->product, expected) == 0;
      free(expected);
      if (!match) mismatch = true;
    }
    if (mismatch) qa_raise("Error", 0, 0, "Remote application requires content matching the selected native protocol");
  }
  const qa_catalog_discovered_product *audio_product = qa_content_catalog_require(catalog, s->qw ? "q1-quakeworld" : options->product);
  char *audio_root = audio_product->user_content != NULL ? xdup(audio_product->user_content->root)
      : qa_user_product_directory(options->user_content_root != NULL ? options->user_content_root : qa_default_user_content_root(),
            audio_product->expectation.content_directory);
  if (audio_root == NULL) qa_range_error("Out of memory");
  if (!qa_config_store_init(&s->audio_settings, audio_root)) { free(audio_root); qa_range_error("Out of memory"); }
  free(audio_root);
  s->has_audio_settings = true;
  if (s->ownership.borrowed) s->image_settings = s->ownership.client->image_settings;
  else {
    qa_app_audio_preferences audio;
    memset(&audio, 0, sizeof audio);
    bool has_audio = qa_app_load_audio_settings(&s->audio_settings, &audio);
    qa_app_image_settings_options image;
    memset(&image, 0, sizeof image);
    image.has_audio_format = true;
    image.audio_output_format = has_audio ? audio.output_format : qa_default_audio_output_format;
    if (has_audio) { free(audio.menu_track); free(audio.device_name); }
    image.context.session = qa_engine_session_id(s->session);
    image.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_CONSOLE;
    bool rerelease = product->expectation.edition != NULL && strcmp(product->expectation.edition, "rerelease") == 0;
    image.dialect = s->qw ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD : s->q1 ? QA_COMMAND_DIALECT_Q1_NETQUAKE
        : s->q3 ? QA_COMMAND_DIALECT_Q3 : rerelease ? QA_COMMAND_DIALECT_Q2_RERELEASE : QA_COMMAND_DIALECT_Q2_CLASSIC;
    image.has_gamma = true; image.gamma = options->gamma;
    if (options->has_render_worker) { image.has_render_worker = true; image.render_worker = options->render_worker; }
    image.has_display_width = options->has_display_width; image.display_width = options->display_width;
    image.has_display_height = options->has_display_height; image.display_height = options->display_height;
    image.has_display_gamma = options->has_display_gamma; image.display_gamma = options->display_gamma;
    image.user_content_root = options->user_content_root;
    image.print = open_print; image.print_ctx = s;
    s->image_settings = qa_app_image_settings_open(image);
    if (s->image_settings == NULL) qa_range_error("Out of memory");
  }
  if (s->ownership.borrowed) s->renderer = s->ownership.client->renderer;
  else {
    static const char renderer_identity = 0; /* TS Symbol("remote application renderer") */
    qa_renderer_resource_owner owner = {&renderer_identity, qa_engine_session_id(s->session), 0};
    bool worker = qa_cvar_registry_variable_value(qa_app_image_settings_cvars(s->image_settings), "r_smp") != 0;
    s->renderer = qa_app_native_renderer_open((qa_app_native_renderer_host){0}, options->renderer, options->width,
        options->height, options->hidden, options->gamma, worker, owner, NULL);
    if (s->renderer == NULL) qa_range_error("Out of memory");
    qa_app_image_settings_refresh_display(s->image_settings, s->renderer);
  }
  if (s->has_address) {
    qa_app_open_transport_options transport;
    memset(&transport, 0, sizeof transport);
    transport.selection = qa_remote_options_transport_selection(options);
    transport.family = s->qw ? QA_APP_FAMILY_QW : s->q1 ? QA_APP_FAMILY_Q1 : s->q3 ? QA_APP_FAMILY_Q3 : QA_APP_FAMILY_Q2;
    transport.host = s->address.kind == QA_NET_IPV6 ? "::" : "0.0.0.0";
    transport.port = 0;
    transport.limits = s->unified || s->q1 || s->q3 ? QA_UNIFIED_DATAGRAM_LIMITS_NET : QA_Q2_DATAGRAM_LIMITS_NET;
    s->transport = qa_app_open_application_transport(&transport, NULL);
    if (s->transport == NULL) qa_range_error("Out of memory");
  }
  if (host->server_browser == NULL) {
    char *root = host->base.save_directory != NULL ? qa_remote_join3(host->base.save_directory, "..", "settings")
        : qa_user_data_directory("settings", options->user_content_root);
    if (root == NULL) qa_range_error("Out of memory");
    s->browser_settings = xcalloc(1, sizeof *s->browser_settings);
    bool stored = qa_config_store_init(s->browser_settings, root);
    free(root);
    if (!stored) qa_range_error("Out of memory");
    s->browser.owned = true;
    s->browser.browser = qa_app_startup_server_browser_open(s->browser_settings);
    if (s->browser.browser == NULL) qa_range_error("Out of memory");
  } else {
    s->browser.owned = false;
    s->browser.browser = host->server_browser;
  }
  s->has_browser = true;
  qa_remote_launch launch;
  if (s->recording != NULL) launch = *s->recording;
  else {
    if (!s->has_address || s->transport == NULL) qa_raise("Error", 0, 0, "Remote transport missing");
    memset(&launch, 0, sizeof launch);
    launch.kind = QA_REMOTE_SOURCE_LIVE;
    launch.unified = s->unified;
    launch.family = s->qw ? QA_APP_DEMO_QW : s->q1 ? QA_APP_DEMO_Q1 : s->q3 ? QA_APP_DEMO_Q3 : QA_APP_DEMO_Q2;
    launch.address = s->address;
    launch.transport = s->transport;
  }
  s->configuration = qa_remote_prepare_configuration(options, s->content, s->session, s->image_settings, s->ownership,
      &host->base, open_cheats, s, NULL, NULL, &s->arena);
  if (s->ownership.borrowed) s->keys = s->ownership.client->keys;
  else {
    s->keys = qa_app_keys_create(key_print, (void *)host);
    if (s->keys == NULL) qa_range_error("Out of memory");
    s->owns_keys = true;
  }
  s->key_profile = qa_app_keys_prepare(s->keys, qa_app_prepared_profile_options(s->configuration->profile), catalog,
      s->configuration->source);
  if (s->recording != NULL && s->recording->kind == QA_REMOTE_SOURCE_GTV) throw_if_aborted(s->recording->aborted);
  qa_app_options launch_options = s->options;
  memset(&s->options, 0, sizeof s->options); /* handed to the application */
  qa_remote_application *application = qa_remote_application_construct(launch_options, s->content, s->session,
      s->renderer, host, s->image_settings, &launch, s->identity, s->browser, s->ownership, s->configuration, s->keys,
      s->key_profile);
  s->application = application;
  s->key_profile = NULL;
  /* the application owns what openSource opened from here (its closeOwned releases them) */
  /* the configuration's objects are older than the constructor's: they release last (newest first) */
  qa_remote_arena merged = s->arena;
  memset(&s->arena, 0, sizeof s->arena);
  for (size_t i = 0; i < application->arena.count; i++)
    qa_remote_arena_add(&merged, application->arena.entries[i].release, application->arena.entries[i].object);
  free(application->arena.entries);
  application->arena = merged;
  application->owned_mounted = s->content;
  if (s->owns_identity) application->owned_identity = s->identity;
  if (s->owns_keys) application->owned_keys = s->keys;
  application->browser_settings = s->browser_settings;
  s->browser_settings = NULL;
  qa_remote_application_initialize_q3_browser(application);
  qa_remote_application_prepare_remote_seats(application, application->configuration);
  if (!s->ownership.borrowed) {
    qa_remote_application_activate_source_commands(application);
    qa_app_video_restart_host video;
    memset(&video, 0, sizeof video);
    video.ctx = application;
    video.capture = video_capture;
    video.render_worker = video_render_worker;
    video.prepare = video_prepare;
    video.publish_window = video_publish_window;
    video.published = video_published;
    video.settled = video_settled;
    video.print = video_print;
    video.failed = video_failed;
    qa_app_video_restart *restart = qa_app_video_restart_create(application->renderer, video);
    if (restart == NULL) qa_range_error("Out of memory");
    if (application->client_command_owner == NULL) {
      qa_app_video_restart_destroy(restart);
      qa_raise("Error", 0, 0, "Remote video restart requires client commands");
    }
    qa_app_video_restart_register(restart, application->client_command_owner->commands);
    application->owned_video_restart = restart;
    qa_remote_application_publish_configuration(application, application->configuration);
    for (size_t i = 0; i < application->remote_seat_count; i++) {
      qa_session_connection *previous = qa_app_remote_seat_channel_publish_connection(application->remote_seats[i]->channel);
      if (previous != NULL) qa_session_connection_close(previous);
    }
    qa_app_view_settings_load(application->view_settings, application->input_config);
    qa_app_view_settings_bind_cvars(application->view_settings, qa_app_image_settings_cvars(application->image_settings));
    application->release_view_cvars = true;
  }
  if (s->transport != NULL)
    qa_app_client_socks_settings_connect(application->socks_settings, qa_remote_application_socks_connector(s->transport), NULL);
  if (s->recording != NULL && s->recording->kind == QA_REMOTE_SOURCE_GTV) throw_if_aborted(s->recording->aborted);
  if (s->ownership.borrowed)
    qa_remote_application_publish_connecting(application, s->ownership.client,
        s->recording != NULL && s->recording->kind == QA_REMOTE_SOURCE_GTV ? s->recording->aborted : NULL);
  char *line = NULL;
  if (s->recording == NULL && s->has_address) {
    char *key = qa_net_address_key(&s->address, true);
    if (key == NULL) qa_range_error("Out of memory");
    const char *name = s->qw ? "QuakeWorld" : s->q1 ? "Quake" : s->q3 ? "Quake III" : "Quake II";
    size_t length = strlen(key) + strlen(name) + 32;
    line = xcalloc(length, 1);
    snprintf(line, length, "Connecting to %s server %s.\n", name, key);
    free(key);
  } else if (s->recording != NULL && s->recording->kind == QA_REMOTE_SOURCE_GTV) {
    size_t length = strlen(s->recording->label) + 32;
    line = xcalloc(length, 1);
    snprintf(line, length, "Watching GTV %s.\n", s->recording->label);
  } else {
    const char *path = s->recording != NULL ? s->recording->resource->path : "undefined";
    size_t length = strlen(path) + 32;
    line = xcalloc(length, 1);
    snprintf(line, length, "Playing %s.\n", path);
  }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, line);
  if (host->base.print != NULL) host->base.print(host->base.ctx, line);
  qa_cleanup_pop(&guard);
  free(line);
}

/* TS catch: the application exists. */
static void open_close_application(void *ctx) { qa_remote_application_close(ctx); }
/* TS catch: no application; the resources close in TS order. */
static void close_transport(void *ctx) {
  open_state *s = ctx;
  if (s->transport != NULL) qa_app_application_transport_close(s->transport);
}
static void close_scripts(void *ctx) {
  open_state *s = ctx;
  if (s->configuration != NULL) qa_app_console_script_files_close(qa_app_prepared_profile_scripts(s->configuration->profile));
}
static void close_images(void *ctx) {
  open_state *s = ctx;
  if (s->configuration != NULL) qa_app_image_settings_close(s->configuration->images.settings);
}
static void discard_identities(void *ctx) {
  open_state *s = ctx;
  if (s->configuration != NULL) qa_app_remote_seat_identities_discard(s->configuration->identities);
}
static void close_renderer(void *ctx) {
  open_state *s = ctx;
  if (!s->ownership.borrowed && s->renderer != NULL) qa_app_native_renderer_close(s->renderer);
}
static void close_session(void *ctx) {
  open_state *s = ctx;
  if (!s->ownership.borrowed) qa_engine_session_close(s->session);
}
static void close_image_settings(void *ctx) {
  open_state *s = ctx;
  if (!s->ownership.borrowed && s->image_settings != NULL) qa_app_image_settings_close(s->image_settings);
}
static void close_content(void *ctx) {
  open_state *s = ctx;
  qa_app_configuration_content_close(s->content);
}
static void close_browser(void *ctx) {
  open_state *s = ctx;
  if (s->has_browser && s->browser.owned) qa_app_startup_server_browser_close(s->browser.browser);
}

static void open_free(open_state *s) {
  qa_app_options_destroy(&s->options);
  if (s->has_address) qa_net_address_destroy(&s->address);
  if (s->has_audio_settings) qa_config_store_destroy(&s->audio_settings);
}

/* A raise before the TS try block releases what openSource holds so far. */
static void release_open_prefix(void *ctx) {
  open_state *s = ctx;
  if (s->content != NULL) qa_app_configuration_content_destroy(s->content);
  if (s->owns_identity) { qa_identity_owner_destroy(s->identity); free(s->identity); }
  open_free(s);
  free(s);
}

qa_remote_application *qa_remote_application_open_source(qa_remote_ownership ownership, const qa_app_options *options_in,
    const qa_remote_application_host *host, const qa_remote_launch *recording) {
  open_state *s = xcalloc(1, sizeof *s);
  s->ownership = ownership;
  s->host = host;
  s->recording = recording;
  if (recording != NULL && host->server_browser == NULL) {
    free(s);
    qa_raise("Error", 0, 0, "Recorded playback requires the retained server browser");
  }
  if (recording != NULL && recording->kind == QA_REMOTE_SOURCE_RECORDED)
    qa_remote_application_recorded_options(options_in, recording->resource, &s->options);
  else qa_remote_options_copy(options_in, &s->options);
  qa_app_options *options = &s->options;
  qa_cleanup options_guard;
  qa_cleanup_push(&options_guard, release_open_prefix, s);
  if (recording == NULL && network_is(options, "q2-client")) {
    const char *base = options->has_remote_content ? options->remote_content.base : options->product;
    bool rerelease = false;
    for (size_t i = 0; i < qa_expected_product_count; i++)
      if (base != NULL && strcmp(qa_expected_products[i].id, base) == 0)
        rerelease = qa_expected_products[i].edition != NULL && strcmp(qa_expected_products[i].edition, "rerelease") == 0;
    qa_q2_protocol_identity protocol = qa_app_live_q2_protocol(options, rerelease);
    if (!options->has_remote_content) {
      bool kex = strcmp(protocol.kind, "q2-kex") == 0 || strcmp(protocol.kind, "q2-rerelease") == 0;
      if (!qa_remote_content_selection_make(kex ? "q2-rerelease-baseq2" : "q2-classic-baseq2", "baseq2", &options->remote_content))
        qa_range_error("Out of memory");
      options->has_remote_content = true;
    }
    options->has_q2_protocol = true;
    options->q2_protocol = protocol;
    char *product = qa_remote_content_product(&options->remote_content, NULL, 0);
    if (product == NULL) qa_range_error("Out of memory");
    free(options->product);
    options->product = product;
  }
  s->unified = recording == NULL && network_is(options, "unified-client");
  /* selected: "q2" for GTV, the recording's kind, or the unified product's family */
  bool has_selected = false;
  qa_app_demo_family selected = QA_APP_DEMO_Q2;
  if (recording != NULL && recording->kind == QA_REMOTE_SOURCE_GTV) { has_selected = true; selected = QA_APP_DEMO_Q2; }
  else if (recording != NULL) { has_selected = true; selected = recording->resource->kind; }
  else if (s->unified) {
    for (size_t i = 0; i < qa_expected_product_count; i++)
      if (strcmp(qa_expected_products[i].id, options->product) == 0) {
        has_selected = true;
        qa_game_family family = qa_expected_products[i].family;
        selected = family == QA_GAME_FAMILY_Q1 ? QA_APP_DEMO_Q1 : family == QA_GAME_FAMILY_Q3 ? QA_APP_DEMO_Q3 : QA_APP_DEMO_Q2;
      }
  }
  s->qw = !has_selected ? network_is(options, "qw-client") : selected == QA_APP_DEMO_QW;
  s->q1 = !has_selected ? network_is(options, "q1-client") || s->qw : selected == QA_APP_DEMO_Q1 || s->qw;
  s->q3 = !has_selected ? network_is(options, "q3-client") : selected == QA_APP_DEMO_Q3;
  if (!s->unified && !has_selected && !s->q1 && !s->q3 && !network_is(options, "q2-client"))
    qa_raise("Error", 0, 0, "RemoteApplication requires a native connect address");
  s->family = s->q1 ? QA_GAME_FAMILY_Q1 : s->q3 ? QA_GAME_FAMILY_Q3 : QA_GAME_FAMILY_Q2;
  if (!s->unified && !s->q1 && !s->q3 && strcmp(options->character_model, "male") != 0
      && strcmp(options->character_model, "female") != 0 && strcmp(options->character_model, "cyborg") != 0)
    qa_raise("Error", 0, 0, "Remote Q2 character selection requires an installed male, female or cyborg player appearance");
  if (recording == NULL && (network_is(options, "qw-client") || network_is(options, "q1-client")
      || network_is(options, "q2-client") || network_is(options, "q3-client") || network_is(options, "unified-client"))) {
    double port = s->unified ? 27960 : s->qw ? 27500 : s->q1 ? 26000 : s->q3 ? 27960
        : options->has_q2_protocol && strcmp(options->q2_protocol.kind, "q2-kex") == 0 ? 5069 : 27910;
    s->address = qa_app_resolve_application_address(options->network_remote, port, qa_remote_options_transport_selection(options),
        s->qw ? QA_APP_FAMILY_QW : s->q1 ? QA_APP_FAMILY_Q1 : s->q3 ? QA_APP_FAMILY_Q3 : QA_APP_FAMILY_Q2);
    s->has_address = true;
  }
  if (recording == NULL && !s->has_address) qa_raise("Error", 0, 0, "Missing remote address");
  s->content = qa_app_open_remote_application_content_native(options);
  if (s->content == NULL) qa_range_error("Out of memory");
  qa_q3_application_product q3_product;
  if (qa_app_configuration_content_q3_value(s->content, &q3_product)) {
    options->has_q3_product = true;
    options->q3_product = q3_product;
  }
  if (ownership.borrowed) s->identity = ownership.client->identity;
  else {
    char *key = NULL;
    if (s->has_address) key = qa_net_address_key(&s->address, true);
    else key = xdup(recording->kind == QA_REMOTE_SOURCE_GTV ? recording->label : recording->resource->path);
    if (key == NULL) qa_range_error("Out of memory");
    size_t length = strlen(key) + 16;
    /* The owner borrows its name (session.name): the text lives in the same block, after the owner. */
    s->identity = xcalloc(1, sizeof *s->identity + length);
    char *name = (char *)(s->identity + 1);
    snprintf(name, length, "quake:remote:%s", key);
    free(key);
    bool created = qa_identity_owner_create(s->identity, name);
    if (!created) qa_range_error("Out of memory");
    s->owns_identity = true;
  }
  if (ownership.borrowed) s->session = ownership.client->session;
  else {
    s->session = qa_engine_session_create(s->identity, (qa_session_mode){QA_SESSION_MODE_LOCAL});
    if (s->session == NULL) qa_range_error("Out of memory");
  }
  qa_cleanup_pop(&options_guard);
  qa_caught_error caught;
  if (qa_try(open_body, s, &caught)) {
    qa_remote_application *result = s->application;
    open_free(s);
    free(s);
    return result;
  }
  bool aggregate = false;
  qa_caught_error cleanup;
  if (s->application != NULL) {
    qa_remote_application *application = s->application;
    if (!qa_try(open_close_application, application, &cleanup)) aggregate = true;
    bool publication = application->publication_started;
    open_free(s);
    free(s);
    qa_remote_application_destroy(application); /* TS GC of the failed application */
    if (publication) qa_raise("ClientSourcePublicationError", 0, 0, "%s", qa_app_client_source_publication_error);
    if (aggregate) qa_raise("AggregateError", 0, 0, "Remote preparation and cleanup failed");
    qa_rethrow(&caught);
  }
  void (*closes[])(void *) = {close_transport, close_scripts, close_images, discard_identities, close_renderer, close_session,
      close_image_settings, close_content, close_browser};
  for (size_t i = 0; i < sizeof closes / sizeof closes[0]; i++)
    if (!qa_try(closes[i], s, &cleanup)) aggregate = true;
  /* the C objects TS leaves to GC */
  if (s->key_profile != NULL) qa_app_key_profile_destroy(s->key_profile);
  if (s->owns_keys && s->keys != NULL) qa_app_keys_destroy(s->keys);
  qa_remote_arena_release(&s->arena);
  if (s->transport != NULL) qa_app_application_transport_destroy(s->transport);
  if (s->has_browser && s->browser.owned && s->browser.browser != NULL) qa_app_startup_server_browser_destroy(s->browser.browser);
  if (s->browser_settings != NULL) { qa_config_store_destroy(s->browser_settings); free(s->browser_settings); }
  if (!ownership.borrowed && s->renderer != NULL) qa_app_native_renderer_destroy(s->renderer);
  if (!ownership.borrowed && s->image_settings != NULL) qa_app_image_settings_destroy(s->image_settings);
  qa_app_configuration_content_destroy(s->content);
  if (!ownership.borrowed) qa_engine_session_destroy(s->session);
  if (s->owns_identity) { qa_identity_owner_destroy(s->identity); free(s->identity); }
  open_free(s);
  free(s);
  if (aggregate) qa_raise("AggregateError", 0, 0, "Remote preparation and resource cleanup failed");
  qa_rethrow(&caught);
}
