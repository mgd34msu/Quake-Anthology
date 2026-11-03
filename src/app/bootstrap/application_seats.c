/* Port of src/app/bootstrap/application.ts, group P3 (seats, guests and
 * arenas; TS publishBorrowed .. replaceWorld). Layout and per-member status:
 * docs/application_layout.md. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/q1_session_actions.h"
#include "qa/app/bootstrap/startup_commands.h"
#include "qa/app/bootstrap/startup_config.h"
#include "qa/foundation/js.h"
#include "qa/content/catalog/index.h"
#include "qa/network/common/js_number.h"
#include "qa/content/mounts.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/llm/errors.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/q3_client/userinfo.h"
#include "qa/app/bootstrap/q3_client/client_state.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static void grow(void **items, size_t *capacity, size_t needed, size_t size) {
  if (needed <= *capacity) return;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  while (next < needed) next *= 2;
  *items = checked(realloc(*items, next * size));
  *capacity = next;
}
/* JS String.prototype.toLowerCase comparison (ASCII; see PORTING.md strings). */
static bool same_lower(const char *a, const char *b) {
  for (; *a != '\0' && *b != '\0'; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
  return *a == *b;
}

/* TS source.options.product === "missionpack". */
static bool source_missionpack(qa_app_q3_source_runtime *source) {
  const char *product = qa_app_q3_source_runtime_options(source)->product;
  return product != NULL && strcmp(product, "missionpack") == 0;
}

/* ---- isTeamArenaSkirmish(simulation = this.simulation) ---- */
bool qa_application_is_team_arena_skirmish(qa_application *app, qa_app_shared_simulation *simulation) {
  if (simulation == NULL) simulation = app->world_simulation;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  if (source == NULL || !source_missionpack(source)) return false;
  char *next = qa_application_cvar_string(qa_app_q3_source_runtime_host(source)->cvars, "nextmap");
  bool skirmish = strcmp(next, "teamarena-results") == 0;
  free(next);
  return skirmish;
}

/* ---- prepareBaseArenaProgress(simulation, content, restoring) ---- */
static char **arena_list_files(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  *out_count = 0;
  char **files = qa_content_mounted_list_files(ctx, directory, extension, out_count);
  if (files == NULL && *out_count > 0) qa_range_error("Out of memory");
  return files;
}
static char *arena_open_text(void *ctx, const char *path) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return NULL;
  char *text = checked(malloc(opened.byte_length + 1));
  if (opened.byte_length > 0) memcpy(text, opened.bytes, opened.byte_length);
  text[opened.byte_length] = '\0';
  free(opened.bytes);
  return text;
}
static void register_archive(qa_cvar_registry *cvars, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, name, value, QA_CVAR_FLAG_ARCHIVE, &snapshot)) qa_cvar_read_free(&snapshot);
}
static qa_application_base_arena_progress *base_arena_progress_get(qa_application *app, qa_app_shared_simulation *simulation) {
  for (size_t i = 0; i < app->base_arena_progress_count; i++)
    if (app->base_arena_progress[i].simulation == simulation) return &app->base_arena_progress[i];
  return NULL;
}
static void base_arena_progress_release(qa_application_base_arena_progress *state) {
  qa_app_base_arena_progression_destroy(state->progression);
  qa_app_base_arena_catalog_destroy(state->catalog);
  free(state->catalog);
  if (state->has_result) free(state->result.result.awards);
  free(state->result_game.players);
}
/* The BaseArenaProgression catalog prepareBaseArenaProgress builds (TS progression.catalog). */
static qa_app_arena_progression_catalog progression_levels(const qa_app_base_arena_catalog *catalog) {
  qa_app_arena_progression_catalog levels = { catalog->regular_count, catalog->regular_count, -1, -1 };
  bool has_training = false, has_final = false;
  for (size_t i = 0; i < catalog->arena_count; i++) {
    const qa_app_base_arena *arena = &catalog->arenas[i];
    if (!has_training && same_lower(arena->special, "training")) { levels.training = arena->number; has_training = true; }
    if (!has_final && same_lower(arena->special, "final")) { levels.final = arena->number; has_final = true; }
    if (arena->special[0] != '\0') levels.total_levels++;
  }
  return levels;
}
void qa_application_prepare_base_arena_progress(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, bool restoring) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  if (source == NULL || source_missionpack(source)
      || qa_app_q3_source_runtime_game_type(source) != QA_Q3_BASE_GT_SINGLE_PLAYER) return;
  qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(source)->cvars;
  qa_app_arena_mounts mounts = { (void *)qa_app_loaded_content_mounts(content), arena_list_files, arena_open_text };
  qa_app_base_arena_catalog *catalog = checked(malloc(sizeof *catalog));
  *catalog = qa_app_read_base_arena_catalog(mounts);
  qa_app_arena_progression_catalog levels = progression_levels(catalog);
  qa_app_base_arena_progression *progression = checked(qa_app_base_arena_progression_create(cvars, levels));
  const char *requested = qa_app_loaded_content_recipe(content)->map.geometry.requested_path;
  const qa_app_base_arena *arena = NULL;
  for (size_t i = 0; i < catalog->arena_count && arena == NULL; i++)
    if (same_lower(catalog->arenas[i].map, requested)) arena = &catalog->arenas[i];
  if (arena != NULL && !restoring) {
    register_archive(cvars, "ui_spSelection", "0");
    qa_application_cvar_set(cvars, "ui_spSelection", QA_JS_NUMBER(arena->selection), true);
  }
  register_archive(cvars, "g_spRoundIdentity", "");
  register_archive(cvars, "g_spCompletedRound", "");
  char *identity = qa_application_cvar_string(cvars, "g_spRoundIdentity");
  if (!restoring || identity[0] == '\0') {
    char uuid[37];
    qa_llm_random_uuid(uuid);
    qa_application_cvar_set(cvars, "g_spRoundIdentity", uuid, true);
  }
  free(identity);
  /* TS WeakMap set: a second preparation of the same world replaces the record. */
  qa_application_base_arena_progress *state = base_arena_progress_get(app, simulation);
  if (state != NULL) base_arena_progress_release(state);
  else {
    grow((void **)&app->base_arena_progress, &app->base_arena_progress_capacity, app->base_arena_progress_count + 1,
        sizeof *app->base_arena_progress);
    state = &app->base_arena_progress[app->base_arena_progress_count++];
  }
  memset(state, 0, sizeof *state);
  state->simulation = simulation;
  state->catalog = catalog;
  state->progression = progression;
}

/* ---- static startupScriptReader(content, options, scripts) ---- */
typedef struct startup_reader {
  qa_app_prepared_scoped_read_fn read; void *read_ctx; /* createStartupScriptReader's reader */
  const char *base_roots[2]; const char *game_roots[2];
  char *seat_root;
} startup_reader;
static uint8_t *startup_reader_mounted(void *ctx, const char *name, size_t *out_length) {
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!qa_app_console_script_files_read_mounted_script(ctx, name, &bytes, &length)) return NULL;
  *out_length = length;
  return bytes;
}
static char *startup_reader_user(void *ctx, const char *name, qa_command_context source) {
  return qa_app_console_script_files_read(ctx, name, source);
}
static char *startup_reader_read(void *ctx, const char *name, qa_command_context source, qa_app_startup_script_scope scope) {
  startup_reader *reader = ctx;
  return reader->read(reader->read_ctx, name, source, scope);
}
static size_t startup_roots(const qa_catalog_discovered_product *product, const char **roots) {
  size_t count = 0;
  if (product->user_content != NULL && product->user_content->root != NULL) roots[count++] = product->user_content->root;
  if (product->loose_root != NULL) roots[count++] = product->loose_root;
  return count;
}
bool qa_application_startup_script_reader(qa_app_loaded_content *content, const qa_app_options *options,
    qa_app_console_script_files *scripts, qa_app_prepared_scoped_read_fn *out_read, void **out_read_ctx) {
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog,
      qa_app_loaded_content_recipe(content)->engine_behavior.content);
  const qa_catalog_discovered_product *base = product->expectation.base_product == NULL ? product
      : qa_content_catalog_product(catalog, product->expectation.base_product);
  startup_reader *reader = checked(calloc(1, sizeof *reader));
  qa_app_startup_script_reader_options created;
  memset(&created, 0, sizeof created);
  created.ctx = scripts;
  created.mounted = startup_reader_mounted;
  created.user = startup_reader_user;
  created.base_loose_roots = reader->base_roots;
  created.base_loose_root_count = startup_roots(base, reader->base_roots);
  created.game_loose_roots = reader->game_roots;
  created.game_loose_root_count = startup_roots(product, reader->game_roots);
  reader->seat_root = qa_app_console_config_root(options->user_content_root);
  created.seat_root = reader->seat_root;
  qa_app_create_startup_script_reader(&created, &reader->read, &reader->read_ctx);
  *out_read = startup_reader_read;
  *out_read_ctx = reader;
  return true;
}
/* C-only: frees a reader startupScriptReader returned (TS GC of the closure). */
void qa_application_startup_script_reader_free(void *read_ctx) {
  startup_reader *reader = read_ctx;
  if (reader == NULL) return;
  free(reader->read_ctx);
  free(reader->seat_root);
  free(reader);
}

