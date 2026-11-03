/* Port of src/compat/qc/pusher-host.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/pusher_host.h"
#include <math.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/foundation/js.h"
typedef qa_compat_qc_pusher_state S;
typedef struct { qa_compat_qc_machine *vm; int64_t self_off, other_off; int32_t saved_self, saved_other; } SavedGlobals;
static void restore_globals(void *arg) { /* TS finally */
  SavedGlobals *g = arg;
  qa_compat_qc_words_set_int(&g->vm->globals, g->self_off, g->saved_self);
  qa_compat_qc_words_set_int(&g->vm->globals, g->other_off, g->saved_other);
}
static int64_t fld(S *s, const char *n) { return qa_compat_qc_machine_field_offset(s->vm, n); }
static bool source_slot(S *s, qa_actor_id actor, int64_t *out) {
  if (!qa_session_actor_registry_is_live(s->world->options.actors, actor)) return false;
  qa_actor_source_result r = qa_session_actor_registry_source_of(s->world->options.actors, actor);
  if (!r.has_value || strcmp(r.provider, s->world->options.slots_options.provider) != 0) return false;
  *out = r.slot; return true;
}
static bool p_read(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out) {
  S *s = ctx; int64_t slot;
  if (!source_slot(s, actor, &slot)) return s->foreign_read(s->ctx, actor, out);
  qa_body_state_result body = qa_shared_body_table_read(s->world->options.bodies, actor);
  qa_owned_actor owned;
  if (!body.has_value || !qa_session_actor_registry_resolve_owned(s->world->options.actors, actor, &owned)) return false;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->world->options.entities, slot);
  double flags = trunc(qa_compat_qc_words_float(w, fld(s, "flags"))); /* TS keeps the number */
  int32_t ground = qa_compat_qc_words_int(w, fld(s, "groundentity"));
  double solid = qa_compat_qc_words_float(w, fld(s, "solid"));
  bool qw = s->world->options.program->api.kind == QA_QUAKEC_API_Q1_QUAKEWORLD;
  memset(out, 0, sizeof *out);
  out->actor = owned; out->bounds = body.value.bounds;
  out->absolute_bounds.min = qa_compat_qc_words_vector(w, fld(s, "absmin"));
  out->absolute_bounds.max = qa_compat_qc_words_vector(w, fld(s, "absmax"));
  out->solid = solid == 0 ? "not" : solid == 1 ? "trigger" : solid == 4 ? "bsp" : solid == 3 ? "slidebox" : "box";
  out->local_time_seconds = qa_compat_qc_words_float(w, fld(s, "ltime"));
  out->next_think_seconds = qa_compat_qc_words_float(w, fld(s, "nextthink"));
  qa_q1_movement_state *st = &out->state;
  st->origin = body.value.origin; st->velocity = body.value.velocity; st->angles = body.value.angles;
  st->old_origin = qa_compat_qc_words_vector(w, fld(s, "oldorigin"));
  st->angular_velocity = qa_compat_qc_words_vector(w, fld(s, "avelocity"));
  st->view_angles = qa_compat_qc_words_vector(w, fld(s, "v_angle"));
  st->punch_angles = qw ? (qa_vec3){0,0,0} : qa_compat_qc_words_vector(w, fld(s, "punchangle"));
  st->move_type = qa_compat_qc_words_float(w, fld(s, "movetype"));
  st->flags = flags;
  if ((qa_js_to_int32(flags) & 512) == 0) st->ground.kind = QA_TRACE_HIT_NONE;
  else if (ground == 0) { st->ground.kind = QA_TRACE_HIT_WORLD; st->ground.model = 0; }
  else if (body.value.ground == NULL) st->ground.kind = QA_TRACE_HIT_NONE;
  else { st->ground.kind = QA_TRACE_HIT_ACTOR; st->ground.actor = *body.value.ground; }
  st->water_level = qa_compat_qc_words_float(w, fld(s, "waterlevel"));
  st->water_type = qa_compat_qc_words_float(w, fld(s, "watertype"));
  st->teleport_time_seconds = qa_compat_qc_words_float(w, fld(s, "teleport_time"));
  st->water_jump_direction = qa_compat_qc_words_vector(w, fld(s, "movedir"));
  st->ideal_pitch = qw ? 0 : qa_compat_qc_words_float(w, fld(s, "idealpitch"));
  st->fix_angle = qa_compat_qc_words_float(w, fld(s, "fixangle")) != 0;
  st->health = qa_compat_qc_words_float(w, fld(s, "health"));
  return true;
}
static void p_write(void *ctx, qa_q1_physics_entity e) {
  S *s = ctx; int64_t slot;
  if (!source_slot(s, e.actor.id, &slot)) { s->foreign_write(s->ctx, e); return; }
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->world->options.entities, slot);
  qa_compat_qc_words_set_vector(w, fld(s, "origin"), e.state.origin);
  qa_compat_qc_words_set_vector(w, fld(s, "angles"), e.state.angles);
  qa_compat_qc_words_set_float(w, fld(s, "flags"), e.state.flags);
  qa_compat_qc_words_set_vector(w, fld(s, "mins"), e.bounds.min);
  qa_compat_qc_words_set_vector(w, fld(s, "maxs"), e.bounds.max);
  qa_compat_qc_words_set_float(w, fld(s, "ltime"), e.local_time_seconds);
  qa_compat_qc_words_set_float(w, fld(s, "nextthink"), e.next_think_seconds);
}
static void invoke_cb(S *s, qa_actor_id actor, const char *name, const qa_actor_id *other) {
  int64_t slot; if (!source_slot(s, actor, &slot)) qa_compat_qc_program_error("progs.dat", "pusher callback requires a live QC actor");
  const qa_compat_qc_words * w = qa_compat_qc_entity_memory_at(s->world->options.entities, slot);
  int64_t word = fld(s, name);
  int32_t callback = qa_compat_qc_words_int(w, word);
  if (callback == 0 && strcmp(name, "blocked") == 0) return;
  if (s->invoke) {
    qa_owned_actor owner;
    if (!qa_session_actor_registry_resolve_owned(s->world->options.actors, actor, &owner)) qa_compat_qc_program_error("progs.dat", "pusher callback lost its source owner");
    s->invoke(s->ctx, owner, name, other); return;
  }
  qa_compat_qc_machine *vm = s->vm;
  int64_t self_off = qa_compat_qc_machine_global_offset(vm, "self"), other_off = qa_compat_qc_machine_global_offset(vm, "other");
  int32_t saved_self = qa_compat_qc_words_int(&vm->globals, self_off), saved_other = qa_compat_qc_words_int(&vm->globals, other_off);
  SavedGlobals g = {vm, self_off, other_off, saved_self, saved_other}; qa_cleanup scope; qa_cleanup_push(&scope, restore_globals, &g);
  qa_compat_qc_words_set_int(&vm->globals, self_off, (double)qa_compat_qc_entity_memory_reference(s->world->options.entities, slot));
  qa_compat_qc_words_set_int(&vm->globals, other_off, other ? (double)qa_compat_qc_world_host_reference(s->world, *other) : 0);
  qa_compat_qc_words_set_float(&vm->globals, qa_compat_qc_machine_global_offset(vm, "time"), s->server_time(s->ctx));
  qa_compat_qc_machine_execute(vm, callback, 0);
  qa_cleanup_pop(&scope); restore_globals(&g);
}
static void p_link(void *ctx, qa_owned_actor actor, bool touch) {
  S *s = ctx; int64_t slot;
  if (!source_slot(s, actor.id, &slot)) qa_shared_body_table_link(s->world->options.bodies, actor, NULL);
  else qa_compat_qc_world_host_link(s->world, slot);
  if (touch) s->touch_triggers(s->ctx, actor);
}
static void p_blocked(void *ctx, qa_owned_actor pusher, qa_actor_id obstacle) { invoke_cb(ctx, pusher.id, "blocked", &obstacle); }
/* TS {...physical}: physical's read/write/link/blocked are the service's.
 * A physical result that passed the projection through (or left it
 * unset) resolves to the projection with its own context. */
