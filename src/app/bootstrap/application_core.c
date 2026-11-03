/* Port of src/app/bootstrap/application.ts, region 408-886 and 1235-1300:
 * the Application constructor and field initializers, storage, and the
 * accessors. Layout: docs/application_layout.md. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include "qa/app/bootstrap/q2_localization.h"
#include "qa/app/bootstrap/simulation/classic_guest_files.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include "qa/app/bootstrap/simulation/rerelease_guest_services_contract.h"
#include "qa/app/bootstrap/weapon_behavior_selection.h"
#include "qa/content/catalog/index.h"
#include "qa/content/mounts.h"
#include "qa/contracts/mods.h"
#include "qa/platform/sdl.h"
#include "qa/text/localization_resources.h"
#include "qa/content/user_data.h"
#include "qa/app/bootstrap/save_requests.h"
#include "qa/network/common/js_number.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include "qa/console/session.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/content/q3/team_arena/client_spawn.h"
#include "qa/content/q3/team_arena/commands.h"
#include "qa/network/q3/configstrings.h"
#include "qa/world/collision/index.h"

static void *checked(void *p) {
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t n = strlen(text) + 1;
  char *out = checked(malloc(n));
  memcpy(out, text, n);
  return out;
}
static void grow(void **items, size_t *capacity, size_t needed, size_t size) {
  if (needed <= *capacity) return;
  size_t next = *capacity == 0 ? 4 : *capacity * 2;
  while (next < needed) next *= 2;
  *items = checked(realloc(*items, next * size));
  *capacity = next;
}

static void ranking_account_free(void *record);

void qa_application_command_request_free(qa_application_command_request *request) {
  free(request->name);
  for (size_t i = 0; i < request->argument_count; i++) free(request->arguments[i]);
  free(request->arguments);
  if (request->has_source) qa_command_context_release(&request->source);
  memset(request, 0, sizeof *request);
}

/* TS performanceNow. */
double qa_application_performance_now(void) {
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}

/* TS cvars.set(name, value, force) where the result is unused. */
void qa_application_cvar_set(qa_cvar_registry *cvars, const char *name, const char *value, bool force) {
  qa_cvar_snapshot snapshot;
  (void)qa_cvar_registry_set(cvars, name, value, force, &snapshot);
  qa_cvar_read_free(&snapshot);
}
/* TS cvars.variableString(name) compared or copied by the caller; malloc'd. */
char *qa_application_cvar_string(qa_cvar_registry *cvars, const char *name) {
  return checked(qa_cvar_registry_variable_string(cvars, name));
}

void qa_application_print(qa_application *app, const char *text) { app->host.print(app->host.ctx, text); }
void qa_application_printf(qa_application *app, const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list copy;
  va_copy(copy, args);
  int n = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  char *text = checked(malloc((size_t)(n < 0 ? 0 : n) + 1));
  (void)vsnprintf(text, (size_t)(n < 0 ? 0 : n) + 1, format, args);
  va_end(args);
  qa_application_print(app, text);
  free(text);
}

/* node:path normalize of an absolute or relative path (join() results):
 * collapses separators, drops ".", resolves ".." against earlier segments. */
char *qa_application_path_normalize(const char *path) {
  size_t n = strlen(path);
  char *out = checked(malloc(n + 2));
  bool absolute = path[0] == '/';
  size_t length = 0, depth = 0;
  const char *in = path;
  while (*in != '\0') {
    while (*in == '/') in++;
    if (*in == '\0') break;
    const char *segment = in;
    while (*in != '\0' && *in != '/') in++;
    size_t m = (size_t)(in - segment);
    if (m == 1 && segment[0] == '.') continue;
    if (m == 2 && segment[0] == '.' && segment[1] == '.') {
      if (depth > 0) {
        while (length > 0 && out[length - 1] != '/') length--;
        if (length > 0) length--;
        depth--;
        continue;
      }
      if (absolute) continue;
    } else depth++;
    if (length > 0 || absolute) out[length++] = '/';
    memcpy(out + length, segment, m);
    length += m;
  }
  if (length == 0) out[length++] = absolute ? '/' : '.';
  out[length] = '\0';
  return out;
}

/* ---- the qa_simulation the session attaches (SharedSimulation implements Simulation) ---- */
static qa_simulation_output view_step(void *ctx, qa_input_batch input) {
  qa_application_simulation_view *view = ctx;
  return qa_app_shared_sim_step(view->simulation, input);
}
typedef struct progress_forward {
  void (*progress)(void *ctx, qa_simulation_progress frame);
  void *ctx;
} progress_forward;
static void view_progress(void *ctx, qa_simulation_progress frame) {
  progress_forward *forward = ctx;
  forward->progress(forward->ctx, frame);
}
static qa_simulation_output view_step_async(void *ctx, qa_input_batch input,
    void (*progress)(void *progress_ctx, qa_simulation_progress frame), void *progress_ctx) {
  qa_application_simulation_view *view = ctx;
  if (progress == NULL) return qa_app_shared_sim_step_async(view->simulation, input, NULL, NULL);
  progress_forward forward = { progress, progress_ctx };
  return qa_app_shared_sim_step_async(view->simulation, input, view_progress, &forward);
}
static qa_save_image view_checkpoint(void *ctx) {
  qa_application_simulation_view *view = ctx;
  qa_app_shared_sim_save next;
  qa_app_shared_sim_checkpoint(view->simulation, &next);
  if (view->has_save) qa_app_shared_sim_save_free(&view->save);
  view->save = next; view->has_save = true;
  return view->save.image;
}
static void view_close(void *ctx) {
  qa_application_simulation_view *view = ctx;
  qa_app_shared_sim_close(view->simulation);
}
qa_simulation qa_application_simulation_contract(qa_application_simulation_view *view) {
  qa_simulation out;
  memset(&out, 0, sizeof out);
  out.ctx = view;
  out.session = qa_app_shared_sim_session(view->simulation);
  out.recipe = *qa_app_shared_sim_recipe(view->simulation);
  out.step = view_step;
  out.step_async = view_step_async;
  out.checkpoint = view_checkpoint;
  out.close = view_close;
  return out;
}

/* ---- field initializers ---- */
/* TS viewSettings = new ApplicationViewSettings(value => ...). */
static void view_settings_changed(void *ctx, double value) {
  qa_application *app = ctx;
  if (app->graphical == NULL) return;
  for (size_t i = 0; i < app->graphical->presentation_count; i++) {
    qa_application_seat_presentation *presentation = &app->graphical->presentations[i];
    if (presentation->q3_client == NULL || !presentation->q3_client->qvm)
      qa_app_shared_sim_set_player_field_of_view(app->world_simulation, presentation->local->player.actor, value, "change");
    if (presentation->q3_client != NULL) {
      char text[40];
      qa_net_js_number(value, text, sizeof text);
      qa_application_cvar_set(qa_app_application_q3_client_cvars(presentation->q3_client->client), "cg_fov", text, false);
    }
  }
}
/* TS transitions = new SharedTransitionCoordinator(decision => { this.pendingTransition = decision; }). */
static void transition_apply(void *ctx, qa_transition_decision decision) {
  qa_application *app = ctx;
  app->pending_transition = decision;
  app->has_pending_transition = true;
}
/* TS new ApplicationKeys(text => host.print(text)). */
static void keys_print(void *ctx, const char *text) {
  qa_application *app = ctx;
  qa_application_print(app, text);
}

qa_application *qa_application_construct(qa_app_options launch_options, qa_app_loaded_content *loaded_content,
    qa_engine_session *session, qa_app_shared_simulation *world_simulation, qa_application_host host,
    qa_identity_owner *identity, const qa_client_id *local_seat_clients, qa_session_seat *const *local_seats,
    size_t local_seat_count, qa_config_store input_config, qa_application_ownership ownership) {
  qa_application *app = checked(calloc(1, sizeof *app));
  app->launch_options = launch_options;
  app->loaded_content = loaded_content;
  app->session = session;
  app->world_simulation = world_simulation;
  app->host = host;
  app->original_host = host;
  app->identity = identity;
  app->ownership = ownership;
  if (local_seat_count != 0) {
    grow((void **)&app->local_seat_clients, &app->local_seat_capacity, local_seat_count, sizeof *app->local_seat_clients);
    app->local_seats = checked(malloc(app->local_seat_capacity * sizeof *app->local_seats));
    memcpy(app->local_seat_clients, local_seat_clients, local_seat_count * sizeof *local_seat_clients);
    memcpy(app->local_seats, local_seats, local_seat_count * sizeof *local_seats);
    app->local_seat_count = local_seat_count;
  }
  /* field initializers, TS order */
  app->view_settings = checked(qa_app_view_settings_create(view_settings_changed, app));
  app->debug_graph = checked(malloc(sizeof *app->debug_graph));
  qa_source_debug_graph_init(app->debug_graph);
  if (!qa_event_ring_init(&app->requested_commands, QA_APPLICATION_REQUEST_CAPACITY, sizeof(qa_application_command_request))
      || !qa_event_ring_open_cursor(&app->requested_commands, &app->requested_cursor, false))
    qa_range_error("Out of memory");
  app->world_operation = QA_APPLICATION_IDLE;
  app->transitions = checked(qa_shared_transition_coordinator_create(transition_apply, app));
  /* TS field initializer operatorOutput = { write: null } (openSource replaces it with the host wrapper's box). */
  app->operator_output = checked(calloc(1, sizeof *app->operator_output));
  app->pending_transition_id = -1;
  app->last_restart_frame = -1;
  app->local_snapshot_server_bit = 0;
  if (!qa_event_ring_init(&app->round_presentation_events, QA_APPLICATION_ROUND_EVENT_CAPACITY, sizeof(qa_app_sim_presentation_event))
      || !qa_event_ring_open_cursor(&app->round_presentation_events, &app->round_cursor, false))
    qa_range_error("Out of memory");
  qa_app_campaign_codec codec;
  memset(&codec, 0, sizeof codec);
  app->campaign_unit = checked(qa_app_campaign_unit_create(codec));
  app->native_world_count = 1;
  /* constructor body */
  app->input_config = input_config;
  if (ownership.borrowed_client == NULL) {
    app->keys = checked(qa_app_keys_create(keys_print, app));
    app->music_controls = checked(malloc(sizeof *app->music_controls));
    qa_music_controls_init(app->music_controls);
    app->owns_music_controls = true;
  }
  return app;
}

static void free_presentation_events(qa_app_sim_presentation_event *events, size_t count) {
  for (size_t i = 0; i < count; i++) qa_app_sim_presentation_event_release(&events[i]);
  free(events);
}
static void free_request_ring(qa_event_ring *ring, qa_event_cursor *cursor) {
  size_t count = 0;
  qa_application_command_request *pending = qa_event_ring_take(ring, cursor, &count);
  for (size_t i = 0; i < count; i++) qa_application_command_request_free(&pending[i]);
  free(pending);
  qa_event_ring_close_cursor(ring, cursor);
  qa_event_ring_destroy(ring);
}

