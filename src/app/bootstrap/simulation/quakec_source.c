/* Port of src/app/bootstrap/simulation/quakec-source.ts. GPL-2.0-or-later.
 * TS `throw new Error` raises "Error" through qa_raise; TS try/finally
 * restores ride qa_cleanup nodes so a caught raise unwinds them. */
#include "qa/text/localization_key.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/powerup_timers.h"
#include "qa/app/bootstrap/simulation/quakec_map.h"
#include "qa/app/bootstrap/simulation/quakeworld_cvars.h"
#include "qa/audio/wav.h"
#include "qa/compat/qc/actor_state.h"
#include "qa/compat/qc/borrowed_actors.h"
#include "qa/compat/qc/builtins.h"
#include "qa/compat/qc/client_host.h"
#include "qa/compat/qc/entity_host.h"
#include "qa/compat/qc/executor.h"
#include "qa/compat/qc/machine.h"
#include "qa/compat/qc/mod_combat.h"
#include "qa/compat/qc/movement_host.h"
#include "qa/compat/qc/profile.h"
#include "qa/compat/qc/pusher_host.h"
#include "qa/compat/qc/save.h"
#include "qa/compat/qc/source_call.h"
#include "qa/compat/qc/world_host.h"
#include "qa/content/composition/q1/types.h"
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q1/foundation/weapon_names.h"
#include "qa/content/q1/quakec/armor_points.h"
#include "qa/content/q1/quakec/id1_attacks.h"
#include "qa/content/q1/quakec/id1_program.h"
#include "qa/content/q1/quakec/id1_projectiles.h"
#include "qa/content/q1/quakec/pickup_callers.h"
#include "qa/core/common_parse.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/formats/q12_model.h"
#include "qa/formats/q1_map.h"
#include "qa/foundation/event_ring.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q1/water_transition.h"
#include "qa/network/common/js_number.h"
#include "qa/compat/qc/q1_save.h"
#include "qa/compat/qc/q1_source_save.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/value.h"
#include "qa/world/actors/source_slots.h"
#include "qa/world/scheduler.h"

typedef qa_app_sim_quakec_source S;

/* ---------------- small helpers ---------------- */
static noreturn void fail(const char *format, ...) {
  char message[1024];
  va_list args; va_start(args, format);
  (void)vsnprintf(message, sizeof message, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *xalloc(size_t size) {
  void *p = calloc(1, size ? size : 1);
  if (!p) qa_range_error("Out of memory");
  return p;
}
static void *xgrow(void *p, size_t *capacity, size_t need, size_t elem) {
  if (need <= *capacity) return p;
  size_t next = *capacity ? *capacity * 2 : 8;
  while (next < need) next *= 2;
  void *q = realloc(p, next * elem);
  if (!q) qa_range_error("Out of memory");
  memset((char *)q + *capacity * elem, 0, (next - *capacity) * elem);
  *capacity = next;
  return q;
}
static char *xstrdup(const char *text) {
  size_t n = strlen(text ? text : "") + 1;
  char *p = xalloc(n);
  memcpy(p, text ? text : "", n);
  return p;
}
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }
static bool starts(const char *a, const char *prefix) { return a != NULL && strncmp(a, prefix, strlen(prefix)) == 0; }
static bool ends(const char *a, const char *suffix) {
  size_t n = strlen(a), m = strlen(suffix);
  return n >= m && strcmp(a + n - m, suffix) == 0;
}
static bool same_actor(qa_actor_id a, qa_actor_id b) { return qa_same_actor(a, b); }
static const qa_resolved_resource_reference *artifact_of(const qa_execution_module *m) { return m->artifact; }
static bool same_owned(qa_owned_actor a, qa_owned_actor b) { return qa_same_owned_actor(a, b); }
/* Math.fround: IEC 60559 binary64 -> binary32 conversion (Annex F). */
static double fround(double value) { return (double)(float)value; }
static double seconds_of(qa_source_time t) { return t.kind == QA_SOURCE_TIME_SECONDS ? t.value : t.value / 1000; }

/* ---------------- program view ---------------- */
bool qa_app_sim_quakec_program_view_init(qa_app_sim_quakec_program_view *v, const qa_compat_qc_program *p) {
  memset(v, 0, sizeof *v);
  v->globals = calloc(p->global_count ? p->global_count : 1, sizeof *v->globals);
  v->fields = calloc(p->field_count ? p->field_count : 1, sizeof *v->fields);
  v->functions = calloc(p->function_count ? p->function_count : 1, sizeof *v->functions);
  v->statements = calloc(p->statement_count ? p->statement_count : 1, sizeof *v->statements);
  if (!v->globals || !v->fields || !v->functions || !v->statements) {
    qa_app_sim_quakec_program_view_destroy(v);
    return false;
  }
  for (size_t i = 0; i < p->global_count; i++)
    v->globals[i] = (qa_q1qc_definition){p->globals[i].type, p->globals[i].native_type, p->globals[i].save,
                                         p->globals[i].offset, p->globals[i].name};
  for (size_t i = 0; i < p->field_count; i++)
    v->fields[i] = (qa_q1qc_definition){p->fields[i].type, p->fields[i].native_type, p->fields[i].save,
                                        p->fields[i].offset, p->fields[i].name};
  for (size_t i = 0; i < p->function_count; i++)
    v->functions[i] = (qa_q1qc_function){p->functions[i].index, p->functions[i].first_statement,
                                         p->functions[i].parameter_start, p->functions[i].local_words,
                                         p->functions[i].name, p->functions[i].file, p->functions[i].parameter_sizes,
                                         p->functions[i].parameter_size_count, p->functions[i].named_builtin};
  for (size_t i = 0; i < p->statement_count; i++)
    v->statements[i] = (qa_q1qc_statement){(qa_q1qc_opcode)p->statements[i].opcode, p->statements[i].a,
                                           p->statements[i].b, p->statements[i].c};
  v->program = (qa_q1qc_program){p->source, p->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? "q1-quakeworld" : "q1-netquake",
                                 v->statements, p->statement_count, v->globals, p->global_count, v->fields,
                                 p->field_count, v->functions, p->function_count, p->strings, p->string_length,
                                 p->initial_globals, p->initial_globals_length, p->entity_field_words, p->checksum,
                                 p->digest};
  return true;
}
void qa_app_sim_quakec_program_view_destroy(qa_app_sim_quakec_program_view *v) {
  free(v->globals); free(v->fields); free(v->functions); free(v->statements);
  memset(v, 0, sizeof *v);
}

/* ---------------- prepare ---------------- */
void qa_app_sim_quakec_resources_destroy(qa_app_sim_quakec_resources *r) {
  for (size_t i = 0; i < r->count; i++) free(r->items[i].name);
  free(r->items);
  memset(r, 0, sizeof *r);
}
const qa_app_sim_quakec_resource *qa_app_sim_quakec_resources_get(const qa_app_sim_quakec_resources *r, const char *name) {
  for (size_t i = 0; i < r->count; i++) if (strcmp(r->items[i].name, name) == 0) return &r->items[i];
  return NULL;
}
/* TS /\.(mdl|spr|bsp|wav)$/ */
static bool resource_name(const char *name) {
  return ends(name, ".mdl") || ends(name, ".spr") || ends(name, ".bsp") || ends(name, ".wav");
}
typedef struct name_set { char **items; size_t count, capacity; } name_set;
static void name_set_add(name_set *s, const char *name) {
  for (size_t i = 0; i < s->count; i++) if (strcmp(s->items[i], name) == 0) return;
  s->items = xgrow(s->items, &s->capacity, s->count + 1, sizeof *s->items);
  s->items[s->count++] = xstrdup(name);
}
static void name_set_free(void *arg) {
  name_set *s = arg;
  for (size_t i = 0; i < s->count; i++) free(s->items[i]);
  free(s->items);
  memset(s, 0, sizeof *s);
}
typedef struct prepare_cleanup { name_set names; qa_app_sim_quakec_resources resources; qa_entities entities; } prepare_cleanup;
static void prepare_cleanup_run(void *arg) {
  prepare_cleanup *c = arg;
  name_set_free(&c->names);
  qa_app_sim_quakec_resources_destroy(&c->resources);
  qa_entities_free(&c->entities);
}
qa_app_sim_quakec_resources qa_app_sim_prepare_quakec_resources(const qa_compat_qc_program *program,
    qa_content_mounted *mounts, const char *entity_text) {
  prepare_cleanup c; memset(&c, 0, sizeof c);
  qa_cleanup scope; qa_cleanup_push(&scope, prepare_cleanup_run, &c);
  for (size_t offset = 0; offset < program->string_length;) {
    char *name = qa_compat_qc_byte_string(program->strings, program->string_length, (int64_t)offset);
    if (!name) qa_range_error("Out of memory");
    if (resource_name(name)) name_set_add(&c.names, name);
    offset += strlen(name) + 1;
    free(name);
  }
  const char *text = entity_text ? entity_text : "";
  if (!qa_parse_entities(text, strlen(text), NULL, &c.entities)) qa_range_error("Out of memory");
  for (size_t i = 0; i < c.entities.record_count; i++)
    for (size_t j = 0; j < c.entities.records[i].pair_count; j++) {
      const char *value = c.entities.records[i].pairs[j].value;
      if (resource_name(value)) name_set_add(&c.names, value);
    }
  size_t capacity = 0;
  for (size_t i = 0; i < c.names.count; i++) {
    const char *name = c.names.items[i];
    bool wav = ends(name, ".wav");
    char *path = NULL;
    if (wav) {
      size_t n = strlen(name) + 7;
      path = xalloc(n);
      (void)snprintf(path, n, "sound/%s", name);
    }
    qa_content_opened_resource asset; memset(&asset, 0, sizeof asset);
    bool found = qa_content_mounted_open(mounts, wav ? path : name, NULL, NULL, &asset);
    free(path);
    if (!found) continue; /* Conditional source precache fails if it actually requests this absent asset. */
    qa_app_sim_quakec_resource entry; memset(&entry, 0, sizeof entry);
    if (ends(name, ".mdl") || ends(name, ".spr")) {
      qa_q12_model model; memset(&model, 0, sizeof model);
      qa_parse_q12_model(asset.bytes, asset.byte_length, name, &model);
      entry.has_model_bounds = true;
      if (model.kind == QA_Q12_MODEL_MDL) entry.model_bounds = (qa_bounds){{-16, -16, -16}, {16, 16, 16}};
      else if (model.kind == QA_Q12_MODEL_SPR) entry.model_bounds = model.as.spr.bounds;
      else if (model.kind == QA_Q12_MODEL_MD2) entry.model_bounds = model.as.md2.bounds;
      else entry.model_bounds = model.as.sp2.bounds;
      qa_q12_model_destroy(&model);
    } else if (ends(name, ".bsp")) {
      qa_q1_map *map = xalloc(sizeof *map);
      qa_q1_map_options map_options; memset(&map_options, 0, sizeof map_options);
      map_options.source = name;
      qa_q1_read_bsp(asset.bytes, asset.byte_length, &map_options, map);
      if (map->world.model_count == 0) { qa_q1_map_destroy(map); free(map); free(asset.bytes); fail("No world model in %s", name); }
      entry.has_model_bounds = true;
      entry.model_bounds = map->world.models[0].bounds;
      qa_q1_map_destroy(map); free(map);
    } else {
      qa_decoded_wav decoded; memset(&decoded, 0, sizeof decoded);
      if (!qa_decode_quake_wav(asset.bytes, asset.byte_length, name, &decoded)) qa_range_error("Out of memory");
      qa_decoded_wav_dispose(&decoded);
    }
    free(asset.bytes);
    entry.name = xstrdup(name);
    entry.resource = *asset.reference;
    c.resources.items = xgrow(c.resources.items, &capacity, c.resources.count + 1, sizeof *c.resources.items);
    c.resources.items[c.resources.count++] = entry;
  }
  qa_cleanup_pop(&scope);
  qa_app_sim_quakec_resources out = c.resources;
  name_set_free(&c.names);
  qa_entities_free(&c.entities);
  return out;
}
static const char *binding_kind(const qa_app_sim_quakec_program_view *view) {
  return qa_q1_id1_program_binding_get(&view->program, NULL)->kind;
}
static void prepared_cleanup(void *arg) { qa_app_sim_prepared_quakec_source_destroy(arg); }
void qa_app_sim_prepare_quakec_source_mounted(qa_execution_module execution, qa_content_mounted *mounts,
    const char *entity_text, qa_content_mounted *resource_mounts, qa_app_sim_prepared_quakec_source *out) {
  memset(out, 0, sizeof *out);
  out->execution = execution;
  size_t length = 0;
  uint8_t *bytes = qa_content_mounted_read_reference(mounts, execution.artifact, &length);
  if (!bytes) fail("Selected QuakeC artifact is not mounted");
  bool loaded = qa_compat_qc_load_program(bytes, length, NULL, NULL, &out->program);
  free(bytes);
  if (!loaded) qa_range_error("Out of memory");
  qa_cleanup scope; qa_cleanup_push(&scope, prepared_cleanup, out);
  if (!qa_app_sim_quakec_program_view_init(&out->view, &out->program)) qa_range_error("Out of memory");
  const qa_quakec_api_identity *api = &execution.api.as.quakec;
  if (execution.api.kind != QA_GAME_API_QUAKEC || api->kind != out->program.api.kind
      || api->program_version != out->program.api.program_version || api->system_crc != out->program.api.system_crc)
    fail("Shared QuakeC artifact API differs from the selected execution");
  qa_content_opened_resource compatibility; memset(&compatibility, 0, sizeof compatibility);
  bool has_compatibility = qa_content_mounted_open(mounts, "quakec-compatibility.json", NULL, NULL, &compatibility);
  bool read = qa_compat_qc_read_quakec_compatibility(has_compatibility ? compatibility.bytes : NULL,
      has_compatibility ? compatibility.byte_length : 0, out->program.digest, &out->compatibility);
  free(compatibility.bytes);
  if (!read) qa_range_error("Out of memory");
  out->has_compatibility = true;
  qa_compat_qc_quakec_compatibility *c = &out->compatibility;
  out->has_teams = c->has_teams; out->teams = c->teams; out->team_count = c->team_count;
  out->combat_declaration = c->has_combat ? c->combat : NULL;
  out->message_dialect = c->message_dialect;
  out->weapon_declaration = c->has_weapon_stage ? &c->weapon_stage : NULL;
  if (out->combat_declaration) qa_compat_qc_validate_mod_combat(&out->program, *out->combat_declaration);
  else (void)qa_q1_id1_program_binding_get(&out->view.program, NULL);
  out->has_declared_pickups = true;
  out->declared_pickups = qa_q1qc_declared_pickup_stages(&out->view.program, c->pickup_callers, c->pickup_caller_count,
                                                        &out->declared_pickup_count);
  out->has_weapon_stage = true;
  out->weapon_stage = qa_q1qc_weapon_stage_create(&out->view.program, out->weapon_declaration);
  out->kind = binding_kind(&out->view);
  if (out->program.api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD && !is(out->message_dialect, "known-retail"))
    fail("Private NetQuake messages cannot be selected for QuakeWorld");
  out->resources = qa_app_sim_prepare_quakec_resources(&out->program, resource_mounts ? resource_mounts : mounts,
                                                       entity_text);
  qa_cleanup_pop(&scope);
}
qa_q1qc_weapon_stage *qa_app_sim_prepared_quakec_weapon_stage(const qa_app_sim_prepared_quakec_source *prepared, bool *out_owned) {
  if (prepared->has_weapon_stage) { *out_owned = false; return prepared->weapon_stage; }
  *out_owned = true;
  return qa_q1qc_weapon_stage_create(&prepared->view.program, prepared->weapon_declaration);
}
void qa_app_sim_prepared_quakec_source_destroy(qa_app_sim_prepared_quakec_source *p) {
  if (p->weapon_stage) qa_q1qc_weapon_stage_destroy(p->weapon_stage);
  if (p->declared_pickups) qa_q1qc_pickup_stages_destroy(p->declared_pickups, p->declared_pickup_count);
  qa_app_sim_quakec_resources_destroy(&p->resources);
  if (p->has_compatibility) qa_compat_qc_quakec_compatibility_destroy(&p->compatibility);
  qa_app_sim_quakec_program_view_destroy(&p->view);
  if (p->program.functions || p->program.statements || p->program.globals) qa_compat_qc_program_destroy(&p->program);
  memset(p, 0, sizeof *p);
}
qa_app_sim_prepared_quakec_source qa_app_sim_prepare_quakec_source(qa_execution_module execution,
    const char *entity_text, qa_app_sim_quakec_prepare_host host) {
  (void)entity_text; /* resources need mounts; see header */
  if (execution.kind != QA_EXECUTION_MODULE_QUAKEC) qa_range_error("QuakeC preparation requires a QuakeC execution module");
  uint8_t *bytes = NULL; size_t length = 0;
  if (!host.read_artifact || !host.read_artifact(host.ctx, execution.artifact, &bytes, &length) || bytes == NULL)
    qa_range_error("Selected QuakeC program is not mounted");
  qa_app_sim_prepared_quakec_source out; memset(&out, 0, sizeof out);
  out.execution = execution;
  bool loaded = qa_compat_qc_load_program(bytes, length, NULL, NULL, &out.program);
  free(bytes);
  if (!loaded) qa_range_error("Out of memory");
  const qa_quakec_api_identity *api = &execution.api.as.quakec;
  if (execution.api.kind != QA_GAME_API_QUAKEC || api->kind != out.program.api.kind
      || api->program_version != out.program.api.program_version || api->system_crc != out.program.api.system_crc)
    fail("Shared QuakeC artifact API differs from the selected execution");
  out.message_dialect = "known-retail";
  out.kind = out.program.api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? "quakeworld" : "netquake";
  return out;
}

/* ---------------- source state ---------------- */
typedef struct native_weapon {
  qa_item_id item; char label[64]; double bit, impulse; bool has_via; double via;
} native_weapon;
typedef struct info_entry { int64_t slot; qa_app_sim_cvar_name_value *items; size_t count; } info_entry;
typedef struct spawn_entry { int64_t slot; double values[16]; } spawn_entry;
typedef struct identity_entry { int64_t slot; qa_client_id client; } identity_entry;
typedef struct pending_entry { qa_actor_id actor; size_t weapon; bool following; } pending_entry;
typedef struct view_entry { int64_t index; bool has_actor; qa_actor_id actor; } view_entry;
typedef struct model_entry { char *name; double index; qa_bounds bounds; } model_entry;
typedef struct precache_entry { char *key; qa_compat_qc_precached_resource value; } precache_entry;
typedef struct incoming_entry {
  qa_damage_request request; bool entered; bool has_outcome; qa_damage_outcome outcome;
} incoming_entry;
typedef struct network_entry { int64_t slot; qa_app_sim_quakec_client_network info; } network_entry;
typedef struct nq_routed {
  size_t size; qa_netquake_message *messages; size_t count;
  qa_compat_qc_message_destination destination;
  view_entry *views; size_t view_count;
} nq_routed;
/* Per-admitted-actor callback context (TS closures over actor/slot). */
typedef struct actor_cell {
  S *source; qa_owned_actor actor; int64_t slot;
  qa_actor_id owner, enemy; bool live;
  qa_combat_state_binding combat;
  char team[64]; /* read().team storage */
} actor_cell;
typedef struct actor_set { qa_actor_id *items; size_t count, capacity; } actor_set;
typedef struct slot_set { int64_t *items; size_t count, capacity; } slot_set;

struct qa_app_sim_quakec_source {
  const qa_app_sim_prepared_quakec_source *prepared;
  qa_app_sim_quakec_source_options options;
  qa_compat_qc_program *program;
  const qa_q1qc_program *qprogram;
  const qa_q1_id1_program_binding *binding;
  bool quakeworld;
  int64_t reserved_client_slots;
  double current_time;
  bool change_level_issued;
  actor_set active_clients;
  native_weapon weapons[11]; size_t weapon_count;
  pending_entry *pending; size_t pending_count, pending_capacity;
  info_entry *user_info; size_t user_info_count, user_info_capacity;
  spawn_entry *spawn_parameters; size_t spawn_count, spawn_capacity;
  identity_entry *identities; size_t identity_count, identity_capacity;
  network_entry *networks; size_t network_count, network_capacity;
  bool original_save_restored;
  char *original_save_extension_text;
  slot_set spectator_slots, prepared_clients;
  qa_event_ring frag_records;   /* qa_app_sim_quakec_frag */
  qa_event_ring routed;         /* qa_app_sim_quakec_routed (owned entries) */
  qa_compat_qc_routed_message *signon; size_t signon_count, signon_capacity;
  qa_netquake_message *nq_signon; size_t nq_signon_count, nq_signon_capacity;
  view_entry *nq_signon_views; size_t nq_signon_view_count, nq_signon_view_capacity;
  qa_event_ring nq_routed;      /* nq_routed (owned) */
  qa_app_sim_quakec_local_messages *local_messages;
  bool local_messages_started, netquake_wire_attached;
  bool has_physics_callback; qa_q1_id1_physics_callback physics_callback;
  bool spawning;
  incoming_entry *incoming; size_t incoming_count, incoming_capacity;
  model_entry *models; size_t model_entry_count, model_entry_capacity;
  precache_entry *precached; size_t precached_count, precached_capacity;
  double model_count, sound_count;
  int64_t release_id;

  /* compat/qc composition */
  qa_compat_qc_entity_memory entities;
  qa_compat_qc_source_slot_storage_state slot_storage;
  qa_source_actor_slots_options slots_options;
  qa_source_actor_slots *slots;
  qa_compat_qc_borrowed_actors borrowed;
  qa_compat_qc_world_host world_host;
  qa_cvar_registry *cvars; bool owns_cvars;
  qa_compat_qc_client_host clients;
  qa_compat_qc_quakeworld_message_services qw; bool has_qw;
  qa_compat_qc_netquake_message_services nq; bool has_nq;
  qa_compat_qc_presentation_world presentation_world, message_world;
  qa_compat_qc_presentation_state presentation_state;
  qa_compat_qc_movement_state_compat movement_state;
  qa_compat_qc_broadcast_messages messages;
  qa_compat_qc_host_binding host[192]; size_t host_count;
  qa_compat_qc_builtin remove_builtin, aim_builtin;
  qa_compat_qc_aim_state aim_state;
  qa_compat_qc_aim_target_ref *aim_targets; size_t aim_target_count, aim_target_capacity;
  qa_random_source random; double random_words[QA_Q2_RERELEASE_WORDS];
  qa_compat_qc_builtin_registry registry;
  qa_compat_qc_machine machine; bool has_machine;
  const qa_numeric_operations *numeric; /* createNumericOperations(Q1_DONOR_PROFILE) */
  qa_compat_qc_actor_state actor_state;
  /* content/q1/quakec bindings over the q1qc machine view */
  qa_q1qc_entity_memory qentities; qa_q1qc_machine qmachine; qa_q1qc_source qsource; qa_q1qc_foreign_source foreign;
  qa_q1_id1_sync_attacks *attacks;
  qa_q1_id1_projectile_attacks *projectiles;
  qa_q1_id1_environment *environment;
  qa_q1_id1_damage_binding *damage;
  qa_q1_id1_pickup_binding *pickups;
  qa_q1qc_weapon_stage *weapon_stage; bool owns_weapon_stage;
  qa_q1qc_weapon_stage_binding weapon_binding; bool has_weapon_binding;
  qa_q1_id1_sync_composed attack_functions;
  qa_q1_id1_projectile_composed projectile_functions;
  qa_q1_id1_pickup_function_composed pickup_functions;
  qa_q1qc_function_boundary spawn_functions; int64_t *spawn_function_list; bool has_spawn_wrap; int64_t spawn_function;
  const qa_q1qc_function_boundary *functions; /* sourceFunctions or the spawn wrap */
  qa_q1_id1_pickup_region_composed pickup_regions;
  qa_q1qc_weapon_function_composed weapon_functions;
  qa_q1qc_weapon_region_composed weapon_regions;
  const qa_q1qc_function_boundary *top_functions;
  const qa_q1qc_inline_boundary *top_regions;
  qa_compat_qc_function_boundary machine_functions;
  qa_compat_qc_inline_boundary machine_regions;
  qa_compat_qc_inline_region *machine_region_list;
  /* pushers */
  qa_compat_qc_pusher_state pusher_state;
  qa_q1_pusher_services pushers, pusher_services;
  qa_app_sim_q1_pusher_env pusher_env;
  actor_cell **cells; size_t cell_count, cell_capacity;
  struct inventory_cell **inventory_cells; size_t inventory_cell_count, inventory_cell_capacity;
  /* query scratch */
  char *classname_scratch, *team_scratch, *death_scratch, *info_scratch;
  /* SimulationEvents retains message text by pointer until published;
   * source-formatted texts live in this ring (TS strings are values). */
  char *message_texts[1024]; size_t message_text_next;
  qa_app_sim_quakec_frag *drained_frags;
  qa_app_sim_quakec_routed *drained_routed; size_t drained_routed_count;
  nq_routed *drained_nq; size_t drained_nq_count; qa_app_sim_quakec_netquake_routed *drained_nq_view;
  qa_client_id *client_scratch;
  const char **name_scratch;
  qa_app_sim_cvar_name_value *travel_cvars; size_t travel_cvar_count;
  qa_app_sim_quakec_client_travel *travel_clients; size_t travel_client_count;
  double *travel_parameters;
  qa_actor_id solid_owner, flags_enemy;
  /* Messages the local-message state retains borrow their text; this pool
   * keeps every message handed to it alive for the source's lifetime. */
  qa_netquake_message **retained; size_t *retained_counts; size_t retained_count, retained_capacity, retained_counts_capacity;
  /* checkpoint storage */
  qa_guest_checkpoint *checkpoint_storage; qa_compat_qc_machine_snapshot checkpoint_snapshot;
  uint8_t *checkpoint_host_bytes; bool has_checkpoint;
  uint8_t *host_state_bytes;
};

static int64_t field(S *s, const char *name) {
  const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(s->program, name);
  if (!f) fail("Missing id1 field %s", name);
  return f->offset;
}
static bool has_field(S *s, const char *name, int64_t *out) {
  const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(s->program, name);
  if (!f) return false;
  *out = f->offset;
  return true;
}
static qa_compat_qc_words *at(S *s, int64_t slot) { return qa_compat_qc_entity_memory_at(&s->entities, slot); }
static double fget(S *s, qa_compat_qc_words *w, const char *name) { return qa_compat_qc_words_float(w, field(s, name)); }
static void fset(S *s, qa_compat_qc_words *w, const char *name, double value) { qa_compat_qc_words_set_float(w, field(s, name), value); }
static qa_vec3 vget(S *s, qa_compat_qc_words *w, const char *name) { return qa_compat_qc_words_vector(w, field(s, name)); }
static void vset(S *s, qa_compat_qc_words *w, const char *name, qa_vec3 v) { qa_compat_qc_words_set_vector(w, field(s, name), v); }
static double gfloat(S *s, const char *name) {
  return qa_compat_qc_words_float(&s->machine.globals, qa_compat_qc_machine_global_offset(&s->machine, name));
}
static void gset(S *s, const char *name, double value) {
  qa_compat_qc_words_set_float(&s->machine.globals, qa_compat_qc_machine_global_offset(&s->machine, name), value);
}
static const char *qstring(S *s, int64_t reference) { return qa_compat_qc_strings_get(&s->machine.strings, reference); }
static int32_t ibits(double value) { return qa_js_to_int32(value); }
static const char *content_of(S *s) { return s->prepared->execution.owner.content; }
static const char *provider_of(S *s) { return s->prepared->execution.owner.provider; }
static const char *map_path(S *s) { return s->options.recipe->map.geometry.requested_path; }
static size_t world_model_count(qa_decoded_world world) {
  switch (world.kind) {
    case QA_DECODED_WORLD_Q1: return world.as.q1->model_count;
    case QA_DECODED_WORLD_Q2: return world.as.q2->model_count;
    default: return world.as.q3->model_count;
  }
}

/* sets */
static bool actor_set_has(const actor_set *set, qa_actor_id actor) {
  for (size_t i = 0; i < set->count; i++) if (same_actor(set->items[i], actor)) return true;
  return false;
}
static void actor_set_add(actor_set *set, qa_actor_id actor) {
  if (actor_set_has(set, actor)) return;
  set->items = xgrow(set->items, &set->capacity, set->count + 1, sizeof *set->items);
  set->items[set->count++] = actor;
}
static void actor_set_delete(actor_set *set, qa_actor_id actor) {
  for (size_t i = 0; i < set->count; i++)
    if (same_actor(set->items[i], actor)) {
      memmove(&set->items[i], &set->items[i + 1], (set->count - i - 1) * sizeof *set->items);
      set->count--;
      return;
    }
}
static bool slot_set_has(const slot_set *set, int64_t slot) {
  for (size_t i = 0; i < set->count; i++) if (set->items[i] == slot) return true;
  return false;
}
static void slot_set_add(slot_set *set, int64_t slot) {
  if (slot_set_has(set, slot)) return;
  set->items = xgrow(set->items, &set->capacity, set->count + 1, sizeof *set->items);
  set->items[set->count++] = slot;
}
static void slot_set_delete(slot_set *set, int64_t slot) {
  for (size_t i = 0; i < set->count; i++)
    if (set->items[i] == slot) {
      memmove(&set->items[i], &set->items[i + 1], (set->count - i - 1) * sizeof *set->items);
      set->count--;
      return;
    }
}
/* maps keyed by slot */
static info_entry *info_get(S *s, int64_t slot) {
  for (size_t i = 0; i < s->user_info_count; i++) if (s->user_info[i].slot == slot) return &s->user_info[i];
  return NULL;
}
static const char *info_value(const info_entry *entry, const char *key) {
  if (!entry) return NULL;
  for (size_t i = 0; i < entry->count; i++) if (strcmp(entry->items[i].name, key) == 0) return entry->items[i].value;
  return NULL;
}
static void info_free_items(qa_app_sim_cvar_name_value *items, size_t count) {
  for (size_t i = 0; i < count; i++) { free((char *)items[i].name); free((char *)items[i].value); }
  free(items);
}
/* TS new Map(values): later duplicate keys replace the value in place. */
static void info_copy(qa_app_sim_quakec_info values, qa_app_sim_cvar_name_value **out, size_t *out_count) {
  qa_app_sim_cvar_name_value *items = xalloc((values.count ? values.count : 1) * sizeof *items);
  size_t count = 0;
  for (size_t i = 0; i < values.count; i++) {
    size_t j = 0;
    while (j < count && strcmp(items[j].name, values.items[i].name) != 0) j++;
    if (j < count) { free((char *)items[j].value); items[j].value = xstrdup(values.items[i].value); continue; }
    items[count].name = xstrdup(values.items[i].name);
    items[count].value = xstrdup(values.items[i].value);
    count++;
  }
  *out = items; *out_count = count;
}
static void info_set(S *s, int64_t slot, qa_app_sim_quakec_info values) {
  info_entry *entry = info_get(s, slot);
  if (!entry) {
    s->user_info = xgrow(s->user_info, &s->user_info_capacity, s->user_info_count + 1, sizeof *s->user_info);
    entry = &s->user_info[s->user_info_count++];
    entry->slot = slot; entry->items = NULL; entry->count = 0;
  }
  qa_app_sim_cvar_name_value *items; size_t count;
  info_copy(values, &items, &count);
  info_free_items(entry->items, entry->count);
  entry->items = items; entry->count = count;
}
static void info_delete(S *s, int64_t slot) {
  for (size_t i = 0; i < s->user_info_count; i++)
    if (s->user_info[i].slot == slot) {
      info_free_items(s->user_info[i].items, s->user_info[i].count);
      memmove(&s->user_info[i], &s->user_info[i + 1], (s->user_info_count - i - 1) * sizeof *s->user_info);
      s->user_info_count--;
      return;
    }
}
static void info_clear(S *s) { while (s->user_info_count) info_delete(s, s->user_info[0].slot); }
static spawn_entry *spawn_get(S *s, int64_t slot) {
  for (size_t i = 0; i < s->spawn_count; i++) if (s->spawn_parameters[i].slot == slot) return &s->spawn_parameters[i];
  return NULL;
}
static void spawn_set(S *s, int64_t slot, const double values[16]) {
  spawn_entry *entry = spawn_get(s, slot);
  if (!entry) {
    s->spawn_parameters = xgrow(s->spawn_parameters, &s->spawn_capacity, s->spawn_count + 1, sizeof *s->spawn_parameters);
    entry = &s->spawn_parameters[s->spawn_count++];
    entry->slot = slot;
  }
  memcpy(entry->values, values, sizeof entry->values);
}
static void spawn_delete(S *s, int64_t slot) {
  for (size_t i = 0; i < s->spawn_count; i++)
    if (s->spawn_parameters[i].slot == slot) {
      memmove(&s->spawn_parameters[i], &s->spawn_parameters[i + 1], (s->spawn_count - i - 1) * sizeof *s->spawn_parameters);
      s->spawn_count--;
      return;
    }
}
static identity_entry *identity_get(S *s, int64_t slot) {
  for (size_t i = 0; i < s->identity_count; i++) if (s->identities[i].slot == slot) return &s->identities[i];
  return NULL;
}
static void identity_set(S *s, int64_t slot, qa_client_id client) {
  identity_entry *entry = identity_get(s, slot);
  if (!entry) {
    s->identities = xgrow(s->identities, &s->identity_capacity, s->identity_count + 1, sizeof *s->identities);
    entry = &s->identities[s->identity_count++];
    entry->slot = slot;
  }
  entry->client = client;
}
static void identity_delete(S *s, int64_t slot) {
  for (size_t i = 0; i < s->identity_count; i++)
    if (s->identities[i].slot == slot) {
      memmove(&s->identities[i], &s->identities[i + 1], (s->identity_count - i - 1) * sizeof *s->identities);
      s->identity_count--;
      return;
    }
}
static pending_entry *pending_get(S *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->pending_count; i++) if (same_actor(s->pending[i].actor, actor)) return &s->pending[i];
  return NULL;
}
static void pending_set(S *s, qa_actor_id actor, size_t weapon, bool following) {
  pending_entry *entry = pending_get(s, actor);
  if (!entry) {
    s->pending = xgrow(s->pending, &s->pending_capacity, s->pending_count + 1, sizeof *s->pending);
    entry = &s->pending[s->pending_count++];
    entry->actor = actor;
  }
  entry->weapon = weapon; entry->following = following;
}
static void pending_delete(S *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->pending_count; i++)
    if (same_actor(s->pending[i].actor, actor)) {
      memmove(&s->pending[i], &s->pending[i + 1], (s->pending_count - i - 1) * sizeof *s->pending);
      s->pending_count--;
      return;
    }
}
static model_entry *model_get(S *s, const char *name) {
  for (size_t i = 0; i < s->model_entry_count; i++) if (strcmp(s->models[i].name, name) == 0) return &s->models[i];
  return NULL;
}
static void model_set(S *s, const char *name, double index, qa_bounds bounds) {
  model_entry *entry = model_get(s, name);
  if (entry) { entry->index = index; entry->bounds = bounds; return; }
  s->models = xgrow(s->models, &s->model_entry_capacity, s->model_entry_count + 1, sizeof *s->models);
  s->models[s->model_entry_count++] = (model_entry){xstrdup(name), index, bounds};
}
static void models_clear(S *s) {
  for (size_t i = 0; i < s->model_entry_count; i++) free(s->models[i].name);
  s->model_entry_count = 0;
}
static precache_entry *precache_get(S *s, const char *key) {
  for (size_t i = 0; i < s->precached_count; i++) if (strcmp(s->precached[i].key, key) == 0) return &s->precached[i];
  return NULL;
}
static void precache_set(S *s, const char *key, qa_compat_qc_precached_resource value) {
  s->precached = xgrow(s->precached, &s->precached_capacity, s->precached_count + 1, sizeof *s->precached);
  s->precached[s->precached_count++] = (precache_entry){xstrdup(key), value};
}
static void precached_clear(S *s) {
  for (size_t i = 0; i < s->precached_count; i++) free(s->precached[i].key);
  s->precached_count = 0;
}
static char *key_of(const char *kind, const char *name) {
  size_t n = strlen(kind) + strlen(name) + 2;
  char *key = xalloc(n);
  (void)snprintf(key, n, "%s:%s", kind, name);
  return key;
}
static void set_scratch(char **scratch, const char *text) {
  free(*scratch);
  *scratch = xstrdup(text);
}

