/* Port of src/app/bootstrap/startup.ts: preference persistence
 * (saveFrontendInput, persistPreferences, persistPreferencesOrWarn,
 * refreshPreferenceBaseline), stageLobbyLaunch, launch, connect, connectGtv,
 * connectOptions, publishPendingSource, publishDemoIntent,
 * dispatchApplicationRequest, step, run and close. TS awaits are synchronous
 * calls; a TS throw is a qa_raise. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "startup_application_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "qa/app/bootstrap/demo_library.h"
#include "qa/app/bootstrap/demo_playback.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/loading.h"
#include "qa/app/bootstrap/original_save.h"
#include "qa/app/bootstrap/q3_map_command.h"
#include "qa/app/bootstrap/simulation/save.h"
#include "qa/content/user_data.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/network/common/js_number.h"
#include "qa/platform/controller.h"
#include "qa/platform/sdl.h"

static bool eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static void raise_text(const char *error) { if (error != NULL) qa_raise("Error", 0, 0, "%s", error); }
static bool is_publication_error(const qa_caught_error *caught) {
  return caught->name != NULL && strcmp(caught->name, "ClientSourcePublicationError") == 0;
}
static void *grow(void *items, size_t count, size_t size) {
  void *grown = realloc(items, (count + 1) * size);
  if (grown == NULL) qa_range_error("Out of memory");
  return grown;
}

/* Launch records: the options and save images the open applications borrow. */
typedef enum launch_kind { LAUNCH_SELECTION, LAUNCH_OPTIONS, LAUNCH_SAVE } launch_kind;
typedef struct launch_record {
  launch_kind kind;
  qa_app_startup_launch launch;            /* LAUNCH_SELECTION */
  qa_app_options options;                   /* LAUNCH_OPTIONS and the opened copy of every kind */
  void *q3_map_launch;                      /* LAUNCH_OPTIONS: backs options.q3_map_launch */
  qa_app_prepared_application_save save; void *save_backing; /* LAUNCH_SAVE */
} launch_record;
static launch_record *keep_launch(qa_startup_application *app, launch_kind kind) {
  launch_record *record = calloc(1, sizeof *record);
  if (record == NULL) qa_range_error("Out of memory");
  record->kind = kind;
  app->launches = grow(app->launches, app->launch_count, sizeof *app->launches);
  app->launches[app->launch_count++] = record;
  return record;
}
void qa_startup_launches_free(qa_startup_application *app) {
  for (size_t i = 0; i < app->launch_count; i++) {
    launch_record *record = app->launches[i];
    if (record->kind == LAUNCH_SELECTION) qa_app_startup_launch_free(&record->launch);
    else if (record->kind == LAUNCH_SAVE) qa_app_prepared_application_save_free(&record->save, record->save_backing);
    else qa_app_options_destroy(&record->options);
    free(record->q3_map_launch);
    free(record);
  }
  free(app->launches);
  app->launches = NULL;
  app->launch_count = 0;
}
static void keep_game(qa_startup_application *app, qa_application *game) {
  app->games = grow(app->games, app->game_count, sizeof *app->games);
  app->games[app->game_count++] = game;
}
static void keep_remote(qa_startup_application *app, qa_remote_application *remote) {
  app->remotes = grow(app->remotes, app->remote_count, sizeof *app->remotes);
  app->remotes[app->remote_count++] = remote;
}