/* TS GC of a closed Application: frees the storage the struct owns. */
void qa_application_destroy(qa_application *app) {
  if (app == NULL) return;
  /* openSource's simulation, content, session and configuration owners go first (they borrow the application's identity). */
  qa_application_open_free(app);
  qa_app_view_settings_destroy(app->view_settings);
  free(app->debug_graph);
  free_request_ring(&app->requested_commands, &app->requested_cursor);
  size_t round_count = 0;
  qa_app_sim_presentation_event *rounds = qa_event_ring_take(&app->round_presentation_events, &app->round_cursor, &round_count);
  free_presentation_events(rounds, round_count);
  qa_event_ring_close_cursor(&app->round_presentation_events, &app->round_cursor);
  qa_event_ring_destroy(&app->round_presentation_events);
  qa_shared_transition_coordinator_destroy(app->transitions);
  qa_app_campaign_unit_destroy(app->campaign_unit);
  if (app->owns_music_controls) free(app->music_controls);
  if (app->ownership.borrowed_client == NULL) qa_app_keys_destroy(app->keys);
  free(app->rankings ? app->rankings->client_slots : NULL);
  free(app->rankings ? app->rankings->clients : NULL);
  free(app->rankings);
  free(app->ranking_menu_requests);
  free(app->ranking_status_slots); free(app->ranking_statuses);
  free(app->client_cvar_seats); free(app->client_cvars);
  for (size_t i = 0; i < app->published_userinfo_count; i++) free(app->published_userinfo[i]);
  free(app->published_userinfo_cvars); free(app->published_userinfo);
  free(app->key_profile_simulations); free(app->key_profiles);
  free(app->client_inputs); free(app->deferred_input);
  free_presentation_events(app->source_events, app->source_event_count);
  qa_app_unhandled_effects_free(app->unhandled_effects, app->unhandled_effect_count);
  if (app->server_profile_store_value != NULL) { qa_config_store_destroy(app->server_profile_store_value); free(app->server_profile_store_value); }
  qa_config_store_destroy(&app->input_config);
  for (size_t i = 0; i < app->reported_effect_gap_count; i++) free(app->reported_effect_gaps[i]);
  free(app->reported_effect_gaps);
  free(app->movie_request_simulations); free(app->movie_requests);
  free(app->movies);
  free(app->pending_map);
  /* base_arena_progress rows are released, then freed, by qa_application_seats_free below. */
  free(app->team_arena_demo_simulations); free(app->team_arena_demos);
  free(app->pending_save_path); free(app->pending_save_source_product);
  free(app->source_connection_clients); free(app->source_connections);
  free(app->source_clients_added); free(app->source_clients_removed);
  free(app->source_seats_added); free(app->source_seats_removed);
  free(app->local_seat_clients); free(app->local_seats);
  for (size_t i = 0; i < app->network_player_identity_count; i++) free(app->network_player_identities[i].social_id);
  free(app->network_player_clients); free(app->network_player_identities);
  if (app->simulation_view.has_save) qa_app_shared_sim_save_free(&app->simulation_view.save);
  free(app->operator_output);
  free(app->save_directory_text);
  for (size_t i = 0; i < app->external_save_root_count; i++) free(app->external_save_roots[i]);
  free(app->external_save_roots);
  if (app->owns_identity && app->identity != NULL) { qa_identity_owner_destroy(app->identity); free(app->identity); }
  /* C storage of the P1/P7 members (close has released the registrations). */
  for (size_t i = 0; i < app->ranking_account_record_count; i++) ranking_account_free(app->ranking_account_records[i]);
  free(app->ranking_account_records);
  for (size_t i = 0; i < app->guest_output_record_count; i++) free(app->guest_output_records[i]);
  free(app->guest_output_records);
  if (app->dedicated_console != NULL) { qa_dedicated_console_destroy(app->dedicated_console); free(app->dedicated_console); }
  if (app->dedicated_commands != NULL && app->dedicated_commands != app->source_commands) qa_command_buffer_destroy(app->dedicated_commands);
  if (app->published_source_commands != NULL) app->published_source_commands->destroy(app->published_source_commands->ctx);
  if (app->source_commands != NULL) qa_command_buffer_destroy(app->source_commands);
  qa_application_services_free(app);
  qa_application_seats_free(app);
  free(app);
}

/* ---- storage helpers ---- */
qa_session_seat *qa_application_local_seat(qa_application *app, qa_client_id client) {
  for (size_t i = 0; i < app->local_seat_count; i++)
    if (qa_same_client(app->local_seat_clients[i], client)) return app->local_seats[i];
  return NULL;
}
qa_cvar_registry *qa_application_client_cvars_get(qa_application *app, qa_seat_id seat) {
  for (size_t i = 0; i < app->client_cvar_count; i++)
    if (qa_same_seat(app->client_cvar_seats[i], seat)) return app->client_cvars[i];
  return NULL;
}
/* TS get localPlayers: this.graphical?.input.locals.map(local => local.player) ?? []. */
size_t qa_application_local_player_count(qa_application *app) {
  return app->graphical == NULL ? 0 : qa_app_application_input_local_count(app->graphical->input);
}
qa_app_local_player qa_application_local_player(qa_application *app, size_t index) {
  return qa_app_application_input_local(app->graphical->input, index)->player;
}

/* ---- 408-725 members ---- */
/* TS takeRankingMenuRequest(seat): rankingMenuRequests.delete(seat). */
bool qa_application_take_ranking_menu_request(qa_application *app, qa_seat_id seat) {
  for (size_t i = 0; i < app->ranking_menu_request_count; i++) {
    if (!qa_same_seat(app->ranking_menu_requests[i], seat)) continue;
    memmove(&app->ranking_menu_requests[i], &app->ranking_menu_requests[i + 1],
            (app->ranking_menu_request_count - i - 1) * sizeof *app->ranking_menu_requests);
    app->ranking_menu_request_count--;
    return true;
  }
  return false;
}

/* TS overrideSeats(clients = this.clientCvars): one row per local seat. */
size_t qa_application_override_seats(qa_application *app, const qa_seat_id *seats, qa_cvar_registry *const *cvars,
    size_t count, qa_app_override_registry *out, size_t capacity) {
  if (seats == NULL) { seats = app->client_cvar_seats; cvars = app->client_cvars; count = app->client_cvar_count; }
  for (size_t i = 0; i < app->local_seat_count; i++) {
    qa_session_seat *seat = app->local_seats[i];
    qa_seat_id id = qa_session_seat_id(seat);
    qa_cvar_registry *found = NULL;
    for (size_t j = 0; j < count; j++) if (qa_same_seat(seats[j], id)) { found = cvars[j]; break; }
    if (found == NULL) qa_raise("Error", 0, 0, "Missing local override registry");
    if (i < capacity) {
      out[i].seat = (double)id.index;
      out[i].client = (double)qa_session_client_id(qa_session_seat_client(seat)).slot;
      out[i].cvars = found;
    }
  }
  return app->local_seat_count;
}

double qa_application_world_operation_count(const qa_application *app) { return app->world_operations; }

/* TS get serverProfileStore: lazily new ConfigStore(saveDirectory/../settings or userDataDirectory("settings")). */
qa_config_store *qa_application_server_profile_store(qa_application *app) {
  if (app->server_profile_store_value != NULL) return app->server_profile_store_value;
  char *root;
  if (app->host.save_directory != NULL) {
    size_t n = strlen(app->host.save_directory) + sizeof "/../settings";
    root = checked(malloc(n));
    (void)snprintf(root, n, "%s/../settings", app->host.save_directory);
    char *normalized = qa_application_path_normalize(root);
    free(root); root = normalized;
  } else root = checked(qa_user_data_directory("settings", app->launch_options.user_content_root));
  qa_config_store *store = checked(calloc(1, sizeof *store));
  if (!qa_config_store_init(store, root)) qa_range_error("Out of memory");
  free(root);
  app->server_profile_store_value = store;
  return store;
}

/* TS get campaignMovie: movies.get(this.simulation) ?? null. */
qa_application_movie *qa_application_campaign_movie(qa_application *app) {
  for (size_t i = 0; i < app->movie_count; i++)
    if (app->movies[i].simulation == app->world_simulation) return &app->movies[i];
  return NULL;
}
static void movie_requests_delete(qa_application *app, qa_app_shared_simulation *simulation) {
  for (size_t i = 0; i < app->movie_request_count; i++) {
    if (app->movie_request_simulations[i] != simulation) continue;
    app->movie_request_simulations[i] = app->movie_request_simulations[app->movie_request_count - 1];
    app->movie_requests[i] = app->movie_requests[app->movie_request_count - 1];
    app->movie_request_count--;
    return;
  }
}
/* TS set campaignMovie. */
void qa_application_set_campaign_movie(qa_application *app, const qa_application_movie *movie) {
  qa_application_movie *current = qa_application_campaign_movie(app);
  if (movie == NULL) {
    if (current != NULL) {
      size_t index = (size_t)(current - app->movies);
      memmove(&app->movies[index], &app->movies[index + 1], (app->movie_count - index - 1) * sizeof *app->movies);
      app->movie_count--;
    }
    movie_requests_delete(app, app->world_simulation);
    return;
  }
  if (current != NULL) { *current = *movie; current->simulation = app->world_simulation; return; }
  grow((void **)&app->movies, &app->movie_capacity, app->movie_count + 1, sizeof *app->movies);
  app->movies[app->movie_count] = *movie;
  app->movies[app->movie_count].simulation = app->world_simulation;
  app->movie_count++;
}

/* TS get saveDirectory. Owned by the application until destroy. */
const char *qa_application_save_directory(qa_application *app) {
  if (app->host.save_directory != NULL) return app->host.save_directory;
  free(app->save_directory_text);
  app->save_directory_text = checked(qa_user_data_directory("saves", app->launch_options.user_content_root));
  return app->save_directory_text;
}

/* TS get externalSaveDirectories: host value ?? nightdiveAddonDirectories().map(root => root.root). */
const char *const *qa_application_external_save_directories(qa_application *app, size_t *out_count) {
  if (app->host.has_external_save_directories) {
    *out_count = app->host.external_save_directory_count;
    return app->host.external_save_directories;
  }
  qa_nightdive_addon_root *found = NULL;
  size_t found_count = 0;
  if (!qa_nightdive_addon_directories(getenv("HOME") != NULL ? getenv("HOME") : "", &found, &found_count)) qa_range_error("Out of memory");
  for (size_t i = 0; i < app->external_save_root_count; i++) free(app->external_save_roots[i]);
  free(app->external_save_roots);
  app->external_save_roots = found_count == 0 ? NULL : checked(calloc(found_count, sizeof *app->external_save_roots));
  for (size_t i = 0; i < found_count; i++) app->external_save_roots[i] = dup_text(found[i].root);
  app->external_save_root_count = found_count;
  qa_nightdive_addon_roots_free(found, found_count);
  *out_count = app->external_save_root_count;
  return (const char *const *)app->external_save_roots;
}

/* TS levelRecoveryAvailable(simulation, options). */
bool qa_application_level_recovery_available(qa_application *app, qa_app_shared_simulation *simulation, const qa_app_options *options) {
  (void)app;
  return !options->dedicated && strcmp(options->network_kind, "offline") == 0
      && strcmp(simulation->options.mode, "singleplayer") == 0
      && (qa_app_shared_sim_q1_source(simulation) != NULL || qa_app_shared_sim_q2_source(simulation) != NULL
          || qa_app_shared_sim_quakec_source(simulation) != NULL);
}

/* TS enableStartupPersistence. */
void qa_application_enable_startup_persistence(qa_application *app) {
  if (app->launch_options.dedicated || (app->prepared_startup != NULL && qa_app_prepared_startup_pending(app->prepared_startup))) return;
  app->archive_persistence = true;
  if (app->graphical != NULL) qa_app_application_input_enable_archive_persistence(app->graphical->input);
  if (app->image_settings != NULL) qa_app_image_settings_enable_persistence(app->image_settings);
}

