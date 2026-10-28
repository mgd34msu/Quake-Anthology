/* Port of src/content/q3/base/shared/entity-shared.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/shared/entity_shared.h"
#include <string.h>
#include "qa/foundation/support.h"

void qa_q3_base_entity_shared_init(qa_q3_base_entity_shared *r, qa_q3_base_entity_body_binding body) {
  memset(r, 0, sizeof *r);
  r->model.kind = QA_Q3_BASE_MODEL_BOX;
  r->body = body;
}
bool qa_q3_base_entity_shared_linked(const qa_q3_base_entity_shared *r) {
  qa_linked_body out;
  return r->body.linked(r->body.ctx, &out);
}
void qa_q3_base_entity_shared_capture_link(qa_q3_base_entity_shared *r) {
  qa_linked_body linked;
  if (r->body.linked(r->body.ctx, &linked)) { r->previous_link = linked; r->has_previous_link = true; }
}
double qa_q3_base_entity_shared_linkcount(const qa_q3_base_entity_shared *r) {
  qa_linked_body linked;
  if (r->body.linked(r->body.ctx, &linked)) return linked.link_count;
  if (r->has_previous_link) return r->previous_link.link_count;
  return 0;
}
qa_vec3 qa_q3_base_entity_shared_mins(const qa_q3_base_entity_shared *r) {
  return r->body.read(r->body.ctx).bounds.min;
}
void qa_q3_base_entity_shared_set_mins(qa_q3_base_entity_shared *r, qa_vec3 value) {
  qa_body_state state = r->body.read(r->body.ctx);
  state.bounds.min = value;
  r->body.write(r->body.ctx, state);
}
qa_vec3 qa_q3_base_entity_shared_maxs(const qa_q3_base_entity_shared *r) {
  return r->body.read(r->body.ctx).bounds.max;
}
void qa_q3_base_entity_shared_set_maxs(qa_q3_base_entity_shared *r, qa_vec3 value) {
  qa_body_state state = r->body.read(r->body.ctx);
  state.bounds.max = value;
  r->body.write(r->body.ctx, state);
}
qa_vec3 qa_q3_base_entity_shared_current_origin(const qa_q3_base_entity_shared *r) {
  if (r->has_current_origin_view) return r->current_origin_view;
  return r->body.read(r->body.ctx).origin;
}
void qa_q3_base_entity_shared_set_current_origin(qa_q3_base_entity_shared *r, qa_vec3 value) {
  qa_body_state state = r->body.read(r->body.ctx);
  state.origin = value;
  r->body.write(r->body.ctx, state);
  if (r->has_current_origin_view) r->current_origin_view = value;
}
void qa_q3_base_entity_shared_with_current_origin(qa_q3_base_entity_shared *r, qa_vec3 origin,
    void *call_ctx, void (*call)(void *ctx)) {
  bool prev_has = r->has_current_origin_view;
  qa_vec3 prev = r->current_origin_view;
  r->current_origin_view = (qa_vec3){origin.x, origin.y, origin.z};
  r->has_current_origin_view = true;
  call(call_ctx); /* TS try/finally: aborts do not return in C either. */
  r->has_current_origin_view = prev_has;
  r->current_origin_view = prev;
}
qa_vec3 qa_q3_base_entity_shared_current_angles(const qa_q3_base_entity_shared *r) {
  return r->body.read(r->body.ctx).angles;
}
void qa_q3_base_entity_shared_set_current_angles(qa_q3_base_entity_shared *r, qa_vec3 value) {
  qa_body_state state = r->body.read(r->body.ctx);
  state.angles = value;
  r->body.write(r->body.ctx, state);
}
qa_q3_base_entity_shared_private_state qa_q3_base_entity_shared_capture_private_state(const qa_q3_base_entity_shared *r) {
  if (r->has_current_origin_view) qa_range_error("Cannot save inside a Q3 temporary origin view");
  qa_q3_base_entity_shared_private_state out;
  out.has_previous_link = r->has_previous_link; out.previous_link = r->previous_link;
  out.has_abs_min_override = r->has_abs_min_override; out.abs_min_override = r->abs_min_override;
  out.has_abs_max_override = r->has_abs_max_override; out.abs_max_override = r->abs_max_override;
  return out;
}
void qa_q3_base_entity_shared_restore_private_state(qa_q3_base_entity_shared *r,
    qa_q3_base_entity_shared_private_state state) {
  if (r->has_current_origin_view) qa_range_error("Cannot restore inside a Q3 temporary origin view");
  r->has_previous_link = state.has_previous_link; r->previous_link = state.previous_link;
  r->has_abs_min_override = state.has_abs_min_override;
  if (state.has_abs_min_override)
    r->abs_min_override = (qa_vec3){state.abs_min_override.x, state.abs_min_override.y, state.abs_min_override.z};
  r->has_abs_max_override = state.has_abs_max_override;
  if (state.has_abs_max_override)
    r->abs_max_override = (qa_vec3){state.abs_max_override.x, state.abs_max_override.y, state.abs_max_override.z};
}
void qa_q3_base_entity_shared_clear_bounds_overrides(qa_q3_base_entity_shared *r) {
  r->has_abs_min_override = false; r->has_abs_max_override = false;
}
void qa_q3_base_entity_shared_set_absmin(qa_q3_base_entity_shared *r, qa_vec3 value) {
  r->abs_min_override = value; r->has_abs_min_override = true;
}
void qa_q3_base_entity_shared_set_absmax(qa_q3_base_entity_shared *r, qa_vec3 value) {
  r->abs_max_override = value; r->has_abs_max_override = true;
}
qa_vec3 qa_q3_base_entity_shared_absmin(const qa_q3_base_entity_shared *r) {
  qa_linked_body linked;
  if (r->has_abs_min_override) return r->abs_min_override;
  if (r->body.linked(r->body.ctx, &linked)) return linked.absolute_bounds.min;
  if (r->has_previous_link) return r->previous_link.absolute_bounds.min;
  qa_vec3 zero = {0, 0, 0}; return zero;
}
qa_vec3 qa_q3_base_entity_shared_absmax(const qa_q3_base_entity_shared *r) {
  qa_linked_body linked;
  if (r->has_abs_max_override) return r->abs_max_override;
  if (r->body.linked(r->body.ctx, &linked)) return linked.absolute_bounds.max;
  if (r->has_previous_link) return r->previous_link.absolute_bounds.max;
  qa_vec3 zero = {0, 0, 0}; return zero;
}
