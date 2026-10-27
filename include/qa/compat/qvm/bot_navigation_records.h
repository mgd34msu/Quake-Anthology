/*
 * Port of src/compat/qvm/bot-navigation-records.ts.
 * qa_qvmx_bot_goal mirrors bots/behavior/library/goals.ts BotGoal and
 * qa_qvmx_bot_init_move / qa_qvmx_bot_move_result mirror
 * bots/behavior/q3/movement-state.ts (bots module unported).
 * TS getter objects become reference structs plus sampling functions
 * that re-read through the pointer closure on every call.
 * GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_BOT_NAVIGATION_RECORDS_H
#define QA_COMPAT_QVM_BOT_NAVIGATION_RECORDS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/contracts/math.h"

typedef struct qa_qvm_bot_entity_update {
  double type, flags;
  qa_vec3 origin, angles, old_origin, mins, maxs;
  double ground_entity, solid, model_index, model_index2, frame, event, event_parameter;
  double powerups, weapon, legs_animation, torso_animation;
} qa_qvm_bot_entity_update;

typedef struct qa_qvm_aas_entity_info {
  double type, flags;
  qa_vec3 origin, angles, old_origin, mins, maxs;
  double ground_entity, solid, model_index, model_index2, frame, event, event_parameter;
  double powerups, weapon, legs_animation, torso_animation;
  bool valid;
  double number;
  qa_vec3 last_visible_origin;
  double last_update_time, update_interval;
} qa_qvm_aas_entity_info;

typedef struct qa_qvmx_bot_goal {
  qa_vec3 origin;
  double area;
  qa_vec3 mins, maxs;
  double entity, number, flags, item_info;
} qa_qvmx_bot_goal;

#define QA_QVM_BOT_GOAL_BYTES 56
#define QA_QVM_BOT_INIT_MOVE_BYTES 68
#define QA_QVM_BOT_MOVE_RESULT_BYTES 52
#define QA_QVM_BOT_ENTITY_STATE_BYTES 112
#define QA_QVM_AAS_ENTITY_INFO_BYTES 140

/* TS `() => Uint8Array | null`: returned view bytes NULL means null. */
typedef struct qa_qvm_bot_pointer {
  void *ctx;
  qa_qvmx_view (*get)(void *ctx);
} qa_qvm_bot_pointer;

/* TS `(bytes: Uint8Array, offset: number) => DataView`. */
typedef struct qa_qvm_bot_field_view {
  void *ctx;
  qa_qvmx_view (*view)(void *ctx, qa_qvmx_view bytes, double offset);
} qa_qvm_bot_field_view;

typedef struct qa_qvm_bot_goal_ref { qa_qvm_bot_pointer pointer; } qa_qvm_bot_goal_ref;
qa_qvm_bot_goal_ref qa_qvm_read_bot_goal_reference(qa_qvm_bot_pointer pointer);
qa_vec3 qa_qvm_bot_goal_ref_get_origin(const qa_qvm_bot_goal_ref *ref);
double qa_qvm_bot_goal_ref_get_area(const qa_qvm_bot_goal_ref *ref);
qa_vec3 qa_qvm_bot_goal_ref_get_mins(const qa_qvm_bot_goal_ref *ref);
qa_vec3 qa_qvm_bot_goal_ref_get_maxs(const qa_qvm_bot_goal_ref *ref);
double qa_qvm_bot_goal_ref_get_entity(const qa_qvm_bot_goal_ref *ref);
double qa_qvm_bot_goal_ref_get_number(const qa_qvm_bot_goal_ref *ref);
double qa_qvm_bot_goal_ref_get_flags(const qa_qvm_bot_goal_ref *ref);
double qa_qvm_bot_goal_ref_get_item_info(const qa_qvm_bot_goal_ref *ref);
/* Samples every field of the reference into owned values. */
qa_qvmx_bot_goal qa_qvm_bot_goal_ref_read(const qa_qvm_bot_goal_ref *ref);

qa_qvmx_bot_goal qa_qvm_read_bot_goal(qa_qvmx_view view);
/* fields: "full" | "level-item" | "location" (NULL = "full"). */
void qa_qvm_write_bot_goal(qa_qvmx_view view, const qa_qvmx_bot_goal *goal, const char *fields);

