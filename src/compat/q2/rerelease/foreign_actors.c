/* Port of src/compat/q2/rerelease/foreign-actors.ts. GPL-2.0-or-later.
 * RemovedNativeDamage (a TS throw caught inside damageNative) becomes
 * the removed flag + processor restore, per the classic precedent. */
#include "qa/compat/q2/rerelease/foreign_actors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/combat_binding.h" /* qa_compat_q2_lower_native_combat_arguments */
#include "qa/compat/q2/native_damage.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/compat/q2/rerelease/source_state.h"
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/world/actors/body.h"
#include "qa/foundation/js.h"
#include "qa/network/common/js_number.h"

typedef struct projection projection_t;
struct projection {
  qa_owned_actor actor; qa_raw_entity_view view; double generation;
  bool releasing, syncing;
};
typedef struct {
  qa_damage_request request; qa_actor_id attacker, inflictor;
  bool entered, has_stack, observing; uint64_t stack;
} frame_t;
typedef struct {
  frame_t *frame; qa_actor_id actor;
  void *ctx; bool (*committed)(void *ctx);
} publication_t;
typedef struct {
  qa_owned_actor actor;
  qa_armor_stage_intercept_fn regular, powered; void *regular_ctx, *powered_ctx;
  void *armor_ctx; qa_armor_state (*armor)(void *ctx);
} armor_binding_t;
typedef struct stage_ctx {
  qa_compat_q2_rerelease_foreign_actors *f; qa_raw_entity_view view;
  void *armor_ctx; qa_armor_state (*armor)(void *ctx); qa_protection_channel channel;
  struct stage_ctx *next;
} stage_ctx_t;
struct qa_compat_q2_rerelease_foreign_actors {
  qa_compat_q2_rerelease_q2_guest_host *host;
  qa_compat_q2_rerelease_native_entries entries;
  qa_compat_q2_rerelease_foreign_damage_services services;
  qa_compat_q2_rerelease_deferred_damage *deferred;
  projection_t **actors; size_t actor_count, actor_cap;
  frame_t *frames; size_t frame_count, frame_cap;
  publication_t *publications; size_t publication_count, publication_cap;
  armor_binding_t *bindings; size_t binding_count, binding_cap;
  stage_ctx_t *stages;
  uint64_t hook_handles[4]; bool hook_is_inline[4]; size_t hook_count;
  int power_bypass; /* 0 null, 1 pending, 2 stack */ uint64_t bypass_stack;
  qa_guest_address image;
  uint64_t remove_entry, remove_continuation;
  qa_compat_q2_guest_call_signature damage_signature, power_signature;
  qa_guest_value_layout sig_storage[2][16];
  bool restoring; qa_compat_q2_rerelease_projection_save *restoring_saved; size_t restoring_count;
  char restoring_domain[32];
  bool closed, removed; qa_damage_request removed_request;
  qa_compat_q2_rerelease_projection_save *save_scratch; size_t save_cap;
};
static qa_compat_q2_rerelease_guest_module *famod(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_q2_guest_host_get_module(f->host);
}
static qa_compat_q2_memory *fmem(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_guest_module_get_memory(famod(f));
}
static qa_q2_foundation_host *fengine(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_q2_guest_host_get_options(f->host)->engine;
}
static qa_compat_q2_runner *frunner(qa_compat_q2_rerelease_foreign_actors *f) {
  return qa_compat_q2_rerelease_guest_module_get_options(famod(f))->runner;
}
static uint64_t rsp_of(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_runner *r = frunner(f);
  return r->read_rsp(r->ctx, 64);
}
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Q2 rerelease foreign actors out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Q2 rerelease foreign actors out of memory"); return q; }
static qa_compat_q2_rerelease_source_edict *xsource(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view) {
  qa_compat_q2_rerelease_source_edict *e = qa_compat_q2_rerelease_source_edict_create(view, famod(f));
  if (!e) qa_range_error("Q2 rerelease foreign actors out of memory");
  return e;
}
static bool field_pointer(qa_compat_q2_rerelease_foreign_actors *f, const qa_compat_q2_native_combat_call *call,
                          qa_compat_q2_guest_call_signature signature, qa_guest_address *out) {
  qa_compat_q2_runner *r = frunner(f);
  qa_guest_call_value value = r->read_combat_field(r->ctx, call, signature, "target");
  return qa_compat_q2_guest_pointer(&value, 1, 0, out);
}
static qa_vec3 vector_at(qa_compat_q2_rerelease_foreign_actors *f, qa_guest_address address) {
  qa_compat_q2_memory *m = fmem(f);
  double x = qa_compat_q2_mem_read_f32(m, address);
  double y = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 4));
  double z = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 8));
  return (qa_vec3){x, y, z};
}
static qa_actor_id actor_at(qa_compat_q2_rerelease_foreign_actors *f, qa_guest_address address) {
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, table.from_pointer(table.ctx, address), &actor))
    qa_range_error("Native damage references a stale edict");
  return actor.id;
}
static projection_t *find_by_actor(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id id) {
  for (size_t i = 0; i < f->actor_count; i++) if (qa_same_actor(f->actors[i]->actor.id, id)) return f->actors[i];
  return NULL;
}
static projection_t *find_by_slot(qa_compat_q2_rerelease_foreign_actors *f, double slot) {
  for (size_t i = 0; i < f->actor_count; i++) if (f->actors[i]->view.slot == slot) return f->actors[i];
  return NULL;
}
static void remove_projection(qa_compat_q2_rerelease_foreign_actors *f, projection_t *entry) {
  for (size_t i = 0; i < f->actor_count; i++) {
    if (f->actors[i] != entry) continue;
    memmove(&f->actors[i], &f->actors[i + 1], (f->actor_count - i - 1) * sizeof *f->actors);
    f->actor_count--;
    free(entry);
    return;
  }
}
static projection_t *add_projection(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor,
                                    qa_raw_entity_view view, double generation) {
  projection_t *entry = xmalloc(sizeof *entry);
  memset(entry, 0, sizeof *entry);
  entry->actor = actor; entry->view = view; entry->generation = generation;
  if (f->actor_count == f->actor_cap) { f->actor_cap = f->actor_cap ? f->actor_cap * 2 : 8; f->actors = xrealloc(f->actors, f->actor_cap * sizeof *f->actors); }
  f->actors[f->actor_count++] = entry;
  return entry;
}
static armor_binding_t *find_binding(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor) {
  for (size_t i = 0; i < f->binding_count; i++) if (qa_same_owned_actor(f->bindings[i].actor, actor)) return &f->bindings[i];
  return NULL;
}
static frame_t *last_frame(qa_compat_q2_rerelease_foreign_actors *f) {
  return f->frame_count ? &f->frames[f->frame_count - 1] : NULL;
}
static bool publishes_inventory(qa_compat_q2_rerelease_foreign_actors *f, frame_t *frame) {
  if (last_frame(f) != frame) return true;
  for (size_t i = f->publication_count; i > 0; i--) {
    publication_t *scope = &f->publications[i - 1];
    if (scope->frame == frame && qa_same_actor(scope->actor, frame->request.target))
      return !scope->committed || scope->committed(scope->ctx);
  }
  return true;
}
static bool intercept_native_damage(qa_compat_q2_rerelease_foreign_actors *f) {
  frame_t *frame = last_frame(f);
  if (frame && frame->has_stack && frame->stack == rsp_of(f)) return false;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address address;
  if (!field_pointer(f, &profile->calls.damage, f->damage_signature, &address)) return false;
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_raw_entity_view view = table.from_pointer(table.ctx, address);
  if (find_by_slot(f, view.slot)) return false;
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, view, &actor)) return false;
  qa_q2_foundation_host *engine = fengine(f);
  return qa_mod_operation_active(qa_gameplay_authority_damage_operation(engine->combat)) || find_binding(f, actor) != NULL;
}
void *qa_compat_q2_rerelease_foreign_actors_with_inventory_publication(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor,
                                                                        void *ctx, bool (*committed)(void *ctx),
                                                                        void *(*operation)(void *ctx)) {
  if (f->publication_count == f->publication_cap) {
    f->publication_cap = f->publication_cap ? f->publication_cap * 2 : 8;
    f->publications = xrealloc(f->publications, f->publication_cap * sizeof *f->publications);
  }
  f->publications[f->publication_count++] = (publication_t){last_frame(f), actor, ctx, committed};
  void *result = operation(ctx);
  f->publication_count--;
  return result;
}
bool qa_compat_q2_rerelease_foreign_actors_observing_damage(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor) {
  frame_t *frame = last_frame(f);
  return frame && frame->observing && qa_same_actor(frame->request.target, actor);
}
/* TS engaged: true when the slot's actor is owned by a projection or a restore is rebinding slots, so views must go through lookup. */
bool qa_compat_q2_rerelease_foreign_actors_engaged(qa_compat_q2_rerelease_foreign_actors *f, double slot) {
  return f->restoring || find_by_slot(f, slot) != NULL;
}
qa_compat_q2_rerelease_lookup qa_compat_q2_rerelease_foreign_actors_lookup(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view,
                                                                           qa_owned_actor *out) {
  projection_t *entry = find_by_slot(f, view.slot);
  if (!entry && f->restoring) {
    const qa_saved_actor_id *saved = NULL;
    for (size_t i = 0; i < f->restoring_count; i++) if (f->restoring_saved[i].slot == view.slot) saved = &f->restoring_saved[i].actor;
    if (saved) {
      qa_session_actor_registry *actors = fengine(f)->actors;
      qa_actor_id id = qa_session_actor_registry_reference_saved(actors, *saved, f->restoring_domain);
      qa_owned_actor actor;
      if (!qa_session_actor_registry_resolve_owned(actors, id, &actor))
        qa_range_error("Saved foreign projection actor is no longer live");
      qa_compat_q2_rerelease_source_edict *source = xsource(f, view);
      const qa_compat_q2_memory * m = fmem(f);
      qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(source, "shared.inuse");
      bool inuse = qa_compat_q2_mem_read_u8(m, a) != 0;
      double generation = qa_compat_q2_rerelease_source_edict_generation(source);
      qa_compat_q2_rerelease_source_edict_destroy(source);
      if (!inuse) return QA_COMPAT_Q2_RR_LOOKUP_NULL;
      entry = add_projection(f, actor, view, generation);
    }
  }
  if (!entry) return QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED;
  qa_compat_q2_rerelease_source_edict *source = xsource(f, view);
  double generation = qa_compat_q2_rerelease_source_edict_generation(source);
  qa_compat_q2_rerelease_source_edict_destroy(source);
  if (entry->generation != generation || entry->view.address.byte_offset != view.address.byte_offset) {
    remove_projection(f, entry);
    return QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED;
  }
  if (entry->releasing || !qa_session_actor_registry_is_live(fengine(f)->actors, entry->actor.id))
    return QA_COMPAT_Q2_RR_LOOKUP_NULL;
  if (out) *out = entry->actor;
  return QA_COMPAT_Q2_RR_LOOKUP_ACTOR;
}
static void release_projection(qa_compat_q2_rerelease_foreign_actors *f, projection_t *entry) {
  if (entry->releasing) return;
  entry->releasing = true;
  qa_compat_q2_rerelease_source_edict *source = xsource(f, entry->view);
  double generation = qa_compat_q2_rerelease_source_edict_generation(source);
  qa_compat_q2_rerelease_source_edict_destroy(source);
  if (generation == entry->generation) {
    qa_guest_call_value arg = qa_compat_q2_rerelease_guest_pointer(true, entry->view.address);
    qa_raw_entity_view view = entry->view;
    (void)qa_compat_q2_rerelease_guest_module_invoke(famod(f), f->entries.freed,
                                                     qa_compat_q2_rerelease_get_free_signature(), &arg, 1, &view, NULL);
  }
  /* The guest free may have replaced the maps; remove only if present. */
  remove_projection(f, entry);
}
void qa_compat_q2_rerelease_foreign_actors_released(qa_compat_q2_rerelease_foreign_actors *f, qa_owned_actor actor) {
  qa_compat_q2_rerelease_deferred_damage_release(f->deferred, actor);
  projection_t *entry = find_by_actor(f, actor.id);
  if (entry) release_projection(f, entry);
}
static bool sync_actor(void *v, qa_guest_address address, qa_actor_id *out) {
  *out = actor_at(v, address);
  return true;
}
static qa_guest_address sync_address(void *v, qa_actor_id actor) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  return qa_compat_q2_rerelease_q2_guest_host_address_for_actor(f->host, actor);
}
static void sync_projection(qa_compat_q2_rerelease_foreign_actors *f, projection_t *entry) {
  if (entry->releasing || entry->syncing) return;
  qa_q2_foundation_host *engine = fengine(f);
  qa_compat_q2_memory *memory = fmem(f);
  qa_body_state_result body = qa_shared_body_table_read(engine->bodies, entry->actor.id);
  qa_combat_state_result combat = qa_gameplay_authority_read(engine->combat, entry->actor.id);
  if (!body.has_value) qa_range_error("Foreign native projection requires an existing body binding");
  qa_compat_q2_rerelease_source_edict *source = xsource(f, entry->view);
  if (qa_compat_q2_rerelease_source_edict_generation(source) != entry->generation) {
    qa_actor_id id = entry->actor.id;
    qa_compat_q2_rerelease_source_edict_destroy(source);
    remove_projection(f, entry);
    (void)qa_compat_q2_rerelease_foreign_actors_address(f, id);
    return;
  }
  entry->syncing = true;
  qa_compat_q2_rerelease_actor_addresses addresses = {f, sync_actor, sync_address};
  qa_body_state_binding binding = qa_compat_q2_rerelease_source_edict_body(source, addresses);
  binding.write(binding.ctx, body.value);
  qa_compat_q2_rerelease_source_edict_set_health(source, combat.has_value ? combat.value.health : 0);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_rerelease_source_edict_at(source, "mass"),
                             combat.has_value ? qa_js_to_int32(combat.value.mass) : 0);
  qa_compat_q2_mem_write_u8(memory, qa_compat_q2_rerelease_source_edict_at(source, "takedamage"),
                            combat.has_value && combat.value.can_take_damage ? 1 : 0);
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_rerelease_source_edict_at(source, "movetype"), 0);
  qa_compat_q2_mem_write_u32(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.svflags"), 1);
  qa_guest_address null_address; memset(&null_address, 0, sizeof null_address);
  qa_compat_q2_mem_write_pointer(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.client"), false, null_address);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(source, "classname");
  qa_guest_address v = qa_compat_q2_rerelease_core_imports_string(qa_compat_q2_rerelease_q2_guest_host_get_core(f->host), "foreign_actor");
  qa_compat_q2_mem_write_pointer(memory, a, true, v);
  qa_compat_q2_mem_write_u8(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.solid"), 2);
  qa_linked_body_result linked = qa_shared_body_table_linked(engine->bodies, entry->actor.id);
  qa_vec3 absmin = linked.has_value ? linked.value.absolute_bounds.min
                                     : (qa_vec3){body.value.origin.x + body.value.bounds.min.x,
                                                 body.value.origin.y + body.value.bounds.min.y,
                                                 body.value.origin.z + body.value.bounds.min.z};
  qa_vec3 absmax = linked.has_value ? linked.value.absolute_bounds.max
                                     : (qa_vec3){body.value.origin.x + body.value.bounds.max.x,
                                                 body.value.origin.y + body.value.bounds.max.y,
                                                 body.value.origin.z + body.value.bounds.max.z};
  qa_compat_q2_rerelease_source_edict_write_vector(source, "shared.absmin", absmin);
  qa_compat_q2_rerelease_source_edict_write_vector(source, "shared.absmax", absmax);
  qa_compat_q2_mem_write_u8(memory, qa_compat_q2_rerelease_source_edict_at(source, "shared.linked"), linked.has_value ? 1 : 0);
  entry->syncing = false;
  qa_compat_q2_rerelease_source_edict_destroy(source);
}
qa_guest_address qa_compat_q2_rerelease_foreign_actors_address(qa_compat_q2_rerelease_foreign_actors *f, qa_actor_id actor) {
  if (f->closed) qa_range_error("Foreign native projections are closed");
  qa_session_actor_registry *actors = fengine(f)->actors;
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(actors, actor, &owned))
    qa_range_error("Cannot project a stale foreign actor");
  projection_t *entry = find_by_actor(f, actor);
  if (entry) {
    qa_compat_q2_rerelease_source_edict *source = xsource(f, entry->view);
    double generation = qa_compat_q2_rerelease_source_edict_generation(source);
    qa_compat_q2_rerelease_source_edict_destroy(source);
    if (generation != entry->generation) { remove_projection(f, entry); entry = NULL; }
  }
  if (!entry) {
    qa_guest_call_result spawned = qa_compat_q2_rerelease_guest_module_invoke(
        famod(f), f->entries.spawn, qa_compat_q2_rerelease_get_spawn_signature(), NULL, 0, NULL, NULL);
    qa_guest_address address;
    if (!qa_compat_q2_rerelease_result_pointer(spawned, &address)) qa_range_error("Native G_Spawn returned null");
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
    qa_raw_entity_view view = table.from_pointer(table.ctx, address);
    qa_compat_q2_rerelease_source_edict *source = xsource(f, view);
    double generation = qa_compat_q2_rerelease_source_edict_generation(source);
    qa_compat_q2_rerelease_source_edict_destroy(source);
    entry = add_projection(f, owned, view, generation);
    qa_owned_actor phantom;
    if (qa_session_actor_registry_at_source(actors, fmem(f)->module.id, (int64_t)view.slot, &phantom) &&
        !qa_same_owned_actor(phantom, owned))
      qa_session_actor_registry_release(actors, phantom);
  }
  qa_guest_address result = entry->view.address;
  sync_projection(f, entry);
  /* sync may have replaced the entry on a generation change. */
  projection_t *current = find_by_actor(f, actor);
  return current ? current->view.address : result;
}
void qa_compat_q2_rerelease_foreign_actors_synchronize(qa_compat_q2_rerelease_foreign_actors *f) {
  size_t count = f->actor_count;
  projection_t **copy = xmalloc((count ? count : 1) * sizeof *copy);
  if (count) memcpy(copy, f->actors, count * sizeof *copy);
  for (size_t i = 0; i < count; i++) {
    /* Skip entries already removed by an earlier sync in this pass. */
    bool present = false;
    for (size_t j = 0; j < f->actor_count; j++) if (f->actors[j] == copy[i]) present = true;
    if (present) sync_projection(f, copy[i]);
  }
  free(copy);
}
/* --- armor stages --- */
static void remove_armor_hooks(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_runner *r = frunner(f);
  for (size_t i = 0; i < f->hook_count; i++) {
    if (f->hook_is_inline[i]) r->remove_inline_region(r->ctx, f->hook_handles[i]);
    else r->remove_entry(r->ctx, f->hook_handles[i]);
  }
  f->hook_count = 0;
}
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f; qa_owned_actor actor;
  qa_protection_channel channel; qa_armor_stage_intercept_fn fn; void *fn_ctx;
} stage_removal_t;
static void stage_remove(void *v) {
  stage_removal_t *r = v;
  qa_compat_q2_rerelease_foreign_actors *f = r->f;
  armor_binding_t *binding = find_binding(f, r->actor);
  if (binding) {
    if (r->channel == QA_PROTECTION_REGULAR && binding->regular == r->fn && binding->regular_ctx == r->fn_ctx) { binding->regular = NULL; binding->regular_ctx = NULL; }
    if (r->channel == QA_PROTECTION_POWERED && binding->powered == r->fn && binding->powered_ctx == r->fn_ctx) { binding->powered = NULL; binding->powered_ctx = NULL; }
    if (!binding->regular && !binding->powered) {
      size_t index = (size_t)(binding - f->bindings);
      memmove(&f->bindings[index], &f->bindings[index + 1], (f->binding_count - index - 1) * sizeof *f->bindings);
      f->binding_count--;
    }
  }
  if (f->binding_count == 0) remove_armor_hooks(f);
  free(r);
}
static void install_armor_hooks(qa_compat_q2_rerelease_foreign_actors *f);
static qa_armor_stage_removal stage_bind(void *v, qa_armor_stage_intercept_fn intercept, void *intercept_ctx) {
  stage_ctx_t *s = v;
  qa_compat_q2_rerelease_foreign_actors *f = s->f;
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, s->view, &actor))
    qa_range_error("Native armor stage has no live actor");
  armor_binding_t *binding = find_binding(f, actor);
  if (!binding) {
    if (f->binding_count == f->binding_cap) {
      f->binding_cap = f->binding_cap ? f->binding_cap * 2 : 8;
      f->bindings = xrealloc(f->bindings, f->binding_cap * sizeof *f->bindings);
    }
    binding = &f->bindings[f->binding_count++];
    memset(binding, 0, sizeof *binding);
    binding->actor = actor; binding->armor_ctx = s->armor_ctx; binding->armor = s->armor;
  }
  qa_armor_stage_intercept_fn *slot = s->channel == QA_PROTECTION_REGULAR ? &binding->regular : &binding->powered;
  void **slot_ctx = s->channel == QA_PROTECTION_REGULAR ? &binding->regular_ctx : &binding->powered_ctx;
  if (*slot) qa_range_error("Native armor stage already has an owner");
  *slot = intercept; *slot_ctx = intercept_ctx;
  if (f->hook_count == 0) install_armor_hooks(f);
  stage_removal_t *r = xmalloc(sizeof *r);
  *r = (stage_removal_t){f, actor, s->channel, intercept, intercept_ctx};
  return (qa_armor_stage_removal){r, stage_remove};
}
qa_source_armor_stage qa_compat_q2_rerelease_foreign_actors_armor_stage(qa_compat_q2_rerelease_foreign_actors *f, qa_raw_entity_view view,
                                                                        void *armor_ctx, qa_armor_state (*armor)(void *ctx),
                                                                        qa_protection_channel channel) {
  stage_ctx_t *s = xmalloc(sizeof *s);
  memset(s, 0, sizeof *s);
  s->f = f; s->view = view; s->armor_ctx = armor_ctx; s->armor = armor; s->channel = channel;
  s->next = f->stages; f->stages = s;
  return (qa_source_armor_stage){s, stage_bind};
}
static void power_observe(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  if (f->power_bypass == 1) { f->power_bypass = 2; f->bypass_stack = rsp_of(f); }
}
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f;
  const qa_guest_call_value *args; size_t count; qa_raw_entity_view view;
} power_original_t;
static double power_original(void *v) {
  power_original_t *o = v;
  qa_compat_q2_rerelease_foreign_actors *f = o->f;
  int previous = f->power_bypass; uint64_t previous_stack = f->bypass_stack;
  f->power_bypass = 1;
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(
      famod(f), f->entries.power_armor, f->power_signature, o->args, o->count, &o->view, NULL);
  f->power_bypass = previous; f->bypass_stack = previous_stack;
  if (result.kind != QA_GUEST_VALUE_INT32) qa_range_error("Native power stage returned a non-integer result");
  return result.number_value;
}
static qa_guest_call_result power_invoke(void *v, const qa_guest_call_context *context,
                                         const qa_guest_call_value *source_args, size_t source_count) {
  (void)context;
  qa_compat_q2_rerelease_foreign_actors *f = v;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_call_value args[16];
  size_t count = qa_compat_q2_read_native_combat_arguments(&profile->calls.power_armor, QA_Q2CO_POWER_ARMOR, source_args, source_count, 8, args);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_raw_entity_view view = table.from_pointer(table.ctx, qa_compat_q2_guest_required_pointer(args, count, 0));
  qa_owned_actor actor;
  bool has_actor = qa_compat_q2_rerelease_q2_guest_host_actor(f->host, view, &actor);
  frame_t *frame = last_frame(f);
  armor_binding_t *binding = has_actor ? find_binding(f, actor) : NULL;
  qa_armor_stage_intercept_fn intercept = binding ? binding->powered : NULL;
  if (!has_actor || !intercept || !frame || !qa_same_actor(frame->request.target, actor.id))
    qa_range_error("Native power stage has no active source damage request");
  power_original_t original = {f, source_args, source_count, view};
  qa_armor_stage_input input; memset(&input, 0, sizeof input);
  input.request = frame->request;
  if (input.request.attack.attacker) input.request.attack.attacker = &frame->attacker;
  if (input.request.attack.inflictor) input.request.attack.inflictor = &frame->inflictor;
  input.amount = (double)qa_compat_q2_guest_integer(args, count, 3);
  input.direction = frame->request.direction;
  input.point = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 1));
  input.normal = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 2));
  input.flags = qa_compat_q2_native_armor_flags((double)qa_compat_q2_guest_integer(args, count, 4));
  double saved = intercept(binding->powered_ctx, input, power_original, &original);
  if (!qa_session_actor_registry_is_live(fengine(f)->actors, actor.id)) { f->removed = true; f->removed_request = frame->request; }
  return qa_compat_q2_rerelease_guest_int(trunc(saved));
}
static bool power_accepts(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  if (f->power_bypass == 2 && f->bypass_stack == rsp_of(f)) return false;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address address;
  if (!field_pointer(f, &profile->calls.power_armor, f->power_signature, &address)) return false;
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(f->host, table.from_pointer(table.ctx, address), &actor)) return false;
  armor_binding_t *binding = find_binding(f, actor);
  return binding && binding->powered != NULL;
}
/* guest/core/registers.h cannot share a TU with contracts/execution.h
 * (both define qa_guest_address), so the register file is reached
 * through a layout mirror: qa_guest_processor_state begins with the
 * architecture word followed by qa_integer_register_file
 * {architecture, bytes[128]}; register indices are RAX..R15 = 0..15. */
