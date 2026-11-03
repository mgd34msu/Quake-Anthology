/* Application members that build a seat's menu and HUD controller (application.ts windowMutator, createSeatUi,
 * saveMenu, uiSourceOptions). GPL-2.0-or-later. */
#include "application_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
/* C-only: a record the application owns until destroy (TS GC), as the other application files keep theirs. */
static void keep_record(qa_application *app, void *record, void (*release)(void *record)) {
  if (app->seat_record_count == app->seat_record_capacity) {
    size_t capacity = app->seat_record_capacity == 0 ? 4 : app->seat_record_capacity * 2;
    app->seat_records = checked(realloc(app->seat_records, capacity * sizeof *app->seat_records));
    app->seat_record_capacity = capacity;
  }
  app->seat_records[app->seat_record_count].record = record;
  app->seat_records[app->seat_record_count++].release = release;
}

/* ---- windowMutator(renderer): operation => renderer.mutateWindow(operation) ---- */
static void run_window_mutation(void *mutation) { qa_ui_apply_window_mutation(mutation); }
void qa_application_window_mutator(void *renderer, const qa_ui_window_mutation *mutation) {
  qa_app_native_renderer_mutate_window(renderer, run_window_mutation, (void *)mutation);
}

/* ---- saveMenu(simulation, options) ---- */
typedef struct save_menu_record {
  qa_application *app;
  qa_app_startup_saves *saves; /* the list this service was built over (TS closure `saves`) */
  qa_ui_startup_save_row *rows; size_t row_capacity; /* list() view storage */
  char message[QA_CAUGHT_MESSAGE_CAPACITY];          /* the last refresh error text */
} save_menu_record;
static void save_menu_record_release(void *record) { free(((save_menu_record *)record)->rows); }
typedef struct startup_saves_box { qa_app_startup_saves *saves; } startup_saves_box;
static void startup_saves_box_release(void *record) { qa_app_startup_saves_destroy(((startup_saves_box *)record)->saves); }

typedef enum save_menu_operation_kind { SAVE_MENU_RESTART, SAVE_MENU_SAVE, SAVE_MENU_LOAD } save_menu_operation_kind;
typedef struct save_menu_operation {
  save_menu_record *record;
  save_menu_operation_kind kind;
  char *name, *overwrite, *id; /* save(name, overwrite) / load(id) arguments */
  qa_ui_saves_settle settle;   /* the menu's await of the queued promise */
} save_menu_operation;
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  return checked(strdup(text));
}
static void operation_free(save_menu_operation *operation) {
  free(operation->name); free(operation->overwrite); free(operation->id); free(operation);
}
/* The queued run: TS () => this.replaceWorld(...), saveGame(overwrite === null ? namedPath(name) : path(overwrite)),
 * restoreSavedGame(saves.path(id)). */