/* TS sourceCvars(simulation = this.simulation). */
qa_cvar_registry *qa_application_source_cvars(qa_application *app, qa_app_shared_simulation *simulation) {
  if (simulation == NULL) simulation = app->world_simulation;
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  if (guest != NULL) return qa_app_q3_server_state_cvars(qa_app_q3_qvm_server_game_state(guest));
  qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
  if (q3 != NULL) return qa_app_q3_source_runtime_host(q3)->cvars;
  qa_cvar_registry *q2 = qa_app_shared_sim_q2_server_cvars(simulation);
  if (q2 != NULL) return q2;
  const qa_ss_source_runtime *q1 = qa_app_shared_sim_q1_source(simulation);
  if (q1 != NULL) return q1->q1_cvars;
  qa_app_sim_quakec_source *quakec = qa_app_shared_sim_quakec_source(simulation);
  if (quakec != NULL) return qa_app_sim_quakec_source_cvars(quakec);
  return NULL;
}

/* ---- 1235-1300 accessors ---- */
size_t qa_application_bot_clients(qa_application *app, const qa_app_sim_bot_client **out) {
  size_t count = 0;
  *out = app->bots == NULL ? NULL : app->bots->clients(app->bots->ctx, &count);
  return count;
}
double qa_application_frame_count(const qa_application *app) { return app->frames; }
bool qa_application_world_failed(const qa_application *app) { return app->fatal_world_failure; }
bool qa_application_finished(const qa_application *app) {
  return app->stopping || app->closed || (app->launch_options.has_frame_limit && app->frames >= app->launch_options.frame_limit);
}
/* TS get videoRestart: owned applications hold their own (borrowed ones read the client's). */
qa_app_video_restart *qa_application_video_restart(qa_application *app) { return app->owned_video_restart; }
void qa_application_pump_client_input(qa_application *app) {
  if (app->graphical != NULL) qa_app_application_input_pump(app->graphical->input, false);
}
const qa_app_options *qa_application_options(const qa_application *app) { return &app->launch_options; }
qa_app_loaded_content *qa_application_content(const qa_application *app) { return app->loaded_content; }
qa_app_shared_simulation *qa_application_simulation(const qa_application *app) { return app->world_simulation; }
qa_engine_session *qa_application_session(const qa_application *app) { return app->session; }
double qa_application_time_milliseconds(const qa_application *app) { return app->elapsed; }
const qa_app_sim_presentation_event *qa_application_presentation_events(const qa_application *app, size_t *out_count) {
  *out_count = app->source_event_count;
  return app->source_events;
}
const qa_app_unhandled_effect *qa_application_unhandled_presentation_effects(const qa_application *app, size_t *out_count) {
  *out_count = app->unhandled_effect_count;
  return app->unhandled_effects;
}
bool qa_application_network_address(const qa_application *app, qa_net_address *out) {
  if (app->network == NULL) return false;
  *out = app->network->address;
  return true;
}
/* TS get networkWire(): this.network?.server.wire ?? null. Strings are borrowed from the server. */
bool qa_application_network_wire(const qa_application *app, qa_wire_selection *out) {
  const qa_application_native_server *network = app->network;
  if (network == NULL) return false;
  qa_app_application_network server;
  switch (network->kind) {
  case QA_APPLICATION_NETWORK_Q1: server = qa_app_q1_server_network_as_network(network->q1); break;
  case QA_APPLICATION_NETWORK_QW: server = qa_app_qw_server_network_as_network(network->qw); break;
  case QA_APPLICATION_NETWORK_Q2: server = qa_app_q2_server_network_as_network(network->q2); break;
  case QA_APPLICATION_NETWORK_Q3: server = qa_app_q3_server_network_as_network(network->q3); break;
  case QA_APPLICATION_NETWORK_UNIFIED: {
    /* { kind: "unified", version: 1, composition: digest, snapshotSchema: composition.snapshotSchema } */
    const char *digest = NULL;
    const qa_save_value *schema = NULL;
    qa_app_unified_server_network_wire(network->unified, &digest, &schema);
    memset(out, 0, sizeof *out);
    out->is_unified = true;
    out->composition = digest;
    out->snapshot_schema = schema != NULL && schema->kind == QA_SAVE_STRING ? schema->text : NULL;
    return true;
  }
  default: return false;
  }
  *out = server.wire(server.ctx);
  return true;
}
/* TS returnToLobby. */
void qa_application_return_to_lobby(qa_application *app) {
  if (app->host.lobby == NULL) qa_raise("Error", 0, 0, "This session has no retained lobby");
  app->host.lobby->return_to_lobby(app->host.lobby->ctx);
}
/* TS input(event). */
bool qa_application_input(qa_application *app, const qa_seat_input_event *event) {
  if (app->closed) qa_raise("Error", 0, 0, "Application is closed");
  if (app->world_operation != QA_APPLICATION_IDLE) {
    if (app->graphical == NULL) return false;
    grow((void **)&app->deferred_input, &app->deferred_input_capacity, app->deferred_input_count + 1, sizeof *app->deferred_input);
    app->deferred_input[app->deferred_input_count++] = *event;
    return true;
  }
  size_t players = qa_application_local_player_count(app);
  for (size_t i = 0; i < players; i++) {
    qa_app_local_player player = qa_application_local_player(app, i);
    if (!qa_same_seat(qa_session_seat_id(player.seat), event->seat)) continue;
    if (!qa_session_actor_registry_is_live(qa_app_shared_sim_actors(app->world_simulation), player.actor)) return false;
    break;
  }
  return app->graphical != NULL && qa_app_application_input_input(app->graphical->input, event);
}
/* TS resumeInput. */
void qa_application_resume_input(qa_application *app) {
  qa_seat_input_event *events = app->deferred_input;
  size_t count = app->deferred_input_count;
  app->deferred_input = NULL; app->deferred_input_count = 0; app->deferred_input_capacity = 0;
  if (!app->closed) for (size_t i = 0; i < count; i++)
    if (app->graphical != NULL) (void)qa_app_application_input_input(app->graphical->input, &events[i]);
  free(events);
}

/* ---- 2784-2789 (requestQuit / queueCommand, public API) ---- */
void qa_application_request_quit(qa_application *app) { app->stopping = true; }
void qa_application_queue_command(qa_application *app, const char *name,
    const char *const *arguments, size_t argument_count, const qa_seat_id *seat) {
  qa_application_queue_command_source(app, name, arguments, argument_count, seat, NULL);
}
void qa_application_queue_command_source(qa_application *app, const char *name, const char *const *arguments,
    size_t argument_count, const qa_seat_id *seat, const qa_command_context *source) {
  qa_application_command_request request;
  memset(&request, 0, sizeof request);
  request.target = QA_APPLICATION_TARGET_APPLICATION;
  request.name = dup_text(name);
  request.arguments = argument_count == 0 ? NULL : checked(calloc(argument_count, sizeof *request.arguments));
  for (size_t i = 0; i < argument_count; i++) request.arguments[i] = dup_text(arguments[i]);
  request.argument_count = argument_count;
  if (seat != NULL) { request.has_seat = true; request.seat = *seat; }
  /* The request outlives the invocation (TS keeps its context alive): own a copy. */
  if (source != NULL) { request.has_source = true; request.source = qa_command_context_copy(source); }
  (void)qa_event_ring_admit(&app->requested_commands, &request);
}


