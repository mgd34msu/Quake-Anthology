/* Port of src/network/q3/adapters.ts. GPL-2.0-or-later. */
#include "qa/network/q3/adapters.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/network/common/js_number.h"
#include <stdlib.h>
#include <string.h>
const qa_q3_protocol_identity qa_q3_protocol = { "q3", 68, 0, 0 };
qa_q3_protocol_identity qa_q3_require_protocol(qa_protocol_identity protocol) {
  if (!protocol.kind || strcmp(protocol.kind, "q3") != 0 || protocol.version != 68)
    qa_range_error("Quake 3 source codec requires protocol 68");
  qa_q3_protocol_identity r = protocol;
  return r;
}
static int32_t trajectory_type(const char *kind) {
  if (!kind) qa_range_error("Raw Q3 trajectory tag needs a mod-specific presentation binding");
  if (strcmp(kind, "stationary") == 0) return 0;
  if (strcmp(kind, "interpolate") == 0) return 1;
  if (strcmp(kind, "linear") == 0) return 2;
  if (strcmp(kind, "linear-stop") == 0) return 3;
  if (strcmp(kind, "sine") == 0) return 4;
  if (strcmp(kind, "gravity") == 0) return 5;
  qa_range_error("Unknown Q3 trajectory kind %s", kind);
}
static qa_q3_wire_trajectory wire_trajectory(const qa_q3_trajectory *v) {
  qa_q3_wire_trajectory r;
  r.type = trajectory_type(v->kind); r.time = v->time_milliseconds; r.duration = v->duration_milliseconds;
  r.base = v->base; r.delta = v->delta;
  return r;
}
qa_q3_user_command qa_q3_to_user_command(const qa_q3_wire_user_command *value) {
  qa_q3_user_command r;
  r.server_time_milliseconds = value->server_time;
  r.angle_words.values[0] = value->angles[0]; r.angle_words.values[1] = value->angles[1]; r.angle_words.values[2] = value->angles[2];
  r.buttons = value->buttons; r.weapon = value->weapon;
  r.forward_move = value->forwardmove; r.right_move = value->rightmove; r.up_move = value->upmove;
  return r;
}
qa_q3_wire_user_command qa_q3_from_user_command(const qa_q3_user_command *value) {
  qa_q3_wire_user_command r;
  r.server_time = qa_int32(value->server_time_milliseconds);
  r.angles[0] = qa_int32(value->angle_words.values[0]); r.angles[1] = qa_int32(value->angle_words.values[1]); r.angles[2] = qa_int32(value->angle_words.values[2]);
  r.buttons = qa_int32(value->buttons); r.weapon = qa_int32(value->weapon);
  r.forwardmove = qa_int32(value->forward_move); r.rightmove = qa_int32(value->right_move); r.upmove = qa_int32(value->up_move);
  return r;
}
void qa_q3_from_entity_state(const qa_q3_entity_state *v, qa_q3_entity_state_record *out) {
  qa_q3_entity_state_record_init(out, 0);
  out->number = v->number; out->e_type = v->type; out->e_flags = v->flags;
  out->time = v->time_milliseconds; out->time2 = v->time2_milliseconds;
  out->other_entity_num = v->other_entity_number; out->other_entity_num2 = v->other_entity_number2;
  out->ground_entity_num = v->ground_entity_number; out->constant_light = v->constant_light;
  out->loop_sound = v->loop_sound; out->modelindex = v->model_index; out->modelindex2 = v->model_index2;
  out->client_num = v->client_number; out->frame = v->frame; out->solid = v->solid;
  out->event = v->event; out->event_parm = v->event_parameter; out->powerups = v->powerups;
  out->weapon = v->weapon; out->legs_anim = v->legs_animation; out->torso_anim = v->torso_animation;
  out->generic1 = v->generic1;
  out->pos = wire_trajectory(&v->position); out->apos = wire_trajectory(&v->angular_position);
  out->origin = v->origin; out->origin2 = v->origin2; out->angles = v->angles; out->angles2 = v->angles2;
}
void qa_q3_to_player_state(const qa_q3_player_state_record *v, qa_q3_adapted_player_state *out) {
  qa_q3_player_state *r = &out->state;
  memset(out, 0, sizeof *out);
  r->command_time_milliseconds = v->command_time; r->movement_type = v->pm_type; r->bob_cycle = v->bob_cycle;
  r->movement_flags = v->pm_flags; r->movement_time_milliseconds = v->pm_time;
  r->origin = v->origin; r->velocity = v->velocity;
  r->weapon_time_milliseconds = v->weapon_time; r->gravity = v->gravity; r->speed = v->speed;
  r->delta_angle_words.values[0] = v->delta_angles.x; r->delta_angle_words.values[1] = v->delta_angles.y; r->delta_angle_words.values[2] = v->delta_angles.z;
  r->ground_entity_number = v->ground_entity_num; r->legs_timer_milliseconds = v->legs_timer; r->legs_animation = v->legs_anim;
  r->torso_timer_milliseconds = v->torso_timer; r->torso_animation = v->torso_anim; r->movement_direction = v->movement_dir;
  r->grapple_point = v->grapple_point; r->flags = v->e_flags; r->event_sequence = v->event_sequence;
  r->events[0] = v->events[0]; r->events[1] = v->events[1];
  r->event_parameters[0] = v->event_parms[0]; r->event_parameters[1] = v->event_parms[1];
  r->external_event = v->external_event; r->external_event_parameter = v->external_event_parm;
  r->external_event_time_milliseconds = v->external_event_time; r->client_number = v->client_num;
  r->weapon = v->weapon; r->weapon_state = v->weapon_state; r->view_angles = v->viewangles;
  r->view_height = v->viewheight; r->damage_event = v->damage_event; r->damage_yaw = v->damage_yaw;
  r->damage_pitch = v->damage_pitch; r->damage_count = v->damage_count;
  for (int i = 0; i < 16; i++) {
    out->stats[i] = v->stats[i]; out->persistent[i] = v->persistant[i];
    out->powerups[i] = v->powerups[i]; out->ammo[i] = v->ammo[i];
  }
  r->stats.values = out->stats; r->stats.count = 16;
  r->persistent.values = out->persistent; r->persistent.count = 16;
  r->powerups.values = out->powerups; r->powerups.count = 16;
  r->ammo.values = out->ammo; r->ammo.count = 16;
  r->generic1 = v->generic1; r->loop_sound = v->loop_sound; r->jump_pad_entity = v->jumppad_ent;
  r->ping_milliseconds = v->ping; r->movement_frame_count = v->pmove_framecount;
  r->jump_pad_frame = v->jumppad_frame; r->entity_event_sequence = v->entity_event_sequence;
}
static void assign_slots(int32_t *target, int64_t target_len, const double *values, size_t count) {
  if ((int64_t)count != target_len)
    qa_range_error("Q3 wire needs %lld source slots, received %zu", (long long)target_len, count);
  for (size_t i = 0; i < count; i++) target[i] = qa_int32(values[i]);
}
void qa_q3_from_player_state(const qa_q3_player_state *v, qa_q3_product product, qa_q3_player_state_record *out) {
  qa_q3_player_state_record_init(out, product, qa_int32(v->movement_type), qa_int32(v->weapon), qa_int32(v->weapon_state));
  out->command_time = qa_int32(v->command_time_milliseconds); out->bob_cycle = qa_int32(v->bob_cycle);
  out->pm_flags = qa_int32(v->movement_flags); out->pm_time = qa_int32(v->movement_time_milliseconds);
  out->origin = v->origin; out->velocity = v->velocity;
  out->weapon_time = qa_int32(v->weapon_time_milliseconds); out->gravity = qa_int32(v->gravity); out->speed = qa_int32(v->speed);
  out->delta_angles.x = v->delta_angle_words.values[0]; out->delta_angles.y = v->delta_angle_words.values[1]; out->delta_angles.z = v->delta_angle_words.values[2];
  out->ground_entity_num = qa_int32(v->ground_entity_number); out->legs_timer = qa_int32(v->legs_timer_milliseconds);
  out->legs_anim = qa_int32(v->legs_animation); out->torso_timer = qa_int32(v->torso_timer_milliseconds);
  out->torso_anim = qa_int32(v->torso_animation); out->movement_dir = qa_int32(v->movement_direction);
  out->grapple_point = v->grapple_point; out->e_flags = qa_int32(v->flags); out->event_sequence = qa_int32(v->event_sequence);
  assign_slots(out->events, 2, v->events, 2);
  assign_slots(out->event_parms, 2, v->event_parameters, 2);
  out->external_event = qa_int32(v->external_event); out->external_event_parm = qa_int32(v->external_event_parameter);
  out->external_event_time = qa_int32(v->external_event_time_milliseconds); out->client_num = qa_int32(v->client_number);
  out->viewangles = v->view_angles; out->viewheight = qa_int32(v->view_height);
  out->damage_event = qa_int32(v->damage_event); out->damage_yaw = qa_int32(v->damage_yaw);
  out->damage_pitch = qa_int32(v->damage_pitch); out->damage_count = qa_int32(v->damage_count);
  assign_slots(out->stats, 16, v->stats.values, v->stats.count);
  assign_slots(out->persistant, 16, v->persistent.values, v->persistent.count);
  assign_slots(out->powerups, 16, v->powerups.values, v->powerups.count);
  assign_slots(out->ammo, 16, v->ammo.values, v->ammo.count);
  out->generic1 = qa_int32(v->generic1); out->loop_sound = qa_int32(v->loop_sound); out->jumppad_ent = qa_int32(v->jump_pad_entity);
  out->ping = qa_int32(v->ping_milliseconds); out->pmove_framecount = qa_int32(v->movement_frame_count);
  out->jumppad_frame = qa_int32(v->jump_pad_frame); out->entity_event_sequence = qa_int32(v->entity_event_sequence);
}
