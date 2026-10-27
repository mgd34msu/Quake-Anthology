/* Port of src/compat/qc/movement-host.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/movement_host.h"
#include <math.h>
#include <string.h>
#include "qa/core/numeric.h"
typedef qa_compat_qc_movement_state_compat S;
static int64_t foff(S *s, const char *n) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(s->world->options.program, n);
  if (!d) qa_compat_qc_program_error("progs.dat", "missing entity field %s", n);
  return d->offset;
}
static bool source_slot(S *s, qa_actor_id actor, int64_t *out) {
  if (!qa_session_actor_registry_is_live(s->world->options.actors, actor)) return false;
  qa_actor_source_result r = qa_session_actor_registry_source_of(s->world->options.actors, actor);
  if (!r.has_value || strcmp(r.provider, s->world->options.slots_options.provider) != 0) return false;
  *out = r.slot; return true;
}
static bool referenced(S *s, int64_t ref, qa_actor_id *out) {
  if (s->services.actor_reference) return s->services.actor_reference(s->services.ctx, ref, out);
  qa_owned_actor owned;
  if (!qa_source_actor_slots_at(s->world->options.slots, qa_compat_qc_entity_memory_slot(s->world->options.entities, ref), &owned)) return false;
  *out = owned.id; return true;
}
static bool m_read(void *ctx, qa_actor_id actor, qa_q1_monster_move_state *out) {
  S *s = ctx; int64_t slot;
  qa_body_state_result body = qa_shared_body_table_read(s->world->options.bodies, actor);
  if (!source_slot(s, actor, &slot) || !body.has_value) return false;
  qa_compat_qc_entity_memory *e = s->world->options.entities;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(e, slot);
  int64_t flags = (int64_t)trunc(qa_compat_qc_words_float(w, foff(s, "flags")));
  int32_t ground_ref = qa_compat_qc_words_int(w, foff(s, "groundentity"));
  qa_actor_id ground_actor; bool has_ground_actor = ground_ref != 0 && referenced(s, ground_ref, &ground_actor);
  memset(out, 0, sizeof *out);
  out->origin = body.value.origin; out->angles = body.value.angles; out->bounds = body.value.bounds;
  qa_linked_body_result linked = qa_shared_body_table_linked(s->world->options.bodies, actor);
  out->absolute_bounds = linked.has_value ? linked.value.absolute_bounds : qa_compat_qc_link_bounds(body.value, (double)flags, &s->numeric);
  out->flags = (double)flags;
  if ((flags & 512) == 0) out->ground.kind = QA_TRACE_HIT_NONE;
  else if (ground_ref == 0) { out->ground.kind = QA_TRACE_HIT_WORLD; out->ground.model = 0; }
  else if (!has_ground_actor) out->ground.kind = QA_TRACE_HIT_NONE;
  else { out->ground.kind = QA_TRACE_HIT_ACTOR; out->ground.actor = ground_actor; }
  out->ideal_yaw = qa_compat_qc_words_float(w, foff(s, "ideal_yaw"));
  out->yaw_speed = qa_compat_qc_words_float(w, foff(s, "yaw_speed"));
  int32_t enemy_ref = qa_compat_qc_words_int(w, foff(s, "enemy"));
  qa_actor_id enemy;
  if (enemy_ref != 0 && referenced(s, enemy_ref, &enemy)) { out->has_enemy = true; out->enemy = enemy; }
  return true;
}
static bool m_read_target(void *ctx, qa_actor_id actor, qa_q1_monster_target *out) {
  S *s = ctx; int64_t slot; bool has_slot = source_slot(s, actor, &slot);
  qa_body_state_result body = qa_shared_body_table_read(s->world->options.bodies, actor);
  if (!body.has_value) return false;
  if (has_slot) {
    qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->world->options.entities, slot);
    out->origin = body.value.origin;
    out->absolute_bounds.min = qa_compat_qc_words_vector(w, foff(s, "absmin"));
    out->absolute_bounds.max = qa_compat_qc_words_vector(w, foff(s, "absmax"));
    return true;
  }
  qa_linked_body_result linked = qa_shared_body_table_linked(s->world->options.bodies, actor);
  if (!linked.has_value) return false;
  out->origin = body.value.origin; out->absolute_bounds = linked.value.absolute_bounds; return true;
}
static void m_write(void *ctx, qa_owned_actor actor, qa_q1_monster_move_state state) {
  S *s = ctx; int64_t slot;
  if (!source_slot(s, actor.id, &slot)) return;
  if (!qa_shared_body_table_read(s->world->options.bodies, actor.id).has_value) return;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->world->options.entities, slot);
  qa_compat_qc_words_set_vector(w, foff(s, "origin"), state.origin);
  qa_compat_qc_words_set_vector(w, foff(s, "angles"), state.angles);
  if (state.ground.kind == QA_TRACE_HIT_ACTOR) qa_compat_qc_words_set_int(w, foff(s, "groundentity"), (double)qa_compat_qc_world_host_reference(s->world, state.ground.actor));
  else if (state.ground.kind == QA_TRACE_HIT_WORLD) qa_compat_qc_words_set_int(w, foff(s, "groundentity"), 0);
  qa_compat_qc_words_set_float(w, foff(s, "flags"), state.flags);
  qa_compat_qc_words_set_float(w, foff(s, "ideal_yaw"), state.ideal_yaw);
  qa_compat_qc_words_set_float(w, foff(s, "yaw_speed"), state.yaw_speed);
}
static void m_link(void *ctx, qa_owned_actor actor, bool touch) {
  S *s = ctx; int64_t slot; if (!source_slot(s, actor.id, &slot)) return;
  qa_compat_qc_world_host_link(s->world, slot);
  if (touch) s->services.touch_triggers(s->services.ctx, actor);
}
static double m_next_int(void *ctx) { S *s = ctx; return s->services.random.next_integer(s->services.random.ctx); }
static void check(S *s, qa_compat_qc_machine *m) { if (m->program != s->world->options.program || m->entities != s->world->options.entities) qa_compat_qc_machine_fail(m, "movement builtin belongs to another QC machine"); }
static void b_walkmove(void *ctx, qa_compat_qc_machine *m) { S *s=ctx; check(s,m);
  int64_t self_off = qa_compat_qc_machine_global_offset(m, "self");
  int32_t saved = qa_compat_qc_words_int(&m->globals, self_off);
  bool r = qa_q1_monster_walk_move(&s->movement, qa_compat_qc_world_host_actor(s->world, qa_compat_qc_entity_memory_slot(s->world->options.entities, saved)), qa_compat_qc_machine_arg_float(m,0), qa_compat_qc_machine_arg_float(m,1));
  qa_compat_qc_words_set_int(&m->globals, self_off, saved);
  qa_compat_qc_machine_return_float(m, r ? 1 : 0); }
static void b_movetogoal(void *ctx, qa_compat_qc_machine *m) { S *s=ctx; check(s,m);
  qa_compat_qc_entity_memory *e = s->world->options.entities;
  int64_t self_slot = qa_compat_qc_entity_memory_slot(e, qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m, "self")));
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(e, self_slot);
  qa_owned_actor actor = qa_compat_qc_world_host_actor(s->world, self_slot);
  qa_owned_actor goal = qa_compat_qc_world_host_actor(s->world, qa_compat_qc_entity_memory_slot(e, qa_compat_qc_words_int(w, foff(s, "goalentity"))));
  double dist = qa_compat_qc_machine_arg_float(m, 0);
  if (((int64_t)trunc(qa_compat_qc_words_float(w, foff(s, "flags"))) & (512|1|2)) == 0) { qa_compat_qc_machine_return_float(m, 0); return; }
  qa_q1_monster_move_to_goal(&s->movement, actor, goal.id, dist, NULL); }
static void b_changeyaw(void *ctx, qa_compat_qc_machine *m) { S *s=ctx; check(s,m);
  qa_q1_monster_change_yaw(&s->movement, qa_compat_qc_world_host_actor(s->world, qa_compat_qc_entity_memory_slot(s->world->options.entities, qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m, "self"))))); }
static void b_checkbottom(void *ctx, qa_compat_qc_machine *m) { S *s=ctx; check(s,m);
  qa_compat_qc_machine_return_float(m, qa_q1_monster_check_bottom(&s->movement, qa_compat_qc_world_host_actor(s->world, qa_compat_qc_entity_memory_slot(s->world->options.entities, qa_compat_qc_machine_arg_int(m,0))).id) ? 1 : 0); }
qa_compat_qc_movement_bindings qa_compat_qc_create_movement_bindings(qa_compat_qc_world_host *world, qa_compat_qc_movement_services services, S *st) {
  st->world = world; st->services = services;
  st->numeric = *qa_create_numeric_operations(&world->options.numeric);
  qa_q1_monster_move_services ms; memset(&ms, 0, sizeof ms);
  ms.ctx = st; ms.scene = services.scene; ms.numeric = st->numeric;
  ms.random = (qa_q1_monster_random){st, m_next_int};
  ms.read = m_read; ms.read_target = m_read_target; ms.write = m_write; ms.link = m_link;
  qa_q1_monster_movement_init(&st->movement, ms);
  qa_compat_qc_movement_bindings r; r.count = 4;
  r.entries[0]=(qa_compat_qc_host_binding){"walkmove",{st,b_walkmove}};
  r.entries[1]=(qa_compat_qc_host_binding){"movetogoal",{st,b_movetogoal}};
  r.entries[2]=(qa_compat_qc_host_binding){"changeyaw",{st,b_changeyaw}};
  r.entries[3]=(qa_compat_qc_host_binding){"checkbottom",{st,b_checkbottom}};
  return r;
}
qa_compat_qc_touch_callback qa_compat_qc_create_touch_callback(qa_compat_qc_world_host *world, qa_compat_qc_machine *vm, void *ctx, double (*server_time)(void *), qa_compat_qc_touch_state *st) {
  if (vm->program != world->options.program || vm->entities != world->options.entities) qa_compat_qc_program_error("progs.dat", "touch callback belongs to another QC machine");
  st->world = world; st->vm = vm; st->ctx = ctx; st->server_time = server_time;
  st->solid = qa_compat_qc_machine_field_offset(vm, "solid"); st->touch = qa_compat_qc_machine_field_offset(vm, "touch");
  return (qa_compat_qc_touch_callback){st};
}
void qa_compat_qc_touch_callback_call(qa_compat_qc_touch_callback cb, qa_touch_contact contact) {
  qa_compat_qc_touch_state *st = cb.state; qa_compat_qc_world_host *world = st->world; qa_compat_qc_machine *vm = st->vm;
  if (!qa_session_actor_registry_is_live(world->options.actors, contact.self.id) || !qa_session_actor_registry_is_live(world->options.actors, contact.other)) return;
  qa_actor_source_result src = qa_session_actor_registry_source_of(world->options.actors, contact.self.id);
  if (!src.has_value || strcmp(src.provider, world->options.slots_options.provider) != 0) return;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(vm->entities, src.slot);
  int32_t callback = qa_compat_qc_words_int(w, st->touch);
  if (qa_compat_qc_words_float(w, st->solid) != 1 || callback == 0) return;
  int64_t self_off = qa_compat_qc_machine_global_offset(vm, "self"), other_off = qa_compat_qc_machine_global_offset(vm, "other");
  int32_t saved_self = qa_compat_qc_words_int(&vm->globals, self_off), saved_other = qa_compat_qc_words_int(&vm->globals, other_off);
  qa_compat_qc_words_set_int(&vm->globals, self_off, (double)qa_compat_qc_world_host_reference(world, contact.self.id));
  qa_compat_qc_words_set_int(&vm->globals, other_off, (double)qa_compat_qc_world_host_reference(world, contact.other));
  qa_compat_qc_words_set_float(&vm->globals, qa_compat_qc_machine_global_offset(vm, "time"), st->server_time(st->ctx));
  qa_compat_qc_machine_execute(vm, callback, 0);
  qa_compat_qc_words_set_int(&vm->globals, self_off, saved_self);
  qa_compat_qc_words_set_int(&vm->globals, other_off, saved_other);
}