/* message deep copies (TS messages are immutable values) */
static char *dup_or_null(const char *text) { return text ? xstrdup(text) : NULL; }
static qa_netquake_message nq_copy(const qa_netquake_message *m) {
  qa_netquake_message c = *m;
  c.text = dup_or_null(m->text); c.level = dup_or_null(m->level); c.str_value = dup_or_null(m->str_value);
  c.models = NULL; c.sounds = NULL;
  if (m->model_count) {
    c.models = xalloc(m->model_count * sizeof *c.models);
    for (size_t i = 0; i < m->model_count; i++) c.models[i] = dup_or_null(m->models[i]);
  }
  if (m->sound_count) {
    c.sounds = xalloc(m->sound_count * sizeof *c.sounds);
    for (size_t i = 0; i < m->sound_count; i++) c.sounds[i] = dup_or_null(m->sounds[i]);
  }
  return c;
}
static void nq_messages_free(qa_netquake_message *messages, size_t count) {
  for (size_t i = 0; i < count; i++) qa_netquake_message_destroy(&messages[i]);
  free(messages);
}
static qa_quakeworld_message qw_copy(const qa_quakeworld_message *m) {
  qa_quakeworld_message c = *m;
  c.text = dup_or_null(m->text); c.str_value = dup_or_null(m->str_value); c.key = dup_or_null(m->key);
  c.game_directory = dup_or_null(m->game_directory); c.level_name = dup_or_null(m->level_name);
  c.download_bytes = NULL; c.projectiles = NULL; c.names = NULL; c.entities = NULL;
  if (m->download_length) { c.download_bytes = xalloc(m->download_length); memcpy(c.download_bytes, m->download_bytes, m->download_length); }
  if (m->projectile_count) { c.projectiles = xalloc(m->projectile_count * sizeof *c.projectiles); memcpy(c.projectiles, m->projectiles, m->projectile_count * sizeof *c.projectiles); }
  if (m->entity_count) { c.entities = xalloc(m->entity_count * sizeof *c.entities); memcpy(c.entities, m->entities, m->entity_count * sizeof *c.entities); }
  if (m->name_count) {
    c.names = xalloc(m->name_count * sizeof *c.names);
    for (size_t i = 0; i < m->name_count; i++) c.names[i] = dup_or_null(m->names[i]);
  }
  return c;
}
static qa_compat_qc_routed_message *qw_entries_copy(const qa_compat_qc_routed_message *entries, size_t count) {
  qa_compat_qc_routed_message *out = xalloc((count ? count : 1) * sizeof *out);
  for (size_t i = 0; i < count; i++) { out[i] = entries[i]; out[i].message = qw_copy(&entries[i].message); }
  return out;
}
static void routed_free(void *record) {
  qa_app_sim_quakec_routed *r = record;
  for (size_t i = 0; i < r->entry_count; i++) qa_quakeworld_message_destroy(&r->entries[i].message);
  free(r->entries);
}
static void nq_routed_free(nq_routed *r) {
  nq_messages_free(r->messages, r->count);
  free(r->views);
}
static bool view_lookup(const view_entry *views, size_t count, int64_t index, view_entry *out) {
  for (size_t i = 0; i < count; i++) if (views[i].index == index) { if (out) *out = views[i]; return true; }
  return false;
}

/* ---------------- identity helpers ---------------- */
bool qa_app_sim_quakec_source_source_slot(S *s, qa_actor_id actor, int64_t *out) {
  if (!qa_session_actor_registry_is_live(s->options.actors, actor)) return false;
  qa_actor_source_result source = qa_session_actor_registry_source_of(s->options.actors, actor);
  if (!source.has_value || !is(source.provider, provider_of(s))) return false;
  *out = source.slot;
  return true;
}
static bool source_slot(S *s, qa_actor_id actor, int64_t *out) { return qa_app_sim_quakec_source_source_slot(s, actor, out); }
bool qa_app_sim_quakec_source_is_reserved_client(S *s, qa_actor_id actor) {
  int64_t slot;
  return source_slot(s, actor, &slot) && slot > 0 && slot <= s->reserved_client_slots;
}
bool qa_app_sim_quakec_source_is_active_client(S *s, qa_actor_id actor) { return actor_set_has(&s->active_clients, actor); }
static bool active(S *s, qa_actor_id actor) { return actor_set_has(&s->active_clients, actor); }
static int64_t reference(S *s, qa_actor_id actor) { return qa_compat_qc_world_host_reference(&s->world_host, actor); }
static bool slot_actor(S *s, int64_t slot, qa_owned_actor *out) { return qa_source_actor_slots_at(s->slots, slot, out); }
static bool resolve_owned(S *s, qa_actor_id actor, qa_owned_actor *out) {
  return qa_session_actor_registry_resolve_owned(s->options.actors, actor, out);
}
static bool live(S *s, qa_actor_id actor) { return qa_session_actor_registry_is_live(s->options.actors, actor); }
static void print(S *s, const char *text) { s->options.print(s->options.ctx, text); }
static bool primary_selected(S *s, qa_actor_id actor) {
  return s->options.primary_weapon_selected ? s->options.primary_weapon_selected(s->options.ctx, actor) : true;
}

/* q1qc machine view of the compat machine (TS `() => this.machine`). */
static qa_q1qc_machine *qmachine(void *ctx) {
  S *s = ctx;
  qa_compat_qc_entity_memory *e = &s->entities;
  s->qentities = (qa_q1qc_entity_memory){e->layout.stride_bytes, e->layout.variables_offset_bytes, e->layout.field_words,
                                         e->capacity, e->count, e->bytes};
  s->qmachine.ctx = s;
  s->qmachine.program = (qa_q1qc_program *)s->qprogram;
  s->qmachine.globals = (qa_q1qc_words){s->machine.globals.bytes, s->machine.globals.byte_length};
  s->qmachine.entities = &s->qentities;
  s->qmachine.numeric = *s->numeric;
  return &s->qmachine;
}
static void qm_fail(void *ctx, const char *message) { qa_compat_qc_machine_fail(&((S *)ctx)->machine, "%s", message); }
static void qm_execute(void *ctx, int64_t fn, int64_t argc) { qa_compat_qc_machine_execute(&((S *)ctx)->machine, fn, argc); }
static double qm_execute_region(void *ctx, qa_q1qc_inline_region r, int64_t argc) {
  qa_compat_qc_inline_region region = {r.function_index, r.entry, r.exit, r.replaceable, r.has_standalone,
                                       r.standalone_saved, r.standalone_scope};
  return qa_compat_qc_machine_execute_region(&((S *)ctx)->machine, region, argc);
}
static const char *qm_strings_get(void *ctx, int64_t reference, char *buf, size_t size) {
  const char *text = qa_compat_qc_strings_get(&((S *)ctx)->machine.strings, reference);
  if (buf && size) { (void)snprintf(buf, size, "%s", text); return buf; }
  return text;
}
static int64_t qm_strings_set_engine(void *ctx, const char *name, const char *text, size_t capacity) {
  return qa_compat_qc_strings_set_engine(&((S *)ctx)->machine.strings, name, text, (int64_t)capacity);
}

/* TS invoke: self/other restored in a finally. */
typedef struct invoke_restore { S *s; int64_t self_offset, other_offset; int32_t self, other; } invoke_restore;
static void invoke_finally(void *arg) {
  invoke_restore *r = arg;
  qa_compat_qc_words_set_int(&r->s->machine.globals, r->self_offset, r->self);
  qa_compat_qc_words_set_int(&r->s->machine.globals, r->other_offset, r->other);
}
static void invoke(S *s, int64_t callback, int64_t slot, int64_t other, double time) {
  qa_compat_qc_words *globals = &s->machine.globals;
  invoke_restore r = {s, qa_compat_qc_machine_global_offset(&s->machine, "self"),
                      qa_compat_qc_machine_global_offset(&s->machine, "other"), 0, 0};
  r.self = qa_compat_qc_words_int(globals, r.self_offset);
  r.other = qa_compat_qc_words_int(globals, r.other_offset);
  qa_cleanup scope; qa_cleanup_push(&scope, invoke_finally, &r);
  qa_compat_qc_words_set_int(globals, r.self_offset, (double)qa_compat_qc_entity_memory_reference(&s->entities, slot));
  qa_compat_qc_words_set_int(globals, r.other_offset, (double)other);
  qa_compat_qc_words_set_float(globals, qa_compat_qc_machine_global_offset(&s->machine, "time"), time);
  qa_compat_qc_machine_execute(&s->machine, callback, 0);
  qa_cleanup_pop(&scope);
  invoke_finally(&r);
}
static int64_t function_named(S *s, const char *name) { return qa_compat_qc_program_function_named(s->program, name)->index; }
static const qa_compat_qc_function *function_or_null(S *s, const char *name) {
  return qa_compat_qc_program_function_named_map(s->program, name);
}

/* ---------------- events ---------------- */
static qa_actor_id no_actor(void) { qa_actor_id none; memset(&none, 0, sizeof none); return none; }
static void emit_q1(S *s, qa_q1_event event, const qa_actor_id *recipient) {
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &event;
  qa_source_time t = {QA_SOURCE_TIME_SECONDS, 0};
  qa_app_sim_simulation_events_emit(s->options.events, content_of(s), source, false, t,
      recipient != NULL, recipient ? *recipient : no_actor());
}
static void emit_presentation(S *s, qa_app_sim_source_presentation_event source, const qa_actor_id *recipient) {
  qa_source_time t = {QA_SOURCE_TIME_SECONDS, 0};
  qa_app_sim_simulation_events_emit(s->options.events, content_of(s), source, false, t,
      recipient != NULL, recipient ? *recipient : no_actor());
}
static void message(S *s, qa_network_event event, const qa_actor_id *actor) {
  qa_app_sim_simulation_events_message(s->options.events, event, actor != NULL, actor ? *actor : no_actor(), false, 0);
}
static const char *retain_text(S *s, const char *text) {
  size_t slot = s->message_text_next++ % (sizeof s->message_texts / sizeof s->message_texts[0]);
  free(s->message_texts[slot]);
  s->message_texts[slot] = xstrdup(text);
  return s->message_texts[slot];
}
/* TS presentMessage: a localization-key print becomes the same message event native rules emit, so it localizes like theirs.
 * The emitted presentation event is copied by the presentation state, so the decoded strings are released after it. */
static void present_message(S *s, qa_network_event event, const qa_actor_id *actor) {
  qa_localization_key_text localized;
  if (actor != NULL && (event.kind == QA_NETWORK_EVENT_PRINT || event.kind == QA_NETWORK_EVENT_CENTER_PRINT)
      && event.text != NULL && qa_decode_localization_key(event.text, &localized)) {
    qa_q1_msg_arg *args = calloc(localized.arg_count == 0 ? 1 : localized.arg_count, sizeof *args);
    if (args == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < localized.arg_count; i++) { args[i].is_number = false; args[i].text = localized.args[i]; }
    qa_q1_event q; memset(&q, 0, sizeof q);
    q.kind = "message"; q.has_player = true; q.player = *actor; q.text = localized.key;
    q.center = event.kind == QA_NETWORK_EVENT_CENTER_PRINT; q.args = args; q.arg_count = localized.arg_count;
    emit_q1(s, q, actor);
    free(args);
    qa_localization_key_text_release(&localized);
    return;
  }
  message(s, event, actor);
}
static void print_message(S *s, qa_actor_id actor, const char *text) {
  qa_network_event event; memset(&event, 0, sizeof event);
  event.kind = QA_NETWORK_EVENT_PRINT; event.level = 2; event.text = retain_text(s, text);
  message(s, event, &actor);
}

/* ---------------- precache ---------------- */
/* TS mapModelIndex: false = null. */
static bool map_model_index(S *s, const char *name, double *out) {
  if (is(name, map_path(s))) { *out = 1; return true; }
  if (name[0] != '*') return false;
  bool digits = name[1] >= '1' && name[1] <= '9';
  for (const char *p = name + 1; digits && *p; p++) digits = *p >= '0' && *p <= '9';
  double model = digits ? qa_js_string_to_number(name + 1) : -1;
  if (!(model <= (double)QA_MAX_SAFE_INTEGER) || model != trunc(model) || model < 1
      || model >= (double)world_model_count(s->options.world))
    fail("Invalid actual QC inline model %s", name);
  *out = model + 1;
  return true;
}
static qa_compat_qc_precached_resource precache(S *s, const char *kind, const char *name) {
  char *key = key_of(kind, name);
  precache_entry *prior = precache_get(s, key);
  if (prior) { free(key); return prior->value; }
  double map_index = 0;
  qa_compat_qc_precached_resource value; memset(&value, 0, sizeof value);
  if (is(kind, "model") && map_model_index(s, name, &map_index)) {
    value.index = map_index; value.resource = s->options.recipe->map.geometry;
    precache_set(s, key, value); free(key);
    return value;
  }
  free(key);
  const qa_app_sim_quakec_resource *resource = qa_app_sim_quakec_resources_get(&s->prepared->resources, name);
  if (!resource) fail("Unprepared actual QC %s resource %s", kind, name);
  double index = is(kind, "model") ? s->model_count++ : s->sound_count++;
  if (index >= 256) fail("NetQuake %s precache limit exceeded", kind);
  if (is(kind, "model")) {
    if (!resource->has_model_bounds) fail("Missing actual model bounds %s", name);
    model_set(s, name, index, resource->model_bounds);
  }
  value.index = index; value.resource = resource->resource;
  key = key_of(kind, name);
  precache_set(s, key, value); free(key);
  return value;
}
static qa_compat_qc_precached_resource precache_service(void *ctx, const char *kind, const char *name) { return precache(ctx, kind, name); }
static bool lookup_service(void *ctx, const char *kind, const char *name, qa_compat_qc_precached_resource *out) {
  char *key = key_of(kind, name);
  precache_entry *entry = precache_get(ctx, key);
  free(key);
  if (!entry) return false;
  *out = entry->value;
  return true;
}
const char *const *qa_app_sim_quakec_source_precache_names(S *s, const char *kind, size_t *out_count) {
  size_t capacity = s->model_entry_count + s->precached_count + 1;
  free(s->name_scratch);
  s->name_scratch = xalloc(capacity * sizeof *s->name_scratch);
  double *order = xalloc(capacity * sizeof *order);
  size_t count = 0;
  if (is(kind, "model")) {
    for (size_t i = 0; i < s->model_entry_count; i++) { s->name_scratch[count] = s->models[i].name; order[count++] = s->models[i].index; }
  } else {
    for (size_t i = 0; i < s->precached_count; i++)
      if (starts(s->precached[i].key, "sound:")) { s->name_scratch[count] = s->precached[i].key + 6; order[count++] = s->precached[i].value.index; }
  }
  /* stable insertion sort by index (TS Array.sort is stable) */
  for (size_t i = 1; i < count; i++) {
    const char *name = s->name_scratch[i]; double key = order[i]; size_t j = i;
    while (j > 0 && order[j - 1] > key) { order[j] = order[j - 1]; s->name_scratch[j] = s->name_scratch[j - 1]; j--; }
    order[j] = key; s->name_scratch[j] = name;
  }
  free(order);
  *out_count = count;
  return s->name_scratch;
}

/* ---------------- teams and client info ---------------- */
const char *qa_app_sim_quakec_source_kind(const S *s) { return s->binding->kind; }
/* TS QuakeCSource.attacks / .projectiles (readonly members) and .machine, as the
 * q1qc machine view the id1 bindings use (the same view `() => this.machine` hands them). */
qa_q1_id1_sync_attacks *qa_app_sim_quakec_source_attacks(S *s) { return s->attacks; }
qa_q1_id1_projectile_attacks *qa_app_sim_quakec_source_projectiles(S *s) { return s->projectiles; }
qa_q1qc_machine *qa_app_sim_quakec_source_q1qc_machine(S *s) { return qmachine(s); }
const char *qa_app_sim_quakec_source_match_team(S *s, qa_actor_id actor) {
  if (s->quakeworld) {
    int64_t slot;
    const char *value = source_slot(s, actor, &slot) ? info_value(info_get(s, slot), "team") : NULL;
    if (value == NULL || value[0] == '\0') return NULL;
    set_scratch(&s->team_scratch, value);
    return s->team_scratch;
  }
  double value = qa_compat_qc_words_float(qa_compat_qc_entity_memory_from_reference(&s->entities, reference(s, actor)), field(s, "team"));
  if (!(value > 0)) return NULL;
  set_scratch(&s->team_scratch, QA_JS_NUMBER(value));
  return s->team_scratch;
}
static void info_of(S *s, qa_client_id client, qa_app_sim_cvar_name_value **out, size_t *count) {
  info_entry *entry = info_get(s, client.slot + 1);
  qa_app_sim_quakec_info values = {entry ? entry->items : NULL, entry ? entry->count : 0};
  info_copy(values, out, count);
}
static void info_put(qa_app_sim_cvar_name_value **items, size_t *count, const char *key, const char *value) {
  for (size_t i = 0; i < *count; i++)
    if (strcmp((*items)[i].name, key) == 0) { free((char *)(*items)[i].value); (*items)[i].value = xstrdup(value); return; }
  qa_app_sim_cvar_name_value *next = realloc(*items, (*count + 1) * sizeof **items);
  if (!next) qa_range_error("Out of memory");
  *items = next;
  (*items)[*count].name = xstrdup(key); (*items)[*count].value = xstrdup(value);
  (*count)++;
}
static void info_remove(qa_app_sim_cvar_name_value *items, size_t *count, const char *key) {
  for (size_t i = 0; i < *count; i++)
    if (strcmp(items[i].name, key) == 0) {
      free((char *)items[i].name); free((char *)items[i].value);
      memmove(&items[i], &items[i + 1], (*count - i - 1) * sizeof *items);
      (*count)--;
      return;
    }
}
typedef struct owned_info { qa_app_sim_cvar_name_value *items; size_t count; } owned_info;
static void owned_info_free(void *arg) { owned_info *o = arg; info_free_items(o->items, o->count); o->items = NULL; o->count = 0; }
void qa_app_sim_quakec_source_set_match_team(S *s, qa_actor_id actor, const char *team) {
  int64_t slot = 0;
  bool has_slot = source_slot(s, actor, &slot);
  identity_entry *identity = has_slot ? identity_get(s, slot) : NULL;
  if (!has_slot || !identity || !active(s, actor)) fail("Original team command requires an admitted client");
  qa_client_id client = identity->client;
  owned_info info = {NULL, 0};
  info_of(s, client, &info.items, &info.count);
  qa_cleanup scope; qa_cleanup_push(&scope, owned_info_free, &info);
  if (s->quakeworld) {
    if (team != NULL && strpbrk(team, "\\\"\n\r") != NULL) fail("Original QW team userinfo is invalid");
    if (team == NULL) info_remove(info.items, &info.count, "team"); else info_put(&info.items, &info.count, "team", team);
  } else {
    double value = team == NULL ? NAN : qa_js_string_to_number(team);
    if (!(value == trunc(value) && isfinite(value)) || value < 1 || value > 14) fail("Team has no original Quake color command");
    info_put(&info.items, &info.count, "bottomcolor", QA_JS_NUMBER(value - 1));
    fset(s, at(s, slot), "team", value);
  }
  qa_app_sim_quakec_source_set_client_info(s, client, (qa_app_sim_quakec_info){info.items, info.count});
  qa_cleanup_pop(&scope);
  owned_info_free(&info);
}
double qa_app_sim_quakec_source_match_score(S *s, qa_actor_id actor) {
  return qa_compat_qc_words_float(qa_compat_qc_entity_memory_from_reference(&s->entities, reference(s, actor)), field(s, "frags"));
}
void qa_app_sim_quakec_source_set_match_score(S *s, qa_actor_id actor, double score) {
  if (!isfinite(score)) fail("QuakeC score must be finite");
  qa_compat_qc_words_set_float(qa_compat_qc_entity_memory_from_reference(&s->entities, reference(s, actor)), field(s, "frags"), score);
}
const char *qa_app_sim_quakec_source_death_type(S *s, qa_actor_id actor) {
  const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(s->program, "deathtype");
  if (!f) return "";
  set_scratch(&s->death_scratch, qstring(s, qa_compat_qc_words_int(
      qa_compat_qc_entity_memory_from_reference(&s->entities, reference(s, actor)), f->offset)));
  return s->death_scratch;
}
void qa_app_sim_quakec_source_set_client_info_storage(S *s, qa_client_id client, qa_app_sim_quakec_info values) {
  if (client.slot < 0 || (double)client.slot >= s->options.max_clients) fail("QC userinfo slot is unavailable");
  info_set(s, client.slot + 1, values);
}
void qa_app_sim_quakec_source_set_client_info(S *s, qa_client_id client, qa_app_sim_quakec_info values) {
  qa_app_sim_quakec_source_set_client_info_storage(s, client, values);
  qa_owned_actor actor;
  if (slot_actor(s, client.slot + 1, &actor) && (active(s, actor.id) || slot_set_has(&s->prepared_clients, client.slot + 1))) {
    const char *name = NULL;
    for (size_t i = 0; i < values.count; i++) if (strcmp(values.items[i].name, "name") == 0) name = values.items[i].value;
    char truncated[32];
    (void)snprintf(truncated, sizeof truncated, "%s", name ? name : "unnamed"); /* .slice(0, 31) */
    char engine[48];
    (void)snprintf(engine, sizeof engine, "%s-name:%lld", s->quakeworld ? "qw" : "nq", (long long)(client.slot + 1));
    qa_compat_qc_words_set_int(at(s, client.slot + 1), field(s, "netname"),
        (double)qa_compat_qc_strings_set_engine(&s->machine.strings, engine, truncated, 32));
  }
}
qa_app_sim_quakec_info qa_app_sim_quakec_source_client_info(S *s, qa_client_id client) {
  info_entry *entry = info_get(s, client.slot + 1);
  return (qa_app_sim_quakec_info){entry ? entry->items : NULL, entry ? entry->count : 0};
}
void qa_app_sim_quakec_source_set_client_network(S *s, qa_client_id client, qa_app_sim_quakec_client_network info) {
  int64_t slot = client.slot + 1;
  for (size_t i = 0; i < s->network_count; i++) if (s->networks[i].slot == slot) { s->networks[i].info = info; return; }
  s->networks = xgrow(s->networks, &s->network_capacity, s->network_count + 1, sizeof *s->networks);
  s->networks[s->network_count++] = (network_entry){slot, info};
}
static network_entry *network_get(S *s, int64_t slot) {
  for (size_t i = 0; i < s->network_count; i++) if (s->networks[i].slot == slot) return &s->networks[i];
  return NULL;
}

/* ---------------- local NetQuake presentation ---------------- */
typedef struct local_host_ctx { S *s; const qa_actor_id *target; } local_host_ctx;
static qa_actor_id lh_actor(void *ctx, double slot) {
  S *s = ((local_host_ctx *)ctx)->s;
  qa_owned_actor actor;
  if (!slot_actor(s, (int64_t)slot, &actor)) fail("QC service actor %s is not live", QA_JS_NUMBER(slot));
  return actor.id;
}
static void lh_camera(void *ctx, qa_actor_id actor, qa_vec3 *origin, qa_vec3 *angles) {
  S *s = ((local_host_ctx *)ctx)->s;
  int64_t slot;
  if (!source_slot(s, actor, &slot)) fail("Missing QC camera actor");
  *origin = vget(s, at(s, slot), "origin");
  *angles = vget(s, at(s, slot), "angles");
}
static const char *lh_name(S *s, const char *kind, double index) {
  size_t count = 0;
  const char *const *names = qa_app_sim_quakec_source_precache_names(s, kind, &count);
  if (!(index >= 1) || index - 1 >= (double)count || index != trunc(index))
    fail("Unknown QC service %s %s", kind, QA_JS_NUMBER(index));
  return names[(size_t)index - 1];
}
static const char *lh_sound(void *ctx, double index) { return lh_name(((local_host_ctx *)ctx)->s, "sound", index); }
static const char *lh_model(void *ctx, double index) { return lh_name(((local_host_ctx *)ctx)->s, "model", index); }
static void lh_emit(void *ctx, const qa_q1_event *event, const qa_actor_id *recipient) {
  local_host_ctx *h = ctx;
  emit_q1(h->s, *event, recipient ? recipient : h->target);
}
static void lh_message(void *ctx, qa_network_event event, const qa_actor_id *actor) {
  present_message(((local_host_ctx *)ctx)->s, event, actor);
}
static qa_app_sim_source_presentation_event source_of(const char *kind) {
  qa_app_sim_source_presentation_event e; memset(&e, 0, sizeof e);
  e.kind = kind;
  return e;
}
static void lh_music(void *ctx, double track) {
  local_host_ctx *h = ctx;
  qa_app_sim_source_presentation_event e = source_of("music"); e.music.kind = "cd-track"; e.music.track = track;
  emit_presentation(h->s, e, h->target);
}
static void lh_angles(void *ctx, qa_actor_id actor, qa_vec3 angles) {
  local_host_ctx *h = ctx;
  qa_app_sim_source_presentation_event e = source_of("view-reset");
  e.view_reset.reason = "source"; e.view_reset.actor = actor; e.view_reset.angles = angles;
  emit_presentation(h->s, e, h->target);
}
static void lh_pause(void *ctx, bool paused) {
  local_host_ctx *h = ctx;
  qa_app_sim_source_presentation_event e = source_of("music"); e.music.kind = "pause"; e.music.paused = paused;
  emit_presentation(h->s, e, h->target);
}
static void lh_sky(void *ctx, const char *name, const qa_actor_id *recipient) {
  qa_app_sim_source_presentation_event e = source_of("q1-sky"); e.q1_sky.kind = "skybox"; e.q1_sky.name = name;
  emit_presentation(((local_host_ctx *)ctx)->s, e, recipient);
}
static void lh_client_metadata(void *ctx, qa_app_sim_q1_client_metadata_event event, const qa_actor_id *recipient) {
  qa_app_sim_source_presentation_event e = source_of("q1-client"); e.q1_client = event;
  emit_presentation(((local_host_ctx *)ctx)->s, e, recipient);
}
static void lh_session(void *ctx, const char *kind, const qa_actor_id *recipient) {
  qa_app_sim_source_presentation_event e = source_of("q1-session"); e.q1_session.kind = kind;
  emit_presentation(((local_host_ctx *)ctx)->s, e, recipient);
}
static void emit_composition(S *s, const qa_comp_q1_event *composition, const qa_actor_id *recipient) {
  qa_app_sim_source_presentation_event e = source_of("q1-composition"); e.q1_composition = composition;
  emit_presentation(s, e, recipient);
}
/* TS prompt: Extract<Q1CompositionEvent, prompt|clear-prompt>, to its actor. */
static void lh_prompt(void *ctx, qa_app_sim_quakec_prompt value) {
  qa_comp_q1_event composition; memset(&composition, 0, sizeof composition);
  composition.actor = value.actor;
  qa_comp_q1_prompt_choice *choices = NULL;
  if (is(value.kind, "prompt")) {
    composition.kind = QA_COMP_Q1_EVENT_PROMPT;
    composition.title = value.title;
    if (value.choice_count) choices = xalloc(value.choice_count * sizeof *choices);
    for (size_t i = 0; i < value.choice_count; i++) { choices[i].label = value.labels[i]; choices[i].impulse = value.impulses[i]; }
    composition.choices = choices; composition.choice_count = value.choice_count;
  } else composition.kind = QA_COMP_Q1_EVENT_CLEAR_PROMPT;
  emit_composition(((local_host_ctx *)ctx)->s, &composition, &value.actor);
  free(choices);
  qa_app_sim_quakec_prompt_free(&value);
}
/* TS fog: q1-composition addon fog {player: recipient, density, color,
 * duration: max(0, transitionSeconds), skyFactor: 0.5}. */
static void lh_fog(void *ctx, qa_netquake_message value, const qa_actor_id *recipient) {
  qa_q1ad_event fog; memset(&fog, 0, sizeof fog);
  fog.kind = QA_Q1AD_EVENT_FOG; fog.has_player = recipient != NULL; if (recipient) fog.player = *recipient;
  fog.density = value.density; fog.color_vec = value.color;
  fog.duration = qa_js_max(0, value.transition_seconds); fog.sky_factor = 0.5;
  qa_comp_q1_event composition; memset(&composition, 0, sizeof composition);
  composition.kind = QA_COMP_Q1_EVENT_ADDON; composition.addon_event = &fog;
  emit_composition(((local_host_ctx *)ctx)->s, &composition, recipient);
}
static qa_netquake_message *retain_owned(S *s, qa_netquake_message *owned, size_t count) {
  s->retained = xgrow(s->retained, &s->retained_capacity, s->retained_count + 1, sizeof *s->retained);
  s->retained_counts = xgrow(s->retained_counts, &s->retained_counts_capacity, s->retained_count + 1, sizeof *s->retained_counts);
  s->retained[s->retained_count] = owned; s->retained_counts[s->retained_count++] = count;
  return owned;
}
static const qa_netquake_message *retain_messages(S *s, const qa_netquake_message *messages, size_t count) {
  qa_netquake_message *copy = xalloc((count ? count : 1) * sizeof *copy);
  for (size_t i = 0; i < count; i++) copy[i] = nq_copy(&messages[i]);
  return retain_owned(s, copy, count);
}
static void receive_local_messages(S *s, const qa_netquake_message *messages, size_t count,
    qa_compat_qc_message_destination destination, const view_entry *views, size_t view_count, bool has_views) {
  if (destination.kind == QA_QC_DEST_MULTICAST) fail("NetQuake has no multicast destination");
  qa_actor_id target_storage = destination.actor;
  const qa_actor_id *target = destination.kind == QA_QC_DEST_CLIENT ? &target_storage : NULL;
  for (size_t index = 0; index < count; index++) {
    const qa_netquake_message *m = &messages[index];
    view_entry view; bool has_view = has_views && view_lookup(views, view_count, (int64_t)index, &view);
    if (is(m->kind, "set-view") && !has_view) fail("QC camera message has no captured source actor");
    m = retain_messages(s, m, 1);
    qa_app_sim_quakec_local_messages_receive(s->local_messages, m, 1, target, has_view,
        has_view && view.has_actor ? &view.actor : NULL);
    local_host_ctx hctx = {s, target};
    qa_owned_actor world = qa_app_sim_quakec_source_world_actor(s);
    qa_app_sim_quakec_local_message_host host; memset(&host, 0, sizeof host);
    host.ctx = &hctx; host.actor = lh_actor;
    host.recipients = s->active_clients.items; host.recipient_count = s->active_clients.count;
    host.source_actor = world.id; host.map = map_path(s); host.seconds = s->current_time;
    host.camera = lh_camera; host.sound = lh_sound; host.model = lh_model; host.emit = lh_emit; host.message = lh_message;
    host.music = lh_music; host.angles = lh_angles; host.pause = lh_pause; host.sky = lh_sky;
    host.client_metadata = lh_client_metadata; host.session = lh_session; host.prompt = lh_prompt; host.fog = lh_fog;
    qa_app_sim_present_quakec_local_message(*m, target, s->local_messages, host);
  }
}

