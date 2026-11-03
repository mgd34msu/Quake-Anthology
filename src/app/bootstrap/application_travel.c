/* Port of src/app/bootstrap/application.ts, group P5 (save, load and
 * travel; TS saveGame .. retireRemovedLocalPlayers). TS awaits are
 * synchronous calls; a TS try/catch/finally is one qa_try over the body
 * (support.h). Layout and per-member status: docs/application_layout.md.
 * GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "application_internal.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/loading.h"
#include "qa/app/bootstrap/component_client_save.h"
#include "qa/app/bootstrap/original_save.h"
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/content/mounts.h"
#include "qa/core/q3_product_policy.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"
#include "qa/persistence/provider_ownership.h"
#include "qa/persistence/saved_game.h"
#include "qa/world/collision/index.h"

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
/* JS a.toLowerCase() === b (ASCII; see PORTING.md strings). */
static bool lower_equals(const char *a, const char *b) {
  for (; *a != '\0' && *b != '\0'; a++, b++)
    if ((*a >= 'A' && *a <= 'Z' ? *a + 32 : *a) != *b) return false;
  return *a == *b;
}
static char *dup_text(const char *text) { return checked(strdup(text == NULL ? "" : text)); }
/* C-only: a record the application owns until destroy (TS GC); seat_records
 * releases it with release, then frees it. */
static void keep_record(qa_application *app, void *record, void (*release)(void *record)) {
  grow((void **)&app->seat_records, &app->seat_record_capacity, app->seat_record_count + 1, sizeof *app->seat_records);
  app->seat_records[app->seat_record_count].record = record;
  app->seat_records[app->seat_record_count++].release = release;
}

/* TS `error instanceof Error ? error.message : String(error)`: every C raise is an Error. */
static void print_to_locals(qa_application *app, const char *text) {
  if (app->graphical == NULL) return;
  size_t count = qa_app_application_input_local_count(app->graphical->input);
  for (size_t i = 0; i < count; i++) qa_seat_console_print(qa_app_application_input_local(app->graphical->input, i)->console, text);
}
static void report_message(qa_application *app, const qa_caught_error *error) {
  size_t n = strlen(error->message) + 2;
  char *text = checked(malloc(n));
  (void)snprintf(text, n, "%s\n", error->message);
  qa_application_print(app, text);
  print_to_locals(app, text);
  free(text);
}

/* TS () => { if (!this.closed) this.graphical?.input.pollLoadingEvents(); } */
static void poll_loading_events(void *ctx) {
  qa_application *app = ctx;
  if (!app->closed && app->graphical != NULL) qa_app_application_input_poll_loading_events(app->graphical->input);
}
/* TS nextFrame as the loading operation hands it on. */
static void loading_next_frame(void *ctx) { qa_app_loading_next_frame(ctx); }

/* node:path basename(path, ext). */
static char *node_basename(const char *path, const char *ext) {
  size_t end = strlen(path);
  while (end > 1 && path[end - 1] == '/') end--;
  size_t start = end;
  while (start > 0 && path[start - 1] != '/') start--;
  if (end == 1 && path[0] == '/') start = end = 0;
  size_t length = end - start;
  if (ext != NULL) {
    size_t e = strlen(ext);
    if (e < length && memcmp(path + end - e, ext, e) == 0) length -= e;
  }
  char *out = checked(malloc(length + 1));
  memcpy(out, path + start, length);
  out[length] = '\0';
  return out;
}

static bool component_client_request(const void *record, void *ctx) {
  (void)ctx; /* predicate closure takes no capture */
  return ((const qa_application_command_request *)record)->target == QA_APPLICATION_TARGET_COMPONENT_CLIENT;
}
static bool pending_component_requests(qa_application *app) {
  return qa_event_ring_some_live(&app->requested_commands, component_client_request, NULL);
}

/* The ViewingSeat rows of graphical.presentations (malloc'd). */
static qa_app_viewing_seat *viewing_seats(const qa_application_graphical *graphical) {
  qa_app_viewing_seat *seats = checked(calloc(graphical->presentation_count + 1, sizeof *seats));
  for (size_t i = 0; i < graphical->presentation_count; i++)
    seats[i] = qa_app_world_seat_presentation_viewing_seat(graphical->presentations[i].presentation);
  return seats;
}
static const qa_active_mod_presentation **source_pointers(const qa_active_mod_presentation *sources, size_t count) {
  const qa_active_mod_presentation **pointers = checked(calloc(count + 1, sizeof *pointers));
  for (size_t i = 0; i < count; i++) pointers[i] = &sources[i];
  return pointers;
}

/* writeSavedGame(saveDirectory, path, save): the C writer reports failure as
 * false; the Node error text it would carry is the errno description. */
static void write_saved_game(qa_application *app, const char *path, const qa_saved_game *save) {
  errno = 0;
  if (!qa_write_saved_game(qa_application_save_directory(app), path, save))
    qa_raise("Error", 0, 0, "%s, open '%s'", errno == 0 ? "save write failed" : strerror(errno), path);
}

/* ---- saveGame(path, format = "shared") ---- */
typedef struct save_run {
  qa_application *app;
  const char *path;
  qa_app_save_format format;
  qa_app_shared_sim_save captured; bool has_captured;
  qa_q1_save_data data; bool has_data;
  qa_save_value *component; uint8_t *component_bytes;
  qa_save_image teams; bool has_teams;
  uint8_t *campaign_bytes;
  qa_provider_checkpoint *providers;
  qa_app_viewing_seat *seats; const qa_active_mod_presentation **sources;
} save_run;
static void save_run_release(void *ctx) {
  save_run *run = ctx;
  if (run->has_captured) qa_app_shared_sim_save_free(&run->captured);
  if (run->has_data) qa_q1_save_data_free(&run->data);
  if (run->component != NULL) qa_save_value_free(run->component);
  free(run->component_bytes);
  if (run->has_teams) {
    free((void *)run->teams.providers[run->teams.provider_count - 1].bytes.bytes);
    free((void *)run->teams.providers);
  }
  free(run->campaign_bytes);
  free(run->providers);
  free(run->seats);
  free(run->sources);
  memset(run, 0, sizeof *run);
}
/* TS serviceLoading(nextFrame => this.simulation.checkpointLoading(nextFrame), ...). */
static void checkpoint_operation(void *ctx, qa_app_loading_frame *frame) {
  save_run *run = ctx;
  qa_app_shared_sim_checkpoint_loading(run->app->world_simulation, loading_next_frame, frame, &run->captured);
  run->has_captured = true;
}
static void save_body(void *ctx) {
  save_run *run = ctx;
  qa_application *app = run->app;
  if (strcmp(run->format, "shared") != 0) {
    if (qa_application_campaign_movie(app) != NULL) qa_raise("Error", 0, 0, "Active component cinematics require a shared save");
    const qa_catalog_discovered_product *product = qa_content_catalog_product(qa_app_loaded_content_catalog(app->loaded_content),
        qa_app_loaded_content_recipe(app->loaded_content)->map.entities.content);
    char *comment = node_basename(run->path, ".sav");
    char *directories = strcmp(run->format, "v5") == 0 ? NULL : node_basename(product->expectation.content_directory, NULL);
    qa_cleanup comment_cleanup, directory_cleanup;
    qa_cleanup_push(&comment_cleanup, free, comment);
    qa_cleanup_push(&directory_cleanup, free, directories);
    if (!qa_app_shared_sim_capture_original_save(app->world_simulation, directories == NULL ? 5 : 6, directories, comment, &run->data))
      qa_range_error("Out of memory");
    run->has_data = true;
    qa_cleanup_pop(&directory_cleanup);
    qa_cleanup_pop(&comment_cleanup);
    free(directories);
    free(comment);
    qa_saved_game save;
    memset(&save, 0, sizeof save);
    save.kind = QA_SAVED_GAME_Q1_SOURCE;
    save.data = run->data;
    write_saved_game(app, run->path, &save);
    return;
  }
  qa_app_service_loading(checkpoint_operation, run, poll_loading_events, app);
  if (pending_component_requests(app)) qa_raise("Error", 0, 0, "Component commands changed during checkpoint capture");
  size_t source_count = 0;
  const qa_active_mod_presentation *sources = qa_app_shared_sim_mod_presentation_sources(app->world_simulation, &source_count);
  qa_application_movie *movie = qa_application_campaign_movie(app);
  if (movie != NULL && movie->component_owned && (app->graphical == NULL || source_count == 0))
    qa_raise("Error", 0, 0, "Active component cinematic has no saved client owner");
  /* clients = graphical === null || sources.length === 0 ? captured : saveComponentClients(captured, ...). */
  qa_save_image clients = run->captured.image;
  qa_provider_checkpoint component_entry;
  bool has_component = false;
  if (app->graphical != NULL && source_count != 0) {
    run->seats = viewing_seats(app->graphical);
    run->sources = source_pointers(sources, source_count);
    run->component = qa_app_application_mod_presentations_capture_checkpoint(app->graphical->mod_presentations, run->seats,
        app->graphical->presentation_count, run->sources, source_count);
    size_t length = 0;
    qa_app_save_component_clients(&clients, run->component, &component_entry, &run->component_bytes, &length);
    has_component = true;
  }
  run->providers = checked(calloc(clients.provider_count + 2, sizeof *run->providers));
  memcpy(run->providers, clients.providers, clients.provider_count * sizeof *run->providers);
  size_t provider_count = clients.provider_count;
  if (has_component) run->providers[provider_count++] = component_entry;
  clients.providers = run->providers;
  clients.provider_count = provider_count;
  /* saveTeamArenaOverrides(clients, this.teamArenaOverrides, this.overrideSeats()). */
  qa_save_image overridden = clients;
  if (app->team_arena_overrides != NULL) {
    size_t seat_count = qa_application_override_seats(app, NULL, NULL, 0, NULL, 0);
    qa_app_override_registry *seats = checked(calloc(seat_count + 1, sizeof *seats));
    qa_cleanup seats_cleanup;
    qa_cleanup_push(&seats_cleanup, free, seats);
    (void)qa_application_override_seats(app, NULL, NULL, 0, seats, seat_count);
    run->teams = qa_app_save_team_arena_overrides(clients, app->team_arena_overrides, seats, seat_count);
    run->has_teams = run->teams.provider_count != clients.provider_count;
    qa_cleanup_pop(&seats_cleanup);
    free(seats);
    overridden = run->teams;
  }
  /* this.campaignUnit.attach(image): worldOnly(image) plus the unit checkpoint. */
  size_t campaign_length = 0;
  qa_provider_checkpoint campaign = qa_app_campaign_unit_attach(app->campaign_unit, &overridden, &run->campaign_bytes, &campaign_length);
  qa_provider_checkpoint *attached = checked(calloc(overridden.provider_count + 1, sizeof *attached));
  qa_cleanup attached_cleanup;
  qa_cleanup_push(&attached_cleanup, free, attached);
  size_t attached_count = 0;
  for (size_t i = 0; i < overridden.provider_count; i++) {
    const char *schema = overridden.providers[i].schema;
    if (schema != NULL && strcmp(schema, qa_campaign_unit_checkpoint.schema) == 0) continue;
    attached[attached_count++] = overridden.providers[i];
  }
  attached[attached_count++] = campaign;
  qa_saved_game save;
  memset(&save, 0, sizeof save);
  save.kind = QA_SAVED_GAME_SHARED;
  save.image = overridden;
  save.image.providers = attached;
  save.image.provider_count = attached_count;
  write_saved_game(app, run->path, &save);
  qa_cleanup_pop(&attached_cleanup);
  free(attached);
}
void qa_application_save_game(qa_application *app, const char *path, qa_app_save_format format) {
  if (format == NULL) format = "shared";
  if (app->closed) qa_raise("Error", 0, 0, "Application is closed");
  const char *unavailable = qa_application_save_unavailable(app, QA_SAVE_PURPOSE_MANUAL);
  if (unavailable != NULL) qa_raise("Error", 0, 0, "%s", unavailable);
  if (app->world_operation != QA_APPLICATION_IDLE) qa_raise("Error", 0, 0, "Another world operation is in progress");
  qa_application_movie *movie = qa_application_campaign_movie(app);
  if ((movie != NULL && !movie->component_owned) || app->pending_team_arena != NULL || app->pending_map != NULL
      || app->has_pending_native_travel || app->has_pending_restart || app->has_pending_transition || app->has_pending_save)
    qa_raise("Error", 0, 0, "Saving requires pending world travel or restoration to finish");
  if (pending_component_requests(app)
      || (app->graphical != NULL && qa_app_application_mod_presentations_pending_commands(app->graphical->mod_presentations)))
    qa_raise("Error", 0, 0, "Saving requires pending component commands to finish");
  app->world_operation = QA_APPLICATION_SAVING;
  app->world_operation_completion = true;
  save_run run;
  memset(&run, 0, sizeof run);
  run.app = app; run.path = path; run.format = format;
  qa_caught_error caught;
  bool ok = qa_try(save_body, &run, &caught);
  save_run_release(&run);
  app->world_operation = QA_APPLICATION_IDLE;
  app->world_operation_completion = false;
  qa_application_resume_input(app);
  if (!ok) qa_rethrow(&caught);
}