static void save_menu_run(qa_application *app, void *ctx) {
  save_menu_operation *operation = ctx;
  qa_app_startup_saves *saves = operation->record->saves;
  if (operation->kind == SAVE_MENU_RESTART) {
    const char *map = qa_app_loaded_content_recipe(app->loaded_content)->map.geometry.requested_path;
    char *copy = dup_text(map);
    qa_cleanup guard;
    qa_cleanup_push(&guard, free, copy);
    qa_application_replace_world(app, copy, NULL);
    qa_cleanup_pop(&guard);
    free(copy);
    return;
  }
  char *path = operation->kind == SAVE_MENU_SAVE && operation->overwrite == NULL
      ? qa_app_startup_saves_named_path(saves, operation->name)
      : dup_text(qa_app_startup_saves_path(saves, operation->kind == SAVE_MENU_SAVE ? operation->overwrite : operation->id));
  qa_cleanup guard;
  qa_cleanup_push(&guard, free, path);
  if (operation->kind == SAVE_MENU_SAVE) qa_application_save_game(app, path, NULL);
  else qa_application_restore_saved_game(app, path, NULL);
  qa_cleanup_pop(&guard);
  free(path);
}
/* TS resolve/reject of the queued promise: the awaiting menu continues, then the operation's storage is released. */
static void save_menu_settle(void *ctx, const qa_caught_error *error) {
  save_menu_operation *operation = ctx;
  qa_ui_saves_settle settle = operation->settle;
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
  if (error != NULL) (void)snprintf(message, sizeof message, "%s", error->message);
  operation_free(operation);
  settle.fn(settle.ctx, error == NULL ? NULL : message);
}
/* TS queue(run): rejects at once when closed or another operation is queued. */
static void save_menu_queue(save_menu_record *record, save_menu_operation *operation, qa_ui_saves_settle settle) {
  qa_application *app = record->app;
  if (app->closed || app->has_save_operation) {
    operation_free(operation);
    settle.fn(settle.ctx, "Another save operation is in progress.");
    return;
  }
  operation->settle = settle;
  app->save_operation.run = save_menu_run;
  app->save_operation.settle = save_menu_settle;
  app->save_operation.ctx = operation;
  app->has_save_operation = true;
}
static save_menu_operation *new_operation(save_menu_record *record, save_menu_operation_kind kind) {
  save_menu_operation *operation = checked(calloc(1, sizeof *operation));
  operation->record = record;
  operation->kind = kind;
  return operation;
}
static void save_menu_restart(void *ctx, qa_ui_saves_settle settle) {
  save_menu_record *record = ctx;
  save_menu_queue(record, new_operation(record, SAVE_MENU_RESTART), settle);
}
static qa_ui_startup_save_list save_menu_list(void *ctx) {
  save_menu_record *record = ctx;
  const qa_app_startup_save_list *list = qa_app_startup_saves_list(record->saves);
  if (list->row_count > record->row_capacity) {
    free(record->rows);
    record->rows = checked(calloc(list->row_count, sizeof *record->rows));
    record->row_capacity = list->row_count;
  }
  for (size_t i = 0; i < list->row_count; i++) {
    const qa_app_startup_save_row *row = &list->rows[i];
    record->rows[i] = (qa_ui_startup_save_row){row->id, row->label, row->map, row->game, row->saved_at_milliseconds, row->unavailable};
  }
  qa_ui_startup_save_list out = {record->rows, list->row_count, list->error};
  return out;
}
static void refresh_run(void *ctx) { qa_app_startup_saves_refresh(ctx); }
/* refresh: () => saves.refresh(): settles at once (StartupSaves.refresh is synchronous in C). */
static void save_menu_refresh(void *ctx, qa_ui_saves_settle settle) {
  save_menu_record *record = ctx;
  qa_caught_error caught;
  if (qa_try(refresh_run, record->saves, &caught)) { settle.fn(settle.ctx, NULL); return; }
  snprintf(record->message, sizeof record->message, "%s", caught.message);
  settle.fn(settle.ctx, record->message);
}
static const char *save_menu_unavailable(void *ctx, const char *action) {
  (void)action; /* TS unavailable: () => ...: the action is not read */
  save_menu_record *record = ctx;
  return record->app->network != NULL ? "Save/load unavailable while hosting a network game." : NULL;
}
static void save_menu_save(void *ctx, const char *name, const char *overwrite, qa_ui_saves_settle settle) {
  save_menu_record *record = ctx;
  save_menu_operation *operation = new_operation(record, SAVE_MENU_SAVE);
  operation->name = dup_text(name);
  operation->overwrite = dup_text(overwrite);
  save_menu_queue(record, operation, settle);
}
static void save_menu_load(void *ctx, const char *id, qa_ui_saves_settle settle) {
  save_menu_record *record = ctx;
  save_menu_operation *operation = new_operation(record, SAVE_MENU_LOAD);
  operation->id = dup_text(id);
  save_menu_queue(record, operation, settle);
}
qa_ui_saved_game_menu_service qa_application_save_menu(qa_application *app, qa_app_shared_simulation *simulation, const qa_app_options *options) {
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(app->loaded_content);
  /* Each travel discovers a new catalog, so the list must not keep the first world's. */
  if (app->saved_games != NULL && app->saved_games_catalog != (const void *)catalog) app->saved_games = NULL;
  if (app->saved_games == NULL) {
    size_t external_count = 0;
    const char *const *external = qa_application_external_save_directories(app, &external_count);
    startup_saves_box *box = checked(calloc(1, sizeof *box));
    box->saves = qa_app_startup_saves_create(catalog, qa_application_save_directory(app), external, external_count);
    if (box->saves == NULL) { free(box); qa_range_error("Out of memory"); }
    keep_record(app, box, startup_saves_box_release);
    app->saved_games = box->saves;
  }
  app->saved_games_catalog = catalog;
  save_menu_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, save_menu_record_release);
  record->app = app;
  record->saves = app->saved_games;
  qa_ui_saved_game_menu_service service;
  memset(&service, 0, sizeof service);
  service.ctx = record;
  service.list = save_menu_list;
  service.refresh = save_menu_refresh;
  if (qa_application_level_recovery_available(app, simulation, options)) {
    service.has_recovery = true;
    service.recovery_restart = save_menu_restart;
  }
  service.unavailable = save_menu_unavailable;
  service.save = save_menu_save;
  service.load = save_menu_load;
  return service;
}