extern uint64_t qa_integer_register_file_read(const void *regs, int reg, int width, bool high_byte);
extern void qa_integer_register_file_write(void *regs, int reg, int width, uint64_t value, bool high_byte);
static void *register_file_of(qa_compat_q2_runner *r) {
  return (char *)r->cpu_state + sizeof(int);
}
static int register_of(const char *name) {
  static const char *const names[] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                      "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15"};
  for (int i = 0; i < 16; i++) if (name && strcmp(names[i], name) == 0) return i;
  qa_range_error("Native regular stage has an unknown register");
}
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f;
  qa_guest_address source_stack;
  qa_compat_q2_inline_continuation *continuation;
  const qa_compat_q2_rerelease_world_location *result_location;
  bool executed;
} regular_ctx_t;
static int64_t read_location(regular_ctx_t *c, qa_compat_q2_rerelease_world_location location) {
  qa_compat_q2_memory *memory = fmem(c->f);
  if (location.is_register) {
    qa_compat_q2_runner *r = frunner(c->f);
    uint64_t value = qa_integer_register_file_read(register_file_of(r), register_of(location.reg),
                                                   strcmp(location.storage, "pointer") == 0 ? 64 : 32, false);
    return strcmp(location.storage, "int32") == 0 ? (int64_t)(int32_t)value : (int64_t)value;
  }
  qa_guest_address address = qa_compat_q2_mem_offset(memory, c->source_stack, (int64_t)location.offset);
  if (strcmp(location.storage, "pointer") == 0) return (int64_t)qa_compat_q2_mem_read_u64(memory, address);
  return strcmp(location.storage, "int32") == 0 ? qa_compat_q2_mem_read_i32(memory, address)
                                                 : (int64_t)qa_compat_q2_mem_read_u32(memory, address);
}
/* BigInt(value) for an integral number (RangeError for NaN and infinities), as the
 * two's-complement low 64 bits a register or memory store keeps. */