/* ---------------- incoming source damage ---------------- */
static bool same_request(const qa_damage_request *a, const qa_damage_request *b) {
  return same_actor(a->target, b->target) && a->amount == b->amount && a->knockback == b->knockback
    && a->attack.sequence == b->attack.sequence && a->attack.attacker == b->attack.attacker
    && a->attack.inflictor == b->attack.inflictor;
}
static qa_damage_request resolve_request(void *ctx, qa_q1_id1_damage_call call) {
  S *s = ctx;
  if (s->incoming_count) {
    incoming_entry *incoming = &s->incoming[s->incoming_count - 1];
    if (!incoming->entered) { incoming->entered = true; return incoming->request; }
  }
  return s->options.damage_request(s->options.ctx, call);
}
static bool projection_admit(void *ctx, qa_damage_request request) {
  S *s = ctx;
  return s->options.damage_allowed(s->options.ctx, request);
}
static qa_actor_id projection_actor(void *ctx, int64_t ref) {
  S *s = ctx;
  int64_t slot = qa_compat_qc_entity_memory_slot(&s->entities, ref);
  qa_owned_actor actor;
  bool found = qa_compat_qc_borrowed_actors_actor(&s->borrowed, slot, &actor) || slot_actor(s, slot, &actor);
  if (!found || !live(s, actor.id)) fail("QC damage references a free source actor");
  return actor.id;
}
static int64_t projection_reference(void *ctx, const qa_actor_id *actor) {
  S *s = ctx;
  return actor == NULL ? qa_compat_qc_entity_memory_reference(&s->entities, 0) : reference(s, *actor);
}
static void projection_completed(void *ctx, qa_damage_request request, qa_damage_outcome outcome) {
  S *s = ctx;
  if (!s->incoming_count) return;
  incoming_entry *incoming = &s->incoming[s->incoming_count - 1];
  if (same_request(&incoming->request, &request)) { incoming->outcome = outcome; incoming->has_outcome = true; }
}
static int64_t source_call_reference(void *ctx, const qa_actor_id *actor) {
  S *s = ctx;
  return actor == NULL ? 0 : reference(s, *actor);
}
static double source_call_execute(void *ctx, int64_t count) {
  S *s = ctx;
  qa_compat_qc_machine_execute(&s->machine, s->binding->damage_index, count);
  return qa_compat_qc_words_float(&s->machine.globals, 1);
}
typedef struct damage_restore {
  S *s; uint8_t saved[108]; int64_t self, other, time; int32_t previous_self, previous_other, previous_time;
} damage_restore;
static void damage_finally(void *arg) {
  damage_restore *r = arg;
  S *s = r->s;
  s->incoming_count--;
  memcpy(s->machine.globals.bytes + 4, r->saved, sizeof r->saved);
  qa_compat_qc_words_set_int(&s->machine.globals, r->self, r->previous_self);
  qa_compat_qc_words_set_int(&s->machine.globals, r->other, r->previous_other);
  qa_compat_qc_words_set_int(&s->machine.globals, r->time, r->previous_time);
}
static qa_damage_outcome apply_source_damage(S *s, qa_damage_request request) {
  const qa_q1qc_damage_call_layout *call = s->binding->damage_call;
  if (!call->has_declaration && call->parameter_count != 4) fail("QC incoming damage requires its qualified four-argument source ABI");
  if (!isfinite(fround(request.amount)))
    qa_raise("RangeError", 0, 0, "Incoming QC damage must fit its source binary32 ABI");
  int64_t target = reference(s, request.target);
  int64_t attacker = request.attack.attacker == NULL ? 0 : reference(s, *request.attack.attacker);
  int64_t inflictor = request.attack.inflictor == NULL ? 0 : reference(s, *request.attack.inflictor);
  damage_restore r; memset(&r, 0, sizeof r);
  r.s = s;
  memcpy(r.saved, s->machine.globals.bytes + 4, sizeof r.saved);
  r.self = qa_compat_qc_machine_global_offset(&s->machine, "self");
  r.other = qa_compat_qc_machine_global_offset(&s->machine, "other");
  r.time = qa_compat_qc_machine_global_offset(&s->machine, "time");
  r.previous_self = qa_compat_qc_words_int(&s->machine.globals, r.self);
  r.previous_other = qa_compat_qc_words_int(&s->machine.globals, r.other);
  r.previous_time = qa_compat_qc_words_int(&s->machine.globals, r.time);
  s->incoming = xgrow(s->incoming, &s->incoming_capacity, s->incoming_count + 1, sizeof *s->incoming);
  size_t index = s->incoming_count++;
  memset(&s->incoming[index], 0, sizeof s->incoming[index]);
  s->incoming[index].request = request;
  qa_cleanup scope; qa_cleanup_push(&scope, damage_finally, &r);
  qa_compat_qc_words_set_int(&s->machine.globals, r.self, (double)inflictor);
  qa_compat_qc_words_set_int(&s->machine.globals, r.other, (double)target);
  qa_compat_qc_words_set_float(&s->machine.globals, r.time, s->current_time);
  if (!call->has_declaration) {
    qa_compat_qc_words_set_int(&s->machine.globals, 4, (double)target);
    qa_compat_qc_words_set_int(&s->machine.globals, 7, (double)inflictor);
    qa_compat_qc_words_set_int(&s->machine.globals, 10, (double)attacker);
    qa_compat_qc_words_set_float(&s->machine.globals, 13, request.amount);
    qa_compat_qc_machine_execute(&s->machine, s->binding->damage_index, 4);
  } else {
    qa_compat_qc_mod_input_entry inputs[16];
    size_t input_count = qa_compat_qc_damage_inputs(request, s->current_time, inputs, 16);
    (void)qa_compat_qc_with_source_call(&s->machine, *call->declaration, inputs, input_count,
        (qa_compat_qc_actor_reference){s, source_call_reference}, (qa_compat_qc_source_execute){s, source_call_execute});
  }
  incoming_entry entry = s->incoming[index];
  if (!entry.has_outcome) fail("Incoming original QC damage did not complete its authority boundary");
  qa_cleanup_pop(&scope);
  damage_finally(&r);
  return entry.outcome;
}

/* ---------------- admission ---------------- */
static actor_cell *cell_for(S *s, qa_owned_actor actor, int64_t slot) {
  actor_cell *cell = NULL;
  for (size_t i = 0; i < s->cell_count && !cell; i++) {
    actor_cell *c = s->cells[i];
    qa_owned_actor current;
    if (same_owned(c->actor, actor)) cell = c;
    else if (!resolve_owned(s, c->actor.id, &current) || !same_owned(current, c->actor)) cell = c;
  }
  if (!cell) {
    s->cells = xgrow(s->cells, &s->cell_capacity, s->cell_count + 1, sizeof *s->cells);
    cell = xalloc(sizeof *cell);
    s->cells[s->cell_count++] = cell;
  }
  memset(cell, 0, sizeof *cell);
  cell->source = s; cell->actor = actor; cell->slot = slot; cell->live = true;
  return cell;
}
static qa_damage_outcome cell_source_damage(void *ctx, qa_damage_request request) {
  return apply_source_damage(((actor_cell *)ctx)->source, request);
}
static qa_combat_state cell_read(void *ctx) {
  actor_cell *c = ctx; S *s = c->source;
  qa_compat_qc_words *words = at(s, c->slot);
  qa_combat_state state; memset(&state, 0, sizeof state);
  state.health = fget(s, words, "health");
  qa_q1qc_words q = {words->bytes, words->byte_length};
  state.armor = qa_q1_id1_damage_read_armor(s->damage, &q);
  state.mass = 200;
  state.can_take_damage = fget(s, words, "takedamage") != 0;
  state.invulnerable = fget(s, words, "invincible_finished") >= s->current_time;
  const char *team = qa_app_sim_quakec_source_match_team(s, c->actor.id);
  if (team) { (void)snprintf(c->team, sizeof c->team, "%s", team); state.team = c->team; }
  return state;
}
static void cell_write_health(void *ctx, double health) {
  actor_cell *c = ctx;
  fset(c->source, at(c->source, c->slot), "health", health);
}
static void cell_write_traits(void *ctx, qa_combat_traits traits) {
  actor_cell *c = ctx; S *s = c->source;
  qa_compat_qc_words *words = at(s, c->slot);
  const char *mapped = traits.team == NULL ? NULL : s->quakeworld ? traits.team
      : is(traits.team, "red") ? "5" : is(traits.team, "blue") ? "14" : traits.team;
  const char *current = qa_app_sim_quakec_source_match_team(s, c->actor.id);
  bool differs = (mapped == NULL) != (current == NULL) || (mapped && current && strcmp(mapped, current) != 0);
  if (differs) {
    if (active(s, c->actor.id)) qa_app_sim_quakec_source_set_match_team(s, c->actor.id, mapped);
    else if (!s->quakeworld) {
      double value = mapped == NULL ? 0 : qa_js_string_to_number(mapped);
      if (isfinite(value)) fset(s, words, "team", value);
    }
  }
  if (traits.can_take_damage != (fget(s, words, "takedamage") != 0)) fset(s, words, "takedamage", traits.can_take_damage ? 1 : 0);
  if (traits.invulnerable != (fget(s, words, "invincible_finished") >= s->current_time))
    fset(s, words, "invincible_finished", traits.invulnerable ? INFINITY : 0);
}
static void foreign_armor_check(qa_armor_state armor) {
  if (strcmp(armor.powered.kind, "none") != 0 || (armor.regular.kind != QA_REGULAR_ARMOR_NONE && armor.regular.kind != QA_REGULAR_ARMOR_Q1))
    fail("Cannot store foreign armor in native id1 fields");
}
static void cell_validate_armor(void *ctx, qa_armor_state armor) { (void)ctx; foreign_armor_check(armor); }
static void cell_write_armor(void *ctx, qa_armor_state armor) {
  actor_cell *c = ctx; S *s = c->source;
  foreign_armor_check(armor);
  qa_compat_qc_words *words = at(s, c->slot);
  fset(s, words, "armorvalue", armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.regular.points);
  fset(s, words, "armortype", armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : armor.regular.absorption);
  int32_t green = ibits(s->binding->armor_masks[0]), yellow = ibits(s->binding->armor_masks[1]), red = ibits(s->binding->armor_masks[2]);
  int32_t mask = green | yellow | red;
  int32_t bit = armor.regular.kind == QA_REGULAR_ARMOR_NONE ? 0 : is(armor.regular.item, "q1:item_armorInv") ? red
      : is(armor.regular.item, "q1:item_armor2") ? yellow : green;
  int64_t f = field(s, s->binding->armor_field);
  qa_compat_qc_words_set_float(words, f, (double)((ibits(trunc(qa_compat_qc_words_float(words, f))) & ~mask) | bit));
}
static qa_regular_armor_state cell_empty_armor(void *ctx, double points) {
  actor_cell *c = ctx;
  qa_q1qc_empty_armor empty = qa_q1qc_empty_armor_for(c->source->qprogram,
      c->source->prepared->combat_declaration ? c->source->prepared->combat_declaration->empty_armor : NULL);
  return qa_q1qc_empty_armor_apply(empty, points);
}
static void cell_think(void *ctx, qa_owned_actor self, qa_frame_context frame) {
  actor_cell *c = ctx;
  (void)self; /* TS `(_actor, frame) => this.runThink(actor, frame)` runs the admitted actor */
  qa_app_sim_quakec_source_run_think(c->source, c->actor, frame);
}
typedef struct physics_restore { S *s; bool had; qa_q1_id1_physics_callback prior; } physics_restore;
static void physics_finally(void *arg) {
  physics_restore *r = arg;
  r->s->has_physics_callback = r->had; r->s->physics_callback = r->prior;
}
static void cell_touch(void *ctx, qa_touch_contact contact) {
  actor_cell *c = ctx; S *s = c->source;
  int64_t current;
  if (!source_slot(s, contact.self.id, &current)) return;
  qa_compat_qc_words *source = at(s, current);
  int32_t callback = qa_compat_qc_words_int(source, field(s, "touch"));
  if (callback != 0 && fget(s, source, "solid") != 0) {
    physics_restore r = {s, s->has_physics_callback, s->physics_callback};
    s->has_physics_callback = true;
    s->physics_callback = (qa_q1_id1_physics_callback){"touch", contact.self.id, contact.other, callback};
    qa_cleanup scope; qa_cleanup_push(&scope, physics_finally, &r);
    invoke(s, callback, current, reference(s, contact.other), s->current_time);
    qa_cleanup_pop(&scope);
    physics_finally(&r);
  }
}
static void admit(void *ctx, qa_owned_actor actor, int64_t slot) {
  S *s = ctx;
  actor_cell *cell = cell_for(s, actor, slot);
  qa_source_armor_stage powered, regular;
  bool has_powered = qa_q1_id1_damage_protection_stage(s->damage, actor, QA_PROTECTION_POWERED, &powered);
  bool has_regular = qa_q1_id1_damage_protection_stage(s->damage, actor, QA_PROTECTION_REGULAR, &regular);
  qa_q1qc_empty_armor empty = qa_q1qc_empty_armor_for(s->qprogram,
      s->prepared->combat_declaration ? s->prepared->combat_declaration->empty_armor : NULL);
  qa_combat_state_binding *b = &cell->combat;
  memset(b, 0, sizeof *b);
  b->ctx = cell;
  if (empty.has) b->empty_regular_armor = cell_empty_armor;
  b->source_damage = cell_source_damage;
  b->protection_regular = (qa_combat_protection_declaration){true, false, actor.owner, has_regular, has_regular ? regular : (qa_source_armor_stage){0}};
  b->protection_powered = (qa_combat_protection_declaration){true, true, NULL, has_powered, has_powered ? powered : (qa_source_armor_stage){0}};
  b->read = cell_read; b->write_health = cell_write_health; b->write_traits = cell_write_traits;
  b->validate_armor = cell_validate_armor; b->write_armor = cell_write_armor;
  qa_gameplay_authority_bind(s->options.combat, actor, *b);
  qa_actor_callbacks callbacks; memset(&callbacks, 0, sizeof callbacks);
  callbacks.ctx = cell; callbacks.think = cell_think; callbacks.touch = cell_touch;
  qa_actor_callback_table_bind(s->options.callbacks, actor, callbacks);
  s->options.admit(s->options.ctx, actor, slot, s);
}

/* ---------------- composition callbacks ---------------- */
static void on_release(void *ctx, qa_owned_actor actor) {
  S *s = ctx;
  actor_set_delete(&s->active_clients, actor.id);
  qa_app_sim_quakec_local_messages_retire(s->local_messages, actor.id);
  pending_delete(s, actor.id);
}
static qa_source_time slots_now(void *ctx) { return (qa_source_time){QA_SOURCE_TIME_SECONDS, ((S *)ctx)->current_time}; }
static void slots_unlink(void *ctx, qa_owned_actor actor) {
  qa_shared_body_table_unlink(qa_app_sim_shared_physics_bodies(((S *)ctx)->options.physics), actor);
}
static void slots_exhausted(void *ctx, int64_t slot) { (void)ctx; (void)slot; fail("QC source edicts exhausted"); }
static bool slot_free(void *ctx, int64_t slot) {
  S *s = ctx;
  return s->slots_options.storage.read(s->slots_options.storage.ctx, slot).free;
}
static int64_t borrowed_solid(void *ctx, qa_actor_id actor) {
  S *s = ctx;
  qa_app_sim_shared_solid solid;
  if (!qa_app_sim_shared_physics_solid_of_actor(s->options.physics, actor, &solid) || is(solid.solid, "none")) return 0;
  if (is(solid.solid, "trigger")) return 1;
  if (is(solid.solid, "brush")) return 4;
  return solid.has_monster && solid.monster ? 3 : 2;
}
static const char *borrowed_classname(void *ctx, qa_actor_id actor) {
  S *s = ctx;
  if (!s->options.foreign_classname) fail("QC borrowed actor requires source classname metadata");
  return s->options.foreign_classname(s->options.ctx, actor);
}
static qa_compat_qc_machine *borrowed_machine(void *ctx) { return &((S *)ctx)->machine; }
static bool borrowed_combat(void *ctx, qa_actor_id actor, double *health, bool *can_take_damage) {
  qa_combat_state_result state = qa_gameplay_authority_read(((S *)ctx)->options.combat, actor);
  if (!state.has_value) return false;
  *health = state.value.health; *can_take_damage = state.value.can_take_damage;
  return true;
}
static bool world_model(void *ctx, const char *name, qa_compat_qc_spatial_model *out) {
  S *s = ctx;
  memset(out, 0, sizeof *out);
  if (name[0] == '\0') return true;
  model_entry *entry = model_get(s, name);
  if (!entry) return false;
  out->index = entry->index; out->bounds = entry->bounds;
  return true;
}
static int64_t foreign_reference(void *ctx, qa_actor_id actor) { return qa_compat_qc_borrowed_actors_reference(&((S *)ctx)->borrowed, actor); }
static bool foreign_actor(void *ctx, int64_t slot, qa_owned_actor *out) { return qa_compat_qc_borrowed_actors_actor(&((S *)ctx)->borrowed, slot, out); }
static void print_service(void *ctx, const char *text) { print(ctx, text); }
static bool loading_service(void *ctx) { return ((S *)ctx)->spawning; }
static bool client_service(void *ctx, qa_actor_id actor) { return qa_app_sim_quakec_source_is_reserved_client(ctx, actor); }
static bool phs_service(void *ctx) { return qa_cvar_registry_variable_value(((S *)ctx)->cvars, "sv_phs") != 0; }
static bool native_service(void *ctx) { return ((S *)ctx)->netquake_wire_attached; }
static bool local_service(void *ctx) { return ((S *)ctx)->local_messages_started; }
static void qw_route(void *ctx, const qa_compat_qc_routed_message *entries, size_t count, qa_compat_qc_message_destination destination) {
  S *s = ctx;
  if (destination.kind == QA_QC_DEST_SIGNON) {
    s->signon = xgrow(s->signon, &s->signon_capacity, s->signon_count + count, sizeof *s->signon);
    for (size_t i = 0; i < count; i++) { s->signon[s->signon_count] = entries[i]; s->signon[s->signon_count++].message = qw_copy(&entries[i].message); }
    return;
  }
  qa_app_sim_quakec_routed record = {qw_entries_copy(entries, count), count, destination};
  if (qa_event_ring_append(&s->routed, &record) == 0) {
    routed_free(&record);
    qa_raise("RangeError", 0, 0, "Event ring capacity exhausted (8192 undrained records); drain before admitting more");
  }
}
typedef struct queued_size { size_t total; } queued_size;
static void sum_size(const void *record, int64_t id, void *ctx) { (void)id; ((queued_size *)ctx)->total += ((const nq_routed *)record)->size; }
static view_entry *views_of(const qa_netquake_message *messages, size_t count, const qa_actor_id *targets, const bool *has,
                            size_t view_count, size_t *out_count) {
  view_entry *views = xalloc((count ? count : 1) * sizeof *views);
  size_t n = 0;
  if (targets)
    for (size_t i = 0; i < count && i < view_count; i++)
      if (is(messages[i].kind, "set-view")) views[n++] = (view_entry){(int64_t)i, has[i], has[i] ? targets[i] : no_actor()};
  *out_count = n;
  return views;
}
static void nq_route(void *ctx, const qa_netquake_message *messages, size_t count, qa_compat_qc_message_destination destination,
                     const qa_actor_id *targets, const bool *has, size_t view_count) {
  S *s = ctx;
  size_t vcount = 0;
  view_entry *views = views_of(messages, count, targets, has, view_count, &vcount);
  if (destination.kind == QA_QC_DEST_SIGNON) {
    size_t total = s->nq_signon_count + count, length = 0;
    qa_netquake_message *all = xalloc((total ? total : 1) * sizeof *all);
    memcpy(all, s->nq_signon, s->nq_signon_count * sizeof *all);
    memcpy(all + s->nq_signon_count, messages, count * sizeof *all);
    free(qa_compat_qc_broadcast_capture_netquake_messages(&s->messages, all, total, &length));
    free(all);
    for (size_t i = 0; i < vcount; i++) {
      view_entry v = views[i]; v.index += (int64_t)s->nq_signon_count;
      s->nq_signon_views = xgrow(s->nq_signon_views, &s->nq_signon_view_capacity, s->nq_signon_view_count + 1, sizeof *s->nq_signon_views);
      size_t j = 0;
      while (j < s->nq_signon_view_count && s->nq_signon_views[j].index != v.index) j++;
      s->nq_signon_views[j] = v;
      if (j == s->nq_signon_view_count) s->nq_signon_view_count++;
    }
    s->nq_signon = xgrow(s->nq_signon, &s->nq_signon_capacity, s->nq_signon_count + count, sizeof *s->nq_signon);
    for (size_t i = 0; i < count; i++) s->nq_signon[s->nq_signon_count++] = nq_copy(&messages[i]);
  } else if (s->netquake_wire_attached || s->spawning || !s->local_messages_started) {
    size_t bytes = 0;
    free(qa_compat_qc_broadcast_capture_netquake_messages(&s->messages, messages, count, &bytes));
    queued_size queued = {0};
    qa_event_ring_for_each_live(&s->nq_routed, sum_size, &queued);
    if ((double)(queued.total + bytes) > 8000 * (s->options.max_clients + 2)) { free(views); fail("NetQuake source output overflow"); }
    nq_routed record = {bytes, xalloc((count ? count : 1) * sizeof(qa_netquake_message)), count, destination, NULL, 0};
    for (size_t i = 0; i < count; i++) record.messages[i] = nq_copy(&messages[i]);
    record.views = xalloc((vcount ? vcount : 1) * sizeof *record.views);
    memcpy(record.views, views, vcount * sizeof *views); record.view_count = vcount;
    if (qa_event_ring_append(&s->nq_routed, &record) == 0) {
      nq_routed_free(&record); free(views);
      qa_raise("RangeError", 0, 0, "Event ring capacity exhausted (8192 undrained records); drain before admitting more");
    }
  }
  if (s->local_messages_started) receive_local_messages(s, messages, count, destination, views, vcount, true);
  free(views);
}
static void presentation_message(void *ctx, qa_network_event event, qa_actor_id actor) {
  S *s = ctx;
  if (!active(s, actor)) fail("QC message requires an admitted client");
  present_message(s, event, &actor);
}
static void presentation_emit(void *ctx, qa_content_id content, qa_q1_event event) {
  S *s = ctx;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &event;
  qa_source_time t = {QA_SOURCE_TIME_SECONDS, 0};
  qa_app_sim_simulation_events_emit(s->options.events, content, source, false, t, false, no_actor());
}
static void presentation_register(void *ctx, qa_content_id content, const char *path, qa_resolved_resource_reference resource) {
  qa_app_sim_simulation_events_register_resource(((S *)ctx)->options.events, content, path, resource);
}
static qa_owned_actor world_actor_at(void *ctx, int64_t slot) { return qa_compat_qc_world_host_actor(&((S *)ctx)->world_host, slot); }
static bool presentation_slot_actor(void *ctx, int64_t slot, qa_actor_id *out) {
  qa_owned_actor actor;
  if (!slot_actor(ctx, slot, &actor)) return false;
  *out = actor.id;
  return true;
}
static bool message_slot_actor(void *ctx, int64_t slot, qa_actor_id *out) {
  S *s = ctx;
  qa_owned_actor actor;
  if (!qa_compat_qc_borrowed_actors_actor(&s->borrowed, slot, &actor) && !slot_actor(s, slot, &actor)) return false;
  *out = actor.id;
  return true;
}
static void broadcast_emit(void *ctx, qa_q1_event effect, const qa_actor_id *recipient) { emit_q1(ctx, effect, recipient); }
static void touch_triggers(void *ctx, qa_owned_actor actor) { qa_app_sim_shared_physics_touch_triggers(((S *)ctx)->options.physics, actor); }
static double server_time(void *ctx) { return ((S *)ctx)->current_time; }
static double vis_point_leaf(void *ctx, qa_vec3 point) { return qa_shared_point_leaf(((S *)ctx)->options.scene, point); }
static double vis_leaf_cluster(void *ctx, double leaf) { return qa_shared_leaf_cluster(((S *)ctx)->options.scene, leaf); }
static bool vis_cluster_visible(void *ctx, double from, double to, const char *kind) {
  return qa_shared_cluster_visible(((S *)ctx)->options.scene, from, to, kind);
}
static double random_integer(void *ctx) { return qa_app_sim_source_random_next_integer(((S *)ctx)->options.random); }
static double random_unit(void *ctx) { return qa_app_sim_source_random_next_unit(((S *)ctx)->options.random); }
static qa_random_state random_checkpoint(void *ctx) {
  S *s = ctx;
  qa_app_sim_source_random_state state = qa_app_sim_source_random_checkpoint(s->options.random);
  qa_random_state out; memset(&out, 0, sizeof out);
  if (is(state.kind, "q2-rerelease-mt19937")) {
    out.kind = QA_RANDOM_STATE_Q2_RERELEASE_MT19937;
    for (size_t i = 0; i < QA_Q2_RERELEASE_WORDS; i++) s->random_words[i] = state.rerelease.words[i];
    out.words = (qa_double_view){s->random_words, QA_Q2_RERELEASE_WORDS};
    out.index = (double)state.rerelease.index; out.draws = (double)state.rerelease.draws;
    out.module = "msvc-2022-17.6";
  } else {
    out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
    for (size_t i = 0; i < 31; i++) s->random_words[i] = state.words[i];
    out.words = (qa_double_view){s->random_words, 31};
    out.front = state.front; out.rear = state.rear; out.draws = (double)state.draws;
  }
  return out;
}
static bool is_free_entity(void *ctx, int64_t slot) { return qa_compat_qc_world_host_is_free_entity(&((S *)ctx)->world_host, slot); }
static bool server_active(void *ctx) { return !((S *)ctx)->spawning; }

/* host builtins installed by the TS constructor */
static void host_put(S *s, const char *name, qa_compat_qc_builtin fn) {
  for (size_t i = 0; i < s->host_count; i++) if (is(s->host[i].name, name)) { s->host[i].fn = fn; return; }
  if (s->host_count == sizeof s->host / sizeof s->host[0]) qa_range_error("QC host builtin table overflow");
  s->host[s->host_count++] = (qa_compat_qc_host_binding){name, fn};
}
static void host_merge(S *s, const qa_compat_qc_host_binding *bindings, size_t count) {
  for (size_t i = 0; i < count; i++) host_put(s, bindings[i].name, bindings[i].fn);
}
static void b_dprint(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  if (s->has_qw || qa_cvar_registry_variable_value(s->cvars, "developer") != 0) {
    char *text = qa_compat_qc_machine_var_string(vm, 0);
    if (!text) qa_range_error("Out of memory");
    print(s, text); free(text);
  }
}
static void b_cvar(void *ctx, qa_compat_qc_machine *vm) {
  qa_compat_qc_machine_return_float(vm, qa_cvar_registry_variable_value(((S *)ctx)->cvars, qa_compat_qc_machine_arg_string(vm, 0)));
}
static void b_setcolor(void *ctx, qa_compat_qc_machine *vm) {
  (void)ctx;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(vm->entities, qa_compat_qc_machine_arg_int(vm, 0));
  qa_compat_qc_words_set_float(words, qa_compat_qc_machine_field_offset(vm, "colormap"), trunc(qa_compat_qc_machine_arg_float(vm, 1)));
}
static void b_setspawnparms(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  int64_t slot = qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0));
  qa_owned_actor actor, owned; bool has_actor = slot_actor(s, slot, &actor);
  identity_entry *client = identity_get(s, slot);
  spawn_entry *parameters = spawn_get(s, slot);
  if (slot < 1 || (double)slot > s->options.max_clients || !has_actor || !resolve_owned(s, actor.id, &owned)
      || !same_owned(owned, actor) || !client || client->client.slot + 1 != slot || !parameters)
    fail("QC setspawnparms requires a current source client");
  for (int index = 0; index < 16; index++) {
    char name[16]; (void)snprintf(name, sizeof name, "parm%d", index + 1);
    int64_t offset;
    if (qa_compat_qc_machine_global_offset_or_null(vm, name, &offset)) qa_compat_qc_words_set_float(&vm->globals, offset, parameters->values[index]);
  }
}
static void b_changelevel(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  if (s->change_level_issued) return;
  s->change_level_issued = true;
  char *map = xstrdup(qa_compat_qc_machine_arg_string(vm, 0));
  s->options.change_level(s->options.ctx, map);
  free(map);
}
static void b_cvar_set(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  char *name = xstrdup(qa_compat_qc_machine_arg_string(vm, 0));
  char *value = xstrdup(qa_compat_qc_machine_arg_string(vm, 1));
  qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  bool set = qa_cvar_registry_set(s->cvars, name, value, false, &snapshot);
  if (set) qa_cvar_read_free(&snapshot);
  if (strcmp(name, "sv_gravity") == 0) qa_app_sim_shared_physics_set_world_gravity(s->options.physics, qa_cvar_registry_variable_value(s->cvars, name));
  free(name); free(value);
}
static double aim_threshold(void *ctx) { return qa_cvar_registry_variable_value(((S *)ctx)->cvars, "sv_aim"); }
static double aim_teamplay(void *ctx) { return qa_cvar_registry_variable_value(((S *)ctx)->cvars, "teamplay"); }
static const qa_compat_qc_aim_target_ref *aim_targets(void *ctx, size_t *out_count) {
  S *s = ctx;
  s->aim_target_count = 0;
  for (int64_t slot = 1; slot < qa_compat_qc_entity_memory_get_count(&s->entities); slot++) {
    qa_owned_actor actor;
    if (!slot_actor(s, slot, &actor) || !live(s, actor.id)) continue;
    s->aim_targets = xgrow(s->aim_targets, &s->aim_target_capacity, s->aim_target_count + 1, sizeof *s->aim_targets);
    s->aim_targets[s->aim_target_count++] = (qa_compat_qc_aim_target_ref){actor.id, qa_compat_qc_entity_memory_reference(&s->entities, slot)};
  }
  *out_count = s->aim_target_count;
  return s->aim_targets;
}
static void b_aim(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  if (s->quakeworld) {
    const char *noaim = info_value(info_get(s, qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0))), "noaim");
    if (qa_native_atoi(noaim ? noaim : "0") > 0) {
      qa_compat_qc_machine_return_vector(vm, qa_compat_qc_words_vector(&vm->globals, qa_compat_qc_machine_global_offset(vm, "v_forward")));
      return;
    }
  }
  s->aim_builtin.fn(s->aim_builtin.ctx, vm);
}
static void b_infokey(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  int64_t slot = qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0));
  char *key = xstrdup(qa_compat_qc_machine_arg_string(vm, 1));
  network_entry *network = slot > 0 && info_get(s, slot) ? network_get(s, slot) : NULL;
  char *owned = NULL; char ping[40];
  const char *value;
  if (strcmp(key, "ip") == 0 && network) value = network->info.address(network->info.ctx);
  else if (strcmp(key, "ping") == 0 && network) { qa_net_js_number(network->info.ping(network->info.ctx), ping, sizeof ping); value = ping; }
  else {
    value = info_value(info_get(s, slot), key);
    if (!value) {
      if (slot == 0) {
        owned = qa_cvar_registry_variable_string(s->cvars, key);
        value = owned ? owned : "";
        if (value[0] == '\0') { /* `|| (localInfo.get(key) ?? "")` */
          const char *local = qa_cvar_registry_local_info_get(s->cvars, key);
          value = local != NULL ? local : "";
        }
      } else value = "";
    }
  }
  char engine[512]; (void)snprintf(engine, sizeof engine, "qw-infokey:%lld:%s", (long long)slot, key);
  qa_compat_qc_machine_return_int(vm, (double)qa_compat_qc_strings_set_engine(&vm->strings, engine, value, 1024));
  free(owned); free(key);
}
static void b_logfrag(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  qa_owned_actor killer, victim;
  bool has_killer = slot_actor(s, qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0)), &killer);
  bool has_victim = slot_actor(s, qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 1)), &victim);
  if (has_killer && has_victim && qa_app_sim_quakec_source_is_reserved_client(s, killer.id) && qa_app_sim_quakec_source_is_reserved_client(s, victim.id)) {
    qa_app_sim_quakec_frag frag = {killer.id, victim.id};
    (void)qa_event_ring_admit(&s->frag_records, &frag);
  }
}
static void b_remove(void *ctx, qa_compat_qc_machine *vm) {
  S *s = ctx;
  qa_owned_actor borrowed;
  if (qa_compat_qc_borrowed_actors_actor(&s->borrowed, qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0)), &borrowed))
    fail("Original QC cannot remove a borrowed actor");
  s->remove_builtin.fn(s->remove_builtin.ctx, vm);
}

