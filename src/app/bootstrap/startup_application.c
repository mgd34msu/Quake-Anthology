/* Port of src/app/bootstrap/startup.ts: the StartupApplication constructor,
 * StartupAction, print, requestQuit, routeCommand, frontendCommand, connect,
 * lobbyJoinOptions and the public getters. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "startup_application_internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/input.h"
#include "qa/app/bootstrap/q3_map_command.h"
#include "qa/app/bootstrap/save_requests.h"
#include "qa/content/roots.h"
#include "qa/content/user_data.h"
#include "qa/core/cvars/index.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/network/common/endpoint.h"
#include "qa/platform/sdl.h"
#include "qa/persistence/recipe.h"
#include "qa/persistence/save_policy.h"
#include "qa/persistence/value.h"

/* ---- shared helpers ---------------------------------------------------- */
char *qa_startup_dup(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text) + 1;
  char *copy = malloc(length);
  if (copy == NULL) qa_range_error("Out of memory");
  memcpy(copy, text, length);
  return copy;
}
void qa_startup_set_text(char **slot, const char *text) {
  char *copy = qa_startup_dup(text);
  free(*slot);
  *slot = copy;
}
static bool eq(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
/* TS performance.now(). */
double qa_startup_now(void) { return qa_app_performance_now(); }
void qa_startup_printf(qa_startup_application *app, const char *format, ...) {
  va_list args;
  va_start(args, format);
  char text[4096];
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_startup_application_print(app, text);
}
void qa_startup_push_release(qa_startup_application *app, void (*run)(qa_startup_application *app, void *ctx), void *ctx) {
  void *grown = realloc(app->release_source_commands, (app->release_source_command_count + 1) * sizeof *app->release_source_commands);
  if (grown == NULL) qa_range_error("Out of memory");
  app->release_source_commands = grown;
  app->release_source_commands[app->release_source_command_count].run = run;
  app->release_source_commands[app->release_source_command_count].ctx = ctx;
  app->release_source_command_count++;
}
const qa_app_client_source_lifetime *qa_startup_current_source(const qa_startup_application *app) {
  return app->client != NULL ? app->client->source_current : NULL;
}
/* Application and RemoteApplication publish a ClientSourceLifetime whose ctx is
 * the application, so `client.source.current === this.game` is a ctx check. */
bool qa_startup_source_is_game(const qa_app_client_source_lifetime *source, const qa_application *game) {
  return source != NULL && game != NULL && source->ctx == (const void *)game;
}
bool qa_startup_source_is_remote(const qa_app_client_source_lifetime *source, const qa_remote_application *remote) {
  return source != NULL && remote != NULL && source->ctx == (const void *)remote;
}
/* TS this.game?.options ?? this.remote?.options; NULL selects model.options. */
const qa_app_options *qa_startup_active_options(qa_startup_application *app) {
  if (app->game != NULL) return qa_application_options(app->game);
  if (app->remote != NULL) return qa_remote_application_options(app->remote);
  return NULL;
}

void qa_app_startup_action_clear(qa_app_startup_action *action) {
  free(action->gtv_address); free(action->gtv_username); free(action->gtv_password); free(action->gtv_label);
  free(action->connection.remote);
  qa_app_options_destroy(&action->options);
  free(action->q3_map_launch);
  free(action->preset_id); free(action->arena_map); free(action->path); free(action->source_product);
  memset(action, 0, sizeof *action);
}
static void clear_recording_intent(qa_startup_application *app) {
  if (app->has_pending_recording) {
    free(app->pending_recording.name);
    qa_command_context_release(&app->pending_recording.source);
  }
  memset(&app->pending_recording, 0, sizeof app->pending_recording);
  app->has_pending_recording = false;
}
void qa_startup_application_clear_pending(qa_startup_application *app) {
  if (app->has_pending) qa_app_startup_action_clear(&app->pending);
  app->has_pending = false;
  clear_recording_intent(app);
  if (app->has_pending_movie) free(app->pending_movie.name);
  memset(&app->pending_movie, 0, sizeof app->pending_movie);
  app->has_pending_movie = false;
}
/* Every pending record the application owns (destroy). */
void qa_startup_clear_pending_all(qa_startup_application *app) {
  qa_startup_application_clear_pending(app);
  if (app->has_pending_demo) qa_command_context_release(&app->pending_demo.source);
  memset(&app->pending_demo, 0, sizeof app->pending_demo);
  app->has_pending_demo = false;
}
/* this.pending = action (the previous pending action is replaced). */
static qa_app_startup_action *stage(qa_startup_application *app, qa_app_startup_action_kind kind) {
  if (app->has_pending) qa_app_startup_action_clear(&app->pending);
  memset(&app->pending, 0, sizeof app->pending);
  app->pending.kind = kind;
  app->has_pending = true;
  return &app->pending;
}
qa_app_startup_action *qa_startup_stage(qa_startup_application *app, qa_app_startup_action_kind kind) { return stage(app, kind); }
static void note_world_action(qa_app_prepared_startup *prepared) {
  if (prepared != NULL) qa_app_prepared_startup_note_world_action(prepared);
}

/* ---- constructor -------------------------------------------------------- */
static void host_print(qa_startup_application *app, const char *text) {
  if (app->host.base.print != NULL) app->host.base.print(app->host.base.ctx, text);
}
/* lobby transitions (TS constructor). */
static const char *lobby_host(void *ctx, const qa_lobby *lobby, qa_net_address *endpoint_out, qa_wire_selection *wire_out) {
  (void)lobby;
  qa_startup_application *app = ctx;
  const qa_app_startup_launch *selection = app->lobby_selection;
  if (selection == NULL) return "Select a game before starting the lobby";
  qa_app_startup_action action;
  memset(&action, 0, sizeof action);
  action.kind = QA_APP_STARTUP_ACTION_LOBBY_HOST;
  action.selection = selection;
  const char *error = qa_startup_stage_lobby_launch(app, &action);
  if (error != NULL) return error;
  qa_application *game = app->game;
  qa_net_address endpoint;
  qa_wire_selection wire;
  if (game == NULL || !qa_application_network_address(game, &endpoint) || !qa_application_network_wire(game, &wire))
    return "The selected game did not open a listening server";
  app->lobby_source = qa_startup_current_source(app) != NULL && qa_startup_source_is_game(qa_startup_current_source(app), game)
      ? qa_startup_current_source(app) : NULL;
  *endpoint_out = endpoint;
  *wire_out = wire;
  return NULL;
}
typedef struct join_call {
  qa_startup_application *app; const qa_lobby *lobby; qa_app_startup_action action;
  qa_save_value *tree; qa_executable_recipe recipe; bool has_recipe;
} join_call;
static void join_tree_free(void *ctx) { qa_save_value_free(ctx); }
/* TS lobby.composition.composition.recipe: the C SessionComposition carries the
 * composition as its canonical JSON text ({ recipe, schemaVersion, ... }), so the
 * recipe is read back from that text. */
static void join_options_body(void *ctx) {
  join_call *call = ctx;
  const qa_composition_identity *identity = call->lobby->composition;
  if (identity == NULL || identity->composition.canonical_text == NULL)
    qa_raise("Error", 0, 0, "This lobby has no session composition");
  const char *text = identity->composition.canonical_text;
  call->tree = qa_parse_json_value((const uint8_t *)text, strlen(text), false);
  if (call->tree == NULL) qa_raise("Error", 0, 0, "This lobby has no session composition");
  qa_cleanup guard;
  qa_cleanup_push(&guard, join_tree_free, call->tree);
  qa_save_reader root, recipe;
  if (!qa_save_reader_init(&root, call->tree, "composition")) qa_range_error("Out of memory");
  if (!qa_save_reader_field(&root, "recipe", &recipe)) qa_raise("Error", 0, 0, "This lobby has no session composition");
  call->recipe = qa_read_recipe(&recipe);
  call->has_recipe = true;
  qa_cleanup_pop(&guard);
  qa_startup_application_lobby_join_options(call->app, call->lobby, &call->recipe, &call->action.options);
}
static const char *lobby_join(void *ctx, const qa_lobby *lobby) {
  qa_startup_application *app = ctx;
  static char message[sizeof ((qa_caught_error *)0)->message];
  join_call call;
  memset(&call, 0, sizeof call);
  call.app = app; call.lobby = lobby;
  call.action.kind = QA_APP_STARTUP_ACTION_INITIAL;
  qa_caught_error caught;
  bool joined = qa_try(join_options_body, &call, &caught);
  if (!joined) {
    if (call.has_recipe) qa_executable_recipe_free(&call.recipe);
    if (call.tree != NULL) qa_save_value_free(call.tree);
    snprintf(message, sizeof message, "%s", caught.message);
    return message;
  }
  const char *error = qa_startup_stage_lobby_launch(app, &call.action);
  qa_app_startup_action_clear(&call.action);
  qa_executable_recipe_free(&call.recipe);
  qa_save_value_free(call.tree);
  if (error != NULL) return error;
  app->lobby_source = app->remote != NULL ? qa_startup_current_source(app) : NULL;
  return NULL;
}
static const char *lobby_leave(void *ctx, const qa_lobby *lobby) {
  (void)lobby;
  qa_startup_application *app = ctx;
  if (!app->closed && app->lobby_source != NULL) stage(app, QA_APP_STARTUP_ACTION_FRONTEND);
  app->lobby_source = NULL;
  return NULL;
}
static const char *lobby_completed(void *ctx, const qa_lobby *lobby) {
  (void)ctx; (void)lobby;
  return NULL;
}
/* FrontendPreferences: () => movementDialect(model.options). */
static qa_command_dialect preferences_dialect(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_options options;
  qa_app_startup_selection_options(app->model, &options);
  qa_command_dialect dialect = qa_app_movement_dialect(&options, NULL);
  qa_app_options_destroy(&options);
  return dialect;
}
/* preferences.onChange: persistPreferences().catch(print "Could not save controls: ..."). */
typedef struct persist_call { qa_startup_application *app; } persist_call;
static void persist_body(void *ctx) {
  qa_startup_application *app = ((persist_call *)ctx)->app;
  qa_startup_save_frontend_input(app);
  if (app->preference_store != NULL) qa_app_frontend_preferences_save_audio_baseline(app->preferences, app->preference_store);
  qa_app_frontend_preferences_save_global(app->preferences);
}
static void preferences_changed(void *ctx) {
  qa_startup_application *app = ctx;
  if (app->closed) return;
  persist_call call = {app};
  qa_caught_error caught;
  if (!qa_try(persist_body, &call, &caught)) qa_startup_printf(app, "Could not save controls: %s\n", caught.message);
}
/* this.saves = new StartupSaves(model.catalog, saveDirectory, host.externalSaveDirectories ??
 * nightdiveAddonDirectories().map(root => root.root)); the directories are kept for saves.externalDirectories. */
static void open_saves(qa_startup_application *app) {
  if (app->external_directories == NULL) {
    const char *const *directories = app->host.base.external_save_directories;
    size_t count = app->host.base.external_save_directory_count;
    qa_nightdive_addon_root *roots = NULL;
    size_t root_count = 0;
    if (!app->host.base.has_external_save_directories) {
      if (!qa_nightdive_addon_directories(NULL, &roots, &root_count)) qa_range_error("Out of memory");
      count = root_count;
    }
    app->external_directories = calloc(count + 1, sizeof *app->external_directories);
    if (app->external_directories == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < count; i++) app->external_directories[i] = qa_startup_dup(roots != NULL ? roots[i].root : directories[i]);
    app->external_directory_count = count;
    qa_nightdive_addon_roots_free(roots, root_count);
  }
  qa_app_startup_saves *saves = qa_app_startup_saves_create(qa_app_startup_selection_catalog(app->model), app->save_directory,
      (const char *const *)app->external_directories, app->external_directory_count);
  if (saves == NULL) qa_range_error("Out of memory");
  if (app->saves != NULL) qa_app_startup_saves_destroy(app->saves);
  app->saves = saves;
}
/* AddonLibrary changed: refresh the catalog, rebuild the saves, flag a refresh. */
static const char *addons_changed(void *ctx) {
  qa_startup_application *app = ctx;
  qa_app_startup_selection_refresh_catalog(app->model);
  open_saves(app);
  app->refresh_saves = true;
  return NULL;
}
static void addons_launch(void *ctx, const char *product, const char *map) {
  qa_startup_application *app = ctx;
  qa_app_startup_selection_select(app->model, "product", product);
  if (map != NULL) {
    size_t length = strlen(map);
    char *path = malloc(length + 10);
    if (path == NULL) qa_range_error("Out of memory");
    /* "maps/" + map.replace(/\.bsp$/, "") + ".bsp" */
    size_t stem = length >= 4 && strcmp(map + length - 4, ".bsp") == 0 ? length - 4 : length;
    snprintf(path, length + 10, "maps/%.*s.bsp", (int)stem, map);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, path);
    qa_app_startup_selection_select(app->model, "map", path);
    qa_cleanup_pop(&guard);
    free(path);
  }
  stage(app, QA_APP_STARTUP_ACTION_PLAY);
}
/* node:path join(saveDirectory, "..", "settings"): the joined text normalized
 * (".", ".." and repeated separators resolved; a relative path keeps leading "..";
 * an empty result is "."). */
