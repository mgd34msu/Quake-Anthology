/* See include/qa/compat/qvm/shared_entity_record.h. GPL-2.0-or-later. */
#include "qa/compat/qvm/shared_entity_record.h"

#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/foundation/support.h"

#define SVF_CAPSULE 0x00000400

static bool is_modern(qa_qvm_abi_profile profile) {
  return profile == NULL || strcmp(profile, "q3-modern") == 0;
}

int qa_qvm_shared_entity_bytes(qa_qvm_abi_profile profile) {
  return is_modern(profile) ? 516 : 504;
}

static int64_t field_offset(const qa_qvm_shared_entity *entity, int64_t modern) {
  if (is_modern(entity->profile)) return modern;
  return modern < 428 ? modern - 8 : modern - 12;
}

void qa_qvm_borrow_shared_entity(const qa_qvm_writable_view *view, qa_qvm_abi_profile profile,
                                 qa_qvm_shared_entity *out) {
  if (profile == NULL) profile = "q3-modern";
  int bytes = qa_qvm_shared_entity_bytes(profile);
  if (view->length < bytes)
    qa_binary_error("QVM sharedEntity_t", 0, "record requires %d bytes, received %lld",
                    bytes, (long long)view->length);
  out->view = *view;
  out->profile = profile;
  qa_qvmx_view state_view = {view->bytes + view->start, (size_t)view->length};
  out->s = qa_qvm_borrow_entity_state(state_view, profile);
}

static int32_t get_word(qa_qvm_shared_entity *entity, int64_t modern) {
  return qa_qvm_writable_view_get_int32(&entity->view, field_offset(entity, modern), true);
}

static void set_word(qa_qvm_shared_entity *entity, int64_t modern, int32_t value) {
  qa_qvm_writable_view_set_int32(&entity->view, field_offset(entity, modern), value, true);
}

static qa_vec3 get_vector(qa_qvm_shared_entity *entity, int64_t modern) {
  int64_t offset = field_offset(entity, modern);
  qa_vec3 result = {(double)qa_qvm_writable_view_get_float32(&entity->view, offset, true),
                    (double)qa_qvm_writable_view_get_float32(&entity->view, offset + 4, true),
                    (double)qa_qvm_writable_view_get_float32(&entity->view, offset + 8, true)};
  return result;
}

static void set_vector(qa_qvm_shared_entity *entity, int64_t modern, qa_vec3 value) {
  int64_t offset = field_offset(entity, modern);
  qa_qvm_writable_view_set_float32(&entity->view, offset, value.x, true);
  qa_qvm_writable_view_set_float32(&entity->view, offset + 4, value.y, true);
  qa_qvm_writable_view_set_float32(&entity->view, offset + 8, value.z, true);
}

bool qa_qvm_shared_entity_get_linked(qa_qvm_shared_entity *e) { return get_word(e, 416) != 0; }
void qa_qvm_shared_entity_set_linked(qa_qvm_shared_entity *e, bool v) { set_word(e, 416, v ? 1 : 0); }
int32_t qa_qvm_shared_entity_get_linkcount(qa_qvm_shared_entity *e) { return get_word(e, 420); }
void qa_qvm_shared_entity_set_linkcount(qa_qvm_shared_entity *e, int32_t v) { set_word(e, 420, v); }
int32_t qa_qvm_shared_entity_get_sv_flags(qa_qvm_shared_entity *e) { return get_word(e, 424); }
void qa_qvm_shared_entity_set_sv_flags(qa_qvm_shared_entity *e, int32_t v) { set_word(e, 424, v); }

int32_t qa_qvm_shared_entity_get_single_client(qa_qvm_shared_entity *e) {
  return is_modern(e->profile) ? qa_qvm_writable_view_get_int32(&e->view, 428, true) : 0;
}

void qa_qvm_shared_entity_set_single_client(qa_qvm_shared_entity *e, int32_t v) {
  if (is_modern(e->profile)) qa_qvm_writable_view_set_int32(&e->view, 428, v, true);
  else if (v != 0) qa_range_error("Legacy QVM entity has no singleClient field");
}

qa_vec3 qa_qvm_shared_entity_get_mins(qa_qvm_shared_entity *e) { return get_vector(e, 436); }
void qa_qvm_shared_entity_set_mins(qa_qvm_shared_entity *e, qa_vec3 v) { set_vector(e, 436, v); }
qa_vec3 qa_qvm_shared_entity_get_maxs(qa_qvm_shared_entity *e) { return get_vector(e, 448); }
void qa_qvm_shared_entity_set_maxs(qa_qvm_shared_entity *e, qa_vec3 v) { set_vector(e, 448, v); }
int32_t qa_qvm_shared_entity_get_contents(qa_qvm_shared_entity *e) { return get_word(e, 460); }
void qa_qvm_shared_entity_set_contents(qa_qvm_shared_entity *e, int32_t v) { set_word(e, 460, v); }
qa_vec3 qa_qvm_shared_entity_get_absmin(qa_qvm_shared_entity *e) { return get_vector(e, 464); }
void qa_qvm_shared_entity_set_absmin(qa_qvm_shared_entity *e, qa_vec3 v) { set_vector(e, 464, v); }
qa_vec3 qa_qvm_shared_entity_get_absmax(qa_qvm_shared_entity *e) { return get_vector(e, 476); }
void qa_qvm_shared_entity_set_absmax(qa_qvm_shared_entity *e, qa_vec3 v) { set_vector(e, 476, v); }
qa_vec3 qa_qvm_shared_entity_get_current_origin(qa_qvm_shared_entity *e) { return get_vector(e, 488); }
void qa_qvm_shared_entity_set_current_origin(qa_qvm_shared_entity *e, qa_vec3 v) { set_vector(e, 488, v); }
qa_vec3 qa_qvm_shared_entity_get_current_angles(qa_qvm_shared_entity *e) { return get_vector(e, 500); }
void qa_qvm_shared_entity_set_current_angles(qa_qvm_shared_entity *e, qa_vec3 v) { set_vector(e, 500, v); }
int32_t qa_qvm_shared_entity_get_owner_num(qa_qvm_shared_entity *e) { return get_word(e, 512); }
void qa_qvm_shared_entity_set_owner_num(qa_qvm_shared_entity *e, int32_t v) { set_word(e, 512, v); }

qa_qvm_entity_collision_model qa_qvm_shared_entity_get_model(qa_qvm_shared_entity *e) {
  qa_qvm_entity_collision_model result;
  if (get_word(e, 432) != 0) {
    result.kind = "inline";
    result.index = (int32_t)qa_qvm_entity_state_get_modelindex(&e->s);
    return result;
  }
  result.kind = (get_word(e, 424) & SVF_CAPSULE) != 0 ? "capsule" : "box";
  result.index = 0;
  return result;
}

void qa_qvm_shared_entity_set_model(qa_qvm_shared_entity *e, qa_qvm_entity_collision_model value) {
  if (strcmp(value.kind, "inline") == 0) {
    qa_qvm_entity_state_set_modelindex(&e->s, (double)value.index);
    set_word(e, 432, 1);
  } else if (strcmp(value.kind, "box") == 0) {
    set_word(e, 432, 0);
    set_word(e, 424, get_word(e, 424) & ~SVF_CAPSULE);
  } else if (strcmp(value.kind, "capsule") == 0) {
    set_word(e, 432, 0);
    set_word(e, 424, get_word(e, 424) | SVF_CAPSULE);
  }
}
