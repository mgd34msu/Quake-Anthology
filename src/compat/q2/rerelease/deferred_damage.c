/* Port of src/compat/q2/rerelease/deferred-damage.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/deferred_damage.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/native_combat_call.h"
#include "qa/compat/q2/rerelease/foreign_actors.h"
#include "qa/compat/q2/rerelease/host.h"
#include "qa/compat/q2/rerelease/source_state.h"
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/foundation/cleanup.h"

typedef struct { qa_damage_request request; qa_actor_id attacker, inflictor; } stored_request_t;
static void store_request(stored_request_t *dst, qa_damage_request src) {
  dst->request = qa_capture_request(src);
  if (src.attack.attacker) { dst->attacker = *src.attack.attacker; dst->request.attack.attacker = &dst->attacker; }
  if (src.attack.inflictor) { dst->inflictor = *src.attack.inflictor; dst->request.attack.inflictor = &dst->inflictor; }
}
typedef struct tracked tracked_t;
typedef struct { tracked_t *tracked; uint64_t handle; } obs_t;
struct tracked {
  qa_compat_q2_rerelease_deferred_damage *owner;
  qa_owned_actor actor;
  qa_raw_entity_view view;
  double generation;
  qa_cleanup_stack remove;
  bool has_pending;
  stored_request_t pending;
  obs_t obs[2];
};
typedef struct { uint64_t stack; stored_request_t request; } call_t;
struct qa_compat_q2_rerelease_deferred_damage {
  qa_compat_q2_rerelease_q2_guest_host *host;
  qa_compat_q2_rerelease_foreign_damage_services services;
  void *ctx;
  bool (*current_request)(void *, qa_actor_id, qa_damage_request *);
  bool (*intercepted)(void *);
  tracked_t **tracked; size_t tracked_count, tracked_cap;
  call_t **calls; size_t call_count, call_cap;
  uint64_t remove_entry, remove_pain;
  qa_guest_value_layout sig_storage[2][16];
  qa_compat_q2_guest_call_signature damage_signature, process_signature;
};
static qa_compat_q2_rerelease_guest_module *dmod(qa_compat_q2_rerelease_deferred_damage *d) {
  return qa_compat_q2_rerelease_q2_guest_host_get_module(d->host);
}
static qa_compat_q2_memory *dmem(qa_compat_q2_rerelease_deferred_damage *d) {
  return qa_compat_q2_rerelease_guest_module_get_memory(dmod(d));
}
static qa_q2_foundation_host *dengine(qa_compat_q2_rerelease_deferred_damage *d) {
  return qa_compat_q2_rerelease_q2_guest_host_get_options(d->host)->engine;
}
static qa_compat_q2_runner *drunner(qa_compat_q2_rerelease_deferred_damage *d) {
  return qa_compat_q2_rerelease_guest_module_get_options(dmod(d))->runner;
}
static qa_compat_q2_rerelease_source_edict *xsource(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view) {
  qa_compat_q2_rerelease_source_edict *e = qa_compat_q2_rerelease_source_edict_create(view, dmod(d));
  if (!e) qa_range_error("Q2 rerelease deferred damage out of memory");
  return e;
}
static qa_guest_address at_view(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view, double offset) {
  return qa_compat_q2_mem_offset(dmem(d), view.address, (int64_t)offset);
}
static const qa_compat_q2_rerelease_primary_world_profile *dprofile(qa_compat_q2_rerelease_deferred_damage *d) {
  return qa_compat_q2_rerelease_guest_module_require_world_profile(dmod(d));
}
static bool host_actor(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view, qa_owned_actor *out) {
  return qa_compat_q2_rerelease_q2_guest_host_actor(d->host, view, out);
}
static tracked_t *find_tracked(qa_compat_q2_rerelease_deferred_damage *d, qa_actor_id id) {
  for (size_t i = 0; i < d->tracked_count; i++) if (qa_same_actor(d->tracked[i]->actor.id, id)) return d->tracked[i];
  return NULL;
}
static bool entry_valid(qa_compat_q2_rerelease_deferred_damage *d, tracked_t *entry) {
  qa_compat_q2_rerelease_source_edict *source = xsource(d, entry->view);
  bool ok = qa_session_actor_registry_is_live(dengine(d)->actors, entry->actor.id) &&
            qa_compat_q2_rerelease_source_edict_generation(source) == entry->generation &&
            qa_compat_q2_mem_read_u8(dmem(d), qa_compat_q2_rerelease_source_edict_at(source, "shared.inuse")) != 0;
  qa_compat_q2_rerelease_source_edict_destroy(source);
  return ok;
}
static qa_vec3 monster_point(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view) {
  double base = dprofile(d)->monster.point;
  qa_compat_q2_memory *m = dmem(d);
  return (qa_vec3){qa_compat_q2_mem_read_f32(m, at_view(d, view, base)), qa_compat_q2_mem_read_f32(m, at_view(d, view, base + 4)),
                   qa_compat_q2_mem_read_f32(m, at_view(d, view, base + 8))};
}
static void unobserve(void *v) {
  obs_t *obs = v;
  qa_compat_q2_memory *m = dmem(obs->tracked->owner);
  if (m->unobserve_writes) m->unobserve_writes(m->ctx, obs->handle);
}
static void mod_written(void *v) {
  tracked_t *entry = v;
  qa_compat_q2_rerelease_deferred_damage *d = entry->owner;
  qa_owned_actor actor = entry->actor;
  if (!entry_valid(d, entry)) { qa_compat_q2_rerelease_deferred_damage_release(d, actor); return; }
  uint64_t stack = drunner(d)->read_rsp(drunner(d)->ctx, 64);
  call_t *found = NULL;
  for (size_t i = d->call_count; i > 0; i--) {
    call_t *call = d->calls[i - 1];
    if (call->stack >= stack && qa_same_actor(call->request.request.target, actor.id)) { found = call; break; }
  }
  if (!found) qa_range_error("Native monster accumulation has no damage call provenance");
  store_request(&entry->pending, found->request.request);
  entry->has_pending = true;
}
static void blood_written(void *v) {
  tracked_t *entry = v;
  qa_compat_q2_rerelease_deferred_damage *d = entry->owner;
  if (qa_compat_q2_mem_read_i32(dmem(d), at_view(d, entry->view, dprofile(d)->monster.blood)) == 0) entry->has_pending = false;
}
void qa_compat_q2_rerelease_deferred_damage_track(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view) {
  qa_compat_q2_rerelease_source_edict *source = xsource(d, view);
  bool monster = (qa_compat_q2_mem_read_u32(dmem(d), qa_compat_q2_rerelease_source_edict_at(source, "shared.svflags")) & 4) != 0;
  double generation = qa_compat_q2_rerelease_source_edict_generation(source);
  qa_compat_q2_rerelease_source_edict_destroy(source);
  if (!monster) return;
  qa_owned_actor actor;
  if (!host_actor(d, view, &actor) || find_tracked(d, actor.id)) return;
  tracked_t *entry = calloc(1, sizeof *entry);
  if (!entry) qa_range_error("Q2 rerelease deferred damage out of memory");
  entry->owner = d; entry->actor = actor; entry->view = view; entry->generation = generation;
  qa_cleanup_stack_init(&entry->remove);
  if (d->tracked_count == d->tracked_cap) {
    d->tracked_cap = d->tracked_cap ? d->tracked_cap * 2 : 8;
    d->tracked = realloc(d->tracked, d->tracked_cap * sizeof *d->tracked);
    if (!d->tracked) qa_range_error("Q2 rerelease deferred damage out of memory");
  }
  d->tracked[d->tracked_count++] = entry;
  qa_compat_q2_memory *memory = dmem(d);
  const qa_compat_q2_rerelease_primary_world_profile *profile = dprofile(d);
  entry->obs[0].tracked = entry;
  entry->obs[0].handle = memory->observe_writes(memory->ctx, at_view(d, view, profile->monster.mod + 2), 1, entry, mod_written);
  qa_cleanup_stack_add(&entry->remove, unobserve, &entry->obs[0]);
  entry->obs[1].tracked = entry;
  entry->obs[1].handle = memory->observe_writes(memory->ctx, at_view(d, view, profile->monster.blood), 4, entry, blood_written);
  qa_cleanup_stack_add(&entry->remove, unobserve, &entry->obs[1]);
}
static qa_actor_id request_actor(qa_compat_q2_rerelease_deferred_damage *d, const qa_guest_call_value *args, size_t count, size_t index) {
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(dmod(d));
  qa_owned_actor found;
  if (!host_actor(d, table.from_pointer(table.ctx, qa_compat_q2_guest_required_pointer(args, count, index)), &found))
    qa_range_error("Native damage references a stale actor");
  return found.id;
}
static qa_vec3 request_vector(qa_compat_q2_rerelease_deferred_damage *d, const qa_guest_call_value *args, size_t count, size_t index) {
  qa_guest_address address = qa_compat_q2_guest_required_pointer(args, count, index);
  qa_compat_q2_memory *m = dmem(d);
  return (qa_vec3){qa_compat_q2_mem_read_f32(m, address), qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 4)),
                   qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 8))};
}
typedef struct { qa_damage_request request; qa_actor_id attacker, inflictor; } request_parts_t;
static qa_damage_request native_request(qa_compat_q2_rerelease_deferred_damage *d, qa_actor_id target, const qa_guest_call_value *args, size_t count,
                                         request_parts_t *parts) {
  const qa_guest_call_value *mod = count > 9 ? &args[9] : NULL;
  if (!mod || mod->kind != QA_GUEST_VALUE_AGGREGATE || mod->bytes.length != 3) qa_range_error("Invalid native monster mod_t");
  double id = mod->bytes.bytes[0];
  qa_q2_native_cause native;
  memset(&native, 0, sizeof native);
  native.edition = "rerelease"; native.id = id;
  native.friendly_fire = mod->bytes.bytes[1] != 0;
  native.no_point_loss = mod->bytes.bytes[2] != 0;
  double cause;
  if (!qa_q2mp_canonical_cause_from_native(native, &cause)) qa_range_error("Unclassified native monster damage cause");
  parts->attacker = request_actor(d, args, count, 2);
  parts->inflictor = request_actor(d, args, count, 1);
  double damage_flags = (double)qa_compat_q2_guest_integer(args, count, 8);
  qa_damage_request request;
  memset(&request, 0, sizeof request);
  request.target = target;
  request.amount = (double)qa_compat_q2_guest_integer(args, count, 6);
  request.knockback = (double)qa_compat_q2_guest_integer(args, count, 7);
  request.point = request_vector(d, args, count, 4);
  request.direction = request_vector(d, args, count, 3);
  request.normal = request_vector(d, args, count, 5);
  request.delivery = ((int64_t)damage_flags & 1) != 0 ? "radius" : "direct";
  request.attack = d->services.provenance(d->services.ctx, parts->attacker, parts->inflictor, target);
  request.attack.attacker = &parts->attacker;
  request.attack.inflictor = &parts->inflictor;
  request.attack.cause.kind = QA_ATTACK_CAUSE_Q2;
  request.attack.cause.means_of_death = cause;
  request.attack.cause.damage_flags = damage_flags;
  request.attack.cause.has_native = true;
  request.attack.cause.native = native;
  parts->request = request;
  return request;
}
static bool field_pointer(qa_compat_q2_rerelease_deferred_damage *d, const qa_compat_q2_native_combat_call *call,
                          qa_compat_q2_guest_call_signature signature, qa_guest_address *out) {
  qa_guest_call_value value = drunner(d)->read_combat_field(drunner(d)->ctx, call, signature, "target");
  return qa_compat_q2_guest_pointer(&value, 1, 0, out);
}
static void damage_entered(void *v) {
  qa_compat_q2_rerelease_deferred_damage *d = v;
  if (d->intercepted(d->ctx)) return;
  const qa_compat_q2_rerelease_primary_world_profile *profile = dprofile(d);
  qa_guest_address address;
  if (!field_pointer(d, &profile->calls.damage, d->damage_signature, &address)) return;
  uint64_t stack = drunner(d)->read_rsp(drunner(d)->ctx, 64);
  while (d->call_count > 0 && d->calls[d->call_count - 1]->stack <= stack) free(d->calls[--d->call_count]);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(dmod(d));
  qa_raw_entity_view view = table.from_pointer(table.ctx, address);
  qa_compat_q2_rerelease_source_edict *source = xsource(d, view);
  bool monster = (qa_compat_q2_mem_read_u32(dmem(d), qa_compat_q2_rerelease_source_edict_at(source, "shared.svflags")) & 4) != 0;
  qa_compat_q2_rerelease_source_edict_destroy(source);
  if (!monster) return;
  qa_owned_actor actor;
  if (!host_actor(d, view, &actor)) return;
  qa_compat_q2_rerelease_deferred_damage_track(d, view);
  call_t *call = calloc(1, sizeof *call);
  if (!call) qa_range_error("Q2 rerelease deferred damage out of memory");
  call->stack = stack;
  qa_damage_request request;
  request_parts_t parts;
  if (!d->current_request(d->ctx, actor.id, &request)) {
    qa_guest_call_value raw[16], lowered[10];
    size_t raw_count = drunner(d)->adapter_arguments(drunner(d)->ctx, d->damage_signature, raw, 16);
    size_t count = qa_compat_q2_read_native_combat_arguments(&profile->calls.damage, QA_Q2CO_DAMAGE, raw, raw_count, 8, lowered);
    request = native_request(d, actor.id, lowered, count, &parts);
  }
  store_request(&call->request, request);
  if (d->call_count == d->call_cap) {
    d->call_cap = d->call_cap ? d->call_cap * 2 : 8;
    d->calls = realloc(d->calls, d->call_cap * sizeof *d->calls);
    if (!d->calls) qa_range_error("Q2 rerelease deferred damage out of memory");
  }
  d->calls[d->call_count++] = call;
}
static void pain_entered(void *v) {
  qa_compat_q2_rerelease_deferred_damage *d = v;
  const qa_compat_q2_rerelease_primary_world_profile *profile = dprofile(d);
  qa_guest_address address;
  if (!field_pointer(d, &profile->calls.process_pain, d->process_signature, &address)) return;
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(dmod(d));
  qa_raw_entity_view view = table.from_pointer(table.ctx, address);
  qa_owned_actor actor;
  tracked_t *entry = host_actor(d, view, &actor) ? find_tracked(d, actor.id) : NULL;
  if (!entry || !entry_valid(d, entry) || !entry->has_pending) return;
  qa_compat_q2_memory *memory = dmem(d);
  int32_t blood = qa_compat_q2_mem_read_i32(memory, at_view(d, view, profile->monster.blood));
  if (blood == 0) return;
  stored_request_t pending = entry->pending;
  /* Retarget the copies: pending now points at its own scratch. */
  if (pending.request.attack.attacker) pending.request.attack.attacker = &pending.attacker;
  if (pending.request.attack.inflictor) pending.request.attack.inflictor = &pending.inflictor;
  entry->has_pending = false;
  qa_damage_request request = pending.request;
  request.knockback = qa_compat_q2_mem_read_i32(memory, at_view(d, view, profile->monster.knockback));
  request.point = monster_point(d, view);
  qa_compat_q2_rerelease_source_edict *source = xsource(d, view);
  double health = qa_compat_q2_rerelease_source_edict_get_health(source);
  qa_compat_q2_rerelease_source_edict_destroy(source);
  qa_source_damage_result result = {blood, health <= 0 ? "death" : "pain"};
  qa_gameplay_authority_source_reaction(dengine(d)->combat, request, result);
}