char *qa_startup_settings_directory(const char *save_directory) {
  size_t length = strlen(save_directory);
  char *joined = malloc(length + 16);
  if (joined == NULL) qa_range_error("Out of memory");
  snprintf(joined, length + 16, "%s/../settings", save_directory);
  bool absolute = joined[0] == '/';
  char **segments = calloc(strlen(joined) + 1, sizeof *segments);
  if (segments == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (char *segment = strtok(joined, "/"); segment != NULL; segment = strtok(NULL, "/")) {
    if (strcmp(segment, ".") == 0) continue;
    if (strcmp(segment, "..") == 0) {
      if (count > 0 && strcmp(segments[count - 1], "..") != 0) { count--; continue; }
      if (absolute) continue;
    }
    segments[count++] = segment;
  }
  char *out = malloc(length + 16);
  if (out == NULL) qa_range_error("Out of memory");
  size_t at = 0;
  if (absolute) out[at++] = '/';
  for (size_t i = 0; i < count; i++) {
    if (i > 0) out[at++] = '/';
    size_t n = strlen(segments[i]);
    memcpy(out + at, segments[i], n);
    at += n;
  }
  if (at == 0) out[at++] = '.';
  out[at] = '\0';
  free(segments);
  free(joined);
  return out;
}

qa_startup_application *qa_startup_application_create(qa_app_startup_selection_model *model,
    qa_startup_application_host host, const char *save_directory, const char *entry) {
  qa_startup_application *app = calloc(1, sizeof *app);
  if (app == NULL) qa_range_error("Out of memory");
  app->host = host;
  app->model = model;
  app->entry = entry != NULL && strcmp(entry, "run") == 0 ? "run" : "menu";
  app->save_directory = qa_startup_dup(save_directory);
  app->initial_configuration = true;
  app->last_frame = qa_startup_now();
  app->menu_input_binding = -1; app->menu_output_binding = -1;
  if (!qa_identity_owner_create(&app->identity, "startup-client")) qa_range_error("Out of memory");
  app->has_identity = true;
  qa_session_mode mode = {QA_SESSION_MODE_LOCAL};
  app->session = qa_engine_session_create(&app->identity, mode);
  if (app->session == NULL) qa_range_error("Out of memory");
  app->keys = qa_app_keys_create((qa_app_keys_print_fn)qa_startup_application_print, app);
  if (app->keys == NULL) qa_range_error("Out of memory");
  qa_music_controls_init(&app->music_controls);
  /* matchRankings = host.localRankings ?? new ApplicationMatchRankings() */
  app->match_rankings = host.base.local_rankings;
  if (app->match_rankings == NULL) {
    app->match_rankings = qa_app_match_rankings_create();
    if (app->match_rankings == NULL) qa_range_error("Out of memory");
    app->owns_match_rankings = true;
  }
  /* lobby = new ApplicationLocalLobby(host.localLobbies ?? new LocalLobbyService(), localClientAccount(identity), ...) */
  qa_local_lobby_service *service = host.local_lobbies;
  if (service == NULL) {
    service = qa_local_lobby_service_new();
    if (service == NULL) qa_range_error("Out of memory");
    app->owned_lobby_service = service;
  }
  app->accounts = qa_app_local_accounts_create_os_random();
  if (app->accounts == NULL) qa_range_error("Out of memory");
  const qa_app_local_account *account = qa_app_local_client_account(app->accounts, &app->identity);
  app->lobby_account.id = account->id;
  app->lobby_account.name = account->name;
  qa_app_local_lobby_transitions transitions = {app, lobby_host, lobby_join, lobby_leave, lobby_completed};
  qa_app_application_local_lobby_init(&app->lobby, service, &app->lobby_account, transitions);
  /* preferences = new FrontendPreferences(() => movementDialect(model.options), new ConfigStore(join(saveDirectory, "..", "settings"))) */
  char *shared_root = qa_startup_settings_directory(save_directory);
  qa_config_store shared;
  bool shared_open = shared_root != NULL && qa_config_store_init(&shared, shared_root);
  free(shared_root);
  if (!shared_open) qa_range_error("Out of memory");
  app->preferences = qa_app_frontend_preferences_create(preferences_dialect, app, &shared);
  qa_config_store_destroy(&shared);
  if (app->preferences == NULL) qa_range_error("Out of memory");
  /* preferences.onChange = () => void this.persistPreferences().catch(...) */
  qa_app_frontend_preferences_set_on_change(app->preferences, preferences_changed, app);
  open_saves(app);
  /* addons = new AddonLibrary({ edition, root, changed, launch }) */
  qa_content_catalog *catalog = qa_app_startup_selection_catalog(model);
  bool classic = false;
  for (size_t i = 0; i < catalog->product_count; i++)
    if (eq(catalog->products[i].expectation.id, "q1-classic-id1") && catalog->products[i].availability.kind == QA_CATALOG_INSTALLED)
      classic = true;
  qa_app_options options;
  qa_app_startup_selection_options(model, &options);
  char *root = options.user_content_root != NULL ? qa_startup_dup(options.user_content_root) : qa_default_user_content_root();
  qa_app_options_destroy(&options);
  if (root == NULL) qa_range_error("Out of memory");
  qa_app_addon_library_options addons = {0};
  addons.root = root;
  addons.edition = classic ? NULL : "rerelease";
  addons.ctx = app;
  addons.changed = addons_changed;
  addons.launch = addons_launch;
  app->addons = qa_app_addon_library_create(addons);
  free(root);
  if (app->addons == NULL) qa_range_error("Out of memory");
  return app;
}

/* this.host.print as a fn+ctx printer (ctx is the application). */
void qa_startup_host_print(void *ctx, const char *text) { host_print(ctx, text); }
/* TS clipboard: () => { bytes = readSdlClipboard(); return bytes === null ? null : new TextDecoder().decode(bytes) }.
 * The text is kept until the next read. */
const char *qa_startup_clipboard(qa_startup_application *app) {
  size_t length = 0;
  uint8_t *bytes = qa_read_sdl_clipboard(&length);
  free(app->clipboard_text);
  app->clipboard_text = NULL;
  if (bytes == NULL) return NULL;
  app->clipboard_text = malloc(length + 1);
  if (app->clipboard_text == NULL) { free(bytes); qa_range_error("Out of memory"); }
  memcpy(app->clipboard_text, bytes, length);
  app->clipboard_text[length] = '\0';
  free(bytes);
  return app->clipboard_text;
}

/* ---- print / requestQuit ------------------------------------------------- */
static qa_seat_console *client_console(qa_app_client_bootstrap *client, const struct qa_session_seat *seat) {
  for (size_t i = 0; i < client->console_count; i++)
    if (client->consoles[i].seat == seat) return client->consoles[i].console;
  return NULL;
}
void qa_startup_application_print(qa_startup_application *app, const char *text) {
  host_print(app, text);
  qa_app_client_bootstrap *client = app->client;
  if (client != NULL && client->platform.kind == QA_APP_CLIENT_INPUT_MENU && client->local_count > 0) {
    qa_seat_console *console = client_console(client, client->locals[0].seat);
    if (console != NULL) qa_seat_console_print(console, text);
  }
}
void qa_startup_application_request_quit(qa_startup_application *app) {
  if (app->gtv_abort_active) app->gtv_aborted = true;
  app->stopping = true;
  if (app->game != NULL) qa_application_request_quit(app->game);
  if (app->remote != NULL) qa_remote_application_request_quit(app->remote);
}

/* ---- closeFrontendMovie call used by routeCommand ----------------------- */
typedef struct gtv_call {
  const char *const *args; size_t count; const char *username, *password;
  qa_app_gtv_connect_request request; bool parsed;
  const qa_app_gtv_current *current; bool matches;
} gtv_call;
static void gtv_parse_body(void *ctx) {
  gtv_call *call = ctx;
  call->parsed = qa_app_parse_gtv_connect(call->args, call->count, call->username, call->password, &call->request);
}
static void gtv_disconnect_body(void *ctx) {
  gtv_call *call = ctx;
  call->matches = qa_app_matches_gtv_disconnect(call->args, call->count, call->current);
}
typedef struct map_call { const char *argument; char *path; } map_call;
static void map_body(void *ctx) {
  map_call *call = ctx;
  call->path = qa_app_map_resource_path(call->argument);
}
typedef struct load_call {
  const char *const *args; size_t count; const char *directory; qa_command_context source; bool allow_any;
  char *path; char *source_product;
} load_call;
static void load_body(void *ctx) {
  load_call *call = ctx;
  qa_app_load_request request = qa_app_parse_load_request(call->args, call->count);
  char *path = qa_save_command_path(call->directory, request.name);
  if (path == NULL) qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, path);
  qa_assert_load_path_allowed(call->directory, path, &call->source, call->allow_any, NULL, 0);
  qa_cleanup_pop(&cleanup);
  call->path = path;
  call->source_product = qa_startup_dup(request.source_product);
}

