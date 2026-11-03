/* Port of src/compat/q2/native-mod-deferred.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/native_mod_deferred.h"
#include <stdlib.h>
#include <string.h>
#include "qa/compat/q2/classic/records.h"
#include "qa/persistence/save_image.h"
#include "qa/persistence/shared.h"
#include "qa/foundation/js.h"

typedef qa_save_reader reader_t;
typedef qa_compat_q2_native_mod_deferred_damage_state state_t;
typedef qa_compat_q2_saved_native_deferred_damage saved_t;
static const reader_t *field(const reader_t *r, const char *name, reader_t *out) { if (!qa_save_reader_field(r, name, out)) qa_range_error("Out of memory"); return out; }
saved_t qa_compat_q2_read_native_deferred_damage(const reader_t *reader) {
  saved_t o; memset(&o, 0, sizeof o); reader_t request, mod, x;
  field(reader, "request", &request); field(reader, "mod", &mod);
  size_t count = qa_save_reader_list_count(&mod); bool over = false;
  for (size_t i = 0; i < count; i++) {
    reader_t item; if (!qa_save_reader_item(&mod, i, &item)) qa_range_error("Out of memory");
    double value = qa_save_reader_integer_min(&item, 0); qa_save_reader_destroy(&item);
    if (value > 255) over = true;
    if (i < 3) o.mod[i] = value;
  }
  if (count != 3 || over) qa_save_reader_fail(reader, "native deferred damage requires its three-byte mod_t");
  qa_save_reader_destroy(&mod);
  o.target = qa_read_saved_actor(field(reader, "target", &x)); qa_save_reader_destroy(&x);
  o.attack = qa_read_q2_attack_checkpoint(field(reader, "attack", &x)); qa_save_reader_destroy(&x);
  o.amount = qa_save_reader_finite(field(&request, "amount", &x)); qa_save_reader_destroy(&x);
  o.knockback_req = qa_save_reader_finite(field(&request, "knockback", &x)); qa_save_reader_destroy(&x);
  o.point_req = qa_read_vector(field(&request, "point", &x)); qa_save_reader_destroy(&x);
  o.direction = qa_read_vector(field(&request, "direction", &x)); qa_save_reader_destroy(&x);
  o.normal = qa_read_vector(field(&request, "normal", &x)); qa_save_reader_destroy(&x);
  { static const char *const deliveries[] = {"direct", "radius"}; o.delivery = qa_save_reader_choice_string(field(&request, "delivery", &x), deliveries, 2); qa_save_reader_destroy(&x); }
  qa_save_reader_destroy(&request);
  o.blood = qa_save_reader_finite(field(reader, "blood", &x)); qa_save_reader_destroy(&x);
  o.knockback = qa_save_reader_finite(field(reader, "knockback", &x)); qa_save_reader_destroy(&x);
  o.point = qa_read_vector(field(reader, "point", &x)); qa_save_reader_destroy(&x);
  field(reader, "attackerSlot", &x); o.has_attacker = !qa_save_reader_is_null(&x); if (o.has_attacker) o.attacker_slot = qa_save_reader_integer_min(&x, 0); qa_save_reader_destroy(&x);
  field(reader, "inflictorSlot", &x); o.has_inflictor = !qa_save_reader_is_null(&x); if (o.has_inflictor) o.inflictor_slot = qa_save_reader_integer_min(&x, 0); qa_save_reader_destroy(&x);
  return o;
}
static qa_save_value *set_value(qa_save_value *object, const char *key, qa_save_value *value) {
  if (value == NULL || !qa_save_value_set(object, key, value)) qa_range_error("Out of memory");
  return object;
}
static qa_save_value *vector_value(qa_vec3 v) {
  qa_save_value *o = qa_save_value_object(); if (o == NULL) qa_range_error("Out of memory");
  set_value(o, "x", qa_save_value_number(v.x)); set_value(o, "y", qa_save_value_number(v.y)); set_value(o, "z", qa_save_value_number(v.z));
  return o;
}
qa_save_value *qa_compat_q2_native_deferred_damage_value(const saved_t *r) {
  qa_save_value *o = qa_save_value_object(), *target = qa_save_value_object(), *request = qa_save_value_object(), *mod = qa_save_value_array();
  if (o == NULL || target == NULL || request == NULL || mod == NULL) qa_range_error("Out of memory");
  set_value(target, "slot", qa_save_value_number(r->target.slot)); set_value(target, "generation", qa_save_value_number(r->target.generation));
  set_value(o, "target", target); set_value(o, "attack", qa_q2_attack_checkpoint_value(&r->attack));
  set_value(request, "amount", qa_save_value_number(r->amount)); set_value(request, "knockback", qa_save_value_number(r->knockback_req));
  set_value(request, "point", vector_value(r->point_req)); set_value(request, "direction", vector_value(r->direction)); set_value(request, "normal", vector_value(r->normal));
  set_value(request, "delivery", qa_save_value_string(r->delivery)); set_value(o, "request", request);
  set_value(o, "blood", qa_save_value_number(r->blood)); set_value(o, "knockback", qa_save_value_number(r->knockback)); set_value(o, "point", vector_value(r->point));
  for (int i = 0; i < 3; i++) { qa_save_value *v = qa_save_value_number(r->mod[i]); if (v == NULL || !qa_save_value_push(mod, v)) qa_range_error("Out of memory"); }
  set_value(o, "mod", mod);
  set_value(o, "attackerSlot", r->has_attacker ? qa_save_value_number(r->attacker_slot) : qa_save_value_null());
  set_value(o, "inflictorSlot", r->has_inflictor ? qa_save_value_number(r->inflictor_slot) : qa_save_value_null());
  return o;
}

/* One retained request; its actor pointers aim at this record's storage. */
struct qa_compat_q2_native_deferred_pending { qa_actor_id actor; qa_damage_request request; qa_actor_id attacker, inflictor; };
typedef qa_compat_q2_native_deferred_pending pending_t;
static void retain_request(pending_t *entry, qa_damage_request request) {
  entry->request = qa_capture_request(request);
  if (request.attack.attacker != NULL) { entry->attacker = *request.attack.attacker; entry->request.attack.attacker = &entry->attacker; }
  if (request.attack.inflictor != NULL) { entry->inflictor = *request.attack.inflictor; entry->request.attack.inflictor = &entry->inflictor; }
}
void qa_compat_q2_native_mod_deferred_damage_state_init(state_t *s, const qa_native_mod_deferred_damage *definition, qa_native_mod_scalar_field health,
    const qa_compat_q2_native_mod_host *host, const qa_mod_host_services *services, qa_compat_q2_native_deferred_calls calls, void *current_ctx, qa_compat_q2_native_deferred_current_fn current) {
  memset(s, 0, sizeof *s);
  s->definition = definition; s->health = health; s->host = host; s->services = services; s->calls = calls; s->current_ctx = current_ctx; s->current = current;
}
void qa_compat_q2_native_mod_deferred_damage_state_clear(state_t *s) {
  for (size_t i = 0; i < s->pending_count; i++) free(s->pending[i]);
  s->pending_count = 0;
}
void qa_compat_q2_native_mod_deferred_damage_state_destroy(state_t *s) {
  qa_compat_q2_native_mod_deferred_damage_state_clear(s);
  free(s->pending); free(s->processing); s->pending = s->processing = NULL; s->pending_capacity = s->processing_capacity = 0; s->processing_count = 0;
}
static size_t find_pending(const state_t *s, qa_actor_id actor) {
  for (size_t i = 0; i < s->pending_count; i++) if (qa_same_actor(s->pending[i]->actor, actor)) return i;
  return (size_t)-1;
}
/* Map.delete keeps the survivors in insertion order. Returns the removed entry. */
static pending_t *take_pending(state_t *s, qa_actor_id actor) {
  size_t index = find_pending(s, actor); if (index == (size_t)-1) return NULL;
  pending_t *entry = s->pending[index];
  memmove(&s->pending[index], &s->pending[index + 1], (s->pending_count - index - 1) * sizeof *s->pending); s->pending_count--;
  return entry;
}
static pending_t *new_pending(qa_actor_id actor) { pending_t *e = calloc(1, sizeof *e); if (e == NULL) qa_range_error("Out of memory"); e->actor = actor; return e; }
static void append_pending(state_t *s, pending_t *entry) {
  pending_t **slot = qa_compat_q2_grow((void **)&s->pending, &s->pending_count, &s->pending_capacity, sizeof *slot); *slot = entry;
}
static qa_guest_address at(const state_t *s, double slot, double offset) {
  const qa_compat_q2_native_mod_host *h = s->host;
  return qa_compat_q2_mem_offset(h->memory, h->entity(h->ctx, slot).address, (int64_t)offset);
}
static double scalar(const state_t *s, double slot, qa_native_mod_scalar_field f, bool has_value, double value) {
  const qa_compat_q2_native_mod_host *h = s->host;
  return s->calls.scalar(s->calls.ctx, h->entity(h->ctx, slot).address, f, has_value, value);
}
/* False is TS null. */
static bool pointer_slot(const state_t *s, double slot, double offset, double *out) {
  qa_guest_address address;
  if (!qa_compat_q2_mem_read_pointer(s->host->memory, at(s, slot, offset), &address)) return false;
  *out = qa_compat_q2_native_mod_table_slot(s->host, address, "Native deferred damage pointer is outside its source table");
  return true;
}
static bool hits(qa_compat_q2_written_range range, double offset, double bytes) { return range.byte_offset < offset + bytes && offset < range.byte_offset + range.byte_length; }
void qa_compat_q2_native_mod_deferred_damage_state_changed(state_t *s, double slot, qa_compat_q2_written_range range) {
  qa_owned_actor actor; if (!s->calls.owner(s->calls.ctx, slot, &actor)) return;
  const qa_native_mod_deferred_damage *d = s->definition;
  if (hits(range, d->receipt, 1) && scalar(s, slot, d->blood, false, 0) != 0) {
    qa_damage_request request;
    if (!s->current(s->current_ctx, &request) || !qa_same_actor(request.target, actor.id)) qa_range_error("Native deferred damage has no original attack provenance");
    size_t index = find_pending(s, actor.id);
    if (index != (size_t)-1) retain_request(s->pending[index], request);
    else { pending_t *entry = new_pending(actor.id); retain_request(entry, request); append_pending(s, entry); }
  }
  if (hits(range, d->blood.offset, qa_compat_q2_native_scalar_bytes(d->blood.encoding)) && scalar(s, slot, d->blood, false, 0) == 0) free(take_pending(s, actor.id));
}
bool qa_compat_q2_native_mod_deferred_damage_state_attack(state_t *s, qa_actor_id actor, qa_damage_request *out) {
  if (s->processing_count == 0) return false;
  pending_t *top = s->processing[s->processing_count - 1];
  if (!qa_same_actor(top->request.target, actor)) return false;
  *out = top->request; return true;
}
typedef struct process_call { state_t *s; pending_t *entry; double slot, blood; void *ctx; void (*original)(void *); } process_call;
static void process_body(void *value) {
  process_call *c = value; state_t *s = c->s; qa_source_damage_result result;
  result.reaction = scalar(s, c->slot, s->health, false, 0) <= 0 ? "death" : "pain"; result.applied_damage = c->blood;
  qa_gameplay_authority_source_reaction(s->services->combat, c->entry->request, result);
  c->original(c->ctx);
}
static void process_cleanup(void *value) { process_call *c = value; c->s->processing_count--; free(c->entry); }
void qa_compat_q2_native_mod_deferred_damage_state_process(state_t *s, qa_actor_id actor, double slot, void *original_ctx, void (*original)(void *)) {
  size_t index = find_pending(s, actor); double blood = scalar(s, slot, s->definition->blood, false, 0);
  if (index == (size_t)-1 || blood == 0) { original(original_ctx); return; }
  pending_t *entry = take_pending(s, actor);
  entry->request.knockback = scalar(s, slot, s->definition->knockback, false, 0);
  entry->request.point = qa_compat_q2_read_classic_vector(s->host->memory, at(s, slot, s->definition->point));
  entry->request = qa_capture_request(entry->request);
  pending_t **top = qa_compat_q2_grow((void **)&s->processing, &s->processing_count, &s->processing_capacity, sizeof *top); *top = entry;
  process_call call = {s, entry, slot, blood, original_ctx, original};
  qa_compat_q2_native_mod_finally(process_body, &call, process_cleanup, &call);
}
void qa_compat_q2_native_mod_deferred_damage_state_release(state_t *s, qa_actor_id actor) { free(take_pending(s, actor)); }
/* TS saveQ2Attack. */
static qa_q2_attack_checkpoint save_attack(qa_attack_provenance a) { return qa_q2_save_attack(a); }
static qa_actor_id reference(const state_t *s, qa_saved_actor_id actor) {
  return s->services->reference_saved != NULL ? s->services->reference_saved(s->services->ctx, actor) : qa_session_actor_registry_reference_saved(s->services->actors, actor, "current");
}
static qa_actor_id reference_saved(void *ctx, qa_saved_actor_id actor) { return reference(ctx, actor); }
/* TS restoreQ2Attack with this state's saved-actor reference. */
static void restore_attack(const state_t *s, pending_t *entry, const qa_q2_attack_checkpoint *in) {
  entry->request.attack = qa_q2_restore_attack(*in, reference_saved, (void *)s, &entry->attacker, &entry->inflictor);
}
saved_t *qa_compat_q2_native_mod_deferred_damage_state_checkpoint(state_t *s, size_t *out_count) {
  saved_t *records = calloc(s->pending_count + 1, sizeof *records); if (records == NULL) qa_range_error("Out of memory");
  size_t count = 0; const qa_native_mod_deferred_damage *d = s->definition;
  for (size_t i = 0; i < s->pending_count; i++) {
    const pending_t *entry = s->pending[i]; const qa_damage_request *request = &entry->request;
    qa_actor_source_result source = qa_session_actor_registry_source_of(s->services->actors, entry->actor);
    if (!source.has_value || !qa_session_actor_registry_is_live(s->services->actors, entry->actor)) continue;
    double slot = (double)source.slot; saved_t *r = &records[count++];
    r->target = qa_saved_actor_id_of(request->target); r->attack = save_attack(request->attack);
    r->amount = request->amount; r->knockback_req = request->knockback; r->point_req = request->point; r->direction = request->direction; r->normal = request->normal; r->delivery = request->delivery;
    r->blood = scalar(s, slot, d->blood, false, 0); r->knockback = scalar(s, slot, d->knockback, false, 0);
    r->point = qa_compat_q2_read_classic_vector(s->host->memory, at(s, slot, d->point));
    uint8_t mod[3]; s->host->memory->copy(s->host->memory->ctx, at(s, slot, d->mod), mod, 3);
    for (int m = 0; m < 3; m++) r->mod[m] = mod[m];
    r->has_attacker = pointer_slot(s, slot, d->attacker, &r->attacker_slot);
    r->has_inflictor = pointer_slot(s, slot, d->inflictor, &r->inflictor_slot);
  }
  *out_count = count; return records;
}
/* TS restoreRequest. */
static pending_t *restore_request(const state_t *s, const saved_t *record) {
  qa_actor_id target = reference(s, record->target);
  qa_actor_source_result source = qa_session_actor_registry_source_of(s->services->actors, target);
  if (!source.has_value || !qa_compat_q2_text_equal(source.provider, s->host->memory->module.id) || !qa_session_actor_registry_is_live(s->services->actors, target))
    qa_range_error("Saved deferred damage target is not owned by this native mod");
  pending_t *entry = new_pending(target);
  entry->request.target = target; entry->request.amount = record->amount; entry->request.knockback = record->knockback_req;
  entry->request.point = record->point_req; entry->request.direction = record->direction; entry->request.normal = record->normal; entry->request.delivery = record->delivery;
  restore_attack(s, entry, &record->attack);
  entry->request = qa_capture_request(entry->request);
  return entry;
}
void qa_compat_q2_native_mod_deferred_damage_state_validate_saved(state_t *s, const saved_t *records, size_t count) {
  for (size_t i = 0; i < count; i++) free(restore_request(s, &records[i]));
}
void qa_compat_q2_native_mod_deferred_damage_state_restore(state_t *s, const saved_t *records, size_t count) {
  pending_t **prepared = calloc(count + 1, sizeof *prepared); if (prepared == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < count; i++) prepared[i] = restore_request(s, &records[i]);
  const qa_compat_q2_native_mod_host *h = s->host; const qa_native_mod_deferred_damage *d = s->definition; qa_compat_q2_memory *memory = h->memory;
  for (size_t i = 0; i < count; i++) {
    double table_count = h->entities(h->ctx).count;
    if ((records[i].has_attacker && records[i].attacker_slot >= table_count) || (records[i].has_inflictor && records[i].inflictor_slot >= table_count))
      qa_range_error("Saved deferred damage pointer exceeds the restored source table");
  }
  qa_compat_q2_native_mod_deferred_damage_state_clear(s);
  for (size_t i = 0; i < count; i++) {
    const saved_t *record = &records[i];
    qa_actor_source_result source = qa_session_actor_registry_source_of(s->services->actors, prepared[i]->request.target);
    if (!source.has_value) qa_range_error("Saved deferred damage target lost its source slot");
    double slot = (double)source.slot;
    qa_guest_address none; memset(&none, 0, sizeof none);
    qa_guest_address a2 = at(s, slot, d->attacker);
    bool has2 = record->has_attacker;
    qa_guest_address v2 = record->has_attacker ? h->entity(h->ctx, record->attacker_slot).address : none;
    qa_compat_q2_mem_write_pointer(memory, a2, has2, v2);
    qa_guest_address a = at(s, slot, d->inflictor);
    bool has = record->has_inflictor;
    qa_guest_address v = record->has_inflictor ? h->entity(h->ctx, record->inflictor_slot).address : none;
    qa_compat_q2_mem_write_pointer(memory, a, has, v);
    scalar(s, slot, d->blood, true, record->blood); scalar(s, slot, d->knockback, true, record->knockback);
    qa_compat_q2_write_classic_vector(memory, at(s, slot, d->point), record->point);
    uint8_t mod[3] = {qa_js_to_uint8(record->mod[0]), qa_js_to_uint8(record->mod[1]), qa_js_to_uint8(record->mod[2])};
    memory->write(memory->ctx, at(s, slot, d->mod), mod, 3);
    size_t existing = find_pending(s, prepared[i]->request.target);
    if (existing != (size_t)-1) { free(s->pending[existing]); s->pending[existing] = prepared[i]; }
    else append_pending(s, prepared[i]);
  }
  free(prepared);
}
