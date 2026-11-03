/* Port of src/compat/q2/native-mod-combat.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_combat.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/native_damage.h"
#include "qa/compat/q2/rerelease/native_entries.h"
#include "qa/content/q2/missionpacks/damage.h"
#include "qa/foundation/js.h"

typedef qa_compat_q2_native_mod_combat_state combat_t;
typedef enum kind_t { KIND_USE, KIND_TOUCH, KIND_PAIN, KIND_DIE, KIND_COUNT } kind_t;
static const char *const kind_names[KIND_COUNT] = {"use", "touch", "pain", "die"};
static const qa_guest_value_layout P = {false, "pointer", {0}}, I = {false, "int32", {0}}, F = {false, "float32", {0}}, B = {false, "uint8", {0}};
static const qa_guest_value_layout use_params[] = {P, P, P};
static const qa_guest_value_layout touch_classic[] = {P, P, P, P}, touch_rerelease[] = {P, P, P, B};
static const qa_guest_value_layout pain_classic[] = {P, P, F, I}, pain_rerelease[] = {P, P, F, I, P};
static const qa_guest_value_layout die_classic[] = {P, P, P, I, P}, die_rerelease[] = {P, P, P, I, P, P};
static const qa_guest_value_layout process_params[] = {P};

typedef struct callback_entry { combat_t *owner; kind_t kind; qa_guest_address address; double offset; qa_compat_q2_native_entry *binding; } callback_entry;
typedef struct damage_frame { qa_damage_request request; qa_source_damage_observer observer; bool has_result; qa_source_damage_result result; double applied; } damage_frame;
typedef struct bound_actor { combat_t *owner; double slot; qa_owned_actor actor; struct bound_actor *next; } bound_actor;
struct qa_compat_q2_native_mod_combat_state {
  const qa_native_mod_source_actors *definition; const qa_native_mod_declaration *declaration;
  const qa_compat_q2_native_mod_host *host; const qa_mod_host_services *services; qa_provider_id instance;
  qa_compat_q2_native_mod_combat_calls calls;
  callback_entry **entries; size_t entry_count, entry_capacity;
  qa_compat_q2_native_entry *damage, *process;
  qa_guest_value_layout damage_params[10];
  bool has_deferred; qa_compat_q2_native_mod_deferred_damage_state deferred;
  qa_compat_q2_native_mod_armor_state armor;
  damage_frame **frames; size_t frame_count, frame_capacity;
  bool watching; qa_guest_address watched_base; double watched_bytes; uint64_t watched;
  bool suspended, closed;
  bound_actor *bound;
};
static qa_guest_call_value pointer_value(bool has, qa_guest_address value) { return qa_compat_q2_call_pointer(has, value); }
static qa_guest_call_value int_value(double value) { return qa_compat_q2_call_int32(trunc(value)); }
static qa_compat_q2_guest_call_signature signature_of(const combat_t *s, const qa_guest_value_layout *parameters, size_t count) {
  qa_compat_q2_guest_call_signature signature; memset(&signature, 0, sizeof signature);
  signature.abi = s->declaration->target.abi; signature.parameters = parameters; signature.parameter_count = count; signature.result_is_void = true; signature.variadic = false;
  return signature;
}
static bool has_offset(const combat_t *s, kind_t kind, double *out) {
  const qa_native_mod_source_actors *d = s->definition; const qa_native_mod_source_callbacks *c = d->callbacks;
  switch (kind) {
    case KIND_USE: if (!d->fields.has_use) return false; *out = d->fields.use; return true;
    case KIND_TOUCH: if (c == NULL || !c->has_touch) return false; *out = c->touch; return true;
    case KIND_PAIN: if (c == NULL || !c->has_pain) return false; *out = c->pain; return true;
    default: if (c == NULL || !c->has_die) return false; *out = c->die; return true;
  }
}
static qa_guest_address at(const combat_t *s, double slot, double offset) {
  return qa_compat_q2_mem_offset(s->host->memory, s->host->entity(s->host->ctx, slot).address, (int64_t)offset);
}
static double slot_of(const combat_t *s, qa_guest_address address) { return qa_compat_q2_native_mod_table_slot(s->host, address, "Native callback actor is outside its source table"); }
/* TS nullable(value): false is null. */
static bool nullable_actor(const combat_t *s, const qa_guest_call_value *value, qa_actor_id *out) {
  if (value == NULL || value->kind != QA_GUEST_VALUE_POINTER) qa_range_error("Native callback requires an actor pointer");
  if (!value->pointer_value.has_value) return false;
  *out = s->calls.actor(s->calls.ctx, value->pointer_value.value); return true;
}
static const qa_guest_call_value *arg(const qa_guest_call_value *values, size_t count, size_t index) { return index < count ? &values[index] : NULL; }
static qa_guest_call_value actor_pointer(const combat_t *s, const qa_actor_id *actor) {
  qa_guest_address none; memset(&none, 0, sizeof none);
  return actor == NULL ? pointer_value(false, none) : pointer_value(true, s->calls.address(s->calls.ctx, *actor));
}
static bool eligible(const combat_t *s, const qa_actor_id *actor) { return actor == NULL || s->calls.eligible(s->calls.ctx, *actor); }
static bool allows_pain(const combat_t *s, const qa_pain_reaction *reaction) {
  return eligible(s, &reaction->self.id) && eligible(s, reaction->attacker)
      && (reaction->attack == NULL || (eligible(s, reaction->attack->attacker) && eligible(s, reaction->attack->inflictor)));
}
static bool allows_die(const combat_t *s, const qa_death_reaction *reaction) { return allows_pain(s, &reaction->pain) && eligible(s, reaction->inflictor); }
static bool allows_touch(const combat_t *s, const qa_touch_contact *contact) {
  if (!eligible(s, &contact->self.id) || !eligible(s, &contact->other)) return false;
  const qa_touch_source_trace *trace = contact->source_trace; if (trace == NULL) return true;
  return eligible(s, &trace->ent) && (trace->trace.hit.kind != QA_TRACE_HIT_ACTOR || eligible(s, &trace->trace.hit.actor));
}
static bool rerelease_callbacks(const combat_t *s) { const qa_native_mod_source_callbacks *c = s->definition->callbacks; return c != NULL && !strcmp(c->abi, "q2-rerelease"); }
static qa_compat_q2_guest_call_signature callback_signature(const combat_t *s, kind_t kind) {
  bool rr = rerelease_callbacks(s);
  switch (kind) {
    case KIND_USE: return signature_of(s, use_params, 3);
    case KIND_TOUCH: return signature_of(s, rr ? touch_rerelease : touch_classic, 4);
    case KIND_PAIN: return rr ? signature_of(s, pain_rerelease, 5) : signature_of(s, pain_classic, 4);
    default: return rr ? signature_of(s, die_rerelease, 6) : signature_of(s, die_classic, 5);
  }
}
static qa_guest_call_result callback_invoked(void *ctx, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding);
static callback_entry *ensure_entry(combat_t *s, kind_t kind, double slot) {
  double offset; if (!has_offset(s, kind, &offset)) return NULL;
  qa_guest_address address;
  if (!qa_compat_q2_mem_read_pointer(s->host->memory, at(s, slot, offset), &address)) return NULL;
  /* One function address shared by two callback kinds (a no-op assigned to pain and die) keeps the binding of its first kind. */
  for (size_t i = 0; i < s->entry_count; i++) if (s->entries[i]->address.byte_offset == address.byte_offset) return s->entries[i];
  callback_entry *entry = calloc(1, sizeof *entry); if (entry == NULL) qa_range_error("Out of memory");
  entry->owner = s; entry->kind = kind; entry->address = address; entry->offset = offset;
  char id[320]; (void)snprintf(id, sizeof id, "%s:actor-%s-%lld", s->instance, kind_names[kind], (long long)address.byte_offset);
  entry->binding = qa_compat_q2_native_mod_bind_entry(s->host, address, id, callback_signature(s, kind), entry, callback_invoked, s, NULL);
  callback_entry **slot_entry = qa_compat_q2_grow((void **)&s->entries, &s->entry_count, &s->entry_capacity, sizeof *slot_entry); *slot_entry = entry;
  return entry;
}
static bool accepts_active(void *ctx) { return !((combat_t *)ctx)->suspended; }
static void on_changed(void *ctx, const qa_compat_q2_written_range *ranges, size_t count) {
  combat_t *s = ctx; if (s->suspended) return;
  qa_compat_q2_native_mod_entities table = s->host->entities(s->host->ctx);
  for (size_t r = 0; r < count; r++) {
    double first = floor(ranges[r].byte_offset / table.stride), last = floor((ranges[r].byte_offset + ranges[r].byte_length - 1) / table.stride);
    for (double slot = first; slot <= last; slot++) {
      qa_owned_actor owner; if (!s->calls.owner(s->calls.ctx, slot, &owner)) continue;
      if (s->has_deferred) { qa_compat_q2_written_range local = {ranges[r].byte_offset - slot * table.stride, ranges[r].byte_length}; qa_compat_q2_native_mod_deferred_damage_state_changed(&s->deferred, slot, local); }
      for (int kind = 0; kind < KIND_COUNT; kind++) {
        double offset; if (!has_offset(s, (kind_t)kind, &offset)) continue;
        double start = slot * table.stride + offset;
        if (ranges[r].byte_offset < start + (double)s->host->memory->pointer_bytes && start < ranges[r].byte_offset + ranges[r].byte_length) (void)ensure_entry(s, (kind_t)kind, slot);
      }
    }
  }
}
static void stop_watching(combat_t *s) { if (s->watching) { s->host->unobserve_writes(s->host->ctx, s->watched); s->watching = false; } }
static void watch(combat_t *s) {
  qa_compat_q2_native_mod_entities table = s->host->entities(s->host->ctx); double bytes = table.stride * table.capacity;
  if (s->watching && s->watched_base.byte_offset == table.base.byte_offset && s->watched_bytes == bytes) return;
  stop_watching(s);
  s->watched_base = table.base; s->watched_bytes = bytes; s->watched = s->host->observe_writes(s->host->ctx, table.base, bytes, s, on_changed); s->watching = true;
}