/* ---- preference persistence ------------------------------------------------- */
static const char *const *frontend_history(qa_startup_application *app, size_t *out_count, bool *has) {
  *out_count = 0;
  *has = false;
  qa_app_client_bootstrap *client = app->client;
  if (client == NULL || client->local_count == 0) return NULL;
  for (size_t i = 0; i < client->console_count; i++)
    if (client->consoles[i].seat == client->locals[0].seat) {
      *has = true;
      return qa_console_history_lines(&client->consoles[i].console->history, out_count);
    }
  return NULL;
}
static qa_settings_controller_selection settings_selection(qa_controller_selection selection) {
  qa_settings_controller_selection out;
  memset(&out, 0, sizeof out);
  out.kind = selection.kind == QA_CONTROLLER_SELECTION_AUTOMATIC ? QA_CONTROLLER_AUTOMATIC
      : selection.kind == QA_CONTROLLER_SELECTION_NONE ? QA_CONTROLLER_NONE
      : selection.kind == QA_CONTROLLER_SELECTION_DEVICE ? QA_CONTROLLER_DEVICE : QA_CONTROLLER_SERIAL;
  memcpy(out.guid, selection.guid, sizeof out.guid);
  out.ordinal = selection.ordinal;
  out.serial = selection.serial != NULL ? qa_startup_dup(selection.serial) : NULL;
  return out;
}
void qa_startup_save_frontend_input(qa_startup_application *app) {
  qa_startup_graphics *graphics = app->graphics;
  qa_app_client_bootstrap *client = app->client;
  if (graphics == NULL) return;
  qa_app_frontend_preference_overrides values = qa_app_frontend_preferences_effective_values(app->preferences);
  size_t history_count = 0;
  bool has_history = false;
  const char *const *history = frontend_history(app, &history_count, &has_history);
  qa_app_startup_input_profile_save(graphics->input_profile, &values, history, history_count, has_history);
  free((void *)history);
  if (client == NULL || client->platform.kind != QA_APP_CLIENT_INPUT_MENU) return;
  const qa_config_store *settings = app->preference_store != NULL ? app->preference_store : client->settings;
  qa_input_router *router = client->platform.router;
  char *routing = qa_startup_frontend_routing_state(router);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, routing);
  if (app->frontend_routing_baseline != NULL && strcmp(routing, app->frontend_routing_baseline) == 0) {
    qa_cleanup_pop(&guard);
    free(routing);
    return;
  }
  size_t input_count = 0;
  qa_seat_input **inputs = qa_input_router_inputs(router, &input_count);
  size_t seat_count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(client->prepared, &seat_count);
  for (size_t i = 0; i < input_count; i++) {
    qa_seat_id seat = qa_seat_input_seat(inputs[i]);
    const qa_app_prepared_seat *prepared = NULL;
    for (size_t s = 0; s < seat_count && prepared == NULL; s++) if (qa_same_seat(seats[s].id, seat)) prepared = &seats[s];
    if (prepared == NULL) qa_raise("Error", 0, 0, "Frontend routing refers to a retired seat");
    char path[64];
    snprintf(path, sizeof path, "input/seat-%lld.json", (long long)seat.index + 1);
    qa_seat_settings saved;
    memset(&saved, 0, sizeof saved);
    qa_settings_controller_selection controller = settings_selection(qa_input_router_controller_selection(router, seat));
    bool stored;
    if (qa_config_store_load_seat(settings, path, &saved)) {
      free(saved.controller.serial);
      saved.controller = controller;
      stored = qa_config_store_save_seat(settings, path, &saved);
      qa_seat_settings_free(&saved);
    } else {
      size_t binding_count = 0;
      const qa_input_binding *bindings = qa_seat_input_bindings(inputs[i], &binding_count);
      size_t lines = 0;
      bool has_lines = false;
      const char *const *recent = frontend_history(app, &lines, &has_lines);
      qa_seat_settings fresh;
      memset(&fresh, 0, sizeof fresh);
      fresh.version = 1;
      fresh.bindings = (qa_input_binding *)bindings;
      fresh.binding_count = binding_count;
      fresh.gamepad = qa_seat_input_gamepad(inputs[i])->tuning;
      fresh.mouse = qa_mouse_settings_read(&prepared->mouse);
      fresh.history = (char **)recent;
      fresh.history_count = has_lines ? lines : 0;
      fresh.rumble = true;
      fresh.rumble_strength = 1;
      fresh.controller = controller;
      stored = qa_config_store_save_seat(settings, path, &fresh);
      free((void *)recent);
      free(controller.serial);
    }
    if (!stored) qa_raise("Error", 0, 0, "Could not write %s", path);
  }
  qa_seat_id keyboard;
  bool has_keyboard = qa_input_router_keyboard_seat(router, &keyboard);
  if (!qa_config_store_save_input_routing(settings, "input/routing.json", has_keyboard ? (double)keyboard.index : 0, has_keyboard))
    qa_raise("Error", 0, 0, "Could not write input/routing.json");
  qa_cleanup_pop(&guard);
  free(app->frontend_routing_baseline);
  app->frontend_routing_baseline = routing;
}
static void persist_preferences(qa_startup_application *app) {
  qa_startup_save_frontend_input(app);
  if (app->preference_store != NULL) qa_app_frontend_preferences_save_audio_baseline(app->preferences, app->preference_store);
  qa_app_frontend_preferences_save_global(app->preferences);
}
static void persist_body(void *ctx) { persist_preferences(ctx); }
/* A settings write failure (full disk, read-only folder) is reported; it does not end the application. */
void qa_startup_persist_preferences_or_warn(qa_startup_application *app) {
  qa_caught_error caught;
  if (!qa_try(persist_body, app, &caught)) qa_startup_printf(app, "Could not save settings: %s\n", caught.message);
}
void qa_startup_refresh_preference_baseline(qa_startup_application *app, bool force) {
  qa_app_options options;
  qa_app_startup_selection_options(app->model, &options);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_app_options_destroy, &options);
  const char *dialect = qa_app_configuration_dialect_name(qa_app_movement_dialect(&options, NULL));
  const char *items = qa_app_startup_selection_binding_items_key(app->model);
  size_t length = strlen(dialect) + strlen(items) + 2;
  char *input_key = malloc(length);
  if (input_key == NULL) qa_range_error("Out of memory");
  snprintf(input_key, length, "%s|%s", dialect, items);
  qa_cleanup key_guard;
  qa_cleanup_push(&key_guard, free, input_key);
  if (!force && app->baseline_product != NULL && eq(app->baseline_product, options.product) && eq(app->baseline_input, input_key)) {
    qa_cleanup_pop(&key_guard);
    free(input_key);
    qa_cleanup_pop(&guard);
    qa_app_options_destroy(&options);
    return;
  }
  /* Never persist before the first baseline load: writing the (default)
   * global tuning first would clobber the per-product seat profile being read. */
  if (app->baseline_product != NULL) qa_startup_persist_preferences_or_warn(app);
  /* this.model.catalog.product(options.product): a missing product still names its settings root. */
  const qa_catalog_discovered_product *product = qa_content_catalog_product(qa_app_startup_selection_catalog(app->model), options.product);
  char *root;
  if (product->user_content != NULL) root = qa_startup_dup(product->user_content->root);
  else {
    char *base = options.user_content_root != NULL ? qa_startup_dup(options.user_content_root) : qa_default_user_content_root();
    if (base == NULL) qa_range_error("Out of memory");
    root = qa_user_product_directory(base, product->expectation.content_directory);
    free(base);
  }
  if (root == NULL) qa_range_error("Out of memory");
  qa_config_store settings;
  bool stored = qa_config_store_init(&settings, root);
  free(root);
  if (!stored) qa_range_error("Out of memory");
  /* await this.preferences.loadBaseline(settings); this.preferenceStore = settings; */
  qa_cleanup store_guard;
  qa_cleanup_push(&store_guard, (void (*)(void *))qa_config_store_destroy, &settings);
  qa_app_frontend_preferences_load_baseline(app->preferences, &settings);
  qa_cleanup_pop(&store_guard);
  if (app->preference_store != NULL) qa_config_store_destroy(&app->preference_store_storage);
  app->preference_store_storage = settings;
  app->preference_store = &app->preference_store_storage;
  qa_startup_set_text(&app->baseline_product, options.product);
  qa_startup_set_text(&app->baseline_input, input_key);
  if (app->graphics != NULL) {
    qa_app_startup_input_profile *profile = qa_app_startup_input_profile_retained(app->preference_store,
        qa_app_startup_input_profile_input(app->graphics->input_profile));
    qa_app_startup_input_profile_destroy(app->graphics->input_profile);
    app->graphics->input_profile = profile;
  }
  qa_cleanup_pop(&key_guard);
  free(input_key);
  qa_cleanup_pop(&guard);
  qa_app_options_destroy(&options);
}

/* ---- stageLobbyLaunch(action) --------------------------------------------------- */
/* TS stages the action as lobbyTransition and resolves it when publishPendingSource
 * runs it; the C lobby transitions are synchronous, so the launch runs here with the
 * transition marked pending (client.hasPendingSource sees it). */
typedef struct stage_call { qa_startup_application *app; qa_app_startup_action *action; } stage_call;
static void stage_body(void *ctx) {
  stage_call *call = ctx;
  qa_startup_launch(call->app, call->action, true);
}
const char *qa_startup_stage_lobby_launch(qa_startup_application *app, qa_app_startup_action *action) {
  static char message[sizeof ((qa_caught_error *)0)->message];
  if (app->closed || app->stopping || app->lobby_transition) return "The client cannot start another lobby transition";
  app->lobby_transition = true;
  stage_call call = {app, action};
  qa_caught_error caught;
  bool launched = qa_try(stage_body, &call, &caught);
  app->lobby_transition = false;
  if (launched) return NULL;
  snprintf(message, sizeof message, "%s", caught.message);
  return message;
}

/* ---- connectOptions(options) ----------------------------------------------------- */
static void remote_print(void *ctx, const char *text) {
  qa_startup_application *app = ctx;
  qa_startup_application_print(app, text);
  /* message = text.trim(); if (message !== "") this.status = message.slice(-512) */
  const char *start = text;
  while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r' || *start == '\v' || *start == '\f') start++;
  size_t length = strlen(start);
  while (length > 0 && (start[length - 1] == ' ' || start[length - 1] == '\t' || start[length - 1] == '\n' || start[length - 1] == '\r'
      || start[length - 1] == '\v' || start[length - 1] == '\f'))
    length--;
  if (length == 0) return;
  if (length > 512) { start += length - 512; length = 512; }
  char *status = malloc(length + 1);
  if (status == NULL) qa_range_error("Out of memory");
  memcpy(status, start, length);
  status[length] = '\0';
  free(app->status);
  app->status = status;
}
static void plain_print(void *ctx, const char *text) { qa_startup_application_print(ctx, text); }
static const char *display_renderer(qa_startup_application *app, const char *fallback) {
  return app->graphics != NULL && app->graphics->display_renderer != NULL ? app->graphics->display_renderer : fallback;
}
void qa_startup_connect_options(qa_startup_application *app, const qa_app_options *options) {
  qa_app_client_bootstrap *client = app->client;
  if (client == NULL) qa_raise("Error", 0, 0, "Startup has no retained client");
  if (app->browser == NULL) qa_raise("Error", 0, 0, "Startup browser is unavailable");
  launch_record *record = keep_launch(app, LAUNCH_OPTIONS);
  qa_app_startup_options_copy(&record->options, options);
  record->options.renderer = display_renderer(app, options->renderer);
  qa_remote_application_host host;
  memset(&host, 0, sizeof host);
  host.base = app->host.base;
  host.base.save_directory = qa_app_startup_saves_directory(app->saves);
  host.base.ctx = app;
  host.base.print = remote_print;
  host.server_browser = app->browser;
  qa_remote_application *remote = qa_remote_application_open_borrowed(client, &record->options, &host);
  if (remote == NULL) qa_range_error("Out of memory");
  keep_remote(app, remote);
  app->remote = remote;
  app->game = NULL;
  app->has_active_demo = false;
  if (app->demos != NULL) {
    qa_app_client_demo_commands_manual_game(app->demos);
    qa_app_client_demo_commands_refresh(app->demos);
  }
  app->last_frame = qa_startup_now();
}