/* ---------------- function/inline boundary composition ---------------- */
static bool weapon_selected(void *ctx, int64_t ref) {
  S *s = ctx;
  qa_owned_actor actor;
  if (!slot_actor(s, qa_compat_qc_entity_memory_slot(&s->entities, ref), &actor) || !active(s, actor.id)) return true;
  return primary_selected(s, actor.id);
}
static bool pickup_primary_selected(void *ctx, qa_owned_actor actor) { return primary_selected(ctx, actor.id); }
static bool pickup_owns_weapon(void *ctx, qa_owned_actor actor, qa_item_id item) {
  S *s = ctx;
  return s->options.owns_weapon(s->options.ctx, actor.id, item);
}
static const qa_q1qc_pickup_policy *pickup_policy(void *ctx) {
  S *s = ctx;
  return s->options.pickup_policy(s->options.ctx);
}
/* TS spawn notification wrap around sourceFunctions. */
static void spawn_run(void *ctx, qa_q1qc_call_site call, qa_q1qc_function_execution execute) {
  S *s = ctx;
  const qa_q1qc_function_boundary *source = &s->pickup_functions.boundary;
  if (call.function_index != s->spawn_function) { source->run(source->ctx, call, execute); return; }
  int64_t ref = qa_q1qc_client_stage_self(qmachine(s), &s->weapon_stage->client.spawn);
  qa_owned_actor actor; bool has_actor = slot_actor(s, qa_compat_qc_entity_memory_slot(&s->entities, ref), &actor);
  if (qa_q1qc_boundary_has(source, call.function_index)) source->run(source->ctx, call, execute);
  else qa_q1qc_execution_run(&execute);
  qa_owned_actor owned;
  if (has_actor && resolve_owned(s, actor.id, &owned) && same_owned(owned, actor) && active(s, actor.id))
    s->options.client_spawned(s->options.ctx, actor);
}
static void machine_functions_run(void *ctx, qa_compat_qc_call_site call, qa_compat_qc_function_execution execute) {
  S *s = ctx;
  qa_compat_qc_run_q1qc_function(s->top_functions, qmachine, s, call, execute);
}
static void machine_regions_run(void *ctx, qa_compat_qc_inline_region region, qa_compat_qc_inline_continuation execute) {
  S *s = ctx;
  qa_compat_qc_run_q1qc_region(s->top_regions, region, execute);
}
static void observe_call(void *ctx, qa_compat_qc_call_site call) {
  S *s = ctx;
  qa_q1_id1_pickup_binding_validate(s->pickups);
  qa_q1_id1_damage_observe_call(s->damage, (qa_q1qc_call_site){call.function_index, call.caller, call.statement});
}
static void validate_entity_access(void *ctx, int64_t ref, int64_t word, int64_t words, const char *kind) {
  S *s = ctx;
  qa_q1_id1_pickup_binding_validate(s->pickups);
  qa_compat_qc_borrowed_actors_access(&s->borrowed, ref, word, words, kind);
}
static void observe_entity_store(void *ctx, qa_compat_qc_entity_store_observation store) {
  S *s = ctx;
  qa_q1qc_entity_store_observation q = {store.function_index, store.statement, store.reference, store.word,
                                        store.before, store.before_length, store.after, store.after_length};
  qa_q1_id1_pickup_binding_observe_store(s->pickups, q);
  qa_q1_id1_projectile_attacks_observe_store(s->projectiles, q);
  qa_compat_qc_borrowed_actors_observe_store(&s->borrowed, store.reference, store.word, store.after, store.after_length);
  qa_q1_id1_damage_observe_entity_store(s->damage, q);
}
static bool actor_state_slot(void *ctx, qa_actor_id actor, int64_t *out) { return source_slot(ctx, actor, out); }
static bool actor_state_reference(void *ctx, int64_t ref, qa_actor_id *out) {
  S *s = ctx;
  qa_owned_actor actor;
  if (!slot_actor(s, qa_compat_qc_entity_memory_slot(&s->entities, ref), &actor)) return false;
  *out = actor.id;
  return true;
}
static bool actor_state_client(void *ctx, qa_actor_id actor) { return qa_app_sim_quakec_source_is_reserved_client(ctx, actor); }

/* pushers: TS {...pushers, blocked} forwards every leaf to the inner services' own ctx. */
static qa_q1_pusher_services pusher_physical(void *ctx, qa_compat_qc_pusher_projection projection) {
  S *s = ctx;
  /* The candidate buffer is the env's scratch (freed with the source); a new projection drops the previous one. */
  free(s->pusher_env.candidates);
  s->pusher_env = (qa_app_sim_q1_pusher_env){s->options.physics, projection.ctx, projection.read, projection.write,
                                              projection.link, projection.blocked, NULL, 0};
  return qa_app_sim_shared_physics_q1_pusher_services(&s->pusher_env);
}
static bool pusher_foreign_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  return qa_app_sim_shared_physics_read_q1_pusher(((S *)ctx)->options.physics, actor, out);
}
static void pusher_foreign_write(void *ctx, qa_q1_physics_entity entity) {
  qa_app_sim_shared_physics_write_q1_pusher(((S *)ctx)->options.physics, entity);
}
static bool ps_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) { S *s = ctx; return s->pushers.read(s->pushers.ctx, actor, out); }
static const qa_actor_id *ps_candidates(void *ctx, size_t *count) { S *s = ctx; return s->pushers.candidates(s->pushers.ctx, count); }
static void ps_write(void *ctx, qa_q1_physics_entity entity) { S *s = ctx; s->pushers.write(s->pushers.ctx, entity); }
static void ps_link(void *ctx, qa_owned_actor actor, bool touch) { S *s = ctx; s->pushers.link(s->pushers.ctx, actor, touch); }
static void ps_collision_enabled(void *ctx, qa_owned_actor actor, bool enabled) { S *s = ctx; s->pushers.collision_enabled(s->pushers.ctx, actor, enabled); }
static qa_trace_hit ps_test_position(void *ctx, qa_q1_physics_entity entity) { S *s = ctx; return s->pushers.test_position(s->pushers.ctx, entity); }
static qa_q1_push_result ps_push(void *ctx, qa_q1_physics_entity entity, qa_vec3 displacement) { S *s = ctx; return s->pushers.push(s->pushers.ctx, entity, displacement); }
static void ps_think(void *ctx, qa_owned_actor pusher) { S *s = ctx; s->pushers.think(s->pushers.ctx, pusher); }
static void ps_blocked(void *ctx, qa_owned_actor actor, qa_actor_id other) {
  S *s = ctx;
  int64_t slot;
  if (!source_slot(s, actor.id, &slot)) fail("Missing live blocked QC actor");
  physics_restore r = {s, s->has_physics_callback, s->physics_callback};
  s->has_physics_callback = true;
  s->physics_callback = (qa_q1_id1_physics_callback){"blocked", actor.id, other, qa_compat_qc_words_int(at(s, slot), field(s, "blocked"))};
  qa_cleanup scope; qa_cleanup_push(&scope, physics_finally, &r);
  s->pushers.blocked(s->pushers.ctx, actor, other);
  qa_cleanup_pop(&scope);
  physics_finally(&r);
}

/* ---------------- native weapons ---------------- */
static void native_weapons(S *s) {
  s->weapon_count = 0;
  for (size_t index = 0; index < 8; index++) {
    native_weapon *w = &s->weapons[s->weapon_count++];
    memset(w, 0, sizeof *w);
    w->item = qa_q1_weapon_item(QA_Q1_WEAPONS[index]);
    char label[64];
    (void)snprintf(w->label, sizeof w->label, "%s", qa_q1_weapon_display_name(QA_Q1_WEAPONS[index], label, sizeof label));
    w->bit = index == 0 ? 4096 : (double)(1u << (index - 1));
    w->impulse = (double)index + 1;
  }
  if (!is(s->program->digest, "sha256:35a2fdc3acb04bdafe8d0269f5327cd1d5b47971f1ef024429f1572d3201dc82")) return;
  /* This artifact's W_ChangeWeapon uses 225/226 and toggles grenade/proximity with impulse 6. */
  static const struct { const char *weapon; double bit, impulse; bool has_via; double via; } extra[3] = {
    {"hipnotic:laser", 8388608, 225, false, 0}, {"hipnotic:mjolnir", 128, 226, false, 0}, {"hipnotic:proximity", 65536, 6, true, 16}};
  for (size_t i = 0; i < 3; i++) {
    native_weapon *w = &s->weapons[s->weapon_count++];
    memset(w, 0, sizeof *w);
    w->item = qa_q1_weapon_item(extra[i].weapon);
    char label[64];
    (void)snprintf(w->label, sizeof w->label, "%s", qa_q1_weapon_display_name(extra[i].weapon, label, sizeof label));
    w->bit = extra[i].bit; w->impulse = extra[i].impulse; w->has_via = extra[i].has_via; w->via = extra[i].via;
  }
}
static const native_weapon *weapon_of_item(S *s, qa_item_id item, size_t *index) {
  for (size_t i = 0; i < s->weapon_count; i++)
    if (is(s->weapons[i].item, item)) { if (index) *index = i; return &s->weapons[i]; }
  return NULL;
}

