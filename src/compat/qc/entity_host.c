/* Port of src/compat/qc/entity-host.ts. GPL-2.0-or-later. */
#include "qa/compat/qc/entity_host.h"
#include <string.h>
#include "qa/foundation/support.h"
typedef qa_compat_qc_source_slot_storage_state S;
static int32_t rd_i32(uint8_t *b, size_t o) { int32_t v; memcpy(&v, b + o, 4); return v; }
static void wr_i32(uint8_t *b, size_t o, int32_t v) { memcpy(b + o, &v, 4); }
static float rd_f32(uint8_t *b, size_t o) { float v; memcpy(&v, b + o, 4); return v; }
static void wr_f32(uint8_t *b, size_t o, float v) { memcpy(b + o, &v, 4); }
static double s_count(void *ctx) { return (double)((S *)ctx)->entities->count; }
static void s_set_count(void *ctx, double c) { qa_compat_qc_entity_memory_set_count(((S *)ctx)->entities, (int64_t)c); }
static qa_source_slot_state s_read(void *ctx, int64_t slot) {
  S *s = ctx; size_t ref = (size_t)qa_compat_qc_entity_memory_reference(s->entities, slot);
  return (qa_source_slot_state){rd_i32(s->entities->bytes, ref + (size_t)s->metadata.free_offset_bytes) != 0,
    {QA_SOURCE_TIME_SECONDS, rd_f32(s->entities->bytes, ref + (size_t)s->metadata.free_time_offset_bytes)}};
}
static void s_initialize(void *ctx, int64_t slot, qa_owned_actor actor) {
  (void)actor; S *s = ctx;
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->entities, slot);
  memset(w->bytes, 0, w->byte_length);
  /* TS also zeroes the whole edict via entities.at(slot).bytes.fill(0) on the field view only; free word is in prefix. */
  wr_i32(s->entities->bytes, (size_t)qa_compat_qc_entity_memory_reference(s->entities, slot) + (size_t)s->metadata.free_offset_bytes, 0);
}
static void s_clear_freed(void *ctx, int64_t slot, qa_source_time now) {
  S *s = ctx;
  if (now.kind != QA_SOURCE_TIME_SECONDS) qa_compat_qc_program_error("progs.dat", "QC source free time must use seconds");
  size_t ref = (size_t)qa_compat_qc_entity_memory_reference(s->entities, slot);
  wr_i32(s->entities->bytes, ref + (size_t)s->metadata.free_offset_bytes, 1);
  qa_compat_qc_words *w = qa_compat_qc_entity_memory_at(s->entities, slot);
  for (int i = 0; i < 7; i++) qa_compat_qc_words_set_int(w, s->cleared[i], 0);
  qa_compat_qc_words_set_vector(w, s->origin, (qa_vec3){0,0,0});
  qa_compat_qc_words_set_vector(w, s->angles, (qa_vec3){0,0,0});
  qa_compat_qc_words_set_float(w, s->nextthink, -1);
  wr_f32(s->entities->bytes, ref + (size_t)s->metadata.free_time_offset_bytes, (float)now.value);
}
static bool s_can_free(void *ctx, int64_t slot) { (void)ctx; (void)slot; return true; }
static int64_t foff(qa_compat_qc_program *p, const char *n) {
  const qa_compat_qc_definition *d = qa_compat_qc_program_field_named(p, n);
  if (!d) qa_compat_qc_program_error("progs.dat", "missing entity field %s", n);
  return d->offset;
}
qa_source_slot_storage qa_compat_qc_create_source_slot_storage(qa_compat_qc_program *p, qa_compat_qc_entity_memory *e, qa_compat_qc_edict_metadata_layout md, S *st) {
  int64_t offs[2] = {md.free_offset_bytes, md.free_time_offset_bytes};
  for (int i = 0; i < 2; i++) if (offs[i] < 0 || offs[i] + 4 > e->layout.variables_offset_bytes || offs[i] % 4) qa_range_error("QC edict metadata must reside in the source prefix");
  st->program = p; st->entities = e; st->metadata = md;
  static const char *const cleared[7] = {"model","takedamage","modelindex","colormap","skin","frame","solid"};
  for (int i = 0; i < 7; i++) st->cleared[i] = foff(p, cleared[i]);
  st->origin = foff(p, "origin"); st->angles = foff(p, "angles"); st->nextthink = foff(p, "nextthink");
  return (qa_source_slot_storage){st, s_count, s_set_count, s_read, s_initialize, s_clear_freed, s_can_free};
}
static void b_spawn(void *ctx, qa_compat_qc_machine *m) {
  qa_compat_qc_actor_bindings_state *s = ctx;
  if (m->entities != s->entities) { qa_compat_qc_machine_fail(m, "actor builtin belongs to another machine"); }
  qa_owned_actor actor = qa_source_actor_slots_allocate(s->slots, "quakec:dynamic");
  qa_actor_source_result src = qa_session_actor_registry_source_of(s->actors, actor.id);
  if (!src.has_value || strcmp(src.provider, s->slots_options.provider) != 0) { qa_compat_qc_machine_fail(m, "allocated actor has no QC source slot"); }
  qa_compat_qc_machine_return_int(m, (double)qa_compat_qc_entity_memory_reference(m->entities, src.slot));
}
static void b_remove(void *ctx, qa_compat_qc_machine *m) {
  qa_compat_qc_actor_bindings_state *s = ctx;
  if (m->entities != s->entities) { qa_compat_qc_machine_fail(m, "actor builtin belongs to another machine"); }
  int64_t slot = qa_compat_qc_entity_memory_slot(m->entities, qa_compat_qc_machine_arg_int(m, 0));
  qa_owned_actor actor;
  if (qa_source_actor_slots_at(s->slots, slot, &actor)) qa_source_actor_slots_free(s->slots, actor);
  else s->slots_options.storage.clear_freed(s->slots_options.storage.ctx, slot, s->slots_options.now(s->slots_options.ctx));
}
qa_compat_qc_actor_bindings qa_compat_qc_create_actor_bindings(qa_compat_qc_entity_memory *e, qa_session_actor_registry *a, qa_source_actor_slots *slots, qa_source_actor_slots_options so, qa_compat_qc_actor_bindings_state *st) {
  if ((int64_t)so.capacity != e->capacity) qa_compat_qc_program_error("progs.dat", "actor slot capacity disagrees with QC storage");
  st->entities = e; st->actors = a; st->slots = slots; st->slots_options = so;
  qa_compat_qc_actor_bindings r; r.state = st; r.host_count = 2;
  r.host_entries[0] = (qa_compat_qc_host_binding){"spawn", {st, b_spawn}};
  r.host_entries[1] = (qa_compat_qc_host_binding){"remove", {st, b_remove}};
  return r;
}
bool qa_compat_qc_actor_bindings_is_free_entity(qa_compat_qc_actor_bindings_state *s, int64_t slot) {
  return s->slots_options.storage.read(s->slots_options.storage.ctx, slot).free;
}