static int64_t bigint_bits(double value) {
  if (!isfinite(value)) qa_range_error("The number %s cannot be converted to a BigInt because it is not an integer", QA_JS_NUMBER(value));
  uint64_t magnitude = (uint64_t)fmod(fabs(value), 18446744073709551616.0);
  uint64_t bits = value < 0 ? (uint64_t)0 - magnitude : magnitude;
  int64_t out; memcpy(&out, &bits, sizeof out); return out;
}
static void write_location(regular_ctx_t *c, qa_compat_q2_rerelease_world_location location, int64_t value) {
  qa_compat_q2_memory *memory = fmem(c->f);
  if (location.is_register) {
    qa_compat_q2_runner *r = frunner(c->f);
    qa_integer_register_file_write(register_file_of(r), register_of(location.reg),
                                   strcmp(location.storage, "pointer") == 0 ? 64 : 32, (uint64_t)value, false);
    return;
  }
  qa_guest_address address = qa_compat_q2_mem_offset(memory, c->source_stack, (int64_t)location.offset);
  if (strcmp(location.storage, "pointer") == 0) qa_compat_q2_mem_write_u64(memory, address, (uint64_t)value);
  else if (strcmp(location.storage, "int32") == 0) qa_compat_q2_mem_write_i32(memory, address, qa_js_to_int32(value));
  else qa_compat_q2_mem_write_u32(memory, address, qa_js_to_uint32(value));
}
static double regular_original(void *v) {
  regular_ctx_t *c = v;
  c->continuation->execute(c->continuation->ctx);
  c->executed = true;
  return (double)read_location(c, *c->result_location);
}
static void regular_region(void *v, qa_compat_q2_inline_continuation *continuation) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  qa_compat_q2_memory *memory = fmem(f);
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address_result source_stack = qa_compat_q2_mem_pointer(memory, (int64_t)rsp_of(f));
  if (!source_stack.has_value) qa_range_error("Native regular stage has no source stack");
  regular_ctx_t ctx = {f, source_stack.value, continuation, &profile->regular_armor.result, false};
  qa_guest_address_result address = qa_compat_q2_mem_pointer(memory, read_location(&ctx, profile->regular_armor.target));
  qa_owned_actor actor;
  bool has_actor = false;
  if (address.has_value) {
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
    has_actor = qa_compat_q2_rerelease_q2_guest_host_actor(f->host, table.from_pointer(table.ctx, address.value), &actor);
  }
  armor_binding_t *binding = has_actor ? find_binding(f, actor) : NULL;
  qa_armor_stage_intercept_fn intercept = binding ? binding->regular : NULL;
  if (!intercept) { continuation->execute(continuation->ctx); return; }
  frame_t *frame = last_frame(f);
  if (!has_actor || !frame || !qa_same_actor(frame->request.target, actor.id))
    qa_range_error("Native regular stage has no active source damage request");
  qa_guest_address_result point = qa_compat_q2_mem_pointer(memory, read_location(&ctx, profile->regular_armor.point));
  if (!point.has_value) qa_range_error("Native regular stage has no source arguments");
  qa_guest_address_result normal = qa_compat_q2_mem_pointer(memory, read_location(&ctx, profile->regular_armor.normal));
  if (!normal.has_value) qa_range_error("Native regular stage has no source normal");
  size_t repair_count = profile->regular_armor.repair_count;
  int64_t *repair_values = xmalloc(repair_count * sizeof *repair_values);
  for (size_t i = 0; i < repair_count; i++) repair_values[i] = read_location(&ctx, profile->regular_armor.repair[i].source);
  qa_armor_stage_input input; memset(&input, 0, sizeof input);
  input.request = frame->request;
  if (input.request.attack.attacker) input.request.attack.attacker = &frame->attacker;
  if (input.request.attack.inflictor) input.request.attack.inflictor = &frame->inflictor;
  input.amount = (double)read_location(&ctx, profile->regular_armor.amount);
  input.direction = frame->request.direction;
  input.point = vector_at(f, point.value);
  input.normal = vector_at(f, normal.value);
  input.flags = qa_compat_q2_native_armor_flags((double)read_location(&ctx, profile->regular_armor.flags));
  double saved = intercept(binding->regular_ctx, input, regular_original, &ctx);
  if (!qa_session_actor_registry_is_live(fengine(f)->actors, actor.id)) { f->removed = true; f->removed_request = frame->request; }
  if (!ctx.executed) {
    continuation->skip(continuation->ctx);
    for (size_t i = 0; i < repair_count; i++) write_location(&ctx, profile->regular_armor.repair[i].target, repair_values[i]);
  }
  free(repair_values);
  write_location(&ctx, profile->regular_armor.result, bigint_bits(trunc(saved)));
}
static void install_armor_hooks(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_runner *r = frunner(f);
  qa_compat_q2_memory *memory = fmem(f);
  f->hook_handles[f->hook_count] = r->observe_entry(r->ctx, f->entries.power_armor, f, power_observe);
  f->hook_is_inline[f->hook_count++] = false;
  char id[512];
  snprintf(id, sizeof id, "%s:powered-armor", memory->module.id ? memory->module.id : "");
  f->hook_handles[f->hook_count] = r->bind_entry(r->ctx, f->entries.power_armor, strdup(id), f->power_signature, f, power_invoke, f, power_accepts);
  f->hook_is_inline[f->hook_count++] = false;
  f->hook_handles[f->hook_count] = r->bind_inline_region(r->ctx, f->entries.regular_armor.entry, f->entries.regular_armor.join,
                                                         qa_compat_q2_rerelease_abi, f, regular_region);
  f->hook_is_inline[f->hook_count++] = true;
}
/* --- damage --- */
typedef struct {
  qa_compat_q2_rerelease_foreign_actors *f;
  const qa_guest_call_value *source_args; size_t source_count;
} incoming_ctx_t;
static qa_damage_outcome incoming_source_damage(void *v, qa_damage_request request) {
  incoming_ctx_t *c = v;
  return qa_compat_q2_rerelease_foreign_actors_damage_native(c->f, request, c->source_args, c->source_count);
}
static void incoming_damage(qa_compat_q2_rerelease_foreign_actors *f, const qa_guest_call_value *source_args, size_t source_count) {
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_call_value args[16];
  size_t count = qa_compat_q2_read_native_combat_arguments(&profile->calls.damage, QA_Q2CO_DAMAGE, source_args, source_count, 8, args);
  qa_actor_id target = actor_at(f, qa_compat_q2_guest_required_pointer(args, count, 0));
  qa_actor_id inflictor = actor_at(f, qa_compat_q2_guest_required_pointer(args, count, 1));
  qa_actor_id attacker = actor_at(f, qa_compat_q2_guest_required_pointer(args, count, 2));
  const qa_guest_call_value *mod = count > 9 ? &args[9] : NULL;
  if (!mod || mod->kind != QA_GUEST_VALUE_AGGREGATE || mod->bytes.length != 3)
    qa_range_error("Native damage requires the API2023 mod_t ABI");
  qa_q2_native_cause native; memset(&native, 0, sizeof native);
  native.edition = "rerelease"; native.id = mod->bytes.bytes[0];
  native.friendly_fire = mod->bytes.bytes[1] != 0;
  native.no_point_loss = mod->bytes.bytes[2] != 0;
  double canonical;
  if (!qa_q2mp_canonical_cause_from_native(native, &canonical)) qa_range_error("Unclassified native damage cause");
  double damage_flags = (double)qa_compat_q2_guest_integer(args, count, 8);
  qa_damage_request request; memset(&request, 0, sizeof request);
  request.target = target;
  request.amount = (double)qa_compat_q2_guest_integer(args, count, 6);
  request.knockback = (double)qa_compat_q2_guest_integer(args, count, 7);
  request.direction = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 3));
  request.point = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 4));
  request.normal = vector_at(f, qa_compat_q2_guest_required_pointer(args, count, 5));
  request.delivery = (qa_js_to_int32(damage_flags) & 1) != 0 ? "radius" : "direct";
  request.attack = f->services.provenance(f->services.ctx, attacker, inflictor, target);
  /* The ids live in this frame; apply() captures the request synchronously. */
  request.attack.attacker = &attacker;
  request.attack.inflictor = &inflictor;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q2;
  request.attack.cause.means_of_death = canonical;
  request.attack.cause.damage_flags = damage_flags;
  request.attack.cause.has_native = true;
  request.attack.cause.native = native;
  qa_gameplay_authority *combat = fengine(f)->combat;
  if (find_by_actor(f, target)) {
    (void)qa_gameplay_authority_apply(combat, request, NULL, NULL);
  } else {
    incoming_ctx_t ctx = {f, source_args, source_count};
    (void)qa_gameplay_authority_apply(combat, request, incoming_source_damage, &ctx);
  }
  projection_t *entry = find_by_actor(f, target);
  if (entry && !entry->releasing) sync_projection(f, entry);
}
typedef struct exec exec_t;
struct exec {
  qa_compat_q2_rerelease_foreign_actors *f;
  const qa_guest_call_value *source_args; size_t source_count;
  qa_compat_q2_rerelease_source_edict *source;
  qa_compat_q2_rerelease_source_client *client;
  frame_t *frame;
  qa_source_damage_observer observer;
  bool has_result; qa_source_damage_result result;
  double health, applied_damage; qa_vec3 velocity;
  bool has_armor; qa_armor_state armor;
  armor_binding_t *power;
  qa_damage_request request;
  uint64_t write_handles[3]; size_t write_count;
  uint64_t entry_handles[2]; size_t entry_count;
  uint8_t mod_bytes[3];
};
static bool read_armor(exec_t *ex, qa_armor_state *out) {
  if (ex->power && ex->power->armor) { *out = ex->power->armor(ex->power->armor_ctx); return true; }
  qa_combat_state_result state = qa_gameplay_authority_read(fengine(ex->f)->combat, ex->request.target);
  if (!state.has_value) return false;
  *out = state.value.armor;
  return true;
}
static bool exec_current(exec_t *ex) { return last_frame(ex->f) == ex->frame && !ex->has_result; }
static void stop_writes(exec_t *ex) {
  ex->frame->observing = false;
  qa_compat_q2_memory *memory = fmem(ex->f);
  if (memory->unobserve_writes)
    for (size_t i = 0; i < ex->write_count; i++) memory->unobserve_writes(memory->ctx, ex->write_handles[i]);
  ex->write_count = 0;
}
typedef struct { exec_t *ex; int kind; } write_obs_t;
static bool armor_equal(qa_armor_state a, qa_armor_state b) {
  if (a.regular.kind != b.regular.kind) return false;
  if (a.regular.kind == QA_REGULAR_ARMOR_Q2 &&
      (a.regular.points != b.regular.points || a.regular.normal_protection != b.regular.normal_protection ||
       a.regular.energy_protection != b.regular.energy_protection ||
       strcmp(a.regular.item ? a.regular.item : "", b.regular.item ? b.regular.item : "") != 0)) return false;
  if (strcmp(a.powered.kind ? a.powered.kind : "", b.powered.kind ? b.powered.kind : "") != 0) return false;
  if (a.powered.kind && strcmp(a.powered.kind, "none") != 0 && a.powered.cells != b.powered.cells) return false;
  return true;
}
static void write_observed(void *v) {
  write_obs_t *obs = v;
  exec_t *ex = obs->ex;
  if (obs->kind == 0) {
    double after = qa_compat_q2_rerelease_source_edict_get_health(ex->source);
    double before = ex->health; ex->health = after;
    if (!exec_current(ex) || after == before) return;
    ex->applied_damage += before - after;
    qa_damage_mutation m; memset(&m, 0, sizeof m);
    m.kind = QA_DAMAGE_MUTATION_HEALTH; m.health_before = before; m.health_after = after;
    ex->observer.stored(ex->observer.ctx, m);
  } else if (obs->kind == 1) {
    qa_vec3 after = qa_compat_q2_rerelease_source_edict_vector(ex->source, "velocity");
    qa_vec3 before = ex->velocity; ex->velocity = after;
    if (!exec_current(ex)) return;
    qa_damage_mutation m; memset(&m, 0, sizeof m);
    m.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY; m.velocity_before = before; m.velocity_after = after;
    m.movement_provider = ex->request.attack.movement_provider;
    ex->observer.stored(ex->observer.ctx, m);
  } else {
    if (!publishes_inventory(ex->f, ex->frame)) return;
    qa_armor_state after; bool has_after = read_armor(ex, &after);
    bool has_before = ex->has_armor; qa_armor_state before = ex->armor;
    ex->has_armor = has_after; if (has_after) ex->armor = after;
    if (!exec_current(ex)) return;
    if (!has_after || !has_before) qa_range_error("Native armor binding disappeared during damage");
    qa_combat_state_result effective = qa_gameplay_authority_read(fengine(ex->f)->combat, ex->request.target);
    if (!effective.has_value) qa_range_error("Native armor binding disappeared during damage");
    qa_armor_state prior;
    prior.regular = (!ex->power || !ex->power->regular) ? before.regular : effective.value.armor.regular;
    prior.powered = (!ex->power || !ex->power->powered) ? before.powered : effective.value.armor.powered;
    if (armor_equal(prior, effective.value.armor)) return;
    qa_damage_mutation m; memset(&m, 0, sizeof m);
    m.kind = QA_DAMAGE_MUTATION_ARMOR; m.armor_before = prior; m.armor_after = effective.value.armor;
    ex->observer.stored(ex->observer.ctx, m);
  }
}
typedef struct {
  exec_t *ex; const char *reaction;
  qa_compat_q2_native_combat_call call;
  qa_compat_q2_guest_call_signature signature;
  qa_guest_value_layout storage[16];
  qa_raw_entity_view view;
} reaction_obs_t;
static void reaction_observed(void *v) {
  reaction_obs_t *obs = v;
  exec_t *ex = obs->ex;
  if (!exec_current(ex)) return;
  qa_compat_q2_runner *r = frunner(ex->f);
  qa_guest_call_value target = r->read_combat_field(r->ctx, &obs->call, obs->signature, "target");
  qa_guest_address address;
  if (!qa_compat_q2_guest_pointer(&target, 1, 0, &address) || address.byte_offset != obs->view.address.byte_offset) return;
  qa_guest_call_value amount = r->read_combat_field(r->ctx, &obs->call, obs->signature, "amount");
  ex->result = (qa_source_damage_result){(double)qa_compat_q2_guest_integer(&amount, 1, 0), obs->reaction};
  ex->has_result = true;
  stop_writes(ex);
  ex->observer.before_reaction(ex->observer.ctx, ex->result);
}
static qa_source_damage_result damage_execute(void *v, qa_source_damage_observer observer, qa_damage_request request) {
  exec_t *ex = v;
  qa_compat_q2_rerelease_foreign_actors *f = ex->f;
  qa_q2_foundation_host *engine = fengine(f);
  qa_compat_q2_memory *memory = fmem(f);
  ex->observer = observer; ex->request = request;
  qa_actor_source_result target = qa_session_actor_registry_source_of(engine->actors, request.target);
  if (!target.has_value || !target.provider || !memory->module.id || strcmp(target.provider, memory->module.id) != 0)
    qa_range_error("Native damage execution requires a native target");
  qa_q2mp_native_cause_profile cause_profile = {true, NULL, false};
  qa_compat_q2_native_damage_arguments_result cause = qa_compat_q2_native_damage_arguments(request, cause_profile);
  qa_q2_native_cause native = cause.native;
  if (!native.edition || strcmp(native.edition, "rerelease") != 0)
    qa_range_error("Damage cause has no native representation");
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(famod(f));
  qa_raw_entity_view view = table.at_slot(table.ctx, (double)target.slot);
  ex->source = xsource(f, view);
  bool monster = (qa_compat_q2_mem_read_u32(memory, qa_compat_q2_rerelease_source_edict_at(ex->source, "shared.svflags")) & 4) != 0;
  qa_compat_q2_rerelease_deferred_damage_track(f->deferred, view);
  qa_actor_id world = engine->world_actor(engine->ctx);
  qa_guest_address attacker = qa_compat_q2_rerelease_q2_guest_host_address_for_actor(
      f->host, request.attack.attacker ? *request.attack.attacker : world);
  qa_guest_address inflictor = qa_compat_q2_rerelease_q2_guest_host_address_for_actor(
      f->host, request.attack.inflictor ? *request.attack.inflictor : world);
  qa_owned_actor target_owner;
  ex->power = qa_session_actor_registry_resolve_owned(engine->actors, request.target, &target_owner)
                  ? find_binding(f, target_owner) : NULL;
  ex->health = qa_compat_q2_rerelease_source_edict_get_health(ex->source);
  ex->velocity = qa_compat_q2_rerelease_source_edict_vector(ex->source, "velocity");
  ex->has_armor = read_armor(ex, &ex->armor);
  ex->applied_damage = 0;
  if (!ex->has_armor) qa_range_error("Native target lacks a combat binding");
  if (f->frame_count == f->frame_cap) {
    f->frame_cap = f->frame_cap ? f->frame_cap * 2 : 8;
    f->frames = xrealloc(f->frames, f->frame_cap * sizeof *f->frames);
  }
  frame_t *frame = &f->frames[f->frame_count++];
  memset(frame, 0, sizeof *frame);
  frame->request = qa_capture_request(request);
  if (request.attack.attacker) { frame->attacker = *request.attack.attacker; frame->request.attack.attacker = &frame->attacker; }
  if (request.attack.inflictor) { frame->inflictor = *request.attack.inflictor; frame->request.attack.inflictor = &frame->inflictor; }
  ex->frame = frame;
  write_obs_t *wobs = xmalloc(3 * sizeof *wobs);
  reaction_obs_t *robs = xmalloc(2 * sizeof *robs);
  frame->observing = true;
  if (memory->observe_writes) {
    for (int k = 0; k < 3; k++) { wobs[k].ex = ex; wobs[k].kind = k; }
    ex->write_handles[ex->write_count++] = memory->observe_writes(
        memory->ctx, qa_compat_q2_rerelease_source_edict_at(ex->source, "health"), 4, &wobs[0], write_observed);
    ex->write_handles[ex->write_count++] = memory->observe_writes(
        memory->ctx, qa_compat_q2_rerelease_source_edict_at(ex->source, "velocity"), 12, &wobs[1], write_observed);
    qa_guest_address client_address;
    if (qa_compat_q2_rerelease_source_edict_client(ex->source, &client_address)) {
      const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
      ex->client = qa_compat_q2_rerelease_source_client_create(client_address, famod(f), &profile->client);
      if (!ex->client) qa_range_error("Q2 rerelease foreign actors out of memory");
      ex->write_handles[ex->write_count++] = memory->observe_writes(
          memory->ctx, qa_compat_q2_rerelease_source_client_at(ex->client, "pers.inventory"),
          profile->client.inventory_count * 4, &wobs[2], write_observed);
    }
  }
  if (!monster) {
    const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
    const char *names[2] = {"pain", "death"};
    const char *fields[2] = {"pain.value", "die.value"};
    for (int i = 0; i < 2; i++) {
      qa_guest_address callback;
      if (!qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_rerelease_source_edict_at(ex->source, fields[i]), &callback)) continue;
      robs[i].ex = ex; robs[i].reaction = names[i]; robs[i].view = view;
      robs[i].call = i == 0 ? profile->calls.pain : profile->calls.death;
      robs[i].signature = qa_compat_q2_native_combat_signature(&robs[i].call, i == 0 ? QA_Q2CO_PAIN : QA_Q2CO_DEATH,
                                                               qa_compat_q2_rerelease_abi, robs[i].storage);
      qa_compat_q2_runner *r = frunner(f);
      ex->entry_handles[ex->entry_count++] = r->observe_entry(r->ctx, callback, &robs[i], reaction_observed);
    }
  }
  qa_guest_address vectors = memory->allocate(memory->ctx, 36, true, 4, "Q2 damage vectors");
  const qa_vec3 values[3] = {request.direction, request.point, request.normal};
  for (int i = 0; i < 3; i++) {
    qa_guest_address base = qa_compat_q2_mem_offset(memory, vectors, i * 12);
    qa_compat_q2_mem_write_f32(memory, base, values[i].x);
    qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, base, 4), values[i].y);
    qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, base, 8), values[i].z);
  }
  qa_compat_q2_runner *runner = frunner(f);
  void *saved_processor = runner->capture_processor ? runner->capture_processor(runner->ctx) : NULL;
  ex->mod_bytes[0] = qa_js_to_uint8(native.id); /* Uint8Array store */
  ex->mod_bytes[1] = native.friendly_fire ? 1 : 0;
  ex->mod_bytes[2] = native.no_point_loss ? 1 : 0;
  qa_guest_call_value aggregate; memset(&aggregate, 0, sizeof aggregate);
  aggregate.kind = QA_GUEST_VALUE_AGGREGATE;
  aggregate.layout = qa_compat_q2_rerelease_mod_layout;
  aggregate.bytes.bytes = ex->mod_bytes; aggregate.bytes.length = 3;
  qa_guest_call_value semantic[10] = {
      qa_compat_q2_rerelease_guest_pointer(true, view.address),
      qa_compat_q2_rerelease_guest_pointer(true, inflictor),
      qa_compat_q2_rerelease_guest_pointer(true, attacker),
      qa_compat_q2_rerelease_guest_pointer(true, vectors),
      qa_compat_q2_rerelease_guest_pointer(true, qa_compat_q2_mem_offset(memory, vectors, 12)),
      qa_compat_q2_rerelease_guest_pointer(true, qa_compat_q2_mem_offset(memory, vectors, 24)),
      qa_compat_q2_rerelease_guest_int(trunc(request.amount)),
      qa_compat_q2_rerelease_guest_int(trunc(request.knockback)),
      qa_compat_q2_rerelease_guest_int(cause.damage_flags),
      aggregate};
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_call_value lowered[16];
  size_t lowered_count = qa_compat_q2_lower_native_combat_arguments(
      &profile->calls.damage, QA_Q2CO_DAMAGE, semantic, 10, memory, true, f->image, ex->source_args, ex->source_count, lowered);
  f->removed = false;
  (void)qa_compat_q2_rerelease_guest_module_invoke(famod(f), f->entries.damage, f->damage_signature, lowered, lowered_count, &view, NULL);
  if (f->removed && qa_same_actor(f->removed_request.target, request.target) && saved_processor && runner->restore_processor)
    runner->restore_processor(runner->ctx, saved_processor);
  qa_source_damage_result result = ex->has_result ? ex->result : (qa_source_damage_result){ex->applied_damage, "none"};
  stop_writes(ex);
  for (size_t i = 0; i < ex->entry_count; i++) runner->remove_entry(runner->ctx, ex->entry_handles[i]);
  ex->entry_count = 0;
  memory->unmap(memory->ctx, vectors, 36);
  f->frame_count--;
  qa_compat_q2_rerelease_q2_guest_host_reconcile(f->host);
  free(wobs); free(robs);
  if (ex->client) { qa_compat_q2_rerelease_source_client_destroy(ex->client); ex->client = NULL; }
  qa_compat_q2_rerelease_source_edict_destroy(ex->source); ex->source = NULL;
  return result;
}
qa_damage_outcome qa_compat_q2_rerelease_foreign_actors_damage_native(qa_compat_q2_rerelease_foreign_actors *f, qa_damage_request input,
                                                                      const qa_guest_call_value *source_args, size_t source_count) {
  exec_t ex; memset(&ex, 0, sizeof ex);
  ex.f = f; ex.source_args = source_args; ex.source_count = source_count;
  return qa_gameplay_authority_run_source_damage(fengine(f)->combat, input, damage_execute, &ex);
}
/* --- lifecycle --- */
void qa_compat_q2_rerelease_foreign_actors_clear(qa_compat_q2_rerelease_foreign_actors *f) {
  qa_compat_q2_rerelease_deferred_damage_clear(f->deferred);
  while (f->actor_count > 0) release_projection(f, f->actors[0]);
}
const qa_compat_q2_rerelease_projection_save *qa_compat_q2_rerelease_foreign_actors_save_projections(
    qa_compat_q2_rerelease_foreign_actors *f, size_t *out_count) {
  size_t count = f->actor_count;
  projection_t **copy = xmalloc(count * sizeof *copy);
  memcpy(copy, f->actors, count * sizeof *copy);
  if (count > f->save_cap) { f->save_scratch = xrealloc(f->save_scratch, count * sizeof *f->save_scratch); f->save_cap = count; }
  size_t written = 0;
  for (size_t i = 0; i < count; i++) {
    qa_owned_actor found;
    if (qa_compat_q2_rerelease_foreign_actors_lookup(f, copy[i]->view, &found) != QA_COMPAT_Q2_RR_LOOKUP_ACTOR) continue;
    if (!qa_same_owned_actor(found, copy[i]->actor)) continue;
    f->save_scratch[written].slot = copy[i]->view.slot;
    f->save_scratch[written].actor.slot = (double)copy[i]->actor.id.slot;
    f->save_scratch[written].actor.generation = (double)copy[i]->actor.id.generation;
    written++;
  }
  free(copy);
  *out_count = written;
  return f->save_scratch;
}
void qa_compat_q2_rerelease_foreign_actors_begin_restore(qa_compat_q2_rerelease_foreign_actors *f,
                                                         const qa_compat_q2_rerelease_projection_save *saved, size_t count, const char *domain) {
  if (f->restoring) qa_range_error("Native projection restore is already active");
  f->restoring_saved = xmalloc(count * sizeof *f->restoring_saved);
  memcpy(f->restoring_saved, saved, count * sizeof *saved);
  f->restoring_count = count;
  snprintf(f->restoring_domain, sizeof f->restoring_domain, "%s", domain ? domain : "current");
  f->restoring = true;
}
void qa_compat_q2_rerelease_foreign_actors_end_restore(qa_compat_q2_rerelease_foreign_actors *f) {
  f->restoring = false;
  free(f->restoring_saved); f->restoring_saved = NULL; f->restoring_count = 0;
}
void qa_compat_q2_rerelease_foreign_actors_close(qa_compat_q2_rerelease_foreign_actors *f) {
  remove_armor_hooks(f);
  f->binding_count = 0;
  if (f->closed) return;
  f->closed = true;
  qa_compat_q2_runner *r = frunner(f);
  if (f->remove_entry) { r->remove_entry(r->ctx, f->remove_entry); f->remove_entry = 0; }
  if (f->remove_continuation) { r->remove_entry(r->ctx, f->remove_continuation); f->remove_continuation = 0; }
  qa_compat_q2_rerelease_deferred_damage_close(f->deferred);
  qa_compat_q2_rerelease_foreign_actors_clear(f);
}
static void continuation_observed(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  frame_t *frame = last_frame(f);
  if (!frame || frame->has_stack) return;
  frame->stack = rsp_of(f);
  frame->has_stack = true;
}
static bool deferred_current_request(void *v, qa_actor_id actor, qa_damage_request *out) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  frame_t *frame = last_frame(f);
  if (!frame || frame->entered || !qa_same_actor(frame->request.target, actor)) return false;
  frame->entered = true;
  *out = frame->request;
  if (out->attack.attacker) out->attack.attacker = &frame->attacker;
  if (out->attack.inflictor) out->attack.inflictor = &frame->inflictor;
  return true;
}
static bool deferred_intercepted(void *v) { return intercept_native_damage(v); }
static qa_guest_call_result damage_invoke(void *v, const qa_guest_call_context *context,
                                          const qa_guest_call_value *args, size_t count) {
  (void)context;
  incoming_damage(v, args, count);
  qa_guest_call_result out; memset(&out, 0, sizeof out);
  out.kind = QA_GUEST_VALUE_VOID;
  return out;
}
static bool damage_accepts(void *v) {
  qa_compat_q2_rerelease_foreign_actors *f = v;
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(famod(f));
  qa_guest_address address;
  if (!field_pointer(f, &profile->calls.damage, f->damage_signature, &address)) return false;
  projection_t *entry = NULL;
  for (size_t i = 0; i < f->actor_count; i++)
    if (f->actors[i]->view.address.byte_offset == address.byte_offset) entry = f->actors[i];
  if (!entry) return intercept_native_damage(f);
  qa_owned_actor found;
  if (qa_compat_q2_rerelease_foreign_actors_lookup(f, entry->view, &found) == QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED)
    return intercept_native_damage(f);
  qa_compat_q2_rerelease_source_edict *source = xsource(f, entry->view);
  const qa_compat_q2_memory * m = fmem(f);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(source, "shared.inuse");
  bool inuse = qa_compat_q2_mem_read_u8(m, a) != 0;
  qa_compat_q2_rerelease_source_edict_destroy(source);
  return !entry->releasing && qa_session_actor_registry_is_live(fengine(f)->actors, entry->actor.id) && inuse;
}
qa_compat_q2_rerelease_foreign_actors *qa_compat_q2_rerelease_foreign_actors_create(
    qa_compat_q2_rerelease_q2_guest_host *host, qa_compat_q2_rerelease_native_entries entries,
    qa_compat_q2_rerelease_foreign_damage_services services) {
  qa_compat_q2_rerelease_foreign_actors *f = calloc(1, sizeof *f);
  if (!f) return NULL;
  f->host = host; f->entries = entries; f->services = services;
  qa_compat_q2_rerelease_guest_module *module = famod(f);
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
  f->damage_signature = qa_compat_q2_native_combat_signature(&profile->calls.damage, QA_Q2CO_DAMAGE,
                                                             qa_compat_q2_rerelease_abi, f->sig_storage[0]);
  f->power_signature = qa_compat_q2_native_combat_signature(&profile->calls.power_armor, QA_Q2CO_POWER_ARMOR,
                                                            qa_compat_q2_rerelease_abi, f->sig_storage[1]);
  f->image = qa_compat_q2_mem_offset(fmem(f), entries.damage, -(int64_t)profile->entries.damage);
  qa_compat_q2_runner *r = frunner(f);
  f->remove_continuation = r->observe_entry(r->ctx, entries.damage, f, continuation_observed);
  f->deferred = qa_compat_q2_rerelease_deferred_damage_create(host, services, f, deferred_current_request, deferred_intercepted);
  if (!f->deferred) qa_range_error("Q2 rerelease foreign actors out of memory");
  char id[512];
  snprintf(id, sizeof id, "%s:foreign-damage", fmem(f)->module.id ? fmem(f)->module.id : "");
  f->remove_entry = r->bind_entry(r->ctx, entries.damage, strdup(id), f->damage_signature, f, damage_invoke, f, damage_accepts);
  return f;
}
void qa_compat_q2_rerelease_foreign_actors_destroy(qa_compat_q2_rerelease_foreign_actors *f) {
  if (!f) return;
  qa_compat_q2_rerelease_foreign_actors_close(f);
  qa_compat_q2_rerelease_deferred_damage_destroy(f->deferred);
  for (size_t i = 0; i < f->actor_count; i++) free(f->actors[i]);
  free(f->actors); free(f->frames); free(f->publications); free(f->bindings);
  free(f->restoring_saved); free(f->save_scratch);
  while (f->stages) { stage_ctx_t *next = f->stages->next; free(f->stages); f->stages = next; }
  free(f);
}
qa_compat_q2_rerelease_q2_guest_host *qa_compat_q2_rerelease_foreign_actors_get_host(const qa_compat_q2_rerelease_foreign_actors *f) {
  return f->host;
}
qa_compat_q2_rerelease_native_entries qa_compat_q2_rerelease_foreign_actors_get_entries(const qa_compat_q2_rerelease_foreign_actors *f) {
  return f->entries;
}
qa_compat_q2_rerelease_deferred_damage *qa_compat_q2_rerelease_foreign_actors_get_deferred(qa_compat_q2_rerelease_foreign_actors *f) {
  return f->deferred;
}