/* ---- connect(connection) ------------------------------------------------------ */
static void connect_browser(qa_startup_application *app, const qa_app_browser_connection *connection) {
  qa_app_options options;
  qa_startup_application_connection_options(app, connection, &options);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_app_options_destroy, &options);
  qa_startup_connect_options(app, &options);
  qa_cleanup_pop(&guard);
  qa_app_options_destroy(&options);
}

/* ---- connectGtv(request) --------------------------------------------------------- */
typedef struct gtv_state {
  qa_startup_application *app;
  qa_app_client_bootstrap *client;
  const qa_app_startup_action *request;
  const qa_app_client_source_lifetime *previous;
} gtv_state;
static void gtv_operation(void *ctx, qa_app_loading_frame *frame) {
  (void)frame;
  gtv_state *state = ctx;
  qa_startup_application *app = state->app;
  qa_net_address address;
  memset(&address, 0, sizeof address);
  if (!qa_net_resolve_address(&address, state->request->gtv_address, 27910, 0)) qa_range_error("Out of memory");
  qa_cleanup address_guard;
  qa_cleanup_push(&address_guard, (void (*)(void *))qa_net_address_destroy, &address);
  if (app->gtv_aborted) qa_raise("Error", 0, 0, "GTV connection canceled");
  launch_record *record = keep_launch(app, LAUNCH_OPTIONS);
  qa_app_startup_selection_options(app->model, &record->options);
  record->options.renderer = display_renderer(app, record->options.renderer);
  qa_remote_application_host host;
  memset(&host, 0, sizeof host);
  host.base = app->host.base;
  host.base.save_directory = qa_app_startup_saves_directory(app->saves);
  host.base.ctx = app;
  host.base.print = plain_print;
  host.server_browser = app->browser;
  char *host_text = qa_net_address_host(&address);
  if (host_text == NULL) qa_range_error("Out of memory");
  qa_cleanup host_guard;
  qa_cleanup_push(&host_guard, free, host_text);
  qa_app_gtv_source_options request;
  memset(&request, 0, sizeof request);
  request.host = host_text;
  request.port = address.port;
  request.identity.username = state->request->gtv_username;
  request.identity.password = state->request->gtv_password;
  request.identity.version = "quake-typescript";
  request.aborted = &app->gtv_aborted;
  qa_remote_application *remote = qa_remote_application_open_gtv_borrowed(state->client, &record->options, &host, request);
  if (remote == NULL) qa_range_error("Out of memory");
  keep_remote(app, remote);
  qa_cleanup_pop(&host_guard);
  free(host_text);
  qa_cleanup_pop(&address_guard);
  qa_net_address_destroy(&address);
  if (app->closed || app->stopping || app->gtv_aborted) {
    qa_remote_application_close(remote);
    return;
  }
  double id = app->gtv_id++;
  char label[64];
  if (state->request->gtv_label == NULL) snprintf(label, sizeof label, "net%s", QA_JS_NUMBER(id));
  app->gtv_source = remote;
  app->gtv_id_value = id;
  qa_startup_set_text(&app->gtv_label, state->request->gtv_label != NULL ? state->request->gtv_label : label);
  app->remote = remote;
  app->game = NULL;
  app->has_active_demo = false;
  if (app->demos != NULL) {
    qa_app_client_demo_commands_manual_game(app->demos);
    qa_app_client_demo_commands_refresh(app->demos);
  }
  app->last_frame = qa_startup_now();
  qa_startup_printf(app, "GTV %s: %s (%s)\n", QA_JS_NUMBER(id), app->gtv_label, state->request->gtv_address);
}
static bool window_quit_event(const qa_sdl_event *event) {
  return event->kind == QA_SDL_EVENT_QUIT || (event->kind == QA_SDL_EVENT_WINDOW && event->as.window.event == 14);
}
typedef struct gtv_service_call { gtv_state *state; } gtv_service_call;
static void gtv_service_body(void *ctx) {
  gtv_state *state = ctx;
  qa_startup_application *app = state->app;
  qa_startup_graphics *graphics = app->graphics;
  size_t count = 0;
  qa_sdl_event *events = qa_sdl_window_poll_events(qa_app_native_renderer_window(graphics->renderer), &count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, events);
  for (size_t i = 0; i < count; i++) if (window_quit_event(&events[i])) qa_startup_application_request_quit(app);
  qa_cleanup_pop(&guard);
  free(events);
  if (!app->stopping && state->client->source_current == state->previous) qa_startup_graphics_draw(app, state->previous, true);
}
static void gtv_service(void *ctx) {
  gtv_state *state = ctx;
  qa_startup_application *app = state->app;
  if (app->closed || app->stopping || app->graphics == NULL) {
    app->gtv_aborted = true;
    return;
  }
  qa_caught_error caught;
  if (!qa_try(gtv_service_body, state, &caught)) {
    app->gtv_aborted = true;
    qa_rethrow(&caught);
  }
}
static void gtv_finally(void *ctx) {
  qa_startup_application *app = ctx;
  app->gtv_abort_active = false;
  app->gtv_preparation = false;
}
static void connect_gtv(qa_startup_application *app, const qa_app_startup_action *request) {
  qa_app_client_bootstrap *client = app->client;
  if (client == NULL || app->browser == NULL || app->closed || app->stopping) qa_raise("Error", 0, 0, "GTV client is unavailable");
  app->gtv_abort_active = true;
  app->gtv_aborted = false;
  gtv_state state = {app, client, request, client->source_current};
  app->gtv_preparation = true;
  qa_cleanup guard;
  qa_cleanup_push(&guard, gtv_finally, app);
  qa_app_service_loading(gtv_operation, &state, gtv_service, &state);
  qa_cleanup_pop(&guard);
  gtv_finally(app);
}

