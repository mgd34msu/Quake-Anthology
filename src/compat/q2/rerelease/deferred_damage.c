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
#include "qa/foundation/js.h"

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
  const qa_compat_q2_memory * m;
  qa_guest_address a;
  qa_compat_q2_rerelease_source_edict *source = xsource(d, entry->view);
  bool ok = qa_session_actor_registry_is_live(dengine(d)->actors, entry->actor.id) &&
            qa_compat_q2_rerelease_source_edict_generation(source) == entry->generation &&
            (m = dmem(d), a = qa_compat_q2_rerelease_source_edict_at(source, "shared.inuse"), qa_compat_q2_mem_read_u8(m, a)) != 0;
  qa_compat_q2_rerelease_source_edict_destroy(source);
  return ok;
}
static qa_vec3 monster_point(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view) {
  double base = dprofile(d)->monster.point;
  qa_compat_q2_memory *m = dmem(d);
  double x = qa_compat_q2_mem_read_f32(m, at_view(d, view, base));
  double y = qa_compat_q2_mem_read_f32(m, at_view(d, view, base + 4));
  double z = qa_compat_q2_mem_read_f32(m, at_view(d, view, base + 8));
  return (qa_vec3){x, y, z};
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
  const qa_compat_q2_memory * m = dmem(d);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(source, "shared.svflags");
  bool monster = (qa_compat_q2_mem_read_u32(m, a) & 4) != 0;
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
  double x = qa_compat_q2_mem_read_f32(m, address);
  double y = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 4));
  double z = qa_compat_q2_mem_read_f32(m, qa_compat_q2_mem_offset(m, address, 8));
  return (qa_vec3){x, y, z};
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
  request.delivery = (qa_js_to_int32(damage_flags) & 1) != 0 ? "radius" : "direct";
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
  const qa_compat_q2_memory * m = dmem(d);
  qa_guest_address a = qa_compat_q2_rerelease_source_edict_at(source, "shared.svflags");
  bool monster = (qa_compat_q2_mem_read_u32(m, a) & 4) != 0;
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
static double pointer_slot(qa_compat_q2_rerelease_deferred_damage *d, qa_raw_entity_view view, double offset) {
  qa_guest_address address;
  if (!qa_compat_q2_mem_read_pointer(dmem(d), at_view(d, view, offset), &address))
    qa_range_error("Native monster accumulation has a null edict pointer");
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(dmod(d));
  return table.from_pointer(table.ctx, address).slot;
}
qa_compat_q2_rerelease_deferred_damage *qa_compat_q2_rerelease_deferred_damage_create(
    qa_compat_q2_rerelease_q2_guest_host *host, qa_compat_q2_rerelease_foreign_damage_services services,
    void *ctx, bool (*current_request)(void *ctx, qa_actor_id actor, qa_damage_request *out),
    bool (*intercepted)(void *ctx)) {
  qa_compat_q2_rerelease_deferred_damage *d = calloc(1, sizeof *d);
  if (!d) return NULL;
  d->host = host; d->services = services; d->ctx = ctx;
  d->current_request = current_request; d->intercepted = intercepted;
  const qa_compat_q2_rerelease_q2_host_options *options = qa_compat_q2_rerelease_q2_guest_host_get_options(host);
  if (!options->has_native_entries) qa_range_error("Missing declared native entries");
  const qa_compat_q2_rerelease_primary_world_profile *profile = dprofile(d);
  d->damage_signature = qa_compat_q2_native_combat_signature(&profile->calls.damage, QA_Q2CO_DAMAGE,
                                                             qa_compat_q2_rerelease_abi, d->sig_storage[0]);
  d->process_signature = qa_compat_q2_native_combat_signature(&profile->calls.process_pain, QA_Q2CO_DEFERRED,
                                                              qa_compat_q2_rerelease_abi, d->sig_storage[1]);
  qa_compat_q2_runner *runner = drunner(d);
  d->remove_entry = runner->observe_entry(runner->ctx, options->native_entries.damage, d, damage_entered);
  d->remove_pain = runner->observe_entry(runner->ctx, options->native_entries.process_pain, d, pain_entered);
  return d;
}
void qa_compat_q2_rerelease_deferred_damage_release(qa_compat_q2_rerelease_deferred_damage *d, qa_owned_actor actor) {
  for (size_t i = 0; i < d->tracked_count; i++) {
    if (!qa_same_actor(d->tracked[i]->actor.id, actor.id)) continue;
    tracked_t *entry = d->tracked[i];
    memmove(&d->tracked[i], &d->tracked[i + 1], (d->tracked_count - i - 1) * sizeof *d->tracked);
    d->tracked_count--;
    qa_cleanup_stack_run_all(&entry->remove);
    qa_cleanup_stack_destroy(&entry->remove);
    free(entry);
    return;
  }
}
static void clear_calls(qa_compat_q2_rerelease_deferred_damage *d) {
  for (size_t i = 0; i < d->call_count; i++) free(d->calls[i]);
  d->call_count = 0;
}
void qa_compat_q2_rerelease_deferred_damage_clear(qa_compat_q2_rerelease_deferred_damage *d) {
  while (d->tracked_count > 0) qa_compat_q2_rerelease_deferred_damage_release(d, d->tracked[0]->actor);
  clear_calls(d);
}
void qa_compat_q2_rerelease_deferred_damage_close(qa_compat_q2_rerelease_deferred_damage *d) {
  qa_compat_q2_runner *runner = drunner(d);
  if (d->remove_pain) { runner->remove_entry(runner->ctx, d->remove_pain); d->remove_pain = 0; }
  if (d->remove_entry) { runner->remove_entry(runner->ctx, d->remove_entry); d->remove_entry = 0; }
  qa_compat_q2_rerelease_deferred_damage_clear(d);
  clear_calls(d);
}
void qa_compat_q2_rerelease_deferred_damage_destroy(qa_compat_q2_rerelease_deferred_damage *d) {
  if (!d) return;
  qa_compat_q2_rerelease_deferred_damage_close(d);
  free(d->tracked); free(d->calls); free(d);
}
qa_compat_q2_rerelease_saved_actor qa_compat_q2_rerelease_deferred_damage_save_actor(
    qa_compat_q2_rerelease_deferred_damage *d, qa_actor_id actor) {
  qa_actor_source_result source = qa_session_actor_registry_source_of(dengine(d)->actors, actor);
  qa_compat_q2_memory *memory = dmem(d);
  if (source.has_value && source.provider && memory->module.id && strcmp(source.provider, memory->module.id) == 0) {
    qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(dmod(d));
    qa_raw_entity_view view = table.at_slot(table.ctx, (double)source.slot);
    qa_owned_actor current;
    if (host_actor(d, view, &current) && qa_same_actor(current.id, actor)) {
      qa_compat_q2_rerelease_source_edict *edict = xsource(d, view);
      double generation = qa_compat_q2_rerelease_source_edict_generation(edict);
      qa_compat_q2_rerelease_source_edict_destroy(edict);
      qa_compat_q2_rerelease_saved_actor out; memset(&out, 0, sizeof out);
      out.is_native = true; out.slot = (double)source.slot; out.generation = generation;
      return out;
    }
  }
  qa_compat_q2_rerelease_saved_actor out; memset(&out, 0, sizeof out);
  out.is_native = false;
  out.shared.slot = (double)actor.slot; out.shared.generation = (double)actor.generation;
  return out;
}
qa_actor_id qa_compat_q2_rerelease_deferred_damage_restore_actor(qa_compat_q2_rerelease_deferred_damage *d,
                                                                 qa_compat_q2_rerelease_saved_actor saved, const char *domain) {
  if (!saved.is_native)
    return qa_session_actor_registry_reference_saved(dengine(d)->actors, saved.shared, domain ? domain : "current");
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(dmod(d));
  qa_raw_entity_view view = table.at_slot(table.ctx, saved.slot);
  qa_compat_q2_rerelease_source_edict *source = xsource(d, view);
  double generation = qa_compat_q2_rerelease_source_edict_generation(source);
  qa_compat_q2_rerelease_source_edict_destroy(source);
  if (generation != saved.generation) qa_range_error("Saved native damage actor generation changed");
  qa_owned_actor actor;
  if (!host_actor(d, view, &actor)) qa_range_error("Saved native damage actor is not live");
  return actor.id;
}
qa_compat_q2_rerelease_deferred_damage_save *qa_compat_q2_rerelease_deferred_damage_save_entries(
    qa_compat_q2_rerelease_deferred_damage *d, size_t *out_count) {
  qa_compat_q2_memory *memory = dmem(d);
  const qa_compat_q2_rerelease_primary_world_profile *profile = dprofile(d);
  qa_compat_q2_rerelease_deferred_damage_save *saved = NULL;
  size_t count = 0, cap = 0;
  for (size_t i = 0; i < d->tracked_count; i++) {
    tracked_t *entry = d->tracked[i];
    if (!entry->has_pending || !entry_valid(d, entry)) continue;
    qa_damage_request request = entry->pending.request;
    if (request.attack.attacker) request.attack.attacker = &entry->pending.attacker;
    if (request.attack.inflictor) request.attack.inflictor = &entry->pending.inflictor;
    if (count == cap) {
      cap = cap ? cap * 2 : 4;
      saved = realloc(saved, cap * sizeof *saved);
      if (!saved) { *out_count = 0; return NULL; }
    }
    qa_compat_q2_rerelease_deferred_damage_save *out = &saved[count];
    memset(out, 0, sizeof *out);
    out->target = qa_compat_q2_rerelease_deferred_damage_save_actor(d, request.target);
    out->attack = qa_q2_save_attack(request.attack);
    const qa_actor_id *refs[3] = {request.attack.attacker, request.attack.inflictor, NULL};
    qa_actor_id projectile;
    if (request.attack.has_originating_projectile) { projectile = request.attack.originating_projectile; refs[2] = &projectile; }
    size_t ref_count = 0;
    for (size_t r = 0; r < 3; r++) if (refs[r]) ref_count++;
    if (ref_count) {
      out->references = malloc(ref_count * sizeof *out->references);
      if (!out->references) { qa_compat_q2_rerelease_deferred_damage_free_saves(saved, count + 1); *out_count = 0; return NULL; }
    }
    for (size_t r = 0; r < 3; r++) {
      if (!refs[r]) continue;
      qa_compat_q2_rerelease_saved_reference *ref = &out->references[out->reference_count++];
      ref->actor.slot = (double)refs[r]->slot; ref->actor.generation = (double)refs[r]->generation;
      ref->reference = qa_compat_q2_rerelease_deferred_damage_save_actor(d, *refs[r]);
    }
    out->amount = request.amount; out->knockback = request.knockback;
    out->direction = request.direction; out->point = request.point; out->normal = request.normal;
    out->delivery = request.delivery;
    out->blood = qa_compat_q2_mem_read_i32(memory, at_view(d, entry->view, profile->monster.blood));
    out->save_knockback = qa_compat_q2_mem_read_i32(memory, at_view(d, entry->view, profile->monster.knockback));
    out->save_point = monster_point(d, entry->view);
    uint8_t mod[3];
    memory->copy(memory->ctx, at_view(d, entry->view, profile->monster.mod), mod, 3);
    out->mod[0] = mod[0]; out->mod[1] = mod[1]; out->mod[2] = mod[2]; out->mod_count = 3;
    out->attacker_slot = pointer_slot(d, entry->view, profile->monster.attacker);
    out->inflictor_slot = pointer_slot(d, entry->view, profile->monster.inflictor);
    count++;
  }
  if (!saved) {
    saved = malloc(sizeof *saved);
    if (!saved) { *out_count = 0; return NULL; }
  }
  *out_count = count;
  return saved;
}
void qa_compat_q2_rerelease_deferred_damage_free_saves(qa_compat_q2_rerelease_deferred_damage_save *saves, size_t count) {
  if (!saves) return;
  for (size_t i = 0; i < count; i++) free(saves[i].references);
  free(saves);
}
typedef struct { qa_compat_q2_rerelease_deferred_damage *d; const qa_compat_q2_rerelease_deferred_damage_save *state; const char *domain; } restore_ref_ctx_t;
static qa_actor_id restore_reference(void *v, qa_saved_actor_id actor) {
  restore_ref_ctx_t *ctx = v;
  for (size_t i = 0; i < ctx->state->reference_count; i++) {
    const qa_compat_q2_rerelease_saved_reference *ref = &ctx->state->references[i];
    if (ref->actor.slot == actor.slot && ref->actor.generation == actor.generation)
      return qa_compat_q2_rerelease_deferred_damage_restore_actor(ctx->d, ref->reference, ctx->domain);
  }
  qa_range_error("Missing saved native damage reference");
}
void qa_compat_q2_rerelease_deferred_damage_restore(qa_compat_q2_rerelease_deferred_damage *d,
                                                    const qa_compat_q2_rerelease_deferred_damage_save *saved, size_t count, const char *domain) {
  const char *dom = domain ? domain : "current";
  qa_compat_q2_memory *memory = dmem(d);
  const qa_compat_q2_rerelease_primary_world_profile *profile = dprofile(d);
  qa_raw_entity_table table = qa_compat_q2_rerelease_guest_module_entities(dmod(d));
  for (size_t i = 0; i < count; i++) {
    const qa_compat_q2_rerelease_deferred_damage_save *state = &saved[i];
    qa_actor_id target = qa_compat_q2_rerelease_deferred_damage_restore_actor(d, state->target, dom);
    qa_actor_source_result source = qa_session_actor_registry_source_of(dengine(d)->actors, target);
    if (!source.has_value) qa_range_error("Saved monster has no native source slot");
    qa_raw_entity_view view = table.at_slot(table.ctx, (double)source.slot);
    qa_guest_address a3 = at_view(d, view, profile->monster.attacker);
    qa_guest_address v2 = table.at_slot(table.ctx, state->attacker_slot).address;
    qa_compat_q2_mem_write_pointer(memory, a3, true, v2);
    qa_guest_address a2 = at_view(d, view, profile->monster.inflictor);
    qa_guest_address v = table.at_slot(table.ctx, state->inflictor_slot).address;
    qa_compat_q2_mem_write_pointer(memory, a2, true, v);
    qa_compat_q2_mem_write_i32(memory, at_view(d, view, profile->monster.blood), qa_js_to_int32(state->blood));
    qa_compat_q2_mem_write_i32(memory, at_view(d, view, profile->monster.knockback), qa_js_to_int32(state->save_knockback));
    double components[3] = {state->save_point.x, state->save_point.y, state->save_point.z};
    for (size_t c = 0; c < 3; c++)
      qa_compat_q2_mem_write_f32(memory, at_view(d, view, profile->monster.point + (double)c * 4), components[c]);
    if (state->mod_count != 3) qa_range_error("Invalid saved native monster mod_t");
    uint8_t mod[3] = {qa_js_to_uint8(state->mod[0]), qa_js_to_uint8(state->mod[1]), qa_js_to_uint8(state->mod[2])};
    memory->write(memory->ctx, at_view(d, view, profile->monster.mod), mod, 3);
    qa_compat_q2_rerelease_deferred_damage_track(d, view);
    tracked_t *entry = find_tracked(d, target);
    if (!entry) qa_range_error("Saved pending damage target is not a monster");
    qa_damage_request request; memset(&request, 0, sizeof request);
    request.target = target; request.amount = state->amount; request.knockback = state->knockback;
    request.direction = state->direction; request.point = state->point; request.normal = state->normal;
    request.delivery = state->delivery;
    restore_ref_ctx_t ctx = {d, state, dom};
    request.attack = qa_q2_restore_attack(state->attack, restore_reference, &ctx,
                                          &entry->pending.attacker, &entry->pending.inflictor);
    entry->pending.request = qa_capture_request(request);
    if (request.attack.attacker) entry->pending.request.attack.attacker = &entry->pending.attacker;
    if (request.attack.inflictor) entry->pending.request.attack.inflictor = &entry->pending.inflictor;
    entry->has_pending = true;
  }
}