/* --- Provenance and request decoding. --- */
static qa_q2_native_cause native_cause_of(const combat_t *s, const qa_guest_call_value *value) {
  const qa_native_mod_combat *combat = s->definition->combat;
  if (combat == NULL) qa_range_error("Native damage causes have not been declared");
  qa_q2_native_cause native; memset(&native, 0, sizeof native);
  if (!strcmp(combat->causes.edition, "classic")) {
    if (value == NULL || value->kind != QA_GUEST_VALUE_INT32) qa_range_error("Classic damage requires integer MOD");
    native.edition = "classic"; native.game = combat->causes.game; native.value = value->number_value; return native;
  }
  if (value == NULL || value->kind != QA_GUEST_VALUE_AGGREGATE || value->bytes.length != 3) qa_range_error("Rerelease damage requires its declared mod_t aggregate");
  native.edition = "rerelease"; native.id = value->bytes.bytes[0]; native.friendly_fire = value->bytes.bytes[1] != 0; native.no_point_loss = value->bytes.bytes[2] != 0;
  return native;
}
static qa_attack_provenance provenance(const combat_t *s, const qa_actor_id *attacker, const qa_actor_id *inflictor, qa_q2_native_cause native) {
  double means_of_death = 0;
  if (s->services->damage_context == NULL || !qa_q2mp_canonical_cause_from_native(native, &means_of_death)) qa_range_error("Native damage requires declared source cause and shared attack provenance");
  qa_attack_provenance attack; memset(&attack, 0, sizeof attack);
  s->services->damage_context(s->services->ctx, s->instance, &attack.sequence, &attack.weapon_provider, &attack.combat_provider, &attack.inventory_provider, &attack.movement_provider);
  attack.time = qa_compat_q2_services_time(s->services); attack.attacker = attacker; attack.inflictor = inflictor; attack.weapon = NULL;
  attack.cause.kind = QA_ATTACK_CAUSE_Q2; attack.cause.means_of_death = means_of_death; attack.cause.damage_flags = 0; attack.cause.has_native = true; attack.cause.native = native;
  return attack;
}
typedef struct decoded_request { qa_damage_request request; qa_actor_id attacker, inflictor; } decoded_request;
static void decode_request(const combat_t *s, const qa_guest_call_value *values, size_t count, decoded_request *out) {
  memset(out, 0, sizeof *out); qa_compat_q2_memory *memory = s->host->memory;
  out->request.target = s->calls.actor(s->calls.ctx, qa_compat_q2_guest_required_pointer(values, count, 0));
  bool has_inflictor = nullable_actor(s, arg(values, count, 1), &out->inflictor), has_attacker = nullable_actor(s, arg(values, count, 2), &out->attacker);
  double flags = (double)qa_compat_q2_guest_integer(values, count, 8);
  qa_attack_provenance attack = provenance(s, has_attacker ? &out->attacker : NULL, has_inflictor ? &out->inflictor : NULL, native_cause_of(s, arg(values, count, 9)));
  attack.cause.damage_flags = flags;
  out->request.attack = attack;
  out->request.amount = (double)qa_compat_q2_guest_integer(values, count, 6); out->request.knockback = (double)qa_compat_q2_guest_integer(values, count, 7);
  out->request.direction = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_guest_required_pointer(values, count, 3));
  out->request.point = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_guest_required_pointer(values, count, 4));
  out->request.normal = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_guest_required_pointer(values, count, 5));
  out->request.delivery = (qa_js_to_int32(flags) & 1) != 0 ? "radius" : "direct";
}