/* ---- queueLocalGuestCommand(world, command) ---- */
void qa_application_queue_local_guest_command(qa_application *app, qa_application_local_q3_guest_world *world,
    qa_application_command_request *command) {
  if (world == app->local_guest) (void)qa_event_ring_admit(&app->requested_commands, command);
  else (void)qa_event_ring_admit(&world->pending_commands, command);
  memset(command, 0, sizeof *command);
}

/* ---- shared Q3 source reads ---- */
static qa_q3_game_level *source_level(qa_app_q3_source_runtime *source) {
  return qa_app_q3_source_subsystems_owner_level(qa_app_q3_source_runtime_owner(source));
}
static double skill_bucket(double value) {
  return value >= 5 ? 5 : value >= 4 ? 4 : value >= 3 ? 3 : value >= 2 ? 2 : 1;
}
static qa_app_player_progress_store *player_progress(qa_application *app) {
  if (app->player_progress == NULL) {
    char *path = checked(malloc(strlen(app->input_config.root) + sizeof "/player-progress.json"));
    strcpy(path, app->input_config.root);
    strcat(path, "/player-progress.json");
    char *normalized = qa_application_path_normalize(path);
    free(path);
    app->player_progress = checked(qa_app_player_progress_store_open(normalized));
    free(normalized);
  }
  return app->player_progress;
}
static char *local_seat_participant(qa_session_seat *seat) {
  char *text = checked(malloc(48));
  snprintf(text, 48, "local-seat:%s", QA_JS_NUMBER((double)qa_session_seat_id(seat).index));
  return text;
}
static void set_arena_result(qa_application_base_arena_progress *state, qa_app_arena_postgame *game, qa_app_arena_progression_result result) {
  if (state->has_result) free(state->result.result.awards);
  free(state->result_game.players);
  state->result_game = *game;
  state->result = qa_app_arena_postgame_presentation_make(&state->result_game, result);
  state->has_result = true;
}

/* ---- completeBaseArena(args) ---- */
typedef struct arena_cvar_value { const char *name; char *value; } arena_cvar_value;
typedef struct arena_save_attempt { qa_application *app; } arena_save_attempt;
static void arena_save_body(void *ctx) {
  qa_application *app = ((arena_save_attempt *)ctx)->app;
  qa_application_save_source_archive(app, qa_application_options(app), app->loaded_content, app->world_simulation, app->team_arena_overrides);
}
void qa_application_complete_base_arena(qa_application *app, const char *const *args, size_t arg_count) {
  qa_application_base_arena_progress *state = base_arena_progress_get(app, app->world_simulation);
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(app->world_simulation);
  if (state == NULL || source == NULL) qa_raise("Error", 0, 0, "Base arena postgame lost its source owner");
  qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(source)->cvars;
  const qa_app_base_arena *arena = qa_app_base_arena_for_map(state->catalog, qa_application_options(app)->map);
  double skill = skill_bucket(qa_cvar_registry_variable_value(cvars, "g_spSkill"));
  qa_app_arena_postgame game = qa_app_parse_arena_postgame(args, arg_count, arena->number, skill);
  char *identity = qa_application_cvar_string(cvars, "g_spRoundIdentity");
  size_t round_length = strlen(identity) + 48;
  char *round = checked(malloc(round_length));
  snprintf(round, round_length, "%s:%s", identity, QA_JS_NUMBER(source_level(source)->base.start_time));
  free(identity);
  char *completed = qa_application_cvar_string(cvars, "g_spCompletedRound");
  bool replay = strcmp(completed, round) == 0;
  free(completed);
  if (replay) {
    qa_app_arena_progression_result result;
    memset(&result, 0, sizeof result);
    result.rank = game.result.rank;
    result.completed_tier = qa_app_base_arena_progression_completed_tier(state->progression, arena->number);
    result.unlocked_movie = -1;
    result.next_level = qa_app_base_arena_progression_current_level(state->progression);
    set_arena_result(state, &game, result);
    state->announced = true; state->movie_played = true;
    free(round);
    return;
  }
  qa_session_seat *local = NULL;
  for (size_t i = 0; i < app->local_seat_count && local == NULL; i++)
    if ((double)app->local_seat_clients[i].slot == game.player_client) local = app->local_seats[i];
  if (local != NULL) {
    qa_app_player_progress_event event;
    memset(&event, 0, sizeof event);
    char *participant = local_seat_participant(local);
    event.kind = "match-completed"; event.source = "q3"; event.participant = participant;
    event.event = round; event.map = arena->map; event.score = game.result.frags;
    (void)qa_app_player_progress_store_record(player_progress(app), event);
    free(participant);
  }
  static const char *const names[] = { "g_spScores1", "g_spScores2", "g_spScores3", "g_spScores4", "g_spScores5", "g_spAwards",
      "g_spVideos", "g_spCompletedRound" };
  arena_cvar_value before[8];
  for (size_t i = 0; i < 8; i++) { before[i].name = names[i]; before[i].value = qa_application_cvar_string(cvars, names[i]); }
  qa_app_arena_progression_result result = qa_app_base_arena_progression_record(state->progression, game.result);
  qa_application_cvar_set(cvars, "g_spCompletedRound", round, true);
  free(round);
  arena_save_attempt attempt = { app };
  qa_caught_error caught;
  if (!qa_try(arena_save_body, &attempt, &caught)) {
    for (size_t i = 0; i < 8; i++) { qa_application_cvar_set(cvars, before[i].name, before[i].value, true); free(before[i].value); }
    free(result.awards);
    free(game.players);
    qa_rethrow(&caught);
  }
  for (size_t i = 0; i < 8; i++) free(before[i].value);
  set_arena_result(state, &game, result);
  state->result_at = qa_application_performance_now();
  state->announced = false; state->movie_played = false;
  /* TS result.musicCommand.split(" ").slice(1). */
  char *music = checked(malloc(strlen(state->result.music_command) + 1));
  strcpy(music, state->result.music_command);
  const char *parts[16]; size_t part_count = 0;
  char *cursor = music;
  for (;;) {
    char *space = strchr(cursor, ' ');
    if (space != NULL) *space = '\0';
    if (part_count < 16) parts[part_count++] = cursor;
    if (space == NULL) break;
    cursor = space + 1;
  }
  qa_seat_id seat;
  if (local != NULL) seat = qa_session_seat_id(local);
  qa_application_queue_command(app, "music", part_count > 1 ? parts + 1 : NULL, part_count > 1 ? part_count - 1 : 0,
      local != NULL ? &seat : NULL);
  free(music);
}

/* C-only storage of the service records the seat members return (TS closures). */
static void *seat_record(qa_application *app, size_t size, void (*release)(void *record)) {
  void *record = checked(calloc(1, size));
  grow((void **)&app->seat_records, &app->seat_record_capacity, app->seat_record_count + 1, sizeof *app->seat_records);
  app->seat_records[app->seat_record_count].record = record;
  app->seat_records[app->seat_record_count++].release = release;
  return record;
}