/* ==== 409-886: rankings, saves, effects, configuration stores, statics ==== */
static bool same_text(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static qa_cvar_registry *q3_source_cvars(qa_app_shared_simulation *simulation) {
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  return source == NULL ? NULL : qa_app_q3_source_runtime_host(source)->cvars;
}
static qa_q3_game_level *q3_level(qa_app_q3_source_runtime *source) {
  return qa_app_q3_source_subsystems_owner_level(qa_app_q3_source_runtime_owner(source));
}
static void ranking_status_delete(qa_application *app, int64_t slot) {
  for (size_t i = 0; i < app->ranking_status_count; i++) {
    if (app->ranking_status_slots[i] != slot) continue;
    memmove(&app->ranking_status_slots[i], &app->ranking_status_slots[i + 1], (app->ranking_status_count - i - 1) * sizeof *app->ranking_status_slots);
    memmove(&app->ranking_statuses[i], &app->ranking_statuses[i + 1], (app->ranking_status_count - i - 1) * sizeof *app->ranking_statuses);
    app->ranking_status_count--;
    return;
  }
}
static void ranking_status_set(qa_application *app, int64_t slot, qa_ranking_player_state state) {
  for (size_t i = 0; i < app->ranking_status_count; i++)
    if (app->ranking_status_slots[i] == slot) { app->ranking_statuses[i] = state; return; }
  size_t capacity = app->ranking_status_capacity;
  grow((void **)&app->ranking_status_slots, &capacity, app->ranking_status_count + 1, sizeof *app->ranking_status_slots);
  grow((void **)&app->ranking_statuses, &app->ranking_status_capacity, app->ranking_status_count + 1, sizeof *app->ranking_statuses);
  app->ranking_status_slots[app->ranking_status_count] = slot;
  app->ranking_statuses[app->ranking_status_count] = state;
  app->ranking_status_count++;
}
static void ranking_menu_add(qa_application *app, qa_seat_id seat) {
  for (size_t i = 0; i < app->ranking_menu_request_count; i++) if (qa_same_seat(app->ranking_menu_requests[i], seat)) return;
  grow((void **)&app->ranking_menu_requests, &app->ranking_menu_request_capacity, app->ranking_menu_request_count + 1, sizeof *app->ranking_menu_requests);
  app->ranking_menu_requests[app->ranking_menu_request_count++] = seat;
}
static void ranking_client_delete(qa_application_rankings *state, int64_t slot) {
  for (size_t i = 0; i < state->client_count; i++) {
    if (state->client_slots[i] != slot) continue;
    memmove(&state->client_slots[i], &state->client_slots[i + 1], (state->client_count - i - 1) * sizeof *state->client_slots);
    memmove(&state->clients[i], &state->clients[i + 1], (state->client_count - i - 1) * sizeof *state->clients);
    state->client_count--;
    return;
  }
}
static void ranking_client_set(qa_application_rankings *state, int64_t slot, qa_client_id client) {
  for (size_t i = 0; i < state->client_count; i++) if (state->client_slots[i] == slot) { state->clients[i] = client; return; }
  size_t capacity = state->client_capacity;
  grow((void **)&state->client_slots, &capacity, state->client_count + 1, sizeof *state->client_slots);
  grow((void **)&state->clients, &state->client_capacity, state->client_count + 1, sizeof *state->clients);
  state->client_slots[state->client_count] = slot;
  state->clients[state->client_count] = client;
  state->client_count++;
}
static bool ranking_active(qa_application_rankings *state) {
  return same_text(qa_ranking_lifecycle_get_state(qa_app_q3_rankings_lifecycle(state->owner)).kind, "active");
}

/* TS rankingAccountActions(seat): the owner's actions with player() reading the live status first. */
/* C ownership: TS keeps the closed owner's lifecycle alive for a retained
 * record, so service()/player() keep answering from the ended lifecycle and the
 * account operations throw "Ranking match owner is closed" (q3-rankings.ts
 * assertOpen). The C owner frees its lifecycle on close: closeRankings records
 * the ended answers here first (ranking_accounts_close) and the record never
 * calls the freed owner again. */
typedef struct ranking_account_ctx {
  qa_application *app;
  qa_application_rankings *state;
  int64_t slot;
  qa_ui_ranking_account_actions actions; /* the owner's */
  qa_ui_ranking_account_actions view;    /* the returned record */
  bool closed;
  qa_ranking_service_state closed_service; char *closed_service_reason;
  qa_ranking_player_state closed_player; char *closed_player_reason;
} ranking_account_ctx;
static void ranking_account_free(void *record) {
  ranking_account_ctx *account = record;
  free(account->closed_service_reason);
  free(account->closed_player_reason);
  free(account);
}
/* Called with the ended lifecycle, before the owner frees it. */
static void ranking_accounts_close(qa_application *app, qa_app_q3_rankings *owner, const qa_ranking_lifecycle *lifecycle) {
  for (size_t i = 0; i < app->ranking_account_record_count; i++) {
    ranking_account_ctx *account = app->ranking_account_records[i];
    if (account->closed || account->state->owner != owner) continue;
    account->closed = true;
    account->closed_service = qa_ranking_lifecycle_get_state(lifecycle);
    account->closed_service_reason = dup_text(account->closed_service.reason);
    account->closed_service.reason = account->closed_service_reason;
    account->closed_player = qa_ranking_lifecycle_player(lifecycle, (double)account->slot);
    account->closed_player_reason = dup_text(account->closed_player.reason);
    account->closed_player.reason = account->closed_player_reason;
  }
}
static void ranking_account_assert_open(ranking_account_ctx *account) {
  if (account->closed) qa_raise("Error", 0, 0, "Ranking match owner is closed");
}
static qa_ranking_service_state account_service(void *ctx) {
  ranking_account_ctx *account = ctx;
  if (account->closed) return account->closed_service;
  return account->actions.service(account->actions.ctx);
}
static qa_ranking_player_state account_player(void *ctx) {
  ranking_account_ctx *account = ctx;
  /* A closed record's state is never this.rankings again (TS identity). */
  if (account->closed) return account->closed_player;
  if (account->app->rankings == account->state)
    for (size_t i = 0; i < account->app->ranking_status_count; i++)
      if (account->app->ranking_status_slots[i] == account->slot) return account->app->ranking_statuses[i];
  return account->actions.player(account->actions.ctx);
}
static const char *account_submit(void *ctx, qa_ranking_account_request request) {
  ranking_account_ctx *account = ctx;
  ranking_account_assert_open(account);
  return account->actions.submit(account->actions.ctx, request);
}
static const char *account_reset(void *ctx) {
  ranking_account_ctx *account = ctx;
  ranking_account_assert_open(account);
  return account->actions.reset(account->actions.ctx);
}
static const char *account_spectate(void *ctx) {
  ranking_account_ctx *account = ctx;
  ranking_account_assert_open(account);
  return account->actions.spectate(account->actions.ctx);
}
const qa_ui_ranking_account_actions *qa_application_ranking_account_actions(qa_application *app, qa_seat_id seat) {
  qa_application_rankings *state = app->rankings;
  size_t players = qa_application_local_player_count(app);
  bool found = false;
  qa_app_local_player local;
  for (size_t i = 0; i < players && !found; i++) {
    local = qa_application_local_player(app, i);
    found = qa_same_seat(qa_session_seat_id(local.seat), seat);
  }
  if (state == NULL || state->simulation != app->world_simulation || !found) return NULL;
  qa_client_id client;
  if (!qa_app_shared_sim_player_client(app->world_simulation, local.actor, &client)) return NULL;
  ranking_account_ctx *account = checked(calloc(1, sizeof *account));
  account->app = app;
  account->state = state;
  account->slot = client.slot;
  account->actions = qa_app_q3_rankings_account_actions(state->owner, (double)client.slot);
  /* C ownership: the returned actions live until the application is destroyed. */
  grow((void **)&app->ranking_account_records, &app->ranking_account_record_capacity, app->ranking_account_record_count + 1,
      sizeof *app->ranking_account_records);
  app->ranking_account_records[app->ranking_account_record_count++] = account;
  qa_ui_ranking_account_actions *actions = &account->view;
  actions->ctx = account;
  actions->service = account_service;
  actions->player = account_player;
  actions->submit = account_submit;
  actions->reset = account_reset;
  actions->spectate = account_spectate;
  return actions;
}

/* TS publishRankingState(simulation). */
void qa_application_publish_ranking_state(qa_application *app, qa_app_shared_simulation *simulation) {
  qa_cvar_registry *cvars = q3_source_cvars(simulation);
  if (cvars == NULL) return;
  qa_application_rankings *state = app->rankings;
  const char *value = state != NULL && state->simulation == simulation && qa_cvar_registry_variable_value(cvars, "sv_enableRankings") != 0
      && ranking_active(state) ? "1" : "0";
  char *current = qa_application_cvar_string(cvars, "sv_rankingsActive");
  if (strcmp(current, value) != 0) qa_application_cvar_set(cvars, "sv_rankingsActive", value, true);
  free(current);
}

/* TS disconnectRankings(simulation, client). The task is synchronous: it has settled. */
void qa_application_disconnect_rankings(qa_application *app, qa_app_shared_simulation *simulation, qa_client_id client) {
  qa_application_rankings *state = app->rankings;
  if (state == NULL || state->simulation != simulation) return;
  qa_app_q3_rankings_disconnect(state->owner, (double)client.slot);
  ranking_client_delete(state, client.slot);
  ranking_status_delete(app, client.slot);
  for (size_t i = 0; i < app->local_seat_count; i++)
    if (qa_same_client(qa_session_client_id(qa_session_seat_client(app->local_seats[i])), client))
      (void)qa_application_take_ranking_menu_request(app, qa_session_seat_id(app->local_seats[i]));
}

static void free_rankings_state(qa_application_rankings *state) {
  free(state->client_slots);
  free(state->clients);
  free(state->host_ctx);
  free(state);
}
/* TS closeRankings(simulation?): simulation NULL is TS undefined. */
typedef struct close_rankings_try { qa_application_rankings *state; } close_rankings_try;
static void close_rankings_body(void *ctx) {
  close_rankings_try *attempt = ctx;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(attempt->state->simulation);
  if ((source == NULL ? 0 : q3_level(source)->base.intermission_time) != 0) qa_app_q3_rankings_game_over(attempt->state->owner);
}
void qa_application_close_rankings(qa_application *app, qa_app_shared_simulation *simulation) {
  qa_application_rankings *state = app->rankings;
  if (state == NULL || (simulation != NULL && state->simulation != simulation)) return;
  app->rankings = NULL;
  qa_application_publish_ranking_state(app, state->simulation);
  close_rankings_try attempt = { state };
  qa_caught_error caught;
  bool ok = qa_try(close_rankings_body, &attempt, &caught);
  app->ranking_menu_request_count = 0;
  app->ranking_status_count = 0;
  qa_app_q3_rankings_close(state->owner);
  free_rankings_state(state);
  if (!ok) qa_rethrow(&caught);
}

/* TS queueRankingFrame(). TS runs the frames as a task; here they run now,
 * and a frame queued while one runs sets pending. */
typedef struct ranking_frames { qa_application *app; qa_application_rankings *state; } ranking_frames;
static void ranking_frames_run(void *ctx) {
  ranking_frames *frames = ctx;
  qa_application_rankings *state = frames->state;
  do { state->pending = false; qa_application_ranking_frame(frames->app); }
  while (state->pending && frames->app->rankings == state && !frames->app->closed && ranking_active(state));
}
void qa_application_queue_ranking_frame(qa_application *app) {
  qa_application_rankings *state = app->rankings;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(app->world_simulation);
  if (state == NULL || source == NULL) return;
  if (state->task) { state->pending = true; return; }
  const char *kind = qa_ranking_lifecycle_get_state(qa_app_q3_rankings_lifecycle(state->owner)).kind;
  if ((same_text(kind, "disabled") || same_text(kind, "unavailable"))
      && state->enabled == (qa_cvar_registry_variable_value(qa_app_q3_source_runtime_host(source)->cvars, "sv_enableRankings") != 0)) return;
  state->task = true;
  ranking_frames frames = { app, state };
  qa_caught_error caught;
  if (!qa_try(ranking_frames_run, &frames, &caught)) qa_application_printf(app, "Ranking service failed: %s\n", caught.message);
  /* finally: the state may have been closed (and freed) by the frame; only a live state's task clears. */
  if (app->rankings == state) state->task = false;
}

/* The Q3RankingHost the application gives ApplicationQ3Rankings. */
typedef struct ranking_host {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_q3_source_runtime *source;
  qa_app_q3_rankings *owner;
  char *cvar_text;
} ranking_host;
static bool host_current(ranking_host *host) {
  return host->app->rankings != NULL && host->app->rankings->owner == host->owner && host->app->world_simulation == host->simulation;
}
static qa_q3_game_entity *ranking_entity(ranking_host *host, double slot) {
  qa_q3_game_entity *value = qa_q3_entity_pool_get(qa_app_q3_source_runtime_pool(host->source), slot);
  if (value == NULL || value->client == NULL || !qa_q3_game_entity_inuse(value)) qa_raise("Error", 0, 0, "Ranking client has left its source world");
  return value;
}
static void rh_status(void *ctx, double slot, qa_ranking_player_state status) {
  ranking_host *host = ctx;
  if (host->app->rankings != NULL && host->app->rankings->owner == host->owner) ranking_status_set(host->app, (int64_t)slot, status);
}
static void rh_service_status(void *ctx, qa_ranking_service_state state) {
  ranking_host *host = ctx;
  (void)state; /* TS serviceStatus: () => publishRankingState(simulation) */
  if (host_current(host)) qa_application_publish_ranking_state(host->app, host->simulation);
}
static void rh_menu(void *ctx, double slot) {
  ranking_host *host = ctx;
  if (!host_current(host)) return;
  for (size_t i = 0; i < host->app->local_seat_count; i++)
    if ((double)qa_session_client_id(qa_session_seat_client(host->app->local_seats[i])).slot == slot)
      ranking_menu_add(host->app, qa_session_seat_id(host->app->local_seats[i]));
}
static void rh_spectator(void *ctx, double slot) {
  ranking_host *host = ctx;
  if (!host_current(host)) return;
  qa_q3_game_entity *value = ranking_entity(host, slot);
  qa_q3_game_client *client = value->client;
  if (client == NULL) qa_raise("Error", 0, 0, "Ranking client has no source state");
  client->sess.session_team = QA_Q3_BASE_TEAM_SPECTATOR;
  client->sess.spectator_state = QA_Q3_SPEC_FREE;
  qa_q3_ta_client_spawn_runtime_client_spawn(qa_app_q3_source_subsystems_owner_spawns(qa_app_q3_source_runtime_owner(host->source)), value);
}
static void rh_activate(void *ctx, double slot) {
  ranking_host *host = ctx;
  if (!host_current(host)) return;
  qa_q3_ta_game_command_runtime_set_team(qa_app_q3_source_subsystems_owner_commands(qa_app_q3_source_runtime_owner(host->source)),
      ranking_entity(host, slot), "free");
}
static void rh_scoreboard(void *ctx, double slot) {
  ranking_host *host = ctx;
  if (!host_current(host)) return;
  qa_q3_ta_game_command_runtime_scoreboard(qa_app_q3_source_subsystems_owner_commands(qa_app_q3_source_runtime_owner(host->source)),
      ranking_entity(host, slot));
}
static void rh_drop_bot(void *ctx, double slot) {
  ranking_host *host = ctx;
  if (!host_current(host)) return;
  qa_app_q3_source_engine *engine = &qa_app_q3_source_runtime_host(host->source)->engine;
  engine->drop_client(engine->ctx, slot, "Bots cannot participate in ranked games");
}
static double rh_game_type(void *ctx) {
  ranking_host *host = ctx;
  return qa_cvar_registry_variable_value(qa_app_q3_source_runtime_host(host->source)->cvars, "g_gametype");
}
static const char *rh_cvar(void *ctx, const char *name) {
  ranking_host *host = ctx;
  free(host->cvar_text);
  host->cvar_text = qa_application_cvar_string(qa_app_q3_source_runtime_host(host->source)->cvars, name);
  return host->cvar_text;
}
static void rh_set_cvar(void *ctx, const char *name, const char *value) {
  ranking_host *host = ctx;
  qa_application_cvar_set(qa_app_q3_source_runtime_host(host->source)->cvars, name, value, true);
}
/* TS pool.get(slot) reads ApplicationQ3Rankings makes (the C port reads them through its host). */
static qa_q3_game_entity *pool_entity(ranking_host *host, double slot) {
  return qa_q3_entity_pool_get(qa_app_q3_source_runtime_pool(host->source), slot);
}
static bool rh_entity_inuse(void *ctx, double slot) {
  qa_q3_game_entity *entity = pool_entity(ctx, slot);
  return entity != NULL && qa_q3_game_entity_inuse(entity);
}
static double rh_entity_sv_flags(void *ctx, double slot) { return pool_entity(ctx, slot)->r.sv_flags; }
static double rh_entity_weapon(void *ctx, double slot) { return pool_entity(ctx, slot)->s.weapon; }
static double rh_entity_session_team(void *ctx, double slot) { return pool_entity(ctx, slot)->client->sess.session_team; }
static qa_ranking_lifecycle *rh_create_lifecycle(void *ctx, const qa_ranking_provider *provider,
    qa_ranking_changed_fn changed, void *changed_ctx, qa_ranking_service_changed_fn service_changed, void *service_ctx) {
  (void)ctx;
  return checked(qa_ranking_lifecycle_create(provider, changed, changed_ctx, service_changed, service_ctx));
}
static void rh_destroy_lifecycle(void *ctx, qa_ranking_lifecycle *lifecycle) {
  ranking_host *host = ctx;
  ranking_accounts_close(host->app, host->owner, lifecycle);
  free(host->cvar_text);
  host->cvar_text = NULL;
  qa_ranking_lifecycle_free(lifecycle);
}
static void report_unavailable(qa_application *app, qa_application_rankings *state) {
  qa_ranking_service_state status = qa_ranking_lifecycle_get_state(qa_app_q3_rankings_lifecycle(state->owner));
  if (same_text(status.kind, "unavailable")) qa_application_printf(app, "Rankings unavailable: %s\n", status.reason);
}

/* TS rankingFrame(). */
void qa_application_ranking_frame(qa_application *app) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(simulation);
  if (source == NULL) return;
  qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(source)->cvars;
  bool enabled = qa_cvar_registry_variable_value(cvars, "sv_enableRankings") != 0;
  qa_application_rankings *state = app->rankings;
  if (state != NULL && state->simulation != app->world_simulation) qa_raise("Error", 0, 0, "Ranking owner outlived its Q3 world");
  const char *game_key = app->host.rankings == NULL || app->host.rankings->game_key == NULL ? "" : app->host.rankings->game_key;
  if (state == NULL) {
    ranking_host *host = checked(calloc(1, sizeof *host));
    host->app = app; host->simulation = simulation; host->source = source;
    qa_app_q3_ranking_host callbacks;
    memset(&callbacks, 0, sizeof callbacks);
    callbacks.ctx = host;
    callbacks.status = rh_status;
    callbacks.service_status = rh_service_status;
    callbacks.menu = rh_menu;
    callbacks.spectator = rh_spectator;
    callbacks.activate = rh_activate;
    callbacks.scoreboard = rh_scoreboard;
    callbacks.drop_bot = rh_drop_bot;
    callbacks.game_type = rh_game_type;
    callbacks.cvar = rh_cvar;
    callbacks.set_cvar = rh_set_cvar;
    callbacks.entity_inuse = rh_entity_inuse;
    callbacks.entity_sv_flags = rh_entity_sv_flags;
    callbacks.entity_weapon = rh_entity_weapon;
    callbacks.entity_session_team = rh_entity_session_team;
    callbacks.create_lifecycle = rh_create_lifecycle;
    callbacks.destroy_lifecycle = rh_destroy_lifecycle;
    qa_app_q3_rankings *owner = qa_app_q3_rankings_create(qa_app_q3_source_runtime_pool(source), q3_level(source),
        app->host.rankings == NULL ? NULL : app->host.rankings->provider, callbacks);
    host->owner = owner;
    state = checked(calloc(1, sizeof *state));
    state->simulation = simulation; state->owner = owner; state->enabled = enabled; state->host_ctx = host;
    app->rankings = state;
    qa_app_q3_rankings_begin(owner, enabled, qa_cvar_registry_variable_value(cvars, "g_gametype") == QA_Q3_BASE_GT_SINGLE_PLAYER, game_key);
    report_unavailable(app, state);
  } else if (state->enabled != enabled) {
    if (!enabled) {
      qa_ranking_lifecycle_end(qa_app_q3_rankings_lifecycle(state->owner));
      qa_application_publish_ranking_state(app, state->simulation);
    }
    state->enabled = enabled;
    qa_app_q3_rankings_begin(state->owner, enabled, qa_cvar_registry_variable_value(cvars, "g_gametype") == QA_Q3_BASE_GT_SINGLE_PLAYER, game_key);
    report_unavailable(app, state);
  }
  size_t count = 0;
  const qa_client_id *clients = qa_app_shared_sim_client_identities(state->simulation, &count);
  size_t known = state->client_count;
  qa_client_id *previous = known == 0 ? NULL : checked(malloc(known * sizeof *previous));
  if (known != 0) memcpy(previous, state->clients, known * sizeof *previous);
  for (size_t i = 0; i < known; i++) {
    bool present = false;
    for (size_t j = 0; j < count; j++) if (qa_same_client(clients[j], previous[i])) { present = true; break; }
    if (present) continue;
    qa_app_q3_rankings_disconnect(state->owner, (double)previous[i].slot);
    ranking_client_delete(state, previous[i].slot);
    ranking_status_delete(app, previous[i].slot);
  }
  free(previous);
  clients = qa_app_shared_sim_client_identities(state->simulation, &count);
  for (size_t i = 0; i < count; i++) ranking_client_set(state, clients[i].slot, clients[i]);
  qa_app_q3_rankings_frame(state->owner);
  if (q3_level(source)->base.intermission_time != 0) qa_app_q3_rankings_game_over(state->owner);
}

