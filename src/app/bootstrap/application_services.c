/* Port of src/app/bootstrap/application.ts, region 1751-2308 (group P2):
 * sourceActions, the recording feeds (standaloneRecording, prepareMvdRecording,
 * prepareServerRecording, prepareRecording, localRecordingSource), server
 * administration (operatorHost, executeAdministration), the native server
 * hosts (networkHost, validateNetworkHost, changeNetworkWorld, openNetwork),
 * prepareNativeQ2Seat, openGraphical, captureMap, prepareVideoRestart,
 * prepareRetirement, readClientResource, releaseSettings, adoptImageSettings and
 * retire. closeGameLog (1751) is defined beside writeGameLog in
 * application_commands.c. TS awaits are synchronous calls; a TS throw is
 * qa_raise. Layout and status: docs/application_layout.md. GPL-2.0-or-later. */
#define _DEFAULT_SOURCE
#include "application_internal.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include "qa/app/bootstrap/demo_library.h"
#include "qa/app/bootstrap/network/transport.h"
#include "qa/app/bootstrap/network/unified_content.h"
#include "qa/app/bootstrap/simulation/network_q1.h"
#include "qa/app/bootstrap/simulation/network_q2_guest.h"
#include "qa/app/bootstrap/simulation/network_q2_rerelease_native.h"
#include "qa/app/bootstrap/simulation/network_qw.h"
#include "qa/app/bootstrap/simulation/network_unified.h"
#include "qa/persistence/save_image.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/q1_session_actions.h"
#include "qa/app/bootstrap/q2_match_reports.h"
#include "qa/content/roots.h"
#include "qa/network/common/js_number.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *dup_text(const char *text) {
  size_t length = strlen(text) + 1;
  char *copy = checked(malloc(length));
  memcpy(copy, text, length);
  return copy;
}
/* node:path join(root, name). */
static char *path_join(const char *root, const char *name) {
  size_t length = strlen(root) + strlen(name) + 2;
  char *joined = checked(malloc(length));
  snprintf(joined, length, "%s/%s", root, name);
  char *normalized = qa_application_path_normalize(joined);
  free(joined);
  return normalized;
}

/* TS simulation.q2Native() !== null. */
static bool is_q2_native(qa_app_shared_simulation *simulation) {
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  return qa_app_shared_sim_q2_native(simulation, &classic, &rerelease);
}
/* this.localSeats.delete(client). */
static void local_seats_delete(qa_application *app, qa_client_id client) {
  for (size_t i = 0; i < app->local_seat_count; i++) {
    if (!qa_same_client(app->local_seat_clients[i], client)) continue;
    memmove(&app->local_seat_clients[i], &app->local_seat_clients[i + 1], (app->local_seat_count - i - 1) * sizeof *app->local_seat_clients);
    memmove(&app->local_seats[i], &app->local_seats[i + 1], (app->local_seat_count - i - 1) * sizeof *app->local_seats);
    app->local_seat_count--;
    return;
  }
}
/* this.network?.server.disconnectClient(client, reason): false without a server. */
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
/* this.bots?.disconnect(slot). */
static bool bots_disconnect(qa_application *app, double slot) {
  return app->bots != NULL && app->bots->disconnect(app->bots->ctx, (int64_t)slot);
}
/* Prints to the host and to every graphical local console (the frontend printer). */
static void print_everywhere(qa_application *app, const char *text) {
  qa_application_print(app, text);
  if (app->graphical == NULL) return;
  size_t count = qa_app_application_input_local_count(app->graphical->input);
  for (size_t i = 0; i < count; i++) qa_seat_console_print(qa_app_application_input_local(app->graphical->input, i)->console, text);
}
/* TS this.requestedCommands.someLive(command => command.name === name && command.seat?.equals(seat) === true). */
static bool requested_live(qa_application *app, const char *name, qa_seat_id seat) {
  int64_t live = qa_event_ring_live(&app->requested_commands);
  for (int64_t i = 0; i < live; i++) {
    const qa_application_command_request *request = qa_event_ring_at_offset(&app->requested_commands, i);
    if (strcmp(request->name, name) == 0 && request->has_seat && qa_same_seat(request->seat, seat)) return true;
  }
  return false;
}

/* ---- sourceActions (1753-1871) ---- */
typedef struct team_arena_files { qa_application *app; qa_user_file_store *files; } team_arena_files;
static uint8_t *team_arena_read(void *ctx, const char *path, size_t *out_length) {
  team_arena_files *files = ctx;
  char *full = path_join(files->files->root, path);
  FILE *file = fopen(full, "rb");
  free(full);
  *out_length = 0;
  if (file == NULL) return NULL;
  uint8_t *bytes = NULL; size_t length = 0, capacity = 0;
  for (;;) {
    if (length == capacity) { capacity = capacity == 0 ? 256 : capacity * 2; bytes = checked(realloc(bytes, capacity)); }
    size_t read = fread(bytes + length, 1, capacity - length, file);
    length += read;
    if (read == 0) break;
  }
  fclose(file);
  *out_length = length;
  return bytes;
}
static void team_arena_print(const char *text, void *ctx) { qa_application_print(ctx, text); }
static void team_arena_write(void *ctx, const char *path, const uint8_t *bytes, size_t length) {
  team_arena_files *files = ctx;
  qa_writable_binary_file file;
  if (!qa_user_file_store_open(files->files, path, QA_WRITABLE_WRITE, team_arena_print, files->app, &file))
    qa_raise("Error", 0, 0, "Cannot open Team Arena score file");
  int64_t written = qa_writable_binary_file_write(&file, bytes, length);
  qa_writable_binary_file_close(&file);
  if (written != (int64_t)length) qa_raise("Error", 0, 0, "Incomplete Team Arena score write");
}
static char **demo_list_files(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  qa_app_loaded_content *content = ctx;
  return qa_content_mounted_list_files((qa_content_mounted *)qa_app_loaded_content_mounts(content), directory, extension, out_count);
}
static bool demo_read(void *ctx, const char *path, uint8_t **out_bytes, size_t *out_count) {
  qa_app_loaded_content *content = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open((qa_content_mounted *)qa_app_loaded_content_mounts(content), path, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; /* the caller owns the opened bytes */
  *out_count = opened.byte_length;
  return true;
}
static bool demo_exists(void *ctx, const char *path) {
  uint8_t *bytes = NULL; size_t count = 0; const char *error = NULL;
  bool found = qa_app_demo_library_read(ctx, path, &bytes, &count, &error);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  free(bytes);
  return found;
}
/* this.options.map.replace(/^maps\//, "").replace(/\.bsp$/, ""). */
static char *map_short_name(const char *map) {
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  char *name = dup_text(map);
  size_t length = strlen(name);
  if (length >= 4 && strcmp(name + length - 4, ".bsp") == 0) name[length - 4] = '\0';
  return name;
}
static void team_arena_demos_set(qa_application *app, qa_app_shared_simulation *simulation, qa_app_team_arena_demo *demo) {
  for (size_t i = 0; i < app->team_arena_demo_count; i++) {
    if (app->team_arena_demo_simulations[i] != simulation) continue;
    if (demo == NULL) {
      qa_app_team_arena_demo_free(app->team_arena_demos[i]); free(app->team_arena_demos[i]);
      memmove(&app->team_arena_demo_simulations[i], &app->team_arena_demo_simulations[i + 1], (app->team_arena_demo_count - i - 1) * sizeof *app->team_arena_demo_simulations);
      memmove(&app->team_arena_demos[i], &app->team_arena_demos[i + 1], (app->team_arena_demo_count - i - 1) * sizeof *app->team_arena_demos);
      app->team_arena_demo_count--;
    } else {
      qa_app_team_arena_demo_free(app->team_arena_demos[i]); free(app->team_arena_demos[i]);
      app->team_arena_demos[i] = demo;
    }
    return;
  }
  if (demo == NULL) return;
  if (app->team_arena_demo_count == app->team_arena_demo_capacity) {
    app->team_arena_demo_capacity = app->team_arena_demo_capacity == 0 ? 4 : app->team_arena_demo_capacity * 2;
    app->team_arena_demo_simulations = checked(realloc(app->team_arena_demo_simulations, app->team_arena_demo_capacity * sizeof *app->team_arena_demo_simulations));
    app->team_arena_demos = checked(realloc(app->team_arena_demos, app->team_arena_demo_capacity * sizeof *app->team_arena_demos));
  }
  app->team_arena_demo_simulations[app->team_arena_demo_count] = simulation;
  app->team_arena_demos[app->team_arena_demo_count++] = demo;
}
/* The pendingTeamArenaPostgame block of sourceActions. */
static void team_arena_postgame(qa_application *app) {
  qa_app_team_arena_postgame stats = app->pending_team_arena_postgame;
  app->has_pending_team_arena_postgame = false;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(app->world_simulation);
  if (source == NULL || !qa_application_is_team_arena_skirmish(app, NULL)) qa_raise("Error", 0, 0, "Team Arena postgame lost its source owner");
  qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(source)->cvars;
  qa_config_store config = qa_application_source_config(&app->launch_options, app->loaded_content);
  qa_user_file_store files;
  if (!qa_user_file_store_init(&files, config.root)) qa_range_error("Out of memory");
  qa_config_store_destroy(&config);
  char *map = map_short_name(app->launch_options.map);
  qa_q3_game_level *level = qa_app_q3_source_subsystems_owner_level(qa_app_q3_source_runtime_owner(source));
  qa_app_team_arena_score_input input = { stats, level->base.start_time,
    qa_cvar_registry_variable_value(cvars, "g_spSkill"), qa_cvar_registry_variable_value(cvars, "ui_teamArenaTimeToBeat") };
  double game_type = qa_app_q3_source_runtime_game_type(source);
  team_arena_files score_files = { app, &files };
  qa_app_team_arena_score_files host = { &score_files, team_arena_read, team_arena_write };
  qa_app_team_arena_score_result result = qa_app_record_team_arena_score(input, map, game_type, host);
  char number[48], text[160];
  qa_net_js_number(result.score.score, number, sizeof number);
  qa_application_cvar_set(cvars, "ui_scoreScore", number, true);
  char minutes[48], seconds[48];
  qa_net_js_number(trunc(result.score.time / 60), minutes, sizeof minutes);
  qa_net_js_number(fmod(result.score.time, 60), seconds, sizeof seconds);
  snprintf(text, sizeof text, "%s%s:%s%s", strlen(minutes) < 2 ? "0" : "", minutes, strlen(seconds) < 2 ? "0" : "", seconds);
  qa_application_cvar_set(cvars, "ui_scoreTime", text, true);
  char red[48], blue[48];
  qa_net_js_number(result.score.red_score, red, sizeof red);
  qa_net_js_number(result.score.blue_score, blue, sizeof blue);
  snprintf(text, sizeof text, "%s to %s", red, blue);
  qa_application_cvar_set(cvars, "ui_scoreTeam", text, true);
  for (size_t i = 0; i < sizeof qa_app_team_arena_server_overrides / sizeof qa_app_team_arena_server_overrides[0]; i++) {
    char *saved = qa_application_cvar_string(cvars, qa_app_team_arena_server_overrides[i].saved);
    qa_application_cvar_set(cvars, qa_app_team_arena_server_overrides[i].name, saved, true);
    free(saved);
  }
  qa_app_override_registry rows[8];
  size_t row_count = qa_application_override_seats(app, NULL, NULL, 0, rows, 8);
  bool released_new = false;
  qa_app_team_arena_overrides released = qa_app_release_team_arena_overrides(app->team_arena_overrides, rows, row_count < 8 ? row_count : 8, &released_new);
  if (released_new) {
    qa_app_team_arena_overrides *next = checked(malloc(sizeof *next));
    *next = released;
    if (app->team_arena_overrides != NULL) { qa_app_team_arena_overrides_free(app->team_arena_overrides); free(app->team_arena_overrides); }
    app->team_arena_overrides = next;
  }
  if (result.new_high_score) qa_application_printf(app, "New Team Arena high score: %s\n", number);
  qa_app_demo_library_mounts mounts = { app->loaded_content, demo_list_files, demo_read };
  qa_app_demo_library *library = checked(qa_app_demo_library_create(files.root, mounts));
  qa_cvar_snapshot protocol;
  double protocol_value = 68;
  if (qa_cvar_registry_get(cvars, "protocol", &protocol)) { protocol_value = protocol.integer_value; qa_cvar_read_free(&protocol); }
  qa_app_team_arena_demo found;
  bool exists = qa_app_team_arena_demo_lookup(map, game_type, protocol_value, demo_exists, library, &found);
  qa_app_demo_library_destroy(library);
  qa_user_file_store_destroy(&files);
  free(map);
  if (!exists) team_arena_demos_set(app, app->world_simulation, NULL);
  else {
    qa_app_team_arena_demo *demo = checked(malloc(sizeof *demo));
    *demo = found;
    team_arena_demos_set(app, app->world_simulation, demo);
  }
}

/* The takeModClientDrops loop of sourceActions. */
static void mod_client_drops(qa_application *app) {
  qa_app_shared_simulation *dropping = app->world_simulation;
  size_t count = 0;
  qa_ss_mod_client_drop *drops = qa_app_shared_sim_take_mod_client_drops(dropping, &count);
  for (size_t i = 0; i < count; i++) {
    qa_ss_mod_client_drop request = drops[i];
    qa_client_id current;
    if (app->world_simulation != dropping || !qa_app_shared_sim_player_client(dropping, request.actor, &current)
        || !qa_same_client(current, request.client)) continue;
    qa_session_seat *local_seat = qa_application_local_seat(app, request.client);
    if (local_seat != NULL && app->local_seat_count > 1) {
      qa_application_change_local_players(app, "drop", NULL, 0, qa_session_seat_id(local_seat));
      qa_application_printf(app, "%s: client %lld: %s\n", request.content, (long long)request.client.slot, request.reason);
      continue;
    }
    qa_application_disconnect_rankings(app, dropping, request.client);
    if (app->world_simulation != dropping || !qa_app_shared_sim_player_client(dropping, request.actor, &current)
        || !qa_same_client(current, request.client)) continue;
    if (bots_disconnect(app, (double)request.client.slot)) continue;
    if (network_disconnect_client(app, request.client, request.reason)) continue;
    bool local = qa_application_local_seat(app, request.client) != NULL;
    qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(app->world_simulation);
    qa_app_q3_player player;
    if (guest != NULL && qa_app_q3_qvm_server_game_player(guest, request.client, &player)) qa_app_q3_qvm_server_game_disconnect(guest, player);
    else qa_app_shared_sim_disconnect_player(app->world_simulation, request.actor);
    qa_engine_session_close_client(app->session, request.client);
    local_seats_delete(app, request.client);
    qa_application_printf(app, "%s: client %lld: %s\n", request.content, (long long)request.client.slot, request.reason);
    if (local) qa_application_request_quit(app);
  }
}

/* The q3-source drop-client branch; true is the TS `continue` after a bot drop. */
static void q3_drop_client(qa_application *app, const qa_app_q3_source_event *event) {
  size_t identity_count = 0;
  const qa_client_id *identities = qa_app_shared_sim_client_identities(app->world_simulation, &identity_count);
  for (size_t i = 0; i < identity_count; i++)
    if ((double)identities[i].slot == event->client) { qa_application_disconnect_rankings(app, app->world_simulation, identities[i]); break; }
  if (bots_disconnect(app, event->client)) {
    char slot[48]; qa_net_js_number(event->client, slot, sizeof slot);
    qa_application_printf(app, "Client %s: %s\n", slot, event->reason);
    return;
  }
  size_t player_count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(app->world_simulation, &player_count);
  bool found = false; qa_actor_id actor = { 0 };
  for (size_t i = 0; i < player_count; i++) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(app->world_simulation, players[i]);
    if (player != NULL && (double)player->client.slot == event->client) { actor = players[i]; found = true; break; }
  }
  if (!found) return;
  qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(app->world_simulation, actor);
  if (player == NULL) qa_raise("Error", 0, 0, "Source disconnect lost its client identity");
  qa_client_id client = player->client;
  if (network_disconnect_client(app, client, event->reason)) return;
  bool local = false;
  size_t local_count = qa_application_local_player_count(app);
  for (size_t i = 0; i < local_count; i++) if (qa_same_actor(qa_application_local_player(app, i).actor, actor)) { local = true; break; }
  qa_app_shared_sim_disconnect_player(app->world_simulation, actor);
  qa_engine_session_close_client(app->session, client);
  local_seats_delete(app, client);
  char slot[48]; qa_net_js_number(event->client, slot, sizeof slot);
  qa_application_printf(app, "Client %s: %s\n", slot, event->reason);
  if (local) qa_application_request_quit(app);
}