/* ---- baseArenaMenu(simulation, seat) ---- */
typedef struct base_arena_menu {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_seat_id seat;
  bool has_selection; qa_app_arena_selection selection;
  char *player_name;
  qa_app_arena_progress_row *rows; char **row_values; size_t row_count;
} base_arena_menu;
static qa_application_base_arena_progress *menu_state(base_arena_menu *menu) {
  qa_application_base_arena_progress *state = base_arena_progress_get(menu->app, menu->simulation);
  if (state == NULL) qa_raise("Error", 0, 0, "Base arena menu outlived its progress record");
  return state;
}
static qa_cvar_registry *menu_cvars(base_arena_menu *menu) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(menu->simulation);
  return source == NULL ? NULL : qa_app_q3_source_runtime_host(source)->cvars;
}
static const qa_app_arena_selection *menu_selection(void *ctx) {
  base_arena_menu *menu = ctx;
  qa_application_base_arena_progress *state = menu_state(menu);
  qa_cvar_registry *cvars = menu_cvars(menu);
  char *saved = cvars == NULL ? NULL : qa_application_cvar_string(cvars, "ui_spSelection");
  if (menu->has_selection) qa_app_arena_selection_destroy(&menu->selection);
  menu->selection = qa_app_arena_selection_make(state->catalog, state->progression, saved == NULL ? "" : saved);
  menu->has_selection = true;
  free(saved);
  return &menu->selection;
}
static double menu_skill(void *ctx) {
  qa_cvar_registry *cvars = menu_cvars(ctx);
  return skill_bucket(cvars == NULL ? 2 : qa_cvar_registry_variable_value(cvars, "g_spSkill"));
}
static void menu_play(void *ctx, const char *map, double skill) {
  base_arena_menu *menu = ctx;
  qa_application_base_arena_progress *state = menu_state(menu);
  const qa_app_base_arena *row = NULL;
  for (size_t i = 0; i < state->catalog->arena_count && row == NULL; i++)
    if (strcmp(state->catalog->arenas[i].map, map) == 0) row = &state->catalog->arenas[i];
  if (row == NULL || !qa_app_base_arena_progression_level_available(state->progression, row->number))
    qa_raise("Error", 0, 0, "That arena is not unlocked in this profile.");
  qa_cvar_registry *cvars = menu_cvars(menu);
  if (cvars == NULL) qa_raise("Error", 0, 0, "Arena selection lost its game owner");
  qa_application_cvar_set(cvars, "g_spSkill", QA_JS_NUMBER(skill), true);
  const char *args[1] = { map };
  qa_application_queue_command(menu->app, "spmap", args, 1, &menu->seat);
}
/* TS netname.replace(/\^[0-9]/g, "").toLowerCase(). */
static char *announcer_name(const char *netname) {
  size_t length = strlen(netname);
  char *out = checked(malloc(length + 1));
  size_t n = 0;
  for (size_t i = 0; i < length; i++) {
    if (netname[i] == '^' && netname[i + 1] >= '0' && netname[i + 1] <= '9') { i++; continue; }
    out[n++] = (char)tolower((unsigned char)netname[i]);
  }
  out[n] = '\0';
  return out;
}
static qa_q3_game_client *source_client(qa_app_shared_simulation *simulation, double slot) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  return source == NULL ? NULL : qa_q3_entity_pool_client_at(qa_app_q3_source_runtime_pool(source), slot);
}
static bool menu_result(void *ctx, const qa_app_arena_postgame_presentation **out) {
  base_arena_menu *menu = ctx;
  qa_application_base_arena_progress *state = menu_state(menu);
  if (!state->has_result) return false;
  const qa_app_arena_postgame_presentation *result = &state->result;
  double elapsed = qa_application_performance_now() - state->result_at;
  if (!state->announced && elapsed >= result->winner_announcement_after_milliseconds) {
    state->announced = true;
    char *name = NULL;
    if (result->podium_count > 0) {
      qa_q3_game_client *winner = source_client(menu->simulation, result->podium[0].client);
      name = announcer_name(winner == NULL || winner->pers.netname == NULL ? "" : winner->pers.netname);
    }
    if (result->result.rank == 1 || (name != NULL && name[0] != '\0')) {
      char sound[256];
      if (result->result.rank == 1) snprintf(sound, sizeof sound, "sound/player/announce/youwin.wav");
      else snprintf(sound, sizeof sound, "sound/player/announce/%s_wins.wav", name);
      const char *args[1] = { sound };
      qa_application_queue_command(menu->app, "play", args, 1, &menu->seat);
    }
    free(name);
  }
  double awards = (double)result->result.award_count * 2000;
  if (!state->movie_played && result->result.unlocked_movie != -1 && elapsed >= 5000 + qa_js_max(5000, awards)) {
    state->movie_played = true;
    qa_app_arena_progression_catalog levels = progression_levels(state->catalog);
    char movie[64];
    if (levels.final != -1 && result->result.completed_tier == levels.regular_levels / 4 + 1) snprintf(movie, sizeof movie, "end.RoQ");
    else snprintf(movie, sizeof movie, "tier%s.RoQ", QA_JS_NUMBER(result->result.unlocked_movie));
    const char *args[1] = { movie };
    qa_application_queue_command(menu->app, "cinematic", args, 1, &menu->seat);
  }
  *out = result;
  return true;
}
static const char *menu_player_name(void *ctx, double client) {
  base_arena_menu *menu = ctx;
  free(menu->player_name);
  qa_q3_game_client *value = source_client(menu->simulation, client);
  char buffer[48];
  if (value == NULL) snprintf(buffer, sizeof buffer, "Player %s", QA_JS_NUMBER(client + 1));
  const char *name = value == NULL ? buffer : value->pers.netname == NULL ? "" : value->pers.netname;
  menu->player_name = checked(malloc(strlen(name) + 1));
  strcpy(menu->player_name, name);
  return menu->player_name;
}
static const qa_app_arena_progress_row *menu_progress(void *ctx, size_t *out_count) {
  base_arena_menu *menu = ctx;
  qa_application_base_arena_progress *state = menu_state(menu);
  for (size_t i = 0; i < menu->row_count; i++) free(menu->row_values[i]);
  static const char *const medals[] = { "Accuracy", "Impressive", "Excellent", "Gauntlet", "Frags", "Perfect" };
  size_t count = state->catalog->arena_count + 6;
  menu->rows = checked(realloc(menu->rows, count * sizeof *menu->rows));
  menu->row_values = checked(realloc(menu->row_values, count * sizeof *menu->row_values));
  for (size_t i = 0; i < state->catalog->arena_count; i++) {
    const qa_app_base_arena *arena = &state->catalog->arenas[i];
    qa_app_arena_best best = qa_app_base_arena_progression_best(state->progression, arena->number);
    char text[96];
    if (best.rank == 0) snprintf(text, sizeof text, "%s", qa_app_base_arena_progression_level_available(state->progression, arena->number) ? "Available" : "Locked");
    else {
      char rank[40];
      snprintf(rank, sizeof rank, "%s", QA_JS_NUMBER(best.rank));
      snprintf(text, sizeof text, "Best rank %s, skill %s", rank, QA_JS_NUMBER(best.skill));
    }
    menu->row_values[i] = checked(malloc(strlen(text) + 1));
    strcpy(menu->row_values[i], text);
    menu->rows[i].label = arena->title; menu->rows[i].value = menu->row_values[i];
  }
  for (size_t medal = 0; medal < 6; medal++) {
    size_t i = state->catalog->arena_count + medal;
    const char *value = QA_JS_NUMBER(qa_app_base_arena_progression_award(state->progression, (double)medal));
    menu->row_values[i] = checked(malloc(strlen(value) + 1));
    strcpy(menu->row_values[i], value);
    menu->rows[i].label = medals[medal]; menu->rows[i].value = menu->row_values[i];
  }
  menu->row_count = count;
  *out_count = count;
  return menu->rows;
}
static void menu_retry(void *ctx) {
  base_arena_menu *menu = ctx;
  const char *args[1] = { qa_application_options(menu->app)->map };
  qa_application_queue_command(menu->app, "spmap", args, 1, &menu->seat);
}
static void menu_next(void *ctx) {
  base_arena_menu *menu = ctx;
  qa_application_base_arena_progress *state = menu_state(menu);
  double current = qa_app_base_arena_progression_current_level(state->progression);
  for (size_t i = 0; i < state->catalog->arena_count; i++) {
    if (state->catalog->arenas[i].number != current) continue;
    const char *args[1] = { state->catalog->arenas[i].map };
    qa_application_queue_command(menu->app, "spmap", args, 1, &menu->seat);
    return;
  }
}
static void menu_quit(void *ctx) { qa_application_request_quit(((base_arena_menu *)ctx)->app); }
static void menu_reset(void *ctx) {
  base_arena_menu *menu = ctx;
  qa_application_command_request request;
  memset(&request, 0, sizeof request);
  request.target = QA_APPLICATION_TARGET_SOURCE;
  request.name = checked(malloc(sizeof "arena-reset"));
  strcpy(request.name, "arena-reset");
  request.has_seat = true; request.seat = menu->seat;
  (void)qa_event_ring_admit(&menu->app->requested_commands, &request);
}
static void base_arena_menu_free(void *record) {
  base_arena_menu *menu = record;
  if (menu->has_selection) qa_app_arena_selection_destroy(&menu->selection);
  free(menu->player_name);
  for (size_t i = 0; i < menu->row_count; i++) free(menu->row_values[i]);
  free(menu->rows); free(menu->row_values);
}
/* false is TS undefined. The service lives until the application is destroyed. */
bool qa_application_base_arena_menu(qa_application *app, qa_app_shared_simulation *simulation, qa_seat_id seat,
    qa_app_base_arena_menu_service *out) {
  if (base_arena_progress_get(app, simulation) == NULL) return false;
  base_arena_menu *menu = seat_record(app, sizeof *menu, base_arena_menu_free);
  menu->app = app; menu->simulation = simulation; menu->seat = seat;
  memset(out, 0, sizeof *out);
  out->ctx = menu;
  out->selection = menu_selection; out->skill = menu_skill; out->play = menu_play; out->result = menu_result;
  out->player_name = menu_player_name; out->progress = menu_progress; out->retry = menu_retry; out->next = menu_next;
  out->quit = menu_quit; out->reset = menu_reset;
  return true;
}

/* C-only: the P3 storage the application owns (TS GC). */
void qa_application_seats_free(qa_application *app) {
  for (size_t i = 0; i < app->base_arena_progress_count; i++) base_arena_progress_release(&app->base_arena_progress[i]);
  app->base_arena_progress_count = 0;
  free(app->base_arena_progress);
  app->base_arena_progress = NULL;
  app->base_arena_progress_capacity = 0;
  for (size_t i = 0; i < app->seat_record_count; i++) {
    if (app->seat_records[i].release != NULL) app->seat_records[i].release(app->seat_records[i].record);
    free(app->seat_records[i].record);
  }
  free(app->seat_records);
  app->seat_records = NULL;
  app->seat_record_count = app->seat_record_capacity = 0;
  qa_application_bots_free(app);
}

