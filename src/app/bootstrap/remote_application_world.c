/* Port of src/app/bootstrap/remote-application.ts, the world region (TS
 * 1640-2240 in the 2026-11-03 tree): prepareVideoRestart, prepareRetirement,
 * retire, saveSourceSettings, initializeQ3Browser, browserCommand,
 * loadFrontend, releaseFrontend, retireFrontends, refreshImages,
 * loadQ2ServerWorld, selectRemoteContent, refreshRemoteContent,
 * prepareQwDownloads, prepareQ3Downloads, loadQ3ServerWorld and
 * loadServerWorld. Async members are synchronous; a TS throw is qa_raise, a
 * TS catch is qa_try. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "remote_application_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/menu_art.h"
#include "qa/app/bootstrap/q3_seat_client.h"
#include "qa/app/bootstrap/server_map.h"
#include "qa/app/bootstrap/startup_selection.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include "qa/network/q3/admission.h"
#include "qa/network/q3/pak_references.h"
#include "qa/network/q3/pure.h"
#include "qa/render/scene/textures.h"

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
static void print_line(qa_remote_application *app, const char *format, const char *a, const char *b, const char *c) {
  size_t length = strlen(format) + strlen(a) + (b != NULL ? strlen(b) : 0) + (c != NULL ? strlen(c) : 0) + 1;
  char *line = xcalloc(length, 1);
  snprintf(line, length, format, a, b != NULL ? b : "", c != NULL ? c : "");
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, line);
  qa_remote_application_print(app, line, NULL);
  qa_cleanup_pop(&guard);
  free(line);
}
static char *game_state_text(qa_q3_client_connection *connection, int64_t index) {
  char *text = NULL;
  if (!qa_q3_client_game_state_storage_get(qa_q3_client_connection_game_state(connection), index, &text) || text == NULL)
    return xdup("");
  return text;
}

/* ---- prepareVideoRestart (TS 1640-1675) -------------------------------- */
typedef struct video_guest { qa_remote_application *app; qa_app_application_input *input; qa_app_world_seat_presentation *presentation;
  qa_app_application_q3_client *client; } video_guest;
typedef struct video_state { qa_remote_application *app; qa_app_application_input *input;
  qa_app_world_seat_presentation **presentations; size_t presentation_count; video_guest *guests; } video_state;
static qa_rect video_viewport(void *ctx, qa_sdl_window *window) {
  video_guest *guest = ctx;
  size_t count = qa_app_application_input_local_count(guest->input);
  qa_session_seat *seat = qa_app_world_seat_presentation_local(guest->presentation)->player.seat;
  double index = -1;
  for (size_t i = 0; i < count; i++)
    if (qa_app_application_input_local(guest->input, i)->player.seat == seat) { index = (double)i; break; }
  qa_sdl_size size = qa_sdl_window_drawable_size(window);
  return qa_app_seat_viewport(index, (double)count, size.width, size.height);
}
static void video_publish(void *ctx, qa_app_application_q3_client *candidate) {
  video_guest *guest = ctx;
  qa_app_seat_q3_client view = qa_app_application_q3_client_seat(candidate), replaced;
  (void)qa_app_world_seat_presentation_replace_q3_client(guest->presentation, &view, &replaced);
  /* this.clientInputs = this.clientInputs.filter(event => event.client !== client) */
  qa_remote_application *app = guest->app;
  size_t kept = 0;
  for (size_t i = 0; i < app->client_input_count; i++)
    if (app->client_inputs[i].client != guest->client) app->client_inputs[kept++] = app->client_inputs[i];
  app->client_input_count = kept;
}
static void video_assert(void *ctx) {
  video_state *state = ctx;
  qa_remote_application *app = state->app;
  bool changed = app->closed || app->closing || app->controls != state->input;
  for (size_t i = 0; i < state->presentation_count && !changed; i++) {
    qa_session_seat *seat = qa_app_world_seat_presentation_local(state->presentations[i])->player.seat;
    qa_seat_presentation current;
    if (!qa_session_seat_presentation(seat, &current)
        || current.ctx != qa_app_world_seat_presentation_seat(state->presentations[i]).ctx) changed = true;
  }
  if (changed) qa_raise("Error", 0, 0, "Remote guest changed during video preparation");
}
static void release_video_state(void *object) {
  video_state *state = object;
  free(state->presentations);
  free(state->guests);
  free(state);
}
bool qa_remote_application_prepare_video_restart(qa_remote_application *app, qa_app_prepared_video_presentation *out) {
  qa_app_application_input *input = app->controls;
  if (input == NULL) return false;
  /* the assertCurrent closure and the guests outlive this call (the prepared presentation keeps them) */
  video_state *state = xcalloc(1, sizeof *state);
  qa_remote_arena_add(&app->arena, release_video_state, state);
  state->app = app;
  state->input = input;
  state->presentations = xcalloc(app->remote_seat_count + 1, sizeof *state->presentations);
  qa_app_world_seat_presentation *primary = qa_remote_application_presentation(app);
  if (primary != NULL) state->presentations[state->presentation_count++] = primary;
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (peer == app->primary_peer || peer->view == NULL) continue;
    state->presentations[state->presentation_count++] = qa_app_remote_seat_view_presentation(peer->view);
  }
  state->guests = xcalloc(state->presentation_count, sizeof *state->guests);
  qa_app_video_guest_seat *seats = xcalloc(state->presentation_count, sizeof *seats);
  size_t count = 0;
  for (size_t i = 0; i < state->presentation_count; i++) {
    const qa_app_seat_q3_client *client = qa_app_world_seat_presentation_q3_client(state->presentations[i]);
    if (client == NULL || strcmp(client->kind, "qvm") != 0) continue;
    video_guest *guest = &state->guests[count];
    guest->app = app;
    guest->input = input;
    guest->presentation = state->presentations[i];
    guest->client = client->ctx;
    seats[count].client = client->ctx;
    seats[count].ctx = guest;
    seats[count].viewport = video_viewport;
    seats[count].publish = video_publish;
    count++;
  }
  if (count == 0) { free(seats); return false; }
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, seats);
  bool prepared = qa_app_prepare_video_guests(input, app->renderer, seats, count, video_assert, state, out);
  qa_cleanup_pop(&guard);
  free(seats);
  return prepared;
}

/* ---- prepareRetirement / retire / saveSourceSettings (TS 1677-1704) ---- */
void qa_remote_application_prepare_retirement(qa_remote_application *app) {
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
  qa_remote_application_save_source_settings(app);
}
void qa_remote_application_retire(qa_remote_application *app) { qa_remote_application_close(app); }

static void save_audio(const qa_config_store *store, qa_app_application_audio *audio) {
  qa_app_audio_music_preferences music = qa_app_application_audio_music_preferences(audio);
  qa_app_audio_settings_input input;
  memset(&input, 0, sizeof input);
  input.has_music_preferences = true;
  input.music_shuffle = music.music_shuffle;
  input.menu_track = music.menu_track;
  input.has_output_format = true;
  input.output_format = qa_app_application_audio_output_format(audio);
  input.selected_output = qa_app_application_audio_selected_output(audio);
  input.effects_volume = qa_app_application_audio_effects_volume(audio);
  input.music_volume = qa_app_application_audio_music_volume(audio);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, music.menu_track);
  qa_app_save_audio_settings(store, input);
  qa_cleanup_pop(&cleanup);
  free(music.menu_track);
}
/* TS saveAudioSettings(store, audio). */
void qa_remote_save_audio_settings(const qa_config_store *store, qa_app_application_audio *audio) { save_audio(store, audio); }
static void free_string_list(char **items, size_t count) {
  for (size_t i = 0; i < count; i++) free(items[i]);
  free(items);
}
void qa_remote_application_save_source_settings(qa_remote_application *app) {
  if (app->configuration_published) qa_app_keys_save(app->keys);
  if (app->controls != NULL) qa_app_application_input_save_settings(app->controls);
  if (app->frontend != NULL) {
    qa_app_view_settings_save(app->view_settings, app->input_config);
    save_audio(app->input_config, app->frontend->audio);
  }
  if (app->client_command_owner != NULL && app->client_config != NULL) {
    size_t count = 0;
    char **bindings = qa_cvar_registry_archive_commands(qa_app_client_socks_settings_cvars(app->socks_settings), NULL, NULL, &count);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, bindings);
    (void)qa_config_store_save_cvars(app->client_config, "settings/client.cfg", app->client_command_owner->cvars,
        (const char *const *)bindings, count);
    qa_cleanup_pop(&guard);
    free_string_list(bindings, count);
  }
  if (app->loaded_content != NULL) {
    const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(app->loaded_content);
    for (size_t i = 0; i < app->configuration->seat_source_count; i++) {
      char index[40];
      qa_net_js_number((double)app->configuration->seat_sources[i].id.index, index, sizeof index);
      const char *owner[4] = {"client", recipe->engine_behavior.content, recipe->engine_behavior.provider, index};
      qa_app_save_cvar_archive(app->input_config, owner, 4, app->configuration->seat_sources[i].cvars, NULL, 0);
    }
  }
}