void qa_application_source_actions(qa_application *app, const qa_app_sim_presentation_event *events, size_t event_count) {
  size_t local_count = qa_application_local_player_count(app);
  qa_app_q2_match_local *locals = checked(calloc(local_count + 1, sizeof *locals));
  qa_app_seat_actor *seat_actors = checked(calloc(local_count + 1, sizeof *seat_actors));
  for (size_t i = 0; i < local_count; i++) {
    qa_app_local_player player = qa_application_local_player(app, i);
    locals[i].actor = player.actor; locals[i].seat = qa_session_seat_id(player.seat);
    seat_actors[i].actor = player.actor; seat_actors[i].seat = locals[i].seat;
  }
  qa_app_q2_match_simulation match = qa_app_shared_sim_q2_match_simulation(app->world_simulation);
  qa_app_q2_match_report_rows reports = qa_app_q2_match_reports(&match, locals, local_count, NULL);
  if (reports.count > 0) {
    if (app->player_progress == NULL) {
      char *path = path_join(app->input_config.root, "player-progress.json");
      app->player_progress = qa_app_player_progress_store_open(path);
      free(path);
    }
    for (size_t i = 0; i < reports.count; i++) (void)qa_app_player_progress_store_record(app->player_progress, reports.items[i]);
  }
  qa_app_q2_match_report_rows_destroy(&reports);
  free(locals);
  qa_seat_id *leaving = checked(calloc(local_count + 1, sizeof *leaving));
  size_t leaving_count = qa_app_q1_session_departures(events, event_count, seat_actors, local_count, leaving);
  free(seat_actors);
  if (leaving_count > 0 && leaving_count == local_count) {
    if (app->host.lobby == NULL) qa_application_request_quit(app);
    else qa_application_return_to_lobby(app);
  } else for (size_t i = 0; i < leaving_count; i++) {
    if (!requested_live(app, "local_drop", leaving[i])) qa_application_queue_command(app, "local_drop", NULL, 0, &leaving[i]);
  }
  free(leaving);
  mod_client_drops(app);
  for (size_t i = 0; i < event_count; i++) {
    const qa_app_sim_presentation_event *source = &events[i];
    qa_application_record_player_progress(app, source);
    if (strcmp(source->kind, "q1-composition") == 0) {
      const qa_comp_q1_event *composition = source->q1_composition;
      const char *text = NULL;
      if (composition->kind == QA_COMP_Q1_EVENT_ADDON) {
        if (composition->addon_event->kind == QA_Q1AD_EVENT_DEVELOPER_MESSAGE) text = composition->addon_event->text;
      } else if (composition->kind == QA_COMP_Q1_EVENT_DEVELOPER_MESSAGE) text = composition->text;
      if (text != NULL) print_everywhere(app, text);
      continue;
    }
    if (strcmp(source->kind, "q2-composition") == 0 && source->q2_composition->kind == QA_Q2_COMPOSITION_EVENT_KICK) {
      qa_actor_id actor = source->q2_composition->actor;
      qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(app->world_simulation, actor);
      if (player != NULL) {
        qa_client_id client = player->client;
        if (network_disconnect_client(app, client, "was kicked")) continue;
        bool local = qa_application_local_seat(app, client) != NULL;
        qa_app_shared_sim_disconnect_player(app->world_simulation, actor);
        qa_engine_session_close_client(app->session, client);
        local_seats_delete(app, client);
        if (local) qa_application_request_quit(app);
      }
      continue;
    }
    if (strcmp(source->kind, "q2-rerelease") == 0) {
      if (strcmp(source->q2_rerelease->kind, "restart-level") == 0) {
        free(app->pending_map);
        app->pending_map = qa_app_map_resource_path(source->q2_rerelease->map);
        app->has_pending_q3_map = false;
      }
      continue;
    }
    if (strcmp(source->kind, "q3-source") != 0) continue;
    const qa_app_q3_source_event *event = source->q3_source;
    if (event->kind == QA_APP_Q3_EVENT_PRINT) qa_application_print(app, event->text);
    else if (event->kind == QA_APP_Q3_EVENT_LOG) qa_application_write_game_log(app, app->world_simulation, event->text);
    else if (event->kind == QA_APP_Q3_EVENT_CONSOLE_COMMAND) {
      if (app->source_commands == NULL) qa_raise("Error", 0, 0, "Q3 source console has no command owner");
      if (strcmp(event->execution, "now") == 0) (void)qa_command_buffer_execute_now(app->source_commands, event->text, NULL, -1);
      else qa_command_buffer_append(app->source_commands, event->text, NULL, -1);
    } else if (event->kind == QA_APP_Q3_EVENT_DROP_CLIENT) q3_drop_client(app, event);
  }
  if (app->has_pending_team_arena_postgame) team_arena_postgame(app);
}