/* ---- teamArenaResults(simulation, seat) ---- */
typedef struct team_arena_results {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_session_seat *seat;
  char *title, *time;
} team_arena_results;
static void team_arena_results_free(void *record) {
  team_arena_results *results = record;
  free(results->title); free(results->time);
}
static bool results_read(void *ctx, qa_app_team_arena_result *out) {
  team_arena_results *results = ctx;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(results->simulation);
  if (source == NULL || source_level(source)->base.intermission_time == 0) return false;
  qa_q3_entity_pool *pool = qa_app_q3_source_runtime_pool(source);
  qa_q3_game_level *level = source_level(source);
  double slot = (double)qa_session_client_id(qa_session_seat_client(results->seat)).slot;
  qa_q3_game_client *human = qa_q3_entity_pool_client_at(pool, slot);
  double team = human->sess.session_team;
  bool team_game = qa_app_q3_source_runtime_game_type(source) >= QA_Q3_BASE_GT_CTF;
  double score = team_game ? qa_q3_base_player_state_slots_get(&level->team_scores, team)
      : qa_q3_base_player_state_slots_get(&human->ps.persistant, QA_Q3_BASE_PERS_SCORE);
  double opponent;
  if (team_game) opponent = qa_q3_base_player_state_slots_get(&level->team_scores, team == QA_Q3_BASE_TEAM_RED ? QA_Q3_BASE_TEAM_BLUE : QA_Q3_BASE_TEAM_RED);
  else {
    opponent = -9999;
    for (size_t i = 0; i < (size_t)pool->max_clients && i < QA_Q3_MAX_CLIENTS; i++) {
      qa_q3_game_client *client = pool->clients[i];
      if (client == NULL || client == human) continue;
      opponent = qa_js_max(opponent, qa_q3_base_player_state_slots_get(&client->ps.persistant, QA_Q3_BASE_PERS_SCORE));
    }
  }
  qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(source)->cvars;
  free(results->title); free(results->time);
  results->title = qa_application_cvar_string(cvars, "ui_scoreMap");
  results->time = qa_application_cvar_string(cvars, "ui_scoreTime");
  out->title = results->title;
  out->won = team_game ? score > opponent : level->base.sorted_clients[0] == slot;
  out->score = score;
  out->opponent = opponent;
  out->points = qa_cvar_registry_variable_value(cvars, "ui_scoreScore");
  out->time = results->time;
  return true;
}
static const qa_app_team_arena_demo *results_demo(team_arena_results *results) {
  for (size_t i = 0; i < results->app->team_arena_demo_count; i++)
    if (results->app->team_arena_demo_simulations[i] == results->simulation) return results->app->team_arena_demos[i];
  return NULL;
}
static bool results_demo_available(void *ctx) {
  team_arena_results *results = ctx;
  return results->app->prepared_startup != NULL && results_demo(results) != NULL;
}
static void results_demo_play(void *ctx) {
  team_arena_results *results = ctx;
  const qa_app_team_arena_demo *demo = results_demo(results);
  qa_app_prepared_startup *prepared = results->app->prepared_startup;
  if (demo == NULL || prepared == NULL) return;
  const char *argv[2] = { "+demo", demo->path };
  qa_app_startup_command command = qa_app_read_startup_command(argv, 2, 0);
  char *text = checked(malloc(strlen(command.text) + 2));
  strcpy(text, command.text);
  strcat(text, "\n");
  free(command.text);
  qa_command_context context;
  memset(&context, 0, sizeof context);
  qa_seat_id seat = qa_session_seat_id(results->seat);
  context.session = seat.session;
  context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
  context.origin.seat = seat;
  context.origin.client = qa_session_client_id(qa_session_seat_client(results->seat));
  qa_command_buffer_append(qa_app_prepared_startup_commands(prepared), text, &context, QA_COMMAND_DIALECT_Q3);
  free(text);
}
static void results_command(team_arena_results *results, const char *name) {
  qa_seat_id seat = qa_session_seat_id(results->seat);
  qa_application_queue_command(results->app, name, NULL, 0, &seat);
}
static void results_next(void *ctx) { results_command(ctx, "teamarena-next"); }
static void results_retry(void *ctx) { results_command(ctx, "teamarena-retry"); }
static void results_quit(void *ctx) { qa_application_request_quit(((team_arena_results *)ctx)->app); }
bool qa_application_team_arena_results(qa_application *app, qa_app_shared_simulation *simulation, qa_session_seat *seat,
    qa_app_team_arena_result_service *out) {
  if (!qa_application_is_team_arena_skirmish(app, simulation)) return false;
  team_arena_results *results = seat_record(app, sizeof *results, team_arena_results_free);
  results->app = app; results->simulation = simulation; results->seat = seat;
  memset(out, 0, sizeof *out);
  out->ctx = results;
  out->read = results_read;
  out->has_demo = true;
  out->demo_available = results_demo_available;
  out->demo_play = results_demo_play;
  out->next = results_next; out->retry = results_retry; out->quit = results_quit;
  return true;
}

/* ---- prepareKeys(content, simulation) / publishKeys() ---- */
static qa_app_key_profile *key_profile_get(qa_application *app, qa_app_shared_simulation *simulation) {
  for (size_t i = 0; i < app->key_profile_count; i++)
    if (app->key_profile_simulations[i] == simulation) return app->key_profiles[i];
  return NULL;
}
qa_app_key_profile *qa_application_prepare_keys(qa_application *app, qa_app_loaded_content *content, qa_app_shared_simulation *simulation) {
  qa_app_key_profile *current = key_profile_get(app, simulation);
  if (current != NULL) return current;
  qa_cvar_registry *cvars = qa_application_source_cvars(app, simulation);
  if (cvars == NULL) qa_raise("Error", 0, 0, "Q3 key profile requires its actual source registry");
  qa_app_options options = *qa_application_options(app);
  options.product = (char *)qa_app_loaded_content_recipe(content)->engine_behavior.content; /* borrowed for the call */
  qa_q3_application_product product;
  if (qa_app_loaded_content_q3_product(content, &product)) { options.has_q3_product = true; options.q3_product = product; }
  qa_app_key_profile *profile = qa_app_keys_prepare(app->keys, &options, qa_app_loaded_content_catalog(content), cvars);
  if (profile == NULL) qa_raise("Error", 0, 0, "Q3 guest has no Q3 key profile");
  size_t capacity = app->key_profile_capacity;
  grow((void **)&app->key_profile_simulations, &capacity, app->key_profile_count + 1, sizeof *app->key_profile_simulations);
  grow((void **)&app->key_profiles, &app->key_profile_capacity, app->key_profile_count + 1, sizeof *app->key_profiles);
  app->key_profile_simulations[app->key_profile_count] = simulation;
  app->key_profiles[app->key_profile_count++] = profile;
  return profile;
}
void qa_application_publish_keys(qa_application *app) {
  qa_app_key_profile *profile = key_profile_get(app, app->world_simulation);
  qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
  if (profile != NULL && cvars != NULL) qa_app_keys_publish(app->keys, profile, cvars);
}

/* ---- loadClientArchive(seat, content, options = this.options) ---- */
qa_cvar_archive_entry *qa_application_load_client_archive(qa_application *app, qa_session_seat *seat, qa_app_loaded_content *content,
    const qa_app_options *options, size_t *out_count) {
  if (options == NULL) options = qa_application_options(app);
  qa_config_store store = qa_application_client_config(options, content);
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  const char *index = QA_JS_NUMBER((double)qa_session_seat_id(seat).index);
  const char *owner[4] = { "client", recipe->engine_behavior.content, recipe->engine_behavior.provider, index };
  qa_cvar_archive_entry *entries = qa_app_load_cvar_archive(&store, owner, 4, qa_application_source_dialect(app, content), out_count);
  qa_config_store_destroy(&store);
  return entries;
}

