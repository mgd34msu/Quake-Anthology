/* Port of src/compat/q2/classic/pmove.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/classic/pmove.h"
#include <stdlib.h>
#include <string.h>
#include "qa/movement/q2/classic.h"
#include "qa/movement/q2/dimensions.h"
#include "qa/movement/q2/types.h"

typedef struct ctx {
  qa_compat_q2_classic_guest_host *host; qa_compat_q2_memory *memory; qa_guest_address address, scratch;
  uint8_t *view; qa_q2_classic_pmove *pm; const qa_movement_body_shape *body;
  /* canonical entity cache: pointer offset -> hit */
  struct { int64_t key; qa_q2_movement_entity hit; } canonical[64]; size_t canonical_count;
  qa_q2_surface_info surfaces[16]; char surface_names[16][17]; size_t surface_count;
} ctx_t;
static bool entity_of(ctx_t *c, bool has, qa_guest_address pointer, qa_q2_movement_entity *out) {
  if (!has) return false;
  for (size_t i = 0; i < c->canonical_count; i++) if (c->canonical[i].key == pointer.byte_offset) { *out = c->canonical[i].hit; return true; }
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_guest_host_get_edicts(c->host);
  qa_raw_entity_view record = qa_compat_q2_classic_edicts_from_pointer(edicts, pointer);
  qa_owned_actor actor;
  if (!qa_compat_q2_classic_edicts_observe(edicts, pointer, &actor)) qa_range_error("Pmove callback returned an inactive edict");
  qa_q2_movement_entity result; memset(&result, 0, sizeof result);
  if (record.slot == 0) { result.kind = QA_TRACE_HIT_WORLD; result.model = 0; }
  else { result.kind = QA_TRACE_HIT_ACTOR; result.actor = actor.id; }
  if (c->canonical_count < 64) { c->canonical[c->canonical_count].key = pointer.byte_offset; c->canonical[c->canonical_count].hit = result; c->canonical_count++; }
  *out = result; return true;
}
static bool pointer_of(ctx_t *c, bool has, qa_q2_movement_entity hit, qa_guest_address *out) {
  if (!has) return false;
  qa_compat_q2_classic_edicts *edicts = qa_compat_q2_classic_guest_host_get_edicts(c->host);
  *out = hit.kind == QA_TRACE_HIT_WORLD ? qa_compat_q2_classic_edicts_at(edicts, 0).address
                                        : qa_compat_q2_classic_edicts_pointer(edicts, hit.actor);
  return true;
}
static void vec_short(const uint8_t *view, size_t off, qa_q2_vec3 out) {
  out[0] = qa_compat_q2_view_i16(view, off); out[1] = qa_compat_q2_view_i16(view, off + 2); out[2] = qa_compat_q2_view_i16(view, off + 4);
}
static void vec_float(const uint8_t *view, size_t off, qa_q2_vec3 out) {
  out[0] = qa_compat_q2_view_f32(view, off); out[1] = qa_compat_q2_view_f32(view, off + 4); out[2] = qa_compat_q2_view_f32(view, off + 8);
}
static void put_short(uint8_t *view, size_t off, const qa_q2_vec3 v) {
  qa_compat_q2_view_set_i16(view, off, (int32_t)v[0]); qa_compat_q2_view_set_i16(view, off + 2, (int32_t)v[1]); qa_compat_q2_view_set_i16(view, off + 4, (int32_t)v[2]);
}
static void put_float(uint8_t *view, size_t off, const qa_q2_vec3 v) {
  qa_compat_q2_view_set_f32(view, off, v[0]); qa_compat_q2_view_set_f32(view, off + 4, v[1]); qa_compat_q2_view_set_f32(view, off + 8, v[2]);
}
static qa_guest_address prepare_vector(ctx_t *c, int index, const qa_q2_vec3 v) {
  qa_guest_address loc = qa_compat_q2_mem_offset(c->memory, c->scratch, index * 12);
  qa_compat_q2_mem_write_f32(c->memory, loc, v[0]);
  qa_compat_q2_mem_write_f32(c->memory, qa_compat_q2_mem_offset(c->memory, loc, 4), v[1]);
  qa_compat_q2_mem_write_f32(c->memory, qa_compat_q2_mem_offset(c->memory, loc, 8), v[2]);
  return loc;
}
static void commit(ctx_t *c) {
  qa_q2_classic_pmove *pm = c->pm; uint8_t *view = c->view;
  qa_compat_q2_view_set_i32(view, 0, (int32_t)pm->s.pm_type);
  put_short(view, 4, pm->s.origin); put_short(view, 10, pm->s.velocity);
  view[16] = (uint8_t)pm->s.pm_flags; view[17] = (uint8_t)pm->s.pm_time;
  qa_compat_q2_view_set_i16(view, 18, (int32_t)pm->s.gravity); put_short(view, 20, pm->s.delta_angles);
  qa_compat_q2_view_set_i32(view, 48, (int32_t)pm->numtouch);
  int n = pm->numtouch < 32 ? (int)pm->numtouch : 32;
  for (int i = 0; i < n; i++) {
    if (i >= 32) qa_range_error("Pmove exceeded MAXTOUCH");
    qa_guest_address a; bool has = pointer_of(c, true, pm->touchents[i], &a);
    qa_compat_q2_mem_write_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, c->address, 52 + i * 4), has, a);
  }
  put_float(view, 180, pm->viewangles); qa_compat_q2_view_set_f32(view, 192, pm->viewheight);
  put_float(view, 196, pm->mins); put_float(view, 208, pm->maxs);
  qa_guest_address g; bool has_g = pm->has_groundentity && pointer_of(c, true, pm->groundentity, &g);
  qa_compat_q2_mem_write_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, c->address, 220), has_g, g);
  qa_compat_q2_view_set_i32(view, 224, (int32_t)pm->watertype); qa_compat_q2_view_set_i32(view, 228, (int32_t)pm->waterlevel);
}
static void reload(ctx_t *c) {
  qa_q2_classic_pmove *pm = c->pm; uint8_t *view = c->view;
  pm->s.pm_type = qa_compat_q2_view_i32(view, 0);
  vec_short(view, 4, pm->s.origin); vec_short(view, 10, pm->s.velocity);
  pm->s.pm_flags = view[16]; pm->s.pm_time = view[17]; pm->s.gravity = qa_compat_q2_view_i16(view, 18);
  vec_short(view, 20, pm->s.delta_angles);
  pm->cmd.msec = view[28]; pm->cmd.buttons = view[29]; vec_short(view, 30, pm->cmd.angles);
  pm->cmd.forwardmove = qa_compat_q2_view_i16(view, 36); pm->cmd.sidemove = qa_compat_q2_view_i16(view, 38);
  pm->cmd.upmove = qa_compat_q2_view_i16(view, 40); pm->cmd.impulse = view[42]; pm->cmd.lightlevel = view[43];
  pm->snapinitial = qa_compat_q2_view_i32(view, 44) != 0; pm->numtouch = qa_compat_q2_view_i32(view, 48);
  if (pm->numtouch < 0 || pm->numtouch > 32) qa_range_error("Source callback set an invalid Pmove touch count");
  for (int i = 0; i < (int)pm->numtouch; i++) {
    qa_guest_address a; bool has = qa_compat_q2_mem_read_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, c->address, 52 + i * 4), &a);
    if (!entity_of(c, has, a, &pm->touchents[i])) qa_range_error("Source callback set a null Pmove touch");
  }
  vec_float(view, 180, pm->viewangles); pm->viewheight = qa_compat_q2_view_f32(view, 192);
  vec_float(view, 196, pm->mins); vec_float(view, 208, pm->maxs);
  qa_guest_address g; bool has_g = qa_compat_q2_mem_read_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, c->address, 220), &g);
  pm->has_groundentity = entity_of(c, has_g, g, &pm->groundentity);
  pm->watertype = qa_compat_q2_view_i32(view, 224); pm->waterlevel = qa_compat_q2_view_i32(view, 228);
}
static void current_actor(ctx_t *c) { if (c->body && c->body->current_actor) c->body->current_actor(c->body->ctx); }
static qa_q2_trace trace_cb(void *vctx, qa_q2_vec3 start, qa_q2_vec3 mins, qa_q2_vec3 maxs, qa_q2_vec3 end) {
  ctx_t *c = vctx; current_actor(c); commit(c);
  qa_guest_address target;
  if (!qa_compat_q2_mem_read_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, c->address, 232), &target))
    qa_range_error("Source cleared Pmove trace callback");
  static const qa_guest_value_layout pp[] = {{false,"pointer",{0}},{false,"pointer",{0}},{false,"pointer",{0}},{false,"pointer",{0}}};
  double *vecs[] = {start, mins, maxs, end};
  qa_guest_call_value args[4];
  for (int i = 0; i < 4; i++) args[i] = qa_compat_q2_call_pointer(true, prepare_vector(c, i, vecs[i]));
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(c->host);
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_invoke(
      c->host, target, qa_compat_q2_classic_signature(pp, 4, false, qa_compat_q2_q2_trace, false), args, 4, NULL, opts->instruction_budget);
  reload(c); current_actor(c);
  if (result.kind != QA_GUEST_VALUE_AGGREGATE || result.bytes.length != 56) qa_range_error("API 3 Pmove trace returned an invalid trace_t");
  const uint8_t *data = result.bytes.bytes;
  qa_guest_address_result hit_ptr = qa_compat_q2_mem_pointer(c->memory, (int64_t)qa_compat_q2_view_u32(data, 52));
  qa_q2_movement_entity hit; bool has_hit = entity_of(c, hit_ptr.has_value, hit_ptr.value, &hit);
  qa_bsp_plane plane; memset(&plane, 0, sizeof plane);
  plane.plane.normal = (qa_vec3){qa_compat_q2_view_f32(data, 24), qa_compat_q2_view_f32(data, 28), qa_compat_q2_view_f32(data, 32)};
  plane.plane.distance = qa_compat_q2_view_f32(data, 36); plane.type = data[40]; plane.signbits = data[41];
  qa_guest_address_result surf_ptr = qa_compat_q2_mem_pointer(c->memory, (int64_t)qa_compat_q2_view_u32(data, 44));
  const qa_q2_surface_info *surface = NULL; qa_q2_surface_info surf_storage;
  if (surf_ptr.has_value) {
    size_t idx = c->surface_count < 16 ? c->surface_count++ : 15;
    uint8_t raw[16]; c->memory->copy(c->memory->ctx, surf_ptr.value, raw, 16);
    size_t ln = 0; while (ln < 16 && raw[ln]) ln++;
    memcpy(c->surface_names[idx], raw, ln); c->surface_names[idx][ln] = 0;
    c->surfaces[idx].name = c->surface_names[idx];
    c->surfaces[idx].flags = qa_compat_q2_mem_read_i32(c->memory, qa_compat_q2_mem_offset(c->memory, surf_ptr.value, 16));
    c->surfaces[idx].value = qa_compat_q2_mem_read_i32(c->memory, qa_compat_q2_mem_offset(c->memory, surf_ptr.value, 20));
    c->surfaces[idx].material = "";
    surf_storage = c->surfaces[idx]; surface = &surf_storage;
  }
  double fraction = qa_compat_q2_view_f32(data, 8); bool all_solid = qa_compat_q2_view_i32(data, 0) != 0;
  qa_trace_result source; memset(&source, 0, sizeof source);
  source.kind = "q2"; source.fraction = fraction; source.start_solid = qa_compat_q2_view_i32(data, 4) != 0; source.all_solid = all_solid;
  source.end = (qa_vec3){qa_compat_q2_view_f32(data, 12), qa_compat_q2_view_f32(data, 16), qa_compat_q2_view_f32(data, 20)};
  source.source_plane = plane;
  source.contact.has_plane = fraction < 1 && !all_solid; source.contact.plane = plane.plane;
  if (has_hit) source.hit = hit; else { memset(&source.hit, 0, sizeof source.hit); source.hit.kind = QA_TRACE_HIT_NONE; }
  source.contents = qa_compat_q2_view_i32(data, 48); source.surface = surface;
  qa_q2_trace trace; memset(&trace, 0, sizeof trace);
  trace.allsolid = all_solid; trace.startsolid = source.start_solid; trace.fraction = fraction;
  trace.endpos[0] = source.end.x; trace.endpos[1] = source.end.y; trace.endpos[2] = source.end.z;
  trace.plane.normal[0] = plane.plane.normal.x; trace.plane.normal[1] = plane.plane.normal.y; trace.plane.normal[2] = plane.plane.normal.z;
  trace.plane.dist = plane.plane.distance; trace.plane.type = plane.type; trace.plane.signbits = plane.signbits;
  if (surface) { trace.has_surface = true; trace.surface = *surface; }
  trace.contents = source.contents; trace.has_ent = has_hit; if (has_hit) trace.ent = hit;
  trace.source = source;
  return trace;
}
static double contents_cb(void *vctx, qa_q2_vec3 point) {
  ctx_t *c = vctx; current_actor(c); commit(c);
  qa_guest_address target;
  if (!qa_compat_q2_mem_read_pointer(c->memory, qa_compat_q2_mem_offset(c->memory, c->address, 236), &target))
    qa_range_error("Source cleared Pmove contents callback");
  static const qa_guest_value_layout pp[] = {{false,"pointer",{0}}};
  qa_guest_call_value arg = qa_compat_q2_call_pointer(true, prepare_vector(c, 0, point));
  const qa_compat_q2_classic_guest_host_options *opts = qa_compat_q2_classic_guest_host_get_options(c->host);
  qa_guest_call_result result = qa_compat_q2_classic_guest_host_invoke(
      c->host, target, qa_compat_q2_classic_signature(pp, 1, false, qa_compat_q2_q2_int, false), &arg, 1, NULL, opts->instruction_budget);
  reload(c); current_actor(c);
  if (result.kind != QA_GUEST_VALUE_INT32) qa_range_error("API 3 Pmove pointcontents did not return int");
  return result.number_value;
}
typedef struct run_ctx { qa_compat_q2_classic_guest_host *host; qa_guest_address address; qa_compat_q2_classic_guest_pmove_options options; } run_ctx_t;
static void run_movement(void *vctx, const qa_movement_body_shape *body) {
  run_ctx_t *rc = vctx;
  qa_compat_q2_classic_guest_host *host = rc->host; qa_guest_address address = rc->address;
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(host);
  uint8_t *view = memory->borrow(memory->ctx, address, QA_COMPAT_Q2_CLASSIC_PMOVE_BYTES);
  qa_guest_address scratch = memory->allocate(memory->ctx, 48, false, 0, "API 3 nested Pmove vectors");
  qa_q2_classic_pmove pm; memset(&pm, 0, sizeof pm);
  ctx_t c; memset(&c, 0, sizeof c);
  c.host = host; c.memory = memory; c.address = address; c.scratch = scratch; c.view = view; c.pm = &pm; c.body = body;
  pm.ctx = &c;
  pm.s.pm_type = qa_compat_q2_view_i32(view, 0);
  vec_short(view, 4, pm.s.origin); vec_short(view, 10, pm.s.velocity);
  pm.s.pm_flags = view[16]; pm.s.pm_time = view[17]; pm.s.gravity = qa_compat_q2_view_i16(view, 18);
  vec_short(view, 20, pm.s.delta_angles);
  pm.cmd.msec = view[28]; pm.cmd.buttons = view[29]; vec_short(view, 30, pm.cmd.angles);
  pm.cmd.forwardmove = qa_compat_q2_view_i16(view, 36); pm.cmd.sidemove = qa_compat_q2_view_i16(view, 38); pm.cmd.upmove = qa_compat_q2_view_i16(view, 40);
  pm.cmd.impulse = view[42]; pm.cmd.lightlevel = view[43];
  pm.snapinitial = qa_compat_q2_view_i32(view, 44) != 0;
  vec_float(view, 180, pm.viewangles); pm.viewheight = qa_compat_q2_view_f32(view, 192);
  vec_float(view, 196, pm.mins); vec_float(view, 208, pm.maxs);
  pm.watertype = qa_compat_q2_view_i32(view, 224); pm.waterlevel = qa_compat_q2_view_i32(view, 228);
  pm.character_bounds = rc->options.character_bounds ? *rc->options.character_bounds : qa_q2_player_bounds;
  if (body) { pm.has_previous_bounds = true; pm.previous_bounds = body->current; if (body->has_requested) { pm.has_body_bounds = true; pm.body_bounds = body->requested; } }
  pm.trace = trace_cb; pm.pointcontents = contents_cb;
  const qa_fixed_movement_pose *pose = rc->options.equipment ? rc->options.equipment->pose : NULL;
  double type = pm.s.pm_type;
  if (pose) { pm.s.pm_type = QA_Q2_PM_FREEZE; pm.s.velocity[0] = pm.s.velocity[1] = pm.s.velocity[2] = 0; }
  qa_q2_pmove_classic(&pm, &rc->options.numeric, rc->options.has_air_accelerate ? rc->options.air_accelerate : 0, false, false,
                      rc->options.equipment && rc->options.equipment->has_speed_multiplier ? rc->options.equipment->speed_multiplier : 1);
  if (pose) {
    pm.s.pm_type = type; pm.viewheight = pose->view_height;
    pm.mins[0] = pose->bounds.min.x; pm.mins[1] = pose->bounds.min.y; pm.mins[2] = pose->bounds.min.z;
    pm.maxs[0] = pose->bounds.max.x; pm.maxs[1] = pose->bounds.max.y; pm.maxs[2] = pose->bounds.max.z;
    pm.s.pm_flags = pose->crouched ? (double)((int)pm.s.pm_flags | QA_Q2_PMF_DUCKED) : (double)((int)pm.s.pm_flags & ~QA_Q2_PMF_DUCKED);
  }
  commit(&c);
  memory->unmap(memory->ctx, scratch, 48);
}
void qa_compat_q2_run_classic_guest_pmove(qa_guest_address address, qa_compat_q2_classic_guest_host *host,
                                          qa_compat_q2_classic_guest_pmove_options options) {
  qa_compat_q2_memory *memory = qa_compat_q2_classic_guest_host_get_memory(host);
  uint8_t *view = memory->borrow(memory->ctx, address, QA_COMPAT_Q2_CLASSIC_PMOVE_BYTES);
  qa_guest_address scratch_a, scratch_b;
  bool has_trace = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, 232), &scratch_a);
  bool has_contents = qa_compat_q2_mem_read_pointer(memory, qa_compat_q2_mem_offset(memory, address, 236), &scratch_b);
  if (!has_trace || !has_contents) qa_range_error("API 3 Pmove callback pointer is null");
  if (options.equipment) {
    double g = options.numeric.to_int32(options.numeric.ctx, options.numeric.multiply(options.numeric.ctx, qa_compat_q2_view_i16(view, 18), options.equipment->gravity_scale));
    qa_compat_q2_view_set_i16(view, 18, (int32_t)g);
    view[16] = (uint8_t)(options.equipment->prediction_suppressed ? (view[16] | 64) : (view[16] & ~64));
  }
  if (options.equipment && options.equipment->velocity) {
    const double vals[] = {options.equipment->velocity->x, options.equipment->velocity->y, options.equipment->velocity->z};
    for (int i = 0; i < 3; i++)
      qa_compat_q2_view_set_i16(view, 10 + (size_t)i * 2, (int32_t)options.numeric.to_int32(options.numeric.ctx, options.numeric.multiply(options.numeric.ctx, vals[i], 8)));
  }
  run_ctx_t rc = {host, address, options};
  qa_compat_q2_classic_guest_host_apply_input_movement(host, address, &rc, run_movement);
}