/* ---- launch(action, propagateFailure) ---------------------------------------------- */
typedef struct launch_state {
  qa_startup_application *app;
  qa_app_startup_action *action;
  qa_startup_graphics *loading;
  qa_app_client_bootstrap *client;
  const qa_app_client_source_lifetime *previous;
  qa_app_loading_frame *frame;
  qa_application *game;
  qa_application_lobby lobby;
  qa_application_loading host_loading;
} launch_state;
static void lobby_complete(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->lobby_source != NULL && app->client != NULL && app->client->source_current == app->lobby_source)
    raise_text(qa_app_application_local_lobby_complete(&app->lobby));
}
static void lobby_return(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->lobby_source == NULL || app->client == NULL || app->client->source_current != app->lobby_source) return;
  raise_text(qa_app_application_local_lobby_complete(&app->lobby));
  qa_app_startup_action_clear(&app->pending);
  app->pending.kind = QA_APP_STARTUP_ACTION_FRONTEND;
  app->has_pending = true;
}
static void loading_next_frame(void *ctx) {
  launch_state *state = ctx;
  qa_app_loading_next_frame(state->frame);
  if (state->app->stopping) qa_raise("Error", 0, 0, "Startup cancelled");
}
static void loading_stage(void *ctx, const char *message) {
  launch_state *state = ctx;
  if (state->loading != NULL) qa_app_startup_menu_set_status(state->loading->menu, message, true);
}
static bool file_exists(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}
/* The load action: the save image and the options it restores. */
static launch_record *prepare_load(qa_startup_application *app, const qa_app_startup_action *action) {
  char *load_path = qa_startup_dup(action->path);
  qa_cleanup path_guard;
  qa_cleanup_push(&path_guard, free, load_path);
  if (!file_exists(load_path)) {
    /* loadPath.split("/").at(-1)?.replace(/\.sav$/i, "") */
    const char *slash = strrchr(load_path, '/');
    const char *base = slash != NULL ? slash + 1 : load_path;
    size_t length = strlen(base);
    char *name = malloc(length + 1);
    if (name == NULL) qa_range_error("Out of memory");
    memcpy(name, base, length + 1);
    if (length >= 4 && name[length - 4] == '.' && (name[length - 3] | 32) == 's' && (name[length - 2] | 32) == 'a'
        && (name[length - 1] | 32) == 'v')
      name[length - 4] = '\0';
    char *resolved = qa_app_resolve_external_save_path((const char *const *)app->external_directories, app->external_directory_count, name);
    free(name);
    if (resolved != NULL) {
      free(load_path);
      load_path = resolved;
      qa_cleanup_pop(&path_guard);
      qa_cleanup_push(&path_guard, free, load_path);
    }
  }
  qa_app_options model_options;
  qa_app_startup_selection_options(app->model, &model_options);
  qa_cleanup options_guard;
  qa_cleanup_push(&options_guard, (void (*)(void *))qa_app_options_destroy, &model_options);
  launch_record *record = keep_launch(app, LAUNCH_SAVE);
  qa_app_prepare_application_save_native(&model_options, qa_app_startup_selection_catalog(app->model), load_path,
      action->source_product, &record->save, &record->save_backing);
  qa_cleanup_pop(&options_guard);
  qa_app_options_destroy(&model_options);
  qa_cleanup_pop(&path_guard);
  free(load_path);
  qa_app_sim_saved_settings settings = qa_app_sim_saved_simulation_settings(record->save.image);
  qa_app_sim_saved_bots *bots = qa_app_sim_saved_bot_checkpoint(record->save.image);
  double seats = 0;
  for (size_t i = 0; i < settings.client_slot_count; i++) {
    bool bot = false;
    for (size_t c = 0; bots != NULL && c < bots->connection_count && !bot; c++) bot = bots->connections[c].client_slot == settings.client_slots[i];
    if (!bot) seats++;
  }
  if (bots != NULL) { qa_save_value_free(bots->value); free(bots->connections); free(bots); }
  double skill = settings.skill, seed = settings.seed;
  const char *mode = settings.mode;
  record->save.options.has_bot_skill = false;
  record->save.options.skill = skill;
  record->save.options.mode = mode;
  record->save.options.seed = seed;
  record->save.options.seats = seats;
  free(settings.client_slots);
  /* mode is a static TS literal; the settings tree is released after it is read. */
  qa_save_value_free(settings.value);
  if (seats < 1 || seats > 4) qa_raise("Error", 0, 0, "This saved game requires between 1 and 4 local players.");
  return record;
}
static void launch_operation(void *ctx, qa_app_loading_frame *frame) {
  launch_state *state = ctx;
  qa_startup_application *app = state->app;
  qa_app_startup_action *action = state->action;
  state->frame = frame;
  if (state->loading != NULL) qa_app_startup_menu_set_status(state->loading->menu, "Loading map...", true);
  const qa_app_options *options;
  const qa_executable_recipe *recipe = NULL;
  const qa_save_image *image = NULL;
  launch_record *record;
  if (action->kind == QA_APP_STARTUP_ACTION_LOBBY_HOST) {
    options = &action->selection->options;
    recipe = &action->selection->recipe;
  } else if (action->kind == QA_APP_STARTUP_ACTION_INITIAL) {
    record = keep_launch(app, LAUNCH_OPTIONS);
    qa_app_startup_options_copy(&record->options, &action->options);
    if (action->q3_map_launch != NULL) {
      /* The record keeps its own copy of the Q3 map launch the options point at. */
      record->q3_map_launch = malloc(sizeof (qa_app_q3_map_launch));
      if (record->q3_map_launch == NULL) qa_range_error("Out of memory");
      memcpy(record->q3_map_launch, action->q3_map_launch, sizeof (qa_app_q3_map_launch));
      record->options.q3_map_launch = record->q3_map_launch;
    }
    options = &record->options;
  } else if (action->kind == QA_APP_STARTUP_ACTION_PRESET || action->kind == QA_APP_STARTUP_ACTION_PLAY) {
    record = keep_launch(app, LAUNCH_SELECTION);
    if (action->kind == QA_APP_STARTUP_ACTION_PRESET)
      qa_app_startup_selection_resolve_preset(app->model, action->preset_id, true, action->skill, action->arena_map, &record->launch);
    else qa_app_startup_selection_resolve(app->model, &record->launch);
    options = &record->launch.options;
    recipe = &record->launch.recipe;
  } else {
    record = prepare_load(app, action);
    options = &record->save.options;
    recipe = &record->save.image.recipe;
    image = &record->save.image;
  }
  if (state->loading != NULL) qa_app_startup_menu_set_status(state->loading->menu, "Preparing world...", true);
  qa_app_options opened = *options;
  opened.renderer = state->loading != NULL && state->loading->display_renderer != NULL ? state->loading->display_renderer : options->renderer;
  qa_application_host host = app->host.base;
  if (action->kind == QA_APP_STARTUP_ACTION_LOBBY_HOST) {
    state->lobby.ctx = app;
    state->lobby.complete = lobby_complete;
    state->lobby.return_to_lobby = lobby_return;
    state->lobby.account = &app->lobby_account;
    host.lobby = &state->lobby;
  }
  host.local_rankings = app->match_rankings;
  host.save_directory = qa_app_startup_saves_directory(app->saves);
  host.external_save_directories = (const char *const *)app->external_directories;
  host.external_save_directory_count = app->external_directory_count;
  host.has_external_save_directories = true;
  state->host_loading.ctx = state;
  state->host_loading.defer_window_visibility = true;
  state->host_loading.next_frame = loading_next_frame;
  state->host_loading.stage = loading_stage;
  host.loading = &state->host_loading;
  qa_app_frontend_preference_overrides preferences = *qa_app_frontend_preferences_values(app->preferences);
  qa_application *game = qa_application_open_borrowed(state->client, opened, host, recipe, &preferences, image);
  if (game == NULL) qa_range_error("Out of memory");
  keep_game(app, game);
  app->game = game;
  app->remote = NULL;
  qa_application_flush_client_commands(game);
  app->last_frame = qa_startup_now();
  state->game = game;
}
static void launch_service(void *ctx) {
  launch_state *state = ctx;
  qa_startup_graphics *loading = state->loading;
  if (loading == NULL) return;
  qa_sdl_window *window = qa_app_native_renderer_window(loading->renderer);
  size_t count = 0;
  qa_sdl_event *events = qa_sdl_window_poll_events(window, &count);
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, events);
  for (size_t i = 0; i < count; i++)
    if (window_quit_event(&events[i])) {
      qa_startup_application_request_quit(state->app);
      qa_sdl_window_set_visible(window, false);
    }
  qa_cleanup_pop(&guard);
  free(events);
  qa_startup_graphics_draw(state->app, state->previous, true);
}
typedef struct launch_call { launch_state *state; } launch_call;
static void launch_body(void *ctx) {
  launch_state *state = ctx;
  qa_startup_application *app = state->app;
  qa_app_startup_action *action = state->action;
  if (action->kind == QA_APP_STARTUP_ACTION_GTV) { connect_gtv(app, action); return; }
  if (action->kind == QA_APP_STARTUP_ACTION_FRONTEND) { qa_startup_return_to_frontend(app); return; }
  if (action->kind == QA_APP_STARTUP_ACTION_CONNECT) { connect_browser(app, &action->connection); return; }
  if (action->kind == QA_APP_STARTUP_ACTION_INITIAL
      && (eq(action->options.network_kind, "q1-client") || eq(action->options.network_kind, "qw-client")
          || eq(action->options.network_kind, "q2-client") || eq(action->options.network_kind, "q3-client")
          || eq(action->options.network_kind, "unified-client"))) {
    qa_startup_connect_options(app, &action->options);
    return;
  }
  state->loading = app->graphics;
  state->client = app->client;
  if (state->client == NULL) qa_raise("Error", 0, 0, "Startup has no retained client");
  qa_app_service_loading(launch_operation, state, launch_service, state);
  qa_application *game = state->game;
  if (app->game != game) return;
  if (!app->stopping) {
    qa_sdl_window *window = qa_application_window(game);
    if (window != NULL) qa_sdl_window_set_visible(window, !qa_application_options(game)->hidden);
  }
  app->has_active_demo = false;
  if (app->demos != NULL) {
    qa_app_client_demo_commands_manual_game(app->demos);
    qa_app_client_demo_commands_refresh(app->demos);
  }
  qa_startup_set_text(&app->status, "");
}
void qa_startup_launch(qa_startup_application *app, qa_app_startup_action *action, bool propagate_failure) {
  if (!propagate_failure && action->kind != QA_APP_STARTUP_ACTION_FRONTEND && qa_app_application_local_lobby_current(&app->lobby) != NULL) {
    raise_text(qa_app_application_local_lobby_leave(&app->lobby));
    if (app->has_pending && app->pending.kind == QA_APP_STARTUP_ACTION_FRONTEND) {
      qa_app_startup_action_clear(&app->pending);
      app->has_pending = false;
    }
  }
  qa_app_addon_library_suspend(app->addons);
  qa_startup_close_frontend_movie(app);
  const qa_app_client_source_lifetime *previous = qa_startup_current_source(app);
  if (app->game != NULL) *qa_app_frontend_preferences_values(app->preferences) = qa_application_frontend_settings(app->game);
  qa_startup_persist_preferences_or_warn(app);
  if (app->graphics != NULL) {
    qa_app_startup_menu_set_status(app->graphics->menu, "Loading...", true);
    qa_startup_graphics_draw(app, previous, true);
  }
  launch_state state;
  memset(&state, 0, sizeof state);
  state.app = app;
  state.action = action;
  state.previous = previous;
  qa_caught_error caught;
  if (qa_try(launch_body, &state, &caught)) return;
  app->last_frame = qa_startup_now();
  qa_startup_set_text(&app->status, caught.message);
  qa_startup_printf(app, "%s\n", app->status);
  if (app->graphics != NULL) qa_app_startup_menu_set_status(app->graphics->menu, app->status, false);
  if (is_publication_error(&caught) || qa_startup_current_source(app) != previous) {
    app->stopping = true;
    qa_rethrow(&caught);
  }
  if (propagate_failure) qa_rethrow(&caught);
  /* The borrowed launch may already have taken game focus (and relative
   * mouse capture) for its seats; hand the frontend its menu focus back. */
  if (app->graphics != NULL && app->client != NULL) qa_startup_activate_frontend(app, NULL);
}

