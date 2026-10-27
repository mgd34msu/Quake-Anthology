/* Port of src/compat/qvm/bot-navigation-records.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/bot_navigation_records.h"

#include <string.h>

#include "qa/core/binary/index.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static void require_goal_record(const qa_qvmx_view *view, int64_t length) {
  if ((int64_t)view->length < length)
    qa_binary_error("QVM bot_goal_t", 0, "record requires %lld bytes, received %zu",
                    (long long)length, view->length);
}

static qa_vec3 read_vector(const qa_qvmx_view *view, int64_t offset) {
  qa_vec3 v = {qa_qvmx_view_f32(view, offset), qa_qvmx_view_f32(view, offset + 4),
               qa_qvmx_view_f32(view, offset + 8)};
  return v;
}

static void write_goal_vector(qa_qvmx_view *view, int64_t offset, qa_vec3 value) {
  qa_qvmx_view_set_f32(view, offset, qa_float32(value.x));
  qa_qvmx_view_set_f32(view, offset + 4, qa_float32(value.y));
  qa_qvmx_view_set_f32(view, offset + 8, qa_float32(value.z));
}

static qa_qvmx_view reference_field(const qa_qvm_bot_pointer *pointer, int64_t offset) {
  qa_qvmx_view bytes = pointer->get(pointer->ctx);
  if (bytes.bytes == NULL) qa_range_error("QVM bot goal input requires a nonnull pointer");
  if (offset + 4 > (int64_t)bytes.length) qa_range_error("QVM bot goal input exceeds allocation");
  qa_qvmx_view field = {bytes.bytes + offset, 4};
  return field;
}

static qa_vec3 reference_vector(const qa_qvm_bot_pointer *pointer, int64_t offset) {
  qa_vec3 v;
  qa_qvmx_view x = reference_field(pointer, offset);
  v.x = qa_qvmx_view_f32(&x, 0);
  qa_qvmx_view y = reference_field(pointer, offset + 4);
  v.y = qa_qvmx_view_f32(&y, 0);
  qa_qvmx_view z = reference_field(pointer, offset + 8);
  v.z = qa_qvmx_view_f32(&z, 0);
  return v;
}

qa_qvm_bot_goal_ref qa_qvm_read_bot_goal_reference(qa_qvm_bot_pointer pointer) {
  qa_qvm_bot_goal_ref ref = {pointer};
  return ref;
}
qa_vec3 qa_qvm_bot_goal_ref_get_origin(const qa_qvm_bot_goal_ref *ref) {
  return reference_vector(&ref->pointer, 0);
}
double qa_qvm_bot_goal_ref_get_area(const qa_qvm_bot_goal_ref *ref) {
  qa_qvmx_view f = reference_field(&ref->pointer, 12);
  return qa_qvmx_view_i32(&f, 0);
}
qa_vec3 qa_qvm_bot_goal_ref_get_mins(const qa_qvm_bot_goal_ref *ref) {
  return reference_vector(&ref->pointer, 16);
}
qa_vec3 qa_qvm_bot_goal_ref_get_maxs(const qa_qvm_bot_goal_ref *ref) {
  return reference_vector(&ref->pointer, 28);
}
double qa_qvm_bot_goal_ref_get_entity(const qa_qvm_bot_goal_ref *ref) {
  qa_qvmx_view f = reference_field(&ref->pointer, 40);
  return qa_qvmx_view_i32(&f, 0);
}
double qa_qvm_bot_goal_ref_get_number(const qa_qvm_bot_goal_ref *ref) {
  qa_qvmx_view f = reference_field(&ref->pointer, 44);
  return qa_qvmx_view_i32(&f, 0);
}
double qa_qvm_bot_goal_ref_get_flags(const qa_qvm_bot_goal_ref *ref) {
  qa_qvmx_view f = reference_field(&ref->pointer, 48);
  return qa_qvmx_view_i32(&f, 0);
}
double qa_qvm_bot_goal_ref_get_item_info(const qa_qvm_bot_goal_ref *ref) {
  qa_qvmx_view f = reference_field(&ref->pointer, 52);
  return qa_qvmx_view_i32(&f, 0);
}
qa_qvmx_bot_goal qa_qvm_bot_goal_ref_read(const qa_qvm_bot_goal_ref *ref) {
  qa_qvmx_bot_goal goal;
  goal.origin = qa_qvm_bot_goal_ref_get_origin(ref);
  goal.area = qa_qvm_bot_goal_ref_get_area(ref);
  goal.mins = qa_qvm_bot_goal_ref_get_mins(ref);
  goal.maxs = qa_qvm_bot_goal_ref_get_maxs(ref);
  goal.entity = qa_qvm_bot_goal_ref_get_entity(ref);
  goal.number = qa_qvm_bot_goal_ref_get_number(ref);
  goal.flags = qa_qvm_bot_goal_ref_get_flags(ref);
  goal.item_info = qa_qvm_bot_goal_ref_get_item_info(ref);
  return goal;
}

qa_qvmx_bot_goal qa_qvm_read_bot_goal(qa_qvmx_view view) {
  require_goal_record(&view, QA_QVM_BOT_GOAL_BYTES);
  qa_qvmx_bot_goal goal;
  goal.origin = read_vector(&view, 0);
  goal.area = qa_qvmx_view_i32(&view, 12);
  goal.mins = read_vector(&view, 16);
  goal.maxs = read_vector(&view, 28);
  goal.entity = qa_qvmx_view_i32(&view, 40);
  goal.number = qa_qvmx_view_i32(&view, 44);
  goal.flags = qa_qvmx_view_i32(&view, 48);
  goal.item_info = qa_qvmx_view_i32(&view, 52);
  return goal;
}

void qa_qvm_write_bot_goal(qa_qvmx_view view, const qa_qvmx_bot_goal *goal, const char *fields) {
  const char *mode = fields == NULL ? "full" : fields;
  require_goal_record(&view, strcmp(mode, "full") == 0 ? QA_QVM_BOT_GOAL_BYTES
                             : strcmp(mode, "level-item") == 0 ? 52 : 44);
  qa_qvmx_view_set_i32(&view, 12, qa_int32(goal->area));
  write_goal_vector(&view, 0, goal->origin);
  qa_qvmx_view_set_i32(&view, 40, qa_int32(goal->entity));
  write_goal_vector(&view, 16, goal->mins);
  write_goal_vector(&view, 28, goal->maxs);
  if (strcmp(mode, "location") == 0) return;
  qa_qvmx_view_set_i32(&view, 44, qa_int32(goal->number));
  qa_qvmx_view_set_i32(&view, 48, qa_int32(goal->flags));
  if (strcmp(mode, "full") == 0) qa_qvmx_view_set_i32(&view, 52, qa_int32(goal->item_info));
}

static qa_qvmx_view movement_field(const qa_qvm_bot_pointer *pointer, int64_t offset,
                                   const qa_qvm_bot_field_view *field_view) {
  qa_qvmx_view bytes = pointer->get(pointer->ctx);
  if (bytes.bytes == NULL) qa_range_error("QVM bot movement requires a nonnull pointer");
  if (offset + 4 > (int64_t)bytes.length)
    qa_binary_error("QVM bot movement record", offset, "field exceeds %zu-byte allocation",
                    bytes.length);
  if (field_view == NULL || field_view->view == NULL) {
    qa_qvmx_view field = {bytes.bytes + offset, 4};
    return field;
  }
  return field_view->view(field_view->ctx, bytes, (double)offset);
}

static qa_vec3 movement_vector(const qa_qvm_bot_pointer *pointer, int64_t offset) {
  qa_vec3 v;
  qa_qvmx_view x = movement_field(pointer, offset, NULL);
  v.x = qa_qvmx_view_f32(&x, 0);
  qa_qvmx_view y = movement_field(pointer, offset + 4, NULL);
  v.y = qa_qvmx_view_f32(&y, 0);
  qa_qvmx_view z = movement_field(pointer, offset + 8, NULL);
  v.z = qa_qvmx_view_f32(&z, 0);
  return v;
}

static void write_movement_vector(const qa_qvm_bot_pointer *pointer, int64_t offset, qa_vec3 value,
                                  const qa_qvm_bot_field_view *field_view) {
  qa_qvmx_view x = movement_field(pointer, offset, field_view);
  qa_qvmx_view_set_f32(&x, 0, qa_float32(value.x));
  qa_qvmx_view y = movement_field(pointer, offset + 4, field_view);
  qa_qvmx_view_set_f32(&y, 0, qa_float32(value.y));
  qa_qvmx_view z = movement_field(pointer, offset + 8, field_view);
  qa_qvmx_view_set_f32(&z, 0, qa_float32(value.z));
}

qa_qvm_bot_init_move_ref qa_qvm_bot_init_move_reference(qa_qvm_bot_pointer pointer) {
  qa_qvm_bot_init_move_ref ref = {pointer};
  return ref;
}
qa_vec3 qa_qvm_bot_init_move_get_origin(const qa_qvm_bot_init_move_ref *ref) {
  return movement_vector(&ref->pointer, 0);
}
qa_vec3 qa_qvm_bot_init_move_get_velocity(const qa_qvm_bot_init_move_ref *ref) {
  return movement_vector(&ref->pointer, 12);
}
qa_vec3 qa_qvm_bot_init_move_get_view_offset(const qa_qvm_bot_init_move_ref *ref) {
  return movement_vector(&ref->pointer, 24);
}
double qa_qvm_bot_init_move_get_entity_num(const qa_qvm_bot_init_move_ref *ref) {
  qa_qvmx_view f = movement_field(&ref->pointer, 36, NULL);
  return qa_qvmx_view_i32(&f, 0);
}
double qa_qvm_bot_init_move_get_client(const qa_qvm_bot_init_move_ref *ref) {
  qa_qvmx_view f = movement_field(&ref->pointer, 40, NULL);
  return qa_qvmx_view_i32(&f, 0);
}
double qa_qvm_bot_init_move_get_think_time(const qa_qvm_bot_init_move_ref *ref) {
  qa_qvmx_view f = movement_field(&ref->pointer, 44, NULL);
  return qa_qvmx_view_f32(&f, 0);
}
double qa_qvm_bot_init_move_get_presence_type(const qa_qvm_bot_init_move_ref *ref) {
  qa_qvmx_view f = movement_field(&ref->pointer, 48, NULL);
  return qa_qvmx_view_i32(&f, 0);
}
qa_vec3 qa_qvm_bot_init_move_get_view_angles(const qa_qvm_bot_init_move_ref *ref) {
  return movement_vector(&ref->pointer, 52);
}
double qa_qvm_bot_init_move_get_or_move_flags(const qa_qvm_bot_init_move_ref *ref) {
  qa_qvmx_view f = movement_field(&ref->pointer, 64, NULL);
  return qa_qvmx_view_i32(&f, 0);
}
qa_qvmx_bot_init_move qa_qvm_bot_init_move_read(const qa_qvm_bot_init_move_ref *ref) {
  qa_qvmx_bot_init_move move;
  move.origin = qa_qvm_bot_init_move_get_origin(ref);
  move.velocity = qa_qvm_bot_init_move_get_velocity(ref);
  move.view_offset = qa_qvm_bot_init_move_get_view_offset(ref);
  move.entity_num = qa_qvm_bot_init_move_get_entity_num(ref);
  move.client = qa_qvm_bot_init_move_get_client(ref);
  move.think_time = qa_qvm_bot_init_move_get_think_time(ref);
  move.presence_type = qa_qvm_bot_init_move_get_presence_type(ref);
  move.view_angles = qa_qvm_bot_init_move_get_view_angles(ref);
  move.or_move_flags = qa_qvm_bot_init_move_get_or_move_flags(ref);
  return move;
}

qa_qvm_bot_move_result_ref qa_qvm_bot_move_result_reference(qa_qvm_bot_pointer pointer,
                                                            qa_qvm_bot_field_view view) {
  qa_qvm_bot_move_result_ref ref = {pointer, view};
  return ref;
}

#define MOVE_RESULT_I32(name, offset)                                                          \
  double qa_qvm_bot_move_result_get_##name(const qa_qvm_bot_move_result_ref *ref) {            \
    qa_qvmx_view f = movement_field(&ref->pointer, offset, NULL);                              \
    return qa_qvmx_view_i32(&f, 0);                                                            \
  }                                                                                            \
  void qa_qvm_bot_move_result_set_##name(const qa_qvm_bot_move_result_ref *ref, double value) { \
    qa_qvmx_view f = movement_field(&ref->pointer, offset, &ref->view);                        \
    qa_qvmx_view_set_i32(&f, 0, qa_int32(value));                                              \
  }
#define MOVE_RESULT_BOOL(name, offset)                                                         \
  bool qa_qvm_bot_move_result_get_##name(const qa_qvm_bot_move_result_ref *ref) {              \
    qa_qvmx_view f = movement_field(&ref->pointer, offset, NULL);                              \
    return qa_qvmx_view_i32(&f, 0) != 0;                                                       \
  }                                                                                            \
  void qa_qvm_bot_move_result_set_##name(const qa_qvm_bot_move_result_ref *ref, bool value) {   \
    qa_qvmx_view f = movement_field(&ref->pointer, offset, &ref->view);                        \
    qa_qvmx_view_set_i32(&f, 0, value ? 1 : 0);                                                \
  }

MOVE_RESULT_BOOL(failure, 0)
MOVE_RESULT_I32(type, 4)
MOVE_RESULT_BOOL(blocked, 8)
MOVE_RESULT_I32(block_entity, 12)
MOVE_RESULT_I32(travel_type, 16)
MOVE_RESULT_I32(flags, 20)
MOVE_RESULT_I32(weapon, 24)

qa_vec3 qa_qvm_bot_move_result_get_move_direction(const qa_qvm_bot_move_result_ref *ref) {
  return movement_vector(&ref->pointer, 28);
}
void qa_qvm_bot_move_result_set_move_direction(const qa_qvm_bot_move_result_ref *ref, qa_vec3 value) {
  write_movement_vector(&ref->pointer, 28, value, &ref->view);
}
qa_vec3 qa_qvm_bot_move_result_get_ideal_view_angles(const qa_qvm_bot_move_result_ref *ref) {
  return movement_vector(&ref->pointer, 40);
}
void qa_qvm_bot_move_result_set_ideal_view_angles(const qa_qvm_bot_move_result_ref *ref, qa_vec3 value) {
  write_movement_vector(&ref->pointer, 40, value, &ref->view);
}
qa_qvmx_bot_move_result qa_qvm_bot_move_result_read(const qa_qvm_bot_move_result_ref *ref) {
  qa_qvmx_bot_move_result result;
  result.failure = qa_qvm_bot_move_result_get_failure(ref);
  result.type = qa_qvm_bot_move_result_get_type(ref);
  result.blocked = qa_qvm_bot_move_result_get_blocked(ref);
  result.block_entity = qa_qvm_bot_move_result_get_block_entity(ref);
  result.travel_type = qa_qvm_bot_move_result_get_travel_type(ref);
  result.flags = qa_qvm_bot_move_result_get_flags(ref);
  result.weapon = qa_qvm_bot_move_result_get_weapon(ref);
  result.move_direction = qa_qvm_bot_move_result_get_move_direction(ref);
  result.ideal_view_angles = qa_qvm_bot_move_result_get_ideal_view_angles(ref);
  return result;
}

qa_vec3 qa_qvm_bot_movement_vector(qa_qvm_bot_pointer pointer) {
  return movement_vector(&pointer, 0);
}

qa_qvm_bot_movement_target_ref qa_qvm_bot_movement_target(qa_qvm_bot_pointer pointer,
                                                          qa_qvm_bot_field_view view) {
  qa_qvm_bot_movement_target_ref ref = {pointer, view};
  return ref;
}
qa_vec3 qa_qvm_bot_movement_target_get(const qa_qvm_bot_movement_target_ref *ref) {
  return movement_vector(&ref->pointer, 0);
}
void qa_qvm_bot_movement_target_set(const qa_qvm_bot_movement_target_ref *ref, qa_vec3 value) {
  write_movement_vector(&ref->pointer, 0, value, &ref->view);
}

qa_qvm_bot_entity_state_ref qa_qvm_borrow_bot_entity_state(qa_qvmx_view view) {
  if ((int64_t)view.length < QA_QVM_BOT_ENTITY_STATE_BYTES)
    qa_binary_error("QVM bot_entitystate_t", 0, "record requires %d bytes, received %zu",
                    QA_QVM_BOT_ENTITY_STATE_BYTES, view.length);
  qa_qvm_bot_entity_state_ref ref = {view};
  return ref;
}

qa_qvm_bot_entity_update qa_qvm_bot_entity_state_read_ref(const qa_qvm_bot_entity_state_ref *ref) {
  const qa_qvmx_view *view = &ref->view;
  qa_qvm_bot_entity_update update;
  update.type = qa_qvmx_view_i32(view, 0);
  update.flags = qa_qvmx_view_i32(view, 4);
  update.origin = read_vector(view, 8);
  update.angles = read_vector(view, 20);
  update.old_origin = read_vector(view, 32);
  update.mins = read_vector(view, 44);
  update.maxs = read_vector(view, 56);
  update.ground_entity = qa_qvmx_view_i32(view, 68);
  update.solid = qa_qvmx_view_i32(view, 72);
  update.model_index = qa_qvmx_view_i32(view, 76);
  update.model_index2 = qa_qvmx_view_i32(view, 80);
  update.frame = qa_qvmx_view_i32(view, 84);
  update.event = qa_qvmx_view_i32(view, 88);
  update.event_parameter = qa_qvmx_view_i32(view, 92);
  update.powerups = qa_qvmx_view_i32(view, 96);
  update.weapon = qa_qvmx_view_i32(view, 100);
  update.legs_animation = qa_qvmx_view_i32(view, 104);
  update.torso_animation = qa_qvmx_view_i32(view, 108);
  return update;
}

qa_qvm_bot_entity_update qa_qvm_read_bot_entity_state(qa_qvmx_view view) {
  qa_qvm_bot_entity_state_ref ref = qa_qvm_borrow_bot_entity_state(view);
  return qa_qvm_bot_entity_state_read_ref(&ref);
}

void qa_qvm_write_aas_entity_info(qa_qvmx_view view, const qa_qvm_aas_entity_info *info) {
  require_goal_record(&view, QA_QVM_AAS_ENTITY_INFO_BYTES);
  qa_qvmx_view_set_i32(&view, 0, info->valid ? 1 : 0);
  qa_qvmx_view_set_i32(&view, 4, qa_int32(info->type));
  qa_qvmx_view_set_i32(&view, 8, qa_int32(info->flags));
  qa_qvmx_view_set_f32(&view, 12, qa_float32(info->last_update_time));
  qa_qvmx_view_set_f32(&view, 16, qa_float32(info->update_interval));
  qa_qvmx_view_set_i32(&view, 20, qa_int32(info->number));
  write_goal_vector(&view, 24, info->origin);
  write_goal_vector(&view, 36, info->angles);
  write_goal_vector(&view, 48, info->old_origin);
  write_goal_vector(&view, 60, info->last_visible_origin);
  write_goal_vector(&view, 72, info->mins);
  write_goal_vector(&view, 84, info->maxs);
  qa_qvmx_view_set_i32(&view, 96, qa_int32(info->ground_entity));
  qa_qvmx_view_set_i32(&view, 100, qa_int32(info->solid));
  qa_qvmx_view_set_i32(&view, 104, qa_int32(info->model_index));
  qa_qvmx_view_set_i32(&view, 108, qa_int32(info->model_index2));
  qa_qvmx_view_set_i32(&view, 112, qa_int32(info->frame));
  qa_qvmx_view_set_i32(&view, 116, qa_int32(info->event));
  qa_qvmx_view_set_i32(&view, 120, qa_int32(info->event_parameter));
  qa_qvmx_view_set_i32(&view, 124, qa_int32(info->powerups));
  qa_qvmx_view_set_i32(&view, 128, qa_int32(info->weapon));
  qa_qvmx_view_set_i32(&view, 132, qa_int32(info->legs_animation));
  qa_qvmx_view_set_i32(&view, 136, qa_int32(info->torso_animation));
}