static void restore_constructed(S *s);
static void register_default(S *s, const char *name, const char *value) {
  qa_cvar_read read; memset(&read, 0, sizeof read);
  if (qa_cvar_registry_find(s->cvars, name, &read)) { qa_cvar_read_free(&read); return; }
  qa_cvar_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  if (qa_cvar_registry_register(s->cvars, name, value, 0, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void source_print(void *ctx, const char *text) { print(ctx, text); }

/* ---------------- constructor ---------------- */
S *qa_app_sim_quakec_source_create(const qa_app_sim_prepared_quakec_source *prepared, qa_app_sim_quakec_source_options options) {
  S *s = xalloc(sizeof *s);
  s->prepared = prepared; s->options = options;
  s->program = (qa_compat_qc_program *)&prepared->program;
  s->qprogram = &prepared->view.program;
  s->binding = qa_q1_id1_program_binding_get(s->qprogram, NULL);
  s->quakeworld = is(s->binding->kind, "quakeworld");
  s->spawning = true; s->sound_count = 1;
  s->original_save_extension_text = xstrdup("");
  native_weapons(s);
  bool selected = false;
  for (size_t i = 0; i < options.recipe->execution_count; i++) {
    const qa_execution_module *value = &options.recipe->execution[i];
    if (value->kind == QA_EXECUTION_MODULE_QUAKEC && is(value->owner.provider, prepared->execution.owner.provider)
        && is(value->owner.content, prepared->execution.owner.content)
        && is(artifact_of(value)->digest, artifact_of(&prepared->execution)->digest)) selected = true;
  }
  if (!selected) fail("QC source differs from selected execution");
  if (!isfinite(options.initial_source_time_seconds) || options.initial_source_time_seconds < 0
      || !(options.max_clients == trunc(options.max_clients) && fabs(options.max_clients) <= (double)QA_MAX_SAFE_INTEGER)
      || options.max_clients < 1 || options.max_clients >= 2048)
    fail("Invalid dedicated QC source timing or reserved clients");
  s->current_time = options.initial_source_time_seconds;
  if (s->quakeworld && options.max_clients > 32) fail("Native QuakeWorld supports at most 32 clients");
  s->reserved_client_slots = s->quakeworld ? 32 : (int64_t)options.max_clients;
  if (!(s->local_messages = qa_app_sim_quakec_local_messages_create())) qa_range_error("Out of memory");
  s->release_id = qa_session_actor_registry_on_release(options.actors, on_release, s);
  if (!qa_event_ring_init(&s->frag_records, 4096, sizeof(qa_app_sim_quakec_frag))
      || !qa_event_ring_init(&s->routed, 8192, sizeof(qa_app_sim_quakec_routed))
      || !qa_event_ring_init(&s->nq_routed, 8192, sizeof(nq_routed)))
    qa_range_error("Out of memory");
  if (!qa_compat_qc_entity_memory_init(&s->entities, qa_compat_qc_classic_entity_layout(s->program),
                                       s->quakeworld ? 768 : 2048, s->reserved_client_slots + 1))
    qa_range_error("Out of memory");
  qa_compat_qc_edict_metadata_layout metadata = {0, s->quakeworld ? 100 : 92};
  s->slots_options.provider = prepared->execution.owner.provider;
  s->slots_options.capacity = (double)s->entities.capacity;
  s->slots_options.lifetime = qa_quake_edict_lifetime((double)(s->reserved_client_slots + 1), NULL);
  s->slots_options.storage = qa_compat_qc_create_source_slot_storage(s->program, &s->entities, metadata, &s->slot_storage);
  s->slots_options.ctx = s; s->slots_options.now = slots_now; s->slots_options.unlink = slots_unlink;
  s->slots_options.exhausted = slots_exhausted;
  if (!(s->slots = qa_source_actor_slots_create(options.actors, s->slots_options))) qa_range_error("Out of memory");
  if (!options.restore)
    for (int64_t slot = 0; slot <= s->reserved_client_slots; slot++)
      (void)qa_source_actor_slots_bind_existing(s->slots, slot, slot == 0 ? "quakec:worldspawn" : "quakec:reserved-client");
  size_t world_models = world_model_count(options.world);
  s->model_count = (double)world_models + 1;
  model_set(s, map_path(s), 1, qa_shared_model_bounds(options.scene, 0));
  for (size_t model = 1; model < world_models; model++) {
    char name[32]; (void)snprintf(name, sizeof name, "*%zu", model);
    model_set(s, name, (double)model + 1, qa_shared_model_bounds(options.scene, (double)model));
  }
  qa_shared_body_table *bodies = qa_app_sim_shared_physics_bodies(options.physics);
  qa_compat_qc_borrowed_host borrowed = {options.actors, qa_shared_body_table_as_contract(bodies), s->slots, s->slots_options,
                                         s, borrowed_machine, borrowed_combat, borrowed_classname, borrowed_solid};
  qa_compat_qc_borrowed_actors_init(&s->borrowed, borrowed);
  qa_compat_qc_world_host_options world; memset(&world, 0, sizeof world);
  world.program = s->program; world.entities = &s->entities; world.actors = options.actors; world.slots = s->slots;
  world.slots_options = s->slots_options; world.bodies = bodies; world.scene = qa_shared_as_scene_queries(options.scene);
  world.numeric = qa_q1_donor_profile; world.ctx = s; world.model = world_model;
  world.foreign_reference = foreign_reference; world.foreign_actor = foreign_actor; world.admit = admit;
  if (!qa_compat_qc_world_host_init(&s->world_host, world)) qa_range_error("Out of memory");
  if (options.source_registry) s->cvars = options.source_registry;
  else {
    qa_cvar_registry_options cvar_options; memset(&cvar_options, 0, sizeof cvar_options);
    cvar_options.dialect = s->program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD : QA_COMMAND_DIALECT_Q1_NETQUAKE;
    cvar_options.context.session = qa_session_actor_registry_session(options.actors);
    cvar_options.context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
    cvar_options.print = source_print; cvar_options.print_ctx = s;
    if (!(s->cvars = qa_cvar_registry_create(&cvar_options))) qa_range_error("Out of memory");
    s->owns_cvars = true;
  }
  char skill[40], max_clients[40];
  qa_net_js_number((double)options.skill, skill, sizeof skill);
  qa_net_js_number(options.max_clients, max_clients, sizeof max_clients);
  const struct { const char *name, *value; } defaults[] = {
    {"skill", skill}, {"deathmatch", is(options.mode, "deathmatch") ? "1" : "0"}, {"coop", is(options.mode, "coop") ? "1" : "0"},
    {"teamplay", "0"}, {"sv_cheats", "0"}, {"sv_aim", s->quakeworld ? "2" : "0.93"}, {"sv_gravity", "800"},
    {"sv_maxspeed", "320"}, {"samelevel", "0"}, {"timelimit", "0"}, {"fraglimit", "0"}, {"gamecfg", "0"},
    {"registered", "1"}, {"maxclients", max_clients}, {"campaign", "0"}};
  for (size_t i = 0; i < sizeof defaults / sizeof defaults[0]; i++) register_default(s, defaults[i].name, defaults[i].value);
  qa_compat_qc_client_host_services client; memset(&client, 0, sizeof client);
  client.scene = (qa_q1_client_visibility_scene){s, options.world.kind == QA_DECODED_WORLD_Q1 ? options.world.as.q1->leaf_count : 0,
                                                 vis_point_leaf, vis_leaf_cluster, vis_cluster_visible};
  client.max_clients = s->reserved_client_slots; client.ctx = s; client.server_time = server_time;
  if (!qa_compat_qc_client_host_init(&s->clients, &s->world_host, client)) qa_range_error("Out of memory");
  if (s->quakeworld) {
    s->has_qw = true;
    s->qw = (qa_compat_qc_quakeworld_message_services){s, loading_service, client_service, phs_service, qw_route};
  } else {
    s->has_nq = true;
    s->nq = (qa_compat_qc_netquake_message_services){s, prepared->message_dialect ? prepared->message_dialect : "known-retail", true,
                                                     native_service, local_service, loading_service, client_service, nq_route};
  }
  s->presentation_world = (qa_compat_qc_presentation_world){s, s->program, &s->entities, s->world_host.host_entries,
                                                            s->world_host.host_count, world_actor_at, presentation_slot_actor};
  qa_compat_qc_presentation_services presentation; memset(&presentation, 0, sizeof presentation);
  presentation.ctx = s; presentation.qw = s->has_qw ? &s->qw : NULL; presentation.nq = s->has_nq ? &s->nq : NULL;
  presentation.content = content_of(s); presentation.precache = precache_service; presentation.lookup = lookup_service;
  presentation.loading = loading_service; presentation.print = print_service; presentation.message = presentation_message;
  presentation.emit = presentation_emit; presentation.register_resource = presentation_register;
  qa_compat_qc_presentation_bindings presentation_bindings =
      qa_compat_qc_create_presentation_bindings(&s->presentation_world, presentation, &s->presentation_state);
  s->random = (qa_random_source){s, random_integer, random_unit, random_checkpoint};
  qa_compat_qc_movement_services movement; memset(&movement, 0, sizeof movement);
  movement.scene = qa_shared_as_scene_queries(options.scene); movement.random = s->random;
  movement.ctx = s; movement.touch_triggers = touch_triggers;
  qa_compat_qc_movement_bindings movement_bindings = qa_compat_qc_create_movement_bindings(&s->world_host, movement, &s->movement_state);
  s->message_world = (qa_compat_qc_presentation_world){s, s->program, &s->entities, s->world_host.host_entries,
                                                       s->world_host.host_count, world_actor_at, message_slot_actor};
  if (!qa_compat_qc_broadcast_messages_init(&s->messages, &s->message_world, s, broadcast_emit,
                                            s->has_qw ? &s->qw : NULL, s->has_nq ? &s->nq : NULL))
    qa_range_error("Out of memory");
  qa_compat_qc_broadcast_messages_set_dropped(&s->messages, s, print_service);
  host_merge(s, s->world_host.host_entries, s->world_host.host_count);
  host_merge(s, presentation_bindings.entries, presentation_bindings.count);
  host_merge(s, movement_bindings.entries, movement_bindings.count);
  host_merge(s, s->clients.host_entries, s->clients.host_count);
  host_merge(s, s->messages.host_entries, s->messages.host_count);
  register_default(s, "developer", "0");
  host_put(s, "dprint", (qa_compat_qc_builtin){s, b_dprint});
  host_put(s, "cvar", (qa_compat_qc_builtin){s, b_cvar});
  host_put(s, "setcolor", (qa_compat_qc_builtin){s, b_setcolor});
  host_put(s, "setspawnparms", (qa_compat_qc_builtin){s, b_setspawnparms});
  host_put(s, "changelevel", (qa_compat_qc_builtin){s, b_changelevel});
  host_put(s, "cvar_set", (qa_compat_qc_builtin){s, b_cvar_set});
  s->aim_state = (qa_compat_qc_aim_state){&s->world_host, s, aim_threshold, aim_teamplay, aim_targets, NULL};
  s->aim_builtin = qa_compat_qc_create_aim_binding(&s->world_host, &s->aim_state);
  host_put(s, "aim", (qa_compat_qc_builtin){s, b_aim});
  if (s->quakeworld) {
    qa_app_sim_register_quakeworld_engine_cvars(s->cvars);
    host_put(s, "infokey", (qa_compat_qc_builtin){s, b_infokey});
    host_put(s, "logfrag", (qa_compat_qc_builtin){s, b_logfrag});
  }
  s->numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  s->qmachine.fail = qm_fail; s->qmachine.execute = qm_execute; s->qmachine.execute_region = qm_execute_region;
  s->qmachine.strings_get = qm_strings_get; s->qmachine.strings_set_engine = qm_strings_set_engine;
  (void)qmachine(s);
  s->qsource = (qa_q1qc_source){(qa_q1qc_program *)s->qprogram, &s->qentities, options.actors, s->slots,
                                prepared->execution.owner.provider, s, slot_free};
  s->foreign = (qa_q1qc_foreign_source){s, foreign_actor, foreign_reference};
  if (!(s->attacks = qa_q1_id1_sync_attacks_create_foreign(s->qsource, &s->foreign, qmachine, s))
      || !(s->projectiles = qa_q1_id1_projectile_attacks_create_foreign(s->qsource, &s->foreign, qmachine, s))
      || !(s->environment = qa_q1_id1_environment_create_foreign(s->qsource, &s->foreign, qmachine, s)))
    qa_range_error("Out of memory");
  const qa_mod_qc_combat *combat = prepared->combat_declaration;
  if (combat) qa_compat_qc_validate_mod_combat(s->program, *combat);
  qa_q1_id1_damage_projection projection = {s, options.damage_allowed ? projection_admit : NULL, projection_actor,
                                            projection_reference, NULL, projection_completed};
  s->damage = qa_q1_id1_damage_binding_create(s->qsource, options.combat, qmachine, s, resolve_request, s, &projection,
      combat ? combat->armor_stage : NULL, combat && combat->damage_scale ? &combat->damage : NULL,
      combat ? combat->damage_scale : NULL);
  if (!s->damage) qa_range_error("Out of memory");
  s->pickups = qa_q1_id1_pickup_binding_create(s->qsource, options.pickups, qmachine, s, pickup_primary_selected, s,
      options.owns_weapon ? pickup_owns_weapon : NULL, s, options.pickup_policy ? pickup_policy : NULL, s,
      prepared->has_declared_pickups ? prepared->declared_pickups : NULL, prepared->has_declared_pickups ? prepared->declared_pickup_count : 0);
  if (!s->pickups) qa_range_error("Out of memory");
  s->weapon_stage = qa_app_sim_prepared_quakec_weapon_stage(prepared, &s->owns_weapon_stage);
  if (s->weapon_stage) {
    s->weapon_binding = (qa_q1qc_weapon_stage_binding){s->weapon_stage, qmachine, s, weapon_selected, s};
    s->has_weapon_binding = true;
  }
  qa_q1_id1_sync_attacks_compose(&s->attack_functions, s->attacks, *qa_q1_id1_damage_function_boundary(s->damage));
  qa_q1_id1_projectile_attacks_compose(&s->projectile_functions, s->projectiles, s->attack_functions.boundary);
  qa_q1_id1_pickup_compose_functions(&s->pickup_functions, s->pickups, s->projectile_functions.boundary);
  /* Without a derivable client spawn call the program spawns clients
   * through its own engine-called flow; the notification wrap is skipped. */
  s->functions = &s->pickup_functions.boundary;
  if (options.client_spawned && s->weapon_stage && s->weapon_stage->has_client) {
    s->spawn_function = s->weapon_stage->client.spawn.function_index;
    const qa_q1qc_function_boundary *source = &s->pickup_functions.boundary;
    s->spawn_function_list = xalloc((source->function_count + 1) * sizeof *s->spawn_function_list);
    size_t n = 0;
    for (size_t i = 0; i < source->function_count; i++) s->spawn_function_list[n++] = source->functions[i];
    if (!qa_q1qc_boundary_has(source, s->spawn_function)) s->spawn_function_list[n++] = s->spawn_function;
    s->spawn_functions = (qa_q1qc_function_boundary){s, s->spawn_function_list, n, spawn_run};
    s->has_spawn_wrap = true;
    s->functions = &s->spawn_functions;
  }
  qa_q1_id1_pickup_compose_regions(&s->pickup_regions, s->pickups, *qa_q1_id1_damage_inline_boundary(s->damage));
  s->top_functions = s->functions; s->top_regions = &s->pickup_regions.boundary;
  if (s->has_weapon_binding) {
    qa_q1qc_weapon_compose_functions(&s->weapon_functions, &s->weapon_binding, *s->functions);
    qa_q1qc_weapon_compose_regions(&s->weapon_regions, &s->weapon_binding, s->pickup_regions.boundary);
    s->top_functions = &s->weapon_functions.boundary; s->top_regions = &s->weapon_regions.boundary;
  }
  bool has_remove = false;
  for (size_t i = 0; i < s->host_count; i++) if (is(s->host[i].name, "remove")) { has_remove = true; s->remove_builtin = s->host[i].fn; }
  if (!has_remove) fail("Missing QC remove builtin");
  host_put(s, "remove", (qa_compat_qc_builtin){s, b_remove});
  qa_compat_qc_builtin_services builtins; memset(&builtins, 0, sizeof builtins);
  builtins.kind = qa_compat_qc_host_kind_for_program(s->program); builtins.random = &s->random;
  builtins.ctx = s; builtins.is_free_entity = is_free_entity; builtins.host = s->host; builtins.host_count = s->host_count;
  if (!qa_compat_qc_create_builtins(builtins, &s->registry)) qa_range_error("Out of memory");
  s->machine_functions = (qa_compat_qc_function_boundary){s, s->top_functions->functions, s->top_functions->function_count, machine_functions_run};
  s->machine_region_list = xalloc((s->top_regions->region_count ? s->top_regions->region_count : 1) * sizeof *s->machine_region_list);
  for (size_t i = 0; i < s->top_regions->region_count; i++) {
    qa_q1qc_inline_region r = s->top_regions->regions[i];
    s->machine_region_list[i] = (qa_compat_qc_inline_region){r.function_index, r.entry, r.exit, r.replaceable, r.has_standalone,
                                                            r.standalone_saved, r.standalone_scope};
  }
  s->machine_regions = (qa_compat_qc_inline_boundary){s, s->machine_region_list, s->top_regions->region_count, machine_regions_run};
  qa_compat_qc_machine_options machine; memset(&machine, 0, sizeof machine);
  machine.program = s->program; machine.entities = &s->entities;
  machine.numeric = s->numeric; machine.builtins = &s->registry;
  machine.ctx = s; machine.server_active = server_active;
  machine.function_boundary = &s->machine_functions; machine.observe_call = observe_call;
  machine.inline_boundary = &s->machine_regions; machine.validate_entity_access = validate_entity_access;
  machine.observe_entity_store = observe_entity_store;
  if (!qa_compat_qc_machine_init(&s->machine, machine)) qa_range_error("Out of memory");
  s->has_machine = true;
  qa_compat_qc_actor_state_options state; memset(&state, 0, sizeof state);
  state.machine = &s->machine;
  state.rerelease = starts(options.recipe->engine_behavior.content, "q1:rerelease:");
  state.ctx = s; state.source_slot = actor_state_slot; state.reference = actor_state_reference; state.is_client = actor_state_client;
  qa_compat_qc_actor_state_init(&s->actor_state, state);
  memset(&s->pusher_state, 0, sizeof s->pusher_state);
  s->pusher_state.ctx = s; s->pusher_state.physical = pusher_physical; s->pusher_state.foreign_read = pusher_foreign_read;
  s->pusher_state.foreign_write = pusher_foreign_write; s->pusher_state.touch_triggers = touch_triggers;
  s->pusher_state.server_time = server_time;
  s->pushers = qa_compat_qc_create_pusher_services(&s->world_host, &s->machine, &s->pusher_state);
  s->pusher_services = (qa_q1_pusher_services){s, s->pushers.numeric, ps_read, ps_candidates, ps_write, ps_link,
                                               ps_collision_enabled, ps_test_position, ps_push, ps_blocked,
                                               s->pushers.think ? ps_think : NULL};
  if (!options.restore) {
    for (int64_t slot = 0; slot <= s->reserved_client_slots; slot++) (void)qa_compat_qc_world_host_actor(&s->world_host, slot);
  } else restore_constructed(s);
  return s;
}

/* ---------------- getters ---------------- */
qa_compat_qc_machine *qa_app_sim_quakec_source_machine(S *s) { return &s->machine; }
qa_compat_qc_entity_memory *qa_app_sim_quakec_source_entities(S *s) { return &s->entities; }
qa_source_actor_slots *qa_app_sim_quakec_source_slots(S *s) { return s->slots; }
qa_compat_qc_world_host *qa_app_sim_quakec_source_world_host(S *s) { return &s->world_host; }
qa_cvar_registry *qa_app_sim_quakec_source_cvars(S *s) { return s->cvars; }
qa_compat_qc_broadcast_messages *qa_app_sim_quakec_source_messages(S *s) { return &s->messages; }
qa_q1_id1_environment *qa_app_sim_quakec_source_environment(S *s) { return s->environment; }
const qa_q1_pusher_services *qa_app_sim_quakec_source_pusher_services(S *s) { return &s->pusher_services; }
int64_t qa_app_sim_quakec_source_reserved_client_slots(const S *s) { return s->reserved_client_slots; }
const qa_app_sim_prepared_quakec_source *qa_app_sim_quakec_source_prepared(const S *s) { return s->prepared; }
bool qa_app_sim_quakec_source_current_physics_callback(const S *s, qa_q1_id1_physics_callback *out) {
  if (!s->has_physics_callback) return false;
  *out = s->physics_callback;
  return true;
}
qa_owned_actor qa_app_sim_quakec_source_world_actor(S *s) { return qa_compat_qc_world_host_actor(&s->world_host, 0); }
double qa_app_sim_quakec_source_time_seconds(const S *s) { return s->current_time; }
bool qa_app_sim_quakec_source_loading(const S *s) { return s->spawning; }
qa_compat_qc_program *qa_app_sim_quakec_source_program(S *s) { return s->program; }
qa_execution_module qa_app_sim_quakec_source_execution(S *s) { return s->prepared->execution; }

/* ---------------- clients ---------------- */
static bool spectator(S *s, qa_actor_id actor) { return qa_app_sim_quakec_source_is_spectator_client(s, actor); }
bool qa_app_sim_quakec_source_is_spectator_client(S *s, qa_actor_id actor) {
  int64_t slot;
  return source_slot(s, actor, &slot) && s->quakeworld && slot_set_has(&s->spectator_slots, slot);
}
static void spectator_callback(S *s, const char *name, int64_t slot) {
  const qa_compat_qc_function *callback = function_or_null(s, name);
  if (callback && callback->index != 0) invoke(s, callback->index, slot, 0, s->current_time);
}
static void read_spawn_parameters(S *s, double out[16]) {
  /* Retail mods do not all declare the full parm1..parm16 set (qdoom stops
   * at parm9); absent spawn parms read as zero and are never written back. */
  for (int index = 0; index < 16; index++) {
    char name[16]; (void)snprintf(name, sizeof name, "parm%d", index + 1);
    int64_t offset;
    out[index] = qa_compat_qc_machine_global_offset_or_null(&s->machine, name, &offset)
        ? qa_compat_qc_words_float(&s->machine.globals, offset) : 0;
  }
}
static void write_spawn_parameters(S *s, const double parameters[16]) {
  for (int index = 0; index < 16; index++) {
    char name[16]; (void)snprintf(name, sizeof name, "parm%d", index + 1);
    int64_t offset;
    if (qa_compat_qc_machine_global_offset_or_null(&s->machine, name, &offset))
      qa_compat_qc_words_set_float(&s->machine.globals, offset, parameters[index]);
  }
}
qa_owned_actor qa_app_sim_quakec_source_reserved_client(S *s, qa_client_id client) {
  if (client.slot < 0 || (double)client.slot >= s->options.max_clients || s->spawning) fail("QC reserved client is unavailable");
  int64_t slot = client.slot + 1;
  identity_entry *existing = identity_get(s, slot);
  if (existing && !qa_same_client(existing->client, client)) fail("QC client slot still belongs to an earlier connection");
  if (!existing && !s->quakeworld) {
    qa_compat_qc_words *words = at(s, slot);
    if (strcmp(qstring(s, qa_compat_qc_words_int(words, field(s, "classname"))), "player") == 0) {
      qa_owned_actor previous;
      if (slot_actor(s, slot, &previous)) qa_session_actor_registry_release(s->options.actors, previous);
      qa_compat_qc_words_set_int(words, field(s, "classname"), 0);
    }
  }
  identity_set(s, slot, client);
  if (!spawn_get(s, slot)) {
    invoke(s, function_named(s, "SetNewParms"), 0, 0, s->current_time);
    double parameters[16]; read_spawn_parameters(s, parameters);
    spawn_set(s, slot, parameters);
  }
  return qa_compat_qc_world_host_actor(&s->world_host, slot);
}
void qa_app_sim_quakec_source_set_client_role(S *s, qa_client_id client, const char *role) {
  if (is(role, "spectator") && !s->quakeworld) fail("Spectator clients require the QuakeWorld host ABI");
  qa_owned_actor actor = qa_app_sim_quakec_source_reserved_client(s, client);
  int64_t slot = client.slot + 1;
  if (active(s, actor.id)) fail("QC client role must be selected before begin");
  if (is(role, "spectator")) slot_set_add(&s->spectator_slots, slot); else slot_set_delete(&s->spectator_slots, slot);
}
void qa_app_sim_quakec_source_prepare_client_spawn(S *s, qa_client_id client) {
  qa_owned_actor actor = qa_app_sim_quakec_source_reserved_client(s, client);
  int64_t slot = client.slot + 1;
  if (!s->quakeworld || active(s, actor.id)) fail("QW spawn requires an inactive reserved client");
  qa_compat_qc_words *words = at(s, slot);
  memset(words->bytes, 0, words->byte_length);
  fset(s, words, "colormap", (double)slot); fset(s, words, "team", 0);
  const char *name = info_value(info_get(s, slot), "name");
  char truncated[32]; (void)snprintf(truncated, sizeof truncated, "%s", name ? name : "unnamed");
  char engine[48]; (void)snprintf(engine, sizeof engine, "qw-name:%lld", (long long)slot);
  qa_compat_qc_words_set_int(words, field(s, "netname"), (double)qa_compat_qc_strings_set_engine(&s->machine.strings, engine, truncated, 32));
  int64_t offset;
  if (has_field(s, "gravity", &offset)) qa_compat_qc_words_set_float(words, offset, 1);
  if (has_field(s, "maxspeed", &offset)) qa_compat_qc_words_set_float(words, offset, qa_cvar_registry_variable_value(s->cvars, "sv_maxspeed"));
  slot_set_add(&s->prepared_clients, slot);
}
const qa_app_sim_quakec_frag *qa_app_sim_quakec_source_drain_frag_log(S *s, size_t *out_count) {
  free(s->drained_frags);
  s->drained_frags = qa_event_ring_drain_all(&s->frag_records, out_count);
  return s->drained_frags;
}
static void drained_routed_free(S *s) {
  for (size_t i = 0; i < s->drained_routed_count; i++) routed_free(&s->drained_routed[i]);
  free(s->drained_routed); s->drained_routed = NULL; s->drained_routed_count = 0;
}
static void drained_nq_free(S *s) {
  for (size_t i = 0; i < s->drained_nq_count; i++) nq_routed_free(&s->drained_nq[i]);
  free(s->drained_nq); free(s->drained_nq_view);
  s->drained_nq = NULL; s->drained_nq_view = NULL; s->drained_nq_count = 0;
}
const qa_app_sim_quakec_routed *qa_app_sim_quakec_source_drain_messages(S *s, size_t *out_count) {
  qa_compat_qc_broadcast_messages_flush(&s->messages);
  drained_routed_free(s);
  s->drained_routed = qa_event_ring_drain_all(&s->routed, &s->drained_routed_count);
  *out_count = s->drained_routed_count;
  return s->drained_routed;
}
const qa_compat_qc_routed_message *qa_app_sim_quakec_source_signon_messages(S *s, size_t *out_count) {
  *out_count = s->signon_count;
  return s->signon;
}
void qa_app_sim_quakec_source_attach_netquake_wire(S *s) {
  if (s->quakeworld || s->netquake_wire_attached) fail("NetQuake wire already attached or source ABI differs");
  s->netquake_wire_attached = true;
}
const qa_netquake_message *qa_app_sim_quakec_source_netquake_signon_messages(S *s, size_t *out_count) {
  *out_count = s->nq_signon_count;
  return s->nq_signon;
}
const qa_app_sim_quakec_netquake_routed *qa_app_sim_quakec_source_drain_netquake_messages(S *s, size_t *out_count) {
  if (!s->netquake_wire_attached) fail("NetQuake wire is not attached");
  qa_compat_qc_broadcast_messages_flush(&s->messages);
  drained_nq_free(s);
  s->drained_nq = qa_event_ring_drain_all(&s->nq_routed, &s->drained_nq_count);
  s->drained_nq_view = xalloc((s->drained_nq_count ? s->drained_nq_count : 1) * sizeof *s->drained_nq_view);
  for (size_t i = 0; i < s->drained_nq_count; i++)
    s->drained_nq_view[i] = (qa_app_sim_quakec_netquake_routed){s->drained_nq[i].messages, s->drained_nq[i].count, s->drained_nq[i].destination};
  *out_count = s->drained_nq_count;
  return s->drained_nq_view;
}
const qa_client_id *qa_app_sim_quakec_source_connected_client_identities(S *s, size_t *out_count) {
  free(s->client_scratch);
  s->client_scratch = xalloc((s->identity_count ? s->identity_count : 1) * sizeof *s->client_scratch);
  for (size_t i = 0; i < s->identity_count; i++) s->client_scratch[i] = s->identities[i].client;
  for (size_t i = 1; i < s->identity_count; i++) { /* stable sort by client slot */
    qa_client_id key = s->client_scratch[i]; size_t j = i;
    while (j > 0 && s->client_scratch[j - 1].slot > key.slot) { s->client_scratch[j] = s->client_scratch[j - 1]; j--; }
    s->client_scratch[j] = key;
  }
  *out_count = s->identity_count;
  return s->client_scratch;
}
bool qa_app_sim_quakec_source_has_client(S *s, qa_client_id client) {
  identity_entry *entry = identity_get(s, client.slot + 1);
  return entry && qa_same_client(entry->client, client);
}
bool qa_app_sim_quakec_source_client_actor(S *s, qa_client_id client, qa_actor_id *out) {
  qa_owned_actor actor;
  if (!qa_app_sim_quakec_source_has_client(s, client) || !slot_actor(s, client.slot + 1, &actor)) return false;
  *out = actor.id;
  return true;
}

/* TS bindClientInventory: per-actor binding over the program's item bits and ammo fields. */
typedef struct inventory_cell { S *source; qa_owned_actor actor; int64_t slot; qa_inventory_entry entries[16]; } inventory_cell;
static const struct { qa_item_id item; const char *field; double capacity; } AMMO[4] = {
  {"q1:ammo/shells", "ammo_shells", 100}, {"q1:ammo/nails", "ammo_nails", 200},
  {"q1:ammo/rockets", "ammo_rockets", 100}, {"q1:ammo/cells", "ammo_cells", 100}};
static const qa_inventory_entry *inventory_read(void *ctx, size_t *out_count) {
  inventory_cell *c = ctx; S *s = c->source;
  qa_compat_qc_words *words = at(s, c->slot);
  int64_t items = field(s, "items");
  size_t n = 0;
  for (size_t i = 0; i < s->weapon_count; i++) {
    qa_inventory_entry e; memset(&e, 0, sizeof e);
    e.item = s->weapons[i].item; e.capacity = 1;
    e.count = qa_js_bit_and(trunc(qa_compat_qc_words_float(words, items)), s->weapons[i].bit) == 0 ? 0 : 1;
    c->entries[n++] = e;
  }
  for (size_t i = 0; i < 4; i++) {
    qa_inventory_entry e; memset(&e, 0, sizeof e);
    e.item = AMMO[i].item; e.count = fget(s, words, AMMO[i].field); e.capacity = AMMO[i].capacity;
    e.has_count_policy = true; e.count_policy = (qa_inventory_count_policy){"source-counter", "binary32"};
    c->entries[n++] = e;
  }
  *out_count = n;
  return c->entries;
}
static void inventory_write(void *ctx, qa_inventory_entry entry) {
  inventory_cell *c = ctx; S *s = c->source;
  qa_compat_qc_words *words = at(s, c->slot);
  int64_t items = field(s, "items");
  const native_weapon *weapon = weapon_of_item(s, entry.item, NULL);
  if (weapon) {
    double bits = trunc(qa_compat_qc_words_float(words, items));
    qa_compat_qc_words_set_float(words, items, entry.count > 0 ? qa_js_bit_or(bits, weapon->bit) : qa_js_bit_and(bits, qa_js_bit_not(weapon->bit)));
    return;
  }
  for (size_t i = 0; i < 4; i++) if (is(AMMO[i].item, entry.item)) { fset(s, words, AMMO[i].field, entry.count); return; }
  fail("Unsupported QC inventory field %s", entry.item ? entry.item : "(null)");
}
static void bind_client_inventory(S *s, qa_owned_actor actor, int64_t slot) {
  inventory_cell *cell = NULL;
  for (size_t i = 0; i < s->inventory_cell_count && !cell; i++) {
    inventory_cell *c = s->inventory_cells[i];
    qa_owned_actor current;
    if (same_owned(c->actor, actor) || !resolve_owned(s, c->actor.id, &current) || !same_owned(current, c->actor)) cell = c;
  }
  if (!cell) {
    s->inventory_cells = xgrow(s->inventory_cells, &s->inventory_cell_capacity, s->inventory_cell_count + 1, sizeof *s->inventory_cells);
    cell = xalloc(sizeof *cell);
    s->inventory_cells[s->inventory_cell_count++] = cell;
  }
  memset(cell, 0, sizeof *cell);
  cell->source = s; cell->actor = actor; cell->slot = slot;
  qa_inventory_state_binding binding; memset(&binding, 0, sizeof binding);
  binding.ctx = cell; binding.read = inventory_read; binding.write = inventory_write;
  if (s->options.bind_inventory) s->options.bind_inventory(s->options.ctx, actor, binding);
  else qa_shared_inventory_table_bind(s->options.inventory, actor, binding);
}

/* ---------------- host commands ---------------- */
static void give_message(S *s, qa_actor_id actor, const char *text) { print_message(s, actor, text); }
static char *ascii_lower(const char *text) {
  char *out = xstrdup(text);
  for (char *p = out; *p; p++) if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
  return out;
}
typedef struct give_restore { S *s; qa_owned_actor item; } give_restore;
static void give_finally(void *arg) {
  give_restore *r = arg;
  if (live(r->s, r->item.id)) qa_session_actor_registry_release(r->s->options.actors, r->item);
}
static void host_give(S *s, qa_actor_id actor, int64_t slot, const char *const *args, size_t arg_count);
static void host_give_lowered(S *s, qa_actor_id actor, int64_t slot, const char *const *args, size_t arg_count, const char *input) {
  if (strcmp(input, "all") != 0 && s->options.give_inventory && s->options.give_inventory(s->options.ctx, actor, args, arg_count)) return;
  qa_compat_qc_words *words = at(s, slot);
  bool all = strcmp(input, "all") == 0, has_amount = arg_count > 1;
  double amount = has_amount ? (double)qa_native_atoi(args[1]) : 0;
  if (all || is(input, "health") || is(input, "h")) {
    fset(s, words, "health", has_amount ? amount : is(input, "h") ? 0 : 100);
    if (!all) return;
  }
  if (all || is(input, "armor") || is(input, "a")) {
    double points = has_amount ? amount : is(input, "a") ? 0 : 200;
    double armor_bit = points > 150 ? 32768 : points > 100 ? 16384 : points > 0 ? 8192 : 0;
    fset(s, words, "items", qa_js_bit_or(qa_js_bit_and(trunc(fget(s, words, "items")), qa_js_bit_not(8192 | 16384 | 32768)), armor_bit));
    fset(s, words, "armorvalue", points);
    fset(s, words, "armortype", points > 150 ? 0.8 : points > 100 ? 0.6 : points > 0 ? 0.3 : 0);
    if (!all) return;
  }
  if (all || is(input, "weapons")) {
    const char *weapons[1] = {"weapons"};
    if (!(s->options.give_inventory && s->options.give_inventory(s->options.ctx, actor, weapons, 1)))
      for (size_t i = 0; i < s->weapon_count; i++) fset(s, words, "items", qa_js_bit_or(trunc(fget(s, words, "items")), s->weapons[i].bit));
    if (!all) return;
  }
  const char *ammo_args[1] = {"ammo"};
  if ((all || is(input, "ammo")) && !(s->options.give_inventory && s->options.give_inventory(s->options.ctx, actor, ammo_args, 1))) {
    static const struct { const char *field; double value; } base[4] = {{"ammo_shells", 100}, {"ammo_nails", 200}, {"ammo_rockets", 100}, {"ammo_cells", 100}};
    for (size_t i = 0; i < 4; i++) fset(s, words, base[i].field, base[i].value);
    static const char *const extra[7] = {"ammo_shells1", "ammo_nails1", "ammo_rockets1", "ammo_cells1", "ammo_lava_nails", "ammo_multi_rockets", "ammo_plasma"};
    for (size_t i = 0; i < 7; i++) {
      int64_t offset;
      if (has_field(s, extra[i], &offset)) qa_compat_qc_words_set_float(words, offset, strstr(extra[i], "nails") ? 200 : 100);
    }
    if (!all) return;
  }
  if (all || is(input, "keys")) { fset(s, words, "items", qa_js_bit_or(trunc(fget(s, words, "items")), 131072 | 262144)); return; }
  if (is(input, "items")) {
    static const char *const items[4] = {"quad", "pent", "ring", "suit"};
    for (size_t i = 0; i < 4; i++) host_give(s, actor, slot, &items[i], 1);
    return;
  }
  bool hipnotic = weapon_of_item(s, "q1:weapon/hipnotic:laser", NULL) != NULL;
  const char *named = hipnotic && is(input, "6a") ? "q1:weapon/hipnotic:proximity" : hipnotic && is(input, "9") ? "q1:weapon/hipnotic:laser"
      : hipnotic && is(input, "0") ? "q1:weapon/hipnotic:mjolnir"
      : (input[0] >= '2' && input[0] <= '8' && input[1] == '\0') ? ((size_t)(input[0] - '1') < s->weapon_count ? s->weapons[input[0] - '1'].item : NULL) : NULL;
  for (size_t i = 0; i < s->weapon_count; i++) {
    const char *item = s->weapons[i].item, *tail = strrchr(item, '/');
    if (is(item, named) || is(item, input) || is(tail ? tail + 1 : item, input)) {
      fset(s, words, "items", qa_js_bit_or(trunc(fget(s, words, "items")), s->weapons[i].bit));
      return;
    }
  }
  const char *ammo = is(input, "s") || is(input, "shells") ? "ammo_shells" : is(input, "n") || is(input, "nails") ? "ammo_nails"
      : is(input, "r") || is(input, "rockets") ? "ammo_rockets" : is(input, "c") || is(input, "cells") ? "ammo_cells"
      : is(input, "l") ? "ammo_lava_nails" : is(input, "m") ? "ammo_multi_rockets" : is(input, "p") ? "ammo_plasma" : NULL;
  if (ammo) {
    double value = has_amount ? amount : 0;
    char alternate_name[64]; (void)snprintf(alternate_name, sizeof alternate_name, "%s1", ammo);
    int64_t alternate;
    bool has_alternate = has_field(s, alternate_name, &alternate);
    if (has_alternate) qa_compat_qc_words_set_float(words, alternate, value);
    if (!has_alternate || is(ammo, "ammo_shells") || fget(s, words, "weapon") <= 64) fset(s, words, ammo, value);
    const char *native = is(ammo, "ammo_lava_nails") ? "ammo_nails" : is(ammo, "ammo_multi_rockets") ? "ammo_rockets"
        : is(ammo, "ammo_plasma") ? "ammo_cells" : NULL;
    if (native && fget(s, words, "weapon") > 64) fset(s, words, native, value);
    return;
  }
  const char *classname = is(input, "quad") ? "item_artifact_super_damage" : is(input, "pent") ? "item_artifact_invulnerability"
      : is(input, "ring") ? "item_artifact_invisibility" : is(input, "suit") ? "item_artifact_envirosuit" : input;
  if (!starts(classname, "item_") && !starts(classname, "weapon_")) {
    char text[512]; (void)snprintf(text, sizeof text, "Unknown item: %s\n", input);
    give_message(s, actor, text);
    return;
  }
  uint8_t *template_bytes = NULL; size_t template_length = 0;
  for (int64_t candidate = s->reserved_client_slots + 1; candidate < qa_compat_qc_entity_memory_get_count(&s->entities); candidate++) {
    qa_owned_actor owner; qa_compat_qc_words *source = at(s, candidate);
    if (slot_actor(s, candidate, &owner) && live(s, owner.id) && qa_compat_qc_words_int(source, field(s, "touch")) != 0
        && strcmp(qstring(s, qa_compat_qc_words_int(source, field(s, "classname"))), classname) == 0) {
      template_length = source->byte_length;
      template_bytes = xalloc(template_length);
      memcpy(template_bytes, source->bytes, template_length);
      break;
    }
  }
  if (!template_bytes) {
    char text[512]; (void)snprintf(text, sizeof text, "Cannot give %s: this map has no source item template\n", classname);
    give_message(s, actor, text);
    return;
  }
  qa_cleanup bytes_scope; qa_cleanup_push(&bytes_scope, free, template_bytes);
  qa_compat_qc_machine_execute(&s->machine, function_named(s, "spawn"), 0);
  int64_t ref = qa_compat_qc_words_int(&s->machine.globals, 1), item_slot = qa_compat_qc_entity_memory_slot(&s->entities, ref);
  give_restore r = {s, qa_compat_qc_world_host_actor(&s->world_host, item_slot)};
  qa_compat_qc_words *item_words = at(s, item_slot);
  qa_cleanup scope; qa_cleanup_push(&scope, give_finally, &r);
  memcpy(item_words->bytes, template_bytes, template_length);
  vset(s, item_words, "origin", vget(s, words, "origin"));
  fset(s, item_words, "solid", 1); fset(s, item_words, "nextthink", 0);
  static const char *const cleared[3] = {"target", "targetname", "killtarget"};
  for (size_t i = 0; i < 3; i++) {
    int64_t offset;
    if (has_field(s, cleared[i], &offset)) qa_compat_qc_words_set_int(item_words, offset, 0);
  }
  int32_t touch = qa_compat_qc_words_int(item_words, field(s, "touch"));
  invoke(s, touch, item_slot, reference(s, actor), s->current_time);
  qa_cleanup_pop(&scope);
  give_finally(&r);
  qa_cleanup_pop(&bytes_scope);
  free(template_bytes);
}
static void host_give(S *s, qa_actor_id actor, int64_t slot, const char *const *args, size_t arg_count) {
  if (arg_count == 0) { give_message(s, actor, "Usage: give <all|weapons|ammo|health|armor|keys|item> [amount]\n"); return; }
  char *input = ascii_lower(args[0]);
  qa_cleanup scope; qa_cleanup_push(&scope, free, input);
  host_give_lowered(s, actor, slot, args, arg_count, input);
  qa_cleanup_pop(&scope);
  free(input);
}
void qa_app_sim_quakec_source_host_cheat(S *s, qa_actor_id actor, const char *name, const char *const *args, size_t arg_count) {
  int64_t slot;
  if (!source_slot(s, actor, &slot) || !active(s, actor)) fail("QC host command requires an admitted client");
  /* QW's server -cheats authority is represented by the source-owned server cvar. */
  bool denied = s->quakeworld ? qa_cvar_registry_variable_value(s->cvars, "sv_cheats") == 0 : gfloat(s, "deathmatch") != 0;
  if (denied) { print_message(s, actor, "Cheats are disabled on this server.\n"); return; }
  qa_compat_qc_words *words = at(s, slot);
  if (is(name, "give")) {
    host_give(s, actor, slot, args, arg_count);
    const qa_compat_qc_function *refresh = function_or_null(s, "W_SetCurrentAmmo");
    if (refresh && primary_selected(s, actor)) invoke(s, refresh->index, slot, 0, s->current_time);
    return;
  }
  bool enabled;
  if (is(name, "noclip") || is(name, "fly")) {
    double move_type = is(name, "fly") ? 5 : 8;
    enabled = fget(s, words, "movetype") != move_type;
    fset(s, words, "movetype", enabled ? move_type : 3);
  } else {
    double bit = is(name, "god") ? 64 : 128, flags = qa_js_bit_xor(trunc(fget(s, words, "flags")), bit);
    fset(s, words, "flags", flags);
    enabled = qa_js_bit_and(flags, bit) != 0;
  }
  char text[64];
  (void)snprintf(text, sizeof text, "%s %s\n", is(name, "god") ? "godmode" : name, enabled ? "ON" : "OFF");
  print_message(s, actor, text);
}

/* ---------------- admission / disconnect ---------------- */
static double invoke_client_stage(S *s, const qa_q1qc_client_stage_call *call, qa_actor_id actor);
qa_owned_actor qa_app_sim_quakec_source_admit_client(S *s, qa_client_id client) {
  int64_t slot = client.slot + 1;
  if ((double)slot > s->options.max_clients || s->spawning) fail("QC client slot is unavailable");
  qa_owned_actor reserved = qa_app_sim_quakec_source_reserved_client(s, client);
  if (active(s, reserved.id)) fail("QC reserved client is active");
  if (strcmp(qa_app_sim_quakec_source_classname(s, reserved.id), "player") == 0) qa_session_actor_registry_release(s->options.actors, reserved);
  qa_owned_actor actor = qa_compat_qc_world_host_actor(&s->world_host, slot);
  qa_compat_qc_words *words = at(s, slot);
  spawn_entry *parameters = spawn_get(s, slot);
  if (!parameters) fail("Native QuakeC client has no source spawn parameters");
  if (s->quakeworld) {
    /* Spawn preparation is an admission phase, not a caller obligation:
     * network signon prepares explicitly, local admission prepares here. */
    if (!slot_set_has(&s->prepared_clients, slot)) qa_app_sim_quakec_source_prepare_client_spawn(s, client);
  } else {
    memset(words->bytes, 0, words->byte_length);
    fset(s, words, "colormap", (double)slot); fset(s, words, "team", 1);
    const char *name = info_value(info_get(s, slot), "name");
    char fallback[32]; (void)snprintf(fallback, sizeof fallback, "Player %lld", (long long)slot);
    qa_compat_qc_words_set_int(words, field(s, "netname"), (double)qa_compat_qc_strings_allocate(&s->machine.strings, name ? name : fallback));
  }
  bool is_spectator = slot_set_has(&s->spectator_slots, slot);
  const qa_compat_qc_function *spectator_connect = function_or_null(s, "SpectatorConnect");
  if (!is_spectator || (spectator_connect && spectator_connect->index != 0)) {
    double values[16]; memcpy(values, spawn_get(s, slot)->values, sizeof values);
    write_spawn_parameters(s, values);
  }
  bool replay_local = s->local_messages_started;
  actor_set_add(&s->active_clients, actor.id);
  if (!s->quakeworld && (!s->netquake_wire_attached || s->local_messages_started)) {
    if (!s->local_messages_started) {
      s->local_messages_started = true;
      qa_app_sim_quakec_local_messages_admit(s->local_messages, actor.id);
      receive_local_messages(s, s->nq_signon, s->nq_signon_count, (qa_compat_qc_message_destination){QA_QC_DEST_SIGNON, false, no_actor(), {0, 0, 0}, NULL},
                             s->nq_signon_views, s->nq_signon_view_count, true);
      size_t count = 0;
      nq_routed *live_records = qa_event_ring_snapshot(&s->nq_routed, &count);
      qa_cleanup scope; qa_cleanup_push(&scope, free, live_records);
      for (size_t i = 0; i < count; i++)
        receive_local_messages(s, live_records[i].messages, live_records[i].count, live_records[i].destination,
                               live_records[i].views, live_records[i].view_count, true);
      qa_cleanup_pop(&scope);
      free(live_records);
    }
    qa_app_sim_quakec_local_messages_admit(s->local_messages, actor.id);
  }
  bind_client_inventory(s, actor, slot);
  if (is_spectator) {
    vset(s, words, "origin", (qa_vec3){0, 0, 0});
    vset(s, words, "view_ofs", (qa_vec3){0, 0, 22});
    for (int64_t candidate = s->reserved_client_slots - 1; candidate < qa_compat_qc_entity_memory_get_count(&s->entities); candidate++) {
      qa_compat_qc_words *entity = at(s, candidate);
      if (strcmp(qstring(s, qa_compat_qc_words_int(entity, field(s, "classname"))), "info_player_start") != 0) continue;
      vset(s, words, "origin", vget(s, entity, "origin"));
      break;
    }
    spectator_callback(s, "SpectatorConnect", slot);
  } else {
    invoke(s, function_named(s, "ClientConnect"), slot, 0, s->current_time);
    if (!s->weapon_stage || !s->weapon_stage->has_client) invoke(s, function_named(s, "PutClientInServer"), slot, 0, s->current_time);
    else (void)invoke_client_stage(s, &s->weapon_stage->client.spawn, actor.id);
  }
  if (replay_local) {
    size_t count = 0;
    const qa_netquake_message *presentation = qa_app_sim_quakec_local_messages_presentation(s->local_messages, actor.id, &count);
    qa_netquake_message *copy = xalloc((count ? count : 1) * sizeof *copy);
    for (size_t i = 0; i < count; i++) copy[i] = nq_copy(&presentation[i]);
    for (size_t i = 0; i < count; i++)
      receive_local_messages(s, &copy[i], 1, (qa_compat_qc_message_destination){QA_QC_DEST_CLIENT, true, actor.id, {0, 0, 0}, NULL}, NULL, 0, false);
    nq_messages_free(copy, count);
  }
  return actor;
}
bool qa_app_sim_quakec_source_client_kill(S *s, qa_actor_id actor) {
  int64_t slot;
  if (!source_slot(s, actor, &slot) || !active(s, actor) || spectator(s, actor) || fget(s, at(s, slot), "health") <= 0) return false;
  invoke(s, function_named(s, "ClientKill"), slot, 0, s->current_time);
  return true;
}
void qa_app_sim_quakec_source_disconnect_client(S *s, qa_owned_actor actor) {
  int64_t slot;
  if (!source_slot(s, actor.id, &slot) || !qa_app_sim_quakec_source_is_reserved_client(s, actor.id)) fail("QC disconnect requires a reserved client");
  if (active(s, actor.id)) {
    if (spectator(s, actor.id)) spectator_callback(s, "SpectatorDisconnect", slot);
    else invoke(s, function_named(s, "ClientDisconnect"), slot, 0, s->current_time);
  }
  slot_set_delete(&s->spectator_slots, slot);
  actor_set_delete(&s->active_clients, actor.id);
  qa_app_sim_quakec_local_messages_retire(s->local_messages, actor.id);
  pending_delete(s, actor.id);
  slot_set_delete(&s->prepared_clients, slot); spawn_delete(s, slot); info_delete(s, slot); identity_delete(s, slot);
}

/* ---------------- client equipment and state ---------------- */
static int64_t require_active(S *s, qa_actor_id actor, const char *message) {
  int64_t slot;
  if (!source_slot(s, actor, &slot) || !active(s, actor)) fail("%s", message);
  return slot;
}
bool qa_app_sim_quakec_source_client_weapon_settled(S *s, qa_actor_id actor) {
  int64_t slot = require_active(s, actor, "Missing QC weapon client");
  if (!s->has_weapon_binding) fail("QC artifact has no qualified weapon stage");
  return qa_q1qc_weapon_stage_settled(&s->weapon_binding, qa_compat_qc_entity_memory_reference(&s->entities, slot));
}
qa_app_sim_quakec_equipment qa_app_sim_quakec_source_client_equipment(S *s, qa_actor_id actor) {
  int64_t slot = require_active(s, actor, "Missing QC equipment client");
  qa_compat_qc_words *words = at(s, slot);
  return (qa_app_sim_quakec_equipment){fget(s, words, "max_health"), fget(s, words, "super_damage_finished")};
}
double qa_app_sim_quakec_source_damage_amount(S *s, const qa_actor_id *actor, double amount) {
  qa_actor_id owner = actor ? *actor : qa_app_sim_quakec_source_world_actor(s).id;
  int64_t ref = reference(s, owner);
  double out;
  if (qa_q1_id1_damage_amount_of(s->damage, owner, ref, s->current_time, amount, &out)) return out;
  return fround(fround(amount) * fround(qa_q1_id1_damage_multiplier(qmachine(s), ref, ref)));
}
double qa_app_sim_quakec_source_client_powerup_expires(S *s, qa_actor_id actor, const char *powerup) {
  int64_t slot = require_active(s, actor, "Missing QC powerup client");
  const char *name = is(powerup, "quad") ? "super_damage_finished" : is(powerup, "invulnerability") ? "invincible_finished"
      : is(powerup, "invisibility") ? "invisible_finished" : "radsuit_finished";
  return fget(s, at(s, slot), name);
}
bool qa_app_sim_quakec_source_weapon_target(S *s, qa_actor_id actor, qa_app_sim_quakec_weapon_target *out) {
  int64_t slot;
  if (!source_slot(s, actor, &slot) || !live(s, actor)) return false;
  qa_compat_qc_words *words = at(s, slot);
  out->monster = qa_js_bit_and(trunc(fget(s, words, "flags")), 32) != 0;
  out->aimed_damage = fget(s, words, "takedamage") == 2;
  return true;
}
void qa_app_sim_quakec_source_set_client_max_health(S *s, qa_actor_id actor, double value) {
  if (!isfinite(fround(value))) qa_raise("RangeError", 0, 0, "QC max health exceeds binary32 range");
  int64_t slot = require_active(s, actor, "Missing QC equipment client");
  fset(s, at(s, slot), "max_health", value);
}
static int64_t stage_reference(void *ctx, const qa_actor_id *actor) {
  S *s = ctx;
  return actor == NULL ? qa_compat_qc_entity_memory_reference(&s->entities, 0) : reference(s, *actor);
}
static double invoke_client_stage(S *s, const qa_q1qc_client_stage_call *call, qa_actor_id actor) {
  qa_owned_actor owner, after;
  int64_t slot;
  if (!resolve_owned(s, actor, &owner) || !source_slot(s, actor, &slot) || !active(s, actor)) fail("Missing live QC client stage actor");
  double result = qa_q1qc_invoke_client_stage(qmachine(s), call, actor, s->current_time, stage_reference, s);
  if (!resolve_owned(s, actor, &after) || !same_owned(after, owner) || !active(s, actor)) fail("Original QC client stage retired its client");
  return result;
}
void qa_app_sim_quakec_source_drop_client_objectives(S *s, qa_actor_id actor) {
  int64_t slot;
  if (!active(s, actor) || !source_slot(s, actor, &slot)) fail("Missing QC equipment client");
  if (!s->weapon_stage || !s->weapon_stage->has_client) fail("QC artifact has no qualified objective contract");
  if (is(s->weapon_stage->client.objectives_kind, "call")) (void)invoke_client_stage(s, &s->weapon_stage->client.objectives_call, actor);
}
qa_app_sim_quakec_spawn_point qa_app_sim_quakec_source_client_spawn_point(S *s, qa_actor_id actor) {
  (void)require_active(s, actor, "Missing QC equipment client");
  if (!s->weapon_stage || !s->weapon_stage->has_client) fail("QC artifact has no qualified spawn selection");
  int64_t ref = (int64_t)invoke_client_stage(s, &s->weapon_stage->client.select_spawn, actor);
  qa_owned_actor spawn;
  bool has = slot_actor(s, qa_compat_qc_entity_memory_slot(&s->entities, ref), &spawn);
  if (!has || !live(s, spawn.id) || same_actor(spawn.id, qa_app_sim_quakec_source_world_actor(s).id)) fail("Original QC source selected no spawn point");
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_from_reference(&s->entities, ref);
  return (qa_app_sim_quakec_spawn_point){vget(s, words, "origin"), vget(s, words, "angles")};
}
bool qa_app_sim_quakec_source_pickup_selection_deferred(S *s, qa_original_pickup_offer offer) {
  return qa_q1_id1_pickup_binding_selection_deferred(s->pickups, offer);
}
qa_q1_id1_pickup_supply qa_app_sim_quakec_source_pickup_supply(S *s, qa_original_pickup_offer offer) {
  return qa_q1_id1_pickup_binding_supply(s->pickups, offer);
}

/* ---------------- movement ---------------- */
static qa_compat_qc_words *words_of(S *s, qa_actor_id actor) { return qa_compat_qc_entity_memory_from_reference(&s->entities, reference(s, actor)); }
static void clear_ground(S *s, qa_owned_actor actor) {
  qa_shared_body_table *bodies = qa_app_sim_shared_physics_bodies(s->options.physics);
  qa_body_state_result current = qa_shared_body_table_read(bodies, actor.id);
  if (!current.has_value) return;
  qa_body_state next = current.value; next.ground = NULL;
  qa_shared_body_table_write(bodies, actor, next);
}
static qa_frame_context source_frame(S *s, qa_frame_context frame, double elapsed) {
  qa_frame_context f = frame;
  f.time = (qa_source_time){QA_SOURCE_TIME_SECONDS, s->current_time};
  f.elapsed = (qa_source_time){QA_SOURCE_TIME_SECONDS, elapsed};
  return f;
}
qa_app_sim_quakec_client_movement qa_app_sim_quakec_source_mixed_client_pre_think(S *s, qa_owned_actor actor, qa_user_command command, qa_frame_context frame) {
  qa_q1_user_command input = qa_app_sim_quakec_client_command(command);
  qa_app_sim_quakec_source_client_input(s, actor.id, input);
  qa_compat_qc_words *words = words_of(s, actor.id);
  qa_app_sim_quakec_client_transition_state before = {trunc(fget(s, words, "flags")), vget(s, words, "velocity")};
  double elapsed = seconds_of(frame.elapsed);
  gset(s, "frametime", elapsed);
  qa_app_sim_quakec_source_client_pre_think(s, actor);
  qa_app_sim_quakec_client_transition_state after = {trunc(fget(s, words, "flags")), vget(s, words, "velocity")};
  bool accepted = qa_app_sim_quakec_source_jump(command, before, after);
  bool source_jump = accepted || ((qa_js_to_int32(input.buttons) & 2) != 0 && (qa_js_to_int32(before.flags) & 4096) == 0);
  if (accepted) clear_ground(s, actor);
  qa_app_sim_quakec_source_run_think(s, actor, source_frame(s, frame, elapsed));
  return (qa_app_sim_quakec_client_movement){source_jump ? qa_app_sim_consume_quakec_jump(command) : command, source_jump};
}
qa_app_sim_quakec_client_movement qa_app_sim_quakec_source_mixed_quakeworld_pre_think(S *s, qa_owned_actor actor,
    qa_qw_user_command source_command, qa_user_command command, qa_frame_context frame) {
  qa_compat_qc_words *words = words_of(s, actor.id);
  int32_t before_flags = ibits(trunc(fget(s, words, "flags"))); qa_vec3 before_velocity = vget(s, words, "velocity");
  qa_app_sim_quakec_source_quakeworld_pre_think(s, actor, source_command, frame);
  int32_t after_flags = ibits(trunc(fget(s, words, "flags"))); qa_vec3 after_velocity = vget(s, words, "velocity");
  bool accepted = (qa_js_to_int32(source_command.buttons) & 2) != 0 && (before_flags & (512 | 4096)) == (512 | 4096) && (after_flags & 4096) == 0;
  bool impulse = accepted && after_velocity.z > before_velocity.z;
  if (impulse) clear_ground(s, actor);
  return (qa_app_sim_quakec_client_movement){impulse ? qa_app_sim_consume_quakec_jump(command) : command, accepted};
}
void qa_app_sim_quakec_source_mixed_client_post_think(S *s, qa_owned_actor actor, qa_trace_hit ground, double water_level,
    double water_type, const char *movement, const char *post_think) {
  qa_compat_qc_words *words = words_of(s, actor.id);
  fset(s, words, "flags", (double)((ibits(trunc(fget(s, words, "flags"))) & ~512) | (ground.kind != QA_TRACE_HIT_NONE ? 512 : 0)));
  qa_compat_qc_words_set_int(words, field(s, "groundentity"), ground.kind == QA_TRACE_HIT_ACTOR ? (double)reference(s, ground.actor) : 0);
  fset(s, words, "waterlevel", water_level);
  int32_t type = qa_js_to_int32(water_type);
  double source_water = water_level == 0 ? -1 : (is(movement, "q1-netquake") || is(movement, "q1-quakeworld")) ? water_type
      : (type & 16) != 0 ? -4 : (type & 8) != 0 ? -5 : -3;
  fset(s, words, "watertype", source_water);
  if (post_think == NULL || is(post_think, "immediate")) qa_app_sim_quakec_source_client_post_think(s, actor);
}
void qa_app_sim_quakec_source_client_input(S *s, qa_actor_id actor, qa_q1_user_command command) {
  int64_t slot = require_active(s, actor, "QC input requires an admitted client");
  qa_compat_qc_words *words = at(s, slot);
  vset(s, words, "v_angle", command.view_angles);
  int32_t buttons = qa_js_to_int32(command.buttons);
  fset(s, words, "button0", buttons & 1); fset(s, words, "button2", (buttons >> 1) & 1);
  if (command.impulse != 0) {
    if (qa_app_sim_quakec_local_messages_answer_prompt(s->local_messages, actor, command.impulse)) {
      qa_comp_q1_event composition; memset(&composition, 0, sizeof composition);
      composition.kind = QA_COMP_Q1_EVENT_CLEAR_PROMPT; composition.actor = actor;
      emit_composition(s, &composition, &actor);
    }
    pending_delete(s, actor);
    fset(s, words, "impulse", command.impulse);
  }
}
qa_qw_movement_state qa_app_sim_quakec_source_read_quakeworld_state(S *s, qa_actor_id actor, qa_qw_movement_state state) {
  qa_compat_qc_words *words = words_of(s, actor);
  qa_vec3 origin = vget(s, words, "origin"), mins = vget(s, words, "mins");
  state.origin = (qa_vec3){origin.x + mins.x + 16, origin.y + mins.y + 16, origin.z + mins.z + 24};
  state.velocity = vget(s, words, "velocity"); state.angles = vget(s, words, "v_angle");
  state.water_jump_time_seconds = fget(s, words, "teleport_time"); state.dead = fget(s, words, "health") <= 0;
  state.spectator = spectator(s, actor) ? 1 : 0;
  return state;
}
void qa_app_sim_quakec_source_write_quakeworld_state(S *s, qa_actor_id actor, qa_qw_movement_state state) {
  qa_compat_qc_words *words = words_of(s, actor);
  qa_vec3 mins = vget(s, words, "mins");
  vset(s, words, "origin", (qa_vec3){state.origin.x - mins.x - 16, state.origin.y - mins.y - 16, state.origin.z - mins.z - 24});
  vset(s, words, "velocity", state.velocity); vset(s, words, "v_angle", state.angles);
  fset(s, words, "teleport_time", state.water_jump_time_seconds);
  bool grounded = state.ground.kind != QA_TRACE_HIT_NONE;
  fset(s, words, "flags", (double)((ibits(trunc(fget(s, words, "flags"))) & ~512) | (grounded ? 512 : 0)));
  if (grounded) qa_compat_qc_words_set_int(words, field(s, "groundentity"), state.ground.kind == QA_TRACE_HIT_ACTOR ? (double)reference(s, state.ground.actor) : 0);
}
void qa_app_sim_quakec_source_quakeworld_water(S *s, qa_actor_id actor, double level, double type) {
  qa_compat_qc_words *words = words_of(s, actor);
  fset(s, words, "waterlevel", level); fset(s, words, "watertype", type);
}
qa_qw_movement_profile qa_app_sim_quakec_source_quakeworld_profile(S *s, qa_actor_id actor, qa_qw_movement_profile profile) {
  qa_compat_qc_words *words = words_of(s, actor);
  int64_t offset;
  qa_q1_movement_parameters *p = &profile.parameters;
  p->gravity = qa_cvar_registry_variable_value(s->cvars, "sv_gravity");
  p->stop_speed = qa_cvar_registry_variable_value(s->cvars, "sv_stopspeed");
  p->spectator_max_speed = qa_cvar_registry_variable_value(s->cvars, "sv_spectatormaxspeed");
  p->accelerate = qa_cvar_registry_variable_value(s->cvars, "sv_accelerate");
  p->air_accelerate = qa_cvar_registry_variable_value(s->cvars, "sv_airaccelerate");
  p->water_accelerate = qa_cvar_registry_variable_value(s->cvars, "sv_wateraccelerate");
  p->friction = qa_cvar_registry_variable_value(s->cvars, "sv_friction");
  p->water_friction = qa_cvar_registry_variable_value(s->cvars, "sv_waterfriction");
  p->max_speed = has_field(s, "maxspeed", &offset) ? qa_compat_qc_words_float(words, offset) : qa_cvar_registry_variable_value(s->cvars, "sv_maxspeed");
  p->entity_gravity = has_field(s, "gravity", &offset) ? qa_compat_qc_words_float(words, offset) : 1;
  return profile;
}
void qa_app_sim_quakec_source_quakeworld_pre_think(S *s, qa_owned_actor actor, qa_qw_user_command command, qa_frame_context frame) {
  if (!s->quakeworld || !active(s, actor.id)) fail("QW movement requires a begun native client");
  qa_compat_qc_words *words = words_of(s, actor.id);
  if (fget(s, words, "fixangle") == 0) vset(s, words, "v_angle", command.angles);
  int32_t buttons = qa_js_to_int32(command.buttons);
  fset(s, words, "button0", buttons & 1); fset(s, words, "button2", (buttons >> 1) & 1);
  if (command.impulse != 0) fset(s, words, "impulse", command.impulse);
  if (fget(s, words, "health") > 0) {
    qa_vec3 angles = vget(s, words, "angles");
    if (fget(s, words, "fixangle") == 0) { angles.x = -command.angles.x / 3; angles.y = command.angles.y; }
    qa_vec3 right = {0, 0, 0}, velocity = vget(s, words, "velocity");
    qa_donor_angle_vectors(angles, NULL, &right, NULL);
    double side = velocity.x * right.x + velocity.y * right.y + velocity.z * right.z;
    angles.z = (fabs(side) < 200 ? fabs(side) * 2 / 200 : 2) * (side < 0 ? -1 : 1) * 4;
    vset(s, words, "angles", angles);
  }
  if (spectator(s, actor.id)) return;
  gset(s, "frametime", command.milliseconds * 0.001);
  qa_app_sim_quakec_source_client_pre_think(s, actor);
  qa_app_sim_quakec_source_run_think(s, actor, source_frame(s, frame, command.milliseconds * 0.001));
}
bool qa_app_sim_quakec_source_take_new_missile(S *s, qa_owned_actor *out) {
  if (!s->quakeworld) return false;
  int64_t offset = qa_compat_qc_machine_global_offset(&s->machine, "newmis");
  int32_t ref = qa_compat_qc_words_int(&s->machine.globals, offset);
  if (ref == 0) return false;
  qa_compat_qc_words_set_int(&s->machine.globals, offset, 0);
  return slot_actor(s, qa_compat_qc_entity_memory_slot(&s->entities, ref), out);
}
bool qa_app_sim_quakec_source_run_actor_once(S *s, qa_actor_id actor, qa_frame_context frame) {
  if (!s->quakeworld) return true;
  double time = fround(seconds_of(frame.time));
  qa_compat_qc_words *words = words_of(s, actor);
  int64_t f = field(s, "lastruntime");
  if (qa_compat_qc_words_float(words, f) == time) return false;
  qa_compat_qc_words_set_float(words, f, time);
  return true;
}
qa_q1_movement_state qa_app_sim_quakec_source_read_client_state(S *s, qa_actor_id actor, qa_q1_movement_state state) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) fail("QC player has no source slot");
  qa_compat_qc_words *w = at(s, slot);
  state.origin = vget(s, w, "origin"); state.velocity = vget(s, w, "velocity"); state.angles = vget(s, w, "angles");
  state.old_origin = vget(s, w, "oldorigin"); state.angular_velocity = vget(s, w, "avelocity");
  state.view_angles = fget(s, w, "fixangle") != 0 ? vget(s, w, "angles") : vget(s, w, "v_angle");
  state.punch_angles = vget(s, w, "punchangle"); state.move_type = fget(s, w, "movetype");
  state.flags = trunc(fget(s, w, "flags")); state.water_level = fget(s, w, "waterlevel"); state.water_type = fget(s, w, "watertype");
  state.teleport_time_seconds = fget(s, w, "teleport_time"); state.water_jump_direction = vget(s, w, "movedir");
  state.ideal_pitch = fget(s, w, "idealpitch"); state.fix_angle = fget(s, w, "fixangle") != 0; state.health = fget(s, w, "health");
  return state;
}
qa_vec3 qa_app_sim_quakec_source_client_punch_angles(S *s, qa_actor_id actor) {
  if (s->quakeworld) fail("QuakeWorld does not own a NetQuake punch vector");
  return vget(s, words_of(s, actor), "punchangle");
}
void qa_app_sim_quakec_source_set_client_view_roll(S *s, qa_actor_id actor, double roll) {
  if (!active(s, actor)) fail("QC source view requires an admitted client");
  qa_compat_qc_words *words = words_of(s, actor);
  qa_vec3 angle = vget(s, words, "v_angle"); angle.z = roll;
  vset(s, words, "v_angle", angle);
}
bool qa_app_sim_quakec_source_client_punch_advances(S *s, qa_actor_id actor) { return fget(s, words_of(s, actor), "movetype") != 0; }
void qa_app_sim_quakec_source_set_client_punch_angles(S *s, qa_actor_id actor, qa_vec3 angles) {
  if (s->quakeworld) fail("QuakeWorld does not own a NetQuake punch vector");
  vset(s, words_of(s, actor), "punchangle", angles);
}
void qa_app_sim_quakec_source_write_client_state(S *s, qa_actor_id actor, qa_q1_movement_state state) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) fail("QC player has no source slot");
  qa_compat_qc_words *w = at(s, slot);
  vset(s, w, "oldorigin", state.old_origin); vset(s, w, "avelocity", state.angular_velocity); vset(s, w, "v_angle", state.view_angles);
  vset(s, w, "punchangle", state.punch_angles); vset(s, w, "movedir", state.water_jump_direction);
  fset(s, w, "movetype", state.move_type); fset(s, w, "flags", state.flags); fset(s, w, "waterlevel", state.water_level);
  fset(s, w, "watertype", state.water_type); fset(s, w, "teleport_time", state.teleport_time_seconds);
  fset(s, w, "idealpitch", state.ideal_pitch); fset(s, w, "fixangle", state.fix_angle ? 1 : 0);
}
bool qa_app_sim_quakec_source_consume_netquake_damage(S *s, qa_actor_id actor, qa_app_sim_quakec_netquake_damage *out) {
  int64_t slot;
  if (s->quakeworld || !source_slot(s, actor, &slot) || !active(s, actor)) fail("NetQuake damage requires an active source client");
  qa_compat_qc_words *words = at(s, slot);
  double armor = fget(s, words, "dmg_save"), blood = fget(s, words, "dmg_take");
  if (armor == 0 && blood == 0) return false;
  qa_compat_qc_words *inflictor = qa_compat_qc_entity_memory_from_reference(&s->entities, qa_compat_qc_words_int(words, field(s, "dmg_inflictor")));
  qa_vec3 origin = vget(s, inflictor, "origin"), min = vget(s, inflictor, "mins"), max = vget(s, inflictor, "maxs");
  fset(s, words, "dmg_save", 0); fset(s, words, "dmg_take", 0);
  out->armor = armor; out->blood = blood;
  out->source = (qa_vec3){origin.x + (min.x + max.x) * 0.5, origin.y + (min.y + max.y) * 0.5, origin.z + (min.z + max.z) * 0.5};
  return true;
}
bool qa_app_sim_quakec_source_consume_client_view_reset(S *s, qa_actor_id actor, qa_vec3 *out) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) return false;
  qa_compat_qc_words *words = at(s, slot);
  if (fget(s, words, "fixangle") == 0) return false;
  fset(s, words, "fixangle", 0);
  *out = vget(s, words, "angles");
  return true;
}
bool qa_app_sim_quakec_source_request_client_weapon(S *s, qa_actor_id actor, qa_item_id item) {
  int64_t slot; size_t index = 0;
  const native_weapon *weapon = weapon_of_item(s, item, &index);
  if (!source_slot(s, actor, &slot) || !weapon || qa_shared_inventory_table_count(s->options.inventory, actor, item) <= 0) return false;
  qa_compat_qc_words *words = at(s, slot);
  double current = fget(s, words, "weapon");
  pending_delete(s, actor);
  if (current == weapon->bit) { fset(s, words, "impulse", 0); return true; }
  if (weapon->has_via && current != weapon->via && qa_js_bit_and(trunc(fget(s, words, "items")), weapon->via) != 0)
    pending_set(s, actor, index, false);
  fset(s, words, "impulse", weapon->impulse);
  return true;
}
void qa_app_sim_quakec_source_client_pre_think(S *s, qa_owned_actor actor) {
  if (spectator(s, actor.id)) return;
  int64_t slot;
  if (!source_slot(s, actor.id, &slot)) fail("Missing QC client");
  invoke(s, function_named(s, "PlayerPreThink"), slot, 0, s->current_time);
}
void qa_app_sim_quakec_source_client_post_think(S *s, qa_owned_actor actor) {
  int64_t slot;
  if (!source_slot(s, actor.id, &slot)) return;
  if (spectator(s, actor.id)) { spectator_callback(s, "SpectatorThink", slot); return; }
  qa_compat_qc_words *words = at(s, slot);
  pending_entry *pending = pending_get(s, actor.id);
  double impulse = fget(s, words, "impulse");
  if (pending && (qa_shared_inventory_table_count(s->options.inventory, actor.id, s->weapons[pending->weapon].item) <= 0 || fget(s, words, "health") <= 0)) {
    double pending_impulse = s->weapons[pending->weapon].impulse;
    pending_delete(s, actor.id);
    if (impulse == pending_impulse) fset(s, words, "impulse", 0);
  } else if (pending && impulse != 0 && impulse != s->weapons[pending->weapon].impulse) pending_delete(s, actor.id);
  invoke(s, function_named(s, "PlayerPostThink"), slot, 0, s->current_time);
  pending_entry *selection = pending_get(s, actor.id);
  if (selection && fget(s, words, "impulse") == 0) {
    pending_entry chosen = *selection;
    pending_delete(s, actor.id);
    const native_weapon *weapon = &s->weapons[chosen.weapon];
    if (!chosen.following && weapon->has_via && fget(s, words, "weapon") == weapon->via
        && qa_shared_inventory_table_count(s->options.inventory, actor.id, weapon->item) > 0) {
      pending_set(s, actor.id, chosen.weapon, true);
      fset(s, words, "impulse", weapon->impulse);
    }
  }
}
qa_app_sim_quakec_client_ui qa_app_sim_quakec_source_client_ui(S *s, qa_actor_id actor) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) fail("Missing QC UI actor");
  qa_compat_qc_words *words = at(s, slot);
  static const struct { const char *kind, *field; } timers[4] = {
    {"quad", "super_damage_finished"}, {"invulnerability", "invincible_finished"}, {"invisibility", "invisible_finished"}, {"suit", "radsuit_finished"}};
  qa_app_sim_q1_powerup_entry powerups[4]; size_t powerup_count = 0;
  for (size_t i = 0; i < 4; i++) {
    const qa_compat_qc_definition *f = qa_compat_qc_program_field_named(s->program, timers[i].field);
    if (f && is(f->type, "float")) powerups[powerup_count++] = (qa_app_sim_q1_powerup_entry){timers[i].kind, qa_compat_qc_words_float(words, f->offset)};
  }
  qa_app_sim_quakec_weapon_ui_binding bindings[11];
  for (size_t i = 0; i < s->weapon_count; i++)
    bindings[i] = (qa_app_sim_quakec_weapon_ui_binding){s->weapons[i].item, s->weapons[i].label, s->weapons[i].bit, s->weapons[i].impulse};
  double stat;
  double current_ammo = qa_app_sim_quakec_local_messages_stat(s->local_messages, actor, 3, &stat) ? stat : fget(s, words, "currentammo");
  qa_app_sim_quakec_client_ui ui; memset(&ui, 0, sizeof ui);
  ui.weapon = qa_app_sim_quakec_weapon_ui_of(fget(s, words, "items"), fget(s, words, "weapon"), current_ammo, bindings, s->weapon_count);
  ui.powerup_count = qa_app_sim_q1_powerup_timers(powerups, powerup_count, s->current_time, ui.powerups, 4);
  if (ui.powerup_count > 4) ui.powerup_count = 4;
  return ui;
}
qa_arsenal_state qa_app_sim_quakec_source_client_arsenal(S *s, qa_actor_id actor) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) fail("Missing QC arsenal actor");
  qa_compat_qc_words *words = at(s, slot);
  double value = fget(s, words, "weapon");
  const native_weapon *weapon = NULL;
  for (size_t i = 0; i < s->weapon_count && !weapon; i++) if (s->weapons[i].bit == value) weapon = &s->weapons[i];
  if (!weapon && !(value == 0 && spectator(s, actor))) fail("Unsupported actual QC weapon %s", QA_JS_NUMBER(value));
  qa_arsenal_state state; memset(&state, 0, sizeof state);
  state.provider = provider_of(s); state.active_weapon = weapon ? weapon->item : NULL;
  state.ammo = qa_shared_inventory_table_entries(s->options.inventory, actor, &state.ammo_count);
  state.state.kind = "q1"; state.state.source_weapon_first = true; state.state.source_weapon = value; state.state.frame = fget(s, words, "weaponframe");
  state.state.attack_finished_seconds = fget(s, words, "attack_finished");
  return state;
}
qa_actor_animation_state qa_app_sim_quakec_source_client_animation(S *s, qa_actor_id actor) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) fail("Missing QC animation actor");
  qa_compat_qc_words *words = at(s, slot);
  qa_actor_animation_state state; memset(&state, 0, sizeof state);
  state.provider = s->options.recipe->character.definition.provider;
  state.state.kind = "q1"; state.state.frame = fget(s, words, "frame"); state.state.next_frame_seconds = fget(s, words, "nextthink");
  return state;
}
qa_app_sim_quakec_session_state qa_app_sim_quakec_source_local_client_session(S *s, qa_actor_id actor) {
  return qa_app_sim_quakec_local_messages_session_state(s->local_messages, actor);
}
bool qa_app_sim_quakec_source_local_client_intermission(S *s, qa_actor_id actor, bool *out) {
  if (!qa_app_sim_quakec_local_messages_has_client(s->local_messages, actor)) return false;
  *out = qa_app_sim_quakec_local_messages_intermission(s->local_messages, actor);
  return true;
}
static bool local_view_read(void *ctx, qa_actor_id target, qa_vec3 *origin, qa_vec3 *angles) {
  S *s = ctx;
  if (!live(s, target)) return false;
  int64_t slot;
  if (!source_slot(s, target, &slot)) {
    qa_body_state_result body = qa_shared_body_table_read(qa_app_sim_shared_physics_bodies(s->options.physics), target);
    if (!body.has_value) return false;
    *origin = body.value.origin; *angles = body.value.angles;
    return true;
  }
  *origin = vget(s, at(s, slot), "origin"); *angles = vget(s, at(s, slot), "angles");
  return true;
}
static qa_vec3 local_view_offset(void *ctx, qa_actor_id actor) { return qa_app_sim_quakec_source_client_view_offset(ctx, actor); }
bool qa_app_sim_quakec_source_local_client_view(S *s, qa_actor_id actor, qa_app_sim_player_view *out) {
  return qa_app_sim_quakec_local_view(actor, s->local_messages, (qa_app_sim_quakec_local_view_source){s, local_view_read, local_view_offset}, out);
}
qa_vec3 qa_app_sim_quakec_source_client_view_offset(S *s, qa_actor_id actor) {
  qa_compat_qc_words *words = words_of(s, actor);
  if (s->quakeworld) return (qa_vec3){0, 0, vget(s, words, "mins").z != -24 ? 8 : fget(s, words, "health") <= 0 ? -16 : 22};
  return vget(s, words, "view_ofs");
}