/* ---- recordPlayerProgress(source) ---- */
void qa_application_record_player_progress(qa_application *app, const qa_app_sim_presentation_event *source) {
  const char *family = NULL, *id = NULL;
  bool has_actor = false; qa_actor_id actor = { 0 };
  if (strcmp(source->kind, "q1") == 0 && strcmp(source->q1->kind, "achievement") == 0) {
    family = "q1"; id = source->q1->id; has_actor = source->q1->has_player; actor = source->q1->player;
  } else if (strcmp(source->kind, "q2-rerelease") == 0 && strcmp(source->q2_rerelease->kind, "achievement") == 0) {
    family = "q2"; id = source->q2_rerelease->id;
  }
  const char *completion = qa_app_source_level_completion(source);
  if ((family == NULL || id == NULL || id[0] == '\0') && completion == NULL) return;
  qa_app_player_progress_store *store = player_progress(app);
  const char *map = qa_application_options(app)->map;
  for (size_t i = 0; i < app->local_seat_count; i++) {
    qa_session_seat *seat = app->local_seats[i];
    qa_client_id client = qa_session_client_id(qa_session_seat_client(seat));
    if (source->has_recipient) {
      qa_client_id recipient;
      if (!qa_app_shared_sim_player_client(app->world_simulation, source->recipient, &recipient) || !qa_same_client(recipient, client)) continue;
    }
    if (family != NULL && has_actor) {
      qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(app->world_simulation, actor);
      if (player == NULL || !qa_same_client(player->client, client)) continue;
    }
    char *participant = local_seat_participant(seat);
    if (family != NULL && id != NULL && id[0] != '\0') {
      size_t length = strlen(source->content) + strlen(id) + 16;
      char *event_name = checked(malloc(length));
      snprintf(event_name, length, "achievement:%s:%s", source->content, id);
      qa_app_player_progress_event event;
      memset(&event, 0, sizeof event);
      event.kind = "achievement"; event.source = strcmp(family, "q1") == 0 ? "q1" : "q2";
      event.participant = participant; event.event = event_name; event.award = id;
      (void)qa_app_player_progress_store_record(store, event);
      free(event_name);
    }
    if (completion != NULL) {
      size_t length = strlen(source->content) + strlen(map) + 16;
      char *event_name = checked(malloc(length));
      snprintf(event_name, length, "level:%s:%s", source->content, map);
      qa_app_player_progress_event event;
      memset(&event, 0, sizeof event);
      event.kind = "level-completed"; event.source = completion;
      event.participant = participant; event.event = event_name; event.map = map;
      (void)qa_app_player_progress_store_record(store, event);
      free(event_name);
    }
    free(participant);
  }
}

static char *dup_seat_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = checked(malloc(length));
  memcpy(copy, text, length);
  return copy;
}
/* Keeps an already allocated record with the application's seat records (TS GC). */
static void *seat_record_keep(qa_application *app, void *record, void (*release)(void *record)) {
  grow((void **)&app->seat_records, &app->seat_record_capacity, app->seat_record_count + 1, sizeof *app->seat_records);
  app->seat_records[app->seat_record_count].record = record;
  app->seat_records[app->seat_record_count++].release = release;
  return record;
}

/* ---- startTeamArenaSkirmish(setup) ---- */
void qa_application_start_team_arena_skirmish(qa_application *app, const qa_app_team_arena_skirmish *setup) {
  qa_command_buffer *commands = app->source_commands;
  if (commands == NULL || qa_app_shared_sim_q3_source(app->world_simulation) == NULL)
    qa_raise("Error", 0, 0, "Team Arena requires the native Quake III source command owner");
  for (size_t i = 0; i < setup->bot_count; i++) {
    const qa_app_team_arena_bot *bot = &setup->bots[i];
    char skill[40], delay[40];
    qa_net_js_number(setup->skill, skill, sizeof skill);
    qa_net_js_number(bot->delay_milliseconds, delay, sizeof delay);
    size_t length = strlen(bot->ai) + strlen(skill) + strlen(bot->team) + strlen(delay) + strlen(bot->name) + 16;
    char *text = checked(malloc(length));
    snprintf(text, length, "addbot %s %s %s %s %s\n", bot->ai, skill, bot->team[0] == '\0' ? "," : bot->team, delay, bot->name);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, text);
    qa_command_buffer_append(commands, text, NULL, -1);
    qa_cleanup_pop(&cleanup);
    free(text);
  }
  if (app->graphical == NULL) return;
  size_t length = strlen(setup->player_team) + 8;
  char *team = checked(malloc(length));
  snprintf(team, length, "team %s", setup->player_team);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, team);
  for (size_t i = 0; i < app->graphical->q3_count; i++) qa_app_application_q3_client_reliable(app->graphical->q3[i].client, team);
  qa_cleanup_pop(&cleanup);
  free(team);
}

/* ---- requestRestoredScores() ---- */
void qa_application_request_restored_scores(qa_application *app) {
  if (app->graphical == NULL) return;
  for (size_t i = 0; i < app->graphical->q3_count; i++) qa_app_application_q3_client_reliable(app->graphical->q3[i].client, "score");
}

/* ---- restoreSourceInputAngles(input, simulation, seat?) ---- */
/* ((angle << 16) >> 16) * (360 / 65536) */
static double short_angle(double value) {
  int32_t word = qa_js_to_int32(value);
  return (double)(int16_t)(uint16_t)(word & 0xffff) * (360.0 / 65536.0);
}
void qa_application_restore_source_input_angles(qa_application *app, qa_app_application_input *input, qa_app_shared_simulation *simulation,
    const qa_seat_id *seat) {
  (void)app; /* restoreSourceInputAngles reads only its arguments */
  size_t count = qa_app_application_input_local_count(input);
  if (qa_app_shared_sim_q2_native(simulation, NULL, NULL)) {
    for (size_t i = 0; i < count; i++) {
      qa_app_local_input *local = qa_app_application_input_local(input, i);
      if (seat == NULL || qa_same_seat(qa_session_seat_id(local->player.seat), *seat))
        qa_input_command_builder_set_view_angles(local->builder, qa_app_shared_sim_player_view(simulation, local->player.actor).angles);
    }
    return;
  }
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  qa_app_q3_server_state *state = source != NULL ? qa_app_q3_source_runtime_host(source)->server_state
      : guest != NULL ? qa_app_q3_qvm_server_game_state(guest) : NULL;
  if (state == NULL) return;
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(input, i);
    if (qa_input_command_builder_dialect(local->builder) != QA_COMMAND_DIALECT_Q3
        || (seat != NULL && !qa_same_seat(qa_session_seat_id(local->player.seat), *seat))) continue;
    qa_app_q3_wire_user_command command;
    double slot = (double)qa_session_client_id(qa_session_seat_client(local->player.seat)).slot;
    qa_vec3 angles = {0, 0, 0};
    if (qa_app_q3_server_state_get_user_command(state, slot, &command))
      angles = (qa_vec3){short_angle(command.angles.x), short_angle(command.angles.y), short_angle(command.angles.z)};
    qa_input_command_builder_set_view_angles(local->builder, angles);
  }
}

/* ---- publishLocalGuestSnapshots(local = this.localGuest, simulation = this.simulation) ---- */
void qa_application_publish_local_guest_snapshots_with(qa_application *app, qa_application_local_q3_guest_world *local,
    qa_app_shared_simulation *simulation) {
  (void)app; /* the defaults are resolved by qa_application_publish_local_guest_snapshots */
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  if (local == NULL || guest == NULL) return;
  qa_app_q3_player players[64];
  size_t player_count = qa_app_q3_qvm_server_game_players(guest, players, 64);
  for (size_t i = 0; i < local->seat_count; i++) {
    qa_app_local_q3_client_state *state = local->seats[i].state;
    const qa_app_q3_player *player = NULL;
    for (size_t p = 0; p < player_count && player == NULL; p++)
      if (players[p].source_entity == qa_app_local_q3_client_state_client_number(state)) player = &players[p];
    if (player == NULL) continue;
    qa_app_q3_snapshot snapshot = local->authority.host.snapshot(local->authority.host.ctx, *player);
    qa_q3_wire_snapshot wire;
    memset(&wire, 0, sizeof wire);
    wire.message_number = (int64_t)qa_app_local_q3_client_state_server_message_sequence(state) + 1;
    wire.server_time = (int64_t)local->authority.host.time(local->authority.host.ctx);
    wire.delta_number = -1;
    wire.flags = 0;
    wire.server_command_number = (int64_t)qa_app_local_q3_client_state_server_command_sequence(state);
    wire.parse_entities_number = 0;
    wire.player_state = *(const qa_q3_player_state_record *)snapshot.player;
    size_t mask = snapshot.area_mask_length < sizeof wire.area_mask ? snapshot.area_mask_length : sizeof wire.area_mask;
    memcpy(wire.area_mask, snapshot.area_mask, mask);
    wire.area_mask_len = mask;
    wire.entities = snapshot.entity_count == 0 ? NULL : checked(calloc(snapshot.entity_count, sizeof *wire.entities));
    for (size_t e = 0; e < snapshot.entity_count; e++) wire.entities[e] = *(const qa_q3_entity_state_record *)snapshot.entities[e];
    wire.entity_count = snapshot.entity_count;
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, wire.entities);
    qa_app_local_q3_client_state_receive_snapshot(state, &wire, 0);
    qa_cleanup_pop(&cleanup);
    free(wire.entities);
  }
}
void qa_application_publish_local_guest_snapshots(qa_application *app) {
  qa_application_publish_local_guest_snapshots_with(app, app->local_guest, app->world_simulation);
}

