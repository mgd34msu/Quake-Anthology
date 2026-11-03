/* Port of src/app/bootstrap/simulation/native-primary-weapons.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/native_primary_weapons.h"
#include <string.h>

typedef qa_app_sim_native_primary_weapon_context context_t;
static qa_compat_q2_classic_guest_host *classic_host(context_t *c) { return qa_app_sim_classic_guest_source_host(qa_app_sim_classic_guest_world_source(c->world.classic)); }
static qa_compat_q2_rerelease_guest_module *rerelease_module(context_t *c) {
  return qa_compat_q2_rerelease_q2_guest_host_get_module(qa_app_sim_rerelease_guest_source_host(qa_app_sim_rerelease_guest_world_source(c->world.rerelease_world)));
}
static qa_guest_call_result invoke(void *ctx, qa_guest_address address, qa_compat_q2_guest_call_signature signature, const qa_guest_call_value *values, size_t count) {
  context_t *c = ctx;
  if (!c->world.rerelease) { qa_compat_q2_classic_guest_host *host = classic_host(c); return qa_compat_q2_classic_guest_host_invoke(host, address, signature, values, count, NULL, qa_compat_q2_classic_guest_host_get_options(host)->instruction_budget); }
  return qa_compat_q2_rerelease_guest_module_invoke(rerelease_module(c), address, signature, values, count, NULL, NULL);
}
static qa_guest_address entry(void *ctx, const char *name) {
  context_t *c = ctx;
  return c->world.rerelease ? qa_app_sim_rerelease_guest_source_entry(qa_app_sim_rerelease_guest_world_source(c->world.rerelease_world), name)
                            : qa_app_sim_classic_guest_source_entry(qa_app_sim_classic_guest_world_source(c->world.classic), name);
}
static qa_raw_entity_view record(void *ctx, qa_guest_address address) {
  context_t *c = ctx;
  if (!c->world.rerelease) return qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_guest_host_get_edicts(classic_host(c)), address);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(rerelease_module(c)); return table.from_pointer(table.ctx, address);
}
static bool actor(void *ctx, qa_raw_entity_view view, qa_actor_id *out) {
  context_t *c = ctx;
  if (!c->world.rerelease) { qa_owned_actor owned; if (!qa_compat_q2_classic_edicts_current(qa_compat_q2_classic_guest_host_get_edicts(classic_host(c)), view, &owned)) return false; *out = owned.id; return true; }
  qa_actor_id_result current = view.current_actor(view.ctx); if (current.has_value) *out = current.value; return current.has_value;
}
static bool record_for(void *ctx, qa_actor_id id, qa_raw_entity_view *out) {
  context_t *c = ctx; qa_actor_source_result position = qa_session_actor_registry_source_of(c->actors, id);
  qa_module_identity module = c->world.rerelease ? qa_app_sim_rerelease_guest_world_module(c->world.rerelease_world) : qa_app_sim_classic_guest_world_module(c->world.classic);
  if (!position.has_value || position.provider == NULL || module.id == NULL || strcmp(position.provider, module.id) != 0) return false;
  if (!c->world.rerelease) *out = qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_guest_host_get_edicts(classic_host(c)), (double)position.slot);
  else { qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(rerelease_module(c)); *out = table.at_slot(table.ctx, (double)position.slot); }
  return true;
}
qa_compat_q2_native_primary_weapon_host qa_app_sim_native_primary_weapon_host(context_t *context, qa_app_sim_q2_native_world world, qa_session_actor_registry *actors) {
  context->world = world; context->actors = actors;
  qa_compat_q2_native_primary_weapon_host host; memset(&host, 0, sizeof host);
  if (!world.rerelease) {
    qa_app_sim_classic_guest_source *source = qa_app_sim_classic_guest_world_source(world.classic);
    host.memory = qa_app_sim_classic_guest_source_memory(source); host.runner = qa_compat_q2_classic_guest_host_get_options(classic_host(context))->runner;
    host.image = qa_app_sim_classic_guest_source_image_base(source);
  } else {
    qa_app_sim_rerelease_guest_source *source = qa_app_sim_rerelease_guest_world_source(world.rerelease_world);
    host.memory = qa_compat_q2_rerelease_guest_module_get_memory(rerelease_module(context));
    host.runner = qa_compat_q2_rerelease_guest_module_get_options(rerelease_module(context))->runner;
    host.image = qa_app_sim_rerelease_guest_source_image_base(source);
  }
  host.ctx = context; host.invoke = invoke; host.entry = entry; host.record = record; host.actor = actor; host.record_for = record_for;
  return host;
}
qa_compat_q2_native_primary_weapons *qa_app_sim_bind_native_primary_weapons(context_t *context, qa_app_sim_q2_native_world world, qa_session_actor_registry *actors,
                                                                            const qa_compat_q2_native_primary_weapon_profile *profile, qa_compat_q2_native_primary_weapon_hooks hooks) {
  return qa_compat_q2_native_primary_weapons_create(qa_app_sim_native_primary_weapon_host(context, world, actors), profile, hooks);
}