/* TS saveUnavailable(purpose). */
const char *qa_application_save_unavailable(qa_application *app, qa_save_purpose purpose) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  size_t count = 0;
  qa_actor_id *players;
  if (app->launch_options.dedicated) {
    const qa_actor_id *actors = qa_app_shared_sim_players(simulation, &count);
    players = count == 0 ? NULL : checked(malloc(count * sizeof *players));
    if (count != 0) memcpy(players, actors, count * sizeof *players);
  } else {
    count = qa_application_local_player_count(app);
    players = count == 0 ? NULL : checked(malloc(count * sizeof *players));
    for (size_t i = 0; i < count; i++) players[i] = qa_application_local_player(app, i).actor;
  }
  qa_app_sim_classic_guest_world *classic = NULL;
  qa_app_sim_rerelease_guest_world *rerelease = NULL;
  bool native = qa_app_shared_sim_q2_native(simulation, &classic, &rerelease);
  /* TS native.services.options.cvars: the native world's registry (q2ServerCvars). */
  if (native && qa_cvar_registry_variable_value(qa_app_shared_sim_q2_server_cvars(simulation), "deathmatch") != 0) {
    free(players);
    return "Native Quake II deathmatch games cannot be saved";
  }
  qa_app_q3_source_runtime *q3 = qa_app_shared_sim_q3_source(simulation);
  qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(simulation);
  qa_save_eligibility state;
  memset(&state, 0, sizeof state);
  state.authority = app->network == NULL && same_text(app->launch_options.network_kind, "offline") ? QA_SAVE_AUTHORITY_OFFLINE : QA_SAVE_AUTHORITY_SERVER;
  state.family = q3 != NULL || guest != NULL ? QA_GAME_FAMILY_Q3
      : qa_app_shared_sim_q2_source(simulation) != NULL || native ? QA_GAME_FAMILY_Q2 : QA_GAME_FAMILY_Q1;
  state.mode = same_text(app->launch_options.mode, "coop") ? QA_SAVE_MODE_COOP
      : same_text(app->launch_options.mode, "deathmatch") ? QA_SAVE_MODE_DEATHMATCH : QA_SAVE_MODE_SINGLEPLAYER;
  state.match_save = app->launch_options.dedicated || same_text(qa_app_loaded_content_recipe(app->loaded_content)->match.provider, "q2:lmctf");
  qa_application_movie *movie = qa_application_campaign_movie(app);
  state.active = !app->closed && (movie == NULL || movie->component_owned);
  bool intermission = false;
  for (size_t i = 0; i < count && !intermission; i++) {
    qa_app_sim_movement_player *player = qa_app_shared_sim_movement_player(simulation, players[i]);
    intermission = player != NULL && player->intermission;
  }
  if (!intermission) intermission = (q3 == NULL ? 0 : q3_level(q3)->base.intermission_time) != 0;
  if (!intermission && guest != NULL)
    intermission = same_text(qa_app_q3_server_state_configstring_get(qa_app_q3_qvm_server_game_state(guest), 22), "1");
  state.intermission = intermission;
  double *health = count == 0 ? NULL : checked(malloc(count * sizeof *health));
  for (size_t i = 0; i < count; i++) {
    if (!native) {
      qa_combat_state_result read = qa_gameplay_authority_read(simulation->combat, players[i]);
      health[i] = read.has_value ? read.value.health : 0;
    } else health[i] = qa_app_shared_sim_player_ui(simulation, players[i]).health;
  }
  state.player_health = health;
  state.player_health_count = count;
  const char *reason = qa_save_unavailable(&state, purpose);
  free(health);
  free(players);
  return reason;
}

/* TS createWorldEffects(assets, simulation, seed): players are the simulation's players. */
static bool world_effects_is_player(void *ctx, qa_actor_id actor) {
  size_t count = 0;
  const qa_actor_id *players = qa_app_shared_sim_players(ctx, &count);
  for (size_t i = 0; i < count; i++) if (qa_same_actor(players[i], actor)) return true;
  return false;
}
qa_app_application_effects *qa_application_create_world_effects(qa_application *app, qa_app_application_assets *assets,
    qa_app_shared_simulation *simulation, double seed) {
  (void)app; /* TS member reads only its arguments */
  return checked(qa_app_application_effects_create(assets, qa_shared_as_scene_queries(simulation->scene),
      world_effects_is_player, simulation, seed));
}
/* TS preloadWorldEffects(effects). */
void qa_application_preload_world_effects(qa_application *app, qa_app_application_effects *effects) {
  size_t count = 0;
  qa_app_effect_preload_failure *failures = qa_app_application_effects_preload_transient_resources(effects, &count);
  for (size_t i = 0; i < count; i++)
    qa_application_printf(app, "Optional effect preload skipped: %s/%s: %s\n", failures[i].content, failures[i].path, failures[i].error);
  qa_app_effect_preload_failures_free(failures, count);
}

