/* Port of src/app/bootstrap/simulation/quakec-weapon-behavior.ts. GPL-2.0-or-later.
 * TS Map/Set iteration order is insertion order; the ordered arrays below keep
 * it (allocation takes the oldest free slot, checkpoints list in that order). */
#include "qa/app/bootstrap/simulation/quakec_weapon_behavior.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/qc/builtins.h"
#include "qa/compat/qc/memory.h"
#include "qa/compat/qc/profile.h"
#include "qa/compat/qc/weapon_behavior_profile.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#include "qa/world/gameplay/weapon_behaviors.h"

static noreturn void fail_error(const char *format, ...) {
  char message[QA_CAUGHT_MESSAGE_CAPACITY];
  va_list ap; va_start(ap, format); vsnprintf(message, sizeof message, format, ap); va_end(ap);
  qa_raise("Error", 0, 0, "%s", message);
}
static void *checked(void *pointer) { if (!pointer) qa_range_error("Out of memory"); return pointer; }
static bool is(const char *a, const char *b) { return a != NULL && b != NULL && strcmp(a, b) == 0; }

/* ---- insertion-ordered collections ---- */
typedef struct slot_set { int64_t *values; size_t count, capacity; } slot_set;
static bool set_has(const slot_set *s, int64_t v) { for (size_t i = 0; i < s->count; i++) if (s->values[i] == v) return true; return false; }
static void set_add(slot_set *s, int64_t v) {
  if (set_has(s, v)) return;
  if (s->count == s->capacity) { s->capacity = s->capacity ? s->capacity * 2 : 8; s->values = checked(realloc(s->values, s->capacity * sizeof *s->values)); }
  s->values[s->count++] = v;
}
static void set_delete(slot_set *s, int64_t v) {
  for (size_t i = 0; i < s->count; i++) if (s->values[i] == v) { memmove(&s->values[i], &s->values[i + 1], (s->count - i - 1) * sizeof *s->values); s->count--; return; }
}
typedef struct actor_slot { qa_actor_id actor; int64_t slot; } actor_slot;
typedef struct actor_slot_map { actor_slot *entries; size_t count, capacity; } actor_slot_map;
static actor_slot *by_actor(actor_slot_map *m, qa_actor_id actor) { for (size_t i = 0; i < m->count; i++) if (qa_same_actor(m->entries[i].actor, actor)) return &m->entries[i]; return NULL; }
static actor_slot *by_slot(actor_slot_map *m, int64_t slot) { for (size_t i = 0; i < m->count; i++) if (m->entries[i].slot == slot) return &m->entries[i]; return NULL; }
static void map_append(actor_slot_map *m, qa_actor_id actor, int64_t slot) {
  if (m->count == m->capacity) { m->capacity = m->capacity ? m->capacity * 2 : 8; m->entries = checked(realloc(m->entries, m->capacity * sizeof *m->entries)); }
  m->entries[m->count++] = (actor_slot){actor, slot};
}
static void map_remove_at(actor_slot_map *m, actor_slot *entry) {
  size_t i = (size_t)(entry - m->entries);
  memmove(&m->entries[i], &m->entries[i + 1], (m->count - i - 1) * sizeof *m->entries); m->count--;
}
/* slots: Map<ActorId, number> keyed by actor. */
static void slots_set(actor_slot_map *m, qa_actor_id actor, int64_t slot) { actor_slot *e = by_actor(m, actor); if (e) e->slot = slot; else map_append(m, actor, slot); }
/* actors: Map<number, ActorId> keyed by slot. */
static void actors_set(actor_slot_map *m, int64_t slot, qa_actor_id actor) { actor_slot *e = by_slot(m, slot); if (e) e->actor = actor; else map_append(m, actor, slot); }
static void actors_delete(actor_slot_map *m, int64_t slot) { actor_slot *e = by_slot(m, slot); if (e) map_remove_at(m, e); }

typedef struct qc_instance {
  qa_app_sim_quakec_weapon_behavior_source *source;
  int64_t slot; bool closed;
  struct qc_instance *next;
} qc_instance;

#define HOST_BUILTIN_COUNT 13
struct qa_app_sim_quakec_weapon_behavior_source {
  qa_weapon_behavior_definition definition;
  qa_app_sim_qc_weapon_behavior_options options;
  qa_compat_qc_entity_memory entities;
  qa_compat_qc_builtin_registry registry;
  qa_compat_qc_machine machine;
  bool has_entities, has_registry, has_machine;
  qa_compat_qc_host_binding host[HOST_BUILTIN_COUNT];
  qa_random_source builtin_random; /* forwards to `random`, which restore replaces */
  actor_slot_map slots, actors;
  slot_set free, projectiles, retired, bound;
  double source_time;
  qa_random_source random;
  bool spawning; int64_t *spawned; size_t spawned_count, spawned_capacity;
  bool has_launching; qa_weapon_behavior_launch launching;
  qc_instance *instances;
};

