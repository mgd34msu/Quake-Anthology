/* Port of src/compat/qc/client-host.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/client_host.h"
#include <math.h>
#include <string.h>
#include "qa/world/gameplay/q1_aim.h"
static qa_q1_visibility_client client_at(void *ctx, int64_t slot) {
  qa_compat_qc_client_host *h = ctx; qa_compat_qc_world_host *w = h->world;
  qa_compat_qc_words *words = qa_compat_qc_entity_memory_at(w->options.entities, slot);
  qa_owned_actor actor; bool has = qa_source_actor_slots_at(w->options.slots, slot, &actor);
  qa_q1_visibility_client r; memset(&r, 0, sizeof r);
  r.has_actor = has; if (has) r.actor = actor.id;
  r.free = qa_compat_qc_world_host_is_free_entity(w, slot) || !has || !qa_session_actor_registry_is_live(w->options.actors, actor.id);
  r.health = qa_compat_qc_words_float(words, h->health);
  r.notarget = ((int64_t)trunc(qa_compat_qc_words_float(words, h->flags)) & 128) != 0;
  r.origin = qa_compat_qc_words_vector(words, h->origin);
  r.view_offset = qa_compat_qc_words_vector(words, h->view_ofs);
  return r;
}
static void b_checkclient(void *ctx, qa_compat_qc_machine *m) {
  qa_compat_qc_client_host *h = ctx; qa_compat_qc_world_host *w = h->world;
  if (m->program != w->options.program || m->entities != w->options.entities) { qa_compat_qc_machine_fail(m, "client builtin belongs to another QC machine"); }
  qa_compat_qc_words *self = qa_compat_qc_entity_memory_at(w->options.entities, qa_compat_qc_entity_memory_slot(w->options.entities, qa_compat_qc_words_int(&m->globals, qa_compat_qc_machine_global_offset(m, "self"))));
  qa_actor_id actor;
  bool has = qa_q1_client_visibility_check(h->visibility, (qa_q1_client_eye){qa_compat_qc_words_vector(self, h->origin), qa_compat_qc_words_vector(self, h->view_ofs)}, h->services.server_time(h->services.ctx), *m->numeric, &actor);
  qa_compat_qc_machine_return_int(m, has ? (double)qa_compat_qc_world_host_reference(w, actor) : 0);
}
static int64_t efield(qa_compat_qc_program *p, const char *n) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(p, n);
  if (!d) qa_compat_qc_program_error(p->source, "missing entity field %s", n);
  return d->offset;
}
bool qa_compat_qc_client_host_init(qa_compat_qc_client_host *h, qa_compat_qc_world_host *w, qa_compat_qc_client_host_services services) {
  memset(h, 0, sizeof *h); h->world = w; h->services = services;
  if (services.max_clients < 1 || services.max_clients >= w->options.entities->count) qa_compat_qc_program_error(w->options.program->source, "invalid reserved QC client range");
  for (int64_t slot = 0; slot <= services.max_clients; slot++) {
    qa_owned_actor a; if (!qa_source_actor_slots_at(w->options.slots, slot, &a)) qa_compat_qc_program_error(w->options.program->source, "missing reserved QC client/world slot %lld", (long long)slot);
  }
  h->origin = efield(w->options.program, "origin"); h->view_ofs = efield(w->options.program, "view_ofs");
  h->health = efield(w->options.program, "health"); h->flags = efield(w->options.program, "flags");
  h->visibility = qa_q1_client_visibility_create(services.max_clients, h, client_at, services.scene);
  if (!h->visibility) return false;
  h->host_count = 1; h->host_entries[0] = (qa_compat_qc_host_binding){"checkclient", {h, b_checkclient}};
  return true;
}
void qa_compat_qc_client_host_destroy(qa_compat_qc_client_host *h) { qa_q1_client_visibility_destroy(h->visibility); memset(h, 0, sizeof *h); }
typedef struct { qa_compat_qc_aim_state *s; qa_compat_qc_machine *m; int64_t reference; qa_actor_id self; const qa_compat_qc_aim_target_ref *targets; size_t count; } AimCtx;
static bool actor_eq(qa_actor_id a, qa_actor_id b) { return a.slot == b.slot && a.generation == b.generation && a.token == b.token && a.session.token == b.session.token; }
static qa_body_state_result aim_body(void *ctx, qa_actor_id a) { return qa_shared_body_table_read(((AimCtx *)ctx)->s->world->options.bodies, a); }
static bool aim_eligible(void *ctx, qa_actor_id target) {
  AimCtx *c = ctx; const qa_compat_qc_aim_target_ref *projected = NULL;
  for (size_t i = 0; i < c->count; i++) if (c->targets[i].actor.slot == target.slot) { projected = &c->targets[i]; break; }
  if (!projected || !actor_eq(projected->actor, target) || actor_eq(target, c->self)) return false;
  if (qa_compat_qc_machine_entity_float(c->m, projected->reference, "takedamage") != 2) return false;
  return c->s->teamplay(c->s->ctx) == 0 || qa_compat_qc_machine_entity_float(c->m, c->reference, "team") <= 0
      || qa_compat_qc_machine_entity_float(c->m, c->reference, "team") != qa_compat_qc_machine_entity_float(c->m, projected->reference, "team");
}
static bool aim_trace(void *ctx, qa_vec3 start, qa_vec3 end, qa_actor_id *out) {
  AimCtx *c = ctx; qa_compat_qc_world_host *w = c->s->world;
  qa_trace_query q; memset(&q, 0, sizeof q);
  q.start = start; q.end = end; q.shape.kind = "point"; q.policy.kind = "q1"; q.policy.move = "normal";
  q.numeric = w->options.numeric; q.pass_actor = &c->self;
  qa_trace_result t = w->options.scene.trace(w->options.scene.ctx, q);
  if (t.hit.kind != QA_TRACE_HIT_ACTOR) return false;
  *out = t.hit.actor; return true;
}
static void b_aim(void *ctx, qa_compat_qc_machine *m) {
  qa_compat_qc_aim_state *s = ctx; qa_compat_qc_world_host *w = s->world;
  if (m->program != w->options.program || m->entities != w->options.entities) { qa_compat_qc_machine_fail(m, "aim builtin belongs to another QC machine"); }
  int64_t ref = qa_compat_qc_machine_arg_int(m, 0);
  qa_owned_actor actor = qa_compat_qc_world_host_actor(w, qa_compat_qc_entity_memory_slot(w->options.entities, ref));
  qa_vec3 forward = qa_compat_qc_words_vector(&m->globals, qa_compat_qc_machine_global_offset(m, "v_forward"));
  if (s->no_aim && s->no_aim(s->ctx, actor.id)) { qa_compat_qc_machine_return_vector(m, forward); return; }
  static const char *const fields[2] = {"team", "takedamage"};
  for (int i = 0; i < 2; i++) { const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(w->options.program, fields[i]);
    if (!d || strcmp(d->type, "float") != 0) { qa_compat_qc_machine_fail(m, "Missing QC aim float field %s", fields[i]); } }
  qa_body_state_result body = qa_shared_body_table_read(w->options.bodies, actor.id);
  if (!body.has_value) { qa_compat_qc_machine_fail(m, "QC aim actor has no body"); }
  size_t count = 0; const qa_compat_qc_aim_target_ref *targets = s->targets(s->ctx, &count);
  qa_actor_id ids[256]; size_t n = count < 256 ? count : 256;
  for (size_t i = 0; i < n; i++) ids[i] = targets[i].actor;
  AimCtx c = {s, m, ref, actor.id, targets, count};
  qa_q1_aim_targets at = {ids, n, &c, aim_body, aim_eligible, aim_trace};
  qa_compat_qc_machine_return_vector(m, qa_aim_q1(body.value.origin, forward, s->aim_threshold(s->ctx), at));
}
qa_compat_qc_builtin qa_compat_qc_create_aim_binding(qa_compat_qc_world_host *world, qa_compat_qc_aim_state *st) {
  st->world = world; return (qa_compat_qc_builtin){st, b_aim};
}