/* ---- publishDemoIntent() ------------------------------------------------------- */
static bool demo_read(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count) {
  qa_startup_application *app = ctx;
  const char *error = NULL;
  bool found = qa_app_demo_library_read(qa_startup_current_demo_library(app), path, out_bytes, out_count, &error);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  return found;
}
typedef struct demo_complete { qa_startup_application *app; const void *token; } demo_complete;
static void demo_completed(void *ctx, const char *reason) {
  demo_complete *complete = ctx;
  if (complete->app->demos != NULL) qa_app_client_demo_commands_complete(complete->app->demos, complete->token, reason);
}
typedef struct demo_call { qa_startup_application *app; qa_app_client_demo_intent *intent; } demo_call;
static void demo_body(void *ctx) {
  demo_call *call = ctx;
  qa_startup_application *app = call->app;
  qa_app_client_bootstrap *client = app->client;
  const qa_app_options *options = qa_startup_active_options(app);
  if (options == NULL) options = client->configuration_current.options;
  if (app->browser == NULL) qa_raise("Error", 0, 0, "Startup browser is unavailable");
  qa_app_demo_reader reader = {app, demo_read, plain_print};
  qa_app_demo_resource resource = qa_app_open_demo_resource(call->intent->request, reader);
  qa_cleanup guard;
  qa_cleanup_push(&guard, (void (*)(void *))qa_app_demo_resource_destroy, &resource);
  launch_record *record = keep_launch(app, LAUNCH_OPTIONS);
  qa_app_startup_options_copy(&record->options, options);
  qa_remote_application_host host;
  memset(&host, 0, sizeof host);
  host.base = app->host.base;
  host.base.save_directory = qa_app_startup_saves_directory(app->saves);
  host.server_browser = app->browser;
  demo_complete *complete = calloc(1, sizeof *complete);
  if (complete == NULL) qa_range_error("Out of memory");
  complete->app = app;
  complete->token = call->intent->request_token;
  app->demo_completions = grow(app->demo_completions, app->demo_completion_count, sizeof *app->demo_completions);
  app->demo_completions[app->demo_completion_count++] = complete;
  qa_remote_application *remote = qa_remote_application_open_demo_borrowed(client, &record->options, &host, &resource,
      call->intent->request.timedemo, demo_completed, complete);
  if (remote == NULL) qa_range_error("Out of memory");
  keep_remote(app, remote);
  qa_cleanup_pop(&guard);
  qa_app_demo_resource_destroy(&resource);
  app->game = NULL;
  app->remote = remote;
  app->has_active_demo = true;
  qa_startup_set_text(&app->active_demo_name, call->intent->request.name);
  app->active_demo = call->intent->request;
  app->active_demo.name = app->active_demo_name;
  app->active_demo_token = call->intent->request_token;
  if (app->demos != NULL) qa_app_client_demo_commands_refresh(app->demos);
  app->last_frame = qa_startup_now();
}
static void publish_demo_intent(qa_startup_application *app) {
  if (!app->has_pending_demo) return;
  qa_app_client_demo_intent intent = app->pending_demo;
  app->has_pending_demo = false;
  memset(&app->pending_demo, 0, sizeof app->pending_demo);
  qa_cleanup context_guard;
  qa_cleanup_push(&context_guard, (void (*)(void *))qa_command_context_release, &intent.source);
  qa_startup_close_frontend_movie(app);
  if (eq(intent.kind, "stop")) {
    qa_startup_return_to_frontend(app);
    app->has_active_demo = false;
    if (app->demos != NULL) qa_app_client_demo_commands_refresh(app->demos);
    qa_cleanup_pop(&context_guard);
    qa_command_context_release(&intent.source);
    return;
  }
  if (app->client == NULL) qa_raise("Error", 0, 0, "Demo playback has no retained client");
  demo_call call = {app, &intent};
  qa_caught_error caught;
  bool opened = qa_try(demo_body, &call, &caught);
  qa_cleanup_pop(&context_guard);
  qa_command_context_release(&intent.source);
  if (opened) return;
  if (app->demos != NULL) qa_app_client_demo_commands_failed(app->demos, intent.request_token);
  app->last_frame = qa_startup_now();
  if (is_publication_error(&caught)) {
    app->stopping = true;
    qa_rethrow(&caught);
  }
  qa_startup_printf(app, "%s\n", caught.message);
}