/* ---- prepareLocalGuestSeat(world, simulation, seat, cvars, userinfo, initial) ---- */
typedef struct local_guest_bindings {
  qa_application *app;
  qa_application_local_q3_guest_world *world;
  qa_app_shared_simulation *simulation;
  qa_session_seat *seat;
  qa_app_q3_qvm_server_game *guest;
} local_guest_bindings;
static void lg_assert_current(void *ctx) {
  local_guest_bindings *b = ctx;
  if (qa_session_client_is_closed(qa_session_seat_client(b->seat)) || qa_app_q3_qvm_server_game_is_retired(b->guest))
    qa_raise("Error", 0, 0, "Guest client world is retired");
}
static bool lg_source_actor(local_guest_bindings *b, double number, qa_actor_id *out) {
  qa_owned_actor actor;
  if (!qa_session_actor_registry_at_source(b->simulation->actors, qa_app_shared_sim_recipe(b->simulation)->map.entities.provider,
          (int64_t)number, &actor)) return false;
  *out = actor.id;
  return true;
}
static qa_actor_id lg_actor_at(void *ctx, double number) {
  local_guest_bindings *b = ctx;
  if (number == 1022) return b->world->world_sound;
  qa_actor_id actor;
  if (!lg_source_actor(b, number, &actor)) {
    char text[40];
    qa_net_js_number(number, text, sizeof text);
    qa_raise("Error", 0, 0, "Guest entity %s is not published", text);
  }
  return actor;
}
static bool lg_sound_actor(void *ctx, double number, qa_actor_id *out) {
  local_guest_bindings *b = ctx;
  if (number == 1022) { *out = b->world->world_sound; return true; }
  return lg_source_actor(b, number, out);
}
static void lg_system_info(void *ctx, const char *info) {
  local_guest_bindings *b = ctx;
  (void)info; /* TS systemInfo: () => world.seats.get(seat.id)?.client?.refreshSystemInfo() takes no argument */
  qa_seat_id id = qa_session_seat_id(b->seat);
  for (size_t i = 0; i < b->world->seat_count; i++)
    if (qa_same_seat(b->world->seat_ids[i], id) && b->world->seats[i].client != NULL)
      qa_app_application_q3_client_refresh_system_info(b->world->seats[i].client);
}
static void lg_map_restart(void *ctx) { (void)ctx; qa_raise("Error", 0, 0, "Local guest map restart is unsupported"); }
static void lg_level_shot(void *ctx) {
  local_guest_bindings *b = ctx;
  qa_application_command_request request;
  memset(&request, 0, sizeof request);
  request.target = QA_APPLICATION_TARGET_APPLICATION;
  request.name = dup_seat_text("clientLevelShot");
  request.has_seat = true; request.seat = qa_session_seat_id(b->seat);
  qa_application_queue_local_guest_command(b->app, b->world, &request);
}
static void lg_print(void *ctx, const char *text) { qa_application_print(((local_guest_bindings *)ctx)->app, text); }
void qa_application_prepare_local_guest_seat(qa_application *app, qa_application_local_q3_guest_world *world,
    qa_app_shared_simulation *simulation, qa_session_seat *seat, qa_cvar_registry *cvars, const char *userinfo, const void *initial) {
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  if (guest == NULL) qa_raise("Error", 0, 0, "Local guest authority is missing");
  local_guest_bindings *bindings = seat_record(app, sizeof *bindings, NULL);
  bindings->app = app; bindings->world = world; bindings->simulation = simulation; bindings->seat = seat; bindings->guest = guest;
  qa_app_local_q3_client_bindings host = {bindings, lg_assert_current, lg_actor_at, lg_sound_actor, lg_system_info, lg_map_restart,
      lg_level_shot, lg_print};
  qa_app_local_q3_client_state *state = checked(qa_app_local_q3_client_state_create(initial, host));
  qa_seat_id id = qa_session_seat_id(seat);
  size_t index = world->seat_count;
  for (size_t i = 0; i < world->seat_count; i++) if (qa_same_seat(world->seat_ids[i], id)) index = i;
  if (index == world->seat_count) {
    if (world->seat_count == world->seat_capacity) {
      world->seat_capacity = world->seat_capacity == 0 ? 4 : world->seat_capacity * 2;
      world->seat_ids = checked(realloc(world->seat_ids, world->seat_capacity * sizeof *world->seat_ids));
      world->seats = checked(realloc(world->seats, world->seat_capacity * sizeof *world->seats));
    }
    world->seat_count++;
  }
  world->seat_ids[index] = id;
  world->seats[index].state = state;
  world->seats[index].cvars = cvars;
  world->seats[index].userinfo = dup_seat_text(userinfo);
  world->seats[index].client = NULL;
}

/* ---- prepareRestoredGuestSeat(world, simulation, seat, content, options, previous?, archive = []) ---- */
qa_actor_id qa_application_prepare_restored_guest_seat(qa_application *app, qa_application_local_q3_guest_world *world,
    qa_app_shared_simulation *simulation, qa_session_seat *seat, qa_app_loaded_content *content, const qa_app_options *options,
    qa_cvar_registry *previous, const qa_cvar_archive_entry *archive, size_t archive_count) {
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  qa_app_q3_player player;
  if (guest == NULL || !qa_app_q3_qvm_server_game_player(guest, qa_session_client_id(qa_session_seat_client(seat)), &player))
    qa_raise("Error", 0, 0, "Restored guest is missing a connected local player");
  qa_app_q3_server_state *state = qa_app_q3_qvm_server_game_state(guest);
  const char *userinfo = qa_app_q3_server_state_get_userinfo(state, player.source_entity);
  if (userinfo == NULL) qa_raise("Error", 0, 0, "Restored guest is missing local userinfo");
  qa_cvar_registry *cvars = qa_application_create_client_cvars(app, simulation, content, options, seat, userinfo, previous, archive,
      archive_count, false);
  /* Baseline against the locally built string (as initial admission does), not the
   * gamecode-normalized guest copy, or the first frame sync spuriously resends userinfo. */
  char *info = checked(qa_cvar_registry_info_string(cvars, QA_CVAR_FLAG_USER_INFO, -1));
  size_t length = strlen(info) + 16;
  char *local = checked(malloc(length));
  snprintf(local, length, "%s\\ip\\localhost", info);
  free(info);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, local);
  double server_id = qa_cvar_registry_variable_value(qa_app_q3_server_state_cvars(state), "sv_serverid");
  const void *initial = world->authority.host.game_state(world->authority.host.ctx, player, server_id);
  qa_application_prepare_local_guest_seat(app, world, simulation, seat, cvars, local, initial);
  qa_cleanup_pop(&cleanup);
  free(local);
  qa_app_q3_wire_user_command command;
  if (qa_app_q3_server_state_get_user_command(state, player.source_entity, &command)) {
    qa_seat_id id = qa_session_seat_id(seat);
    /* The guest state's user command is the WireUserCommand the TS appends (int32 words). */
    qa_q3_wire_user_command wire;
    memset(&wire, 0, sizeof wire);
    wire.server_time = qa_js_to_int32(command.server_time);
    wire.angles[0] = qa_js_to_int32(command.angles.x);
    wire.angles[1] = qa_js_to_int32(command.angles.y);
    wire.angles[2] = qa_js_to_int32(command.angles.z);
    wire.forwardmove = qa_js_to_int32(command.forwardmove);
    wire.rightmove = qa_js_to_int32(command.rightmove);
    wire.upmove = qa_js_to_int32(command.upmove);
    wire.buttons = qa_js_to_int32(command.buttons);
    wire.weapon = qa_js_to_int32(command.weapon);
    for (size_t i = 0; i < world->seat_count; i++)
      if (qa_same_seat(world->seat_ids[i], id))
        (void)qa_q3_wire_command_history_append(qa_app_local_q3_client_state_commands(world->seats[i].state), &wire);
  }
  return player.actor;
}

