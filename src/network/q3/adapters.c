/* Port of src/network/q3/adapters.ts. GPL-2.0-or-later. */
#include "qa/network/q3/adapters.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
const qa_q3_protocol_identity qa_q3_protocol = { "q3", 68, 0, 0 };
qa_q3_protocol_identity qa_q3_require_protocol(qa_protocol_identity protocol) {
  if (!protocol.kind || strcmp(protocol.kind, "q3") != 0 || protocol.version != 68)
    qa_range_error("Quake 3 source codec requires protocol 68");
  qa_q3_protocol_identity r = protocol;
  return r;
}
static const char *trajectory_kind(int32_t type) {
  switch (type) {
    case 0: return "stationary"; case 1: return "interpolate"; case 2: return "linear";
    case 3: return "linear-stop"; case 4: return "sine"; case 5: return "gravity";
    default: qa_range_error("Raw Q3 trajectory tag %d needs a mod-specific presentation binding", type);
  }
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
static qa_q3_trajectory canonical_trajectory(const qa_q3_wire_trajectory *v) {
  qa_q3_trajectory r = { trajectory_kind(v->type), v->time, v->duration, v->base, v->delta };
  return r;
}
static qa_q3_wire_trajectory wire_trajectory(const qa_q3_trajectory *v) {
  qa_q3_wire_trajectory r;
  r.type = trajectory_type(v->kind); r.time = (int32_t)v->time_milliseconds; r.duration = (int32_t)v->duration_milliseconds;
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
  r.server_time = (int32_t)value->server_time_milliseconds;
  r.angles[0] = (int32_t)value->angle_words.values[0]; r.angles[1] = (int32_t)value->angle_words.values[1]; r.angles[2] = (int32_t)value->angle_words.values[2];
  r.buttons = (int32_t)value->buttons; r.weapon = (int32_t)value->weapon;
  r.forwardmove = (int32_t)value->forward_move; r.rightmove = (int32_t)value->right_move; r.upmove = (int32_t)value->up_move;
  return r;
}
qa_q3_entity_state qa_q3_to_entity_state(const qa_q3_entity_state_record *v) {
  qa_q3_entity_state r;
  r.number = v->number; r.type = v->e_type; r.flags = v->e_flags;
  r.position = canonical_trajectory(&v->pos); r.angular_position = canonical_trajectory(&v->apos);
  r.time_milliseconds = v->time; r.time2_milliseconds = v->time2;
  r.origin = v->origin; r.origin2 = v->origin2; r.angles = v->angles; r.angles2 = v->angles2;
  r.other_entity_number = v->other_entity_num; r.other_entity_number2 = v->other_entity_num2;
  r.ground_entity_number = v->ground_entity_num; r.constant_light = v->constant_light;
  r.loop_sound = v->loop_sound; r.model_index = v->modelindex; r.model_index2 = v->modelindex2;
  r.client_number = v->client_num; r.frame = v->frame; r.solid = v->solid; r.event = v->event;
  r.event_parameter = v->event_parm; r.powerups = v->powerups; r.weapon = v->weapon;
  r.legs_animation = v->legs_anim; r.torso_animation = v->torso_anim; r.generic1 = v->generic1;
  return r;
}
void qa_q3_from_entity_state(const qa_q3_entity_state *v, qa_q3_entity_state_record *out) {
  qa_q3_entity_state_record_init(out, 0);
  out->number = (int32_t)v->number; out->e_type = (int32_t)v->type; out->e_flags = (int32_t)v->flags;
  out->pos = wire_trajectory(&v->position); out->apos = wire_trajectory(&v->angular_position);
  out->time = (int32_t)v->time_milliseconds; out->time2 = (int32_t)v->time2_milliseconds;
  out->origin = v->origin; out->origin2 = v->origin2; out->angles = v->angles; out->angles2 = v->angles2;
  out->other_entity_num = (int32_t)v->other_entity_number; out->other_entity_num2 = (int32_t)v->other_entity_number2;
  out->ground_entity_num = (int32_t)v->ground_entity_number; out->constant_light = (int32_t)v->constant_light;
  out->loop_sound = (int32_t)v->loop_sound; out->modelindex = (int32_t)v->model_index; out->modelindex2 = (int32_t)v->model_index2;
  out->client_num = (int32_t)v->client_number; out->frame = (int32_t)v->frame; out->solid = (int32_t)v->solid;
  out->event = (int32_t)v->event; out->event_parm = (int32_t)v->event_parameter; out->powerups = (int32_t)v->powerups;
  out->weapon = (int32_t)v->weapon; out->legs_anim = (int32_t)v->legs_animation; out->torso_anim = (int32_t)v->torso_animation;
  out->generic1 = (int32_t)v->generic1;
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
  for (size_t i = 0; i < count; i++) target[i] = (int32_t)values[i];
}
void qa_q3_from_player_state(const qa_q3_player_state *v, qa_q3_product product, qa_q3_player_state_record *out) {
  qa_q3_player_state_record_init(out, product, (int32_t)v->movement_type, (int32_t)v->weapon, (int32_t)v->weapon_state);
  out->command_time = (int32_t)v->command_time_milliseconds; out->bob_cycle = (int32_t)v->bob_cycle;
  out->pm_flags = (int32_t)v->movement_flags; out->pm_time = (int32_t)v->movement_time_milliseconds;
  out->origin = v->origin; out->velocity = v->velocity;
  out->weapon_time = (int32_t)v->weapon_time_milliseconds; out->gravity = (int32_t)v->gravity; out->speed = (int32_t)v->speed;
  out->delta_angles.x = v->delta_angle_words.values[0]; out->delta_angles.y = v->delta_angle_words.values[1]; out->delta_angles.z = v->delta_angle_words.values[2];
  out->ground_entity_num = (int32_t)v->ground_entity_number; out->legs_timer = (int32_t)v->legs_timer_milliseconds;
  out->legs_anim = (int32_t)v->legs_animation; out->torso_timer = (int32_t)v->torso_timer_milliseconds;
  out->torso_anim = (int32_t)v->torso_animation; out->movement_dir = (int32_t)v->movement_direction;
  out->grapple_point = v->grapple_point; out->e_flags = (int32_t)v->flags; out->event_sequence = (int32_t)v->event_sequence;
  assign_slots(out->events, 2, v->events, 2);
  assign_slots(out->event_parms, 2, v->event_parameters, 2);
  out->external_event = (int32_t)v->external_event; out->external_event_parm = (int32_t)v->external_event_parameter;
  out->external_event_time = (int32_t)v->external_event_time_milliseconds; out->client_num = (int32_t)v->client_number;
  out->viewangles = v->view_angles; out->viewheight = (int32_t)v->view_height;
  out->damage_event = (int32_t)v->damage_event; out->damage_yaw = (int32_t)v->damage_yaw;
  out->damage_pitch = (int32_t)v->damage_pitch; out->damage_count = (int32_t)v->damage_count;
  assign_slots(out->stats, 16, v->stats.values, v->stats.count);
  assign_slots(out->persistant, 16, v->persistent.values, v->persistent.count);
  assign_slots(out->powerups, 16, v->powerups.values, v->powerups.count);
  assign_slots(out->ammo, 16, v->ammo.values, v->ammo.count);
  out->generic1 = (int32_t)v->generic1; out->loop_sound = (int32_t)v->loop_sound; out->jumppad_ent = (int32_t)v->jump_pad_entity;
  out->ping = (int32_t)v->ping_milliseconds; out->pmove_framecount = (int32_t)v->movement_frame_count;
  out->jumppad_frame = (int32_t)v->jump_pad_frame; out->entity_event_sequence = (int32_t)v->entity_event_sequence;
}
bool qa_q3_to_snapshot(const qa_q3_wire_snapshot *value, double ping_milliseconds, double server_command_count, qa_q3_adapted_snapshot *out) {
  memset(out, 0, sizeof *out);
  out->entities = malloc((value->entity_count ? value->entity_count : 1) * sizeof *out->entities);
  if (!out->entities) return false;
  for (size_t i = 0; i < value->entity_count; i++) out->entities[i] = qa_q3_to_entity_state(&value->entities[i]);
  qa_q3_to_player_state(&value->player_state, &out->player);
  memcpy(out->area_mask, value->area_mask, value->area_mask_len);
  qa_q3_snapshot *r = &out->snapshot;
  r->protocol = qa_q3_protocol;
  r->flags = (double)value->flags; r->ping_milliseconds = ping_milliseconds;
  r->server_time_milliseconds = (double)value->server_time;
  r->area_mask.bytes = out->area_mask; r->area_mask.length = value->area_mask_len;
  r->player = out->player.state;
  /* rebind slot views to out->player storage (struct copy kept pointers valid) */
  r->entities = out->entities; r->entity_count = value->entity_count;
  r->server_command_count = server_command_count;
  r->server_command_sequence = (double)value->server_command_number;
  return true;
}
void qa_q3_adapted_snapshot_destroy(qa_q3_adapted_snapshot *s) { free(s->entities); s->entities = NULL; }