/* ---- C storage the services keep for the application's life (TS closures and
 * the host objects they capture). Released by qa_application_services_free. ---- */
typedef struct service_record { void (*free)(void *record); void *record; } service_record;
typedef struct service_records { service_record *items; size_t count, capacity; } service_records;
static void remember(qa_application *app, void (*release)(void *record), void *record) {
  service_records *records = app->service_records;
  if (records == NULL) records = app->service_records = checked(calloc(1, sizeof *records));
  if (records->count == records->capacity) {
    records->capacity = records->capacity == 0 ? 8 : records->capacity * 2;
    records->items = checked(realloc(records->items, records->capacity * sizeof *records->items));
  }
  records->items[records->count].free = release;
  records->items[records->count++].record = record;
}
void qa_application_services_free(qa_application *app) {
  free(app->recording_host);
  app->recording_host = NULL;
  service_records *records = app->service_records;
  if (records == NULL) return;
  for (size_t i = records->count; i-- > 0;) records->items[i].free(records->items[i].record);
  free(records->items);
  free(records);
  app->service_records = NULL;
}

/* ---- operatorHost (1979-1986) ---- */
static void operator_print(void *ctx, const char *text) { qa_application_print(ctx, text); }
static bool operator_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  qa_application *app = ctx;
  return app->operator_send != NULL && app->operator_send(app->operator_send_ctx, to, bytes, length);
}
static void operator_write_config(void *ctx, const char *name, const char *text) {
  qa_application *app = ctx;
  qa_config_store config = qa_application_source_config(&app->launch_options, app->loaded_content);
  bool written = qa_config_store_dump(&config, name, text);
  qa_config_store_destroy(&config);
  if (!written) qa_raise("Error", 0, 0, "Cannot write %s", name);
}
static void operator_heartbeat(void *ctx) {
  qa_application *app = ctx;
  qa_application_native_server *network = app->network;
  if (network == NULL) return;
  double now = qa_application_performance_now();
  /* TS `"heartbeat" in server`: the QW, Q2 and Q3 servers have one. */
  if (network->kind == QA_APPLICATION_NETWORK_QW) qa_app_qw_server_network_heartbeat(network->qw, now);
  else if (network->kind == QA_APPLICATION_NETWORK_Q2) qa_app_q2_server_network_heartbeat(network->q2, now);
  else if (network->kind == QA_APPLICATION_NETWORK_Q3) qa_app_q3_server_network_heartbeat(network->q3, now);
}
/* ServerOperatorState imports resolveAddress(name, port, 4). */
static const char *operator_resolve(void *ctx, const char *name, int64_t default_port, qa_net_address *out) {
  (void)ctx;
  return qa_net_resolve_address(out, name, default_port, 4) == 0 ? "Unable to resolve address" : NULL;
}
qa_app_server_operator_host qa_application_operator_host(qa_application *app) {
  qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
  if (cvars == NULL) qa_raise("Error", 0, 0, "Server administration has no source cvars");
  qa_app_server_operator_host host;
  memset(&host, 0, sizeof host);
  host.ctx = app;
  host.dialect = qa_cvar_registry_dialect(cvars);
  host.cvars = cvars;
  host.dedicated = app->launch_options.dedicated;
  host.print = operator_print;
  host.send = operator_send;
  host.write_config = operator_write_config;
  host.heartbeat = operator_heartbeat;
  host.resolve_address = operator_resolve;
  return host;
}

/* ---- executeAdministration (1987-2002) ---- */
typedef struct administration_output { void (*write)(void *ctx, const char *text); void *ctx; } administration_output;
static void administration_bound_output(void *ctx, const char *text, const qa_command_context *source) {
  (void)source;
  administration_output *output = ctx;
  output->write(output->ctx, text);
}
static void administration_after(void *ctx) { qa_application_commands_routed(ctx, false); }
typedef struct administration_call { qa_application *app; qa_command_buffer *commands; const char *text; bool limited; } administration_call;
static void administration_run(void *ctx) {
  administration_call *call = ctx;
  qa_command_context source;
  memset(&source, 0, sizeof source);
  source.session = qa_engine_session_id(call->app->session);
  source.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  (void)qa_command_buffer_execute_command_async(call->commands, call->text, source, administration_after, call->app,
      !call->limited, NULL, NULL);
}
void qa_application_execute_administration(qa_application *app, const char *text,
    void (*output)(void *ctx, const char *text), void *output_ctx, bool limited) {
  qa_command_buffer *commands = app->source_commands;
  if (commands == NULL) qa_raise("Error", 0, 0, "Server administration has no source command owner");
  if (app->operator_output->write != NULL) qa_raise("Error", 0, 0, "Nested server administration is not supported");
  size_t pending_count = 0;
  qa_application_command_request *pending = qa_event_ring_take(&app->requested_commands, &app->requested_cursor, &pending_count);
  qa_event_ring_retire(&app->requested_commands);
  administration_output bound = { output, output_ctx };
  int64_t binding = qa_command_buffer_bind_output(commands, administration_bound_output, &bound);
  app->operator_output->write = output; app->operator_output->ctx = output_ctx;
  administration_call call = { app, commands, text, limited };
  qa_caught_error caught;
  bool ok = qa_try(administration_run, &call, &caught);
  bool rethrow = false;
  if (!ok) {
    if (strcmp(caught.name, "Q3GameCallbackError") == 0 || strcmp(caught.name, "Q2GameCallbackError") == 0 || app->fatal_world_failure) rethrow = true;
    else {
      size_t length = strlen(caught.message) + 2;
      char *line = checked(malloc(length));
      snprintf(line, length, "%s\n", caught.message);
      output(output_ctx, line);
      free(line);
    }
  }
  app->operator_output->write = NULL; app->operator_output->ctx = NULL;
  qa_command_buffer_unbind_output(commands, binding);
  qa_event_ring_prepend(&app->requested_commands, pending, pending_count);
  free(pending);
  if (rethrow) qa_rethrow(&caught);
}

/* ---- networkHost (2004-2041) ---- */
/* The administration closures one native host binds (TS `shared`, `administration`,
 * the QW administration object and the Q2 operator), with the access structs the
 * C adapters borrow. */
typedef struct host_binding {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_sim_network_simulation network_simulation;
  qa_app_sim_network_content network_content;
  qa_cvar_registry *cvars;
  qa_app_server_operator_state *state;
  qa_server_administration administration;
  qa_app_qw_server_administration qw_administration;
  qa_q2_connectionless_host q2_operator;
  qa_app_limited_rcon limited;
  const char **limited_prefixes;
  const qa_net_address *masters; size_t master_count;
  const void **master_pointers;
  qa_net_address *qw_masters; size_t qw_master_count;
  qa_application_native_server_host host;
  void (*q2_destroy)(qa_app_q2_server_host host);
  /* the Q3 host the disconnect override forwards to */
  qa_app_sim_q3_server_authority original_q3;
} host_binding;
static void host_binding_free(void *record) {
  host_binding *binding = record;
  switch (binding->host.kind) {
    case QA_APPLICATION_NETWORK_Q1: qa_app_sim_q1_application_server_host_destroy(binding->host.q1); break;
    case QA_APPLICATION_NETWORK_QW: qa_app_sim_qw_application_server_host_destroy(binding->host.qw); break;
    case QA_APPLICATION_NETWORK_Q2: binding->q2_destroy(binding->host.q2); break;
    case QA_APPLICATION_NETWORK_Q3: qa_app_sim_q3_application_server_authority_destroy(binding->original_q3); break;
    case QA_APPLICATION_NETWORK_UNIFIED: break;
  }
  qa_app_limited_rcon_free(&binding->limited);
  free(binding->limited_prefixes);
  free(binding->master_pointers);
  free(binding->qw_masters);
  qa_app_source_server_administration_destroy(&binding->administration);
  free(binding);
}
static void binding_execute(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx);
typedef struct rcon_writer { qa_rcon_output_fn output; void *ctx; } rcon_writer;
static void rcon_write(void *ctx, const char *text) { rcon_writer *writer = ctx; writer->output(writer->ctx, text, strlen(text)); }
/* shared.execute: (text, output) => this.executeAdministration(text, output). */
static void binding_execute(void *ctx, const char *command, qa_rcon_output_fn output, void *output_ctx) {
  host_binding *binding = ctx;
  rcon_writer writer = { output, output_ctx };
  qa_application_execute_administration(binding->app, command, rcon_write, &writer, false);
}
/* shared.record: event => this.host.print(`Rcon ${addressKey(event.address)}: ${event.result}\n`). */
static void binding_record(void *ctx, const qa_net_address *address, const char *result) {
  host_binding *binding = ctx;
  char *key = checked(qa_net_address_key(address, true));
  qa_application_printf(binding->app, "Rcon %s: %s\n", key, result);
  free(key);
}
static void binding_print(void *ctx, const char *text) { host_binding *binding = ctx; qa_application_print(binding->app, text); }
static bool binding_rejects(void *ctx, const qa_net_address *address) {
  host_binding *binding = ctx;
  return binding->administration.rejects(binding->administration.ctx, address);
}
/* QW administration: blocked, status, log, executeAdmin. */
static bool qw_blocked(void *ctx, const qa_net_address *from) { return binding_rejects(ctx, from); }
static char *qw_status(void *ctx) {
  host_binding *binding = ctx;
  char *info = checked(qa_cvar_registry_info_string(binding->cvars, QA_CVAR_FLAG_SERVER_INFO, -1));
  size_t length = strlen(info) + 2;
  char *text = checked(malloc(length));
  snprintf(text, length, "%s\n", info);
  free(info);
  return text;
}
static char *qw_log(void *ctx, double sequence) { (void)ctx; (void)sequence; return NULL; }
static void qw_execute_admin(void *ctx, const char *command, void (*write)(void *, const char *, size_t), void *write_ctx) {
  host_binding *binding = ctx;
  binding->administration.execute(binding->administration.ctx, command, write, write_ctx);
}
/* QW administration: the TS rconPassword getter. */
static const char *qw_rcon_password(void *ctx) {
  host_binding *binding = ctx;
  return binding->administration.rcon_password(binding->administration.ctx);
}
/* Q2 operator: rconPassword, limitedRcon, rconRateAllowed, rechargeRconRate, executeRcon. */
static const char *q2_rcon_password(void *ctx) {
  host_binding *binding = ctx;
  return binding->administration.rcon_password(binding->administration.ctx);
}
static bool q2_limited_rcon(void *ctx, const char **password, const char ***prefixes, size_t *prefix_count) {
  host_binding *binding = ctx;
  qa_app_limited_rcon_free(&binding->limited);
  free(binding->limited_prefixes); binding->limited_prefixes = NULL;
  binding->limited = qa_app_server_operator_limited_rcon(binding->state, binding->cvars);
  if (binding->limited.password == NULL) return false;
  binding->limited_prefixes = checked(calloc(binding->limited.prefix_count + 1, sizeof *binding->limited_prefixes));
  for (size_t i = 0; i < binding->limited.prefix_count; i++) binding->limited_prefixes[i] = binding->limited.prefixes[i];
  *password = binding->limited.password; *prefixes = binding->limited_prefixes; *prefix_count = binding->limited.prefix_count;
  return true;
}
static bool q2_rcon_rate_allowed(void *ctx, double now) {
  host_binding *binding = ctx;
  return qa_app_server_operator_rcon_rate_allowed(binding->state, binding->cvars, now, binding_print, binding);
}
static void q2_recharge_rcon_rate(void *ctx) { host_binding *binding = ctx; qa_app_server_operator_recharge_rcon_rate(binding->state); }
typedef struct q2_output { void (*output)(void *ctx, const char *text); void *ctx; } q2_output;
static void q2_execute_rcon(void *ctx, const char *command, bool limited, void (*output)(void *octx, const char *text), void *output_ctx) {
  host_binding *binding = ctx;
  qa_application_execute_administration(binding->app, command, output, output_ctx, limited);
}
static void q2_player_identity(void *ctx, qa_client_id client, bool has_identity, double seat, const char *social_id) {
  host_binding *binding = ctx;
  qa_application *app = binding->app;
  size_t i = 0;
  while (i < app->network_player_identity_count && !qa_same_client(app->network_player_clients[i], client)) i++;
  if (!has_identity) {
    if (i == app->network_player_identity_count) return;
    free(app->network_player_identities[i].social_id);
    memmove(&app->network_player_clients[i], &app->network_player_clients[i + 1], (app->network_player_identity_count - i - 1) * sizeof *app->network_player_clients);
    memmove(&app->network_player_identities[i], &app->network_player_identities[i + 1], (app->network_player_identity_count - i - 1) * sizeof *app->network_player_identities);
    app->network_player_identity_count--;
    return;
  }
  if (i == app->network_player_identity_count) {
    if (app->network_player_identity_count == app->network_player_identity_capacity) {
      app->network_player_identity_capacity = app->network_player_identity_capacity == 0 ? 4 : app->network_player_identity_capacity * 2;
      app->network_player_clients = checked(realloc(app->network_player_clients, app->network_player_identity_capacity * sizeof *app->network_player_clients));
      app->network_player_identities = checked(realloc(app->network_player_identities, app->network_player_identity_capacity * sizeof *app->network_player_identities));
    }
    app->network_player_clients[i] = client;
    app->network_player_identities[i].social_id = NULL;
    app->network_player_identity_count++;
  }
  free(app->network_player_identities[i].social_id);
  app->network_player_identities[i].seat = seat;
  app->network_player_identities[i].social_id = dup_text(social_id);
}
/* The Q3 host with TS's disconnect override; every other member forwards to the
 * created host with its own context. */