/* ---- uiSourceOptions(simulation, rerelease, seat, clients = this.clientCvars) ---- */
typedef struct source_options_record {
  qa_application *app;
  qa_app_rerelease_presentation *rerelease;
  qa_seat_id seat;
  qa_app_seat_ui_lobby lobby;
  qa_app_seat_ui_rankings rankings;
  qa_ui_gameplay_settings_source gameplay;
  qa_app_base_arena_menu_service base_arena;
} source_options_record;
typedef struct lobby_return { qa_application *app; } lobby_return;
static void return_to_lobby_run(void *ctx) { qa_application_return_to_lobby(((lobby_return *)ctx)->app); }
/* returnToLobby: () => { void this.returnToLobby().catch(error => this.host.print(`Return to lobby failed: ...`)) } */
static void ui_return_to_lobby(void *ctx) {
  source_options_record *record = ctx;
  lobby_return call = {record->app};
  qa_caught_error caught;
  if (!qa_try(return_to_lobby_run, &call, &caught))
    qa_application_printf(record->app, "Return to lobby failed: %s: %s\n", caught.name, caught.message);
}
static const qa_ui_ranking_account_actions *ui_rankings_current(void *ctx) {
  source_options_record *record = ctx;
  return qa_application_ranking_account_actions(record->app, record->seat);
}
static bool ui_rankings_take(void *ctx) {
  source_options_record *record = ctx;
  return qa_application_take_ranking_menu_request(record->app, record->seat);
}
/* nativeHudLocalizer: content => rerelease.sourceLocalizer(seat, content), applied to the text. */
static char *ui_native_hud_localizer(void *ctx, qa_content_id content, const char *text, const char *const *args, size_t count) {
  source_options_record *record = ctx;
  return qa_app_rerelease_source_localize(record->rerelease, record->seat, content, text, args, count);
}
static char *ui_localize(void *ctx, qa_content_id content, const char *text, const char *const *args, size_t count) {
  source_options_record *record = ctx;
  return qa_app_rerelease_localize_message(record->rerelease, record->seat, content, text, args, count);
}
static qa_cvar_registry *client_cvars_of(qa_application *app, qa_seat_id seat, const qa_seat_id *seats,
                                         qa_cvar_registry *const *cvars, size_t count) {
  if (seats == NULL) return qa_application_client_cvars_get(app, seat);
  for (size_t i = 0; i < count; i++) if (qa_same_seat(seats[i], seat)) return cvars[i];
  return NULL;
}
void qa_application_ui_source_options(qa_application *app, qa_app_shared_simulation *simulation, qa_app_rerelease_presentation *rerelease,
    qa_seat_id seat, const qa_seat_id *client_seats, qa_cvar_registry *const *client_cvars, size_t client_count,
    qa_app_seat_ui_options *out) {
  qa_cvar_registry *cvars = qa_application_source_cvars(app, simulation);
  qa_cvar_registry *client = client_cvars_of(app, seat, client_seats, client_cvars, client_count);
  source_options_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, NULL);
  record->app = app;
  record->rerelease = rerelease;
  record->seat = seat;
  bool has_base_arena = qa_application_base_arena_menu(app, simulation, seat, &record->base_arena);
  /* weaponPickupPolicy: simulation.q1Source() === null ? "source-owned" : "shared" */
  bool shared_pickups = qa_app_shared_sim_q1_source(simulation) != NULL;
  if (app->host.lobby != NULL) {
    record->lobby = (qa_app_seat_ui_lobby){record, ui_return_to_lobby};
    out->lobby = &record->lobby;
  }
  if (qa_app_shared_sim_q3_source(simulation) != NULL) {
    record->rankings = (qa_app_seat_ui_rankings){record, ui_rankings_current, ui_rankings_take};
    out->rankings = &record->rankings;
  }
  if (cvars != NULL) {
    record->gameplay.cvars = cvars;
    record->gameplay.has_weapon_pickup_policy = true;
    record->gameplay.weapon_pickup_shared = shared_pickups;
    record->gameplay.client = client; /* client === undefined ? {} : { client } */
    out->gameplay = &record->gameplay;
  }
  if (has_base_arena) out->base_arena = &record->base_arena;
  out->native_hud_localizer = ui_native_hud_localizer; out->native_hud_localizer_ctx = record;
  out->localize = ui_localize; out->localize_ctx = record;
}