/* ---- loadGame(path, sourceProduct?) ---- */
void qa_application_load_game(qa_application *app, const char *path, const char *source_product) {
  if (app->network != NULL) qa_raise("Error", 0, 0, "Save/load unavailable while hosting a network game.");
  if (app->closed || app->stepping) qa_raise("Error", 0, 0, "Save restoration requires an idle open application");
  qa_application_restore_saved_game(app, path, source_product);
}

/* ---- restoreSavedGame(path, sourceProduct?) ---- */
typedef struct restored_save {
  qa_app_prepared_application_save prepared;
  void *backing;
} restored_save;
static void restored_save_release(void *record) {
  restored_save *save = record;
  qa_app_prepared_application_save_free(&save->prepared, save->backing);
}
typedef struct restore_run { qa_application *app; const char *path; const char *source_product; } restore_run;
static void campaign_unit_cleanup(void *unit) { qa_app_campaign_unit_destroy(unit); }
static void restore_operation(void *ctx, qa_app_loading_frame *frame) {
  restore_run *run = ctx;
  qa_application *app = run->app;
  restored_save *save = checked(calloc(1, sizeof *save));
  /* The image stays referenced by the restored world (TS GC): the application keeps it. */
  keep_record(app, save, restored_save_release);
  qa_app_prepare_application_save_native(&app->launch_options, qa_app_loaded_content_catalog(app->loaded_content), run->path,
      run->source_product, &save->prepared, &save->backing);
  qa_app_campaign_unit *unit = qa_app_campaign_unit_create((qa_app_campaign_codec){ NULL, NULL, NULL });
  if (unit == NULL) qa_range_error("Out of memory");
  qa_cleanup unit_cleanup;
  qa_cleanup_push(&unit_cleanup, campaign_unit_cleanup, unit);
  qa_app_campaign_unit_restore(unit, &save->prepared.image);
  qa_application_world_replacement_request request;
  memset(&request, 0, sizeof request);
  request.map = save->prepared.image.recipe.map.geometry.requested_path;
  request.save = &save->prepared.image;
  request.next_frame = loading_next_frame; request.next_frame_ctx = frame;
  request.q2_next_server = "";
  request.restored_options = &save->prepared.options;
  qa_application_prepare_and_replace_world(app, &request);
  qa_cleanup_pop(&unit_cleanup);
  qa_app_campaign_unit_destroy(app->campaign_unit);
  app->campaign_unit = unit;
}
static void clear_pending_world(qa_application *app, bool clear_save) {
  app->pending_team_arena = NULL;
  free(app->pending_map); app->pending_map = NULL;
  app->has_pending_q3_map = false;
  if (app->has_pending_native_travel) {
    free(app->pending_native_travel.name); free(app->pending_native_travel.spawn_point);
    qa_app_q2_travel_target_destroy(app->pending_native_travel.next);
    app->has_pending_native_travel = false;
  }
  app->has_pending_restart = false;
  app->has_pending_transition = false;
  if (clear_save && app->has_pending_save) {
    free(app->pending_save_path); free(app->pending_save_source_product);
    app->pending_save_path = app->pending_save_source_product = NULL;
    app->has_pending_save = false;
  }
}
static void restore_body(void *ctx) {
  restore_run *run = ctx;
  qa_app_service_loading(restore_operation, run, poll_loading_events, run->app);
  clear_pending_world(run->app, true);
}
void qa_application_restore_saved_game(qa_application *app, const char *path, const char *source_product) {
  if (qa_application_campaign_movie(app) != NULL) qa_raise("Error", 0, 0, "Finish or skip the campaign cinematic before loading a save");
  if (app->world_operation != QA_APPLICATION_IDLE) qa_raise("Error", 0, 0, "Another world operation is in progress");
  app->world_operation = QA_APPLICATION_LOADING;
  app->world_operation_completion = true;
  restore_run run = { app, path, source_product };
  qa_caught_error caught;
  bool ok = qa_try(restore_body, &run, &caught);
  app->world_operation = QA_APPLICATION_IDLE;
  app->world_operation_completion = false;
  app->world_operations++;
  if (!app->closed) qa_application_resume_input(app);
  if (!ok) {
    if (app->fatal_world_failure && !app->stepping) qa_application_close_failed_world(app, &caught);
    qa_rethrow(&caught);
  }
}

/* ---- restartSourceRound() ---- */
static qa_application_q3_seat_client *seat_q3(qa_application_graphical *graphical, qa_seat_id seat) {
  if (graphical == NULL) return NULL;
  for (size_t i = 0; i < graphical->q3_count; i++) if (qa_same_seat(graphical->q3_seats[i], seat)) return &graphical->q3[i];
  return NULL;
}
static int compare_slot(const void *a, const void *b) {
  int64_t left = ((const qa_client_id *)a)->slot, right = ((const qa_client_id *)b)->slot;
  return left < right ? -1 : left > right;
}
/* TS [...simulation.clientIdentities()].sort((left, right) => left.slot - right.slot) (slots are distinct). */
static qa_client_id *sorted_clients(qa_app_shared_simulation *simulation, size_t *out_count) {
  size_t count = 0;
  const qa_client_id *clients = qa_app_shared_sim_client_identities(simulation, &count);
  qa_client_id *sorted = checked(calloc(count + 1, sizeof *sorted));
  if (count != 0) memcpy(sorted, clients, count * sizeof *sorted);
  qsort(sorted, count, sizeof *sorted, compare_slot);
  *out_count = count;
  return sorted;
}
static bool has_client(const qa_client_id *clients, size_t count, qa_client_id client) {
  for (size_t i = 0; i < count; i++) if (qa_same_client(clients[i], client)) return true;
  return false;
}
static size_t local_count(qa_application_graphical *graphical) {
  return graphical == NULL ? 0 : qa_app_application_input_local_count(graphical->input);
}
static qa_app_local_input *local_at(qa_application_graphical *graphical, size_t index) {
  return qa_app_application_input_local(graphical->input, index);
}
static bool local_live(qa_app_local_input *local, const qa_client_id *clients, size_t count) {
  qa_session_seat *seat = local->player.seat;
  return !qa_session_seat_is_closed(seat) && !qa_session_client_is_closed(qa_session_seat_client(seat))
      && has_client(clients, count, qa_session_client_id(qa_session_seat_client(seat)));
}

typedef struct event_batch { const qa_app_sim_presentation_event **pointers; qa_app_sim_presentation_event *owned; size_t count; } event_batch;
static event_batch drain_batch(qa_app_shared_simulation *simulation) {
  event_batch batch = { NULL, NULL, 0 };
  size_t count = 0;
  const qa_app_sim_presentation_event *const *events = qa_app_shared_sim_drain_presentation_events(simulation, &count);
  batch.owned = checked(calloc(count + 1, sizeof *batch.owned));
  batch.pointers = checked(calloc(count + 1, sizeof *batch.pointers));
  for (size_t i = 0; i < count; i++) {
    if (!qa_app_sim_presentation_event_copy(&batch.owned[i], events[i])) qa_range_error("Out of memory");
    batch.pointers[i] = &batch.owned[i];
    batch.count = i + 1;
  }
  return batch;
}
static void batch_free(void *ctx) {
  event_batch *batch = ctx;
  for (size_t i = 0; i < batch->count; i++) qa_app_sim_presentation_event_release(&batch->owned[i]);
  free(batch->owned);
  free(batch->pointers);
  memset(batch, 0, sizeof *batch);
}
/* TS network?.server.publish(output, events, performance.now()) (Q3 only here: restartSourceRound returns early otherwise). */
static void q3_network_publish(qa_application *app, const qa_simulation_output *output, const event_batch *events) {
  if (app->network == NULL) return;
  qa_app_q3_server_network_publish(app->network->q3, output, (const void *const *)events->pointers, events->count,
      qa_application_performance_now());
}

typedef struct round_restart {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_application_graphical *graphical;
  qa_app_q3_source_runtime *old_source;
  qa_world_snapshot snapshot;
  double server_id;
  qa_client_id *clients; size_t client_count;
  bool mutated;
  bool result;
  qa_app_q3_network_round_restart *round;
} round_restart;

/* source.sourceState(): the client and prediction keep the state, so the application owns it (TS GC). */
static void source_state_release(void *record) {
  qa_app_q3_source_presentation_state **state = record;
  qa_app_q3_source_presentation_state_destroy(*state);
}
static qa_app_q3_source_presentation_state *round_source_state(round_restart *r, qa_app_q3_source_runtime *source) {
  qa_app_q3_source_presentation_state *state = qa_app_q3_source_runtime_source_state(source);
  if (state == NULL) qa_range_error("Out of memory");
  qa_app_q3_source_presentation_state **record = checked(malloc(sizeof *record));
  *record = state;
  keep_record(r->app, record, source_state_release);
  return state;
}
/* The rebindRound closures over the restarted source and the seat's prediction host. */
typedef qa_application_q3_round_binding round_binding;
static void round_settings_free(round_binding *binding) {
  for (size_t i = 0; i < binding->setting_count; i++) qa_cvar_read_free(&binding->settings[i]);
  free(binding->settings);
  binding->settings = NULL;
  binding->setting_count = 0;
}
void qa_application_round_binding_release(void *record) { round_settings_free(record); }
/* linkBounds: number => source.world.linkState(number)?.absbounds ?? null. */
bool qa_application_round_link_bounds(void *ctx, double number, qa_bounds *out) {
  round_binding *binding = ctx;
  qa_q3_base_server_world world = qa_app_q3_source_runtime_world(binding->source);
  qa_q3_base_link_state state;
  if (!world.link_state(world.ctx, number, &state)) return false;
  *out = state.absbounds;
  return true;
}
/* sourceActor: number => { const record = source.records.get(number); return record?.inuse ? record.actor.id : null; }. */
bool qa_application_round_source_actor(void *ctx, double number, qa_actor_id *out) {
  round_binding *binding = ctx;
  qa_q3_game_entity *record = qa_q3_base_entity_records_get(qa_app_q3_source_runtime_records(binding->source), number);
  if (record == NULL || !qa_q3_game_entity_inuse(record)) return false;
  *out = qa_q3_game_entity_actor(record).id;
  return true;
}
/* predictionCommand: (command, time) => prediction.submit(command, time). */
qa_q3_base_user_command qa_application_round_prediction_command(void *ctx, qa_actor_command command, double time) {
  round_binding *binding = ctx;
  return qa_app_presentation_prediction_adapter_submit(binding->adapter, command, time);
}
/* serverSettings: () => source.host.cvars.snapshots().filter(variable =>
 * source.settings.definitions.some(definition => definition.name === variable.name)). */
qa_cvar_snapshot *qa_application_round_server_settings(void *ctx, size_t *out_count) {
  round_binding *binding = ctx;
  round_settings_free(binding);
  size_t count = 0, name_count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(qa_app_q3_source_runtime_host(binding->source)->cvars, 0, &count);
  const char *const *names = qa_app_q3_source_runtime_setting_names(binding->source, &name_count);
  size_t kept = 0;
  for (size_t i = 0; i < count; i++) {
    bool defined = false;
    for (size_t j = 0; j < name_count && !defined; j++) defined = strcmp(names[j], snapshots[i].name) == 0;
    if (defined) snapshots[kept++] = snapshots[i];
    else qa_cvar_read_free(&snapshots[i]);
  }
  binding->settings = snapshots;
  binding->setting_count = kept;
  *out_count = kept;
  return snapshots;
}