bool qa_startup_application_route_command(qa_startup_application *app, const char *name,
    const char *const *args, size_t arg_count, qa_command_context source, qa_app_prepared_startup *prepared,
    bool use_client_prepared) {
  if (use_client_prepared) prepared = app->client != NULL ? app->client->prepared : NULL;
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) return false;
  if (app->demos != NULL && qa_app_client_demo_commands_handle(app->demos, name, args, arg_count, source)) return true;
  if (eq(name, "record") || eq(name, "rerecord") || eq(name, "stop") || eq(name, "stoprecord") || eq(name, "mvdrecord")
      || eq(name, "mvdstop") || eq(name, "serverrecord") || eq(name, "serverstop")) {
    const char *kind;
    const char *filename = NULL;
    if (eq(name, "record") || eq(name, "rerecord") || eq(name, "mvdrecord") || eq(name, "serverrecord")) {
      filename = arg_count > 0 ? args[0] : NULL;
      if (arg_count > 1 || (!eq(name, "record") && filename == NULL)) { qa_startup_printf(app, "Usage: %s <name>\n", name); return true; }
      kind = (eq(name, "rerecord") || eq(name, "mvdrecord") || eq(name, "serverrecord")) && filename != NULL
          ? (eq(name, "rerecord") ? "rerecord" : eq(name, "mvdrecord") ? "mvdrecord" : "serverrecord") : "record";
    } else {
      if (arg_count != 0) { qa_startup_printf(app, "Usage: %s\n", name); return true; }
      kind = eq(name, "serverstop") ? "serverstop" : eq(name, "mvdstop") ? "mvdstop" : "stop";
    }
    clear_recording_intent(app);
    app->pending_recording.kind = kind;
    app->pending_recording.name = qa_startup_dup(filename);
    app->pending_recording.source = qa_command_context_copy(&source);
    app->has_pending_recording = true;
    note_world_action(prepared);
    return true;
  }
  if (app->game == NULL && app->remote == NULL && eq(name, "cinematic")) {
    const char *selected = arg_count > 0 ? args[0] : NULL, *mode = arg_count > 1 ? args[1] : NULL;
    if (selected == NULL || arg_count > 2) { qa_startup_application_print(app, "Usage: cinematic <name> [loop|hold]\n"); return true; }
    char *copy = qa_startup_dup(selected);
    if (app->has_pending_movie) free(app->pending_movie.name);
    app->pending_movie.name = copy;
    app->pending_movie.loop = eq(mode, "loop") || eq(mode, "2");
    app->pending_movie.hold = eq(mode, "hold") || eq(mode, "1");
    app->pending_movie.silent = false;
    app->has_pending_movie = true;
    note_world_action(prepared);
    return true;
  }
  if (app->frontend_movie != NULL && (eq(name, "cinematicpause") || eq(name, "stopcinematic"))) {
    if (eq(name, "cinematicpause"))
      qa_app_campaign_cinematic_pause(app->frontend_movie, !eq(qa_app_campaign_cinematic_status(app->frontend_movie), "paused"));
    else qa_startup_close_frontend_movie(app);
    return true;
  }
  if (eq(name, "in_restart") || eq(name, "midiinfo")) {
    qa_app_client_bootstrap *client = app->client;
    if (client == NULL) qa_raise("Error", 0, 0, "Input command requires the retained client");
    if (eq(name, "midiinfo")) qa_app_input_devices_info(client->input_devices);
    else {
      bool world = client->platform.kind == QA_APP_CLIENT_INPUT_WORLD;
      if (world) qa_app_application_input_release_for_profile_change(client->platform.input, NULL);
      else {
        size_t seat_count = 0;
        const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(client->prepared, &seat_count);
        for (size_t i = 0; i < seat_count; i++) qa_seat_input_release(seats[i].input, qa_startup_now(), NULL);
      }
      qa_app_input_devices_restart(client->input_devices);
      if (world) qa_input_router_restart(qa_app_application_input_router(client->platform.input));
      else if (client->platform.kind == QA_APP_CLIENT_INPUT_MENU) qa_input_router_restart(client->platform.router);
    }
    return true;
  }
  if (eq(name, "quit")) { qa_startup_application_request_quit(app); return true; }
  if (eq(name, "mvdconnect")) {
    qa_cvar_registry *cvars = prepared != NULL ? qa_app_prepared_startup_source(prepared) : NULL;
    char *username = NULL, *password = NULL;
    qa_cvar_read found;
    if (cvars != NULL && qa_cvar_registry_find(cvars, "mvd_username", &found)) {
      qa_cvar_read_free(&found);
      username = qa_cvar_registry_variable_string(cvars, "mvd_username");
    } else username = qa_startup_dup("unnamed");
    password = cvars != NULL ? qa_cvar_registry_variable_string(cvars, "mvd_password") : qa_startup_dup("");
    gtv_call call;
    memset(&call, 0, sizeof call);
    call.args = args; call.count = arg_count; call.username = username; call.password = password;
    qa_caught_error caught;
    if (!qa_try(gtv_parse_body, &call, &caught)) qa_startup_printf(app, "%s\n", caught.message);
    else if (!call.parsed) qa_startup_application_print(app, "Usage: mvdconnect [-u user] [-p password] [-n name] <address[:port]>\n");
    else {
      qa_app_startup_action *action = stage(app, QA_APP_STARTUP_ACTION_GTV);
      action->gtv_address = qa_startup_dup(call.request.address);
      action->gtv_username = qa_startup_dup(call.request.username);
      action->gtv_password = qa_startup_dup(call.request.password);
      action->gtv_label = qa_startup_dup(call.request.label);
      note_world_action(prepared);
    }
    free(username); free(password);
    return true;
  }
  if (eq(name, "mvdisconnect")) {
    /* TS current = this.gtv?.source === this.client?.source.current ? this.gtv : null */
    qa_app_gtv_current current = {app->gtv_id_value, app->gtv_label};
    bool same = app->gtv_source == NULL ? app->client == NULL
        : app->client != NULL && qa_startup_source_is_remote(app->client->source_current, app->gtv_source);
    gtv_call call;
    memset(&call, 0, sizeof call);
    call.args = args; call.count = arg_count;
    call.current = same && app->gtv_source != NULL ? &current : NULL;
    qa_caught_error caught;
    if (!qa_try(gtv_disconnect_body, &call, &caught)) qa_startup_printf(app, "%s\n", caught.message);
    else if (call.matches) { stage(app, QA_APP_STARTUP_ACTION_FRONTEND); note_world_action(prepared); }
    else qa_startup_application_print(app, "Usage: mvdisconnect [-a|--all] [conn_id]\n");
    return true;
  }
  if (eq(name, "disconnect")) { stage(app, QA_APP_STARTUP_ACTION_FRONTEND); return true; }
  qa_app_options model_options;
  bool owned = false;
  const qa_app_options *options = qa_startup_active_options(app);
  if (options == NULL) {
    qa_app_startup_selection_options(app->model, &model_options);
    options = &model_options;
    owned = true;
  }
  bool handled = false;
  if (eq(name, "connect")) {
    handled = true;
    if (arg_count != 1 || args[0] == NULL) {
      qa_startup_application_print(app, "Usage: connect <address>; use qts://<address> for mixed-game servers\n");
    } else {
      if (prepared == NULL) {
        if (owned) qa_app_options_destroy(&model_options);
        qa_raise("Error", 0, 0, "Connect has no prepared command owner");
      }
      qa_command_dialect dialect = qa_command_buffer_dialect(qa_app_prepared_startup_commands(prepared));
      bool unified = strncmp(args[0], "qts://", 6) == 0;
      const char *remote = unified ? args[0] + 6 : args[0];
      if (remote[0] == '\0') qa_startup_application_print(app, "Connect requires a server address.\n");
      else {
        qa_app_startup_action *action = stage(app, QA_APP_STARTUP_ACTION_INITIAL);
        qa_app_startup_options_copy(&action->options, options);
        action->options.network_kind = unified ? "unified-client" : dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE ? "q1-client"
            : dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD ? "qw-client" : dialect == QA_COMMAND_DIALECT_Q3 ? "q3-client" : "q2-client";
        free(action->options.network_host); action->options.network_host = NULL;
        action->options.network_port = 0;
        free(action->options.network_remote); action->options.network_remote = qa_startup_dup(remote);
      }
    }
  } else if (app->game == NULL && (eq(name, "map") || (prepared != NULL
      && qa_command_buffer_dialect(qa_app_prepared_startup_commands(prepared)) == QA_COMMAND_DIALECT_Q3
      && (eq(name, "devmap") || eq(name, "spmap") || eq(name, "spdevmap"))))) {
    handled = true;
    if (arg_count != 1 || args[0] == NULL) qa_startup_printf(app, "Usage: %s <name>\n", name);
    else {
      map_call call;
      memset(&call, 0, sizeof call);
      call.argument = args[0];
      qa_caught_error caught;
      if (!qa_try(map_body, &call, &caught)) qa_startup_printf(app, "%s\n", caught.message);
      else if (prepared != NULL && qa_command_buffer_dialect(qa_app_prepared_startup_commands(prepared)) == QA_COMMAND_DIALECT_Q3) {
        qa_cvar_registry *cvars = qa_app_prepared_startup_source(prepared);
        qa_cvar_read found;
        double game_type;
        /* Number(prepared.source.find("g_gametype")?.latchedValue ?? prepared.source.variableString("g_gametype")) */
        if (qa_cvar_registry_find(cvars, "g_gametype", &found) && found.latched_value != NULL) {
          game_type = qa_js_string_to_number(found.latched_value);
          qa_cvar_read_free(&found);
        } else {
          if (qa_cvar_registry_find(cvars, "g_gametype", &found)) qa_cvar_read_free(&found);
          char *text = qa_cvar_registry_variable_string(cvars, "g_gametype");
          game_type = qa_js_string_to_number(text);
          free(text);
        }
        qa_app_q3_map_launch *launch = malloc(sizeof *launch);
        if (launch == NULL) qa_range_error("Out of memory");
        qa_q3_product_policy retail = {QA_Q3_PRODUCT_RETAIL, QA_Q3_TEAM_ARENA_UI_RETAIL};
        qa_q3_product_policy policy = options->has_q3_product ? options->q3_product.policy : retail;
        qa_app_q3_map_launch_make(launch, policy, name, game_type);
        qa_app_startup_action *action = stage(app, QA_APP_STARTUP_ACTION_INITIAL);
        qa_app_startup_options_copy(&action->options, options);
        action->options.team_arena_skirmish = NULL;
        action->options.q3_map_launch = launch;
        action->q3_map_launch = launch;
        action->options.mode = launch->single_player ? "singleplayer" : "deathmatch";
        free(action->options.map); action->options.map = call.path;
        action->options.network_kind = "offline";
        free(action->options.network_host); action->options.network_host = NULL;
        free(action->options.network_remote); action->options.network_remote = NULL;
        action->options.network_port = 0;
      } else {
        qa_app_startup_action *action = stage(app, QA_APP_STARTUP_ACTION_INITIAL);
        qa_app_startup_options_copy(&action->options, options);
        free(action->options.map); action->options.map = call.path;
        action->options.network_kind = "offline";
        free(action->options.network_host); action->options.network_host = NULL;
        free(action->options.network_remote); action->options.network_remote = NULL;
        action->options.network_port = 0;
      }
    }
  } else if (app->game == NULL && eq(name, "load")) {
    handled = true;
    load_call call;
    memset(&call, 0, sizeof call);
    call.args = args; call.count = arg_count; call.directory = qa_app_startup_saves_directory(app->saves); call.source = source;
    call.allow_any = options->allow_load_paths;
    qa_caught_error caught;
    if (!qa_try(load_body, &call, &caught)) qa_startup_printf(app, "%s\n", caught.message);
    else {
      qa_app_startup_action *action = stage(app, QA_APP_STARTUP_ACTION_LOAD);
      action->path = call.path;
      action->source_product = call.source_product;
    }
  }
  if (owned) qa_app_options_destroy(&model_options);
  return handled;
}