/* TS static sourceConfig / clientConfig: the product's user store. */
static qa_config_store product_config(const qa_app_options *options, qa_app_loaded_content *content, qa_content_id id) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(qa_app_loaded_content_catalog(content), id);
  char *root;
  if (product->user_content != NULL) root = dup_text(product->user_content->root);
  else {
    char *base = options->user_content_root != NULL ? dup_text(options->user_content_root) : checked(qa_default_user_content_root());
    root = checked(qa_user_product_directory(base, product->expectation.content_directory));
    free(base);
  }
  qa_config_store store;
  if (!qa_config_store_init(&store, root)) qa_range_error("Out of memory");
  free(root);
  return store;
}
qa_config_store qa_application_source_config(const qa_app_options *options, qa_app_loaded_content *content) {
  return product_config(options, content, qa_app_loaded_content_recipe(content)->map.entities.content);
}
qa_config_store qa_application_client_config(const qa_app_options *options, qa_app_loaded_content *content) {
  return product_config(options, content, qa_app_loaded_content_recipe(content)->engine_behavior.content);
}

/* TS saveSourceArchive(options, content, simulation, overrides). */
void qa_application_save_source_archive(qa_application *app, const qa_app_options *options, qa_app_loaded_content *content,
    qa_app_shared_simulation *simulation, qa_app_team_arena_overrides *overrides) {
  qa_cvar_registry *cvars = qa_application_source_cvars(app, simulation);
  if (cvars == NULL) return;
  qa_config_store store = qa_application_source_config(options, content);
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  const char *owner[3] = { "source", recipe->map.entities.content, recipe->map.entities.provider };
  size_t count = 0;
  qa_cvar_archive_entry *entries = qa_app_team_arena_archive_entries(cvars, overrides, false, 0, &count);
  qa_app_save_cvar_archive(&store, owner, 3, cvars, entries, count);
  qa_app_cvar_archive_entries_free(entries, count);
  qa_config_store_destroy(&store);
}
/* TS saveClientArchives(options, content, clients, overrides). */
void qa_application_save_client_archives(qa_application *app, const qa_app_options *options, qa_app_loaded_content *content,
    const qa_seat_id *seats, qa_cvar_registry *const *cvars, size_t count, qa_app_team_arena_overrides *overrides) {
  (void)app; /* TS member reads only its arguments */
  qa_config_store store = qa_application_client_config(options, content);
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  for (size_t i = 0; i < count; i++) {
    char index[40];
    qa_net_js_number((double)seats[i].index, index, sizeof index);
    const char *owner[4] = { "client", recipe->engine_behavior.content, recipe->engine_behavior.provider, index };
    size_t entry_count = 0;
    qa_cvar_archive_entry *entries = qa_app_team_arena_archive_entries(cvars[i], overrides, true, (double)seats[i].index, &entry_count);
    qa_app_save_cvar_archive(&store, owner, 4, cvars[i], entries, entry_count);
    qa_app_cvar_archive_entries_free(entries, entry_count);
  }
  qa_config_store_destroy(&store);
}

/* ---- 849-886 statics: closures that outlive openSource ---- */
/* TS static operatorHost(host, operatorOutput): host whose print goes to operatorOutput.write ?? the original print. */
typedef struct operator_print { qa_application_host original; qa_application_operator_output *output; } operator_print;
static void operator_host_print(void *ctx, const char *text) {
  operator_print *wrapper = ctx;
  if (wrapper->output->write != NULL) wrapper->output->write(wrapper->output->ctx, text);
  else wrapper->original.print(wrapper->original.ctx, text);
}
qa_application_host qa_application_operator_host_wrap(qa_application_host host, qa_application_operator_output *output,
    void **out_record) {
  operator_print *wrapper = checked(malloc(sizeof *wrapper));
  wrapper->original = host;
  wrapper->output = output;
  qa_application_host wrapped = host;
  wrapped.ctx = wrapper;
  wrapped.print = operator_host_print;
  *out_record = wrapper;
  return wrapped;
}
/* TS static hostPrint(host). The printer keeps a copy of the host's print. */
typedef struct host_printer { void (*print)(void *ctx, const char *text); void *ctx; } host_printer;
static void host_printer_print(void *ctx, const char *text) {
  host_printer *printer = ctx;
  printer->print(printer->ctx, text);
}
qa_application_printer qa_application_host_print(const qa_application_host *host) {
  host_printer *record = checked(malloc(sizeof *record));
  record->print = host->print;
  record->ctx = host->ctx;
  qa_application_printer printer = { host_printer_print, record, record };
  return printer;
}
/* TS static frontendPrint(host, owner): the host and every local console. */
typedef struct frontend_printer { host_printer host; qa_application_owner_ref *owner; } frontend_printer;
static void frontend_printer_print(void *ctx, const char *text) {
  frontend_printer *printer = ctx;
  printer->host.print(printer->host.ctx, text);
  qa_application *app = printer->owner->application;
  if (app == NULL || app->graphical == NULL) return;
  size_t count = qa_app_application_input_local_count(app->graphical->input);
  for (size_t i = 0; i < count; i++) qa_seat_console_print(qa_app_application_input_local(app->graphical->input, i)->console, text);
}
qa_application_printer qa_application_frontend_print(const qa_application_host *host, qa_application_owner_ref *owner) {
  frontend_printer *record = checked(malloc(sizeof *record));
  record->host.print = host->print;
  record->host.ctx = host->ctx;
  record->owner = owner;
  qa_application_printer printer = { frontend_printer_print, record, record };
  return printer;
}
/* TS static startupDemoService(dedicated, prepared, host, owner). */
typedef struct startup_demo { qa_app_prepared_startup *prepared; host_printer host; qa_application_owner_ref *owner; } startup_demo;
static qa_cvar_registry *startup_demo_source(startup_demo *demo) {
  if (demo->owner->application == NULL) return qa_app_prepared_startup_source(demo->prepared);
  qa_cvar_registry *current = qa_application_source_cvars(demo->owner->application, NULL);
  if (current == NULL) qa_raise("Error", 0, 0, "Local demo commands require the active world's source cvars");
  return current;
}
static qa_command_dialect startup_demo_dialect(void *ctx) { return qa_cvar_registry_dialect(startup_demo_source(ctx)); }
static char *startup_demo_take_next(void *ctx, const char *name) {
  qa_cvar_registry *source = startup_demo_source(ctx);
  char *text = qa_application_cvar_string(source, name);
  qa_application_cvar_set(source, name, "", true);
  return text;
}
static void startup_demo_print(void *ctx, const char *text) {
  startup_demo *demo = ctx;
  qa_application *app = demo->owner->application;
  if (app == NULL || app->graphical == NULL) demo->host.print(demo->host.ctx, text);
  else qa_app_application_input_print(app->graphical->input, text,
      qa_command_buffer_execution_context(qa_app_prepared_startup_commands(demo->prepared)));
}
qa_app_client_demo_commands *qa_application_startup_demo_service(bool dedicated, qa_app_prepared_startup *prepared,
    const qa_application_host *host, qa_application_owner_ref *owner, void **out_record) {
  startup_demo *demo = checked(malloc(sizeof *demo));
  demo->prepared = prepared;
  demo->host.print = host->print;
  demo->host.ctx = host->ctx;
  demo->owner = owner;
  qa_app_local_world_demo_host services;
  memset(&services, 0, sizeof services);
  services.ctx = demo;
  services.dedicated = dedicated;
  services.commands = qa_app_prepared_startup_commands(prepared);
  services.dialect = startup_demo_dialect;
  services.take_next = startup_demo_take_next;
  services.print = startup_demo_print;
  *out_record = demo;
  return checked(qa_app_local_world_demo_commands(services));
}
/* TS static forwardStartupCommands(prepared, application). */
static void forward_startup_command(void *ctx, const char *name, const char *const *args, size_t count, qa_command_context source) {
  qa_application *app = ctx;
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT) origin = origin->caller;
  qa_seat_id seat = origin->seat;
  qa_application_command_request request;
  memset(&request, 0, sizeof request);
  request.target = QA_APPLICATION_TARGET_APPLICATION;
  request.name = dup_text(name);
  request.arguments = count == 0 ? NULL : checked(calloc(count, sizeof *request.arguments));
  for (size_t i = 0; i < count; i++) request.arguments[i] = dup_text(args[i]);
  request.argument_count = count;
  if (origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT) { request.has_seat = true; request.seat = seat; }
  request.has_source = true;
  request.source = qa_command_context_copy(&source);
  (void)qa_event_ring_admit(&app->requested_commands, &request);
}
void qa_application_forward_startup_commands(qa_app_prepared_startup *prepared, qa_application *app) {
  qa_app_prepared_startup_forward_commands(prepared, forward_startup_command, app);
}

/* ---- 820-835 guestOutput ---- */
typedef struct guest_output { qa_application *app; qa_application_local_q3_guest_world *local_guest; } guest_output;
static void guest_send(guest_output *output, double slot, const char *text) {
  qa_application_local_q3_guest_world *guest = output->local_guest;
  if (guest == NULL) return;
  for (size_t i = 0; i < guest->seat_count; i++) {
    qa_app_local_q3_client_state *state = guest->seats[i].state;
    if (slot == -1 || qa_app_local_q3_client_state_client_number(state) == slot)
      qa_app_local_q3_client_state_receive_server_command(state, qa_app_local_q3_client_state_server_command_sequence(state) + 1, text);
  }
}
static void guest_send_server_command(void *ctx, double slot, const char *text) { guest_send(ctx, slot, text); }
static void guest_configstring(void *ctx, double index, const char *value) {
  char **commands = NULL;
  size_t count = 0;
  if (!qa_q3_configstring_commands((int64_t)index, value, &commands, &count)) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) guest_send(ctx, -1, commands[i]);
  for (size_t i = 0; i < count; i++) free(commands[i]);
  free(commands);
}
static void guest_drop_client(void *ctx, double slot, const char *reason) {
  guest_output *output = ctx;
  qa_application_local_q3_guest_world *guest = output->local_guest;
  bool local = false;
  for (size_t i = 0; guest != NULL && i < guest->seat_count; i++)
    if (qa_app_local_q3_client_state_client_number(guest->seats[i].state) == slot) local = true;
  if (!local) return;
  if (guest != output->app->local_guest) qa_raise("Error", 0, 0, "Restored guest dropped a local client: %s", reason);
  qa_application_printf(output->app, "Local guest client dropped: %s\n", reason);
  qa_application_request_quit(output->app);
}
qa_app_q3_guest_output qa_application_guest_output(qa_application *app, qa_application_local_q3_guest_world *local_guest,
    bool default_local_guest) {
  if (default_local_guest) local_guest = app->local_guest;
  if (app->network != NULL && app->network->kind == QA_APPLICATION_NETWORK_Q3) return qa_app_q3_server_network_game_output(app->network->q3);
  if (!same_text(app->launch_options.network_kind, "offline")) qa_raise("Error", 0, 0, "Q3 guest has no server output owner");
  guest_output *record = checked(malloc(sizeof *record));
  record->app = app;
  record->local_guest = local_guest;
  grow((void **)&app->guest_output_records, &app->guest_output_record_capacity, app->guest_output_record_count + 1,
      sizeof *app->guest_output_records);
  app->guest_output_records[app->guest_output_record_count++] = record;
  qa_app_q3_guest_output output;
  output.ctx = record;
  output.configstring = guest_configstring;
  output.send_server_command = guest_send_server_command;
  output.drop_client = guest_drop_client;
  return output;
}