/* TS deliver(initial = []). */
static void round_deliver(round_restart *r, event_batch *initial) {
  qa_application *app = r->app;
  bool use_initial = initial != NULL && initial->count != 0;
  for (;;) {
    event_batch events = use_initial ? *initial : drain_batch(r->simulation);
    bool owned = !use_initial;
    use_initial = false;
    qa_cleanup cleanup;
    if (owned) qa_cleanup_push(&cleanup, batch_free, &events);
    if (events.count == 0) {
      if (owned) { qa_cleanup_pop(&cleanup); batch_free(&events); }
      break;
    }
    for (size_t i = 0; r->graphical != NULL && i < r->graphical->q3_count; i++)
      qa_app_application_q3_client_receive_events(r->graphical->q3[i].client, events.owned, events.count);
    if (app->bots != NULL) app->bots->receive(app->bots->ctx, events.owned, events.count);
    if (r->round != NULL) r->round->receive_events(r->round->ctx, (const void *const *)events.pointers, events.count);
    qa_application_source_actions(app, events.owned, events.count);
    if (events.count != 0) qa_event_ring_admit_all(&app->round_presentation_events, events.owned, events.count);
    if (owned) {
      qa_cleanup_pop(&cleanup);
      /* The round ring took the records over (a bytewise copy); only the arrays are freed. */
      free(events.owned); free(events.pointers);
    }
  }
  if (app->closed) qa_raise("Error", 0, 0, "Application closed during round restart");
}
/* TS settle(). */
static void round_settle(round_restart *r) {
  qa_application *app = r->app;
  qa_input_batch input;
  memset(&input, 0, sizeof input);
  input.elapsed_milliseconds = 100;
  qa_simulation_output output = qa_engine_session_step_async(app->session, input, NULL, NULL);
  app->elapsed += 100;
  app->has_last_output = true;
  app->last_output_simulation = r->simulation;
  app->last_output = output;
  round_deliver(r, NULL);
}
/* TS restart(round). */
static void round_restart_body(round_restart *r, qa_app_q3_network_round_restart *round) {
  qa_application *app = r->app;
  r->round = round;
  r->mutated = true;
  app->last_restart_frame = app->frames;
  event_batch epoch = drain_batch(r->simulation);
  qa_cleanup epoch_cleanup;
  qa_cleanup_push(&epoch_cleanup, batch_free, &epoch);
  if (r->graphical != NULL) {
    qa_app_application_effects_reset_round(r->graphical->effects);
    qa_app_application_audio_reset_round(r->graphical->audio);
  }
  if (app->bots != NULL) { size_t ignored = 0; (void)app->bots->begin_round_restart(app->bots->ctx, &ignored); }
  if (app->network == NULL) {
    char text[40];
    qa_net_js_number(r->server_id + 1, text, sizeof text);
    qa_application_cvar_set(qa_app_q3_source_runtime_host(r->old_source)->cvars, "sv_serverid", text, true);
  }
  qa_application_close_rankings(app, r->simulation);
  size_t ignored = 0;
  (void)qa_app_shared_sim_restart_source_round(r->simulation, &ignored);
  qa_app_shared_sim_begin_source_round_settlement(r->simulation);
  if (app->bots != NULL) app->bots->bind_restarted_round(app->bots->ctx);
  if (round != NULL) round->bind_source(round->ctx);
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(r->simulation);
  if (source == NULL) qa_raise("Error", 0, 0, "Round restart lost its native source");
  int bit = app->local_snapshot_server_bit == 0 ? 4 : 0;
  /* The epoch events are delivered first; the round ring keeps copies. */
  if (epoch.count != 0) {
    for (size_t i = 0; r->graphical != NULL && i < r->graphical->q3_count; i++)
      qa_app_application_q3_client_receive_events(r->graphical->q3[i].client, epoch.owned, epoch.count);
    if (app->bots != NULL) app->bots->receive(app->bots->ctx, epoch.owned, epoch.count);
    if (round != NULL) round->receive_events(round->ctx, (const void *const *)epoch.pointers, epoch.count);
    qa_application_source_actions(app, epoch.owned, epoch.count);
    qa_event_ring_admit_all(&app->round_presentation_events, epoch.owned, epoch.count);
    free(epoch.owned); free(epoch.pointers);
    epoch.owned = NULL; epoch.pointers = NULL; epoch.count = 0;
  }
  qa_cleanup_pop(&epoch_cleanup);
  batch_free(&epoch);
  round_deliver(r, NULL);
  for (int frame = 0; frame < 3; frame++) round_settle(r);
  qa_app_local_player *players = checked(calloc(r->client_count + 1, sizeof *players));
  qa_cleanup players_cleanup;
  qa_cleanup_push(&players_cleanup, free, players);
  size_t player_count = 0;
  for (size_t i = 0; i < r->client_count; i++) {
    qa_client_id client = r->clients[i];
    if (round != NULL && round->reconnect_client(round->ctx, client)) { round_deliver(r, NULL); continue; }
    if (app->bots != NULL && app->bots->reconnect_restarted_client(app->bots->ctx, client)) { round_deliver(r, NULL); continue; }
    qa_app_local_input *local = NULL;
    for (size_t j = 0; j < local_count(r->graphical) && local == NULL; j++) {
      qa_app_local_input *candidate = local_at(r->graphical, j);
      if (qa_same_client(qa_session_client_id(qa_session_seat_client(candidate->player.seat)), client)) local = candidate;
    }
    if (local == NULL || r->graphical == NULL) qa_raise("Error", 0, 0, "Round restart client has no admission owner");
    qa_application_q3_seat_client *presentation = seat_q3(r->graphical, qa_session_seat_id(local->player.seat));
    if (presentation == NULL || presentation->qvm) qa_raise("Error", 0, 0, "Round restart lost its local cgame");
    qa_app_application_q3_client_begin_round_restart(presentation->client, (double)bit, round_source_state(r, source));
    players[player_count].seat = local->player.seat;
    players[player_count++].actor = qa_app_shared_sim_admit_player(r->simulation, client).actor;
    round_deliver(r, NULL);
  }
  if (r->graphical != NULL) {
    qa_app_sim_presentation_access access = qa_app_shared_sim_presentation_access(r->simulation);
    qa_app_application_input_rebind_players(r->graphical->input, players, player_count, &access, false);
  }
  qa_cleanup_pop(&players_cleanup);
  free(players);
  for (size_t i = 0; i < local_count(r->graphical); i++) {
    qa_app_local_input *local = local_at(r->graphical, i);
    qa_application_q3_seat_client *presentation = seat_q3(r->graphical, qa_session_seat_id(local->player.seat));
    if (presentation == NULL || presentation->qvm) qa_raise("Error", 0, 0, "Round restart lost its prediction owner");
    qa_app_simulation_prediction_host *prediction = qa_app_create_simulation_prediction_host(
        qa_app_shared_sim_prediction_source(r->simulation), local->player.actor, qa_session_seat_id(local->player.seat));
    if (prediction == NULL) qa_range_error("Out of memory");
    qa_app_q3_source_presentation_state *initial = round_source_state(r, source);
    qa_app_simulation_prediction_host_capture_source(prediction, initial);
    round_binding *binding = checked(calloc(1, sizeof *binding));
    keep_record(app, binding, qa_application_round_binding_release);
    binding->source = source;
    binding->adapter = qa_app_simulation_prediction_host_adapter(prediction);
    qa_app_q3_local_round round_binding_value;
    memset(&round_binding_value, 0, sizeof round_binding_value);
    round_binding_value.actor = local->player.actor;
    round_binding_value.initial = initial;
    round_binding_value.movement = qa_app_presentation_prediction_adapter_host(binding->adapter);
    round_binding_value.ctx = binding;
    round_binding_value.link_bounds = qa_application_round_link_bounds;
    round_binding_value.source_actor = qa_application_round_source_actor;
    round_binding_value.prediction_command = qa_application_round_prediction_command;
    round_binding_value.server_settings = qa_application_round_server_settings;
    qa_app_application_q3_client_rebind_round(presentation->client, &round_binding_value);
    /* graphical.q3.set(seat, { kind: "native", client, prediction }): the previous prediction host is dropped. */
    qa_app_simulation_prediction_host *previous = presentation->prediction;
    presentation->prediction = prediction;
    presentation->actor = local->player.actor;
    if (previous != NULL && previous != prediction) qa_app_simulation_prediction_host_destroy(previous);
  }
  round_settle(r);
  qa_app_shared_sim_complete_source_round_settlement(r->simulation);
  app->local_snapshot_server_bit = bit;
}
static void round_restart_run(void *ctx, qa_app_q3_network_round_restart *round) { round_restart_body(ctx, round); }
static void round_mutation(void *ctx) {
  round_restart *r = ctx;
  r->mutated = true;
  r->app->last_restart_frame = r->app->frames;
}
static void restart_round_body(void *ctx) {
  round_restart *r = ctx;
  qa_application *app = r->app;
  for (;;) {
    event_batch old = drain_batch(r->simulation);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, batch_free, &old);
    if (old.count == 0) { qa_cleanup_pop(&cleanup); batch_free(&old); break; }
    for (size_t i = 0; r->graphical != NULL && i < r->graphical->q3_count; i++)
      qa_app_application_q3_client_receive_events(r->graphical->q3[i].client, old.owned, old.count);
    for (size_t i = 0; r->graphical != NULL && i < r->graphical->presentation_count; i++)
      qa_app_world_seat_presentation_source_events(r->graphical->presentations[i].presentation, old.owned, old.count);
    if (app->bots != NULL) app->bots->receive(app->bots->ctx, old.owned, old.count);
    qa_simulation_output output;
    memset(&output, 0, sizeof output);
    output.snapshot = r->snapshot;
    q3_network_publish(app, &output, &old);
    qa_application_source_actions(app, old.owned, old.count);
    qa_cleanup_pop(&cleanup);
    batch_free(&old);
  }
  if (app->closed) qa_raise("Error", 0, 0, "Application closed before round restart");
  if (app->stopping) { r->result = true; return; }
  free(r->clients);
  r->clients = sorted_clients(r->simulation, &r->client_count);
  for (size_t i = 0; i < local_count(r->graphical); i++) {
    qa_app_local_input *local = local_at(r->graphical, i);
    if (!local_live(local, r->clients, r->client_count))
      qa_raise("Error", 0, 0, "Round restart lost a local client during old event delivery");
    qa_application_q3_seat_client *presentation = seat_q3(r->graphical, qa_session_seat_id(local->player.seat));
    if (presentation == NULL || presentation->qvm) qa_raise("Error", 0, 0, "Round restart lost its local cgame during old event delivery");
    qa_app_application_q3_client_assert_can_restart_round(presentation->client);
  }
  if (!qa_app_shared_sim_source_restart_plan(r->simulation).source_reset) { r->result = false; return; }
  if (app->network != NULL && app->network->kind == QA_APPLICATION_NETWORK_Q3)
    qa_app_q3_server_network_restart_source_round(app->network->q3, round_restart_run, r, round_mutation, r);
  else round_restart_body(r, NULL);
  if (app->bots != NULL) app->bots->resume_round_bots(app->bots->ctx);
  qa_application_ranking_frame(app);
  qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(r->simulation);
  if (!app->has_last_output || app->last_output_simulation != r->simulation || source == NULL)
    qa_raise("Error", 0, 0, "Round restart has no final frame");
  qa_simulation_output final = app->last_output;
  qa_app_q3_source_presentation_state *state = round_source_state(r, source);
  for (size_t i = 0; r->graphical != NULL && i < r->graphical->q3_count; i++) {
    qa_application_q3_seat_client *presentation = &r->graphical->q3[i];
    if (presentation->qvm) qa_raise("Error", 0, 0, "Round restart has a foreign cgame");
    qa_app_simulation_prediction_host_capture_source(presentation->prediction, state);
    qa_app_application_q3_client_receive(presentation->client, state, NULL, 0, NULL, 0);
  }
  event_batch none = { NULL, NULL, 0 };
  q3_network_publish(app, &final, &none);
  r->result = true;
}
static bool restart_source_round(qa_application *app) {
  qa_app_shared_simulation *simulation = app->world_simulation;
  qa_application_graphical *graphical = app->graphical;
  qa_application_native_server *network = app->network;
  if (!qa_app_shared_sim_source_restart_plan(simulation).source_reset || (network != NULL && network->kind != QA_APPLICATION_NETWORK_Q3)
      || qa_application_campaign_movie(app) != NULL || app->local_guest != NULL)
    return false;
  if (app->world_operation != QA_APPLICATION_IDLE) qa_raise("Error", 0, 0, "Another world operation is in progress");
  round_restart r;
  memset(&r, 0, sizeof r);
  r.app = app; r.simulation = simulation; r.graphical = graphical;
  bool has_snapshot = qa_engine_session_snapshot(app->session, &r.snapshot);
  r.old_source = qa_app_shared_sim_q3_source(simulation);
  if (!has_snapshot || r.old_source == NULL) return false;
  r.server_id = qa_cvar_registry_variable_value(qa_app_q3_source_runtime_host(r.old_source)->cvars, "sv_serverid");
  if (network == NULL && (!(isfinite(r.server_id) && trunc(r.server_id) == r.server_id) || r.server_id < 0 || r.server_id >= 0x7fffffff))
    qa_raise("Error", 0, 0, "Local Q3 server id exhausted");
  r.clients = sorted_clients(simulation, &r.client_count);
  qa_cleanup clients_cleanup;
  qa_cleanup_push(&clients_cleanup, free, r.clients);
  for (size_t i = 0; i < local_count(graphical); i++) {
    qa_app_local_input *local = local_at(graphical, i);
    if (!local_live(local, r.clients, r.client_count)) qa_raise("Error", 0, 0, "Round restart has no live local client");
    qa_application_q3_seat_client *presentation = seat_q3(graphical, qa_session_seat_id(local->player.seat));
    if (presentation == NULL || presentation->qvm) {
      qa_cleanup_pop(&clients_cleanup);
      free(r.clients);
      return false;
    }
    qa_app_application_q3_client_assert_can_restart_round(presentation->client);
  }
  qa_cleanup_pop(&clients_cleanup);
  app->world_operation = QA_APPLICATION_TRAVEL;
  app->world_operation_completion = true;
  qa_caught_error caught;
  bool ok = qa_try(restart_round_body, &r, &caught);
  free(r.clients);
  if (!ok && r.mutated) {
    app->fatal_world_failure = true;
    app->closed = true;
    app->stopping = true;
  }
  app->world_operation = QA_APPLICATION_IDLE;
  app->world_operation_completion = false;
  if (!app->closed) qa_application_resume_input(app);
  if (!ok) qa_rethrow(&caught);
  return r.result;
}
bool qa_application_restart_source_round(qa_application *app) { return restart_source_round(app); }