/* TS frontendCommand(name, args, source). */
static void print_ctx(void *ctx, const char *text) { qa_startup_application_print(ctx, text); }
void qa_startup_application_frontend_command(qa_startup_application *app, const char *name,
    const char *const *args, size_t arg_count, qa_command_context source) {
  if (qa_startup_application_route_command(app, name, args, arg_count, source, NULL, true)) return;
  if (eq(name, "snd_restart") && app->graphics != NULL) {
    qa_app_startup_audio_restart_output(app->graphics->audio);
    return;
  }
  if (eq(name, "music") && app->graphics != NULL) {
    qa_app_startup_audio_queue_music_command(app->graphics->audio, args, arg_count, print_ctx, app);
    return;
  }
  if (eq(name, "cd") && app->graphics != NULL) {
    qa_app_startup_audio_queue_cd_command(app->graphics->audio, args, arg_count, print_ctx, app);
    return;
  }
  qa_startup_printf(app, "Cannot execute %s without an active world.\n", name);
}

/* TS connect(connection): the options handed to connectOptions. */
void qa_startup_application_connection_options(qa_startup_application *app,
    const qa_app_browser_connection *connection, qa_app_options *out) {
  bool qw = eq(connection->protocol, "qw");
  const char *family = qw ? "q1" : connection->protocol;
  qa_app_options model_options;
  qa_app_startup_selection_options(app->model, &model_options);
  const qa_app_options *active = qa_startup_active_options(app);
  double seats = active != NULL ? active->seats : model_options.seats;
  *out = model_options;
  free(out->movement_product); out->movement_product = NULL;
  out->has_q1_protocol = false;
  out->has_q2_protocol = false;
  out->has_remote_content = false;
  free(out->remote_content.directory); memset(&out->remote_content, 0, sizeof out->remote_content);
  if (connection->has_q2_protocol) {
    out->has_q2_protocol = true;
    out->q2_protocol.kind = connection->q2_protocol_kind;
    out->q2_protocol.version = connection->q2_protocol_version;
  }
  bool kex = connection->has_q2_protocol && eq(connection->q2_protocol_kind, "q2-kex");
  free(out->product);
  out->product = qa_startup_dup(qw ? "q1-quakeworld" : eq(family, "q1") ? "q1-classic-id1" : eq(family, "q2")
      ? (kex ? "q2-rerelease-baseq2" : "q2-classic-baseq2") : "q3-baseq3");
  free(out->map);
  out->map = qa_startup_dup(eq(family, "q1") ? "maps/e1m1.bsp" : eq(family, "q2") ? "maps/base1.bsp" : "maps/q3dm1.bsp");
  qa_game_family game_family = eq(family, "q1") ? QA_GAME_FAMILY_Q1 : eq(family, "q2") ? QA_GAME_FAMILY_Q2 : QA_GAME_FAMILY_Q3;
  out->movement = game_family;
  out->character = game_family;
  free(out->character_model);
  out->character_model = qa_startup_dup(eq(family, "q1") ? "player" : eq(family, "q2") ? "male" : "sarge");
  out->seats = seats;
  out->dedicated = false;
  out->rules = "standard";
  out->network_kind = qw ? "qw-client" : eq(family, "q1") ? "q1-client" : eq(family, "q2") ? "q2-client" : "q3-client";
  free(out->network_host); out->network_host = NULL;
  out->network_port = 0;
  free(out->network_remote); out->network_remote = qa_startup_dup(connection->remote);
}