/* ---------------- frame lifecycle ---------------- */
static void entities_cleanup(void *arg) { qa_entities_free(arg); }
void qa_app_sim_quakec_source_spawn_map(S *s) {
  if (!s->spawning) fail("QC map was already spawned");
  qa_compat_qc_words *world = at(s, 0);
  const char *map = map_path(s);
  qa_compat_qc_words_set_int(world, field(s, "model"), (double)qa_compat_qc_strings_allocate(&s->machine.strings, map));
  fset(s, world, "modelindex", 1); fset(s, world, "solid", 4); fset(s, world, "movetype", 7);
  gset(s, "time", s->current_time);
  static const char *const names[4] = {"skill", "deathmatch", "coop", "teamplay"};
  for (size_t i = 0; i < 4; i++) {
    const qa_compat_qc_definition *g = qa_compat_qc_program_global_named(s->program, names[i]);
    if (g) qa_compat_qc_words_set_float(&s->machine.globals, g->offset, qa_cvar_registry_variable_value(s->cvars, names[i]));
  }
  /* map.replace(/^maps\//, "").replace(/\.bsp$/, "") */
  const char *base = starts(map, "maps/") ? map + 5 : map;
  char *mapname = xstrdup(base);
  if (ends(mapname, ".bsp")) mapname[strlen(mapname) - 4] = '\0';
  qa_compat_qc_words_set_int(&s->machine.globals, qa_compat_qc_machine_global_offset(&s->machine, "mapname"),
                             (double)qa_compat_qc_strings_allocate(&s->machine.strings, mapname));
  free(mapname);
  char *generated = s->options.entity_text ? NULL : qa_app_sim_quakec_map_entities(s->options.world, s->options.mode);
  const char *text = s->options.entity_text ? s->options.entity_text : generated;
  qa_entities entities; memset(&entities, 0, sizeof entities);
  bool parsed = qa_parse_entities(text, strlen(text), NULL, &entities);
  free(generated);
  if (!parsed) qa_range_error("Out of memory");
  qa_cleanup scope; qa_cleanup_push(&scope, entities_cleanup, &entities);
  int32_t excluded = is(s->options.mode, "deathmatch") ? 2048 : s->options.skill == 0 ? 256 : s->options.skill == 1 ? 512 : 1024;
  for (size_t ordinal = 0; ordinal < entities.record_count; ordinal++) {
    qa_owned_actor actor = ordinal == 0 ? qa_app_sim_quakec_source_world_actor(s) : qa_source_actor_slots_allocate(s->slots, "quakec:authored");
    int64_t slot;
    if (!source_slot(s, actor.id, &slot)) fail("Missing authored QC source slot");
    (void)qa_compat_qc_world_host_actor(&s->world_host, slot);
    const qa_entity_record *record = &entities.records[ordinal];
    qa_compat_qc_text_pair *pairs = xalloc((record->pair_count ? record->pair_count : 1) * sizeof *pairs);
    for (size_t i = 0; i < record->pair_count; i++) pairs[i] = (qa_compat_qc_text_pair){record->pairs[i].key, record->pairs[i].value};
    qa_compat_qc_entity_parse_result_compat applied; memset(&applied, 0, sizeof applied);
    qa_cleanup pairs_scope; qa_cleanup_push(&pairs_scope, free, pairs);
    bool ok = qa_compat_qc_apply_entity_pairs(&s->machine, slot, pairs, record->pair_count, &applied);
    qa_cleanup_pop(&pairs_scope);
    free(pairs);
    if (!ok) qa_range_error("Out of memory");
    qa_compat_qc_text_pairs_free(&applied.unknown); qa_compat_qc_text_pairs_free(&applied.retained);
    if ((ibits(trunc(fget(s, at(s, slot), "spawnflags"))) & excluded) != 0) { (void)qa_source_actor_slots_free(s->slots, actor); continue; }
    char *classname = xstrdup(qstring(s, qa_compat_qc_words_int(at(s, slot), field(s, "classname"))));
    const qa_compat_qc_function *spawn = function_or_null(s, classname);
    if (classname[0] == '\0' || !spawn) {
      size_t n = strlen(classname) + 64;
      char *line = xalloc(n);
      (void)snprintf(line, n, "%s for: %s\n", classname[0] == '\0' ? "No classname" : "No spawn function", classname);
      free(classname);
      print(s, line); free(line);
      (void)qa_source_actor_slots_free(s->slots, actor);
      continue;
    }
    free(classname);
    invoke(s, spawn->index, slot, 0, s->current_time);
    qa_compat_qc_broadcast_messages_flush_signon(&s->messages);
  }
  qa_cleanup_pop(&scope);
  qa_entities_free(&entities);
  s->spawning = false;
  qa_app_sim_shared_physics_set_world_gravity(s->options.physics, qa_cvar_registry_variable_value(s->cvars, "sv_gravity"));
  qa_compat_qc_broadcast_messages_flush(&s->messages);
}
void qa_app_sim_quakec_source_begin_frame(S *s, qa_frame_context frame) {
  if (frame.time.kind != QA_SOURCE_TIME_SECONDS || frame.elapsed.kind != QA_SOURCE_TIME_SECONDS || !isfinite(frame.time.value))
    fail("QC frame requires source seconds");
  if (!s->netquake_wire_attached && !s->quakeworld) {
    if (s->nq_signon_count != 0) fail("QuakeC MSG_INIT requires a native NetQuake wire consumer");
    size_t count = 0;
    nq_routed *drained = qa_event_ring_drain_all(&s->nq_routed, &count);
    for (size_t i = 0; i < count; i++) {
      nq_routed *entry = &drained[i];
      qa_actor_id *targets = xalloc((entry->count ? entry->count : 1) * sizeof *targets);
      bool *has = xalloc((entry->count ? entry->count : 1) * sizeof *has);
      for (size_t v = 0; v < entry->view_count; v++)
        if (entry->views[v].index >= 0 && (size_t)entry->views[v].index < entry->count) {
          has[entry->views[v].index] = entry->views[v].has_actor; targets[entry->views[v].index] = entry->views[v].actor;
        }
      nq_route(s, entry->messages, entry->count, entry->destination, targets, has, entry->count);
      free(targets); free(has);
      nq_routed_free(entry);
    }
    free(drained);
  }
  s->current_time = frame.time.value;
  gset(s, "frametime", frame.elapsed.value);
  qa_app_sim_shared_physics_set_world_gravity(s->options.physics, qa_cvar_registry_variable_value(s->cvars, "sv_gravity"));
  invoke(s, function_named(s, "StartFrame"), 0, 0, s->current_time);
}
void qa_app_sim_quakec_source_before_actor(S *s, qa_owned_actor actor) {
  int64_t slot;
  if (source_slot(s, actor.id, &slot) && gfloat(s, "force_retouch") != 0) {
    qa_compat_qc_world_host_link(&s->world_host, slot);
    qa_app_sim_shared_physics_touch_triggers(s->options.physics, actor);
  }
}
static void take_effects(S *s) {
  size_t count = 0;
  qa_cvar_effect *effects = qa_cvar_registry_take_effects(s->cvars, &count);
  for (size_t i = 0; i < count; i++) qa_cvar_effect_free(&effects[i]);
  free(effects);
}
static void free_nq_record(const void *record, int64_t id, void *ctx) { (void)id; (void)ctx; nq_routed_free((nq_routed *)record); }
static void free_routed_record(const void *record, int64_t id, void *ctx) { (void)id; (void)ctx; routed_free((void *)record); }
static void nq_routed_reset(S *s) {
  qa_event_ring_for_each_live(&s->nq_routed, free_nq_record, NULL);
  qa_event_ring_reset(&s->nq_routed);
}
void qa_app_sim_quakec_source_end_frame(S *s) {
  /* Source cvar effects are change notifications with no in-process consumer
   * (server info is recomposed from registry state); the checkpoint already
   * drops them, so frame end must too or they accumulate forever. */
  take_effects(s);
  qa_compat_qc_broadcast_messages_flush(&s->messages);
  if (s->local_messages_started && !s->netquake_wire_attached) nq_routed_reset(s);
  int64_t offset = qa_compat_qc_machine_global_offset(&s->machine, "force_retouch");
  double current = qa_compat_qc_words_float(&s->machine.globals, offset);
  if (current != 0) qa_compat_qc_words_set_float(&s->machine.globals, offset, s->machine.numeric->subtract(s->machine.numeric->ctx, current, 1));
}
void qa_app_sim_quakec_source_run_think(S *s, qa_owned_actor actor, qa_frame_context frame) {
  int64_t slot;
  if (!source_slot(s, actor.id, &slot)) return;
  qa_compat_qc_words *words = at(s, slot);
  double due = fget(s, words, "nextthink");
  qa_clock_profile profile; memset(&profile, 0, sizeof profile);
  profile.kind = QA_CLOCK_Q1_NETQUAKE; profile.minimum_frame_seconds = 0.001; profile.maximum_frame_seconds = 0.1;
  profile.has_fixed_frame_seconds = false;
  qa_source_time_result time = qa_think_callback_time(profile, (qa_source_time){QA_SOURCE_TIME_SECONDS, due}, frame);
  if (!time.has_value) return;
  fset(s, words, "nextthink", 0);
  invoke(s, qa_compat_qc_words_int(words, field(s, "think")), slot, 0, time.value.value);
}
bool qa_app_sim_quakec_source_read_move_type(S *s, qa_actor_id actor, double *out) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) return false;
  *out = fget(s, at(s, slot), "movetype");
  return true;
}
bool qa_app_sim_quakec_source_notarget(S *s, qa_actor_id actor, bool *out) {
  int64_t slot;
  if (!source_slot(s, actor, &slot)) return false;
  *out = qa_js_bit_and(trunc(fget(s, at(s, slot), "flags")), 128) != 0;
  return true;
}
bool qa_app_sim_quakec_source_collision(S *s, qa_owned_actor actor, qa_app_sim_shared_solid *out) {
  qa_compat_qc_shared_solid solid;
  if (!qa_compat_qc_actor_state_collision(&s->actor_state, actor, &solid)) return false;
  memset(out, 0, sizeof *out);
  out->solid = solid.solid; out->has_model = solid.has_model; out->model = solid.model; out->family = solid.family;
  if (solid.has_owner) { s->solid_owner = solid.owner; out->owner = &s->solid_owner; }
  out->has_monster = solid.has_monster; out->monster = solid.monster;
  out->has_dead_monster = solid.has_dead_monster; out->dead_monster = solid.dead_monster;
  out->q1_corpse = solid.q1_corpse; out->has_item = solid.has_item; out->item = solid.item;
  return true;
}
bool qa_app_sim_quakec_source_motion(S *s, qa_owned_actor actor, qa_body_state body, qa_q2_motion *out) {
  return qa_compat_qc_actor_state_motion(&s->actor_state, actor, body, out);
}
qa_app_sim_shared_physics_flags qa_app_sim_quakec_source_flags(S *s, qa_owned_actor actor) {
  qa_compat_qc_shared_physics_flags flags = qa_compat_qc_actor_state_flags(&s->actor_state, actor);
  qa_app_sim_shared_physics_flags out; memset(&out, 0, sizeof out);
  out.has_always_touch = flags.has_always_touch; out.always_touch = flags.always_touch;
  out.has_fly = flags.has_fly; out.fly = flags.fly; out.has_swim = flags.has_swim; out.swim = flags.swim;
  out.has_partial_ground = flags.has_partial_ground; out.partial_ground = flags.partial_ground;
  out.has_dead = flags.has_dead; out.dead = flags.dead; out.has_player = flags.has_player; out.player = flags.player;
  out.has_water_level = flags.has_water_level; out.water_level = flags.water_level;
  out.has_water_type = flags.has_water_type; out.water_type = flags.water_type;
  out.has_enemy = flags.has_enemy;
  if (flags.has_enemy && !flags.enemy_is_null) { s->flags_enemy = flags.enemy; out.enemy = &s->flags_enemy; }
  out.has_delta_yaw = flags.has_delta_yaw; out.delta_yaw = flags.delta_yaw;
  out.has_team_slave = flags.has_team_slave; out.team_slave = flags.team_slave;
  { /* TS QuakeCActorState.flags() literal order. */
    static const unsigned char order[] = {QA_APP_SIM_FLAG_FLY, QA_APP_SIM_FLAG_SWIM, QA_APP_SIM_FLAG_PARTIAL_GROUND, QA_APP_SIM_FLAG_PLAYER,
                                          QA_APP_SIM_FLAG_WATER_LEVEL, QA_APP_SIM_FLAG_WATER_TYPE, QA_APP_SIM_FLAG_DEAD};
    qa_app_sim_physics_flags_order(&out, order, sizeof order);
  }
  return out;
}
void qa_app_sim_quakec_source_write_flags(S *s, qa_owned_actor actor, qa_app_sim_shared_physics_flags changes) {
  qa_compat_qc_shared_physics_flags flags; memset(&flags, 0, sizeof flags);
  flags.has_always_touch = changes.has_always_touch; flags.always_touch = changes.always_touch;
  flags.has_fly = changes.has_fly; flags.fly = changes.fly; flags.has_swim = changes.has_swim; flags.swim = changes.swim;
  flags.has_partial_ground = changes.has_partial_ground; flags.partial_ground = changes.partial_ground;
  flags.has_dead = changes.has_dead; flags.dead = changes.dead; flags.has_player = changes.has_player; flags.player = changes.player;
  flags.has_water_level = changes.has_water_level; flags.water_level = changes.water_level;
  flags.has_water_type = changes.has_water_type; flags.water_type = changes.water_type;
  flags.has_enemy = changes.has_enemy; flags.enemy_is_null = changes.enemy == NULL;
  if (changes.enemy) flags.enemy = *changes.enemy;
  flags.has_delta_yaw = changes.has_delta_yaw; flags.delta_yaw = changes.delta_yaw;
  flags.has_team_slave = changes.has_team_slave; flags.team_slave = changes.team_slave;
  qa_compat_qc_actor_state_write_flags(&s->actor_state, actor, flags);
}
const char *qa_app_sim_quakec_source_classname(S *s, qa_actor_id actor) {
  int64_t slot;
  set_scratch(&s->classname_scratch, source_slot(s, actor, &slot) ? qstring(s, qa_compat_qc_words_int(at(s, slot), field(s, "classname"))) : "");
  return s->classname_scratch;
}
void qa_app_sim_quakec_source_check_water_transition(S *s, qa_owned_actor actor) {
  int64_t slot;
  if (!source_slot(s, actor.id, &slot)) return;
  qa_compat_qc_words *words = at(s, slot);
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = vget(s, words, "origin"); query.target.is_model = false;
  query.policy.kind = "q1"; query.policy.move = "normal"; query.policy.has_hull = false;
  query.numeric = qa_q1_donor_profile; query.pass_actor = &actor.id;
  qa_point_contents_result contents = qa_shared_point_contents(s->options.scene, query);
  if (!is(contents.kind, "q1")) fail("QC water transition requires Q1 contents");
  qa_q1_water_transition_result decision = qa_q1_water_transition(fget(s, words, "watertype"), contents.contents);
  if (decision.splash) {
    const char *path = "misc/h2ohit1.wav";
    if (!precache_get(s, "sound:misc/h2ohit1.wav")) print(s, "SV_StartSound: misc/h2ohit1.wav not precacheed\n");
    else {
      qa_q1_event event; memset(&event, 0, sizeof event);
      event.kind = "sound"; event.has_actor = true; event.actor = actor.id; event.path = path;
      event.sound_channel = "auto"; event.volume = 1; event.attenuation = 1;
      emit_q1(s, event, NULL);
    }
  }
  fset(s, words, "watertype", decision.water_type); fset(s, words, "waterlevel", decision.water_level);
}
void qa_app_sim_quakec_source_write_angular_velocity(S *s, qa_owned_actor actor, qa_vec3 value) {
  qa_compat_qc_actor_state_write_angular_velocity(&s->actor_state, actor, value);
}