/* ---- captureBlocksTransition() ---- */
bool qa_application_capture_blocks_transition(qa_application *app) {
  return app->capture != NULL && qa_app_capture_pending_readback(app->capture)
      && (app->has_pending_save || app->pending_team_arena != NULL || app->pending_map != NULL || app->has_pending_native_travel
          || app->has_pending_transition || qa_app_shared_sim_pending_match_map(app->world_simulation) != NULL
          || (app->has_pending_restart && app->elapsed >= app->pending_restart));
}

/* ---- applyTransition() ---- */
typedef struct transition_try {
  qa_application *app;
  char *path, *source_product;
  qa_app_team_arena_skirmish *setup;
  qa_app_q2_travel_target target;
  char *map; bool has_q3_map; qa_app_q3_map_launch q3_map;
  const qa_app_simulation_travel *carry;
} transition_try;
static void pending_save_body(void *ctx) {
  transition_try *t = ctx;
  qa_application_restore_saved_game(t->app, t->path, t->source_product);
  clear_pending_world(t->app, false);
}
/* TS mounts.read / mounts.listFiles as readTeamArenaCampaign's source. */
static char *team_arena_read(void *ctx, const char *path) {
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_path(ctx, path, &length);
  char *text = checked(malloc(length + 1));
  if (length > 0) memcpy(text, bytes, length);
  text[length] = '\0';
  free(bytes);
  return text;
}
static char **team_arena_list(void *ctx, size_t *out_count) {
  *out_count = 0;
  char **files = qa_content_mounted_list_files(ctx, "scripts", ".team", out_count);
  if (files == NULL && *out_count > 0) qa_range_error("Out of memory");
  return files;
}
static void skirmish_release(void *record) { qa_app_team_arena_skirmish_free(record); }
typedef struct team_arena_try {
  qa_application *app;
  const char *action;
  double skill, game_type;
  qa_app_q3_source_runtime *source;
} team_arena_try;
static void team_arena_body(void *ctx) {
  team_arena_try *t = ctx;
  qa_application *app = t->app;
  qa_cvar_registry *cvars = qa_app_q3_source_runtime_host(t->source)->cvars;
  char *player = qa_application_cvar_string(cvars, "ui_teamName");
  qa_cleanup player_cleanup;
  qa_cleanup_push(&player_cleanup, free, player);
  char *opponent = qa_application_cvar_string(cvars, "ui_opponentName");
  qa_cleanup opponent_cleanup;
  qa_cleanup_push(&opponent_cleanup, free, opponent);
  qa_q3_product_policy policy;
  memset(&policy, 0, sizeof policy);
  qa_q3_application_product product;
  if (qa_app_loaded_content_q3_product(app->loaded_content, &product)) policy = product.policy;
  else policy.kind = QA_Q3_PRODUCT_RETAIL;
  qa_q3_team_arena_catalog_policy files = qa_q3_team_arena_catalog_policy_make(policy);
  qa_app_team_arena_campaign_host host = { (void *)qa_app_loaded_content_mounts(app->loaded_content), team_arena_read, team_arena_list,
      files.additional_teams, files.game_info, files.team_info };
  qa_app_team_arena_skirmish *setup = checked(calloc(1, sizeof *setup));
  /* options.teamArenaSkirmish keeps the setup for the destination world (TS GC). */
  keep_record(app, setup, skirmish_release);
  qa_app_team_arena_teams teams = { player, opponent };
  qa_app_read_team_arena_skirmish(setup, host, t->skill, true, app->launch_options.map, t->game_type,
      strcmp(t->action, "next") == 0, true, teams);
  qa_cleanup_pop(&opponent_cleanup);
  free(opponent);
  qa_cleanup_pop(&player_cleanup);
  free(player);
  qa_application_replace_world_options options;
  memset(&options, 0, sizeof options);
  options.skirmish = setup;
  qa_application_replace_world_with(app, setup->map, NULL, &options);
}
static void native_travel_body(void *ctx) {
  transition_try *t = ctx;
  qa_application_advance_q2_travel(t->app, &t->target, NULL);
}
static void pending_map_body(void *ctx) {
  transition_try *t = ctx;
  qa_application_replace_world_options options;
  memset(&options, 0, sizeof options);
  options.q3_map = t->has_q3_map ? &t->q3_map : NULL;
  qa_application_replace_world_with(t->app, t->map, NULL, &options);
}
static void travel_q2_body(void *ctx) {
  transition_try *t = ctx;
  qa_app_q2_travel_target *target = qa_app_parse_q2_travel(t->map);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, (void (*)(void *))qa_app_q2_travel_target_destroy, target);
  qa_application_advance_q2_travel(t->app, target, t->carry);
  qa_cleanup_pop(&cleanup);
  qa_app_q2_travel_target_destroy(target);
}
/* TS this.host.lobby !== undefined && await this.finishQ2Match(). */
static bool lobby_finishes_match(qa_application *app) {
  return app->host.lobby != NULL && qa_application_finish_q2_match(app, NULL);
}
void qa_application_apply_transition(qa_application *app) {
  if (qa_application_client_commands_blocked(app)) return;
  if (app->has_pending_save) {
    transition_try t;
    memset(&t, 0, sizeof t);
    t.app = app;
    t.path = app->pending_save_path; t.source_product = app->pending_save_source_product;
    const qa_app_loaded_content *previous = app->loaded_content;
    app->has_pending_save = false;
    app->pending_save_path = app->pending_save_source_product = NULL;
    qa_caught_error caught;
    bool ok = qa_try(pending_save_body, &t, &caught);
    free(t.path); free(t.source_product);
    if (!ok) {
      if (app->fatal_world_failure || app->loaded_content != previous) qa_rethrow(&caught);
      report_message(app, &caught);
    }
    return;
  }
  if (app->pending_team_arena != NULL) {
    char action[8];
    (void)snprintf(action, sizeof action, "%s", app->pending_team_arena);
    app->pending_team_arena = NULL;
    qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(app->world_simulation);
    if (source == NULL || !qa_application_is_team_arena_skirmish(app, NULL))
      qa_raise("Error", 0, 0, "Team Arena progression has no native source owner");
    double game_type = qa_app_q3_source_runtime_game_type(source),
           skill = qa_cvar_registry_variable_value(qa_app_q3_source_runtime_host(source)->cvars, "g_spSkill");
    if ((game_type != 1 && game_type != 4 && game_type != 5 && game_type != 6 && game_type != 7)
        || (skill != 1 && skill != 2 && skill != 3 && skill != 4 && skill != 5))
      qa_raise("Error", 0, 0, "Team Arena source settings do not identify an authored match");
    const qa_app_loaded_content *previous = app->loaded_content;
    team_arena_try t = { app, action, skill, game_type, source };
    qa_caught_error caught;
    if (!qa_try(team_arena_body, &t, &caught)) {
      if (app->fatal_world_failure || app->loaded_content != previous) qa_rethrow(&caught);
      report_message(app, &caught);
    }
    return;
  }
  const char *match_map = qa_app_shared_sim_pending_match_map(app->world_simulation);
  if (match_map != NULL) {
    if (lobby_finishes_match(app)) return;
    char *path = qa_app_map_resource_path(match_map);
    qa_cleanup cleanup;
    qa_cleanup_push(&cleanup, free, path);
    qa_app_simulation_travel carry = qa_app_shared_sim_capture_travel(app->world_simulation, "");
    qa_application_travel_or_restart(app, path, &carry);
    qa_cleanup_pop(&cleanup);
    free(path);
    return;
  }
  if (app->has_pending_native_travel) {
    transition_try t;
    memset(&t, 0, sizeof t);
    t.app = app;
    t.target = app->pending_native_travel;
    app->has_pending_native_travel = false;
    qa_caught_error caught;
    bool ok = qa_try(native_travel_body, &t, &caught);
    free(t.target.name); free(t.target.spawn_point);
    qa_app_q2_travel_target_destroy(t.target.next);
    if (!ok) {
      if (app->fatal_world_failure) qa_rethrow(&caught);
      qa_application_report_campaign_travel_error(app, &caught);
    }
    return;
  }
  if (app->pending_map != NULL) {
    transition_try t;
    memset(&t, 0, sizeof t);
    t.app = app;
    t.map = app->pending_map;
    const qa_app_loaded_content *previous = app->loaded_content;
    t.has_q3_map = app->has_pending_q3_map;
    if (t.has_q3_map) t.q3_map = app->pending_q3_map;
    app->has_pending_q3_map = false;
    app->pending_map = NULL;
    if (app->has_pending_native_travel) {
      free(app->pending_native_travel.name); free(app->pending_native_travel.spawn_point);
      qa_app_q2_travel_target_destroy(app->pending_native_travel.next);
      app->has_pending_native_travel = false;
    }
    app->has_pending_transition = false;
    app->has_pending_restart = false;
    qa_caught_error caught;
    bool ok = qa_try(pending_map_body, &t, &caught);
    free(t.map);
    if (!ok) {
      if (app->fatal_world_failure || app->loaded_content != previous) qa_rethrow(&caught);
      report_message(app, &caught);
    }
    return;
  }
  if (app->has_pending_restart) {
    qa_app_q3_source_runtime *source = qa_app_shared_sim_q3_source(app->world_simulation);
    qa_app_q3_qvm_server_game *guest = qa_app_shared_sim_q3_guest(app->world_simulation);
    double now = source != NULL ? qa_app_q3_source_runtime_host(source)->now(qa_app_q3_source_runtime_host(source)->ctx)
        : guest != NULL ? qa_app_q3_qvm_server_game_time_milliseconds(guest) : app->elapsed;
    if (now >= app->pending_restart) {
      app->has_pending_restart = false;
      qa_application_replace_world_options options;
      memset(&options, 0, sizeof options);
      if (qa_app_shared_sim_q3_guest(app->world_simulation) != NULL) {
        options.initial_source_milliseconds = qa_app_q3_qvm_server_game_time_milliseconds(qa_app_shared_sim_q3_guest(app->world_simulation));
        options.guest_restart = true;
        qa_application_replace_world_with(app, app->launch_options.map, NULL, &options);
        app->last_restart_frame = app->frames;
        return;
      }
      if (qa_app_shared_sim_q3_source(app->world_simulation) == NULL) qa_raise("Error", 0, 0, "Quake III restart has no source game");
      if (!restart_source_round(app)) {
        options.initial_source_milliseconds = app->elapsed;
        qa_application_replace_world_with(app, app->launch_options.map, NULL, &options);
        app->last_restart_frame = app->frames;
      }
      return;
    }
  }
  if (!app->has_pending_transition) return;
  qa_transition_decision decision = app->pending_transition;
  app->has_pending_transition = false;
  if (strcmp(decision.kind, "travel") == 0) {
    if (lobby_finishes_match(app)) return;
    const char *colon_at = strchr(decision.map, ':');
    /* JS slice(0, -1) when indexOf returns -1. */
    size_t length = strlen(decision.map);
    size_t colon = colon_at == NULL ? (length == 0 ? 0 : length - 1) : (size_t)(colon_at - decision.map);
    long long after = colon_at == NULL ? 0 : (long long)colon + 1;
    char *family = checked(strndup(decision.map, colon));
    qa_cleanup family_cleanup;
    qa_cleanup_push(&family_cleanup, free, family);
    const qa_catalog_discovered_product *product =
        qa_content_catalog_product(qa_app_loaded_content_catalog(app->loaded_content), app->launch_options.product);
    if (strcmp(family, qa_game_family_name(product->expectation.family)) != 0)
      qa_raise("Error", 0, 0, "Campaign travel selected another content provider without a resolved launch recipe");
    qa_app_simulation_travel carry = qa_app_shared_sim_capture_travel(app->world_simulation, decision.spawn_point);
    const char *rest = decision.map + after;
    if (strcmp(family, "q2") == 0) {
      qa_ss_level_change change;
      char *original;
      if (qa_app_shared_sim_take_level_change(app->world_simulation, &change)) original = change.map;
      else {
        size_t n = strlen(rest) + strlen(decision.spawn_point) + 2;
        original = checked(malloc(n));
        (void)snprintf(original, n, "%s%s%s", rest, decision.spawn_point[0] == '\0' ? "" : "$", decision.spawn_point);
      }
      qa_cleanup original_cleanup;
      qa_cleanup_push(&original_cleanup, free, original);
      transition_try t;
      memset(&t, 0, sizeof t);
      t.app = app; t.map = original; t.carry = &carry;
      qa_caught_error caught;
      if (!qa_try(travel_q2_body, &t, &caught)) qa_application_report_campaign_travel_error(app, &caught);
      qa_cleanup_pop(&original_cleanup);
      free(original);
    } else {
      const char *map = rest[0] == '*' ? rest + 1 : rest;
      qa_application_travel_or_restart(app, map, &carry);
    }
    qa_cleanup_pop(&family_cleanup);
    free(family);
  } else if (strcmp(decision.kind, "campaign-complete") == 0) qa_application_print(app, "Campaign complete.\n");
  else qa_raise("Error", 0, 0, "Round restart requires the selected match provider");
}