/* ---- publishPendingSource() ---------------------------------------------------- */
typedef struct recording_call { qa_startup_application *app; qa_app_startup_recording_intent *intent; } recording_call;
static void recording_body(void *ctx) {
  recording_call *call = ctx;
  qa_app_client_demo_recording *recording = call->app->recording;
  if (recording == NULL) return;
  const char *kind = call->intent->kind;
  const char *error;
  if (eq(kind, "record")) error = qa_app_client_demo_recording_start(recording, call->intent->name, call->intent->source);
  else if (eq(kind, "rerecord")) error = qa_app_client_demo_recording_rerecord(recording, call->intent->name, call->intent->source);
  else if (eq(kind, "mvdrecord")) error = qa_app_client_demo_recording_start_mvd(recording, call->intent->name, call->intent->source);
  else if (eq(kind, "serverrecord")) error = qa_app_client_demo_recording_start_server(recording, call->intent->name, call->intent->source);
  else if (eq(kind, "serverstop")) error = qa_app_client_demo_recording_stop_server(recording);
  else if (eq(kind, "mvdstop")) error = qa_app_client_demo_recording_stop_mvd(recording);
  else error = qa_app_client_demo_recording_stop(recording);
  raise_text(error);
}
static void movie_body(void *ctx) {
  qa_startup_application *app = ctx;
  qa_startup_open_frontend_movie(app, &app->opening_movie);
}
void qa_startup_publish_pending_source(qa_startup_application *app) {
  if (app->client != NULL && (qa_app_capture_pending_readback(app->client->capture) || qa_app_video_restart_pending(app->client->video_restart)))
    return;
  if (app->has_pending_movie) {
    app->opening_movie = app->pending_movie;
    app->has_pending_movie = false;
    memset(&app->pending_movie, 0, sizeof app->pending_movie);
    qa_caught_error caught;
    bool opened = qa_try(movie_body, app, &caught);
    free(app->opening_movie.name);
    memset(&app->opening_movie, 0, sizeof app->opening_movie);
    if (!opened && !app->closed && !app->stopping) qa_startup_printf(app, "%s\n", caught.message);
  }
  if (app->closed || app->stopping) return;
  if (app->has_pending_recording) {
    qa_app_startup_recording_intent intent = app->pending_recording;
    app->has_pending_recording = false;
    memset(&app->pending_recording, 0, sizeof app->pending_recording);
    recording_call call = {app, &intent};
    qa_caught_error caught;
    if (!qa_try(recording_body, &call, &caught)) qa_startup_printf(app, "%s\n", caught.message);
    if (app->recording != NULL && qa_app_client_demo_recording_path(app->recording) == NULL)
      memset(&app->recording_preparation, 0, sizeof app->recording_preparation);
    free(intent.name);
    qa_command_context_release(&intent.source);
  }
  /* lobbyTransition: the C lobby transitions run inside the lobby call (stageLobbyLaunch). */
  if (app->has_pending) {
    qa_app_startup_action action = app->pending;
    app->has_pending = false;
    memset(&app->pending, 0, sizeof app->pending);
    qa_cleanup guard;
    qa_cleanup_push(&guard, (void (*)(void *))qa_app_startup_action_clear, &action);
    if (!app->stopping) qa_startup_launch(app, &action, false);
    qa_cleanup_pop(&guard);
    qa_app_startup_action_clear(&action);
  }
  if (!app->stopping) publish_demo_intent(app);
}

/* ---- dispatchApplicationRequest(request) ------------------------------------------- */
void qa_startup_dispatch_application_request(qa_startup_application *app, const qa_app_configuration_command_request *request) {
  if (app->stopping) return;
  const qa_command_origin *origin = &request->source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) {
    qa_app_client_bootstrap *client = app->client;
    bool active = false;
    if (client != NULL && client->platform.kind == QA_APP_CLIENT_INPUT_WORLD) {
      size_t count = qa_app_application_input_local_count(client->platform.input);
      for (size_t i = 0; i < count && !active; i++) {
        qa_session_seat *seat = qa_app_application_input_local(client->platform.input, i)->player.seat;
        active = qa_same_seat(qa_session_seat_id(seat), origin->seat)
            && qa_same_client(qa_session_client_id(qa_session_seat_client(seat)), origin->client);
      }
    } else if (client != NULL && client->local_count > 0) {
      qa_session_seat *seat = client->locals[0].seat;
      active = qa_same_seat(qa_session_seat_id(seat), origin->seat)
          && qa_same_client(qa_session_client_id(qa_session_seat_client(seat)), origin->client);
    }
    if (!active) {
      qa_startup_application_print(app, "Ignoring application request from an inactive or retired client seat.\n");
      return;
    }
  }
  if (qa_startup_application_route_command(app, request->name, request->arguments, request->argument_count, request->source, NULL, true)) {
    qa_startup_publish_pending_source(app);
    return;
  }
  if (app->game != NULL) qa_application_execute_application_request(app->game, request);
  else if (app->remote != NULL) qa_remote_application_execute_application_request(app->remote, request);
  else {
    qa_startup_application_frontend_command(app, request->name, request->arguments, request->argument_count, request->source);
    if (app->graphics != NULL) qa_app_startup_audio_flush_commands(app->graphics->audio);
  }
}