/* ---------------- travel ---------------- */
static void travel_free(S *s) {
  for (size_t i = 0; i < s->travel_cvar_count; i++) { free((char *)s->travel_cvars[i].name); free((char *)s->travel_cvars[i].value); }
  free(s->travel_cvars); s->travel_cvars = NULL; s->travel_cvar_count = 0;
  for (size_t i = 0; i < s->travel_client_count; i++) info_free_items((qa_app_sim_cvar_name_value *)s->travel_clients[i].userinfo, s->travel_clients[i].userinfo_count);
  free(s->travel_clients); s->travel_clients = NULL; s->travel_client_count = 0;
  free(s->travel_parameters); s->travel_parameters = NULL;
}
qa_app_sim_quakec_source_travel qa_app_sim_quakec_source_capture_travel(S *s) {
  if (s->spawning) fail("Native QuakeC travel requires a loaded source world");
  travel_free(s);
  double server_flags = gfloat(s, "serverflags");
  size_t count = 0;
  const qa_client_id *clients = qa_app_sim_quakec_source_connected_client_identities(s, &count);
  s->travel_clients = xalloc((count ? count : 1) * sizeof *s->travel_clients);
  s->travel_parameters = xalloc((count ? count : 1) * 16 * sizeof *s->travel_parameters);
  for (size_t i = 0; i < count; i++) {
    int64_t slot = clients[i].slot + 1;
    qa_owned_actor actor;
    if (slot_actor(s, slot, &actor) && active(s, actor.id)) {
      invoke(s, function_named(s, "SetChangeParms"), slot, 0, s->current_time);
      double values[16]; read_spawn_parameters(s, values);
      spawn_set(s, slot, values);
    }
    spawn_entry *parameters = spawn_get(s, slot);
    if (!parameters) fail("Native QuakeC connection has no spawn parameters");
    memcpy(&s->travel_parameters[i * 16], parameters->values, sizeof parameters->values);
    info_entry *entry = info_get(s, slot);
    qa_app_sim_cvar_name_value *items; size_t item_count;
    info_copy((qa_app_sim_quakec_info){entry ? entry->items : NULL, entry ? entry->count : 0}, &items, &item_count);
    s->travel_clients[i] = (qa_app_sim_quakec_client_travel){clients[i], &s->travel_parameters[i * 16], 16, items, item_count,
                                                             slot_set_has(&s->spectator_slots, slot) ? "spectator" : "player"};
    s->travel_client_count++;
  }
  size_t cvar_count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(s->cvars, 0, &cvar_count);
  s->travel_cvars = xalloc((cvar_count ? cvar_count : 1) * sizeof *s->travel_cvars);
  for (size_t i = 0; i < cvar_count; i++) {
    s->travel_cvars[i].name = xstrdup(snapshots[i].name);
    s->travel_cvars[i].value = xstrdup(snapshots[i].latched_value ? snapshots[i].latched_value : snapshots[i].value);
    qa_cvar_read_free(&snapshots[i]);
  }
  free(snapshots);
  s->travel_cvar_count = cvar_count;
  return (qa_app_sim_quakec_source_travel){s->binding->kind, server_flags, s->travel_cvars, s->travel_cvar_count,
                                           s->travel_clients, s->travel_client_count};
}
void qa_app_sim_quakec_source_restore_travel(S *s, qa_app_sim_quakec_source_travel travel) {
  if (!is(travel.kind, s->binding->kind) || !s->spawning || s->identity_count != 0)
    fail("Native QuakeC travel requires a fresh source world with the same ABI");
  gset(s, "serverflags", travel.server_flags);
  for (size_t i = 0; i < travel.cvar_count; i++) {
    qa_cvar_snapshot existing;
    if (!qa_cvar_registry_get(s->cvars, travel.cvars[i].name, &existing)) {
      qa_cvar_snapshot registered;
      if (qa_cvar_registry_register(s->cvars, travel.cvars[i].name, travel.cvars[i].value, 0, &registered)) qa_cvar_read_free(&registered);
    } else qa_cvar_read_free(&existing);
    qa_cvar_snapshot set;
    if (qa_cvar_registry_set(s->cvars, travel.cvars[i].name, travel.cvars[i].value, true, &set)) qa_cvar_read_free(&set);
  }
  for (size_t i = 0; i < travel.client_count; i++) {
    const qa_app_sim_quakec_client_travel *record = &travel.clients[i];
    int64_t slot = record->client.slot + 1;
    bool finite = true;
    for (size_t j = 0; j < record->parameter_count; j++) if (!isfinite(record->parameters[j])) finite = false;
    if (slot < 1 || slot > s->reserved_client_slots || identity_get(s, slot) || record->parameter_count != 16 || !finite)
      fail("Invalid native QuakeC travel client parameters");
    if (is(record->role, "spectator")) {
      if (!s->quakeworld) fail("NetQuake travel cannot contain a spectator");
      slot_set_add(&s->spectator_slots, slot);
    }
    identity_set(s, slot, record->client);
    spawn_set(s, slot, record->parameters);
    info_set(s, slot, (qa_app_sim_quakec_info){record->userinfo, record->userinfo_count});
  }
}

/* ---------------- checkpoint value trees ---------------- */
static qa_save_value *sv_check(qa_save_value *v) { if (!v) qa_range_error("Out of memory"); return v; }
static void put(qa_save_value *object, const char *key, qa_save_value *value) {
  if (!qa_save_value_set(object, key, sv_check(value))) qa_range_error("Out of memory");
}
static void push(qa_save_value *array, qa_save_value *value) {
  if (!qa_save_value_push(array, sv_check(value))) qa_range_error("Out of memory");
}
static qa_save_value *num(double v) { return sv_check(qa_save_value_number(v)); }
static qa_save_value *str(const char *v) { return sv_check(qa_save_value_string(v)); }
static qa_save_value *boolean(bool v) { return sv_check(qa_save_value_boolean(v)); }
static qa_save_value *obj(void) { return sv_check(qa_save_value_object()); }
static qa_save_value *arr(void) { return sv_check(qa_save_value_array()); }
static qa_save_value *saved_actor(const qa_actor_id *actor) {
  if (!actor) return sv_check(qa_save_value_null());
  qa_save_value *o = obj();
  put(o, "slot", num((double)actor->slot)); put(o, "generation", num((double)actor->generation));
  return o;
}
static qa_save_value *saved_id(qa_saved_actor_id id) {
  qa_save_value *o = obj();
  put(o, "slot", num(id.slot)); put(o, "generation", num(id.generation));
  return o;
}
static qa_save_value *vec(qa_vec3 v) {
  qa_save_value *o = obj();
  put(o, "x", num(v.x)); put(o, "y", num(v.y)); put(o, "z", num(v.z));
  return o;
}
static qa_save_value *owned_bytes(uint8_t *bytes, size_t length) {
  qa_save_value *v = qa_save_value_bytes(bytes, length);
  free(bytes);
  return sv_check(v);
}
static qa_save_value *clone(const qa_save_value *v) {
  if (!v) return sv_check(qa_save_value_undefined());
  switch (v->kind) {
    case QA_SAVE_UNDEFINED: return sv_check(qa_save_value_undefined());
    case QA_SAVE_NULL: return sv_check(qa_save_value_null());
    case QA_SAVE_BOOLEAN: return boolean(v->boolean);
    case QA_SAVE_NUMBER: return num(v->number);
    case QA_SAVE_BIGINT: return sv_check(qa_save_value_bigint_text(v->text));
    case QA_SAVE_STRING: return str(v->text);
    case QA_SAVE_BYTES: return sv_check(qa_save_value_bytes(v->bytes, v->byte_length));
    case QA_SAVE_ARRAY: { qa_save_value *a = arr(); for (size_t i = 0; i < v->item_count; i++) push(a, clone(v->items[i])); return a; }
    default: { qa_save_value *o = obj(); for (size_t i = 0; i < v->entry_count; i++) put(o, v->entries[i].key, clone(v->entries[i].value)); return o; }
  }
}
/* isDeepStrictEqual over checkpoint trees (object key order is not significant). */
static bool sv_equal(const qa_save_value *a, const qa_save_value *b) {
  bool a_undefined = !a || a->kind == QA_SAVE_UNDEFINED, b_undefined = !b || b->kind == QA_SAVE_UNDEFINED;
  if (a_undefined || b_undefined) return a_undefined == b_undefined;
  if (a->kind != b->kind) return false;
  switch (a->kind) {
    case QA_SAVE_NULL: return true;
    case QA_SAVE_BOOLEAN: return a->boolean == b->boolean;
    case QA_SAVE_NUMBER: return a->number == b->number || (isnan(a->number) && isnan(b->number));
    case QA_SAVE_BIGINT: case QA_SAVE_STRING: return strcmp(a->text, b->text) == 0;
    case QA_SAVE_BYTES: return a->byte_length == b->byte_length && (a->byte_length == 0 || memcmp(a->bytes, b->bytes, a->byte_length) == 0);
    case QA_SAVE_ARRAY:
      if (a->item_count != b->item_count) return false;
      for (size_t i = 0; i < a->item_count; i++) if (!sv_equal(a->items[i], b->items[i])) return false;
      return true;
    default:
      if (a->entry_count != b->entry_count) return false;
      for (size_t i = 0; i < a->entry_count; i++) if (!sv_equal(a->entries[i].value, qa_save_value_get(b, a->entries[i].key))) return false;
      return true;
  }
}
static qa_save_value *destination_value(qa_compat_qc_message_destination d) {
  qa_save_value *o = obj();
  static const char *const kinds[4] = {"broadcast", "client", "signon", "multicast"};
  put(o, "kind", str(kinds[d.kind]));
  if (d.kind == QA_QC_DEST_BROADCAST) put(o, "reliable", boolean(d.reliable));
  else if (d.kind == QA_QC_DEST_CLIENT) { put(o, "actor", saved_actor(&d.actor)); put(o, "reliable", boolean(d.reliable)); }
  else if (d.kind == QA_QC_DEST_MULTICAST) {
    put(o, "origin", vec(d.origin)); put(o, "visibility", str(d.visibility ? d.visibility : "all")); put(o, "reliable", boolean(d.reliable));
  }
  return o;
}
static qa_save_value *scalar_value(qa_qc_pickup_scalar v) {
  qa_save_value *o = obj();
  if (v.is_global) { put(o, "kind", str("global")); put(o, "word", num(v.word)); }
  else { put(o, "kind", str("field")); put(o, "name", str(v.name)); }
  return o;
}
static qa_save_value *descriptor_value(const qa_q1qc_pickup_descriptor *d) {
  qa_save_value *o = obj();
  put(o, "value", d->value_is_string ? str(d->value_string) : num(d->value_number));
  put(o, "item", str(d->item));
  if (!d->has_resource) put(o, "resource", sv_check(qa_save_value_null()));
  else {
    qa_save_value *r = obj();
    if (d->resource.kind == QA_PICKUP_RESOURCE_PROTECTION) { put(r, "kind", str("protection")); put(r, "channel", str(d->resource.channel == QA_PROTECTION_REGULAR ? "regular" : "powered")); }
    else { put(r, "kind", str("inventory")); put(r, "item", str(d->resource.item)); }
    put(o, "resource", r);
  }
  if (d->has_count) put(o, "count", scalar_value(d->count));
  if (d->has_supply) {
    qa_save_value *supply = obj(), *quantity = obj();
    put(supply, "item", str(d->supply.item));
    if (d->supply.quantity_is_global) { put(quantity, "kind", str("global")); put(quantity, "word", num((double)d->supply.quantity_word)); }
    else { put(quantity, "kind", str("field")); put(quantity, "name", str(d->supply.quantity_name)); }
    put(supply, "quantity", quantity);
    if (d->supply.has_leave) put(supply, "leave", num((double)d->supply.leave));
    put(o, "supply", supply);
  }
  return o;
}
static qa_save_value *region_value(qa_q1qc_inline_region r) {
  qa_save_value *o = obj();
  put(o, "functionIndex", num((double)r.function_index)); put(o, "entry", num((double)r.entry)); put(o, "exit", num((double)r.exit));
  if (r.replaceable) put(o, "replaceable", boolean(true));
  if (r.has_standalone) {
    qa_save_value *standalone = obj();
    put(standalone, "saved", num((double)r.standalone_saved));
    if (r.standalone_scope) put(standalone, "scope", str(r.standalone_scope));
    put(o, "standalone", standalone);
  }
  return o;
}
static qa_save_value *word_items_value(const qa_q1qc_pickup_word_item *items, size_t count) {
  qa_save_value *a = arr();
  for (size_t i = 0; i < count; i++) { qa_save_value *o = obj(); put(o, "word", num((double)items[i].word)); put(o, "item", str(items[i].item)); push(a, o); }
  return a;
}
static qa_save_value *pickup_stage_value(const qa_q1qc_pickup_stage *stage) {
  qa_save_value *o = obj();
  put(o, "functionIndex", num((double)stage->function_index));
  if (stage->has_dropped) put(o, "dropped", scalar_value(stage->dropped));
  qa_save_value *descriptor = obj();
  if (is(stage->descriptor_kind, "string") || is(stage->descriptor_kind, "float")) {
    put(descriptor, "field", str(stage->descriptor_field));
    put(descriptor, "kind", str(stage->descriptor_kind));
    qa_save_value *values = arr();
    for (size_t i = 0; i < stage->value_count; i++) push(values, descriptor_value(&stage->values[i]));
    put(descriptor, "values", values);
  } else {
    put(descriptor, "kind", str(stage->descriptor_kind));
    if (stage->value_count) put(descriptor, "value", descriptor_value(&stage->values[0]));
    if (is(stage->descriptor_kind, "cargo")) {
      qa_save_value *counters = arr();
      for (size_t i = 0; i < stage->counter_count; i++) {
        qa_save_value *c = obj(); put(c, "field", str(stage->counters[i].field)); put(c, "item", str(stage->counters[i].item)); push(counters, c);
      }
      put(descriptor, "counters", counters);
      put(descriptor, "weapons", word_items_value(stage->cargo_weapons, stage->cargo_weapon_count));
    }
  }
  put(o, "descriptor", descriptor);
  qa_save_value *regions = arr();
  for (size_t i = 0; i < stage->region_count; i++) {
    const qa_q1qc_pickup_region *r = &stage->regions[i];
    qa_save_value *entry = obj(), *operation = obj();
    put(entry, "region", region_value(r->region));
    if (r->self_recipient) put(entry, "self", str("recipient"));
    put(operation, "kind", str(r->operation.kind));
    if (is(r->operation.kind, "decision")) { put(operation, "word", num((double)r->operation.word)); put(operation, "accepted", num(r->operation.accepted)); }
    else if (is(r->operation.kind, "cargo-ownership") || is(r->operation.kind, "cargo-current")) put(operation, "word", num((double)r->operation.word));
    else if (is(r->operation.kind, "counter")) { put(operation, "field", str(r->operation.field)); put(operation, "item", str(r->operation.item)); }
    put(entry, "operation", operation);
    push(regions, entry);
  }
  put(o, "regions", regions);
  if (stage->has_source_selection) {
    qa_save_value *selection = obj(), *calls = arr();
    put(selection, "functionIndex", num((double)stage->selection_function));
    for (size_t i = 0; i < stage->selection_call_count; i++) push(calls, num((double)stage->selection_calls[i]));
    put(selection, "calls", calls);
    put(selection, "weapons", word_items_value(stage->selection_weapons, stage->selection_weapon_count));
    put(o, "sourceSelection", selection);
  }
  if (stage->has_source_effect) {
    qa_save_value *effect = obj();
    put(effect, "word", num((double)stage->source_effect_word)); put(effect, "value", num(stage->source_effect_value));
    put(o, "sourceEffect", effect);
  }
  return o;
}
/* Declarations as the checkpoint records them: the compatibility JSON
 * subtrees the readers decoded (TS encodes the decoded objects, which keep
 * the JSON's shape), and the derived pickup stages. */
static qa_save_value *declaration_value(S *s, const char *key) {
  const qa_save_value *tree = s->prepared->has_compatibility ? s->prepared->compatibility.tree : NULL;
  const qa_save_value *value = tree ? qa_save_value_get(tree, key) : NULL;
  if (value) return clone(value);
  return is(key, "teams") ? arr() : sv_check(qa_save_value_null());
}
static qa_save_value *pickup_callers_value(S *s) {
  qa_save_value *a = arr();
  if (s->prepared->has_declared_pickups)
    for (size_t i = 0; i < s->prepared->declared_pickup_count; i++) push(a, pickup_stage_value(&s->prepared->declared_pickups[i]));
  return a;
}
static qa_save_value *entries_value(S *s, const qa_compat_qc_routed_message *entries, size_t count) {
  return sv_check(qa_compat_qc_broadcast_capture_entries(&s->messages, entries, count));
}
static qa_save_value *nq_bytes_value(S *s, const qa_netquake_message *messages, size_t count) {
  size_t length = 0;
  uint8_t *bytes = qa_compat_qc_broadcast_capture_netquake_messages(&s->messages, messages, count, &length);
  return owned_bytes(bytes, length);
}
static qa_save_value *views_value(const view_entry *views, size_t count) {
  qa_save_value *a = arr();
  for (size_t i = 0; i < count; i++) {
    qa_save_value *o = obj();
    put(o, "index", num((double)views[i].index)); put(o, "actor", saved_actor(views[i].has_actor ? &views[i].actor : NULL));
    push(a, o);
  }
  return a;
}
typedef struct collect { qa_save_value *array; S *s; } collect;
static void collect_frag(const void *record, int64_t id, void *ctx) {
  (void)id; const qa_app_sim_quakec_frag *f = record; collect *c = ctx;
  qa_save_value *o = obj(); put(o, "killer", saved_actor(&f->killer)); put(o, "victim", saved_actor(&f->victim)); push(c->array, o);
}
static void collect_routed(const void *record, int64_t id, void *ctx) {
  (void)id; const qa_app_sim_quakec_routed *r = record; collect *c = ctx;
  qa_save_value *o = obj(); put(o, "entries", entries_value(c->s, r->entries, r->entry_count)); put(o, "destination", destination_value(r->destination));
  push(c->array, o);
}
static void collect_nq(const void *record, int64_t id, void *ctx) {
  (void)id; const nq_routed *r = record; collect *c = ctx;
  qa_save_value *o = obj();
  put(o, "bytes", nq_bytes_value(c->s, r->messages, r->count)); put(o, "destination", destination_value(r->destination));
  put(o, "views", views_value(r->views, r->view_count));
  push(c->array, o);
}
static qa_save_value *host_state(S *s) {
  qa_save_value *o = obj();
  put(o, "kind", str(s->binding->kind));
  put(o, "teams", declaration_value(s, "teams"));
  put(o, "messageDialect", str(s->prepared->message_dialect ? s->prepared->message_dialect : "known-retail"));
  put(o, "pickupCallers", pickup_callers_value(s));
  put(o, "weaponStage", declaration_value(s, "weaponStage"));
  put(o, "combat", declaration_value(s, "combat"));
  put(o, "maxClients", num(s->options.max_clients));
  put(o, "reservedClientSlots", num((double)s->reserved_client_slots));
  put(o, "currentTime", num(s->current_time));
  put(o, "changeLevelIssued", boolean(s->change_level_issued));
  put(o, "spawning", boolean(s->spawning));
  qa_save_value *a = arr();
  for (size_t i = 0; i < s->active_clients.count; i++) push(a, saved_actor(&s->active_clients.items[i]));
  put(o, "activeClients", a);
  qa_compat_qc_borrowed_checkpoint rows[2048];
  size_t row_count = qa_compat_qc_borrowed_actors_checkpoint(&s->borrowed, rows, 2048);
  a = arr();
  for (size_t i = 0; i < row_count && i < 2048; i++) {
    qa_save_value *row = obj(); put(row, "actor", saved_id(rows[i].actor)); put(row, "slot", num((double)rows[i].slot)); push(a, row);
  }
  put(o, "borrowedActors", a);
  a = arr();
  for (size_t i = 0; i < s->pending_count; i++) {
    qa_save_value *row = obj();
    put(row, "actor", saved_actor(&s->pending[i].actor)); put(row, "weapon", str(s->weapons[s->pending[i].weapon].item));
    put(row, "following", boolean(s->pending[i].following));
    push(a, row);
  }
  put(o, "pendingWeapons", a);
  a = arr();
  for (size_t i = 0; i < s->user_info_count; i++) {
    qa_save_value *row = obj(), *values = arr();
    put(row, "slot", num((double)s->user_info[i].slot));
    for (size_t j = 0; j < s->user_info[i].count; j++) {
      qa_save_value *kv = obj(); put(kv, "key", str(s->user_info[i].items[j].name)); put(kv, "value", str(s->user_info[i].items[j].value)); push(values, kv);
    }
    put(row, "values", values);
    push(a, row);
  }
  put(o, "userInfo", a);
  a = arr(); for (size_t i = 0; i < s->spectator_slots.count; i++) push(a, num((double)s->spectator_slots.items[i]));
  put(o, "spectatorSlots", a);
  put(o, "originalSaveExtensionText", str(s->original_save_extension_text));
  a = arr();
  for (size_t i = 0; i < s->spawn_count; i++) {
    qa_save_value *row = obj(), *values = arr();
    put(row, "slot", num((double)s->spawn_parameters[i].slot));
    for (int j = 0; j < 16; j++) push(values, num(s->spawn_parameters[i].values[j]));
    put(row, "values", values);
    push(a, row);
  }
  put(o, "spawnParameters", a);
  a = arr();
  for (size_t i = 0; i < s->identity_count; i++) {
    qa_save_value *row = obj(); put(row, "slot", num((double)s->identities[i].slot)); put(row, "clientSlot", num((double)s->identities[i].client.slot)); push(a, row);
  }
  put(o, "clientIdentities", a);
  a = arr(); for (size_t i = 0; i < s->prepared_clients.count; i++) push(a, num((double)s->prepared_clients.items[i]));
  put(o, "preparedClients", a);
  collect c = {arr(), s};
  qa_event_ring_for_each_live(&s->frag_records, collect_frag, &c);
  put(o, "fragRecords", c.array);
  c.array = arr();
  qa_event_ring_for_each_live(&s->routed, collect_routed, &c);
  put(o, "routed", c.array);
  qa_save_value *local = obj();
  put(local, "started", boolean(s->local_messages_started));
  qa_app_sim_quakec_capture captured = qa_app_sim_quakec_local_messages_capture(s->local_messages);
  put(local, "baseline", nq_bytes_value(s, captured.baseline, captured.baseline_count));
  a = arr();
  for (size_t i = 0; i < captured.client_count; i++) {
    qa_save_value *row = obj();
    put(row, "actor", saved_actor(&captured.clients[i].actor));
    put(row, "bytes", nq_bytes_value(s, captured.clients[i].messages, captured.clients[i].message_count));
    push(a, row);
  }
  put(local, "clients", a);
  qa_app_sim_quakec_capture_free(&captured);
  qa_app_sim_quakec_captured_views views = qa_app_sim_quakec_local_messages_capture_views(s->local_messages);
  qa_save_value *v = obj();
  put(v, "baseline", saved_actor(views.has_baseline ? &views.baseline : NULL));
  a = arr();
  for (size_t i = 0; i < views.client_count; i++) {
    qa_save_value *row = obj(); put(row, "actor", saved_actor(&views.clients[i].actor)); put(row, "target", saved_actor(&views.clients[i].target)); push(a, row);
  }
  put(v, "clients", a);
  free(views.clients);
  put(local, "views", v);
  put(o, "localMessages", local);
  put(o, "netQuakeSignon", nq_bytes_value(s, s->nq_signon, s->nq_signon_count));
  put(o, "netQuakeSignonViews", views_value(s->nq_signon_views, s->nq_signon_view_count));
  c.array = arr();
  qa_event_ring_for_each_live(&s->nq_routed, collect_nq, &c);
  put(o, "netQuakeRouted", c.array);
  put(o, "signon", entries_value(s, s->signon, s->signon_count));
  a = arr();
  for (size_t i = 0; i < s->model_entry_count; i++) {
    qa_save_value *row = obj(), *bounds = obj();
    put(row, "name", str(s->models[i].name)); put(row, "index", num(s->models[i].index));
    put(bounds, "min", vec(s->models[i].bounds.min)); put(bounds, "max", vec(s->models[i].bounds.max));
    put(row, "bounds", bounds);
    push(a, row);
  }
  put(o, "models", a);
  a = arr();
  for (size_t i = 0; i < s->precached_count; i++) {
    qa_save_value *row = obj();
    put(row, "key", str(s->precached[i].key)); put(row, "index", num(s->precached[i].value.index));
    put(row, "id", str(s->precached[i].value.resource.id)); put(row, "digest", str(s->precached[i].value.resource.digest));
    push(a, row);
  }
  put(o, "precached", a);
  put(o, "modelCount", num(s->model_count)); put(o, "soundCount", num(s->sound_count));
  put(o, "cvars", qa_cvar_registry_capture_quakec_state(s->cvars));
  qa_q1_client_visibility_state visibility = qa_q1_client_visibility_capture(s->clients.visibility);
  qa_save_value *vis = obj();
  put(vis, "lastCheckSlot", num(visibility.last_check_slot)); put(vis, "lastCheckTime", num(visibility.last_check_time));
  put(vis, "checkedCluster", visibility.has_checked_cluster ? num(visibility.checked_cluster) : sv_check(qa_save_value_null()));
  put(o, "visibility", vis);
  put(o, "messages", qa_compat_qc_broadcast_messages_capture(&s->messages));
  size_t projectile_count = 0;
  qa_q1_id1_projectile_capture *projectiles = qa_q1_id1_projectile_attacks_capture(s->projectiles, &projectile_count);
  a = arr();
  for (size_t i = 0; i < projectile_count; i++) {
    qa_save_value *row = obj();
    put(row, "actor", saved_id(projectiles[i].actor)); put(row, "owner", saved_id(projectiles[i].owner));
    put(row, "emittedAt", num(projectiles[i].emitted_at)); put(row, "weapon", str(projectiles[i].weapon));
    push(a, row);
  }
  free(projectiles);
  put(o, "projectiles", a);
  return o;
}