static int64_t field(qa_app_sim_quakec_weapon_behavior_source *s, const char *name) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(s->options.program, name);
  if (d == NULL) fail_error("Behavior source lacks entity field %s", name);
  return d->offset;
}
static void global(qa_app_sim_quakec_weapon_behavior_source *s, const char *name, double value) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_global_named(s->options.program, name);
  if (d != NULL) qa_compat_qc_words_set_float(&s->machine.globals, d->offset, value);
}
static int64_t allocate(qa_app_sim_quakec_weapon_behavior_source *s) {
  if (s->free.count > 0) {
    int64_t slot = s->free.values[0]; set_delete(&s->free, slot);
    qa_compat_qc_words *fields = qa_compat_qc_entity_memory_at(&s->entities, slot);
    memset(fields->bytes, 0, fields->byte_length);
    return slot;
  }
  int64_t slot = s->entities.count; qa_compat_qc_entity_memory_set_count(&s->entities, slot + 1); return slot;
}
static int64_t reference(qa_app_sim_quakec_weapon_behavior_source *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->projectiles.count; i++) {
    actor_slot *bound = by_slot(&s->actors, s->projectiles.values[i]);
    if (bound && qa_same_actor(bound->actor, actor)) return qa_compat_qc_entity_memory_reference(&s->entities, s->projectiles.values[i]);
  }
  actor_slot *known = by_actor(&s->slots, actor);
  int64_t slot;
  if (known == NULL) { slot = allocate(s); slots_set(&s->slots, actor, slot); actors_set(&s->actors, slot, actor); }
  else slot = known->slot;
  return qa_compat_qc_entity_memory_reference(&s->entities, slot);
}
static void project_body(qa_app_sim_quakec_weapon_behavior_source *s, int64_t slot, qa_body_state body) {
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_at(&s->entities, slot);
  qa_compat_qc_words_set_vector(fields, field(s, "origin"), body.origin); qa_compat_qc_words_set_vector(fields, field(s, "velocity"), body.velocity);
  qa_compat_qc_words_set_vector(fields, field(s, "angles"), body.angles);
  qa_compat_qc_words_set_vector(fields, field(s, "mins"), body.bounds.min); qa_compat_qc_words_set_vector(fields, field(s, "maxs"), body.bounds.max);
}
static void refresh_targets(qa_app_sim_quakec_weapon_behavior_source *s, const qa_actor_id *excluded) {
  size_t count = 0;
  const qa_app_sim_qc_weapon_target *targets = s->options.targets(s->options.ctx, &count);
  qa_actor_id *live = count ? checked(malloc(count * sizeof *live)) : NULL; size_t live_count = 0;
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, live);
  for (size_t i = 0; i < count; i++) {
    const qa_app_sim_qc_weapon_target *target = &targets[i];
    if (excluded && qa_same_actor(target->actor, *excluded)) continue;
    live[live_count++] = target->actor;
    int64_t slot = qa_compat_qc_entity_memory_slot(&s->entities, reference(s, target->actor));
    qa_compat_qc_words *fields = qa_compat_qc_entity_memory_at(&s->entities, slot);
    if (set_has(&s->projectiles, slot)) continue;
    project_body(s, slot, target->body);
    qa_compat_qc_words_set_float(fields, field(s, "health"), target->health);
    qa_compat_qc_words_set_float(fields, field(s, "solid"), target->solid ? 2 : 0);
    int64_t classname = qa_compat_qc_strings_intern(&s->machine.strings, target->classname);
    qa_compat_qc_words_set_int(fields, field(s, "classname"), (double)classname);
    int64_t netname = qa_compat_qc_strings_intern(&s->machine.strings, target->name);
    qa_compat_qc_words_set_int(fields, field(s, "netname"), (double)netname);
  }
  for (size_t i = 0; i < s->slots.count;) {
    qa_actor_id actor = s->slots.entries[i].actor; int64_t slot = s->slots.entries[i].slot;
    bool alive = false;
    for (size_t l = 0; l < live_count && !alive; l++) alive = qa_same_actor(live[l], actor);
    if (alive) { i++; continue; }
    qa_compat_qc_words_set_float(qa_compat_qc_entity_memory_at(&s->entities, slot), field(s, "health"), 0);
    qa_compat_qc_words_set_float(qa_compat_qc_entity_memory_at(&s->entities, slot), field(s, "solid"), 0);
    /* A target that left the world returns its slot, or long maps exhaust the 8192-entity table. */
    if ((excluded && qa_same_actor(actor, *excluded)) || set_has(&s->projectiles, slot) || set_has(&s->bound, slot)) { i++; continue; }
    map_remove_at(&s->slots, &s->slots.entries[i]); actors_delete(&s->actors, slot); set_add(&s->free, slot);
  }
  qa_cleanup_pop(&cleanup); free(live);
}

typedef struct execute_call { qa_compat_qc_machine *vm; int64_t function_index; } execute_call;
static void run_execute(void *ctx) { execute_call *call = ctx; qa_compat_qc_machine_execute(call->vm, call->function_index, 0); }
static void invoke(qa_app_sim_quakec_weapon_behavior_source *s, int64_t function_index, int64_t slot, double time) {
  qa_compat_qc_machine *vm = &s->machine;
  int64_t self = qa_compat_qc_machine_global_offset(vm, "self");
  int64_t other = qa_compat_qc_machine_global_offset(vm, "other");
  int32_t prior_self = qa_compat_qc_words_int(&vm->globals, self);
  int32_t prior_other = qa_compat_qc_words_int(&vm->globals, other);
  qa_compat_qc_words_set_int(&vm->globals, self, (double)qa_compat_qc_entity_memory_reference(&s->entities, slot));
  qa_compat_qc_words_set_int(&vm->globals, other, 0); global(s, "time", time);
  execute_call call = {vm, function_index}; qa_caught_error caught;
  bool ok = qa_try(run_execute, &call, &caught);
  qa_compat_qc_words_set_int(&vm->globals, self, prior_self); qa_compat_qc_words_set_int(&vm->globals, other, prior_other);
  if (!ok) qa_rethrow(&caught);
}

