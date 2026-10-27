/* Port of src/compat/qc/actor-state.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/actor_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
void qa_compat_qc_actor_state_init(qa_compat_qc_actor_state *s, qa_compat_qc_actor_state_options o) { s->options = o; }
static int64_t fld(qa_compat_qc_actor_state *s, const char *n) { return qa_compat_qc_machine_field_offset(s->options.machine, n); }
bool qa_compat_qc_actor_state_collision(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_compat_qc_shared_solid *out) {
  int64_t slot; if (!s->options.source_slot(s->options.ctx, actor.id, &slot)) return false;
  qa_compat_qc_machine *m = s->options.machine;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(m->entities, slot);
  double solid = qa_compat_qc_words_float(w, fld(s, "solid"));
  int64_t flags = (int64_t)trunc(qa_compat_qc_words_float(w, fld(s, "flags")));
  const char *name = qa_compat_qc_strings_get(&m->strings, qa_compat_qc_words_int(w, fld(s, "model")));
  memset(out, 0, sizeof *out); out->family = "q1";
  if (name[0] == '*') { out->has_model = true; out->model = strtod(name + 1, NULL); /* TS Number() */ }
  else if (slot == 0) { out->has_model = true; out->model = 0; }
  bool corpse = solid == 5 && s->options.rerelease;
  out->q1_corpse = corpse;
  out->solid = (solid == 0 || (solid == 5 && !corpse)) ? "none" : solid == 1 ? "trigger" : solid == 4 ? "brush" : "box";
  int32_t owner = qa_compat_qc_words_int(w, fld(s, "owner"));
  if (owner != 0) out->has_owner = s->options.reference(s->options.ctx, owner, &out->owner);
  out->has_monster = true; out->monster = (flags & 32) != 0;
  out->has_item = true; out->item = (flags & 256) != 0;
  return true;
}
bool qa_compat_qc_actor_state_motion(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_body_state body, qa_q2_motion *out) {
  int64_t slot; if (!s->options.source_slot(s->options.ctx, actor.id, &slot)) return false;
  qa_compat_qc_machine *m = s->options.machine;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(m->entities, slot);
  double move = qa_compat_qc_words_float(w, fld(s, "movetype"));
  const char *kind;
  if (move == 0 || move == 8) kind = "stationary";
  else if (move == 3 || move == 4) kind = "step";
  else if (move == 5) kind = "fly";
  else if (move == 6) kind = "toss";
  else if (move == 7) kind = "push";
  else if (move == 9) kind = "fly-missile";
  else if (move == 10) kind = "bounce";
  else if (move == 11 && s->options.rerelease) kind = "bounce";
  else { qa_compat_qc_machine_fail(m, "Unsupported id1 movetype %g", move); }
  memset(out, 0, sizeof *out);
  out->actor = actor; out->kind = kind; out->velocity = body.velocity;
  out->angular_velocity = qa_compat_qc_words_vector(w, fld(s, "avelocity"));
  out->gravity = qa_compat_qc_program_field_named(m->program, "gravity") ? (qa_compat_qc_words_float(w, fld(s, "gravity")) != 0 ? qa_compat_qc_words_float(w, fld(s, "gravity")) : 1) : 1;
  out->gravity_vector = (qa_vec3){0, 0, -1}; out->clip_mask = 3;
  qa_compat_qc_shared_solid col;
  if (qa_compat_qc_actor_state_collision(s, actor, &col) && col.has_owner) { out->has_owner = true; out->owner = col.owner; }
  return true;
}
qa_compat_qc_shared_physics_flags qa_compat_qc_actor_state_flags(qa_compat_qc_actor_state *s, qa_owned_actor actor) {
  qa_compat_qc_shared_physics_flags r; memset(&r, 0, sizeof r);
  int64_t slot; if (!s->options.source_slot(s->options.ctx, actor.id, &slot)) return r;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->options.machine->entities, slot);
  int64_t flags = (int64_t)trunc(qa_compat_qc_words_float(w, fld(s, "flags")));
  r.has_fly = true; r.fly = (flags & 1) != 0;
  r.has_swim = true; r.swim = (flags & 2) != 0;
  r.has_partial_ground = true; r.partial_ground = (flags & 1024) != 0;
  r.has_player = true; r.player = s->options.is_client(s->options.ctx, actor.id);
  r.has_water_level = true; r.water_level = qa_compat_qc_words_float(w, fld(s, "waterlevel"));
  r.has_water_type = true; r.water_type = qa_compat_qc_words_float(w, fld(s, "watertype"));
  r.has_dead = true; r.dead = qa_compat_qc_words_float(w, fld(s, "health")) <= 0;
  return r;
}
void qa_compat_qc_actor_state_write_flags(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_compat_qc_shared_physics_flags ch) {
  int64_t slot; if (!s->options.source_slot(s->options.ctx, actor.id, &slot)) return;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->options.machine->entities, slot);
  int64_t flags = (int64_t)trunc(qa_compat_qc_words_float(w, fld(s, "flags")));
  struct { bool has, v; int64_t bit; } map[3] = {{ch.has_fly, ch.fly, 1}, {ch.has_swim, ch.swim, 2}, {ch.has_partial_ground, ch.partial_ground, 1024}};
  for (int i = 0; i < 3; i++) if (map[i].has) flags = map[i].v ? flags | map[i].bit : flags & ~map[i].bit;
  qa_compat_qc_words_set_float(w, fld(s, "flags"), (double)flags);
  if (ch.has_water_level) qa_compat_qc_words_set_float(w, fld(s, "waterlevel"), ch.water_level);
  if (ch.has_water_type) qa_compat_qc_words_set_float(w, fld(s, "watertype"), ch.water_type);
}
void qa_compat_qc_actor_state_write_angular_velocity(qa_compat_qc_actor_state *s, qa_owned_actor actor, qa_vec3 v) {
  int64_t slot; if (s->options.source_slot(s->options.ctx, actor.id, &slot))
    qa_compat_qc_words_set_vector(qa_compat_qc_entity_memory_at(s->options.machine->entities, slot), fld(s, "avelocity"), v);
}
static int64_t bfield(qa_compat_qc_body_binding_state *st, const char *n) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(st->program, n);
  if (!d) qa_range_error("Missing QC body field %s", n);
  return d->offset;
}
static qa_body_state bread(void *ctx) {
  qa_compat_qc_body_binding_state *st = ctx; qa_compat_qc_words *f = st->fields;
  qa_body_state r;
  r.origin = qa_compat_qc_words_vector(f, st->origin); r.angles = qa_compat_qc_words_vector(f, st->angles);
  r.velocity = qa_compat_qc_words_vector(f, st->velocity);
  r.bounds.min = qa_compat_qc_words_vector(f, st->mins); r.bounds.max = qa_compat_qc_words_vector(f, st->maxs);
  r.ground = NULL;
  if (((int64_t)trunc(qa_compat_qc_words_float(f, st->flags)) & 512) != 0) {
    if (st->references.actor(st->references.ctx, qa_compat_qc_words_int(f, st->ground), &st->ground_actor)) r.ground = &st->ground_actor;
  }
  return r;
}
static void bwrite(void *ctx, qa_body_state state) {
  qa_compat_qc_body_binding_state *st = ctx; qa_compat_qc_words *f = st->fields;
  qa_compat_qc_words_set_vector(f, st->origin, state.origin); qa_compat_qc_words_set_vector(f, st->angles, state.angles);
  qa_compat_qc_words_set_vector(f, st->velocity, state.velocity);
  qa_compat_qc_words_set_vector(f, st->mins, state.bounds.min); qa_compat_qc_words_set_vector(f, st->maxs, state.bounds.max);
  int64_t cur = (int64_t)trunc(qa_compat_qc_words_float(f, st->flags));
  qa_compat_qc_words_set_float(f, st->flags, (double)(state.ground == NULL ? cur & ~512 : cur | 512));
  if (state.ground) qa_compat_qc_words_set_int(f, st->ground, (double)st->references.reference(st->references.ctx, *state.ground));
}
static void blinked(void *ctx, qa_linked_body body) {
  qa_compat_qc_body_binding_state *st = ctx;
  qa_compat_qc_words_set_vector(st->fields, bfield(st, "absmin"), body.absolute_bounds.min);
  qa_compat_qc_words_set_vector(st->fields, bfield(st, "absmax"), body.absolute_bounds.max);
}
qa_body_state_binding qa_compat_qc_create_body_binding(qa_compat_qc_program *p, qa_compat_qc_entity_memory *e, int64_t slot, qa_compat_qc_body_references refs, qa_compat_qc_body_binding_state *st) {
  st->program = p; st->references = refs; st->fields = qa_compat_qc_entity_memory_at(e, slot);
  st->origin = bfield(st, "origin"); st->angles = bfield(st, "angles"); st->velocity = bfield(st, "velocity");
  st->mins = bfield(st, "mins"); st->maxs = bfield(st, "maxs"); st->flags = bfield(st, "flags"); st->ground = bfield(st, "groundentity");
  return (qa_body_state_binding){st, bread, bwrite, blinked};
}
