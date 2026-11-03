/* Port of src/compat/q2/native-mod-actors.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_actors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/compat/q2/rerelease/public_state.h"
#include "qa/foundation/js.h"

typedef qa_compat_q2_native_mod_actors actors_t;
typedef struct owned_slot { double slot; qa_owned_actor actor; } owned_slot;
typedef struct body_binding { actors_t *owner; double slot; qa_actor_id ground; struct body_binding *next; } body_binding;
static const qa_guest_value_layout pointer_layout = {false, "pointer", {0}};
struct qa_compat_q2_native_mod_actors {
  const qa_native_mod_source_actors *definition; const qa_native_mod_declaration *declaration;
  const qa_compat_q2_native_mod_host *host; const qa_mod_host_services *services; qa_provider_id instance;
  qa_compat_q2_native_mod_source_calls calls;
  owned_slot *slots; size_t slot_count, slot_capacity;
  double *pending; size_t pending_count, pending_capacity;
  qa_compat_q2_native_entry *allocate_entry, *release_entry;
  qa_compat_q2_native_mod_combat_state *behavior;
  int64_t subscription;
  bool subscribed, suspended, closing, closed, has_tick;
  double frame, next_frame, tick_time;
  body_binding *bodies;
};
static double now_seconds(const actors_t *s) { return qa_compat_q2_time_seconds(qa_compat_q2_services_time(s->services)); }
static double slot_of_address(const actors_t *s, qa_guest_address address) { return qa_compat_q2_native_mod_table_slot(s->host, address, "Native actor pointer is outside its source table"); }
static qa_guest_address at(const actors_t *s, double slot, double offset) { return qa_compat_q2_mem_offset(s->host->memory, s->host->entity(s->host->ctx, slot).address, (int64_t)offset); }
static owned_slot *find_slot(const actors_t *s, double slot) { for (size_t i = 0; i < s->slot_count; i++) if (s->slots[i].slot == slot) return &s->slots[i]; return NULL; }
static owned_slot *find_actor(const actors_t *s, qa_actor_id actor) { for (size_t i = 0; i < s->slot_count; i++) if (qa_same_actor(s->slots[i].actor.id, actor)) return &s->slots[i]; return NULL; }
static bool pending_has(const actors_t *s, double slot) { for (size_t i = 0; i < s->pending_count; i++) if (s->pending[i] == slot) return true; return false; }
static void pending_add(actors_t *s, double slot) { if (pending_has(s, slot)) return; double *v = qa_compat_q2_grow((void **)&s->pending, &s->pending_count, &s->pending_capacity, sizeof *v); *v = slot; }
static void pending_delete(actors_t *s, double slot) { for (size_t i = 0; i < s->pending_count; i++) if (s->pending[i] == slot) { memmove(&s->pending[i], &s->pending[i + 1], (s->pending_count - i - 1) * sizeof *s->pending); s->pending_count--; return; } }
static bool reserved(const actors_t *s, double slot) {
  if (slot == 0) return true;
  const qa_native_mod_declaration *d = s->declaration;
  for (size_t i = 0; i < d->actor_record_count; i++) if (qa_compat_q2_text_equal(d->actor_records[i].id, d->entity_record))
    return slot >= d->actor_records[i].first_slot && slot < d->actor_records[i].first_slot + d->actor_records[i].capacity;
  return false;
}
static void on_release(void *ctx, qa_owned_actor actor) {
  actors_t *s = ctx; owned_slot *owned = find_actor(s, actor.id); if (owned == NULL) return;
  pending_add(s, owned->slot); s->host->presentation.release(s->host->presentation.ctx, actor.id);
}
static void retire(actors_t *s, double slot) {
  owned_slot *owned = find_slot(s, slot); bool has = owned != NULL; qa_owned_actor actor; memset(&actor, 0, sizeof actor);
  if (has) { actor = owned->actor; size_t index = (size_t)(owned - s->slots); memmove(owned, owned + 1, (s->slot_count - index - 1) * sizeof *owned); s->slot_count--; }
  pending_delete(s, slot);
  if (!has) return;
  qa_compat_q2_native_mod_combat_release(s->behavior, actor.id);
  s->host->presentation.release(s->host->presentation.ctx, actor.id);
  if (qa_session_actor_registry_is_live(s->services->actors, actor.id)) qa_session_actor_registry_release(s->services->actors, actor);
}
/* --- Bodies. --- */
static bool classic_api(const actors_t *s) { return s->declaration->target.api.kind == QA_Q2_GAME_API_CLASSIC; }
static qa_vec3 body_vector(const body_binding *b, const char *name, double offset) {
  const actors_t *s = b->owner;
  if (classic_api(s)) return qa_compat_q2_read_classic_vector(s->host->memory, at(s, b->slot, offset));
  qa_compat_q2_rerelease_public_edict *edict = qa_compat_q2_rerelease_public_edict_create(s->host->memory, s->host->entity(s->host->ctx, b->slot));
  if (edict == NULL) qa_range_error("Out of memory");
  qa_vec3 value = qa_compat_q2_rerelease_public_edict_vector(edict, name);
  qa_compat_q2_rerelease_public_edict_destroy(edict); return value;
}
static void body_set_vector(const body_binding *b, const char *name, double offset, qa_vec3 value) {
  const actors_t *s = b->owner;
  if (classic_api(s)) { qa_compat_q2_write_classic_vector(s->host->memory, at(s, b->slot, offset), value); return; }
  qa_compat_q2_rerelease_public_edict *edict = qa_compat_q2_rerelease_public_edict_create(s->host->memory, s->host->entity(s->host->ctx, b->slot));
  if (edict == NULL) qa_range_error("Out of memory");
  qa_compat_q2_rerelease_public_edict_set_vector(edict, name, value);
  qa_compat_q2_rerelease_public_edict_destroy(edict);
}
static qa_body_state body_read(void *ctx) {
  body_binding *b = ctx; const actors_t *s = b->owner; const qa_native_mod_source_fields *fields = &s->definition->fields; qa_compat_q2_memory *memory = s->host->memory;
  qa_body_state state; memset(&state, 0, sizeof state);
  qa_guest_address ground; bool has_ground = qa_compat_q2_mem_read_pointer(memory, at(s, b->slot, fields->ground), &ground);
  state.origin = body_vector(b, "s.origin", 4); state.angles = body_vector(b, "s.angles", 16);
  state.bounds.min = body_vector(b, "mins", 188); state.bounds.max = body_vector(b, "maxs", 200);
  state.velocity = qa_compat_q2_read_classic_vector(memory, at(s, b->slot, fields->velocity));
  if (has_ground && s->calls.actor_at(s->calls.ctx, slot_of_address(s, ground), &b->ground)) state.ground = &b->ground;
  return state;
}
static void body_write(void *ctx, qa_body_state state) {
  body_binding *b = ctx; const actors_t *s = b->owner; const qa_native_mod_source_fields *fields = &s->definition->fields; qa_compat_q2_memory *memory = s->host->memory;
  body_set_vector(b, "s.origin", 4, state.origin); body_set_vector(b, "s.angles", 16, state.angles);
  body_set_vector(b, "mins", 188, state.bounds.min); body_set_vector(b, "maxs", 200, state.bounds.max);
  qa_compat_q2_write_classic_vector(memory, at(s, b->slot, fields->velocity), state.velocity);
  qa_guest_address none; memset(&none, 0, sizeof none);
  qa_guest_address a = at(s, b->slot, fields->ground);
  bool has = state.ground != NULL;
  qa_guest_address v = state.ground != NULL ? s->calls.address(s->calls.ctx, *state.ground) : none;
  qa_compat_q2_mem_write_pointer(memory, a, has, v);
}
static void bind_actor(actors_t *s, double slot, qa_owned_actor actor) {
  body_binding *b = calloc(1, sizeof *b); if (b == NULL) qa_range_error("Out of memory");
  b->owner = s; b->slot = slot; b->next = s->bodies; s->bodies = b;
  qa_body_state_binding binding; memset(&binding, 0, sizeof binding); binding.ctx = b; binding.read = body_read; binding.write = body_write;
  qa_shared_body_table_rebind(s->services->bodies, actor, binding);
  if (s->services->callbacks != NULL) qa_actor_callback_table_bind(s->services->callbacks, actor, qa_compat_q2_native_mod_combat_bind(s->behavior, slot, actor));
}
static void adopt(actors_t *s, double slot) {
  if (reserved(s, slot) || !s->host->active(s->host->ctx, slot)) return;
  if (find_slot(s, slot) != NULL) return;
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(s->services->actors, s->instance, (int64_t)slot, "native:mod-actor");
  owned_slot *entry = qa_compat_q2_grow((void **)&s->slots, &s->slot_count, &s->slot_capacity, sizeof *entry); entry->slot = slot; entry->actor = actor;
  bind_actor(s, slot, actor);
}
static qa_guest_call_result allocate_invoked(void *ctx, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  actors_t *s = ctx; qa_guest_call_result result = qa_compat_q2_native_entry_call_original(binding, values, count);
  if (!s->suspended && result.kind == QA_GUEST_VALUE_POINTER && result.pointer_value.has_value) { double slot = slot_of_address(s, result.pointer_value.value); retire(s, slot); adopt(s, slot); }
  return result;
}
static qa_guest_call_result release_invoked(void *ctx, const qa_guest_call_value *values, size_t count, qa_compat_q2_native_entry *binding) {
  actors_t *s = ctx; bool has_slot = count > 0 && values[0].kind == QA_GUEST_VALUE_POINTER && values[0].pointer_value.has_value;
  double slot = has_slot ? slot_of_address(s, values[0].pointer_value.value) : 0;
  qa_guest_call_result result = qa_compat_q2_native_entry_call_original(binding, values, count);
  if (!s->suspended && has_slot && !s->host->active(s->host->ctx, slot)) retire(s, slot);
  return result;
}
/* TS { ...calls.combat, address, owner, actor }: this object is the combat ctx, so
 * the provider's leaves forward with the provider's own ctx. */
