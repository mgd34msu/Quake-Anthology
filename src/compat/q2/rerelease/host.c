/* Port of src/compat/q2/rerelease/host.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/host.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/rerelease/layouts.h"
#include "qa/compat/q2/rerelease/pickup_profile.h"
#include "qa/compat/q2/rerelease/pickup_protection.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/callbacks.h"
#include "qa/world/gameplay/inventory.h"
#include "qa/foundation/js.h"

typedef struct { double slot; qa_owned_actor actor; bool has_generation; double generation; int64_t address; } lifetime_t;
typedef struct { qa_actor_id actor; qa_raw_entity_view view; } bot_entry_t;
typedef struct { const qa_q2_surface_info *surface; qa_guest_address address; } surface_entry_t;
typedef struct callback_wrap {
  qa_compat_q2_rerelease_q2_guest_host *host;
  qa_actor_callbacks inner;
  struct callback_wrap *next;
} callback_wrap_t;
struct qa_compat_q2_rerelease_q2_guest_host {
  qa_compat_q2_rerelease_q2_host_options options;
  qa_compat_q2_rerelease_guest_module *module;
  qa_compat_q2_rerelease_core_imports *core;
  qa_compat_q2_rerelease_foreign_actors *foreign;
  qa_compat_q2_rerelease_message_imports *messages;
  qa_compat_q2_rerelease_debug_shape_imports *debug_shapes;
  qa_compat_q2_rerelease_world_text_imports *world_text;
  qa_compat_q2_rerelease_sound_imports *sounds;
  lifetime_t *lifetimes; size_t lifetime_count, lifetime_cap;
  double *retired; size_t retired_count, retired_cap;
  double *reserved; size_t reserved_count, reserved_cap;
  surface_entry_t *surfaces; size_t surface_count, surface_cap;
  bot_entry_t *bots; size_t bot_count, bot_cap;
  qa_raw_entity_view *bot_scratch; size_t bot_scratch_cap;
  callback_wrap_t *wraps;
  int64_t filter_depth;
  int64_t unsubscribe_id; bool subscribed;
  bool initialized, closed, has_pickups;
  qa_compat_q2_native_primary_pickups pickups;
  qa_guest_value_layout box_params[2];
  qa_compat_q2_guest_call_signature box_filter_signature;
  uint8_t *trace_scratch; size_t trace_scratch_cap;
};
static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Q2 rerelease host out of memory"); return p; }
static void *xrealloc(void *p, size_t n) { void *q = realloc(p, n ? n : 1); if (!q) qa_range_error("Q2 rerelease host out of memory"); return q; }
static qa_compat_q2_memory *hmem(qa_compat_q2_rerelease_q2_guest_host *h) {
  return qa_compat_q2_rerelease_guest_module_get_memory(h->module);
}
static qa_guest_address at_field(qa_compat_q2_rerelease_q2_guest_host *h, qa_raw_entity_view view, const char *name) {
  const qa_compat_q2_memory * m = hmem(h);
  qa_guest_address a = view.address;
  int64_t d = (int64_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, name);
  return qa_compat_q2_mem_offset(m, a, d);
}
static qa_vec3 read_vector(qa_compat_q2_rerelease_q2_guest_host *h, qa_guest_address address) {
  return qa_compat_q2_mem_read_f32_vector(hmem(h), address);
}
static void write_vector(qa_compat_q2_rerelease_q2_guest_host *h, qa_guest_address address, qa_vec3 vector) {
  qa_compat_q2_memory *memory = hmem(h);
  qa_compat_q2_mem_write_f32(memory, address, vector.x);
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, address, 4), vector.y);
  qa_compat_q2_mem_write_f32(memory, qa_compat_q2_mem_offset(memory, address, 8), vector.z);
}
static bool set_contains(const double *set, size_t count, double value) {
  for (size_t i = 0; i < count; i++) if (set[i] == value) return true;
  return false;
}
static void set_add(double **set, size_t *count, size_t *cap, double value) {
  if (set_contains(*set, *count, value)) return;
  if (*count == *cap) { *cap = *cap ? *cap * 2 : 8; *set = xrealloc(*set, *cap * sizeof **set); }
  (*set)[(*count)++] = value;
}
static void set_remove(double *set, size_t *count, double value) {
  for (size_t i = 0; i < *count; i++) if (set[i] == value) { memmove(&set[i], &set[i + 1], (*count - i - 1) * sizeof *set); (*count)--; return; }
}
static lifetime_t *find_lifetime(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  for (size_t i = 0; i < h->lifetime_count; i++) if (h->lifetimes[i].slot == slot) return &h->lifetimes[i];
  return NULL;
}
static void remove_lifetime(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  for (size_t i = 0; i < h->lifetime_count; i++)
    if (h->lifetimes[i].slot == slot) {
      memmove(&h->lifetimes[i], &h->lifetimes[i + 1], (h->lifetime_count - i - 1) * sizeof *h->lifetimes);
      h->lifetime_count--;
      return;
    }
}
void qa_compat_q2_rerelease_q2_guest_host_retire_input_client(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  set_add(&h->retired, &h->retired_count, &h->retired_cap, slot);
}
void qa_compat_q2_rerelease_q2_guest_host_finish_input_retirement(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  set_remove(h->retired, &h->retired_count, slot);
  remove_lifetime(h, slot);
}
static void wrap_think(void *v, qa_owned_actor self, qa_frame_context frame) {
  callback_wrap_t *w = v;
  if (w->inner.think) w->inner.think(w->inner.ctx, self, frame);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(w->host);
}
static void wrap_touch(void *v, qa_touch_contact contact) {
  callback_wrap_t *w = v;
  if (w->inner.touch) w->inner.touch(w->inner.ctx, contact);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(w->host);
}
static void wrap_use(void *v, qa_owned_actor self, const qa_actor_id *other, const qa_actor_id *activator) {
  callback_wrap_t *w = v;
  if (w->inner.use) w->inner.use(w->inner.ctx, self, other, activator);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(w->host);
}
static void wrap_pain(void *v, qa_pain_reaction reaction) {
  callback_wrap_t *w = v;
  if (w->inner.pain) w->inner.pain(w->inner.ctx, reaction);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(w->host);
}
static void wrap_die(void *v, qa_death_reaction reaction) {
  callback_wrap_t *w = v;
  if (w->inner.die) w->inner.die(w->inner.ctx, reaction);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(w->host);
}
bool qa_compat_q2_rerelease_q2_guest_host_actor(qa_compat_q2_rerelease_q2_guest_host *h, qa_raw_entity_view view, qa_owned_actor *out) {
  if (set_contains(h->retired, h->retired_count, view.slot)) return false;
  const qa_compat_q2_rerelease_semantic_bindings *semantics = &h->options.semantics;
  if (semantics->project) return semantics->project(semantics->ctx, view, h->module, out);
  if (h->foreign) {
    qa_compat_q2_rerelease_lookup projected = qa_compat_q2_rerelease_foreign_actors_lookup(h->foreign, view, out);
    if (projected != QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED) return projected == QA_COMPAT_Q2_RR_LOOKUP_ACTOR;
  }
  qa_compat_q2_memory *memory = hmem(h);
  qa_q2_foundation_host *engine = h->options.engine;
  bool live = qa_compat_q2_mem_read_u8(memory, at_field(h, view, "inuse")) != 0 ||
              set_contains(h->reserved, h->reserved_count, view.slot);
  bool has_generation = semantics->has_generation;
  double generation = has_generation ? semantics->generation(semantics->ctx, view) : 0;
  lifetime_t *previous = find_lifetime(h, view.slot);
  if (previous) {
    qa_owned_actor previous_actor = previous->actor;
    bool stale = !live || previous->has_generation != has_generation ||
                 (has_generation && previous->generation != generation) ||
                 previous->address != view.address.byte_offset ||
                 !qa_session_actor_registry_is_live(engine->actors, previous_actor.id);
    if (stale) {
      remove_lifetime(h, view.slot);
      if (qa_session_actor_registry_is_live(engine->actors, previous_actor.id))
        qa_session_actor_registry_release(engine->actors, previous_actor);
    }
  }
  if (!live) return false;
  lifetime_t *current = find_lifetime(h, view.slot);
  if (current) { *out = current->actor; return true; }
  qa_owned_actor actor;
  if (!qa_session_actor_registry_at_source(engine->actors, memory->module.id, (int64_t)view.slot, &actor))
    actor = qa_session_actor_registry_allocate_at_source(engine->actors, memory->module.id, (int64_t)view.slot,
                                                         "q2-rerelease:native-edict");
  qa_compat_q2_rerelease_actor_bindings binding = semantics->bind(semantics->ctx, view, actor, h->module);
  qa_shared_body_table_bind(engine->bodies, actor, binding.body);
  if (binding.has_combat) qa_gameplay_authority_bind(engine->combat, actor, binding.combat);
  if (binding.has_power_armor_cells) qa_gameplay_authority_bind_power_armor_cells(engine->combat, actor, binding.power_armor_cells);
  if (binding.has_inventory) qa_shared_inventory_table_bind(engine->inventory, actor, binding.inventory);
  callback_wrap_t *wrap = xmalloc(sizeof *wrap);
  wrap->host = h; wrap->inner = binding.callbacks;
  wrap->next = h->wraps; h->wraps = wrap;
  qa_actor_callbacks callbacks; memset(&callbacks, 0, sizeof callbacks);
  callbacks.ctx = wrap;
  callbacks.think = binding.callbacks.think ? wrap_think : NULL;
  callbacks.touch = binding.callbacks.touch ? wrap_touch : NULL;
  callbacks.use = binding.callbacks.use ? wrap_use : NULL;
  callbacks.pain = binding.callbacks.pain ? wrap_pain : NULL;
  callbacks.die = binding.callbacks.die ? wrap_die : NULL;
  qa_actor_callback_table_bind(engine->callbacks, actor, callbacks);
  if (h->lifetime_count == h->lifetime_cap) {
    h->lifetime_cap = h->lifetime_cap ? h->lifetime_cap * 2 : 16;
    h->lifetimes = xrealloc(h->lifetimes, h->lifetime_cap * sizeof *h->lifetimes);
  }
  h->lifetimes[h->lifetime_count++] = (lifetime_t){view.slot, actor, has_generation, generation, view.address.byte_offset};
  if (semantics->bound) semantics->bound(semantics->ctx, view, actor);
  *out = actor;
  return true;
}
qa_owned_actor qa_compat_q2_rerelease_q2_guest_host_reserve_client(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  if (h->closed || slot < 1 || trunc(slot) != slot) qa_range_error("Invalid Q2 client reservation");
  set_add(&h->reserved, &h->reserved_count, &h->reserved_cap, slot);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(h->module);
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(h, table.at_slot(table.ctx, slot), &actor))
    qa_range_error("Q2 reserved source client has no actor");
  return actor;
}
bool qa_compat_q2_rerelease_q2_guest_host_is_client_reserved(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  return set_contains(h->reserved, h->reserved_count, slot);
}
void qa_compat_q2_rerelease_q2_guest_host_release_client_reservation(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  set_remove(h->reserved, &h->reserved_count, slot);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
static void release_actors(qa_compat_q2_rerelease_q2_guest_host *h) {
  qa_session_actor_registry *registry = h->options.engine->actors;
  qa_provider_id provider = hmem(h)->module.id;
  if (!h->options.semantics.project) {
    size_t count = 0;
    const qa_owned_actor *owned = qa_session_actor_registry_owned_by(registry, provider, &count);
    qa_owned_actor *copy = xmalloc(count * sizeof *copy);
    if (count) memcpy(copy, owned, count * sizeof *copy);
    for (size_t i = 0; i < count; i++) {
      qa_actor_source_result source = qa_session_actor_registry_source_of(registry, copy[i].id);
      if (source.has_value && source.provider && provider && strcmp(source.provider, provider) == 0 &&
          qa_session_actor_registry_is_live(registry, copy[i].id))
        qa_session_actor_registry_release(registry, copy[i]);
    }
    free(copy);
  } else {
    for (size_t i = 0; i < h->lifetime_count; i++)
      if (qa_session_actor_registry_is_live(registry, h->lifetimes[i].actor.id))
        qa_session_actor_registry_release(registry, h->lifetimes[i].actor);
  }
  h->lifetime_count = 0;
}
static void on_released(void *v, qa_owned_actor actor) {
  qa_compat_q2_rerelease_q2_guest_host *h = v;
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_released(h->foreign, actor);
  for (size_t i = 0; i < h->bot_count; i++)
    if (qa_same_actor(h->bots[i].actor, actor.id)) {
      memmove(&h->bots[i], &h->bots[i + 1], (h->bot_count - i - 1) * sizeof *h->bots);
      h->bot_count--;
      break;
    }
}
static void subscribe(qa_compat_q2_rerelease_q2_guest_host *h) {
  h->unsubscribe_id = qa_session_actor_registry_on_release(h->options.engine->actors, on_released, h);
  h->subscribed = true;
}
static void unsubscribe(qa_compat_q2_rerelease_q2_guest_host *h) {
  if (!h->subscribed) return;
  qa_session_actor_registry_off_release(h->options.engine->actors, h->unsubscribe_id);
  h->subscribed = false;
}
void qa_compat_q2_rerelease_q2_guest_host_rebind_world(qa_compat_q2_rerelease_q2_guest_host *h, void *ctx, void (*commit)(void *ctx)) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  if (h->closed || !h->initialized || h->filter_depth != 0)
    qa_range_error("Q2 world rebind requires an idle initialized host");
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_clear(h->foreign);
  h->reserved_count = 0;
  release_actors(h);
  unsubscribe(h);
  h->bot_count = 0;
  qa_compat_q2_memory *memory = hmem(h);
  for (size_t i = 0; i < h->surface_count; i++)
    memory->unmap(memory->ctx, h->surfaces[i].address, qa_compat_q2_rerelease_surface_layout.byte_length);
  h->surface_count = 0;
  commit(ctx);
  subscribe(h);
}
void qa_compat_q2_rerelease_q2_guest_host_reconcile(qa_compat_q2_rerelease_q2_guest_host *h) {
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(h->module);
  size_t i = 0;
  while (i < h->lifetime_count) {
    if (h->lifetimes[i].slot >= table.count) {
      qa_owned_actor actor = h->lifetimes[i].actor;
      remove_lifetime(h, h->lifetimes[i].slot);
      if (qa_session_actor_registry_is_live(h->options.engine->actors, actor.id))
        qa_session_actor_registry_release(h->options.engine->actors, actor);
    } else i++;
  }
  for (double slot = 0; slot < table.count; slot++) {
    qa_owned_actor actor;
    (void)qa_compat_q2_rerelease_q2_guest_host_actor(h, table.at_slot(table.ctx, slot), &actor);
  }
}
const qa_raw_entity_view *qa_compat_q2_rerelease_q2_guest_host_bot_entities(qa_compat_q2_rerelease_q2_guest_host *h, size_t *out_count) {
  if (h->bot_count > h->bot_scratch_cap) {
    h->bot_scratch = xrealloc(h->bot_scratch, h->bot_count * sizeof *h->bot_scratch);
    h->bot_scratch_cap = h->bot_count;
  }
  for (size_t i = 0; i < h->bot_count; i++) h->bot_scratch[i] = h->bots[i].view;
  *out_count = h->bot_count;
  return h->bot_scratch;
}
qa_guest_address qa_compat_q2_rerelease_q2_guest_host_address_for_actor(qa_compat_q2_rerelease_q2_guest_host *h, qa_actor_id actor) {
  if (h->options.semantics.project) return h->options.semantics.foreign_address(h->options.semantics.ctx, actor);
  qa_actor_source_result source = qa_session_actor_registry_source_of(h->options.engine->actors, actor);
  qa_provider_id provider = hmem(h)->module.id;
  if (source.has_value && source.provider && provider && strcmp(source.provider, provider) == 0) {
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(h->module);
    return table.at_slot(table.ctx, (double)source.slot).address;
  }
  if (h->foreign) return qa_compat_q2_rerelease_foreign_actors_address(h->foreign, actor);
  return h->options.semantics.foreign_address(h->options.semantics.ctx, actor);
}
void qa_compat_q2_rerelease_q2_guest_host_pre_init(qa_compat_q2_rerelease_q2_guest_host *h) {
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_rerelease_guest_module_pre_init(h->module);
}
void qa_compat_q2_rerelease_q2_guest_host_init(qa_compat_q2_rerelease_q2_guest_host *h) {
  if (h->closed) qa_range_error("Q2 guest host is closed");
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_rerelease_guest_module_init(h->module);
  h->initialized = true;
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
qa_guest_call_result qa_compat_q2_rerelease_q2_guest_host_call_loading(qa_compat_q2_rerelease_q2_guest_host *h, const char *name,
                                                                       const qa_guest_call_value *arguments, size_t count,
                                                                       void (*next_frame)(void *), void *next_ctx) {
  if (h->closed) qa_range_error("Q2 guest host is closed");
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_call_game_loading(h->module, name, arguments, count, next_frame, next_ctx);
  if (h->initialized) qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
  return result;
}
void qa_compat_q2_rerelease_q2_guest_host_init_loading(qa_compat_q2_rerelease_q2_guest_host *h, void (*next_frame)(void *), void *next_ctx) {
  if (h->initialized) qa_range_error("API 2023 Init already completed");
  (void)qa_compat_q2_rerelease_q2_guest_host_call_loading(h, "PreInit", NULL, 0, next_frame, next_ctx);
  (void)qa_compat_q2_rerelease_q2_guest_host_call_loading(h, "Init", NULL, 0, next_frame, next_ctx);
  h->initialized = true;
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
void qa_compat_q2_rerelease_q2_guest_host_spawn_entities_loading(qa_compat_q2_rerelease_q2_guest_host *h, const char *map, const char *entities,
                                                                 const char *spawnpoint, void (*next_frame)(void *), void *next_ctx) {
  const char *texts[3] = {map, entities, spawnpoint};
  qa_guest_address addresses[3];
  qa_guest_call_value args[3];
  for (int i = 0; i < 3; i++) {
    addresses[i] = qa_compat_q2_rerelease_guest_module_string(h->module, texts[i]);
    args[i] = qa_compat_q2_rerelease_guest_pointer(true, addresses[i]);
  }
  (void)qa_compat_q2_rerelease_q2_guest_host_call_loading(h, "SpawnEntities", args, 3, next_frame, next_ctx);
  qa_compat_q2_memory *memory = hmem(h);
  for (int i = 0; i < 3; i++) memory->unmap(memory->ctx, addresses[i], qa_compat_q2_native_allocation_bytes((double)strlen(texts[i]) + 1));
}
void qa_compat_q2_rerelease_q2_guest_host_run_frame_loading(qa_compat_q2_rerelease_q2_guest_host *h, bool main_loop,
                                                            void (*next_frame)(void *), void *next_ctx) {
  (void)qa_compat_q2_rerelease_q2_guest_host_call_loading(h, "PrepFrame", NULL, 0, next_frame, next_ctx);
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_synchronize(h->foreign);
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_bool(main_loop);
  (void)qa_compat_q2_rerelease_q2_guest_host_call_loading(h, "RunFrame", &arg, 1, next_frame, next_ctx);
}
static void check_save(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_rerelease_source_save saved) {
  if (saved.native_length && memchr(saved.native, 0, saved.native_length))
    qa_range_error("Q2 JSON save contains an embedded terminator");
  if (!h->foreign && (saved.deferred_damage_count != 0 || saved.projection_count != 0))
    qa_range_error("Native save requires its foreign actor binding");
}
void qa_compat_q2_rerelease_q2_guest_host_read_save_loading(qa_compat_q2_rerelease_q2_guest_host *h, const char *kind,
                                                            qa_compat_q2_rerelease_source_save saved, void (*next_frame)(void *), void *next_ctx,
                                                            const char *domain) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  check_save(h, saved);
  const char *dom = domain ? domain : "current";
  qa_compat_q2_memory *memory = hmem(h);
  qa_guest_address address = qa_compat_q2_allocate_native_memory(memory, (double)saved.native_length + 1, "Q2 source save input");
  memory->write(memory->ctx, address, saved.native, saved.native_length);
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_begin_restore(h->foreign, saved.projections, saved.projection_count, dom);
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_pointer(true, address);
  (void)qa_compat_q2_rerelease_q2_guest_host_call_loading(h, strcmp(kind, "game") == 0 ? "ReadGameJson" : "ReadLevelJson", &arg, 1, next_frame, next_ctx);
  if (h->foreign) {
    qa_compat_q2_rerelease_deferred_damage_restore(qa_compat_q2_rerelease_foreign_actors_get_deferred(h->foreign),
                                                   saved.deferred_damage, saved.deferred_damage_count, dom);
    qa_compat_q2_rerelease_foreign_actors_end_restore(h->foreign);
  }
  memory->unmap(memory->ctx, address, qa_compat_q2_native_allocation_bytes((double)saved.native_length + 1));
}
void qa_compat_q2_rerelease_q2_guest_host_read_save(qa_compat_q2_rerelease_q2_guest_host *h, const char *kind,
                                                    qa_compat_q2_rerelease_source_save saved, const char *domain) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  check_save(h, saved);
  const char *dom = domain ? domain : "current";
  qa_compat_q2_memory *memory = hmem(h);
  qa_guest_address address = qa_compat_q2_allocate_native_memory(memory, (double)saved.native_length + 1, "Q2 source save input");
  memory->write(memory->ctx, address, saved.native, saved.native_length);
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_begin_restore(h->foreign, saved.projections, saved.projection_count, dom);
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_guest_call_value arg = qa_compat_q2_rerelease_guest_pointer(true, address);
  (void)qa_compat_q2_rerelease_guest_module_call_game(h->module, strcmp(kind, "game") == 0 ? "ReadGameJson" : "ReadLevelJson", &arg, 1, NULL, NULL);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
  if (h->foreign) {
    qa_compat_q2_rerelease_deferred_damage_restore(qa_compat_q2_rerelease_foreign_actors_get_deferred(h->foreign),
                                                   saved.deferred_damage, saved.deferred_damage_count, dom);
    qa_compat_q2_rerelease_foreign_actors_end_restore(h->foreign);
  }
  memory->unmap(memory->ctx, address, qa_compat_q2_native_allocation_bytes((double)saved.native_length + 1));
}
void qa_compat_q2_rerelease_q2_guest_host_shutdown(qa_compat_q2_rerelease_q2_guest_host *h) {
  if (h->closed) return;
  h->closed = true;
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_close(&h->pickups);
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_close(h->foreign);
  if (h->initialized) (void)qa_compat_q2_rerelease_guest_module_call_game(h->module, "Shutdown", NULL, 0, NULL, NULL);
  release_actors(h);
  unsubscribe(h);
}
void qa_compat_q2_rerelease_q2_guest_host_spawn_entities(qa_compat_q2_rerelease_q2_guest_host *h, const char *map, const char *entities, const char *spawnpoint) {
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_rerelease_guest_module_spawn_entities(h->module, map, entities, spawnpoint);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
void qa_compat_q2_rerelease_q2_guest_host_prep_frame(qa_compat_q2_rerelease_q2_guest_host *h) {
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_rerelease_guest_module_prep_frame(h->module);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
void qa_compat_q2_rerelease_q2_guest_host_run_frame(qa_compat_q2_rerelease_q2_guest_host *h, bool main_loop) {
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_synchronize(h->foreign);
  qa_compat_q2_rerelease_guest_module_run_frame(h->module, main_loop);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
qa_compat_q2_rerelease_client_connect qa_compat_q2_rerelease_q2_guest_host_client_connect(
    qa_compat_q2_rerelease_q2_guest_host *h, double slot, const char *userinfo, const char *social_id, bool is_bot) {
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_rerelease_client_connect result = qa_compat_q2_rerelease_guest_module_client_connect(h->module, slot, userinfo, social_id, is_bot);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
  return result;
}
void qa_compat_q2_rerelease_q2_guest_host_client_begin(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_rerelease_guest_module_client_begin(h->module, slot);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
void qa_compat_q2_rerelease_q2_guest_host_client_think(qa_compat_q2_rerelease_q2_guest_host *h, double slot,
                                                       qa_q2_rerelease_user_command command, const qa_equipment_movement *equipment) {
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_rerelease_guest_module_client_think(h->module, slot, command, equipment);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
void qa_compat_q2_rerelease_q2_guest_host_client_disconnect(qa_compat_q2_rerelease_q2_guest_host *h, double slot) {
  qa_compat_q2_rerelease_guest_module_client_disconnect(h->module, slot);
  qa_compat_q2_rerelease_q2_guest_host_reconcile(h);
}
static qa_compat_q2_rerelease_source_save finish_save(qa_compat_q2_rerelease_q2_guest_host *h, const char *kind,
                                                      qa_guest_call_result written, qa_guest_address size) {
  qa_compat_q2_memory *memory = hmem(h);
  qa_guest_address output;
  bool has_output = qa_compat_q2_rerelease_result_pointer(written, &output);
  qa_compat_q2_rerelease_source_save save; memset(&save, 0, sizeof save);
  if (!has_output) {
    memory->unmap(memory->ctx, size, 8);
    qa_range_error("Q2 source save returned null");
  }
  uint64_t length = qa_compat_q2_mem_read_u64(memory, size);
  if (length > 9007199254740991ull) {
    qa_compat_q2_rerelease_core_imports_free(h->core, true, output);
    memory->unmap(memory->ctx, size, 8);
    qa_range_error("Q2 source save length exceeds host copy range");
  }
  save.native = xmalloc((size_t)length);
  save.native_length = (size_t)length;
  memory->copy(memory->ctx, output, save.native, (double)length);
  if (strcmp(kind, "level") == 0 && h->foreign) {
    size_t count = 0;
    qa_compat_q2_rerelease_deferred_damage_save *entries = qa_compat_q2_rerelease_deferred_damage_save_entries(
        qa_compat_q2_rerelease_foreign_actors_get_deferred(h->foreign), &count);
    save.deferred_damage = entries; save.deferred_damage_count = count;
    save.projections = qa_compat_q2_rerelease_foreign_actors_save_projections(h->foreign, &save.projection_count);
  }
  qa_compat_q2_rerelease_core_imports_free(h->core, true, output);
  memory->unmap(memory->ctx, size, 8);
  return save;
}
qa_compat_q2_rerelease_source_save qa_compat_q2_rerelease_q2_guest_host_write_save(qa_compat_q2_rerelease_q2_guest_host *h, const char *kind,
                                                                                   bool automatic_or_transition) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  qa_compat_q2_memory *memory = hmem(h);
  qa_guest_address size = memory->allocate(memory->ctx, 8, true, 8, "Q2 save size");
  qa_guest_call_value args[2] = {qa_compat_q2_rerelease_guest_bool(automatic_or_transition),
                                 qa_compat_q2_rerelease_guest_pointer(true, size)};
  qa_guest_call_result written = qa_compat_q2_rerelease_guest_module_call_game(
      h->module, strcmp(kind, "game") == 0 ? "WriteGameJson" : "WriteLevelJson", args, 2, NULL, NULL);
  return finish_save(h, kind, written, size);
}
qa_compat_q2_rerelease_source_save qa_compat_q2_rerelease_q2_guest_host_write_save_loading(
    qa_compat_q2_rerelease_q2_guest_host *h, const char *kind, bool automatic_or_transition, void (*next_frame)(void *), void *next_ctx) {
  if (h->has_pickups) qa_compat_q2_native_primary_pickups_assert_idle(&h->pickups);
  if (h->closed) qa_range_error("Q2 guest host is closed");
  qa_compat_q2_rerelease_core_imports_refresh_cvars(h->core);
  qa_compat_q2_memory *memory = hmem(h);
  qa_guest_address size = memory->allocate(memory->ctx, 8, true, 8, "Q2 save size");
  qa_guest_call_value args[2] = {qa_compat_q2_rerelease_guest_bool(automatic_or_transition),
                                 qa_compat_q2_rerelease_guest_pointer(true, size)};
  qa_guest_call_result written = qa_compat_q2_rerelease_guest_module_call_game_loading(
      h->module, strcmp(kind, "game") == 0 ? "WriteGameJson" : "WriteLevelJson", args, 2, next_frame, next_ctx);
  return finish_save(h, kind, written, size);
}
void qa_compat_q2_rerelease_q2_guest_host_bind_pickup_supply(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_native_pickup_supply owner) {
  if (!h->has_pickups) qa_range_error("This original game has no admitted pickup supply interface");
  qa_compat_q2_native_primary_pickups_bind_supply(&h->pickups, owner);
}
void qa_compat_q2_rerelease_q2_guest_host_unbind_pickup_supply(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_native_pickup_supply owner) {
  qa_compat_q2_native_primary_pickups_unbind_supply(&h->pickups, owner);
}
qa_compat_q2_native_pickup_supply_evaluation qa_compat_q2_rerelease_q2_guest_host_pickup_supply(qa_compat_q2_rerelease_q2_guest_host *h, const qa_original_pickup_offer *offer) {
  if (!h->has_pickups) qa_range_error("This original game has no admitted pickup supply interface");
  return qa_compat_q2_native_primary_pickups_evaluate_supply(&h->pickups, offer);
}
bool qa_compat_q2_rerelease_q2_guest_host_pickup_supply_owns(qa_compat_q2_rerelease_q2_guest_host *h, qa_actor_id actor, qa_item_id item, qa_actor_id pickup) {
  if (!h->has_pickups) qa_range_error("This original game has no admitted pickup supply interface");
  return h->pickups.has_supply && h->pickups.supply.owns(h->pickups.supply.ctx, actor, item, pickup);
}
static bool surface_address(qa_compat_q2_rerelease_q2_guest_host *h, const qa_q2_surface_info *surface, qa_guest_address *out) {
  if (!surface) return false;
  for (size_t i = 0; i < h->surface_count; i++)
    if (h->surfaces[i].surface == surface) { *out = h->surfaces[i].address; return true; }
  qa_compat_q2_memory *memory = hmem(h);
  qa_guest_address address = memory->allocate(memory->ctx, qa_compat_q2_rerelease_surface_layout.byte_length, true, 4, "Q2 trace surface");
  size_t name_length = strlen(surface->name ? surface->name : "");
  if (name_length > 31) name_length = 31;
  memory->write(memory->ctx, address, (const uint8_t *)(surface->name ? surface->name : ""), name_length);
  qa_compat_q2_mem_write_u32(memory, qa_compat_q2_mem_offset(memory, address, 32), qa_js_to_uint32(surface->flags));
  qa_compat_q2_mem_write_i32(memory, qa_compat_q2_mem_offset(memory, address, 36), qa_js_to_int32(surface->value));
  qa_guest_address a = qa_compat_q2_mem_offset(memory, address, 40);
  uint32_t v = qa_js_to_uint32(h->options.spatial.surface_id(h->options.spatial.ctx, *surface));
  qa_compat_q2_mem_write_u32(memory, a, v);
  size_t material_length = strlen(surface->material ? surface->material : "");
  if (material_length > 15) material_length = 15;
  memory->write(memory->ctx, qa_compat_q2_mem_offset(memory, address, 44),
                (const uint8_t *)(surface->material ? surface->material : ""), material_length);
  if (h->surface_count == h->surface_cap) {
    h->surface_cap = h->surface_cap ? h->surface_cap * 2 : 8;
    h->surfaces = xrealloc(h->surfaces, h->surface_cap * sizeof *h->surfaces);
  }
  h->surfaces[h->surface_count++] = (surface_entry_t){surface, address};
  *out = address;
  return true;
}
static void put_u64(uint8_t *view, size_t offset, uint64_t value) { memcpy(view + offset, &value, 8); }
static void put_vector(uint8_t *view, size_t offset, qa_vec3 value) {
  qa_compat_q2_view_set_f32(view, offset, value.x);
  qa_compat_q2_view_set_f32(view, offset + 4, value.y);
  qa_compat_q2_view_set_f32(view, offset + 8, value.z);
}
static void put_plane(uint8_t *view, size_t offset, qa_bsp_plane value) {
  put_vector(view, offset, value.plane.normal);
  qa_compat_q2_view_set_f32(view, offset + 12, value.plane.distance);
  view[offset + 16] = qa_js_to_uint8(value.type); /* setUint8 */
  view[offset + 17] = qa_js_to_uint8(value.signbits);
}
qa_guest_call_result qa_compat_q2_rerelease_q2_guest_host_encode_trace(qa_compat_q2_rerelease_q2_guest_host *h, qa_trace_result trace,
                                                                       const qa_guest_address *clipped_entity) {
  if (!trace.kind || strcmp(trace.kind, "q2") != 0) qa_range_error("Rerelease trace requires Q2 source collision fields");
  size_t length = (size_t)qa_compat_q2_rerelease_trace_layout.byte_length;
  if (length > h->trace_scratch_cap) { h->trace_scratch = xrealloc(h->trace_scratch, length); h->trace_scratch_cap = length; }
  uint8_t *view = h->trace_scratch;
  memset(view, 0, length);
  view[0] = trace.all_solid ? 1 : 0;
  view[1] = trace.start_solid ? 1 : 0;
  qa_compat_q2_view_set_f32(view, 4, trace.fraction);
  put_vector(view, 8, trace.end);
  put_plane(view, 20, trace.source_plane);
  qa_guest_address surface;
  put_u64(view, 40, surface_address(h, trace.surface, &surface) ? (uint64_t)surface.byte_offset : 0);
  qa_compat_q2_view_set_i32(view, 48, qa_js_to_int32(trace.contents)); /* setUint32: same 32 bits as ToInt32 */
  qa_guest_address hit;
  if (clipped_entity) hit = *clipped_entity;
  else if (trace.hit.kind == QA_TRACE_HIT_ACTOR) hit = qa_compat_q2_rerelease_q2_guest_host_address_for_actor(h, trace.hit.actor);
  else {
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(h->module);
    hit = table.at_slot(table.ctx, 0).address;
  }
  put_u64(view, 56, (uint64_t)hit.byte_offset);
  if (trace.has_secondary) {
    put_plane(view, 64, trace.secondary_plane);
    put_u64(view, 88, surface_address(h, trace.secondary_surface, &surface) ? (uint64_t)surface.byte_offset : 0);
  }
  qa_guest_call_result out; memset(&out, 0, sizeof out);
  out.kind = QA_GUEST_VALUE_AGGREGATE;
  out.layout = qa_compat_q2_rerelease_trace_layout;
  out.bytes.bytes = view; out.bytes.length = length;
  return out;
}
/* --- imports --- */
static qa_guest_call_result void_result(void) {
  qa_guest_call_result out; memset(&out, 0, sizeof out);
  out.kind = QA_GUEST_VALUE_VOID;
  return out;
}
static qa_guest_call_result dispatch_import(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_rerelease_import_call call);
typedef struct { qa_compat_q2_rerelease_q2_guest_host *host; qa_compat_q2_rerelease_import_call call; } boundary_ctx_t;
static qa_guest_call_result boundary_invoke(void *v) {
  boundary_ctx_t *c = v;
  return dispatch_import(c->host, c->call);
}
static qa_guest_call_result host_import(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_rerelease_import_call call) {
  if (!h->options.import_boundary) return dispatch_import(h, call);
  boundary_ctx_t ctx = {h, call};
  return h->options.import_boundary(h->options.import_boundary_ctx, call.name, call.arguments, call.argument_count,
                                    boundary_invoke, &ctx);
}
qa_guest_call_result qa_compat_q2_rerelease_q2_guest_host_dispatch_import(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_rerelease_import_call call) {
  return dispatch_import(h, call);
}
static bool foreign_lookup_defined(qa_compat_q2_rerelease_q2_guest_host *h, qa_raw_entity_view view) {
  if (!h->foreign) return false;
  qa_owned_actor found;
  return qa_compat_q2_rerelease_foreign_actors_lookup(h->foreign, view, &found) != QA_COMPAT_Q2_RR_LOOKUP_UNDEFINED;
}
static qa_guest_call_result dispatch_import(qa_compat_q2_rerelease_q2_guest_host *h, qa_compat_q2_rerelease_import_call call) {
  if (h->options.intercept_import) {
    qa_guest_call_result intercepted;
    if (h->options.intercept_import(h->options.intercept_ctx, call, h, &intercepted)) return intercepted;
  }
  if (h->options.debug_drawing_headless && call.api && strcmp(call.api, "game") == 0) {
    static const char *const draw[] = {"Draw_Line", "Draw_Point", "Draw_Circle", "Draw_Bounds", "Draw_Sphere",
                                       "Draw_Cylinder", "Draw_Ray", "Draw_Arrow", "Draw_OrientedWorldText", "Draw_StaticWorldText"};
    for (size_t i = 0; i < sizeof draw / sizeof *draw; i++) if (strcmp(call.name, draw[i]) == 0) return void_result();
  }
  qa_guest_call_result handled;
  if (h->debug_shapes && qa_compat_q2_rerelease_debug_shape_imports_invoke(h->debug_shapes, call, &handled)) return handled;
  if (h->world_text && qa_compat_q2_rerelease_world_text_imports_invoke(h->world_text, call, &handled)) return handled;
  if (h->sounds && qa_compat_q2_rerelease_sound_imports_invoke(h->sounds, call, &handled)) return handled;
  if (h->messages && qa_compat_q2_rerelease_message_imports_invoke(h->messages, call, &handled)) return handled;
  const qa_guest_call_value *args = call.arguments;
  size_t count = call.argument_count;
  qa_compat_q2_memory *memory = hmem(h);
  qa_q2_foundation_host *engine = h->options.engine;
  /* TS reads module.entities() only in the cases that need it: PreInit imports run before the edicts exist. */
#define table (qa_compat_q2_rerelease_guest_module_entities(h->module))
  const char *name = call.name;
  if (strcmp(name, "Bot_RegisterEdict") == 0 || strcmp(name, "Bot_UnRegisterEdict") == 0) {
    qa_raw_entity_view view = table.from_pointer(table.ctx, qa_compat_q2_guest_required_pointer(args, count, 0));
    if (foreign_lookup_defined(h, view)) return void_result();
    qa_owned_actor actor;
    bool has_actor = qa_compat_q2_rerelease_q2_guest_host_actor(h, view, &actor);
    if (strcmp(name, "Bot_RegisterEdict") == 0) {
      if (!has_actor) qa_range_error("Q2 bot registration requires a live source edict");
      bool present = false;
      for (size_t i = 0; i < h->bot_count; i++) if (qa_same_actor(h->bots[i].actor, actor.id)) { h->bots[i].view = view; present = true; }
      if (!present) {
        if (h->bot_count == h->bot_cap) { h->bot_cap = h->bot_cap ? h->bot_cap * 2 : 8; h->bots = xrealloc(h->bots, h->bot_cap * sizeof *h->bots); }
        h->bots[h->bot_count++] = (bot_entry_t){actor.id, view};
      }
    } else if (has_actor) {
      for (size_t i = 0; i < h->bot_count; i++)
        if (qa_same_actor(h->bots[i].actor, actor.id)) {
          memmove(&h->bots[i], &h->bots[i + 1], (h->bot_count - i - 1) * sizeof *h->bots);
          h->bot_count--;
          break;
        }
    }
    return void_result();
  }
  if (strcmp(name, "FreeTags") == 0) {
    int64_t tag = qa_compat_q2_guest_integer(args, count, 0);
    if (tag == 765 || tag == 766) {
      if (h->foreign) qa_compat_q2_rerelease_foreign_actors_clear(h->foreign);
      release_actors(h);
    }
    return qa_compat_q2_rerelease_core_imports_invoke(h->core, call);
  }
  if (strcmp(name, "BoxEdicts") == 0) {
    qa_vec3 min = read_vector(h, qa_compat_q2_guest_required_pointer(args, count, 0));
    qa_vec3 max = read_vector(h, qa_compat_q2_guest_required_pointer(args, count, 1));
    qa_guest_address list, filter, data;
    bool has_list = qa_compat_q2_guest_pointer(args, count, 2, &list);
    int64_t maximum = qa_compat_q2_guest_integer(args, count, 3);
    double area = (double)qa_compat_q2_guest_integer(args, count, 4);
    bool has_filter = qa_compat_q2_guest_pointer(args, count, 5, &filter);
    bool has_data = qa_compat_q2_guest_pointer(args, count, 6, &data);
    if (area != 1 && area != 2) qa_range_error("Q2 BoxEdicts requires AREA_SOLID or AREA_TRIGGERS");
    if (maximum < 0 || maximum > 9007199254740991ll) qa_range_error("Invalid Q2 BoxEdicts capacity");
    if (maximum > 0 && !has_list) qa_range_error("Q2 BoxEdicts output is null with nonzero capacity");
    size_t candidate_count = 0;
    const qa_actor_id *candidates = h->options.spatial.box_edicts(h->options.spatial.ctx, min, max, area, &candidate_count);
    uint64_t found = 0;
    for (size_t i = 0; i < candidate_count; i++) {
      qa_guest_address address = qa_compat_q2_rerelease_q2_guest_host_address_for_actor(h, candidates[i]);
      int64_t outcome = 0;
      if (has_filter) {
        h->filter_depth++;
        qa_guest_call_value filter_args[2] = {qa_compat_q2_rerelease_guest_pointer(true, address),
                                              qa_compat_q2_rerelease_guest_pointer(has_data, data)};
        qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(h->module, filter, h->box_filter_signature,
                                                                                filter_args, 2, NULL, NULL);
        h->filter_depth--;
        if (result.kind != QA_GUEST_VALUE_INT32) qa_range_error("Q2 BoxEdicts filter must return its source enum");
        outcome = (int64_t)result.number_value;
      }
      if ((outcome & ~65) != 0) qa_range_error("Invalid Q2 BoxEdicts filter result %lld", (long long)outcome);
      if ((outcome & 1) == 0) {
        if (maximum == 0) found++;
        else if (found < (uint64_t)maximum && has_list) {
          qa_compat_q2_mem_write_pointer(memory, qa_compat_q2_mem_offset(memory, list, (int64_t)found * 8), true, address);
          found++;
        }
      }
      if ((outcome & 64) != 0) break;
    }
    qa_guest_call_result out; memset(&out, 0, sizeof out);
    out.kind = QA_GUEST_VALUE_UINT64; out.uint64_value = found;
    return out;
  }
  if (strcmp(name, "pointcontents") == 0) {
    qa_guest_call_result out; memset(&out, 0, sizeof out);
    out.kind = QA_GUEST_VALUE_UINT32;
    out.number_value = engine->point_contents(engine->ctx, read_vector(h, qa_compat_q2_guest_required_pointer(args, count, 0))); /* TS passes the number through */
    return out;
  }
  if (strcmp(name, "inPVS") == 0 || strcmp(name, "inPHS") == 0) {
    void * value2 = h->options.spatial.ctx;
    const char * value3 = strcmp(name, "inPVS") == 0 ? "pvs" : "phs";
    qa_vec3 value4 = read_vector(h, qa_compat_q2_guest_required_pointer(args, count, 0));
    qa_vec3 value5 = read_vector(h, qa_compat_q2_guest_required_pointer(args, count, 1));
    bool value6 = qa_compat_q2_guest_integer(args, count, 2) != 0;
    return qa_compat_q2_rerelease_guest_bool(h->options.spatial.visibility(
        value2, value3, value4, value5, value6));
  }
  if (strcmp(name, "AreasConnected") == 0) {
    return qa_compat_q2_rerelease_guest_bool(h->options.spatial.areas_connected(
        h->options.spatial.ctx, (double)qa_compat_q2_guest_integer(args, count, 0),
        (double)qa_compat_q2_guest_integer(args, count, 1)));
  }
  if (strcmp(name, "SetAreaPortalState") == 0) {
    engine->set_area_portal(engine->ctx, (double)qa_compat_q2_guest_integer(args, count, 0),
                            qa_compat_q2_guest_integer(args, count, 1) != 0);
    return void_result();
  }
  if (strcmp(name, "trace") == 0) {
    qa_guest_address min_a, max_a, pass;
    bool has_min = qa_compat_q2_guest_pointer(args, count, 1, &min_a);
    bool has_max = qa_compat_q2_guest_pointer(args, count, 2, &max_a);
    bool has_pass = qa_compat_q2_guest_pointer(args, count, 4, &pass);
    if (has_min != has_max) qa_range_error("Q2 trace requires both bounds or neither");
    qa_actor_id ignore;
    const qa_actor_id *ignore_ptr = NULL;
    if (has_pass) {
      qa_owned_actor passed;
      if (qa_compat_q2_rerelease_q2_guest_host_actor(h, table.from_pointer(table.ctx, pass), &passed)) {
        ignore = passed.id; ignore_ptr = &ignore;
      }
    }
    qa_bounds bounds;
    qa_q2_trace_request request; memset(&request, 0, sizeof request);
    request.start = read_vector(h, qa_compat_q2_guest_required_pointer(args, count, 0));
    request.end = read_vector(h, qa_compat_q2_guest_required_pointer(args, count, 3));
    if (has_min && has_max) {
      bounds.min = read_vector(h, min_a); bounds.max = read_vector(h, max_a);
      request.bounds = &bounds;
    }
    request.ignore = ignore_ptr;
    request.mask = (double)qa_compat_q2_guest_integer(args, count, 5);
    qa_trace_result trace = engine->trace(engine->ctx, request);
    return qa_compat_q2_rerelease_q2_guest_host_encode_trace(h, trace, NULL);
  }
  if (strcmp(name, "linkentity") == 0 || strcmp(name, "unlinkentity") == 0) {
    qa_guest_address address2;
    qa_vec3 vector;
    qa_raw_entity_view view = table.from_pointer(table.ctx, qa_compat_q2_guest_required_pointer(args, count, 0));
    if (view.slot == 0) return void_result();
    if (foreign_lookup_defined(h, view)) return void_result();
    qa_owned_actor actor;
    if (!qa_compat_q2_rerelease_q2_guest_host_actor(h, view, &actor)) {
      qa_compat_q2_mem_write_u8(memory, at_field(h, view, "linked"), 0);
      return void_result();
    }
    if (strcmp(name, "unlinkentity") == 0) {
      qa_shared_body_table_unlink(engine->bodies, actor);
      qa_compat_q2_mem_write_u8(memory, at_field(h, view, "linked"), 0);
      return void_result();
    }
    uint32_t solid = qa_compat_q2_mem_read_u8(memory, at_field(h, view, "solid"));
    if (solid > 3) qa_range_error("Unsupported source Q2 solid %u", solid);
    double model = 0;
    const double *model_ptr = NULL;
    if (solid == 3) {
      model = h->options.spatial.inline_model(h->options.spatial.ctx,
                                              qa_compat_q2_mem_read_i32(memory, at_field(h, view, "s.modelindex")));
      model_ptr = &model;
    }
    engine->set_solid(engine->ctx, actor, solid == 0 ? "none" : solid == 1 ? "trigger" : solid == 2 ? "box" : "brush", model_ptr);
    if (h->options.spatial.prepare_link) h->options.spatial.prepare_link(h->options.spatial.ctx, actor, view);
    qa_shared_body_table_link(engine->bodies, actor, NULL);
    qa_compat_q2_rerelease_link_metadata metadata = h->options.spatial.link_metadata(h->options.spatial.ctx, actor, view);
    qa_linked_body_result linked = qa_shared_body_table_linked(engine->bodies, actor.id);
    if (!linked.has_value) qa_range_error("Shared body failed to retain source link");
    write_vector(h, at_field(h, view, "absmin"), linked.value.absolute_bounds.min);
    write_vector(h, at_field(h, view, "absmax"), linked.value.absolute_bounds.max);
    qa_vec3 mins = read_vector(h, at_field(h, view, "mins"));
    qa_vec3 maxs = read_vector(h, at_field(h, view, "maxs"));
    write_vector(h, at_field(h, view, "size"), (qa_vec3){maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z});
    qa_compat_q2_mem_write_i32(memory, at_field(h, view, "areanum"), qa_js_to_int32(metadata.area));
    qa_compat_q2_mem_write_i32(memory, at_field(h, view, "areanum2"), qa_js_to_int32(metadata.area2));
    qa_compat_q2_mem_write_u32(memory, at_field(h, view, "s.solid"), qa_js_to_uint32(metadata.network_solid));
    if (qa_compat_q2_mem_read_i32(memory, at_field(h, view, "linkcount")) == 0 &&
        (qa_compat_q2_mem_read_u32(memory, at_field(h, view, "s.renderfx")) & 128) == 0)
      (address2 = at_field(h, view, "s.old_origin"), vector = read_vector(h, at_field(h, view, "s.origin")), write_vector(h, address2, vector));
    qa_guest_address a3 = at_field(h, view, "linkcount");
    int v2 = qa_compat_q2_mem_read_i32(memory, at_field(h, view, "linkcount")) + 1;
    qa_compat_q2_mem_write_i32(memory, a3, v2);
    qa_compat_q2_mem_write_u8(memory, at_field(h, view, "linked"), solid == 0 ? 0 : 1);
    return void_result();
  }
  if (strcmp(name, "setmodel") == 0) {
    qa_raw_entity_view view = table.from_pointer(table.ctx, qa_compat_q2_guest_required_pointer(args, count, 0));
    char *model_name = qa_compat_q2_read_guest_string(memory, qa_compat_q2_guest_required_pointer(args, count, 1), -1);
    qa_guest_address a2 = at_field(h, view, "s.modelindex");
    int32_t v = qa_js_to_int32(h->options.services.resource_index(h->options.services.ctx, "model", model_name));
    qa_compat_q2_mem_write_i32(memory, a2, v);
    if (model_name[0] == '*') {
      const char *text = model_name + 1;
      double model = 0;
      bool valid = true;
      if (text[0] != '\0') {
        model = qa_js_string_to_number(text); /* Number(name.slice(1)); strtod missed "0b11" and " 5 " */
        valid = !isnan(model);
      }
      if (!valid || trunc(model) != model || model < 0 || model > 9007199254740991.0) {
        free(model_name);
        qa_range_error("Invalid source inline model name");
      }
      qa_bounds bounds = engine->inline_model_bounds(engine->ctx, model);
      write_vector(h, at_field(h, view, "mins"), bounds.min);
      write_vector(h, at_field(h, view, "maxs"), bounds.max);
      qa_compat_q2_rerelease_import_call link = call;
      qa_guest_call_value fallback; memset(&fallback, 0, sizeof fallback);
      fallback.kind = QA_GUEST_VALUE_POINTER;
      const qa_guest_call_value *link_args = count > 0 ? args : &fallback;
      link.name = "linkentity"; link.arguments = link_args; link.argument_count = 1;
      (void)host_import(h, link);
    }
    free(model_name);
    return void_result();
  }
  return qa_compat_q2_rerelease_core_imports_invoke(h->core, call);
#undef table
}
static bool module_actor_at_slot(void *v, double slot, qa_actor_id *out) {
  qa_compat_q2_rerelease_q2_guest_host *h = v;
  if (!h->options.semantics.project) {
    qa_owned_actor actor;
    if (!qa_session_actor_registry_at_source(h->options.engine->actors, hmem(h)->module.id, (int64_t)slot, &actor)) return false;
    *out = actor.id;
    return true;
  }
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(h->module);
  qa_owned_actor actor;
  if (!qa_compat_q2_rerelease_q2_guest_host_actor(h, table.at_slot(table.ctx, slot), &actor)) return false;
  *out = actor.id;
  return true;
}
static qa_guest_call_result module_invoke_import(void *v, qa_compat_q2_rerelease_import_call call) {
  return host_import(v, call);
}
static double source_slot(void *v, qa_guest_address address) {
  qa_compat_q2_rerelease_q2_guest_host *h = v;
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(h->module);
  return table.from_pointer(table.ctx, address).slot;
}
static qa_guest_call_result pickup_invoke(void *ctx, qa_guest_address target, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  qa_compat_q2_rerelease_q2_guest_host *h = ctx;
  return qa_compat_q2_rerelease_guest_module_invoke(h->module, target, signature, values, count, NULL, NULL);
}
static qa_raw_entity_view pickup_record(void *ctx, qa_guest_address address) {
  qa_compat_q2_rerelease_q2_guest_host *h = ctx;
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(h->module);
  return table.from_pointer(table.ctx, address);
}
static bool pickup_current(void *ctx, qa_raw_entity_view record, qa_actor_id *out) {
  qa_compat_q2_rerelease_q2_guest_host *h = ctx;
  qa_owned_actor actor;
  if (set_contains(h->retired, h->retired_count, record.slot)) return false;
  if (!qa_session_actor_registry_at_source(h->options.engine->actors, hmem(h)->module.id, (int64_t)record.slot, &actor)) return false;
  *out = actor.id;
  return true;
}
static const qa_original_pickup_admission *pickup_admission(void *ctx) {
  qa_compat_q2_rerelease_q2_guest_host *h = ctx;
  if (!h->options.has_pickups) qa_range_error("API2023 primary pickup authority disappeared");
  return h->options.pickup_admission;
}
static void *pickup_with_protection(void *ctx, qa_raw_entity_view recipient, qa_protection_channel channel, void *operation_ctx, void (*current)(void *),
                                    void *(*operation)(void *, void *, qa_compat_q2_native_pickup_consume_fn), bool (*committed)(void *)) {
  return qa_compat_q2_with_rerelease_primary_protection(ctx, recipient, channel, operation_ctx, current, operation, committed);
}
qa_compat_q2_rerelease_q2_guest_host *qa_compat_q2_rerelease_q2_guest_host_create(const qa_compat_q2_rerelease_q2_host_options *options) {
  if (options->has_pickups && options->semantics.project)
    qa_range_error("Primary pickup callers cannot bind a component projection");
  if (options->debug_drawing_headless && (options->debug_shapes || options->world_text))
    qa_range_error("Headless Q2 debug drawing cannot bind renderer callbacks");
  qa_compat_q2_rerelease_q2_guest_host *h = calloc(1, sizeof *h);
  if (!h) return NULL;
  h->options = *options;
  qa_compat_q2_memory *memory = options->module_options.runner->memory;
  h->core = qa_compat_q2_rerelease_core_imports_create(memory, options->services);
  if (!h->core) { free(h); return NULL; }
  qa_compat_q2_rerelease_module_options module_options = options->module_options;
  module_options.actor_ctx = h; module_options.actor_at_slot = module_actor_at_slot;
  module_options.invoke_import_ctx = h; module_options.invoke_import = module_invoke_import;
  h->module = qa_compat_q2_rerelease_guest_module_create(&module_options);
  if (!h->module) qa_range_error("Q2 rerelease host out of memory");
  h->box_params[0] = qa_compat_q2_rr_pointer_layout;
  h->box_params[1] = qa_compat_q2_rr_pointer_layout;
  h->box_filter_signature = qa_compat_q2_rerelease_signature(h->box_params, 2, false, qa_compat_q2_rr_int_layout);
  qa_compat_q2_memory *module_memory = hmem(h);
  if (options->debug_shapes) {
    h->debug_shapes = qa_compat_q2_rerelease_debug_shape_imports_create(module_memory, options->debug_shapes_ctx, options->debug_shapes);
    if (!h->debug_shapes) qa_range_error("Q2 rerelease host out of memory");
  }
  if (options->world_text) {
    h->world_text = qa_compat_q2_rerelease_world_text_imports_create(module_memory, options->world_text_ctx, options->world_text);
    if (!h->world_text) qa_range_error("Q2 rerelease host out of memory");
  }
  if (options->sound) {
    h->sounds = qa_compat_q2_rerelease_sound_imports_create(module_memory, options->sound_ctx, options->sound, h, source_slot);
    if (!h->sounds) qa_range_error("Q2 rerelease host out of memory");
  }
  if (options->messages) {
    h->messages = qa_compat_q2_rerelease_message_imports_create(module_memory, *options->messages, h, source_slot);
    if (!h->messages) qa_range_error("Q2 rerelease host out of memory");
  }
  if (options->foreign_damage && !options->has_native_entries)
    qa_range_error("Foreign native damage requires verified entry points");
  if (options->foreign_damage && options->has_native_entries) {
    h->foreign = qa_compat_q2_rerelease_foreign_actors_create(h, options->native_entries, *options->foreign_damage);
    if (!h->foreign) qa_range_error("Q2 rerelease host out of memory");
  }
  subscribe(h);
  if (options->has_pickups) {
    const qa_compat_q2_native_pickup_profile *profile = options->pickup_profile;
    if (!profile) profile = qa_compat_q2_rerelease_get_pickup_profile(module_memory->module.digest);
    if (profile) {
      qa_compat_q2_native_pickup_host pickup_host;
      memset(&pickup_host, 0, sizeof pickup_host);
      pickup_host.memory = module_memory; pickup_host.runner = options->module_options.runner; pickup_host.ctx = h;
      pickup_host.invoke = pickup_invoke; pickup_host.record = pickup_record; pickup_host.current = pickup_current;
      pickup_host.admission = pickup_admission; pickup_host.with_protection = pickup_with_protection;
      qa_compat_q2_native_primary_pickups_init(&h->pickups, pickup_host, options->pickup_image_base, profile);
      h->has_pickups = true;
    }
  }
  return h;
}
void qa_compat_q2_rerelease_q2_guest_host_destroy(qa_compat_q2_rerelease_q2_guest_host *h) {
  if (!h) return;
  qa_compat_q2_rerelease_q2_guest_host_shutdown(h);
  if (h->foreign) qa_compat_q2_rerelease_foreign_actors_destroy(h->foreign);
  if (h->messages) qa_compat_q2_rerelease_message_imports_destroy(h->messages);
  if (h->sounds) qa_compat_q2_rerelease_sound_imports_destroy(h->sounds);
  if (h->world_text) qa_compat_q2_rerelease_world_text_imports_destroy(h->world_text);
  if (h->debug_shapes) qa_compat_q2_rerelease_debug_shape_imports_destroy(h->debug_shapes);
  qa_compat_q2_rerelease_guest_module_destroy(h->module);
  qa_compat_q2_rerelease_core_imports_destroy(h->core);
  free(h->lifetimes); free(h->retired); free(h->reserved); free(h->surfaces);
  free(h->bots); free(h->bot_scratch); free(h->trace_scratch);
  while (h->wraps) { callback_wrap_t *next = h->wraps->next; free(h->wraps); h->wraps = next; }
  free(h);
}
const qa_compat_q2_rerelease_q2_host_options *qa_compat_q2_rerelease_q2_guest_host_get_options(const qa_compat_q2_rerelease_q2_guest_host *h) {
  return &h->options;
}
qa_compat_q2_rerelease_guest_module *qa_compat_q2_rerelease_q2_guest_host_get_module(qa_compat_q2_rerelease_q2_guest_host *h) {
  return h->module;
}
qa_compat_q2_rerelease_core_imports *qa_compat_q2_rerelease_q2_guest_host_get_core(qa_compat_q2_rerelease_q2_guest_host *h) {
  return h->core;
}
qa_compat_q2_rerelease_foreign_actors *qa_compat_q2_rerelease_q2_guest_host_get_foreign_actors(qa_compat_q2_rerelease_q2_guest_host *h) {
  return h->foreign;
}