/* --- apply(): the original damage body under the shared authority. --- */
typedef struct apply_call {
  combat_t *s; qa_damage_request request; qa_source_damage_observer observer;
  damage_frame frame; double slot, health; qa_vec3 velocity; qa_guest_address target, vectors;
  qa_compat_q2_native_armor_observation *armor_observation; uint64_t health_observer, velocity_observer;
  qa_guest_call_value args[10];
  uint8_t mod[3];
  qa_source_damage_result result;
} apply_call;
static bool frame_current(const apply_call *c) {
  combat_t *s = c->s;
  return s->frame_count != 0 && s->frames[s->frame_count - 1] == &c->frame && !c->frame.has_result && s->host->active(s->host->ctx, c->slot) && qa_session_actor_registry_is_live(s->services->actors, c->request.target);
}
static void apply_armor_changed(void *ctx, qa_armor_state before, qa_armor_state after) {
  apply_call *c = ctx; if (!frame_current(c)) return;
  qa_damage_mutation write; memset(&write, 0, sizeof write); write.kind = QA_DAMAGE_MUTATION_ARMOR; write.armor_before = before; write.armor_after = after;
  c->observer.stored(c->observer.ctx, write);
}
static void apply_health_written(void *ctx, const qa_compat_q2_written_range *ranges, size_t count) {
  (void)ranges; (void)count; /* TS observer ignores its ranges. */
  apply_call *c = ctx; combat_t *s = c->s;
  double before = c->health, after = s->calls.scalar(s->calls.ctx, c->target, s->definition->combat->health, false, 0); c->health = after;
  if (frame_current(c) && before != after) {
    c->frame.applied += before - after;
    qa_damage_mutation write; memset(&write, 0, sizeof write); write.kind = QA_DAMAGE_MUTATION_HEALTH; write.health_before = before; write.health_after = after;
    c->observer.stored(c->observer.ctx, write);
  }
}
static void apply_velocity_written(void *ctx, const qa_compat_q2_written_range *ranges, size_t count) {
  (void)ranges; (void)count; /* TS observer ignores its ranges. */
  apply_call *c = ctx; combat_t *s = c->s;
  qa_vec3 before = c->velocity, after = qa_compat_q2_read_classic_vector(s->host->memory, at(s, c->slot, s->definition->fields.velocity)); c->velocity = after;
  if (frame_current(c)) {
    qa_damage_mutation write; memset(&write, 0, sizeof write); write.kind = QA_DAMAGE_MUTATION_SOURCE_VELOCITY; write.velocity_before = before; write.velocity_after = after; write.movement_provider = c->request.attack.movement_provider;
    c->observer.stored(c->observer.ctx, write);
  }
}
static void damage_original(void *ctx) { apply_call *c = ctx; (void)qa_compat_q2_native_entry_call_original(c->s->damage, c->args, 10); }
static void apply_body(void *ctx) {
  apply_call *c = ctx; combat_t *s = c->s; qa_compat_q2_memory *memory = s->host->memory; const qa_native_mod_combat *definition = s->definition->combat;
  c->armor_observation = qa_compat_q2_native_mod_armor_observe(&s->armor, c->slot, c, apply_armor_changed);
  void * value2 = s->host->ctx;
  qa_guest_address value3 = at(s, c->slot, definition->health.offset);
  double value4 = qa_compat_q2_native_scalar_bytes(definition->health.encoding);
  c->health_observer = s->host->observe_writes(value2, value3, value4, c, apply_health_written);
  c->velocity_observer = s->host->observe_writes(s->host->ctx, at(s, c->slot, s->definition->fields.velocity), 12, c, apply_velocity_written);
  qa_compat_q2_write_classic_vector(memory, c->vectors, c->request.direction);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, c->vectors, 12), c->request.point);
  qa_compat_q2_write_classic_vector(memory, qa_compat_q2_mem_offset(memory, c->vectors, 24), c->request.normal);
  qa_q2mp_native_cause_profile profile; memset(&profile, 0, sizeof profile);
  profile.rerelease = !strcmp(definition->causes.edition, "rerelease"); profile.game = definition->causes.game;
  qa_compat_q2_native_damage_arguments_result cause = qa_compat_q2_native_damage_arguments(c->request, profile);
  if (cause.native.edition == NULL) qa_range_error("Shared attack has no declared native cause representation");
  c->args[3] = pointer_value(true, c->vectors);
  c->args[4] = pointer_value(true, qa_compat_q2_mem_offset(memory, c->vectors, 12));
  c->args[5] = pointer_value(true, qa_compat_q2_mem_offset(memory, c->vectors, 24));
  c->args[6] = int_value(c->request.amount); c->args[7] = int_value(c->request.knockback); c->args[8] = int_value(cause.damage_flags);
  if (!strcmp(cause.native.edition, "classic")) c->args[9] = int_value(cause.native.value);
  else {
    c->mod[0] = qa_js_to_uint8(cause.native.id); c->mod[1] = cause.native.friendly_fire; c->mod[2] = cause.native.no_point_loss;
    memset(&c->args[9], 0, sizeof c->args[9]);
    c->args[9].kind = QA_GUEST_VALUE_AGGREGATE; c->args[9].layout = qa_compat_q2_rerelease_mod_layout; c->args[9].bytes.bytes = c->mod; c->args[9].bytes.length = 3;
  }
  (void)s->calls.source_execution(s->calls.ctx, &c->request.target, damage_original, c);
  if (c->frame.has_result) c->result = c->frame.result;
  else { c->result.reaction = "none"; c->result.applied_damage = c->frame.applied; }
}
static void apply_cleanup(void *ctx) {
  apply_call *c = ctx; combat_t *s = c->s;
  s->frame_count--;
  /* removals.runAll(): registration order. */
  if (c->armor_observation != NULL) qa_compat_q2_native_armor_observation_close(c->armor_observation);
  if (c->health_observer != 0) s->host->unobserve_writes(s->host->ctx, c->health_observer);
  if (c->velocity_observer != 0) s->host->unobserve_writes(s->host->ctx, c->velocity_observer);
  s->host->memory->unmap(s->host->memory->ctx, c->vectors, 36);
}
static void apply_transfer(void *ctx) {
  apply_call *c = ctx; combat_t *s = c->s; qa_compat_q2_memory *memory = s->host->memory;
  c->slot = slot_of(s, c->target);
  memset(&c->frame, 0, sizeof c->frame); c->frame.request = c->request; c->frame.observer = c->observer;
  c->health = s->calls.scalar(s->calls.ctx, c->target, s->definition->combat->health, false, 0);
  c->velocity = qa_compat_q2_read_classic_vector(memory, at(s, c->slot, s->definition->fields.velocity));
  c->vectors = memory->allocate(memory->ctx, 36, true, 4, "Native mod damage vectors");
  damage_frame **top = qa_compat_q2_grow((void **)&s->frames, &s->frame_count, &s->frame_capacity, sizeof *top); *top = &c->frame;
  qa_compat_q2_native_mod_finally(apply_body, c, apply_cleanup, c);
}
static qa_source_damage_result run_source_damage(void *ctx, qa_source_damage_observer observer, qa_damage_request request) {
  combat_t *s = ctx; qa_source_damage_result none = {0, "none"};
  if (!eligible(s, &request.target) || !eligible(s, request.attack.attacker) || !eligible(s, request.attack.inflictor)) return none;
  const qa_session_mod_engine_services *engine = s->services->engine;
  if (engine == NULL) qa_range_error("Native damage requires its destination world actor");
  qa_actor_id world; bool has_world = engine->world(engine->ctx, &world);
  apply_call call; memset(&call, 0, sizeof call); call.s = s; call.request = request; call.observer = observer;
  call.target = s->calls.address(s->calls.ctx, request.target);
  call.args[0] = pointer_value(true, call.target);
  call.args[1] = actor_pointer(s, request.attack.inflictor != NULL ? request.attack.inflictor : has_world ? &world : NULL);
  call.args[2] = actor_pointer(s, request.attack.attacker != NULL ? request.attack.attacker : has_world ? &world : NULL);
  s->calls.transfer(s->calls.ctx, apply_transfer, &call);
  return call.result;
}
static qa_damage_outcome apply_damage(combat_t *s, qa_damage_request input) {
  if (s->definition->combat == NULL || s->damage == NULL) qa_range_error("Native damage body has not been declared");
  return qa_gameplay_authority_run_source_damage(s->services->combat, input, run_source_damage, s);
}
typedef struct foreign_apply { combat_t *s; qa_damage_request request; } foreign_apply;
static void foreign_apply_body(void *ctx) { foreign_apply *f = ctx; (void)qa_gameplay_authority_apply(f->s->services->combat, f->request, NULL, NULL); }
static qa_guest_call_result damage_entry(void *ctx, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  (void)binding; combat_t *s = ctx; decoded_request decoded; decode_request(s, values, count, &decoded);
  qa_damage_request *request = &decoded.request; qa_owned_actor target;
  if (!qa_session_actor_registry_resolve_owned(s->services->actors, request->target, &target)
      || !eligible(s, &request->target) || !eligible(s, request->attack.attacker) || !eligible(s, request->attack.inflictor)) return qa_compat_q2_call_void();
  if (!strcmp(target.owner, s->instance)) (void)apply_damage(s, *request);
  else { foreign_apply f = {s, *request}; s->calls.transfer(s->calls.ctx, foreign_apply_body, &f); s->calls.synchronize(s->calls.ctx); }
  return qa_compat_q2_call_void();
}
typedef struct process_original { qa_compat_q2_native_entry *binding; const qa_guest_call_value *values; size_t count; qa_guest_call_result result; } process_original;
static void process_original_body(void *ctx) { process_original *p = ctx; p->result = qa_compat_q2_native_entry_call_original(p->binding, p->values, p->count); }
static qa_guest_call_result process_entry(void *ctx, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  combat_t *s = ctx; double slot = slot_of(s, qa_compat_q2_guest_required_pointer(values, count, 0)); qa_owned_actor actor;
  if (!s->calls.owner(s->calls.ctx, slot, &actor)) return qa_compat_q2_native_entry_call_original(binding, values, count);
  process_original p = {binding, values, count, {0}};
  qa_compat_q2_native_mod_deferred_damage_state_process(&s->deferred, actor.id, slot, &p, process_original_body);
  return p.result;
}
static bool deferred_current(void *ctx, qa_damage_request *out) {
  combat_t *s = ctx; if (s->frame_count == 0) return false;
  *out = s->frames[s->frame_count - 1]->request; return true;
}
static bool deferred_owner(void *ctx, double slot, qa_owned_actor *out) { combat_t *s = ctx; return s->calls.owner(s->calls.ctx, slot, out); }
static double deferred_scalar(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value) { combat_t *s = ctx; return s->calls.scalar(s->calls.ctx, base, field, has_value, value); }
combat_t *qa_compat_q2_native_mod_combat_create(const qa_native_mod_source_actors *definition, const qa_native_mod_declaration *declaration,
    const qa_compat_q2_native_mod_host *host, const qa_mod_host_services *services, qa_provider_id instance, qa_compat_q2_native_mod_combat_calls calls) {
  combat_t *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->definition = definition; s->declaration = declaration; s->host = host; s->services = services; s->instance = instance; s->calls = calls;
  const qa_native_mod_combat *combat = definition->combat;
  qa_native_mod_armor no_armor; memset(&no_armor, 0, sizeof no_armor); no_armor.kind = "none";
  qa_compat_q2_native_mod_armor_state_init(&s->armor, combat != NULL ? combat->armor : no_armor, declaration, host, calls.scalar, calls.ctx);
  if (combat == NULL) return s;
  char id[320];
  for (int i = 0; i < 9; i++) s->damage_params[i] = i < 6 ? P : I;
  if (!strcmp(combat->damage.abi, "q2-classic")) s->damage_params[9] = I;
  else { memset(&s->damage_params[9], 0, sizeof s->damage_params[9]); s->damage_params[9].is_aggregate = true; s->damage_params[9].layout = qa_compat_q2_rerelease_mod_layout; }
  (void)snprintf(id, sizeof id, "%s:damage", instance);
  const qa_compat_q2_native_mod_host * host3 = host;
  qa_guest_address address2 = qa_compat_q2_native_mod_entry_address(host, combat->damage.entry);
  qa_compat_q2_guest_call_signature signature2 = signature_of(s, s->damage_params, 10);
  s->damage = qa_compat_q2_native_mod_bind_entry(host3, address2, id, signature2, s, damage_entry, s, accepts_active);
  if (combat->deferred != NULL) {
    qa_compat_q2_native_deferred_calls deferred_calls = {s, deferred_owner, deferred_scalar};
    qa_compat_q2_native_mod_deferred_damage_state_init(&s->deferred, combat->deferred, combat->health, host, services, deferred_calls, s, deferred_current); s->has_deferred = true;
    (void)snprintf(id, sizeof id, "%s:deferred-damage", instance);
    const qa_compat_q2_native_mod_host * host2 = host;
    qa_guest_address address = qa_compat_q2_native_mod_entry_address(host, combat->deferred->process);
    qa_compat_q2_guest_call_signature signature = signature_of(s, process_params, 1);
    s->process = qa_compat_q2_native_mod_bind_entry(host2, address, id, signature, s, process_entry, s, accepts_active);
  }
  return s;
}