#define Q3_ORIGINAL(ctx) (((host_binding *)(ctx))->original_q3.host)
static void q3_prepare(void *ctx, double feed, double server_id, void (*configstring)(void *, double, const char *), void *configstring_ctx) {
  Q3_ORIGINAL(ctx).prepare(Q3_ORIGINAL(ctx).ctx, feed, server_id, configstring, configstring_ctx);
}
static const void *q3_pure(void *ctx, double server_id, const double *feed) { return Q3_ORIGINAL(ctx).pure(Q3_ORIGINAL(ctx).ctx, server_id, feed); }
static bool q3_downloads_enabled(void *ctx) { return Q3_ORIGINAL(ctx).downloads_enabled(Q3_ORIGINAL(ctx).ctx); }
static const void *q3_open_download(void *ctx, const char *name) { return Q3_ORIGINAL(ctx).open_download(Q3_ORIGINAL(ctx).ctx, name); }
static const void *q3_rate(void *ctx, qa_app_q3_player player) { return Q3_ORIGINAL(ctx).rate(Q3_ORIGINAL(ctx).ctx, player); }
static const void *q3_supports(void *ctx) { return Q3_ORIGINAL(ctx).supports_source_wire(Q3_ORIGINAL(ctx).ctx); }
static double q3_time(void *ctx) { return Q3_ORIGINAL(ctx).time(Q3_ORIGINAL(ctx).ctx); }
static const double *q3_occupied(void *ctx, size_t *out_count) { return Q3_ORIGINAL(ctx).occupied_slots(Q3_ORIGINAL(ctx).ctx, out_count); }
static qa_app_q3_admission q3_admit(void *ctx, const void *request) { return Q3_ORIGINAL(ctx).admit(Q3_ORIGINAL(ctx).ctx, request); }
static qa_app_q3_player q3_carried(void *ctx, qa_client_id client) { return Q3_ORIGINAL(ctx).carried_player(Q3_ORIGINAL(ctx).ctx, client); }
typedef struct q3_disconnect_call { host_binding *binding; qa_app_q3_player player; } q3_disconnect_call;
static void q3_disconnect_rankings(void *ctx) {
  q3_disconnect_call *call = ctx;
  qa_application_disconnect_rankings(call->binding->app, call->binding->simulation, call->player.client);
}
/* TS disconnect: try { disconnectRankings } finally { host.disconnect(player, reason) }. */
static void q3_disconnect(void *ctx, qa_app_q3_player player, const char *reason) {
  host_binding *binding = ctx;
  q3_disconnect_call call = { binding, player };
  qa_caught_error caught;
  bool ok = qa_try(q3_disconnect_rankings, &call, &caught);
  Q3_ORIGINAL(ctx).disconnect(Q3_ORIGINAL(ctx).ctx, player, reason);
  if (!ok) qa_rethrow(&caught);
}
static const void *q3_game_state(void *ctx, qa_app_q3_player player, double server_id) { return Q3_ORIGINAL(ctx).game_state(Q3_ORIGINAL(ctx).ctx, player, server_id); }
static qa_app_q3_snapshot q3_snapshot(void *ctx, qa_app_q3_player player) { return Q3_ORIGINAL(ctx).snapshot(Q3_ORIGINAL(ctx).ctx, player); }
static void q3_begin(void *ctx, qa_app_q3_player player, const void *command) { Q3_ORIGINAL(ctx).begin(Q3_ORIGINAL(ctx).ctx, player, command); }
static const void *q3_input(void *ctx, qa_app_q3_player player, const void *command, double sequence) {
  return Q3_ORIGINAL(ctx).input(Q3_ORIGINAL(ctx).ctx, player, command, sequence);
}
static void q3_command(void *ctx, qa_app_q3_player player, const char *name, const char *const *args, size_t count) {
  Q3_ORIGINAL(ctx).command(Q3_ORIGINAL(ctx).ctx, player, name, args, count);
}
static void q3_userinfo(void *ctx, qa_app_q3_player player, const char *value) { Q3_ORIGINAL(ctx).userinfo(Q3_ORIGINAL(ctx).ctx, player, value); }
static const char *q3_status(void *ctx, const char *challenge, bool detailed) { return Q3_ORIGINAL(ctx).status(Q3_ORIGINAL(ctx).ctx, challenge, detailed); }
static void q3_print(void *ctx, const char *text) { Q3_ORIGINAL(ctx).print(Q3_ORIGINAL(ctx).ctx, text); }
static qa_app_q3_admission q3_connect(void *ctx, qa_client_id client, const char *userinfo) {
  host_binding *binding = ctx;
  return binding->original_q3.connect(binding->original_q3.host.ctx, client, userinfo);
}
static qa_app_sim_q3_server_authority q3_overridden(host_binding *binding) {
  qa_app_sim_q3_server_authority authority = binding->original_q3;
  qa_app_q3_server_host *host = &authority.host;
  host->ctx = binding;
  if (host->prepare) host->prepare = q3_prepare;
  if (host->pure) host->pure = q3_pure;
  if (host->downloads_enabled) host->downloads_enabled = q3_downloads_enabled;
  if (host->open_download) host->open_download = q3_open_download;
  if (host->rate) host->rate = q3_rate;
  if (host->supports_source_wire) host->supports_source_wire = q3_supports;
  if (host->time) host->time = q3_time;
  if (host->occupied_slots) host->occupied_slots = q3_occupied;
  if (host->admit) host->admit = q3_admit;
  if (host->carried_player) host->carried_player = q3_carried;
  host->disconnect = q3_disconnect;
  if (host->game_state) host->game_state = q3_game_state;
  if (host->snapshot) host->snapshot = q3_snapshot;
  if (host->begin) host->begin = q3_begin;
  if (host->input) host->input = q3_input;
  if (host->command) host->command = q3_command;
  if (host->userinfo) host->userinfo = q3_userinfo;
  if (host->status) host->status = q3_status;
  if (host->print) host->print = q3_print;
  if (authority.connect) authority.connect = q3_connect;
  return authority;
}