static qa_weapon_trajectory_update fields_trajectory(qa_app_sim_quakec_weapon_behavior_source *s, qa_compat_qc_words *fields) {
  qa_weapon_trajectory_update out;
  out.origin = qa_compat_qc_words_vector(fields, field(s, "origin"));
  out.velocity = qa_compat_qc_words_vector(fields, field(s, "velocity"));
  out.angles = qa_compat_qc_words_vector(fields, field(s, "angles"));
  return out;
}
static qa_weapon_trajectory_update_result instance_step(void *ctx, qa_body_state body, double time) {
  qc_instance *instance = ctx; qa_app_sim_quakec_weapon_behavior_source *s = instance->source; int64_t slot = instance->slot;
  qa_weapon_trajectory_update_result none; memset(&none, 0, sizeof none);
  if (instance->closed || set_has(&s->retired, slot)) return none;
  s->source_time = time;
  refresh_targets(s, NULL); project_body(s, slot, body);
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_at(&s->entities, slot);
  double next = qa_compat_qc_words_float(fields, field(s, "nextthink"));
  if (next <= 0 || next > time) return none;
  int32_t callback = qa_compat_qc_words_int(fields, field(s, "think")); if (callback == 0) return none;
  qa_compat_qc_words_set_float(fields, field(s, "nextthink"), 0); invoke(s, callback, slot, time);
  if (set_has(&s->retired, slot)) return none;
  qa_weapon_trajectory_update_result out; out.has_value = true; out.value = fields_trajectory(s, fields);
  return out;
}
static void instance_close(void *ctx) {
  qc_instance *instance = ctx; qa_app_sim_quakec_weapon_behavior_source *s = instance->source; int64_t slot = instance->slot;
  if (instance->closed) return;
  instance->closed = true; set_delete(&s->bound, slot); set_delete(&s->retired, slot); set_add(&s->free, slot);
  actors_delete(&s->actors, slot); set_delete(&s->projectiles, slot);
}
static qa_weapon_behavior_instance make_instance(qa_app_sim_quakec_weapon_behavior_source *s, int64_t slot) {
  if (set_has(&s->bound, slot)) fail_error("QuakeC projectile behavior already has an attachment");
  set_add(&s->bound, slot);
  qc_instance *instance = checked(calloc(1, sizeof *instance));
  instance->source = s; instance->slot = slot; instance->next = s->instances; s->instances = instance;
  qa_weapon_behavior_instance out; memset(&out, 0, sizeof out);
  out.ctx = instance; out.definition = s->definition;
  out.initial = fields_trajectory(s, qa_compat_qc_entity_memory_at(&s->entities, slot));
  out.step = instance_step; out.close = instance_close;
  return out;
}

typedef struct attach_call {
  qa_app_sim_quakec_weapon_behavior_source *s;
  bool has_activate; int64_t activate, fire, shooter; double time;
} attach_call;
static void run_attach(void *ctx) {
  attach_call *call = ctx;
  if (call->has_activate) invoke(call->s, call->activate, call->shooter, call->time);
  invoke(call->s, call->fire, call->shooter, call->time);
}
static void release_spawned(qa_app_sim_quakec_weapon_behavior_source *s) {
  for (size_t i = 0; i < s->spawned_count; i++) { set_delete(&s->retired, s->spawned[i]); set_add(&s->free, s->spawned[i]); }
}
qa_weapon_behavior_instance_result qa_app_sim_quakec_weapon_behavior_source_attach(
    qa_app_sim_quakec_weapon_behavior_source *s, qa_weapon_behavior_launch launch) {
  if (!is(launch.role, s->definition.role)) fail_error("Source behavior projectile role differs");
  if (s->has_launching) fail_error("Weapon behavior launch is already active");
  s->source_time = launch.time_seconds;
  refresh_targets(s, &launch.projectile.id);
  int64_t shooter = qa_compat_qc_entity_memory_slot(&s->entities, reference(s, launch.shooter));
  qa_compat_qc_words *source = qa_compat_qc_entity_memory_at(&s->entities, shooter);
  int64_t v_angle = field(s, "v_angle");
  qa_compat_qc_words_set_vector(source, v_angle, qa_compat_qc_words_vector(source, field(s, "angles")));
  qa_weapon_behavior_callback fire = s->definition.fire, activate = s->definition.activate;
  if (fire.kind != QA_WEAPON_CALLBACK_QUAKEC || (s->definition.has_activate && activate.kind != QA_WEAPON_CALLBACK_QUAKEC))
    fail_error("Behavior callback is not QuakeC");
  s->has_launching = true; s->launching = launch; s->spawning = true; s->spawned_count = 0;
  attach_call call = {s, s->definition.has_activate, (int64_t)activate.function_index, (int64_t)fire.function_index, shooter, launch.time_seconds};
  qa_caught_error caught;
  bool ok = qa_try(run_attach, &call, &caught);
  if (!ok) release_spawned(s);
  s->has_launching = false; s->spawning = false;
  if (!ok) qa_rethrow(&caught);
  qa_weapon_behavior_instance_result out; memset(&out, 0, sizeof out);
  if (s->spawned_count == 0) return out; /* The source activation or weapon gate declined the shot. */
  if (s->spawned_count != 1) { release_spawned(s); fail_error("Behavior source produced multiple actors; a declared multi-projectile composition is required"); }
  int64_t slot = s->spawned[0];
  actors_set(&s->actors, slot, launch.projectile.id); set_add(&s->projectiles, slot);
  out.has_value = true; out.value = make_instance(s, slot);
  return out;
}
qa_weapon_behavior_instance qa_app_sim_quakec_weapon_behavior_source_resume(
    qa_app_sim_quakec_weapon_behavior_source *s, qa_actor_id projectile) {
  for (size_t i = 0; i < s->projectiles.count; i++) {
    actor_slot *bound = by_slot(&s->actors, s->projectiles.values[i]);
    if (bound && qa_same_actor(bound->actor, projectile)) return make_instance(s, s->projectiles.values[i]);
  }
  fail_error("Saved projectile has no restored QuakeC behavior");
}