/* --- Guest argument scratch. --- */
typedef void (*with_address_fn)(void *ctx, qa_guest_address address);
typedef struct bytes_call { const combat_t *s; qa_guest_address address; size_t length; void *ctx; with_address_fn run; } bytes_call;
static void bytes_body(void *ctx) { bytes_call *b = ctx; b->run(b->ctx, b->address); }
static void bytes_cleanup(void *ctx) { bytes_call *b = ctx; b->s->host->memory->unmap(b->s->host->memory->ctx, b->address, (double)b->length); }
static void with_bytes(const combat_t *s, const uint8_t *bytes, size_t length, void *ctx, with_address_fn run) {
  qa_compat_q2_memory *memory = s->host->memory;
  bytes_call b = {s, memory->allocate(memory->ctx, (double)length, true, 8, "Native mod callback value"), length, ctx, run};
  memory->write(memory->ctx, b.address, bytes, length);
  qa_compat_q2_native_mod_finally(bytes_body, &b, bytes_cleanup, &b);
}
typedef void (*values_run_fn)(void *ctx, const qa_guest_call_value *values, size_t count);
typedef struct mod_call { qa_guest_call_value *args; size_t count; void *ctx; values_run_fn run; } mod_call;
static void mod_body(void *ctx, qa_guest_address address) {
  mod_call *m = ctx; m->args[m->count] = pointer_value(true, address); m->run(m->ctx, m->args, m->count + 1);
}
/* TS withMod: appends the rerelease mod_t pointer. args holds count + 1. */
static void with_mod(const combat_t *s, const qa_attack_provenance *attack, qa_guest_call_value *args, size_t count, void *ctx, values_run_fn run) {
  if (!rerelease_callbacks(s)) { run(ctx, args, count); return; }
  qa_q2_native_cause native; memset(&native, 0, sizeof native);
  bool is_q2 = attack != NULL && attack->cause.kind == QA_ATTACK_CAUSE_Q2;
  if (is_q2 && attack->cause.has_native && attack->cause.native.edition != NULL && !strcmp(attack->cause.native.edition, "rerelease")) native = attack->cause.native;
  else {
    qa_q2mp_native_cause_profile profile; memset(&profile, 0, sizeof profile); profile.rerelease = true;
    if (!qa_q2mp_native_cause_from_canonical(profile, is_q2 ? attack->cause.means_of_death : 0, &native)) qa_range_error("Reaction has no native mod_t representation");
    if (native.edition == NULL || strcmp(native.edition, "rerelease") != 0) qa_range_error("Reaction has no native mod_t representation");
  }
  uint8_t bytes[3] = {qa_js_to_uint8(native.id), native.friendly_fire, native.no_point_loss};
  mod_call m = {args, count, ctx, run};
  with_bytes(s, bytes, 3, &m, mod_body);
}
static void with_pain(combat_t *s, const qa_pain_reaction *reaction, void *ctx, values_run_fn run) {
  if (!allows_pain(s, reaction)) return;
  qa_guest_call_value args[5];
  args[0] = actor_pointer(s, &reaction->self.id); args[1] = actor_pointer(s, reaction->attacker);
  memset(&args[2], 0, sizeof args[2]); args[2].kind = QA_GUEST_VALUE_FLOAT32; args[2].number_value = reaction->kick;
  args[3] = int_value(reaction->damage);
  with_mod(s, reaction->attack, args, 4, ctx, run);
}
typedef struct die_call { combat_t *s; const qa_death_reaction *reaction; void *ctx; values_run_fn run; } die_call;
static void die_body(void *ctx, qa_guest_address point) {
  die_call *d = ctx; combat_t *s = d->s; qa_guest_call_value args[6];
  args[0] = actor_pointer(s, &d->reaction->pain.self.id); args[1] = actor_pointer(s, d->reaction->inflictor); args[2] = actor_pointer(s, d->reaction->pain.attacker);
  args[3] = int_value(d->reaction->pain.damage); args[4] = pointer_value(true, point);
  with_mod(s, d->reaction->pain.attack, args, 5, d->ctx, d->run);
}
static void put_f32(uint8_t *bytes, size_t offset, double value) { qa_compat_q2_view_set_f32(bytes, offset, value); }
static void with_die(combat_t *s, const qa_death_reaction *reaction, void *ctx, values_run_fn run) {
  if (!allows_die(s, reaction)) return;
  uint8_t bytes[12]; put_f32(bytes, 0, reaction->point.x); put_f32(bytes, 4, reaction->point.y); put_f32(bytes, 8, reaction->point.z);
  die_call d = {s, reaction, ctx, run};
  with_bytes(s, bytes, 12, &d, die_body);
}
/* TS withTouch. */
typedef struct touch_trace_call { combat_t *s; const qa_touch_contact *contact; void *ctx; values_run_fn run; } touch_trace_call;
static void touch_trace_body(void *ctx, qa_guest_address trace) {
  touch_trace_call *t = ctx; combat_t *s = t->s; qa_guest_call_value args[4];
  args[0] = actor_pointer(s, &t->contact->self.id); args[1] = actor_pointer(s, &t->contact->other); args[2] = pointer_value(true, trace);
  args[3] = qa_compat_q2_call_uint32(t->contact->source_trace->inverted ? 1 : 0);
  t->run(t->ctx, args, 4);
}
typedef struct touch_plane_call { combat_t *s; const qa_touch_contact *contact; const uint8_t *surface; void *ctx; values_run_fn run; bool has_plane_address; qa_guest_address plane; } touch_plane_call;
static void touch_surface_body(void *ctx, qa_guest_address surface) {
  touch_plane_call *t = ctx; combat_t *s = t->s; qa_guest_call_value args[4];
  args[0] = actor_pointer(s, &t->contact->self.id); args[1] = actor_pointer(s, &t->contact->other);
  args[2] = pointer_value(t->contact->plane != NULL, t->plane); args[3] = pointer_value(t->contact->surface != NULL, surface);
  t->run(t->ctx, args, 4);
}
static void touch_plane_body(void *ctx, qa_guest_address plane) {
  touch_plane_call *t = ctx; t->plane = plane; with_bytes(t->s, t->surface, 24, t, touch_surface_body);
}
static void with_touch(combat_t *s, const qa_touch_contact *contact, void *ctx, values_run_fn run) {
  if (!allows_touch(s, contact)) return;
  if (rerelease_callbacks(s)) {
    if (contact->source_trace == NULL) qa_range_error("Native rerelease touch requires the shared source trace");
    size_t length = 0; uint8_t *encoded = s->host->encode_trace(s->host->ctx, &contact->source_trace->trace, &length);
    touch_trace_call t = {s, contact, ctx, run};
    with_bytes(s, encoded, length, &t, touch_trace_body);
    free(encoded); return;
  }
  uint8_t plane[20], surface[24]; memset(plane, 0, sizeof plane); memset(surface, 0, sizeof surface);
  if (contact->plane != NULL) {
    qa_vec3 n = contact->plane->normal;
    put_f32(plane, 0, n.x); put_f32(plane, 4, n.y); put_f32(plane, 8, n.z); put_f32(plane, 12, contact->plane->distance);
    plane[16] = n.x == 1 ? 0 : n.y == 1 ? 1 : n.z == 1 ? 2 : 3;
    plane[17] = (uint8_t)((n.x < 0 ? 1 : 0) | (n.y < 0 ? 2 : 0) | (n.z < 0 ? 4 : 0));
  }
  if (contact->surface != NULL) {
    size_t n = strlen(contact->surface->name); memcpy(surface, contact->surface->name, n < 16 ? n : 16);
    qa_compat_q2_view_set_i32(surface, 16, qa_js_to_int32(contact->surface->native_flags)); qa_compat_q2_view_set_i32(surface, 20, qa_js_to_int32(contact->surface->native_value));
  }
  touch_plane_call t; memset(&t, 0, sizeof t); t.s = s; t.contact = contact; t.surface = surface; t.ctx = ctx; t.run = run;
  with_bytes(s, plane, 20, &t, touch_plane_body);
}