qa_application_native_server_host qa_application_network_host(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, double server_count) {
  if (simulation == NULL) simulation = app->world_simulation;
  if (content == NULL) content = app->loaded_content;
  if (server_count < 0) server_count = app->native_world_count;
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);
  const qa_catalog_product_expectation *source =
      &qa_content_catalog_product(catalog, qa_app_loaded_content_recipe(content)->map.entities.content)->expectation;
  const qa_app_options *options = &app->launch_options;
  if (options->has_q2_protocol && source->family != QA_GAME_FAMILY_Q2) qa_raise("Error", 0, 0, "--q2-protocol requires a Quake II source game");
  if (options->has_q1_protocol && source->family != QA_GAME_FAMILY_Q1) qa_raise("Error", 0, 0, "--q1-protocol requires a Quake I source game");
  if (strcmp(options->network_kind, "q2-server") == 0 && source->family != QA_GAME_FAMILY_Q2)
    qa_raise("Error", 0, 0, "--listen-q2 requires a Quake II source game; use --listen for the selected native protocol");
  qa_cvar_registry *cvars = qa_application_source_cvars(app, simulation);
  qa_app_server_operator_state *state = app->operator_state;
  if (cvars == NULL || state == NULL) qa_raise("Error", 0, 0, "Server administration was not initialized");
  host_binding *binding = checked(calloc(1, sizeof *binding));
  binding->app = app; binding->simulation = simulation; binding->cvars = cvars; binding->state = state;
  qa_app_source_administration_options shared = { qa_cvar_registry_dialect(cvars), state, cvars, options->dedicated,
    binding, binding_execute, binding, binding_record };
  binding->administration = qa_app_source_server_administration(&shared);
  binding->network_simulation = qa_app_shared_sim_network_simulation(simulation);
  binding->network_content = qa_app_shared_sim_network_content(simulation, content);
  remember(app, host_binding_free, binding);
  const bool rerelease = source->edition != NULL && strcmp(source->edition, "rerelease") == 0;
  switch (source->family) {
    case QA_GAME_FAMILY_Q1:
      if (source->edition != NULL && strcmp(source->edition, "quakeworld") == 0) {
        /* masters: administration.masters().filter(ipv4 | ipv6). */
        size_t count = 0;
        const qa_net_address *masters = binding->administration.masters(binding->administration.ctx, &count);
        binding->master_pointers = checked(calloc(count + 1, sizeof *binding->master_pointers));
        for (size_t i = 0; i < count; i++)
          if (masters[i].kind == QA_NET_IPV4 || masters[i].kind == QA_NET_IPV6) binding->master_pointers[binding->master_count++] = &masters[i];
        binding->qw_administration.ctx = binding;
        binding->qw_administration.rcon_password = qw_rcon_password;
        binding->qw_administration.blocked = qw_blocked;
        binding->qw_administration.status = qw_status;
        binding->qw_administration.log = qw_log;
        binding->qw_administration.execute_admin = qw_execute_admin;
        qa_app_sim_qw_server_binding_options qw;
        memset(&qw, 0, sizeof qw);
        qw.session = app->session; qw.simulation = &binding->network_simulation; qw.content = &binding->network_content;
        qw.quakec = qa_app_shared_sim_quakec_source(simulation);
        qw.server_count = server_count;
        qw.administration = &binding->qw_administration;
        qw.masters = binding->master_pointers; qw.master_count = binding->master_count;
        qw.print_ctx = binding; qw.print = binding_print;
        binding->host.kind = QA_APPLICATION_NETWORK_QW;
        binding->host.qw = qa_app_sim_create_qw_application_server_host(qw);
      } else {
        qa_app_sim_q1_server_binding_options q1;
        memset(&q1, 0, sizeof q1);
        q1.session = app->session;
        q1.rejects_ctx = binding; q1.rejects = binding_rejects;
        q1.simulation = &binding->network_simulation; q1.content = &binding->network_content;
        if (options->has_q1_protocol) q1.protocol = options->q1_protocol;
        else { q1.protocol.kind = "q1-netquake"; q1.protocol.version = 15; }
        q1.quakec = qa_app_shared_sim_quakec_source(simulation);
        q1.print_ctx = binding; q1.print = binding_print;
        binding->host.kind = QA_APPLICATION_NETWORK_Q1;
        binding->host.q1 = qa_app_sim_create_q1_application_server_host(q1);
      }
      break;
    case QA_GAME_FAMILY_Q2: {
      qa_app_sim_classic_guest_world *classic = NULL;
      qa_app_sim_rerelease_guest_world *rerelease_world = NULL;
      bool native = qa_app_shared_sim_q2_native(simulation, &classic, &rerelease_world);
      binding->q2_operator.ctx = binding;
      binding->q2_operator.profile_rerelease = rerelease;
      binding->q2_operator.rcon_password = q2_rcon_password;
      binding->q2_operator.limited_rcon = q2_limited_rcon;
      binding->q2_operator.rcon_rate_allowed = q2_rcon_rate_allowed;
      binding->q2_operator.recharge_rcon_rate = q2_recharge_rcon_rate;
      binding->q2_operator.execute_rcon = q2_execute_rcon;
      size_t count = 0;
      const qa_net_address *masters = binding->administration.masters(binding->administration.ctx, &count);
      binding->master_pointers = checked(calloc(count + 1, sizeof *binding->master_pointers));
      for (size_t i = 0; i < count; i++) binding->master_pointers[binding->master_count++] = &masters[i];
      qa_app_sim_q2_server_binding_options q2;
      memset(&q2, 0, sizeof q2);
      q2.session = app->session;
      q2.rejects_ctx = binding; q2.rejects = binding_rejects;
      q2.simulation = &binding->network_simulation; q2.content = &binding->network_content;
      q2.protocol = qa_app_live_q2_protocol(options, rerelease);
      q2.administration = &binding->q2_operator;
      q2.masters = binding->master_pointers; q2.master_count = binding->master_count;
      q2.player_identity_ctx = binding; q2.player_identity = q2_player_identity;
      q2.print_ctx = binding; q2.print = binding_print;
      binding->host.kind = QA_APPLICATION_NETWORK_Q2;
      if (!native) {
        binding->host.q2 = qa_app_sim_create_q2_application_server_host(q2);
        binding->q2_destroy = qa_app_sim_q2_application_server_host_destroy;
      } else {
        qa_app_sim_q2_guest_world world = qa_app_shared_sim_q2_guest_world(simulation);
        if (classic != NULL) {
          binding->host.q2 = qa_app_sim_create_classic_q2_application_server_host(q2, &world);
          binding->q2_destroy = qa_app_sim_classic_q2_application_server_host_destroy;
        } else {
          binding->host.q2 = qa_app_sim_create_rerelease_native_q2_application_server_host(q2, &world);
          binding->q2_destroy = qa_app_sim_rerelease_native_q2_application_server_host_destroy;
        }
      }
      break;
    }
    case QA_GAME_FAMILY_Q3: {
      qa_app_sim_q3_server_binding_options q3;
      memset(&q3, 0, sizeof q3);
      q3.session = app->session; q3.simulation = &binding->network_simulation; q3.content = &binding->network_content;
      q3.administration = &binding->administration;
      q3.print_ctx = binding; q3.print = binding_print;
      binding->original_q3 = qa_app_sim_create_q3_application_server_host(q3);
      binding->host.kind = QA_APPLICATION_NETWORK_Q3;
      binding->host.q3 = q3_overridden(binding);
      break;
    }
  }
  return binding->host;
}

/* ---- validateNetworkHost (2043-2047) ---- */
static const char *network_kind_upper(qa_application_network_kind kind) {
  return kind == QA_APPLICATION_NETWORK_Q1 ? "Q1" : kind == QA_APPLICATION_NETWORK_QW ? "QW" : kind == QA_APPLICATION_NETWORK_Q2 ? "Q2" : "Q3";
}
void qa_application_validate_network_host(qa_application *app, const qa_application_native_server_host *next) {
  if (app->network != NULL && app->network->kind != next->kind)
    qa_raise("Error", 0, 0, "A native server cannot change wire families while carrying connected clients");
  const void *supported =
      next->kind == QA_APPLICATION_NETWORK_Q1 ? next->q1.supports_source_wire(next->q1.ctx)
    : next->kind == QA_APPLICATION_NETWORK_QW ? next->qw.supports_source_wire(next->qw.ctx)
    : next->kind == QA_APPLICATION_NETWORK_Q2 ? next->q2.supports_source_wire(next->q2.ctx)
    : next->q3.host.supports_source_wire(next->q3.host.ctx);
  const qa_wire_admission *admission = supported;
  if (admission == NULL || admission->supported) return;
  size_t total = 1;
  for (size_t i = 0; i < admission->reason_count; i++) total += strlen(admission->reasons[i]) + 2;
  char *reasons = checked(malloc(total));
  reasons[0] = '\0';
  for (size_t i = 0; i < admission->reason_count; i++) {
    if (i != 0) strcat(reasons, "; ");
    strcat(reasons, admission->reasons[i]);
  }
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, reasons);
  qa_raise("Error", 0, 0, "Native %s wire is unavailable: %s", network_kind_upper(next->kind), reasons);
}

/* ---- changeNetworkWorld (2049-2065) ---- */
void qa_application_change_network_world(qa_application *app, const qa_application_native_server_host *next) {
  qa_application_native_server *network = app->network;
  if (network == NULL) qa_raise("Error", 0, 0, "Native world replacement requires an open server");
  if (app->recording_host == NULL) app->recording_host = checked(malloc(sizeof *app->recording_host));
  *app->recording_host = *next;
  switch (next->kind) {
    case QA_APPLICATION_NETWORK_Q1:
      if (network->kind != QA_APPLICATION_NETWORK_Q1) qa_raise("Error", 0, 0, "Native Q1 host cannot replace another wire family");
      qa_app_q1_server_network_change_world(network->q1, next->q1); return;
    case QA_APPLICATION_NETWORK_QW:
      if (network->kind != QA_APPLICATION_NETWORK_QW) qa_raise("Error", 0, 0, "Native QW host cannot replace another wire family");
      qa_app_qw_server_network_change_world(network->qw, next->qw); app->native_world_count++; return;
    case QA_APPLICATION_NETWORK_Q2: {
      if (network->kind != QA_APPLICATION_NETWORK_Q2) qa_raise("Error", 0, 0, "Native Q2 host cannot replace another wire family");
      qa_app_client_demo_recording *recorder = app->ownership.borrowed_client != NULL ? app->ownership.borrowed_client->recording : app->owned_recording;
      if (recorder != NULL && qa_app_client_demo_recording_server_path(recorder) != NULL) {
        const char *error = qa_app_client_demo_recording_stop_server(recorder);
        if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
      }
      qa_app_q2_server_network_change_world(network->q2, next->q2); return;
    }
    case QA_APPLICATION_NETWORK_Q3:
      if (network->kind != QA_APPLICATION_NETWORK_Q3) qa_raise("Error", 0, 0, "Native Q3 host cannot replace another wire family");
      qa_app_q3_server_network_change_world(network->q3, next->q3.host, NULL, 0); return;
    case QA_APPLICATION_NETWORK_UNIFIED: return;
  }
}