/* ---- host builtins ---- */
static qa_app_sim_quakec_weapon_behavior_source *host_source(void *ctx) { return ctx; }
static void b_spawn(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  if (!s->spawning) qa_compat_qc_machine_fail(vm, "Trajectory callback spawned an undeclared additional actor");
  int64_t slot = allocate(s);
  if (s->spawned_count == s->spawned_capacity) { s->spawned_capacity = s->spawned_capacity ? s->spawned_capacity * 2 : 4; s->spawned = checked(realloc(s->spawned, s->spawned_capacity * sizeof *s->spawned)); }
  s->spawned[s->spawned_count++] = slot;
  qa_compat_qc_machine_return_int(vm, (double)qa_compat_qc_entity_memory_reference(&s->entities, slot));
}
static void b_remove(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  int64_t slot = qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0));
  bool spawned = false;
  if (s->spawning) for (size_t i = 0; i < s->spawned_count; i++) if (s->spawned[i] == slot) spawned = true;
  if (!set_has(&s->projectiles, slot) && !spawned) qa_compat_qc_machine_fail(vm, "Trajectory behavior cannot remove another owner\xe2\x80\x99s actor");
  set_add(&s->retired, slot);
}
static void b_setorigin(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_from_reference(&s->entities, qa_compat_qc_machine_arg_int(vm, 0));
  qa_compat_qc_words_set_vector(fields, field(s, "origin"), qa_compat_qc_machine_arg_vector(vm, 1));
}
static void b_setsize(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_from_reference(&s->entities, qa_compat_qc_machine_arg_int(vm, 0));
  qa_compat_qc_words_set_vector(fields, field(s, "mins"), qa_compat_qc_machine_arg_vector(vm, 1));
  qa_compat_qc_words_set_vector(fields, field(s, "maxs"), qa_compat_qc_machine_arg_vector(vm, 2));
}
static void b_setmodel(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  qa_app_sim_qc_weapon_model model;
  if (!s->options.model(s->options.ctx, qa_compat_qc_machine_arg_string(vm, 1), &model))
    qa_compat_qc_machine_fail(vm, "No source model: %s", qa_compat_qc_machine_arg_string(vm, 1));
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_from_reference(&s->entities, qa_compat_qc_machine_arg_int(vm, 0));
  qa_compat_qc_words_set_int(fields, field(s, "model"), qa_compat_qc_machine_arg_int(vm, 1));
  qa_compat_qc_words_set_float(fields, field(s, "modelindex"), model.index);
  qa_compat_qc_words_set_vector(fields, field(s, "mins"), model.bounds.min);
  qa_compat_qc_words_set_vector(fields, field(s, "maxs"), model.bounds.max);
}
/* Presentation belongs to the selected launcher, so captured donor sound does not publish a second weapon sound. */
static void b_sound(void *ctx, qa_compat_qc_machine *vm) { (void)ctx; (void)vm; }
static void print_text(qa_app_sim_quakec_weapon_behavior_source *s, const qa_actor_id *recipient, qa_compat_qc_machine *vm, int64_t first) {
  char *text = checked(qa_compat_qc_machine_var_string(vm, first));
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, text);
  s->options.print(s->options.ctx, recipient, text);
  qa_cleanup_pop(&cleanup); free(text);
}
static void b_sprint(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  actor_slot *bound = by_slot(&s->actors, qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0)));
  qa_actor_id recipient; if (bound) recipient = bound->actor;
  print_text(s, bound ? &recipient : NULL, vm, 1);
}
static void b_bprint(void *ctx, qa_compat_qc_machine *vm) { print_text(host_source(ctx), NULL, vm, 0); }
static void b_dprint(void *ctx, qa_compat_qc_machine *vm) { print_text(host_source(ctx), NULL, vm, 0); }
static void b_aim(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  actor_slot *bound = by_slot(&s->actors, qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 0)));
  if (bound == NULL) qa_compat_qc_machine_fail(vm, "Behavior aim has no shared shooter");
  qa_actor_id actor = bound->actor;
  qa_compat_qc_machine_return_vector(vm, s->options.aim(s->options.ctx, actor, qa_compat_qc_machine_arg_float(vm, 1)));
}
static qa_trace_policy q1_policy(const char *move) {
  qa_trace_policy policy; memset(&policy, 0, sizeof policy);
  policy.kind = "q1"; policy.move = move; policy.has_hull = false; return policy;
}
static void b_pointcontents(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  qa_point_contents_query query; memset(&query, 0, sizeof query);
  query.point = qa_compat_qc_machine_arg_vector(vm, 0); query.target.is_model = false;
  query.policy = q1_policy("normal"); query.numeric = qa_q1_donor_profile; query.pass_actor = NULL;
  qa_point_contents_result sample = s->options.scene.point_contents(s->options.scene.ctx, query);
  if (!is(sample.kind, "q1")) qa_compat_qc_machine_fail(vm, "Behavior contents lost source dialect");
  qa_compat_qc_machine_return_float(vm, sample.contents);
}
static void b_traceline(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  double mode = trunc(qa_compat_qc_machine_arg_float(vm, 2));
  actor_slot *bound = by_slot(&s->actors, qa_compat_qc_entity_memory_slot(&s->entities, qa_compat_qc_machine_arg_int(vm, 3)));
  qa_actor_id pass; const qa_actor_id *pass_actor = NULL;
  if (bound) { pass = bound->actor; pass_actor = &pass; }
  else if (s->has_launching) { pass = s->launching.projectile.id; pass_actor = &pass; }
  qa_trace_query query; memset(&query, 0, sizeof query);
  query.start = qa_compat_qc_machine_arg_vector(vm, 0); query.end = qa_compat_qc_machine_arg_vector(vm, 1);
  query.shape.kind = "point"; query.target.is_model = false;
  query.policy = q1_policy(mode == 1 ? "no-monsters" : mode == 2 ? "missile" : "normal");
  query.numeric = qa_q1_donor_profile; query.pass_actor = pass_actor;
  qa_trace_result trace = s->options.scene.trace(s->options.scene.ctx, query);
  if (!is(trace.kind, "q1")) qa_compat_qc_machine_fail(vm, "Behavior trace lost source dialect");
  global(s, "trace_fraction", trace.fraction); global(s, "trace_allsolid", trace.all_solid ? 1 : 0);
  global(s, "trace_startsolid", trace.start_solid ? 1 : 0); global(s, "trace_inwater", trace.in_water ? 1 : 0);
  global(s, "trace_inopen", trace.in_open ? 1 : 0); global(s, "trace_plane_dist", trace.source_plane_q1.distance);
  qa_compat_qc_words_set_vector(&vm->globals, qa_compat_qc_machine_global_offset(vm, "trace_endpos"), trace.end);
  qa_compat_qc_words_set_vector(&vm->globals, qa_compat_qc_machine_global_offset(vm, "trace_plane_normal"), trace.source_plane_q1.normal);
  int64_t entity = qa_compat_qc_machine_global_offset(vm, "trace_ent");
  qa_compat_qc_words_set_int(&vm->globals, entity, trace.hit.kind == QA_TRACE_HIT_ACTOR ? (double)reference(s, trace.hit.actor) : 0);
}
static double distance(const qa_numeric_operations *n, double c, double o, double a, double b) {
  return n->subtract(n->ctx, c, n->add(n->ctx, o, n->multiply(n->ctx, n->add(n->ctx, a, b), 0.5)));
}
static void b_findradius(void *ctx, qa_compat_qc_machine *vm) {
  qa_app_sim_quakec_weapon_behavior_source *s = host_source(ctx);
  qa_vec3 center = qa_compat_qc_machine_arg_vector(vm, 0); double radius = qa_compat_qc_machine_arg_float(vm, 1);
  const qa_numeric_operations *n = vm->numeric; int64_t chain = 0;
  for (int64_t slot = 1; slot < s->entities.count; slot++) {
    if (set_has(&s->free, slot) || set_has(&s->retired, slot)) continue;
    qa_compat_qc_words *fields = qa_compat_qc_entity_memory_at(&s->entities, slot);
    if (qa_compat_qc_words_float(fields, field(s, "solid")) == 0) continue;
    qa_vec3 origin = qa_compat_qc_words_vector(fields, field(s, "origin"));
    qa_vec3 min = qa_compat_qc_words_vector(fields, field(s, "mins")), max = qa_compat_qc_words_vector(fields, field(s, "maxs"));
    double x = distance(n, center.x, origin.x, min.x, max.x);
    double y = distance(n, center.y, origin.y, min.y, max.y);
    double z = distance(n, center.z, origin.z, min.z, max.z);
    double xx = n->multiply(n->ctx, x, x), yy = n->multiply(n->ctx, y, y), zz = n->multiply(n->ctx, z, z);
    if (n->square_root(n->ctx, n->add(n->ctx, n->add(n->ctx, xx, yy), zz)) > radius) continue;
    qa_compat_qc_words_set_int(fields, field(s, "chain"), (double)chain); chain = qa_compat_qc_entity_memory_reference(&s->entities, slot);
  }
  qa_compat_qc_machine_return_int(vm, (double)chain);
}
static void builtins(qa_app_sim_quakec_weapon_behavior_source *s) {
  static const struct { const char *name; void (*fn)(void *, qa_compat_qc_machine *); } table[HOST_BUILTIN_COUNT] = {
    {"spawn", b_spawn}, {"remove", b_remove}, {"setorigin", b_setorigin}, {"setsize", b_setsize}, {"setmodel", b_setmodel},
    {"sound", b_sound}, {"sprint", b_sprint}, {"bprint", b_bprint}, {"dprint", b_dprint}, {"aim", b_aim},
    {"pointcontents", b_pointcontents}, {"traceline", b_traceline}, {"findradius", b_findradius},
  };
  for (size_t i = 0; i < HOST_BUILTIN_COUNT; i++) { s->host[i].name = table[i].name; s->host[i].fn.ctx = s; s->host[i].fn.fn = table[i].fn; }
}
static double random_next_integer(void *ctx) { qa_app_sim_quakec_weapon_behavior_source *s = ctx; return s->random.next_integer(s->random.ctx); }
static double random_next_unit(void *ctx) { qa_app_sim_quakec_weapon_behavior_source *s = ctx; return s->random.next_unit(s->random.ctx); }
static qa_random_state random_checkpoint(void *ctx) { qa_app_sim_quakec_weapon_behavior_source *s = ctx; return s->random.checkpoint(s->random.ctx); }
static bool is_free_entity(void *ctx, int64_t slot) { qa_app_sim_quakec_weapon_behavior_source *s = ctx; return set_has(&s->free, slot); }
static bool server_active(void *ctx) { (void)ctx; return true; }

