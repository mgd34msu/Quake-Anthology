/* Port of src/compat/q2/classic/records.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/records.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) qa_range_error("Classic Q2 records out of memory"); return p; }
char *qa_compat_q2_read_classic_string(const qa_compat_q2_memory *memory, bool has, qa_guest_address address, double maximum) {
  if (!has) { char *s = xmalloc(1); s[0] = 0; return s; }
  size_t max = maximum < 0 ? 0 : (size_t)maximum;
  char *text = xmalloc(max + 1);
  for (size_t i = 0; i < max; i++) {
    uint8_t byte = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, address, (int64_t)i));
    if (byte == 0) { text[i] = 0; return text; }
    text[i] = (char)byte;
  }
  free(text);
  qa_range_error("Unterminated API 3 string at 0x%llx", (unsigned long long)address.byte_offset);
}
void qa_compat_q2_write_classic_string(const qa_compat_q2_memory *memory, qa_guest_address address, const char *text, double capacity) {
  size_t len = strlen(text);
  if ((double)(len + 1) > capacity) qa_range_error("API 3 string exceeds its guest allocation");
  memory->write(memory->ctx, address, (const uint8_t *)text, len + 1);
}
qa_guest_address qa_compat_q2_allocate_classic_string(const qa_compat_q2_memory *memory, const char *text) {
  qa_guest_address a = qa_compat_q2_allocate_native_memory(memory, (double)strlen(text) + 1, "API 3 string");
  qa_compat_q2_write_classic_string(memory, a, text, (double)strlen(text) + 1);
  return a;
}
double qa_compat_q2_classic_string_allocation_bytes(const char *text) { return qa_compat_q2_native_allocation_bytes((double)strlen(text) + 1); }
qa_vec3 qa_compat_q2_read_classic_vector(const qa_compat_q2_memory *m, qa_guest_address a) { return qa_compat_q2_mem_read_f32_vector(m, a); }
void qa_compat_q2_write_classic_vector(const qa_compat_q2_memory *m, qa_guest_address a, qa_vec3 v) {
  uint8_t b[12]; qa_compat_q2_view_set_f32(b, 0, v.x); qa_compat_q2_view_set_f32(b, 4, v.y); qa_compat_q2_view_set_f32(b, 8, v.z);
  m->write(m->ctx, a, b, 12);
}

struct qa_compat_q2_classic_edicts {
  qa_compat_q2_memory *memory; qa_guest_address exports; qa_session_actor_registry *actors; qa_provider_id provider;
  void *bind_ctx; void (*bind)(void *, qa_raw_entity_view, qa_owned_actor);
  bool has_projection; qa_compat_q2_classic_actor_projection projection;
  qa_guest_address f_base, f_stride, f_count, f_capacity;
  bool has_descriptor; qa_compat_q2_classic_edict_descriptor descriptor;
  double retained[256]; size_t retained_count; double retired[256]; size_t retired_count;
};
static bool slot_has(const double *list, size_t n, double slot) { for (size_t i = 0; i < n; i++) if (list[i] == slot) return true; return false; }
static void slot_add(double *list, size_t *n, double slot) { if (!slot_has(list, *n, slot) && *n < 256) list[(*n)++] = slot; }
static void slot_del(double *list, size_t *n, double slot) { for (size_t i = 0; i < *n; i++) if (list[i] == slot) list[i] = list[--(*n)]; }

qa_compat_q2_classic_edicts *qa_compat_q2_classic_edicts_create(
    qa_compat_q2_memory *memory, qa_guest_address exports, qa_session_actor_registry *actors,
    qa_provider_id provider, void *bind_ctx, void (*bind)(void *, qa_raw_entity_view, qa_owned_actor),
    const qa_compat_q2_classic_actor_projection *projection) {
  qa_compat_q2_classic_edicts *e = calloc(1, sizeof *e); if (!e) return NULL;
  e->memory = memory; e->exports = exports; e->actors = actors; e->provider = provider;
  e->bind_ctx = bind_ctx; e->bind = bind;
  if (projection) { e->has_projection = true; e->projection = *projection; }
  memory->check(memory->ctx, exports, QA_COMPAT_Q2_CLASSIC_EXPORT_BYTES, "read");
  if (qa_compat_q2_mem_read_i32(memory, exports) != 3) qa_range_error("GetGameAPI returned an API version other than 3");
  e->f_base = qa_compat_q2_mem_offset(memory, exports, 64); e->f_stride = qa_compat_q2_mem_offset(memory, exports, 68);
  e->f_count = qa_compat_q2_mem_offset(memory, exports, 72); e->f_capacity = qa_compat_q2_mem_offset(memory, exports, 76);
  return e;
}
void qa_compat_q2_classic_edicts_destroy(qa_compat_q2_classic_edicts *e) { free(e); }
void qa_compat_q2_classic_edicts_retire_input_client(qa_compat_q2_classic_edicts *e, double slot) { slot_add(e->retired, &e->retired_count, slot); }
void qa_compat_q2_classic_edicts_finish_input_retirement(qa_compat_q2_classic_edicts *e, double slot) { slot_del(e->retired, &e->retired_count, slot); }
qa_compat_q2_classic_edict_descriptor qa_compat_q2_classic_edicts_descriptor(qa_compat_q2_classic_edicts *e) {
  qa_guest_address base; bool has = qa_compat_q2_mem_read_pointer(e->memory, e->f_base, &base);
  double stride = qa_compat_q2_mem_read_i32(e->memory, e->f_stride), count = qa_compat_q2_mem_read_i32(e->memory, e->f_count),
         capacity = qa_compat_q2_mem_read_i32(e->memory, e->f_capacity);
  if (!has) qa_range_error("API 3 edicts are not allocated; Init has not completed");
  if (stride < QA_COMPAT_Q2_CLASSIC_EDICT_BYTES || (int64_t)stride % 4 != 0 || count < 0 || capacity < count || capacity > 65536)
    qa_range_error("Invalid source API 3 edict descriptor");
  e->memory->check(e->memory->ctx, base, stride * capacity, "read");
  if (e->has_descriptor && e->descriptor.base.byte_offset == base.byte_offset && e->descriptor.stride == stride &&
      e->descriptor.count == count && e->descriptor.capacity == capacity) return e->descriptor;
  e->descriptor = (qa_compat_q2_classic_edict_descriptor){base, stride, count, capacity}; e->has_descriptor = true;
  return e->descriptor;
}
static qa_actor_id_result current_actor_cb(void *ctx) {
  /* ctx is a heap cell {edicts*, slot} allocated per record; the cell
   * is leaked with the borrowed record (TS closure lifetime). */
  void **cell = ctx; qa_compat_q2_classic_edicts *e = cell[0]; double slot = *(double *)cell[1];
  qa_actor_id_result r; memset(&r, 0, sizeof r);
  qa_raw_entity_view rec = qa_compat_q2_classic_edicts_at(e, slot);
  qa_owned_actor owned;
  if (e->has_projection) { if (e->projection.project(e->projection.ctx, rec, &owned)) { r.has_value = true; r.value = owned.id; } return r; }
  if (qa_session_actor_registry_at_source(e->actors, e->provider, (int64_t)slot, &owned)) { r.has_value = true; r.value = owned.id; }
  return r;
}
static qa_raw_entity_view at_desc(qa_compat_q2_classic_edicts *e, double slot, qa_compat_q2_classic_edict_descriptor d) {
  if (slot != trunc(slot) || slot < 0 || slot >= d.count) qa_range_error("API 3 edict slot exceeds num_edicts");
  qa_guest_address address = qa_compat_q2_mem_offset(e->memory, d.base, (int64_t)(slot * d.stride));
  void **cell = xmalloc(2 * sizeof(void *)); double *sp = xmalloc(sizeof(double)); *sp = slot; cell[0] = e; cell[1] = sp;
  qa_raw_entity_view rec; memset(&rec, 0, sizeof rec);
  rec.ctx = cell; rec.module = e->memory->module; rec.slot = slot; rec.address = address; rec.stride_bytes = d.stride;
  rec.public_layout = qa_compat_q2_classic_edict_layout;
  rec.bytes.bytes = e->memory->borrow(e->memory->ctx, address, d.stride); rec.bytes.length = (size_t)d.stride;
  rec.current_actor = current_actor_cb;
  return rec;
}
qa_raw_entity_view qa_compat_q2_classic_edicts_at(qa_compat_q2_classic_edicts *e, double slot) {
  return at_desc(e, slot, qa_compat_q2_classic_edicts_descriptor(e));
}
qa_raw_entity_view qa_compat_q2_classic_edicts_from_pointer(qa_compat_q2_classic_edicts *e, qa_guest_address address) {
  if (address.address_space != e->memory->address_space) qa_range_error("Foreign guest edict address space");
  qa_compat_q2_classic_edict_descriptor d = qa_compat_q2_classic_edicts_descriptor(e);
  int64_t diff = address.byte_offset - d.base.byte_offset;
  if (diff < 0 || diff % (int64_t)d.stride != 0) qa_range_error("Pointer does not identify the start of an API 3 edict");
  return at_desc(e, (double)(diff / (int64_t)d.stride), d);
}
bool qa_compat_q2_classic_edicts_current(qa_compat_q2_classic_edicts *e, qa_raw_entity_view record, qa_owned_actor *out) {
  if (slot_has(e->retired, e->retired_count, record.slot)) return false;
  qa_raw_entity_view current = qa_compat_q2_classic_edicts_at(e, record.slot);
  if (current.address.byte_offset != record.address.byte_offset || current.address.address_space != record.address.address_space) return false;
  if (qa_compat_q2_view_i32((const uint8_t *)current.bytes.bytes, 88) == 0 && !slot_has(e->retained, e->retained_count, record.slot)) return false;
  return qa_session_actor_registry_at_source(e->actors, e->provider, (int64_t)record.slot, out);
}
bool qa_compat_q2_classic_edicts_resolves(qa_compat_q2_classic_edicts *e, qa_actor_id actor) {
  if (e->has_projection) { (void)e->projection.address(e->projection.ctx, actor); return true; }
  qa_actor_source_result s = qa_session_actor_registry_source_of(e->actors, actor);
  return s.has_value && s.provider && e->provider && strcmp(s.provider, e->provider) == 0;
}
qa_guest_address qa_compat_q2_classic_edicts_pointer(qa_compat_q2_classic_edicts *e, qa_actor_id actor) {
  if (e->has_projection) return e->projection.address(e->projection.ctx, actor);
  qa_actor_source_result s = qa_session_actor_registry_source_of(e->actors, actor);
  if (!s.has_value || !s.provider || !e->provider || strcmp(s.provider, e->provider) != 0)
    qa_range_error("Foreign actor requires an explicit native semantic edict adapter");
  return qa_compat_q2_classic_edicts_at(e, (double)s.slot).address;
}
static bool observe_record(qa_compat_q2_classic_edicts *e, qa_raw_entity_view record, qa_owned_actor *out) {
  if (slot_has(e->retired, e->retired_count, record.slot)) return false;
  if (e->has_projection) return e->projection.project(e->projection.ctx, record, out);
  qa_owned_actor existing; bool has_existing = qa_session_actor_registry_at_source(e->actors, e->provider, (int64_t)record.slot, &existing);
  if (qa_compat_q2_view_i32((const uint8_t *)record.bytes.bytes, 88) == 0 && !slot_has(e->retained, e->retained_count, record.slot)) {
    if (has_existing) qa_session_actor_registry_release(e->actors, existing);
    return false;
  }
  if (has_existing) { if (out) *out = existing; return true; }
  qa_owned_actor actor = qa_session_actor_registry_allocate_at_source(e->actors, e->provider, (int64_t)record.slot, "q2-native:edict");
  e->bind(e->bind_ctx, record, actor);
  if (out) *out = actor;
  return true;
}
bool qa_compat_q2_classic_edicts_observe(qa_compat_q2_classic_edicts *e, qa_guest_address address, qa_owned_actor *out) {
  return observe_record(e, qa_compat_q2_classic_edicts_from_pointer(e, address), out);
}
qa_owned_actor qa_compat_q2_classic_edicts_retain_client(qa_compat_q2_classic_edicts *e, double slot) {
  if (slot != trunc(slot) || slot < 1) qa_range_error("Invalid retained API 3 client slot");
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(e, slot);
  slot_add(e->retained, &e->retained_count, slot);
  qa_owned_actor actor;
  if (!qa_compat_q2_classic_edicts_observe(e, record.address, &actor)) qa_range_error("Retained source client has no actor");
  return actor;
}
void qa_compat_q2_classic_edicts_release_client(qa_compat_q2_classic_edicts *e, double slot) {
  slot_del(e->retained, &e->retained_count, slot);
  qa_owned_actor ignored; (void)qa_compat_q2_classic_edicts_observe(e, qa_compat_q2_classic_edicts_at(e, slot).address, &ignored);
}
void qa_compat_q2_classic_edicts_reconcile(qa_compat_q2_classic_edicts *e) {
  if (e->has_projection) return;
  qa_compat_q2_classic_edict_descriptor d = qa_compat_q2_classic_edicts_descriptor(e);
  size_t n = 0; const qa_owned_actor *owned = qa_session_actor_registry_owned_by(e->actors, e->provider, &n);
  qa_owned_actor copy[512]; size_t m = n < 512 ? n : 512; if (owned) memcpy(copy, owned, m * sizeof *copy);
  for (size_t i = 0; i < m; i++) {
    qa_actor_source_result s = qa_session_actor_registry_source_of(e->actors, copy[i].id);
    if (s.has_value && (double)s.slot >= d.count) qa_session_actor_registry_release(e->actors, copy[i]);
  }
  for (double slot = 0; slot < d.count; slot++) { qa_owned_actor o; (void)observe_record(e, qa_compat_q2_classic_edicts_at(e, slot), &o); }
}
uint8_t *qa_compat_q2_classic_edicts_client_prefix(qa_compat_q2_classic_edicts *e, double slot) {
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_at(e, slot);
  qa_guest_address client;
  if (!qa_compat_q2_mem_read_pointer(e->memory, qa_compat_q2_mem_offset(e->memory, record.address, 84), &client)) return NULL;
  return e->memory->borrow(e->memory->ctx, client, QA_COMPAT_Q2_CLASSIC_CLIENT_PREFIX_BYTES);
}
void qa_compat_q2_classic_edicts_set_client_ping(qa_compat_q2_classic_edicts *e, double slot, double ping) {
  uint8_t *prefix = qa_compat_q2_classic_edicts_client_prefix(e, slot);
  if (!prefix) qa_range_error("Edict has no API 3 client prefix");
  qa_compat_q2_view_set_i32(prefix, 184, (int32_t)ping);
}