/* ---- openNetwork (2067-2099) ---- */
/* crypto.getRandomValues(new Uint32Array(1))[0] ?? 0. */
static double network_random(void *ctx) {
  (void)ctx;
  uint32_t value = 0;
  if (getrandom(&value, sizeof value, 0) != (ssize_t)sizeof value) qa_raise("Error", 0, 0, "Random source unavailable");
  return (double)value;
}
/* The transport and the server built over it (TS NativeServer); released with the application. */
typedef struct network_record {
  qa_app_application_transport *transport;
  qa_datagram_transport datagram;
  qa_application_native_server *server;
  qa_app_unified_server_host unified_host;
  qa_app_unified_composition *composition;
  qa_app_sim_network_simulation network_simulation;
  qa_app_sim_network_content network_content;
} network_record;
static void network_record_free(void *record) {
  network_record *network = record;
  qa_application_native_server *server = network->server;
  if (server != NULL) {
    switch (server->kind) {
      case QA_APPLICATION_NETWORK_Q1: qa_app_q1_server_network_destroy(server->q1); break;
      case QA_APPLICATION_NETWORK_QW: qa_app_qw_server_network_destroy(server->qw); break;
      case QA_APPLICATION_NETWORK_Q2: qa_app_q2_server_network_destroy(server->q2); break;
      case QA_APPLICATION_NETWORK_Q3: qa_app_q3_server_network_destroy(server->q3); break;
      case QA_APPLICATION_NETWORK_UNIFIED: qa_app_unified_server_network_destroy(server->unified); break;
    }
    qa_net_address_destroy(&server->address);
    free(server);
  }
  if (network->unified_host.ctx != NULL) qa_app_sim_unified_application_server_host_destroy(network->unified_host);
  qa_app_unified_composition_destroy(network->composition);
  if (network->transport != NULL) qa_app_application_transport_destroy(network->transport);
  free(network);
}
static bool operator_transport_send(void *ctx, const qa_net_address *to, const uint8_t *bytes, size_t length) {
  network_record *network = ctx;
  return to->kind != QA_NET_LOOPBACK && qa_app_application_transport_send(network->transport, to, bytes, length);
}
static void network_print(void *ctx, const char *text) { qa_application_print(ctx, text); }
static qa_app_network_family family_of(qa_application_network_kind kind) {
  return kind == QA_APPLICATION_NETWORK_Q1 ? QA_APP_FAMILY_Q1 : kind == QA_APPLICATION_NETWORK_QW ? QA_APP_FAMILY_QW
    : kind == QA_APPLICATION_NETWORK_Q2 ? QA_APP_FAMILY_Q2 : QA_APP_FAMILY_Q3;
}
static qa_app_network_transport_selection transport_selection(const qa_app_options *options) {
  qa_app_network_transport_selection selection = { QA_APP_TRANSPORT_UDP, NULL };
  const char *kind = options->network_transport_kind;
  if (kind != NULL && strcmp(kind, "ipx-native") == 0) selection.kind = QA_APP_TRANSPORT_IPX_NATIVE;
  else if (kind != NULL && strcmp(kind, "ipx-dosbox") == 0) { selection.kind = QA_APP_TRANSPORT_IPX_DOSBOX; selection.relay = options->network_transport_relay; }
  return selection;
}
static char *address_text(const qa_net_address *address) { return checked(qa_net_address_key(address, true)); }
/* The body after the transport opens: TS try { this.network = ...; print } catch { transport.close(); throw }. */
typedef struct open_server_call { qa_application *app; network_record *network; const qa_application_native_server_host *selected; } open_server_call;
static void open_native_server(void *ctx) {
  open_server_call *call = ctx;
  qa_application *app = call->app;
  network_record *network = call->network;
  const qa_application_native_server_host *selected = call->selected;
  qa_application_native_server *server = checked(calloc(1, sizeof *server));
  network->server = server;
  (void)qa_app_application_transport_address(network->transport, &server->address);
  server->kind = selected->kind;
  switch (selected->kind) {
    case QA_APPLICATION_NETWORK_Q1: {
      qa_app_q1_server_network_options options = { &network->datagram, selected->q1, false, 0, app->launch_options.trusted_networks_only };
      server->q1 = checked(qa_app_q1_server_network_create(options));
      break;
    }
    case QA_APPLICATION_NETWORK_QW: {
      qa_app_udp_socket *socket = qa_app_application_transport_udp_socket(network->transport);
      qa_app_qw_server_network_options options = { &socket->transport, selected->qw, network_random, NULL, false, 0 };
      server->qw = checked(qa_app_qw_server_network_create(options));
      break;
    }
    case QA_APPLICATION_NETWORK_Q2: {
      qa_app_q2_server_network_options options = { &network->datagram, selected->q2, network_random, NULL, false, 0,
          app->launch_options.trusted_networks_only };
      server->q2 = checked(qa_app_q2_server_network_create(options));
      break;
    }
    case QA_APPLICATION_NETWORK_Q3: {
      qa_app_q3_server_network_options options;
      memset(&options, 0, sizeof options);
      options.transport = &network->datagram; options.host = selected->q3.host; options.random = network_random;
      server->q3 = checked(qa_app_q3_server_network_create(options));
      break;
    }
    case QA_APPLICATION_NETWORK_UNIFIED: break;
  }
  app->network = server;
  char *address = address_text(&server->address);
  qa_application_printf(app, "Listening for %s peers on %s.\n", network_kind_upper(selected->kind), address);
  free(address);
}
static void open_unified_server(void *ctx) {
  open_server_call *call = ctx;
  qa_application *app = call->app;
  network_record *network = call->network;
  qa_application_native_server *server = checked(calloc(1, sizeof *server));
  network->server = server;
  (void)qa_app_application_transport_address(network->transport, &server->address);
  server->kind = QA_APPLICATION_NETWORK_UNIFIED;
  qa_app_unified_server_options options = { &network->datagram, network->unified_host, network->composition, app, network_print };
  server->unified = checked(qa_app_unified_server_network_create(options));
  app->network = server;
  char *address = address_text(&server->address);
  qa_application_printf(app, "Listening for mixed-game peers on %s.\n", address);
  free(address);
}
static qa_app_unified_composition *build_unified_composition(qa_app_loaded_content *content) {
  size_t count = 0;
  const qa_app_application_map_sidecar *sidecars = qa_app_loaded_content_map_sidecars(content, &count);
  qa_app_unified_map_sidecar *observed = checked(calloc(count + 1, sizeof *observed));
  for (size_t i = 0; i < count; i++) {
    observed[i].content = sidecars[i].content; observed[i].path = sidecars[i].path;
    observed[i].has_resource = sidecars[i].resource != NULL;
    if (observed[i].has_resource) observed[i].resource = qa_app_unified_resource_key_from_reference(sidecars[i].resource);
  }
  qa_save_value *recipe = checked(qa_save_recipe_value(qa_app_loaded_content_recipe(content)));
  qa_app_unified_composition *composition = qa_app_unified_build_composition(recipe, observed, count);
  qa_save_value_free(recipe);
  free(observed);
  return checked(composition);
}
static void open_transport_or_close(qa_application *app, network_record *network, void (*body)(void *ctx), open_server_call *call) {
  qa_caught_error caught;
  if (qa_try(body, call, &caught)) return;
  qa_app_application_transport_close(network->transport);
  (void)app;
  qa_rethrow(&caught);
}
void qa_application_open_network(qa_application *app) {
  const qa_app_options *options = &app->launch_options;
  const char *selection = options->network_kind;
  if (strcmp(selection, "unified-server") == 0) {
    network_record *network = checked(calloc(1, sizeof *network));
    remember(app, network_record_free, network);
    network->network_simulation = qa_app_shared_sim_network_simulation(app->world_simulation);
    network->network_content = qa_app_shared_sim_network_content(app->world_simulation, app->loaded_content);
    qa_app_sim_unified_host_options host;
    memset(&host, 0, sizeof host);
    host.session = app->session; host.simulation = &network->network_simulation; host.content = &network->network_content;
    host.print_ctx = app; host.print = network_print;
    network->unified_host = qa_app_sim_create_unified_application_server_host(host);
    network->composition = build_unified_composition(app->loaded_content);
    qa_app_open_transport_options open = { { QA_APP_TRANSPORT_UDP, NULL }, QA_APP_FAMILY_Q2, options->network_host, options->network_port,
      QA_UNIFIED_DATAGRAM_LIMITS_NET, NULL };
    network->transport = checked(qa_app_open_application_transport(&open, NULL));
    network->datagram = qa_app_application_transport_as_transport(network->transport);
    open_server_call call = { app, network, NULL };
    open_transport_or_close(app, network, open_unified_server, &call);
    return;
  }
  if (strcmp(selection, "q2-server") != 0 && strcmp(selection, "native-server") != 0) {
    if (is_q2_native(app->world_simulation)) {
      qa_application_native_server_host host = qa_application_network_host(app, NULL, NULL, -1);
      if (app->recording_host == NULL) app->recording_host = checked(malloc(sizeof *app->recording_host));
      *app->recording_host = host;
    }
    return;
  }
  qa_application_native_server_host selected = qa_application_network_host(app, NULL, NULL, -1);
  qa_application_validate_network_host(app, &selected);
  if (app->recording_host == NULL) app->recording_host = checked(malloc(sizeof *app->recording_host));
  *app->recording_host = selected;
  qa_datagram_limits limits = selected.kind == QA_APPLICATION_NETWORK_Q1 ? QA_UNIFIED_DATAGRAM_LIMITS_NET
    : selected.kind == QA_APPLICATION_NETWORK_Q2 ? QA_Q2_DATAGRAM_LIMITS_NET : QA_Q3_DATAGRAM_LIMITS_NET;
  network_record *network = checked(calloc(1, sizeof *network));
  remember(app, network_record_free, network);
  qa_app_open_transport_options open = { transport_selection(options), family_of(selected.kind), options->network_host,
    options->network_port, limits, NULL };
  network->transport = checked(qa_app_open_application_transport(&open, NULL));
  network->datagram = qa_app_application_transport_as_transport(network->transport);
  app->operator_send = operator_transport_send; app->operator_send_ctx = network;
  open_server_call call = { app, network, &selected };
  open_transport_or_close(app, network, open_native_server, &call);
}

/* ---- recording feeds (1873-1977) ---- */
/* TS ClientRecordingFeed as the application prepares it: the local recorder or one
 * of the hosted Q2 server's feeds. The seed's packets stay owned by the feed until
 * the next seed or its release. */