/* ---- travelOrRestart(map, carry) ---- */
typedef struct travel_try { qa_application *app; const char *map; const qa_app_simulation_travel *carry; } travel_try;
static void travel_body(void *ctx) {
  travel_try *t = ctx;
  qa_application_replace_world(t->app, t->map, t->carry);
}
void qa_application_travel_or_restart(qa_application *app, const char *map, const qa_app_simulation_travel *carry) {
  const qa_app_loaded_content *previous = app->loaded_content;
  travel_try t = { app, map, carry };
  qa_caught_error caught;
  if (qa_try(travel_body, &t, &caught)) return;
  if (app->fatal_world_failure || app->loaded_content != previous) qa_rethrow(&caught);
  qa_application_report_campaign_travel_error(app, &caught);
  if (app->launch_options.dedicated) qa_application_replace_world(app, app->launch_options.map, NULL);
}

/* ---- reportCampaignTravelError(error) ---- */
void qa_application_report_campaign_travel_error(qa_application *app, const qa_caught_error *error) {
  if (app->fatal_world_failure) qa_rethrow(error);
  size_t n = strlen(error->message) + sizeof "Campaign transition failed: \n";
  char *message = checked(malloc(n));
  (void)snprintf(message, n, "Campaign transition failed: %s\n", error->message);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, message);
  qa_application_print(app, message);
  for (size_t i = 0; i < local_count(app->graphical); i++) {
    qa_app_local_input *local = local_at(app->graphical, i);
    qa_seat_console_print(local->console, message);
    /* Open through the console so its opened state stays in sync with input focus
     * (a bare setFocus leaves the toggle key re-opening instead of closing). */
    qa_seat_console_open(local->console);
  }
  qa_cleanup_pop(&cleanup);
  free(message);
}

/* ---- advanceQ2Travel(target, carry) ---- */
static qa_app_q2_travel_target *copy_target(const qa_app_q2_travel_target *target) {
  if (target == NULL) return NULL;
  qa_app_q2_travel_target *copy = checked(calloc(1, sizeof *copy));
  copy->kind = target->kind;
  copy->name = dup_text(target->name);
  copy->spawn_point = dup_text(target->spawn_point);
  copy->new_unit = target->new_unit;
  copy->next = copy_target(target->next);
  return copy;
}
/* TS captured checkpoint for the gamemap carry. */
typedef struct q2_checkpoint_run { qa_application *app; qa_app_shared_sim_save save; } q2_checkpoint_run;
static void q2_checkpoint_operation(void *ctx, qa_app_loading_frame *frame) {
  q2_checkpoint_run *run = ctx;
  qa_app_shared_sim_checkpoint_loading(run->app->world_simulation, loading_next_frame, frame, &run->save);
}
static void q2_checkpoint_body(void *ctx) {
  q2_checkpoint_run *run = ctx;
  qa_app_service_loading(q2_checkpoint_operation, run, poll_loading_events, run->app);
}
/* The visit's published() closure: nextserver and visit.commit(). */
typedef struct q2_visit { qa_application *app; qa_app_campaign_unit_visit *visit; char *next_server; bool committed; } q2_visit;
static void q2_visit_published(void *ctx) {
  q2_visit *visit = ctx;
  qa_cvar_registry *cvars = qa_application_source_cvars(visit->app, NULL);
  if (cvars != NULL) qa_application_cvar_set(cvars, "nextserver", visit->next_server, true);
  visit->committed = true;
  qa_app_campaign_unit_visit_commit(visit->visit);
}
static void q2_visit_cleanup(void *ctx) {
  q2_visit *visit = ctx;
  if (!visit->committed) qa_app_campaign_unit_visit_destroy(visit->visit);
  free(visit->next_server);
}
static void save_free_cleanup(void *ctx) { qa_app_shared_sim_save_free(ctx); }

/* The movie record the cinematic branch publishes: its complete() continues the chain. */
typedef struct q2_movie {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  uint64_t request;
  qa_app_q2_travel_target *next;     /* owned copy, NULL = TS null */
  bool new_unit;                     /* target.newUnit */
  bool has_carry; qa_app_simulation_travel carry;
} q2_movie;
static void q2_movie_release(void *record) {
  q2_movie *movie = record;
  qa_app_q2_travel_target_destroy(movie->next);
}
static uint64_t movie_request_of(qa_application *app, qa_app_shared_simulation *simulation, bool *found) {
  for (size_t i = 0; i < app->movie_request_count; i++)
    if (app->movie_request_simulations[i] == simulation) { *found = true; return app->movie_requests[i]; }
  *found = false;
  return 0;
}
/* TS current = () => !closed && !stopping && simulation === this.simulation && movieRequests.get(simulation) === request. */
static bool q2_movie_current(void *ctx) {
  q2_movie *movie = ctx;
  qa_application *app = movie->app;
  bool found = false;
  uint64_t request = movie_request_of(app, movie->simulation, &found);
  return !app->closed && !app->stopping && app->world_simulation == movie->simulation && found && request == movie->request;
}
static void q2_movie_continue(q2_movie *movie) {
  qa_application *app = movie->app;
  if (movie->next != NULL) {
    qa_app_q2_travel_target next = *movie->next;
    if (!movie->has_carry && movie->new_unit) next.new_unit = true;
    qa_application_advance_q2_travel(app, &next, movie->has_carry ? &movie->carry : NULL);
  } else qa_application_request_quit(app);
}
static void q2_movie_complete(void *ctx) {
  q2_movie *movie = ctx;
  if (!q2_movie_current(movie)) return;
  q2_movie_continue(movie);
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
static void movie_requests_set(qa_application *app, qa_app_shared_simulation *simulation, uint64_t request) {
  for (size_t i = 0; i < app->movie_request_count; i++)
    if (app->movie_request_simulations[i] == simulation) { app->movie_requests[i] = request; return; }
  grow((void **)&app->movie_request_simulations, &app->movie_request_capacity, app->movie_request_count + 1, sizeof *app->movie_request_simulations);
  app->movie_requests = checked(realloc(app->movie_requests, app->movie_request_capacity * sizeof *app->movie_requests));
  app->movie_request_simulations[app->movie_request_count] = simulation;
  app->movie_requests[app->movie_request_count++] = request;
}
static qa_application_movie *movie_of(qa_application *app, qa_app_shared_simulation *simulation) {
  for (size_t i = 0; i < app->movie_count; i++) if (app->movies[i].simulation == simulation) return &app->movies[i];
  return NULL;
}
static void movies_delete(qa_application *app, qa_app_shared_simulation *simulation) {
  qa_application_movie *movie = movie_of(app, simulation);
  if (movie == NULL) return;
  size_t index = (size_t)(movie - app->movies);
  memmove(&app->movies[index], &app->movies[index + 1], (app->movie_count - index - 1) * sizeof *app->movies);
  app->movie_count--;
}
static void movies_set(qa_application *app, const qa_application_movie *movie) {
  qa_application_movie *current = movie_of(app, movie->simulation);
  if (current != NULL) { *current = *movie; return; }
  grow((void **)&app->movies, &app->movie_capacity, app->movie_count + 1, sizeof *app->movies);
  app->movies[app->movie_count++] = *movie;
}

typedef struct q2_cinematic_try {
  qa_application *app;
  const qa_app_q2_travel_target *target;
  qa_application_screen_cinematic_captions *captions;
  q2_movie *movie;
  qa_seat_id seat;
  qa_app_campaign_cinematic *playback;
} q2_cinematic_try;
static void q2_cinematic_prepare(void *ctx) {
  q2_cinematic_try *t = ctx;
  qa_application_graphical *graphical = t->app->graphical;
  qa_app_screen_cinematic_request request = { t->target->name, false, false, false };
  t->playback = qa_app_campaign_cinematic_prepare_content(request, qa_app_loaded_content_mounts(t->app->loaded_content), graphical->assets, graphical->audio,
      graphical->renderer, t->seat, q2_movie_current, t->movie, t->captions);
}
void qa_application_advance_q2_travel(qa_application *app, const qa_app_q2_travel_target *target, const qa_app_simulation_travel *carry) {
  if (strcmp(target->kind, "map") == 0) {
    if (carry == NULL) {
      bool classic_or_rerelease = false;
      {
        qa_app_sim_classic_guest_world *classic = NULL;
        qa_app_sim_rerelease_guest_world *rerelease = NULL;
        classic_or_rerelease = qa_app_shared_sim_q2_native(app->world_simulation, &classic, &rerelease);
      }
      if (!classic_or_rerelease) qa_raise("Error", 0, 0, "Native campaign travel has no retained DLL");
      char *next_server = qa_app_q2_next_server_command(target);
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, next_server);
      qa_application_replace_world_options options;
      memset(&options, 0, sizeof options);
      options.native_travel = target;
      options.q2_next_server = next_server;
      qa_application_replace_world_with(app, target->name, NULL, &options);
      qa_cleanup_pop(&cleanup);
      free(next_server);
      return;
    }
    if (app->world_operation != QA_APPLICATION_IDLE) qa_raise("Error", 0, 0, "Another world operation is in progress");
    app->world_operation = QA_APPLICATION_TRAVEL;
    app->world_operation_completion = true;
    q2_checkpoint_run run;
    memset(&run, 0, sizeof run);
    run.app = app;
    qa_caught_error caught;
    bool ok = qa_try(q2_checkpoint_body, &run, &caught);
    app->world_operation = QA_APPLICATION_IDLE;
    app->world_operation_completion = false;
    if (!ok) qa_rethrow(&caught);
    qa_cleanup save_cleanup;
    qa_cleanup_push(&save_cleanup, save_free_cleanup, &run.save);
    qa_app_resource_request destination = { qa_app_loaded_content_recipe(app->loaded_content)->map.geometry_content, target->name };
    q2_visit visit = { app, qa_app_campaign_unit_stage(app->campaign_unit, destination, target->new_unit, &run.save.image), NULL, false };
    visit.next_server = qa_app_q2_next_server_command(target);
    qa_cleanup visit_cleanup;
    qa_cleanup_push(&visit_cleanup, q2_visit_cleanup, &visit);
    qa_app_simulation_travel spawned = *carry;
    spawned.spawn_point = target->spawn_point;
    qa_application_replace_world_options options;
    memset(&options, 0, sizeof options);
    options.save = qa_app_campaign_unit_visit_restore(visit.visit);
    options.published = q2_visit_published; options.published_ctx = &visit;
    options.q2_next_server = visit.next_server;
    qa_application_replace_world_with(app, target->name, &spawned, &options);
    qa_cleanup_pop(&visit_cleanup);
    q2_visit_cleanup(&visit);
    qa_cleanup_pop(&save_cleanup);
    qa_app_shared_sim_save_free(&run.save);
    return;
  }
  qa_application_graphical *graphical = app->graphical;
  bool has_seat = qa_application_local_player_count(app) != 0;
  qa_seat_id seat;
  memset(&seat, 0, sizeof seat);
  if (has_seat) seat = qa_session_seat_id(qa_application_local_player(app, 0).seat);
  if (graphical == NULL || !has_seat || app->network != NULL) qa_raise("Error", 0, 0, "Campaign cinematics require an offline local presentation");
  qa_app_shared_simulation *simulation = app->world_simulation;
  q2_movie *movie = checked(calloc(1, sizeof *movie));
  keep_record(app, movie, q2_movie_release);
  movie->app = app;
  movie->simulation = simulation;
  movie->request = qa_application_reserve_movie(app, simulation);
  movie->new_unit = target->new_unit;
  movie->has_carry = carry != NULL;
  if (carry != NULL) movie->carry = *carry;
  bool victory = strcmp(target->kind, "picture") == 0 && lower_equals(target->name, "victory.pcx")
      && app->launch_options.mode != NULL && strcmp(app->launch_options.mode, "coop") == 0;
  movie->next = victory ? qa_app_parse_q2_travel("*base1") : copy_target(target->next);
  q2_cinematic_try t;
  memset(&t, 0, sizeof t);
  t.app = app; t.target = target; t.movie = movie; t.seat = seat;
  t.captions = qa_application_screen_captions(app, simulation, app->loaded_content, seat);
  qa_caught_error caught;
  if (!qa_try(q2_cinematic_prepare, &t, &caught)) {
    /* A cinematic that cannot be played is skipped to the next stop, as
     * in the source engines -- never a blocked campaign transition. */
    if (!q2_movie_current(movie)) qa_rethrow(&caught);
    qa_application_printf(app, "Campaign cinematic unavailable: %s\n", caught.message);
    movie_requests_delete(app, simulation);
    q2_movie_continue(movie);
    return;
  }
  if (!q2_movie_current(movie)) {
    qa_app_campaign_cinematic_close(t.playback, app->presentation_frames);
    qa_raise("Error", 0, 0, "Campaign cinematic belongs to a retired world request");
  }
  qa_application_activate_movie(app, t.playback);
  qa_application_movie record;
  memset(&record, 0, sizeof record);
  record.simulation = simulation;
  record.playback = t.playback;
  record.request = movie->request;
  record.current = q2_movie_current;
  record.complete = q2_movie_complete;
  record.ctx = movie;
  movies_set(app, &record);
}