static bool same_module(qa_module_identity a, qa_module_identity b) {
  return is(a.id, b.id) && is(a.digest, b.digest) && is(a.revision, b.revision) && is(a.artifact_path, b.artifact_path);
}
static bool callback_identity_valid(const qa_compat_qc_program *program, qa_weapon_behavior_callback callback, qa_module_identity expected) {
  if (!same_module(callback.module, expected) || callback.kind != QA_WEAPON_CALLBACK_QUAKEC || callback.function_index <= 0) return false;
  double index = callback.function_index;
  /* TS functions[index]?.firstStatement: a non-integer or out-of-range index is undefined. */
  if (index != floor(index) || index >= (double)program->function_count) return false;
  const qa_compat_qc_function *function = &program->functions[(size_t)index];
  return function->first_statement >= 0 && function->parameter_size_count == 0;
}
static void source_free(qa_app_sim_quakec_weapon_behavior_source *s) {
  if (s->has_machine) qa_compat_qc_machine_destroy(&s->machine);
  if (s->has_registry) qa_compat_qc_registry_destroy(&s->registry);
  if (s->has_entities) qa_compat_qc_entity_memory_destroy(&s->entities);
  for (qc_instance *i = s->instances, *next; i; i = next) { next = i->next; free(i); }
  free(s->slots.entries); free(s->actors.entries);
  free(s->free.values); free(s->projectiles.values); free(s->retired.values); free(s->bound.values);
  free(s->spawned); free(s);
}
qa_app_sim_quakec_weapon_behavior_source *qa_app_sim_quakec_weapon_behavior_source_create(
    qa_app_sim_qc_weapon_behavior_options options) {
  char *capability = qa_compat_qc_weapon_behavior_capability_error(options.program);
  if (capability != NULL) {
    char message[QA_CAUGHT_MESSAGE_CAPACITY]; snprintf(message, sizeof message, "%s", capability); free(capability);
    fail_error("%s", message);
  }
  qa_weapon_behavior_definition definition = options.definition;
  if (!is(definition.module.digest, options.program->digest) || definition.fire.kind != QA_WEAPON_CALLBACK_QUAKEC
      || (definition.has_activate && definition.activate.kind != QA_WEAPON_CALLBACK_QUAKEC))
    fail_error("Weapon behavior requires its exact declared QuakeC artifact");
  for (int i = 0; i < 2; i++) {
    if (i == 1 && !definition.has_activate) continue;
    qa_weapon_behavior_callback callback = i == 0 ? definition.fire : definition.activate;
    if (callback.kind != QA_WEAPON_CALLBACK_QUAKEC) fail_error("Behavior callback is not QuakeC");
    if (!callback_identity_valid(options.program, callback, definition.module)) fail_error("Behavior callback identity differs from its source module");
  }
  qa_app_sim_quakec_weapon_behavior_source *s = calloc(1, sizeof *s);
  if (!s) return NULL;
  s->definition = definition; s->options = options; s->random = options.random;
  if (!qa_compat_qc_entity_memory_init(&s->entities, qa_compat_qc_classic_entity_layout(options.program), 8192, 1)) { source_free(s); return NULL; }
  s->has_entities = true;
  builtins(s);
  s->builtin_random.ctx = s; s->builtin_random.next_integer = random_next_integer;
  s->builtin_random.next_unit = random_next_unit; s->builtin_random.checkpoint = random_checkpoint;
  qa_compat_qc_builtin_services services; memset(&services, 0, sizeof services);
  services.kind = qa_compat_qc_host_kind_for_program(options.program); services.random = &s->builtin_random;
  services.ctx = s; services.is_free_entity = is_free_entity; services.host = s->host; services.host_count = HOST_BUILTIN_COUNT;
  if (!qa_compat_qc_create_builtins(services, &s->registry)) { source_free(s); return NULL; }
  s->has_registry = true;
  qa_compat_qc_machine_options machine; memset(&machine, 0, sizeof machine);
  machine.program = options.program; machine.entities = &s->entities; machine.numeric = qa_create_numeric_operations(&qa_q1_donor_profile);
  machine.builtins = &s->registry; machine.ctx = s; machine.server_active = server_active;
  if (!qa_compat_qc_machine_init(&s->machine, machine)) { source_free(s); return NULL; }
  s->has_machine = true;
  global(s, "deathmatch", is(options.mode, "deathmatch") ? 1 : 0); global(s, "coop", is(options.mode, "coop") ? 1 : 0);
  return s;
}
void qa_app_sim_quakec_weapon_behavior_source_destroy(qa_app_sim_quakec_weapon_behavior_source *source) { if (source) source_free(source); }
qa_weapon_behavior_definition qa_app_sim_quakec_weapon_behavior_source_definition(const qa_app_sim_quakec_weapon_behavior_source *source) { return source->definition; }
qa_compat_qc_machine *qa_app_sim_quakec_weapon_behavior_source_machine(qa_app_sim_quakec_weapon_behavior_source *source) { return &source->machine; }
static qa_weapon_behavior_instance_result as_attach(void *ctx, qa_weapon_behavior_launch launch) { return qa_app_sim_quakec_weapon_behavior_source_attach(ctx, launch); }
static qa_weapon_behavior_instance as_resume(void *ctx, qa_actor_id projectile) { return qa_app_sim_quakec_weapon_behavior_source_resume(ctx, projectile); }
qa_weapon_behavior_source qa_app_sim_quakec_weapon_behavior_source_as_source(qa_app_sim_quakec_weapon_behavior_source *source) {
  qa_weapon_behavior_source out; memset(&out, 0, sizeof out);
  out.ctx = source; out.definition = source->definition; out.attach = as_attach; out.resume = as_resume;
  return out;
}