/* TS lobbyJoinOptions(lobby). */
void qa_startup_application_lobby_join_options(qa_startup_application *app, const qa_lobby *lobby,
    const qa_executable_recipe *recipe, qa_app_options *out) {
  if (lobby->endpoint.kind == QA_NET_LOOPBACK) qa_raise("Error", 0, 0, "This lobby endpoint requires an in-process transport");
  qa_app_options model_options;
  qa_app_startup_selection_options(app->model, &model_options);
  qa_app_options selected = qa_app_application_options_for_recipe(&model_options, qa_app_startup_selection_catalog(app->model), recipe);
  char *remote = qa_net_address_key(&lobby->endpoint, true);
  if (remote == NULL) qa_range_error("Out of memory");
  bool member = false;
  double seats = 0;
  for (size_t i = 0; i < lobby->member_count && !member; i++)
    if (eq(lobby->members[i].account.id, app->lobby_account.id)) { member = true; seats = lobby->members[i].seats; }
  if (!member) {
    free(remote); qa_app_options_for_recipe_free(&selected); qa_app_options_destroy(&model_options);
    qa_raise("Error", 0, 0, "The local client is not a member of this lobby");
  }
  qa_app_startup_options_copy(out, &selected);
  qa_app_options_for_recipe_free(&selected);
  out->has_q1_protocol = false;
  out->has_q2_protocol = false;
  out->network_transport_kind = "";
  free(out->network_transport_relay); out->network_transport_relay = NULL;
  out->dedicated = false;
  out->seats = seats;
  free(out->network_host); out->network_host = NULL;
  out->network_port = 0;
  free(out->network_remote); out->network_remote = remote;
  const char *failure = NULL;
  if (lobby->wire.is_unified) out->network_kind = "unified-client";
  else {
    bool ipx = lobby->endpoint.kind == QA_NET_IPX;
    const char *transport = ipx ? model_options.network_transport_kind : NULL;
    if (ipx && (transport == NULL || transport[0] == '\0' || eq(transport, "udp"))) failure = "Choose an IPX transport to join this lobby";
    else if (transport != NULL && transport[0] != '\0') {
      out->network_transport_kind = transport;
      out->network_transport_relay = qa_startup_dup(model_options.network_transport_relay);
    }
    const char *kind = lobby->wire.protocol.kind;
    if (failure == NULL) {
      if (eq(kind, "q1-netquake") || eq(kind, "q1-fitzquake") || eq(kind, "q1-rmq")) {
        out->has_q1_protocol = true; out->q1_protocol = lobby->wire.protocol; out->network_kind = "q1-client";
      } else if (eq(kind, "q1-quakeworld")) out->network_kind = "qw-client";
      else if (eq(kind, "q1-quakeworld-donor-wide")) failure = "This lobby uses a replay-only QuakeWorld wire format";
      else if (eq(kind, "q2-classic") || eq(kind, "q2-r1q2") || eq(kind, "q2-q2pro") || eq(kind, "q2-rerelease")
               || eq(kind, "q2-kex") || eq(kind, "q2-private-classic")) {
        out->has_q2_protocol = true; out->q2_protocol = lobby->wire.protocol; out->network_kind = "q2-client";
      } else if (eq(kind, "q2-kex-demo")) failure = "This lobby advertises a recorded-only Quake II protocol";
      else if (eq(kind, "q3")) out->network_kind = "q3-client";
    }
  }
  qa_app_options_destroy(&model_options);
  if (failure != NULL) { qa_app_options_destroy(out); qa_raise("Error", 0, 0, "%s", failure); }
}