static bool out_read(void *ctx, qa_actor_id a, qa_q1_physics_entity *o) { S *s=ctx; return (!s->inner.read || s->inner.read == p_read) ? p_read(s,a,o) : s->inner.read(s->inner.ctx,a,o); }
static void out_write(void *ctx, qa_q1_physics_entity e) { S *s=ctx; if (!s->inner.write || s->inner.write == p_write) p_write(s,e); else s->inner.write(s->inner.ctx,e); }
static void out_link(void *ctx, qa_owned_actor a, bool t) { S *s=ctx; if (!s->inner.link || s->inner.link == p_link) p_link(s,a,t); else s->inner.link(s->inner.ctx,a,t); }
static void out_blocked(void *ctx, qa_owned_actor a, qa_actor_id o) { S *s=ctx; if (!s->inner.blocked || s->inner.blocked == p_blocked) p_blocked(s,a,o); else s->inner.blocked(s->inner.ctx,a,o); }
static const qa_actor_id *out_candidates(void *ctx, size_t *n) { S *s=ctx; return s->inner.candidates(s->inner.ctx, n); }
static qa_trace_hit out_test(void *ctx, qa_q1_physics_entity e) { S *s=ctx; return s->inner.test_position(s->inner.ctx, e); }
static qa_q1_push_result out_push(void *ctx, qa_q1_physics_entity e, qa_vec3 d) { S *s=ctx; return s->inner.push(s->inner.ctx, e, d); }
static void out_collision(void *ctx, qa_owned_actor actor, bool enabled) {
  qa_compat_qc_words * w;
  int64_t word;
  S *s = ctx; int64_t slot;
  if (source_slot(s, actor.id, &slot)) (w = qa_compat_qc_entity_memory_at(s->world->options.entities, slot), word = fld(s, "solid"), qa_compat_qc_words_set_float(w, word, enabled ? 4 : 0));
  s->inner.collision_enabled(s->inner.ctx, actor, enabled);
}
static void out_think(void *ctx, qa_owned_actor actor) { invoke_cb(ctx, actor.id, "think", NULL); }
qa_q1_pusher_services qa_compat_qc_create_pusher_services(qa_compat_qc_world_host *world, qa_compat_qc_machine *vm, S *st) {
  if (vm->program != world->options.program || vm->entities != world->options.entities) qa_compat_qc_program_error("progs.dat", "pusher belongs to another QC machine");
  st->world = world; st->vm = vm;
  qa_compat_qc_pusher_projection proj = {st, p_read, p_write, p_link, p_blocked};
  st->inner = st->physical(st->ctx, proj);
  qa_q1_pusher_services out = st->inner;
  out.ctx = st; out.read = out_read; out.write = out_write; out.link = out_link; out.blocked = out_blocked;
  out.collision_enabled = out_collision; out.think = out_think;
  out.candidates = out_candidates; out.test_position = out_test; out.push = out_push;
  return out;
}