/* ---- checkpoints ---- */
static void own_random(qa_random_state *out, qa_random_state in) {
  *out = in;
  out->words.values = NULL; out->bytes.bytes = NULL; out->module = NULL;
  if (in.words.count) { double *words = checked(malloc(in.words.count * sizeof *words)); memcpy(words, in.words.values, in.words.count * sizeof *words); out->words.values = words; }
  if (in.bytes.length) { uint8_t *bytes = checked(malloc(in.bytes.length)); memcpy(bytes, in.bytes.bytes, in.bytes.length); out->bytes.bytes = bytes; }
  if (in.module) { size_t n = strlen(in.module) + 1; char *module = checked(malloc(n)); memcpy(module, in.module, n); out->module = module; }
}
void qa_app_sim_quakec_weapon_behavior_checkpoint_free(qa_app_sim_quakec_weapon_behavior_checkpoint *c) {
  if (!c) return;
  free((void *)c->random.words.values); free((void *)c->random.bytes.bytes); free((void *)c->random.module);
  qa_compat_qc_machine_snapshot_destroy(&c->machine);
  free(c->free_slots); free(c->retired); free(c->bindings);
  memset(c, 0, sizeof *c);
}
static void checkpoint_cleanup(void *arg) { qa_app_sim_quakec_weapon_behavior_checkpoint_free(arg); }
static double *slot_list(const slot_set *s) {
  double *out = checked(malloc((s->count ? s->count : 1) * sizeof *out));
  for (size_t i = 0; i < s->count; i++) out[i] = (double)s->values[i];
  return out;
}
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_quakec_weapon_behavior_source_checkpoint(
    qa_app_sim_quakec_weapon_behavior_source *s) {
  if (s->has_launching) fail_error("Cannot save during a weapon behavior launch");
  qa_app_sim_quakec_weapon_behavior_checkpoint out; memset(&out, 0, sizeof out);
  out.version = 1; out.definition = s->definition; out.mode = s->options.mode; out.source_time = s->source_time;
  own_random(&out.random, s->random.checkpoint(s->random.ctx));
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, checkpoint_cleanup, &out);
  if (!qa_compat_qc_machine_get_snapshot(&s->machine, &out.machine)) qa_range_error("Out of memory");
  qa_cleanup_pop(&cleanup);
  out.free_slots = slot_list(&s->free); out.free_count = s->free.count;
  out.retired = slot_list(&s->retired); out.retired_count = s->retired.count;
  out.binding_count = s->actors.count;
  out.bindings = checked(malloc((out.binding_count ? out.binding_count : 1) * sizeof *out.bindings));
  for (size_t i = 0; i < s->actors.count; i++) {
    actor_slot e = s->actors.entries[i];
    out.bindings[i].slot = (double)e.slot;
    out.bindings[i].actor.slot = (double)e.actor.slot; out.bindings[i].actor.generation = (double)e.actor.generation;
    out.bindings[i].kind = set_has(&s->projectiles, e.slot) ? "projectile" : "target";
  }
  return out;
}
/* JSON.stringify equality of two RandomState values (same producer key order). */
static bool same_number(double a, double b) { return a == b || (isnan(a) && isnan(b)); }
static bool same_random_state(qa_random_state a, qa_random_state b) {
  if (a.kind != b.kind || !same_number(a.draws, b.draws)) return false;
  switch (a.kind) {
    case QA_RANDOM_STATE_Q3_LCG: case QA_RANDOM_STATE_MSVCRT_RAND: return same_number(a.seed, b.seed);
    case QA_RANDOM_STATE_GLIBC_RANDOM: case QA_RANDOM_STATE_Q2_RERELEASE_MT19937:
      if (a.words.count != b.words.count) return false;
      for (size_t i = 0; i < a.words.count; i++) if (!same_number(a.words.values[i], b.words.values[i])) return false;
      if (a.kind == QA_RANDOM_STATE_GLIBC_RANDOM) return same_number(a.front, b.front) && same_number(a.rear, b.rear);
      return same_number(a.index, b.index);
    case QA_RANDOM_STATE_GUEST:
      return is(a.module, b.module) && a.bytes.length == b.bytes.length && (a.bytes.length == 0 || memcmp(a.bytes.bytes, b.bytes.bytes, a.bytes.length) == 0);
  }
  return false;
}
static bool safe_integer(double v) { return isfinite(v) && v == floor(v) && fabs(v) <= (double)QA_MAX_SAFE_INTEGER; }
typedef struct staged_binding { int64_t slot; qa_actor_id actor; bool projectile; } staged_binding;
typedef struct restore_scratch { int64_t *free; staged_binding *bindings; int64_t *retired; } restore_scratch;
static void scratch_free(void *arg) { restore_scratch *r = arg; free(r->free); free(r->bindings); free(r->retired); }
static bool contains(const int64_t *values, size_t count, int64_t v) { for (size_t i = 0; i < count; i++) if (values[i] == v) return true; return false; }
void qa_app_sim_quakec_weapon_behavior_source_restore(qa_app_sim_quakec_weapon_behavior_source *s,
    const qa_app_sim_quakec_weapon_behavior_checkpoint *checkpoint,
    qa_actor_id (*actor_reference)(void *ctx, qa_saved_actor_id saved), void *reference_ctx,
    qa_random_source random) {
  if (checkpoint->version != 1 || !qa_same_weapon_behavior(checkpoint->definition, s->definition) || !is(checkpoint->mode, s->options.mode)
      || !isfinite(checkpoint->source_time) || s->actors.count != 0 || s->has_launching) fail_error("Incompatible QuakeC weapon behavior checkpoint");
  if (!same_random_state(random.checkpoint(random.ctx), checkpoint->random)) fail_error("Weapon behavior random source was not restored");
  double count = (double)checkpoint->machine.entity_count;
  restore_scratch scratch = {
    checked(malloc((checkpoint->free_count ? checkpoint->free_count : 1) * sizeof(int64_t))),
    checked(malloc((checkpoint->binding_count ? checkpoint->binding_count : 1) * sizeof(staged_binding))),
    checked(malloc((checkpoint->retired_count ? checkpoint->retired_count : 1) * sizeof(int64_t))),
  };
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, scratch_free, &scratch);
  size_t free_count = 0, binding_count = 0, retired_count = 0;
