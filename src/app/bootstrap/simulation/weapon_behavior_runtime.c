/* Port of src/app/bootstrap/simulation/weapon-behavior-runtime.ts.
 * checkpoint()/checkpointLoading() build the { version, sources,
 * attachments } record as the checkpoint-value tree TS encodes, each
 * source record in its TS literal key order; restore reads it back
 * through the per-kind readers. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/weapon_behavior_runtime.h"
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/app/bootstrap/simulation/rerelease_weapon_checkpoint.h"
#include "qa/app/bootstrap/simulation/native_q2_rerelease_save.h"
#include "qa/compat/q2/rerelease/native_weapon_declaration.h"
#include "qa/persistence/save_image.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#define MAX_SOURCES 16
typedef struct behavior_source {
  const char *kind;
  void *source; /* per-kind source pointer */
} behavior_source;
struct qa_app_sim_simulation_weapon_behaviors {
  qa_weapon_behavior_attachments *attachments;
  behavior_source sources[MAX_SOURCES]; size_t source_count;
  qa_weapon_behavior_definition definitions[MAX_SOURCES]; size_t definition_count;
  qa_app_prepared_weapon_behavior deferred[MAX_SOURCES]; size_t deferred_count;
  bool has_pending_restore; qa_save_value *pending_tree;
  bool ready;
  qa_app_sim_weapon_behavior_runtime_host host;
  struct quakec_random *randoms[MAX_SOURCES]; /* SourceRandom per source (constructed or restored), retained by it */
  struct quakec_binding *bindings[MAX_SOURCES]; /* the QcWeaponBehaviorOptions closures of each QuakeC source */
};
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
/* TS `new SourceRandom(seed)` restored for a QuakeC source, as the RandomSource it retains. */
typedef struct quakec_random { qa_app_sim_source_random *random; double words[31]; } quakec_random;
static void quakec_random_destroy(quakec_random *r) { if (r) { qa_app_sim_source_random_destroy(r->random); free(r); } }
static double quakec_random_next_integer(void *ctx) { quakec_random *r = ctx; return qa_app_sim_source_random_next_integer(r->random); }
static double quakec_random_next_unit(void *ctx) { quakec_random *r = ctx; return qa_app_sim_source_random_next_unit(r->random); }
static qa_random_state quakec_random_checkpoint(void *ctx) {
  quakec_random *r = ctx; qa_app_sim_source_random_state state = qa_app_sim_source_random_checkpoint(r->random);
  qa_random_state out; memset(&out, 0, sizeof out);
  out.kind = QA_RANDOM_STATE_GLIBC_RANDOM;
  for (size_t i = 0; i < 31; i++) r->words[i] = state.words[i];
  out.words.values = r->words; out.words.count = 31; out.front = state.front; out.rear = state.rear; out.draws = (double)state.draws;
  return out;
}
/* The closures TS passes to new QuakeCWeaponBehaviorSource: targets/aim/print
 * forward to the host with the host's own ctx; model reads the entry's
 * resources (TS models = resources with modelBounds, in insertion order). */