/* ---- step() ------------------------------------------------------------------------- */
static void after_dispatch(qa_startup_application *app) {
  qa_app_client_bootstrap *client = app->client;
  (void)qa_app_video_restart_drain(client->video_restart);
  qa_app_startup_audio_flush_commands(app->graphics->audio);
  if (app->game != NULL) qa_application_flush_client_commands(app->game);
  else if (app->remote != NULL) qa_remote_application_flush_client_commands(app->remote);
  qa_startup_publish_pending_source(app);
}
static void after_dispatch_fn(void *ctx) { after_dispatch(ctx); }
static bool commands_blocked(qa_startup_application *app) {
  return app->game != NULL ? qa_application_client_commands_blocked(app->game)
      : app->remote != NULL ? qa_remote_application_client_commands_blocked(app->remote) : false;
}
static bool should_continue(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_client_bootstrap *client = app->client;
  return !app->closed && !app->stopping && !qa_app_capture_pending_readback(client->capture)
      && !qa_app_video_restart_pending(client->video_restart) && !commands_blocked(app);
}
typedef struct step_call { qa_startup_application *app; double elapsed; bool remote_output; bool application; } step_call;
static void active_step_body(void *ctx) {
  step_call *call = ctx;
  qa_startup_application *app = call->app;
  if (call->application) (void)qa_application_step(app->game, call->elapsed);
  else {
    qa_simulation_output output;
    call->remote_output = qa_remote_application_step(app->remote, call->elapsed, &output);
  }
}
static void discard_pending(void *ctx) {
  qa_application_pending_client_commands **pending = ctx;
  if (*pending != NULL) qa_application_pending_client_commands_discard(*pending);
  *pending = NULL;
}
void qa_startup_application_step(qa_startup_application *app) {
  if (app->closed || app->stopping) return;
  /* The preferences' 100 ms onChange timer fires between frames, as the TS event loop runs it. */
  qa_app_frontend_preferences_pump(app->preferences);
  if (app->closed || app->stopping) return;
  double now = qa_startup_now();
  double elapsed = qa_js_max(4, now - app->last_frame);
  app->last_frame = now;
  if (app->browser != NULL) qa_app_startup_server_browser_poll(app->browser);
  {
    const char *error = qa_app_application_local_lobby_poll(&app->lobby);
    if (error != NULL) qa_startup_printf(app, "Local lobby: %s\n", error);
  }
  qa_startup_graphics *graphics = app->graphics;
  qa_app_client_bootstrap *client = app->client;
  if (graphics == NULL || client == NULL) qa_raise("Error", 0, 0, "Startup frame has no client");
  qa_application *source_game = app->game;
  qa_remote_application *source_remote = app->game != NULL ? NULL : app->remote;
  if (client->platform.kind == QA_APP_CLIENT_INPUT_MENU) {
    qa_app_input_devices_frame(client->input_devices, now);
    size_t count = 0;
    qa_sdl_event *events = qa_sdl_window_poll_events(qa_app_native_renderer_window(graphics->renderer), &count);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, events);
    for (size_t i = 0; i < count; i++) qa_input_router_handle_platform(graphics->router, &events[i]);
    qa_cleanup_pop(&guard);
    free(events);
    size_t controller_count = 0;
    qa_controller_event *controller_events = qa_sdl_controllers_poll_events(graphics->controllers, &controller_count);
    qa_cleanup controller_guard;
    qa_cleanup_push(&controller_guard, free, controller_events);
    for (size_t i = 0; i < controller_count; i++) qa_input_router_handle_controller(graphics->router, &controller_events[i]);
    qa_cleanup_pop(&controller_guard);
    qa_controller_events_free(controller_events, controller_count);
  } else if (source_game != NULL) qa_application_pump_client_input(source_game);
  else if (source_remote != NULL) qa_remote_application_pump_client_input(source_remote);
  after_dispatch(app);
  if (app->closed || app->stopping) return;
  bool startup_frame;
  if (qa_app_capture_pending_readback(client->capture)) startup_frame = false;
  else if (app->game != NULL) startup_frame = qa_application_advance_client_startup(app->game);
  else if (app->remote != NULL) startup_frame = qa_remote_application_advance_client_startup(app->remote);
  else startup_frame = qa_app_prepared_startup_advance_frame(client->prepared);
  if (!qa_app_prepared_startup_pending(client->prepared)) app->initial_configuration = false;
  after_dispatch(app);
  if (app->closed || app->stopping) return;
  if (!startup_frame && !qa_app_capture_pending_readback(client->capture) && !commands_blocked(app))
    (void)qa_command_buffer_execute_scripts_async(qa_app_prepared_startup_commands(client->prepared), after_dispatch_fn, app,
        should_continue, app);
  after_dispatch(app);
  if (app->closed || app->stopping) return;
  qa_application *active_game = app->game;
  qa_remote_application *active_remote = app->game != NULL ? NULL : app->remote;
  if ((active_game != NULL || active_remote != NULL) && !app->stopping) {
    bool same_source = active_game != NULL ? active_game == source_game : active_remote == source_remote;
    double operations = active_game != NULL ? qa_application_world_operation_count(active_game) : 0;
    step_call call = {app, same_source ? elapsed : 4, false, active_game != NULL};
    qa_caught_error caught;
    if (!qa_try(active_step_body, &call, &caught)) {
      qa_startup_fail_active_source(app, active_game != NULL, &caught);
      return;
    }
    if (active_game != NULL && qa_application_world_operation_count(active_game) != operations) app->last_frame = qa_startup_now();
    if (active_remote != NULL && !qa_remote_application_presented_cinematic(active_remote) && !call.remote_output
        && active_remote == app->remote && (client->platform.kind == QA_APP_CLIENT_INPUT_WORLD || qa_app_capture_pending_readback(client->capture))) {
      qa_startup_graphics_draw(app, client->source_current, true);
      qa_app_capture_drain(client->capture);
    }
    if (client->has_pending_source(client->bootstrap_ctx)) {
      qa_application_pending_client_commands *remaining = active_game != NULL ? qa_application_take_pending_client_commands(active_game) : NULL;
      qa_cleanup guard;
      qa_cleanup_push(&guard, discard_pending, &remaining);
      qa_startup_publish_pending_source(app);
      qa_cleanup_pop(&guard);
      if (remaining != NULL) qa_application_pending_client_commands_run(remaining);
    }
    bool still_active = active_game != NULL ? active_game == app->game : active_remote == (app->game != NULL ? NULL : app->remote);
    bool finished = active_game != NULL ? qa_application_finished(active_game) : qa_remote_application_finished(active_remote);
    if (still_active && finished) {
      if (app->game != NULL) *qa_app_frontend_preferences_values(app->preferences) = qa_application_frontend_settings(app->game);
      qa_startup_return_to_frontend(app);
    }
  }
  qa_startup_publish_pending_source(app);
  app->frames++;
  if (client->platform.kind != QA_APP_CLIENT_INPUT_MENU || (app->remote != NULL && qa_remote_application_presented_cinematic(app->remote)))
    return;
  qa_app_campaign_cinematic *movie = app->frontend_movie;
  if (movie != NULL) {
    size_t seat_count = 0;
    const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(client->prepared, &seat_count);
    bool console_open = seat_count > 0 && eq(qa_seat_input_get_focus(seats[0].input).kind, "console");
    bool ended = qa_app_campaign_cinematic_frame(movie, elapsed, app->frames, console_open, false);
    if (ended && app->frontend_movie == movie) qa_startup_close_frontend_movie(app);
    if (!console_open) {
      qa_app_capture_drain(client->capture);
      return;
    }
  }
  qa_startup_refresh_preference_baseline(app, false);
  qa_app_controller_settings_update(graphics->controller_settings);
  qa_app_input_devices_activate(client->input_devices, graphics->router);
  qa_input_router_update_capture(graphics->router);
  if (app->refresh_saves) {
    app->refresh_saves = false;
    qa_app_startup_menu_set_status(graphics->menu, "Reading saved games...", true);
    qa_startup_graphics_draw(app, NULL, false);
    qa_app_startup_saves_refresh(app->saves);
    const char *error = qa_app_startup_saves_list(app->saves)->error;
    qa_app_startup_menu_set_status(graphics->menu, error != NULL ? error : "", false);
  }
  qa_app_image_settings_refresh_display(graphics->image_settings, graphics->renderer);
  qa_sdl_size logical = qa_sdl_window_logical_size(qa_app_native_renderer_window(graphics->renderer));
  qa_app_startup_selection_set_display(app->model, logical.width, logical.height, qa_app_native_renderer_output_gamma(graphics->renderer));
  qa_startup_graphics_draw(app, NULL, false);
  qa_app_capture_drain(client->capture);
}

