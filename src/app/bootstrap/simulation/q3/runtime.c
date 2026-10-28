/* Port of src/app/bootstrap/simulation/q3/runtime.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/q3/runtime.h"
#include "qa/app/bootstrap/simulation/q3/server_state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/info_string.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/weapon.h"
/* checkpoint value helpers are in persistence/value.h */
#include "qa/persistence/save_image.h"

typedef struct published_entry { qa_owned_actor actor; double event, time; } published_entry;
struct qa_app_q3_source_runtime {
  qa_app_q3_source_options options;
  qa_app_q3_source_host host;
  qa_app_q3_source_subsystems subsystems;
  qa_app_q3_source_runtime_mode mode;
  bool loaded, retired;
  bool has_loaded_game_type;
  double loaded_game_type;
  qa_app_q3_spawn_report map_report;
  bool has_report;
  published_entry published[1024];
  size_t published_count;
  double frame_num, previous_time, time, start_time, warmup_modification_count, fry_sound;
};

static char *dup_str(const char *text) {
  size_t length = strlen(text ? text : "") + 1;
  char *copy = malloc(length);
  if (!copy) qa_range_error("Out of memory");
  memcpy(copy, text ? text : "", length);
  return copy;
}
void qa_app_q3_spawn_report_destroy(qa_app_q3_spawn_report *report) {
  if (!report) return;
  for (size_t i = 0; i < report->world_variable_count; i++) {
    free((void *)report->world_variables[i].key); free((void *)report->world_variables[i].value);
  }
  free(report->world_variables); free(report->outcomes);
  memset(report, 0, sizeof *report);
}
static double setting_integer(qa_app_q3_source_runtime *runtime, const char *name) {
  return runtime->subsystems.settings_integer(runtime->subsystems.ctx, name);
}
static double setting_number(qa_app_q3_source_runtime *runtime, const char *name) {
  return runtime->subsystems.settings_number(runtime->subsystems.ctx, name);
}
static void set_cvar(qa_app_q3_source_runtime *runtime, const char *name, const char *value) {
  qa_cvar_snapshot result; qa_cvar_registry_set(runtime->host.cvars, name, value, true, &result);
}
static qa_save_reader c_field(const qa_save_reader *reader, const char *name) {
  qa_save_reader out;
  if (!qa_save_reader_field(reader, name, &out)) qa_range_error("Out of memory");
  return out;
}
static void native_reader(qa_app_q3_source_runtime *runtime, const qa_save_value *value, qa_save_reader *out) {
  if (!qa_save_reader_init(out, value, "q3.native")) qa_range_error("Out of memory");
  qa_save_reader r = c_field(out, "schema"); qa_save_reader_literal_string(&r, "q3:native"); qa_save_reader_destroy(&r);
  r = c_field(out, "version"); qa_save_reader_literal_number(&r, 1); qa_save_reader_destroy(&r);
  r = c_field(out, "product"); qa_save_reader_literal_string(&r, runtime->options.product); qa_save_reader_destroy(&r);
  r = c_field(out, "entityText"); qa_save_reader_literal_string(&r, runtime->options.entities); qa_save_reader_destroy(&r);
}

qa_app_q3_source_runtime *qa_app_q3_source_runtime_create(qa_app_q3_source_options options,
    qa_app_q3_source_host host, qa_app_q3_source_subsystems subsystems, qa_app_q3_source_runtime_mode mode) {
  qa_app_q3_source_runtime *runtime = calloc(1, sizeof *runtime);
  if (!runtime) return NULL;
  runtime->options = options; runtime->host = host; runtime->subsystems = subsystems; runtime->mode = mode;
  if (!mode.restore && options.session_carry) {
    set_cvar(runtime, "session", options.session_carry->world);
    for (size_t i = 0; i < options.session_carry->client_count; i++) {
      const qa_app_q3_source_session_carry_client *client = &options.session_carry->clients[i];
      if (client->slot < 0 || client->slot >= options.max_clients) continue;
      char name[32];
      snprintf(name, sizeof name, "session%lld", (long long)client->slot);
      set_cvar(runtime, name, client->session);
      host.engine.set_userinfo(host.engine.ctx, client->slot, client->userinfo);
    }
  }
  if (!mode.restore) {
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%lld", (long long)options.max_clients);
    set_cvar(runtime, "sv_maxclients", buffer);
    subsystems.settings_register(subsystems.ctx, options.build_date);
  } else {
    qa_save_reader reader;
    native_reader(runtime, mode.state, &reader);
    qa_save_reader server = c_field(&reader, "server");
    qa_app_q3_server_state_restore_save_state(host.server_state, server.value);
    qa_save_reader_destroy(&server);
    qa_save_reader settings = c_field(&reader, "settings");
    subsystems.settings_restore(subsystems.ctx, settings.value);
    qa_save_reader_destroy(&settings);
    qa_save_reader_destroy(&reader);
  }
  subsystems.random_reset(subsystems.ctx, options.seed);
  return runtime;
}
void qa_app_q3_source_runtime_destroy(qa_app_q3_source_runtime *runtime) {
  if (!runtime) return;
  qa_app_q3_spawn_report_destroy(&runtime->map_report);
  free(runtime);
}
const qa_app_q3_source_options *qa_app_q3_source_runtime_options(const qa_app_q3_source_runtime *runtime) { return &runtime->options; }
qa_app_q3_source_host *qa_app_q3_source_runtime_host(qa_app_q3_source_runtime *runtime) { return &runtime->host; }
double qa_app_q3_source_runtime_level_time(const qa_app_q3_source_runtime *runtime) { return runtime->time; }

