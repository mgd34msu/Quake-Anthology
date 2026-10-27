/* Port of src/compat/qc/world-host.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/world_host.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
qa_bounds qa_compat_qc_link_bounds(qa_body_state s, double flags, const qa_numeric_operations *n) {
  bool item = ((int64_t)trunc(flags) & 256) != 0; double xy = item ? 15 : 1, z = item ? 0 : 1; void *c = n->ctx;
  return (qa_bounds){
    {n->subtract(c,n->add(c,s.origin.x,s.bounds.min.x),xy), n->subtract(c,n->add(c,s.origin.y,s.bounds.min.y),xy), n->subtract(c,n->add(c,s.origin.z,s.bounds.min.z),z)},
    {n->add(c,n->add(c,s.origin.x,s.bounds.max.x),xy), n->add(c,n->add(c,s.origin.y,s.bounds.max.y),xy), n->add(c,n->add(c,s.origin.z,s.bounds.max.z),z)}};
}
bool qa_compat_qc_world_host_is_free_entity(qa_compat_qc_world_host *h, int64_t slot) { return qa_compat_qc_actor_bindings_is_free_entity(&h->bindings_state, slot); }
int64_t qa_compat_qc_world_host_reference(qa_compat_qc_world_host *h, qa_actor_id actor) {
  if (!qa_session_actor_registry_is_live(h->options.actors, actor)) qa_compat_qc_program_error(h->options.program->source, "cannot encode a stale actor as a QC entity");
  qa_actor_source_result src = qa_session_actor_registry_source_of(h->options.actors, actor);
  if (src.has_value && strcmp(src.provider, h->options.slots_options.provider) == 0) return qa_compat_qc_entity_memory_reference(h->options.entities, src.slot);
  return h->options.foreign_reference(h->options.ctx, actor);
}
static int64_t body_ref(void *ctx, qa_actor_id a) { return qa_compat_qc_world_host_reference(ctx, a); }
static bool body_actor(void *ctx, int64_t ref, qa_actor_id *out) {
  qa_compat_qc_world_host *h = ctx; int64_t slot = qa_compat_qc_entity_memory_slot(h->options.entities, ref);
  qa_owned_actor owned;
  if (h->options.foreign_actor && h->options.foreign_actor(h->options.ctx, slot, &owned)) { *out = owned.id; return true; }
  if (qa_source_actor_slots_at(h->options.slots, slot, &owned)) { *out = owned.id; return true; }
  return false;
}
qa_owned_actor qa_compat_qc_world_host_actor(qa_compat_qc_world_host *h, int64_t slot) {
  if (qa_compat_qc_world_host_is_free_entity(h, slot)) qa_compat_qc_program_error(h->options.program->source, "world builtin references a free source edict");
  qa_owned_actor borrowed;
  if (h->options.foreign_actor && h->options.foreign_actor(h->options.ctx, slot, &borrowed)) { qa_session_actor_registry_assert_owned(h->options.actors, borrowed); return borrowed; }
  qa_owned_actor actor;
  if (!qa_source_actor_slots_at(h->options.slots, slot, &actor)) actor = qa_source_actor_slots_bind_existing(h->options.slots, slot, "quakec:edict");
  if (!qa_shared_body_table_read(h->options.bodies, actor.id).has_value) {
    qa_compat_qc_body_references refs = {h, body_ref, body_actor};
    qa_body_state_binding b = qa_compat_qc_create_body_binding(h->options.program, h->options.entities, slot, refs, &h->body_storage[slot]);
    qa_shared_body_table_bind(h->options.bodies, actor, b);
    if (h->options.admit) h->options.admit(h->options.ctx, actor, slot);
  }
  return actor;
}
void qa_compat_qc_world_host_link(qa_compat_qc_world_host *h, int64_t slot) {
  if (slot == 0 || qa_compat_qc_world_host_is_free_entity(h, slot)) return;
  qa_shared_body_table_link(h->options.bodies, qa_compat_qc_world_host_actor(h, slot), NULL);
}
static qa_owned_actor sp_actor(void *ctx, int64_t slot) { return qa_compat_qc_world_host_actor(ctx, slot); }
static int64_t sp_ref(void *ctx, qa_actor_id a) { return qa_compat_qc_world_host_reference(ctx, a); }
static bool sp_free(void *ctx, int64_t slot) { return qa_compat_qc_world_host_is_free_entity(ctx, slot); }
static void sp_link(void *ctx, int64_t slot) { qa_compat_qc_world_host_link(ctx, slot); }
static bool sp_model(void *ctx, const char *name, qa_compat_qc_spatial_model *out) { qa_compat_qc_world_host *h=ctx; return h->options.model(h->options.ctx, name, out); }
typedef struct { qa_compat_qc_world_host *h; qa_compat_qc_builtin inner; } Wrap;
static void wrap_fn(void *ctx, qa_compat_qc_machine *m) {
  Wrap *w = ctx;
  if (m->entities != w->h->options.entities || m->program != w->h->options.program) { qa_compat_qc_machine_fail(m, "world builtin belongs to another QC machine"); }
  w->inner.fn(w->inner.ctx, m);
}
typedef struct { qa_compat_qc_world_host *h; qa_compat_qc_builtin spawn; } SpawnWrap;
typedef struct { Wrap wraps[7]; SpawnWrap spawn; } WrapStorage;
static void spawn_fn(void *ctx, qa_compat_qc_machine *m) {
  SpawnWrap *w = ctx;
  if (m->entities != w->h->options.entities || m->program != w->h->options.program) { qa_compat_qc_machine_fail(m, "world builtin belongs to another QC machine"); }
  w->spawn.fn(w->spawn.ctx, m);
  qa_compat_qc_world_host_actor(w->h, qa_compat_qc_entity_memory_slot(w->h->options.entities, qa_compat_qc_words_int(&m->globals, 1)));
}
bool qa_compat_qc_world_host_init(qa_compat_qc_world_host *h, qa_compat_qc_world_host_options o) {
  memset(h, 0, sizeof *h); h->options = o;
  h->body_storage = calloc((size_t)o.entities->capacity, sizeof *h->body_storage);
  if (!h->body_storage) return false;
  WrapStorage *ws = calloc(1, sizeof *ws); if (!ws) { free(h->body_storage); return false; } h->wrap_storage = ws;
  qa_compat_qc_actor_bindings bindings = qa_compat_qc_create_actor_bindings(o.entities, o.actors, o.slots, o.slots_options, &h->bindings_state);
  h->spatial = (qa_compat_qc_spatial_world){h, o.program, o.entities, o.scene, o.numeric, sp_model, sp_actor, sp_ref, sp_free, NULL, sp_link};
  qa_compat_qc_spatial_bindings spatial = qa_compat_qc_create_spatial_bindings(&h->spatial);
  size_t n = 0;
  /* spawn wrapped to admit actor; remove as-is (already machine-checked) */
  ws->spawn.h = h; ws->spawn.spawn = bindings.host_entries[0].fn;
  h->host_entries[n++] = (qa_compat_qc_host_binding){"spawn", {&ws->spawn, spawn_fn}};
  h->host_entries[n++] = bindings.host_entries[1];
  for (size_t i = 0; i < spatial.count; i++) {
    ws->wraps[i].h = h; ws->wraps[i].inner = spatial.entries[i].fn;
    h->host_entries[n++] = (qa_compat_qc_host_binding){spatial.entries[i].name, {&ws->wraps[i], wrap_fn}};
  }
  h->host_count = n;
  return true;
}
void qa_compat_qc_world_host_destroy(qa_compat_qc_world_host *h) { free(h->body_storage); free(h->wrap_storage); memset(h, 0, sizeof *h); }