/* ---- run() -------------------------------------------------------------------------- */
static void sleep_ms(double milliseconds) {
  struct timespec delay;
  delay.tv_sec = (time_t)(milliseconds / 1000);
  delay.tv_nsec = (long)(fmod(milliseconds, 1000) * 1e6);
  nanosleep(&delay, NULL);
}
void qa_startup_application_run(qa_startup_application *app) {
  for (;;) {
    qa_app_options options;
    qa_app_startup_selection_options(app->model, &options);
    bool limited = options.has_frame_limit && app->frames >= options.frame_limit;
    qa_app_options_destroy(&options);
    if (app->stopping || app->closed || limited) break;
    double started = qa_startup_now();
    qa_startup_application_step(app);
    double wait = app->game == NULL && app->remote == NULL ? 8 : 4 - (qa_startup_now() - started);
    if (wait > 0) sleep_ms(wait);
  }
}

/* ---- close() ------------------------------------------------------------------------ */
typedef struct close_errors { char messages[16][sizeof ((qa_caught_error *)0)->message]; size_t count; } close_errors;
static void collect(close_errors *errors, void (*step)(void *ctx), void *ctx) {
  qa_caught_error caught;
  if (qa_try(step, ctx, &caught)) return;
  if (errors->count < 16) snprintf(errors->messages[errors->count], sizeof errors->messages[0], "%s", caught.message);
  errors->count++;
}
static void close_addons(void *ctx) { qa_app_addon_library_close(((qa_startup_application *)ctx)->addons); }
static void close_save_controls(void *ctx) {
  qa_startup_application *app = ctx;
  qa_startup_save_frontend_input(app);
  qa_app_frontend_preferences_save_global(app->preferences);
}
static void close_save_audio(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->preference_store != NULL) qa_app_frontend_preferences_save_audio_baseline(app->preferences, app->preference_store);
}
static void close_lobby(void *ctx) { raise_text(qa_app_application_local_lobby_close(&((qa_startup_application *)ctx)->lobby)); }
static void close_devices(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->client != NULL) qa_app_input_devices_save(app->client->input_devices);
}
static void close_keys(void *ctx) { qa_app_keys_save(((qa_startup_application *)ctx)->keys); }
static void close_movie(void *ctx) { qa_startup_close_frontend_movie(ctx); }
static void close_recording(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->recording != NULL) raise_text(qa_app_client_demo_recording_stop_all(app->recording));
}
static void close_capture(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->client != NULL) qa_app_capture_close(app->client->capture);
}
static void close_menus(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->graphics != NULL && !app->graphics->closed) qa_native_ui_controller_close_all(qa_app_startup_menu_controller(app->graphics->menu));
}
static void close_source(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->client != NULL) raise_text(qa_app_client_source_retire(app->client->source_current));
}
static void close_browser(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->browser != NULL) qa_app_startup_server_browser_close(app->browser);
}
static void close_session(void *ctx) { qa_engine_session_close(((qa_startup_application *)ctx)->session); }
static void close_scripts(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_console_script_files *scripts = app->client != NULL && app->client->configuration_current.scripts != NULL
      ? app->client->configuration_current.scripts : app->scripts;
  if (scripts != NULL) qa_app_console_script_files_close(scripts);
}
static void close_image_settings(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->image_settings != NULL) qa_app_image_settings_close(app->image_settings);
}
static void close_release_commands(void *ctx) {
  qa_startup_application *app = ctx;
  /* CleanupStack.runAll(): newest first. */
  while (app->release_source_command_count > 0) {
    app->release_source_command_count--;
    app->release_source_commands[app->release_source_command_count].run(app,
        app->release_source_commands[app->release_source_command_count].ctx);
  }
}
static void close_routing(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->frontend_routing != NULL) qa_app_console_routing_close(app->frontend_routing);
}
static void close_graphics(void *ctx) { qa_startup_graphics_close(ctx); }
void qa_startup_application_close(qa_startup_application *app) {
  if (app->closed) return;
  app->closed = true;
  app->stopping = true;
  /* lobbyTransition?.cancel(new Error("Startup closed before lobby launch")): the C transition runs inside its lobby call. */
  app->lobby_transition = false;
  close_errors errors;
  memset(&errors, 0, sizeof errors);
  collect(&errors, close_addons, app);
  if (app->gtv_abort_active) app->gtv_aborted = true;
  app->gtv_source = NULL;
  app->movie_generation++;
  if (app->has_pending_movie) free(app->pending_movie.name);
  memset(&app->pending_movie, 0, sizeof app->pending_movie);
  app->has_pending_movie = false;
  if (app->game != NULL) *qa_app_frontend_preferences_values(app->preferences) = qa_application_frontend_settings(app->game);
  {
    qa_caught_error caught;
    if (!qa_try(close_save_controls, app, &caught)) qa_startup_printf(app, "Could not save controls: %s\n", caught.message);
    if (!qa_try(close_save_audio, app, &caught)) qa_startup_printf(app, "Could not save audio settings: %s\n", caught.message);
  }
  collect(&errors, close_lobby, app);
  collect(&errors, close_devices, app);
  collect(&errors, close_keys, app);
  collect(&errors, close_movie, app);
  collect(&errors, close_recording, app);
  if (app->game != NULL) qa_application_request_quit(app->game);
  if (app->remote != NULL) qa_remote_application_request_quit(app->remote);
  if (app->client != NULL) qa_app_video_restart_close(app->client->video_restart);
  collect(&errors, close_capture, app);
  collect(&errors, close_menus, app);
  collect(&errors, close_source, app);
  collect(&errors, close_browser, app);
  collect(&errors, close_session, app);
  collect(&errors, close_scripts, app);
  collect(&errors, close_image_settings, app);
  qa_startup_release_menu_input(app);
  if (app->release_demos != NULL) qa_app_client_demo_commands_detach(app->demos, app->release_demos);
  app->release_demos = NULL;
  collect(&errors, close_release_commands, app);
  collect(&errors, close_routing, app);
  collect(&errors, close_graphics, app);
  app->client = NULL;
  app->game = NULL;
  app->remote = NULL;
  if (errors.count != 0) qa_raise("AggregateError", 0, 0, "Shared client shutdown failed: %s", errors.messages[0]);
}