typedef struct quakec_binding {
  qa_app_sim_simulation_weapon_behaviors *behaviors;
  const qa_app_sim_quakec_resources *resources;
} quakec_binding;
static const qa_app_sim_qc_weapon_target *quakec_targets(void *ctx, size_t *out_count) {
  quakec_binding *binding = ctx;
  return binding->behaviors->host.targets(binding->behaviors->host.ctx, out_count);
}
static qa_vec3 quakec_aim(void *ctx, qa_actor_id shooter, double speed) {
  quakec_binding *binding = ctx;
  return binding->behaviors->host.aim(binding->behaviors->host.ctx, shooter, speed);
}
static void quakec_print(void *ctx, const qa_actor_id *recipient, const char *text) {
  quakec_binding *binding = ctx;
  binding->behaviors->host.print(binding->behaviors->host.ctx, recipient, text);
}
static bool quakec_model(void *ctx, const char *path, qa_app_sim_qc_weapon_model *out) {
  quakec_binding *binding = ctx;
  if (binding->resources == NULL) return false;
  double index = 0;
  for (size_t i = 0; i < binding->resources->count; i++) {
    const qa_app_sim_quakec_resource *resource = &binding->resources->items[i];
    if (!resource->has_model_bounds) continue;
    index++;
    if (strcmp(resource->name, path) != 0) continue;
    out->index = index;
    out->bounds = resource->model_bounds;
    return true;
  }
  return false;
}
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static qa_weapon_behavior_definition source_definition(behavior_source *s) {
  if (is(s->kind, "quakec")) return qa_app_sim_quakec_weapon_behavior_source_definition(s->source);
  if (is(s->kind, "qvm")) return qa_app_sim_qvm_weapon_behavior_source_definition(s->source);
  return qa_app_sim_rerelease_weapon_behavior_source_definition(s->source);
}
qa_app_sim_simulation_weapon_behaviors *qa_app_sim_simulation_weapon_behaviors_create(
    const qa_app_prepared_weapon_behavior *prepared, size_t prepared_count,
    qa_app_sim_weapon_behavior_runtime_host host) {
  qa_app_sim_simulation_weapon_behaviors *b = calloc(1, sizeof *b);
  if (!b) qa_range_error("Out of memory");
  b->host = host;
  b->attachments = qa_weapon_behavior_attachments_create(host.actors);
  if (!b->attachments) qa_range_error("Out of memory");
  for (size_t i = 0; i < prepared_count; i++) {
    const qa_weapon_behavior_definition *definition = prepared[i].selection.definition;
    for (size_t j = 0; j < b->definition_count; j++)
      if (is(b->definitions[j].role, definition->role))
        qa_range_error("Multiple selected trajectory behaviors for %s", definition->role);
    if (b->definition_count >= MAX_SOURCES) qa_range_error("Out of memory");
    b->definitions[b->definition_count++] = *definition;
    if (!is(prepared[i].kind, "quakec")) {
      if (b->deferred_count < MAX_SOURCES) b->deferred[b->deferred_count++] = prepared[i];
      continue;
    }
    const qa_app_prepared_quakec_weapon_behavior *payload = prepared[i].payload;
    size_t index = b->source_count;
    quakec_random *random = calloc(1, sizeof *random);
    quakec_binding *binding = calloc(1, sizeof *binding);
    if (!random || !binding) qa_range_error("Out of memory");
    random->random = qa_app_sim_source_random_create(host.seed, "classic");
    binding->behaviors = b; binding->resources = payload->resources;
    b->randoms[index] = random; b->bindings[index] = binding;
    qa_app_sim_qc_weapon_behavior_options options; memset(&options, 0, sizeof options);
    options.definition = *definition;
    options.program = (qa_compat_qc_program *)payload->program;
    options.random = (qa_random_source){random, quakec_random_next_integer, quakec_random_next_unit, quakec_random_checkpoint};
    options.scene = host.scene; options.mode = host.mode;
    options.ctx = binding; options.targets = quakec_targets; options.aim = quakec_aim; options.model = quakec_model;
    options.print = quakec_print;
    qa_app_sim_quakec_weapon_behavior_source *source = qa_app_sim_quakec_weapon_behavior_source_create(options);
    if (!source) qa_range_error("Out of memory");
    b->sources[b->source_count++] = (behavior_source){"quakec", source};
    qa_weapon_behavior_attachments_register(b->attachments,
        qa_app_sim_quakec_weapon_behavior_source_as_source(source));
  }
  b->ready = b->deferred_count == 0;
  return b;
}
void qa_app_sim_simulation_weapon_behaviors_destroy(qa_app_sim_simulation_weapon_behaviors *behaviors) {
  if (!behaviors) return;
  qa_weapon_behavior_attachments_destroy(behaviors->attachments);
  qa_save_value_free(behaviors->pending_tree);
  for (size_t i = 0; i < behaviors->source_count; i++)
    if (is(behaviors->sources[i].kind, "quakec")) qa_app_sim_quakec_weapon_behavior_source_destroy(behaviors->sources[i].source);
  for (size_t i = 0; i < MAX_SOURCES; i++) { quakec_random_destroy(behaviors->randoms[i]); free(behaviors->bindings[i]); }
  free(behaviors);
}
static void restore_loading(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader,
    void (*next_frame)(void *ctx), void *frame_ctx);