/* ---- 1288-1300 openDedicatedConsole ---- */
static void dedicated_quit(void *ctx, qa_command_invocation *invocation) {
  (void)invocation; /* TS () => this.requestQuit() */
  qa_application_request_quit(ctx);
}
static void dedicated_queue_run(void *ctx, qa_command_invocation *invocation) {
  qa_application_dedicated_queue *queue = ctx;
  size_t count = invocation->argc == 0 ? 0 : invocation->argc - 1;
  qa_application_queue_command(queue->app, queue->name, count == 0 ? NULL : (const char *const *)invocation->argv + 1, count, NULL);
}
static void dedicated_print(void *ctx, const char *text, const qa_command_context *source) {
  (void)source; /* TS print: text => { this.host.print(text); } */
  qa_application_print(ctx, text);
}
void qa_application_open_dedicated_console(qa_application *app) {
  if (app->source_commands != NULL) {
    app->dedicated_commands = app->source_commands;
    app->dedicated_console = checked(malloc(sizeof *app->dedicated_console));
    if (!qa_dedicated_console_init(app->dedicated_console)) qa_range_error("Out of memory");
    return;
  }
  qa_command_buffer_options options;
  memset(&options, 0, sizeof options);
  options.dialect = qa_application_source_dialect(app, NULL);
  if (app->image_settings != NULL) options.cvars = qa_app_image_settings_cvars(app->image_settings);
  options.context.session = qa_engine_session_id(app->session);
  options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  options.print = dedicated_print;
  options.print_ctx = app;
  qa_command_buffer *commands = checked(qa_command_buffer_create(&options));
  (void)qa_command_buffer_register(commands, "quit", dedicated_quit, app, NULL, QA_COMMAND_WORLD);
  static const char *const names[] = { "save", "load", "map", "say", "addbot", "removebot", "botlist" };
  for (size_t i = 0; i < 7; i++) {
    app->dedicated_queues[i].app = app;
    app->dedicated_queues[i].name = names[i];
    (void)qa_command_buffer_register(commands, names[i], dedicated_queue_run, &app->dedicated_queues[i],
        qa_app_save_command_documentation(names[i]), QA_COMMAND_WORLD);
  }
  app->dedicated_commands = commands;
  app->dedicated_console = checked(malloc(sizeof *app->dedicated_console));
  if (!qa_dedicated_console_init(app->dedicated_console)) qa_range_error("Out of memory");
}

/* ---- 1235-1269 frontend getters, window, network clients ---- */
qa_app_frontend_preference_overrides qa_application_frontend_settings(qa_application *app) {
  qa_app_frontend_preference_values current;
  bool has_current = app->graphical != NULL
      && qa_app_read_frontend_preferences(app->graphical->input, app->graphical->audio, &current);
  if (!has_current || !app->has_frontend_baseline) return app->frontend_overrides;
  return qa_app_changed_frontend_preferences(app->frontend_baseline, current, app->frontend_overrides);
}
bool qa_application_frontend_values(qa_application *app, qa_app_frontend_preference_values *out) {
  return app->graphical != NULL && qa_app_read_frontend_preferences(app->graphical->input, app->graphical->audio, out);
}
/* TS get window: this.graphical?.renderer.window ?? null. */
qa_sdl_window *qa_application_window(qa_application *app) {
  return app->graphical == NULL ? NULL : qa_app_native_renderer_window(app->graphical->renderer);
}
/* TS get networkClients: malloc'd rows (QuakeWorld rows gain sourceEntity = slot + 1). */
qa_app_network_player *qa_application_network_clients(qa_application *app, size_t *out_count) {
  *out_count = 0;
  qa_application_native_server *network = app->network;
  if (network == NULL) return NULL;
  size_t count = 0;
  qa_app_network_player *rows = NULL;
  switch (network->kind) {
    case QA_APPLICATION_NETWORK_QW: {
      count = qa_app_qw_server_network_clients(network->qw, NULL, 0);
      qa_app_qw_player *players = count == 0 ? NULL : checked(calloc(count, sizeof *players));
      count = qa_app_qw_server_network_clients(network->qw, players, count);
      rows = count == 0 ? NULL : checked(calloc(count, sizeof *rows));
      for (size_t i = 0; i < count; i++) { rows[i].client = players[i].client; rows[i].actor = players[i].actor; rows[i].source_entity = players[i].slot + 1; }
      free(players);
      break;
    }
    case QA_APPLICATION_NETWORK_Q1: {
      count = qa_app_q1_server_network_clients(network->q1, NULL, 0);
      qa_app_q1_player *players = count == 0 ? NULL : checked(calloc(count, sizeof *players));
      count = qa_app_q1_server_network_clients(network->q1, players, count);
      rows = count == 0 ? NULL : checked(calloc(count, sizeof *rows));
      for (size_t i = 0; i < count; i++) { rows[i].client = players[i].client; rows[i].actor = players[i].actor; rows[i].source_entity = players[i].source_entity; }
      free(players);
      break;
    }
    case QA_APPLICATION_NETWORK_Q3: {
      count = qa_app_q3_server_network_clients(network->q3, NULL, 0);
      qa_app_q3_player *players = count == 0 ? NULL : checked(calloc(count, sizeof *players));
      count = qa_app_q3_server_network_clients(network->q3, players, count);
      rows = count == 0 ? NULL : checked(calloc(count, sizeof *rows));
      for (size_t i = 0; i < count; i++) { rows[i].client = players[i].client; rows[i].actor = players[i].actor; rows[i].source_entity = players[i].source_entity; }
      free(players);
      break;
    }
    case QA_APPLICATION_NETWORK_Q2:
      count = qa_app_q2_server_network_clients(network->q2, NULL, 0);
      rows = count == 0 ? NULL : checked(calloc(count, sizeof *rows));
      count = qa_app_q2_server_network_clients(network->q2, rows, count);
      break;
    case QA_APPLICATION_NETWORK_UNIFIED:
      count = qa_app_unified_server_network_clients(network->unified, NULL, 0);
      rows = count == 0 ? NULL : checked(calloc(count, sizeof *rows));
      count = qa_app_unified_server_network_clients(network->unified, rows, count);
      break;
  }
  *out_count = count;
  return rows;
}

/* ---- static nativeCommandContext(content, session) ---- */
qa_command_context qa_application_native_command_context(qa_app_loaded_content *content, qa_session_id session) {
  const qa_command_producer *producer = qa_app_loaded_content_native_command_producer(content);
  if (producer == NULL) qa_raise("Error", 0, 0, "Native source command has no selected game module");
  qa_command_context context;
  memset(&context, 0, sizeof context);
  context.session = session;
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  context.producer = producer;
  return context;
}

/* ---- static guestOptions(content, options, host, commands, graph, nativeCommand?) ---- */
/* The closures the returned options capture; kept with the content (TS GC). */
typedef struct guest_options_record {
  qa_app_loaded_content *content;
  qa_application_host host;
  qa_command_buffer *(*guest_commands)(void *ctx); void *guest_ctx;
  qa_source_debug_graph *graph;
  void (*native_command)(void *ctx, const char *text); void *native_ctx;
  qa_app_sim_real_time real_time;
  qa_windows_capabilities weapon_clock;
  qa_windows_capabilities capabilities;
  double guest_clock_ticks; /* TS guestClockTicks: the Q2 guest's deterministic clock */
  qa_app_sim_classic_guest_files *writable, *installed;
  char *directory;
  bool has_localization; qa_localization_table localization;
  qa_app_sim_rerelease_localize localize;
  qa_app_sim_rerelease_clipboard clipboard;
  qa_app_sim_q3_guest_options q3;
  bool has_files; qa_user_file_store files;
  qa_qvm_common_services common;
  char *game_directory;
} guest_options_record;
static void guest_options_release(void *value) {
  guest_options_record *record = value;
  if (record->writable != NULL) qa_app_sim_classic_guest_files_destroy(record->writable);
  if (record->installed != NULL) qa_app_sim_classic_guest_files_destroy(record->installed);
  if (record->has_localization) qa_localization_table_destroy(&record->localization);
  if (record->has_files) qa_user_file_store_destroy(&record->files);
  free(record->directory);
  free(record->game_directory);
  free(record);
}
/* JS Math.trunc(performance.now()). */
static double guest_now_milliseconds(void) { return trunc(qa_application_performance_now()); }
static bool guest_clock_now(void *ctx, double *out) { (void)ctx; *out = guest_now_milliseconds(); return true; }
/* BigInt(Math.trunc(performance.now() * 1000)). */
static bool guest_clock_counter(void *ctx, int64_t *out) {
  (void)ctx; /* the clock reads no record state */
  *out = (int64_t)trunc(qa_application_performance_now() * 1000);
  return true;
}
/* The rerelease game seeds its mt19937 from the system clock. A wall-clock seed made gib model choices and
   other random outcomes differ per run, so the Q2 guest sees a clock that advances one millisecond per query:
   nowMilliseconds = 1_700_000_000_000 + ticks++, performanceCounter = BigInt(ticks++) * 1000n. */