/* --- Decoding source callback arguments. --- */
static qa_bsp_plane read_plane(const combat_t *s, qa_guest_address address) {
  qa_compat_q2_memory *memory = s->host->memory; qa_bsp_plane plane; memset(&plane, 0, sizeof plane);
  plane.plane.normal = qa_compat_q2_read_classic_vector(memory, address);
  plane.plane.distance = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, address, 12));
  plane.type = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, address, 16));
  plane.signbits = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, address, 17));
  return plane;
}
/* name and material are malloc'd into *storage (caller frees); false is null. */
typedef struct surface_storage { qa_q2_surface_info info; char *name, *material; } surface_storage;
static bool read_surface(const combat_t *s, bool has, qa_guest_address address, bool rerelease, surface_storage *out) {
  memset(out, 0, sizeof *out); if (!has) return false;
  qa_compat_q2_memory *memory = s->host->memory; int size = rerelease ? 32 : 16;
  out->name = qa_compat_q2_read_classic_string(memory, true, address, size);
  out->info.name = out->name;
  out->info.flags = qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, address, size));
  out->info.value = qa_compat_q2_mem_read_i32(memory, qa_compat_q2_mem_offset(memory, address, size + 4));
  if (rerelease) { out->material = qa_compat_q2_read_classic_string(memory, true, qa_compat_q2_mem_offset(memory, address, 44), 16); out->info.material = out->material; }
  else out->info.material = "";
  return true;
}
typedef struct decoded_touch {
  qa_touch_contact contact; qa_plane plane; qa_touch_surface surface; qa_touch_source_trace trace;
  surface_storage primary, secondary;
} decoded_touch;
static void decode_touch(const combat_t *s, qa_owned_actor self, const qa_guest_call_value *args, size_t count, decoded_touch *o) {
  memset(o, 0, sizeof *o); qa_compat_q2_memory *memory = s->host->memory;
  qa_actor_id other = s->calls.actor(s->calls.ctx, qa_compat_q2_guest_required_pointer(args, count, 1));
  o->contact.self = self; o->contact.other = other;
  if (rerelease_callbacks(s)) {
    qa_guest_address address = qa_compat_q2_guest_required_pointer(args, count, 2), pointer, ent_address;
    qa_bsp_plane source_plane = read_plane(s, qa_compat_q2_mem_offset(memory, address, 20));
    bool has = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, 40), &pointer);
    bool has_surface = read_surface(s, has, pointer, true, &o->primary);
    qa_actor_id ent = other;
    if (qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, 56), &ent_address)) ent = s->calls.actor(s->calls.ctx, ent_address);
    double fraction = qa_compat_q2_mem_read_f32(memory, qa_compat_q2_mem_offset(memory, address, 4));
    qa_bsp_plane plane2 = read_plane(s, qa_compat_q2_mem_offset(memory, address, 64));
    has = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, 88), &pointer);
    bool has_surface2 = read_surface(s, has, pointer, true, &o->secondary);
    qa_trace_result *trace = &o->trace.trace;
    trace->kind = "q2"; trace->all_solid = qa_compat_q2_mem_read_u8(memory, address) != 0; trace->start_solid = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, address, 1)) != 0;
    trace->fraction = fraction; trace->end = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_mem_offset(memory, address, 8));
    trace->source_plane = source_plane; trace->contact.has_plane = fraction < 1; if (fraction < 1) trace->contact.plane = source_plane.plane;
    trace->contents = qa_compat_q2_mem_read_u32(memory, qa_compat_q2_mem_offset(memory, address, 48));
    trace->surface = has_surface ? &o->primary.info : NULL;
    trace->hit.kind = QA_TRACE_HIT_ACTOR; trace->hit.actor = ent;
    trace->has_secondary = true; trace->secondary_plane = plane2; trace->secondary_surface = has_surface2 ? &o->secondary.info : NULL;
    o->plane = source_plane.plane; o->contact.plane = &o->plane;
    if (has_surface) { o->surface.name = o->primary.info.name; o->surface.native_flags = o->primary.info.flags; o->surface.native_value = o->primary.info.value; o->contact.surface = &o->surface; }
    o->trace.ent = ent; o->trace.inverted = qa_compat_q2_guest_integer(args, count, 3) != 0;
    o->contact.source_trace = &o->trace;
    return;
  }
  const qa_guest_call_value *plane = arg(args, count, 2), *surface = arg(args, count, 3);
  if (plane == NULL || surface == NULL || plane->kind != QA_GUEST_VALUE_POINTER || surface->kind != QA_GUEST_VALUE_POINTER) qa_range_error("Classic touch requires plane and surface pointers");
  if (read_surface(s, surface->pointer_value.has_value, surface->pointer_value.value, false, &o->primary)) {
    o->surface.name = o->primary.info.name; o->surface.native_flags = o->primary.info.flags; o->surface.native_value = o->primary.info.value; o->contact.surface = &o->surface;
  }
  if (plane->pointer_value.has_value) { o->plane = read_plane(s, plane->pointer_value.value).plane; o->contact.plane = &o->plane; }
}
static void decoded_touch_free(decoded_touch *o) { free(o->primary.name); free(o->primary.material); free(o->secondary.name); free(o->secondary.material); }
static bool same_nullable_actor(const qa_actor_id *a, const qa_actor_id *b) { return (a == NULL && b == NULL) || (a != NULL && b != NULL && qa_same_actor(*a, *b)); }

