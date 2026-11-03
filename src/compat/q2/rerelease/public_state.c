/* Port of src/compat/q2/rerelease/public-state.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/public_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

static double ef(const char *name) { qa_compat_q2_rerelease_layouts_ensure(); return qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_entity_state_layout, name); }
static double cf(const char *name) { qa_compat_q2_rerelease_layouts_ensure(); return qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_client_layout, name); }
static double bf(const char *name) { qa_compat_q2_rerelease_layouts_ensure(); return qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, name); }
struct qa_compat_q2_rerelease_public_edict {
  qa_compat_q2_memory *memory; qa_raw_entity_view record;
  bool has_body; qa_body_state body;
};
typedef struct snapshot_buf { double length; uint8_t *bytes; struct snapshot_buf *next; } snapshot_buf_t;
static snapshot_buf_t *snapshots = NULL;
static const uint8_t *snapshot(qa_compat_q2_memory *memory, qa_guest_address address, double length) {
  (void)memory;
  snapshot_buf_t *b = NULL;
  for (snapshot_buf_t *x = snapshots; x; x = x->next) if (x->length == length) b = x;
  if (!b) { b = calloc(1, sizeof *b); b->length = length; b->bytes = calloc(length ? (size_t)length : 1, 1); b->next = snapshots; snapshots = b; }
  memory->copy_into(memory->ctx, address, b->bytes, (size_t)length, length);
  return b->bytes;
}
static qa_vec3 rvec(const uint8_t *v, size_t o) { return (qa_vec3){qa_compat_q2_view_f32(v, o), qa_compat_q2_view_f32(v, o + 4), qa_compat_q2_view_f32(v, o + 8)}; }
static bool same_float(double a, double b) { /* Object.is on float32-read doubles */ return a == b || (a != a && b != b); }
static qa_vec3 retain(const uint8_t *view, size_t off, bool has_prev, qa_vec3 prev) {
  qa_vec3 next = rvec(view, off);
  return has_prev && same_float(prev.x, next.x) && same_float(prev.y, next.y) && same_float(prev.z, next.z) ? prev : next;
}
qa_compat_q2_rerelease_public_edict *qa_compat_q2_rerelease_public_edict_create(qa_compat_q2_memory *memory, qa_raw_entity_view record) {
  qa_compat_q2_rerelease_public_edict *e = calloc(1, sizeof *e); if (!e) return NULL;
  qa_compat_q2_rerelease_layouts_ensure();
  memory->check(memory->ctx, record.address, qa_compat_q2_rerelease_edict_layout.byte_length, "read");
  e->memory = memory; e->record = record; return e;
}
void qa_compat_q2_rerelease_public_edict_destroy(qa_compat_q2_rerelease_public_edict *e) { free(e); }
qa_compat_q2_memory *qa_compat_q2_rerelease_public_edict_get_memory(const qa_compat_q2_rerelease_public_edict *e) { return e->memory; }
qa_raw_entity_view qa_compat_q2_rerelease_public_edict_get_record(const qa_compat_q2_rerelease_public_edict *e) { return e->record; }
qa_guest_address qa_compat_q2_rerelease_public_edict_address(const qa_compat_q2_rerelease_public_edict *e, const char *name) {
  return qa_compat_q2_mem_offset(e->memory, e->record.address, (int64_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_edict_layout, name));
}
double qa_compat_q2_rerelease_public_edict_int(const qa_compat_q2_rerelease_public_edict *e, const char *name) { return qa_compat_q2_mem_read_i32(e->memory, qa_compat_q2_rerelease_public_edict_address(e, name)); }
double qa_compat_q2_rerelease_public_edict_uint(const qa_compat_q2_rerelease_public_edict *e, const char *name) { return qa_compat_q2_mem_read_u32(e->memory, qa_compat_q2_rerelease_public_edict_address(e, name)); }
double qa_compat_q2_rerelease_public_edict_byte(const qa_compat_q2_rerelease_public_edict *e, const char *name) { return qa_compat_q2_mem_read_u8(e->memory, qa_compat_q2_rerelease_public_edict_address(e, name)); }
double qa_compat_q2_rerelease_public_edict_float(const qa_compat_q2_rerelease_public_edict *e, const char *name) { return qa_compat_q2_mem_read_f32(e->memory, qa_compat_q2_rerelease_public_edict_address(e, name)); }
bool qa_compat_q2_rerelease_public_edict_pointer(const qa_compat_q2_rerelease_public_edict *e, const char *name, qa_guest_address *out) {
  return qa_compat_q2_mem_read_pointer(e->memory, qa_compat_q2_rerelease_public_edict_address(e, name), out);
}
qa_vec3 qa_compat_q2_rerelease_public_edict_vector(const qa_compat_q2_rerelease_public_edict *e, const char *name) {
  return qa_compat_q2_mem_read_f32_vector(e->memory, qa_compat_q2_rerelease_public_edict_address(e, name));
}
qa_body_state qa_compat_q2_rerelease_public_edict_body(qa_compat_q2_rerelease_public_edict *e, const qa_vec3 *velocity, const qa_vec3 *origin) {
  double max_off = bf("maxs");
  const uint8_t *view = snapshot(e->memory, e->record.address, max_off + 12);
  bool has_prev = e->has_body;
  qa_vec3 next_origin = origin ? *origin : retain(view, (size_t)ef("origin"), has_prev, has_prev ? e->body.origin : (qa_vec3){0, 0, 0});
  qa_vec3 angles = retain(view, (size_t)ef("angles"), has_prev, has_prev ? e->body.angles : (qa_vec3){0, 0, 0});
  qa_vec3 next_velocity = velocity ? *velocity : retain(view, (size_t)bf("sv.velocity"), has_prev, has_prev ? e->body.velocity : (qa_vec3){0, 0, 0});
  qa_vec3 min = retain(view, (size_t)bf("mins"), has_prev, has_prev ? e->body.bounds.min : (qa_vec3){0, 0, 0});
  qa_vec3 max = retain(view, (size_t)bf("maxs"), has_prev, has_prev ? e->body.bounds.max : (qa_vec3){0, 0, 0});
  if (has_prev && memcmp(&e->body.origin, &next_origin, sizeof next_origin) == 0 && memcmp(&e->body.angles, &angles, sizeof angles) == 0 &&
      memcmp(&e->body.velocity, &next_velocity, sizeof next_velocity) == 0 && memcmp(&e->body.bounds.min, &min, sizeof min) == 0 &&
      memcmp(&e->body.bounds.max, &max, sizeof max) == 0) return e->body;
  qa_body_state state; memset(&state, 0, sizeof state);
  state.origin = next_origin; state.angles = angles; state.velocity = next_velocity;
  state.bounds = (qa_bounds){min, max}; state.ground = NULL;
  e->body = state; e->has_body = true;
  return state;
}
void qa_compat_q2_rerelease_public_edict_set_vector(const qa_compat_q2_rerelease_public_edict *e, const char *name, qa_vec3 value) {
  qa_guest_address at = qa_compat_q2_rerelease_public_edict_address(e, name);
  qa_compat_q2_mem_write_f32(e->memory, at, value.x);
  qa_compat_q2_mem_write_f32(e->memory, qa_compat_q2_mem_offset(e->memory, at, 4), value.y);
  qa_compat_q2_mem_write_f32(e->memory, qa_compat_q2_mem_offset(e->memory, at, 8), value.z);
}
qa_guest_address qa_compat_q2_rerelease_public_edict_client(const qa_compat_q2_rerelease_public_edict *e) {
  qa_guest_address address;
  if (!qa_compat_q2_rerelease_public_edict_pointer(e, "client", &address)) qa_range_error("API2023 source slot has no public client prefix");
  e->memory->check(e->memory->ctx, address, qa_compat_q2_rerelease_client_layout.byte_length, "read");
  return address;
}
qa_q2_rerelease_player_state qa_compat_q2_rerelease_public_edict_player_state(const qa_compat_q2_rerelease_public_edict *e) {
  return qa_compat_q2_read_rerelease_player_state(e->memory, qa_compat_q2_rerelease_public_edict_client(e));
}
qa_vec3 qa_compat_q2_rerelease_public_edict_player_velocity(const qa_compat_q2_rerelease_public_edict *e) {
  const qa_compat_q2_memory * m;
  qa_guest_address a;
  int64_t d;
  return qa_compat_q2_mem_read_f32_vector(e->memory, (m = e->memory, a = qa_compat_q2_rerelease_public_edict_client(e), d = (int64_t)cf("ps.pmove.velocity"), qa_compat_q2_mem_offset(m, a, d)));
}
double qa_compat_q2_rerelease_public_edict_player_movement_flags(const qa_compat_q2_rerelease_public_edict *e) {
  const qa_compat_q2_memory * m;
  qa_guest_address a;
  int64_t d;
  return qa_compat_q2_mem_read_u16(e->memory, (m = e->memory, a = qa_compat_q2_rerelease_public_edict_client(e), d = (int64_t)cf("ps.pmove.pm_flags"), qa_compat_q2_mem_offset(m, a, d)));
}
qa_compat_q2_rerelease_player_view qa_compat_q2_rerelease_public_edict_player_view(const qa_compat_q2_rerelease_public_edict *e) {
  double flags = cf("ps.pmove.pm_flags"), height = cf("ps.pmove.viewheight"), offset = cf("ps.viewoffset");
  const uint8_t *view = snapshot(e->memory, qa_compat_q2_mem_offset(e->memory, qa_compat_q2_rerelease_public_edict_client(e), (int64_t)flags), offset + 12 - flags);
  return (qa_compat_q2_rerelease_player_view){rvec(view, (size_t)(offset - flags)), (int8_t)view[(size_t)(height - flags)], qa_compat_q2_view_u16(view, 0)};
}
qa_compat_q2_rerelease_model_state qa_compat_q2_rerelease_public_edict_model_state(const qa_compat_q2_rerelease_public_edict *e) {
  double model = ef("modelindex");
  qa_compat_q2_memory * memory2 = e->memory;
  qa_guest_address address = qa_compat_q2_rerelease_public_edict_address(e, "s.modelindex");
  double length = ef("skinnum") + 4 - model;
  const uint8_t *view = snapshot(memory2, address, length);
  qa_compat_q2_rerelease_model_state s;
  s.model_indexes[0] = qa_compat_q2_view_i32(view, 0);
  s.model_indexes[1] = qa_compat_q2_view_i32(view, (size_t)(ef("modelindex2") - model));
  s.model_indexes[2] = qa_compat_q2_view_i32(view, (size_t)(ef("modelindex3") - model));
  s.model_indexes[3] = qa_compat_q2_view_i32(view, (size_t)(ef("modelindex4") - model));
  s.skin = qa_compat_q2_view_i32(view, (size_t)(ef("skinnum") - model));
  return s;
}
double qa_compat_q2_rerelease_public_edict_ping(const qa_compat_q2_rerelease_public_edict *e) {
  const qa_compat_q2_memory * m;
  qa_guest_address a;
  int64_t d;
  return qa_compat_q2_mem_read_i32(e->memory, (m = e->memory, a = qa_compat_q2_rerelease_public_edict_client(e), d = (int64_t)cf("ping"), qa_compat_q2_mem_offset(m, a, d)));
}
void qa_compat_q2_rerelease_public_edict_set_ping(const qa_compat_q2_rerelease_public_edict *e, double value) {
  const qa_compat_q2_memory * m;
  qa_guest_address a;
  int64_t d;
  /* Number.isInteger first: NaN and infinities must not reach an int64 cast. */
  if (!(value == trunc(value)) || value < 0 || value > 2147483647.0) qa_range_error("Invalid API2023 ping");
  qa_compat_q2_mem_write_i32(e->memory, (m = e->memory, a = qa_compat_q2_rerelease_public_edict_client(e), d = (int64_t)cf("ping"), qa_compat_q2_mem_offset(m, a, d)), qa_js_to_int32(value));
}
qa_q2_rerelease_entity_state qa_compat_q2_rerelease_public_edict_state(const qa_compat_q2_rerelease_public_edict *e) {
  const uint8_t *view = snapshot(e->memory, qa_compat_q2_rerelease_public_edict_address(e, "s.number"), qa_compat_q2_rerelease_entity_state_layout.byte_length);
  qa_q2_rerelease_entity_state s; memset(&s, 0, sizeof s);
  s.base.number = qa_compat_q2_view_u32(view, (size_t)ef("number"));
  s.base.origin = rvec(view, (size_t)ef("origin")); s.base.angles = rvec(view, (size_t)ef("angles")); s.base.old_origin = rvec(view, (size_t)ef("old_origin"));
  s.base.model_indexes[0] = qa_compat_q2_view_i32(view, (size_t)ef("modelindex"));
  s.base.model_indexes[1] = qa_compat_q2_view_i32(view, (size_t)ef("modelindex2"));
  s.base.model_indexes[2] = qa_compat_q2_view_i32(view, (size_t)ef("modelindex3"));
  s.base.model_indexes[3] = qa_compat_q2_view_i32(view, (size_t)ef("modelindex4"));
  s.base.frame = qa_compat_q2_view_i32(view, (size_t)ef("frame")); s.base.skin = qa_compat_q2_view_i32(view, (size_t)ef("skinnum"));
  uint64_t effects = 0; for (int i = 7; i >= 0; i--) effects = (effects << 8) | view[(size_t)ef("effects") + (size_t)i];
  s.effects = effects;
  s.base.render_effects = qa_compat_q2_view_u32(view, (size_t)ef("renderfx"));
  s.base.solid = qa_compat_q2_view_u32(view, (size_t)ef("solid")); s.base.sound = qa_compat_q2_view_i32(view, (size_t)ef("sound"));
  s.base.event = view[(size_t)ef("event")];
  s.alpha = qa_compat_q2_view_f32(view, (size_t)ef("alpha")); s.scale = qa_compat_q2_view_f32(view, (size_t)ef("scale"));
  s.instance_bits = view[(size_t)ef("instance_bits")];
  s.loop_volume = qa_compat_q2_view_f32(view, (size_t)ef("loop_volume")); s.loop_attenuation = qa_compat_q2_view_f32(view, (size_t)ef("loop_attenuation"));
  s.owner = qa_compat_q2_view_i32(view, (size_t)ef("owner")); s.old_frame = qa_compat_q2_view_i32(view, (size_t)ef("old_frame"));
  return s;
}