void qa_app_sim_simulation_weapon_behaviors_initialize_loading(qa_app_sim_simulation_weapon_behaviors *b,
    void (*next_frame)(void *ctx), void *frame_ctx) {
  if (b->ready) return;
  for (size_t i = 0; i < b->deferred_count; i++) {
    if (is(b->deferred[i].kind, "qvm")) {
      qa_app_sim_qvm_weapon_behavior_source *source = b->host.create_qvm(b->host.ctx, b->deferred[i]);
      b->sources[b->source_count++] = (behavior_source){"qvm", source};
      qa_weapon_behavior_attachments_register(b->attachments,
          qa_app_sim_qvm_weapon_behavior_source_as_source(source));
    } else {
      qa_app_sim_rerelease_weapon_behavior_source *source =
          b->host.create_native(b->host.ctx, b->deferred[i], next_frame, frame_ctx);
      b->sources[b->source_count++] = (behavior_source){"rerelease-native", source};
      qa_weapon_behavior_attachments_register(b->attachments,
          qa_app_sim_rerelease_weapon_behavior_source_as_source(source));
    }
  }
  b->ready = true;
  if (b->has_pending_restore) {
    b->has_pending_restore = false;
    qa_save_reader reader; qa_save_reader_init(&reader, b->pending_tree, "weapon-behavior-runtime");
    restore_loading(b, &reader, next_frame, frame_ctx);
    qa_save_reader_destroy(&reader);
    qa_save_value_free(b->pending_tree); b->pending_tree = NULL;
  }
}
qa_weapon_trajectory_update_result qa_app_sim_simulation_weapon_behaviors_launch(
    qa_app_sim_simulation_weapon_behaviors *b, qa_weapon_behavior_launch input) {
  if (!b->ready) qa_range_error("Weapon components require completed asynchronous loading");
  for (size_t i = 0; i < b->definition_count; i++)
    if (is(b->definitions[i].role, input.role))
      return qa_weapon_behavior_attachments_launch(b->attachments, b->definitions[i].id, input);
  qa_weapon_trajectory_update_result none; memset(&none, 0, sizeof none);
  return none;
}
bool qa_app_sim_simulation_weapon_behaviors_controls_trajectory(qa_app_sim_simulation_weapon_behaviors *b, qa_actor_id projectile) {
  return qa_weapon_behavior_attachments_controls_trajectory(b->attachments, projectile);
}
qa_weapon_trajectory_update_result qa_app_sim_simulation_weapon_behaviors_step(
    qa_app_sim_simulation_weapon_behaviors *b, qa_owned_actor projectile, qa_body_state body, double time_seconds) {
  return qa_weapon_behavior_attachments_step(b->attachments, projectile, body, time_seconds);
}
/* ---- checkpoint trees (the TS records encodeCheckpointValue sees) ---- */
static void wb_oom(const void *p) { if (p == NULL) qa_range_error("Out of memory"); }
static qa_save_value *wb_obj(void) { qa_save_value *v = qa_save_value_object(); wb_oom(v); return v; }
static qa_save_value *wb_arr(void) { qa_save_value *v = qa_save_value_array(); wb_oom(v); return v; }
static void wb_set(qa_save_value *o, const char *key, qa_save_value *child) {
  wb_oom(child);
  if (!qa_save_value_set(o, key, child)) { qa_save_value_free(child); wb_oom(NULL); }
}
static void wb_push(qa_save_value *a, qa_save_value *child) {
  wb_oom(child);
  if (!qa_save_value_push(a, child)) { qa_save_value_free(child); wb_oom(NULL); }
}
static qa_save_value *wb_saved(qa_saved_actor_id actor) { return qa_save_actor_value(actor); }
/* WeaponTrajectoryUpdate literal { origin, velocity, angles }. */
static qa_save_value *wb_trajectory(qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles) {
  qa_save_value *o = wb_obj();
  wb_set(o, "origin", qa_save_vector_value(origin));
  wb_set(o, "velocity", qa_save_vector_value(velocity));
  wb_set(o, "angles", qa_save_vector_value(angles));
  return o;
}
/* QcMachine.snapshot() literal. */
static qa_save_value *wb_machine(const qa_compat_qc_machine_snapshot *m) {
  qa_save_value *o = wb_obj(), *profiling = wb_arr();
  wb_set(o, "globals", qa_save_value_bytes(m->globals, m->globals_length));
  wb_set(o, "entities", qa_save_value_bytes(m->entities, m->entities_length));
  wb_set(o, "entityCount", qa_save_value_number((double)m->entity_count));
  wb_set(o, "strings", qa_save_value_bytes(m->strings, m->strings_length));
  wb_set(o, "statement", qa_save_value_number((double)m->statement));
  wb_set(o, "functionIndex", qa_save_value_number((double)m->function_index));
  wb_set(o, "argumentCount", qa_save_value_number((double)m->argument_count));
  for (size_t i = 0; i < m->profiling_count; i++) wb_push(profiling, qa_save_value_number(m->profiling[i]));
  wb_set(o, "profiling", profiling);
  wb_set(o, "traceEnabled", qa_save_value_boolean(m->trace_enabled));
  return o;
}
/* QuakeCWeaponBehaviorSource.checkpoint() literal. */
static qa_save_value *wb_quakec(const qa_app_sim_quakec_weapon_behavior_checkpoint *c) {
  qa_save_value *o = wb_obj(), *free_slots = wb_arr(), *retired = wb_arr(), *bindings = wb_arr();
  wb_set(o, "version", qa_save_value_number(c->version));
  wb_set(o, "definition", qa_save_weapon_behavior_definition_value(&c->definition));
  wb_set(o, "mode", qa_save_value_string(c->mode));
  wb_set(o, "sourceTime", qa_save_value_number(c->source_time));
  wb_set(o, "random", qa_save_random_value(c->random));
  wb_set(o, "machine", wb_machine(&c->machine));
  for (size_t i = 0; i < c->free_count; i++) wb_push(free_slots, qa_save_value_number(c->free_slots[i]));
  wb_set(o, "free", free_slots);
  for (size_t i = 0; i < c->retired_count; i++) wb_push(retired, qa_save_value_number(c->retired[i]));
  wb_set(o, "retired", retired);
  for (size_t i = 0; i < c->binding_count; i++) {
    qa_save_value *entry = wb_obj();
    wb_set(entry, "slot", qa_save_value_number(c->bindings[i].slot));
    wb_set(entry, "actor", wb_saved(c->bindings[i].actor));
    wb_set(entry, "kind", qa_save_value_string(c->bindings[i].kind));
    wb_push(bindings, entry);
  }
  wb_set(o, "bindings", bindings);
  return o;
}
/* QvmWeaponBehaviorSource.checkpoint() literal; bindings are
 * { ...binding, actor } over the { actor, pointer, kind } binding literal. */