/* ---- public getters ------------------------------------------------------ */
qa_app_startup_selection_model *qa_startup_application_model(const qa_startup_application *app) { return app->model; }
qa_app_frontend_preferences *qa_startup_application_preferences(const qa_startup_application *app) { return app->preferences; }
qa_application *qa_startup_application_active_game(const qa_startup_application *app) { return app->game; }
bool qa_startup_application_input_seat(const qa_startup_application *app, qa_seat_id *out) {
  if (app->client == NULL || app->client->platform.kind != QA_APP_CLIENT_INPUT_MENU || app->graphics == NULL) return false;
  *out = qa_native_ui_controller_get_seat(qa_app_startup_menu_controller(app->graphics->menu));
  return true;
}
bool qa_startup_application_input(qa_startup_application *app, const qa_seat_input_event *event) {
  qa_app_client_bootstrap *client = app->client;
  if (client != NULL && client->platform.kind == QA_APP_CLIENT_INPUT_WORLD) {
    if (app->game != NULL) return qa_application_input(app->game, event);
    if (app->remote != NULL) return qa_remote_application_input(app->remote, event);
    return false;
  }
  if (client == NULL || client->local_count == 0) return false;
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(client->prepared, &count);
  for (size_t i = 0; i < count; i++)
    if (qa_same_seat(seats[i].id, client->locals[0].prepared)) return qa_seat_input_input(seats[i].input, event);
  return false;
}
bool qa_startup_application_quit_requested(const qa_startup_application *app) { return app->stopping; }
uint8_t *qa_startup_application_read_pixels(qa_startup_application *app, size_t *out_length) {
  if (app->graphics == NULL) qa_raise("Error", 0, 0, "Startup menu is not visible");
  return qa_app_native_renderer_read_pixels(app->graphics->renderer, out_length);
}
const qa_app_startup_action *qa_startup_application_pending(const qa_startup_application *app) {
  return app->has_pending ? &app->pending : NULL;
}
const qa_app_startup_recording_intent *qa_startup_application_pending_recording(const qa_startup_application *app) {
  return app->has_pending_recording ? &app->pending_recording : NULL;
}
const qa_app_startup_movie_request *qa_startup_application_pending_movie(const qa_startup_application *app) {
  return app->has_pending_movie ? &app->pending_movie : NULL;
}
const char *qa_startup_application_status(const qa_startup_application *app) { return app->status != NULL ? app->status : ""; }