static bool combat_eligible(void *ctx, qa_actor_id actor) { actors_t *s = ctx; return s->calls.combat.eligible(s->calls.combat.ctx, actor); }
static qa_actor_id combat_actor(void *ctx, qa_guest_address address) {
  actors_t *s = ctx; qa_actor_id actor;
  if (!s->calls.actor_at(s->calls.ctx, slot_of_address(s, address), &actor)) qa_range_error("Native callback references an unowned source actor");
  return actor;
}
static qa_guest_address combat_address(void *ctx, qa_actor_id actor) { actors_t *s = ctx; return s->calls.address(s->calls.ctx, actor); }
static bool combat_owner(void *ctx, double slot, qa_owned_actor *out) { return qa_compat_q2_native_mod_actors_actor_at(ctx, slot, out); }
static double combat_scalar(void *ctx, qa_guest_address base, qa_native_mod_scalar_field field, bool has_value, double value) { actors_t *s = ctx; return s->calls.combat.scalar(s->calls.combat.ctx, base, field, has_value, value); }
static void combat_transfer(void *ctx, void (*invoke)(void *), void *invoke_ctx) { actors_t *s = ctx; s->calls.combat.transfer(s->calls.combat.ctx, invoke, invoke_ctx); }
static qa_compat_q2_native_mod_invocation_kind combat_source_execution(void *ctx, const qa_actor_id *actor, void (*invoke)(void *), void *invoke_ctx) { actors_t *s = ctx; return s->calls.combat.source_execution(s->calls.combat.ctx, actor, invoke, invoke_ctx); }
static void combat_synchronize(void *ctx) { actors_t *s = ctx; s->calls.combat.synchronize(s->calls.combat.ctx); }
actors_t *qa_compat_q2_native_mod_actors_create(const qa_native_mod_source_actors *definition, const qa_native_mod_declaration *declaration,
    const qa_compat_q2_native_mod_host *host, const qa_mod_host_services *services, qa_provider_id instance, qa_compat_q2_native_mod_source_calls calls) {
  actors_t *s = calloc(1, sizeof *s); if (s == NULL) qa_range_error("Out of memory");
  s->definition = definition; s->declaration = declaration; s->host = host; s->services = services; s->instance = instance; s->calls = calls;
  s->next_frame = now_seconds(s) + definition->frame_seconds;
  s->subscription = qa_session_actor_registry_on_release(services->actors, on_release, s); s->subscribed = true;
  char id[320]; qa_compat_q2_guest_call_signature signature; memset(&signature, 0, sizeof signature);
  signature.abi = declaration->target.abi; signature.parameters = &pointer_layout; signature.parameter_count = 0; signature.result_is_void = false; signature.result = pointer_layout; signature.variadic = false;
  (void)snprintf(id, sizeof id, "%s:source-allocate", instance);
  s->allocate_entry = qa_compat_q2_native_mod_bind_entry(host, qa_compat_q2_native_mod_entry_address(host, definition->allocate), id, signature, s, allocate_invoked, NULL, NULL);
  qa_compat_q2_native_mod_combat_calls combat; memset(&combat, 0, sizeof combat);
  combat.ctx = s; combat.eligible = combat_eligible; combat.actor = combat_actor; combat.address = combat_address; combat.owner = combat_owner; combat.scalar = combat_scalar;
  combat.transfer = combat_transfer; combat.source_execution = combat_source_execution; combat.synchronize = combat_synchronize;
  s->behavior = qa_compat_q2_native_mod_combat_create(definition, declaration, host, services, instance, combat);
  signature.parameter_count = 1; signature.result_is_void = true; memset(&signature.result, 0, sizeof signature.result);
  (void)snprintf(id, sizeof id, "%s:source-release", instance);
  s->release_entry = qa_compat_q2_native_mod_bind_entry(host, qa_compat_q2_native_mod_entry_address(host, definition->release), id, signature, s, release_invoked, NULL, NULL);
  return s;
}
bool qa_compat_q2_native_mod_actors_advancing(const actors_t *s) { return s->has_tick; }
double qa_compat_q2_native_mod_actors_source_frame(const actors_t *s) { return s->frame; }
double qa_compat_q2_native_mod_actors_source_time(const actors_t *s) { return s->has_tick ? s->tick_time : now_seconds(s); }
bool qa_compat_q2_native_mod_actors_actor_at(const actors_t *s, double slot, qa_owned_actor *out) {
  const owned_slot *owned = find_slot(s, slot);
  if (owned == NULL || !qa_session_actor_registry_is_live(s->services->actors, owned->actor.id)) return false;
  *out = owned->actor; return true;
}
bool qa_compat_q2_native_mod_actors_slot_of(const actors_t *s, qa_actor_id actor, double *out) {
  const owned_slot *owned = find_actor(s, actor); if (owned == NULL) return false;
  *out = owned->slot; return true;
}
qa_compat_q2_native_mod_actor_slot *qa_compat_q2_native_mod_actors_entries(const actors_t *s, size_t *out_count) {
  qa_compat_q2_native_mod_actor_slot *rows = calloc(s->slot_count + 1, sizeof *rows); if (rows == NULL) qa_range_error("Out of memory");
  size_t count = 0;
  for (size_t i = 0; i < s->slot_count; i++) if (qa_session_actor_registry_is_live(s->services->actors, s->slots[i].actor.id)) { rows[count].actor = s->slots[i].actor.id; rows[count].slot = s->slots[i].slot; count++; }
  *out_count = count; return rows;
}
void qa_compat_q2_native_mod_actors_synchronize_clock(actors_t *s) {
  if (s->suspended) return;
  double time = qa_compat_q2_native_mod_actors_source_time(s);
  s->calls.synchronize_frame(s->calls.ctx, time, s->frame);
  for (size_t i = 0; i < s->definition->clock_count; i++) {
    const qa_native_mod_clock_entry *field = &s->definition->clock[i];
    bool milliseconds = !strcmp(field->units, "milliseconds"), is_time = strcmp(field->input, "frame") != 0;
    double value = !is_time ? s->frame : time * (milliseconds ? 1000 : 1);
    bool round_value = is_time && milliseconds && strcmp(field->encoding, "float32") != 0 && strcmp(field->encoding, "float64") != 0;
    s->calls.scalar(s->calls.ctx, s->calls.resolve(s->calls.ctx, field->address), round_value ? qa_js_round(value) : value, field->encoding);
  }
}
void qa_compat_q2_native_mod_actors_validate(actors_t *s) {
  double stride = s->host->entities(s->host->ctx).stride, pointer_bytes = (double)s->host->memory->pointer_bytes; const qa_native_mod_source_fields *f = &s->definition->fields;
  double checks[5][2] = {{f->velocity, 12}, {f->ground, pointer_bytes}, {f->think, pointer_bytes}, {f->has_use ? f->use : 0, pointer_bytes}, {f->nextthink.offset, qa_compat_q2_native_scalar_bytes(f->nextthink.encoding)}};
  for (int i = 0; i < 5; i++) {
    double offset = checks[i][0];
    if (!isfinite(offset) || offset != trunc(offset) || fabs(offset) > (double)QA_MAX_SAFE_INTEGER || offset < 0 || offset + checks[i][1] > stride) qa_range_error("Native owned-actor field exceeds the source stride");
  }
  s->host->memory->check(s->host->memory->ctx, qa_compat_q2_native_mod_entry_address(s->host, s->definition->update.entry), 1, "execute");
  qa_compat_q2_native_mod_combat_validate(s->behavior);
  qa_compat_q2_native_mod_actors_synchronize_clock(s);
}
void qa_compat_q2_native_mod_actors_drain_releases(actors_t *s) {
  if (s->suspended) return;
  size_t count = s->pending_count; double *snapshot = calloc(count + 1, sizeof *snapshot); if (snapshot == NULL) qa_range_error("Out of memory");
  if (count != 0) memcpy(snapshot, s->pending, count * sizeof *snapshot);
  for (size_t i = 0; i < count; i++) {
    double slot = snapshot[i];
    if (s->host->active(s->host->ctx, slot)) {
      qa_guest_call_value value = qa_compat_q2_call_pointer(true, s->host->entity(s->host->ctx, slot).address); qa_guest_call_result out;
      (void)s->calls.invoke(s->calls.ctx, qa_compat_q2_native_mod_entry_address(s->host, s->definition->release), &value, 1, "void", NULL, &out);
    }
    if (!s->closing && s->host->active(s->host->ctx, slot)) qa_range_error("Native source refused to release its owned actor");
    retire(s, slot);
  }
  free(snapshot);
}
typedef struct tick_call { actors_t *s; } tick_call;
static void tick_body(void *ctx) {
  actors_t *s = ((tick_call *)ctx)->s;
  qa_compat_q2_native_mod_actors_synchronize_clock(s);
  s->calls.begin_frame(s->calls.ctx);
  for (double slot = 0; !s->closing && slot < s->host->entities(s->host->ctx).count; slot++) {
    if (s->calls.client_frame(s->calls.ctx, slot)) continue;
    owned_slot *owned = find_slot(s, slot); if (owned == NULL) continue;
    qa_owned_actor actor = owned->actor;
    if (!qa_session_actor_registry_is_live(s->services->actors, actor.id) || !s->host->active(s->host->ctx, slot)) { retire(s, slot); continue; }
    qa_guest_call_value value = qa_compat_q2_call_pointer(true, s->host->entity(s->host->ctx, slot).address); qa_guest_call_result out;
    (void)s->calls.invoke(s->calls.ctx, qa_compat_q2_native_mod_entry_address(s->host, s->definition->update.entry), &value, 1, s->definition->update.returns, &actor.id, &out);
  }
  if (!s->closing) s->calls.end_frame(s->calls.ctx);
}
static void tick_cleanup(void *ctx) { ((tick_call *)ctx)->s->has_tick = false; }
void qa_compat_q2_native_mod_actors_advance(actors_t *s, qa_frame_context frame) {
  if (s->suspended || s->closing) return;
  double now = qa_compat_q2_time_seconds(frame.time);
  qa_compat_q2_native_mod_actors_drain_releases(s);
  while (!s->closing && s->next_frame <= now + 1e-9) {
    s->has_tick = true; s->tick_time = s->next_frame; s->next_frame += s->definition->frame_seconds; s->frame++;
    tick_call call = {s};
    qa_compat_q2_native_mod_finally(tick_body, &call, tick_cleanup, &call);
  }
}
void qa_compat_q2_native_mod_actors_suspend(actors_t *s, bool value) { s->suspended = value; qa_compat_q2_native_mod_combat_suspend(s->behavior, value); }
qa_compat_q2_saved_native_actors qa_compat_q2_native_mod_actors_checkpoint(actors_t *s) {
  qa_compat_q2_saved_native_actors saved; memset(&saved, 0, sizeof saved);
  saved.frame = s->frame; saved.next_frame = s->next_frame;
  saved.deferred = qa_compat_q2_native_mod_combat_checkpoint(s->behavior, &saved.deferred_count);
  size_t count = 0; qa_compat_q2_native_mod_actor_slot *rows = qa_compat_q2_native_mod_actors_entries(s, &count);
  saved.actors = calloc(count + 1, sizeof *saved.actors); if (saved.actors == NULL) qa_range_error("Out of memory");
  saved.actor_count = count;
  for (size_t i = 0; i < count; i++) {
    saved.actors[i].actor.slot = (double)rows[i].actor.slot; saved.actors[i].actor.generation = (double)rows[i].actor.generation; saved.actors[i].slot = rows[i].slot;
    saved.actors[i].linked = qa_shared_body_table_linked(s->services->bodies, rows[i].actor).has_value;
  }
  free(rows); return saved;
}
void qa_compat_q2_saved_native_actors_free(qa_compat_q2_saved_native_actors *saved) { free(saved->deferred); free(saved->actors); memset(saved, 0, sizeof *saved); }
qa_compat_q2_native_owned_saved *qa_compat_q2_native_mod_actors_validate_saved(actors_t *s, const qa_compat_q2_saved_native_actors *saved, size_t *out_count) {
  qa_compat_q2_native_mod_combat_validate_saved(s->behavior, saved->deferred, saved->deferred_count);
  qa_compat_q2_native_owned_saved *rows = calloc(saved->actor_count + 1, sizeof *rows); if (rows == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < saved->actor_count; i++) {
    const qa_compat_q2_saved_native_actor *entry = &saved->actors[i];
    qa_actor_id id = s->services->reference_saved != NULL ? s->services->reference_saved(s->services->ctx, entry->actor) : qa_session_actor_registry_reference_saved(s->services->actors, entry->actor, "current");
    qa_owned_actor actor; bool has = qa_session_actor_registry_resolve_owned(s->services->actors, id, &actor);
    qa_actor_source_result source = qa_session_actor_registry_source_of(s->services->actors, id);
    if (!has || !qa_compat_q2_text_equal(actor.owner, s->instance) || reserved(s, entry->slot) || !source.has_value || (double)source.slot != entry->slot) {
      char source_text[32] = "unbound"; if (source.has_value) (void)snprintf(source_text, sizeof source_text, "%lld", (long long)source.slot);
      qa_range_error("Saved native owned actor %.0f/%.0f does not belong to %s/%.0f: %s/%s", entry->actor.slot, entry->actor.generation, s->instance, entry->slot, has ? actor.owner : "expired", source_text);
    }
    rows[i].actor = actor; rows[i].slot = entry->slot; rows[i].linked = entry->linked;
  }
  *out_count = saved->actor_count; return rows;
}
void qa_compat_q2_native_mod_actors_restore(actors_t *s, const qa_compat_q2_saved_native_actors *saved, const qa_compat_q2_native_owned_saved *actors, size_t count) {
  for (size_t i = 0; i < count; i++) if (actors[i].slot >= s->host->entities(s->host->ctx).count || !s->host->active(s->host->ctx, actors[i].slot)) qa_range_error("Original native save lost an owned source actor");
  s->slot_count = 0; s->pending_count = 0; s->frame = saved->frame; s->next_frame = saved->next_frame;
  for (size_t i = 0; i < count; i++) { owned_slot *entry = qa_compat_q2_grow((void **)&s->slots, &s->slot_count, &s->slot_capacity, sizeof *entry); entry->slot = actors[i].slot; entry->actor = actors[i].actor; }
  for (size_t i = 0; i < count; i++) { bind_actor(s, actors[i].slot, actors[i].actor); if (actors[i].linked) qa_shared_body_table_link(s->services->bodies, actors[i].actor, NULL); }
  qa_compat_q2_native_mod_combat_restore(s->behavior, saved->deferred, saved->deferred_count);
}
void qa_compat_q2_native_mod_actors_close(actors_t *s) {
  if (s->closing) return;
  s->closing = true;
  for (size_t i = 0; i < s->slot_count; i++) pending_add(s, s->slots[i].slot);
  qa_compat_q2_native_mod_actors_drain_releases(s);
  if (s->subscribed) { qa_session_actor_registry_off_release(s->services->actors, s->subscription); s->subscribed = false; }
  while (s->slot_count > 0) retire(s, s->slots[0].slot);
  qa_compat_q2_native_mod_combat_close(s->behavior);
  /* removals.invokeReverse(): release, then allocate. */
  if (s->release_entry != NULL) { qa_compat_q2_native_entry_close(s->release_entry); s->release_entry = NULL; }
  if (s->allocate_entry != NULL) { qa_compat_q2_native_entry_close(s->allocate_entry); s->allocate_entry = NULL; }
}
void qa_compat_q2_native_mod_actors_destroy(actors_t *s) {
  if (s == NULL) return;
  qa_compat_q2_native_mod_actors_close(s);
  qa_compat_q2_native_mod_combat_destroy(s->behavior);
  while (s->bodies != NULL) { body_binding *next = s->bodies->next; free(s->bodies); s->bodies = next; }
  free(s->slots); free(s->pending); free(s);
}