#define VALID_SLOT(v) (safe_integer(v) && (v) > 0 && (v) < count)
  for (size_t i = 0; i < checkpoint->free_count; i++) {
    double slot = checkpoint->free_slots[i];
    if (!VALID_SLOT(slot) || contains(scratch.free, free_count, (int64_t)slot)) fail_error("Invalid saved behavior free slot");
    scratch.free[free_count++] = (int64_t)slot;
  }
  for (size_t i = 0; i < checkpoint->binding_count; i++) {
    const qa_app_sim_qc_weapon_binding *binding = &checkpoint->bindings[i];
    bool occupied = false;
    if (VALID_SLOT(binding->slot)) for (size_t b = 0; b < binding_count; b++) if (scratch.bindings[b].slot == (int64_t)binding->slot) occupied = true;
    if (!VALID_SLOT(binding->slot) || occupied || contains(scratch.free, free_count, (int64_t)binding->slot)) fail_error("Invalid saved behavior entity slot");
    staged_binding *staged = &scratch.bindings[binding_count++];
    staged->slot = (int64_t)binding->slot; staged->projectile = is(binding->kind, "projectile");
    staged->actor = actor_reference(reference_ctx, binding->actor);
    for (size_t b = 0; b + 1 < binding_count; b++) if (qa_same_actor(scratch.bindings[b].actor, staged->actor)) fail_error("Duplicate saved behavior actor reference");
  }
#undef VALID_SLOT
  if ((double)(binding_count + free_count) != count - 1) fail_error("Saved behavior entity ownership is incomplete");
  for (size_t i = 0; i < checkpoint->retired_count; i++) {
    double slot = checkpoint->retired[i];
    bool projectile = false;
    for (size_t b = 0; b < binding_count; b++) if ((double)scratch.bindings[b].slot == slot && scratch.bindings[b].projectile) projectile = true;
    if (contains(scratch.retired, retired_count, (int64_t)slot) || !projectile) fail_error("Invalid retired behavior slot");
    scratch.retired[retired_count++] = (int64_t)slot;
  }
  if (!qa_compat_qc_machine_restore(&s->machine, &checkpoint->machine)) qa_range_error("Out of memory");
  s->random = random; s->source_time = checkpoint->source_time;
  for (size_t i = 0; i < free_count; i++) set_add(&s->free, scratch.free[i]);
  for (size_t i = 0; i < retired_count; i++) set_add(&s->retired, scratch.retired[i]);
  for (size_t i = 0; i < binding_count; i++) {
    actors_set(&s->actors, scratch.bindings[i].slot, scratch.bindings[i].actor);
    if (scratch.bindings[i].projectile) set_add(&s->projectiles, scratch.bindings[i].slot);
    else slots_set(&s->slots, scratch.bindings[i].actor, scratch.bindings[i].slot);
  }
  qa_cleanup_pop(&cleanup); scratch_free(&scratch);
}