typedef struct source_call {
  combat_t *s; qa_owned_actor self; const qa_guest_call_value *args; size_t count; qa_compat_q2_native_entry *binding;
  const qa_actor_id *other, *activator; decoded_touch *touch; const qa_pain_reaction *pain; const qa_death_reaction *death;
} source_call;
/* proceed(values). */
static void proceed(void *ctx, const qa_guest_call_value *values, size_t count) {
  source_call *c = ctx; c->s->calls.synchronize(c->s->calls.ctx);
  (void)qa_compat_q2_native_entry_call_original(c->binding, values, count);
}
static void check_self(const source_call *c, qa_owned_actor actor) { if (!qa_same_actor(actor.id, c->self.id)) qa_range_error("Native source callback target changes require replacement"); }
static void source_use_execute(void *ctx, qa_owned_actor actor, const qa_actor_id *other, const qa_actor_id *activator) {
  source_call *c = ctx; check_self(c, actor); combat_t *s = c->s;
  if (!eligible(s, &actor.id) || !eligible(s, other) || !eligible(s, activator)) return;
  if (same_nullable_actor(other, c->other) && same_nullable_actor(activator, c->activator)) { proceed(c, c->args, c->count); return; }
  qa_guest_call_value value = actor_pointer(s, &actor.id);
  qa_guest_call_value value2 = actor_pointer(s, other);
  qa_guest_call_value value3 = actor_pointer(s, activator);
  qa_guest_call_value values[3] = {value, value2, value3};
  proceed(c, values, 3);
}
static void source_touch_execute(void *ctx, qa_touch_contact effective) {
  source_call *c = ctx; check_self(c, effective.self); const qa_touch_contact *contact = &c->touch->contact;
  if (!allows_touch(c->s, &effective)) return;
  if (qa_same_actor(effective.other, contact->other) && effective.plane == contact->plane && effective.surface == contact->surface && effective.source_trace == contact->source_trace) { proceed(c, c->args, c->count); return; }
  with_touch(c->s, &effective, c, proceed);
}
static bool same_pain(const qa_pain_reaction *a, const qa_pain_reaction *b) {
  return qa_same_actor(a->self.id, b->self.id) && a->attack == b->attack && same_nullable_actor(a->attacker, b->attacker) && a->kick == b->kick && a->damage == b->damage;
}
static void source_pain_execute(void *ctx, qa_pain_reaction effective) {
  source_call *c = ctx; check_self(c, effective.self);
  if (!allows_pain(c->s, &effective)) return;
  if (same_pain(&effective, c->pain)) { proceed(c, c->args, c->count); return; }
  with_pain(c->s, &effective, c, proceed);
}
static void source_die_execute(void *ctx, qa_death_reaction effective) {
  source_call *c = ctx; check_self(c, effective.pain.self);
  if (!allows_die(c->s, &effective)) return;
  const qa_death_reaction *death = c->death;
  if (same_pain(&effective.pain, &death->pain) && same_nullable_actor(effective.inflictor, death->inflictor)
      && effective.point.x == death->point.x && effective.point.y == death->point.y && effective.point.z == death->point.z) { proceed(c, c->args, c->count); return; }
  with_die(c->s, &effective, c, proceed);
}
static qa_actor_callback_table *callback_table(const combat_t *s) {
  if (s->services->callbacks == NULL) qa_range_error("Native callbacks require shared actor operations");
  return s->services->callbacks;
}
static void source_touch(combat_t *s, source_call *c) {
  decoded_touch touch; decode_touch(s, c->self, c->args, c->count, &touch); c->touch = &touch;
  (void)qa_actor_callback_table_source_touch(callback_table(s), touch.contact, source_touch_execute, c);
  decoded_touch_free(&touch);
}
static void source_use(combat_t *s, source_call *c) {
  qa_actor_id other, activator;
  bool has_other = nullable_actor(s, arg(c->args, c->count, 1), &other), has_activator = nullable_actor(s, arg(c->args, c->count, 2), &activator);
  c->other = has_other ? &other : NULL; c->activator = has_activator ? &activator : NULL;
  (void)qa_actor_callback_table_source_use(callback_table(s), c->self, c->other, c->activator, source_use_execute, c);
}
static void source_reaction(combat_t *s, source_call *c, kind_t kind) {
  qa_actor_callback_table *table = callback_table(s); qa_compat_q2_memory *memory = s->host->memory;
  damage_frame *current = s->frame_count == 0 ? NULL : s->frames[s->frame_count - 1];
  double amount = (double)qa_compat_q2_guest_integer(c->args, c->count, 3);
  qa_actor_id attacker_storage, inflictor_storage;
  bool has_attacker = nullable_actor(s, arg(c->args, c->count, kind == KIND_PAIN ? 1 : 2), &attacker_storage);
  const qa_actor_id *attacker = has_attacker ? &attacker_storage : NULL;
  bool current_target = current != NULL && qa_same_actor(current->request.target, c->self.id);
  if (current_target && !current->has_result) {
    current->has_result = true; current->result.reaction = kind == KIND_PAIN ? "pain" : "death"; current->result.applied_damage = amount;
    current->observer.before_reaction(current->observer.ctx, current->result);
  }
  qa_damage_request deferred_request; bool has_request = current_target;
  if (current_target) deferred_request = current->request;
  else if (s->has_deferred) has_request = qa_compat_q2_native_mod_deferred_damage_state_attack(&s->deferred, c->self.id, &deferred_request);
  qa_attack_provenance attack_storage; const qa_attack_provenance *attack = NULL;
  if (has_request) { attack_storage = deferred_request.attack; attack = &attack_storage; }
  else if (s->definition->combat != NULL && rerelease_callbacks(s)) {
    qa_guest_address mod = qa_compat_q2_guest_required_pointer(c->args, c->count, kind == KIND_PAIN ? 4 : 5); uint8_t bytes[3];
    memory->copy(memory->ctx, mod, bytes, 3);
    qa_guest_call_value value; memset(&value, 0, sizeof value); value.kind = QA_GUEST_VALUE_AGGREGATE; value.bytes.bytes = bytes; value.bytes.length = 3;
    const qa_actor_id *inflictor = attacker;
    if (kind == KIND_DIE) inflictor = nullable_actor(s, arg(c->args, c->count, 1), &inflictor_storage) ? &inflictor_storage : NULL;
    attack_storage = provenance(s, attacker, inflictor, native_cause_of(s, &value)); attack = &attack_storage;
  }
  const qa_guest_call_value *kick_value = arg(c->args, c->count, 2);
  double kick = kind == KIND_PAIN && kick_value != NULL && kick_value->kind == QA_GUEST_VALUE_FLOAT32 ? kick_value->number_value : has_request ? deferred_request.knockback : 0;
  qa_pain_reaction reaction; memset(&reaction, 0, sizeof reaction);
  reaction.self = c->self; reaction.attack = attack; reaction.attacker = attacker; reaction.kick = kick; reaction.damage = amount;
  if (kind == KIND_PAIN) { c->pain = &reaction; (void)qa_actor_callback_table_source_pain(table, reaction, source_pain_execute, c); return; }
  qa_death_reaction death; memset(&death, 0, sizeof death); death.pain = reaction;
  death.inflictor = nullable_actor(s, arg(c->args, c->count, 1), &inflictor_storage) ? &inflictor_storage : NULL;
  death.point = qa_compat_q2_read_classic_vector(memory, qa_compat_q2_guest_required_pointer(c->args, c->count, 4));
  c->death = &death;
  (void)qa_actor_callback_table_source_die(table, death, source_die_execute, c);
}
static qa_guest_call_result callback_invoked(void *ctx, const qa_guest_call_value *args, size_t count, qa_compat_q2_native_entry *binding) {
  callback_entry *entry = ctx; combat_t *s = entry->owner;
  double slot = slot_of(s, qa_compat_q2_guest_required_pointer(args, count, 0)); qa_owned_actor self;
  if (!s->calls.owner(s->calls.ctx, slot, &self)) return qa_compat_q2_native_entry_call_original(binding, args, count);
  qa_guest_address actual;
  if (!qa_compat_q2_mem_read_pointer(s->host->memory, at(s, slot, entry->offset), &actual) || actual.byte_offset != entry->address.byte_offset)
    return qa_compat_q2_native_entry_call_original(binding, args, count);
  (void)callback_table(s);
  source_call call; memset(&call, 0, sizeof call); call.s = s; call.self = self; call.args = args; call.count = count; call.binding = binding;
  if (entry->kind == KIND_USE) source_use(s, &call);
  else if (entry->kind == KIND_TOUCH) source_touch(s, &call);
  else source_reaction(s, &call, entry->kind);
  return qa_compat_q2_call_void();
}

