/* Port of src/compat/qvm/entity-record.ts. The TS borrowed
 * object's getters/setters become qa_qvm_entity_state_get/set accessors
 * over a live view; copy() owns its bytes. qa_qvmx_event etc. mirror
 * legacy-presentation.ts (qvm-b slice). GPL-2.0-or-later. */
#ifndef QA_COMPAT_QVM_ENTITY_RECORD_H
#define QA_COMPAT_QVM_ENTITY_RECORD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/compat/qvm/interpreter.h"
#include "qa/contracts/execution.h"
#include "qa/contracts/math.h"

#define QA_QVM_ENTITY_STATE_BYTES 208

typedef struct qa_qvm_trajectory { double type, time, duration; qa_vec3 base, delta; } qa_qvm_trajectory;

typedef struct qa_qvm_entity_state_fields {
  double number;
  double e_type;
  double e_flags;
  qa_qvm_trajectory pos;
  qa_qvm_trajectory apos;
  double time;
  double time2;
  qa_vec3 origin;
  qa_vec3 origin2;
  qa_vec3 angles;
  qa_vec3 angles2;
  double other_entity_num;
  double other_entity_num2;
  double ground_entity_num;
  double constant_light;
  double loop_sound;
  double modelindex;
  double modelindex2;
  double client_num;
  double frame;
  double solid;
  double event;
  double event_parm;
  double powerups;
  double weapon;
  double legs_anim;
  double torso_anim;
  double generic1;
} qa_qvm_entity_state_fields;

typedef struct qa_qvm_entity_state {
  qa_qvmx_view view;
  qa_qvm_abi_profile profile;
  bool source;
  uint8_t *owned_bytes; /* non-NULL after copy/read */
} qa_qvm_entity_state;

double qa_qvm_entity_state_bytes_for(qa_qvm_abi_profile profile);
/* profile NULL means "q3-modern". */
qa_qvm_entity_state qa_qvm_borrow_entity_state(qa_qvmx_view view, qa_qvm_abi_profile profile);
qa_qvm_entity_state qa_qvm_read_entity_state(qa_qvmx_view view);
qa_qvm_entity_state qa_qvm_read_source_entity_state(qa_qvmx_view view, qa_qvm_abi_profile profile);
void qa_qvm_write_source_entity_state(qa_qvmx_view view, const qa_qvm_entity_state_fields *state, qa_qvm_abi_profile profile);
void qa_qvm_write_entity_state(qa_qvmx_view view, const qa_qvm_entity_state_fields *state, qa_qvm_abi_profile profile);
qa_qvm_entity_state qa_qvm_entity_state_copy(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_copy_from(qa_qvm_entity_state *state, const qa_qvm_entity_state_fields *source);
void qa_qvm_entity_state_destroy(qa_qvm_entity_state *state);
void qa_qvm_get_entity_state_fields(const qa_qvm_entity_state *state, qa_qvm_entity_state_fields *out);

double qa_qvm_entity_state_get_number(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_number(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_e_type(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_e_type(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_e_flags(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_e_flags(qa_qvm_entity_state *state, double value);
qa_qvm_trajectory qa_qvm_entity_state_get_pos(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_pos(qa_qvm_entity_state *state, qa_qvm_trajectory value);
qa_qvm_trajectory qa_qvm_entity_state_get_apos(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_apos(qa_qvm_entity_state *state, qa_qvm_trajectory value);
double qa_qvm_entity_state_get_time(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_time(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_time2(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_time2(qa_qvm_entity_state *state, double value);
qa_vec3 qa_qvm_entity_state_get_origin(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_origin(qa_qvm_entity_state *state, qa_vec3 value);
qa_vec3 qa_qvm_entity_state_get_origin2(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_origin2(qa_qvm_entity_state *state, qa_vec3 value);
qa_vec3 qa_qvm_entity_state_get_angles(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_angles(qa_qvm_entity_state *state, qa_vec3 value);
qa_vec3 qa_qvm_entity_state_get_angles2(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_angles2(qa_qvm_entity_state *state, qa_vec3 value);
double qa_qvm_entity_state_get_other_entity_num(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_other_entity_num(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_other_entity_num2(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_other_entity_num2(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_ground_entity_num(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_ground_entity_num(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_constant_light(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_constant_light(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_loop_sound(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_loop_sound(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_modelindex(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_modelindex(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_modelindex2(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_modelindex2(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_client_num(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_client_num(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_frame(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_frame(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_solid(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_solid(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_event(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_event(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_event_parm(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_event_parm(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_powerups(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_powerups(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_weapon(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_weapon(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_legs_anim(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_legs_anim(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_torso_anim(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_torso_anim(qa_qvm_entity_state *state, double value);
double qa_qvm_entity_state_get_generic1(const qa_qvm_entity_state *state);
void qa_qvm_entity_state_set_generic1(qa_qvm_entity_state *state, double value);

/* Mirrors of legacy-presentation.ts (qvm-b slice), shared by this slice. */
int32_t qa_qvmx_event(double value, qa_qvm_abi_profile profile, bool reverse);
int32_t qa_qvmx_entity_type(double value, qa_qvm_abi_profile profile, bool reverse);
int32_t qa_qvmx_powerup_bits(double bits, qa_qvm_abi_profile profile);
int32_t qa_qvmx_configstring(double index, qa_qvm_abi_profile profile);

#endif