/* ---- initializeQ3Browser (TS 1748-1777) -------------------------------- */
static double browser_now(void *ctx) { (void)ctx; return qa_app_remote_access_now_milliseconds(NULL); }
static double browser_max_ping(void *ctx) {
  qa_remote_application *app = ctx;
  return qa_cvar_registry_variable_value(app->client_command_owner->cvars, "cl_maxPing");
}
static double browser_resend(void *ctx) {
  qa_remote_application *app = ctx;
  return qa_cvar_registry_variable_value(app->client_command_owner->cvars, "cl_serverStatusResendTime");
}
static void browser_print(void *ctx, const char *text) { qa_remote_application_print(ctx, text, NULL); }
static const char *const localservers_examples[] = {"localservers"};
static const char *const globalservers_examples[] = {"globalservers 0 68"};
static const char *const ping_examples[] = {"ping localhost:27960"};
static const char *const serverstatus_examples[] = {"serverstatus", "serverstatus localhost:27960"};
void qa_remote_application_initialize_q3_browser(qa_remote_application *app) {
  if (app->family != QA_APP_DEMO_Q3) return;
  if (app->client_command_owner == NULL) qa_raise("Error", 0, 0, "Q3 browser requires the client command owner");
  app->q3_browser_host = qa_app_startup_server_browser_q3_host_create(app->browser.browser);
  if (app->q3_browser_host == NULL) qa_range_error("Out of memory");
  qa_q3_browser_view_options options;
  memset(&options, 0, sizeof options);
  options.browser = qa_app_startup_q3_browser_host_view(app->q3_browser_host);
  options.now = browser_now;
  options.max_ping = browser_max_ping;
  options.status_resend_time = browser_resend;
  options.print = browser_print;
  options.print_ctx = app;
  options.ctx = app;
  if (!qa_q3_browser_view_init(&app->q3_browser, &options)) qa_range_error("Out of memory");
  static const qa_command_documentation documentation[] = {
    {"Discover Quake III servers on the local network.", "localservers", localservers_examples, 1, NULL, 0},
    {"Request Quake III servers from the configured master.", "globalservers <master# 0-1> <protocol> [keywords]",
        globalservers_examples, 1, NULL, 0},
    {"Query a Quake III server's latency and information.", "ping <server>", ping_examples, 1, NULL, 0},
    {"Print a Quake III server's settings and players.", "serverstatus [server]", serverstatus_examples, 2, NULL, 0},
  };
  static const char *const names[] = {"localservers", "globalservers", "ping", "serverstatus"};
  for (size_t i = 0; i < 4; i++) qa_remote_application_set_source_handler(app, names[i], false, &documentation[i]);
}

/* ---- browserCommand (TS 1779-1814) ------------------------------------- */
bool qa_remote_application_browser_command(qa_remote_application *app, const char *name, const char *const *args,
    size_t arg_count, void (*print)(void *ctx, const char *text), void *print_ctx) {
  qa_q3_browser_view *browser = app->q3_browser;
  if (browser == NULL) return false;
  if (strcmp(name, "localservers") == 0) {
    print(print_ctx, "Scanning for servers on the local network...\n");
    qa_q3_browser_view_local_servers(browser);
    return true;
  }
  if (strcmp(name, "globalservers") == 0) {
    if (arg_count < 2) {
      print(print_ctx, "usage: globalservers <master# 0-1> <protocol> [keywords]\n");
      return true;
    }
    if (app->client_command_owner == NULL) qa_raise("Error", 0, 0, "Q3 browser requires client cvars");
    qa_cvar_registry *cvars = app->client_command_owner->cvars;
    const char **keywords = xcalloc(arg_count, sizeof *keywords);
    size_t keyword_count = 0;
    for (size_t i = 2; i < arg_count; i++) keywords[keyword_count++] = args[i];
    if (qa_cvar_registry_variable_value(cvars, "fs_restrict") != 0) keywords[keyword_count++] = "demo";
    print(print_ctx, "Requesting servers from the master...\n");
    char *master = qa_cvar_registry_variable_string(cvars, "sv_master1");
    if (master == NULL) { free(keywords); qa_range_error("Out of memory"); }
    qa_cleanup keyword_guard, master_guard;
    qa_cleanup_push(&keyword_guard, free, keywords);
    qa_cleanup_push(&master_guard, free, master);
    qa_q3_browser_view_global_servers(browser, qa_native_atoi(args[0]) == 1 ? 1 : 2, master, qa_native_atoi(args[1]),
        keywords, keyword_count);
    qa_cleanup_pop(&master_guard);
    qa_cleanup_pop(&keyword_guard);
    free(master);
    free(keywords);
    return true;
  }
  if (strcmp(name, "ping") == 0) {
    if (arg_count != 1) print(print_ctx, "usage: ping [server]\n");
    else qa_q3_browser_view_ping(browser, args[0]);
    return true;
  }
  if (strcmp(name, "serverstatus") == 0) {
    const char *server = arg_count == 1 ? args[0]
        : strcmp(qa_remote_application_network_phase(app), "active") == 0 && qa_remote_network_is(&app->launch_options, "q3-client")
            ? app->launch_options.network_remote : NULL;
    if (server == NULL) print(print_ctx, "Not connected to a server.\nUsage: serverstatus [server]\n");
    else qa_q3_browser_view_server_status_command(browser, server);
    return true;
  }
  return false;
}

/* ---- the frontend's scene queries (TS RemoteWorldFrontend.scene) ------- */
static qa_scene_queries remote_scene(qa_remote_application *app) {
  return qa_shared_as_scene_queries(qa_app_remote_seat_presentation_scene(qa_remote_application_remote(app)));
}
static qa_trace_result scene_trace(void *ctx, qa_trace_query query) {
  qa_scene_queries scene = remote_scene(ctx);
  return scene.trace(scene.ctx, query);
}
static qa_point_contents_result scene_point_contents(void *ctx, qa_point_contents_query query) {
  qa_scene_queries scene = remote_scene(ctx);
  return scene.point_contents(scene.ctx, query);
}
static qa_leaf_query_result scene_box_leaves(void *ctx, qa_bounds bounds, double limit) {
  qa_scene_queries scene = remote_scene(ctx);
  return scene.box_leaves(scene.ctx, bounds, limit);
}
static bool scene_areas_connected(void *ctx, double first, double second) {
  qa_scene_queries scene = remote_scene(ctx);
  return scene.areas_connected(scene.ctx, first, second);
}
static bool scene_cluster_visible(void *ctx, double from, double to, const char *kind) {
  qa_scene_queries scene = remote_scene(ctx);
  return scene.cluster_visible(scene.ctx, from, to, kind);
}
static bool effects_is_player(void *ctx, qa_actor_id actor) {
  return qa_app_remote_seat_presentation_is_player(qa_remote_application_remote(ctx), actor);
}
static double audio_now(void *ctx) { return ((qa_remote_application *)ctx)->elapsed; }
static void audio_print(void *ctx, const char *text) { qa_remote_application_print(ctx, text, NULL); }
static void assets_print(void *ctx, const char *text) {
  qa_remote_application *app = ctx;
  if (app->host.base.print != NULL) app->host.base.print(app->host.base.ctx, text);
}
static bool menu_art_read(void *ctx, const char *path, qa_image_level *out) {
  (void)ctx; /* TS loadMenuArtImage captures nothing */
  qa_app_load_menu_art_image(path, out);
  return true;
}