/* --- bind(): the ActorCallbacks and CombatStateBinding closures. --- */
typedef struct guest_call { callback_entry *entry; const qa_guest_call_value *args; size_t count; combat_t *s; qa_actor_id actor; } guest_call;
static void guest_original(void *ctx) { guest_call *g = ctx; (void)qa_compat_q2_native_entry_call_original(g->entry->binding, g->args, g->count); }
static void guest_execution(void *ctx) { guest_call *g = ctx; (void)g->s->calls.source_execution(g->s->calls.ctx, &g->actor, guest_original, g); }
/* TS call(kind, slot, args). */
static void call_kind(bound_actor *bound, kind_t kind, const qa_guest_call_value *args, size_t count) {
  combat_t *s = bound->owner; callback_entry *entry = ensure_entry(s, kind, bound->slot); qa_owned_actor actor;
  if (entry == NULL || !s->calls.owner(s->calls.ctx, bound->slot, &actor)) return;
  guest_call g = {entry, args, count, s, actor.id};
  s->calls.transfer(s->calls.ctx, guest_execution, &g);
}
static void run_touch(void *ctx, const qa_guest_call_value *args, size_t count) { call_kind(ctx, KIND_TOUCH, args, count); }
static void run_pain(void *ctx, const qa_guest_call_value *args, size_t count) { call_kind(ctx, KIND_PAIN, args, count); }
static void run_die(void *ctx, const qa_guest_call_value *args, size_t count) { call_kind(ctx, KIND_DIE, args, count); }
static void callback_use(void *ctx, qa_owned_actor self, const qa_actor_id *other, const qa_actor_id *activator) {
  (void)self; /* TS (_self, other, activator) closes over its bound actor. */
  bound_actor *b = ctx; combat_t *s = b->owner;
  if (!eligible(s, &b->actor.id) || !eligible(s, other) || !eligible(s, activator)) return;
  qa_guest_call_value value = actor_pointer(s, &b->actor.id);
  qa_guest_call_value value2 = actor_pointer(s, other);
  qa_guest_call_value value3 = actor_pointer(s, activator);
  qa_guest_call_value args[3] = {value, value2, value3};
  call_kind(b, KIND_USE, args, 3);
}
static void callback_touch(void *ctx, qa_touch_contact contact) { bound_actor *b = ctx; with_touch(b->owner, &contact, b, run_touch); }
static void callback_pain(void *ctx, qa_pain_reaction reaction) { bound_actor *b = ctx; with_pain(b->owner, &reaction, b, run_pain); }
static void callback_die(void *ctx, qa_death_reaction reaction) { bound_actor *b = ctx; with_die(b->owner, &reaction, b, run_die); }
static double bound_scalar(const bound_actor *b, qa_native_mod_scalar_field field, bool has_value, double value) {
  const combat_t *s = b->owner;
  return s->calls.scalar(s->calls.ctx, s->host->entity(s->host->ctx, b->slot).address, field, has_value, value);
}
static qa_damage_outcome binding_source_damage(void *ctx, qa_damage_request request) { return apply_damage(((bound_actor *)ctx)->owner, request); }
static qa_combat_state binding_read(void *ctx) {
  bound_actor *b = ctx; combat_t *s = b->owner; const qa_native_mod_combat *combat = s->definition->combat;
  qa_combat_state state; memset(&state, 0, sizeof state);
  state.health = bound_scalar(b, combat->health, false, 0); state.mass = bound_scalar(b, combat->mass, false, 0);
  state.armor = qa_compat_q2_native_mod_armor_read(&s->armor, b->slot);
  state.can_take_damage = bound_scalar(b, combat->takedamage, false, 0) != 0;
  /* flags & mask on TS numbers: ToInt32 on both. */
  int32_t flags = qa_js_to_int32(bound_scalar(b, combat->flags.field, false, 0));
  state.invulnerable = (flags & qa_js_to_int32(combat->flags.invulnerable)) != 0;
  state.has_no_knockback = true; state.no_knockback = (flags & qa_js_to_int32(combat->flags.no_knockback)) != 0;
  state.team = NULL;
  if (s->services->match != NULL) {
    qa_source_match_player_result player = s->services->match->player(s->services->match->ctx, b->actor.id);
    if (player.has_value) state.team = player.value.team(player.value.ctx);
  }
  return state;
}
static void binding_write_health(void *ctx, double value) { bound_actor *b = ctx; (void)bound_scalar(b, b->owner->definition->combat->health, true, value); }
static qa_armor_state binding_normalize(void *ctx, qa_armor_state armor) { bound_actor *b = ctx; return qa_compat_q2_native_mod_armor_normalize_legacy(&b->owner->armor, b->slot, armor); }
static void binding_write_armor(void *ctx, qa_armor_state armor) { bound_actor *b = ctx; qa_compat_q2_native_mod_armor_write(&b->owner->armor, b->slot, armor); }
qa_actor_callbacks qa_compat_q2_native_mod_combat_bind(combat_t *s, double slot, qa_owned_actor actor) {
  qa_compat_q2_native_mod_combat_validate(s); watch(s);
  for (int kind = 0; kind < KIND_COUNT; kind++) (void)ensure_entry(s, (kind_t)kind, slot);
  bound_actor *b = calloc(1, sizeof *b); if (b == NULL) qa_range_error("Out of memory");
  b->owner = s; b->slot = slot; b->actor = actor; b->next = s->bound; s->bound = b;
  if (s->definition->combat != NULL) {
    qa_combat_state_binding binding; memset(&binding, 0, sizeof binding);
    binding.ctx = b; binding.source_damage = binding_source_damage; binding.read = binding_read; binding.write_health = binding_write_health;
    binding.normalize_legacy_armor = binding_normalize; binding.write_armor = binding_write_armor;
    qa_gameplay_authority_rebind(s->services->combat, actor, binding);
  }
  qa_actor_callbacks callbacks; memset(&callbacks, 0, sizeof callbacks);
  callbacks.ctx = b; callbacks.think = NULL; callbacks.use = callback_use; callbacks.touch = callback_touch; callbacks.pain = callback_pain; callbacks.die = callback_die;
  return callbacks;
}
void qa_compat_q2_native_mod_combat_validate(combat_t *s) {
  double stride = s->host->entities(s->host->ctx).stride, pointer_bytes = (double)s->host->memory->pointer_bytes; const qa_native_mod_combat *combat = s->definition->combat;
  for (int kind = 0; kind < KIND_COUNT; kind++) { double offset; if (has_offset(s, (kind_t)kind, &offset) && offset + pointer_bytes > stride) qa_range_error("Native callback field exceeds its source actor"); }
  if (combat == NULL) return;
  qa_native_mod_scalar_field fields[4] = {combat->health, combat->mass, combat->takedamage, combat->flags.field};
  for (int i = 0; i < 4; i++) if (fields[i].offset + qa_compat_q2_native_scalar_bytes(fields[i].encoding) > stride) qa_range_error("Native combat field exceeds its source actor");
  const qa_native_mod_deferred_damage *d = combat->deferred; if (d == NULL) return;
  double checks[7][2] = {{d->attacker, pointer_bytes}, {d->inflictor, pointer_bytes}, {d->blood.offset, qa_compat_q2_native_scalar_bytes(d->blood.encoding)}, {d->knockback.offset, qa_compat_q2_native_scalar_bytes(d->knockback.encoding)}, {d->point, 12}, {d->mod, 3}, {d->receipt, 1}};
  for (int i = 0; i < 7; i++) {
    double offset = checks[i][0];
    if (!isfinite(offset) || offset != trunc(offset) || fabs(offset) > (double)QA_MAX_SAFE_INTEGER || offset < 0 || offset + checks[i][1] > stride) qa_range_error("Native deferred damage field exceeds its source actor");
  }
}
void qa_compat_q2_native_mod_combat_release(combat_t *s, qa_actor_id actor) { if (s->has_deferred) qa_compat_q2_native_mod_deferred_damage_state_release(&s->deferred, actor); }
qa_compat_q2_saved_native_deferred_damage *qa_compat_q2_native_mod_combat_checkpoint(combat_t *s, size_t *out_count) {
  *out_count = 0; return s->has_deferred ? qa_compat_q2_native_mod_deferred_damage_state_checkpoint(&s->deferred, out_count) : NULL;
}
void qa_compat_q2_native_mod_combat_validate_saved(combat_t *s, const qa_compat_q2_saved_native_deferred_damage *records, size_t count) { if (s->has_deferred) qa_compat_q2_native_mod_deferred_damage_state_validate_saved(&s->deferred, records, count); }
void qa_compat_q2_native_mod_combat_restore(combat_t *s, const qa_compat_q2_saved_native_deferred_damage *records, size_t count) { if (s->has_deferred) qa_compat_q2_native_mod_deferred_damage_state_restore(&s->deferred, records, count); }
void qa_compat_q2_native_mod_combat_suspend(combat_t *s, bool value) { s->suspended = value; if (value) stop_watching(s); }
void qa_compat_q2_native_mod_combat_close(combat_t *s) {
  if (s->closed) return;
  s->closed = true;
  stop_watching(s);
  for (size_t i = 0; i < s->entry_count; i++) { qa_compat_q2_native_entry_close(s->entries[i]->binding); free(s->entries[i]); }
  s->entry_count = 0;
  if (s->damage != NULL) { qa_compat_q2_native_entry_close(s->damage); s->damage = NULL; }
  if (s->process != NULL) { qa_compat_q2_native_entry_close(s->process); s->process = NULL; }
  if (s->has_deferred) qa_compat_q2_native_mod_deferred_damage_state_clear(&s->deferred);
}
void qa_compat_q2_native_mod_combat_destroy(combat_t *s) {
  if (s == NULL) return;
  qa_compat_q2_native_mod_combat_close(s);
  if (s->has_deferred) qa_compat_q2_native_mod_deferred_damage_state_destroy(&s->deferred);
  qa_compat_q2_native_mod_armor_state_destroy(&s->armor);
  while (s->bound != NULL) { bound_actor *next = s->bound->next; free(s->bound); s->bound = next; }
  free(s->entries); free(s->frames); free(s);
}