/* ---- createSeatUi(local, art, input, mutateWindow, simulation, font, audio, typography, rerelease, content, options, qvm,
 * clientCvars?) ---- */
typedef struct seat_ui_record {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_local_input *local;
  qa_seat_id seat;
  qa_app_sim_presentation_access access;
  qa_ui_host_server_settings host_settings;
  bool has_bound; qa_app_shared_sim_bound_settings bound;
  qa_ui_setting_binding language;
  qa_ui_saved_game_menu_service saves;
  qa_ui_setting_binding view;
  qa_app_team_arena_result_service team_arena;
} seat_ui_record;
static void seat_ui_record_release(void *record) {
  seat_ui_record *r = record;
  if (r->has_bound) qa_app_shared_sim_bound_settings_free(&r->bound);
}
static void ui_quit(void *ctx) { qa_application_request_quit(((seat_ui_record *)ctx)->app); }
static void ui_command(void *ctx, const char *name, const char *const *args, size_t count) {
  seat_ui_record *record = ctx;
  qa_application_queue_command(record->app, name, args, count, &record->seat);
}
/* hostSettings.bindings: () => simulation.serverSettings(). The bindings depend on the simulation's source strategy and
 * recipe only, so the first result is kept for the record's life and later calls return it (their reads stay live). */
static const qa_bound_server_setting *ui_server_bindings(void *ctx, size_t *out_count) {
  seat_ui_record *record = ctx;
  if (!record->has_bound) {
    record->bound = qa_app_shared_sim_server_settings(record->simulation);
    record->has_bound = true;
  }
  *out_count = record->bound.count;
  return record->bound.bindings;
}
/* language failed: error => local.console.print(`Language reload failed: ${String(error)}\n`) */
static void ui_language_failed(void *ctx, const char *error) {
  seat_ui_record *record = ctx;
  size_t length = strlen(error) + 32;
  char *text = checked(malloc(length));
  snprintf(text, length, "Language reload failed: %s\n", error);
  qa_seat_console_print(record->local->console, text);
  free(text);
}
qa_app_seat_ui *qa_application_create_seat_ui(qa_application *app, qa_app_local_input *local, qa_native_ui_art *art,
    qa_app_application_input *input, qa_app_native_renderer *renderer, qa_app_shared_simulation *simulation, qa_text_font_selection font,
    qa_app_application_audio *audio, const qa_app_menu_typography *typography, qa_app_rerelease_presentation *rerelease,
    qa_app_loaded_content *content, const qa_app_options *options, bool qvm, const qa_seat_id *client_cvar_seats,
    qa_cvar_registry *const *client_cvars, size_t client_cvar_count) {
  qa_session_seat *seat = local->player.seat;
  seat_ui_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, seat_ui_record_release);
  record->app = app;
  record->simulation = simulation;
  record->local = local;
  record->seat = qa_session_seat_id(seat);
  record->access = qa_app_shared_sim_presentation_access(simulation);
  qa_app_seat_ui_options spec;
  memset(&spec, 0, sizeof spec);
  spec.local = local;
  spec.art = art;
  spec.input = input;
  spec.mutate_window = qa_application_window_mutator; spec.mutate_ctx = renderer; /* windowMutator(renderer) */
  spec.simulation = &record->access;
  spec.font = font;
  spec.audio = audio;
  spec.quit = ui_quit; spec.quit_ctx = record;
  spec.command = ui_command; spec.command_ctx = record;
  spec.typography = typography;
  record->host_settings = (qa_ui_host_server_settings){ui_server_bindings, record, qa_application_server_profile_store(app)};
  spec.host_settings = &record->host_settings;
  if (qa_app_rerelease_language_binding(rerelease, record->seat, qa_app_loaded_content_recipe(content)->map.entities.content,
                                        ui_language_failed, record, &record->language))
    spec.language = &record->language;
  record->saves = qa_application_save_menu(app, simulation, options);
  spec.saves = &record->saves;
  record->view = qa_app_view_settings_binding(app->view_settings);
  spec.view_setting = &record->view;
  spec.llm = app->host.llm_settings;
  spec.guest_ui = qvm;
  if (qa_application_team_arena_results(app, simulation, seat, &record->team_arena)) spec.team_arena = &record->team_arena;
  qa_application_ui_source_options(app, simulation, rerelease, record->seat, client_cvar_seats, client_cvars, client_cvar_count, &spec);
  qa_app_seat_ui *ui = qa_app_seat_ui_create(&spec);
  if (ui == NULL) qa_range_error("Out of memory");
  return ui;
}