static qa_save_value *wb_qvm(const qa_app_sim_qvm_weapon_behavior_checkpoint *c) {
  qa_save_value *o = wb_obj(), *activated = wb_arr(), *bindings = wb_arr(), *retired = wb_arr();
  wb_set(o, "version", qa_save_value_number(c->version));
  wb_set(o, "definition", qa_save_weapon_behavior_definition_value(&c->definition));
  wb_set(o, "module", qa_save_guest_checkpoint_value(&c->module));
  wb_set(o, "profileDigest", qa_save_value_string(c->profile_digest));
  for (size_t i = 0; i < c->activated_count; i++) wb_push(activated, wb_saved(c->activated[i]));
  wb_set(o, "activated", activated);
  for (size_t i = 0; i < c->binding_count; i++) {
    qa_save_value *entry = wb_obj();
    wb_set(entry, "actor", wb_saved(c->bindings[i].actor));
    wb_set(entry, "pointer", qa_save_value_number(c->bindings[i].pointer));
    wb_set(entry, "kind", qa_save_value_string(c->bindings[i].kind));
    wb_push(bindings, entry);
  }
  wb_set(o, "bindings", bindings);
  for (size_t i = 0; i < c->retired_count; i++) {
    const qa_weapon_trajectory_update *t = &c->retired[i].trajectory;
    qa_save_value *entry = wb_obj();
    wb_set(entry, "actor", wb_saved(c->retired[i].actor));
    wb_set(entry, "trajectory", wb_trajectory(t->origin, t->velocity, t->angles));
    wb_push(retired, entry);
  }
  wb_set(o, "retired", retired);
  return o;
}
/* RereleaseWeaponBehaviorSource.checkpoint(nextFrame) literal. */
static qa_save_value *wb_rerelease(const qa_app_sim_rerelease_weapon_behavior_checkpoint *c) {
  qa_save_value *o = wb_obj();
  wb_set(o, "version", qa_save_value_number(c->version));
  char *json = qa_compat_q2_serialize_native_weapon_declaration(&c->declaration);
  wb_oom(json);
  qa_save_value *declaration = qa_parse_json_value((const uint8_t *)json, strlen(json), false);
  free(json);
  wb_set(o, "declaration", declaration);
  wb_set(o, "definition", qa_save_weapon_behavior_definition_value(&c->definition));
  qa_save_value *map = wb_obj();
  wb_set(map, "map", qa_save_value_string(c->map));
  wb_set(map, "entities", qa_save_value_string(c->entities));
  wb_set(map, "spawnPoint", qa_save_value_string(c->spawn_point));
  wb_set(o, "map", map);
  wb_set(o, "time", qa_save_value_number(c->time));
  wb_set(o, "game", qa_app_sim_rerelease_source_save_value(&c->game));
  wb_set(o, "level", qa_app_sim_rerelease_source_save_value(&c->level));
  wb_set(o, "cvars", qa_save_value_bytes(c->cvars, c->cvars_length));
  qa_save_value *configstrings = wb_arr(), *retired = wb_arr(), *bindings = wb_arr();
  for (size_t i = 0; i < c->configstring_count; i++) {
    qa_save_value *entry = wb_obj();
    wb_set(entry, "index", qa_save_value_number(c->configstrings[i].index));
    wb_set(entry, "value", qa_save_value_string(c->configstrings[i].value));
    wb_push(configstrings, entry);
  }
  wb_set(o, "configstrings", configstrings);
  for (size_t i = 0; i < c->retired_count; i++) {
    const qa_app_sim_rerelease_weapon_trajectory *t = &c->retired[i].trajectory;
    qa_save_value *entry = wb_obj();
    wb_set(entry, "actor", wb_saved(c->retired[i].actor));
    wb_set(entry, "trajectory", wb_trajectory(t->origin, t->velocity, t->angles));
    wb_push(retired, entry);
  }
  wb_set(o, "retired", retired);
  for (size_t i = 0; i < c->binding_count; i++) {
    qa_save_value *entry = wb_obj();
    wb_set(entry, "slot", qa_save_value_number(c->bindings[i].slot));
    wb_set(entry, "actor", wb_saved(c->bindings[i].actor));
    wb_set(entry, "kind", qa_save_value_string(c->bindings[i].kind));
    wb_set(entry, "generation", qa_save_value_number(c->bindings[i].generation));
    wb_push(bindings, entry);
  }
  wb_set(o, "bindings", bindings);
  return o;
}
/* WeaponBehaviorAttachments.checkpoint() literal. */
static qa_save_value *wb_attachments(qa_weapon_behavior_attachments *attachments) {
  qa_weapon_behavior_attachment_checkpoint checkpoint = qa_weapon_behavior_attachments_checkpoint(attachments);
  qa_save_value *o = wb_obj(), *list = wb_arr();
  wb_set(o, "version", qa_save_value_number((double)checkpoint.version));
  for (size_t i = 0; i < checkpoint.attachment_count; i++) {
    const qa_weapon_behavior_attachment *entry = &checkpoint.attachments[i];
    qa_save_value *value = wb_obj();
    wb_set(value, "projectile", wb_saved(entry->projectile));
    wb_set(value, "owner", qa_save_value_string(entry->owner));
    wb_set(value, "definition", qa_save_weapon_behavior_definition_value(&entry->definition));
    wb_push(list, value);
  }
  wb_set(o, "attachments", list);
  qa_free_weapon_behavior_attachment_checkpoint(checkpoint);
  return o;
}
/* The source's checkpoint (sources[] entry); next_frame NULL is the
 * synchronous checkpoint(), which never reaches a native source. */