/* ---- prepareSavedClients(slots, botClients) ---- */
typedef struct saved_clients_record {
  qa_application_saved_clients result;
  qa_client_id *clients;
  qa_session_client **added, **removed;
  double *bot_slots, *bot_generations; qa_session_client **bot_clients; size_t bot_count;
} saved_clients_record;
static void saved_clients_release(void *record) {
  saved_clients_record *r = record;
  free(r->clients); free(r->added); free(r->removed); free(r->bot_slots); free(r->bot_generations); free(r->bot_clients);
  free(r);
}
static qa_session_client *saved_resolve_bot(void *ctx, qa_client_id saved) {
  saved_clients_record *r = ctx;
  for (size_t i = 0; i < r->bot_count; i++)
    if (r->bot_slots[i] == (double)saved.slot) return r->bot_generations[i] == (double)saved.generation ? r->bot_clients[i] : NULL;
  return NULL;
}
typedef struct saved_clients_call {
  qa_application *app;
  saved_clients_record *record;
  const double *slots; size_t slot_count;
  const qa_app_sim_bot_transport_connection *bot_clients; size_t bot_count;
  qa_client_id *humans; size_t human_count;
  const qa_app_sim_bot_client *previous; size_t previous_count;
  size_t added_count;
} saved_clients_call;
static qa_session_client *previous_bot(saved_clients_call *c, double slot) {
  for (size_t i = 0; i < c->previous_count; i++)
    if ((double)qa_session_client_id(c->previous[i].client).slot == slot) return c->previous[i].client;
  return NULL;
}
static bool saved_bot(saved_clients_call *c, double slot) {
  for (size_t i = 0; i < c->bot_count; i++) if (c->bot_clients[i].client_slot == slot) return true;
  return false;
}
static void saved_clients_body(void *ctx) {
  saved_clients_call *c = ctx;
  saved_clients_record *r = c->record;
  for (size_t i = 0; i < c->bot_count; i++) {
    double slot = c->bot_clients[i].client_slot;
    qa_session_client *existing = previous_bot(c, slot);
    if (existing != NULL && qa_session_client_is_closed(existing)) qa_raise("Error", 0, 0, "Current bot client is closed");
    qa_session_client *client = existing != NULL ? existing : checked(qa_engine_session_prepare_client(c->app->session, (int64_t)slot, NULL));
    if (existing == NULL) r->added[c->added_count++] = client;
    r->bot_slots[r->bot_count] = slot;
    r->bot_generations[r->bot_count] = c->bot_clients[i].client_generation;
    r->bot_clients[r->bot_count++] = client;
  }
  for (size_t i = 0; i < c->slot_count; i++) {
    bool found = false;
    for (size_t b = 0; b < r->bot_count && !found; b++)
      if (r->bot_slots[b] == c->slots[i]) { r->clients[i] = qa_session_client_id(r->bot_clients[b]); found = true; }
    for (size_t h = 0; h < c->human_count && !found; h++)
      if ((double)c->humans[h].slot == c->slots[i]) { r->clients[i] = c->humans[h]; found = true; }
    if (!found) qa_raise("Error", 0, 0, "Saved client slot has no restored session identity");
  }
}
qa_application_saved_clients *qa_application_prepare_saved_clients(qa_application *app, const double *slots, size_t slot_count,
    const qa_app_sim_bot_transport_connection *bot_clients, size_t bot_count) {
  saved_clients_call c;
  memset(&c, 0, sizeof c);
  c.app = app; c.slots = slots; c.slot_count = slot_count; c.bot_clients = bot_clients; c.bot_count = bot_count;
  c.previous_count = qa_application_bot_clients(app, &c.previous);
  size_t identity_count = 0;
  const qa_client_id *identities = qa_app_shared_sim_client_identities(app->world_simulation, &identity_count);
  qa_client_id *humans = checked(calloc(identity_count + 1, sizeof *humans));
  size_t human_count = 0;
  for (size_t i = 0; i < identity_count; i++) {
    qa_session_client *bot = previous_bot(&c, (double)identities[i].slot);
    if (bot != NULL && qa_same_client(qa_session_client_id(bot), identities[i])) continue;
    humans[human_count++] = identities[i];
  }
  c.humans = humans; c.human_count = human_count;
  size_t human_slots = 0;
  bool mismatch = false;
  for (size_t i = 0; i < slot_count; i++) {
    if (saved_bot(&c, slots[i])) continue;
    human_slots++;
  }
  if (human_count != human_slots) mismatch = true;
  for (size_t h = 0; h < human_count && !mismatch; h++) {
    bool present = false;
    for (size_t i = 0; i < slot_count; i++) if (!saved_bot(&c, slots[i]) && slots[i] == (double)humans[h].slot) present = true;
    if (!present) mismatch = true;
  }
  if (mismatch) { free(humans); qa_raise("Error", 0, 0, "Saved human players do not match the connected session client slots"); }
  for (size_t i = 0; i < app->local_seat_count; i++) {
    qa_client_id client = app->local_seat_clients[i];
    bool human = false;
    for (size_t h = 0; h < human_count; h++) if (qa_same_client(humans[h], client)) human = true;
    if (saved_bot(&c, (double)client.slot) || !human) { free(humans); qa_raise("Error", 0, 0, "Saved bot slots conflict with a local seat"); }
  }
  saved_clients_record *r = checked(calloc(1, sizeof *r));
  r->clients = checked(calloc(slot_count + 1, sizeof *r->clients));
  r->added = checked(calloc(bot_count + 1, sizeof *r->added));
  r->bot_slots = checked(calloc(bot_count + 1, sizeof *r->bot_slots));
  r->bot_generations = checked(calloc(bot_count + 1, sizeof *r->bot_generations));
  r->bot_clients = checked(calloc(bot_count + 1, sizeof *r->bot_clients));
  c.record = r;
  qa_caught_error error;
  if (!qa_try(saved_clients_body, &c, &error)) {
    size_t failures = 1;
    for (size_t i = 0; i < c.added_count; i++) qa_session_client_close(r->added[i]);
    free(humans);
    saved_clients_release(r);
    if (failures > 1) qa_raise("AggregateError", 0, (long long)failures, "Saved client preparation failed");
    qa_rethrow(&error);
  }
  free(humans);
  r->removed = checked(calloc(c.previous_count + 1, sizeof *r->removed));
  size_t removed = 0;
  for (size_t i = 0; i < c.previous_count; i++)
    if (!saved_bot(&c, (double)qa_session_client_id(c.previous[i].client).slot)) r->removed[removed++] = c.previous[i].client;
  r->result.clients = r->clients; r->result.client_count = slot_count;
  r->result.added = r->added; r->result.added_count = c.added_count;
  r->result.removed = r->removed; r->result.removed_count = removed;
  r->result.resolve_bot_client = saved_resolve_bot; r->result.ctx = r;
  (void)seat_record_keep(app, r, saved_clients_release);
  return &r->result;
}