/* ---- loadFrontend (TS 1816-1873) --------------------------------------- */
typedef struct frontend_state { qa_remote_application *app; qa_app_loaded_content *content; qa_remote_world_frontend *frontend; } frontend_state;
static void frontend_body(void *ctx) {
  frontend_state *state = ctx;
  qa_remote_application *app = state->app;
  qa_remote_world_frontend *frontend = state->frontend;
  (void)qa_app_application_assets_load_world(frontend->assets);
  frontend->font = qa_app_application_assets_load_console_font(frontend->assets);
  const qa_renderer_image *source = &frontend->font.classic->picture.image.image;
  if (!source->source_is_resource) qa_raise("Error", 0, 0, "Remote console font has no mounted resource identity");
  frontend->art = xcalloc(1, sizeof *frontend->art);
  qa_ui_load_native_art_selected(frontend->art, source->source_resource.id, qa_app_application_assets_images(frontend->assets),
      menu_art_read, NULL, false, false);
  qa_app_audio_preferences loaded;
  memset(&loaded, 0, sizeof loaded);
  bool has_loaded = qa_app_load_audio_settings(app->input_config, &loaded);
  qa_app_audio_options options;
  memset(&options, 0, sizeof options);
  if (has_loaded) {
    options.has_output_format = loaded.has_output_format; options.output_format = loaded.output_format;
    options.device_name = loaded.device_name;
    options.has_effects_volume = true; options.effects_volume = loaded.effects_volume;
    options.has_music_volume = true; options.music_volume = loaded.music_volume;
  }
  options.music_controls = app->music_controls;
  options.defer_output = app->ownership.borrowed;
  qa_cleanup track_guard, device_guard;
  qa_cleanup_push(&track_guard, free, loaded.menu_track);
  qa_cleanup_push(&device_guard, free, loaded.device_name);
  frontend->audio = qa_app_application_audio_create(state->content, audio_now, app, app->launch_options.seed,
      app->launch_options.character_model, audio_print, app, &options);
  qa_cleanup_pop(&device_guard);
  qa_cleanup_pop(&track_guard);
  free(loaded.menu_track);
  free(loaded.device_name);
  if (frontend->audio == NULL) qa_range_error("Out of memory");
  frontend->scene = (qa_scene_queries){app, scene_trace, scene_point_contents, scene_box_leaves, scene_areas_connected,
      scene_cluster_visible};
  qa_app_application_audio_prepare_environment(frontend->audio, &frontend->scene);
  frontend->effects = qa_app_application_effects_create(frontend->assets, frontend->scene, effects_is_player, app,
      app->launch_options.seed);
  if (frontend->effects == NULL) qa_range_error("Out of memory");
  size_t failure_count = 0;
  qa_app_effect_preload_failure *failures = qa_app_application_effects_preload_transient_resources(frontend->effects,
      &failure_count);
  for (size_t i = 0; i < failure_count; i++) {
    size_t length = strlen(failures[i].content) + strlen(failures[i].path) + strlen(failures[i].error) + 48;
    char *line = xcalloc(length, 1);
    snprintf(line, length, "Optional effect preload skipped: %s/%s: %s\n", failures[i].content, failures[i].path, failures[i].error);
    qa_remote_application_print(app, line, NULL);
    free(line);
  }
  qa_app_effect_preload_failures_free(failures, failure_count);
}
static void frontend_free(qa_remote_world_frontend *frontend) {
  if (frontend->effects != NULL) qa_app_application_effects_destroy(frontend->effects);
  if (frontend->audio != NULL) qa_app_application_audio_destroy(frontend->audio);
  free(frontend->art);
  if (frontend->assets != NULL) qa_app_application_assets_destroy(frontend->assets);
  free(frontend);
}
static void frontend_cleanup(void *ctx) {
  qa_remote_world_frontend *frontend = ctx;
  if (frontend->effects != NULL) qa_app_application_effects_close(frontend->effects);
  if (frontend->audio != NULL) qa_app_application_audio_close(frontend->audio);
  if (frontend->art != NULL) qa_native_ui_art_close(frontend->art);
  qa_app_application_assets_close(frontend->assets);
}
static void execute_empty(void *ctx) { qa_remote_renderer_execute(ctx, NULL, 0); }
qa_remote_world_frontend *qa_remote_application_load_frontend(qa_remote_application *app, qa_app_loaded_content *content) {
  qa_remote_world_frontend *frontend = xcalloc(1, sizeof *frontend);
  qa_scene_image_policy_owned policy;
  qa_app_image_settings_policy(app->image_settings, &policy);
  qa_app_world_asset_images images;
  memset(&images, 0, sizeof images);
  images.policy = policy.policy;
  images.model_policy = qa_app_image_settings_model_policy(app->image_settings);
  images.cvars = qa_app_image_settings_cvars(app->image_settings);
  frontend->assets = qa_app_create_world_assets(content, qa_app_native_renderer_owner(app->renderer),
      qa_app_native_renderer_images(app->renderer), assets_print, app, &images);
  qa_scene_image_policy_owned_destroy(&policy);
  if (frontend->assets == NULL) { free(frontend); qa_range_error("Out of memory"); }
  frontend_state state = {app, content, frontend};
  qa_caught_error caught;
  if (!qa_try(frontend_body, &state, &caught)) {
    qa_caught_error cleanup;
    (void)qa_try(frontend_cleanup, frontend, &cleanup);
    frontend_free(frontend);
    if (!qa_try(execute_empty, app, &cleanup))
      qa_raise("AggregateError", 0, 0, "Remote frontend preparation and image retirement failed");
    qa_rethrow(&caught);
  }
  return frontend;
}

/* ---- releaseFrontend (TS 1875-1886) ------------------------------------ */
void qa_remote_application_release_frontend(qa_remote_application *app, qa_remote_world_frontend *frontend) {
  qa_app_application_audio_close(frontend->audio);
  qa_app_application_effects_close(frontend->effects);
  qa_native_ui_art_close(frontend->art);
  qa_app_application_assets_close(frontend->assets);
  if (!app->closed) {
    size_t count = 0;
    qa_image_resource_operation *operations = qa_scene_image_registry_drain_operations(
        qa_app_application_assets_images(frontend->assets), &count);
    qa_render_command *commands = xcalloc(count, sizeof *commands);
    for (size_t i = 0; i < count; i++) {
      commands[i].kind = QA_RENDER_COMMAND_IMAGE_RESOURCE;
      commands[i].operation = operations[i];
    }
    qa_cleanup guard, operations_guard;
    qa_cleanup_push(&operations_guard, free, operations);
    qa_cleanup_push(&guard, free, commands);
    qa_remote_renderer_execute(app, commands, count);
    qa_cleanup_pop(&guard);
    qa_cleanup_pop(&operations_guard);
    free(commands);
    free(operations);
  }
  frontend_free(frontend);
}

/* ---- retireFrontends (TS 1129-1144) ------------------------------------ */
typedef struct retire_call { qa_remote_application *app; qa_remote_retired_frontend retired; } retire_call;
static void retire_release(void *ctx) {
  retire_call *call = ctx;
  qa_remote_application_release_frontend(call->app, call->retired.frontend);
}
static void retire_content(void *ctx) {
  retire_call *call = ctx;
  if (call->retired.content != NULL) {
    qa_app_loaded_content_close(call->retired.content);
    qa_app_loaded_content_destroy(call->retired.content);
  }
}
void qa_remote_application_retire_frontends(qa_remote_application *app) {
  bool failed = false;
  qa_caught_error caught;
  while (qa_event_ring_live(app->retired_frontends) > 0) {
    retire_call call;
    call.app = app;
    call.retired = *(const qa_remote_retired_frontend *)qa_event_ring_head(app->retired_frontends);
    qa_event_ring_remove_at_offset(app->retired_frontends, 0);
    if (!qa_try(retire_release, &call, &caught)) failed = true;
    if (!qa_try(retire_content, &call, &caught)) failed = true;
  }
  if (failed) qa_raise("AggregateError", 0, 0, "Remote frontend retirement failed");
}

/* ---- refreshImages (TS 1888-1906) -------------------------------------- */
void qa_remote_application_refresh_images(qa_remote_application *app) {
  qa_remote_world_frontend *frontend = app->frontend;
  if (frontend == NULL) return;
  qa_app_world_seat_presentation **presentations = xcalloc(app->remote_seat_count + 1, sizeof *presentations);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, presentations);
  size_t count = 0;
  qa_app_world_seat_presentation *primary = qa_remote_application_presentation(app);
  if (primary != NULL) presentations[count++] = primary;
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (peer == app->primary_peer || peer->view == NULL || qa_app_remote_seat_view_assets(peer->view) != frontend->assets) continue;
    presentations[count++] = qa_app_remote_seat_view_presentation(peer->view);
  }
  qa_app_image_settings_refresh(app->image_settings, frontend->assets, presentations, count, NULL, app->renderer);
  qa_cleanup_pop(&guard);
  free(presentations);
  frontend->font = qa_app_application_assets_load_console_font(frontend->assets);
  for (size_t i = 0; i < app->remote_seat_count; i++) {
    qa_remote_seat_peer *peer = app->remote_seats[i];
    if (peer->view == NULL || qa_app_remote_seat_view_assets(peer->view) == frontend->assets) continue;
    qa_app_world_seat_presentation *presentation = qa_app_remote_seat_view_presentation(peer->view);
    qa_app_image_settings_refresh(app->image_settings, qa_app_remote_seat_view_assets(peer->view), &presentation, 1, NULL,
        app->renderer);
  }
}