typedef enum feed_kind { FEED_LOCAL, FEED_SERVER, FEED_MVD } feed_kind;
struct qa_application_recording_feed {
  qa_application *app;
  feed_kind kind;
  char *root;
  qa_application_native_server *network;
  qa_app_q2_server_recording_seed server_seed; bool has_server_seed;
  qa_app_q2_mvd_recording_seed mvd_seed; bool has_mvd_seed;
  qa_app_demo_recording_packet *packets;
  qa_app_local_demo_recording_attachment local;
};
static void feed_free_seed(qa_application_recording_feed *feed) {
  if (feed->has_server_seed) qa_app_q2_server_recording_seed_free(&feed->server_seed);
  if (feed->has_mvd_seed) qa_app_q2_mvd_recording_seed_free(&feed->mvd_seed);
  feed->has_server_seed = feed->has_mvd_seed = false;
  free(feed->packets); feed->packets = NULL;
}
static void feed_record_free(void *record) {
  qa_application_recording_feed *feed = record;
  feed_free_seed(feed);
  free(feed->root);
  free(feed);
}
const char *qa_application_recording_feed_root(const qa_application_recording_feed *feed) { return feed->root; }
qa_app_demo_recording_seed qa_application_recording_feed_seed(qa_application_recording_feed *feed) {
  qa_app_demo_recording_seed seed;
  memset(&seed, 0, sizeof seed);
  if (feed->kind == FEED_LOCAL) return qa_app_local_demo_recording_seed(feed->app->local_recording);
  feed_free_seed(feed);
  if (feed->kind == FEED_SERVER) {
    qa_app_q2_server_network_server_recording_seed(feed->network->q2, &feed->server_seed);
    feed->has_server_seed = true;
    feed->packets = checked(calloc(1, sizeof *feed->packets));
    feed->packets[0].kind = QA_APP_RECORDING_Q2_SERVER;
    feed->packets[0].message = feed->server_seed.message; feed->packets[0].message_length = feed->server_seed.length;
    seed.identity.kind = QA_APP_RECORDING_Q2_SERVER; seed.identity.protocol = feed->server_seed.protocol;
    seed.packets = feed->packets; seed.packet_count = 1;
    return seed;
  }
  qa_app_q2_server_network_mvd_recording_seed(feed->network->q2, &feed->mvd_seed);
  feed->has_mvd_seed = true;
  feed->packets = checked(calloc(feed->mvd_seed.packet_count + 1, sizeof *feed->packets));
  for (size_t i = 0; i < feed->mvd_seed.packet_count; i++) {
    feed->packets[i].kind = QA_APP_RECORDING_MVD;
    feed->packets[i].message = feed->mvd_seed.packets[i]; feed->packets[i].message_length = feed->mvd_seed.lengths[i];
  }
  seed.identity.kind = QA_APP_RECORDING_MVD; seed.identity.protocol = feed->mvd_seed.revision;
  seed.packets = feed->packets; seed.packet_count = feed->mvd_seed.packet_count;
  return seed;
}
/* The Q2 network writes (kind, message) records; the demo sink takes packets. */
typedef struct q2_sink_bridge { qa_app_demo_recording_sink sink; void *token; qa_application_recording_feed *feed; } q2_sink_bridge;
static void q2_sink_append(void *ctx, const char *kind, const uint8_t *message, size_t length) {
  q2_sink_bridge *bridge = ctx;
  qa_app_demo_recording_packet packet;
  memset(&packet, 0, sizeof packet);
  packet.kind = strcmp(kind, "mvd") == 0 ? QA_APP_RECORDING_MVD : QA_APP_RECORDING_Q2_SERVER;
  packet.message = message; packet.message_length = length;
  const char *error = bridge->sink.append(bridge->sink.ctx, packet);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
void *qa_application_recording_feed_attach(qa_application_recording_feed *feed, qa_app_demo_recording_sink sink) {
  qa_application *app = feed->app;
  if (feed->kind == FEED_LOCAL) {
    if (app->closed || app->closing) qa_raise("Error", 0, 0, "Recording source was retired");
    feed->local = qa_app_local_demo_recording_attach(app->local_recording, sink);
    app->local_recording_active = true;
    return &feed->local;
  }
  if (app->closed || app->closing || app->network != feed->network)
    qa_raise("Error", 0, 0, feed->kind == FEED_MVD ? "Multiview server was retired before attachment" : "Server demo source was retired before attachment");
  q2_sink_bridge *bridge = checked(calloc(1, sizeof *bridge));
  bridge->sink = sink; bridge->feed = feed;
  qa_app_q2_recording_sink q2 = { bridge, q2_sink_append };
  bridge->token = feed->kind == FEED_MVD ? qa_app_q2_server_network_mvd_recording_attach(feed->network->q2, q2)
    : qa_app_q2_server_network_server_recording_attach(feed->network->q2, q2);
  return bridge;
}
void qa_application_recording_feed_detach(qa_application_recording_feed *feed, void *token) {
  if (feed->kind == FEED_LOCAL) {
    qa_app_local_demo_recording_detach(token);
    feed->app->local_recording_active = false;
    return;
  }
  q2_sink_bridge *bridge = token;
  if (feed->kind == FEED_MVD) qa_app_q2_server_network_mvd_recording_detach(feed->network->q2, bridge->token);
  else qa_app_q2_server_network_server_recording_detach(feed->network->q2, bridge->token);
  free(bridge);
}
static qa_application_recording_feed *new_feed(qa_application *app, feed_kind kind) {
  qa_application_recording_feed *feed = checked(calloc(1, sizeof *feed));
  feed->app = app; feed->kind = kind;
  qa_config_store config = qa_application_client_config(&app->launch_options, app->loaded_content);
  feed->root = config.root;
  remember(app, feed_record_free, feed);
  return feed;
}
/* let origin = context.origin; while (origin.kind === "script") origin = origin.caller; */
static const qa_command_origin *root_origin(const qa_command_context *context) {
  const qa_command_origin *origin = &context->origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  return origin;
}
static qa_application_recording_feed *prepare_q2_feed(qa_application *app, qa_command_context context, feed_kind kind) {
  bool mvd = kind == FEED_MVD;
  if (context.session.token != qa_engine_session_id(app->session).token)
    qa_raise("Error", 0, 0, mvd ? "Multiview command belongs to another session" : "Server demo command belongs to another session");
  if (root_origin(&context)->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT)
    qa_raise("Error", 0, 0, mvd ? "Multiview recording requires the local server console" : "Server demo recording requires the local server console");
  qa_application_native_server *network = app->network;
  if (network == NULL || network->kind != QA_APPLICATION_NETWORK_Q2)
    qa_raise("Error", 0, 0, mvd ? "mvdrecord requires a hosted Quake II server" : "serverrecord requires a hosted Quake II server");
  qa_application_recording_feed *feed = new_feed(app, kind);
  feed->network = network;
  return feed;
}
qa_application_recording_feed *qa_application_prepare_mvd_recording(qa_application *app, qa_command_context context) {
  return prepare_q2_feed(app, context, FEED_MVD);
}
qa_application_recording_feed *qa_application_prepare_server_recording(qa_application *app, qa_command_context context) {
  return prepare_q2_feed(app, context, FEED_SERVER);
}
static qa_app_local_recording_source local_recording_read(void *ctx) { return qa_application_local_recording_source(ctx); }
qa_application_recording_feed *qa_application_prepare_recording(qa_application *app, qa_command_context context) {
  if (context.session.token != qa_engine_session_id(app->session).token) qa_raise("Error", 0, 0, "Recording command belongs to another session");
  const qa_command_origin *origin = root_origin(&context);
  if (origin->kind == QA_COMMAND_ORIGIN_REMOTE_CLIENT) qa_raise("Error", 0, 0, "Recording requires a local player");
  bool requested = origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT;
  size_t count = qa_application_local_player_count(app);
  bool found = false; qa_app_local_player player; memset(&player, 0, sizeof player);
  for (size_t i = 0; i < count && !found; i++) {
    qa_app_local_player candidate = qa_application_local_player(app, i);
    if (!requested || (qa_same_seat(qa_session_seat_id(candidate.seat), origin->seat)
        && qa_same_client(qa_session_client_id(qa_session_seat_client(candidate.seat)), origin->client))) { player = candidate; found = true; }
  }
  if (!found) qa_raise("Error", 0, 0, "Recording requires an active local player");
  if (app->recording_host == NULL) {
    qa_application_native_server_host host;
    if (app->local_guest == NULL) host = qa_application_network_host(app, NULL, NULL, -1);
    else { memset(&host, 0, sizeof host); host.kind = QA_APPLICATION_NETWORK_Q3; host.q3 = app->local_guest->authority; }
    app->recording_host = checked(malloc(sizeof *app->recording_host));
    *app->recording_host = host;
    if (host.kind == QA_APPLICATION_NETWORK_Q3 && app->local_guest == NULL) {
      qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
      qa_cvar_read read;
      if (cvars == NULL || !qa_cvar_registry_find(cvars, "sv_serverid", &read)) qa_raise("Error", 0, 0, "Q3 recording requires the published source epoch");
      qa_cvar_read_free(&read);
      host.q3.host.prepare(host.q3.host.ctx, 0, qa_cvar_registry_variable_value(cvars, "sv_serverid"), NULL, NULL);
    }
    if (host.kind != QA_APPLICATION_NETWORK_Q3) {
      qa_simulation_output output = qa_app_shared_sim_current_output(app->world_simulation);
      if (host.kind == QA_APPLICATION_NETWORK_Q1) host.q1.observe(host.q1.ctx, &output, NULL, 0);
      else if (host.kind == QA_APPLICATION_NETWORK_QW) host.qw.observe(host.qw.ctx, &output, NULL, 0);
      else host.q2.observe(host.q2.ctx, &output, NULL, 0);
    }
  }
  app->has_recording_seat = true; app->recording_seat = qa_session_seat_id(player.seat);
  if (app->local_recording == NULL) app->local_recording = checked(qa_app_local_demo_recording_create(local_recording_read, app));
  return new_feed(app, FEED_LOCAL);
}
qa_app_local_recording_source qa_application_local_recording_source(qa_application *app) {
  qa_application_native_server_host *host = app->recording_host;
  qa_app_local_input *local = NULL;
  if (app->graphical != NULL && app->has_recording_seat) {
    size_t count = qa_app_application_input_local_count(app->graphical->input);
    for (size_t i = 0; i < count && local == NULL; i++) {
      qa_app_local_input *candidate = qa_app_application_input_local(app->graphical->input, i);
      if (qa_same_seat(app->recording_seat, qa_session_seat_id(candidate->player.seat))) local = candidate;
    }
  }
  if (host == NULL || local == NULL) qa_raise("Error", 0, 0, "Recorded local player is no longer active");
  qa_client_id client = qa_session_client_id(qa_session_seat_client(local->player.seat));
  qa_app_local_recording_source source;
  memset(&source, 0, sizeof source);
  source.world = app->world_simulation;
  switch (host->kind) {
    case QA_APPLICATION_NETWORK_Q1:
      source.kind = QA_APP_LOCAL_RECORDING_Q1; source.q1_host = &host->q1;
      source.q1_player = host->q1.carried_player(host->q1.ctx, client);
      source.view_angles = qa_input_command_builder_view_angles(local->builder);
      return source;
    case QA_APPLICATION_NETWORK_QW: {
      qa_app_sim_quakec_source *game = qa_app_shared_sim_quakec_source(app->world_simulation);
      if (game == NULL || strcmp(qa_app_sim_quakec_source_kind(game), "quakeworld") != 0) qa_raise("Error", 0, 0, "QW recording lost its source clock");
      source.kind = QA_APP_LOCAL_RECORDING_QW; source.qw_host = &host->qw;
      source.qw_player = host->qw.carried_player(host->qw.ctx, client);
      source.view_angles = qa_input_command_builder_view_angles(local->builder);
      source.seconds = qa_app_sim_quakec_source_time_seconds(game);
      return source;
    }
    case QA_APPLICATION_NETWORK_Q2:
      source.kind = QA_APP_LOCAL_RECORDING_Q2; source.q2_host = &host->q2;
      source.q2_player = host->q2.carried_player(host->q2.ctx, client);
      source.protocol = host->q2.protocol;
      return source;
    case QA_APPLICATION_NETWORK_Q3: default: {
      bool published = app->network != NULL && app->network->kind == QA_APPLICATION_NETWORK_Q3;
      qa_app_q3_recording_source recording = { 0 };
      if (published) recording = qa_app_q3_server_network_recording_source(app->network->q3);
      qa_cvar_registry *cvars = qa_application_source_cvars(app, NULL);
      qa_cvar_read read;
      bool has_epoch = cvars != NULL && qa_cvar_registry_find(cvars, "sv_serverid", &read);
      if (has_epoch) qa_cvar_read_free(&read);
      if (!published && !has_epoch) qa_raise("Error", 0, 0, "Q3 recording lost its source epoch");
      const qa_app_q3_server_host *q3 = published ? recording.host : &host->q3.host;
      /* The local recorder only reads the host (demo_local_recording.c); its field is non-const. */
      source.kind = QA_APP_LOCAL_RECORDING_Q3; source.q3_host = (qa_app_q3_server_host *)q3;
      source.q3_player = q3->carried_player(q3->ctx, client);
      source.server_id = published ? recording.server_id : cvars != NULL ? qa_cvar_registry_variable_value(cvars, "sv_serverid") : 0;
      source.snapshot_server_bit = published ? recording.snapshot_server_bit : app->local_snapshot_server_bit;
      return source;
    }
  }
}

/* standaloneRecording (1873-1894): the ClientDemoRecording over the feeds above. */
typedef struct standalone_host { qa_application *app; qa_application_recording_feed *feed; char error[QA_CAUGHT_MESSAGE_CAPACITY]; } standalone_host;
static const char *standalone_root(void *ctx) {
  standalone_host *host = ctx;
  if (host->feed == NULL) qa_raise("Error", 0, 0, "Recording has no selected source");
  return host->feed->root;
}
typedef struct standalone_seed { standalone_host *host; qa_command_context source; feed_kind kind; qa_app_demo_recording_seed *out; } standalone_seed;
static void standalone_seed_run(void *ctx) {
  standalone_seed *call = ctx;
  qa_application *app = call->host->app;
  call->host->feed = call->kind == FEED_LOCAL ? qa_application_prepare_recording(app, call->source)
    : call->kind == FEED_SERVER ? qa_application_prepare_server_recording(app, call->source) : qa_application_prepare_mvd_recording(app, call->source);
  *call->out = qa_application_recording_feed_seed(call->host->feed);
}
static const char *standalone_seed_kind(standalone_host *host, qa_command_context source, feed_kind kind, qa_app_demo_recording_seed *out) {
  standalone_seed call = { host, source, kind, out };
  qa_caught_error caught;
  if (qa_try(standalone_seed_run, &call, &caught)) return NULL;
  snprintf(host->error, sizeof host->error, "%s", caught.message);
  return host->error;
}
static const char *standalone_seed_local(void *ctx, qa_command_context source, qa_app_demo_recording_seed *out) { return standalone_seed_kind(ctx, source, FEED_LOCAL, out); }
static const char *standalone_seed_server(void *ctx, qa_command_context source, qa_app_demo_recording_seed *out) { return standalone_seed_kind(ctx, source, FEED_SERVER, out); }
static const char *standalone_seed_mvd(void *ctx, qa_command_context source, qa_app_demo_recording_seed *out) { return standalone_seed_kind(ctx, source, FEED_MVD, out); }
static const void *standalone_attach_kind(standalone_host *host, qa_app_demo_recording_sink sink, const char *missing) {
  if (host->feed == NULL) qa_raise("Error", 0, 0, "%s", missing);
  return qa_application_recording_feed_attach(host->feed, sink);
}
static const void *standalone_attach(void *ctx, qa_app_demo_recording_sink sink) { return standalone_attach_kind(ctx, sink, "Recording has no prepared feed"); }
static const void *standalone_attach_server(void *ctx, qa_app_demo_recording_sink sink) { return standalone_attach_kind(ctx, sink, "Server recording has no prepared feed"); }
static const void *standalone_attach_mvd(void *ctx, qa_app_demo_recording_sink sink) { return standalone_attach_kind(ctx, sink, "Multiview recording has no prepared feed"); }
static void standalone_detach(void *ctx, const void *token) {
  standalone_host *host = ctx;
  qa_application_recording_feed_detach(host->feed, (void *)token);
}
static const void *standalone_reconnect(void *ctx, qa_command_context source, qa_app_demo_recording_sink sink, const char **out_error) {
  (void)ctx; (void)source; (void)sink;
  *out_error = "rerecord requires the retained client connection owner";
  return NULL;
}
static void standalone_print(void *ctx, const char *text) { standalone_host *host = ctx; qa_application_print(host->app, text); }
static void standalone_stage(void *ctx, qa_app_demo_recording_intent intent) {
  standalone_host *host = ctx;
  bool stop = strcmp(intent.kind, "stop") == 0;
  bool bare = stop || strcmp(intent.kind, "mvdstop") == 0 || strcmp(intent.kind, "serverstop") == 0 || intent.name == NULL;
  const char *args[1] = { intent.name };
  qa_application_queue_command_source(host->app, stop ? "stop" : intent.kind, bare ? NULL : args, bare ? 0 : 1, NULL, &intent.source);
}
static void standalone_host_free(void *record) {
  standalone_host *host = record;
  free(host);
}
qa_app_client_demo_recording *qa_application_standalone_recording(qa_application *app) {
  if (app->ownership.borrowed_client != NULL) qa_raise("Error", 0, 0, "Borrowed sources use the retained client recorder");
  if (app->owned_recording != NULL) return app->owned_recording;
  standalone_host *state = checked(calloc(1, sizeof *state));
  state->app = app;
  remember(app, standalone_host_free, state);
  qa_app_demo_recording_host host;
  memset(&host, 0, sizeof host);
  host.ctx = state;
  host.has_server_recording = true;
  host.server_recording = (qa_app_recording_feed){ state, standalone_seed_server, standalone_attach_server, standalone_detach };
  host.has_mvd_recording = true;
  host.mvd_recording = (qa_app_recording_feed){ state, standalone_seed_mvd, standalone_attach_mvd, standalone_detach };
  host.root = standalone_root;
  host.main_feed = (qa_app_recording_feed){ state, standalone_seed_local, standalone_attach, standalone_detach };
  host.reconnect_recording = standalone_reconnect;
  host.print = standalone_print;
  host.stage = standalone_stage;
  app->owned_recording = checked(qa_app_client_demo_recording_create(host));
  return app->owned_recording;
}

/* ---- captureMap, prepareRetirement, readClientResource, releaseSettings,
 * adoptImageSettings, retire (2267-2308) ---- */
const char *qa_application_capture_map(qa_application *app) {
  return qa_app_loaded_content_recipe(app->loaded_content)->map.geometry.requested_path;
}
void qa_application_prepare_retirement(qa_application *app) {
  if (app->tools != NULL) {
    const char *error = qa_app_application_tools_drain(app->tools);
    if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
  }
  if (app->capture != NULL) qa_app_capture_before_world_change(app->capture);
  for (size_t i = 0; i < app->key_profile_count; i++)
    if (app->key_profile_simulations[i] == app->world_simulation) { qa_app_key_profile_save(app->key_profiles[i]); break; }
}
/* Malloc'd bytes; false is TS undefined. */
bool qa_application_read_client_resource(qa_application *app, const char *path, uint8_t **out_bytes, size_t *out_length) {
  qa_app_loaded_content *content = app->loaded_content;
  if (app->closed) qa_raise("Error", 0, 0, "Client source has retired");
  qa_content_opened_resource opened;
  bool found = qa_content_mounted_open((qa_content_mounted *)qa_app_loaded_content_mounts(content), path, NULL, NULL, &opened);
  if (app->closed || app->loaded_content != content) qa_raise("Error", 0, 0, "Client source changed during resource read");
  *out_bytes = NULL; *out_length = 0;
  if (!found) return false;
  *out_bytes = opened.bytes; /* the caller owns the opened bytes */
  *out_length = opened.byte_length;
  return true;
}
void qa_application_release_settings(qa_application *app) {
  if (app->tools != NULL) qa_app_application_tools_before_world_change(app->tools);
  qa_application_release release = app->release_view_cvars;
  if (release.fn != NULL) release.fn(release.ctx);
  app->release_view_cvars.fn = NULL; app->release_view_cvars.ctx = NULL;
}
/* settings?.bindPrint(text => { this.host.print(text); for (local of graphical.input.locals) local.console.print(text); }). */
static void image_settings_print(void *ctx, const char *text) { print_everywhere(ctx, text); }
void qa_application_adopt_image_settings(qa_application *app, qa_app_image_settings *settings) {
  app->image_settings = settings;
  if (settings != NULL) qa_app_image_settings_bind_print(settings, image_settings_print, app);
}
void qa_application_retire(qa_application *app) { qa_application_close(app); }