/* ---- reader ---- */
static qa_save_reader fld(const qa_save_reader *r, const char *n) {
  qa_save_reader o; if (!qa_save_reader_field(r, n, &o)) qa_save_reader_fail(r, "out of memory"); return o;
}
static double *read_slots(const qa_save_reader *r, const char *n, size_t *out_count) {
  qa_save_reader f = fld(r, n);
  size_t count = qa_save_reader_list_count(&f);
  double *out = checked(malloc((count ? count : 1) * sizeof *out));
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, free, out);
  for (size_t i = 0; i < count; i++) {
    qa_save_reader item; qa_save_reader_item(&f, i, &item);
    out[i] = qa_save_reader_integer_min(&item, 1); qa_save_reader_destroy(&item);
  }
  qa_cleanup_pop(&cleanup);
  qa_save_reader_destroy(&f); *out_count = count; return out;
}
static uint8_t *read_bytes(const qa_save_reader *r, const char *n, size_t *out_length) {
  qa_save_reader f = fld(r, n);
  size_t length = 0; const uint8_t *bytes = qa_save_reader_bytes(&f, &length);
  uint8_t *out = checked(malloc(length ? length : 1)); if (length) memcpy(out, bytes, length);
  qa_save_reader_destroy(&f); *out_length = length; return out;
}
static double read_number(const qa_save_reader *r, const char *n, double (*read)(const qa_save_reader *)) {
  qa_save_reader f = fld(r, n); double v = read(&f); qa_save_reader_destroy(&f); return v;
}
static double read_min(const qa_save_reader *r, const char *n, double minimum) {
  qa_save_reader f = fld(r, n); double v = qa_save_reader_integer_min(&f, minimum); qa_save_reader_destroy(&f); return v;
}
static double read_literal(const qa_save_reader *r, const char *n, double expected) {
  qa_save_reader f = fld(r, n); double v = qa_save_reader_literal_number(&f, expected); qa_save_reader_destroy(&f); return v;
}
static const char *read_choice(const qa_save_reader *r, const char *n, const char *const *choices, size_t count) {
  qa_save_reader f = fld(r, n); const char *v = qa_save_reader_choice_string(&f, choices, count); qa_save_reader_destroy(&f); return v;
}
qa_app_sim_quakec_weapon_behavior_checkpoint qa_app_sim_read_quakec_weapon_behavior_checkpoint(
    const qa_save_reader *reader, qa_weapon_behavior_definition expected) {
  static const char *const modes[] = {"singleplayer", "coop", "deathmatch"};
  static const char *const kinds[] = {"target", "projectile"};
  qa_app_sim_quakec_weapon_behavior_checkpoint out; memset(&out, 0, sizeof out);
  qa_cleanup cleanup; qa_cleanup_push(&cleanup, checkpoint_cleanup, &out);
  qa_save_reader machine = fld(reader, "machine"), f;
  out.version = read_literal(reader, "version", 1);
  f = fld(reader, "definition"); out.definition = qa_read_weapon_behavior_definition(&f, expected); qa_save_reader_destroy(&f);
  out.mode = read_choice(reader, "mode", modes, 3);
  out.source_time = read_number(reader, "sourceTime", qa_save_reader_finite);
  f = fld(reader, "random");
  qa_random_state random = qa_read_random(&f); own_random(&out.random, random); free((void *)random.words.values);
  qa_save_reader_destroy(&f);
  out.machine.globals = read_bytes(&machine, "globals", &out.machine.globals_length);
  out.machine.entities = read_bytes(&machine, "entities", &out.machine.entities_length);
  out.machine.entity_count = (int64_t)read_min(&machine, "entityCount", 1);
  out.machine.strings = read_bytes(&machine, "strings", &out.machine.strings_length);
  out.machine.statement = (int64_t)read_number(&machine, "statement", qa_save_reader_integer);
  out.machine.function_index = (int64_t)read_literal(&machine, "functionIndex", 0);
  out.machine.argument_count = (int64_t)read_min(&machine, "argumentCount", 0);
  f = fld(&machine, "profiling");
  out.machine.profiling_count = qa_save_reader_list_count(&f);
  out.machine.profiling = checked(malloc((out.machine.profiling_count ? out.machine.profiling_count : 1) * sizeof(double)));
  for (size_t i = 0; i < out.machine.profiling_count; i++) {
    qa_save_reader item; qa_save_reader_item(&f, i, &item);
    out.machine.profiling[i] = qa_save_reader_integer_min(&item, 0); qa_save_reader_destroy(&item);
  }
  qa_save_reader_destroy(&f);
  f = fld(&machine, "traceEnabled"); out.machine.trace_enabled = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
  qa_save_reader_destroy(&machine);
  out.free_slots = read_slots(reader, "free", &out.free_count);
  out.retired = read_slots(reader, "retired", &out.retired_count);
  qa_save_reader bindings = fld(reader, "bindings");
  size_t count = qa_save_reader_list_count(&bindings);
  out.bindings = checked(malloc((count ? count : 1) * sizeof *out.bindings));
  for (size_t i = 0; i < count; i++) {
    qa_save_reader entry; qa_save_reader_item(&bindings, i, &entry);
    out.bindings[i].slot = read_min(&entry, "slot", 1);
    f = fld(&entry, "actor"); out.bindings[i].actor = qa_read_saved_actor(&f); qa_save_reader_destroy(&f);
    out.bindings[i].kind = read_choice(&entry, "kind", kinds, 2);
    out.binding_count = i + 1;
    qa_save_reader_destroy(&entry);
  }
  qa_save_reader_destroy(&bindings);
  qa_cleanup_pop(&cleanup);
  return out;
}