/* ---- loadQ2ServerWorld (TS 1908-1920) ---------------------------------- */
typedef struct q2_names { char **items; size_t count; } q2_names;
static void q2_names_free(void *object) {
  q2_names *names = object;
  for (size_t i = 0; i < names->count; i++) free(names->items[i]);
  free(names->items);
  names->items = NULL;
  names->count = 0;
}
/* Array.from({ length: maximum - 1 }, (_, index) => state.configStrings.get(first + index + 1) ?? "").filter(Boolean) */
/* TS state.configStrings.get(index): NULL is undefined. */
static const char *q2_config_get(const qa_app_q2_config_snapshot *configs, double index) {
  for (size_t i = 0; i < configs->count; i++)
    if (configs->entries[i].index == index) return configs->entries[i].value;
  return NULL;
}
static void q2_config_names(const qa_app_q2_config_snapshot *configs, double first, double maximum, q2_names *out) {
  size_t length = maximum > 1 ? (size_t)(maximum - 1) : 0;
  out->items = xcalloc(length, sizeof *out->items);
  out->count = 0;
  for (size_t i = 0; i < length; i++) {
    const char *value = q2_config_get(configs, first + (double)i + 1);
    if (value != NULL && value[0] != '\0') out->items[out->count++] = xdup(value);
  }
}
typedef struct q2_world_names { q2_names models, sounds, images; } q2_world_names;
static void q2_world_names_free(void *object) {
  q2_world_names *names = object;
  q2_names_free(&names->models);
  q2_names_free(&names->sounds);
  q2_names_free(&names->images);
}
qa_app_loaded_content *qa_remote_application_load_q2_server_world(qa_remote_application *app, const qa_app_q2_game_state *state) {
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  if (remote->family != QA_APP_REMOTE_FAMILY_Q2) qa_raise("Error", 0, 0, "Q2 world requires its native presentation owner");
  qa_app_q2_layout layout = qa_app_q2_remote_config_layout(remote->q2);
  const qa_app_q2_config_snapshot *configs = state->config_strings;
  const char *map = q2_config_get(configs, layout.models + 1);
  if (map == NULL) qa_raise("Error", 0, 0, "Q2 server supplied no world model");
  q2_world_names names;
  memset(&names, 0, sizeof names);
  qa_cleanup guard;
  qa_cleanup_push(&guard, q2_world_names_free, &names);
  q2_config_names(configs, layout.models, layout.max_models, &names.models);
  q2_config_names(configs, layout.sounds, layout.max_sounds, &names.sounds);
  q2_config_names(configs, layout.images, layout.max_images, &names.images);
  qa_app_q1_remote_world_info world = {map, (const char *const *)names.models.items, names.models.count,
      (const char *const *)names.sounds.items, names.sounds.count};
  qa_app_loaded_content *content = qa_remote_application_load_server_world(app, &world,
      (const char *const *)names.images.items, names.images.count, NULL, true);
  qa_cleanup_pop(&guard);
  q2_world_names_free(&names);
  return content;
}

/* ---- selectRemoteContent (TS 1922-1979) -------------------------------- */
typedef struct select_current {
  qa_remote_application *app; double generation;
  qa_app_assert_current_fn assert_peer; void *assert_ctx;
} select_current;
static const char *select_assert(void *ctx) {
  select_current *current = ctx;
  const char *error = current->assert_peer(current->assert_ctx);
  if (error != NULL) return error;
  qa_remote_application *app = current->app;
  if (app->closed || app->closing || current->generation != app->remote_content_generation)
    return "Remote directory selection was cancelled";
  return NULL;
}
static void select_assert_raise(select_current *current) {
  const char *error = select_assert(current);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
typedef struct admission_state {
  qa_remote_application *app; qa_remote_peer_admission *request; select_current *current;
} admission_state;
static void admission_body(void *ctx) {
  admission_state *state = ctx;
  qa_remote_application_publish_peer_configuration(state->app, state->request);
  select_assert_raise(state->current);
}
static void release_admission(void *object) {
  qa_remote_peer_admission *request = object;
  qa_app_options_destroy(&request->options);
  free(request->cancel_error);
  free(request);
}
qa_app_remote_content_mounts *qa_remote_application_select_remote_content(qa_remote_application *app,
    const qa_remote_content_selection *selection, qa_app_assert_current_fn assert_peer_current, void *assert_ctx) {
  double generation = ++app->remote_content_generation;
  app->world_load_generation++;
  select_current *current = xcalloc(1, sizeof *current);
  qa_remote_arena_add(&app->arena, free, current);
  current->app = app;
  current->generation = generation;
  current->assert_peer = assert_peer_current;
  current->assert_ctx = assert_ctx;
  qa_app_remote_content_mounts *prepared = qa_app_open_remote_content_native(&app->launch_options, selection, select_assert,
      current, (int64_t)generation);
  if (prepared == NULL) qa_range_error("Out of memory");
  const char *error = select_assert(current);
  if (error != NULL) {
    qa_app_remote_content_mounts_close(prepared);
    qa_app_remote_content_mounts_destroy(prepared);
    qa_raise("Error", 0, 0, "%s", error);
  }
  /* options = { ...roots, product, remoteContent: prepared.selection, q3Product? } */
  qa_app_options options;
  qa_remote_options_copy(&app->launch_options, &options);
  free(options.product);
  options.product = xdup(prepared->product->expectation.id);
  if (options.has_remote_content) qa_remote_content_selection_free(&options.remote_content);
  if (!qa_remote_content_selection_make(prepared->selection.base, prepared->selection.directory, &options.remote_content))
    qa_range_error("Out of memory");
  options.has_remote_content = true;
  if (prepared->has_q3_product) { options.has_q3_product = true; options.q3_product = prepared->q3_product; }
  if (strcmp(options.product, app->launch_options.product) != 0) {
    /* TS: with no source frame in flight (or a peer driving frames) the admission publishes
     * immediately; otherwise it suspends to the next frame boundary. The C source
     * operation runs synchronously to its end inside advanceSource, so no frame
     * boundary can intervene: the admission publishes here in both cases. */
    if (app->peer_admission != NULL && app->pending_advance == NULL && app->primary_peer == NULL) {
      qa_app_options_destroy(&options);
      qa_app_remote_content_mounts_close(prepared);
      qa_app_remote_content_mounts_destroy(prepared);
      qa_raise("Error", 0, 0, "Peer directory admission has no active source frame");
    }
    qa_remote_peer_admission *request = xcalloc(1, sizeof *request);
    qa_remote_arena_add(&app->arena, release_admission, request);
    request->content = prepared;
    request->options = options;
    request->assert_current = select_assert;
    request->assert_ctx = current;
    request->published = false;
    admission_state state = {app, request, current};
    qa_caught_error caught;
    if (!qa_try(admission_body, &state, &caught)) {
      if (app->remote_content != prepared) {
        qa_app_remote_content_mounts_close(prepared);
        qa_app_remote_content_mounts_destroy(prepared);
      }
      qa_rethrow(&caught);
    }
    qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_content_mounts_destroy, prepared);
    return prepared;
  }
  qa_app_remote_content_mounts *previous = app->remote_content;
  app->remote_content = prepared;
  qa_remote_arena_add(&app->arena, (void (*)(void *))qa_app_remote_content_mounts_destroy, prepared);
  qa_app_options_destroy(&app->launch_options);
  app->launch_options = options;
  if (previous != NULL) qa_app_remote_content_mounts_close(previous);
  return prepared;
}

/* ---- refreshRemoteContent (TS 1981-1985) ------------------------------- */
qa_app_remote_content_mounts *qa_remote_application_refresh_remote_content(qa_remote_application *app,
    qa_app_assert_current_fn assert_current, void *assert_ctx) {
  if (!app->launch_options.has_remote_content) qa_raise("Error", 0, 0, "Remote package refresh has no selected directory");
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make(app->launch_options.remote_content.base, app->launch_options.remote_content.directory,
          &selection))
    qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_remote_content_selection_free, &selection);
  qa_app_remote_content_mounts *content = qa_remote_application_select_remote_content(app, &selection, assert_current, assert_ctx);
  qa_cleanup_pop(&guard);
  qa_remote_content_selection_free(&selection);
  return content;
}