/* ---------------- checkpoint restore ---------------- */
typedef qa_save_reader R;
static R fld(const R *r, const char *name) {
  R out;
  if (!qa_save_reader_field(r, name, &out)) qa_range_error("Out of memory");
  return out;
}
static R itm(const R *r, size_t index) {
  R out;
  if (!qa_save_reader_item(r, index, &out)) qa_range_error("Out of memory");
  return out;
}
static double fint(const R *r, const char *name, double minimum) { R f = fld(r, name); return qa_save_reader_integer_min(&f, minimum); }
static qa_saved_actor_id read_saved(const R *r) { return qa_read_saved_actor(r); }
static qa_actor_id resolve_saved(S *s, qa_saved_actor_id saved) {
  return qa_session_actor_registry_reference_saved(s->options.actors, saved, "checkpoint");
}
static qa_actor_id resolver(void *ctx, qa_saved_actor_id saved) { return resolve_saved(ctx, saved); }
static qa_actor_id ref_actor(S *s, const R *entry) {
  qa_saved_actor_id saved = {fint(entry, "slot", 0), fint(entry, "generation", 0)};
  return resolve_saved(s, saved);
}
static qa_actor_id live_actor(S *s, const R *entry) {
  qa_actor_id actor = ref_actor(s, entry);
  int64_t slot;
  if (!source_slot(s, actor, &slot)) qa_save_reader_fail(entry, "missing live QC actor");
  return actor;
}
static int64_t client_slot_of(S *s, const R *entry) {
  double slot = qa_save_reader_integer_min(entry, 1);
  if (slot > (double)s->reserved_client_slots) qa_save_reader_fail(entry, "invalid reserved client slot");
  return (int64_t)slot;
}
static void check_declaration(S *s, const R *reader, const char *key, const char *message) {
  R f = fld(reader, key);
  qa_save_value *current = is(key, "pickupCallers") ? pickup_callers_value(s) : declaration_value(s, key);
  const qa_save_value *saved = f.value;
  qa_save_value *fallback = NULL;
  if (!saved || saved->kind == QA_SAVE_UNDEFINED) saved = fallback = (is(key, "teams") || is(key, "pickupCallers")) ? arr() : sv_check(qa_save_value_null());
  bool same = sv_equal(saved, current);
  qa_save_value_free(current); qa_save_value_free(fallback);
  if (!same) qa_save_reader_fail(&f, "%s", message);
}
static void read_views(S *s, const R *reader, const qa_netquake_message *messages, size_t count, view_entry **out, size_t *out_count) {
  view_entry *views = xalloc((count ? count : 1) * sizeof *views);
  size_t n = 0;
  if (!qa_save_reader_is_undefined(reader)) {
    size_t list = qa_save_reader_list_count(reader);
    for (size_t i = 0; i < list; i++) {
      R entry = itm(reader, i);
      double index = fint(&entry, "index", 0);
      if (index >= (double)count || !is(messages[(size_t)index].kind, "set-view") || view_lookup(views, n, (int64_t)index, NULL)) {
        free(views); qa_save_reader_fail(&entry, "Invalid retained QC camera message");
      }
      R actor = fld(&entry, "actor");
      view_entry v = {(int64_t)index, !qa_save_reader_is_null(&actor), no_actor()};
      if (v.has_actor) v.actor = resolve_saved(s, read_saved(&actor));
      views[n++] = v;
    }
  }
  for (size_t i = 0; i < count; i++)
    if (is(messages[i].kind, "set-view") && !view_lookup(views, n, (int64_t)i, NULL)) {
      free(views); qa_save_reader_fail(reader, "Legacy QC camera message has no captured actor identity");
    }
  *out = views; *out_count = n;
}
static qa_netquake_message *restore_nq(S *s, const R *reader, size_t *out_count) {
  size_t length = 0;
  const uint8_t *bytes = qa_save_reader_bytes(reader, &length);
  return qa_compat_qc_broadcast_restore_netquake_messages(&s->messages, bytes, length, out_count);
}
static void clear_host_state(S *s) {
  s->active_clients.count = 0;
  s->pending_count = 0;
  info_clear(s);
  s->spawn_count = 0; s->identity_count = 0;
  s->spectator_slots.count = 0; s->prepared_clients.count = 0;
  qa_event_ring_reset(&s->frag_records);
  qa_event_ring_for_each_live(&s->routed, free_routed_record, NULL);
  qa_event_ring_reset(&s->routed);
  for (size_t i = 0; i < s->signon_count; i++) qa_quakeworld_message_destroy(&s->signon[i].message);
  s->signon_count = 0;
  for (size_t i = 0; i < s->nq_signon_count; i++) qa_netquake_message_destroy(&s->nq_signon[i]);
  s->nq_signon_count = 0; s->nq_signon_view_count = 0;
  nq_routed_reset(s);
}
static void restore_host(S *s, const qa_save_value *value) {
  R reader;
  if (!qa_save_reader_init(&reader, value, "quakec.source")) qa_range_error("Out of memory");
  const qa_app_sim_quakec_source_restore *restore = s->options.restore;
  if (!restore) qa_save_reader_fail(&reader, "missing restore clients");
  R dialect = fld(&reader, "messageDialect");
  const char *saved_dialect = "known-retail";
  if (!qa_save_reader_is_undefined(&dialect)) {
    static const char *const choices[3] = {"known-retail", "q1-rerelease-private", "quake-1-re-ts-private"};
    saved_dialect = qa_save_reader_choice_string(&dialect, choices, 3);
    if (is(saved_dialect, "quake-1-re-ts-private")) saved_dialect = "q1-rerelease-private";
  }
  if (!is(saved_dialect, s->prepared->message_dialect ? s->prepared->message_dialect : "known-retail")) qa_save_reader_fail(&dialect, "QuakeC message dialect changed");
  check_declaration(s, &reader, "teams", "QuakeC team declarations changed");
  check_declaration(s, &reader, "combat", "QuakeC combat declaration changed");
  check_declaration(s, &reader, "weaponStage", "QuakeC weapon stage declaration changed");
  check_declaration(s, &reader, "pickupCallers", "QuakeC pickup caller declarations changed");
  { R f = fld(&reader, "kind"); (void)qa_save_reader_literal_string(&f, s->binding->kind); }
  { R f = fld(&reader, "maxClients"); (void)qa_save_reader_literal_number(&f, s->options.max_clients); }
  { R f = fld(&reader, "reservedClientSlots"); (void)qa_save_reader_literal_number(&f, (double)s->reserved_client_slots); }
  { R f = fld(&reader, "spawning"); (void)qa_save_reader_literal_boolean(&f, false); }
  s->spawning = false;
  { R f = fld(&reader, "currentTime"); s->current_time = qa_save_reader_finite(&f); }
  if (s->current_time < 0) qa_save_reader_fail(&reader, "negative source frame-entry time");
  { R f = fld(&reader, "changeLevelIssued"); s->change_level_issued = qa_save_reader_boolean(&f); }
  { R f = fld(&reader, "borrowedActors"); qa_compat_qc_borrowed_actors_restore(&s->borrowed, &f); }
  { R f = fld(&reader, "originalSaveExtensionText");
    set_scratch(&s->original_save_extension_text, qa_save_reader_is_undefined(&f) ? "" : qa_save_reader_string(&f)); }
  clear_host_state(s);
  R list = fld(&reader, "activeClients");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i);
    qa_actor_id actor = live_actor(s, &entry);
    if (!qa_app_sim_quakec_source_is_reserved_client(s, actor) || active(s, actor)) qa_save_reader_fail(&reader, "invalid active QC client");
    actor_set_add(&s->active_clients, actor);
  }
  list = fld(&reader, "pendingWeapons");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), actor_reader = fld(&entry, "actor"), weapon_reader = fld(&entry, "weapon");
    qa_actor_id actor = live_actor(s, &actor_reader);
    size_t index = 0;
    const native_weapon *weapon = weapon_of_item(s, qa_namespaced(&weapon_reader), &index);
    if (!weapon || !active(s, actor) || pending_get(s, actor)) qa_save_reader_fail(&entry, "invalid pending weapon");
    R following = fld(&entry, "following");
    pending_set(s, actor, index, qa_save_reader_boolean(&following));
  }
  list = fld(&reader, "userInfo");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), slot_reader = fld(&entry, "slot"), values = fld(&entry, "values");
    int64_t slot = client_slot_of(s, &slot_reader);
    if (info_get(s, slot)) qa_save_reader_fail(&entry, "duplicate userinfo slot");
    size_t count = qa_save_reader_list_count(&values);
    qa_app_sim_cvar_name_value *items = xalloc((count ? count : 1) * sizeof *items);
    for (size_t j = 0; j < count; j++) {
      R kv = itm(&values, j), k = fld(&kv, "key"), v = fld(&kv, "value");
      items[j] = (qa_app_sim_cvar_name_value){qa_save_reader_string(&k), qa_save_reader_string(&v)};
    }
    info_set(s, slot, (qa_app_sim_quakec_info){items, count});
    free(items);
  }
  list = fld(&reader, "spawnParameters");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), slot_reader = fld(&entry, "slot"), values = fld(&entry, "values");
    int64_t slot = client_slot_of(s, &slot_reader);
    size_t count = qa_save_reader_list_count(&values);
    double parameters[16] = {0};
    for (size_t j = 0; j < count; j++) { R v = itm(&values, j); double value = qa_save_reader_number(&v); if (j < 16) parameters[j] = value; }
    if (count != 16 || spawn_get(s, slot)) qa_save_reader_fail(&entry, "invalid spawn parameters");
    spawn_set(s, slot, parameters);
  }
  list = fld(&reader, "clientIdentities");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), slot_reader = fld(&entry, "slot");
    int64_t slot = client_slot_of(s, &slot_reader);
    double client_slot = fint(&entry, "clientSlot", 0);
    const qa_client_id *client = NULL;
    for (size_t j = 0; j < restore->client_count && !client; j++) if ((double)restore->clients[j].slot == client_slot) client = &restore->clients[j];
    if ((double)slot != client_slot + 1 || !client || identity_get(s, slot)) qa_save_reader_fail(&entry, "missing restored QC client identity");
    identity_set(s, slot, *client);
  }
  R spectators = fld(&reader, "spectatorSlots");
  if (!qa_save_reader_is_undefined(&spectators))
    for (size_t i = 0, n = qa_save_reader_list_count(&spectators); i < n; i++) {
      R entry = itm(&spectators, i);
      int64_t slot = client_slot_of(s, &entry);
      if (!s->quakeworld || !identity_get(s, slot) || slot_set_has(&s->spectator_slots, slot)) qa_save_reader_fail(&spectators, "invalid spectator slot");
      slot_set_add(&s->spectator_slots, slot);
    }
  list = fld(&reader, "preparedClients");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i);
    int64_t slot = client_slot_of(s, &entry);
    if (slot_set_has(&s->prepared_clients, slot) || !identity_get(s, slot)) qa_save_reader_fail(&reader, "invalid prepared client phase");
    slot_set_add(&s->prepared_clients, slot);
  }
  if (s->active_clients.count != s->identity_count) qa_save_reader_fail(&reader, "client phase identity mismatch");
  for (size_t i = 0; i < s->active_clients.count; i++) {
    int64_t slot;
    if (!source_slot(s, s->active_clients.items[i], &slot) || !identity_get(s, slot) || (s->quakeworld && !slot_set_has(&s->prepared_clients, slot)))
      qa_save_reader_fail(&reader, "incomplete saved client phase");
  }
  list = fld(&reader, "fragRecords");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), killer = fld(&entry, "killer"), victim = fld(&entry, "victim");
    qa_app_sim_quakec_frag frag = {ref_actor(s, &killer), ref_actor(s, &victim)};
    (void)qa_event_ring_admit(&s->frag_records, &frag);
  }
  qa_compat_qc_actor_resolver resolve = {s, resolver};
  list = fld(&reader, "routed");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), entries = fld(&entry, "entries"), destination = fld(&entry, "destination");
    size_t count = 0;
    qa_compat_qc_routed_message *restored = qa_compat_qc_broadcast_restore_entries(&s->messages, entries.value, resolve, &count);
    qa_app_sim_quakec_routed record = {restored, count, qa_compat_qc_read_destination(&destination, resolve)};
    (void)qa_event_ring_admit(&s->routed, &record);
  }
  {
    R signon = fld(&reader, "signon");
    size_t count = 0;
    qa_compat_qc_routed_message *restored = qa_compat_qc_broadcast_restore_entries(&s->messages, signon.value, resolve, &count);
    s->signon = xgrow(s->signon, &s->signon_capacity, count, sizeof *s->signon);
    memcpy(s->signon, restored, count * sizeof *restored);
    s->signon_count = count;
    free(restored);
  }
  R nq_signon = fld(&reader, "netQuakeSignon");
  if (!qa_save_reader_is_undefined(&nq_signon)) {
    size_t count = 0;
    qa_netquake_message *restored = restore_nq(s, &nq_signon, &count);
    s->nq_signon = xgrow(s->nq_signon, &s->nq_signon_capacity, count, sizeof *s->nq_signon);
    memcpy(s->nq_signon, restored, count * sizeof *restored);
    s->nq_signon_count = count;
    free(restored);
  }
  {
    R views = fld(&reader, "netQuakeSignonViews");
    view_entry *entries; size_t count;
    read_views(s, &views, s->nq_signon, s->nq_signon_count, &entries, &count);
    s->nq_signon_views = xgrow(s->nq_signon_views, &s->nq_signon_view_capacity, count, sizeof *s->nq_signon_views);
    memcpy(s->nq_signon_views, entries, count * sizeof *entries);
    s->nq_signon_view_count = count;
    free(entries);
  }
  R nq_routed_reader = fld(&reader, "netQuakeRouted");
  if (!qa_save_reader_is_undefined(&nq_routed_reader))
    for (size_t i = 0, n = qa_save_reader_list_count(&nq_routed_reader); i < n; i++) {
      R entry = itm(&nq_routed_reader, i), bytes = fld(&entry, "bytes"), destination = fld(&entry, "destination"), views = fld(&entry, "views");
      nq_routed record; memset(&record, 0, sizeof record);
      record.messages = restore_nq(s, &bytes, &record.count);
      size_t length = 0;
      free(qa_compat_qc_broadcast_capture_netquake_messages(&s->messages, record.messages, record.count, &length));
      record.size = length;
      record.destination = qa_compat_qc_read_destination(&destination, resolve);
      read_views(s, &views, record.messages, record.count, &record.views, &record.view_count);
      (void)qa_event_ring_admit(&s->nq_routed, &record);
    }
  models_clear(s);
  list = fld(&reader, "models");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), name_reader = fld(&entry, "name"), bounds = fld(&entry, "bounds");
    const char *name = qa_save_reader_string(&name_reader);
    if (model_get(s, name)) qa_save_reader_fail(&entry, "duplicate model");
    double map_index;
    bool is_map = map_model_index(s, name, &map_index);
    double index = fint(&entry, "index", 1);
    if (is_map && index != map_index) qa_save_reader_fail(&entry, "saved map model index differs from current world");
    R min = fld(&bounds, "min"), max = fld(&bounds, "max");
    R a = fld(&min, "x"), b = fld(&min, "y"), c = fld(&min, "z"), d = fld(&max, "x"), e = fld(&max, "y"), f = fld(&max, "z");
    qa_bounds value = {{qa_save_reader_finite(&a), qa_save_reader_finite(&b), qa_save_reader_finite(&c)},
                       {qa_save_reader_finite(&d), qa_save_reader_finite(&e), qa_save_reader_finite(&f)}};
    model_set(s, name, index, value);
  }
  precached_clear(s);
  list = fld(&reader, "precached");
  for (size_t i = 0, n = qa_save_reader_list_count(&list); i < n; i++) {
    R entry = itm(&list, i), key_reader = fld(&entry, "key");
    const char *key = qa_save_reader_string(&key_reader), *colon = strchr(key, ':');
    const char *name = colon ? colon + 1 : key;
    double map_index = 0;
    bool is_map = starts(key, "model:") && map_model_index(s, name, &map_index);
    const qa_resolved_resource_reference *resource = NULL;
    if (is_map) resource = &s->options.recipe->map.geometry;
    else { const qa_app_sim_quakec_resource *r = qa_app_sim_quakec_resources_get(&s->prepared->resources, name); if (r) resource = &r->resource; }
    double index = fint(&entry, "index", 1);
    if (is_map && index != map_index) qa_save_reader_fail(&entry, "saved inline model index differs from current map");
    R id = fld(&entry, "id"), digest = fld(&entry, "digest");
    if (!colon || colon == key || !resource || !is(resource->id, qa_save_reader_string(&id)) || !is(resource->digest, qa_save_reader_string(&digest))
        || precache_get(s, key))
      qa_save_reader_fail(&entry, "saved precache resource differs from mounted content");
    precache_set(s, key, (qa_compat_qc_precached_resource){index, *resource});
  }
  R local = fld(&reader, "localMessages");
  if (!qa_save_reader_is_undefined(&local)) {
    R started = fld(&local, "started"), baseline = fld(&local, "baseline"), clients = fld(&local, "clients");
    s->local_messages_started = qa_save_reader_boolean(&started);
    size_t baseline_count = 0;
    qa_netquake_message *baseline_messages = retain_owned(s, restore_nq(s, &baseline, &baseline_count), baseline_count);
    size_t client_count = qa_save_reader_list_count(&clients);
    qa_app_sim_quakec_captured_client *captured = xalloc((client_count ? client_count : 1) * sizeof *captured);
    bool any_view = false;
    for (size_t i = 0; i < baseline_count; i++) if (is(baseline_messages[i].kind, "set-view")) any_view = true;
    for (size_t i = 0; i < client_count; i++) {
      R entry = itm(&clients, i), actor = fld(&entry, "actor"), bytes = fld(&entry, "bytes");
      size_t count = 0;
      qa_netquake_message *messages = retain_owned(s, restore_nq(s, &bytes, &count), count);
      captured[i] = (qa_app_sim_quakec_captured_client){ref_actor(s, &actor), messages, count};
      for (size_t j = 0; j < count; j++) if (is(messages[j].kind, "set-view")) any_view = true;
    }
    qa_app_sim_quakec_local_messages_restore(s->local_messages, baseline_messages, baseline_count, captured, client_count);
    free(captured);
    R views = fld(&local, "views");
    if (!qa_save_reader_is_undefined(&views)) {
      R base = fld(&views, "baseline"), view_clients = fld(&views, "clients");
      qa_actor_id base_actor; bool has_base = !qa_save_reader_is_null(&base);
      if (has_base) base_actor = resolve_saved(s, read_saved(&base));
      size_t count = qa_save_reader_list_count(&view_clients);
      qa_app_sim_quakec_view_entry *entries = xalloc((count ? count : 1) * sizeof *entries);
      for (size_t i = 0; i < count; i++) {
        R entry = itm(&view_clients, i), actor = fld(&entry, "actor"), target = fld(&entry, "target");
        entries[i] = (qa_app_sim_quakec_view_entry){resolve_saved(s, read_saved(&actor)), resolve_saved(s, read_saved(&target))};
      }
      qa_app_sim_quakec_local_messages_restore_views(s->local_messages, has_base ? &base_actor : NULL, entries, count);
      free(entries);
    } else if (any_view) qa_save_reader_fail(&views, "Legacy QC camera state has no captured actor identity");
  }
  s->model_count = fint(&reader, "modelCount", 1); s->sound_count = fint(&reader, "soundCount", 1);
  { R f = fld(&reader, "cvars"); qa_cvar_registry_restore_quakec_state(s->cvars, f.value); }
  { R f = fld(&reader, "visibility"); qa_q1_client_visibility_restore(s->clients.visibility, f.value); }
  { R f = fld(&reader, "messages"); qa_compat_qc_broadcast_messages_restore(&s->messages, f.value, resolve); }
  { R f = fld(&reader, "projectiles"); qa_q1_id1_projectile_attacks_restore(s->projectiles, f.value); }
}

/* ---------------- executor host ---------------- */
static qa_module_identity module_of(S *s) {
  const qa_resolved_resource_reference *artifact = artifact_of(&s->prepared->execution);
  return (qa_module_identity){s->prepared->execution.owner.provider, artifact->requested_path, artifact->digest, artifact->digest};
}
static qa_compat_qc_host_saved_state host_checkpoint(void *ctx) {
  S *s = ctx;
  /* Source-registry effects are transfer bookkeeping with no queued consumer (info strings
   * already updated synchronously); a save boundary drains them instead of refusing to save. */
  take_effects(s);
  qa_save_value *tree = host_state(s);
  size_t length = 0;
  uint8_t *bytes = qa_encode_checkpoint_value(tree, &length);
  qa_save_value_free(tree);
  if (!bytes) qa_range_error("Out of memory");
  free(s->host_state_bytes);
  s->host_state_bytes = bytes;
  qa_compat_qc_host_saved_state saved; memset(&saved, 0, sizeof saved);
  saved.state.module = module_of(s); saved.state.format = "quakec:source-v1";
  saved.state.bytes = (qa_byte_view){bytes, length};
  return saved;
}
static void save_value_cleanup(void *arg) { qa_save_value_free(arg); }
static void host_restore(void *ctx, qa_compat_qc_host_saved_state saved) {
  S *s = ctx;
  if (!is(saved.state.format, "quakec:source-v1") || saved.random_count != 0 || saved.callback_count != 0)
    fail("Unsupported QC source host checkpoint");
  qa_save_value *value = qa_decode_checkpoint_value(saved.state.bytes.bytes, saved.state.bytes.length);
  if (!value) qa_range_error("Out of memory");
  qa_cleanup scope; qa_cleanup_push(&scope, save_value_cleanup, value);
  restore_host(s, value);
  qa_cleanup_pop(&scope);
  qa_save_value_free(value);
}
static qa_compat_qc_executor_host checkpoint_host(S *s) { return (qa_compat_qc_executor_host){s, host_checkpoint, host_restore}; }
static void checkpoint_storage_free(S *s) {
  if (!s->has_checkpoint) return;
  qa_compat_qc_machine_snapshot_destroy(&s->checkpoint_snapshot);
  free(s->checkpoint_host_bytes); s->checkpoint_host_bytes = NULL;
  s->has_checkpoint = false;
}
const qa_guest_checkpoint *qa_app_sim_quakec_source_checkpoint(S *s) {
  if (s->spawning || s->has_physics_callback) fail("QC save requires a completed source frame");
  for (size_t i = 0; i < s->identity_count; i++) {
    qa_owned_actor actor;
    if (!slot_actor(s, s->identities[i].slot, &actor) || !active(s, actor.id)) fail("QC save requires pending client handshakes to finish");
  }
  if (s->incoming_count != 0) fail("Cannot save during original incoming QC damage");
  qa_q1_id1_sync_attacks_assert_idle(s->attacks);
  qa_q1_id1_pickup_binding_assert_idle(s->pickups);
  checkpoint_storage_free(s);
  if (!s->checkpoint_storage) s->checkpoint_storage = xalloc(sizeof *s->checkpoint_storage);
  memset(s->checkpoint_storage, 0, sizeof *s->checkpoint_storage);
  if (!qa_compat_qc_capture_checkpoint(&s->machine, module_of(s), checkpoint_host(s), s->checkpoint_storage,
                                       &s->checkpoint_snapshot, &s->checkpoint_host_bytes))
    qa_range_error("Out of memory");
  s->has_checkpoint = true;
  return s->checkpoint_storage;
}
static void restore_constructed(S *s) {
  if (!qa_compat_qc_restore_checkpoint(&s->machine, module_of(s), checkpoint_host(s), s->options.restore->checkpoint))
    qa_range_error("Out of memory");
  for (int64_t slot = 0; slot < qa_compat_qc_entity_memory_get_count(&s->entities); slot++) {
    qa_owned_actor actor, borrowed;
    if (!slot_actor(s, slot, &actor)) {
      if (qa_compat_qc_borrowed_actors_actor(&s->borrowed, slot, &borrowed)) continue;
      if (slot <= s->reserved_client_slots || !slot_free(s, slot)) fail("Saved QC edict has no restored actor");
      continue;
    }
    if (slot > s->reserved_client_slots && slot_free(s, slot)) fail("Saved free QC edict has a live actor");
    (void)qa_compat_qc_world_host_actor(&s->world_host, slot);
    if (active(s, actor.id)) bind_client_inventory(s, actor, slot);
  }
}

/* ---------------- original Quake saves (persistence/q1-source.ts) ---------------- */
static qa_q1_text_record record_of(qa_compat_qc_text_pairs pairs) {
  qa_q1_text_record r = {NULL, pairs.count};
  if (pairs.count) {
    r.pairs = xalloc(pairs.count * sizeof *r.pairs);
    for (size_t i = 0; i < pairs.count; i++) r.pairs[i] = (qa_q1_text_pair){pairs.items[i].key, pairs.items[i].value};
  }
  free(pairs.items);
  return r;
}
static qa_compat_qc_text_pair *pairs_of(const qa_q1_text_record *record) {
  qa_compat_qc_text_pair *pairs = xalloc((record->pair_count ? record->pair_count : 1) * sizeof *pairs);
  for (size_t i = 0; i < record->pair_count; i++) pairs[i] = (qa_compat_qc_text_pair){record->pairs[i].key, record->pairs[i].value};
  return pairs;
}
static void qc_snapshot(void *ctx) {
  qa_compat_qc_machine_snapshot snapshot; memset(&snapshot, 0, sizeof snapshot);
  if (!qa_compat_qc_machine_get_snapshot(&((S *)ctx)->machine, &snapshot)) qa_range_error("Out of memory");
  qa_compat_qc_machine_snapshot_destroy(&snapshot);
}
static size_t qc_entity_count(void *ctx) { return (size_t)qa_compat_qc_entity_memory_get_count(&((S *)ctx)->entities); }
static size_t qc_entity_capacity(void *ctx) { return (size_t)((S *)ctx)->entities.capacity; }
static void qc_set_entity_count(void *ctx, size_t count) { qa_compat_qc_entity_memory_set_count(&((S *)ctx)->entities, (int64_t)count); }
static uint8_t *qc_entity_bytes(void *ctx, size_t slot, size_t *out_length) {
  qa_compat_qc_words *words = at(ctx, (int64_t)slot);
  *out_length = words->byte_length;
  return words->bytes;
}
static qa_q1_text_record qc_save_global_pairs(void *ctx) {
  qa_compat_qc_text_pairs pairs = {NULL, 0};
  if (!qa_compat_qc_save_global_pairs(&((S *)ctx)->machine, &pairs)) qa_range_error("Out of memory");
  return record_of(pairs);
}
static qa_q1_text_record qc_save_entity_pairs(void *ctx, size_t slot, bool is_free) {
  qa_compat_qc_text_pairs pairs = {NULL, 0};
  if (!qa_compat_qc_save_entity_pairs(&((S *)ctx)->machine, (int64_t)slot, is_free, &pairs)) qa_range_error("Out of memory");
  return record_of(pairs);
}
static qa_q1_text_record qc_apply_global_pairs(void *ctx, const qa_q1_text_record *record) {
  qa_compat_qc_text_pair *pairs = pairs_of(record);
  qa_compat_qc_text_pairs unknown = {NULL, 0};
  qa_cleanup scope; qa_cleanup_push(&scope, free, pairs);
  bool ok = qa_compat_qc_apply_global_pairs(&((S *)ctx)->machine, pairs, record->pair_count, &unknown);
  qa_cleanup_pop(&scope);
  free(pairs);
  if (!ok) qa_range_error("Out of memory");
  return record_of(unknown);
}
static qa_qc_entity_parse_result qc_apply_entity_pairs(void *ctx, size_t slot, const qa_q1_text_record *record) {
  qa_compat_qc_text_pair *pairs = pairs_of(record);
  qa_compat_qc_entity_parse_result_compat applied; memset(&applied, 0, sizeof applied);
  qa_cleanup scope; qa_cleanup_push(&scope, free, pairs);
  bool ok = qa_compat_qc_apply_entity_pairs(&((S *)ctx)->machine, (int64_t)slot, pairs, record->pair_count, &applied);
  qa_cleanup_pop(&scope);
  free(pairs);
  if (!ok) qa_range_error("Out of memory");
  return (qa_qc_entity_parse_result){applied.empty, record_of(applied.unknown), record_of(applied.retained)};
}
static void mirror_checkpoint(void *ctx) { (void)qa_app_sim_quakec_source_checkpoint(ctx); }
static double mirror_time(void *ctx) { return ((S *)ctx)->current_time; }
static bool mirror_loading(void *ctx) { return ((S *)ctx)->spawning; }
static const char *mirror_light_style(void *ctx, size_t index) {
  return qa_app_sim_simulation_events_light_style(((S *)ctx)->options.events, (double)index);
}
static void mirror_emit_lightstyle(void *ctx, qa_content_id content, size_t style, const char *pattern) {
  S *s = ctx;
  qa_q1_event event; memset(&event, 0, sizeof event);
  event.kind = "lightstyle"; event.style = (double)style; event.pattern = pattern;
  qa_app_sim_source_event_input source; memset(&source, 0, sizeof source);
  source.kind = "q1"; source.q1 = &event;
  qa_source_time t = {QA_SOURCE_TIME_SECONDS, 0};
  qa_app_sim_simulation_events_emit(s->options.events, content, source, false, t, false, no_actor());
}
static size_t mirror_entity_count(void *ctx) { return qc_entity_count(ctx); }
static void mirror_set_entity_count(void *ctx, size_t count) { qc_set_entity_count(ctx, count); }
static bool mirror_slot_actor(void *ctx, size_t slot, qa_owned_actor *out) { return slot_actor(ctx, (int64_t)slot, out); }
static bool mirror_slot_is_free(void *ctx, size_t slot) { return slot_free(ctx, (int64_t)slot); }
static void mirror_clear_freed(void *ctx, size_t slot, qa_source_time now) {
  S *s = ctx;
  s->slots_options.storage.clear_freed(s->slots_options.storage.ctx, (int64_t)slot, now);
}
static qa_owned_actor mirror_bind_existing(void *ctx, size_t slot, const char *definition) {
  return qa_source_actor_slots_bind_existing(((S *)ctx)->slots, (int64_t)slot, definition);
}
static void mirror_initialize(void *ctx, size_t slot, qa_owned_actor actor) {
  S *s = ctx;
  s->slots_options.storage.initialize(s->slots_options.storage.ctx, (int64_t)slot, actor);
}
static void mirror_unlink(void *ctx, qa_owned_actor actor) { slots_unlink(ctx, actor); }
static void mirror_release(void *ctx, qa_owned_actor actor) { qa_session_actor_registry_release(((S *)ctx)->options.actors, actor); }
static void mirror_link(void *ctx, size_t slot) { qa_compat_qc_world_host_link(&((S *)ctx)->world_host, (int64_t)slot); }
static qa_quakec_source mirror_of(S *s) {
  qa_quakec_source m; memset(&m, 0, sizeof m);
  m.ctx = s;
  m.machine = (qa_qc_machine){s, qc_snapshot, qc_entity_count, qc_entity_capacity, qc_set_entity_count, qc_entity_bytes,
                              qc_save_global_pairs, qc_save_entity_pairs, qc_apply_global_pairs, qc_apply_entity_pairs};
  m.kind = s->binding->kind; m.max_clients = s->options.max_clients; m.mode = s->options.mode; m.skill = s->options.skill;
  m.map_requested_path = map_path(s); m.reserved_client_slots = (double)s->reserved_client_slots; m.owner_content = content_of(s);
  m.checkpoint = mirror_checkpoint; m.time_seconds = mirror_time; m.loading = mirror_loading;
  m.light_style = mirror_light_style; m.emit_lightstyle = mirror_emit_lightstyle;
  m.entity_count = mirror_entity_count; m.set_entity_count = mirror_set_entity_count; m.slot_actor = mirror_slot_actor;
  m.slot_is_free = mirror_slot_is_free; m.clear_freed_slot = mirror_clear_freed; m.bind_existing_slot = mirror_bind_existing;
  m.initialize_slot = mirror_initialize; m.unlink_body = mirror_unlink; m.release_actor = mirror_release; m.link_slot = mirror_link;
  return m;
}
bool qa_app_sim_quakec_source_capture_original_save(S *s, int format_version, const char *game_directories, const char *comment, qa_q1_save_data *out) {
  qa_owned_actor actor;
  spawn_entry *parameters = spawn_get(s, 1);
  if (!slot_actor(s, 1, &actor) || !active(s, actor.id) || !parameters) fail("Original Quake save requires an active source client");
  double values[16]; memcpy(values, parameters->values, sizeof values);
  qa_quakec_source mirror = mirror_of(s);
  return qa_capture_q1_source_save(&mirror, format_version, game_directories, comment, values, 16, s->original_save_extension_text, out);
}
typedef struct finish_ctx { S *s; const qa_q1_save_data *save; } finish_ctx;
static void restore_finish(void *ctx, qa_q1_save_header header) {
  finish_ctx *f = ctx; S *s = f->s;
  s->current_time = header.time;
  double values[16] = {0};
  for (size_t i = 0; i < header.spawn_parameter_count && i < 16; i++) values[i] = header.spawn_parameters[i];
  spawn_set(s, 1, values);
  s->change_level_issued = false; s->pending_count = 0;
  char *text = xalloc(f->save->extension_length + 1);
  if (f->save->extension_length) memcpy(text, f->save->extension_text, f->save->extension_length);
  free(s->original_save_extension_text);
  s->original_save_extension_text = text;
}
bool qa_app_sim_quakec_source_restore_original_save(S *s, const qa_q1_save_data *save, qa_q1_unknown_save_fields *out) {
  if (!s->options.original_save_candidate || s->original_save_restored) fail("Original Quake restore requires a fresh staged candidate");
  qa_owned_actor actor;
  if (!slot_actor(s, 1, &actor) || !active(s, actor.id)) fail("Original Quake restore requires an active source client binding");
  s->original_save_restored = true;
  qa_quakec_source mirror = mirror_of(s);
  finish_ctx f = {s, save};
  return qa_restore_q1_source_save(&mirror, save, (qa_q1_source_finish){&f, restore_finish}, out);
}

/* ---------------- destroy ---------------- */
void qa_app_sim_quakec_source_destroy(S *s) {
  if (!s) return;
  checkpoint_storage_free(s); free(s->checkpoint_storage); free(s->host_state_bytes);
  free(s->pusher_env.candidates);
  if (s->has_machine) qa_compat_qc_machine_destroy(&s->machine);
  qa_compat_qc_registry_destroy(&s->registry);
  free(s->machine_region_list);
  if (s->has_weapon_binding) {
    qa_q1qc_weapon_function_composed_destroy(&s->weapon_functions);
    qa_q1qc_weapon_region_composed_destroy(&s->weapon_regions);
  }
  free(s->spawn_function_list);
  if (s->pickups) {
    qa_q1_id1_pickup_region_composed_destroy(&s->pickup_regions);
    qa_q1_id1_pickup_function_composed_destroy(&s->pickup_functions);
  }
  if (s->projectiles) qa_q1_id1_projectile_composed_destroy(&s->projectile_functions);
  if (s->attacks) qa_q1_id1_sync_composed_destroy(&s->attack_functions);
  if (s->owns_weapon_stage && s->weapon_stage) qa_q1qc_weapon_stage_destroy(s->weapon_stage);
  qa_q1_id1_pickup_binding_destroy(s->pickups);
  qa_q1_id1_damage_binding_destroy(s->damage);
  qa_q1_id1_environment_destroy(s->environment);
  qa_q1_id1_projectile_attacks_destroy(s->projectiles);
  qa_q1_id1_sync_attacks_destroy(s->attacks);
  qa_compat_qc_broadcast_messages_destroy(&s->messages);
  qa_compat_qc_client_host_destroy(&s->clients);
  if (s->owns_cvars && s->cvars) qa_cvar_registry_destroy(s->cvars);
  qa_compat_qc_world_host_destroy(&s->world_host);
  qa_compat_qc_borrowed_actors_destroy(&s->borrowed);
  qa_session_actor_registry_off_release(s->options.actors, s->release_id);
  qa_source_actor_slots_destroy(s->slots);
  qa_compat_qc_entity_memory_destroy(&s->entities);
  drained_routed_free(s); drained_nq_free(s); free(s->drained_frags);
  qa_event_ring_for_each_live(&s->routed, free_routed_record, NULL);
  nq_routed_reset(s);
  qa_event_ring_destroy(&s->frag_records); qa_event_ring_destroy(&s->routed); qa_event_ring_destroy(&s->nq_routed);
  for (size_t i = 0; i < s->signon_count; i++) qa_quakeworld_message_destroy(&s->signon[i].message);
  free(s->signon);
  nq_messages_free(s->nq_signon, s->nq_signon_count);
  free(s->nq_signon_views);
  for (size_t i = 0; i < s->retained_count; i++) nq_messages_free(s->retained[i], s->retained_counts[i]);
  free(s->retained); free(s->retained_counts);
  qa_app_sim_quakec_local_messages_destroy(s->local_messages);
  info_clear(s); free(s->user_info);
  free(s->spawn_parameters); free(s->identities); free(s->networks); free(s->pending); free(s->incoming);
  free(s->active_clients.items); free(s->spectator_slots.items); free(s->prepared_clients.items);
  models_clear(s); free(s->models); precached_clear(s); free(s->precached);
  for (size_t i = 0; i < s->cell_count; i++) free(s->cells[i]);
  free(s->cells);
  for (size_t i = 0; i < s->inventory_cell_count; i++) free(s->inventory_cells[i]);
  free(s->inventory_cells);
  for (size_t i = 0; i < sizeof s->message_texts / sizeof s->message_texts[0]; i++) free(s->message_texts[i]);
  free(s->aim_targets); free(s->classname_scratch); free(s->team_scratch); free(s->death_scratch); free(s->info_scratch);
  free(s->client_scratch); free((void *)s->name_scratch);
  travel_free(s);
  free(s->original_save_extension_text);
  free(s);
}
