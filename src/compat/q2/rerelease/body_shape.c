/* Port of src/compat/q2/rerelease/body-shape.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/body_shape.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct frame {
  qa_compat_q2_rerelease_guest_module *module; qa_guest_address address;
  const qa_movement_body_shape *body; uint8_t *view; qa_guest_address trace;
  qa_bounds accepted; uint16_t previous_duck; int8_t previous_height; size_t flags_off, height_off;
  struct frame *next;
} frame_t;
static frame_t *active_frames = NULL; /* per-process stack (TS WeakMap by module) */
static qa_vec3 read_vec(frame_t *f, const char *name) {
  size_t at = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, name);
  return (qa_vec3){qa_compat_q2_view_f32(f->view, at), qa_compat_q2_view_f32(f->view, at + 4), qa_compat_q2_view_f32(f->view, at + 8)};
}
static void write_vec(frame_t *f, const char *name, qa_vec3 v) {
  size_t at = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, name);
  qa_compat_q2_view_set_f32(f->view, at, v.x); qa_compat_q2_view_set_f32(f->view, at + 4, v.y); qa_compat_q2_view_set_f32(f->view, at + 8, v.z);
}
static bool fits_cb(void *vctx, qa_bounds bounds) {
  frame_t *f = vctx;
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(f->module);
  qa_guest_address scratch = memory->allocate(memory->ctx, 36, true, 4, "original Pmove body expansion");
  qa_vec3 vecs[3] = {read_vec(f, "s.origin"), bounds.min, bounds.max};
  qa_guest_address targets[3];
  for (int i = 0; i < 3; i++) {
    targets[i] = qa_compat_q2_mem_offset(memory, scratch, i * 12);
    uint8_t *data = memory->borrow(memory->ctx, targets[i], 12);
    qa_compat_q2_view_set_f32(data, 0, vecs[i].x); qa_compat_q2_view_set_f32(data, 4, vecs[i].y); qa_compat_q2_view_set_f32(data, 8, vecs[i].z);
  }
  static const qa_guest_value_layout params[] = {{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},
    {false,"pointer",{NULL,0,0,0,NULL,0}},{false,"pointer",{NULL,0,0,0,NULL,0}},{false,"uint32",{NULL,0,0,0,NULL,0}}};
  qa_guest_value_layout trace_result; memset(&trace_result, 0, sizeof trace_result);
  trace_result.is_aggregate = true; trace_result.layout = qa_compat_q2_rerelease_trace_layout;
  qa_guest_call_value args[] = {qa_compat_q2_call_pointer(true, targets[0]), qa_compat_q2_call_pointer(true, targets[1]),
                                qa_compat_q2_call_pointer(true, targets[2]), qa_compat_q2_call_pointer(true, targets[0]), qa_compat_q2_call_uint32(0)};
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(f->module, f->trace,
      qa_compat_q2_rerelease_signature(params, 5, false, trace_result), args, 5, NULL, NULL);
  if (f->body->current_actor) f->body->current_actor(f->body->ctx);
  memory->unmap(memory->ctx, scratch, 36);
  if (result.kind != QA_GUEST_VALUE_AGGREGATE || result.bytes.length != (size_t)qa_compat_q2_rerelease_trace_layout.byte_length)
    qa_range_error("Original Pmove expansion returned an invalid trace");
  return result.bytes.bytes[0] == 0;
}
static void apply_frame(frame_t *f) {
  if (f->body->current_actor) f->body->current_actor(f->body->ctx);
  qa_bounds desired = f->body->has_requested ? f->body->requested : (qa_bounds){read_vec(f, "mins"), read_vec(f, "maxs")};
  qa_bounds next = qa_movement_bounds(f->accepted, desired, fits_cb, f);
  bool same = next.min.x == desired.min.x && next.min.y == desired.min.y && next.min.z == desired.min.z &&
              next.max.x == desired.max.x && next.max.y == desired.max.y && next.max.z == desired.max.z;
  if (!same) {
    qa_compat_q2_view_set_u16(f->view, f->flags_off, (qa_compat_q2_view_u16(f->view, f->flags_off) & ~1u) | f->previous_duck);
    f->view[f->height_off] = (uint8_t)f->previous_height;
  }
  f->accepted = next; write_vec(f, "mins", next.min); write_vec(f, "maxs", next.max);
  f->previous_duck = qa_compat_q2_view_u16(f->view, f->flags_off) & 1;
  f->previous_height = (int8_t)f->view[f->height_off];
}
typedef struct dim_ctx { qa_compat_q2_rerelease_guest_module *module; qa_guest_address entry, global; qa_compat_q2_guest_call_signature signature; } dim_ctx_t;
static qa_guest_call_result dimensions_invoke(void *vctx, const qa_guest_call_context *context, const qa_guest_call_value *values, size_t count) {
  (void)context; dim_ctx_t *d = vctx;
  qa_guest_call_result result = qa_compat_q2_rerelease_guest_module_invoke(d->module, d->entry, d->signature, values, count, NULL, NULL);
  frame_t *current = NULL;
  for (frame_t *f = active_frames; f; f = f->next) if (f->module == d->module) { current = f; break; }
  if (current) {
    qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(d->module);
    qa_guest_address global_value;
    if (qa_compat_q2_mem_read_pointer(memory, d->global, &global_value) && global_value.byte_offset == current->address.byte_offset)
      apply_frame(current);
  }
  return result;
}
void qa_compat_q2_with_rerelease_body_shape(qa_compat_q2_rerelease_guest_module *module, qa_guest_address address,
                                            const qa_movement_body_shape *body, void *run_ctx, void (*run)(void *)) {
  if (!body) { run(run_ctx); return; }
  const qa_compat_q2_rerelease_primary_world_profile *profile = qa_compat_q2_rerelease_guest_module_require_world_profile(module);
  if (!profile->movement.has_body) {
    if (body->has_requested) qa_range_error("Native body shape requires an admitted original dimensions and trace boundary");
    run(run_ctx); return;
  }
  qa_compat_q2_memory *memory = qa_compat_q2_rerelease_guest_module_get_memory(module);
  const qa_compat_q2_rerelease_module_options *opts = qa_compat_q2_rerelease_guest_module_get_options(module);
  qa_guest_address image = qa_compat_q2_mem_offset(memory, opts->get_game_api, -(int64_t)profile->movement.game_api);
  qa_guest_address dimensions = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.body.dimensions);
  qa_guest_address trace = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.body.trace);
  qa_guest_address global = qa_compat_q2_mem_offset(memory, image, (int64_t)profile->movement.body.movement_global);
  qa_compat_q2_rerelease_layouts_ensure();
  uint8_t *view = memory->borrow(memory->ctx, address, qa_compat_q2_rerelease_pmove_layout.byte_length);
  frame_t *frame = calloc(1, sizeof *frame);
  frame->module = module; frame->address = address; frame->body = body; frame->view = view; frame->trace = trace;
  frame->accepted = body->current;
  frame->flags_off = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.pm_flags");
  frame->height_off = (size_t)qa_compat_q2_rerelease_field_offset(&qa_compat_q2_rerelease_pmove_layout, "s.viewheight");
  frame->previous_duck = qa_compat_q2_view_u16(view, frame->flags_off) & 1;
  frame->previous_height = (int8_t)view[frame->height_off];
  bool outermost = true;
  for (frame_t *f = active_frames; f; f = f->next) if (f->module == module) outermost = false;
  frame->next = active_frames; active_frames = frame;
  uint64_t binding = 0;
  dim_ctx_t *d = NULL;
  if (outermost) {
    d = malloc(sizeof *d); d->module = module; d->entry = dimensions; d->global = global;
    d->signature = qa_compat_q2_rerelease_signature(NULL, 0, true, qa_compat_q2_rr_int_layout);
    char id[512]; snprintf(id, sizeof id, "%s:client-body-dimensions", memory->module.id ? memory->module.id : "");
    char *owned = malloc(strlen(id) + 1); strcpy(owned, id);
    binding = opts->runner->bind_entry(opts->runner->ctx, dimensions, owned, d->signature, d, dimensions_invoke, NULL, NULL);
  }
  if (body->current_actor) body->current_actor(body->ctx);
  run(run_ctx);
  if (binding) opts->runner->remove_entry(opts->runner->ctx, binding);
  free(d);
  active_frames = frame->next;
  free(frame);
}