static qa_save_value *wb_source(behavior_source *entry, void (*next_frame)(void *ctx), void *frame_ctx) {
  if (is(entry->kind, "quakec")) {
    qa_app_sim_quakec_weapon_behavior_checkpoint c = qa_app_sim_quakec_weapon_behavior_source_checkpoint(entry->source);
    qa_save_value *value = wb_quakec(&c);
    qa_app_sim_quakec_weapon_behavior_checkpoint_free(&c);
    return value;
  }
  if (is(entry->kind, "qvm")) {
    qa_app_sim_qvm_weapon_behavior_checkpoint c = qa_app_sim_qvm_weapon_behavior_source_checkpoint(entry->source);
    qa_save_value *value = wb_qvm(&c);
    qa_app_sim_qvm_weapon_behavior_checkpoint_free(&c);
    return value;
  }
  qa_app_sim_rerelease_weapon_behavior_checkpoint c =
      qa_app_sim_rerelease_weapon_behavior_source_checkpoint(entry->source, next_frame, frame_ctx);
  qa_save_value *value = wb_rerelease(&c);
  qa_app_sim_rerelease_weapon_behavior_checkpoint_free(&c);
  return value;
}
static qa_app_sim_weapon_behavior_runtime_checkpoint wb_checkpoint(qa_app_sim_simulation_weapon_behaviors *b, bool loading,
    void (*next_frame)(void *ctx), void *frame_ctx) {
  qa_app_sim_weapon_behavior_runtime_checkpoint out; memset(&out, 0, sizeof out);
  out.version = 1;
  out.value = wb_obj();
  qa_save_value *sources = wb_arr();
  for (size_t i = 0; i < b->source_count; i++) {
    if (!loading && is(b->sources[i].kind, "rerelease-native")) continue;
    wb_push(sources, wb_source(&b->sources[i], next_frame, frame_ctx));
  }
  wb_set(out.value, "version", qa_save_value_number(1));
  wb_set(out.value, "sources", sources);
  wb_set(out.value, "attachments", wb_attachments(b->attachments));
  return out;
}
qa_app_sim_weapon_behavior_runtime_checkpoint qa_app_sim_simulation_weapon_behaviors_checkpoint(qa_app_sim_simulation_weapon_behaviors *b) {
  bool has_native = false;
  for (size_t i = 0; i < b->deferred_count; i++) if (is(b->deferred[i].kind, "rerelease-native")) has_native = true;
  if (!b->ready || has_native) qa_range_error("Native weapon components require asynchronous checkpoint capture");
  return wb_checkpoint(b, false, NULL, NULL);
}
qa_app_sim_weapon_behavior_runtime_checkpoint qa_app_sim_simulation_weapon_behaviors_checkpoint_loading(
    qa_app_sim_simulation_weapon_behaviors *b, void (*next_frame)(void *ctx), void *frame_ctx) {
  if (!b->ready) qa_range_error("Weapon components have not finished loading");
  return wb_checkpoint(b, true, next_frame, frame_ctx);
}
typedef struct read_entry {
  const char *kind; behavior_source *source;
  qa_app_sim_quakec_weapon_behavior_checkpoint quakec;
  qa_app_sim_qvm_weapon_behavior_checkpoint qvm;
  qa_app_sim_rerelease_weapon_behavior_checkpoint rerelease;
} read_entry;
static size_t read_sources(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader, read_entry *out) {
  qa_save_reader f = fld(reader, "version"); qa_save_reader_literal_number(&f, 1); qa_save_reader_destroy(&f);
  qa_save_reader sources = fld(reader, "sources");
  size_t count = qa_save_reader_list_count(&sources);
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    qa_save_reader value, df, idf;
    qa_save_reader_item(&sources, i, &value);
    df = fld(&value, "definition"); idf = fld(&df, "id");
    const char *id = qa_save_reader_string(&idf);
    qa_save_reader_destroy(&idf); qa_save_reader_destroy(&df);
    behavior_source *entry = NULL;
    for (size_t s = 0; s < b->source_count; s++)
      if (is(source_definition(&b->sources[s]).id, id)) entry = &b->sources[s];
    /* definitions map when source struct definition is not directly readable */
    if (entry == NULL) {
      for (size_t s = 0; s < b->source_count; s++)
        if (s < b->definition_count && is(b->definitions[s].id, id)) entry = &b->sources[s];
    }
    bool dup = false;
    for (size_t s = 0; s < n; s++) if (out[s].source == entry && entry) dup = true;
    if (entry == NULL || dup) qa_save_reader_fail(&value, "Saved weapon behavior differs from selection");
    read_entry *e = &out[n++]; memset(e, 0, sizeof *e);
    e->kind = entry->kind; e->source = entry;
    qa_weapon_behavior_definition def = source_definition(entry);
    if (def.id == NULL) { for (size_t s = 0; s < b->definition_count; s++) if (is(b->definitions[s].id, id)) def = b->definitions[s]; }
    if (is(entry->kind, "quakec")) e->quakec = qa_app_sim_read_quakec_weapon_behavior_checkpoint(&value, def);
    else if (is(entry->kind, "qvm")) e->qvm = qa_app_sim_read_qvm_weapon_behavior_checkpoint(&value, def);
    else {
      qa_native_weapon_behavior_declaration decl =
          qa_app_sim_rerelease_weapon_behavior_source_declaration(entry->source);
      e->rerelease = qa_app_sim_read_rerelease_weapon_behavior_checkpoint(&value, def, decl, b->host.profiles);
    }
    qa_save_reader_destroy(&value);
  }
  qa_save_reader_destroy(&sources);
  if (n != b->source_count) qa_save_reader_fail(reader, "Saved weapon behavior source is missing");
  return n;
}
static qa_actor_id ref_saved(void *ctx, qa_saved_actor_id saved) {
  qa_app_sim_simulation_weapon_behaviors *b = ctx;
  return qa_session_actor_registry_reference_saved(b->host.actors, saved, "checkpoint");
}
static void restore_quakec(qa_app_sim_simulation_weapon_behaviors *b, behavior_source *entry,
    qa_app_sim_quakec_weapon_behavior_checkpoint checkpoint, const qa_save_reader *reader) {
  if (checkpoint.random.kind != QA_RANDOM_STATE_GLIBC_RANDOM)
    qa_save_reader_fail(reader, "QuakeC trajectory requires its source random stream");
  quakec_random *random = calloc(1, sizeof *random);
  if (!random) qa_range_error("Out of memory");
  random->random = qa_app_sim_source_random_create(b->host.seed, "classic");
  qa_app_sim_source_random_state state; memset(&state, 0, sizeof state);
  state.kind = "glibc-random";
  for (size_t i = 0; i < checkpoint.random.words.count && i < 31; i++) state.words[i] = qa_js_to_int32(checkpoint.random.words.values[i]);
  state.front = (int)checkpoint.random.front; state.rear = (int)checkpoint.random.rear; state.draws = (int64_t)checkpoint.random.draws;
  qa_app_sim_source_random_restore(random->random, state);
  qa_random_source source = {random, quakec_random_next_integer, quakec_random_next_unit, quakec_random_checkpoint};
  qa_app_sim_quakec_weapon_behavior_source_restore(entry->source, &checkpoint, ref_saved, b, source);
  size_t index = (size_t)(entry - b->sources);
  quakec_random_destroy(b->randoms[index]); b->randoms[index] = random;
  qa_app_sim_quakec_weapon_behavior_checkpoint_free(&checkpoint);
}
static void restore_loading(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader,
    void (*next_frame)(void *ctx), void *frame_ctx) {
  read_entry entries[MAX_SOURCES];
  size_t n = read_sources(b, reader, entries);
  for (size_t i = 0; i < n; i++) {
    if (is(entries[i].kind, "quakec")) restore_quakec(b, entries[i].source, entries[i].quakec, reader);
    else if (is(entries[i].kind, "qvm"))
      { qa_app_sim_qvm_weapon_behavior_source_restore(entries[i].source->source, &entries[i].qvm, ref_saved, b); qa_app_sim_qvm_weapon_behavior_checkpoint_free(&entries[i].qvm); }
    else qa_app_sim_rerelease_weapon_behavior_source_restore(entries[i].source->source, entries[i].rerelease, next_frame, frame_ctx);
  }
  qa_save_reader af = fld(reader, "attachments");
  qa_weapon_behavior_attachment_checkpoint attachments =
      qa_read_weapon_behavior_attachment_checkpoint(&af, b->definitions, b->definition_count);
  qa_save_reader_destroy(&af);
  qa_weapon_behavior_attachments_restore(b->attachments, attachments);
}
void qa_app_sim_simulation_weapon_behaviors_restore(qa_app_sim_simulation_weapon_behaviors *b, const qa_save_reader *reader) {
  if (qa_save_reader_is_undefined(reader) && b->definition_count == 0) return;
  if (!b->ready) {
    if (b->has_pending_restore) qa_range_error("Weapon component restore is already queued");
    /* TS retains the reader; copy its tree for the deferred restore. */
    size_t length = 0;
    uint8_t *bytes = qa_encode_checkpoint_value(reader->value, &length);
    b->pending_tree = qa_decode_checkpoint_value(bytes, length);
    free(bytes);
    b->has_pending_restore = true;
    return;
  }
  read_entry entries[MAX_SOURCES];
  size_t n = read_sources(b, reader, entries);
  for (size_t i = 0; i < n; i++) {
    if (is(entries[i].kind, "rerelease-native")) qa_range_error("Native weapon components require asynchronous restoration");
    if (is(entries[i].kind, "quakec")) restore_quakec(b, entries[i].source, entries[i].quakec, reader);
    else { qa_app_sim_qvm_weapon_behavior_source_restore(entries[i].source->source, &entries[i].qvm, ref_saved, b); qa_app_sim_qvm_weapon_behavior_checkpoint_free(&entries[i].qvm); }
  }
  qa_save_reader af = fld(reader, "attachments");
  qa_weapon_behavior_attachment_checkpoint attachments =
      qa_read_weapon_behavior_attachment_checkpoint(&af, b->definitions, b->definition_count);
  qa_save_reader_destroy(&af);
  qa_weapon_behavior_attachments_restore(b->attachments, attachments);
}
void qa_app_sim_simulation_weapon_behaviors_close(qa_app_sim_simulation_weapon_behaviors *b) {
  qa_weapon_behavior_attachments_close(b->attachments);
  for (size_t i = 0; i < b->source_count; i++) {
    if (is(b->sources[i].kind, "qvm")) qa_app_sim_qvm_weapon_behavior_source_close(b->sources[i].source);
    else if (is(b->sources[i].kind, "rerelease-native")) qa_app_sim_rerelease_weapon_behavior_source_close(b->sources[i].source);
  }
  b->source_count = 0; b->definition_count = 0; b->has_pending_restore = false;
}