static bool guest_q2_clock_now(void *ctx, double *out) {
  guest_options_record *record = ctx;
  *out = 1700000000000.0 + record->guest_clock_ticks++;
  return true;
}
static bool guest_q2_clock_counter(void *ctx, int64_t *out) {
  guest_options_record *record = ctx;
  *out = (int64_t)(record->guest_clock_ticks++) * 1000;
  return true;
}
/* new Date(year, month, 1).getTimezoneOffset(): minutes west of UTC at local midnight on that day. */
static double timezone_offset(int year, int month) {
  struct tm local;
  memset(&local, 0, sizeof local);
  local.tm_year = year - 1900; local.tm_mon = month; local.tm_mday = 1; local.tm_isdst = -1;
  time_t at = mktime(&local);
  struct tm resolved;
  localtime_r(&at, &resolved);
  return -(double)resolved.tm_gmtoff / 60;
}
/* realTime(output?): the local calendar; returns Math.trunc(now.getTime() / 1000). */
static double guest_real_time(void *ctx, bool has_output, qa_qvm_calendar *out_calendar) {
  (void)ctx; /* realTime reads only the wall clock */
  struct timespec wall;
  (void)clock_gettime(CLOCK_REALTIME, &wall);
  double milliseconds = floor((double)wall.tv_sec * 1000 + (double)wall.tv_nsec / 1e6);
  time_t seconds = (time_t)floor(milliseconds / 1000);
  struct tm now;
  localtime_r(&seconds, &now);
  int year = now.tm_year + 1900;
  if (has_output) {
    qa_qvm_calendar calendar;
    calendar.second = now.tm_sec; calendar.minute = now.tm_min; calendar.hour = now.tm_hour;
    calendar.day = now.tm_mday; calendar.month = now.tm_mon; calendar.year = year - 1900;
    calendar.weekday = now.tm_wday;
    /* Math.trunc((Date.UTC(year, month, day) - Date.UTC(year, 0, 1)) / 86400000) is the zero-based day of the year. */
    calendar.year_day = now.tm_yday;
    double offset = -(double)now.tm_gmtoff / 60;
    double january = timezone_offset(year, 0), july = timezone_offset(year, 6);
    calendar.is_dst = offset < (january > july ? january : july) ? 1 : 0;
    *out_calendar = calendar;
  }
  return trunc(milliseconds / 1000);
}
static qa_windows_file *guest_open_file(void *ctx, const char *path, bool read, bool write, int creation) {
  guest_options_record *record = ctx;
  qa_windows_file *file = qa_app_sim_classic_guest_files_open(record->writable, path, read, write, creation);
  if (file != NULL || write || record->installed == NULL) return file;
  return qa_app_sim_classic_guest_files_open(record->installed, path, read, write, creation);
}
/* new TextDecoder().decode(bytes): WHATWG UTF-8 decode, each maximal invalid subpart becomes U+FFFD; malloc'd. */
static char *decode_utf8_lossy(const uint8_t *bytes, size_t length) {
  char *out = checked(malloc(length * 3 + 1));
  size_t written = 0, i = 0;
  static const char replacement[] = "\xEF\xBF\xBD";
  while (i < length) {
    uint8_t first = bytes[i];
    size_t needed = 0; uint8_t lower = 0x80, upper = 0xBF;
    if (first < 0x80) { out[written++] = (char)first; i++; continue; }
    if (first >= 0xC2 && first <= 0xDF) needed = 1;
    else if (first >= 0xE0 && first <= 0xEF) { needed = 2; if (first == 0xE0) lower = 0xA0; if (first == 0xED) upper = 0x9F; }
    else if (first >= 0xF0 && first <= 0xF4) { needed = 3; if (first == 0xF0) lower = 0x90; if (first == 0xF4) upper = 0x8F; }
    else { memcpy(out + written, replacement, 3); written += 3; i++; continue; }
    size_t seen = 0;
    while (seen < needed && i + 1 + seen < length) {
      uint8_t next = bytes[i + 1 + seen];
      if (next < lower || next > upper) break;
      lower = 0x80; upper = 0xBF;
      seen++;
    }
    if (seen == needed) { memcpy(out + written, bytes + i, needed + 1); written += needed + 1; }
    else { memcpy(out + written, replacement, 3); written += 3; }
    i += seen + 1;
  }
  out[written] = '\0';
  return out;
}
static void guest_host_print(guest_options_record *record, const char *text) { record->host.print(record->host.ctx, text); }
/* standardOutput: (_stream, bytes) => host.print(new TextDecoder().decode(bytes)). */
static void guest_standard_output(void *ctx, qa_windows_stream stream, const uint8_t *bytes, size_t length) {
  (void)stream; /* TS prints both streams the same way */
  char *text = decode_utf8_lossy(bytes, length);
  guest_host_print(ctx, text);
  free(text);
}
static void guest_print(void *ctx, const char *text) { guest_host_print(ctx, text); }
static void guest_add_command(void *ctx, const char *text) {
  guest_options_record *record = ctx;
  if (record->native_command != NULL) { record->native_command(record->native_ctx, text); return; }
  qa_command_buffer *owner = record->guest_commands(record->guest_ctx);
  qa_command_context context = qa_application_native_command_context(record->content, qa_command_buffer_context(owner).session);
  qa_command_buffer_append(owner, text, &context, -1);
}
static void guest_debug_graph(void *ctx, double value, double color) {
  guest_options_record *record = ctx;
  qa_source_debug_graph_add(record->graph, value, color);
}
static uint8_t *guest_localization_read(void *ctx, const char *path, size_t *out_length) {
  qa_content_opened_resource opened;
  memset(&opened, 0, sizeof opened);
  if (!qa_content_mounted_open(ctx, path, NULL, NULL, &opened)) return NULL;
  *out_length = opened.byte_length;
  return opened.bytes;
}
static char *guest_localize(void *ctx, const char *key, const char *const *arguments, size_t count) {
  guest_options_record *record = ctx;
  return checked(qa_app_q2_localized_text(&record->localization, key, arguments, count));
}
static void guest_clipboard_write(void *ctx, const char *text) { (void)ctx; qa_write_sdl_clipboard(text); }
static double guest_milliseconds(void *ctx) { (void)ctx; return guest_now_milliseconds(); }
static void guest_execute_now(void *ctx, const char *text) {
  guest_options_record *record = ctx;
  (void)qa_command_buffer_execute_now(record->guest_commands(record->guest_ctx), text, NULL, -1);
}
static void guest_append(void *ctx, const char *text) {
  guest_options_record *record = ctx;
  qa_command_buffer_append(record->guest_commands(record->guest_ctx), text, NULL, -1);
}
static void guest_insert(void *ctx, const char *text) {
  guest_options_record *record = ctx;
  qa_command_buffer_insert(record->guest_commands(record->guest_ctx), text, NULL, -1);
}
/* mkdirSync(directory, { recursive: true }). */
static void make_directories(const char *directory) {
  char *path = dup_text(directory);
  for (char *cursor = path + 1; ; cursor++) {
    bool end = *cursor == '\0';
    if (*cursor == '/' || end) {
      char saved = *cursor;
      *cursor = '\0';
      if (mkdir(path, 0777) != 0 && errno != EEXIST) {
        int failure = errno;
        free(path);
        qa_raise("Error", 0, 0, "%s, mkdir '%s'", strerror(failure), directory);
      }
      *cursor = saved;
    }
    if (end) break;
  }
  free(path);
}
static char *product_user_directory(const qa_app_options *options, const qa_catalog_discovered_product *product) {
  if (product->user_content != NULL) return dup_text(product->user_content->root);
  char *base = options->user_content_root != NULL ? dup_text(options->user_content_root) : checked(qa_default_user_content_root());
  char *root = checked(qa_user_product_directory(base, product->expectation.content_directory));
  free(base);
  return root;
}
static bool same_string(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
void qa_application_guest_options(qa_app_loaded_content *content, const qa_app_options *options, const qa_application_host *host,
    qa_command_buffer *(*guest_commands)(void *ctx), void *guest_ctx, qa_source_debug_graph *graph,
    void (*native_command)(void *ctx, const char *text), void *native_ctx, qa_app_simulation_options *out) {
  guest_options_record *record = checked(calloc(1, sizeof *record));
  qa_app_loaded_content_keep(content, record, guest_options_release);
  record->content = content; record->host = *host;
  record->guest_commands = guest_commands; record->guest_ctx = guest_ctx; record->graph = graph;
  record->native_command = native_command; record->native_ctx = native_ctx;
  record->real_time.ctx = record; record->real_time.real_time = guest_real_time;
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(content);
  size_t behavior_count = 0;
  const qa_app_prepared_weapon_behavior *behaviors = qa_app_loaded_content_prepared_weapon_behaviors(content, &behavior_count);
  bool qvm_behavior = false, native_behavior = false;
  for (size_t i = 0; i < behavior_count; i++) {
    if (same_string(behaviors[i].kind, "qvm")) qvm_behavior = true;
    if (same_string(behaviors[i].kind, "rerelease-native")) native_behavior = true;
  }
  for (size_t i = 0; i < recipe->mod_count; i++)
    if (recipe->mods[i]->declaration.runtime == QA_MOD_RUNTIME_NATIVE) native_behavior = true;
  const qa_app_prepared_qvm_grapple *grapple = qa_app_loaded_content_prepared_qvm_grapple(content);
  /* components */
  out->weapon_behavior_real_time = qvm_behavior || grapple != NULL ? &record->real_time : NULL;
  out->prepared_qvm_grapple = grapple;
  if (native_behavior) {
    record->weapon_clock.ctx = record;
    record->weapon_clock.now_milliseconds = guest_clock_now;
    record->weapon_clock.performance_counter = guest_clock_counter;
    record->weapon_clock.has_performance_frequency = true; record->weapon_clock.performance_frequency = 1000000;
    out->weapon_behavior_clock = &record->weapon_clock;
  } else out->weapon_behavior_clock = NULL;
  out->has_q2_guest = false;
  out->q3_guest = NULL;
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);
  const char *edition = NULL;
  const void *prepared_q2 = qa_app_loaded_content_prepared_q2_game(content, &edition);
  if (prepared_q2 != NULL) {
    const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, recipe->map.entities.content);
    record->directory = product_user_directory(options, product);
    make_directories(record->directory);
    record->writable = checked(qa_app_sim_classic_guest_files_create(record->directory, NULL, 0));
    if (product->loose_root != NULL) record->installed = checked(qa_app_sim_classic_guest_files_create(product->loose_root, NULL, 0));
    record->capabilities.ctx = record;
    record->guest_clock_ticks = 0;
    record->capabilities.now_milliseconds = guest_q2_clock_now;
    record->capabilities.performance_counter = guest_q2_clock_counter;
    record->capabilities.has_performance_frequency = true; record->capabilities.performance_frequency = 1000000;
    record->capabilities.open_file = guest_open_file;
    record->capabilities.standard_output = guest_standard_output;
    out->has_q2_guest = true;
    memset(&out->q2_guest, 0, sizeof out->q2_guest);
    out->q2_guest.capabilities = &record->capabilities;
    out->q2_guest.print_ctx = record; out->q2_guest.print = guest_print;
    out->q2_guest.cmd_ctx = record; out->q2_guest.add_command = guest_add_command;
    out->q2_guest.graph_ctx = record; out->q2_guest.debug_graph = guest_debug_graph;
    out->q2_guest.edition = edition; out->q2_guest.prepared = prepared_q2;
    if (strcmp(edition, "classic") == 0) return;
    const qa_app_sim_prepared_rerelease_guest *rerelease = prepared_q2;
    qa_content_mounted *mounts = (qa_content_mounted *)qa_app_loaded_content_for_content(content, rerelease->execution.owner.content);
    qa_load_server_localization_resources("english", guest_localization_read, mounts, QA_LOC_Q2_RERELEASE, &record->localization);
    record->has_localization = true;
    record->localize.ctx = record; record->localize.localize = guest_localize;
    record->clipboard.client = !options->dedicated;
    if (!options->dedicated) { record->clipboard.ctx = record; record->clipboard.write = guest_clipboard_write; }
    out->q2_guest.localize = &record->localize;
    out->q2_guest.clipboard = &record->clipboard;
    return;
  }
  const qa_app_prepared_q3_game *prepared_q3 = qa_app_loaded_content_prepared_q3_game(content);
  if (prepared_q3 == NULL) return;
  if (!options->dedicated && strcmp(options->network_kind, "offline") != 0) qa_raise("Error", 0, 0, "Local Q3 guests require offline operation");
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, recipe->map.entities.content);
  record->q3.prepared = prepared_q3;
  record->q3.has_game_type = true;
  record->q3.game_type = same_string(recipe->match.provider, "q3:ctf") || same_string(product->expectation.campaign, "ctf")
      || same_string(product->expectation.campaign, "lrctf") ? 4 : same_string(options->mode, "singleplayer") ? 2 : 0;
  char *root = product_user_directory(options, product);
  bool initialized = qa_user_file_store_init(&record->files, root);
  free(root);
  if (!initialized) qa_range_error("Out of memory");
  record->has_files = true;
  record->q3.writable = &record->files;
  /* basename(product.expectation.contentDirectory) */
  const char *directory = product->expectation.content_directory;
  size_t length = strlen(directory);
  while (length > 1 && directory[length - 1] == '/') length--;
  size_t start = length;
  while (start > 0 && directory[start - 1] != '/') start--;
  record->game_directory = checked(malloc(length - start + 1));
  memcpy(record->game_directory, directory + start, length - start);
  record->game_directory[length - start] = '\0';
  record->q3.game_directory = record->game_directory;
  record->q3.print_ctx = record; record->q3.print = guest_print;
  record->common.ctx = record;
  record->common.milliseconds = guest_milliseconds;
  record->common.real_time = guest_real_time;
  record->common.commands.ctx = record;
  record->common.commands.execute_now = guest_execute_now;
  record->common.commands.append = guest_append;
  record->common.commands.insert = guest_insert;
  record->q3.common = &record->common;
  out->q3_guest = &record->q3;
}