/* ---- activateMovie(playback) ---- */
typedef struct movie_close { qa_app_campaign_cinematic *playback; double sequence; } movie_close;
static void movie_activate_body(void *ctx) { qa_app_campaign_cinematic_activate(ctx); }
static void movie_close_body(void *ctx) {
  movie_close *close = ctx;
  qa_app_campaign_cinematic_close(close->playback, close->sequence);
}
void qa_application_activate_movie(qa_application *app, qa_app_campaign_cinematic *playback) {
  qa_caught_error error;
  if (qa_try(movie_activate_body, playback, &error)) return;
  movie_close close = { playback, app->presentation_frames };
  qa_caught_error cleanup;
  if (!qa_try(movie_close_body, &close, &cleanup))
    qa_raise("AggregateError", 0, 2, "Cinematic activation and cleanup failed");
  qa_rethrow(&error);
}

/* ---- reserveMovie(simulation) ---- */
uint64_t qa_application_reserve_movie(qa_application *app, qa_app_shared_simulation *simulation) {
  uint64_t request = ++app->next_movie_request;
  movie_requests_set(app, simulation, request);
  qa_application_movie *previous = movie_of(app, simulation);
  if (previous != NULL) {
    qa_app_campaign_cinematic *playback = previous->playback;
    movies_delete(app, simulation);
    qa_app_campaign_cinematic_close(playback, app->presentation_frames);
  }
  return request;
}

/* ---- screenCaptions(simulation, content, seat) ---- */
struct qa_application_screen_captions_record {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  const qa_content_mounted *mounts;
  qa_seat_id seat;
  qa_seat_media_captions captions;
  qa_application_screen_cinematic_captions view;
};
static uint8_t *captions_read(void *ctx, const char *path, size_t *out_length) {
  struct qa_application_screen_captions_record *record = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open((qa_content_mounted *)record->mounts, path, NULL, NULL, &opened)) { *out_length = 0; return NULL; }
  *out_length = opened.byte_length;
  return opened.bytes;
}
static const char *captions_language(void *ctx) {
  struct qa_application_screen_captions_record *record = ctx;
  if (record->app->graphical == NULL) return "english";
  return qa_app_rerelease_selected_language(record->app->graphical->rerelease, record->seat);
}
static void captions_failed(void *ctx, const char *error) {
  struct qa_application_screen_captions_record *record = ctx;
  size_t length = strlen(error) + 40;
  char *text = malloc(length);
  if (text == NULL) qa_range_error("Out of memory");
  snprintf(text, length, "Caption language reload failed: %s\n", error);
  qa_application_print(record->app, text);
  free(text);
}
static void captions_prepare(void *ctx, const char *source) {
  struct qa_application_screen_captions_record *record = ctx;
  if (!qa_seat_media_captions_prepare(&record->captions, source, captions_language(record))) qa_range_error("Out of memory");
}
static qa_render_command *captions_commands(void *ctx, const qa_application_cinematic_timeline *timeline, qa_rect viewport,
    size_t *out_count) {
  struct qa_application_screen_captions_record *record = ctx;
  qa_application *app = record->app;
  *out_count = 0;
  if (app->world_simulation != record->simulation || app->graphical == NULL) return NULL;
  qa_application_seat_presentation *presentation = NULL;
  for (size_t i = 0; i < app->graphical->presentation_count && presentation == NULL; i++)
    if (qa_same_seat(qa_session_seat_id(app->graphical->presentations[i].local->player.seat), record->seat))
      presentation = &app->graphical->presentations[i];
  if (presentation == NULL) return NULL;
  qa_ui_preference_values preferences = qa_ui_seat_preferences_values(qa_app_seat_ui_preferences(presentation->ui));
  qa_caption_preferences selected;
  memset(&selected, 0, sizeof selected);
  selected.subtitles = preferences.captions;
  selected.sound_captions = preferences.captions;
  selected.speakers = true;
  qa_active_caption *active = NULL;
  size_t active_count = 0;
  if (!qa_seat_media_captions_active(&record->captions, timeline->state, selected, &active, &active_count)) qa_range_error("Out of memory");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, active);
  qa_seat_client_state state = qa_app_world_seat_presentation_state(presentation->presentation);
  qa_ui_draw_context binding;
  memset(&binding, 0, sizeof binding);
  binding.binding = state.presentation;
  binding.binding.viewport = viewport;
  binding.binding.safe_area = viewport;
  binding.time_milliseconds = timeline->elapsed_milliseconds;
  qa_render_command *commands = qa_app_seat_ui_caption_commands(presentation->ui, active, active_count, binding, out_count);
  qa_cleanup_pop(&cleanup);
  free(active);
  return commands;
}
static void screen_captions_release(void *record) {
  struct qa_application_screen_captions_record *captions = record;
  qa_seat_media_captions_destroy(&captions->captions);
}
static qa_application_screen_cinematic_captions *screen_captions_for(qa_application *app, qa_app_shared_simulation *simulation,
    const qa_content_mounted *mounts, qa_seat_id seat) {
  struct qa_application_screen_captions_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, screen_captions_release);
  record->app = app;
  record->simulation = simulation;
  record->mounts = mounts;
  record->seat = seat;
  qa_live_language live = { record, captions_language, captions_failed };
  qa_seat_media_captions_init(&record->captions, seat, captions_read, record, NULL, false, &live);
  record->view.ctx = record;
  record->view.prepare = captions_prepare;
  record->view.commands = captions_commands;
  return &record->view;
}
qa_application_screen_cinematic_captions *qa_application_screen_captions(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, qa_seat_id seat) {
  return screen_captions_for(app, simulation, qa_app_loaded_content_mounts(content), seat);
}

/* ---- underwater(camera, actor) ---- */
bool qa_application_underwater(qa_application *app, qa_scene_camera camera, qa_actor_id actor) {
  const qa_executable_recipe *recipe = qa_app_loaded_content_recipe(app->loaded_content);
  const qa_provider_timing *timing = NULL;
  for (size_t i = 0; i < recipe->timing_count && timing == NULL; i++)
    if (strcmp(recipe->timing[i].provider, recipe->engine_behavior.provider) == 0) timing = &recipe->timing[i];
  if (timing == NULL) qa_raise("Error", 0, 0, "World has no numeric profile for camera contents");
  const char *kind = qa_app_loaded_content_world(app->loaded_content).kind;
  qa_point_contents_query query;
  memset(&query, 0, sizeof query);
  query.point = camera.origin;
  query.target.is_model = false;
  query.pass_actor = &actor;
  query.numeric = timing->numeric;
  if (strcmp(kind, "q1-bsp") == 0) {
    query.policy.kind = "q1";
    query.policy.move = "normal";
    query.policy.has_hull = false;
  } else if (strcmp(kind, "q2-bsp") == 0) {
    query.policy.kind = "q2";
    query.policy.contents_mask = -1;
    query.policy.leaf_contents = "merged";
  } else {
    query.policy.kind = "q3";
    query.policy.contents_mask = -1;
    query.policy.curves = true;
    query.policy.player_curve_clip = true;
  }
  qa_point_contents_result contents = qa_shared_point_contents(app->world_simulation->scene, query);
  if (strcmp(contents.kind, "q1") == 0) return contents.contents <= -3 && contents.contents >= -5;
  int32_t value = qa_js_to_int32(strcmp(contents.kind, "q2") == 0 ? contents.merged : contents.contents);
  return (value & 56) != 0;
}

/* ---- localViewport(seat, width, height) ---- */
static int compare_seat_index(const void *a, const void *b) {
  int64_t left = (*(qa_session_seat *const *)a == NULL) ? 0 : qa_session_seat_id(*(qa_session_seat *const *)a).index;
  int64_t right = (*(qa_session_seat *const *)b == NULL) ? 0 : qa_session_seat_id(*(qa_session_seat *const *)b).index;
  return left < right ? -1 : left > right;
}
qa_rect qa_application_local_viewport(qa_application *app, qa_seat_id seat, double width, double height) {
  qa_session_seat **seats = checked(calloc(app->local_seat_count + 1, sizeof *seats));
  if (app->local_seat_count != 0) memcpy(seats, app->local_seats, app->local_seat_count * sizeof *seats);
  qsort(seats, app->local_seat_count, sizeof *seats, compare_seat_index);
  double index = -1;
  for (size_t i = 0; i < app->local_seat_count; i++)
    if (qa_same_seat(qa_session_seat_id(seats[i]), seat)) { index = (double)i; break; }
  double count = (double)app->local_seat_count;
  free(seats);
  return qa_app_seat_viewport(index, count, width, height);
}