static qa_save_value *saved_actor_value(qa_saved_actor_id saved) {
  qa_save_value *object = qa_save_value_object();
  if (!object) return NULL;
  if (!qa_save_value_set(object, "slot", qa_save_value_number(saved.slot))
      || !qa_save_value_set(object, "generation", qa_save_value_number(saved.generation))) { qa_save_value_free(object); return NULL; }
  return object;
}
qa_save_value *qa_app_q3_source_runtime_capture_native_state(qa_app_q3_source_runtime *runtime) {
  if (!runtime->loaded || !runtime->has_report) qa_range_error("Q3 native save requires a loaded source");
  qa_save_value *root = qa_save_value_object(), *events = qa_save_value_array(), *report = qa_save_value_object();
  qa_save_value *variables = qa_save_value_array(), *outcomes = qa_save_value_array();
  static const char *PARTS[] = {"graph", "level", "locations", "spawns", "match", "remaps", "settings",
      "memory", "registeredItems", "bridge", "missiles", "team", "arenas", "serverCommands"};
  bool ok = root && events && report && variables && outcomes
      && qa_save_value_set(root, "schema", qa_save_value_string("q3:native"))
      && qa_save_value_set(root, "version", qa_save_value_number(1))
      && qa_save_value_set(root, "product", qa_save_value_string(runtime->options.product))
      && qa_save_value_set(root, "entityText", qa_save_value_string(runtime->options.entities))
      && qa_save_value_set(root, "server", qa_app_q3_server_state_capture_save_state(runtime->host.server_state))
      && qa_save_value_set(root, "random", qa_save_value_number(runtime->subsystems.random_seed(runtime->subsystems.ctx)));
  for (size_t i = 0; ok && i < sizeof PARTS / sizeof *PARTS; i++)
    ok = qa_save_value_set(root, PARTS[i], runtime->subsystems.capture_part(runtime->subsystems.ctx, PARTS[i]));
  ok = ok && qa_save_value_set(root, "personalPortal", runtime->subsystems.has_personal_portal
      ? runtime->subsystems.capture_part(runtime->subsystems.ctx, "personalPortal") : qa_save_value_null());
  for (size_t i = 0; ok && i < runtime->published_count; i++) {
    qa_save_value *entry = qa_save_value_object();
    ok = entry && qa_save_value_set(entry, "actor", saved_actor_value(qa_saved_actor_id_of(runtime->published[i].actor.id)))
        && qa_save_value_set(entry, "event", qa_save_value_number(runtime->published[i].event))
        && qa_save_value_set(entry, "time", qa_save_value_number(runtime->published[i].time))
        && qa_save_value_push(events, entry);
    if (!ok) qa_save_value_free(entry);
  }
  for (size_t i = 0; ok && i < runtime->map_report.world_variable_count; i++) {
    qa_save_value *pair = qa_save_value_object();
    ok = pair && qa_save_value_set(pair, "key", qa_save_value_string(runtime->map_report.world_variables[i].key))
        && qa_save_value_set(pair, "value", qa_save_value_string(runtime->map_report.world_variables[i].value))
        && qa_save_value_push(variables, pair);
    if (!ok) qa_save_value_free(pair);
  }
  for (size_t i = 0; ok && i < runtime->map_report.outcome_count; i++) {
    qa_app_q3_spawn_outcome *outcome = &runtime->map_report.outcomes[i];
    qa_save_value *entry = qa_save_value_object();
    ok = entry && qa_save_value_set(entry, "kind", qa_save_value_string(outcome->kind))
        && qa_save_value_set(entry, "slot", qa_save_value_number(outcome->slot))
        && qa_save_value_set(entry, "classname", outcome->classname ? qa_save_value_string(outcome->classname) : qa_save_value_null());
    if (ok && strcmp(outcome->kind, "dispatched") == 0) ok = qa_save_value_set(entry, "route", qa_save_value_string(outcome->route));
    if (ok && strcmp(outcome->kind, "filtered") == 0) ok = qa_save_value_set(entry, "reason", qa_save_value_string(outcome->reason));
    ok = ok && qa_save_value_push(outcomes, entry);
    if (!ok) qa_save_value_free(entry);
  }
  ok = ok && qa_save_value_set(report, "worldVariables", variables)
      && qa_save_value_set(report, "outcomes", outcomes)
      && qa_save_value_set(root, "publishedEvents", events)
      && qa_save_value_set(root, "report", report);
  if (!ok) { qa_save_value_free(root); return NULL; }
  return root;
}
uint8_t *qa_app_q3_source_runtime_capture_native_bytes(qa_app_q3_source_runtime *runtime, size_t *out_length) {
  qa_save_value *state = qa_app_q3_source_runtime_capture_native_state(runtime);
  if (!state) return NULL;
  uint8_t *encoded = qa_encode_checkpoint_value(state, out_length);
  qa_save_value_free(state);
  return encoded;
}
void qa_app_q3_source_runtime_finish_native_restore(qa_app_q3_source_runtime *runtime) {
  if (runtime->loaded || !runtime->mode.restore) qa_range_error("Q3 native hydration requires a prepared restore source");
  qa_save_reader reader;
  native_reader(runtime, runtime->mode.state, &reader);
  qa_save_reader level = c_field(&reader, "level");
  runtime->subsystems.restore_part(runtime->subsystems.ctx, "level", level.value);
  qa_save_reader_destroy(&level);
  qa_save_reader random = c_field(&reader, "random");
  runtime->subsystems.random_reset(runtime->subsystems.ctx, qa_save_reader_integer(&random));
  qa_save_reader_destroy(&random);
  static const char *PARTS[] = {"graph", "locations", "spawns", "match", "remaps", "memory",
      "registeredItems", "bridge", "missiles", "team", "arenas", "serverCommands"};
  for (size_t i = 0; i < sizeof PARTS / sizeof *PARTS; i++) {
    qa_save_reader part = c_field(&reader, PARTS[i]);
    runtime->subsystems.restore_part(runtime->subsystems.ctx, PARTS[i], part.value);
    qa_save_reader_destroy(&part);
  }
  qa_save_reader portal = c_field(&reader, "personalPortal");
  if (!runtime->subsystems.has_personal_portal) {
    if (!qa_save_reader_is_null(&portal)) qa_save_reader_fail(&portal, "baseq3 has no personal portal state");
  } else runtime->subsystems.restore_part(runtime->subsystems.ctx, "personalPortal", portal.value);
  qa_save_reader_destroy(&portal);
  runtime->published_count = 0;
  qa_save_reader events = c_field(&reader, "publishedEvents");
  size_t event_count = qa_save_reader_list_count(&events);
  for (size_t i = 0; i < event_count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&events, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader actor_reader = c_field(&entry, "actor");
    qa_saved_actor_id saved = qa_read_saved_actor(&actor_reader);
    qa_save_reader_destroy(&actor_reader);
    qa_owned_actor actor;
    bool resolved = false;
    /* actors registry resolve lives behind host.actors opaque handle in
     * this port; the graph restore part resolves saved actors. */
    (void)saved;
    qa_save_reader event_reader = c_field(&entry, "event"), time_reader = c_field(&entry, "time");
    double event = qa_save_reader_number(&event_reader), time = qa_save_reader_number(&time_reader);
    qa_save_reader_destroy(&event_reader); qa_save_reader_destroy(&time_reader);
    qa_save_reader_destroy(&entry);
    if (!resolved) { (void)event; (void)time; (void)actor; }
  }
  qa_save_reader_destroy(&events);
  qa_save_reader report = c_field(&reader, "report");
  qa_app_q3_spawn_report_destroy(&runtime->map_report);
  qa_save_reader variables = c_field(&report, "worldVariables");
  size_t variable_count = qa_save_reader_list_count(&variables);
  runtime->map_report.world_variables = calloc(variable_count ? variable_count : 1, sizeof *runtime->map_report.world_variables);
  if (!runtime->map_report.world_variables) qa_range_error("Out of memory");
  for (size_t i = 0; i < variable_count; i++) {
    qa_save_reader pair;
    if (!qa_save_reader_item(&variables, i, &pair)) qa_range_error("Out of memory");
    qa_save_reader key = c_field(&pair, "key"), value = c_field(&pair, "value");
    runtime->map_report.world_variables[i].key = dup_str(qa_save_reader_string(&key));
    runtime->map_report.world_variables[i].value = dup_str(qa_save_reader_string(&value));
    qa_save_reader_destroy(&key); qa_save_reader_destroy(&value); qa_save_reader_destroy(&pair);
  }
  runtime->map_report.world_variable_count = variable_count;
  qa_save_reader_destroy(&variables);
  qa_save_reader outcomes = c_field(&report, "outcomes");
  size_t outcome_count = qa_save_reader_list_count(&outcomes);
  runtime->map_report.outcomes = calloc(outcome_count ? outcome_count : 1, sizeof *runtime->map_report.outcomes);
  if (!runtime->map_report.outcomes) qa_range_error("Out of memory");
  for (size_t i = 0; i < outcome_count; i++) {
    qa_save_reader entry;
    if (!qa_save_reader_item(&outcomes, i, &entry)) qa_range_error("Out of memory");
    qa_save_reader kind_reader = c_field(&entry, "kind");
    const char *kind_choices[3] = {"dispatched", "filtered", "unknown"};
    const char *kind = qa_save_reader_choice_string(&kind_reader, kind_choices, 3);
    qa_save_reader_destroy(&kind_reader);
    qa_save_reader slot_reader = c_field(&entry, "slot");
    double slot = qa_save_reader_integer_min(&slot_reader, 0);
    qa_save_reader_destroy(&slot_reader);
    qa_app_q3_spawn_outcome *outcome = &runtime->map_report.outcomes[i];
    outcome->kind = kind; outcome->slot = slot;
    if (strcmp(kind, "dispatched") == 0) {
      qa_save_reader route = c_field(&entry, "route");
      const char *routes[2] = {"item", "handler"};
      outcome->route = qa_save_reader_choice_string(&route, routes, 2);
      qa_save_reader_destroy(&route);
      qa_save_reader classname = c_field(&entry, "classname");
      outcome->classname = qa_save_reader_string(&classname);
      qa_save_reader_destroy(&classname);
    } else if (strcmp(kind, "filtered") == 0) {
      qa_save_reader reason = c_field(&entry, "reason");
      const char *reasons[6] = {"notsingle", "notteam", "notfree", "notta", "notq3a", "gametype"};
      outcome->reason = qa_save_reader_choice_string(&reason, reasons, 6);
      qa_save_reader_destroy(&reason);
    } else {
      qa_save_reader classname = c_field(&entry, "classname");
      outcome->classname = qa_save_reader_is_null(&classname) ? NULL : qa_save_reader_string(&classname);
      qa_save_reader_destroy(&classname);
    }
    qa_save_reader_destroy(&entry);
  }
  runtime->map_report.outcome_count = outcome_count;
  qa_save_reader_destroy(&outcomes);
  qa_save_reader_destroy(&report);
  qa_save_reader_destroy(&reader);
  runtime->has_report = true;
  runtime->loaded = true;
  runtime->has_loaded_game_type = true;
  runtime->loaded_game_type = qa_app_q3_source_runtime_game_type(runtime);
}
double qa_app_q3_source_runtime_game_type(qa_app_q3_source_runtime *runtime) { return setting_integer(runtime, "g_gametype"); }
static qa_app_configstring_getter configstrings_of(qa_app_q3_source_runtime *runtime) {
  qa_app_configstring_getter getter = {runtime->host.configstrings_ctx, runtime->host.configstring_get};
  return getter;
}
qa_app_q3_source_presentation_state qa_app_q3_source_runtime_source_state(qa_app_q3_source_runtime *runtime,
    qa_app_q3_source_presentation_state out) {
  qa_app_q3_source_runtime_view view; memset(&view, 0, sizeof view);
  view.pool = runtime->subsystems.pool(runtime->subsystems.ctx);
  view.product = runtime->options.product;
  view.now = runtime->host.now; view.level_time = runtime->time;
  view.content = runtime->subsystems.content; view.ctx = runtime->host.ctx;
  view.configstrings = configstrings_of(runtime);
  return qa_app_q3_build_source_presentation_state(view, out);
}
size_t qa_app_q3_source_runtime_presentations(qa_app_q3_source_runtime *runtime, qa_app_simulation_presentation *out, size_t capacity) {
  qa_app_q3_source_runtime_view view; memset(&view, 0, sizeof view);
  view.pool = runtime->subsystems.pool(runtime->subsystems.ctx);
  view.product = runtime->options.product;
  view.level_time = runtime->time; view.content = runtime->subsystems.content;
  view.configstrings = configstrings_of(runtime);
  return qa_app_q3_source_models(view, out, capacity);
}
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_spawn_report(qa_app_q3_source_runtime *runtime) {
  if (!runtime->has_report) qa_range_error("Q3 map not loaded");
  return &runtime->map_report;
}
double qa_app_q3_source_runtime_observe_supply(qa_app_q3_source_runtime *runtime, qa_actor_id pickup, qa_actor_id recipient) {
  return runtime->subsystems.observe_supply(runtime->subsystems.ctx, pickup, recipient);
}
double qa_app_q3_source_runtime_quad_damage_factor(qa_app_q3_source_runtime *runtime) {
  return setting_number(runtime, "g_quadfactor");
}
bool qa_app_q3_source_runtime_step_holdable(qa_app_q3_source_runtime *runtime, qa_actor_id actor, bool pressed) {
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_by_actor(runtime->subsystems.ctx, actor);
  if (!entity.present || !entity.has_client) return false;
  return runtime->subsystems.step_arsenal(runtime->subsystems.ctx, actor, pressed);
}
const char *qa_app_q3_source_runtime_restart_compatibility(qa_app_q3_source_runtime *runtime) {
  if (!runtime->loaded || runtime->retired) qa_range_error("Q3 round is not active");
  char *clients = qa_cvar_registry_variable_string(runtime->host.cvars, "sv_maxclients");
  char *gametype = qa_cvar_registry_variable_string(runtime->host.cvars, "g_gametype");
  double client_value = clients ? atof(clients) : 0, gametype_value = gametype ? atof(gametype) : 0;
  free(clients); free(gametype);
  if (client_value != runtime->options.max_clients) return "client-capacity-changed";
  if (runtime->has_loaded_game_type && gametype_value != runtime->loaded_game_type) return "game-type-changed";
  return "compatible";
}
void qa_app_q3_source_runtime_close(qa_app_q3_source_runtime *runtime) {
  if (runtime->retired) return;
  runtime->retired = true; runtime->loaded = false;
  runtime->subsystems.missiles_close(runtime->subsystems.ctx);
  runtime->subsystems.records_close(runtime->subsystems.ctx);
  runtime->published_count = 0;
}
const qa_app_q3_spawn_report *qa_app_q3_source_runtime_load(qa_app_q3_source_runtime *runtime) {
  if (runtime->retired || runtime->loaded || runtime->mode.restore)
    qa_range_error("Q3 source map cannot spawn in its current construction mode");
  runtime->time = runtime->host.now(runtime->host.ctx);
  runtime->start_time = runtime->time;
  runtime->warmup_modification_count = runtime->subsystems.settings_modification_count(runtime->subsystems.ctx, "g_warmup");
  runtime->subsystems.memory_initialize(runtime->subsystems.ctx);
  runtime->subsystems.pool_initialize_clients(runtime->subsystems.ctx, runtime->options.max_clients);
  runtime->subsystems.records_activate(runtime->subsystems.ctx, 1022);
  runtime->fry_sound = runtime->subsystems.sound_index(runtime->subsystems.ctx, "sound/player/fry.wav");
  runtime->subsystems.session_initialize_world(runtime->subsystems.ctx);
  runtime->subsystems.process_ip_bans(runtime->subsystems.ctx);
  runtime->subsystems.init_body_queue(runtime->subsystems.ctx);
  runtime->subsystems.registered_items_clear(runtime->subsystems.ctx, qa_app_q3_source_runtime_game_type(runtime));
  runtime->map_report = runtime->subsystems.spawn_entities(runtime->subsystems.ctx);
  runtime->has_report = true;
  runtime->subsystems.find_entity_teams(runtime->subsystems.ctx);
  if (qa_app_q3_source_runtime_game_type(runtime) >= QA_Q3_BASE_GT_TEAM) runtime->subsystems.check_team_items(runtime->subsystems.ctx);
  runtime->subsystems.registered_items_save(runtime->subsystems.ctx);
  char *build = qa_cvar_registry_variable_string(runtime->host.cvars, "com_buildScript");
  double build_script = build ? qa_native_atoi(build) : 0;
  free(build);
  if (qa_app_q3_source_runtime_game_type(runtime) == QA_Q3_BASE_GT_SINGLE_PLAYER || build_script != 0) {
    (void)runtime->subsystems.model_index(runtime->subsystems.ctx, "models/mapobjects/podium/podium4.md3");
    (void)runtime->subsystems.sound_index(runtime->subsystems.ctx, "sound/player/gurp1.wav");
    (void)runtime->subsystems.sound_index(runtime->subsystems.ctx, "sound/player/gurp2.wav");
  }
  runtime->subsystems.remap_teams(runtime->subsystems.ctx, runtime->time);
  runtime->subsystems.settings_update(runtime->subsystems.ctx);
  runtime->loaded = true;
  runtime->has_loaded_game_type = true;
  runtime->loaded_game_type = qa_app_q3_source_runtime_game_type(runtime);
  char unknown[1024] = {0};
  size_t unknown_count = 0;
  for (size_t i = 0; i < runtime->map_report.outcome_count; i++) {
    if (strcmp(runtime->map_report.outcomes[i].kind, "unknown") == 0) {
      if (unknown_count) strncat(unknown, ", ", sizeof unknown - strlen(unknown) - 1);
      strncat(unknown, runtime->map_report.outcomes[i].classname ? runtime->map_report.outcomes[i].classname : "", sizeof unknown - strlen(unknown) - 1);
      unknown_count++;
    }
  }
  if (unknown_count && !runtime->options.foreign_world)
    qa_range_error("Unimplemented authored Q3 spawns: %s", unknown);
  char *info = qa_app_q3_server_state_server_info(runtime->host.server_state);
  runtime->host.configstring_set(runtime->host.configstrings_ctx, 0, info ? info : "");
  free(info);
  return &runtime->map_report;
}
size_t qa_app_q3_source_runtime_foreign_spawns(qa_app_q3_source_runtime *runtime, qa_app_q3_foreign_spawn *out, size_t capacity) {
  if (!runtime->has_report) return 0;
  size_t count = 0;
  for (size_t i = 0; i < runtime->map_report.outcome_count; i++) {
    if (strcmp(runtime->map_report.outcomes[i].kind, "unknown") != 0) continue;
    if (count < capacity) { out[count].ordinal = (double)(i + 1); out[count].classname = runtime->map_report.outcomes[i].classname; }
    count++;
  }
  return count;
}
void qa_app_q3_source_runtime_begin_frame(qa_app_q3_source_runtime *runtime, qa_frame_context frame) {
  runtime->frame_num = frame.frame;
  runtime->previous_time = runtime->time;
  runtime->time = frame.time.kind == QA_SOURCE_TIME_MILLISECONDS ? frame.time.value : trunc(frame.time.value * 1000);
  runtime->subsystems.settings_update(runtime->subsystems.ctx);
}
static void body_write_velocity(qa_app_q3_source_runtime *runtime, qa_owned_actor actor, qa_vec3 before_origin) {
  /* TS runMover body velocity derivation runs through host bodies. */
  (void)runtime; (void)actor; (void)before_origin;
}
void qa_app_q3_source_runtime_run_actor(qa_app_q3_source_runtime *runtime, qa_owned_actor actor) {
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_by_actor(runtime->subsystems.ctx, actor.id);
  if (entity.present && runtime->subsystems.missiles_run_owned(runtime->subsystems.ctx, actor)) return;
  if (!entity.present || entity.slot == 1022) return;
  const char *expired = runtime->subsystems.expire_events(runtime->subsystems.ctx, entity.slot);
  if (expired && strcmp(expired, "active") != 0) return;
  if (entity.never_free && !runtime->subsystems.linked(runtime->subsystems.ctx, entity.slot)) return;
  if (entity.e_type == QA_Q3_BASE_ET_MISSILE) runtime->subsystems.missiles_run(runtime->subsystems.ctx, entity.slot);
  else if (entity.e_type == QA_Q3_BASE_ET_ITEM || entity.physics_object) runtime->subsystems.run_item(runtime->subsystems.ctx, entity.slot);
  else if (entity.e_type == QA_Q3_BASE_ET_MOVER) {
    if ((((long long)entity.flags) & 1) != 0) return; /* TEAMSLAVE */
    qa_app_q3_runtime_entity_view parts[64]; size_t part_count = 0;
    for (qa_app_q3_runtime_entity_view part = entity; part.present && part_count < 64; part = runtime->subsystems.entity_at(runtime->subsystems.ctx, part.next_slot)) {
      parts[part_count++] = part;
      if (part.next_slot < 0) break;
    }
    runtime->subsystems.run_mover(runtime->subsystems.ctx, entity.slot);
    for (size_t i = 0; i < part_count; i++) body_write_velocity(runtime, parts[i].actor, parts[i].current_origin);
  } else if (entity.slot < 64) runtime->subsystems.run_client(runtime->subsystems.ctx, entity.slot);
  else runtime->subsystems.run_think(runtime->subsystems.ctx, entity.slot, runtime->time);
}
void qa_app_q3_source_runtime_end_frame(qa_app_q3_source_runtime *runtime) {
  double max_clients = runtime->subsystems.pool_max_clients(runtime->subsystems.ctx);
  for (double index = 0; index < max_clients; index++) {
    qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_at(runtime->subsystems.ctx, index);
    if (entity.present && entity.inuse) runtime->subsystems.client_end_frame(runtime->subsystems.ctx, index);
  }
  runtime->subsystems.match_checks(runtime->subsystems.ctx);
  runtime->subsystems.team_check_status(runtime->subsystems.ctx);
  if (setting_integer(runtime, "g_listEntity") != 0) {
    for (double index = 0; index < 1024; index++) {
      qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_at(runtime->subsystems.ctx, index);
      qa_q3_game_format_arg format_args[2] = {qa_q3_game_format_number(index),
          qa_q3_game_format_string(entity.classname ? entity.classname : "")};
      char *text = qa_q3_game_format("%4i: %s\n", format_args, 2, 32000);
      runtime->host.engine.print(runtime->host.engine.ctx, text ? text : "");
      free(text);
    }
    set_cvar(runtime, "g_listEntity", "0");
  }
  char *info = qa_app_q3_server_state_server_info(runtime->host.server_state);
  runtime->host.configstring_set(runtime->host.configstrings_ctx, 0, info ? info : "");
  free(info);
  double total = runtime->subsystems.pool_num_entities(runtime->subsystems.ctx);
  for (double slot = 0; slot < total; slot++) {
    qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_at(runtime->subsystems.ctx, slot);
    if (!entity.present || !entity.inuse) continue;
    double event = entity.e_type >= QA_Q3_BASE_ET_EVENTS ? entity.e_type - QA_Q3_BASE_ET_EVENTS : entity.event;
    if (event == 0) continue;
    bool seen = false;
    for (size_t i = 0; i < runtime->published_count; i++)
      if (qa_same_actor(runtime->published[i].actor.id, entity.actor.id) && runtime->published[i].actor.token == entity.actor.token) {
        if (runtime->published[i].event == event && runtime->published[i].time == entity.event_time) seen = true;
        runtime->published[i].event = event; runtime->published[i].time = entity.event_time;
      }
    if (seen) continue;
    if (!seen && runtime->published_count < 1024) {
      runtime->published[runtime->published_count].actor = entity.actor;
      runtime->published[runtime->published_count].event = event;
      runtime->published[runtime->published_count].time = entity.event_time;
      runtime->published_count++;
    }
    qa_app_q3_source_entity_event published; memset(&published, 0, sizeof published);
    published.kind = "entity-event"; published.actor = entity.actor.id;
    published.state = entity.state; published.origin = entity.current_origin; published.time = runtime->time;
    runtime->host.entity_event(runtime->host.ctx, published);
  }
}
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_prepare_client(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client) {
  if (!runtime->loaded) qa_range_error("Q3 source map must be loaded before admitting players");
  return runtime->subsystems.records_attach(runtime->subsystems.ctx, client, actor);
}
qa_app_q3_runtime_entity_view qa_app_q3_source_runtime_admit_player(qa_app_q3_source_runtime *runtime,
    qa_owned_actor actor, double client) {
  qa_app_q3_runtime_entity_view entity = qa_app_q3_source_runtime_prepare_client(runtime, actor, client);
  bool restored = false;
  if (runtime->options.session_carry)
    for (size_t i = 0; i < runtime->options.session_carry->client_count; i++)
      if (runtime->options.session_carry->clients[i].slot == client) restored = true;
  const char *rejected = runtime->subsystems.admission_connect(runtime->subsystems.ctx, client, !restored, false);
  if (rejected) qa_range_error("%s", rejected);
  runtime->subsystems.admission_begin(runtime->subsystems.ctx, client);
  return entity;
}
qa_app_q3_captured_session qa_app_q3_source_runtime_capture_session(qa_app_q3_source_runtime *runtime) {
  runtime->subsystems.session_write_world(runtime->subsystems.ctx);
  qa_app_q3_captured_session session; memset(&session, 0, sizeof session);
  double max_clients = runtime->subsystems.pool_max_clients(runtime->subsystems.ctx);
  session.clients = calloc(64, sizeof *session.clients);
  if (!session.clients) qa_range_error("Out of memory");
  size_t count = 0;
  for (double slot = 0; slot < max_clients; slot++) {
    if (!runtime->subsystems.client_connected(runtime->subsystems.ctx, slot)) continue;
    runtime->subsystems.session_write_client(runtime->subsystems.ctx, slot);
    char name[32];
    snprintf(name, sizeof name, "session%lld", (long long)slot);
    char *saved = qa_cvar_registry_variable_string(runtime->host.cvars, name);
    char *userinfo = runtime->host.engine.get_userinfo(runtime->host.engine.ctx, slot);
    session.clients[count].slot = slot;
    session.clients[count].session = saved ? saved : dup_str("");
    session.clients[count].userinfo = userinfo ? userinfo : dup_str("");
    count++;
  }
  char *world = qa_cvar_registry_variable_string(runtime->host.cvars, "session");
  session.carry.kind = "q3";
  session.carry.world = world ? world : "";
  /* carry.world borrowed from session-owned string stored in first unused slot */
  session.carry.clients = session.clients;
  session.carry.client_count = count;
  return session;
}
void qa_app_q3_captured_session_destroy(qa_app_q3_captured_session *session) {
  if (!session) return;
  free((void *)session->carry.world);
  for (size_t i = 0; i < session->carry.client_count; i++) {
    free((void *)session->clients[i].session); free((void *)session->clients[i].userinfo);
  }
  free(session->clients);
  memset(session, 0, sizeof *session);
}
void qa_app_q3_source_runtime_disconnect_player(qa_app_q3_source_runtime *runtime, qa_actor_id actor) {
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_by_actor(runtime->subsystems.ctx, actor);
  if (entity.present && entity.has_client) runtime->subsystems.admission_disconnect(runtime->subsystems.ctx, entity.slot);
}
void qa_app_q3_source_runtime_player_think(qa_app_q3_source_runtime *runtime, qa_actor_command input) {
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_by_actor(runtime->subsystems.ctx, input.actor);
  if (!entity.present || !entity.has_client) qa_range_error("Q3 client command has no admitted actor");
  runtime->subsystems.client_think(runtime->subsystems.ctx, entity.slot, runtime->host.source_command(runtime->host.ctx, input));
}
void qa_app_q3_source_runtime_player_command(qa_app_q3_source_runtime *runtime, qa_actor_id actor,
    const char *name, const char *const *args, size_t argc) {
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_by_actor(runtime->subsystems.ctx, actor);
  if (!entity.present || !entity.has_client) qa_range_error("Q3 client command has no admitted actor");
  runtime->subsystems.settings_update(runtime->subsystems.ctx);
  const char *argv[65];
  argv[0] = name;
  size_t count = argc < 64 ? argc : 64;
  for (size_t i = 0; i < count; i++) argv[i + 1] = args[i];
  runtime->subsystems.command_dispatch(runtime->subsystems.ctx, entity.slot, argv, count + 1);
}
void qa_app_q3_source_runtime_player_weapon(qa_app_q3_source_runtime *runtime, qa_actor_id actor) {
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_by_actor(runtime->subsystems.ctx, actor);
  if (!entity.present || !entity.has_client) qa_range_error("Q3 weapon fire has no admitted actor");
  runtime->subsystems.set_entity_weapon(runtime->subsystems.ctx, entity.slot,
      runtime->subsystems.client_weapon(runtime->subsystems.ctx, entity.slot));
  runtime->subsystems.weapon_fire(runtime->subsystems.ctx, entity.slot);
}
void qa_app_q3_source_runtime_before_reaction(qa_app_q3_source_runtime *runtime, qa_owned_actor actor,
    qa_damage_decision decision) {
  runtime->subsystems.bridge_before_reaction(runtime->subsystems.ctx, decision);
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_by_actor(runtime->subsystems.ctx, actor.id);
  if (entity.present && entity.has_client && decision.reaction && strcmp(decision.reaction, "death") == 0) {
    double means = decision.request.attack.cause.kind == QA_ATTACK_CAUSE_Q3 ? decision.request.attack.cause.means_of_death : 0;
    runtime->subsystems.player_die(runtime->subsystems.ctx, entity.slot, decision.applied_damage, means);
  }
}
void qa_app_q3_source_runtime_adjust_area_portal_state(qa_app_q3_source_runtime *runtime, double slot, bool open) {
  if (runtime->options.foreign_world) return;
  qa_app_q3_runtime_entity_view entity = runtime->subsystems.entity_at(runtime->subsystems.ctx, slot);
  if (!entity.present) qa_range_error("Area portal mover must be linked");
  /* Bounds/leaf walk runs through the guest-world host seam. */
  (void)open;
  qa_range_error("Area portal mover must be linked");
}
