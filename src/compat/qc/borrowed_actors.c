/* Port of src/compat/qc/borrowed-actors.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/borrowed_actors.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/persistence/save_image.h"
static const char *const names[14] = {"origin","angles","velocity","mins","maxs","absmin","absmax","size","chain","invincible_sound","health","takedamage","classname","solid"};
enum { F_ORIGIN, F_ANGLES, F_VELOCITY, F_MINS, F_MAXS, F_ABSMIN, F_ABSMAX, F_SIZE, F_CHAIN, F_INVSND, F_HEALTH, F_TAKEDAMAGE, F_CLASSNAME, F_SOLID };
static bool owned_eq(qa_owned_actor a, qa_owned_actor b) { return qa_same_owned_actor(a, b); }
static qa_compat_qc_borrowed_row *find_actor(qa_compat_qc_borrowed_actors *b, qa_owned_actor a) {
  for (size_t i = 0; i < b->row_count; i++) { if (owned_eq(b->rows[i].actor, a)) return &b->rows[i]; } return NULL;
}
static qa_compat_qc_borrowed_row *find_slot(qa_compat_qc_borrowed_actors *b, int64_t slot) {
  for (size_t i = 0; i < b->row_count; i++) { if (b->rows[i].slot == slot) return &b->rows[i]; } return NULL;
}
static void remove_row(qa_compat_qc_borrowed_actors *b, qa_compat_qc_borrowed_row *r) {
  size_t i = (size_t)(r - b->rows); free(r->classname);
  memmove(&b->rows[i], &b->rows[i + 1], (b->row_count - i - 1) * sizeof *b->rows); b->row_count--;
}
static void on_release(void *ctx, qa_owned_actor actor) {
  qa_compat_qc_borrowed_actors *b = ctx;
  qa_compat_qc_borrowed_row *r = find_actor(b, actor);
  if (!r) return;
  int64_t slot = r->slot; remove_row(b, r);
  b->host.slots_options.storage.clear_freed(b->host.slots_options.storage.ctx, slot, b->host.slots_options.now(b->host.slots_options.ctx));
}
void qa_compat_qc_borrowed_actors_init(qa_compat_qc_borrowed_actors *b, qa_compat_qc_borrowed_host host) {
  memset(b, 0, sizeof *b); b->host = host;
  b->release_id = qa_session_actor_registry_on_release(host.actors, on_release, b);
}
void qa_compat_qc_borrowed_actors_destroy(qa_compat_qc_borrowed_actors *b) {
  qa_session_actor_registry_off_release(b->host.actors, b->release_id);
  for (size_t i = 0; i < b->row_count; i++) free(b->rows[i].classname);
  free(b->rows); free(b->fields_by_word); memset(b, 0, sizeof *b);
}
bool qa_compat_qc_borrowed_actors_actor(qa_compat_qc_borrowed_actors *b, int64_t slot, qa_owned_actor *out) {
  qa_compat_qc_borrowed_row *r = find_slot(b, slot); if (!r) return false;
  qa_session_actor_registry_assert_owned(b->host.actors, r->actor); *out = r->actor; return true;
}
static void field_offsets(qa_compat_qc_borrowed_actors *b) {
  if (b->offsets_ready) return;
  qa_compat_qc_machine *vm = b->host.machine(b->host.ctx);
  int64_t maxw = 0;
  for (int i = 0; i < 14; i++) { b->offsets[i] = qa_compat_qc_machine_field_offset(vm, names[i]); int64_t end = b->offsets[i] + (i < 8 ? 3 : 1); if (end > maxw) maxw = end; }
  b->fields_by_word = malloc((size_t)(maxw ? maxw : 1) * sizeof(int64_t));
  if (!b->fields_by_word) qa_compat_qc_program_error("progs.dat", "borrowed field allocation failed");
  for (int64_t i = 0; i < maxw; i++) b->fields_by_word[i] = -1;
  b->fields_by_word_count = (size_t)maxw;
  for (int i = 0; i < 14; i++) { int64_t width = i < 8 ? 3 : 1; for (int64_t w = b->offsets[i]; w < b->offsets[i] + width; w++) b->fields_by_word[w] = i; }
  b->offsets_ready = true;
}
static void refresh(qa_compat_qc_borrowed_actors *b, qa_compat_qc_borrowed_row *row, const bool requested[14]) {
  qa_compat_qc_machine *vm = b->host.machine(b->host.ctx);
  qa_compat_qc_words *fields = qa_compat_qc_entity_memory_at(vm->entities, row->slot);
  field_offsets(b);
  bool any_vec = false; for (int i = 0; i < 8; i++) if (requested[i]) any_vec = true;
  if (any_vec) {
    qa_body_state_result body_r = b->host.bodies.read(b->host.bodies.ctx, row->actor.id);
    if (!body_r.has_value) qa_compat_qc_program_error("progs.dat", "Borrowed QC actor has no canonical body");
    qa_body_state body = body_r.value;
    qa_linked_body_result linked = (requested[F_ABSMIN] || requested[F_ABSMAX]) ? b->host.bodies.linked(b->host.bodies.ctx, row->actor.id) : (qa_linked_body_result){0};
    for (int i = 0; i < 8; i++) if (requested[i]) {
      qa_vec3 v;
      switch (i) {
        case F_ORIGIN: v = body.origin; break;
        case F_ANGLES: v = body.angles; break;
        case F_VELOCITY: v = body.velocity; break;
        case F_MINS: v = body.bounds.min; break;
        case F_MAXS: v = body.bounds.max; break;
        case F_ABSMIN: v = linked.has_value ? linked.value.absolute_bounds.min : (qa_vec3){body.origin.x + body.bounds.min.x, body.origin.y + body.bounds.min.y, body.origin.z + body.bounds.min.z}; break;
        case F_ABSMAX: v = linked.has_value ? linked.value.absolute_bounds.max : (qa_vec3){body.origin.x + body.bounds.max.x, body.origin.y + body.bounds.max.y, body.origin.z + body.bounds.max.z}; break;
        default: v = (qa_vec3){body.bounds.max.x - body.bounds.min.x, body.bounds.max.y - body.bounds.min.y, body.bounds.max.z - body.bounds.min.z}; break;
      }
      if (!isfinite((double)qa_float32(v.x)) || !isfinite((double)qa_float32(v.y)) || !isfinite((double)qa_float32(v.z)))
        qa_compat_qc_program_error("progs.dat", "Borrowed actor geometry exceeds QC binary32 range");
      qa_compat_qc_words_set_vector(fields, b->offsets[i], v);
    }
  }
  if (requested[F_HEALTH] || requested[F_TAKEDAMAGE]) {
    double health = 0; bool can = false; bool has = b->host.combat_read(b->host.ctx, row->actor.id, &health, &can);
    if (!has) { health = 0; can = false; }
    if (requested[F_HEALTH]) {
      if (!isfinite((double)qa_float32(health))) qa_compat_qc_program_error("progs.dat", "Borrowed actor health exceeds QC binary32 range");
      qa_compat_qc_words_set_float(fields, b->offsets[F_HEALTH], health);
    }
    if (requested[F_TAKEDAMAGE]) qa_compat_qc_words_set_float(fields, b->offsets[F_TAKEDAMAGE], can ? 1 : 0);
  }
  if (requested[F_CLASSNAME]) {
    const char *cn = b->host.classname(b->host.ctx, row->actor.id);
    if (!row->classname || strcmp(row->classname, cn) != 0) {
      char engine_name[64]; snprintf(engine_name, sizeof engine_name, "borrowed-classname:%lld", (long long)row->slot);
      int64_t cap = (int64_t)strlen(cn) + 1; if (cap < 128) cap = 128;
      qa_compat_qc_words_set_int(fields, b->offsets[F_CLASSNAME], (double)qa_compat_qc_strings_set_engine(&vm->strings, engine_name, cn, cap));
      free(row->classname); row->classname = strdup(cn);
    }
  }
  if (requested[F_SOLID]) qa_compat_qc_words_set_float(fields, b->offsets[F_SOLID], (double)b->host.solid(b->host.ctx, row->actor.id));
}
/* TS catch: forget the provisional row and return its slot. */
typedef struct { qa_compat_qc_borrowed_actors *b; int64_t slot; qa_source_time now; } BorrowUnwind;
static void unwind_borrow(void *arg) {
  BorrowUnwind *u = arg; qa_compat_qc_borrowed_row *r = find_slot(u->b, u->slot);
  if (r) remove_row(u->b, r);
  u->b->host.slots_options.storage.clear_freed(u->b->host.slots_options.storage.ctx, u->slot, u->now);
}
int64_t qa_compat_qc_borrowed_actors_reference(qa_compat_qc_borrowed_actors *b, qa_actor_id actor) {
  qa_owned_actor owned;
  if (!qa_session_actor_registry_resolve_owned(b->host.actors, actor, &owned)) qa_compat_qc_program_error("progs.dat", "Cannot borrow a retired canonical actor");
  qa_actor_source_result src = qa_session_actor_registry_source_of(b->host.actors, actor);
  if (src.has_value && strcmp(src.provider, b->host.slots_options.provider) == 0) qa_compat_qc_program_error("progs.dat", "Source actors must use their original QC row");
  qa_compat_qc_borrowed_row *existing = find_actor(b, owned);
  if (existing) return qa_compat_qc_entity_memory_reference(b->host.machine(b->host.ctx)->entities, existing->slot);
  qa_source_slot_storage *storage = &b->host.slots_options.storage;
  qa_actor_lifetime_policy *lifetime = &b->host.slots_options.lifetime;
  qa_source_time now = b->host.slots_options.now(b->host.slots_options.ctx);
  int64_t count = (int64_t)storage->count(storage->ctx);
  int64_t selected = count;
  for (int64_t slot = (int64_t)lifetime->first_dynamic_slot; slot < count; slot++) {
    qa_source_slot_state state = storage->read(storage->ctx, slot);
    if (state.free && (strcmp(lifetime->reuse, "immediate") == 0 || lifetime->reusable_after(lifetime->ctx, state.freed_at, now))) { selected = slot; break; }
  }
  if (selected >= (int64_t)b->host.slots_options.capacity) qa_compat_qc_program_error("progs.dat", "No free QC row for a borrowed actor");
  qa_owned_actor at; if (qa_source_actor_slots_at(b->host.slots, selected, &at) || find_slot(b, selected)) qa_compat_qc_program_error("progs.dat", "Borrowed QC row collides with a current source actor");
  if (selected == count) storage->set_count(storage->ctx, (double)(count + 1));
  storage->initialize(storage->ctx, selected, owned);
  if (b->row_count == b->row_capacity) { size_t c = b->row_capacity ? b->row_capacity * 2 : 8; void *p = realloc(b->rows, c * sizeof *b->rows); if (!p) qa_compat_qc_program_error("progs.dat", "borrowed row allocation failed"); b->rows = p; b->row_capacity = c; }
  b->rows[b->row_count] = (qa_compat_qc_borrowed_row){owned, selected, NULL};
  qa_compat_qc_borrowed_row *row = &b->rows[b->row_count++];
  bool requested[14] = {0}; for (int i = 0; i < 8; i++) requested[i] = true;
  requested[F_HEALTH] = requested[F_TAKEDAMAGE] = requested[F_CLASSNAME] = requested[F_SOLID] = true;
  BorrowUnwind unwind = {b, selected, now}; qa_cleanup scope; qa_cleanup_push(&scope, unwind_borrow, &unwind);
  refresh(b, row, requested);
  qa_cleanup_pop(&scope);
  return qa_compat_qc_entity_memory_reference(b->host.machine(b->host.ctx)->entities, selected);
}
void qa_compat_qc_borrowed_actors_access(qa_compat_qc_borrowed_actors *b, int64_t reference, int64_t word, int64_t words, const char *kind) {
  qa_compat_qc_machine *vm = b->host.machine(b->host.ctx);
  qa_compat_qc_borrowed_row *row = find_slot(b, qa_compat_qc_entity_memory_slot(vm->entities, reference));
  if (!row) return;
  qa_session_actor_registry_assert_owned(b->host.actors, row->actor);
  field_offsets(b);
  if (strcmp(kind, "write") == 0) {
    for (int64_t cur = word; cur < word + words; cur++) {
      int64_t f = (cur >= 0 && cur < (int64_t)b->fields_by_word_count) ? b->fields_by_word[cur] : -1;
      if (f >= 0 && f != F_VELOCITY && f != F_CHAIN && f != F_INVSND) qa_compat_qc_program_error("progs.dat", "Original QC cannot mutate borrowed field %s outside its source owner", names[f]);
    }
    return;
  }
  bool requested[14] = {0}; bool any = false;
  for (int64_t cur = word; cur < word + words; cur++) {
    int64_t f = (cur >= 0 && cur < (int64_t)b->fields_by_word_count) ? b->fields_by_word[cur] : -1;
    if (f >= 0 && !requested[f]) { requested[f] = true; any = true; }
  }
  if (any) refresh(b, row, requested);
}
void qa_compat_qc_borrowed_actors_observe_store(qa_compat_qc_borrowed_actors *b, int64_t reference, int64_t word,
    const uint8_t *after, size_t after_length) {
  qa_compat_qc_machine *vm = b->host.machine(b->host.ctx);
  qa_compat_qc_borrowed_row *row = find_slot(b, qa_compat_qc_entity_memory_slot(vm->entities, reference));
  /* fieldsByWord is filled by fieldOffsets(); before that the TS Map lookup misses. */
  if (!row || !b->offsets_ready || word < 0 || (size_t)word >= b->fields_by_word_count
      || b->fields_by_word[word] != F_VELOCITY) return;
  qa_body_state_result body = b->host.bodies.read(b->host.bodies.ctx, row->actor.id);
  if (!body.has_value) return;
  int64_t index = word - b->offsets[F_VELOCITY];
  qa_vec3 velocity = body.value.velocity;
  for (size_t w = 0; w < after_length / 4; w++) {
    int64_t axis = index + (int64_t)w;
    if (axis < 0 || axis > 2) continue;
    float value; memcpy(&value, after + w * 4, 4);
    if (axis == 0) velocity.x = value; else if (axis == 1) velocity.y = value; else velocity.z = value;
  }
  qa_body_state next = body.value; next.velocity = velocity;
  b->host.bodies.write(b->host.bodies.ctx, row->actor, next);
}
size_t qa_compat_qc_borrowed_actors_checkpoint(qa_compat_qc_borrowed_actors *b, qa_compat_qc_borrowed_checkpoint *out, size_t cap) {
  for (size_t i = 0; i < b->row_count && i < cap; i++) out[i] = (qa_compat_qc_borrowed_checkpoint){qa_saved_actor_id_of(b->rows[i].actor.id), b->rows[i].slot};
  return b->row_count;
}
void qa_compat_qc_borrowed_actors_restore(qa_compat_qc_borrowed_actors *b, const qa_save_reader *reader) {
  for (size_t i = 0; i < b->row_count; i++) free(b->rows[i].classname);
  b->row_count = 0;
  if (qa_save_reader_is_undefined(reader)) return;
  size_t n = qa_save_reader_list_count(reader);
  for (size_t i = 0; i < n; i++) {
    qa_save_reader entry, af, sf;
    qa_save_reader_item(reader, i, &entry);
    qa_save_reader_field(&entry, "actor", &af);
    qa_saved_actor_id saved = qa_read_saved_actor(&af); qa_save_reader_destroy(&af);
    qa_owned_actor actor; bool has_actor = qa_session_actor_registry_resolve_saved(b->host.actors, saved, &actor);
    qa_save_reader_field(&entry, "slot", &sf);
    int64_t slot = (int64_t)qa_save_reader_integer_min(&sf, b->host.slots_options.lifetime.first_dynamic_slot);
    qa_save_reader_destroy(&sf);
    qa_owned_actor at;
    qa_actor_source_result src = has_actor ? qa_session_actor_registry_source_of(b->host.actors, actor.id) : (qa_actor_source_result){0};
    if (!has_actor || slot >= (int64_t)b->host.slots_options.storage.count(b->host.slots_options.storage.ctx)
        || qa_source_actor_slots_at(b->host.slots, slot, &at) || b->host.slots_options.storage.read(b->host.slots_options.storage.ctx, slot).free
        || find_actor(b, actor) || find_slot(b, slot) || (src.has_value && strcmp(src.provider, b->host.slots_options.provider) == 0)) {
      qa_save_reader_fail(&entry, "invalid borrowed QC actor mapping");
    }
    if (b->row_count == b->row_capacity) { size_t c = b->row_capacity ? b->row_capacity * 2 : 8; void *p = realloc(b->rows, c * sizeof *b->rows); if (!p) qa_save_reader_fail(&entry, "borrowed row allocation failed"); b->rows = p; b->row_capacity = c; }
    b->rows[b->row_count++] = (qa_compat_qc_borrowed_row){actor, slot, NULL};
    qa_save_reader_destroy(&entry);
  }
}