/* ---- retireRemovedLocalPlayers() ---- */
typedef struct retire_failures { size_t count; qa_caught_error first; } retire_failures;
static void retire_failure(retire_failures *failures, const qa_caught_error *error) {
  if (failures->count++ == 0) failures->first = *error;
}
typedef struct retire_call {
  qa_application *app;
  qa_application_graphical *graphical;
  qa_session_seat **seats; size_t seat_count;
  qa_app_viewing_seat *kept; size_t kept_count;
  qa_application_q3_seat_client source; bool has_source;
  qa_application_local_q3_guest_seat guest; bool has_guest;
  qa_session_client *client;
  qa_command_buffer *buffer;
  size_t index;
} retire_call;
static void retire_mod_presentations(void *ctx) {
  retire_call *c = ctx;
  qa_app_application_mod_presentations_retain_presentations(c->graphical->mod_presentations, c->kept, c->kept_count);
}
static void retire_selected_presentations(void *ctx) {
  retire_call *c = ctx;
  qa_app_selected_q3_presentations_retain(c->graphical->selected_q3_presentations, c->kept, c->kept_count);
}
static void retire_source_shutdown(void *ctx) {
  retire_call *c = ctx;
  if (c->has_source) qa_app_application_q3_client_shutdown(c->source.client);
}
static void retire_guest_state(void *ctx) {
  retire_call *c = ctx;
  if (c->has_guest) qa_app_local_q3_client_state_retire(c->guest.state);
}
static void retire_stop_recording(void *ctx) {
  retire_call *c = ctx;
  const char *error = NULL;
  qa_app_client_bootstrap *client = c->app->ownership.borrowed_client;
  /* TS client.stopRecording(this): the published application is the client's source.current. */
  if (client != NULL) error = client->stop_recording(client->stop_recording_ctx, client->source_current);
  else if (c->app->owned_recording != NULL) error = qa_app_client_demo_recording_stop(c->app->owned_recording);
  if (error != NULL) qa_raise("Error", 0, 0, "%s", error);
}
static void retire_input_seats(void *ctx) {
  retire_call *c = ctx;
  qa_app_application_input_retire_local_seats(c->graphical->input, c->seats, c->seat_count, c->app->ownership.borrowed_client);
}
static void retire_discard_client(void *ctx) {
  retire_call *c = ctx;
  qa_command_buffer_discard_client(c->buffer, qa_session_client_id(c->client));
}
static void retire_disconnect_rankings(void *ctx) {
  retire_call *c = ctx;
  qa_application_disconnect_rankings(c->app, c->app->world_simulation, qa_session_client_id(c->client));
}
static void retire_close_client(void *ctx) {
  retire_call *c = ctx;
  qa_engine_session_close_client(c->app->session, qa_session_client_id(c->client));
}
static void retire_publish_seats(void *ctx) {
  retire_call *c = ctx;
  size_t count = qa_app_application_input_local_count(c->graphical->input);
  qa_app_rerelease_seat *rows = checked(calloc(count + 1, sizeof *rows));
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, rows);
  for (size_t i = 0; i < count; i++) {
    qa_app_local_input *local = qa_app_application_input_local(c->graphical->input, i);
    rows[i].seat = qa_session_seat_id(local->player.seat);
    rows[i].actor = local->player.actor;
  }
  qa_app_rerelease_publish_seats(c->graphical->rerelease, rows, count);
  qa_cleanup_pop(&cleanup);
  free(rows);
}
static void retire_publish_layout(void *ctx) {
  retire_call *c = ctx;
  qa_app_world_seat_presentation_publish_layout(c->app->graphical->presentations[c->index].presentation, (double)c->index,
      (double)c->app->graphical->presentation_count);
}
static void retire_dispose(retire_failures *failures, void (*run)(void *ctx), retire_call *call) {
  qa_caught_error caught;
  if (!qa_try(run, call, &caught)) retire_failure(failures, &caught);
}
static bool seat_listed(qa_session_seat *const *seats, size_t count, qa_seat_id id) {
  for (size_t i = 0; i < count; i++) if (qa_same_seat(qa_session_seat_id(seats[i]), id)) return true;
  return false;
}
static bool request_kept(const void *record, void *ctx) {
  const qa_application_command_request *request = record;
  retire_call *call = ctx;
  if (!(request->has_seat && seat_listed(call->seats, call->seat_count, request->seat))) return true;
  /* The dropped request's storage is released with it (TS GC). */
  qa_application_command_request_free((qa_application_command_request *)record);
  return false;
}
/* Presentations hold their seat's q3 client by address; re-point them after the q3 rows moved. */
static void repoint_q3_clients(qa_application_graphical *graphical) {
  for (size_t i = 0; i < graphical->presentation_count; i++) {
    qa_application_seat_presentation *presentation = &graphical->presentations[i];
    if (presentation->q3_client == NULL) continue;
    presentation->q3_client = NULL;
    qa_seat_id id = qa_session_seat_id(presentation->local->player.seat);
    for (size_t j = 0; j < graphical->q3_count; j++)
      if (qa_same_seat(graphical->q3_seats[j], id)) presentation->q3_client = &graphical->q3[j];
  }
}
static size_t filter_input_events(qa_seat_input_event *events, size_t count, qa_session_seat *const *seats, size_t seat_count) {
  size_t kept = 0;
  for (size_t i = 0; i < count; i++)
    if (!seat_listed(seats, seat_count, events[i].seat)) events[kept++] = events[i];
  return kept;
}
void qa_application_retire_removed_local_players(qa_application *app) {
  qa_application_graphical *graphical = app->graphical;
  if (graphical == NULL) return;
  size_t locals = qa_app_application_input_local_count(graphical->input);
  qa_session_seat **seats = checked(calloc(locals + 1, sizeof *seats));
  size_t seat_count = 0;
  for (size_t i = 0; i < locals; i++) {
    qa_app_local_input *local = qa_app_application_input_local(graphical->input, i);
    if (!qa_session_actor_registry_is_live(qa_app_shared_sim_actors(app->world_simulation), local->player.actor))
      seats[seat_count++] = local->player.seat;
  }
  if (seat_count == 0) { free(seats); return; }
  qa_cleanup seats_cleanup;
  qa_cleanup_push(&seats_cleanup, free, seats);
  qa_session_client **clients = checked(calloc(seat_count + 1, sizeof *clients));
  qa_cleanup clients_cleanup;
  qa_cleanup_push(&clients_cleanup, free, clients);
  size_t client_count = 0;
  for (size_t i = 0; i < seat_count; i++) {
    qa_session_client *client = qa_session_seat_client(seats[i]);
    bool seen = false;
    for (size_t j = 0; j < client_count; j++) if (clients[j] == client) seen = true;
    if (!seen) clients[client_count++] = client;
  }
  /* Publish the reduced readers before resource disposal can invoke another host callback. */
  qa_application_seat_presentation *kept = checked(calloc(graphical->presentation_count + 1, sizeof *kept));
  size_t kept_count = 0;
  for (size_t i = 0; i < graphical->presentation_count; i++)
    if (!seat_listed(seats, seat_count, qa_session_seat_id(graphical->presentations[i].local->player.seat)))
      kept[kept_count++] = graphical->presentations[i];
  free(graphical->presentations);
  graphical->presentations = kept;
  graphical->presentation_count = kept_count;
  retire_failures failures;
  memset(&failures, 0, sizeof failures);
  retire_call call;
  memset(&call, 0, sizeof call);
  call.app = app; call.graphical = graphical; call.seats = seats; call.seat_count = seat_count;
  call.kept = viewing_seats(graphical); call.kept_count = kept_count;
  qa_cleanup kept_cleanup;
  qa_cleanup_push(&kept_cleanup, free, call.kept);
  retire_dispose(&failures, retire_mod_presentations, &call);
  retire_dispose(&failures, retire_selected_presentations, &call);
  for (size_t s = 0; s < seat_count; s++) {
    qa_seat_id id = qa_session_seat_id(seats[s]);
    call.has_source = false;
    for (size_t i = 0; i < graphical->q3_count; i++) {
      if (!qa_same_seat(graphical->q3_seats[i], id)) continue;
      call.source = graphical->q3[i]; call.has_source = true;
      memmove(&graphical->q3_seats[i], &graphical->q3_seats[i + 1], (graphical->q3_count - i - 1) * sizeof *graphical->q3_seats);
      memmove(&graphical->q3[i], &graphical->q3[i + 1], (graphical->q3_count - i - 1) * sizeof *graphical->q3);
      graphical->q3_count--;
      break;
    }
    for (size_t i = 0; i < graphical->native_q2_count; i++) {
      if (!qa_same_seat(graphical->native_q2_seats[i], id)) continue;
      memmove(&graphical->native_q2_seats[i], &graphical->native_q2_seats[i + 1], (graphical->native_q2_count - i - 1) * sizeof *graphical->native_q2_seats);
      memmove(&graphical->native_q2[i], &graphical->native_q2[i + 1], (graphical->native_q2_count - i - 1) * sizeof *graphical->native_q2);
      graphical->native_q2_count--;
      break;
    }
    retire_dispose(&failures, retire_source_shutdown, &call);
    call.has_guest = false;
    qa_application_local_q3_guest_world *guest_world = app->local_guest;
    for (size_t i = 0; guest_world != NULL && i < guest_world->seat_count; i++) {
      if (!qa_same_seat(guest_world->seat_ids[i], id)) continue;
      call.guest = guest_world->seats[i]; call.has_guest = true;
      memmove(&guest_world->seat_ids[i], &guest_world->seat_ids[i + 1], (guest_world->seat_count - i - 1) * sizeof *guest_world->seat_ids);
      memmove(&guest_world->seats[i], &guest_world->seats[i + 1], (guest_world->seat_count - i - 1) * sizeof *guest_world->seats);
      guest_world->seat_count--;
      break;
    }
    retire_dispose(&failures, retire_guest_state, &call);
    for (size_t i = 0; i < app->client_cvar_count; i++) {
      if (!qa_same_seat(app->client_cvar_seats[i], id)) continue;
      memmove(&app->client_cvar_seats[i], &app->client_cvar_seats[i + 1], (app->client_cvar_count - i - 1) * sizeof *app->client_cvar_seats);
      memmove(&app->client_cvars[i], &app->client_cvars[i + 1], (app->client_cvar_count - i - 1) * sizeof *app->client_cvars);
      app->client_cvar_count--;
      break;
    }
    for (size_t i = 0; i < app->ranking_menu_request_count; i++) {
      if (!qa_same_seat(app->ranking_menu_requests[i], id)) continue;
      memmove(&app->ranking_menu_requests[i], &app->ranking_menu_requests[i + 1], (app->ranking_menu_request_count - i - 1) * sizeof *app->ranking_menu_requests);
      app->ranking_menu_request_count--;
      break;
    }
  }
  repoint_q3_clients(graphical);
  bool recording_seat = false;
  for (size_t s = 0; s < seat_count; s++)
    if (app->has_recording_seat && qa_same_seat(app->recording_seat, qa_session_seat_id(seats[s]))) recording_seat = true;
  if (recording_seat) {
    retire_dispose(&failures, retire_stop_recording, &call);
    app->has_recording_seat = false;
    app->local_recording_active = false;
  }
  retire_dispose(&failures, retire_input_seats, &call);
  for (size_t c = 0; c < client_count; c++) {
    qa_client_id id = qa_session_client_id(clients[c]);
    for (size_t i = 0; i < app->local_seat_count; i++) {
      if (!qa_same_client(app->local_seat_clients[i], id)) continue;
      memmove(&app->local_seat_clients[i], &app->local_seat_clients[i + 1], (app->local_seat_count - i - 1) * sizeof *app->local_seat_clients);
      memmove(&app->local_seats[i], &app->local_seats[i + 1], (app->local_seat_count - i - 1) * sizeof *app->local_seats);
      app->local_seat_count--;
      break;
    }
    for (size_t i = 0; i < app->source_connection_count; i++) {
      if (app->source_connection_clients[i] != clients[c]) continue;
      memmove(&app->source_connection_clients[i], &app->source_connection_clients[i + 1],
          (app->source_connection_count - i - 1) * sizeof *app->source_connection_clients);
      memmove(&app->source_connections[i], &app->source_connections[i + 1], (app->source_connection_count - i - 1) * sizeof *app->source_connections);
      app->source_connection_count--;
      break;
    }
    call.client = clients[c];
    /* new Set([this.sourceCommands, this.dedicatedCommands]) */
    if (app->source_commands != NULL) { call.buffer = app->source_commands; retire_dispose(&failures, retire_discard_client, &call); }
    if (app->dedicated_commands != NULL && app->dedicated_commands != app->source_commands) {
      call.buffer = app->dedicated_commands;
      retire_dispose(&failures, retire_discard_client, &call);
    }
    retire_dispose(&failures, retire_disconnect_rankings, &call);
    retire_dispose(&failures, retire_close_client, &call);
  }
  qa_event_ring_filter_live(&app->requested_commands, request_kept, &call);
  app->client_input_count = filter_input_events(app->client_inputs, app->client_input_count, seats, seat_count);
  app->deferred_input_count = filter_input_events(app->deferred_input, app->deferred_input_count, seats, seat_count);
  retire_dispose(&failures, retire_publish_seats, &call);
  for (size_t i = 0; i < kept_count; i++) { call.index = i; retire_dispose(&failures, retire_publish_layout, &call); }
  if (kept_count == 0) qa_application_request_quit(app);
  else app->launch_options.seats = (double)kept_count;
  qa_cleanup_pop(&kept_cleanup);
  free(call.kept);
  qa_cleanup_pop(&clients_cleanup);
  free(clients);
  qa_cleanup_pop(&seats_cleanup);
  free(seats);
  if (failures.count != 0) qa_raise("AggregateError", 0, (long long)failures.count, "Retired local player cleanup failed");
}