/* ---- createClientCvars(simulation, content, options, seat, userinfo, previous?, archive = [], selectedIdentity = false) ---- */
typedef struct client_cvars_record { qa_application *app; qa_app_shared_simulation *simulation; } client_cvars_record;
static void client_cvars_print(void *ctx, const char *text) {
  qa_application_print(((client_cvars_record *)ctx)->app, text);
}
/* cheatsAllowed: () => this.sourceCvars(simulation)?.variableValue("sv_cheats") === 1 */
static int client_cvars_cheats(void *ctx) {
  client_cvars_record *record = ctx;
  qa_cvar_registry *cvars = qa_application_source_cvars(record->app, record->simulation);
  return cvars != NULL && qa_cvar_registry_variable_value(cvars, "sv_cheats") == 1;
}
static void register_cvar(qa_cvar_registry *cvars, const char *name, const char *value, double flags) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_register(cvars, name, value, flags, &snapshot)) qa_cvar_read_free(&snapshot);
}
static double cvar_flags(qa_cvar_registry *cvars, const char *name, bool *found) {
  qa_cvar_read read;
  *found = qa_cvar_registry_find(cvars, name, &read);
  if (!*found) return 0;
  double flags = read.flags;
  qa_cvar_read_free(&read);
  return flags;
}
static bool userinfo_free(void *ctx, const char *name) {
  bool found = false;
  return ((int32_t)cvar_flags(ctx, name, &found) & QA_CVAR_FLAG_USER_INFO) == 0;
}
static const qa_app_prepared_seat_configuration *profile_seat(qa_application *app, qa_session_seat *seat) {
  if (app->profile_configuration == NULL) return NULL;
  size_t count = 0;
  const qa_app_prepared_seat_configuration *seats = qa_app_prepared_profile_seats(app->profile_configuration, &count);
  qa_seat_id id = qa_session_seat_id(seat);
  qa_client_id client = qa_session_client_id(qa_session_seat_client(seat));
  for (size_t i = 0; i < count; i++)
    if (qa_same_seat(seats[i].id, id) && seats[i].context.origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
        && qa_same_client(seats[i].context.origin.client, client)) return &seats[i];
  return NULL;
}
static const qa_app_prepared_seat *startup_seat(qa_application *app, qa_session_seat *seat) {
  if (app->prepared_startup == NULL) return NULL;
  size_t count = 0;
  const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(app->prepared_startup, &count);
  qa_seat_id id = qa_session_seat_id(seat);
  qa_client_id client = qa_session_client_id(qa_session_seat_client(seat));
  for (size_t i = 0; i < count; i++)
    if (qa_same_seat(seats[i].id, id) && seats[i].context.origin.kind == QA_COMMAND_ORIGIN_LOCAL_SEAT
        && qa_same_client(seats[i].context.origin.client, client)) return &seats[i];
  return NULL;
}
static const char *family_name(qa_game_family family) {
  return family == QA_GAME_FAMILY_Q1 ? "q1" : family == QA_GAME_FAMILY_Q2 ? "q2" : "q3";
}
static void published_userinfo_set(qa_application *app, qa_cvar_registry *cvars, char *userinfo) {
  for (size_t i = 0; i < app->published_userinfo_count; i++)
    if (app->published_userinfo_cvars[i] == cvars) { free(app->published_userinfo[i]); app->published_userinfo[i] = userinfo; return; }
  grow((void **)&app->published_userinfo_cvars, &app->published_userinfo_capacity, app->published_userinfo_count + 1,
      sizeof *app->published_userinfo_cvars);
  app->published_userinfo = checked(realloc(app->published_userinfo, app->published_userinfo_capacity * sizeof *app->published_userinfo));
  app->published_userinfo_cvars[app->published_userinfo_count] = cvars;
  app->published_userinfo[app->published_userinfo_count++] = userinfo;
}
static void client_registry_release(void *record) { qa_cvar_registry_destroy(record); }
qa_cvar_registry *qa_application_create_client_cvars(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, const qa_app_options *options, qa_session_seat *seat, const char *userinfo,
    qa_cvar_registry *previous, const qa_cvar_archive_entry *archive, size_t archive_count, bool selected_identity) {
  const qa_app_prepared_seat *startup = previous == NULL && app->graphical == NULL ? startup_seat(app, seat) : NULL;
  qa_cvar_registry *prepared = NULL;
  if (app->graphical == NULL) {
    const qa_app_prepared_seat_configuration *profile = profile_seat(app, seat);
    if (profile != NULL) prepared = profile->cvars;
    else if (app->ownership.borrowed_client == NULL && startup != NULL) prepared = startup->cvars;
  }
  if (previous == NULL && startup != NULL) previous = startup->cvars;
  qa_cvar_registry *cvars = prepared;
  if (cvars == NULL) {
    client_cvars_record *record = seat_record(app, sizeof *record, NULL);
    record->app = app; record->simulation = simulation;
    qa_cvar_registry_options registry;
    memset(&registry, 0, sizeof registry);
    registry.dialect = qa_application_source_dialect(app, content);
    registry.context.session = qa_engine_session_id(app->session);
    registry.context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
    registry.context.origin.seat = qa_session_seat_id(seat);
    registry.context.origin.client = qa_session_client_id(qa_session_seat_client(seat));
    registry.print = client_cvars_print; registry.print_ctx = record;
    registry.cheats_allowed = client_cvars_cheats; registry.cheats_allowed_ctx = record;
    cvars = checked(qa_cvar_registry_create(&registry));
    (void)seat_record_keep(app, cvars, client_registry_release);
  }
  qa_command_dialect dialect = qa_cvar_registry_dialect(cvars);
  if (prepared == NULL) {
    if (userinfo == NULL && previous != NULL && qa_cvar_registry_dialect(previous) == dialect) {
      qa_save_value *state = checked(qa_cvar_registry_capture_save_state(previous));
      qa_cvar_registry_restore_save_state(cvars, state);
      qa_save_value_free(state);
    } else {
      if (userinfo != NULL) {
        /* userinfo.split("\\"): fields from index 1 when the first is "" */
        size_t length = strlen(userinfo);
        char *copy = dup_seat_text(userinfo);
        char **fields = checked(calloc(length + 2, sizeof *fields));
        size_t field_count = 0;
        fields[field_count++] = copy;
        for (char *cursor = copy; *cursor != '\0'; cursor++)
          if (*cursor == '\\') { *cursor = '\0'; fields[field_count++] = cursor + 1; }
        for (size_t index = fields[0][0] == '\0' ? 1 : 0; index + 1 < field_count; index += 2) {
          const char *name = fields[index], *value = fields[index + 1];
          if (strcmp(name, "ip") == 0) continue;
          if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE && strcmp(name, "name") == 0) register_cvar(cvars, "_cl_name", value, QA_CVAR_FLAG_ARCHIVE);
          else register_cvar(cvars, name, value, QA_CVAR_FLAG_USER_INFO);
        }
        free(fields);
        free(copy);
      }
      size_t entry_count = 0;
      qa_cvar_archive_entry *previous_entries = previous == NULL ? NULL : qa_cvar_registry_archive_entries(previous, NULL, NULL, &entry_count);
      const qa_cvar_archive_entry *entries = previous == NULL ? archive : previous_entries;
      if (previous == NULL) entry_count = archive_count;
      qa_cvar_archive_entry *available = checked(calloc(entry_count + 1, sizeof *available));
      size_t available_count = 0;
      for (size_t i = 0; i < entry_count; i++) if (userinfo_free(cvars, entries[i].name)) available[available_count++] = entries[i];
      qa_cvar_registry_apply_archive(cvars, available, available_count);
      free(available);
      if (previous_entries != NULL) qa_app_cvar_archive_entries_free(previous_entries, entry_count);
      if (previous != NULL) {
        size_t count = 0;
        qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(previous, 0, &count);
        for (size_t i = 0; i < count; i++) {
          if ((snapshots[i].flags & QA_CVAR_FLAG_USER_INFO) == 0) qa_application_cvar_set(cvars, snapshots[i].name, snapshots[i].value, true);
          qa_cvar_read_free(&snapshots[i]);
        }
        free(snapshots);
      }
    }
  }
  qa_app_register_player_userinfo(cvars, (double)qa_session_seat_id(seat).index,
      options->character == QA_GAME_FAMILY_Q2 ? options->character_model : "male");
  if (userinfo != NULL && dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE) {
    int32_t top = qa_js_to_int32(qa_cvar_registry_variable_value(cvars, "topcolor"));
    int32_t bottom = qa_js_to_int32(qa_cvar_registry_variable_value(cvars, "bottomcolor"));
    char text[40];
    qa_net_js_number((double)((int32_t)((uint32_t)top << 4) | (bottom & 15)), text, sizeof text);
    qa_application_cvar_set(cvars, "_cl_color", text, true);
  }
  if (qa_app_shared_sim_q2_native(simulation, NULL, NULL)) {
    static const char *const names[] = {"name", "skin", "rate", "msg", "hand", "fov", "gender"};
    static const char *const values[] = {"unnamed", "male/grunt", "25000", "1", "0", "90", "male"};
    for (size_t i = 0; i < 7; i++) register_cvar(cvars, names[i], values[i], QA_CVAR_FLAG_USER_INFO | QA_CVAR_FLAG_ARCHIVE);
  }
  const char *model = qa_app_q3_client_model(family_name(options->character), options->character_model);
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    char name[32];
    snprintf(name, sizeof name, "Player %lld", (long long)qa_session_seat_id(seat).index + 1);
    qa_app_initialize_q3_client_cvars(cvars, (qa_app_q3_client_identity){name, model});
    static const char *const paused[] = {"cl_paused", "sv_paused"};
    for (size_t i = 0; i < 2; i++) {
      bool found = false;
      (void)cvar_flags(cvars, paused[i], &found);
      if (!found) register_cvar(cvars, paused[i], "0", QA_CVAR_FLAG_READ_ONLY);
      else qa_cvar_registry_add_flags(cvars, paused[i], QA_CVAR_FLAG_READ_ONLY);
      qa_application_cvar_set(cvars, paused[i], "0", true);
    }
    if (selected_identity) {
      char text[512];
      snprintf(text, sizeof text, "%s/default", model);
      static const char *const models[] = {"model", "headmodel", "team_model", "team_headmodel"};
      for (size_t i = 0; i < 4; i++) qa_application_cvar_set(cvars, models[i], text, true);
    }
  }
  qa_app_character_appearance appearance = {family_name(options->character), options->character_model, options->character_skin,
      options->has_character_top_color, options->character_top_color, options->has_character_bottom_color, options->character_bottom_color};
  qa_app_apply_character_appearance(cvars, appearance);
  if (dialect == QA_COMMAND_DIALECT_Q3) qa_app_apply_q3_proxy_model(cvars, family_name(options->character));
  const qa_app_team_arena_skirmish *setup = options->team_arena_skirmish;
  if (userinfo == NULL && setup != NULL && selected_identity) {
    size_t count = 0;
    qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(cvars, 0, &count);
    qa_app_name_value *baseline = checked(calloc(count + 1, sizeof *baseline));
    for (size_t i = 0; i < count; i++) { baseline[i].name = snapshots[i].name; baseline[i].value = snapshots[i].value; }
    size_t setting_count = 0;
    qa_app_name_value *settings = qa_app_team_arena_client_cvars(setup, baseline, count, &setting_count);
    free(baseline);
    for (size_t i = 0; i < count; i++) qa_cvar_read_free(&snapshots[i]);
    free(snapshots);
    if (qa_session_seat_id(seat).index == 0) {
      qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
      if (source != NULL) {
        const char *timer = "0";
        for (size_t i = 0; i < setting_count; i++) if (strcmp(settings[i].name, "ui_drawTimer") == 0) { timer = settings[i].value; break; }
        qa_application_cvar_set(qa_app_q3_source_runtime_host(source)->cvars, "ui_drawTimer", timer, true);
      }
    }
    qa_application_cvar_set(cvars, "model", setup->player_model, true);
    qa_application_cvar_set(cvars, "team_model", setup->player_model, true);
    qa_application_cvar_set(cvars, "headmodel", setup->player_head_model, true);
    qa_application_cvar_set(cvars, "team_headmodel", setup->player_head_model, true);
    for (size_t i = 0; i < setting_count; i++) qa_application_cvar_set(cvars, settings[i].name, settings[i].value, true);
    qa_app_name_values_free(settings, setting_count);
  }
  published_userinfo_set(app, cvars, checked(qa_app_player_userinfo(cvars)));
  return cvars;
}