/* ---- prepareQwDownloads (TS 1987-2026) --------------------------------- */
struct qa_remote_qw_binding { qa_remote_application *app; double generation; char *game_root, *skin_root; };
void qa_remote_application_close_qw_downloads(qa_remote_application *app) {
  if (app->qw_downloads != NULL) {
    qa_app_qw_download_receiver_close(app->qw_downloads);
    qa_app_qw_download_receiver_destroy(app->qw_downloads);
  }
  app->qw_downloads = NULL;
  if (app->qw_binding != NULL) {
    free(app->qw_binding->game_root);
    free(app->qw_binding->skin_root);
    free(app->qw_binding);
  }
  app->qw_binding = NULL;
}
static const char *assert_nothing(void *ctx) { (void)ctx; /* TS () => {} */ return NULL; }
static bool qw_current(const struct qa_remote_qw_binding *b) {
  return !b->app->closed && !b->app->closing && b->generation == b->app->remote_content_generation;
}
static void qw_assert(const struct qa_remote_qw_binding *b) {
  if (!qw_current(b)) qa_raise("Error", 0, 0, "QW directory selection was cancelled");
}
typedef struct qw_exists_state { struct qa_remote_qw_binding *binding; const char *path; bool found; } qw_exists_state;
static void qw_exists_body(void *ctx) {
  qw_exists_state *state = ctx;
  const qa_resolved_resource_reference *resolved = NULL;
  state->found = qa_content_mounted_resolve(qa_remote_application_mounts(state->binding->app), state->path, &resolved);
}
static bool qw_exists(void *ctx, const char *path, qa_app_qw_download_category category) {
  struct qa_remote_qw_binding *b = ctx;
  if (!qw_current(b)) return false;
  char *joined = qa_remote_join3(category == QA_APP_QW_DL_SKIN ? b->skin_root : b->game_root, path, ".");
  struct stat info;
  bool exists = stat(joined, &info) == 0;
  free(joined);
  if (exists) return true;
  qw_exists_state state = {b, path, false};
  qa_caught_error caught;
  if (!qa_try(qw_exists_body, &state, &caught)) {
    if (!qw_current(b)) return false;
    qa_rethrow(&caught);
  }
  return state.found;
}
static void qw_send(void *ctx, const char *text) {
  struct qa_remote_qw_binding *b = ctx;
  qw_assert(b);
  qa_remote_application_send_command(b->app, text);
}
static void qw_print(void *ctx, const char *text) { qa_remote_application_print(((struct qa_remote_qw_binding *)ctx)->app, text, NULL); }
static double qw_noskins(void *ctx) {
  qa_remote_application *app = ((struct qa_remote_qw_binding *)ctx)->app;
  return app->client_command_owner == NULL ? 0 : qa_cvar_registry_variable_value(app->client_command_owner->cvars, "noskins");
}
static bool qw_demo_recording(void *ctx) {
  qa_remote_application *app = ((struct qa_remote_qw_binding *)ctx)->app;
  return app->ownership.borrowed && qa_app_client_demo_recording_path(app->ownership.client->recording) != NULL;
}
static bool qw_demo_playback(void *ctx) {
  qa_remote_application *app = ((struct qa_remote_qw_binding *)ctx)->app;
  return qa_remote_application_source(app)->kind == QA_REMOTE_SOURCE_RECORDED;
}
void qa_remote_application_prepare_qw_downloads(qa_remote_application *app, const char *game_directory) {
  qa_remote_content_selection selection;
  if (!qa_remote_content_selection_make("q1-quakeworld", game_directory, &selection)) qa_range_error("Out of memory");
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_remote_content_selection_free, &selection);
  if (qa_remote_application_source(app)->kind == QA_REMOTE_SOURCE_RECORDED) {
    (void)qa_remote_application_select_remote_content(app, &selection, assert_nothing, NULL);
    qa_cleanup_pop(&guard);
    qa_remote_content_selection_free(&selection);
    return;
  }
  qa_remote_application_close_qw_downloads(app);
  double generation = app->remote_content_generation + 1;
  qa_app_remote_content_mounts *prepared = qa_remote_application_select_remote_content(app, &selection, assert_nothing, NULL);
  qa_cleanup_pop(&guard);
  qa_remote_content_selection_free(&selection);
  struct qa_remote_qw_binding *binding = xcalloc(1, sizeof *binding);
  binding->app = app;
  binding->generation = generation;
  if (!qw_current(binding)) { free(binding); qa_raise("Error", 0, 0, "QW directory selection was cancelled"); }
  binding->game_root = xdup(prepared->write_root);
  binding->skin_root = xdup(prepared->base_write_root);
  qa_app_qw_download_options options;
  memset(&options, 0, sizeof options);
  options.ctx = binding;
  options.game_root = binding->game_root;
  options.skin_root = binding->skin_root;
  options.exists = qw_exists;
  options.send_command = qw_send;
  options.print = qw_print;
  options.noskins = qw_noskins;
  options.demo_recording = qw_demo_recording;
  options.demo_playback = qw_demo_playback;
  app->qw_binding = binding;
  app->qw_downloads = qa_app_qw_download_receiver_create(options);
  if (app->qw_downloads == NULL) qa_range_error("Out of memory");
}