/* ---- systemCinematics(simulation, content, assets, audio, renderer, seat, input, component?) ---- */
typedef struct cinematic_host_record {
  qa_application *app;
  qa_app_shared_simulation *simulation;
  qa_app_loaded_content *content;
  qa_app_application_assets *assets;
  qa_app_application_audio *audio;
  qa_app_native_renderer *renderer;
  qa_seat_id seat;
  qa_app_application_input *input;
  bool has_component; qa_app_component_cinematic_scope component;
  qa_app_system_cinematic_host host;
} cinematic_host_record;
typedef struct cinematic_handle_record {
  cinematic_host_record *owner;
  const char *phase; /* "playing" | "completed" | "stopped" */
  bool published;
  qa_app_campaign_cinematic *playback;
  uint64_t ticket;
  qa_app_local_input *local;
  qa_cvar_registry *cvars;
  bool (*consumer_current)(void *ctx); void *consumer_ctx;
  qa_application_screen_cinematic_captions *captions;
} cinematic_handle_record;
static bool local_admitted(qa_app_application_input *input, const qa_app_local_input *local) {
  for (size_t i = 0; i < qa_app_application_input_local_count(input); i++) if (qa_app_application_input_local(input, i) == local) return true;
  return false;
}
/* current = () => !closed && !stopping && consumerCurrent() && input.locals.includes(local) && (!published || movieRequests.get(simulation) === ticket). */
static bool handle_current(void *ctx) {
  cinematic_handle_record *h = ctx;
  qa_application *app = h->owner->app;
  if (app->closed || app->stopping || !h->consumer_current(h->consumer_ctx) || !local_admitted(h->owner->input, h->local)) return false;
  if (!h->published) return true;
  bool found = false;
  uint64_t request = movie_request_of(app, h->owner->simulation, &found);
  return found && request == h->ticket;
}
/* complete(). */
static void handle_complete(cinematic_handle_record *h) {
  qa_application *app = h->owner->app;
  if (strcmp(h->phase, "playing") != 0 || !handle_current(h) || app->world_simulation != h->owner->simulation) return;
  h->phase = "completed";
  char *next = h->cvars == NULL ? dup_text("") : qa_application_cvar_string(h->cvars, "nextmap");
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, free, next);
  if (next[0] != '\0') {
    size_t n = strlen(next) + 2;
    char *line = checked(malloc(n));
    (void)snprintf(line, n, "%s\n", next);
    qa_cleanup line_cleanup;
    qa_cleanup_push(&line_cleanup, free, line);
    if (h->owner->has_component) h->owner->component.append(h->owner->component.ctx, line);
    else {
      qa_command_origin caller;
      memset(&caller, 0, sizeof caller);
      caller.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
      caller.seat = h->owner->seat;
      caller.client = qa_session_client_id(qa_session_seat_client(h->local->player.seat));
      qa_command_context source;
      memset(&source, 0, sizeof source);
      source.session = qa_engine_session_id(app->session);
      source.origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
      source.origin.name = "cinematic";
      source.origin.caller = &caller;
      qa_app_application_input_enqueue_client_command(h->owner->input, line, &source);
    }
    if (h->cvars != NULL) qa_application_cvar_set(h->cvars, "nextmap", "", true);
    qa_cleanup_pop(&line_cleanup);
    free(line);
  }
  qa_cleanup_pop(&cleanup);
  free(next);
}
static void handle_movie_complete(void *ctx) { handle_complete(ctx); }
/* detach(). */
static void handle_detach(cinematic_handle_record *h) {
  qa_application *app = h->owner->app;
  qa_application_movie *movie = movie_of(app, h->owner->simulation);
  if (movie != NULL && movie->request == h->ticket) movies_delete(app, h->owner->simulation);
  bool found = false;
  uint64_t request = movie_request_of(app, h->owner->simulation, &found);
  if (found && request == h->ticket) movie_requests_delete(app, h->owner->simulation);
}
static void handle_refresh_status(void *ctx, qa_app_system_cinematic_handle *handle) {
  cinematic_handle_record *h = ctx;
  handle->status = strcmp(h->phase, "completed") == 0 ? "ended"
      : strcmp(h->phase, "stopped") == 0 || !handle_current(h) ? "stopped"
      : h->playback != NULL ? qa_app_campaign_cinematic_status(h->playback) : "stopped";
}
static void handle_skip_complete(void *ctx) { handle_complete(ctx); }
static void handle_skip(void *ctx) {
  cinematic_handle_record *h = ctx;
  if (strcmp(h->phase, "playing") != 0 || !handle_current(h)) return;
  if (h->playback != NULL) {
    qa_app_campaign_cinematic_skip(h->playback);
    qa_app_campaign_cinematic_close(h->playback, h->owner->app->presentation_frames);
  }
  qa_caught_error caught;
  bool ok = qa_try(handle_skip_complete, h, &caught);
  handle_detach(h);
  if (!ok) qa_rethrow(&caught);
}
static void handle_stop(void *ctx) {
  cinematic_handle_record *h = ctx;
  h->phase = "stopped";
  handle_detach(h);
  if (h->playback != NULL) qa_app_campaign_cinematic_close(h->playback, h->owner->app->presentation_frames);
}
static qa_save_value *handle_capture(void *ctx) {
  cinematic_handle_record *h = ctx;
  bool active = strcmp(h->phase, "playing") == 0 && handle_current(h);
  qa_save_value *value = checked(qa_save_value_object());
  bool ok = qa_save_value_set(value, "version", qa_save_value_number(1))
      && qa_save_value_set(value, "phase", qa_save_value_string(active ? "playing" : strcmp(h->phase, "completed") == 0 ? "completed" : "stopped"))
      && qa_save_value_set(value, "playback", !active ? qa_save_value_null()
          : h->playback != NULL ? qa_app_campaign_cinematic_capture_checkpoint(h->playback) : qa_save_value_undefined());
  if (!ok) qa_range_error("Out of memory");
  return value;
}
static void handle_publish_restored(void *ctx) {
  cinematic_handle_record *h = ctx;
  qa_application *app = h->owner->app;
  if (h->published) return;
  if (!handle_current(h)) qa_raise("Error", 0, 0, "Restored component movie has retired before publication");
  if (strcmp(h->phase, "playing") == 0) {
    qa_application_movie *movie = movie_of(app, h->owner->simulation);
    if (h->playback == NULL || movie == NULL || movie->request != h->ticket)
      qa_raise("Error", 0, 0, "Restored fullscreen cinematic lost its destination owner");
    movie_requests_set(app, h->owner->simulation, h->ticket);
    h->published = true;
    qa_application_activate_movie(app, h->playback);
  } else h->published = true;
}
/* resources = component === undefined ? content : { mounts: component.mounts }. */
static const qa_content_mounted *cinematic_mounts(const cinematic_host_record *owner) {
  return owner->has_component ? owner->component.mounts : qa_app_loaded_content_mounts(owner->content);
}
typedef struct cinematic_prepare_try {
  cinematic_handle_record *handle;
  qa_app_screen_cinematic_request request;
  const qa_save_value *saved_playback;
  bool restore;
} cinematic_prepare_try;
static void cinematic_prepare_body(void *ctx) {
  cinematic_prepare_try *t = ctx;
  cinematic_handle_record *h = t->handle;
  cinematic_host_record *owner = h->owner;
  if (t->restore)
    h->playback = qa_app_campaign_cinematic_restore_content(t->saved_playback,
        cinematic_mounts(owner), owner->assets, owner->audio,
        owner->renderer, owner->seat, handle_current, h, h->captions);
  else
    h->playback = qa_app_campaign_cinematic_prepare_content(t->request, cinematic_mounts(owner), owner->assets, owner->audio, owner->renderer,
        owner->seat, handle_current, h, h->captions);
}
/* TS prepare(request, consumerCurrent, saved?). */
static qa_app_system_cinematic_handle cinematic_prepare(cinematic_host_record *owner, const qa_app_q3_screen_cinematic_request *request,
    bool (*consumer_current)(void *ctx), void *consumer_ctx, const qa_save_reader *saved) {
  qa_application *app = owner->app;
  cinematic_handle_record *h = checked(calloc(1, sizeof *h));
  keep_record(app, h, NULL);
  h->owner = owner;
  h->consumer_current = consumer_current; h->consumer_ctx = consumer_ctx;
  h->phase = "playing";
  const qa_save_value *saved_playback = NULL;
  if (saved != NULL) {
    static const char *const phases[] = { "playing", "completed", "stopped" };
    qa_save_reader phase, playback;
    if (!qa_save_reader_field(saved, "phase", &phase) || !qa_save_reader_field(saved, "playback", &playback)) qa_range_error("Out of memory");
    h->phase = qa_save_reader_choice_string(&phase, phases, 3);
    for (size_t i = 0; i < 3; i++) if (strcmp(h->phase, phases[i]) == 0) h->phase = phases[i];
    saved_playback = playback.value;
    bool retained = !qa_save_reader_is_null(&playback);
    qa_save_reader_destroy(&phase); qa_save_reader_destroy(&playback);
    if (strcmp(h->phase, "playing") != 0 && retained) qa_save_reader_fail(saved, "terminal system cinematic retained an active decoder");
  }
  for (size_t i = 0; i < qa_app_application_input_local_count(owner->input) && h->local == NULL; i++) {
    qa_app_local_input *local = qa_app_application_input_local(owner->input, i);
    if (qa_same_seat(qa_session_seat_id(local->player.seat), owner->seat)) h->local = local;
  }
  if (h->local == NULL) qa_raise("Error", 0, 0, "Cinematic has no admitted viewing seat");
  h->cvars = owner->has_component ? owner->component.cvars : qa_application_source_cvars(app, owner->simulation);
  h->published = saved == NULL;
  h->ticket = saved == NULL ? qa_application_reserve_movie(app, owner->simulation) : ++app->next_movie_request;
  if (strcmp(h->phase, "playing") == 0) {
    bool unavailable = false;
    h->captions = screen_captions_for(app, owner->simulation, cinematic_mounts(owner), owner->seat);
    cinematic_prepare_try t;
    memset(&t, 0, sizeof t);
    t.handle = h;
    if (saved == NULL) {
      if (request == NULL) qa_raise("Error", 0, 0, "Missing system cinematic request");
      t.request.name = request->name; t.request.loop = request->loop; t.request.hold = request->hold; t.request.silent = request->silent;
      qa_caught_error caught;
      if (!qa_try(cinematic_prepare_body, &t, &caught)) {
        /* A cinematic that cannot be played completes immediately, as in the source engines -- never a stuck transition. */
        if (!handle_current(h)) qa_rethrow(&caught);
        qa_application_printf(app, "Cinematic unavailable: %s\n", caught.message);
        unavailable = true;
      }
    } else {
      t.restore = true;
      t.saved_playback = saved_playback;
      cinematic_prepare_body(&t);
    }
    if (unavailable) handle_complete(h);
    else if (h->playback != NULL) {
      if (!handle_current(h)) {
        qa_app_campaign_cinematic_close(h->playback, app->presentation_frames);
        qa_raise("Error", 0, 0, "System cinematic belongs to a retired destination request");
      }
      if (saved != NULL && movie_of(app, owner->simulation) != NULL) {
        qa_app_campaign_cinematic_close(h->playback, app->presentation_frames);
        qa_raise("Error", 0, 0, "Saved component cinematics have competing fullscreen owners");
      }
      qa_application_movie movie;
      memset(&movie, 0, sizeof movie);
      movie.simulation = owner->simulation;
      movie.playback = h->playback;
      movie.request = h->ticket;
      movie.current = handle_current;
      movie.complete = handle_movie_complete;
      movie.ctx = h;
      movie.component_owned = owner->has_component;
      if (h->published && app->world_simulation == owner->simulation) qa_application_activate_movie(app, h->playback);
      movies_set(app, &movie);
    }
  }
  qa_app_system_cinematic_handle handle;
  memset(&handle, 0, sizeof handle);
  handle.ctx = h;
  handle.refresh_status = handle_refresh_status;
  handle.skip = handle_skip;
  handle.stop = handle_stop;
  if (owner->has_component) {
    handle.capture_checkpoint = handle_capture;
    handle.publish_restored = handle_publish_restored;
  }
  handle_refresh_status(h, &handle);
  return handle;
}
static qa_app_system_cinematic_handle host_open(void *ctx, qa_app_q3_screen_cinematic_request request, bool (*current)(void *cctx),
    void *current_ctx) {
  return cinematic_prepare(ctx, &request, current, current_ctx, NULL);
}
static qa_app_system_cinematic_handle host_restore(void *ctx, const qa_save_value *value, bool (*current)(void *cctx), void *current_ctx) {
  qa_save_reader reader, version;
  if (!qa_save_reader_init(&reader, value, "component-system-cinematic") || !qa_save_reader_field(&reader, "version", &version))
    qa_range_error("Out of memory");
  (void)qa_save_reader_literal_number(&version, 1);
  qa_save_reader_destroy(&version);
  qa_cleanup cleanup;
  qa_cleanup_push(&cleanup, (void (*)(void *))qa_save_reader_destroy, &reader);
  qa_app_system_cinematic_handle handle = cinematic_prepare(ctx, NULL, current, current_ctx, &reader);
  qa_cleanup_pop(&cleanup);
  qa_save_reader_destroy(&reader);
  return handle;
}
const qa_app_system_cinematic_host *qa_application_system_cinematics(qa_application *app, qa_app_shared_simulation *simulation,
    qa_app_loaded_content *content, qa_app_application_assets *assets, qa_app_application_audio *audio,
    qa_app_native_renderer *renderer, qa_seat_id seat, qa_app_application_input *input,
    const qa_app_component_cinematic_scope *component) {
  cinematic_host_record *record = checked(calloc(1, sizeof *record));
  keep_record(app, record, NULL);
  record->app = app; record->simulation = simulation; record->content = content; record->assets = assets; record->audio = audio;
  record->renderer = renderer; record->seat = seat; record->input = input;
  record->has_component = component != NULL;
  if (component != NULL) record->component = *component;
  record->host.ctx = record;
  record->host.open = host_open;
  record->host.restore = component == NULL ? NULL : host_restore;
  return &record->host;
}