typedef struct qa_qvm_bot_init_move_ref { qa_qvm_bot_pointer pointer; } qa_qvm_bot_init_move_ref;
qa_qvm_bot_init_move_ref qa_qvm_bot_init_move_reference(qa_qvm_bot_pointer pointer);
qa_vec3 qa_qvm_bot_init_move_get_origin(const qa_qvm_bot_init_move_ref *ref);
qa_vec3 qa_qvm_bot_init_move_get_velocity(const qa_qvm_bot_init_move_ref *ref);
qa_vec3 qa_qvm_bot_init_move_get_view_offset(const qa_qvm_bot_init_move_ref *ref);
double qa_qvm_bot_init_move_get_entity_num(const qa_qvm_bot_init_move_ref *ref);
double qa_qvm_bot_init_move_get_client(const qa_qvm_bot_init_move_ref *ref);
double qa_qvm_bot_init_move_get_think_time(const qa_qvm_bot_init_move_ref *ref);
double qa_qvm_bot_init_move_get_presence_type(const qa_qvm_bot_init_move_ref *ref);
qa_vec3 qa_qvm_bot_init_move_get_view_angles(const qa_qvm_bot_init_move_ref *ref);
double qa_qvm_bot_init_move_get_or_move_flags(const qa_qvm_bot_init_move_ref *ref);

typedef struct qa_qvmx_bot_init_move {
  qa_vec3 origin, velocity, view_offset;
  double entity_num, client, think_time, presence_type;
  qa_vec3 view_angles;
  double or_move_flags;
} qa_qvmx_bot_init_move;
qa_qvmx_bot_init_move qa_qvm_bot_init_move_read(const qa_qvm_bot_init_move_ref *ref);

typedef struct qa_qvm_bot_move_result_ref {
  qa_qvm_bot_pointer pointer;
  qa_qvm_bot_field_view view;
} qa_qvm_bot_move_result_ref;
qa_qvm_bot_move_result_ref qa_qvm_bot_move_result_reference(qa_qvm_bot_pointer pointer,
                                                            qa_qvm_bot_field_view view);
bool qa_qvm_bot_move_result_get_failure(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_failure(const qa_qvm_bot_move_result_ref *ref, bool value);
double qa_qvm_bot_move_result_get_type(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_type(const qa_qvm_bot_move_result_ref *ref, double value);
bool qa_qvm_bot_move_result_get_blocked(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_blocked(const qa_qvm_bot_move_result_ref *ref, bool value);
double qa_qvm_bot_move_result_get_block_entity(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_block_entity(const qa_qvm_bot_move_result_ref *ref, double value);
double qa_qvm_bot_move_result_get_travel_type(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_travel_type(const qa_qvm_bot_move_result_ref *ref, double value);
double qa_qvm_bot_move_result_get_flags(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_flags(const qa_qvm_bot_move_result_ref *ref, double value);
double qa_qvm_bot_move_result_get_weapon(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_weapon(const qa_qvm_bot_move_result_ref *ref, double value);
qa_vec3 qa_qvm_bot_move_result_get_move_direction(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_move_direction(const qa_qvm_bot_move_result_ref *ref, qa_vec3 value);
qa_vec3 qa_qvm_bot_move_result_get_ideal_view_angles(const qa_qvm_bot_move_result_ref *ref);
void qa_qvm_bot_move_result_set_ideal_view_angles(const qa_qvm_bot_move_result_ref *ref, qa_vec3 value);

typedef struct qa_qvmx_bot_move_result {
  bool failure;
  double type;
  bool blocked;
  double block_entity, travel_type, flags, weapon;
  qa_vec3 move_direction, ideal_view_angles;
} qa_qvmx_bot_move_result;
qa_qvmx_bot_move_result qa_qvm_bot_move_result_read(const qa_qvm_bot_move_result_ref *ref);

/* TS qvmBotMovementVector(pointer): a live Vec3 at offset 0. */
qa_vec3 qa_qvm_bot_movement_vector(qa_qvm_bot_pointer pointer);

typedef struct qa_qvm_bot_movement_target_ref {
  qa_qvm_bot_pointer pointer;
  qa_qvm_bot_field_view view;
} qa_qvm_bot_movement_target_ref;
qa_qvm_bot_movement_target_ref qa_qvm_bot_movement_target(qa_qvm_bot_pointer pointer,
                                                          qa_qvm_bot_field_view view);
qa_vec3 qa_qvm_bot_movement_target_get(const qa_qvm_bot_movement_target_ref *ref);
void qa_qvm_bot_movement_target_set(const qa_qvm_bot_movement_target_ref *ref, qa_vec3 value);

typedef struct qa_qvm_bot_entity_state_ref { qa_qvmx_view view; } qa_qvm_bot_entity_state_ref;
qa_qvm_bot_entity_state_ref qa_qvm_borrow_bot_entity_state(qa_qvmx_view view);
qa_qvm_bot_entity_update qa_qvm_bot_entity_state_read_ref(const qa_qvm_bot_entity_state_ref *ref);
qa_qvm_bot_entity_update qa_qvm_read_bot_entity_state(qa_qvmx_view view);

void qa_qvm_write_aas_entity_info(qa_qvmx_view view, const qa_qvm_aas_entity_info *info);

#endif