/* ---- prepareQ3Downloads (TS 2028-2100) --------------------------------- */
struct qa_remote_q3_binding {
  qa_remote_application *app; qa_q3_client_connection *connection; int64_t generation;
  qa_app_q3_client_downloads *downloads;
  qa_app_q3_download_mounts mounts; /* strings owned */
};
void qa_remote_application_close_q3_downloads(qa_remote_application *app) {
  if (app->q3_downloads != NULL) {
    qa_app_q3_client_downloads_close(app->q3_downloads);
    qa_app_q3_client_downloads_destroy(app->q3_downloads);
  }
  app->q3_downloads = NULL;
  if (app->q3_binding != NULL) {
    free((char *)app->q3_binding->mounts.selection_directory);
    free((char *)app->q3_binding->mounts.write_root);
    free((char *)app->q3_binding->mounts.base_write_root);
    free(app->q3_binding);
  }
  app->q3_binding = NULL;
}
typedef struct q3_current { qa_remote_application *app; qa_q3_client_connection *connection; int64_t generation; } q3_current;
static const char *q3_assert_text(void *ctx) {
  q3_current *current = ctx;
  qa_remote_application *app = current->app;
  const qa_remote_live_network *network = qa_remote_application_network(app);
  if (app->closed || app->closing || network == NULL || network->family != QA_APP_REMOTE_FAMILY_Q3
      || qa_app_q3_client_network_native(network->q3) != current->connection
      || current->generation != qa_q3_client_connection_generation(current->connection))
    return "Q3 package download belongs to a retired connection";
  return NULL;
}
static void q3_assert(q3_current *current) {
  const char *error = q3_assert_text(current);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void q3_binding_assert(void *ctx) {
  struct qa_remote_q3_binding *b = ctx;
  q3_current current = {b->app, b->connection, b->generation};
  q3_assert(&current);
  if (b->app->q3_downloads != b->downloads) qa_raise("Error", 0, 0, "Q3 package download was replaced");
}
static void q3_binding_reliable(void *ctx, const char *text) {
  struct qa_remote_q3_binding *b = ctx;
  qa_q3_reliable_command added;
  if (!qa_q3_client_reliable_commands_add(qa_q3_client_connection_reliable(b->connection), text, &added))
    qa_range_error("Out of memory");
}
static void q3_binding_send_packet(void *ctx) {
  struct qa_remote_q3_binding *b = ctx;
  q3_current current = {b->app, b->connection, b->generation};
  q3_assert(&current);
  const qa_remote_live_network *network = qa_remote_application_network(b->app);
  if (network != NULL && network->family == QA_APP_REMOTE_FAMILY_Q3) qa_app_q3_client_network_send_packet(network->q3);
}
static void q3_binding_progress(void *ctx, const char *name, double count, double size) {
  struct qa_remote_q3_binding *b = ctx;
  if (count == 0 || count == size) {
    char count_text[40], size_text[40];
    qa_net_js_number(count, count_text, sizeof count_text);
    qa_net_js_number(size, size_text, sizeof size_text);
    print_line(b->app, "Downloading %s: %s/%s bytes\n", name, count_text, size_text);
  }
}
static const char *q3_refresh_assert(void *ctx) {
  struct qa_remote_q3_binding *b = ctx;
  q3_current current = {b->app, b->connection, b->generation};
  const char *error = q3_assert_text(&current);
  if (error != NULL) return error;
  if (b->app->q3_downloads != b->downloads) return "Q3 package refresh was cancelled";
  return NULL;
}
static void q3_binding_reload(void *ctx) {
  struct qa_remote_q3_binding *b = ctx;
  (void)qa_remote_application_refresh_remote_content(b->app, q3_refresh_assert, b);
}
typedef struct q3_exists_ctx { const char *root; const qa_app_q3_download_mounts *seed; bool mapped; } q3_exists_ctx;
static bool q3_exists(void *ctx, const char *path) {
  q3_exists_ctx *e = ctx;
  char *mapped = e->mapped ? qa_app_q3_download_path(path, e->seed) : xdup(path);
  if (mapped == NULL) qa_range_error("Out of memory");
  char *joined = qa_remote_join3(e->root, mapped, ".");
  free(mapped);
  struct stat info;
  bool exists = stat(joined, &info) == 0;
  free(joined);
  return exists;
}
static void make_directories(const char *root) {
  char *path = xdup(root);
  for (char *at = path + 1; *at != '\0'; at++) {
    if (*at != '/') continue;
    *at = '\0';
    if (mkdir(path, 0777) != 0 && errno != EEXIST) {
      int code = errno;
      free(path);
      qa_raise("Error", 0, 0, "%s", strerror(code));
    }
    *at = '/';
  }
  if (mkdir(path, 0777) != 0 && errno != EEXIST) {
    int code = errno;
    free(path);
    qa_raise("Error", 0, 0, "%s", strerror(code));
  }
  free(path);
}
static char *dirname_of(const char *path) {
  char *copy = xdup(path);
  size_t length = strlen(copy);
  while (length > 1 && copy[length - 1] == '/') copy[--length] = '\0';
  char *slash = strrchr(copy, '/');
  if (slash == NULL) { free(copy); return xdup("."); }
  if (slash == copy) { copy[1] = '\0'; return copy; }
  *slash = '\0';
  return copy;
}
typedef struct q3_prepare_state {
  qa_q3_server_pak_set *referenced; qa_q3_server_pak *snapshot; char *info, *root, *missing;
  qa_app_q3_packages *packages; double *loaded; int64_t *loaded64; qa_app_q3_package_mount *mounts;
  qa_remote_content_selection selection; bool has_selection;
} q3_prepare_state;
static void q3_prepare_release(void *ctx) {
  q3_prepare_state *p = ctx;
  if (p->referenced) qa_q3_server_pak_set_destroy(p->referenced);
  free(p->snapshot); free(p->info); free(p->root); free(p->missing);
  if (p->packages) qa_app_q3_packages_destroy(p->packages);
  free(p->loaded); free(p->loaded64); free(p->mounts);
  if (p->has_selection) qa_remote_content_selection_free(&p->selection);
  memset(p, 0, sizeof *p);
}
bool qa_remote_application_prepare_q3_downloads(qa_remote_application *app, qa_q3_client_connection *connection) {
  q3_current *current = xcalloc(1, sizeof *current);
  qa_remote_arena_add(&app->arena, free, current);
  current->app = app;
  current->connection = connection;
  current->generation = qa_q3_client_connection_generation(connection);
  q3_assert(current);
  qa_remote_application_close_q3_downloads(app);
  q3_prepare_state p;
  memset(&p, 0, sizeof p);
  qa_cleanup guard;
  qa_cleanup_push(&guard, q3_prepare_release, &p);
  p.info = game_state_text(connection, 1);
  char *game = qa_q3_info_value(p.info, "fs_game");
  if (game == NULL) qa_range_error("Out of memory");
  bool made = qa_remote_content_selection_make("q3-baseq3", game[0] != '\0' ? game : "baseq3", &p.selection);
  free(game);
  if (!made) qa_range_error("Out of memory");
  p.has_selection = true;
  qa_app_remote_content_mounts *seed = qa_remote_application_select_remote_content(app, &p.selection, q3_assert_text, current);
  p.root = dirname_of(seed->write_root);
  size_t mount_count = 0;
  p.mounts = qa_app_q3_package_mounts_of(seed->catalog, seed->mounts, &mount_count);
  p.packages = qa_app_q3_packages_open(p.mounts, mount_count, (double)qa_q3_client_connection_checksum_feed(connection));
  if (p.packages == NULL) qa_range_error("Out of memory");
  q3_assert(current);
  if (app->remote_content != seed) qa_raise("Error", 0, 0, "Q3 package content selection was replaced");
  if (!qa_q3_server_pak_set_init(&p.referenced)) qa_range_error("Out of memory");
  char *sums = qa_q3_info_value(p.info, "sv_referencedPaks"), *names = qa_q3_info_value(p.info, "sv_referencedPakNames");
  if (sums == NULL || names == NULL) { free(sums); free(names); qa_range_error("Out of memory"); }
  (void)qa_q3_server_pak_set_set_checksums(p.referenced, sums);
  qa_q3_server_pak_set_set_names(p.referenced, names, -1);
  free(sums);
  free(names);
  struct qa_remote_q3_binding *binding = xcalloc(1, sizeof *binding);
  binding->app = app;
  binding->connection = connection;
  binding->generation = current->generation;
  binding->mounts.selection_base = seed->selection.base;
  binding->mounts.selection_directory = xdup(seed->selection.directory);
  binding->mounts.write_root = xdup(seed->write_root);
  binding->mounts.base_write_root = xdup(seed->base_write_root);
  qa_app_q3_download_bindings bindings;
  memset(&bindings, 0, sizeof bindings);
  bindings.ctx = binding;
  bindings.permission = app->download_permission;
  bindings.assert_current = q3_binding_assert;
  bindings.reliable = q3_binding_reliable;
  bindings.send_packet = q3_binding_send_packet;
  bindings.progress = q3_binding_progress;
  bindings.reload_packages = q3_binding_reload;
  qa_app_q3_client_downloads *downloads = qa_app_q3_client_downloads_create(p.root, bindings, &binding->mounts);
  if (downloads == NULL) qa_range_error("Out of memory");
  binding->downloads = downloads;
  app->q3_downloads = downloads;
  app->q3_binding = binding;
  size_t pack_count = 0;
  const qa_q3_mounted_pak *packs = qa_app_q3_packages_packs(p.packages, &pack_count);
  p.loaded = xcalloc(pack_count, sizeof *p.loaded);
  p.loaded64 = xcalloc(pack_count, sizeof *p.loaded64);
  for (size_t i = 0; i < pack_count; i++) {
    p.loaded[i] = (double)packs[i].pack.checksum;
    p.loaded64[i] = packs[i].pack.checksum;
  }
  size_t referenced_count = 0;
  if (!qa_q3_server_pak_set_snapshot(p.referenced, &p.snapshot, &referenced_count)) qa_range_error("Out of memory");
  q3_exists_ctx plain = {p.root, &binding->mounts, false}, mapped = {p.root, &binding->mounts, true};
  bool permitted = app->download_permission != NULL
      && qa_app_client_download_permitted(app->download_permission, QA_APP_DL_NATIVE, QA_APP_DL_PACKAGE);
  if (!permitted) {
    p.missing = qa_q3_compare_packages(p.snapshot, referenced_count, p.loaded64, pack_count, q3_exists, &mapped, false, 1024);
    if (p.missing == NULL) qa_range_error("Out of memory");
    if (p.missing[0] != '\0')
      print_line(app, "Missing server packages: %s\nDownloads are disabled. Enable cl_allowDownload to download server packages.\n",
          p.missing, NULL, NULL);
    qa_cleanup_pop(&guard);
    q3_prepare_release(&p);
    return false;
  }
  bool pending = qa_app_q3_client_downloads_begin(downloads, p.snapshot, referenced_count, p.loaded, pack_count, q3_exists, &plain);
  if (pending) {
    make_directories(p.root);
    q3_assert(current);
    if (app->q3_downloads != downloads) qa_raise("Error", 0, 0, "Q3 download setup was cancelled");
  }
  qa_cleanup_pop(&guard);
  q3_prepare_release(&p);
  return pending;
}

/* ---- loadQ3ServerWorld (TS 2102-2126) ---------------------------------- */
typedef struct q3_content_host {
  qa_remote_application *app;
  qa_app_options options; /* { ...this.options, map: serverMapPath(world.map) } (owned) */
  bool recorded; qa_app_content_source source;
  qa_app_q3_package_mount *catalog_mounts, *loaded_mounts;
  /* TS GC shares the LoadedApplicationContent between Q3ClientContent and the application. In C the prepared content
   * owns it until loadServerWorld adopts it as the application's loadedContent; from then on Q3ClientContent.close()
   * only closes it, while the content lives (content_alive). The host is released by both owners (released). */
  bool adopted, content_alive, released;
} q3_content_host;
static void *q3c_load(void *ctx, const qa_pure_mount_policy *policy) {
  q3_content_host *host = ctx;
  qa_app_options options;
  qa_app_startup_options_copy(&options, &host->options);
  qa_app_loaded_content *content = qa_app_load_application_content_native((qa_app_content_host){0}, options, NULL, false,
      policy, NULL, host->recorded ? &host->source : NULL);
  if (content == NULL) qa_range_error("Out of memory");
  return content;
}
static const qa_app_q3_package_mount *q3c_loaded_mounts(void *ctx, void *loaded, size_t *out_count) {
  q3_content_host *host = ctx;
  free(host->loaded_mounts);
  host->loaded_mounts = qa_app_q3_package_mounts_of(qa_app_loaded_content_catalog(loaded), qa_app_loaded_content_mounts(loaded),
      out_count);
  return host->loaded_mounts;
}
static void q3c_collect(void *ctx, void *loaded,
    void (*visit)(void *visit_ctx, const qa_resolved_resource_reference *reference, bool is_q3), void *visit_ctx) {
  (void)ctx; /* the collection reads the loaded content only */
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(loaded);
  size_t capacity = 16, count = 0;
  const void **opened = NULL;
  for (;;) {
    free(opened);
    opened = xcalloc(capacity, sizeof *opened);
    count = qa_app_loaded_content_opened_mounts(loaded, opened, capacity);
    if (count < capacity) break;
    capacity *= 2;
  }
  for (size_t i = 0; i < count; i++) {
    size_t resources = 0;
    const qa_resolved_resource_reference *rows = qa_content_mounted_opened_resources(opened[i], &resources);
    for (size_t j = 0; j < resources; j++) {
      const qa_resolved_resource_reference *reference = &rows[j];
      qa_content_id content = reference->provenance.kind == QA_CONTENT_MOUNT_ARCHIVE
          ? reference->provenance.archive_mount->identity.content : reference->provenance.loose_mount->identity.content;
      visit(visit_ctx, reference, qa_content_catalog_product(catalog, content)->expectation.family == QA_GAME_FAMILY_Q3);
    }
  }
  free(opened);
}
static void q3c_close(void *ctx, void *loaded) {
  q3_content_host *host = ctx;
  if (!host->adopted) {
    qa_app_loaded_content_close(loaded);
    qa_app_loaded_content_destroy(loaded);
  } else if (host->content_alive) qa_app_loaded_content_close(loaded);
}
static void q3_content_host_free(void *object) {
  q3_content_host *host = object;
  qa_app_options_destroy(&host->options);
  free(host->catalog_mounts);
  free(host->loaded_mounts);
  free(host);
}
/* An adopted host has two owners: the Q3ClientContent and the loaded content it was adopted with. */
void qa_remote_q3_content_host_release(void *object) {
  q3_content_host *host = object;
  if (host == NULL) return;
  if (!host->adopted || host->released) q3_content_host_free(host);
  else host->released = true;
}
static void q3_content_dead(void *object) {
  q3_content_host *host = object;
  host->content_alive = false;
  qa_remote_q3_content_host_release(host);
}
typedef struct q3_world_state {
  qa_remote_application *app; qa_q3_client_connection *connection; int64_t generation; double content_generation;
  const qa_app_q1_remote_world_info *world;
  qa_app_q3_client_content *prepared;
  qa_app_loaded_content *content;
} q3_world_state;
static void q3_world_assert(q3_world_state *state) {
  qa_remote_application *app = state->app;
  if (app->closed || app->closing || state->generation != qa_q3_client_connection_generation(state->connection)
      || state->content_generation != app->remote_content_generation)
    qa_raise("Error", 0, 0, "Remote Q3 content loading was cancelled");
}
static void q3_world_body(void *ctx) {
  q3_world_state *state = ctx;
  q3_world_assert(state);
  state->content = qa_remote_application_load_server_world(state->app, state->world, NULL, 0,
      qa_app_q3_client_content_loaded(state->prepared), false);
  q3_world_assert(state);
}
static const char *recorded_q3_assert(void *ctx) {
  qa_remote_application *app = ctx;
  return app->closed || app->closing ? "Recorded Q3 source was retired" : NULL;
}
qa_app_loaded_content *qa_remote_application_load_q3_server_world(qa_remote_application *app,
    const qa_app_q1_remote_world_info *world, qa_q3_client_connection *connection) {
  bool recorded = qa_remote_application_source(app)->kind == QA_REMOTE_SOURCE_RECORDED;
  char *info = game_state_text(connection, 1);
  qa_cleanup info_guard;
  qa_cleanup_push(&info_guard, free, info);
  if (recorded) {
    char *game = qa_q3_info_value(info, "fs_game");
    if (game == NULL) qa_range_error("Out of memory");
    qa_remote_content_selection selection;
    bool made = qa_remote_content_selection_make("q3-baseq3", game[0] != '\0' ? game : "baseq3", &selection);
    free(game);
    if (!made) qa_range_error("Out of memory");
    qa_cleanup guard;
    qa_cleanup_push(&guard, (void (*)(void *))qa_remote_content_selection_free, &selection);
    (void)qa_remote_application_select_remote_content(app, &selection, recorded_q3_assert, app);
    qa_cleanup_pop(&guard);
    qa_remote_content_selection_free(&selection);
  }
  q3_world_state state;
  memset(&state, 0, sizeof state);
  state.app = app;
  state.connection = connection;
  state.generation = qa_q3_client_connection_generation(connection);
  state.content_generation = app->remote_content_generation;
  state.world = world;
  qa_app_remote_content_mounts *seed = app->remote_content;
  if (seed == NULL) qa_raise("Error", 0, 0, "Q3 world loading requires its selected content owner");
  q3_world_assert(&state);
  q3_content_host *host = xcalloc(1, sizeof *host);
  host->app = app;
  qa_app_startup_options_copy(&host->options, &app->launch_options);
  char *map_path = qa_app_server_map_path(world->map);
  free(host->options.map);
  host->options.map = map_path;
  host->recorded = recorded;
  host->source.kind = "recorded";
  host->source.family = app->family;
  qa_cleanup host_guard;
  qa_cleanup_push(&host_guard, q3_content_host_free, host);
  const qa_catalog_discovered_product *product = qa_content_catalog_require(seed->catalog, host->options.product);
  size_t catalog_count = 0;
  host->catalog_mounts = qa_app_q3_package_mounts_of(seed->catalog, seed->mounts, &catalog_count);
  qa_app_q3_content_host content_host;
  memset(&content_host, 0, sizeof content_host);
  content_host.ctx = host;
  content_host.product_family = qa_game_family_name(product->expectation.family);
  content_host.product_content_directory = product->expectation.content_directory;
  content_host.catalog_mounts = host->catalog_mounts;
  content_host.catalog_mount_count = catalog_count;
  content_host.load_content = q3c_load;
  content_host.loaded_mounts = q3c_loaded_mounts;
  content_host.collect_opened = q3c_collect;
  content_host.close_content = q3c_close;
  state.prepared = qa_app_q3_client_content_open(content_host, info, (double)qa_q3_client_connection_checksum_feed(connection));
  if (state.prepared == NULL) qa_range_error("Out of memory");
  qa_cleanup_pop(&host_guard);
  qa_cleanup_pop(&info_guard);
  free(info);
  qa_caught_error caught;
  if (!qa_try(q3_world_body, &state, &caught)) {
    /* A failure after loadServerWorld published the prepared content leaves it to the application. */
    if (app->loaded_content != NULL && app->loaded_content == qa_app_q3_client_content_loaded(state.prepared)) {
      host->adopted = true;
      host->content_alive = true;
      host->released = true; /* no keep entry: the Q3 content is its only owner */
    }
    qa_app_q3_client_content_close(state.prepared);
    qa_app_q3_client_content_destroy(state.prepared);
    q3_content_host_free(host);
    qa_rethrow(&caught);
  }
  qa_app_q3_client_content *previous = app->q3_content;
  void *previous_host = app->q3_content_host;
  app->q3_content = state.prepared;
  app->q3_content_host = host;
  host->adopted = true;
  host->content_alive = true;
  qa_app_loaded_content_keep(state.content, host, q3_content_dead);
  if (previous != NULL) {
    qa_app_q3_client_content_close(previous);
    qa_app_q3_client_content_destroy(previous);
    qa_remote_q3_content_host_release(previous_host);
  }
  return state.content;
}

/* ---- loadServerWorld (TS 2128-2240) ------------------------------------ */
typedef struct world_state {
  qa_remote_application *app;
  double generation;
  const qa_app_q1_remote_world_info *world;
  const char *const *images; size_t image_count;
  qa_app_loaded_content *prepared_content;
  bool refresh_content;
  qa_app_options options; /* { ...this.options, map: serverMapPath(world.map) } (owned until published) */
  qa_app_remote_content_mounts *pending;
  char *map;
  bool replace_content;
  qa_app_loaded_content *content;      /* the content to publish */
  qa_remote_world_frontend *previous;
  char *previous_output; bool has_previous_output;
  bool output_detached;
  qa_remote_world_frontend *frontend;
} world_state;
static void world_assert(world_state *state) {
  qa_remote_application *app = state->app;
  if (app->closing || app->closed || state->generation != app->world_load_generation)
    qa_raise("Error", 0, 0, "Remote world loading was cancelled");
}
static void world_assert_body(void *ctx) { world_assert(ctx); }
static void world_frontend_body(void *ctx) {
  world_state *state = ctx;
  qa_remote_application *app = state->app;
  world_assert(state);
  if (state->previous != NULL && !app->ownership.borrowed) {
    save_audio(app->input_config, state->previous->audio);
    world_assert(state);
    qa_app_application_audio_detach_output(state->previous->audio);
    state->output_detached = true;
  }
  state->frontend = qa_remote_application_load_frontend(app, state->content);
  qa_caught_error caught;
  qa_remote_world_frontend *frontend = state->frontend;
  if (!qa_try(world_assert_body, state, &caught)) {
    state->frontend = NULL;
    qa_remote_application_release_frontend(app, frontend);
    qa_rethrow(&caught);
  }
}
static void world_restore_output(void *ctx) {
  world_state *state = ctx;
  qa_app_application_audio_select_output(state->previous->audio, state->has_previous_output ? state->previous_output : NULL);
}
static void world_load_content(world_state *state) {
  qa_remote_application *app = state->app;
  if (state->prepared_content != NULL) { state->content = state->prepared_content; return; }
  if (!state->replace_content) { state->content = qa_remote_application_content(app); return; }
  const qa_remote_source *source = qa_remote_application_source(app);
  bool live = source->kind == QA_REMOTE_SOURCE_LIVE;
  qa_app_content_source presentation = {"recorded", app->family};
  qa_app_options options;
  qa_app_startup_options_copy(&options, &state->options);
  state->content = qa_app_load_application_content_native((qa_app_content_host){0}, options, NULL, false, NULL,
      !live && state->pending != NULL ? state->pending->catalog : NULL, live ? NULL : &presentation);
  if (state->content == NULL) qa_range_error("Out of memory");
}
static void world_body(void *ctx) {
  world_state *state = ctx;
  qa_remote_application *app = state->app;
  world_assert(state);
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
  world_assert(state);
  qa_remote_application_retire_remote_views(app);
  world_assert(state);
  if (app->controls != NULL) qa_app_application_input_stop_haptics(app->controls);
  qa_app_world_seat_presentation *presentation = qa_remote_application_presentation(app);
  if (presentation != NULL) {
    app->has_ui_preferences = true;
    app->ui_preferences = qa_ui_seat_preferences_values(qa_app_seat_ui_preferences(qa_app_world_seat_presentation_ui(presentation)));
  }
  const char *current_map = app->loaded_content != NULL
      ? qa_app_loaded_content_recipe(app->loaded_content)->map.geometry.requested_path : NULL;
  bool different_map = current_map == NULL || strcmp(state->map, current_map) != 0;
  state->replace_content = different_map || state->prepared_content != NULL || state->refresh_content || state->pending != NULL;
  qa_actor_id player;
  if (state->replace_content || presentation != NULL
      || qa_app_remote_seat_presentation_player(qa_remote_application_remote(app), &player)) {
    world_load_content(state);
    state->previous = app->frontend;
    if (state->previous != NULL) {
      const char *selected = qa_app_application_audio_selected_output(state->previous->audio);
      state->has_previous_output = selected != NULL;
      state->previous_output = selected != NULL ? xdup(selected) : NULL;
    }
    qa_caught_error caught;
    if (!qa_try(world_frontend_body, state, &caught)) {
      if (state->replace_content && state->content != NULL) {
        qa_app_loaded_content *content = state->content;
        state->content = NULL;
        qa_app_loaded_content_close(content);
        /* TS closes it; a prepared content is released by the caller that prepared it (loadQ3ServerWorld). */
        if (content != state->prepared_content) qa_app_loaded_content_destroy(content);
      }
      if (state->output_detached && state->previous != NULL && app->frontend == state->previous && !app->closing && !app->closed
          && state->generation == app->world_load_generation) {
        qa_caught_error restore;
        if (!qa_try(world_restore_output, state, &restore))
          qa_raise("AggregateError", 0, 0, "Remote audio preparation and output restoration failed");
      }
      qa_rethrow(&caught);
    }
    qa_app_loaded_content *old_content = app->loaded_content;
    qa_remote_application_clear_source_presentation(app);
    qa_remote_world_frontend *previous = state->previous;
    if (previous != NULL) {
      qa_app_application_audio_set_effects_volume(state->frontend->audio, qa_app_application_audio_effects_volume(previous->audio));
      qa_app_application_audio_set_music_volume(state->frontend->audio, qa_app_application_audio_music_volume(previous->audio));
      if (app->ownership.borrowed) {
        qa_remote_retired_frontend retired = {previous, state->replace_content ? old_content : NULL};
        (void)qa_event_ring_admit(app->retired_frontends, &retired);
      } else qa_remote_application_release_frontend(app, previous);
    }
    app->frontend = state->frontend;
    state->frontend = NULL;
    app->loaded_content = state->content;
    app->source_lifetime.capture_map = qa_app_loaded_content_recipe(state->content)->map.geometry.requested_path;
    qa_app_options_destroy(&app->launch_options);
    app->launch_options = state->options;
    memset(&state->options, 0, sizeof state->options);
    if (state->replace_content && (previous == NULL || !app->ownership.borrowed) && old_content != NULL && old_content != state->content) {
      qa_app_loaded_content_close(old_content);
      qa_app_loaded_content_destroy(old_content);
    }
  } else {
    qa_remote_application_clear_source_presentation(app);
  }
  qa_remote_world_frontend *frontend = app->frontend;
  if (frontend == NULL) qa_raise("Error", 0, 0, "Remote frontend has not loaded");
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(qa_remote_application_content(app));
  qa_content_id entities = recipe->map.entities.content;
  qa_app_provider_scene_assets provider = qa_app_application_assets_provider(frontend->assets, entities);
  world_assert(state);
  const qa_remote_presentation *remote = qa_remote_application_remote(app);
  const qa_app_q1_remote_world_info *world = state->world;
  for (size_t i = 0; i < world->model_count; i++) {
    const char *model = world->models[i];
    if (strcmp(model, world->map) == 0 || model[0] == '#') continue;
    qa_app_model_asset asset = qa_app_application_assets_model(frontend->assets, entities, model);
    world_assert(state);
    qa_remote_presentation_register_resource(remote, entities, model, asset.resource);
  }
  for (size_t i = 0; i < world->sound_count; i++) {
    const char *sound = world->sounds[i];
    if (sound[0] == '*') continue;
    char *path = NULL;
    if (sound[0] == '#') path = xdup(sound + 1);
    else if (remote->family == QA_APP_REMOTE_FAMILY_Q3) path = xdup(sound);
    else {
      size_t length = strlen(sound) + 8;
      path = xcalloc(length, 1);
      snprintf(path, length, "sound/%s", sound);
    }
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, path);
    const qa_resolved_resource_reference *resource = NULL;
    bool resolved = qa_content_mounted_resolve(provider.mounts, path, &resource);
    world_assert(state);
    if (!resolved) print_line(app, "Server sound is unavailable: %s\n", path, NULL, NULL);
    else qa_remote_presentation_register_resource(remote, entities, path, *resource);
    qa_cleanup_pop(&guard);
    free(path);
  }
  for (size_t i = 0; i < state->image_count; i++) {
    const char *image = state->images[i];
    char *path = NULL;
    if (image[0] == '/' || image[0] == '\\') path = xdup(image + 1);
    else {
      size_t length = strlen(image) + 16;
      path = xcalloc(length, 1);
      snprintf(path, length, "pics/%s.pcx", image);
    }
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, path);
    qa_scene_texture_load_options options;
    memset(&options, 0, sizeof options);
    options.has_mipmap = true;
    options.mipmap = false;
    options.wrap = "clamp";
    if (qa_scene_texture_loader_load(provider.textures, path, &options) == NULL)
      print_line(app, "Server image is unavailable: %s\n", path, NULL, NULL);
    qa_cleanup_pop(&guard);
    free(path);
  }
  world_assert(state);
  app->source_event_count = 0;
  print_line(app, "Loaded remote world %s.\n", state->map, NULL, NULL);
  if (app->remote_content == state->pending) {
    if (state->pending != NULL) qa_app_remote_content_mounts_close(state->pending);
    app->remote_content = NULL;
  }
}
qa_app_loaded_content *qa_remote_application_load_server_world(qa_remote_application *app,
    const qa_app_q1_remote_world_info *world, const char *const *images, size_t image_count,
    qa_app_loaded_content *prepared_content, bool refresh_content) {
  world_state state;
  memset(&state, 0, sizeof state);
  state.app = app;
  state.generation = ++app->world_load_generation;
  state.world = world;
  state.images = images;
  state.image_count = image_count;
  state.prepared_content = prepared_content;
  state.refresh_content = refresh_content;
  qa_remote_options_copy(&app->launch_options, &state.options);
  state.map = qa_app_server_map_path(world->map);
  if (state.map == NULL) { qa_app_options_destroy(&state.options); qa_range_error("Out of memory"); }
  free(state.options.map);
  state.options.map = xdup(state.map);
  state.pending = app->remote_content;
  qa_caught_error caught;
  bool loaded = qa_try(world_body, &state, &caught);
  qa_app_options_destroy(&state.options);
  free(state.map);
  free(state.previous_output);
  if (!loaded) qa_rethrow(&caught);
  return qa_remote_application_content(app);
}

/* ---- unified content (TS loadUnifiedContent and adoptUnifiedProfile's mounted content) --- */
qa_app_loaded_content *qa_remote_application_load_unified_content(qa_remote_application *app,
    const qa_app_unified_control *offer) {
  return qa_remote_load_unified_content(app, offer);
}
